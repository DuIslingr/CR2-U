#pragma once
// MVGL figure meshes (`.geom`; MVGL::Draw::Figure::Build / BuildMesh / DrawMesh, docs/FORMATS.md):
// the iPad's version 0x3c and Android / the Vita's 0x50 (a header 0x10 bytes longer, the records
// alike). Each mesh describes its own vertex attributes; they are unpacked here to floats.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace cr2 {

struct GeomMesh {
    uint32_t material = 0;
    uint32_t node = 0;           // the skeleton's node it hangs from (+0x2c)
    uint32_t vertexCount = 0;
    std::vector<float> position; // 3 a vertex
    std::vector<float> normal;   // 3 a vertex (empty: none)
    std::vector<float> uv;       // 2 a vertex, already / 1024 (empty: none)
    std::vector<float> uv1;
    std::vector<float> colour;   // 4 a vertex, 0..1 (empty: none)
    std::vector<uint8_t> joints; // blend indices, 4 a vertex (empty: none)
    std::vector<float> weights;  // blend weights, 4 a vertex
    std::vector<uint16_t> strip; // the indices as stored (a triangle strip)
    // the strip as a triangle list, degenerate triangles dropped, every other one turned so all
    // keep the strip's winding
    std::vector<uint32_t> triangles() const;
};

struct GeomMaterial {
    uint32_t hash = 0;
    std::map<int, std::string> textures;     // parameter -> image name: 0x32 diffuse, 0x35 normal map
    std::map<int, std::vector<float>> params; // parameter -> 1, 3 or 4 floats (0x33: colour)
    std::string diffuse() const { auto it = textures.find(0x32); return it == textures.end() ? "" : it->second; }
};

struct Geom {
    uint32_t version = 0; // 0x3c (iPad) or 0x50
    std::vector<GeomMesh> meshes;
    std::vector<GeomMaterial> materials;
    float boundsMin[3] = {0, 0, 0}, boundsMax[3] = {0, 0, 0}; // over every mesh's positions
};

bool parseGeom(const std::vector<uint8_t>& file, Geom& out, std::string* error = nullptr);

} // namespace cr2
