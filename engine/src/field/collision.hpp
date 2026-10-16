#pragma once
// Field collision: MVGL's `.phys` scenes (PhysicsSceneLoader::LoadScene; docs/FORMATS.md) - Bullet
// rigid bodies with box, sphere or triangle-mesh shapes, each triangle naming a surface (the
// floors' footstep sound, `se02`; the walls, `MTR_kabe`) - and the queries the character needs:
// a moving sphere against them, and the triangles a sphere overlaps.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace cr2 {

using Vec3 = std::array<float, 3>;

struct PhysShape {
    enum Kind { Box = 0, Sphere = 1, Mesh = 2 } kind = Mesh;
    Vec3 halfExtents{};   // Box
    float radius = 0;     // Sphere
    std::vector<Vec3> vertices;              // Mesh
    std::vector<std::array<uint32_t, 3>> triangles;
    std::vector<uint32_t> surfaces;          // per triangle (Box / Sphere: one, [0])
};

struct PhysBody {
    std::string name;
    Vec3 position{};
    std::array<float, 4> rotation{0, 0, 0, 1}; // quaternion x y z w
    float friction[3] = {0, 0, 0};             // (+0x1c: friction, restitution, ...)
    uint32_t shape = 0;
    bool contactResponse = true;               // false: a trigger (passed through)
};

struct PhysScene {
    std::vector<std::string> surfaces; // 64-byte names
    std::vector<PhysShape> shapes;
    std::vector<PhysBody> bodies;
};

bool parsePhys(const std::vector<uint8_t>& file, PhysScene& out, std::string* error = nullptr);

// The solid triangles of one or more scenes, in world space (boxes as their 12 triangles; spheres
// left out - none of the fields' solid bodies is one)
class CollisionWorld {
public:
    void addScene(const PhysScene& scene);
    size_t triangles() const { return tris_.size(); }

    struct Hit {
        float fraction = 1;  // of the move
        Vec3 normal{};       // the surface's, towards the sphere
        uint32_t triangle = 0;
    };
    // The first triangle a sphere of `radius` meets moving from `from` to `to` (one it already
    // touches: at once, if the move goes into it), counting only hits whose normal . `up` >=
    // minDot - the filter Bullet's character controller gives its sweeps. False: none.
    bool sweep(const Vec3& from, const Vec3& to, float radius, const Vec3& up, float minDot, Hit& hit) const;

    // The triangles within `radius` of `centre`: the closest point's direction (towards the
    // centre) and depth
    struct Contact {
        Vec3 normal{};
        float depth = 0;
        uint32_t triangle = 0;
    };
    std::vector<Contact> overlaps(const Vec3& centre, float radius) const;

    // The surface name under a point (a ray straight down, within `depth`): "" none
    std::string surfaceBelow(const Vec3& p, float depth) const;

private:
    struct Tri {
        Vec3 a, b, c, n; // n: unit normal
        std::string surface;
    };
    std::vector<Tri> tris_;
};

} // namespace cr2
