// cr2tool: CR2-U's command-line checks on your own copy of the game's data.
//
//   cr2tool check <archive.mvgl | .obb>              every file decodes to its size
//   (an <archive> may be its folder; an <executable> "-": the maps' table found beside the archive -
//   the iPad's executable, the Vita's eboot.bin, the Android release's APK beside its OBB)
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
//   cr2tool walk <archive> <executable> [map]       the field player (field/character.hpp, in the
//                                                    game's updates) on every map's floors: dropped
//                                                    at each pop point (else defaultPos), run 4 s in
//                                                    8 directions - on a floor throughout, never
//                                                    beyond a wall
//   cr2tool exits <archive> <executable>             every map's exits: the map each leads to (the
//                                                    pop point's first five letters) is there and
//                                                    has that point
//   cr2tool fieldscript <archive> <executable> [map] [--all-flags]
//                                                    every map's entry (its dungeon script's evt000,
//                                                    a new game's state - or every flag set): runs or
//                                                    not, the gimmicks it shows; its event boxes',
//                                                    things to tap's, the other gimmicks' and evt
//                                                    functions run to their end; the stubs reached
//   cr2tool event <archive> <script> [function]     an event's timeline: its native calls frame by
//                                                    frame (the waits met at once)
//   cr2tool play <archive> <event | all>             an event played by the runtime (event/
//                                                    runtime.hpp, its pages turning by themselves):
//                                                    its messages (numbers, not text), fades, camera,
//                                                    end; "all": every event script's, to its end
//   cr2tool shaders <archive>                        the materials' shaders: the features their
//                                                    GLSL has, and where the key decoder (gfx/
//                                                    material.cpp) disagrees with it, per bit
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "assets/geom.hpp"
#include "battle/battledata.hpp"
#include "battle/glass.hpp"
#include "script/gamesave.hpp"
#include "battle/rules.hpp"
#include "assets/image.hpp"
#include "assets/skeleton.hpp"
#include "assets/mvgl.hpp"
#include "assets/release.hpp"
#include "battle/script.hpp"
#include "field/character.hpp"
#include "field/fieldmap.hpp"
#include "field/navigation.hpp"
#include "event/runtime.hpp"
#include "field/fieldscript.hpp"
#include "gfx/material.hpp"
#include "script/vm.hpp"
#include "audio/sounddata.hpp"

namespace {

// the release's sound (audio/sounddata.hpp): every track decoded whole, every clip read
int sound(const char* path) {
    cr2::SoundData sd;
    std::string err;
    if (!sd.open(path, &err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    std::printf("sound: %s\n", sd.describe().c_str());
    int tracks = 0, bad = 0;
    for (int t = 1; t <= cr2::SoundData::kTracks; ++t) {
        std::string terr;
        std::unique_ptr<cr2::MusicStream> o = sd.openTrack(t, &terr);
        if (!o) {
            std::printf("bgm%02d: %s\n", t, terr.c_str());
            ++bad;
            continue;
        }
        std::vector<int16_t> buf(4096 * size_t(o->channels()));
        uint64_t n = 0;
        for (int got; (got = o->read(buf.data(), 4096)) > 0;) n += uint64_t(got);
        // (the loop: sought to and read on - the same frames as from the start?)
        bool loopOk = true;
        if (o->loopStart >= 0) {
            std::vector<int16_t> a(1024 * size_t(o->channels()));
            loopOk = o->seek(uint32_t(o->loopStart)) && o->read(a.data(), 1024) > 0;
        }
        std::printf("bgm%02d: %d Hz x%d, %.1f s (%llu frames, %u said), loops %lld .. %lld%s\n", t, o->rate(), o->channels(),
                    double(n) / o->rate(), static_cast<unsigned long long>(n), o->frames(), static_cast<long long>(o->loopStart),
                    static_cast<long long>(o->loopEnd), loopOk ? "" : " (its loop doesn't decode!)");
        if (!loopOk || n == 0) ++bad;
        else ++tracks;
    }
    for (int b = 0; b < 2; ++b) {
        const auto bank = cr2::SoundData::Bank(b);
        int ok = 0;
        double seconds = 0;
        std::vector<int16_t> pcm;
        for (int k = 0; k < sd.clips(bank); ++k) {
            cr2::SoundData::ClipFormat fmt;
            if (sd.clip(bank, k, pcm, &fmt)) ++ok, seconds += double(pcm.size()) / double(fmt.rate * fmt.channels);
        }
        std::printf("%s: %d / %d clips read, %.0f s\n", b == 0 ? "effects" : "voices", ok, sd.clips(bank), seconds);
    }
    std::printf("%d tracks decode, %d don't\n", tracks, bad);
    return bad ? 1 : 0;
}

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
                 "cr2tool fieldscript <archive> <executable> [map] [--all-flags]\n"
                 "cr2tool event <archive> <script> [function]\n"
                 "cr2tool play <archive> <event | all>\n"
                 "cr2tool battle <archive> <coupling | all>\n"
                 "cr2tool encounters <archive> [executable]\n"
                 "cr2tool fight <archive> <coupling> [solo | pair] [seed] [a,b]\n"
                 "cr2tool sound <archive>\n"
                 "cr2tool glass <seeds>\n"
                 "cr2tool save-import <game.dat | cont.dat> [system.dat]  (a CR2-U save on stdout)\n"
                 "cr2tool save-export <archive> <CR2-U save> <game.dat>\n");
    return 2;
}

bool openArchive(cr2::MvglArchive& a, const char* path) {
    std::string err;
    if (!a.open(cr2::resolveArchive(path), &err)) { // (a folder: the OBB or the CRDB.*.mvgl in it)
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

// An event's timeline: the script run frame by frame (WaitFrame waits), its native calls in order
// with their arguments - the messages taken as read at once, no button touched, sounds done
int traceEvent(const char* path, const char* script, const char* function, int maxFrames) {
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
    int frame = 0, wait = 0;
    std::string last;
    int repeats = 0;
    vm.setNative("", "WaitFrame", [&](cr2::NativeCall& c) {
        wait = std::max(1, c.integer(0, 1));
        c.suspend();
    });
    vm.setStubHook([&](const std::string& n, cr2::NativeCall& c) {
        std::string line = n + "(";
        for (int i = 0; i < c.count(); ++i) line += (i ? ", " : "") + c.text(i);
        line += ")";
        if (line == last) {
            ++repeats;
        } else {
            if (repeats) std::printf("         (x%d more)\n", repeats);
            std::printf("%6d  %s\n", frame, line.c_str());
            last = line;
            repeats = 0;
        }
        // the waits' conditions: met at once
        if (n == "EvtWindow.CheckMessage" || n == "EvtFade.Check") c.result(true);
        else if (n == "Cr3Sound.GetState" || n == "EvtSkip.CheckButtonTouch" || n == "Cr3Touch.GetTouchState") c.result(0);
        else if (n == "EvtWindow.CheckYesNoButton") c.result(1);
        else if (n == "EvtIntroduction.GetVisible") c.result(false);
        else if (n == "VistaEvent.CheckExecution") c.result(false);
    });
    bool ok = vm.call(fn);
    while (ok && vm.waiting() && frame < maxFrames) {
        frame += wait;
        wait = 0;
        ok = vm.resume();
    }
    if (repeats) std::printf("         (x%d more)\n", repeats);
    std::printf("%s.%s: %s at frame %d (%.1f s at 20 frames a second)\n", name.c_str(), fn.c_str(),
                !ok ? ("failed: " + vm.error()).c_str() : vm.waiting() ? "still running" : "finished", frame, frame / 20.0);
    return ok ? 0 : 1;
}

// An event to its end through the runtime: what it shows, per change (a message's number and
// pages, never its text); `quiet` only the outcome. 0: it ended
int playEvent(const cr2::MvglArchive& a, const std::string& event, bool quiet, std::string* outcome = nullptr,
              std::map<std::string, int>* stubsOut = nullptr) {
    cr2::GameState state;
    cr2::EventRuntime rt(a, state);
    rt.autoPage = 1.0f;
    if (!rt.start(event)) {
        if (outcome) *outcome = "failed: " + rt.error();
        if (!quiet) std::printf("%s: %s\n", event.c_str(), rt.error().c_str());
        return 1;
    }
    int lastMessage = -1, lastColour = -1, lastVisible = -1;
    float lastTarget = -1;
    bool ok = true;
    const int maxFrames = 20 * 60 * 20;
    while (ok && rt.running() && rt.frame() < maxFrames) {
        ok = rt.tick();
        if (quiet) continue;
        const double t = rt.frame() * cr2::EventRuntime::kTick;
        const int msg = rt.messageShown() ? 1 : 0;
        if (msg != lastMessage && msg)
            std::printf("%6d %6.2f s  message: %zu pages, speaker %d\n", rt.frame(), t, rt.message().pages.size(), rt.message().speaker);
        lastMessage = msg;
        if (rt.fade().colour != lastColour || rt.fade().target != lastTarget)
            std::printf("%6d %6.2f s  fade: colour %d to %.2f\n", rt.frame(), t, rt.fade().colour, rt.fade().target);
        lastColour = rt.fade().colour, lastTarget = rt.fade().target;
        int visible = 0;
        for (const cr2::EventModel& m : rt.scene().models()) visible += m.visible;
        if (visible != lastVisible) std::printf("%6d %6.2f s  models shown: %d of %zu\n", rt.frame(), t, visible, rt.scene().models().size());
        lastVisible = visible;
        if (rt.frame() % 20 == 0) {
            cr2::Mat34 w;
            float fov, zn, zf;
            if (rt.scene().camera(w, fov, zn, zf))
                std::printf("%6d %6.2f s  camera (%.3f %.3f %.3f) fov %.1f, near %.3f far %.1f\n", rt.frame(), t, w.m[3], w.m[7], w.m[11], fov, zn, zf);
            if (std::getenv("CR2_PLAY_MODELS"))
                std::printf("               camera looks along (%.3f %.3f %.3f), up (%.3f %.3f %.3f)\n", -w.m[2], -w.m[6], -w.m[10], w.m[1], w.m[5], w.m[9]);
            if (std::getenv("CR2_PLAY_MODELS"))
                for (const cr2::EventModel& m : rt.scene().models())
                    if (m.visible)
                        std::printf("               %-8s at (%.3f %.3f %.3f) scale %.3f, %s %.2f s\n", m.name.c_str(), m.place.m[3], m.place.m[7], m.place.m[11],
                                    std::sqrt(m.place.m[0] * m.place.m[0] + m.place.m[4] * m.place.m[4] + m.place.m[8] * m.place.m[8]),
                                    m.animation.empty() ? m.figure.c_str() : m.animation.c_str(), m.animTime);
        }
    }
    char buf[256];
    std::snprintf(buf, sizeof buf, "%s at frame %d (%.1f s)%s", !ok ? ("failed: " + rt.error()).c_str() : rt.running() ? "still running" : "ended",
                  rt.frame(), rt.frame() * cr2::EventRuntime::kTick, rt.battle() >= 0 ? (", battle " + std::to_string(rt.battle())).c_str() : "");
    if (outcome) *outcome = buf;
    if (stubsOut)
        for (const auto& [n, k] : rt.stubCalls()) (*stubsOut)[n] += k;
    if (!quiet) {
        std::printf("%s: %s\n", event.c_str(), buf);
        std::string stubs;
        for (const auto& [n, k] : rt.stubCalls()) stubs += " " + n + " " + std::to_string(k);
        std::printf("stubs:%s\n", stubs.c_str());
    }
    return ok && !rt.running() ? 0 : 1;
}

int play(const char* path, const char* event) {
    cr2::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    if (std::strcmp(event, "all") != 0) return playEvent(a, event, false);
    // every event script: script\<name>.nut whose name ends in _evt (its function the same, but for case)
    std::map<std::string, int> outcomes, stubs;
    int n = 0, ended = 0;
    for (const auto& e : a.entries()) {
        const std::string& f = e.name;
        if (f.compare(0, 7, "script\\") != 0 || f.size() < 12 || f.compare(f.size() - 8, 8, "_evt.nut") != 0) continue;
        std::string outcome;
        ++n;
        ended += playEvent(a, f.substr(7, f.size() - 11), true, &outcome, &stubs) == 0;
        const std::string kind = outcome.substr(0, outcome.find(" at frame"));
        if (kind != "ended") std::printf("%s: %s\n", f.c_str(), outcome.c_str());
        ++outcomes[kind.substr(0, 40)];
    }
    std::printf("%d events: %d ended\n", n, ended);
    for (const auto& [k, c] : outcomes) std::printf("  %s: %d\n", k.c_str(), c);
    std::printf("  stub natives reached (calls):");
    for (const auto& [k, c] : stubs) std::printf(" %s %d", k.c_str(), c);
    std::printf("\n");
    return ended == n ? 0 : 1;
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
                                   "alpha test", "fog", "light map", "scroll", "offset", "spec map alpha", "uv1",
                                   "scroll2", "offset2", "distortion", "overlay", "point light"};
    static const char* kUnsupported[] = {"billboard", "upright"}; // (the VertexPlacing bits)
    constexpr int kShadeBits = int(sizeof kShade / sizeof kShade[0]), kUnsupportedBits = 2;
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
            auto bit = [&](const cr2::ShaderFeatures& f) {
                const uint32_t other = f.vertex;
                return k < kShadeBits ? (f.shade >> k & 1) : (other >> (k - kShadeBits) & 1);
            };
            const bool s = bit(src), d = bit(dec);
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

// the maps' table: from `binary` ("-": the release's own, found beside the archive - the iPad's
// executable, the Vita's eboot.bin, the Android release's APK)
bool readTable(const char* path, const char* binary, std::vector<cr2::MapInfo>& table) {
    if (cr2::releaseMapTable(path, std::strcmp(binary, "-") ? binary : "", table)) return true;
    std::fprintf(stderr, "%s: no maps' table\n", binary);
    return false;
}

// fieldscript's state: a new game's (every flag clear), or with --all-flags every story flag set
// (the scripts' other branches: what's past each check)
bool gAllFlags = false;
cr2::GameState fieldState() {
    cr2::GameState s;
    if (gAllFlags) s.flags.assign(10000, true);
    return s;
}

int fieldScripts(const char* path, const char* binary, const char* only) {
    cr2::MvglArchive a;
    std::vector<cr2::MapInfo> table;
    if (!openArchive(a, path) || !readTable(path, binary, table)) return 1;
    size_t maps = 0, ran = 0, npcs = 0, npcsShown = 0;
    size_t events = 0, reserved = 0, eventBoxes = 0, eventFunctions = 0, faded = 0;
    size_t taps = 0, tapFunctions = 0, tapsEnded = 0, tapsTalking = 0, boxesEnded = 0, boxesTalking = 0, hidden = 0, hiddenEnded = 0, hiddenTalking = 0;
    std::map<std::string, int> stubs;
    std::map<std::string, std::string> firstMap;   // the dungeon's script -> its first map (the rest's sweep)
    std::map<std::string, std::set<std::string>> called; // the dungeon's script -> the functions run above
    for (const cr2::MapInfo& info : table) {
        if (only && info.name != only) continue;
        cr2::FieldMap f;
        if (!f.load(a, info)) continue;
        ++maps;
        const std::string dungeon = info.name.substr(0, 3);
        firstMap.emplace(dungeon, info.name);
        cr2::GameState state = fieldState();
        cr2::FieldScript script(a, state);
        // (to its end: an area's set-up may play an event, its pages turning by themselves)
        bool ok = script.enter(info.name, f.gimmicks());
        const bool waited = ok && script.running();
        if (ok) ok = script.runToEnd();
        if (!ok && script.error().empty()) {
            std::printf("  %s: still running after 10 minutes of the game's updates\n", info.name.c_str());
        }
        ran += ok;
        events += waited;
        if (ok && script.events().fade().alpha > 0.01f) { // (a fade the set-up leaves on the screen)
            ++faded;
            if (only) std::printf("  fade left: colour %d at %.0f%%\n", script.events().fade().colour, script.events().fade().alpha * 100);
        }
        if (!state.next.vistaFunction.empty() || state.next.battle >= 0 || !state.next.popPoint.empty()) {
            ++reserved;
            if (only) std::printf("  next: vista %s (%s), battle %d, pop point %s\n", state.next.vistaFunction.c_str(),
                                  state.next.vistaMap.c_str(), state.next.battle, state.next.popPoint.c_str());
        }
        // the event boxes' functions: in the script, and (walked into on the area as its entry left it)
        // played to their end
        for (const cr2::Gimmick& g : f.gimmicks()) {
            if (g.kind != cr2::Gimmick::Event) continue;
            ++eventBoxes;
            if (!script.hasFunction(g.function)) {
                if (only) std::printf("  event box %s: no function %s\n", g.name.c_str(), g.function.c_str());
                continue;
            }
            ++eventFunctions;
            if (!ok) continue;
            called[dungeon].insert(g.function);
            cr2::GameState boxState = state;
            cr2::FieldScript boxScript(a, boxState);
            bool ended = boxScript.enter(info.name, f.gimmicks()) && boxScript.runToEnd();
            const int before = boxScript.events().messagesSet();
            ended = ended && boxScript.call(g.function) && boxScript.runToEnd();
            boxesEnded += ended;
            const int said = boxScript.events().messagesSet() - before;
            boxesTalking += said > 0;
            if (only || !ended)
                std::printf("  %s: event box %s - %s: %d messages, %s\n", info.name.c_str(), g.name.c_str(), g.function.c_str(), said,
                            ended ? "ran to its end" : boxScript.error().empty() ? "still running" : boxScript.error().c_str());
            for (const auto& [k, n] : boxScript.stubCalls()) stubs[k] += n;
        }
        // what the leader could tap there now (a _check gimmick shown, its event on): its function
        // called on the area as its entry left it, played to its end
        for (const cr2::Gimmick& g : f.gimmicks()) {
            if (!ok || g.checkLength() <= 0 || !script.visible(g)) continue;
            const auto st = script.gimmicks().find(g.id);
            if (st != script.gimmicks().end() && !st->second.event) continue;
            ++taps;
            if (!script.hasFunction(g.function)) {
                if (only) std::printf("  %s: no function %s\n", g.name.c_str(), g.function.c_str());
                continue;
            }
            ++tapFunctions;
            called[dungeon].insert(g.function);
            cr2::GameState tapState = state;
            cr2::FieldScript tapScript(a, tapState);
            bool tapped = tapScript.enter(info.name, f.gimmicks()) && tapScript.runToEnd();
            const int before = tapScript.events().messagesSet();
            tapped = tapped && tapScript.call(g.function) && tapScript.runToEnd();
            tapsEnded += tapped;
            const int said = tapScript.events().messagesSet() - before;
            tapsTalking += said > 0;
            if (only || !tapped)
                std::printf("  %s: tapped %s (at %.3f %.3f %.3f) - %s: %d messages, %s\n", info.name.c_str(), g.name.c_str(), g.position[0],
                            g.position[1], g.position[2], g.function.c_str(), said, tapped ? "ran to its end" : tapScript.error().empty() ? "still running" : tapScript.error().c_str());
            for (const auto& [k, n] : tapScript.stubCalls()) stubs[k] += n;
        }
        // the rest of the gimmicks' functions (hidden by a new game's flags: NPCs, things to tap later),
        // called the same way
        for (const cr2::Gimmick& g : f.gimmicks()) {
            if (!ok || g.function.empty() || called[dungeon].count(g.function) || !script.hasFunction(g.function)) continue;
            called[dungeon].insert(g.function);
            ++hidden;
            cr2::GameState hState = state;
            cr2::FieldScript hScript(a, hState);
            bool ended = hScript.enter(info.name, f.gimmicks()) && hScript.runToEnd();
            const int before = hScript.events().messagesSet();
            ended = ended && hScript.call(g.function) && hScript.runToEnd();
            hiddenEnded += ended;
            hiddenTalking += hScript.events().messagesSet() > before;
            if (only || !ended)
                std::printf("  %s: %s - %s: %s\n", info.name.c_str(), g.name.c_str(), g.function.c_str(),
                            ended ? "ran to its end" : hScript.error().empty() ? "still running" : hScript.error().c_str());
            for (const auto& [k, n] : hScript.stubCalls()) stubs[k] += n;
        }
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
    // the scripts' other evt functions (traps, hidden gimmicks', an event's parts, what other events
    // call) - none run above -, each called on its dungeon's first map: they run to their end?
    size_t rest = 0, restEnded = 0, restTalking = 0;
    for (const auto& [dungeon, mapName] : firstMap) {
        cr2::FieldMap f;
        cr2::MapInfo info;
        for (const cr2::MapInfo& m : table)
            if (m.name == mapName) info = m;
        if (!f.load(a, info)) continue;
        cr2::GameState state = fieldState();
        cr2::FieldScript script(a, state);
        if (!script.enter(info.name, f.gimmicks()) || !script.runToEnd()) continue;
        for (const std::string& fn : script.functions()) {
            if (fn.size() < 6 || fn.compare(0, 3, "evt") != 0 || fn == "evt000" || called[dungeon].count(fn)) continue;
            ++rest;
            cr2::GameState s2 = fieldState();
            cr2::FieldScript sc(a, s2);
            bool ended = sc.enter(info.name, f.gimmicks()) && sc.runToEnd();
            const int before = sc.events().messagesSet();
            ended = ended && sc.call(fn) && sc.runToEnd();
            restEnded += ended;
            restTalking += sc.events().messagesSet() > before;
            if (only || !ended)
                std::printf("  %s: %s - %s\n", info.name.c_str(), fn.c_str(), ended ? "ran to its end" : sc.error().empty() ? "still running" : sc.error().c_str());
            for (const auto& [k, n] : sc.stubCalls()) stubs[k] += n;
        }
    }
    std::printf("%zu other gimmicks' functions (hidden in a new game), %zu ran to their end, %zu said something\n", hidden, hiddenEnded, hiddenTalking);
    std::printf("%zu other evt functions (on their dungeon's first map), %zu ran to their end, %zu said something\n", rest, restEnded, restTalking);
    std::printf("%zu event boxes, %zu with their function in the dungeon's script, %zu ran to their end, %zu said something\n", eventBoxes,
                eventFunctions, boxesEnded, boxesTalking);
    std::printf("%zu things to tap (a new game's), %zu with their function, %zu ran to their end, %zu said something\n", taps, tapFunctions,
                tapsEnded, tapsTalking);
    std::printf("%zu maps: evt000 ran to its end on %zu (%zu waiting on the way, %zu reserving what comes next, %zu leaving a fade); NPCs %zu, shown %zu (a new game's flags)\n",
                maps, ran, events, reserved, faded, npcs, npcsShown);
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
    if (!openArchive(a, path) || !readTable(path, binary, table)) return 1;
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

// Where the explorer can walk (field/navigation.hpp): on every map, from each arrival point, the
// grid's reach, and the exits and event boxes beyond it (none of them within the leader's touch)
int nav(const char* path, const char* binary, const char* only) {
    cr2::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    std::vector<cr2::MapInfo> table;
    if (!readTable(path, binary, table)) return 1;
    size_t maps = 0, exits = 0, exitsFar = 0, events = 0, eventsFar = 0, empty = 0;
    for (const cr2::MapInfo& info : table) {
        if (only && info.name != only) continue;
        cr2::FieldMap field;
        float start[3];
        if (!field.load(a, info) || !field.point("defaultPos", start) || !field.collision().triangles()) continue;
        ++maps;
        std::vector<cr2::Vec3> starts;
        for (const cr2::Gimmick& g : field.gimmicks())
            if (g.kind == cr2::Gimmick::PopPoint) starts.push_back({g.position[0], g.position[1], g.position[2]});
        if (starts.empty()) starts.push_back({start[0], start[1], start[2]});
        // the union of what each arrival reaches: the nearest any grid gets to a target
        std::vector<cr2::NavGrid> grids(starts.size());
        size_t most = 0;
        for (size_t k = 0; k < starts.size(); ++k) {
            grids[k].build(field.collision(), starts[k], 0.02f, std::getenv("CR2_NAV_STEP") ? float(std::atof(std::getenv("CR2_NAV_STEP"))) : 0.01f);
            most = std::max(most, grids[k].reached());
            if (only)
                std::printf("    from (%.3f %.3f %.3f): %zu cells\n", starts[k][0], starts[k][1], starts[k][2], grids[k].reached());
        }
        if (most == 0) {
            ++empty;
            std::printf("  %s: no floor reached from its arrival points\n", info.name.c_str());
            continue;
        }
        auto closest = [&](const cr2::Vec3& p) {
            float best = 1e9f;
            for (const cr2::NavGrid& g : grids) {
                cr2::Vec3 at;
                float d;
                if (g.nearest(p, at, d)) best = std::min(best, d);
            }
            return best;
        };
        for (const std::string& e : field.exitNames()) {
            cr2::Vec3 lo, hi;
            if (!field.exitBounds(e, lo, hi)) continue;
            ++exits;
            const cr2::Vec3 c{(lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f};
            const float reach = std::max(hi[0] - lo[0], hi[2] - lo[2]) * 0.5f + 0.04f;
            const float d = closest(c);
            if (d > reach) ++exitsFar, std::printf("  %s: exit to %s out of reach (%.3f, its volume %.3f)\n", info.name.c_str(), e.c_str(), d, reach);
        }
        for (const cr2::Gimmick& g : field.gimmicks()) {
            if (g.kind != cr2::Gimmick::Event) continue;
            ++events;
            const float reach = std::max(g.jointScale[0], g.jointScale[2]) / 100.0f + (g.check ? 0.15f : 0.02f);
            const float d = closest({g.position[0], g.position[1], g.position[2]});
            if (d > reach) ++eventsFar, std::printf("  %s: %s out of reach (%.3f, %.3f)\n", info.name.c_str(), g.name.c_str(), d, reach);
        }
        if (only) std::printf("  %s: %zu cells reached at most, from %zu arrival points\n", info.name.c_str(), most, starts.size());
        if (only)
            for (const std::string& e : field.exitNames()) {
                cr2::Vec3 lo, hi;
                if (field.exitBounds(e, lo, hi))
                    std::printf("    exit %-12s (%.3f %.3f %.3f) .. (%.3f %.3f %.3f)\n", e.c_str(), lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
            }
        if (only) // (the things to tap: how near the leader's centre - on the floor, its radius up - gets, in 3D as the balloons count it)
            for (const cr2::Gimmick& g : field.gimmicks()) {
                if (g.kind == cr2::Gimmick::Event && !g.check)
                    std::printf("    %-32s box   at (%.3f %.3f %.3f) scale (%.0f %.0f %.0f) shape %d\n", g.name.c_str(), g.position[0], g.position[1],
                                g.position[2], g.jointScale[0], g.jointScale[1], g.jointScale[2], int(g.shape));
                if (g.kind == cr2::Gimmick::Ability || g.kind == cr2::Gimmick::Object || g.kind == cr2::Gimmick::Spot)
                    std::printf("    %-32s %-6s at (%.3f %.3f %.3f)\n", g.name.c_str(), g.kindName(), g.position[0], g.position[1], g.position[2]);
                if (!g.check || g.checkLength() <= 0) continue;
                float best = 1e9f;
                for (const cr2::NavGrid& gr : grids) {
                    cr2::Vec3 at;
                    float d;
                    if (gr.nearest({g.position[0], g.position[1], g.position[2]}, at, d))
                        best = std::min(best, std::hypot(d, g.position[1] - (at[1] + 0.02f - 0.02f)));
                }
                std::printf("    %-32s %-6s at (%.3f %.3f %.3f) reach %.3f, nearest %.3f%s\n", g.name.c_str(), g.kindName(), g.position[0],
                            g.position[1], g.position[2], g.checkLength(), best, best > g.checkLength() ? "  OUT" : "");
            }
    }
    std::printf("nav: %zu maps (%zu with no floor reached), %zu exits (%zu out of reach), %zu event boxes (%zu out of reach)\n", maps, empty,
                exits, exitsFar, events, eventsFar);
    return 0;
}

int walk(const char* path, const char* binary, const char* only) {
    cr2::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    std::vector<cr2::MapInfo> table;
    if (!readTable(path, binary, table)) return 1;
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
        // along the controller's own path in an update (playerStep): up a step, across at that
        // height, down - not the straight line, which cuts the corners of the steps it climbs down.
        // A hit: the wall's plane (a point on it, its normal towards where the player came from)
        auto throughWall = [&](const cr2::Vec3& from, const cr2::Vec3& to, cr2::Vec3* at = nullptr, cr2::Vec3* normal = nullptr) {
            const float up = 0.01f; // (the step height)
            const float across = std::max(from[1], to[1]) + up;
            const cr2::Vec3 path[4] = {from, {from[0], across, from[2]}, {to[0], across, to[2]}, to};
            for (int k = 0; k < 3; ++k) {
                cr2::CollisionWorld::Hit h;
                if (w.sweep(path[k], path[k + 1], 0.001f, {0, 1, 0}, -2.0f, h) && std::fabs(h.normal[1]) < 0.7f) {
                    if (at)
                        for (int c = 0; c < 3; ++c) (*at)[c] = path[k][c] + (path[k + 1][c] - path[k][c]) * h.fraction;
                    if (normal) *normal = h.normal;
                    return true;
                }
            }
            return false;
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
            for (int k = 0; k < 60; ++k) c.update({0, 0, 0}); // (3 s of the game's updates)
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
                // a crossing counts when the player stays beyond the wall's plane: the next 10 updates
                // all past it (Bullet's controller jitters in creases at the game's step, its centre
                // dipping past a wall and straight back)
                bool pending = false;
                int pendingFor = 0;
                cr2::Vec3 wallAt{}, wallNormal{};
                auto beyond = [&](const cr2::Vec3& p) {
                    return (p[0] - wallAt[0]) * wallNormal[0] + (p[1] - wallAt[1]) * wallNormal[1] + (p[2] - wallAt[2]) * wallNormal[2] < -0.002f;
                };
                for (int k = 0; k < 80; ++k) { // (4 s of the game's updates)
                    const cr2::Vec3 before = c.position();
                    c.update(v);
                    if (!off && !floorBelow(c.position())) off = true;
                    if (pending) {
                        if (!beyond(c.position())) pending = false; // (back)
                        else if (++pendingFor >= 10) tunnel = true, pending = false;
                    } else if (!tunnel && throughWall(before, c.position(), &wallAt, &wallNormal)) {
                        pending = beyond(c.position()), pendingFor = 0;
                    }
                }
                if (pending) tunnel = true;
                if (tunnel && std::getenv("CR2_WALK_DEBUG"))
                    std::printf("    the wall at (%.4f %.4f %.4f), normal (%.3f %.3f %.3f); ends (%.4f %.4f %.4f)\n", wallAt[0], wallAt[1],
                                wallAt[2], wallNormal[0], wallNormal[1], wallNormal[2], c.position()[0], c.position()[1], c.position()[2]);
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
        std::printf("%zu: base R %.4f %.4f %.4f %.4f  T %.4f %.4f %.4f  S %.4f %.4f %.4f\n", j, sk.basePose[j].rotation[0],
                    sk.basePose[j].rotation[1], sk.basePose[j].rotation[2], sk.basePose[j].rotation[3], sk.basePose[j].translation[0],
                    sk.basePose[j].translation[1], sk.basePose[j].translation[2], sk.basePose[j].scale[0], sk.basePose[j].scale[1],
                    sk.basePose[j].scale[2]);
    for (size_t j = 0; j < p.size(); ++j)
        std::printf("%zu: R %.4f %.4f %.4f %.4f  T %.4f %.4f %.4f  S %.4f %.4f %.4f\n", j, p[j].rotation[0], p[j].rotation[1],
                    p[j].rotation[2], p[j].rotation[3], p[j].translation[0], p[j].translation[1], p[j].translation[2], p[j].scale[0],
                    p[j].scale[1], p[j].scale[2]);
    return 0;
}

// The battles: each coupling's set-up (battle/battledata.cpp) against the archive - its map, its
// figures and their first motions, the battledpos joints they stand on, the commands' camera
// The field maps' encounters (FldEncount): each map's area, steps and chance from the release's
// binary, and what the area draws (its couplings by weight)
int encounters(const char* path, const char* binary) {
    cr2::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    std::vector<uint8_t> d;
    std::string err;
    cr2::BattleData data;
    if (!a.read("data\\battledata.dat", d, &err) || !data.parse(d, &err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    cr2::EncountTable table;
    if (!cr2::releaseEncountTable(path, binary && std::strcmp(binary, "-") ? binary : "", table))
        return std::fprintf(stderr, "the encounters' table: not found in the release's binary\n"), 1;
    std::vector<cr2::MapInfo> maps;
    if (!readTable(path, binary, maps)) return 1;
    int with = 0, bad = 0;
    for (const cr2::MapInfo& m : maps) {
        const cr2::EncountCell* e = table.at(m.name);
        if (!e) continue;
        ++with;
        const cr2::BattleData::Area* ar = data.area(e->area);
        std::string draws;
        for (int k = 0; ar && k < 10; ++k)
            if (ar->rates[k]) {
                if (!data.coupling(ar->couplings[k])) ++bad;
                draws += " " + std::to_string(ar->couplings[k]) + "x" + std::to_string(ar->rates[k]);
            }
        if (!ar) ++bad;
        std::printf("  %s: area %d, every %d steps, %d%% -%s (music %d, map %d)\n", m.name.c_str(), e->area, e->steps, e->chance,
                    draws.c_str(), ar ? ar->music : -1, ar ? ar->map : -1);
    }
    std::printf("encounters: %d maps with them, %d areas or couplings missing\n", with, bad);
    return bad != 0;
}

int battles(const char* path, const char* which) {
    cr2::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    std::vector<uint8_t> d;
    std::string err;
    cr2::BattleData data;
    if (!a.read("data\\battledata.dat", d, &err) || !data.parse(d, &err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    cr2::Skeleton dpos;
    if (!a.read("battledpos.skel", d, &err) || !cr2::parseSkeleton(d, dpos, &err)) return std::fprintf(stderr, "battledpos: %s\n", err.c_str()), 1;
    std::vector<uint32_t> cameras;
    for (const char* f : {"battlecamera_move.geom", "battlecamera_nomove.geom"}) {
        cr2::Geom g;
        if (a.read(f, d) && cr2::parseGeom(d, g))
            for (const cr2::GeomCamera& c : g.cameras) cameras.push_back(c.hash);
    }
    const bool all = !std::strcmp(which, "all");
    const int party[2] = {1, 2}; // (Darwin and Orlando)
    int set = 0, used = 0, complete = 0;
    std::map<std::string, int> missing;
    for (int id = all ? 1 : std::atoi(which); id <= (all ? data.couplings() : std::atoi(which)); ++id) {
        const cr2::BattleData::Coupling* c = data.coupling(id);
        if (!c || c->monsters[0] <= 0) continue; // (an empty record)
        ++used;
        cr2::BattleSetup b;
        if (!cr2::setupBattle(data, id, party, b, &err)) {
            std::printf("  %d: %s\n", id, err.c_str());
            continue;
        }
        ++set;
        std::vector<std::string> lacks;
        if (!a.contains(b.map + ".geom")) lacks.push_back("map " + b.map);
        for (const std::string& cam : {b.camera, b.startCamera})
            if (std::find(cameras.begin(), cameras.end(), cr2::nameHash(cam.c_str())) == cameras.end()) lacks.push_back("camera " + cam);
        auto unit = [&](const cr2::BattleSetup::Unit& u) {
            if (!a.contains(u.figure + ".geom")) lacks.push_back("figure " + u.figure);
            else if (!a.contains(u.figure + u.idle + ".anim") && !a.contains(u.figure + "bb01.anim")) lacks.push_back("motion " + u.figure + u.idle);
            if (dpos.joint(cr2::nameHash(u.position.c_str())) < 0) lacks.push_back("position " + u.position);
        };
        for (const auto& u : b.party) unit(u);
        for (const auto& u : b.monsters) unit(u);
        complete += lacks.empty();
        for (const std::string& l : lacks) ++missing[l];
        if (!all || !lacks.empty()) {
            std::printf("  %d: map %s, music %d, pattern %s:", id, b.map.c_str(), c->music, b.patternName.c_str());
            for (const auto& u : b.monsters) {
                const cr2::BattleData::Monster* m = data.monster(u.id);
                std::printf(" %s (%s%s, %d HP) at %s;", m->name[1].c_str(), u.figure.c_str(), u.colour.c_str(), m->hp, u.position.c_str());
            }
            for (const auto& u : b.party) std::printf(" %s at %s;", data.character(u.id)->name[1].c_str(), u.position.c_str());
            std::printf(" camera %s", b.camera.c_str());
            for (const std::string& l : lacks) std::printf("; no %s", l.c_str());
            std::printf("\n");
        }
    }
    std::printf("%d couplings with monsters: %d set up, %d with everything they use in the archive\n", used, set, complete);
    for (const auto& [k, n] : missing) std::printf("  missing %s (%d)\n", k.c_str(), n);
    return 0;
}

// A battle played by its rules (battle/rules.cpp): the party attacking every turn - solo, each at
// the first monster standing, or as a pair - the monsters choosing their own, to the end
int fight(const char* path, int coupling, bool pair, uint32_t seed, const int party[2]) {
    cr2::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    std::vector<uint8_t> d;
    std::string err;
    cr2::BattleData data;
    if (!a.read("data\\battledata.dat", d, &err) || !data.parse(d, &err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    cr2::BattleRules r(data, seed);
    if (!r.start(coupling, party, 0, &err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    // the battle's script: battle.nut's Battle_Init, the encounter's own then (its calls run to
    // their ends here, the pages turning by themselves)
    cr2::GameState state;
    const int chars[2] = {party[0] - 1, party[1] - 1}, monsters[3] = {r.fighter(2).id, r.fighter(3).id, r.fighter(4).id};
    cr2::BattleScript script(a, state, r, coupling, chars, monsters);
    bool scriptOk = script.begin() && script.runToEnd();
    r.script = [&](const std::string& f) {
        if (script.own() && scriptOk) scriptOk = script.call(f) && script.runToEnd();
    };
    if (script.own() && scriptOk) scriptOk = script.call("Battle_Start") && script.runToEnd();
    if (!scriptOk) std::printf("script: %s\n", script.error().c_str());
    std::printf("script: %s%s; charge %d\n", script.name().c_str(), script.own() ? " (its own)" : "", r.charge());
    if (r.advantage()) std::printf("start: %s\n", r.advantage() == 1 ? "the party strikes first" : "the monsters' assault");
    for (int i = 0; i < 5; ++i) {
        const cr2::Fighter& f = r.fighter(i);
        if (!f.present) continue;
        std::printf("%d %-12s lv %3d  HP %5d  MP %4d  Atk %4d Mag %4d Def %4d Spd %4d %4d\n", i, f.name.c_str(), f.level, f.maxHp, f.maxMp,
                    f.param[0], f.param[1], f.param[2], f.param[3], f.param[4]);
    }
    auto name = [&](int i) { return i >= 0 ? r.fighter(i).name : std::string("-"); };
    while (!r.outcome() && !r.exited() && r.turn() < 50) {
        std::vector<cr2::BattleRules::Order> orders;
        for (int k = 0; k < 2; ++k) {
            const cr2::Fighter& f = r.fighter(k);
            if (!f.alive) continue;
            cr2::BattleRules::Order o;
            o.actor = k;
            o.command = f.attack; // (BtlCommand::StepCommandLoop: "attack" is the character's +0x44 either way)
            for (int m = 2; m < 5; ++m)
                if (r.fighter(m).alive) {
                    o.target = m;
                    break;
                }
            orders.push_back(o);
        }
        const int turn = r.turn() + 1;
        for (const auto& e : r.playTurn(pair, orders)) {
            const cr2::BattleData::Command* c = data.command(e.command);
            const std::string what = e.failed ? "not done" : e.blocked ? "blocked" : e.hit ? std::to_string(e.damage) + " damage" : "missed";
            std::printf("turn %2d: %s%s%s %s%s -> %s: %s%s%s%s\n", turn, name(e.actor).c_str(), e.partner >= 0 ? " + " : "",
                        e.partner >= 0 ? name(e.partner).c_str() : "", c ? c->name[1].c_str() : "?", e.counter ? " (counter)" : "",
                        name(e.target).c_str(), what.c_str(), e.critical ? " (critical)" : e.weak ? " (weak)" : "", e.defeated ? ", down" : "",
                        e.regenerated ? (", back with " + std::to_string(e.regenHp) + " HP").c_str() : "");
        }
        std::printf("         charge %d, gauge %d%s; HP:", r.charge(), r.gauge(), r.breakState() == 1 ? " (the party's turn break)" : r.breakState() == 2 ? " (the monsters' turn break)" : r.breakState() == 3 ? " (a break coming)" : "");
        for (int i = 0; i < 5; ++i)
            if (r.fighter(i).present) std::printf(" %s %d", r.fighter(i).name.c_str(), r.fighter(i).hp);
        std::printf("\n");
        if (r.exitTimer() >= 0) r.exitBattle(); // (its timer: up before the next turn's input)
    }
    if (r.outcome() == 1 && !r.exited()) r.script("Battle_End"); // (BtlResult)
    std::printf("%s after %d turns\n", r.exited() ? "exited by the script" : r.outcome() == 1 ? "won" : r.outcome() == 2 ? "lost" : "undecided",
                r.turn());
    std::printf("script calls:");
    for (const std::string& c : script.calls()) std::printf(" %s", c.c_str());
    std::printf("\nstub natives:");
    for (const auto& [n, k] : script.stubCalls()) std::printf(" %s %d", n.c_str(), k);
    std::printf("\n");
    return 0;
}

// The screen breaking away (battle/glass.hpp), for seeds 1..n: its shards within the points' hull,
// none over another (their areas' sum at most its area - the game's tight super triangle takes the
// hull's thinnest), each triangle's circle holding no other point (Delaunay), how much of the
// screen they cover, and how many are still on it as it ends
int glass(int seeds) {
    int bad = 0;
    double cover = 0, tiles = 0, left = 0;
    size_t shards = 0;
    for (int seed = 1; seed <= seeds; ++seed) {
        cr2::Rand48 rng;
        rng.seed(uint32_t(seed));
        cr2::GlassFragments g;
        g.build(512, 320, rng);
        std::vector<std::pair<double, double>> pts;
        double area = 0;
        for (const cr2::GlassShard& s : g.shards) {
            area += std::fabs((s.x[1] - s.x[0]) * (s.y[2] - s.y[0]) - (s.x[2] - s.x[0]) * (s.y[1] - s.y[0])) / 2;
            for (int c = 0; c < 3; ++c) pts.push_back({s.x[c], s.y[c]});
        }
        std::sort(pts.begin(), pts.end());
        pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
        std::vector<std::pair<double, double>> hull(pts.size() * 2); // (Andrew's monotone chain)
        size_t k = 0;
        auto cross = [](auto& o, auto& a, auto& b) { return (a.first - o.first) * (b.second - o.second) - (a.second - o.second) * (b.first - o.first); };
        for (size_t i = 0; i < pts.size(); hull[k++] = pts[i++])
            while (k >= 2 && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0) --k;
        for (size_t i = pts.size() - 1, t = k + 1; i > 0; hull[k++] = pts[--i])
            while (k >= t && cross(hull[k - 2], hull[k - 1], pts[i - 1]) <= 0) --k;
        hull.resize(k - 1);
        double hullArea = 0;
        for (size_t i = 0; i < hull.size(); ++i) {
            const auto& a = hull[i];
            const auto& b = hull[(i + 1) % hull.size()];
            hullArea += a.first * b.second - b.first * a.second;
        }
        hullArea = std::fabs(hullArea) / 2;
        int empty = 0;
        for (const cr2::GlassShard& s : g.shards) {
            const double ax = s.x[0], ay = s.y[0], bx = s.x[1], by = s.y[1], cx = s.x[2], cy = s.y[2];
            const double d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
            const double a2 = ax * ax + ay * ay, b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
            const double ox = (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / d, oy = (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / d;
            const double r2 = (ax - ox) * (ax - ox) + (ay - oy) * (ay - oy);
            for (const auto& q : pts)
                if ((q.first - ox) * (q.first - ox) + (q.second - oy) * (q.second - oy) < r2 * (1 - 1e-6)) {
                    ++empty;
                    break;
                }
        }
        const bool tiled = area <= hullArea * (1 + 1e-6) && area >= hullArea * 0.9; // (none over another; the hull's thinnest lost)
        g.advanceTo(cr2::GlassFragments::kLength);
        int shown = 0;
        for (const cr2::GlassShard& s : g.shards)
            for (int c = 0; c < 3; ++c)
                if (std::fabs(s.x[c]) < 512 && std::fabs(s.y[c]) < 320) {
                    ++shown;
                    break;
                }
        if (!tiled || empty || pts.size() != size_t(cr2::GlassFragments::kPoints + 4)) {
            ++bad;
            std::printf("seed %d: %zu points, %zu shards, area %.1f of the hull's %.1f, %d with a point in their circle\n", seed,
                        pts.size(), g.shards.size(), area, hullArea, empty);
        }
        cover += area / (1024.0 * 640.0), tiles += area / hullArea, left += shown, shards += g.shards.size();
    }
    std::printf("%d seeds: %.1f shards each, covering %.2f%% of the screen (%.2f%% of their hull); %.2f on it at the end; %d bad\n",
                seeds, double(shards) / seeds, cover * 100 / seeds, tiles * 100 / seeds, left / seeds, bad);
    return bad ? 1 : 0;
}

// The game's own saves (script/gamesave.hpp): game.dat / cont.dat (and its system.dat) as a CR2-U
// save on stdout; a CR2-U save as game.dat (its records' HP, MP and parameters by the battle data)
bool readFile(const char* path, std::vector<uint8_t>& out) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    out.clear();
    uint8_t buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) out.insert(out.end(), buf, buf + n);
    std::fclose(f);
    return true;
}
int saveImport(const char* game, const char* system) {
    std::vector<uint8_t> d;
    if (!readFile(game, d)) return std::fprintf(stderr, "%s: can't read\n", game), 1;
    cr2::GameState g;
    cr2::GameSavePlace place;
    bool crc = false;
    std::string err;
    if (!cr2::readGameSave(d, g, place, &crc, &err)) return std::fprintf(stderr, "%s: %s\n", game, err.c_str()), 1;
    if (!crc) std::fprintf(stderr, "%s: its CRC doesn't hold (read anyway)\n", game);
    std::map<std::string, std::string> extra;
    if (system) {
        uint32_t options[6];
        if (!readFile(system, d) || !cr2::readSystemSave(d, g, options, &crc, &err)) return std::fprintf(stderr, "%s: %s\n", system, err.c_str()), 1;
        if (!crc) std::fprintf(stderr, "%s: its CRC doesn't hold (read anyway)\n", system);
    }
    char pos[96];
    std::snprintf(pos, sizeof pos, "%.6f %.6f %.6f %.6f", place.position[0], place.position[1], place.position[2], place.heading);
    extra["map"] = place.map, extra["place"] = pos, extra["playtime"] = std::to_string(place.playFrames);
    std::fputs(g.save(extra).c_str(), stdout);
    return 0;
}
int saveExport(const char* path, const char* save, const char* out) {
    cr2::MvglArchive a;
    if (!openArchive(a, path)) return 1;
    std::vector<uint8_t> d;
    std::string err;
    cr2::BattleData data;
    if (!a.read("data\\battledata.dat", d, &err) || !data.parse(d, &err)) return std::fprintf(stderr, "%s\n", err.c_str()), 1;
    if (!readFile(save, d)) return std::fprintf(stderr, "%s: can't read\n", save), 1;
    cr2::GameState g;
    std::map<std::string, std::string> extra;
    if (!g.load(std::string(d.begin(), d.end()), &extra, &err)) return std::fprintf(stderr, "%s: %s\n", save, err.c_str()), 1;
    cr2::GameSavePlace place;
    if (!extra["map"].empty()) place.map = extra["map"];
    std::sscanf(extra["place"].c_str(), "%f %f %f %f", &place.position[0], &place.position[1], &place.position[2], &place.heading);
    place.playFrames = uint32_t(std::atol(extra["playtime"].c_str()));
    const auto bytes = cr2::writeGameSave(g, place, [&](int ch, int& hp, int& mp, int param[5]) {
        int eq[8];
        for (int e = 0; e < 8; ++e) eq[e] = g.equip[ch][e];
        const auto st = cr2::BattleRules::playerStats(data, ch + 1, std::max(g.level[ch], 1), eq, g.sopiaLevel, g.seed[ch], g.itemCount);
        hp = st.maxHp, mp = st.maxMp;
        for (int k = 0; k < 5; ++k) param[k] = st.param[k];
    });
    std::FILE* f = std::fopen(out, "wb");
    if (!f || std::fwrite(bytes.data(), 1, bytes.size(), f) != bytes.size()) return std::fprintf(stderr, "%s: can't write\n", out), 1;
    std::fclose(f);
    std::printf("%s: %zu bytes (map %s)\n", out, bytes.size(), place.map.c_str());
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
    if (!std::strcmp(argv[1], "event") && argc > 3) return traceEvent(argv[2], argv[3], argc > 4 ? argv[4] : nullptr, 20 * 60 * 30);
    if (!std::strcmp(argv[1], "play") && argc > 3) return play(argv[2], argv[3]);
    if (!std::strcmp(argv[1], "exits") && argc > 3) return exits(argv[2], argv[3]);
    if (!std::strcmp(argv[1], "nav") && argc > 2) return nav(argv[2], argc > 3 ? argv[3] : "-", argc > 4 ? argv[4] : nullptr);
    if (!std::strcmp(argv[1], "fieldscript") && argc > 3) {
        gAllFlags = argc > 4 && !std::strcmp(argv[argc - 1], "--all-flags");
        return fieldScripts(argv[2], argv[3], argc > 4 + gAllFlags ? argv[4] : nullptr);
    }
    if (!std::strcmp(argv[1], "walk") && argc > 3) return walk(argv[2], argv[3], argc > 4 ? argv[4] : nullptr);
    if (!std::strcmp(argv[1], "images")) return images(argv[2], argc > 3 ? argv[3] : nullptr, argc > 4 ? argv[4] : nullptr);
    if (!std::strcmp(argv[1], "battle") && argc > 3) return battles(argv[2], argv[3]);
    if (!std::strcmp(argv[1], "encounters") && argc > 2) return encounters(argv[2], argc > 3 ? argv[3] : "-");
    if (!std::strcmp(argv[1], "fight") && argc > 3) {
        int party[2] = {1, 2};
        if (argc > 6) std::sscanf(argv[6], "%d,%d", &party[0], &party[1]);
        return fight(argv[2], std::atoi(argv[3]), argc > 4 && !std::strcmp(argv[4], "pair"), argc > 5 ? uint32_t(std::atoi(argv[5])) : 1, party);
    }
    if (!std::strcmp(argv[1], "run") && argc > 3) return run(argv[2], argv[3], argc > 4 ? argv[4] : nullptr);
    if (!std::strcmp(argv[1], "sound")) return sound(argv[2]);
    if (!std::strcmp(argv[1], "glass")) return glass(std::max(std::atoi(argv[2]), 1));
    if (!std::strcmp(argv[1], "save-import")) return saveImport(argv[2], argc > 3 ? argv[3] : nullptr);
    if (!std::strcmp(argv[1], "save-export") && argc > 4) return saveExport(argv[2], argv[3], argv[4]);
    return usage();
}
