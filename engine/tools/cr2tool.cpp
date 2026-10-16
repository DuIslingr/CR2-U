// cr2tool: CR2-U's command-line checks on your own copy of the game's data.
//
//   cr2tool check <archive.mvgl | .obb>              every file decodes to its size
//   cr2tool scripts <archive> [name]                 every script (script\*.nut) loads in the
//                                                    Squirrel 2.2.4 VM and its main runs; the
//                                                    native stubs it calls
//   cr2tool run <archive> <script> [function]        loads one script, runs main, then its
//                                                    function (default: the script's name) with
//                                                    every native a stub; the stubs it reached
//   cr2tool models <archive>                         every mesh file parses; the textures its
//                                                    materials name are in the archive
//   cr2tool compare-models <archive> <archive>       the mesh files both have, decoded: the
//                                                    largest differences in positions, uvs, normals
//   cr2tool animations <archive>                     skeletons: base pose * inverse bind = identity
//                                                    for the skinned joints; every animation:
//                                                    unit rotations, frame set boundaries continuous
//   cr2tool pose <archive> <figure> <anim> <frame>   the animation's pose at a frame (joint: R, T, S)
//   cr2tool images <archive> [pattern [out dir]]     every texture decodes (formats counted);
//                                                    with a folder, those matching as .tga
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "assets/geom.hpp"
#include "assets/image.hpp"
#include "assets/skeleton.hpp"
#include "assets/mvgl.hpp"
#include "script/vm.hpp"

namespace {

int usage() {
    std::fprintf(stderr,
                 "cr2tool check <archive>\n"
                 "cr2tool scripts <archive> [name]\n"
                 "cr2tool run <archive> <script> [function]\n"
                 "cr2tool images <archive> [pattern [out dir]]\n"
                 "cr2tool models <archive>\n"
                 "cr2tool compare-models <archive> <archive>\n"
                 "cr2tool animations <archive>\n");
    return 2;
}

bool openArchive(cr2::MvglArchive& a, const char* path) {
    std::string err;
    if (!a.open(path, &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return false;
    }
    return true;
}

int check(const char* path) {
    cr2::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    size_t ok = 0, bad = 0;
    std::vector<uint8_t> data;
    for (const auto& e : a.entries()) {
        std::string err;
        if (a.read(e, data, &err) && data.size() == e.size) ++ok;
        else ++bad, std::printf("bad %s: %s\n", e.name.c_str(), err.c_str());
    }
    std::printf("%s: %zu files decode, %zu bad\n", path, ok, bad);
    return bad ? 1 : 0;
}

int scripts(const char* path, const char* only) {
    cr2::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    size_t loaded = 0, ran = 0, total = 0;
    std::map<std::string, int> stubs;
    std::vector<uint8_t> data;
    for (const auto& e : a.entries()) {
        if (e.name.size() < 4 || e.name.compare(e.name.size() - 4, 4, ".nut") != 0) continue;
        if (only && e.name.find(only) == std::string::npos) continue;
        ++total;
        if (!a.read(e, data)) {
            std::printf("%s: unreadable\n", e.name.c_str());
            continue;
        }
        cr2::ScriptVM vm; // (one VM per script: each defines the same helper library)
        if (!vm.load(data, e.name)) {
            std::printf("%s: %s\n", e.name.c_str(), vm.error().c_str());
            continue;
        }
        ++loaded;
        if (!vm.runMain()) {
            std::printf("%s: main failed: %s\n", e.name.c_str(), vm.error().c_str());
            continue;
        }
        ++ran;
        for (const auto& [k, n] : vm.stubCalls()) stubs[k] += n;
    }
    std::printf("%zu scripts: %zu load, %zu run main\n", total, loaded, ran);
    for (const auto& [k, n] : stubs) std::printf("  stub called in main: %s x%d\n", k.c_str(), n);
    return loaded == total && ran == total ? 0 : 1;
}

int run(const char* path, const char* script, const char* function) {
    cr2::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    std::string name = script;
    if (name.find('.') == std::string::npos) name += ".nut";
    if (name.find('\\') == std::string::npos) name = "script\\" + name;
    std::vector<uint8_t> data;
    std::string err;
    if (!a.read(name, data, &err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    cr2::ScriptVM vm;
    if (!vm.load(data, name) || !vm.runMain()) return std::fprintf(stderr, "%s\n", vm.error().c_str()), 1;
    std::string fn = function ? function : name.substr(name.rfind('\\') + 1, name.rfind('.') - name.rfind('\\') - 1);
    if (const std::string f = vm.findFunction(fn); !f.empty()) fn = f;
    const bool ok = vm.call(fn);
    std::printf("%s.%s: %s\n", name.c_str(), fn.c_str(), ok ? "returned" : ("failed: " + vm.error()).c_str());
    for (const auto& [k, n] : vm.stubCalls()) std::printf("  %s x%d\n", k.c_str(), n);
    return ok ? 0 : 1;
}

bool writeTga(const std::string& path, const cr2::Image& img) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    uint8_t h[18] = {};
    h[2] = 2; // uncompressed true-colour
    h[12] = uint8_t(img.width), h[13] = uint8_t(img.width >> 8), h[14] = uint8_t(img.height), h[15] = uint8_t(img.height >> 8);
    h[16] = 32;
    h[17] = uint8_t(8 | (img.bottomUp ? 0 : 0x20)); // 8 alpha bits; origin top-left unless stored bottom-up
    std::fwrite(h, 1, sizeof h, f);
    std::vector<uint8_t> row(size_t(img.width) * 4);
    for (int y = 0; y < img.height; ++y) {
        const uint8_t* s = &img.rgba[size_t(y) * img.width * 4];
        for (int x = 0; x < img.width; ++x) row[x * 4] = s[x * 4 + 2], row[x * 4 + 1] = s[x * 4 + 1], row[x * 4 + 2] = s[x * 4], row[x * 4 + 3] = s[x * 4 + 3];
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    return true;
}

int images(const char* path, const char* pattern, const char* outDir) {
    cr2::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    std::map<std::string, int> formats;
    size_t ok = 0, bad = 0, written = 0;
    std::vector<uint8_t> data;
    for (const auto& e : a.entries()) {
        if (e.name.size() < 4 || e.name.compare(e.name.size() - 4, 4, ".img") != 0) continue;
        if (pattern && e.name.find(pattern) == std::string::npos) continue;
        cr2::Image img;
        std::string err;
        if (!a.read(e, data, &err) || !cr2::decodeImg(data, img, &err)) {
            ++bad;
            std::printf("bad %s: %s\n", e.name.c_str(), err.c_str());
            continue;
        }
        ++ok;
        ++formats[img.format];
        if (outDir) {
            std::string n = e.name.substr(e.name.rfind('\\') + 1);
            n = n.substr(0, n.size() - 4);
            written += writeTga(std::string(outDir) + "/" + n + ".tga", img);
        }
    }
    std::printf("%zu textures decode, %zu bad%s\n", ok, bad, outDir ? (", " + std::to_string(written) + " written").c_str() : "");
    for (const auto& [k, n] : formats) std::printf("  %-20s %d\n", k.c_str(), n);
    return bad ? 1 : 0;
}

int models(const char* path) {
    cr2::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    size_t ok = 0, bad = 0, meshes = 0, vertices = 0, triangles = 0, textures = 0, missing = 0;
    std::map<std::string, int> missingNames;
    std::vector<uint8_t> data;
    for (const auto& e : a.entries()) {
        if (e.name.size() < 5 || e.name.compare(e.name.size() - 5, 5, ".geom") != 0) continue;
        cr2::Geom g;
        std::string err;
        if (!a.read(e, data, &err) || !cr2::parseGeom(data, g, &err)) {
            ++bad;
            std::printf("bad %s: %s\n", e.name.c_str(), err.c_str());
            continue;
        }
        ++ok;
        for (const auto& m : g.meshes) meshes++, vertices += m.vertexCount, triangles += m.triangles().size() / 3;
        for (const auto& mat : g.materials)
            for (const auto& [id, name] : mat.textures) {
                ++textures;
                if (!a.contains("images\\" + name + ".img")) ++missing, ++missingNames[name];
            }
    }
    std::printf("%zu mesh files parse, %zu bad: %zu meshes, %zu vertices, %zu triangles; %zu texture references, %zu not in the archive\n",
                ok, bad, meshes, vertices, triangles, textures, missing);
    int shown = 0;
    for (const auto& [n, k] : missingNames)
        if (shown++ < 12) std::printf("  missing %s x%d\n", n.c_str(), k);
    return bad ? 1 : 0;
}

int compareModels(const char* pa, const char* pb) {
    cr2::MvglArchive a, b;
    if (!openArchive(a, pa) || !openArchive(b, pb)) return 1;
    size_t files = 0, alike = 0, shapeDiffers = 0;
    double dPos = 0, dUv = 0, dNorm = 0;
    std::string worstPos, worstUv, worstNorm;
    std::vector<uint8_t> da, db;
    for (const auto& e : a.entries()) {
        if (e.name.size() < 5 || e.name.compare(e.name.size() - 5, 5, ".geom") != 0 || !b.contains(e.name)) continue;
        cr2::Geom ga, gb;
        if (!a.read(e, da) || !b.read(e.name, db) || !cr2::parseGeom(da, ga) || !cr2::parseGeom(db, gb)) continue;
        ++files;
        bool same = ga.meshes.size() == gb.meshes.size();
        for (size_t i = 0; same && i < ga.meshes.size(); ++i) {
            const auto &ma = ga.meshes[i], &mb = gb.meshes[i];
            if (ma.vertexCount != mb.vertexCount || ma.strip != mb.strip) { same = false; break; }
            auto cmp = [](const std::vector<float>& x, const std::vector<float>& y, double& worst, std::string& at, const std::string& name) {
                if (x.size() != y.size()) return;
                for (size_t k = 0; k < x.size(); ++k)
                    if (std::abs(double(x[k]) - y[k]) > worst) worst = std::abs(double(x[k]) - y[k]), at = name;
            };
            cmp(ma.position, mb.position, dPos, worstPos, e.name);
            cmp(ma.uv, mb.uv, dUv, worstUv, e.name);
            cmp(ma.normal, mb.normal, dNorm, worstNorm, e.name);
        }
        same ? ++alike : ++shapeDiffers;
    }
    std::printf("%zu mesh files in both: %zu with the same meshes, vertex counts and strips, %zu not\n", files, alike, shapeDiffers);
    std::printf("  largest differences: position %.6f (%s), uv %.6f (%s), normal %.6f (%s)\n", dPos, worstPos.c_str(), dUv,
                worstUv.c_str(), dNorm, worstNorm.c_str());
    return 0;
}

int animations(const char* path) {
    cr2::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    std::vector<uint8_t> data;
    // skeletons against their figures' inverse binds
    size_t figures = 0, skinned = 0, bound = 0;
    std::map<std::string, cr2::Skeleton> skeletons;
    for (const auto& e : a.entries()) {
        if (e.name.size() < 5 || e.name.compare(e.name.size() - 5, 5, ".skel") != 0) continue;
        cr2::Skeleton sk;
        std::string err;
        if (!a.read(e, data) || !cr2::parseSkeleton(data, sk, &err)) {
            std::printf("bad %s: %s\n", e.name.c_str(), err.c_str());
            continue;
        }
        const std::string base = e.name.substr(0, e.name.size() - 5);
        cr2::Geom g;
        if (a.read(base + ".geom", data) && cr2::parseGeom(data, g) && g.inverseBind.size() == sk.basePose.size()) {
            ++figures;
            const auto world = sk.world(sk.basePose);
            std::vector<bool> used(sk.basePose.size(), false);
            for (const auto& m : g.meshes)
                for (uint32_t j : m.palette)
                    if (j < used.size()) used[j] = true;
            for (size_t j = 0; j < used.size(); ++j) {
                if (!used[j]) continue;
                const cr2::Mat34 m = world[j] * g.inverseBind[j];
                float err = 0;
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 4; ++c) err = std::max(err, std::fabs(m.m[r * 4 + c] - (r == c ? 1.0f : 0.0f)));
                ++skinned;
                bound += err <= 1e-3f;
            }
        }
        skeletons[base] = std::move(sk);
    }
    std::printf("%zu skeletons; %zu figures, %zu skinned joints: %zu with base pose * inverse bind = identity (%.1f%%)\n",
                skeletons.size(), figures, skinned, bound, 100.0 * double(bound) / double(std::max<size_t>(skinned, 1)));
    // animations
    size_t anims = 0, bad = 0, unitBad = 0, boundaries = 0;
    double worst = 0;
    std::string worstAt;
    for (const auto& e : a.entries()) {
        if (e.name.size() < 5 || e.name.compare(e.name.size() - 5, 5, ".anim") != 0) continue;
        cr2::Animation an;
        std::string err;
        if (!a.read(e, data) || !an.parse(data, &err)) {
            ++bad;
            std::printf("bad %s: %s\n", e.name.c_str(), err.c_str());
            continue;
        }
        ++anims;
        std::vector<cr2::JointTransform> base(size_t(an.joints()));
        for (size_t cut = e.name.size() - 5; cut > 0; --cut) { // the figure: the name before the motion's
            const auto it = skeletons.find(e.name.substr(0, cut));
            if (it != skeletons.end() && it->second.basePose.size() == base.size()) { base = it->second.basePose; break; }
        }
        // driven joints: the ones some rotation channel sets (base poses of UI skeletons aren't unit)
        std::vector<cr2::JointTransform> probe = base;
        for (auto& t : probe) t.rotation[0] = t.rotation[1] = t.rotation[2] = t.rotation[3] = 0;
        an.evaluate(0, probe);
        for (int fr = 0; fr < an.frames(); fr += std::max(1, an.frames() / 16)) {
            std::vector<cr2::JointTransform> p = base;
            an.evaluate(float(fr), p);
            for (size_t j = 0; j < p.size(); ++j) {
                const float* q = probe[j].rotation;
                if (q[0] == 0 && q[1] == 0 && q[2] == 0 && q[3] == 0) continue;
                const float* r = p[j].rotation;
                if (std::fabs(std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2] + r[3] * r[3]) - 1) > 1e-3f) { ++unitBad; break; }
            }
        }
        // continuity across frame sets: just before a set's start and at it
        for (int fr = 1; fr < an.frames(); ++fr) {
            std::vector<cr2::JointTransform> p0 = base, p1 = base, pm = base;
            an.evaluate(float(fr) - 1e-4f, p0);
            an.evaluate(float(fr), p1);
            an.evaluate(float(fr) - 2e-4f, pm);
            // a jump: the step at the frame far bigger than the step just before it
            for (size_t j = 0; j < p0.size(); ++j)
                for (int c = 0; c < 3; ++c) {
                    const double step = std::fabs(double(p1[j].translation[c]) - p0[j].translation[c]);
                    const double before = std::fabs(double(p0[j].translation[c]) - pm[j].translation[c]);
                    const double rel = (step - 2 * before) / std::max(1.0, std::fabs(double(p1[j].translation[c])));
                    if (rel > worst) worst = rel, worstAt = e.name + " frame " + std::to_string(fr);
                }
            ++boundaries;
        }
    }
    std::printf("%zu animations parse, %zu bad; driven rotations off unit length in %zu sampled frames; "
                "%zu frames checked for jumps, the largest translation jump %.2g (%s)\n",
                anims, bad, unitBad, boundaries, worst, worstAt.c_str());
    return bad ? 1 : 0;
}

int pose(const char* path, const std::string& figure, const std::string& animName, float frame) {
    cr2::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    std::vector<uint8_t> data;
    cr2::Skeleton sk;
    cr2::Animation an;
    std::string err;
    if (!a.read(figure + ".skel", data, &err) || !cr2::parseSkeleton(data, sk, &err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    if (!a.read(animName + ".anim", data, &err) || !an.parse(data, &err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    std::vector<cr2::JointTransform> p = sk.basePose;
    an.evaluate(frame, p);
    for (size_t j = 0; j < p.size(); ++j)
        std::printf("%zu: R %.4f %.4f %.4f %.4f  T %.4f %.4f %.4f  S %.4f %.4f %.4f\n", j, p[j].rotation[0], p[j].rotation[1],
                    p[j].rotation[2], p[j].rotation[3], p[j].translation[0], p[j].translation[1], p[j].translation[2], p[j].scale[0],
                    p[j].scale[1], p[j].scale[2]);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    if (!std::strcmp(argv[1], "check")) return check(argv[2]);
    if (!std::strcmp(argv[1], "scripts")) return scripts(argv[2], argc > 3 ? argv[3] : nullptr);
    if (!std::strcmp(argv[1], "models")) return models(argv[2]);
    if (!std::strcmp(argv[1], "animations")) return animations(argv[2]);
    if (!std::strcmp(argv[1], "pose") && argc > 5) return pose(argv[2], argv[3], argv[4], float(std::atof(argv[5])));
    if (!std::strcmp(argv[1], "compare-models") && argc > 3) return compareModels(argv[2], argv[3]);
    if (!std::strcmp(argv[1], "images")) return images(argv[2], argc > 3 ? argv[3] : nullptr, argc > 4 ? argv[4] : nullptr);
    if (!std::strcmp(argv[1], "run") && argc > 3) return run(argv[2], argv[3], argc > 4 ? argv[4] : nullptr);
    return usage();
}
