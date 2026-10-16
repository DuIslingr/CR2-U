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
    std::vector<float> uv;       // 2 a vertex, already / 1024 (empty: none)
    std::vector<float> uv1;
    std::vector<float> colour;   // 4 a vertex, 0..1 (empty: none)
    // blend indices, 4 a vertex (empty: none - the mesh is rigid, on palette[0]): palette slot * 3
    // (MatrixPalette's rows), as the vertex shaders index
    std::vector<uint8_t> joints;
    std::vector<float> weights;  // blend weights, 4 a vertex, used as stored (no remainder implied)
    std::vector<uint16_t> strip; // the indices as stored (a triangle strip)
    // the strip as a triangle list, degenerate triangles dropped, every other one turned so all
    // keep the strip's winding
    std::vector<uint32_t> triangles() const;
};

struct GeomMaterial {
    uint32_t hash = 0;
    std::map<int, std::string> textures;     // parameter -> image name: 0x32 diffuse, 0x35 normal map
    std::map<int, std::vector<float>> params; // parameter -> 1, 3 or 4 floats (0x33: colour)
    // render states (kind 100; RenderContext's table): id -> 4 GL values. 0x78 alpha_func (func,
    // ref as a float), 0x79 alpha_test_enable, 0x7a blend_func (src, dst), 0x7b blend_equation,
    // 0x7c blend_enable, 0x7d cull_face, 0x7e cull_face_enable, 0x7f depth_func, 0x80 depth_mask,
    // 0x81 depth_test_enable, ... 0x88; those absent keep the defaults (alpha test and blending
    // off, back faces culled, depth test and writes on)
    std::map<int, std::array<uint32_t, 4>> states;
    bool state(int id, uint32_t fallback, uint32_t& value, int k = 0) const {
        auto it = states.find(id);
        value = it == states.end() ? fallback : it->second[size_t(k)];
        return it != states.end();
    }
    std::string diffuse() const { auto it = textures.find(0x32); return it == textures.end() ? "" : it->second; }
};

struct Geom {
    uint32_t version = 0; // 0x3c (iPad) or 0x50
    std::vector<GeomMesh> meshes;
    std::vector<GeomMaterial> materials;
    std::vector<Mat34> inverseBind; // one per skeleton joint (+0xc: their count)
    float boundsMin[3] = {0, 0, 0}, boundsMax[3] = {0, 0, 0}; // over every mesh's positions
};

bool parseGeom(const std::vector<uint8_t>& file, Geom& out, std::string* error = nullptr);

} // namespace cr2
