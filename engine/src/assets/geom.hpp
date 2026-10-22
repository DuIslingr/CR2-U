#pragma once
// MVGL figure meshes (`.geom`; MVGL::Draw::Figure::Build / BuildMesh / DrawMesh, docs/FORMATS.md):
// the iPad's version 0x3c and Android / the Vita's 0x50 (a header 0x10 bytes longer, the records
// alike). Each mesh describes its own vertex attributes; they are unpacked here to floats.

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "assets/skeleton.hpp"

namespace cr2 {

struct GeomMesh {
    uint32_t material = 0;
    std::vector<uint32_t> palette; // the skeleton joints its matrix palette holds (+0 count, +0x2c list)
    uint32_t vertexCount = 0;
    std::vector<float> position; // 3 a vertex
    std::vector<float> normal;   // 3 a vertex (empty: none)
    std::vector<float> tangent, binormal; // 3 a vertex (empty: none) - the normal maps' frame
    std::vector<float> uv;       // 2 a vertex, already / 1024 (empty: none)
    std::vector<float> uv1;
    std::vector<float> colour;   // 4 a vertex, 0..1 (empty: none)
    // blend indices, 4 a vertex (empty: none - the mesh is rigid, on palette[0]), as stored: after
    // resolveJoints palette slot * 3 (MatrixPalette's rows), as the vertex shaders index
    std::vector<uint8_t> joints;
    // a four-component position's w (empty: none). Meshes on one joint a vertex keep the joint
    // there in the iPad's and the Vita's files (no joints attribute)
    std::vector<float> positionW;
    // The joints as MatrixPalette's rows: the stored ones, or positionW's (weight 1), times `scale` -
    // the vertex shader's (ShaderLibrary::jointScale): the iPad's slots (Indices * 3.0,
    // Position.w * 3.0), Android's and the Vita's already rows (1)
    void resolveJoints(float scale);
    std::vector<float> weights;  // blend weights, 4 a vertex, used as stored (no remainder implied)
    std::vector<uint16_t> strip; // the indices as stored (a triangle strip)
    // the strip as a triangle list, degenerate triangles dropped, every other one turned so all
    // keep the strip's winding
    std::vector<uint32_t> triangles() const;
};

struct GeomMaterial {
    uint32_t hash = 0;
    // the shader: +4..+0x13, four words naming shaders\<%08x_%08x_%08x_%08x>_vp/_fp.shad (generated
    // GLSL; gfx/material.cpp decodes them), all 0 for MVGL's built-in Figure shaders
    std::array<uint32_t, 4> shaderKey{};
    // parameter -> image name: 0x32 diffuse, 0x35 normal map, 0x43 light map (its alpha replaces the
    // colour's: the UI's A8 masks), 0x44 overlay, 0x48 CLUT (toon shading)
    std::map<int, std::string> textures;
    // parameter -> the texture's sampler flags (Texture::Build copies them to Texture+0x3c, which
    // FinishInitialize turns into glTexParameteri): bits 0-2 wrap S, 3-5 wrap T (wrapMode()),
    // 6-8 the min filter function, 9-11 the mag filter (filterLinear())
    std::map<int, uint32_t> samplers;
    // parameter -> 1 to 4 floats. The ids index MVGL's shader parameter table (0x33 DiffuseColor,
    // 0x36 Bumpiness, 0x38 SpecularStrength, 0x39 SpecularPower, 0x3e SurfaceColor, 0x3f
    // FuzzySpecColor, 0x40 SubColor, 0x41 RollOff, 0x42 VelvetStrength, 0x47 OverlayStrength, 0x54
    // Time, 0x55 ScrollSpeedSet1, 0x58 ScrollSpeedSet2, 0x5e OffsetSet1, 0x61 OffsetSet2, 0x64
    // DistortionStrength); textures the same (0x32 ColorSampler, 0x35 NormalSampler, 0x43
    // LightSampler, 0x44 OverlayColorSampler, 0x48 CLUTSampler)
    std::map<int, std::vector<float>> params;
    // render states (kind 100; RenderContext's table): id -> 4 GL values. 0x78 alpha_func (func,
    // ref as a float), 0x79 alpha_test_enable, 0x7a blend_func (src, dst), 0x7b blend_equation,
    // 0x7c blend_enable, 0x7d cull_face, 0x7e cull_face_enable, 0x7f depth_func, 0x80 depth_mask,
    // 0x81 depth_test_enable, 0x82 polygon_offset (factor, units), 0x83 polygon_offset_fill_enable, 0x84
    // color_mask, 0x85 stencil_func (func, ref, mask), 0x86 stencil_mask, 0x87 stencil_op (sfail, dpfail,
    // dppass), 0x88 stencil_test_enable; those absent keep the defaults (alpha test and blending
    // off, back faces culled, depth test and writes on)
    std::map<int, std::array<uint32_t, 4>> states;
    bool state(int id, uint32_t fallback, uint32_t& value, int k = 0) const {
        auto it = states.find(id);
        value = it == states.end() ? fallback : it->second[size_t(k)];
        return it != states.end();
    }
    std::string diffuse() const { return texture(0x32); }
    std::string texture(int id) const { auto it = textures.find(id); return it == textures.end() ? "" : it->second; }
    // a parameter's floats (`n` of them; those it lacks keep `out`'s)
    void param(int id, float* out, size_t n) const {
        auto it = params.find(id);
        if (it != params.end())
            for (size_t k = 0; k < n && k < it->second.size(); ++k) out[k] = it->second[k];
    }
    uint32_t sampler(int id) const { auto it = samplers.find(id); return it == samplers.end() ? 0x58au : it->second; }
};

// a wrap code (the table at 0x53b6a4 in the Android library): 2 mirrored repeat, 3 clamp to edge,
// else repeat
enum class Wrap { Repeat, Mirror, Clamp };
inline Wrap wrapMode(uint32_t code) { return code == 2 ? Wrap::Mirror : code == 3 ? Wrap::Clamp : Wrap::Repeat; }
inline Wrap wrapS(uint32_t flags) { return wrapMode(flags & 7); }
inline Wrap wrapT(uint32_t flags) { return wrapMode((flags >> 3) & 7); }
// the filters (AdjustTextureFilter and the table at 0x53b688): functions 1, 3, 4 are nearest
inline bool minLinear(uint32_t flags) { const uint32_t f = (flags >> 6) & 7; return f != 1 && f != 3 && f != 4; }
inline bool magLinear(uint32_t flags) { return ((flags >> 9) & 7) != 1; }

// A light the figure carries (header +8 count, +0x38 records of 0x34 bytes; Figure::SetupLights,
// RenderContaindLights): maps' <map>c figures light the characters, <map>b battles, *_evt_light
// events. The joint named `hash` places it: a directional light shines towards its +Z axis's
// direction (DirLamp01Dir = that axis, normalised), an ambient light's sky is towards its position.
struct GeomLight {
    enum Type : uint16_t { Point = 0, Ambient = 2, Directional = 3, Fog = 4 };
    uint32_t hash = 0;  // +0 the joint's name hash
    uint16_t type = 0;  // +4
    uint16_t index = 0; // +6 which light of its kind (DirLamp01..03, PointLamp01..07)
    // +8..: directional / point / ambient: intensity, (range), colour[4] at +0x10, ambient's
    // ground colour[3] at +0x20; fog: far, near, colour[3] at +0x10, alpha at +0x1c
    float v[10] = {};
    float intensity() const { return v[0]; }
    const float* colour() const { return &v[2]; }
    const float* ground() const { return &v[6]; }
};

// A camera the figure carries (header +0x0a count, +0x3c records of 0x24 bytes;
// Figure::SetupCameras, GetContaindCamera): the maps' <map>c figures hold theirs. The joint named
// `hash` places it, looking down its -Z axis.
struct GeomCamera {
    uint32_t hash = 0;     // +0x00 the joint's name hash
    float fovY = 45;       // +0x04 degrees, vertical
    float near = 1, far = 5000; // +0x0c, +0x10
    float orthoSize = 0;   // +0x14 (half height)
    bool ortho = false;    // +0x18
};

struct Geom {
    uint32_t version = 0; // 0x3c (iPad) or 0x50
    std::vector<GeomMesh> meshes;
    std::vector<GeomMaterial> materials;
    std::vector<GeomLight> lights;
    std::vector<GeomCamera> cameras;
    std::vector<Mat34> inverseBind; // one per skeleton joint (+0xc: their count)
    float boundsMin[3] = {0, 0, 0}, boundsMax[3] = {0, 0, 0}; // over every mesh's positions
};

bool parseGeom(const std::vector<uint8_t>& file, Geom& out, std::string* error = nullptr);

} // namespace cr2
