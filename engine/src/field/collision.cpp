#include "field/collision.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cr2 {

namespace {

bool fail(std::string* error, const char* what) {
    if (error) *error = what;
    return false;
}

Vec3 sub(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3 add(const Vec3& a, const Vec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec3 mul(const Vec3& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float length(const Vec3& a) { return std::sqrt(dot(a, a)); }

Vec3 rotate(const std::array<float, 4>& q, const Vec3& v) {
    const Vec3 u{q[0], q[1], q[2]};
    const Vec3 t = mul(cross(u, v), 2.0f);
    return add(add(v, mul(t, q[3])), cross(u, t));
}

// the point of triangle abc closest to p (Ericson, Real-Time Collision Detection 5.1.5)
Vec3 closestOnTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
    const Vec3 ab = sub(b, a), ac = sub(c, a), ap = sub(p, a);
    const float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return a;
    const Vec3 bp = sub(p, b);
    const float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return b;
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return add(a, mul(ab, d1 / (d1 - d3)));
    const Vec3 cp = sub(p, c);
    const float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return c;
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return add(a, mul(ac, d2 / (d2 - d6)));
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return add(b, mul(sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
    const float denom = 1.0f / (va + vb + vc);
    return add(a, add(mul(ab, vb * denom), mul(ac, vc * denom)));
}

// the first t in [0, 1] at which a ray o + t d is at `r` from point p (the sphere meeting a vertex)
bool rayToPoint(const Vec3& o, const Vec3& d, const Vec3& p, float r, float& t) {
    const Vec3 m = sub(o, p);
    const float a = dot(d, d), b = dot(m, d), c = dot(m, m) - r * r;
    if (a <= 0 || c <= 0) return false;
    const float disc = b * b - a * c;
    if (disc < 0) return false;
    t = (-b - std::sqrt(disc)) / a;
    return t >= 0 && t <= 1;
}

// ... at `r` from the segment pq (the sphere meeting an edge)
bool rayToSegment(const Vec3& o, const Vec3& d, const Vec3& p, const Vec3& q, float r, float& t) {
    const Vec3 e = sub(q, p), m = sub(o, p);
    const float ee = dot(e, e);
    if (ee <= 0) return false;
    // the components across the edge
    const Vec3 dd = sub(d, mul(e, dot(d, e) / ee)), mm = sub(m, mul(e, dot(m, e) / ee));
    const float a = dot(dd, dd), b = dot(mm, dd), c = dot(mm, mm) - r * r;
    if (a <= 0 || c <= 0) return false;
    const float disc = b * b - a * c;
    if (disc < 0) return false;
    t = (-b - std::sqrt(disc)) / a;
    if (t < 0 || t > 1) return false;
    const float s = dot(add(m, mul(d, t)), e) / ee;
    return s >= 0 && s <= 1;
}

} // namespace

bool parsePhys(const std::vector<uint8_t>& f, PhysScene& out, std::string* error) {
    out = PhysScene();
    auto has = [&](size_t at, size_t n) { return at <= f.size() && n <= f.size() - at; };
    auto u32 = [&](size_t at) { uint32_t v = 0; if (has(at, 4)) std::memcpy(&v, &f[at], 4); return v; };
    auto f32 = [&](size_t at) { float v = 0; if (has(at, 4)) std::memcpy(&v, &f[at], 4); return v; };
    if (!has(0, 0x20) || std::memcmp(f.data(), "SYHP", 4) != 0) return fail(error, "not a PHYS scene"); // ("PHYS" as a u32)
    const uint32_t bodies = u32(8), shapes = u32(0xc), names = u32(0x10);
    const size_t bodyAt = u32(0x14), shapeAt = u32(0x18), nameAt = u32(0x1c);
    if (!has(bodyAt, size_t(bodies) * 0x4c) || !has(shapeAt, size_t(shapes) * 4) || !has(nameAt, size_t(names) * 64))
        return fail(error, "PHYS tables outside the file");
    for (uint32_t k = 0; k < names; ++k) {
        const char* s = reinterpret_cast<const char*>(&f[nameAt + 64 * size_t(k)]);
        out.surfaces.emplace_back(s, strnlen(s, 64));
    }
    for (uint32_t k = 0; k < shapes; ++k) {
        const size_t at = u32(shapeAt + 4 * size_t(k));
        PhysShape s;
        s.kind = PhysShape::Kind(u32(at));
        if (s.kind == PhysShape::Box) {
            for (int c = 0; c < 3; ++c) s.halfExtents[size_t(c)] = f32(at + 4 + 4 * size_t(c));
            s.surfaces.push_back(u32(at + 0x10));
        } else if (s.kind == PhysShape::Sphere) {
            s.radius = f32(at + 4);
            s.surfaces.push_back(u32(at + 8));
        } else if (s.kind == PhysShape::Mesh) {
            const uint32_t nv = u32(at + 4), nt = u32(at + 8);
            const size_t io = u32(at + 0x24), vo = u32(at + 0x28), so = u32(at + 0x2c);
            if (!has(io, size_t(nt) * 12) || !has(vo, size_t(nv) * 12) || !has(so, size_t(nt) * 4))
                return fail(error, "PHYS mesh outside the file");
            for (uint32_t v = 0; v < nv; ++v) s.vertices.push_back({f32(vo + 12 * v), f32(vo + 12 * v + 4), f32(vo + 12 * v + 8)});
            for (uint32_t t = 0; t < nt; ++t) {
                const std::array<uint32_t, 3> tri{u32(io + 12 * t), u32(io + 12 * t + 4), u32(io + 12 * t + 8)};
                if (tri[0] >= nv || tri[1] >= nv || tri[2] >= nv) return fail(error, "PHYS triangle past the vertices");
                s.triangles.push_back(tri);
                s.surfaces.push_back(u32(so + 4 * t));
            }
        } else {
            return fail(error, "PHYS shape of an unknown kind");
        }
        out.shapes.push_back(std::move(s));
    }
    for (uint32_t k = 0; k < bodies; ++k) {
        const size_t at = bodyAt + 0x4c * size_t(k);
        PhysBody b;
        for (int c = 0; c < 3; ++c) b.position[size_t(c)] = f32(at + 4 * size_t(c));
        for (int c = 0; c < 4; ++c) b.rotation[size_t(c)] = f32(at + 0xc + 4 * size_t(c));
        for (int c = 0; c < 3; ++c) b.friction[c] = f32(at + 0x1c + 4 * size_t(c));
        b.shape = u32(at + 0x2c);
        b.contactResponse = u32(at + 0x30) != 0;
        const char* n = reinterpret_cast<const char*>(&f[at + 0x34]);
        b.name.assign(n, strnlen(n, 0x18));
        if (b.shape >= out.shapes.size()) return fail(error, "PHYS body with no shape");
        out.bodies.push_back(std::move(b));
    }
    return true;
}

void CollisionWorld::addScene(const PhysScene& scene) {
    for (const PhysBody& b : scene.bodies) {
        if (!b.contactResponse) continue;
        const PhysShape& s = scene.shapes[b.shape];
        auto world = [&](const Vec3& v) { return add(rotate(b.rotation, v), b.position); };
        auto push = [&](const Vec3& a, const Vec3& bb, const Vec3& c, uint32_t surface) {
            Tri t{world(a), world(bb), world(c), {}, surface < scene.surfaces.size() ? scene.surfaces[surface] : std::string()};
            const Vec3 n = cross(sub(t.b, t.a), sub(t.c, t.a));
            const float l = length(n);
            if (l <= 0) return; // (degenerate)
            t.n = mul(n, 1.0f / l);
            tris_.push_back(std::move(t));
        };
        if (s.kind == PhysShape::Mesh) {
            for (size_t k = 0; k < s.triangles.size(); ++k)
                push(s.vertices[s.triangles[k][0]], s.vertices[s.triangles[k][1]], s.vertices[s.triangles[k][2]], s.surfaces[k]);
        } else if (s.kind == PhysShape::Box) {
            const Vec3& h = s.halfExtents;
            Vec3 c[8];
            for (int k = 0; k < 8; ++k) c[k] = {(k & 1) ? h[0] : -h[0], (k & 2) ? h[1] : -h[1], (k & 4) ? h[2] : -h[2]};
            static const int kFaces[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
            for (const auto& q : kFaces) {
                push(c[q[0]], c[q[1]], c[q[2]], s.surfaces[0]);
                push(c[q[0]], c[q[2]], c[q[3]], s.surfaces[0]);
            }
        }
    }
}

bool CollisionWorld::sweep(const Vec3& from, const Vec3& to, float r, const Vec3& up, float minDot, Hit& hit) const {
    const Vec3 d = sub(to, from);
    bool any = false;
    hit = Hit();
    for (size_t k = 0; k < tris_.size(); ++k) {
        const Tri& t = tris_[k];
        const Vec3 close = closestOnTriangle(from, t.a, t.b, t.c);
        float best = 2;
        Vec3 normal{};
        const Vec3 away = sub(from, close);
        const float dist = length(away);
        if (dist < r + 1e-6f) { // touching already: a hit now if the move goes into it, else none
            const Vec3 n0 = dist > 1e-7f ? mul(away, 1.0f / dist) : t.n;
            if (dot(d, n0) >= 0 || dot(n0, up) < minDot) continue;
            if (!any || hit.fraction > 0) {
                hit.fraction = 0;
                hit.normal = n0;
                hit.triangle = uint32_t(k);
                any = true;
            }
            continue;
        }
        // the face, from the side the sphere is on (Bullet's meshes are two-sided)
        Vec3 n = t.n;
        float d0 = dot(sub(from, t.a), n);
        if (d0 < 0) n = mul(n, -1), d0 = -d0;
        const float d1 = dot(sub(to, t.a), n);
        if (d1 < r && d0 > d1) {
            const float tf = (d0 - r) / (d0 - d1);
            if (tf >= 0 && tf <= 1) {
                const Vec3 p = sub(add(from, mul(d, tf)), mul(n, r));
                const Vec3 cp = closestOnTriangle(p, t.a, t.b, t.c);
                if (length(sub(p, cp)) < 1e-5f) best = tf, normal = n;
            }
        }
        // its edges and corners
        const Vec3* v[3] = {&t.a, &t.b, &t.c};
        for (int e = 0; e < 3; ++e) {
            float te;
            if (rayToSegment(from, d, *v[e], *v[(e + 1) % 3], r, te) && te < best) {
                const Vec3 c = add(from, mul(d, te));
                const Vec3 q = closestOnTriangle(c, t.a, t.b, t.c);
                const Vec3 dir = sub(c, q);
                const float l = length(dir);
                if (l > 0) best = te, normal = mul(dir, 1.0f / l);
            }
            if (rayToPoint(from, d, *v[e], r, te) && te < best) {
                const Vec3 dir = sub(add(from, mul(d, te)), *v[e]);
                const float l = length(dir);
                if (l > 0) best = te, normal = mul(dir, 1.0f / l);
            }
        }
        if (best > 1 || dot(normal, up) < minDot) continue;
        if (!any || best < hit.fraction) {
            hit.fraction = best;
            hit.normal = normal;
            hit.triangle = uint32_t(k);
            any = true;
        }
    }
    return any;
}

std::vector<CollisionWorld::Contact> CollisionWorld::overlaps(const Vec3& centre, float r) const {
    std::vector<Contact> out;
    for (size_t k = 0; k < tris_.size(); ++k) {
        const Tri& t = tris_[k];
        const Vec3 dir = sub(centre, closestOnTriangle(centre, t.a, t.b, t.c));
        const float l = length(dir);
        if (l >= r) continue;
        Contact c;
        c.normal = l > 1e-7f ? mul(dir, 1.0f / l) : t.n;
        c.depth = r - l;
        c.triangle = uint32_t(k);
        out.push_back(c);
    }
    return out;
}

std::string CollisionWorld::surfaceBelow(const Vec3& p, float depth) const {
    float best = depth;
    std::string out;
    const Vec3 dir{0, -1, 0};
    for (const Tri& t : tris_) { // (Moller-Trumbore)
        const Vec3 e1 = sub(t.b, t.a), e2 = sub(t.c, t.a), pv = cross(dir, e2);
        const float det = dot(e1, pv);
        if (std::fabs(det) < 1e-12f) continue;
        const float inv = 1.0f / det;
        const Vec3 tv = sub(p, t.a);
        const float u = dot(tv, pv) * inv;
        if (u < 0 || u > 1) continue;
        const Vec3 qv = cross(tv, e1);
        const float v = dot(dir, qv) * inv;
        if (v < 0 || u + v > 1) continue;
        const float dist = dot(e2, qv) * inv;
        if (dist >= 0 && dist < best) best = dist, out = t.surface;
    }
    return out;
}

} // namespace cr2
