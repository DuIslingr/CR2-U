#pragma once
// The GPU API: OpenGL 3.3 / GLES 3.0 (gl_device.cpp) or Vulkan 1.3 (vk_device.cpp), Vulkan the
// default - CR1:U's device layer, carried over: a device only sees drawable pixels, GL-style
// clip-space matrices and per-draw material state (here MVGL's: GL blend factors, culling, depth,
// an alpha test, a directional light).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "assets/image.hpp"

struct SDL_Window;

namespace cr2 {

struct Texture {
    unsigned id = 0; // GL texture name, or the Vulkan device's handle (0 = none)
    int width = 0, height = 0;
    bool alphaOnly = false;
};

// A mesh uploaded to the GPU: interleaved vertices (kVertexStride bytes: position 3 f32 at 0,
// uv 2 f32 at 12, colour 4 u8 at 20, normal 3 f32 at 24) plus a triangle index buffer
struct GpuMesh {
    unsigned vao = 0, vbo = 0, ebo = 0; // GL objects; Vulkan keeps its handle in vao (0 = none)
    int indexCount = 0;
    int texture = -1;
};
constexpr size_t kVertexStride = 36;

enum class Backend { OpenGL, Vulkan };
const char* backendName(Backend b);

// A mesh draw's fixed-function state (a material's render states)
struct DrawState {
    bool blend = false;
    // factor codes: source 0 ZERO, 1 ONE, 2 DST_COLOR, 3 ONE_MINUS_DST_COLOR, 4 SRC_ALPHA,
    // 5 ONE_MINUS_SRC_ALPHA, 6 DST_ALPHA, 7 ONE_MINUS_DST_ALPHA, 8 SRC_ALPHA_SATURATE; destination
    // the same with 2 SRC_COLOR, 3 ONE_MINUS_SRC_COLOR (blendCode() maps GL enums)
    uint8_t blendSrc = 4, blendDst = 5;
    uint8_t cull = 0;                   // 0 none, 1 front, 2 back
    bool depthTest = true, depthWrite = true;
    // Sampler: clamp (else repeat) per axis, linear (else nearest) per filter
    bool clampS = false, clampT = false, minLinear = false, magLinear = false;
};

// What the mesh fragment shader needs (see kMeshFragment)
struct MeshShading {
    float color[4] = {1, 1, 1, 1};
    int texMode = -1;   // 0 replace, 1 add, 2 blend, 3 decal, 4 modulate, -1 untextured
    bool alphaTex = false;
    float alphaRef = -1; // the alpha test: fragments with alpha <= ref are discarded (< 0: off)
    bool lit = false;    // a directional light over the vertices' normals
    float light[3] = {0.4f, 0.8f, 0.45f};
};

// A GL blend factor enum as a DrawState code (source, or destination with `dst`)
uint8_t blendCode(uint32_t glFactor, bool dst);

// Temporal upscalers (Vulkan): the scene renders smaller (or at the same size: Native AA /
// DLAA) with sub-pixel jitter, motion vectors and a reactive mask, then is resolved at the
// frame's size before the UI draws over it
enum class Upscaler { Off, Fsr3, Dlss };
struct UpscaleSettings {
    Upscaler mode = Upscaler::Off;
    float ratio = 1.0f;     // output / render size per axis (1 = native AA)
    float sharpness = 0.0f; // 0..1
};

class RenderDevice {
public:
    virtual ~RenderDevice() = default;
    // Window flags the device needs when the window is created
    virtual uint64_t windowFlags() const = 0;
    virtual bool init(SDL_Window* window) = 0;
    virtual void shutdown() = 0;
    virtual std::string description() const = 0;

    virtual Texture upload(const Image& img) = 0;
    virtual void destroy(Texture& tex) = 0;
    // Interleaved vertices as GpuMesh describes (kVertexStride bytes each)
    virtual GpuMesh upload(const std::vector<uint8_t>& vertices, const std::vector<uint32_t>& indices, int texture) = 0;
    virtual void updateVertices(GpuMesh& mesh, const std::vector<uint8_t>& vertices) = 0;
    virtual void destroy(GpuMesh& mesh) = 0;

    // A frame: colour cleared to black, depth to 1. endFrame shows it when `present`; either
    // way readPixels can read it back afterwards.
    virtual void beginFrame(int drawableW, int drawableH) = 0;
    virtual void endFrame(bool present) = 0;
    // Drawable pixels, top-left origin (fractional where the API allows: a scene smaller than
    // the frame keeps the frame's layout exactly)
    virtual void setViewport(float x, float y, float w, float h) = 0;
    virtual void clearDepth() = 0;
    virtual void setColorWrite(bool on) = 0;
    // 2D quads drawn additively (src alpha, one) until switched back - the scene effects' tex2d flag
    virtual void setQuadAdditive(bool /*on*/) {}
    // A textured quad: six vertices of (x, y, u, v); `proj` maps them to clip space (GL style)
    virtual void drawQuad(const Texture& tex, const float* vertices24, const float* proj16, const float* rgba) = 0;
    virtual void drawMesh(const GpuMesh& mesh, const Texture* tex, const float* mvp16, const MeshShading& shading,
                          const DrawState& state) = 0;

    // The last frame's pixels, RGBA, top row first
    virtual bool readPixels(std::vector<uint8_t>& rgba, int& w, int& h) = 0;
    // API errors since the last call (glGetError / failed Vulkan calls)
    virtual int takeErrors() = 0;
    virtual void setVSync(bool on) = 0;
    // Offscreen frames: the frame can be smaller / larger than the window (render scale) and
    // multisampled; false = frames are the window's size, single-sampled
    virtual bool offscreen() const { return false; }
    virtual int maxSamples() const { return 1; }
    virtual void setSamples(int /*n*/) {}

    // Temporal upscaling: whether a mode can run here ("" = yes, else why not)
    virtual std::string upscalerProblem(Upscaler m) const { return m == Upscaler::Off ? "" : "needs the Vulkan renderer"; }
    virtual void setUpscaler(const UpscaleSettings& /*s*/) {}
    // The scene's size in a frame of this size (smaller while an upscaler is on)
    virtual void sceneSize(int w, int h, int& sw, int& sh) const {
        sw = w;
        sh = h;
    }
    // The scene ends: what follows (the UI) draws over the resolved frame at full size
    virtual void endScene() {}
    // The scene's projection (temporal upscalers turn depth into distance with it) and a cut:
    // last frame's pixels say nothing about this one
    virtual void setCamera(float /*near*/, float /*far*/, float /*fovY*/) {}
    virtual void resetHistory() {}
};

std::unique_ptr<RenderDevice> makeGlDevice();
std::unique_ptr<RenderDevice> makeVkDevice(); // nullptr when built without Vulkan

} // namespace cr2
