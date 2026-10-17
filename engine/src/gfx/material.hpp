#pragma once
// A figure material's shading for the device: MVGL's generated shaders (the game's
// shaders/<key>_vp.shad and _fp.shad, chosen by the material's shader key; Figure::SetupMaterials)
// as shading.glsl's SHADE_* bits, the material's parameters, and a light figure's lights.

#include <array>
#include <cstdint>
#include <map>
#include <string>

#include "assets/geom.hpp"
#include "assets/mvgl.hpp"
#include "assets/skeleton.hpp"
#include "gfx/render_device.hpp"

namespace cr2 {

// What a key's shaders do that shading.glsl leaves out
enum UnsupportedShading : uint32_t {
    kUnsupportedPointLight = 1, // PointLamp01 (2 shaders)
    kUnsupportedOverlay = 2,    // OverlayColorSampler (3)
    kUnsupportedDistortion = 4, // the normal map at uv1 offsets uv0 (16)
    kUnsupportedUv1 = 8,        // a second texture coordinate set, ScrollSpeedSet2 / OffsetSet2 (18)
    kUnsupportedBillboard = 16, // ViewInverse turns the vertices to the camera (25)
};

struct ShaderFeatures {
    uint32_t shade = 0;       // SHADE_* (0: a built-in Figure shader - the key is all 0)
    uint32_t unsupported = 0; // UnsupportedShading
};

// The key's words, as the shaders' names print them (word 0 first), in Android's and the Vita's
// layout (the iPad's older generator lays words 1 and 2 out differently; its shaders are read
// instead - ShaderLibrary). Bits (checked against every shader's source by `cr2tool shaders`): word 0 - 0x1 DirLamp01, 0x4 PointLamp01, 0x20 hemisphere
// ambient, 0x40 flat ambient, 0x80 CLUT, 0x100 specular, 0xe00 its strength from the vertex
// alpha, 0x4000 the vertex red shades the light, 0x8000 the vertex colour tints, 0x10000
// ColorSampler, 0x20000 NormalSampler, 0x80000 overlay; word 1 - 0x4000 velvet, 0x10000 the
// tint includes alpha, 0x01000000 alpha test, 0x02000000 ScrollSpeedSet1, 0x04000000
// ScrollSpeedSet2, 0x10000000 OffsetSet1, 0x20000000 distortion; word 2 - 0x1 uv1, 0x4 billboard,
// 0x10 fog, 0x700 LightSampler, 0x8000 skinned (0x10000: two weights a vertex)
ShaderFeatures decodeShaderKey(const std::array<uint32_t, 4>& key);

// What a shader pair's GLSL (the iPad's and Android's data: GLSL ES 1.00) does, read from its lines
ShaderFeatures featuresFromGlsl(const std::string& vp, const std::string& fp);

// The archive's shaders: a key's features from its GLSL where the archive has it, else (the
// Vita: compiled programs) decoded from the key
class ShaderLibrary {
public:
    explicit ShaderLibrary(const MvglArchive& archive) : archive_(archive) {}
    ShaderFeatures features(const std::array<uint32_t, 4>& key);
    // how its vertex shader takes a vertex's joint (its Indices, or the position's w) to
    // MatrixPalette's row: 3 (the iPad's: idx = Indices * 3.0 / Position.w * 3.0), 1 (as stored:
    // Android's, and the Vita's compiled programs)
    float jointScale(const std::array<uint32_t, 4>& key);

private:
    const MvglArchive& archive_;
    std::map<std::array<uint32_t, 4>, ShaderFeatures> cache_;
    std::map<std::array<uint32_t, 4>, float> jointScales_;
};

// A texture entry's sampler flags (GeomMaterial::samplers) as the device's Sampling
Sampling sampling(uint32_t flags);

// MeshShading's game-shading part from the material and its shaders' features: shade,
// DiffuseColor and the parameters, the alpha test's reference (the material's alpha_func state,
// else 0.5). The textures (and their Sampling) are the caller's to set.
void applyMaterial(const GeomMaterial& m, const ShaderFeatures& f, MeshShading& sh);

// The scene's lights from a light figure (<map>c, <map>b, *_evt_light) in its skeleton's base
// pose, as RenderContaindLights sets them; what it lacks keeps `out`'s. False: none.
bool lightsFromFigure(const Geom& g, const Skeleton& sk, SceneLighting& out);

} // namespace cr2
