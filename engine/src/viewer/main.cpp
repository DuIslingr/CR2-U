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
// Vulkan by default, OpenGL where Vulkan can't start (or with --renderer opengl).
//
//   cr2view --data <CRDB.ios.mvgl | main.212...obb | CRDB.psp2.mvgl> [--model <name>]
//           [--anim <name>] [--time s] [--size WxH] [--yaw deg] [--pitch deg]
//           [--lights <figure> | none] [--field <map> [--at <point>] [--binary <file>]
//           [--walk sx,sy,seconds]]
//           [--renderer vulkan|opengl] [--capture out.bmp]
//
// Models: drag to turn, the wheel to zoom, Left / Right (or PageUp / PageDown) for the archive's
// other models, Up / Down for the figure's animations. Fields: the arrow keys (or WASD) run,
// with Shift walk, on the map's floors and against its walls;
// PageUp / PageDown for the character's animations. Space pauses, Esc quits. --capture draws
// one frame (at --time) to a BMP and exits.
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
    void pose(ViewFigure& f, float t) {
        if (!f.skinned) return;
        std::vector<cr2::JointTransform> p = f.skeleton.basePose;
        if (f.animLoaded) {
            const float frames = float(std::max(f.anim.frames() - 1, 1));
            float fr = t * f.anim.sampleRate();
            if (fr > frames) fr = std::fmod(fr, frames);
            // (relative to the base pose); the user channels: material parameters (UI fades, colours)
            std::vector<float> user(f.skeleton.userChannels.size(), std::nanf(""));
            p = f.anim.poseOver(f.skeleton.basePose, fr, f.anim.hasUserChannels() ? &user : nullptr);
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

    void draw(int w, int h) {
        dev->beginFrame(w, h);
        dev->setViewport(0, 0, float(w), float(h));
        if (fieldMode) {
            // FldMain's order: the background (and midground), the 3D figures, the foreground
            const cr2::FieldFrame fr = field.frame(w, h, player);
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
        const Mat4 mvp = cr2::mat4Multiply(
            perspective(0.7f, float(w) / float(std::max(h, 1)), fig.radius * 0.01f * zoom, fig.radius * 20 * zoom + d),
            lookAt(eye, fig.centre));
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
};

bool saveBmp(const char* path, const std::vector<uint8_t>& rgba, int w, int h) {
    SDL_Surface* s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ABGR8888);
    if (!s) return false;
    for (int y = 0; y < h; ++y) std::memcpy(static_cast<uint8_t*>(s->pixels) + size_t(y) * s->pitch, &rgba[size_t(y) * w * 4], size_t(w) * 4);
    const bool ok = SDL_SaveBMP(s, path);
    SDL_DestroySurface(s);
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    std::string data, model, capture, animName, renderer = "vulkan", lights = "d0101c", fieldName, at = "defaultPos", binary;
    float walkX = 0, walkY = 0, walkSeconds = 0; // --walk: the field's character runs first (captures)
    int width = 1280, height = 800;
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
    }
    if (data.empty()) {
        std::fprintf(stderr, "cr2view --data <archive> [--model name] [--anim name] [--time s] [--size WxH] [--yaw deg] "
                             "[--pitch deg] [--lights figure|none] [--field map [--at point] [--binary file]] "
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
    v.shaders = std::make_unique<cr2::ShaderLibrary>(v.archive);
    if (!fieldName.empty()) {
        if (!v.loadField(fieldName, at, binary, data)) return 1;
    } else if (lights != "none") {
        if (v.loadLights(lights)) SDL_Log("lights: %s", lights.c_str());
        else SDL_Log("lights: %s has none (the engine's defaults)", lights.c_str());
    }
    v.load(start);
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

    int code = 0;
    if (!capture.empty()) { // one frame to a file
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(win, &w, &h);
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
                } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) dragging = true;
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
            if (v.fieldMode) { // the arrows / WASD run, with Shift walk
                const bool* keys = SDL_GetKeyboardState(nullptr);
                const float sx = float((keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) - (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]));
                const float sy = float((keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) - (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]));
                v.walk(dt, sx, sy, !(keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]));
            }
            if (v.fig.animLoaded && v.time > v.fig.anim.duration()) v.time = std::fmod(v.time, std::max(v.fig.anim.duration(), 0.05f));
            v.pose();
            SDL_SetWindowTitle(win, ("CR2-U viewer (" + std::string(cr2::backendName(dev->windowFlags() & SDL_WINDOW_VULKAN ? cr2::Backend::Vulkan : cr2::Backend::OpenGL)) + ") - " +
                                     v.status + v.animStatus()).c_str());
            int w = 0, h = 0;
            SDL_GetWindowSizeInPixels(win, &w, &h);
            v.draw(w, h);
            dev->endFrame(true);
        }
    }
    v.unload(v.fig);
    v.unload(v.mapFigure);
    for (ViewFigure& g : v.gimmicks) v.unload(g);
    for (auto& [k, t] : v.textures) dev->destroy(t);
    dev->shutdown();
    SDL_DestroyWindow(win);
    SDL_Quit();
    return code;
}
