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
//   cr2tool walk <archive> <executable> [map]       the field player (field/character.hpp) on every
//                                                    map's floors: dropped at each pop point (else
//                                                    defaultPos), run 4 s in 8 directions - on a
//                                                    floor throughout, never through a wall
//   cr2tool exits <archive> <executable>             every map's exits: the map each leads to (the
//                                                    pop point's first five letters) is there and
//                                                    has that point
//   cr2tool fieldscript <archive> <executable> [map] every map's entry (its dungeon script's
//                                                    evt000, a new game's state): runs or not, the
//                                                    gimmicks it shows, the stub natives it reached
//   cr2tool shaders <archive>                        the materials' shaders: the features their
//                                                    GLSL has, and where the key decoder (gfx/
//                                                    material.cpp) disagrees with it, per bit
#include <algorithm>
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
#include "field/character.hpp"
#include "field/fieldmap.hpp"
#include "field/fieldscript.hpp"
#include "gfx/material.hpp"
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
                 "cr2tool animations <archive>\n"
                 "cr2tool shaders <archive>\n"
                 "cr2tool walk <archive> <executable> [map]\n"
                 "cr2tool exits <archive> <executable>\n"
                 "cr2tool fieldscript <archive> <executable> [map]\n");
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
        ++formats[img.format + (img.bottomUp ? " (bottom-up)" : "")];
        if (outDir) {
            std::string n = e.name.substr(e.name.rfind('\\') + 1);
            n = n.substr(0, n.size() - 4);
            written += writeTga(std::string(outDir) + "/" + n + ".tga", img);
        }
    }
    std::printf("%zu textures decode, %zu bad%s\n", ok, bad, outDir ? (", " + std::to_string(written) + " written").c_str() : "");
    for (const auto& [k, n] : formats) std::printf("  %-32s %d\n", k.c_str(), n);
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

// The shaders' GLSL against the key decoder: per SHADE_* bit (and feature left out), what the
// source says (featuresFromGlsl) and what the key says (decodeShaderKey), for every key the
// materials name
int shaders(const char* path) {
    cr2::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    static const char* kShade[] = {"on", "texture", "normal map", "lit", "DirLamp01", "hemisphere", "CLUT", "specular",
                                   "spec vertex alpha", "obscure", "obscure spec", "tint", "tint alpha", "velvet", "fuzzy",
                                   "alpha test", "fog", "light map", "scroll", "offset", "spec map alpha"};
    static const char* kUnsupported[] = {"(point light)", "(overlay)", "(distortion)", "(uv1)", "(billboard)"};
    constexpr int kShadeBits = int(sizeof kShade / sizeof kShade[0]), kUnsupportedBits = 5;
    std::map<std::array<uint32_t, 4>, int> keys;
    size_t materials = 0, builtIn = 0, alphaNoRef = 0, extraClash = 0;
    std::vector<uint8_t> data;
    cr2::ShaderLibrary library(a);
    for (const auto& e : a.entries()) {
        if (e.name.size() < 5 || e.name.compare(e.name.size() - 5, 5, ".geom") != 0) continue;
        cr2::Geom g;
        if (!a.read(e, data) || !cr2::parseGeom(data, g)) continue;
        for (const auto& m : g.materials) {
            ++materials;
            const cr2::ShaderFeatures f = library.features(m.shaderKey);
            if (!f.shade) {
                ++builtIn;
                continue;
            }
            ++keys[m.shaderKey];
            uint32_t ref;
            if ((f.shade & SHADE_ALPHA_TEST) && !m.state(0x78, 0, ref)) ++alphaNoRef;
            if ((f.shade & SHADE_CLUT) && (f.shade & SHADE_LIGHT_MAP)) ++extraClash;
        }
    }
    size_t checked = 0, noSource = 0, wrongKeys = 0;
    size_t have[kShadeBits + kUnsupportedBits] = {}, wrong[kShadeBits + kUnsupportedBits] = {};
    for (const auto& [key, uses] : keys) {
        char name[64];
        std::snprintf(name, sizeof name, "shaders\\%08x_%08x_%08x_%08x", key[0], key[1], key[2], key[3]);
        std::vector<uint8_t> fpData, vpData;
        const bool read = a.read(std::string(name) + "_fp.shad", fpData) && a.read(std::string(name) + "_vp.shad", vpData);
        const std::string fp(fpData.begin(), fpData.end()), vp(vpData.begin(), vpData.end());
        if (!read || fp.find("gl_FragColor") == std::string::npos) { // (the Vita's: compiled programs)
            ++noSource;
            continue;
        }
        ++checked;
        const cr2::ShaderFeatures src = cr2::featuresFromGlsl(vp, fp), dec = cr2::decodeShaderKey(key);
        bool bad = false;
        for (int k = 0; k < kShadeBits + kUnsupportedBits; ++k) {
            const bool s = k < kShadeBits ? (src.shade >> k & 1) : (src.unsupported >> (k - kShadeBits) & 1);
            const bool d = k < kShadeBits ? (dec.shade >> k & 1) : (dec.unsupported >> (k - kShadeBits) & 1);
            have[k] += s;
            if (s != d) ++wrong[k], bad = true;
        }
        wrongKeys += bad;
    }
    std::printf("%zu materials: %zu with built-in Figure shaders (key 0), the rest %zu keys; %zu keys' GLSL read, "
                "%zu without (the Vita's are compiled: their keys decoded)\n",
                materials, builtIn, keys.size(), checked, noSource);
    std::printf("  the GLSL has:");
    for (int k = 1; k < kShadeBits + kUnsupportedBits; ++k)
        std::printf("%s %s %zu", k > 1 ? "," : "", k < kShadeBits ? kShade[k] : kUnsupported[k - kShadeBits], have[k]);
    std::printf("\n  the key decoder (Android / Vita layout) disagrees with the GLSL on %zu keys", wrongKeys);
    for (int k = 0; k < kShadeBits + kUnsupportedBits; ++k)
        if (wrong[k]) std::printf(" - %s %zu", k < kShadeBits ? kShade[k] : kUnsupported[k - kShadeBits], wrong[k]);
    std::printf("\n  alpha-tested materials without an alpha_func state (AlphaRef 0.5): %zu; CLUT + light map: %zu\n",
                alphaNoRef, extraClash);
    return extraClash ? 1 : 0;
}

bool readTable(const char* binary, std::vector<cr2::MapInfo>& table) {
    std::vector<uint8_t> bin;
    if (std::FILE* f = std::fopen(binary, "rb")) {
        std::fseek(f, 0, SEEK_END);
        bin.resize(size_t(std::ftell(f)));
        std::fseek(f, 0, SEEK_SET);
        bin.resize(std::fread(bin.data(), 1, bin.size(), f));
        std::fclose(f);
    }
    if (cr2::findMapTable(bin, table)) return true;
    std::fprintf(stderr, "%s: no maps' table\n", binary);
    return false;
}

int fieldScripts(const char* path, const char* binary, const char* only) {
    cr2::MvglArchive a;
    std::vector<cr2::MapInfo> table;
    if (!openArchive(a, path) || !readTable(binary, table)) return 1;
    size_t maps = 0, ran = 0, npcs = 0, npcsShown = 0;
    std::map<std::string, int> stubs;
    for (const cr2::MapInfo& info : table) {
        if (only && info.name != only) continue;
        cr2::FieldMap f;
        if (!f.load(a, info)) continue;
        ++maps;
        cr2::GameState state; // (a new game's: every flag clear)
        cr2::FieldScript script(a, state);
        const bool ok = script.enter(info.name, f.gimmicks());
        ran += ok;
        std::string shown;
        for (const cr2::Gimmick& g : f.gimmicks()) {
            if (g.kind != cr2::Gimmick::Npc) continue;
            ++npcs;
            if (script.visible(g)) ++npcsShown, shown += " " + g.id;
        }
        if (!ok || only) std::printf("  %s: %s%s%s\n", info.name.c_str(), ok ? "ran" : script.error().c_str(), shown.empty() ? "" : "; NPCs shown:",
                                    shown.c_str());
        for (const auto& [k, n] : script.stubCalls()) stubs[k] += n;
    }
    std::printf("%zu maps: evt000 ran on %zu; NPCs %zu, shown %zu (a new game's flags)\n", maps, ran, npcs, npcsShown);
    std::vector<std::pair<int, std::string>> top;
    for (const auto& [k, n] : stubs) top.push_back({n, k});
    std::sort(top.rbegin(), top.rend());
    std::printf("  stub natives reached (calls):");
    for (size_t k = 0; k < top.size() && k < 30; ++k) std::printf(" %s %d", top[k].second.c_str(), top[k].first);
    std::printf("\n");
    return 0;
}

int exits(const char* path, const char* binary) {
    cr2::MvglArchive a;
    std::vector<cr2::MapInfo> table;
    if (!openArchive(a, path) || !readTable(binary, table)) return 1;
    std::map<std::string, cr2::FieldMap> loaded;
    auto load = [&](const std::string& name) -> cr2::FieldMap* {
        if (auto it = loaded.find(name); it != loaded.end()) return &it->second;
        const auto row = std::find_if(table.begin(), table.end(), [&](const cr2::MapInfo& m) { return m.name == name; });
        if (row == table.end()) return nullptr;
        cr2::FieldMap& f = loaded[name];
        if (!f.load(a, *row)) {
            loaded.erase(name);
            return nullptr;
        }
        return &f;
    };
    size_t maps = 0, total = 0, good = 0;
    std::map<std::string, int> elsewhere; // exits to what isn't a field (events, the world map)
    for (const cr2::MapInfo& info : table) {
        cr2::FieldMap* f = load(info.name);
        if (!f) continue;
        ++maps;
        for (const std::string& e : f->exitNames()) {
            ++total;
            cr2::FieldMap* to = e.size() >= 5 ? load(e.substr(0, 5)) : nullptr;
            float p[3];
            if (to && to->point(e, p)) {
                ++good;
            } else {
                ++elsewhere[e];
                std::printf("  %s: exit %s - %s\n", info.name.c_str(), e.c_str(), to ? "the map has no such point" : "not a field map");
            }
        }
    }
    std::printf("%zu maps, %zu exits: %zu lead to a field map's point, %zu elsewhere\n", maps, total, good, total - good);
    return 0;
}

int walk(const char* path, const char* binary, const char* only) {
    cr2::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    std::vector<uint8_t> bin;
    if (std::FILE* f = std::fopen(binary, "rb")) {
        std::fseek(f, 0, SEEK_END);
        bin.resize(size_t(std::ftell(f)));
        std::fseek(f, 0, SEEK_SET);
        bin.resize(std::fread(bin.data(), 1, bin.size(), f));
        std::fclose(f);
    }
    std::vector<cr2::MapInfo> table;
    if (!cr2::findMapTable(bin, table)) return std::fprintf(stderr, "%s: no maps' table\n", binary), 1;
    size_t maps = 0, settled = 0, points = 0, runs = 0, offFloor = 0, tunnels = 0, stuck = 0, skipped = 0;
    for (const cr2::MapInfo& info : table) {
        if (only && info.name != only) continue;
        cr2::FieldMap field;
        std::string err;
        float start[3];
        if (!field.load(a, info, &err) || !field.point("defaultPos", start) || !field.collision().triangles()) {
            if (only) std::printf("  %s: %s\n", info.name.c_str(), err.empty() ? "no defaultPos or no collision" : err.c_str());
            ++skipped;
            continue;
        }
        ++maps;
        const cr2::CollisionWorld& w = field.collision();
        auto floorBelow = [&](const cr2::Vec3& p) { return !w.surfaceBelow(p, 0.5f).empty(); };
        auto throughWall = [&](const cr2::Vec3& from, const cr2::Vec3& to) {
            cr2::CollisionWorld::Hit h;
            return w.sweep(from, to, 0.001f, {0, 1, 0}, -2.0f, h) && std::fabs(h.normal[1]) < 0.7f;
        };
        // from where the game puts the player: each pop point (an exit's arrival), else defaultPos
        std::vector<std::pair<std::string, cr2::Vec3>> starts;
        for (const cr2::Gimmick& g : field.gimmicks())
            if (g.kind == cr2::Gimmick::PopPoint) starts.push_back({g.name, {g.position[0], g.position[1], g.position[2]}});
        if (starts.empty()) starts.push_back({"defaultPos", {start[0], start[1], start[2]}});
        bool allSettled = true;
        for (const auto& [from, at] : starts) {
            cr2::CharacterController c(w);
            c.setPosition({at[0], at[1] + 0.17f, at[2]});
            for (int k = 0; k < 60; ++k) c.setVelocity({0, 0, 0}), c.step();
            const cr2::Vec3 rest = c.position();
            ++points;
            if (!(c.onGround() && floorBelow(rest))) {
                allSettled = false;
                std::printf("  %s: not on a floor after dropping at %s (%.3f %.3f %.3f -> %.3f %.3f %.3f)\n", info.name.c_str(),
                            from.c_str(), at[0], at[1], at[2], rest[0], rest[1], rest[2]);
                continue;
            }
            for (int dir = 0; dir < 8; ++dir) {
                ++runs;
                c.setPosition(rest);
                const float ang = float(dir) * 0.785398f;
                const cr2::Vec3 v{0.48f * std::sin(ang), 0, 0.48f * std::cos(ang)};
                bool off = false, tunnel = false;
                for (int k = 0; k < 240; ++k) {
                    const cr2::Vec3 before = c.position();
                    c.setVelocity(v);
                    c.step();
                    if (!off && !floorBelow(c.position())) off = true;
                    if (!tunnel && throughWall({before[0], before[1], before[2]}, c.position())) tunnel = true;
                }
                const cr2::Vec3 end = c.position();
                offFloor += off;
                tunnels += tunnel;
                stuck += std::hypot(end[0] - rest[0], end[2] - rest[2]) < 0.01f;
                if (off || tunnel)
                    std::printf("  %s from %s, direction %d: %s%s (ends %.3f %.3f %.3f)\n", info.name.c_str(), from.c_str(), dir,
                                off ? "left the floor " : "", tunnel ? "through a wall" : "", end[0], end[1], end[2]);
            }
        }
        settled += allSettled;
    }
    std::printf("%zu maps walked (%zu without collision or defaultPos), from %zu points: %zu maps settled on a floor at all; "
                "%zu runs, %zu left the floor, %zu went through a wall, %zu didn't move (against a wall at once)\n",
                maps, skipped, points, settled, runs, offFloor, tunnels, stuck);
    return offFloor || tunnels ? 1 : 0;
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
    if (!std::strcmp(argv[1], "shaders")) return shaders(argv[2]);
    if (!std::strcmp(argv[1], "exits") && argc > 3) return exits(argv[2], argv[3]);
    if (!std::strcmp(argv[1], "fieldscript") && argc > 3) return fieldScripts(argv[2], argv[3], argc > 4 ? argv[4] : nullptr);
    if (!std::strcmp(argv[1], "walk") && argc > 3) return walk(argv[2], argv[3], argc > 4 ? argv[4] : nullptr);
    if (!std::strcmp(argv[1], "images")) return images(argv[2], argc > 3 ? argv[3] : nullptr, argc > 4 ? argv[4] : nullptr);
    if (!std::strcmp(argv[1], "run") && argc > 3) return run(argv[2], argv[3], argc > 4 ? argv[4] : nullptr);
    return usage();
}
