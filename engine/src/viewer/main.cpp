// cr2view: CR2-U's model and field viewer.
//
// Models: a figure (`.geom`) from your copy's archive, its meshes with their materials' textures,
// render states and the game's shading (its generated shaders, as shading.glsl), lit by a map's
// character lights (`<map>c.geom`; d0101c unless --lights says), skinned to its skeleton (`.skel`)
// and playing its animations (`<figure>*.anim`).
//
// Fields (--field <map>): the map's painted layers and 3D figure with a character (--model, c001
// unless given) at one of its points (--at; defaultPos), through the map's camera, scrolling to
// keep the character centred (field/fieldmap.hpp). The maps' table is in the game's executable:
// found beside the iPad's or the Vita's archive, in the APK beside Android's OBB (its
// libChaosRings2.so; assets/release.hpp), or given with --binary.
//
// Events (--event <function> --vista <map>): an event's script played by the runtime (event/
// runtime.hpp) in its vista - the map's figure (d0151b for the opening's), the scene's models on
// their locators with their animations, through the scene's camera, under its lights, with the
// screen's fades - 20 updates a second. A message's pages turn on Return or a click (or by
// themselves after --page seconds; 0 never). --time runs the event that far first.
//
// The output: --resolution WxH (up to 3840 x 2160; F1 steps through the window's own size and the
// 16:9 ones) renders frames of that size, shown in the window at their shape (letterboxed; GL
// draws into the window, the resolution's shape letterboxed in it). --upscaler fsr3 (F2 steps
// through its modes) renders the scene smaller - --quality native (AA at full size), quality
// (1.5x), balanced (1.7x), performance (2x), ultra (3x) - and AMD FSR 3.1 resolves it into the
// frame, with --sharpness (0..1, 0.3) for its RCAS pass; Vulkan only.
//
// Frames: 60 a second (--fps; 0 as many as the display shows). The game itself updates 20 times
// a second (its scripts, fades, physics); the frames between show its timelines where they are
// by then - an event's camera, moves, animations and fades - and the field's player between its
// last two updates.
//
// Vulkan by default, OpenGL where Vulkan can't start (or with --renderer opengl).
//
//   cr2view --data <CRDB.ios.mvgl | main.212...obb | CRDB.psp2.mvgl, or a folder holding one:
//           the Vita's PCSG00498, Android's OBB + APK, an unpacked .ipa> [--model <name>]
//           [--anim <name>] [--time s] [--size WxH] [--yaw deg] [--pitch deg]
//           [--lights <figure> | none] [--field <map> [--at <point>] [--binary <file>]
//           [--walk sx,sy,seconds]] [--event <function> [--vista <map>] [--page s]]
//           [--resolution WxH] [--upscaler off|fsr3 [--quality q] [--sharpness s]] [--run s] [--fps n]
//           [--renderer vulkan|opengl] [--capture out.bmp]
//
// Models: drag to turn, the wheel to zoom, Left / Right (or PageUp / PageDown) for the archive's
// other models, Up / Down for the figure's animations. Fields: the arrow keys (or WASD) run,
// with Shift walk, on the map's floors and against its walls;
// PageUp / PageDown for the character's animations. Space pauses, Esc quits. --capture draws
// one frame (at --time; --run plays that many seconds more first, 60 frames a second) to a BMP
// and exits.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

#include "assets/geom.hpp"
#include "assets/image.hpp"
#include "assets/maptable.hpp"
#include "assets/release.hpp"
#include "assets/mvgl.hpp"
#include "assets/skeleton.hpp"
#include "battle/battledata.hpp"
#include "battle/camera.hpp"
#include "battle/motion.hpp"
#include "battle/rules.hpp"
#include "battle/script.hpp"
#include "event/runtime.hpp"
#include "field/character.hpp"
#include "field/fieldmap.hpp"
#include "field/fieldscript.hpp"
#include "gfx/material.hpp"
#include "gfx/render_device.hpp"
#include "gfx/text.hpp"

namespace {

using cr2::Mat4;

// GL enums the materials' render states use
constexpr uint32_t kGlSrcAlpha = 0x0302, kGlOneMinusSrcAlpha = 0x0303, kGlFront = 0x0404, kGlBack = 0x0405;

Mat4 perspective(float fovY, float aspect, float zn, float zf) {
    const float f = 1.0f / std::tan(fovY / 2);
    Mat4 r{};
    r[0] = f / aspect;
    r[5] = f;
    r[10] = (zf + zn) / (zn - zf);
    r[11] = -1;
    r[14] = 2 * zf * zn / (zn - zf);
    return r;
}

// a camera at `eye` looking at `at`, y up
Mat4 lookAt(const float* eye, const float* at) {
    float f[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
    const float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (float& x : f) x /= fl;
    float s[3] = {-f[2], 0, f[0]}; // f x up
    const float sl = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    for (float& x : s) x /= sl;
    const float u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
    Mat4 r{};
    r[0] = s[0], r[4] = s[1], r[8] = s[2];
    r[1] = u[0], r[5] = u[1], r[9] = u[2];
    r[2] = -f[0], r[6] = -f[1], r[10] = -f[2];
    r[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
    r[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    r[14] = f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2];
    r[15] = 1;
    return r;
}

struct ViewMesh {
    cr2::GpuMesh gpu;
    bool lit = false;
    cr2::Texture* texture = nullptr;
    cr2::Texture* normalMap = nullptr; // 0x35
    cr2::Texture* extraMap = nullptr;  // 0x48 CLUT or 0x43 light map
    cr2::GeomMesh source;              // (for skinning)
    cr2::GeomMaterial material;        // (its render states; its parameters as the animation leaves them)
    cr2::GeomMaterial baseMaterial;    // (as the file has it)
    std::vector<uint8_t> vertices;     // kVertexStride bytes each
    bool hidden = false;               // (PartsBase::SetVisible(joint, false): its joint's meshes)
};

// A figure on screen: its meshes, skeleton, animations and where it stands
struct ViewFigure {
    std::string name; // "c005.geom"
    std::vector<ViewMesh> meshes;
    cr2::Skeleton skeleton;
    bool skinned = false;
    std::vector<cr2::Mat34> inverseBind;
    std::vector<std::string> anims; // the figure's animations
    size_t animIndex = 0;
    cr2::Animation anim;
    bool animLoaded = false;
    float centre[3] = {0, 0, 0}, radius = 1;
    cr2::Mat34 place; // in the world (the skinning matrices are place * world * inverse bind)
    bool absolute = false; // its animation alone in slot 0 (map figures): not a motion over the base
    // a cross-fade (EvtMotionManager's two slots): the animation it fades from, where in it, how
    // far the fade is (1: none)
    std::string blendName;
    cr2::Animation blendAnim;
    bool blendLoaded = false, blendAbsolute = false, blendLoop = true;
    float blendTime = 0, blendWeight = 1;
    size_t vertices = 0, triangles = 0;
};

void packVertex(uint8_t* o, const float* p, const float* n, const float* uv, const float* c, const float* t, const float* b) {
    std::memcpy(o, p, 12);
    std::memcpy(o + 12, uv, 8);
    for (int k = 0; k < 4; ++k) o[20 + k] = uint8_t(std::clamp(c[k], 0.0f, 1.0f) * 255.0f + 0.5f);
    std::memcpy(o + 24, n, 12);
    std::memcpy(o + 36, t, 12);
    std::memcpy(o + 48, b, 12);
}

struct Viewer {
    cr2::RenderDevice* dev = nullptr;
    cr2::MvglArchive archive;
    std::vector<std::string> models; // the archive's .geom names, sorted
    size_t current = 0;
    ViewFigure fig;                  // the model (fields: the character)
    std::map<std::string, cr2::Texture> textures; // by name
    cr2::SceneLighting lighting;                  // the light figure's (--lights)
    std::unique_ptr<cr2::ShaderLibrary> shaders;  // the archive's (its GLSL, or the keys decoded)
    float yaw = 30, pitch = 15, zoom = 1;
    std::string status;
    bool paused = false;
    float time = 0;
    // the field
    bool fieldMode = false;
    cr2::FieldMap field;
    ViewFigure mapFigure; // <map>f
    std::vector<ViewFigure> gimmicks; // the figures <map>p's NPCs, objects, boxes and effects show
    cr2::GameState game;                        // the story's flags and work values (a new game's)
    std::unique_ptr<cr2::FieldScript> script;   // the dungeon's script
    float player[3] = {0, 0, 0}, heading = 0;
    std::unique_ptr<cr2::CharacterController> controller; // (on the map's walls and floors)
    float stepClock = 0;

    // FldPlayer::Update: the stick (screen right / up, -1..1) as a direction along the ground from
    // the camera's view, walking (0.09 units a second) or running (0.48). The game's update (20 a
    // second) is the controller's three substeps (character.hpp), one each 1/60 s - the stick read
    // at each - with the exits checked at the update's end as the game does. A frame between
    // substeps shows the player between the last two; the figure stands 0.02 below the sphere.
    float playerPrev[3] = {0, 0, 0}, playerCur[3] = {0, 0, 0};
    int substep = 0;

    // The event boxes the leader walks into (Gimmick::Event, not _check - those are tapped):
    // entering one with its event on (Cr3GimmickData.SetEventEnable) calls its function, the
    // script's coroutine then running as the area's entry does (FldMain::CallEvent)
    std::set<std::string> insideEvents;
    bool pendingBranch = false;
    void touchEvents(const cr2::Vec3& p) {
        if (!script) return;
        std::set<std::string> now;
        for (const cr2::Gimmick& g : field.gimmicks()) {
            if (g.kind != cr2::Gimmick::Event || g.check) continue;
            const float at[3] = {p[0], p[1], p[2]};
            if (!g.touches(at, controller->radius)) continue;
            now.insert(g.name);
            const auto st = script->gimmicks().find(g.id);
            const bool enabled = st == script->gimmicks().end() || st->second.event;
            if (insideEvents.count(g.name) || !enabled || script->running()) continue;
            SDL_Log("%s: touched %s - %s", field.info().name.c_str(), g.name.c_str(), g.function.c_str());
            if (!script->call(g.function)) SDL_Log("%s", script->error().c_str());
            else if (!script->running() && playMode) pendingBranch = true; // (acted on at the next update: not mid-walk)
        }
        insideEvents.swap(now);
    }
    // an event's scene putting the leader at its p000 (FldUtilSetFieldMapPlayerPos / Rot)
    void placePlayer(const cr2::FieldScript::Placement& pl) {
        if (pl.heading) heading = pl.yaw;
        if (!pl.position || !controller) return;
        controller->setPosition({pl.at[0], pl.at[1] + 0.17f, pl.at[2]}); // (dropped onto the floor, as on arriving)
        for (int k = 0; k < 10; ++k) controller->update({0, 0, 0});
        const cr2::Vec3& p = controller->position();
        player[0] = p[0], player[1] = p[1] - 0.02f, player[2] = p[2];
        std::memcpy(playerPrev, player, sizeof player);
        std::memcpy(playerCur, player, sizeof player);
        stepClock = 0, substep = 0;
    }

    // The balloons over what the leader can tap (FldMain::CheckBalloon, each update it has control:
    // Gimmick::checkLength), the nearest the one a tap talks to (CalcNearestBalloonNum)
    std::vector<size_t> balloons; // (field.gimmicks() indices)
    int nearestBalloon = -1;
    void checkBalloons() {
        balloons.clear(), nearestBalloon = -1;
        if (!script || script->running()) return;
        float best = 30000;
        const std::vector<cr2::Gimmick>& gs = field.gimmicks();
        for (size_t i = 0; i < gs.size(); ++i) {
            const cr2::Gimmick& g = gs[i];
            const float reach = g.checkLength();
            if (reach <= 0 || !script->visible(g)) continue;
            const auto st = script->gimmicks().find(g.id);
            if (st != script->gimmicks().end() && !st->second.event) continue;
            const float d = std::hypot(g.position[0] - playerCur[0], g.position[1] - playerCur[1], g.position[2] - playerCur[2]); // (as of the update)
            if (d > reach) continue;
            balloons.push_back(i);
            if (d < best) best = d, nearestBalloon = int(i);
        }
    }

    // A tap (Return, a click): the vista's page; the field's - its event's page, else the nearest
    // balloon's function (FldMain::CheckTouchEventExec_Tap, EventExecute), the leader turned to it
    void tap() {
        if (battleMode) {
            if (battleScript && battleScript->running()) battleScript->events().advance();
            return;
        }
        if (eventMode) {
            if (event) event->advance();
            return;
        }
        if (!fieldMode || !script) return;
        if (script->running()) {
            script->events().advance();
            return;
        }
        if (nearestBalloon < 0) return;
        const cr2::Gimmick& g = field.gimmicks()[size_t(nearestBalloon)];
        heading = std::atan2(g.position[0] - player[0], g.position[2] - player[2]);
        SDL_Log("%s: tapped %s - %s", field.info().name.c_str(), g.name.c_str(), g.function.c_str());
        if (!script->call(g.function)) SDL_Log("%s", script->error().c_str());
        else if (!script->running() && playMode) pendingBranch = true;
        checkBalloons();
    }

    void walk(float dt, float sx, float sy, bool run) {
        if (!controller) return;
        cr2::Vec3 right, forward;
        field.groundAxes(right, forward);
        cr2::Vec3 dir{right[0] * sx + forward[0] * sy, 0, right[2] * sx + forward[2] * sy};
        const float l = std::sqrt(dir[0] * dir[0] + dir[2] * dir[2]);
        const float speed = l > 0 ? (run ? 0.48f : 0.09f) : 0.0f;
        if (l > 0) dir = {dir[0] / l * speed, 0, dir[2] / l * speed}, heading = std::atan2(dir[0], dir[2]);
        constexpr float kSub = cr2::CharacterController::kUpdate / cr2::CharacterController::kSubsteps; // (1/60 s)
        stepClock = std::min(stepClock + dt, 15 * kSub);
        std::string exitTo;
        for (; stepClock >= kSub && exitTo.empty(); stepClock -= kSub) {
            std::memcpy(playerPrev, playerCur, sizeof playerCur);
            controller->move(dir);
            controller->substep();
            const cr2::Vec3& p = controller->position();
            playerCur[0] = p[0], playerCur[1] = p[1] - 0.02f, playerCur[2] = p[2];
            if (++substep < cr2::CharacterController::kSubsteps) continue;
            substep = 0;
            // an exit: the area changes to the map its pop point names (FldMain::ChangeAreaExecute)
            const std::string exit = field.exitTouched(p, controller->radius);
            if (exit.empty()) exitsArmed = true;
            else if (exitsArmed && exit.size() >= 5) exitTo = exit;
            touchEvents(p);
            checkBalloons();
        }
        playMotion(fig, speed == 0 ? "fn01" : run ? "ff02" : "ff01");
        if (!exitTo.empty()) {
            SDL_Log("exit %s", exitTo.c_str());
            enterMap(exitTo.substr(0, 5), exitTo);
            return;
        }
        const float a = std::clamp(stepClock / kSub, 0.0f, 1.0f);
        for (int c = 0; c < 3; ++c) player[c] = playerPrev[c] + (playerCur[c] - playerPrev[c]) * a;
    }

    bool decode(const std::string& name, cr2::Image& img) {
        std::vector<uint8_t> data;
        std::string err;
        if (archive.read("images\\" + name + ".img", data, &err) && cr2::decodeImg(data, img, &err)) return true;
        SDL_Log("texture %s: %s", name.c_str(), err.c_str());
        return false;
    }

    // A material's texture (`mipmaps` off for lookup tables: CLUTs)
    cr2::Texture* texture(const std::string& name, bool mipmaps = true) {
        if (name.empty()) return nullptr;
        if (auto it = textures.find(name); it != textures.end()) return it->second.id ? &it->second : nullptr;
        cr2::Image img;
        cr2::Texture& t = textures[name];
        if (!decode(name, img)) return nullptr;
        img.mipmaps = mipmaps;
        t = dev->upload(img);
        return t.id ? &t : nullptr;
    }

    // The scene's lights from a light figure (a map's <name>c): false when it has none
    bool loadLights(const std::string& figure) {
        std::vector<uint8_t> data;
        cr2::Geom g;
        cr2::Skeleton sk;
        if (!archive.read(figure + ".geom", data) || !cr2::parseGeom(data, g) || !archive.read(figure + ".skel", data) ||
            !cr2::parseSkeleton(data, sk))
            return false;
        cr2::SceneLighting l;
        if (!cr2::lightsFromFigure(g, sk, l)) return false;
        lighting = l;
        return true;
    }

    void unload(ViewFigure& f) {
        for (ViewMesh& m : f.meshes) dev->destroy(m.gpu);
        f.meshes.clear();
    }

    bool loadFigure(const std::string& name, ViewFigure& f, std::string* err) {
        unload(f);
        const size_t keepAnim = f.animIndex;
        const cr2::Mat34 keepPlace = f.place;
        f = ViewFigure();
        f.name = name;
        f.animIndex = keepAnim;
        f.place = keepPlace;
        std::vector<uint8_t> data;
        cr2::Geom g;
        if (!archive.read(name, data, err) || !cr2::parseGeom(data, g, err)) return false;
        static const float kWhite[4] = {1, 1, 1, 1}, kZero[3] = {0, 0, 0};
        for (const cr2::GeomMesh& m : g.meshes) {
            ViewMesh vm;
            vm.vertices.resize(size_t(m.vertexCount) * cr2::kVertexStride);
            for (uint32_t i = 0; i < m.vertexCount; ++i) {
                static const float kNoUv[2] = {0, 0};
                auto at3 = [&](const std::vector<float>& v) { return v.empty() ? kZero : &v[size_t(i) * 3]; };
                packVertex(&vm.vertices[size_t(i) * cr2::kVertexStride], &m.position[size_t(i) * 3], at3(m.normal),
                           m.uv.empty() ? kNoUv : &m.uv[size_t(i) * 2], m.colour.empty() ? kWhite : &m.colour[size_t(i) * 4],
                           at3(m.tangent), at3(m.binormal));
            }
            const std::vector<uint32_t> idx = m.triangles();
            vm.gpu = dev->upload(vm.vertices, idx, -1);
            vm.lit = !m.normal.empty();
            vm.source = m;
            if (m.material < g.materials.size()) { // (the joints as rows: the iPad's slots, a joint in the position's w)
                const std::array<uint32_t, 4>& key = g.materials[m.material].shaderKey;
                const bool builtIn = !key[0] && !key[1] && !key[2] && !key[3]; // (no shader of its own: its file's form says)
                vm.source.resolveJoints(builtIn ? (g.version == 0x3c ? 3.0f : 1.0f) : shaders->jointScale(key));
            }
            if (m.material < g.materials.size()) {
                vm.material = vm.baseMaterial = g.materials[m.material];
                if (SDL_getenv("CR2_MATERIALS")) { // (debugging: the materials' keys and states)
                    const cr2::GeomMaterial& gm = g.materials[m.material];
                    std::string st;
                    for (const auto& [id, v] : gm.states) st += " " + std::to_string(id) + "=" + std::to_string(v[0]) + "/" + std::to_string(v[1]);
                    SDL_Log("%s mesh %zu: material %u key %08x %08x %08x %08x states%s textures %s / %s / %s", name.c_str(), f.meshes.size(),
                            m.material, gm.shaderKey[0], gm.shaderKey[1], gm.shaderKey[2], gm.shaderKey[3], st.c_str(),
                            vm.material.diffuse().c_str(), vm.material.texture(0x43).c_str(), vm.material.texture(0x48).c_str());
                }
                vm.texture = texture(vm.material.diffuse());
                vm.normalMap = texture(vm.material.texture(0x35));
                vm.extraMap = vm.material.textures.count(0x48) ? texture(vm.material.texture(0x48), false)
                                                               : texture(vm.material.texture(0x43));
            }
            f.meshes.push_back(std::move(vm));
            f.vertices += m.vertexCount;
            f.triangles += idx.size() / 3;
        }
        float r2 = 0;
        for (int c = 0; c < 3; ++c) {
            f.centre[c] = (g.boundsMin[c] + g.boundsMax[c]) / 2;
            const float h = (g.boundsMax[c] - g.boundsMin[c]) / 2;
            r2 += h * h;
        }
        f.radius = std::max(std::sqrt(r2), 1e-3f);
        // the skeleton and the figure's animations (<figure>*.anim: c217.anim, c217e080.anim, ...)
        const std::string base = name.substr(0, name.size() - 5);
        f.skinned = archive.read(base + ".skel", data) && cr2::parseSkeleton(data, f.skeleton) &&
                    g.inverseBind.size() == f.skeleton.basePose.size();
        f.inverseBind = g.inverseBind;
        if (f.skinned)
            for (const auto& e : archive.entries())
                if (e.name.size() > 5 && e.name.compare(0, base.size(), base) == 0 && e.name.compare(e.name.size() - 5, 5, ".anim") == 0)
                    f.anims.push_back(e.name);
        std::sort(f.anims.begin(), f.anims.end());
        if (!f.anims.empty()) loadAnim(f, std::min(f.animIndex, f.anims.size() - 1));
        return true;
    }

    // the model viewer's figure: the archive's model `index`
    bool load(size_t index) {
        current = index;
        dev->resetHistory();
        const std::string& name = models[index];
        std::string err;
        if (!loadFigure(name, fig, &err)) {
            status = name + ": " + err;
            SDL_Log("%s", status.c_str());
            return false;
        }
        char buf[256];
        std::snprintf(buf, sizeof buf, "%s (%zu/%zu): %zu meshes, %zu vertices, %zu triangles", name.c_str(), index + 1,
                      models.size(), fig.meshes.size(), fig.vertices, fig.triangles);
        status = buf;
        SDL_Log("%s", status.c_str());
        return true;
    }

    void loadAnim(ViewFigure& f, size_t index) {
        f.animIndex = index;
        std::vector<uint8_t> data;
        std::string err;
        f.animLoaded = archive.read(f.anims[index], data, &err) && f.anim.parse(data, &err) &&
                       f.anim.joints() == int(f.skeleton.basePose.size());
        if (!f.animLoaded) SDL_Log("%s: %s", f.anims[index].c_str(), err.empty() ? "not this skeleton's" : err.c_str());
        time = 0;
    }

    // A field motion of the character (FldPlayer::CreatePlayer: c<id> + "fn01" idle, "ff01" walk,
    // "ff02" run, "fj01".. jumps, "fp01".. its own), if it has it and isn't playing it already
    void playMotion(ViewFigure& f, const char* motion) {
        const std::string want = f.name.substr(0, f.name.size() - 5) + motion + ".anim";
        const auto it = std::find(f.anims.begin(), f.anims.end(), want);
        if (it != f.anims.end() && (!f.animLoaded || f.anims[f.animIndex] != want)) loadAnim(f, size_t(it - f.anims.begin()));
    }

    std::string animStatus() const {
        if (!fig.skinned) return "";
        if (!fig.animLoaded) return " - base pose";
        char buf[160];
        std::snprintf(buf, sizeof buf, " - %s (%zu/%zu) %.2f / %.2f s", fig.anims[fig.animIndex].c_str(), fig.animIndex + 1,
                      fig.anims.size(), time, fig.anim.duration());
        return buf;
    }

    // A figure's pose at time t, the skinning matrices (place * world * inverse bind), the meshes'
    // vertices through their palettes (as the game's vertex shaders: MatrixPalette[index / 3 slot],
    // the weights as stored)
    void pose(ViewFigure& f, float t, bool loop = true) {
        if (!f.skinned) return;
        std::vector<cr2::JointTransform> p = f.skeleton.basePose;
        if (f.animLoaded) {
            const float frames = float(std::max(f.anim.frames() - 1, 1));
            float fr = t * f.anim.sampleRate();
            if (fr > frames) fr = loop ? std::fmod(fr, frames) : frames;
            // (relative to the base pose); the user channels: material parameters (UI fades, colours)
            std::vector<float> user(f.skeleton.userChannels.size(), std::nanf(""));
            p = f.absolute ? f.anim.poseAbsolute(f.skeleton.basePose, fr, f.anim.hasUserChannels() ? &user : nullptr)
                           : f.anim.poseOver(f.skeleton.basePose, fr, f.anim.hasUserChannels() ? &user : nullptr);
            if (f.blendWeight < 1 && f.blendLoaded) { // (fading in from the motion before)
                const float bframes = float(std::max(f.blendAnim.frames() - 1, 1));
                float bfr = f.blendTime * f.blendAnim.sampleRate();
                if (bfr > bframes) bfr = f.blendLoop ? std::fmod(bfr, bframes) : bframes;
                const std::vector<cr2::JointTransform> from = f.blendAbsolute ? f.blendAnim.poseAbsolute(f.skeleton.basePose, bfr)
                                                                              : f.blendAnim.poseOver(f.skeleton.basePose, bfr);
                const float w = std::clamp(f.blendWeight, 0.0f, 1.0f);
                for (size_t j = 0; j < p.size() && j < from.size(); ++j) {
                    cr2::JointTransform& to = p[j];
                    const cr2::JointTransform& a = from[j];
                    float dot = 0;
                    for (int c = 0; c < 4; ++c) dot += a.rotation[c] * to.rotation[c];
                    const float sgn = dot < 0 ? -1.0f : 1.0f;
                    float len = 0;
                    for (int c = 0; c < 4; ++c) to.rotation[c] = a.rotation[c] * (1 - w) + to.rotation[c] * sgn * w, len += to.rotation[c] * to.rotation[c];
                    len = len > 0 ? 1.0f / std::sqrt(len) : 1.0f;
                    for (int c = 0; c < 4; ++c) to.rotation[c] *= len;
                    for (int c = 0; c < 3; ++c) {
                        to.translation[c] = a.translation[c] + (to.translation[c] - a.translation[c]) * w;
                        to.scale[c] = a.scale[c] + (to.scale[c] - a.scale[c]) * w;
                    }
                }
            }
            for (ViewMesh& vm : f.meshes) {
                vm.material = vm.baseMaterial;
                for (size_t k = 0; k < user.size() && k < f.skeleton.userChannels.size(); ++k) {
                    const cr2::Skeleton::UserChannel& c = f.skeleton.userChannels[k];
                    if (std::isnan(user[k]) || c.material != vm.material.hash) continue;
                    std::vector<float>& v = vm.material.params[c.param];
                    if (v.size() <= c.component) v.resize(size_t(c.component) + 1, 0.0f);
                    v[c.component] = user[k];
                }
            }
        }
        const std::vector<cr2::Mat34> world = f.skeleton.world(p, f.place);
        std::vector<cr2::Mat34> skin(world.size());
        for (size_t j = 0; j < world.size(); ++j) skin[j] = world[j] * f.inverseBind[j];
        for (ViewMesh& vm : f.meshes) {
            const cr2::GeomMesh& m = vm.source;
            std::vector<const cr2::Mat34*> pal;
            for (uint32_t j : m.palette) pal.push_back(j < skin.size() ? &skin[j] : nullptr);
            for (uint32_t i = 0; i < m.vertexCount; ++i) {
                const float* sp = &m.position[size_t(i) * 3];
                const float* sn = m.normal.empty() ? nullptr : &m.normal[size_t(i) * 3];
                const float* st = m.tangent.empty() ? nullptr : &m.tangent[size_t(i) * 3];
                const float* sb = m.binormal.empty() ? nullptr : &m.binormal[size_t(i) * 3];
                float op[3] = {0, 0, 0}, on[3] = {0, 0, 0}, ot[3] = {0, 0, 0}, ob[3] = {0, 0, 0};
                auto add = [&](const cr2::Mat34& M, float w) {
                    auto rot = [&](const float* v, float* o, int r) {
                        o[r] += w * (M.m[r * 4] * v[0] + M.m[r * 4 + 1] * v[1] + M.m[r * 4 + 2] * v[2]);
                    };
                    for (int r = 0; r < 3; ++r) {
                        op[r] += w * (M.m[r * 4] * sp[0] + M.m[r * 4 + 1] * sp[1] + M.m[r * 4 + 2] * sp[2] + M.m[r * 4 + 3]);
                        if (sn) rot(sn, on, r);
                        if (st) rot(st, ot, r);
                        if (sb) rot(sb, ob, r);
                    }
                };
                if (m.joints.empty()) {
                    add(pal.empty() || !pal[0] ? f.place : *pal[0], 1.0f);
                } else {
                    for (int k = 0; k < 4; ++k) {
                        const float w = m.weights.empty() ? (k == 0 ? 1.0f : 0.0f) : m.weights[size_t(i) * 4 + k];
                        const size_t slot = m.joints[size_t(i) * 4 + k] / 3;
                        if (w != 0.0f && slot < pal.size() && pal[slot]) add(*pal[slot], w);
                    }
                }
                // (normalised, as the game's vertex shaders do)
                auto unit = [](float* v) {
                    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                    if (l > 0) v[0] /= l, v[1] /= l, v[2] /= l;
                };
                unit(on);
                unit(ot);
                unit(ob);
                uint8_t* o = &vm.vertices[size_t(i) * cr2::kVertexStride];
                std::memcpy(o, op, 12);
                if (sn) std::memcpy(o + 24, on, 12);
                if (st) std::memcpy(o + 36, ot, 12);
                if (sb) std::memcpy(o + 48, ob, 12);
            }
            dev->updateVertices(vm.gpu, vm.vertices);
        }
    }

    void pose() {
        if (battleMode) {
            poseBattle();
            return;
        }
        if (eventMode) {
            poseEvent();
            return;
        }
        if (fieldMode) {
            // the character stands at `player`, facing `heading` (radians about y, 0 = +z)
            const float c = std::cos(heading), s = std::sin(heading);
            fig.place = cr2::Mat34();
            fig.place.m[0] = c, fig.place.m[2] = s, fig.place.m[8] = -s, fig.place.m[10] = c;
            fig.place.m[3] = player[0], fig.place.m[7] = player[1], fig.place.m[11] = player[2];
            pose(mapFigure, time);
            for (ViewFigure& g : gimmicks) pose(g, time);
            if (script && script->events().scene().loaded()) poseEventModels(script->events());
        }
        pose(fig, time);
    }

    // A figure's meshes with their materials' render states; opaque meshes first, then those blending
    void drawFigure(const ViewFigure& f, const float* mvp) {
        std::vector<const ViewMesh*> order;
        for (const ViewMesh& m : f.meshes) order.push_back(&m);
        std::stable_sort(order.begin(), order.end(), [](const ViewMesh* a, const ViewMesh* b) {
            uint32_t ba = 0, bb = 0;
            a->material.state(0x7c, 0, ba);
            b->material.state(0x7c, 0, bb);
            return ba < bb;
        });
        const char* only = SDL_getenv("CR2_MESHES"); // (debugging: "1,3" draws meshes 1 and 3 only)
        for (const ViewMesh* mp : order) {
            const ViewMesh& m = *mp;
            if (m.hidden) continue;
            if (only && !std::strstr((std::string(",") + only + ",").c_str(),
                                     (',' + std::to_string(mp - f.meshes.data()) + ',').c_str()))
                continue;
            cr2::DrawState st;
            cr2::MeshShading sh;
            uint32_t v = 0, v2 = 0;
            if (m.material.state(0x79, 0, v) && v) { // alpha test: GREATER (the game's only func) than ref
                uint32_t bits = 0;
                m.material.state(0x78, 0, bits, 1);
                std::memcpy(&sh.alphaRef, &bits, 4);
            }
            m.material.state(0x7c, 0, v);
            st.blend = v != 0;
            m.material.state(0x7a, kGlSrcAlpha, v, 0);
            m.material.state(0x7a, kGlOneMinusSrcAlpha, v2, 1);
            st.blendSrc = cr2::blendCode(v, false);
            st.blendDst = cr2::blendCode(v2, true);
            m.material.state(0x7e, 1, v);
            m.material.state(0x7d, kGlBack, v2);
            st.cull = v && !SDL_getenv("CR2_NO_CULL") ? (v2 == kGlFront ? 1 : 2) : 0;
            m.material.state(0x80, 1, v);
            st.depthWrite = v != 0;
            m.material.state(0x81, 1, v);
            st.depthTest = v != 0;
            // each texture with its own sampler (the material's texture entries)
            st.sampling = cr2::sampling(m.material.sampler(0x32));
            sh.texMode = m.texture ? 4 : -1; // (the built-in Figure shaders: modulate the vertex colour)
            sh.lit = m.lit;
            cr2::applyMaterial(m.material, shaders->features(m.material.shaderKey), sh); // (the game's shading: keyed materials)
            sh.normalMap = m.normalMap;
            sh.normalSampling = cr2::sampling(m.material.sampler(0x35));
            sh.extraMap = m.extraMap;
            sh.extraSampling = cr2::sampling(m.material.sampler(m.material.textures.count(0x48) ? 0x48 : 0x43));
            dev->drawMesh(m.gpu, m.texture, mvp, sh, st);
        }
    }

    // A painted layer: the 4096-unit quad with its image
    void drawLayer(const std::string& image, const Mat4& proj, float shiftX = 0) {
        cr2::Texture* t = texture(image);
        if (!t) return;
        float v[24];
        cr2::FieldMap::layerQuad(v, shiftX);
        static const float kWhite[4] = {1, 1, 1, 1};
        dev->drawQuad(*t, v, proj.data(), kWhite);
    }

    // The output: a chosen resolution (0 x 0: the window's), and the temporal upscaler (Vulkan)
    int resW = 0, resH = 0;
    cr2::UpscaleSettings upscale;
    int screenX = 0, screenY = 0, screenW = 1, screenH = 1; // the game's screen in the frame

    // A frame of fw x fh: the game's screen all of it (an offscreen device's frames are the
    // chosen resolution, letterboxed into the window when shown), or - drawing straight into the
    // window (GL) - the resolution's shape letterboxed in it. The scene (smaller while an
    // upscaler is on), then the UI over the resolved frame at full size.
    void draw(int fw, int fh) {
        dev->beginFrame(fw, fh);
        screenX = screenY = 0, screenW = fw, screenH = fh;
        if (!dev->offscreen() && resW > 0 && resH > 0) {
            if (int64_t(resW) * fh > int64_t(fw) * resH) screenH = std::max(int(int64_t(fw) * resH / resW), 1);
            else screenW = std::max(int(int64_t(fh) * resW / resH), 1);
            screenX = (fw - screenW) / 2, screenY = (fh - screenH) / 2;
        }
        int rw = fw, rh = fh;
        dev->sceneSize(fw, fh, rw, rh);
        const float kx = float(rw) / float(fw), ky = float(rh) / float(fh);
        dev->setViewport(screenX * kx, screenY * ky, screenW * kx, screenH * ky);
        drawScene(screenW, screenH);
        dev->endScene();
        dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
        drawEventUi(); // (the vista's, or the field's events')
    }

    void drawScene(int w, int h) {
        if (battleMode) {
            drawBattle(w, h);
            return;
        }
        if (eventMode) {
            drawEvent(w, h);
            return;
        }
        if (fieldMode) {
            // FldMain's order: the background (and midground), the 3D figures, the foreground
            // the scroll: following the leader, else the script's camera (a slide drawn on between updates)
            cr2::FieldFrame fr;
            if (script && script->camera().scripted()) {
                const cr2::FieldScript::Camera& c = script->camera();
                float cx = c.x, cy = c.y;
                if (c.frames > 0 && c.t < c.frames) {
                    const float k = std::clamp(fieldClock / cr2::EventRuntime::kTick, 0.0f, 1.0f) / float(c.frames);
                    cx += (c.toX - c.fromX) * k, cy += (c.toY - c.fromY) * k;
                }
                fr = field.frameScrolled(w, h, player, cx, cy);
            } else {
                fr = field.frame(w, h, player);
            }
            if (script) script->setShownScroll(-fr.scroll[0] / (float(w) / 960.0f), -fr.scroll[1] / (float(w) / 960.0f));
            fieldScene = fr.scene;
            dev->setCamera(fr.zNear, fr.zFar, fr.fovY * 3.14159265f / 180);
            cr2::SceneLighting l = field.lighting();
            std::memcpy(l.camera, fr.camera, sizeof l.camera);
            l.time = time;
            dev->setLighting(l);
            drawLayer(field.backgroundImage(), fr.background);
            drawLayer(field.midgroundImage(), fr.midground);
            drawFigure(mapFigure, fr.scene.data());
            for (const ViewFigure& g : gimmicks) drawFigure(g, fr.scene.data());
            // an event's scene in the field (EvtScene in FldMain: its models among the field's
            // figures, through the field's camera), the leader unless the script hid it
            if (script && script->events().sceneShown()) {
                const std::vector<cr2::EventModel>& models = script->events().scene().models();
                for (size_t i = 0; i < models.size() && i < eventFigures.size(); ++i)
                    if (models[i].visible) drawFigure(eventFigures[i], fr.scene.data());
            }
            if (!script || script->playerVisible()) drawFigure(fig, fr.scene.data());
            drawLayer(field.foregroundImage(), fr.foreground, field.info().foregroundShift == 1 ? -1152.0f : 0.0f);
            return;
        }
        const float yr = yaw * 3.14159265f / 180, pr = pitch * 3.14159265f / 180, d = fig.radius * 2.6f * zoom;
        const float eye[3] = {fig.centre[0] + d * std::cos(pr) * std::sin(yr), fig.centre[1] + d * std::sin(pr),
                              fig.centre[2] + d * std::cos(pr) * std::cos(yr)};
        cr2::SceneLighting l = lighting;
        std::memcpy(l.camera, eye, sizeof l.camera);
        l.time = time;
        dev->setLighting(l);
        const float zn = fig.radius * 0.01f * zoom, zf = fig.radius * 20 * zoom + d;
        dev->setCamera(zn, zf, 0.7f);
        const Mat4 mvp = cr2::mat4Multiply(perspective(0.7f, float(w) / float(std::max(h, 1)), zn, zf), lookAt(eye, fig.centre));
        drawFigure(fig, mvp.data());
    }

    // The maps' table from the game's executable (--binary, or beside the iPad's / Vita's archive)
    std::vector<cr2::MapInfo> table;
    std::string tableFrom;
    bool exitsArmed = false; // (an exit counts once the character has left them all since arriving)

    bool loadTable(const std::string& binary, const std::string& dataPath) {
        if (cr2::releaseMapTable(dataPath, binary, table, &tableFrom)) return true;
        SDL_Log("the maps' table: not found beside %s (the iPad's executable, the Vita's eboot.bin, the Android "
                "release's APK) - give the game's executable with --binary", dataPath.c_str());
        return false;
    }

    // the gimmicks shown with a figure: placed by their joints, NPCs idling (FldNPC: <figure>fn01) -
    // as the script has them now
    std::vector<std::string> shownGimmicks;
    void refreshGimmicks() {
        std::vector<std::string> want;
        for (const cr2::Gimmick& gm : field.gimmicks())
            if (!gm.figure.empty() && archive.contains(gm.figure + ".geom") && script->visible(gm)) want.push_back(gm.name);
        if (want == shownGimmicks) return;
        shownGimmicks = want;
        std::string err;
        for (ViewFigure& g : gimmicks) unload(g);
        gimmicks.clear();
        for (const cr2::Gimmick& gm : field.gimmicks()) {
            if (gm.figure.empty() || !archive.contains(gm.figure + ".geom") || !script->visible(gm)) continue;
            ViewFigure g;
            cr2::JointTransform t;
            std::memcpy(t.rotation, gm.rotation.data(), sizeof t.rotation);
            std::memcpy(t.translation, gm.position, sizeof t.translation);
            t.scale[0] = t.scale[1] = t.scale[2] = gm.scale;
            g.place = t.matrix();
            if (!loadFigure(gm.figure + ".geom", g, &err)) continue;
            if (gm.kind == cr2::Gimmick::Npc) playMotion(g, "fn01");
            gimmicks.push_back(std::move(g));
        }
    }

    // A map: its table row, layers and figures, the character dropped at the point `at` (empty:
    // where it stood, the field re-entered after a vista or a battle), the area's `entry` started
    bool enterMap(const std::string& map, const std::string& at, const std::string& entry = "evt000") {
        const auto row = std::find_if(table.begin(), table.end(), [&](const cr2::MapInfo& m) { return m.name == map; });
        if (row == table.end()) {
            SDL_Log("%s: not one of the %zu maps (%s)", map.c_str(), table.size(), tableFrom.c_str());
            return false;
        }
        std::string err;
        if (!field.load(archive, *row, &err)) {
            SDL_Log("%s: %s", map.c_str(), err.c_str());
            return false;
        }
        unload(mapFigure);
        mapFigure = ViewFigure();
        if (!field.figureName().empty() && !loadFigure(field.figureName() + ".geom", mapFigure, &err))
            SDL_Log("%s: %s", field.figureName().c_str(), err.c_str());
        mapFigure.absolute = true; // (FldMap::ChangeArea: BindAnimator(figure, 0, ...))
        // the dungeon's script sets the area up (evt000: which gimmicks show, which collide)
        if (!script) script = std::make_unique<cr2::FieldScript>(archive, game);
        script->events().autoPage = pageSeconds;
        if (!script->enter(map, field.gimmicks(), entry)) SDL_Log("%s's script: %s", map.c_str(), script->error().c_str());
        if (!playMode && script->running() && !script->runToEnd()) // (--field: the area as its set-up leaves it; --play: as it runs)
            SDL_Log("%s's %s: %s", map.c_str(), entry.c_str(), script->error().empty() ? "still running" : script->error().c_str());
        SDL_Log("%s: %s %s", map.c_str(), entry.c_str(), script->running() ? "running" : "done");
        fieldClock = 0;
        field.setSolidGimmicks([this](const cr2::Gimmick& g) {
            const auto it = script->gimmicks().find(g.id);
            return script->visible(g) && (it == script->gimmicks().end() || it->second.collision);
        });
        shownGimmicks.clear();
        refreshGimmicks();
        if (at.empty()) {
            // (where the leader stood)
        } else if (!field.point(at, player)) {
            SDL_Log("%s: no point %s; the points:", map.c_str(), at.c_str());
            for (const std::string& n : field.pointNames()) SDL_Log("  %s", n.c_str());
        }
        fieldMode = true;
        exitsArmed = false;
        dev->resetHistory();
        controller = std::make_unique<cr2::CharacterController>(field.collision());
        controller->setPosition({player[0], player[1] + 0.17f, player[2]}); // (dropped onto the floor)
        for (int k = 0; k < 10; ++k) controller->update({0, 0, 0});
        const cr2::Vec3& p = controller->position();
        player[0] = p[0], player[1] = p[1] - 0.02f, player[2] = p[2];
        std::memcpy(playerPrev, player, sizeof player);
        std::memcpy(playerCur, player, sizeof player);
        stepClock = 0, substep = 0;
        insideEvents.clear();
        { // (where the leader stands as the area starts: those boxes don't fire till it leaves them)
            for (const cr2::Gimmick& g : field.gimmicks())
                if (g.kind == cr2::Gimmick::Event && !g.check && g.touches(player, 0.02f)) insideEvents.insert(g.name);
        }
        checkBalloons();
        std::string exits;
        for (const std::string& e : field.exitNames()) exits += " " + e;
        SDL_Log("%zu gimmicks, %zu with figures; layers bg [%s] fg [%s]", field.gimmicks().size(), gimmicks.size(), field.backgroundImage().c_str(), field.foregroundImage().c_str());
        SDL_Log("field %s (table from %s): variant %d, layers %s%s%s%s, %zu collision triangles, exits%s, at %s (%.3f %.3f %.3f)",
                map.c_str(), tableFrom.c_str(), row->variant, field.backgroundImage().empty() ? "" : "bg ",
                field.midgroundImage().empty() ? "" : "mg ", field.foregroundImage().empty() ? "" : "fg ",
                field.figureName().empty() ? "" : "+ 3D figure", field.collision().triangles(), exits.empty() ? " none" : exits.c_str(),
                at.c_str(), player[0], player[1], player[2]);
        return true;
    }

    bool loadField(const std::string& map, const std::string& at, const std::string& binary, const std::string& dataPath,
                   const std::string& entry = "evt000") {
        return loadTable(binary, dataPath) && enterMap(map, at, entry);
    }

    // The field's script, a coroutine stepped at the game's 20 updates a second (its events'
    // scene, windows and fades with it); the leader held while it runs, as FldMain holds it
    float fieldClock = 0, pageSeconds = 3.0f;
    bool fieldScriptRunning() const { return script && script->running(); }
    void fieldUpdate(float dt) {
        if (!script) return;
        fieldClock = std::min(fieldClock + dt, 0.25f);
        for (; fieldClock >= cr2::EventRuntime::kTick; fieldClock -= cr2::EventRuntime::kTick) {
            if (pendingBranch) {
                pendingBranch = false;
                branchAfterField();
                if (!fieldMode) return;
                continue;
            }
            // (the windows, the fade, the scene go on updating once the script has ended: GameMain's
            // update runs them whether or not a script waits)
            const bool wasRunning = script->running();
            if (!script->tick()) SDL_Log("%s's script: %s", field.info().name.c_str(), script->error().c_str());
            if (!wasRunning) continue;
            refreshGimmicks();
            if (script->events().scene().loaded()) syncEventFigures(script->events());
            placePlayer(script->takePlacement());
            checkBalloons();
            if (!script->running()) {
                SDL_Log("%s: the script ended", field.info().name.c_str());
                if (playMode) branchAfterField();
                if (!fieldMode) return; // (gone to a vista)
            }
        }
    }

    // A frame's time: the vista's event, else the field (its script, then the leader - held while
    // the script runs)
    void advance(float dt, float sx, float sy, bool run) {
        if (battleMode) {
            battleClock += dt;
            updateBattleScript(dt);
            updateBattleShow();
            // a fight of the flow over: its last action shown, a moment after
            if (fightingInFlow && battleStep == BattleStep::Over && !battleScriptRunning() && battleClock > showEnd + 1.5f) {
                fightingInFlow = false;
                SDL_Log("play: battle %s", battleRules->exited() ? "ended by its script" : battleRules->outcome() == 1 ? "won" : "lost (taken as won: no game over yet)");
                ++battlesSkipped;
                afterBattle();
            }
        } else if (eventMode) {
            updateEvent(dt);
        } else if (fieldMode) {
            fieldUpdate(dt);
            if (fieldMode && !eventMode) {
                const bool held = fieldScriptRunning();
                walk(dt, held ? 0 : sx, held ? 0 : sy, run);
            }
        }
    }

    // The events' runtime the screen shows: the vista's, else the field's
    cr2::EventRuntime* activeEvents() {
        if (battleMode && battleScript) return &battleScript->events(); // (the battle's talks)
        return eventMode ? event.get() : script ? &script->events() : nullptr;
    }

    // --play: the game's flow - the field's entry, then what it reserved (GameState::next):
    // another area (SetPopPoint), a battle, a vista event (FldMain::BranchAfterEvent); after a
    // vista, its battle, another vista, else the field again (VistaMain::BranchAfterVista: evt200
    // after SetComeFromVistaToField); after a battle the field (evt100). Battles aren't in yet:
    // one is taken as won.
    bool playMode = false;
    std::string returnMap;
    float returnPlayer[3] = {0, 0, 0}, returnHeading = 0;
    void branchAfterField() {
        cr2::GameState::Next& n = game.next;
        if (!n.popPoint.empty()) {
            const std::string pp = n.popPoint;
            n.popPoint.clear();
            SDL_Log("play: to %s", pp.c_str());
            enterMap(pp.substr(0, 5), pp);
        } else if (n.battle >= 0) {
            returnMap = field.info().name, std::memcpy(returnPlayer, player, sizeof player), returnHeading = heading;
            skipBattle();
        } else if (n.vista) {
            returnMap = field.info().name, std::memcpy(returnPlayer, player, sizeof player), returnHeading = heading;
            n.vista = false;
            SDL_Log("play: vista %s in %s", n.vistaFunction.c_str(), n.vistaMap.c_str());
            if (!startVista(n.vistaFunction, n.vistaMap)) returnToField(-1);
        }
    }
    // a battle the flow reached: fought (its party SetBattleParty's, else the party now; the party
    // attacking as a pair each turn), then on as the game goes after it (BtlMain::Exit) - or,
    // with --skip-battles, taken as won at once
    bool skipBattles = false;
    void skipBattle() {
        cr2::GameState::Next& n = game.next;
        if (!skipBattles) {
            int party[2];
            for (int k = 0; k < 2; ++k) {
                const int c = n.battleParty[k] >= 0 ? n.battleParty[k] : game.party[k];
                party[k] = c >= 0 ? c + 1 : 0; // (characters from 1 in the battle's data)
            }
            SDL_Log("play: battle %d (characters %d, %d)", n.battle, party[0], party[1]);
            const int id = n.battle;
            n.battle = -1, n.battleParty[0] = n.battleParty[1] = -1;
            if (startBattle(id, party)) {
                startFight(2, 1u + uint32_t(battlesSkipped));
                fightingInFlow = true;
                return;
            }
        }
        SDL_Log("play: battle %d - taken as won", n.battle);
        n.battle = -1;
        ++battlesSkipped;
        afterBattle();
    }
    bool fightingInFlow = false;
    // BtlMain::Exit: a vista reserved goes on to it, else the field (evt100)
    void afterBattle() {
        cr2::GameState::Next& n = game.next;
        battleMode = false;
        if (n.vista) { // (BtlMain::Exit: a vista reserved goes on to it)
            n.vista = false;
            if (startVista(n.vistaFunction, n.vistaMap)) return;
        }
        returnToField(1);
    }
    int battlesSkipped = 0;
    void branchAfterVista() {
        cr2::GameState::Next& n = game.next;
        if (n.battle >= 0) skipBattle();
        else if (n.vista) {
            n.vista = false;
            SDL_Log("play: vista %s in %s", n.vistaFunction.c_str(), n.vistaMap.c_str());
            if (!startVista(n.vistaFunction, n.vistaMap)) returnToField(n.fieldMode);
        } else returnToField(n.fieldMode);
    }
    void returnToField(int mode) {
        cr2::GameState::Next& n = game.next;
        const std::string entry = mode == 1 ? "evt100" : mode == 2 ? "evt200" : "evt000";
        n.fieldMode = -1;
        eventMode = false;
        dev->resetHistory();
        if (!n.popPoint.empty()) {
            const std::string pp = n.popPoint;
            n.popPoint.clear();
            SDL_Log("play: to %s (%s)", pp.c_str(), entry.c_str());
            enterMap(pp.substr(0, 5), pp, entry);
        } else {
            SDL_Log("play: back to %s (%s)", returnMap.c_str(), entry.c_str());
            std::memcpy(player, returnPlayer, sizeof player), heading = returnHeading;
            enterMap(returnMap, "", entry);
        }
    }

    // A battle's stage (--battle: BtlMainCtrl::StepInit): the coupling's map (its own lights - the
    // directional one turned to come from the camera, BtlMain::SetLights' default), the party and
    // the monsters each on its battledpos joint (BtlModel::SetPosRot: the joint's place and turn)
    // playing its first motion (BtlMotionTable: the party bb02 then bb01, the monsters bb01), seen
    // through the commands' camera (Cam_Getset_<pattern>_00, a camera of BattleCamera_nomove /
    // _move; BtlCamera::Start: near halved - at least 0.1 - far five times, at most 5000)
    bool battleMode = false;
    cr2::BattleData battleData;
    cr2::BattleSetup battle;
    ViewFigure battleMap;
    struct BattleUnit {
        ViewFigure fig;
        std::string figure;      // ("j001", "m206")
        std::string position;    // its battledpos joint
        cr2::Mat34 home;         // (there)
        // its motion: `motion` (a suffix: "bb02") from `motionStart` (battle seconds), once unless
        // `loop`, then `then` looping
        std::string motion = "bb01", then = "bb01";
        float motionStart = 0;
        bool loop = true;
        bool hidden = false;
    };
    std::vector<BattleUnit> battleUnits; // (the party, then the monsters)
    struct CameraFigure {
        cr2::Geom geom;
        cr2::Skeleton skel;
        cr2::Animation anim;
        bool animated = false;
    } battleCameras[2];
    std::string battleCamera;
    float battleClock = 0, battleCameraStart = 0; // (the camera's animation runs from its start)
    void setBattleCamera(const std::string& name) {
        if (name.empty() || name == battleCamera) return;
        battleCamera = name, battleCameraStart = battleClock;
        dev->resetHistory(); // (a cut)
    }
    void setBattleCamera(int number) {
        if (const char* n = cr2::battleCameraName(number)) setBattleCamera(std::string(n));
    }

    bool startBattle(int id, const int party[2]) {
        std::vector<uint8_t> d;
        std::string err;
        if (!archive.read("data\\battledata.dat", d, &err) || !battleData.parse(d, &err) || !cr2::setupBattle(battleData, id, party, battle, &err)) {
            SDL_Log("battle %d: %s", id, err.c_str());
            return false;
        }
        cr2::Skeleton dpos;
        if (!archive.read("battledpos.skel", d, &err) || !cr2::parseSkeleton(d, dpos, &err)) {
            SDL_Log("battledpos: %s", err.c_str());
            return false;
        }
        const std::vector<cr2::Mat34> places = dpos.world(dpos.basePose);
        unload(battleMap);
        battleMap = ViewFigure();
        if (!loadFigure(battle.map + ".geom", battleMap, &err)) SDL_Log("battle map %s: %s", battle.map.c_str(), err.c_str());
        battleMap.absolute = true;
        cr2::Geom mg;
        cr2::Skeleton ms;
        lighting = cr2::SceneLighting();
        if (archive.read(battle.map + ".geom", d) && cr2::parseGeom(d, mg) && archive.read(battle.map + ".skel", d) && cr2::parseSkeleton(d, ms))
            cr2::lightsFromFigure(mg, ms, lighting);
        for (BattleUnit& u : battleUnits) unload(u.fig);
        battleUnits.clear();
        std::string log;
        auto add = [&](const cr2::BattleSetup::Unit& su) {
            BattleUnit u;
            if (!loadFigure(su.figure + ".geom", u.fig, &err)) SDL_Log("battle %s: %s", su.figure.c_str(), err.c_str());
            // a monster's colour: its images' letter (m206 + a: m206a01)
            if (!su.colour.empty())
                for (ViewMesh& vm : u.fig.meshes) {
                    std::string t = vm.baseMaterial.diffuse();
                    if (t.size() > su.figure.size() && t.compare(0, su.figure.size(), su.figure) == 0 && std::isalpha(static_cast<unsigned char>(t[su.figure.size()]))) {
                        t[su.figure.size()] = su.colour[0];
                        if (cr2::Texture* tex = texture(t)) vm.texture = tex;
                    }
                }
            const int j = dpos.joint(cr2::nameHash(su.position.c_str()));
            if (j >= 0 && size_t(j) < places.size()) {
                cr2::Mat34 m = places[size_t(j)];
                for (int c = 0; c < 3; ++c) { // (its turn without the joint's scale)
                    const float l = std::sqrt(m.m[c] * m.m[c] + m.m[4 + c] * m.m[4 + c] + m.m[8 + c] * m.m[8 + c]);
                    if (l > 0) m.m[c] /= l, m.m[4 + c] /= l, m.m[8 + c] /= l;
                }
                u.fig.place = m;
            } else {
                SDL_Log("battle: no battledpos joint %s", su.position.c_str());
            }
            u.figure = su.figure, u.position = su.position, u.home = u.fig.place;
            u.motion = su.idle, u.then = "bb01", u.loop = su.idle == "bb01", u.motionStart = 0;
            log += " " + su.figure + su.colour + "@" + su.position;
            battleUnits.push_back(std::move(u));
        };
        for (const auto& su : battle.party) add(su);
        for (const auto& su : battle.monsters) add(su);
        const char* camFigures[2] = {"battlecamera_nomove", "battlecamera_move"};
        battleCamera.clear();
        for (int k = 0; k < 2; ++k) {
            CameraFigure& c = battleCameras[k];
            c = CameraFigure();
            if (archive.read(std::string(camFigures[k]) + ".geom", d)) cr2::parseGeom(d, c.geom);
            if (archive.read(std::string(camFigures[k]) + ".skel", d)) cr2::parseSkeleton(d, c.skel);
            c.animated = archive.read(std::string(camFigures[k]) + ".anim", d) && c.anim.parse(d) && c.anim.joints() == int(c.skel.basePose.size());
        }
        battleCamera = battle.camera;
        battleClock = 0;
        battleMode = true;
        fieldMode = eventMode = false;
        dev->resetHistory();
        status = "battle " + std::to_string(id) + " in " + battle.map;
        SDL_Log("battle %d: map %s, pattern %s,%s; camera %s", id, battle.map.c_str(), battle.patternName.c_str(), log.c_str(), battleCamera.c_str());
        return true;
    }

    // the battle's camera now: the named camera's joint in whichever figure has it
    bool battleView(cr2::Mat34& world, float& fovY, float& zn, float& zf) {
        const uint32_t h = cr2::nameHash(battleCamera.c_str());
        for (const CameraFigure& c : battleCameras)
            for (const cr2::GeomCamera& gc : c.geom.cameras) {
                if (gc.hash != h) continue;
                const int j = c.skel.joint(h);
                if (j < 0) continue;
                std::vector<cr2::JointTransform> p = c.skel.basePose;
                if (c.animated) {
                    const float frames = float(std::max(c.anim.frames() - 1, 1));
                    p = c.anim.poseAbsolute(c.skel.basePose, std::clamp((battleClock - battleCameraStart) * c.anim.sampleRate(), 0.0f, frames));
                }
                const std::vector<cr2::Mat34> w = c.skel.world(p);
                if (size_t(j) >= w.size()) continue;
                world = w[size_t(j)];
                fovY = gc.fovY;
                zn = std::max(gc.near * 0.5f, 0.1f), zf = std::min(gc.far * 5.0f, 5000.0f);
                zf = std::max(zf, zn * 2);
                return true;
            }
        return false;
    }

    // a unit's figure on one of its motions (<figure><suffix>.anim; false: it has none)
    bool useMotion(BattleUnit& u, const std::string& suffix) {
        const std::string want = u.figure + suffix + ".anim";
        const auto it = std::find(u.fig.anims.begin(), u.fig.anims.end(), want);
        if (it == u.fig.anims.end()) return false;
        if (!u.fig.animLoaded || u.fig.anims[u.fig.animIndex] != want) {
            const float keep = time;
            loadAnim(u.fig, size_t(it - u.fig.anims.begin()));
            time = keep;
        }
        return true;
    }
    float motionLength(BattleUnit& u, const std::string& suffix) { return useMotion(u, suffix) ? u.fig.anim.duration() : 0.0f; }
    void playMotion(BattleUnit& u, const std::string& suffix, float at, const std::string& then = "bb01") {
        u.motion = suffix, u.then = then, u.motionStart = at, u.loop = false;
    }

    void poseBattle() {
        pose(battleMap, battleClock);
        poseBattleShow();
        for (BattleUnit& u : battleUnits) {
            if (!u.fig.skinned) continue;
            float t = battleClock - u.motionStart;
            if (!u.loop) { // (once, then its stance)
                const float d = motionLength(u, u.motion);
                if (d <= 0 || t >= d) u.motionStart += std::max(d, 0.0f), u.motion = u.then, u.loop = true, t = battleClock - u.motionStart;
            }
            if (!useMotion(u, u.motion) && !useMotion(u, "bb01")) continue;
            pose(u.fig, std::max(t, 0.0f), u.loop);
        }
    }

    // --fight solo | pair: the battle played by its rules (battle/rules.cpp), each turn's actions
    // one after another as BtlActionCtrl shows an attack - the command's motion
    // (BtlMotionTable's), the attacker moving to its aPos joint by the target over the table's
    // move times and back over the second's, the targets' damage motion (bd01) at the first hit
    // time, the downed hidden - the party attacking every turn
    std::unique_ptr<cr2::BattleRules> battleRules;
    int battleFight = 0; // 0 off, 1 solo, 2 pair
    int fighterUnit[5] = {-1, -1, -1, -1, -1};
    cr2::Skeleton battleApos;
    std::vector<cr2::Mat34> battleAposWorld;
    struct ShowAction {
        int actor = -1, partner = -1, command = 0;
        std::vector<cr2::BattleRules::Event> hits;
    };
    std::vector<ShowAction> showActions;
    size_t showIndex = 0;
    float showStart = 0; // (battle seconds)
    float showHitAt = 0, showEnd = 0;
    int showCamera2 = -1;
    bool showHitDone = false;
    struct Move {
        int unit = -1;
        cr2::Mat34 to;
        float start = -1, end = -1, back = -1, backEnd = -1;
        bool active = false;
    } showMoves[2];

    // the battleapos joint `name`'s place (false: none)
    bool aposPlace(const std::string& name, cr2::Mat34& out) {
        const int j = battleApos.joint(cr2::nameHash(name.c_str()));
        if (j < 0 || size_t(j) >= battleAposWorld.size()) return false;
        out = battleAposWorld[size_t(j)];
        for (int c = 0; c < 3; ++c) {
            const float l = std::sqrt(out.m[c] * out.m[c] + out.m[4 + c] * out.m[4 + c] + out.m[8 + c] * out.m[8 + c]);
            if (l > 0) out.m[c] /= l, out.m[4 + c] /= l, out.m[8 + c] /= l;
        }
        return true;
    }
    // where an attacker goes to strike a target (BtlActionCtrl::GetAposName): by the target the
    // party's "aPos_<its place><the attacker's: pSoloA, pPairA...>", by one of the party a
    // monster's "aPos_<pSoloA | pSoloB | pPair><its place: s, ssL, m, msL...>"
    std::string aposName(int actor, int target) {
        auto key = [&](int f) {
            const int u = fighterUnit[f];
            std::string k = u >= 0 ? battleUnits[size_t(u)].position.substr(5) : "";
            return k;
        };
        const bool pair = battleRules && battleRules->fighter(actor).partner >= 0;
        if (actor < 2) return "aPos_" + key(target) + std::string(pair ? (actor == 0 ? "pPairA" : "pPairB") : (actor == 0 ? "pSoloA" : "pSoloB"));
        std::string mk = key(actor);
        if (mk == "sC") mk = "s";
        else if (mk == "mC") mk = "m";
        const bool targetPair = battleRules && battleRules->fighter(target).partner >= 0;
        return "aPos_" + std::string(targetPair ? "pPair" : target == 0 ? "pSoloA" : "pSoloB") + mk;
    }

    // The fight's steps (BtlMainCtrl / BtlCommand / BtlActionCtrl), the battle's script between:
    // the opening, Battle_Start; each turn Turn_Start, its order, then for each action
    // Command_Start, the action shown, Command_End; the end - each step after the script's call
    // is over (BtlEvent::IsRunning), its talks on the screen meanwhile
    enum class BattleStep { Opening, TurnBegin, TurnChoose, ActionStart, ActionDo, ActionEnd, Over };
    BattleStep battleStep = BattleStep::Over;
    std::unique_ptr<cr2::BattleScript> battleScript;
    float battleScriptClock = 0;
    bool battleScriptOk = true;
    // one of the game's updates for the script (20 a second), and the exit's timer
    void updateBattleScript(float dt) {
        if (!battleRules) return;
        battleRules->runExitTimer(dt);
        if (!battleScript || !battleScriptOk) return;
        battleScriptClock = std::min(battleScriptClock + dt, 0.25f);
        for (; battleScriptClock >= cr2::EventRuntime::kTick; battleScriptClock -= cr2::EventRuntime::kTick)
            if (!(battleScriptOk = battleScript->tick())) SDL_Log("battle script: %s", battleScript->error().c_str());
    }
    bool battleScriptRunning() const { return battleScript && battleScriptOk && battleScript->running(); }

    void startFight(int mode, uint32_t seed) {
        battleFight = mode;
        battleRules = std::make_unique<cr2::BattleRules>(battleData, seed);
        std::string err;
        int party[2] = {0, 0};
        for (size_t k = 0; k < battle.party.size() && k < 2; ++k) party[k] = battle.party[k].id;
        if (!battleRules->start(battle.coupling, party, 0, &err)) {
            SDL_Log("fight: %s", err.c_str());
            battleRules.reset();
            return;
        }
        // its script: battle.nut's Battle_Init now (BtlMainCtrl::StepBattleIn), the encounter's own after
        const int chars[2] = {party[0] - 1, party[1] - 1};
        const int monsters[3] = {battleRules->fighter(2).id, battleRules->fighter(3).id, battleRules->fighter(4).id};
        battleScript = std::make_unique<cr2::BattleScript>(archive, game, *battleRules, battle.coupling, chars, monsters);
        battleScriptClock = 0;
        battleScriptOk = battleScript->begin() && battleScript->runToEnd(100);
        if (!battleScriptOk) SDL_Log("battle script: %s", battleScript->error().c_str());
        else SDL_Log("battle script: %s%s", battleScript->name().c_str(), battleScript->own() ? " (its own)" : "");
        battleRules->script = [this](const std::string& f) {
            if (battleScript && battleScriptOk && battleScript->own() && !(battleScriptOk = battleScript->call(f)))
                SDL_Log("battle script: %s", battleScript->error().c_str());
        };
        battleStep = BattleStep::Opening;
        int u = 0;
        for (int k = 0; k < 2; ++k) fighterUnit[k] = party[k] > 0 ? u++ : -1;
        for (int k = 0; k < 3; ++k) fighterUnit[2 + k] = k < int(battle.monsters.size()) ? int(battle.party.size()) + k : -1;
        std::vector<uint8_t> d;
        if (archive.read("battleapos.skel", d) && cr2::parseSkeleton(d, battleApos)) battleAposWorld = battleApos.world(battleApos.basePose);
        showActions.clear(), showIndex = 0;
        // the opening: Cam_Start_<pattern>_00 (BtlMainCtrl::StepBattleIn), then the turns
        setBattleCamera(battle.startCamera);
        showEnd = battleClock + 3.0f;
        SDL_Log("fight: %s", mode == 2 ? "pair" : "solo");
    }

    // the party's places for its formation (BtlUtil::SetPairPosRot)
    void placeParty(bool pair) {
        cr2::Skeleton dpos;
        std::vector<uint8_t> d;
        if (!archive.read("battledpos.skel", d) || !cr2::parseSkeleton(d, dpos)) return;
        const std::vector<cr2::Mat34> w = dpos.world(dpos.basePose);
        for (int k = 0; k < 2; ++k) {
            const int u = fighterUnit[k];
            if (u < 0) continue;
            const bool alone = fighterUnit[1 - k] < 0;
            const std::string name = alone ? "dPos_pSoloC" : pair ? (k == 0 ? "dPos_pPairA" : "dPos_pPairB") : (k == 0 ? "dPos_pSoloA" : "dPos_pSoloB");
            const int j = dpos.joint(cr2::nameHash(name.c_str()));
            if (j < 0) continue;
            cr2::Mat34 m = w[size_t(j)];
            for (int c = 0; c < 3; ++c) {
                const float l = std::sqrt(m.m[c] * m.m[c] + m.m[4 + c] * m.m[4 + c] + m.m[8 + c] * m.m[8 + c]);
                if (l > 0) m.m[c] /= l, m.m[4 + c] /= l, m.m[8 + c] /= l;
            }
            BattleUnit& bu = battleUnits[size_t(u)];
            bu.home = bu.fig.place = m, bu.position = name;
        }
    }

    // the fight's next step once the last is over and the script's call is
    void stepBattleShow() {
        if (!battleRules || battleFight == 0) return;
        if (battleScriptRunning() || battleClock < showEnd) return;
        if (battleRules->exitTimer() >= 0) return; // (its exit coming: before a win or a loss is shown)
        if (battleStep != BattleStep::Over && (battleRules->exited() || (battleRules->outcome() && battleStep != BattleStep::ActionEnd))) {
            if (!battleRules->exited() && battleRules->outcome() == 1) battleRules->script("Battle_End"); // (BtlResult)
            battleStep = BattleStep::Over;
            showIndex = showActions.size();
            setBattleCamera(battle.camera);
            return;
        }
        switch (battleStep) {
        case BattleStep::Opening: // (the opening over: StepBattleIn's Battle_Start)
            battleStep = BattleStep::TurnBegin;
            battleRules->script("Battle_Start");
            break;
        case BattleStep::TurnBegin:
            battleRules->beginTurn();
            battleStep = BattleStep::TurnChoose;
            break;
        case BattleStep::TurnChoose: { // the party attacking the first monster standing
            const bool pair = battleFight == 2;
            std::vector<cr2::BattleRules::Order> orders;
            for (int k = 0; k < 2; ++k) {
                const cr2::Fighter& f = battleRules->fighter(k);
                if (!f.alive) continue;
                cr2::BattleRules::Order o;
                o.actor = k, o.command = f.attack;
                for (int m = 2; m < 5; ++m)
                    if (battleRules->fighter(m).alive) {
                        o.target = m;
                        break;
                    }
                orders.push_back(o);
            }
            battleRules->chooseTurn(pair, orders);
            placeParty(battleRules->fighter(0).partner >= 0);
            SDL_Log("turn %d", battleRules->turn() + 1);
            battleStep = BattleStep::ActionStart;
            break;
        }
        case BattleStep::ActionStart:
            if (!battleRules->actionsLeft()) {
                battleRules->endTurn();
                SDL_Log("turn over; gauge %d", battleRules->gauge());
                battleStep = BattleStep::TurnBegin;
                break;
            }
            battleRules->startAction();
            battleStep = BattleStep::ActionDo;
            break;
        case BattleStep::ActionDo: {
            std::vector<cr2::BattleRules::Event> events;
            battleRules->doAction(events);
            showActions.clear(), showIndex = 0;
            for (const auto& e : events) {
                if (showActions.empty()) showActions.push_back({e.actor, e.partner, e.command, {}});
                showActions.back().hits.push_back(e);
            }
            battleStep = BattleStep::ActionEnd;
            if (!showActions.empty()) beginShowAction(showActions[0]);
            break;
        }
        case BattleStep::ActionEnd:
            showIndex = showActions.size();
            battleRules->endAction();
            battleStep = BattleStep::ActionStart;
            break;
        case BattleStep::Over: break;
        }
    }

    void beginShowAction(const ShowAction& a) {
        showStart = battleClock, showHitDone = false;
        const cr2::BattleData::Command* c = battleData.command(a.command);
        std::string motion = c ? c->motion : "bb01";
        if (a.partner >= 0 && a.actor < 2) motion = "bp01"; // (the pair's command: +0x46, bp01)
        float length = 0;
        for (Move& m : showMoves) m = Move();
        const int target = a.hits.empty() ? -1 : a.hits.front().target;
        int slot = 0;
        for (int f : {a.actor, a.partner}) {
            const int u = f >= 0 ? fighterUnit[f] : -1;
            if (u < 0) continue;
            BattleUnit& bu = battleUnits[size_t(u)];
            playMotion(bu, motion, battleClock);
            length = std::max(length, motionLength(bu, motion));
            cr2::MotionTimes t;
            cr2::motionTimes(bu.figure, cr2::motionIndex(motion), t);
            Move& m = showMoves[slot++];
            m.unit = u;
            if (target >= 0 && t.moveStart >= 0 && t.moveEnd >= t.moveStart && aposPlace(aposName(f, target), m.to)) {
                m.active = true;
                m.start = t.moveStart, m.end = t.moveEnd;
                m.back = t.move2Start, m.backEnd = t.move2End;
            }
            if (f == a.actor) showHitAt = t.hit[0] >= 0 ? t.hit[0] : length * 0.5f;
        }
        showEnd = battleClock + std::max(length, 0.5f) + 0.3f;
        // its cameras (BtlActionCtrl::SetAttackCamera1 / 2): by the party - its kind (0 / 1 the
        // one, 2 alone, 3 a pair), the pattern's base and the target, one of two at random, the
        // next at the hit; by a monster - the base, which it is and its target (3 a pair)
        showCamera2 = -1;
        if (target >= 0) {
            const int base = cr2::cameraPattern(battle.pattern);
            const bool targetPair = battleRules->fighter(target).partner >= 0;
            if (a.actor < 2) {
                const bool alone = fighterUnit[1 - a.actor] < 0;
                const int kind = a.partner >= 0 ? 3 : alone ? 2 : a.actor;
                const int n = battle.pattern == 11 && targetPair ? 21 : base + target - 2;
                int cam = cr2::cameraAttackToEnemy(kind, n);
                if (cam >= 0) {
                    cam += int(battleClock * 1000.0f) % 2 * 2; // (BtlRand(2): either)
                    setBattleCamera(cam);
                    showCamera2 = cam + 1;
                }
            } else {
                const int cam = cr2::cameraAttackFromEnemy(base + a.actor - 2, targetPair ? 3 : target);
                if (cam >= 0) setBattleCamera(cam);
            }
            SDL_Log("    camera %s", battleCamera.c_str());
        }
        if (c) SDL_Log("  %s: %s", battleRules->fighter(a.actor).name.c_str(), c->name[1].c_str());
        if (c) commandShown = c->name[1], commandShownStart = battleClock;
    }

    void poseBattleShow() {
        if (!battleRules || battleFight == 0 || showIndex >= showActions.size()) return;
        const float s = battleClock - showStart;
        // the attackers' way there and back
        for (const Move& m : showMoves) {
            if (m.unit < 0) continue;
            BattleUnit& bu = battleUnits[size_t(m.unit)];
            if (!m.active) {
                bu.fig.place = bu.home;
                continue;
            }
            float k = 0;
            if (s >= m.start) k = m.end > m.start ? std::min((s - m.start) / (m.end - m.start), 1.0f) : 1.0f;
            if (m.back >= 0 && s >= m.back) k = m.backEnd > m.back ? 1.0f - std::min((s - m.back) / (m.backEnd - m.back), 1.0f) : 0.0f;
            if (m.back < 0 && s >= showEnd - showStart - 0.3f) k = 0;
            cr2::Mat34 p = k < 0.5f ? bu.home : m.to; // (the turn: at the move's middle)
            for (int r = 0; r < 3; ++r) p.m[r * 4 + 3] = bu.home.m[r * 4 + 3] + (m.to.m[r * 4 + 3] - bu.home.m[r * 4 + 3]) * k;
            bu.fig.place = p;
        }
    }

    // the fight's time going on: the next action (or turn), the hits as they land
    void updateBattleShow() {
        if (!battleRules || battleFight == 0) return;
        stepBattleShow();
        if (showIndex >= showActions.size()) return;
        const ShowAction& a = showActions[showIndex];
        const float s = battleClock - showStart;
        // the hits
        if (!showHitDone && s >= showHitAt) {
            showHitDone = true;
            if (showCamera2 >= 0) setBattleCamera(showCamera2);
            for (const auto& e : a.hits) {
                const int u = fighterUnit[e.target];
                if (u < 0) continue;
                BattleUnit& bu = battleUnits[size_t(u)];
                if (e.hit) playMotion(bu, "bd01", battleClock);
                if (e.hit) { // (the number at its DummyDP01 - else its middle - on the screen, in UI units)
                    float p[3] = {bu.fig.place.m[3], bu.fig.place.m[7] + 0.15f, bu.fig.place.m[11]};
                    const int dj = bu.fig.skeleton.joint(cr2::nameHash("DummyDP01"));
                    if (dj >= 0) {
                        std::vector<cr2::JointTransform> pp = bu.fig.skeleton.basePose;
                        const std::vector<cr2::Mat34> ww = bu.fig.skeleton.world(pp, bu.fig.place);
                        if (size_t(dj) < ww.size()) p[0] = ww[size_t(dj)].m[3], p[1] = ww[size_t(dj)].m[7], p[2] = ww[size_t(dj)].m[11];
                    }
                    const float* mm = battleVP.data();
                    const float cw = mm[3] * p[0] + mm[7] * p[1] + mm[11] * p[2] + mm[15];
                    if (cw > 0) {
                        const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
                        DamageShown ds;
                        ds.value = e.damage, ds.set = 20, ds.start = battleClock;
                        ds.x = (mm[0] * p[0] + mm[4] * p[1] + mm[8] * p[2] + mm[12]) / cw * float(screenW) * 0.5f / k;
                        ds.y = (mm[1] * p[0] + mm[5] * p[1] + mm[9] * p[2] + mm[13]) / cw * float(screenH) * 0.5f / k;
                        damageShown.push_back(ds);
                    }
                }
                SDL_Log("    %s -> %s: %s", battleRules->fighter(e.actor).name.c_str(), battleRules->fighter(e.target).name.c_str(),
                        e.hit ? (std::to_string(e.damage) + (e.critical ? " critical" : "") + (e.defeated ? ", down" : "")).c_str() : "missed");
            }
        }
        for (const auto& e : a.hits)
            if (e.defeated && showHitDone && s > showHitAt + 0.6f && fighterUnit[e.target] >= 0) battleUnits[size_t(fighterUnit[e.target])].hidden = true;
    }

    void drawBattle(int w, int h) {
        cr2::Mat34 cw;
        float fov = 45, zn = 0.1f, zf = 100;
        if (!battleView(cw, fov, zn, zf)) return;
        dev->setCamera(zn, zf, fov * 3.14159265f / 180);
        const Mat4 world = cr2::mat4FromMat34(cw);
        const Mat4 vp = cr2::mat4Multiply(perspective(fov * 3.14159265f / 180, float(w) / float(std::max(h, 1)), zn, zf),
                                          cr2::mat4RigidInverse(world));
        cr2::SceneLighting l = lighting;
        const float axis[3] = {cw.m[2], cw.m[6], cw.m[10]}; // (the light from the camera)
        const float n = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
        if (n > 0)
            for (int c = 0; c < 3; ++c) l.lightDir[c] = axis[c] / n;
        l.camera[0] = world[12], l.camera[1] = world[13], l.camera[2] = world[14];
        l.time = time;
        dev->setLighting(l);
        battleVP = vp;
        drawFigure(battleMap, vp.data());
        for (const BattleUnit& u : battleUnits)
            if (!u.hidden) drawFigure(u.fig, vp.data());
    }

    // The event (--event): its runtime, the vista's map figure, a figure for each of the scene's
    // models (in its order)
    std::unique_ptr<cr2::EventRuntime> event;
    bool eventMode = false;
    ViewFigure vistaMap;
    std::vector<ViewFigure> eventFigures;
    std::vector<std::string> eventFigureNames; // (what eventFigures were made for)
    float eventClock = 0;
    float fadeColours[4][3] = {{0, 0, 0}, {1, 1, 1}, {1, 1, 1}, {0, 0, 0}};
    cr2::Texture white;
    bool eventEnded = false, messageLogged = false;

    bool startEvent(const std::string& function, const std::string& map, float page) {
        initEventDrawing();
        pageSeconds = page;
        return startVista(function, map);
    }

    bool eventDrawingReady = false;
    void initEventDrawing() {
        if (eventDrawingReady) return;
        eventDrawingReady = true;
        // the fades' colours (InterfaceMain's fade windows: the figures fadeb, fadew, fadef, their
        // material's DiffuseColor); the encounter's wipe black
        const char* fades[3] = {"fadeb", "fadew", "fadef"};
        for (int k = 0; k < 3; ++k) {
            std::vector<uint8_t> d;
            cr2::Geom g;
            if (archive.read(std::string(fades[k]) + ".geom", d) && cr2::parseGeom(d, g) && !g.materials.empty())
                g.materials[0].param(0x33, fadeColours[k], 3);
        }
        cr2::Image px;
        px.width = px.height = 1;
        px.rgba = {255, 255, 255, 255};
        white = dev->upload(px);
    }

    // VistaMain: the map's figure, the event's script (its scene's figures follow it)
    bool startVista(const std::string& function, const std::string& map) {
        std::string err;
        unload(vistaMap);
        vistaMap = ViewFigure();
        for (ViewFigure& f : eventFigures) unload(f);
        eventFigures.clear(), eventFigureNames.clear();
        eventEnded = false, eventClock = 0, messageLogged = false;
        if (map != "null" && !loadFigure(map + ".geom", vistaMap, &err)) SDL_Log("vista %s: %s", map.c_str(), err.c_str());
        vistaMap.absolute = true; // (VistaMain::LoadMapData: slot 0)
        event = std::make_unique<cr2::EventRuntime>(archive, game);
        event->autoPage = pageSeconds;
        if (!event->start(function)) {
            SDL_Log("event %s: %s", function.c_str(), event->error().c_str());
            return false;
        }
        eventMode = true;
        dev->resetHistory();
        stepEvent(); // (the update the game starts it in)
        status = function + " in " + map;
        SDL_Log("event %s: vista %s, %zu scene models", function.c_str(), map.c_str(), event->scene().models().size());
        return true;
    }

    // one of the game's updates, the figures following the scene's models
    void stepEvent() {
        if (eventEnded) return;
        if (!event->tick()) {
            SDL_Log("event: %s", event->error().c_str());
            eventEnded = true;
        }
        if (!event->running() && !eventEnded) {
            eventEnded = true;
            SDL_Log("event ended at frame %d (%.2f s)%s", event->frame(), event->frame() * cr2::EventRuntime::kTick,
                    event->battle() >= 0 ? (": battle " + std::to_string(event->battle())).c_str() : "");
            if (playMode) {
                branchAfterVista();
                return;
            }
        }
        if (event->messageShown() != messageLogged) {
            messageLogged = event->messageShown();
            if (messageLogged) SDL_Log("frame %d: a message, %zu pages", event->frame(), event->message().pages.size());
        }
        syncEventFigures(*event);
    }

    // a figure for each of the scene's models, made again when they change (an EvtScene.Init)
    void syncEventFigures(cr2::EventRuntime& ev) {
        std::vector<std::string> names;
        for (const cr2::EventModel& m : ev.scene().models()) names.push_back(m.figure);
        if (names != eventFigureNames) {
            for (ViewFigure& f : eventFigures) unload(f);
            eventFigures.clear();
            for (const std::string& n : names) {
                ViewFigure f;
                std::string err;
                if (!archive.contains(n + ".geom") || !loadFigure(n + ".geom", f, &err)) SDL_Log("event model %s: %s", n.c_str(), err.empty() ? "no figure" : err.c_str());
                eventFigures.push_back(std::move(f));
            }
            eventFigureNames = names;
        }
    }

    // a frame between the event's updates: how far past the last one it is (none once it ended)
    float eventLead() const { return battleMode && battleScript ? battleScriptClock : eventMode ? (eventEnded ? 0.0f : eventClock) : fieldClock; }

    void updateEvent(float dt) {
        eventClock = std::min(eventClock + dt, 0.25f);
        for (; eventClock >= cr2::EventRuntime::kTick && eventMode; eventClock -= cr2::EventRuntime::kTick) stepEvent();
    }

    // the models where the scene has them, playing what the script chose (ChangeAnimator: their
    // <figure><suffix>; else the figure's own), their faces as ChangeModelFace left them
    void poseEvent() {
        pose(vistaMap, time);
        poseEventModels(*event);
    }

    void poseEventModels(cr2::EventRuntime& ev) {
        std::vector<cr2::EventModel>& models = ev.scene().models();
        for (size_t i = 0; i < models.size() && i < eventFigures.size(); ++i) {
            ViewFigure& f = eventFigures[i];
            const cr2::EventModel& m = models[i];
            if (!m.visible || f.meshes.empty()) continue;
            // its motion (ChangeAnimator: relative to the base pose), else its own animation alone
            // in slot 0 (absolute: an effect's)
            const std::string want = (m.animation.empty() ? m.figure : m.animation) + ".anim";
            const auto it = std::find(f.anims.begin(), f.anims.end(), want);
            if (it != f.anims.end() && (!f.animLoaded || f.anims[f.animIndex] != want)) {
                const float keep = time;
                loadAnim(f, size_t(it - f.anims.begin()));
                time = keep;
            }
            f.absolute = m.animation.empty();
            // a ChangeAnimatorBlend's fade from the motion before
            f.blendWeight = 1;
            if (m.blendSeconds > 0 && m.animTime + eventLead() < m.blendSeconds && !m.blendFrom.empty()) {
                const std::string from = m.blendFrom + ".anim";
                if (from != f.blendName) {
                    std::vector<uint8_t> d;
                    f.blendName = from;
                    f.blendLoaded = archive.read(from, d) && f.blendAnim.parse(d) && f.blendAnim.joints() == int(f.skeleton.basePose.size());
                }
                f.blendAbsolute = m.blendFrom == m.figure;
                f.blendTime = m.blendFromTime + m.animTime + eventLead();
                f.blendWeight = (m.animTime + eventLead()) / m.blendSeconds;
            }
            // EvtModel::ChangeTexture_Face: the j<NNN>face material's colour texture
            const auto face = ev.faces().find(m.face);
            const uint32_t faceMaterial = cr2::nameHash(("j" + m.figure.substr(1, 3) + "face").c_str());
            for (ViewMesh& vm : f.meshes)
                vm.texture = texture(face != ev.faces().end() && vm.baseMaterial.hash == faceMaterial ? face->second : vm.baseMaterial.diffuse());
            f.place = ev.scene().placeAt(m, eventLead());
            pose(f, m.animTime + eventLead(), m.loop);
        }
    }

    void drawEvent(int w, int h) {
        cr2::Mat34 cw;
        float fov = 45, zn = 0.1f, zf = 100;
        if (event->sceneShown() && event->scene().camera(cw, fov, zn, zf, eventLead())) {
            // a cut (the camera jumps between frames): the upscaler's history says nothing of it
            const float* lp = lastCamera.m;
            const float moved = std::hypot(cw.m[3] - lp[3], cw.m[7] - lp[7], cw.m[11] - lp[11]);
            const float turned = cw.m[2] * lp[2] + cw.m[6] * lp[6] + cw.m[10] * lp[10]; // the view axes' cosine
            if (moved > 0.05f * std::max(zn * 20, 0.1f) || turned < 0.97f) dev->resetHistory();
            lastCamera = cw;
            dev->setCamera(zn, zf, fov * 3.14159265f / 180);
            const Mat4 world = cr2::mat4FromMat34(cw);
            const Mat4 vp = cr2::mat4Multiply(perspective(fov * 3.14159265f / 180, float(w) / float(std::max(h, 1)), zn, zf),
                                              cr2::mat4RigidInverse(world));
            // VistaMain::Draw: the vista light figure's lights for the map and the models
            cr2::SceneLighting l = lighting;
            event->scene().lights(l, eventLead());
            l.camera[0] = world[12], l.camera[1] = world[13], l.camera[2] = world[14];
            l.time = time;
            dev->setLighting(l);
            drawFigure(vistaMap, vp.data());
            const std::vector<cr2::EventModel>& models = event->scene().models();
            for (size_t i = 0; i < models.size() && i < eventFigures.size(); ++i)
                if (models[i].visible) drawFigure(eventFigures[i], vp.data());
        }
    }

    cr2::Mat34 lastCamera;

    // The message windows, in the interface's space (the hud_camera2D camera: orthographic,
    // 0.96 x 0.64 units, 3:2), fitted to the screen and centred. A window is a figure whose joints
    // type<size>_<align><NN> place its text (Cr3UtilSearchTextPosition: size S 28, M 32, else 64
    // pixels of a 1000-a-unit layout, drawn 4 smaller; L left, C centred, R right) - slot k the
    // joint numbered by the window's table - and call_sendCsr_loop* its page cursor:
    // - the vista's (VistaConversationWindowMenu, vstWin): a black band across the screen's foot
    //   and the name line (vstWin_talk slides it in), stretched across screens wider than 3:2
    //   (SetScale aspect / 1.5); slots 01 the name, 11 / 12 a page's lines, 21 / 22 centred ones;
    // - the narration (NarrationWindowMenu, narrationPos): no figure of its own - the text over the
    //   screen (the fade's black, as the scripts have it), slot k numbered k + 1 in its centred
    //   column (EvtWindow::ShowMessage: a line's slot its place on the page; NarrationWindowMenu
    //   ::SetStringData: column C).
    cr2::TextRasterizer font;
    struct WindowLayout {
        ViewFigure fig;
        std::vector<std::string> joints;
        bool stretch = false; // (across a wide screen)
        bool nameTag = false; // (the name in slot 0, a page's lines from slot 1)
        int linesPerPage = 2; // (with a name tag: slots 1.. the lines, the centred ones after them)
        std::vector<std::string> slots; // slot -> the joint's number
        std::string cursorJoint;
        int window = -1;                // (EventRuntime's)
        std::string inAnim, outAnim;    // (opening / closing animators, posed at the window's age)
        bool lowerBelow3x2 = false;     // (narrower than 3:2: 0.0403125 lower)
    };
    WindowLayout vistaWindow, narrationWindow, conversationWindow;
    ViewFigure pageCursor;         // sendCsr_loop: once the page is typed
    ViewFigure conversationCursor; // msgCsr_loop: the conversation window's
    struct TextQuad {
        cr2::Texture tex;
        float x = 0, y = 0, w = 0, h = 0; // UI units: the quad's left, centre, size
        float rgb[3] = {1, 1, 1};
        std::vector<float> pen;           // each character's left edge and the end, in UI units
        int from = -1;                    // the page's characters before it (-1: shown whole - the name)
    };
    std::vector<TextQuad> textQuads;
    std::string shownText; // (what textQuads were made for)
    int shownPx = 0;

    void loadWindow(WindowLayout& w, const char* figure) {
        std::string err;
        if (!loadFigure(std::string(figure) + ".geom", w.fig, &err)) SDL_Log("window %s: %s", figure, err.c_str());
        std::vector<uint8_t> d;
        if (archive.read(std::string(figure) + ".name", d)) w.joints = cr2::parseJointNames(d);
        for (const std::string& j : w.joints)
            if (j.compare(0, 17, "call_sendCsr_loop") == 0 || j.compare(0, 16, "call_msgCsr_loop") == 0) w.cursorJoint = j;
    }

    void loadMessageWindow(const std::string& fontPath, std::vector<uint8_t> fontBytes = {}, const std::string& fontName = {}) {
        loadWindow(vistaWindow, "vstwin");
        vistaWindow.stretch = vistaWindow.nameTag = true;
        vistaWindow.slots = {"01", "11", "12", "21", "22"};
        // the cut-ins' message window (ConversationWindowMenu: msgWin, msgWin_in / msgWin_out; its
        // slots 01 the name, 02..04 a page's three lines - EvtCutinWindow::GetLinePerPage - and
        // 05..07 centred ones)
        loadWindow(conversationWindow, "msgwin");
        conversationWindow.stretch = conversationWindow.nameTag = conversationWindow.lowerBelow3x2 = true;
        conversationWindow.linesPerPage = 3;
        conversationWindow.slots = {"01", "02", "03", "04", "05", "06", "07"};
        conversationWindow.window = cr2::EventRuntime::CutIn;
        conversationWindow.inAnim = "msgwin_in.anim", conversationWindow.outAnim = "msgwin_out.anim";
        loadCutIns();
        loadWindow(narrationWindow, "narrationpos");
        for (int k = 1; k <= 30; ++k) {
            char n[4];
            std::snprintf(n, sizeof n, "%02d", k);
            narrationWindow.slots.push_back(n);
        }
        std::string err;
        if (!loadFigure("sendcsr_loop.geom", pageCursor, &err)) SDL_Log("page cursor: %s", err.c_str());
        if (!loadFigure("msgcsr_loop.geom", conversationCursor, &err)) SDL_Log("page cursor: %s", err.c_str());
        if (!fontPath.empty() && !font.load(fontPath, &err)) SDL_Log("font: %s", err.c_str());
        else if (fontPath.empty() && !fontBytes.empty() && !font.loadMemory(std::move(fontBytes), fontName, &err)) SDL_Log("font: %s", err.c_str());
        SDL_Log("message windows: vista %zu meshes, narration %zu joints; font %s", vistaWindow.fig.meshes.size(),
                narrationWindow.joints.size(), font.loaded() ? font.path().c_str() : "none (no text)");
    }

    // the interface's projection over a w x h screen: UI units to clip space (z: the camera's
    // near / far, 2.56 either way)
    static Mat4 uiProjection(int w, int h) {
        const float k = std::min(float(w) / 0.96f, float(h) / 0.64f); // pixels a unit
        Mat4 m = cr2::mat4Identity();
        m[0] = k / (float(w) * 0.5f);
        m[5] = k / (float(h) * 0.5f);
        m[10] = -1.0f / 2.56f;
        return m;
    }

    // a joint of a window's figure, posed, in UI units
    bool windowJoint(const WindowLayout& wl, const std::string& name, float& x, float& y) {
        const auto it = std::find(wl.joints.begin(), wl.joints.end(), name);
        if (it == wl.joints.end() || !wl.fig.skinned) return false;
        std::vector<cr2::JointTransform> p = wl.fig.skeleton.basePose;
        if (wl.fig.animLoaded) p = wl.fig.anim.poseOver(wl.fig.skeleton.basePose, 0);
        const std::vector<cr2::Mat34> w = wl.fig.skeleton.world(p, wl.fig.place);
        const size_t j = size_t(it - wl.joints.begin());
        if (j >= w.size()) return false;
        x = w[j].m[3], y = w[j].m[7];
        return true;
    }

    // a slot's text joint (type?_?NN, NN the slot's number)
    std::string slotJoint(const WindowLayout& wl, size_t slot) {
        if (slot >= wl.slots.size()) return {};
        for (const std::string& j : wl.joints)
            if (j.size() == 9 && j.compare(0, 4, "type") == 0 && j.compare(7, 2, wl.slots[slot]) == 0) return j;
        return {};
    }

    void drawWindow(WindowLayout& wl, bool showsMessage, int w, int h) {
        cr2::EventRuntime* ev = activeEvents();
        if (!ev) return;
        const float aspect = float(w) / float(std::max(h, 1));
        wl.fig.place = cr2::Mat34();
        if (wl.stretch && aspect >= 1.5f) wl.fig.place.m[0] = aspect / 1.5f;
        else if (wl.lowerBelow3x2) wl.fig.place.m[7] = -0.0403125f;
        const bool closing = wl.window >= 0 && ev->windowClosing(wl.window);
        if (closing) showsMessage = false;
        const std::string speaker = showsMessage && wl.nameTag ? ev->speakerName() : std::string();
        float at = 0; // (where in its animation)
        if (!wl.inAnim.empty()) { // (opening, then closing)
            const std::string want = closing ? wl.outAnim : wl.inAnim;
            const auto anim = std::find(wl.fig.anims.begin(), wl.fig.anims.end(), want);
            if (anim != wl.fig.anims.end() && (!wl.fig.animLoaded || wl.fig.anims[wl.fig.animIndex] != want)) {
                const float keep = time;
                loadAnim(wl.fig, size_t(anim - wl.fig.anims.begin()));
                time = keep;
            }
            at = std::max(ev->windowAge(wl.window), 0.0f) + eventLead();
        } else if (wl.nameTag) { // (the vista's: the name line in while someone speaks)
            const std::string want = speaker.empty() ? "vstwin_io.anim" : "vstwin_talk.anim";
            const auto anim = std::find(wl.fig.anims.begin(), wl.fig.anims.end(), want);
            if (anim != wl.fig.anims.end() && (!wl.fig.animLoaded || wl.fig.anims[wl.fig.animIndex] != want)) {
                const float keep = time;
                loadAnim(wl.fig, size_t(anim - wl.fig.anims.begin()));
                time = keep;
            }
        }
        pose(wl.fig, at, false);
        const Mat4 proj = uiProjection(w, h);
        Mat4 camera = cr2::mat4Identity();
        camera[14] = -0.881f; // (CAM_hud2D's z)
        const Mat4 view = cr2::mat4Multiply(proj, camera);
        cr2::SceneLighting l;
        dev->setLighting(l);
        if (!wl.fig.meshes.empty()) drawFigure(wl.fig, view.data());
        if (!showsMessage) return;
        // the page's text: made again when it changes (or the screen's scale does)
        const float k = std::min(float(w) / 0.96f, float(h) / 0.64f);
        const int px = int(k + 0.5f);
        std::string key = std::to_string(ev->messageWindow()) + "\n" + speaker + "\n" + std::to_string(ev->page()) + "\n";
        for (const cr2::TextLine& ln : ev->message().pages[size_t(ev->page())].lines) key += ln.text + "\n";
        if (key != shownText || px != shownPx) {
            for (TextQuad& q : textQuads) dev->destroy(q.tex);
            textQuads.clear();
            shownText = key, shownPx = px;
            int typedBefore = 0; // (the page's characters before the line: it types after them)
            auto add = [&](const std::string& text, size_t slot, int colour, bool whole) {
                int chars = 0;
                for (unsigned char c : text) chars += (c & 0xc0) != 0x80;
                const int from = whole ? -1 : typedBefore;
                if (!whole) typedBefore += chars;
                const std::string joint = slotJoint(wl, slot);
                float jx, jy;
                if (text.empty() || joint.empty() || !font.loaded() || !windowJoint(wl, joint, jx, jy)) return;
                const int size = joint[4] == 'M' ? 32 : joint[4] == 'S' ? 28 : 64;
                const bool centred = joint[6] == 'C', right = joint[6] == 'R';
                const int drawn = size - 4; // (SetTextRender: the size less 4)
                const int rasterPx = std::max(1, int(float(drawn) / 1000.0f * k + 0.5f));
                int tw = 0, th = 0;
                std::vector<float> pen;
                cr2::Image img = font.render(text, rasterPx, tw, th, &pen);
                if (img.rgba.empty()) return;
                TextQuad q;
                q.tex = dev->upload(img);
                q.from = from;
                const float unitsPerPx = float(drawn) / 1000.0f / float(rasterPx);
                for (float p : pen) q.pen.push_back(p * unitsPerPx);
                q.w = float(tw) * unitsPerPx, q.h = float(th) * unitsPerPx;
                // SetTextOffsetData: left - the joint less half the size; centred - on it; right -
                // ending there; the line's middle (size - height) / 2 from the joint
                q.x = centred ? jx - q.w / 2 : right ? jx - q.w + float(drawn) / 2000.0f : jx - float(drawn) / 2000.0f;
                q.y = jy + (float(drawn) / 1000.0f - q.h) / 2;
                cr2::textColour(colour, q.rgb);
                textQuads.push_back(q);
            };
            const std::vector<cr2::TextLine>& lines = ev->message().pages[size_t(ev->page())].lines;
            if (wl.nameTag) {
                add(speaker, 0, 0, true);
                const bool centred = !lines.empty() && lines[0].centred;
                for (size_t i = 0; i < lines.size() && i < size_t(wl.linesPerPage); ++i)
                    add(lines[i].text, (centred ? 1 + wl.linesPerPage : 1) + i, lines[i].colour, false);
            } else {
                for (size_t i = 0; i < lines.size(); ++i) add(lines[i].text, i, lines[i].colour, false);
            }
        }
        for (const TextQuad& q : textQuads) {
            // as far as it has typed (MenuText: the characters shown so far, the rest cut off)
            float tw = q.w;
            if (q.from >= 0 && !q.pen.empty()) {
                const int n = std::clamp(ev->revealed() - q.from, 0, int(q.pen.size()) - 1);
                if (n == 0) continue;
                tw = std::min(q.w, q.pen[size_t(n)]);
            }
            const float u1 = q.w > 0 ? tw / q.w : 1;
            const float x0 = q.x, x1 = q.x + tw, y0 = q.y - q.h / 2, y1 = q.y + q.h / 2; // (rows top first: v 0 at the top)
            const float v[24] = {x0, y0, 0, 1, x1, y0, u1, 1, x1, y1, u1, 0, x0, y0, 0, 1, x1, y1, u1, 0, x0, y1, 0, 0};
            const float rgba[4] = {q.rgb[0], q.rgb[1], q.rgb[2], 1};
            dev->drawQuad(q.tex, v, proj.data(), rgba);
        }
        // the page cursor, looping, once the page is typed
        float cx, cy;
        ViewFigure& cursor = wl.cursorJoint.compare(0, 16, "call_msgCsr_loop") == 0 ? conversationCursor : pageCursor;
        if (ev->typed() && !cursor.meshes.empty() && !wl.cursorJoint.empty() && windowJoint(wl, wl.cursorJoint, cx, cy)) {
            cursor.place = cr2::Mat34();
            cursor.place.m[3] = cx, cursor.place.m[7] = cy;
            pose(cursor, time);
            drawFigure(cursor, view.data());
        }
    }

    // The cut-ins' portraits (CutInWindowMenu): `cutIn`, its first material's colour map the face
    // (SetTexture), its DiffuseColor the brightness (ChangeDiffuseColor), posed by the layout's io
    // animation; the other side's joints hidden (GEO_face*_L or _R); on screens wider than 3:2
    // moved toward its side by the margin's width over the 3:2 area's (PositionCorrection: the fit
    // parameter's side - 3 / 6 right, 1 / 4 left - the two-sided layout not moved), narrower ones
    // 0.0403125 lower. Behind the conversation window (their depths: -0.064 against -0.048).
    std::array<ViewFigure, cr2::EventRuntime::kCutIns> cutInFigures;

    void loadCutIns() {
        static const char* kAnim[cr2::EventRuntime::kCutIns] = {"cutinr_io.anim", "cutinlr_io.anim", "cutinl_io.anim",
                                                                "cutinpty_io.anim", "cutinsgl_io.anim"};
        static const char* kHidden[cr2::EventRuntime::kCutIns] = {"_L", "", "_R", "_L", "_R"};
        std::vector<uint8_t> d;
        const std::vector<std::string> joints = archive.read("cutin.name", d) ? cr2::parseJointNames(d) : std::vector<std::string>();
        const float keep = time;
        for (int k = 0; k < cr2::EventRuntime::kCutIns; ++k) {
            ViewFigure& f = cutInFigures[size_t(k)];
            std::string err;
            if (!loadFigure("cutin.geom", f, &err)) {
                SDL_Log("cut-in: %s", err.c_str());
                continue;
            }
            const auto anim = std::find(f.anims.begin(), f.anims.end(), kAnim[k]);
            if (anim != f.anims.end()) loadAnim(f, size_t(anim - f.anims.begin()));
            for (ViewMesh& vm : f.meshes) {
                const uint32_t j = vm.source.palette.empty() ? ~0u : vm.source.palette[0];
                const std::string& name = j < joints.size() ? joints[j] : std::string();
                vm.hidden = *kHidden[k] && name.size() > 2 && name.compare(name.size() - 2, 2, kHidden[k]) == 0;
            }
        }
        time = keep;
    }

    void drawCutIns(int w, int h) {
        cr2::EventRuntime* ev = activeEvents();
        if (!ev) return;
        static const float kSide[cr2::EventRuntime::kCutIns] = {1, 0, -1, 1, -1};
        const float aspect = float(w) / float(std::max(h, 1));
        const Mat4 proj = uiProjection(w, h);
        Mat4 camera = cr2::mat4Identity();
        camera[14] = -0.881f;
        const Mat4 view = cr2::mat4Multiply(proj, camera);
        for (int k = 0; k < cr2::EventRuntime::kCutIns; ++k) {
            const cr2::EventRuntime::Portrait& c = ev->cutIn(k);
            ViewFigure& f = cutInFigures[size_t(k)];
            if (!c.open || f.meshes.empty()) continue;
            const float t = c.closing ? std::max(c.time - eventLead(), 0.0f) : std::min(c.time + eventLead(), cr2::EventRuntime::kCutInSeconds);
            f.place = cr2::Mat34();
            if (aspect < 1.5f) f.place.m[7] = -0.0403125f;
            else {
                const float s = std::min(float(w) / 960.0f, float(h) / 640.0f);
                f.place.m[3] = kSide[k] * float(int((float(w) - s * 960.0f) * 0.5f)) / (s * 960.0f);
            }
            pose(f, t, false);
            cr2::Texture* face = texture(ev->cutInImage(c.texture));
            for (ViewMesh& vm : f.meshes) {
                vm.texture = face ? face : texture(vm.baseMaterial.diffuse());
                std::vector<float>& dc = vm.material.params[0x33];
                if (dc.size() < 4) dc.resize(4, 1.0f);
                dc[0] = dc[1] = dc[2] = c.bright;
            }
            cr2::SceneLighting l;
            dev->setLighting(l);
            drawFigure(f, view.data());
        }
    }

    // The balloons (FldBalloon: the `balloon` figure, looping), drawn after the foreground through
    // the interface's camera: each over its gimmick - its height up - where the field's camera
    // shows that (Cr3UtilSetBalloonPos: projected, then back through the interface's camera)
    Mat4 fieldScene = cr2::mat4Identity();
    ViewFigure balloonFigure;
    bool balloonLoaded = false;
    // The battle's interface (BtlInterface): an interface figure by name, posed by one of its
    // animations; its joints placed
    struct UiFig {
        ViewFigure fig;
        std::vector<std::string> joints;
        bool loaded = false;
    };
    std::map<std::string, UiFig> uiFigs;
    UiFig& uiFig(const std::string& name) {
        UiFig& u = uiFigs[name];
        if (!u.loaded) {
            u.loaded = true;
            std::string err;
            const float keep = time;
            if (!loadFigure(name + ".geom", u.fig, &err)) SDL_Log("ui %s: %s", name.c_str(), err.c_str());
            time = keep;
            std::vector<uint8_t> d;
            if (archive.read(name + ".name", d)) u.joints = cr2::parseJointNames(d);
        }
        return u;
    }
    void poseUi(UiFig& u, const std::string& anim, float t, bool loop = false) {
        // (the figure's own animation - SetParameterDataBase's, alone in slot 0 - is absolute;
        // the animators added beside it relative)
        u.fig.absolute = anim + ".geom" == u.fig.name;
        const auto it = std::find(u.fig.anims.begin(), u.fig.anims.end(), anim + ".anim");
        if (it != u.fig.anims.end() && (!u.fig.animLoaded || u.fig.anims[u.fig.animIndex] != *it)) {
            const float keep = time;
            loadAnim(u.fig, size_t(it - u.fig.anims.begin()));
            time = keep;
        }
        pose(u.fig, t, loop);
    }
    // a posed joint's place in UI units (false: none)
    bool uiJoint(const UiFig& u, const std::string& joint, float& x, float& y) {
        const auto it = std::find(u.joints.begin(), u.joints.end(), joint);
        if (it == u.joints.end() || !u.fig.skinned) return false;
        std::vector<cr2::JointTransform> p = u.fig.skeleton.basePose;
        if (u.fig.animLoaded) p = u.fig.anim.poseOver(u.fig.skeleton.basePose, 0);
        const std::vector<cr2::Mat34> w = u.fig.skeleton.world(p, u.fig.place);
        const size_t j = size_t(it - u.joints.begin());
        if (j >= w.size()) return false;
        x = w[j].m[3], y = w[j].m[7];
        return true;
    }
    // DotNumId2: a number's digits from hud_dotnum (DotNumParamPackTable: set `set`'s glyph d at
    // set + d, its size w x h pixels of a 1000-a-unit layout and its UVs), right to left from
    // (x, y), `digits` places (the leading ones blank, but a 0)
    struct Glyph {
        float w, h, u0, v0, u1, v1;
    };
    std::vector<Glyph> dotNumGlyphs;
    void loadDotNum() {
        if (!dotNumGlyphs.empty()) return;
        // (the table's entries this port uses: the sets at 20 .. 69 - 48 x 48, ten digits a row -
        // and 90 - 24 x 24 - of hud_dotnum's 512 x 512)
        dotNumGlyphs.assign(170, Glyph{0, 0, 0, 0, 0, 0});
        for (int set = 20; set <= 60; set += 10)
            for (int d = 0; d < 10; ++d) {
                const float y0 = float(set - 20) / 10.0f * 47.8f;
                dotNumGlyphs[size_t(set + d)] = {48, 48, d * 47.8f / 512.0f, 1.0f - y0 / 512.0f, (d + 1) * 47.8f / 512.0f, 1.0f - (y0 + 47.8f) / 512.0f};
            }
        for (int d = 0; d < 10; ++d) dotNumGlyphs[size_t(90 + d)] = {24, 24, d * 24.0f / 512.0f, 0.281f, (d + 1) * 24.0f / 512.0f, 0.234f};
    }
    void drawDigits(int value, int set, int digits, float x, float y, const Mat4& proj, float alpha = 1, float scale = 1) {
        loadDotNum();
        cr2::Texture* tex = texture("hud_dotnum", false);
        if (!tex) return;
        int place = 1;
        for (int i = 0; i < digits; ++i, place *= 10) {
            const int d = (i == 0 && value == 0) ? 0 : value < place ? -1 : value / place % 10; // (Cr3UtilGetNumberDisplay)
            if (d < 0) continue;
            const Glyph& g = dotNumGlyphs[size_t(set + d)];
            const float w = g.w / 1000.0f * scale, h = g.h / 1000.0f * scale;
            const float cx = x - float(i) * w;
            const float x0 = cx - w / 2, x1 = cx + w / 2, y0 = y - h / 2, y1 = y + h / 2;
            const float v[24] = {x0, y0, g.u0, g.v1, x1, y0, g.u1, g.v1, x1, y1, g.u1, g.v0,
                                 x0, y0, g.u0, g.v1, x1, y1, g.u1, g.v0, x0, y1, g.u0, g.v0};
            const float rgba[4] = {1, 1, 1, alpha};
            dev->drawQuad(*tex, v, proj.data(), rgba);
        }
    }
    // a line of text at a UI place (centred), made once
    std::map<std::string, TextQuad> uiTexts;
    void drawUiText(const std::string& text, float size, float x, float y, int w, int h) {
        if (text.empty() || !font.loaded()) return;
        const float k = std::min(float(w) / 0.96f, float(h) / 0.64f);
        const int px = std::max(1, int(size / 1000.0f * k + 0.5f));
        const std::string key = text + "\n" + std::to_string(px);
        auto it = uiTexts.find(key);
        if (it == uiTexts.end()) {
            TextQuad q;
            int tw = 0, th = 0;
            cr2::Image img = font.render(text, px, tw, th);
            if (img.rgba.empty()) return;
            q.tex = dev->upload(img);
            const float unitsPerPx = size / 1000.0f / float(px);
            q.w = float(tw) * unitsPerPx, q.h = float(th) * unitsPerPx;
            it = uiTexts.emplace(key, q).first;
        }
        const TextQuad& q = it->second;
        const float x0 = x - q.w / 2, x1 = x + q.w / 2, y0 = y - q.h / 2, y1 = y + q.h / 2;
        const float v[24] = {x0, y0, 0, 1, x1, y0, 1, 1, x1, y1, 1, 0, x0, y0, 0, 1, x1, y1, 1, 0, x0, y1, 0, 0};
        const float rgba[4] = {1, 1, 1, 1};
        dev->drawQuad(q.tex, v, uiProjection(w, h).data(), rgba);
    }

    // The battle's HUD: the party's windows (BattleStatusMenu: pc01win / pc02win, their HP and MP
    // bars - btlHpBar_per posed at HP x 5 / max seconds, at least 0.07 - the face, the numbers:
    // pc0NhpNum's call_dotNum_num1, five digits of set 90), the break gauge (BreakWindowMenu:
    // brkWin, brkGauge at (gauge + 4) / 2 seconds - 2 even -, brkInfo's EV / PL / EN), the
    // command's name (btlMsg, typeM_C01) and the damage (DamageInfo00: btlNum, btlNum_fgrN for N
    // digits, a digit of set 20 at each call_dotNum_idNN, at the target's DummyDP01)
    Mat4 battleVP = cr2::mat4Identity();
    struct DamageShown {
        int value = 0, set = 20;
        float x = 0, y = 0, start = 0;
    };
    std::vector<DamageShown> damageShown;
    std::string commandShown;
    float commandShownStart = -10;
    void drawBattleHud(int w, int h) {
        if (!battleMode || !battleRules) return;
        if (battleScript && !battleScript->statusShown()) return; // (its talk on)
        const Mat4 proj = uiProjection(w, h);
        Mat4 camera = cr2::mat4Identity();
        camera[14] = -0.881f;
        const Mat4 view = cr2::mat4Multiply(proj, camera);
        cr2::SceneLighting l;
        dev->setLighting(l);
        for (int k = 0; k < 2; ++k) {
            const cr2::Fighter& f = battleRules->fighter(k);
            if (!f.present) continue;
            const std::string n = k == 0 ? "pc01" : "pc02";
            UiFig& win = uiFig(n + "win");
            win.fig.place = cr2::Mat34();
            poseUi(win, n + "win", 0);
            drawFigure(win.fig, view.data());
            auto child = [&](const std::string& joint, const std::string& figure, float t, bool loop) -> UiFig* {
                float x, y;
                if (!uiJoint(win, joint, x, y)) return nullptr;
                UiFig& c = uiFig(figure);
                c.fig.place = cr2::Mat34();
                c.fig.place.m[3] = x, c.fig.place.m[7] = y + 1.0f / float(std::max(h, 1)); // (+ a pixel)
                poseUi(c, figure, t, loop);
                return &c;
            };
            const float hpT = f.hp > 0 ? std::max(float(f.hp) * 5.0f / float(std::max(f.maxHp, 1)), 0.07f) : 0.0f;
            const float mpT = f.maxMp > 0 ? float(f.mp) * 5.0f / float(f.maxMp) : 0.0f;
            if (UiFig* c = child("call_btlHpBar_per", "btlhpbar_per", hpT, false)) drawFigure(c->fig, view.data());
            if (UiFig* c = child("call_btlMpBar_per", "btlmpbar_per", mpT, false)) drawFigure(c->fig, view.data());
            if (UiFig* c = child("call_btlFace", "btlface", time, true)) {
                const std::string id = std::to_string(f.id);
                for (ViewMesh& vm : c->fig.meshes) {
                    vm.texture = texture("btlFaceC00" + id);
                    vm.extraMap = texture("btlFaceA00" + id);
                }
                drawFigure(c->fig, view.data());
            }
            // the labels (StatusId2: hud_statusAI by StatusIdParamPackTable, the id the joint's
            // number - call_status_fr01 "HP", fr02 "MP")
            static const float kStatus[3][6] = {{0, 0, 0, 0, 0, 0}, {40, 24, 0.0f, 1.0f, 0.156f, 0.906f}, {40, 24, 0.0f, 0.906f, 0.156f, 0.813f}};
            for (int id = 1; id <= 2; ++id) {
                float x, y;
                char jn[24];
                std::snprintf(jn, sizeof jn, "call_status_fr%02d", id);
                cr2::Texture* tex = texture("hud_statusAI", false);
                if (!tex || !uiJoint(win, jn, x, y)) continue;
                const float* g = kStatus[id];
                const float x0 = x, x1 = x + g[0] / 1000.0f, y0 = y - g[1] / 2000.0f, y1 = y + g[1] / 2000.0f;
                const float v[24] = {x0, y0, g[2], g[5], x1, y0, g[4], g[5], x1, y1, g[4], g[3],
                                     x0, y0, g[2], g[5], x1, y1, g[4], g[3], x0, y1, g[2], g[3]};
                const float rgba[4] = {1, 1, 1, 1};
                dev->drawQuad(*tex, v, proj.data(), rgba);
            }
            for (const char* what : {"hpNum", "mpNum"}) {
                UiFig& num = uiFig(n + (std::string(what) == "hpNum" ? "hpnum" : "mpnum"));
                num.fig.place = cr2::Mat34();
                float x, y;
                if (uiJoint(num, "call_dotNum_num1", x, y))
                    drawDigits(std::string(what) == "hpNum" ? f.hp : f.mp, 90, std::string(what) == "hpNum" ? 5 : 4, x, y, proj);
            }
        }
        // the break gauge
        UiFig& bw = uiFig("brkwin");
        bw.fig.place = cr2::Mat34();
        poseUi(bw, "brkwin", 0);
        drawFigure(bw.fig, view.data());
        UiFig& bg = uiFig("brkgauge");
        bg.fig.place = cr2::Mat34();
        poseUi(bg, "brkgauge", float(battleRules->gauge() + 4) * 0.5f);
        drawFigure(bg.fig, view.data());
        UiFig& bi = uiFig("brkinfo");
        bi.fig.place = cr2::Mat34();
        const int g = battleRules->gauge();
        poseUi(bi, g > 0 ? "brkinfopl_io" : g < 0 ? "brkinfoen_io" : "brkinfoev_io", 5.0f);
        drawFigure(bi.fig, view.data());
        // the command's name
        if (!commandShown.empty() && battleClock - commandShownStart < 3.0f) {
            UiFig& m = uiFig("btlmsg");
            m.fig.place = cr2::Mat34();
            poseUi(m, "btlmsg_in", battleClock - commandShownStart);
            drawFigure(m.fig, view.data());
            float x, y;
            if (uiJoint(m, "typeM_C01", x, y)) drawUiText(commandShown, 28, x, y, w, h);
        }
        // the damage
        for (const DamageShown& d : damageShown) {
            const float age = battleClock - d.start;
            if (age < 0 || age > 1.2f) continue;
            int digits = 1;
            for (int v = d.value; v >= 10; v /= 10) ++digits;
            UiFig& bn = uiFig("btlnum");
            bn.fig.place = cr2::Mat34();
            bn.fig.place.m[3] = d.x, bn.fig.place.m[7] = d.y;
            poseUi(bn, "btlnum_fgr" + std::to_string(std::min(digits, 5)), age);
            for (int i = 0; i < digits && i < 5; ++i) {
                float x, y;
                char jn[24];
                std::snprintf(jn, sizeof jn, "call_dotNum_id%02d", i + 1);
                if (!uiJoint(bn, jn, x, y)) continue;
                int place = 1; // (id01 the ones)
                for (int k = 0; k < i; ++k) place *= 10;
                drawDigits(d.value / place % 10, d.set, 1, x, y, proj);
            }
        }
    }

    void drawBalloons(int w, int h) {
        if (!fieldMode || eventMode || balloons.empty()) return;
        if (!balloonLoaded) {
            balloonLoaded = true;
            std::string err;
            const float keep = time;
            if (!loadFigure("balloon.geom", balloonFigure, &err)) SDL_Log("balloon: %s", err.c_str());
            time = keep;
        }
        if (balloonFigure.meshes.empty()) return;
        const float k = std::min(float(w) / 0.96f, float(h) / 0.64f);
        const Mat4 proj = uiProjection(w, h);
        Mat4 camera = cr2::mat4Identity();
        camera[14] = -0.881f;
        const Mat4 view = cr2::mat4Multiply(proj, camera);
        cr2::SceneLighting l;
        dev->setLighting(l);
        for (size_t i : balloons) {
            const cr2::Gimmick& g = field.gimmicks()[i];
            const float p[3] = {g.position[0], g.position[1] + g.balloonHeight(), g.position[2]};
            const float* m = fieldScene.data();
            const float cw = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
            if (cw <= 0) continue;
            const float nx = (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]) / cw;
            const float ny = (m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]) / cw;
            balloonFigure.place = cr2::Mat34();
            balloonFigure.place.m[3] = nx * float(w) * 0.5f / k, balloonFigure.place.m[7] = ny * float(h) * 0.5f / k;
            pose(balloonFigure, time);
            drawFigure(balloonFigure, view.data());
        }
    }

    // the event's UI, over the resolved scene: the fade, then the windows over it (their depths
    // in front of the fade windows')
    void drawEventUi() {
        drawBalloons(screenW, screenH);
        drawBattleHud(screenW, screenH);
        cr2::EventRuntime* ev = activeEvents();
        if (!ev || !eventDrawingReady) return;
        const cr2::EventFade& fd = ev->fade();
        const float alpha = fd.at(eventLead() / cr2::EventRuntime::kTick);
        if (alpha > 0 && white.id) {
            static const float kScreen[24] = {-1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1, -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
            const Mat4 id = cr2::mat4Identity();
            const float* c = fadeColours[std::clamp(fd.colour, 0, 3)];
            const float rgba[4] = {c[0], c[1], c[2], alpha};
            dev->drawQuad(white, kScreen, id.data(), rgba);
        }
        const bool message = ev->messageShown() && ev->page() < int(ev->message().pages.size());
        if (ev->windowOpen(cr2::EventRuntime::Vista))
            drawWindow(vistaWindow, message && ev->messageWindow() == cr2::EventRuntime::Vista, screenW, screenH);
        drawCutIns(screenW, screenH);
        if (ev->windowOpen(cr2::EventRuntime::CutIn) || ev->windowClosing(cr2::EventRuntime::CutIn))
            drawWindow(conversationWindow, message && ev->messageWindow() == cr2::EventRuntime::CutIn, screenW, screenH);
        if (ev->windowOpen(cr2::EventRuntime::Narration))
            drawWindow(narrationWindow, message && ev->messageWindow() == cr2::EventRuntime::Narration, screenW, screenH);
    }
};

bool saveBmp(const char* path, const std::vector<uint8_t>& rgba, int w, int h) {
    SDL_Surface* s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ABGR8888);
    if (!s) return false;
    for (int y = 0; y < h; ++y) std::memcpy(static_cast<uint8_t*>(s->pixels) + size_t(y) * s->pitch, &rgba[size_t(y) * w * 4], size_t(w) * 4);
    const bool ok = SDL_SaveBMP(s, path);
    SDL_DestroySurface(s);
    return ok;
}

// The output resolutions F1 steps through: the window's own size, then 16:9 up to 3840 x 2160
const int kResolutions[][2] = {{0, 0}, {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3200, 1800}, {3840, 2160}};
constexpr int kMaxResW = 3840, kMaxResH = 2160;

// FSR 3.1's quality modes (the output / render ratio per axis; native AA renders at full size)
struct Quality {
    const char* name;
    float ratio;
};
const Quality kQualities[] = {{"native", 1.0f}, {"quality", 1.5f}, {"balanced", 1.7f}, {"performance", 2.0f}, {"ultra", 3.0f}};

std::string upscaleName(const cr2::UpscaleSettings& u) {
    if (u.mode == cr2::Upscaler::Off) return "off";
    std::string q = "x" + std::to_string(u.ratio);
    for (const Quality& k : kQualities)
        if (k.ratio == u.ratio) q = k.name;
    return std::string(u.mode == cr2::Upscaler::Fsr3 ? "FSR 3.1 " : "DLSS ") + q;
}

} // namespace

int main(int argc, char** argv) {
    std::string data, model, capture, animName, renderer = "vulkan", lights = "d0101c", fieldName, at = "defaultPos", binary;
    float walkX = 0, walkY = 0, walkSeconds = 0; // --walk: the field's character runs first (captures)
    bool tapAfter = false;
    std::string eventName, vista = "null", fontPath, playFrom, callEntry = "evt000";
    int fightMode = 0;
    bool skipBattlesOpt = false;
    uint32_t fightSeed = 1;
    int battleId = 0, battleParty[2] = {1, 2}; // (--battle: Darwin and Orlando unless --party)
    float page = 3.0f;
    int width = 0, height = 0;    // the window (--size; else the resolution's, fitted to the display)
    int resW = 0, resH = 0;       // --resolution: the output frames (else the window's)
    std::string upscaler = "off", quality = "quality";
    float sharpness = 0.3f;
    float runSeconds = 0; // --run: a capture plays this long first, 60 frames a second
    int fps = 60;         // --fps: the frame rate the window aims at (0: as fast as the display shows them)
    float yaw = 30, pitch = 15, startTime = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const char* next = i + 1 < argc ? argv[i + 1] : nullptr;
        if (a == "--data" && next) data = next, ++i;
        else if (a == "--model" && next) model = next, ++i;
        else if (a == "--capture" && next) capture = next, ++i;
        else if (a == "--anim" && next) animName = next, ++i;
        else if (a == "--time" && next) startTime = float(std::atof(next)), ++i;
        else if (a == "--size" && next) std::sscanf(next, "%dx%d", &width, &height), ++i;
        else if (a == "--yaw" && next) yaw = float(std::atof(next)), ++i;
        else if (a == "--pitch" && next) pitch = float(std::atof(next)), ++i;
        else if (a == "--renderer" && next) renderer = next, ++i;
        else if (a == "--lights" && next) lights = next, ++i;
        else if (a == "--field" && next) fieldName = next, ++i;
        else if (a == "--at" && next) at = next, ++i;
        else if (a == "--binary" && next) binary = next, ++i;
        else if (a == "--walk" && next) std::sscanf(next, "%f,%f,%f", &walkX, &walkY, &walkSeconds), ++i;
        else if (a == "--event" && next) eventName = next, ++i;
        else if (a == "--battle" && next) battleId = std::atoi(next), ++i;
        else if (a == "--skip-battles") skipBattlesOpt = true;
        else if (a == "--fight" && next) fightMode = !std::strcmp(next, "pair") ? 2 : 1, ++i;
        else if (a == "--seed" && next) fightSeed = uint32_t(std::atoi(next)), ++i;
        else if (a == "--party" && next) std::sscanf(next, "%d,%d", &battleParty[0], &battleParty[1]), ++i;
        else if (a == "--vista" && next) vista = next, ++i;
        else if (a == "--page" && next) page = float(std::atof(next)), ++i;
        else if (a == "--font" && next) fontPath = next, ++i;
        else if (a == "--tap") tapAfter = true;
        else if (a == "--call" && next) callEntry = argv[++i]; // (--play: the map's event function, not its evt000)
        else if (a == "--play") playFrom = next && next[0] != '-' ? (++i, next) : "d1020";
        else if (a == "--resolution" && next) std::sscanf(next, "%dx%d", &resW, &resH), ++i;
        else if (a == "--upscaler" && next) upscaler = next, ++i;
        else if (a == "--quality" && next) quality = next, ++i;
        else if (a == "--fps" && next) fps = std::clamp(std::atoi(next), 0, 1000), ++i;
        else if (a == "--run" && next) runSeconds = float(std::atof(next)), ++i;
        else if (a == "--sharpness" && next) sharpness = std::clamp(float(std::atof(next)), 0.0f, 1.0f), ++i;
    }
    if (resW || resH) {
        if (resW < 320 || resH < 180 || resW > kMaxResW || resH > kMaxResH)
            return std::fprintf(stderr, "--resolution: 320x180 up to %dx%d\n", kMaxResW, kMaxResH), 2;
    }
    cr2::UpscaleSettings upscale;
    upscale.sharpness = sharpness;
    if (upscaler == "fsr3" || upscaler == "fsr") upscale.mode = cr2::Upscaler::Fsr3;
    else if (upscaler == "dlss") upscale.mode = cr2::Upscaler::Dlss;
    else if (upscaler != "off") return std::fprintf(stderr, "--upscaler: off, fsr3 or dlss\n"), 2;
    {
        const auto q = std::find_if(std::begin(kQualities), std::end(kQualities), [&](const Quality& k) { return quality == k.name; });
        if (q == std::end(kQualities)) return std::fprintf(stderr, "--quality: native, quality, balanced, performance or ultra\n"), 2;
        upscale.ratio = q->ratio;
    }
    if (data.empty()) { // (CR2_DATA, else a copy beside the executable: vita/, android/, ipad/)
        const char* base = SDL_GetBasePath();
        data = cr2::defaultArchive(base ? base : "");
    }
    data = cr2::resolveArchive(data); // (a folder: the OBB or the CRDB.*.mvgl in it, a Vita copy's PCSG00498 ...)
    if (data.empty()) {
        std::fprintf(stderr, "cr2view --data <archive> [--model name] [--anim name] [--time s] [--size WxH] [--yaw deg] "
                             "[--pitch deg] [--lights figure|none] [--field map [--at point] [--binary file]] "
                             "[--event function [--vista map] [--page s]] [--resolution WxH] [--upscaler off|fsr3|dlss "
                             "[--quality native|quality|balanced|performance|ultra] [--sharpness 0..1]] "
                             "[--renderer vulkan|opengl] [--capture out.bmp]\n");
        return 2;
    }
    Viewer v;
    std::string err;
    if (!v.archive.open(data, &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    for (const auto& e : v.archive.entries())
        if (e.name.size() > 5 && e.name.compare(e.name.size() - 5, 5, ".geom") == 0) v.models.push_back(e.name);
    std::sort(v.models.begin(), v.models.end());
    if (v.models.empty()) return std::fprintf(stderr, "no models in %s\n", data.c_str()), 1;
    if (model.empty() && (!fieldName.empty() || !playFrom.empty())) model = "c001"; // (the leader: Darwin)
    size_t start = 0;
    if (!model.empty()) {
        const std::string want = model.size() > 5 && model.compare(model.size() - 5, 5, ".geom") == 0 ? model : model + ".geom";
        const auto it = std::find(v.models.begin(), v.models.end(), want);
        if (it == v.models.end()) return std::fprintf(stderr, "%s: not in the archive\n", want.c_str()), 1;
        start = size_t(it - v.models.begin());
    }
    v.yaw = yaw, v.pitch = pitch;

    if (!SDL_Init(SDL_INIT_VIDEO)) return std::fprintf(stderr, "SDL: %s\n", SDL_GetError()), 1;
    // The window: --size, else a capture's the resolution itself, else the resolution's shape
    // fitted to 90% of the display (the frames stay the resolution, scaled into it)
    if (!width || !height) {
        width = resW ? resW : 1280, height = resW ? resH : 800;
        SDL_Rect usable{};
        if (capture.empty() && SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) && usable.w > 0 && usable.h > 0) {
            const float fit = std::min({1.0f, 0.9f * float(usable.w) / float(width), 0.9f * float(usable.h) / float(height)});
            width = std::max(int(float(width) * fit), 320), height = std::max(int(float(height) * fit), 180);
        }
    }
    // The device: Vulkan unless asked otherwise or it can't start, then OpenGL
    std::unique_ptr<cr2::RenderDevice> dev;
    SDL_Window* win = nullptr;
    const SDL_WindowFlags shown = capture.empty() ? SDL_WINDOW_RESIZABLE : SDL_WINDOW_HIDDEN;
    for (const cr2::Backend b : {cr2::Backend::Vulkan, cr2::Backend::OpenGL}) {
        if (b == cr2::Backend::Vulkan && renderer != "vulkan") continue;
        std::unique_ptr<cr2::RenderDevice> d = b == cr2::Backend::Vulkan ? cr2::makeVkDevice() : cr2::makeGlDevice();
        if (!d) continue;
        win = SDL_CreateWindow("CR2-U viewer", width, height, SDL_WindowFlags(d->windowFlags()) | shown);
        if (win && d->init(win)) {
            dev = std::move(d);
            SDL_Log("renderer: %s", dev->description().c_str());
            break;
        }
        SDL_Log("%s: could not start (%s)%s", cr2::backendName(b), SDL_GetError(), b == cr2::Backend::Vulkan ? ", trying OpenGL" : "");
        if (d) d->shutdown();
        if (win) SDL_DestroyWindow(win);
        win = nullptr;
    }
    if (!dev) return std::fprintf(stderr, "no renderer could start\n"), 1;
    v.dev = dev.get();
    v.resW = resW, v.resH = resH;
    if (upscale.mode != cr2::Upscaler::Off) {
        const std::string why = dev->upscalerProblem(upscale.mode);
        if (!why.empty()) SDL_Log("upscaler %s: %s - off", upscaler.c_str(), why.c_str()), upscale.mode = cr2::Upscaler::Off;
    }
    v.upscale = upscale;
    dev->setUpscaler(upscale);
    v.shaders = std::make_unique<cr2::ShaderLibrary>(v.archive);
    if (!playFrom.empty()) fieldName = playFrom, v.playMode = true;
    v.skipBattles = skipBattlesOpt; // (a new game: every flag clear)
    if (!fieldName.empty() || !eventName.empty() || battleId > 0) {
        // the font: --font, CR2_FONT, the Vita's beside its archive, the Android release's APK beside
        // its OBB (assets/00001: the Regular face it draws English in), else the system's
        std::string dataDir = data.substr(0, data.find_last_of("/\\") == std::string::npos ? 0 : data.find_last_of("/\\") + 1);
        const char* envFont = SDL_getenv("CR2_FONT");
        std::string fp = cr2::TextRasterizer::firstFont({fontPath, envFont ? envFont : "", dataDir + "font/TBGoPro-Bold.otf"});
        std::vector<uint8_t> apkFont;
        std::string apkFontFrom;
        if (fp.empty() && !cr2::androidFile(data, "assets/00001", apkFont, &apkFontFrom))
            fp = cr2::TextRasterizer::firstFont({"/usr/share/fonts/noto/NotoSans-Regular.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
                                                 "/usr/share/fonts/liberation/LiberationSans-Regular.ttf"});
        v.loadMessageWindow(fp, std::move(apkFont), apkFontFrom);
        v.initEventDrawing();
        v.pageSeconds = page;
    }
    if (!fieldName.empty()) {
        if (!v.loadField(fieldName, at, binary, data, callEntry)) return 1;
    } else if (lights != "none") {
        if (v.loadLights(lights)) SDL_Log("lights: %s", lights.c_str());
        else SDL_Log("lights: %s has none (the engine's defaults)", lights.c_str());
    }
    v.load(start);
    if (battleId > 0 && !v.startBattle(battleId, battleParty)) return 1;
    if (battleId > 0 && fightMode) v.startFight(fightMode, fightSeed);
    if (!eventName.empty()) {
        if (!v.startEvent(eventName, vista, page)) return 1;
        for (float t = 0; t + cr2::EventRuntime::kTick * 0.5f < startTime; t += cr2::EventRuntime::kTick) v.stepEvent();
        startTime = 0;
    }
    if (!animName.empty()) {
        const std::string want = animName.size() > 5 && animName.compare(animName.size() - 5, 5, ".anim") == 0 ? animName : animName + ".anim";
        const auto it = std::find(v.fig.anims.begin(), v.fig.anims.end(), want);
        if (it == v.fig.anims.end()) return std::fprintf(stderr, "%s: not one of the figure's animations\n", want.c_str()), 1;
        v.loadAnim(v.fig, size_t(it - v.fig.anims.begin()));
    }
    if (v.fieldMode && animName.empty()) v.playMotion(v.fig, "fn01");
    if (v.playMode) { // --time: the game played that long first (60 frames a second), then --walk
        for (float t = 0; t + 0.5f / 60.0f < startTime; t += 1.0f / 60.0f) v.advance(1.0f / 60.0f, 0, 0, false);
        for (float t = 0; t < walkSeconds; t += 1.0f / 60.0f) v.advance(1.0f / 60.0f, walkX, walkY, true);
        if (tapAfter) v.tap(); // (--tap: then a tap, before --run)
        for (float t = 0; t < runSeconds; t += 1.0f / 60.0f) v.advance(1.0f / 60.0f, 0, 0, false); // (--run: on after the walk)
        runSeconds = 0;
        SDL_Log("play: after %.2f s (+ %.2f s walking) %s, %d battles skipped", startTime, walkSeconds,
                v.eventMode ? ("in the vista " + v.status).c_str() : ("on " + v.field.info().name).c_str(), v.battlesSkipped);
        startTime = 0;
    } else {
        for (float t = 0; v.fieldMode && t < walkSeconds; t += 1.0f / 60.0f) v.walk(1.0f / 60.0f, walkX, walkY, true);
    }
    if (v.fieldMode && walkSeconds > 0) SDL_Log("after --walk: %s at (%.3f %.3f %.3f)", v.field.info().name.c_str(), v.player[0], v.player[1], v.player[2]);
    v.time = startTime;
    v.pose();
    SDL_Log("%s%s", v.status.c_str(), v.animStatus().c_str());

    // a frame's size: the resolution where the device draws offscreen, else the window's
    auto frameSize = [&](int& w, int& h) {
        SDL_GetWindowSizeInPixels(win, &w, &h);
        if (dev->offscreen() && v.resW > 0 && v.resH > 0) w = v.resW, h = v.resH;
    };
    auto describe = [&]() {
        int w = 0, h = 0;
        frameSize(w, h);
        // (the device's sceneSize follows the mode it last ran: this is the one set)
        const float r = v.upscale.mode == cr2::Upscaler::Off ? 1.0f : std::clamp(v.upscale.ratio, 1.0f, 3.0f);
        const int rw = std::max(int(float(w) / r), 1), rh = std::max(int(float(h) / r), 1);
        char buf[160];
        std::snprintf(buf, sizeof buf, "%dx%d%s, upscaler %s (scene %dx%d)", w, h, v.resW ? "" : " (the window's)", upscaleName(v.upscale).c_str(), rw, rh);
        return std::string(buf);
    };
    SDL_Log("output: %s", describe().c_str());

    int code = 0;
    if (!capture.empty()) { // one frame to a file
        int w = 0, h = 0;
        frameSize(w, h);
        // (a temporal upscaler first gathers a still frame's history: its jitter's phases)
        const int warmup = v.upscale.mode != cr2::Upscaler::Off ? 32 : 0;
        for (int k = 0; k < warmup; ++k) {
            v.draw(w, h);
            dev->endFrame(false);
        }
        // --run: frames as the window would show them, the event and animations going on
        if (v.battleMode && !v.playMode) { // (--time: that far into the battle, played at 60 a second)
            for (float t = 0; t + 0.5f / 60.0f < startTime; t += 1.0f / 60.0f) v.advance(1.0f / 60.0f, 0, 0, false), v.pose();
        }
        for (int k = 0; k < int(runSeconds * 60.0f + 0.5f); ++k) {
            v.time += 1.0f / 60.0f;
            v.advance(1.0f / 60.0f, walkX, walkY, true); // (the field on in --walk's direction)
            v.pose();
            v.draw(w, h);
            dev->endFrame(false);
        }
        v.draw(w, h);
        dev->endFrame(false);
        std::vector<uint8_t> px;
        const bool ok = dev->readPixels(px, w, h) && saveBmp(capture.c_str(), px, w, h);
        SDL_Log("capture %s: %s", capture.c_str(), ok ? "written" : SDL_GetError());
        code = ok ? 0 : 1;
    } else {
        bool running = true, dragging = false;
        Uint64 last = SDL_GetTicksNS();
        // --fps: each frame starts no sooner than its slot (the display's vsync may hold it longer)
        const Uint64 period = fps > 0 ? Uint64(1000000000) / Uint64(fps) : 0;
        Uint64 nextFrame = last, fpsSince = last;
        int fpsFrames = 0, totalFrames = 0;
        float measured = 0;
        const Uint64 started = last;
        while (running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_EVENT_QUIT) running = false;
                else if (e.type == SDL_EVENT_KEY_DOWN) {
                    const SDL_Keycode k = e.key.key;
                    if (k == SDLK_ESCAPE) running = false;
                    if (k == SDLK_SPACE) v.paused = !v.paused;
                    // F1: the next resolution; F2: the upscaler's next mode (off, FSR 3.1 native
                    // AA .. ultra performance)
                    if (k == SDLK_F1) {
                        size_t i = 0;
                        while (i < std::size(kResolutions) && (kResolutions[i][0] != v.resW || kResolutions[i][1] != v.resH)) ++i;
                        i = (i + 1) % std::size(kResolutions);
                        v.resW = kResolutions[i][0], v.resH = kResolutions[i][1];
                        dev->resetHistory();
                        SDL_Log("output: %s", describe().c_str());
                        continue;
                    }
                    if (k == SDLK_F2) {
                        cr2::UpscaleSettings u = v.upscale;
                        size_t q = 0;
                        while (q < std::size(kQualities) && kQualities[q].ratio != u.ratio) ++q;
                        if (u.mode == cr2::Upscaler::Off) u.mode = cr2::Upscaler::Fsr3, u.ratio = kQualities[0].ratio;
                        else if (q + 1 < std::size(kQualities)) u.ratio = kQualities[q + 1].ratio;
                        else u.mode = cr2::Upscaler::Off;
                        const std::string why = dev->upscalerProblem(u.mode);
                        if (!why.empty()) SDL_Log("upscaler: %s", why.c_str()), u.mode = cr2::Upscaler::Off;
                        v.upscale = u;
                        dev->setUpscaler(u);
                        SDL_Log("output: %s", describe().c_str());
                        continue;
                    }
                    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) v.tap();
                    if (v.eventMode) continue;
                    ViewFigure& f = v.fig;
                    if (v.fieldMode) {
                        if ((k == SDLK_PAGEUP || k == SDLK_PAGEDOWN) && !f.anims.empty())
                            v.loadAnim(f, (f.animIndex + (k == SDLK_PAGEDOWN ? 1 : f.anims.size() - 1)) % f.anims.size());
                        continue;
                    }
                    size_t to = v.current;
                    if (k == SDLK_RIGHT || k == SDLK_PAGEDOWN) to = (v.current + (k == SDLK_PAGEDOWN ? 10 : 1)) % v.models.size();
                    else if (k == SDLK_LEFT || k == SDLK_PAGEUP)
                        to = (v.current + v.models.size() * 10 - (k == SDLK_PAGEUP ? 10 : 1)) % v.models.size();
                    if (to != v.current) v.load(to);
                    if ((k == SDLK_UP || k == SDLK_DOWN) && !f.anims.empty())
                        v.loadAnim(f, (f.animIndex + (k == SDLK_DOWN ? 1 : f.anims.size() - 1)) % f.anims.size());
                } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    dragging = true;
                    if (v.eventMode || v.fieldMode) v.tap();
                }
                else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) dragging = false;
                else if (e.type == SDL_EVENT_MOUSE_MOTION && dragging && !v.fieldMode) {
                    v.yaw -= e.motion.xrel * 0.4f;
                    v.pitch = std::clamp(v.pitch + e.motion.yrel * 0.4f, -89.0f, 89.0f);
                } else if (e.type == SDL_EVENT_MOUSE_WHEEL && !v.fieldMode) {
                    v.zoom = std::clamp(v.zoom * (e.wheel.y > 0 ? 0.9f : 1.1f), 0.05f, 20.0f);
                }
            }
            if (period) {
                const Uint64 t = SDL_GetTicksNS();
                if (t < nextFrame) SDL_DelayPrecise(nextFrame - t);
                nextFrame = std::max(nextFrame + period, SDL_GetTicksNS() - period); // (no catching up after a stall)
            }
            const Uint64 now = SDL_GetTicksNS();
            const float dt = float(double(now - last) / 1e9);
            ++fpsFrames, ++totalFrames;
            if (now - fpsSince >= 500000000) {
                measured = float(double(fpsFrames) * 1e9 / double(now - fpsSince));
                fpsFrames = 0, fpsSince = now;
            }
            if (!v.paused) v.time += dt;
            last = now;
            if (!v.paused && v.battleMode && !v.fieldMode && !v.eventMode) v.advance(dt, 0, 0, false);
            if (!v.paused && (v.eventMode || v.fieldMode)) { // the arrows / WASD run, with Shift walk
                const bool* keys = SDL_GetKeyboardState(nullptr);
                const float sx = float((keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) - (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]));
                const float sy = float((keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) - (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]));
                v.advance(dt, sx, sy, !(keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]));
            }
            if (v.fig.animLoaded && v.time > v.fig.anim.duration()) v.time = std::fmod(v.time, std::max(v.fig.anim.duration(), 0.05f));
            v.pose();
            SDL_SetWindowTitle(win, ("CR2-U viewer (" + std::string(cr2::backendName(dev->windowFlags() & SDL_WINDOW_VULKAN ? cr2::Backend::Vulkan : cr2::Backend::OpenGL)) + ", " +
                                     describe() + ", " + std::to_string(int(measured + 0.5f)) + " fps) - " + v.status + v.animStatus()).c_str());
            int w = 0, h = 0;
            frameSize(w, h);
            v.draw(w, h);
            dev->endFrame(true);
        }
        const double secs = double(SDL_GetTicksNS() - started) / 1e9;
        SDL_Log("%d frames in %.1f s: %.1f a second (aiming at %s)", totalFrames, secs, totalFrames / std::max(secs, 1e-3),
                fps ? std::to_string(fps).c_str() : "the display's rate");
    }
    v.unload(v.fig);
    v.unload(v.mapFigure);
    v.unload(v.vistaMap);
    v.unload(v.battleMap);
    for (auto& u : v.battleUnits) v.unload(u.fig);
    v.unload(v.vistaWindow.fig);
    v.unload(v.narrationWindow.fig);
    v.unload(v.conversationWindow.fig);
    v.unload(v.conversationCursor);
    for (ViewFigure& f : v.cutInFigures) v.unload(f);
    v.unload(v.pageCursor);
    for (auto& q : v.textQuads) dev->destroy(q.tex);
    for (ViewFigure& g : v.eventFigures) v.unload(g);
    if (v.white.id) dev->destroy(v.white);
    for (ViewFigure& g : v.gimmicks) v.unload(g);
    for (auto& [k, t] : v.textures) dev->destroy(t);
    dev->shutdown();
    SDL_DestroyWindow(win);
    SDL_Quit();
    return code;
}
