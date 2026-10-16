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
// found beside the iPad's or the Vita's archive, or given with --binary (Android's
// libChaosRings2.so).
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
// Vulkan by default, OpenGL where Vulkan can't start (or with --renderer opengl).
//
//   cr2view --data <CRDB.ios.mvgl | main.212...obb | CRDB.psp2.mvgl> [--model <name>]
//           [--anim <name>] [--time s] [--size WxH] [--yaw deg] [--pitch deg]
//           [--lights <figure> | none] [--field <map> [--at <point>] [--binary <file>]
//           [--walk sx,sy,seconds]] [--event <function> [--vista <map>] [--page s]]
//           [--resolution WxH] [--upscaler off|fsr3 [--quality q] [--sharpness s]] [--run s]
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
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "assets/geom.hpp"
#include "assets/image.hpp"
#include "assets/maptable.hpp"
#include "assets/mvgl.hpp"
#include "assets/skeleton.hpp"
#include "event/runtime.hpp"
#include "field/character.hpp"
#include "field/fieldmap.hpp"
#include "field/fieldscript.hpp"
#include "gfx/material.hpp"
#include "gfx/render_device.hpp"

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
    // the camera's view, walking (0.09 units a second) or running (0.48), the controller stepped
    // at the world's fixed 1/60 s; the figure stands 0.02 below the controller's sphere
    void walk(float dt, float sx, float sy, bool run) {
        if (!controller) return;
        cr2::Vec3 right, forward;
        field.groundAxes(right, forward);
        cr2::Vec3 dir{right[0] * sx + forward[0] * sy, 0, right[2] * sx + forward[2] * sy};
        const float l = std::sqrt(dir[0] * dir[0] + dir[2] * dir[2]);
        const float speed = l > 0 ? (run ? 0.48f : 0.09f) : 0.0f;
        if (l > 0) dir = {dir[0] / l * speed, 0, dir[2] / l * speed}, heading = std::atan2(dir[0], dir[2]);
        stepClock = std::min(stepClock + dt, 4.0f / 60.0f);
        for (; stepClock >= 1.0f / 60.0f; stepClock -= 1.0f / 60.0f) {
            controller->setVelocity(dir);
            controller->step();
        }
        const cr2::Vec3& p = controller->position();
        player[0] = p[0], player[1] = p[1] - 0.02f, player[2] = p[2];
        playMotion(fig, speed == 0 ? "fn01" : run ? "ff02" : "ff01");
        // an exit: the area changes to the map its pop point names (FldMain::ChangeAreaExecute)
        const std::string exit = field.exitTouched(p, controller->radius);
        if (exit.empty()) {
            exitsArmed = true;
        } else if (exitsArmed && exit.size() >= 5) {
            SDL_Log("exit %s", exit.c_str());
            enterMap(exit.substr(0, 5), exit);
        }
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
            if (m.material < g.materials.size()) {
                vm.material = vm.baseMaterial = g.materials[m.material];
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
        if (eventMode) drawEventUi();
    }

    void drawScene(int w, int h) {
        if (eventMode) {
            drawEvent(w, h);
            return;
        }
        if (fieldMode) {
            // FldMain's order: the background (and midground), the 3D figures, the foreground
            const cr2::FieldFrame fr = field.frame(w, h, player);
            dev->setCamera(fr.zNear, fr.zFar, fr.fovY * 3.14159265f / 180);
            cr2::SceneLighting l = field.lighting();
            std::memcpy(l.camera, fr.camera, sizeof l.camera);
            l.time = time;
            dev->setLighting(l);
            drawLayer(field.backgroundImage(), fr.background);
            drawLayer(field.midgroundImage(), fr.midground);
            drawFigure(mapFigure, fr.scene.data());
            for (const ViewFigure& g : gimmicks) drawFigure(g, fr.scene.data());
            drawFigure(fig, fr.scene.data());
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
        std::vector<std::string> candidates;
        if (!binary.empty()) candidates.push_back(binary);
        for (const std::string& c : cr2::mapTableCandidates(dataPath)) candidates.push_back(c);
        for (const std::string& c : candidates) {
            SDL_IOStream* io = SDL_IOFromFile(c.c_str(), "rb");
            if (!io) continue;
            std::vector<uint8_t> bytes(size_t(std::max<Sint64>(SDL_GetIOSize(io), 0)));
            const bool read = !bytes.empty() && SDL_ReadIO(io, bytes.data(), bytes.size()) == bytes.size();
            SDL_CloseIO(io);
            if (read && cr2::findMapTable(bytes, table)) {
                tableFrom = c;
                return true;
            }
        }
        SDL_Log("the maps' table: not found beside %s - give the game's executable with --binary", dataPath.c_str());
        return false;
    }

    // A map: its table row, layers and figures, the character dropped at the point `at`
    bool enterMap(const std::string& map, const std::string& at) {
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
        if (!script->enter(map, field.gimmicks())) SDL_Log("%s's script: %s", map.c_str(), script->error().c_str());
        field.setSolidGimmicks([this](const cr2::Gimmick& g) {
            const auto it = script->gimmicks().find(g.id);
            return script->visible(g) && (it == script->gimmicks().end() || it->second.collision);
        });
        // the gimmicks shown with a figure: placed by their joints, NPCs idling (FldNPC: <figure>fn01)
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
        if (!field.point(at, player)) {
            SDL_Log("%s: no point %s; the points:", map.c_str(), at.c_str());
            for (const std::string& n : field.pointNames()) SDL_Log("  %s", n.c_str());
        }
        fieldMode = true;
        exitsArmed = false;
        dev->resetHistory();
        controller = std::make_unique<cr2::CharacterController>(field.collision());
        controller->setPosition({player[0], player[1] + 0.17f, player[2]}); // (dropped onto the floor)
        for (int k = 0; k < 30; ++k) walk(1.0f / 60.0f, 0, 0, false);
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

    bool loadField(const std::string& map, const std::string& at, const std::string& binary, const std::string& dataPath) {
        return loadTable(binary, dataPath) && enterMap(map, at);
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
        std::string err;
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
        if (map != "null" && !loadFigure(map + ".geom", vistaMap, &err)) SDL_Log("vista %s: %s", map.c_str(), err.c_str());
        vistaMap.absolute = true; // (VistaMain::LoadMapData: slot 0)
        event = std::make_unique<cr2::EventRuntime>(archive, game);
        event->autoPage = page;
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
        }
        if (event->messageShown() != messageLogged) {
            messageLogged = event->messageShown();
            if (messageLogged) SDL_Log("frame %d: a message, %zu pages", event->frame(), event->message().pages.size());
        }
        std::vector<std::string> names;
        for (const cr2::EventModel& m : event->scene().models()) names.push_back(m.figure);
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

    void updateEvent(float dt) {
        eventClock = std::min(eventClock + dt, 0.25f);
        for (; eventClock >= cr2::EventRuntime::kTick; eventClock -= cr2::EventRuntime::kTick) stepEvent();
    }

    // the models where the scene has them, playing what the script chose (ChangeAnimator: their
    // <figure><suffix>; else the figure's own), their faces as ChangeModelFace left them
    void poseEvent() {
        pose(vistaMap, time);
        std::vector<cr2::EventModel>& models = event->scene().models();
        for (size_t i = 0; i < models.size() && i < eventFigures.size(); ++i) {
            ViewFigure& f = eventFigures[i];
            const cr2::EventModel& m = models[i];
            if (!m.visible || f.meshes.empty()) continue;
            const std::string want = (m.animation.empty() ? m.figure : m.animation) + ".anim";
            const auto it = std::find(f.anims.begin(), f.anims.end(), want);
            if (it != f.anims.end() && (!f.animLoaded || f.anims[f.animIndex] != want)) {
                const float keep = time;
                loadAnim(f, size_t(it - f.anims.begin()));
                time = keep;
            }
            // EvtModel::ChangeTexture_Face: the j<NNN>face material's colour texture
            const auto face = event->faces().find(m.face);
            const uint32_t faceMaterial = cr2::nameHash(("j" + m.figure.substr(1, 3) + "face").c_str());
            for (ViewMesh& vm : f.meshes)
                vm.texture = texture(face != event->faces().end() && vm.baseMaterial.hash == faceMaterial ? face->second : vm.baseMaterial.diffuse());
            f.place = m.place;
            pose(f, m.animTime, m.loop);
        }
    }

    void drawEvent(int w, int h) {
        cr2::Mat34 cw;
        float fov = 45, zn = 0.1f, zf = 100;
        if (event->sceneShown() && event->scene().camera(cw, fov, zn, zf)) {
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
            event->scene().lights(l);
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

    // the event's UI, over the resolved scene: the fade over it all
    void drawEventUi() {
        const cr2::EventFade& fd = event->fade();
        if (fd.alpha > 0 && white.id) {
            static const float kScreen[24] = {-1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1, -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
            const Mat4 id = cr2::mat4Identity();
            const float* c = fadeColours[std::clamp(fd.colour, 0, 3)];
            const float rgba[4] = {c[0], c[1], c[2], fd.alpha};
            dev->drawQuad(white, kScreen, id.data(), rgba);
        }
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
    std::string eventName, vista = "null";
    float page = 3.0f;
    int width = 0, height = 0;    // the window (--size; else the resolution's, fitted to the display)
    int resW = 0, resH = 0;       // --resolution: the output frames (else the window's)
    std::string upscaler = "off", quality = "quality";
    float sharpness = 0.3f;
    float runSeconds = 0; // --run: a capture plays this long first, 60 frames a second
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
        else if (a == "--vista" && next) vista = next, ++i;
        else if (a == "--page" && next) page = float(std::atof(next)), ++i;
        else if (a == "--resolution" && next) std::sscanf(next, "%dx%d", &resW, &resH), ++i;
        else if (a == "--upscaler" && next) upscaler = next, ++i;
        else if (a == "--quality" && next) quality = next, ++i;
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
    if (model.empty() && !fieldName.empty()) model = "c001";
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
    if (!fieldName.empty()) {
        if (!v.loadField(fieldName, at, binary, data)) return 1;
    } else if (lights != "none") {
        if (v.loadLights(lights)) SDL_Log("lights: %s", lights.c_str());
        else SDL_Log("lights: %s has none (the engine's defaults)", lights.c_str());
    }
    v.load(start);
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
    for (float t = 0; v.fieldMode && t < walkSeconds; t += 1.0f / 60.0f) v.walk(1.0f / 60.0f, walkX, walkY, true);
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
        for (int k = 0; k < int(runSeconds * 60.0f + 0.5f); ++k) {
            v.time += 1.0f / 60.0f;
            if (v.eventMode) v.updateEvent(1.0f / 60.0f);
            if (v.fieldMode) v.walk(1.0f / 60.0f, walkX, walkY, true); // (on in --walk's direction)
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
                    if (v.eventMode) {
                        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) v.event->advance();
                        continue;
                    }
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
                    if (v.eventMode) v.event->advance();
                }
                else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) dragging = false;
                else if (e.type == SDL_EVENT_MOUSE_MOTION && dragging && !v.fieldMode) {
                    v.yaw -= e.motion.xrel * 0.4f;
                    v.pitch = std::clamp(v.pitch + e.motion.yrel * 0.4f, -89.0f, 89.0f);
                } else if (e.type == SDL_EVENT_MOUSE_WHEEL && !v.fieldMode) {
                    v.zoom = std::clamp(v.zoom * (e.wheel.y > 0 ? 0.9f : 1.1f), 0.05f, 20.0f);
                }
            }
            const Uint64 now = SDL_GetTicksNS();
            const float dt = float(double(now - last) / 1e9);
            if (!v.paused) v.time += dt;
            last = now;
            if (v.eventMode && !v.paused) v.updateEvent(dt);
            if (v.fieldMode) { // the arrows / WASD run, with Shift walk
                const bool* keys = SDL_GetKeyboardState(nullptr);
                const float sx = float((keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) - (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]));
                const float sy = float((keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) - (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]));
                v.walk(dt, sx, sy, !(keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]));
            }
            if (v.fig.animLoaded && v.time > v.fig.anim.duration()) v.time = std::fmod(v.time, std::max(v.fig.anim.duration(), 0.05f));
            v.pose();
            SDL_SetWindowTitle(win, ("CR2-U viewer (" + std::string(cr2::backendName(dev->windowFlags() & SDL_WINDOW_VULKAN ? cr2::Backend::Vulkan : cr2::Backend::OpenGL)) + ", " +
                                     describe() + ") - " + v.status + v.animStatus()).c_str());
            int w = 0, h = 0;
            frameSize(w, h);
            v.draw(w, h);
            dev->endFrame(true);
        }
    }
    v.unload(v.fig);
    v.unload(v.mapFigure);
    v.unload(v.vistaMap);
    for (ViewFigure& g : v.eventFigures) v.unload(g);
    if (v.white.id) dev->destroy(v.white);
    for (ViewFigure& g : v.gimmicks) v.unload(g);
    for (auto& [k, t] : v.textures) dev->destroy(t);
    dev->shutdown();
    SDL_DestroyWindow(win);
    SDL_Quit();
    return code;
}
