// cr2view: CR2-U's model viewer - a figure (`.geom`) from your copy's archive, its meshes with
// their materials' diffuse textures, vertex colours and a simple light.
//
//   cr2view --data <CRDB.ios.mvgl | main.212...obb | CRDB.psp2.mvgl> [--model <name>]
//           [--size WxH] [--yaw deg] [--pitch deg] [--capture out.bmp]
//
// Drag to turn it, the wheel to zoom, Left / Right (or PageUp / PageDown) for the archive's other
// models, Esc to quit. --capture draws one frame to a BMP and exits.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "assets/geom.hpp"
#include "assets/image.hpp"
#include "assets/mvgl.hpp"
#include "gfx/gl.hpp"

namespace {

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
    float s[3] = {f[1] * 0 - f[2] * 1, f[2] * 0 - f[0] * 0, f[0] * 1 - f[1] * 0}; // f x up
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

const char* kVertex = R"(
in vec3 aPosition;
in vec3 aNormal;
in vec2 aUv;
in vec4 aColour;
uniform mat4 uMvp;
out vec3 vNormal;
out vec2 vUv;
out vec4 vColour;
void main() {
    vNormal = aNormal;
    vUv = aUv;
    vColour = aColour;
    gl_Position = uMvp * vec4(aPosition, 1.0);
}
)";

const char* kFragment = R"(
in vec3 vNormal;
in vec2 vUv;
in vec4 vColour;
uniform sampler2D uTexture;
uniform float uLit;      // 1: the mesh has normals
uniform float uTextured; // 1: a diffuse texture
out vec4 outColour;
void main() {
    vec4 c = vColour * mix(vec4(1.0), texture(uTexture, vUv), uTextured);
    if (c.a < 0.25) discard;
    float light = 1.0;
    if (uLit > 0.5) {
        vec3 n = normalize(vNormal);
        light = 0.45 + 0.65 * max(dot(n, normalize(vec3(0.4, 0.8, 0.45))), 0.0);
    }
    outColour = vec4(c.rgb * light, 1.0);
}
)";

GLuint compile(GLenum type, const char* body) {
    const std::string src = std::string(gl::shaderHeader()) + body;
    const char* p = src.c_str();
    const GLuint s = gl::CreateShader(type);
    gl::ShaderSource(s, 1, &p, nullptr);
    gl::CompileShader(s);
    GLint ok = 0;
    gl::GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        gl::GetShaderInfoLog(s, sizeof log, nullptr, log);
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "shader: %s", log);
    }
    return s;
}

struct GpuMesh {
    GLuint vao = 0, vbo = 0, ibo = 0;
    GLsizei indices = 0;
    bool lit = false;
    GLuint texture = 0;
};

struct Viewer {
    cr2::MvglArchive archive;
    std::vector<std::string> models; // the archive's .geom names, sorted
    size_t current = 0;
    std::vector<GpuMesh> meshes;
    std::map<std::string, GLuint> textures;
    float centre[3] = {0, 0, 0}, radius = 1;
    float yaw = 30, pitch = 15, zoom = 1;
    GLuint program = 0;
    std::string status;

    GLuint texture(const std::string& name) {
        if (name.empty()) return 0;
        if (auto it = textures.find(name); it != textures.end()) return it->second;
        GLuint t = 0;
        std::vector<uint8_t> data;
        cr2::Image img;
        std::string err;
        if (archive.read("images\\" + name + ".img", data, &err) && cr2::decodeImg(data, img, &err)) {
            gl::GenTextures(1, &t);
            gl::BindTexture(GL_TEXTURE_2D, t);
            gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
            // as stored: the game uploads the rows in the file's order, its texcoords follow
            gl::TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
            gl::GenerateMipmap(GL_TEXTURE_2D);
            gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        } else {
            SDL_Log("texture %s: %s", name.c_str(), err.c_str());
        }
        textures[name] = t;
        return t;
    }

    void unload() {
        for (GpuMesh& m : meshes) {
            gl::DeleteVertexArrays(1, &m.vao);
            gl::DeleteBuffers(1, &m.vbo);
            gl::DeleteBuffers(1, &m.ibo);
        }
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
        for (const cr2::GeomMesh& m : g.meshes) {
            // interleaved: position 3, normal 3, uv 2, colour 4
            std::vector<float> v(size_t(m.vertexCount) * 12, 0.0f);
            for (uint32_t i = 0; i < m.vertexCount; ++i) {
                float* o = &v[size_t(i) * 12];
                std::memcpy(o, &m.position[size_t(i) * 3], 12);
                if (!m.normal.empty()) std::memcpy(o + 3, &m.normal[size_t(i) * 3], 12);
                if (!m.uv.empty()) std::memcpy(o + 6, &m.uv[size_t(i) * 2], 8);
                if (!m.colour.empty()) std::memcpy(o + 8, &m.colour[size_t(i) * 4], 16);
                else o[8] = o[9] = o[10] = o[11] = 1.0f;
            }
            const std::vector<uint32_t> idx = m.triangles();
            GpuMesh gm;
            gl::GenVertexArrays(1, &gm.vao);
            gl::BindVertexArray(gm.vao);
            gl::GenBuffers(1, &gm.vbo);
            gl::BindBuffer(GL_ARRAY_BUFFER, gm.vbo);
            gl::BufferData(GL_ARRAY_BUFFER, GLsizeiptr(v.size() * 4), v.data(), GL_STATIC_DRAW);
            gl::GenBuffers(1, &gm.ibo);
            gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, gm.ibo);
            gl::BufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(idx.size() * 4), idx.data(), GL_STATIC_DRAW);
            const GLsizei stride = 12 * 4;
            gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
            gl::VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(12));
            gl::VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(24));
            gl::VertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(32));
            for (GLuint a = 0; a < 4; ++a) gl::EnableVertexAttribArray(a);
            gm.indices = GLsizei(idx.size());
            gm.lit = !m.normal.empty();
            if (m.material < g.materials.size()) gm.texture = texture(g.materials[m.material].diffuse());
            meshes.push_back(gm);
            verts += m.vertexCount;
            tris += idx.size() / 3;
        }
        gl::BindVertexArray(0);
        float r2 = 0;
        for (int c = 0; c < 3; ++c) {
            centre[c] = (g.boundsMin[c] + g.boundsMax[c]) / 2;
            const float h = (g.boundsMax[c] - g.boundsMin[c]) / 2;
            r2 += h * h;
        }
        radius = std::max(std::sqrt(r2), 1e-3f);
        char buf[256];
        std::snprintf(buf, sizeof buf, "%s (%zu/%zu): %zu meshes, %zu vertices, %zu triangles", name.c_str(), index + 1,
                      models.size(), g.meshes.size(), verts, tris);
        status = buf;
        SDL_Log("%s", status.c_str());
        return true;
    }

    void draw(int w, int h) {
        gl::Viewport(0, 0, w, h);
        gl::ClearColor(0.16f, 0.17f, 0.20f, 1);
        gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        gl::Enable(GL_DEPTH_TEST);
        gl::DepthFunc(GL_LEQUAL);
        gl::Disable(GL_CULL_FACE); // (the game's culling per material: not read yet)
        const float yr = yaw * 3.14159265f / 180, pr = pitch * 3.14159265f / 180, d = radius * 2.6f * zoom;
        const float eye[3] = {centre[0] + d * std::cos(pr) * std::sin(yr), centre[1] + d * std::sin(pr),
                              centre[2] + d * std::cos(pr) * std::cos(yr)};
        const Mat4 mvp = multiply(perspective(0.7f, float(w) / float(std::max(h, 1)), radius * 0.01f * zoom, radius * 20 * zoom + d),
                                  lookAt(eye, centre));
        gl::UseProgram(program);
        gl::UniformMatrix4fv(gl::GetUniformLocation(program, "uMvp"), 1, GL_FALSE, mvp.m);
        gl::Uniform1i(gl::GetUniformLocation(program, "uTexture"), 0);
        gl::ActiveTexture(GL_TEXTURE0);
        for (const GpuMesh& m : meshes) {
            gl::Uniform1f(gl::GetUniformLocation(program, "uLit"), m.lit ? 1.0f : 0.0f);
            gl::Uniform1f(gl::GetUniformLocation(program, "uTextured"), m.texture ? 1.0f : 0.0f);
            gl::BindTexture(GL_TEXTURE_2D, m.texture);
            gl::BindVertexArray(m.vao);
            gl::DrawElements(GL_TRIANGLES, m.indices, GL_UNSIGNED_INT, nullptr);
        }
        gl::BindVertexArray(0);
    }
};

bool saveBmp(const char* path, int w, int h) {
    std::vector<uint8_t> px(size_t(w) * h * 4);
    gl::PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl::ReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    SDL_Surface* s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ABGR8888);
    if (!s) return false;
    for (int y = 0; y < h; ++y) // (GL's rows run bottom-up)
        std::memcpy(static_cast<uint8_t*>(s->pixels) + size_t(y) * s->pitch, &px[size_t(h - 1 - y) * w * 4], size_t(w) * 4);
    const bool ok = SDL_SaveBMP(s, path);
    SDL_DestroySurface(s);
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    std::string data, model, capture;
    int width = 1280, height = 800;
    float yaw = 30, pitch = 15;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const char* next = i + 1 < argc ? argv[i + 1] : nullptr;
        if (a == "--data" && next) data = next, ++i;
        else if (a == "--model" && next) model = next, ++i;
        else if (a == "--capture" && next) capture = next, ++i;
        else if (a == "--size" && next) std::sscanf(next, "%dx%d", &width, &height), ++i;
        else if (a == "--yaw" && next) yaw = float(std::atof(next)), ++i;
        else if (a == "--pitch" && next) pitch = float(std::atof(next)), ++i;
    }
    if (data.empty()) {
        std::fprintf(stderr, "cr2view --data <archive> [--model name] [--size WxH] [--yaw deg] [--pitch deg] [--capture out.bmp]\n");
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
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    SDL_Window* win = SDL_CreateWindow("CR2-U viewer", width, height,
                                       SDL_WINDOW_OPENGL | (capture.empty() ? SDL_WINDOW_RESIZABLE : SDL_WINDOW_HIDDEN));
    if (!win) return std::fprintf(stderr, "window: %s\n", SDL_GetError()), 1;
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx || !gl::load()) return std::fprintf(stderr, "OpenGL 3.3: %s\n", SDL_GetError()), 1;
    SDL_GL_SetSwapInterval(1);

    const GLuint vs = compile(GL_VERTEX_SHADER, kVertex), fs = compile(GL_FRAGMENT_SHADER, kFragment);
    v.program = gl::CreateProgram();
    gl::AttachShader(v.program, vs);
    gl::AttachShader(v.program, fs);
    gl::BindAttribLocation(v.program, 0, "aPosition");
    gl::BindAttribLocation(v.program, 1, "aNormal");
    gl::BindAttribLocation(v.program, 2, "aUv");
    gl::BindAttribLocation(v.program, 3, "aColour");
    gl::LinkProgram(v.program);
    v.load(start);

    if (!capture.empty()) { // one frame to a file
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(win, &w, &h);
        v.draw(w, h);
        const bool ok = saveBmp(capture.c_str(), w, h);
        SDL_Log("capture %s: %s", capture.c_str(), ok ? "written" : SDL_GetError());
        SDL_GL_DestroyContext(ctx);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return ok ? 0 : 1;
    }
    SDL_SetWindowTitle(win, ("CR2-U viewer - " + v.status).c_str());
    bool running = true, dragging = false;
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
                if (to != v.current) {
                    v.load(to);
                    SDL_SetWindowTitle(win, ("CR2-U viewer - " + v.status).c_str());
                }
            } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) dragging = true;
            else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) dragging = false;
            else if (e.type == SDL_EVENT_MOUSE_MOTION && dragging) {
                v.yaw -= e.motion.xrel * 0.4f;
                v.pitch = std::clamp(v.pitch + e.motion.yrel * 0.4f, -89.0f, 89.0f);
            } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                v.zoom = std::clamp(v.zoom * (e.wheel.y > 0 ? 0.9f : 1.1f), 0.05f, 20.0f);
            }
        }
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(win, &w, &h);
        v.draw(w, h);
        SDL_GL_SwapWindow(win);
    }
    v.unload();
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
