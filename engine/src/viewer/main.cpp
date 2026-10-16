// cr2view: CR2-U's model viewer - a figure (`.geom`) from your copy's archive, its meshes with
// their materials' textures and render states, vertex colours and a simple light, skinned to its
// skeleton (`.skel`) and playing its animations (`<figure>*.anim`). Vulkan by default, OpenGL
// where Vulkan can't start (or with --renderer opengl).
//
//   cr2view --data <CRDB.ios.mvgl | main.212...obb | CRDB.psp2.mvgl> [--model <name>]
//           [--anim <name>] [--time s] [--size WxH] [--yaw deg] [--pitch deg]
//           [--renderer vulkan|opengl] [--capture out.bmp]
//
// Drag to turn it, the wheel to zoom, Left / Right (or PageUp / PageDown) for the archive's other
// models, Up / Down for the figure's animations, Space to pause, Esc to quit. --capture draws one
// frame (at --time) to a BMP and exits.
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
#include "assets/mvgl.hpp"
#include "assets/skeleton.hpp"
#include "gfx/render_device.hpp"

namespace {

// GL enums the materials' render states use
constexpr uint32_t kGlSrcAlpha = 0x0302, kGlOneMinusSrcAlpha = 0x0303, kGlFront = 0x0404, kGlBack = 0x0405;

struct Mat4 {
    float m[16];
};

Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    return r;
}

Mat4 perspective(float fovY, float aspect, float zn, float zf) {
    const float f = 1.0f / std::tan(fovY / 2);
    Mat4 r{};
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zf + zn) / (zn - zf);
    r.m[11] = -1;
    r.m[14] = 2 * zf * zn / (zn - zf);
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
    r.m[0] = s[0], r.m[4] = s[1], r.m[8] = s[2];
    r.m[1] = u[0], r.m[5] = u[1], r.m[9] = u[2];
    r.m[2] = -f[0], r.m[6] = -f[1], r.m[10] = -f[2];
    r.m[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
    r.m[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    r.m[14] = f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2];
    r.m[15] = 1;
    return r;
}

struct ViewMesh {
    cr2::GpuMesh gpu;
    bool lit = false;
    cr2::Texture* texture = nullptr;
    cr2::GeomMesh source;        // (for skinning)
    cr2::GeomMaterial material;  // (its render states)
    std::vector<uint8_t> vertices; // kVertexStride bytes each
};

void packVertex(uint8_t* o, const float* p, const float* n, const float* uv, const float* c) {
    std::memcpy(o, p, 12);
    std::memcpy(o + 12, uv, 8);
    for (int k = 0; k < 4; ++k) o[20 + k] = uint8_t(std::clamp(c[k], 0.0f, 1.0f) * 255.0f + 0.5f);
    std::memcpy(o + 24, n, 12);
}

struct Viewer {
    cr2::RenderDevice* dev = nullptr;
    cr2::MvglArchive archive;
    std::vector<std::string> models; // the archive's .geom names, sorted
    size_t current = 0;
    std::vector<ViewMesh> meshes;
    std::map<std::string, cr2::Texture> textures; // by "colour|alpha map"
    float centre[3] = {0, 0, 0}, radius = 1;
    float yaw = 30, pitch = 15, zoom = 1;
    std::string status;
    cr2::Skeleton skeleton;
    bool skinned = false;
    std::vector<cr2::Mat34> inverseBind;
    std::vector<std::string> anims; // the figure's animations
    size_t animIndex = 0;
    cr2::Animation anim;
    bool animLoaded = false, paused = false;
    float time = 0;

    bool decode(const std::string& name, cr2::Image& img) {
        std::vector<uint8_t> data;
        std::string err;
        if (archive.read("images\\" + name + ".img", data, &err) && cr2::decodeImg(data, img, &err)) return true;
        SDL_Log("texture %s: %s", name.c_str(), err.c_str());
        return false;
    }

    // A material's texture; with an alpha map (0x43), the two merged at the larger size - the
    // colour's alpha times the map's (nearest texels: they share the texture coordinates)
    cr2::Texture* texture(const std::string& name, const std::string& alphaMap) {
        if (name.empty()) return nullptr;
        const std::string key = name + "|" + alphaMap;
        if (auto it = textures.find(key); it != textures.end()) return it->second.id ? &it->second : nullptr;
        cr2::Image img;
        cr2::Texture& t = textures[key];
        if (!decode(name, img)) return nullptr;
        cr2::Image a;
        if (!alphaMap.empty() && decode(alphaMap, a)) {
            cr2::Image m;
            m.width = std::max(img.width, a.width);
            m.height = std::max(img.height, a.height);
            m.rgba.resize(size_t(m.width) * m.height * 4);
            for (int y = 0; y < m.height; ++y)
                for (int x = 0; x < m.width; ++x) {
                    const uint8_t* c = &img.rgba[(size_t(y * img.height / m.height) * img.width + size_t(x * img.width / m.width)) * 4];
                    const uint8_t* al = &a.rgba[(size_t(y * a.height / m.height) * a.width + size_t(x * a.width / m.width)) * 4];
                    uint8_t* o = &m.rgba[(size_t(y) * m.width + x) * 4];
                    o[0] = c[0], o[1] = c[1], o[2] = c[2], o[3] = uint8_t(c[3] * al[3] / 255);
                }
            img = std::move(m);
        }
        img.mipmaps = true;
        t = dev->upload(img);
        return t.id ? &t : nullptr;
    }

    void unload() {
        for (ViewMesh& m : meshes) dev->destroy(m.gpu);
        meshes.clear();
    }

    bool load(size_t index) {
        unload();
        current = index;
        const std::string& name = models[index];
        std::vector<uint8_t> data;
        cr2::Geom g;
        std::string err;
        if (!archive.read(name, data, &err) || !cr2::parseGeom(data, g, &err)) {
            status = name + ": " + err;
            SDL_Log("%s", status.c_str());
            return false;
        }
        size_t verts = 0, tris = 0;
        static const float kWhite[4] = {1, 1, 1, 1}, kZero[3] = {0, 0, 0};
        for (const cr2::GeomMesh& m : g.meshes) {
            ViewMesh vm;
            vm.vertices.resize(size_t(m.vertexCount) * cr2::kVertexStride);
            for (uint32_t i = 0; i < m.vertexCount; ++i) {
                static const float kNoUv[2] = {0, 0};
                packVertex(&vm.vertices[size_t(i) * cr2::kVertexStride], &m.position[size_t(i) * 3],
                           m.normal.empty() ? kZero : &m.normal[size_t(i) * 3], m.uv.empty() ? kNoUv : &m.uv[size_t(i) * 2],
                           m.colour.empty() ? kWhite : &m.colour[size_t(i) * 4]);
            }
            const std::vector<uint32_t> idx = m.triangles();
            vm.gpu = dev->upload(vm.vertices, idx, -1);
            vm.lit = !m.normal.empty();
            vm.source = m;
            if (m.material < g.materials.size()) {
                vm.material = g.materials[m.material];
                const auto am = vm.material.textures.find(0x43);
                vm.texture = texture(vm.material.diffuse(), am == vm.material.textures.end() ? "" : am->second);
            }
            meshes.push_back(std::move(vm));
            verts += m.vertexCount;
            tris += idx.size() / 3;
        }
        float r2 = 0;
        for (int c = 0; c < 3; ++c) {
            centre[c] = (g.boundsMin[c] + g.boundsMax[c]) / 2;
            const float h = (g.boundsMax[c] - g.boundsMin[c]) / 2;
            r2 += h * h;
        }
        radius = std::max(std::sqrt(r2), 1e-3f);
        // the skeleton and the figure's animations (<figure>*.anim: c217.anim, c217e080.anim, ...)
        const std::string base = name.substr(0, name.size() - 5);
        skinned = archive.read(base + ".skel", data) && cr2::parseSkeleton(data, skeleton) &&
                  g.inverseBind.size() == skeleton.basePose.size();
        inverseBind = g.inverseBind;
        anims.clear();
        animLoaded = false;
        if (skinned)
            for (const auto& e : archive.entries())
                if (e.name.size() > 5 && e.name.compare(0, base.size(), base) == 0 && e.name.compare(e.name.size() - 5, 5, ".anim") == 0)
                    anims.push_back(e.name);
        std::sort(anims.begin(), anims.end());
        if (!anims.empty()) loadAnim(std::min(animIndex, anims.size() - 1));
        char buf[256];
        std::snprintf(buf, sizeof buf, "%s (%zu/%zu): %zu meshes, %zu vertices, %zu triangles", name.c_str(), index + 1,
                      models.size(), g.meshes.size(), verts, tris);
        status = buf;
        SDL_Log("%s", status.c_str());
        return true;
    }

    void loadAnim(size_t index) {
        animIndex = index;
        std::vector<uint8_t> data;
        std::string err;
        animLoaded = archive.read(anims[index], data, &err) && anim.parse(data, &err) && anim.joints() == int(skeleton.basePose.size());
        if (!animLoaded) SDL_Log("%s: %s", anims[index].c_str(), err.empty() ? "not this skeleton's" : err.c_str());
        time = 0;
    }

    std::string animStatus() const {
        if (!skinned) return "";
        if (!animLoaded) return " - base pose";
        char buf[160];
        std::snprintf(buf, sizeof buf, " - %s (%zu/%zu) %.2f / %.2f s", anims[animIndex].c_str(), animIndex + 1, anims.size(),
                      time, anim.duration());
        return buf;
    }

    // the pose now, the skinning matrices (world * inverse bind), the meshes' vertices through their
    // palettes (as the game's vertex shaders: MatrixPalette[index / 3 slot], the weights as stored)
    void pose() {
        if (!skinned) return;
        std::vector<cr2::JointTransform> p = skeleton.basePose;
        if (animLoaded) {
            const float frames = float(std::max(anim.frames() - 1, 1));
            float fr = time * anim.sampleRate();
            if (fr > frames) fr = std::fmod(fr, frames);
            p = anim.poseOver(skeleton.basePose, fr); // (relative to the base pose)
        }
        const std::vector<cr2::Mat34> world = skeleton.world(p);
        std::vector<cr2::Mat34> skin(world.size());
        for (size_t j = 0; j < world.size(); ++j) skin[j] = world[j] * inverseBind[j];
        static const cr2::Mat34 kIdentity;
        for (ViewMesh& vm : meshes) {
            const cr2::GeomMesh& m = vm.source;
            std::vector<const cr2::Mat34*> pal;
            for (uint32_t j : m.palette) pal.push_back(j < skin.size() ? &skin[j] : nullptr);
            for (uint32_t i = 0; i < m.vertexCount; ++i) {
                const float* sp = &m.position[size_t(i) * 3];
                const float* sn = m.normal.empty() ? nullptr : &m.normal[size_t(i) * 3];
                float op[3] = {0, 0, 0}, on[3] = {0, 0, 0};
                auto add = [&](const cr2::Mat34& M, float w) {
                    for (int r = 0; r < 3; ++r) {
                        op[r] += w * (M.m[r * 4] * sp[0] + M.m[r * 4 + 1] * sp[1] + M.m[r * 4 + 2] * sp[2] + M.m[r * 4 + 3]);
                        if (sn) on[r] += w * (M.m[r * 4] * sn[0] + M.m[r * 4 + 1] * sn[1] + M.m[r * 4 + 2] * sn[2]);
                    }
                };
                if (m.joints.empty()) {
                    add(pal.empty() || !pal[0] ? kIdentity : *pal[0], 1.0f);
                } else {
                    for (int k = 0; k < 4; ++k) {
                        const float w = m.weights.empty() ? (k == 0 ? 1.0f : 0.0f) : m.weights[size_t(i) * 4 + k];
                        const size_t slot = m.joints[size_t(i) * 4 + k] / 3;
                        if (w != 0.0f && slot < pal.size() && pal[slot]) add(*pal[slot], w);
                    }
                }
                uint8_t* o = &vm.vertices[size_t(i) * cr2::kVertexStride];
                std::memcpy(o, op, 12);
                if (sn) std::memcpy(o + 24, on, 12);
            }
            dev->updateVertices(vm.gpu, vm.vertices);
        }
    }

    void draw(int w, int h) {
        dev->beginFrame(w, h);
        dev->setViewport(0, 0, float(w), float(h));
        const float yr = yaw * 3.14159265f / 180, pr = pitch * 3.14159265f / 180, d = radius * 2.6f * zoom;
        const float eye[3] = {centre[0] + d * std::cos(pr) * std::sin(yr), centre[1] + d * std::sin(pr),
                              centre[2] + d * std::cos(pr) * std::cos(yr)};
        const Mat4 mvp = multiply(perspective(0.7f, float(w) / float(std::max(h, 1)), radius * 0.01f * zoom, radius * 20 * zoom + d),
                                  lookAt(eye, centre));
        // the materials' render states; opaque meshes first, then those blending
        std::vector<const ViewMesh*> order;
        for (const ViewMesh& m : meshes) order.push_back(&m);
        std::stable_sort(order.begin(), order.end(), [](const ViewMesh* a, const ViewMesh* b) {
            uint32_t ba = 0, bb = 0;
            a->material.state(0x7c, 0, ba);
            b->material.state(0x7c, 0, bb);
            return ba < bb;
        });
        for (const ViewMesh* mp : order) {
            const ViewMesh& m = *mp;
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
            st.minLinear = st.magLinear = true;
            sh.texMode = m.texture ? 4 : -1; // modulate the vertex colour
            sh.lit = m.lit;
            dev->drawMesh(m.gpu, m.texture, mvp.m, sh, st);
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

} // namespace

int main(int argc, char** argv) {
    std::string data, model, capture, animName, renderer = "vulkan";
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
    }
    if (data.empty()) {
        std::fprintf(stderr, "cr2view --data <archive> [--model name] [--anim name] [--time s] [--size WxH] [--yaw deg] "
                             "[--pitch deg] [--renderer vulkan|opengl] [--capture out.bmp]\n");
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
    v.load(start);
    if (!animName.empty()) {
        const std::string want = animName.size() > 5 && animName.compare(animName.size() - 5, 5, ".anim") == 0 ? animName : animName + ".anim";
        const auto it = std::find(v.anims.begin(), v.anims.end(), want);
        if (it == v.anims.end()) return std::fprintf(stderr, "%s: not one of the figure's animations\n", want.c_str()), 1;
        v.loadAnim(size_t(it - v.anims.begin()));
    }
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
                    size_t to = v.current;
                    if (k == SDLK_ESCAPE) running = false;
                    else if (k == SDLK_RIGHT || k == SDLK_PAGEDOWN) to = (v.current + (k == SDLK_PAGEDOWN ? 10 : 1)) % v.models.size();
                    else if (k == SDLK_LEFT || k == SDLK_PAGEUP)
                        to = (v.current + v.models.size() * 10 - (k == SDLK_PAGEUP ? 10 : 1)) % v.models.size();
                    if (to != v.current) v.load(to);
                    if (k == SDLK_SPACE) v.paused = !v.paused;
                    if ((k == SDLK_UP || k == SDLK_DOWN) && !v.anims.empty())
                        v.loadAnim((v.animIndex + (k == SDLK_DOWN ? 1 : v.anims.size() - 1)) % v.anims.size());
                } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) dragging = true;
                else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) dragging = false;
                else if (e.type == SDL_EVENT_MOUSE_MOTION && dragging) {
                    v.yaw -= e.motion.xrel * 0.4f;
                    v.pitch = std::clamp(v.pitch + e.motion.yrel * 0.4f, -89.0f, 89.0f);
                } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                    v.zoom = std::clamp(v.zoom * (e.wheel.y > 0 ? 0.9f : 1.1f), 0.05f, 20.0f);
                }
            }
            const Uint64 now = SDL_GetTicksNS();
            if (!v.paused) v.time += float(double(now - last) / 1e9);
            last = now;
            if (v.animLoaded && v.time > v.anim.duration()) v.time = std::fmod(v.time, std::max(v.anim.duration(), 0.05f));
            v.pose();
            SDL_SetWindowTitle(win, ("CR2-U viewer (" + std::string(cr2::backendName(dev->windowFlags() & SDL_WINDOW_VULKAN ? cr2::Backend::Vulkan : cr2::Backend::OpenGL)) + ") - " +
                                     v.status + v.animStatus()).c_str());
            int w = 0, h = 0;
            SDL_GetWindowSizeInPixels(win, &w, &h);
            v.draw(w, h);
            dev->endFrame(true);
        }
    }
    v.unload();
    for (auto& [k, t] : v.textures) dev->destroy(t);
    dev->shutdown();
    SDL_DestroyWindow(win);
    SDL_Quit();
    return code;
}
