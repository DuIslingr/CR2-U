#pragma once
// The model bridge: a figure (.geom + .skel) as glTF 2.0 for editing (Blender and the like), and a
// glTF model in place of a figure's meshes. See docs/FORMATS.md ("Replacement models").
//
// Export (cr2view --export-models): one .glb per figure - the skeleton's joints ("node_<i>_<name>",
// their base pose), the meshes in their bind space skinned to them (inverse bind matrices = the
// figure's), normals and tangents, one material per figure material ("cr_mat_<i>_<name>", its
// colour picture as "tex_<image>", its normal map as the normal texture) and the figure's
// animations sampled at their own rate (only the channels that move).
//
// Replacement (models/hd/<figure>.glb or .gltf): its meshes stand in for the figure's; the figure
// keeps its skeleton, animations, materials (shaders, render states, parameters) and lights.
// Joints are matched to the skeleton by name ("node_<i>..."); a joint that isn't one goes with its
// nearest ancestor that is, and a mesh without a skin rides on its node's. A material named
// "cr_mat_<i>..." is material i - with a picture of its own (an image not named "tex_<name of one
// of the figure's pictures>") as its colour or normal map in a copy of it; other materials are the
// figure's first. Missing normals and (for normal-mapped materials) tangents are worked out;
// meshes on more than 85 joints are split (the palette's byte-sized indices).

#include <string>
#include <utility>
#include <vector>

#include "assets/geom.hpp"
#include "assets/image.hpp"
#include "assets/skeleton.hpp"

namespace cr2 {

struct FigureExport {
    const Geom* geom = nullptr;
    const Skeleton* skeleton = nullptr;
    std::vector<std::string> jointNames, materialNames;          // (<figure>.name; empty: numbers only)
    std::vector<std::pair<std::string, Image>> images;            // the materials' pictures by name
    std::vector<std::pair<std::string, const Animation*>> anims;  // the figure's animations
    bool absolute = false; // its animations replace the base pose (a map's, an object's) rather than move over it
};

struct GltfStats {
    int meshes = 0, vertices = 0, triangles = 0, images = 0, animations = 0, channels = 0, unmatchedJoints = 0;
    size_t bytes = 0;
};

bool exportFigureGlb(const FigureExport& in, const std::string& path, std::string* error = nullptr, GltfStats* stats = nullptr);

// Replaces `geom`'s meshes with the glTF file's (see above). The pictures it brings come back in
// `images`, named `imagePrefix` + a number, and the materials' copies that use them are appended
// to geom.materials. False (the geom untouched) when it can't be read or holds no triangles.
// (`sources`, for tests: each new mesh's vertices' origin - the glTF primitive, in file order, and its vertex)
bool applyFigureReplacement(Geom& geom, const Skeleton& skeleton, const std::string& path, const std::string& imagePrefix,
                            std::vector<std::pair<std::string, Image>>& images, std::string* error = nullptr,
                            GltfStats* stats = nullptr, std::vector<std::vector<std::pair<int, uint32_t>>>* sources = nullptr);

} // namespace cr2
