#pragma once
// Smoothed models (optional, Options > Graphics > Smooth Models; CR1-U's, scene/smooth.cpp there):
// PN triangles (Vlachos et al. 2001) at load time, on the CPU. Each triangle becomes level^2
// triangles on a cubic patch through its corners: the original vertices stay put (feet on the
// floor, joints where they were) while silhouettes round out. The patch's normals come from the
// faces: a corner averages the faces around it within `creaseDegrees` of its own, and edges
// sharper than that - and mesh borders - stay straight, so hard edges (a wall's, a box's) stay
// hard and separate meshes still meet. The new vertices' normals, tangents, UVs and colours are
// blended from the corners, their joints and weights too (the heaviest four), so skinning and
// animation work as before. A mesh is smoothed in its own space (its bind pose) after
// GeomMesh::resolveJoints; it comes out a triangle list (GeomMesh::list).

#include "assets/geom.hpp"

namespace cr2 {

struct SmoothStats {
    int meshes = 0, trianglesBefore = 0, trianglesAfter = 0;
};

// `m` smoothed in place (level 2 = 4x triangles, 3 = 9x); false if it was left as it is
bool smoothMesh(GeomMesh& m, int level, float creaseDegrees = 60.0f, SmoothStats* stats = nullptr);

} // namespace cr2
