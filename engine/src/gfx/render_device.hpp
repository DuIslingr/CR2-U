#pragma once
// The GPU API: OpenGL 3.3 / GLES 3.0 (gl_device.cpp) or Vulkan 1.3 (vk_device.cpp), Vulkan the
// default - CR1:U's device layer, carried over: a device only sees drawable pixels, GL-style
// clip-space matrices and per-draw material state (here MVGL's: GL blend factors, culling, depth,
// and either the game's generated shaders' shading - shading.glsl - or a fixed path).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "assets/image.hpp"
#include "gfx/shaders/shade_flags.h"

struct SDL_Window;

namespace cr2 {

struct Texture {
    unsigned id = 0; // GL texture name, or the Vulkan device's handle (0 = none)
    int width = 0, height = 0;
    bool alphaOnly = false;
};

// A mesh uploaded to the GPU: interleaved vertices (kVertexStride bytes: position 3 f32 at 0,
// uv 2 f32 at 12, colour 4 u8 at 20, normal 3 f32 at 24, tangent 3 f32 at 36, binormal 3 f32 at
// 48, uv1 2 f32 at 60) plus a triangle index buffer
struct GpuMesh {
    unsigned vao = 0, vbo = 0, ebo = 0; // GL objects; Vulkan keeps its handle in vao (0 = none)
    int indexCount = 0;
    int texture = -1;
};
constexpr size_t kVertexStride = 68;

enum class Backend { OpenGL, Vulkan };
const char* backendName(Backend b);

// How a texture is sampled: clamp or mirrored repeat (else repeat) per axis, linear (else
// nearest) per filter
struct Sampling {
    bool clampS = false, clampT = false, mirrorS = false, mirrorT = false;
    bool minLinear = true, magLinear = true;
};

// A mesh draw's fixed-function state (a material's render states)
struct DrawState {
    bool blend = false;
    // factor codes: source 0 ZERO, 1 ONE, 2 DST_COLOR, 3 ONE_MINUS_DST_COLOR, 4 SRC_ALPHA,
    // 5 ONE_MINUS_SRC_ALPHA, 6 DST_ALPHA, 7 ONE_MINUS_DST_ALPHA, 8 SRC_ALPHA_SATURATE; destination
    // the same with 2 SRC_COLOR, 3 ONE_MINUS_SRC_COLOR (blendCode() maps GL enums)
    uint8_t blendSrc = 4, blendDst = 5;
    uint8_t cull = 0;                   // 0 none, 1 front, 2 back
    bool depthTest = true, depthWrite = true;
    Sampling sampling; // the texture's
};

// What the mesh shaders need. `shade` 0: the fixed path (the colour, a GL ES 1.1 texture mode, an
// alpha test, a simple light); SHADE_ON | ...: the game's shading (shading.glsl), with the
// material's parameters below and the scene's lights (setLighting)
struct MeshShading {
    float color[4] = {1, 1, 1, 1}; // (the game's shading: DiffuseColor)
    int texMode = -1;   // 0 replace, 1 add, 2 blend, 3 decal, 4 modulate, -1 untextured
    bool alphaTex = false;
    float alphaRef = -1; // fixed: alpha <= ref discarded (< 0: off); the game's: texture alpha < ref
    bool lit = false;    // fixed: a directional light over the vertices' normals
    float light[3] = {0.4f, 0.8f, 0.45f};

    uint32_t shade = 0; // SHADE_* (shade_flags.h)
    float bumpiness = 1, specularStrength = 0, specularPower = 1, rollOff = 0, velvetStrength = 0;
    float surfaceColor[3] = {1, 1, 1}, fuzzySpecColor[3] = {0, 0, 0}, subColor[3] = {0, 0, 0};
    float scrollSpeed[2] = {0, 0}, offset[2] = {0, 0};
    float scrollSpeed2[2] = {0, 0}, offset2[2] = {0, 0}; // (uv1's)
    float overlayStrength = 0, distortionStrength = 0;
    const Texture* normalMap = nullptr; // SHADE_NORMAL_MAP
    Sampling normalSampling;
    const Texture* extraMap = nullptr;  // SHADE_CLUT's CLUT, SHADE_LIGHT_MAP's light map or SHADE_OVERLAY's overlay
    Sampling extraSampling;
};

// The scene's lights for the game's shading (MVGL's RenderContext; the defaults are its own),
// in the space of the vertices drawn
struct SceneLighting {
    float lightDir[3] = {0.5307f, -0.7581f, 0.3790f}; // DirLamp01Dir: towards the light
    float lightColor[3] = {0.5f, 0.5f, 0.5f};
    float ambient[3] = {0.5f, 0.5f, 0.5f};
    float ground[3] = {0.3f, 0.3f, 0.3f};
    float skyDir[3] = {0, -1, 0};
    float fogParams[3] = {1.5f, -0.005f, 1.0f}; // far / (far - near), -1 / (far - near), alpha
    float fogColor[3] = {1, 1, 1};
    float camera[3] = {0, 0, 0};
    float time = 0; // seconds (scrolling textures)
};

// The shaders' Material block (shading.glsl), std140
struct ShadingBlock {
    float diffuse[4];
    float params[4]; // Bumpiness, SpecularStrength, SpecularPower, AlphaRef
    float velvet[4]; // RollOff, VelvetStrength, Time, -
    int32_t flags[4];
    float surface[4], fuzzy[4], sub[4];
    float uvAnim[4]; // ScrollSpeedSet1, OffsetSet1
    float lightDir[4], lightColor[4], ambient[4], ground[4], skyDir[4], fogParams[4], fogColor[4], camera[4];
    float uvAnim2[4]; // ScrollSpeedSet2, OffsetSet2
    float strengths[4]; // OverlayStrength, DistortionStrength, -, -
};
static_assert(sizeof(ShadingBlock) == 288, "Material block layout");
ShadingBlock shadingBlock(const MeshShading& sh, const SceneLighting& scene);

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
    // The lights (and camera, time) the game's shading uses from here on
    void setLighting(const SceneLighting& l) { lighting_ = l; }
    const SceneLighting& lighting() const { return lighting_; }

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

protected:
    SceneLighting lighting_;
};

std::unique_ptr<RenderDevice> makeGlDevice();
std::unique_ptr<RenderDevice> makeVkDevice(); // nullptr when built without Vulkan

} // namespace cr2
