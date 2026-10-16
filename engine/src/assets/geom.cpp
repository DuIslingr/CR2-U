#include "assets/geom.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cr2 {

namespace {

bool fail(std::string* error, const char* what) {
    if (error) *error = what;
    return false;
}

// vertex attribute semantics (docs/FORMATS.md)
enum Semantic { kPosition = 1, kNormal = 2, kTangent = 3, kBinormal = 4, kUv0 = 5, kUv1 = 6, kColour = 9, kJoints = 10, kWeights = 11 };

float halfToFloat(uint16_t h) {
    const uint32_t sign = uint32_t(h >> 15) << 31, exp = (h >> 10) & 31, man = h & 1023;
    uint32_t bits;
    if (exp == 0) {
        if (man == 0) bits = sign;
        else { // subnormal
            int e = -1;
            uint32_t m = man;
            do { ++e; m <<= 1; } while (!(m & 1024));
            bits = sign | uint32_t(127 - 15 - e) << 23 | (m & 1023) << 13;
        }
    } else if (exp == 31) bits = sign | 0x7f800000u | man << 13;
    else bits = sign | (exp + 127 - 15) << 23 | man << 13;
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

} // namespace

std::vector<uint32_t> GeomMesh::triangles() const {
    std::vector<uint32_t> out;
    out.reserve(strip.size() * 3);
    for (size_t i = 2; i < strip.size(); ++i) {
        const uint32_t a = strip[i - 2], b = strip[i - 1], c = strip[i];
        if (a == b || b == c || a == c) continue;
        if (i % 2 == 0) out.insert(out.end(), {a, b, c});
        else out.insert(out.end(), {b, a, c});
    }
    return out;
}

bool parseGeom(const std::vector<uint8_t>& f, Geom& out, std::string* error) {
    out = Geom();
    auto has = [&](size_t at, size_t n) { return at <= f.size() && n <= f.size() - at; };
    auto u32 = [&](size_t at) { uint32_t v = 0; if (has(at, 4)) std::memcpy(&v, &f[at], 4); return v; };
    auto u16 = [&](size_t at) { uint16_t v = 0; if (has(at, 2)) std::memcpy(&v, &f[at], 2); return v; };
    if (!has(0, 0x40)) return fail(error, "geom too short");
    out.version = u32(0);
    if (out.version != 0x3c && out.version != 0x50) return fail(error, "unknown geom version");
    const unsigned meshes = u16(4), materials = u16(6);
    const size_t meshAt = u32(0x30), materialAt = u32(0x34);
    if (!has(meshAt, size_t(meshes) * 0x54)) return fail(error, "geom meshes outside the file");
    for (int c = 0; c < 3; ++c) out.boundsMin[c] = 1e30f, out.boundsMax[c] = -1e30f;
    for (unsigned i = 0; i < meshes; ++i) {
        const size_t r = meshAt + 0x54 * size_t(i);
        GeomMesh m;
        const unsigned attrs = u16(r + 2), stride = u16(r + 4), indexType = u16(r + 6), prim = u16(r + 0x0a);
        m.material = u32(r + 0x10);
        m.vertexCount = u32(r + 0x14);
        const uint32_t indices = u32(r + 0x18);
        const size_t vAt = u32(r + 0x24), iAt = u32(r + 0x28), nodeAt = u32(r + 0x2c), aAt = u32(r + 0x34);
        // the iPad / Android: GL enums (0x1403 u16 indices, 5 a strip); the Vita: GXM ones (0 u16
        // indices; the primitive 0, but the indices are the same strips)
        const bool gxm = indexType == 0 && prim == 0;
        if (!gxm && (indexType != 0x1403 || prim != 5)) return fail(error, "geom mesh not a u16 triangle strip");
        if (!has(iAt, size_t(indices) * 2) || !has(aAt, size_t(attrs) * 8)) return fail(error, "geom mesh data outside the file");
        // the bytes a vertex's attributes use (the Vita's files leave out the last vertex's padding)
        size_t used = 0;
        for (unsigned k = 0; k < attrs; ++k) {
            const size_t d = aAt + 8 * size_t(k);
            const unsigned t = u16(d + 4), bytes = t == 0x1406 || t == 9 ? 4 : t == 0x1402 || t == 0x1403 || (t >= 2 && t <= 3) || (t >= 6 && t <= 8) ? 2 : 1;
            used = std::max(used, size_t(u16(d + 6)) + size_t(bytes) * u16(d + 2));
        }
        // The Vita's files: some meshes say a stride 2 bytes over what their attributes use and pack
        // their vertices at the used size (the 427 proxy meshes, `p` files: 24 for 22; one more),
        // others are padded to the stride they say (347). The record doesn't tell them apart; the
        // positions do - read at the wrong stride, they leave the mesh's bounding sphere (+0x38
        // centre, +0x50 radius; +0x44 half extents, not exact)
        unsigned vstride = stride;
        if (gxm && used < stride && m.vertexCount > 1) {
            size_t posAt = 0;
            for (unsigned k = 0; k < attrs; ++k)
                if (f[aAt + 8 * size_t(k)] == kPosition) posAt = u16(aAt + 8 * size_t(k) + 6);
            float centre[3], radius;
            std::memcpy(centre, &f[r + 0x38], 12);
            std::memcpy(&radius, &f[r + 0x50], 4);
            auto plausible = [&](size_t st) { // every position within the mesh's bounding sphere
                if (!has(vAt, size_t(m.vertexCount - 1) * st + used)) return false;
                for (uint32_t v = 0; v < m.vertexCount; ++v) {
                    float x[3];
                    std::memcpy(x, &f[vAt + size_t(v) * st + posAt], 12);
                    const float d2 = (x[0] - centre[0]) * (x[0] - centre[0]) + (x[1] - centre[1]) * (x[1] - centre[1]) + (x[2] - centre[2]) * (x[2] - centre[2]);
                    if (!(d2 <= radius * radius * 1.0201f + 1e-6f)) return false; // (NaN fails too)
                }
                return true;
            };
            if (!plausible(stride) && plausible(used)) vstride = unsigned(used);
        }
        if (m.vertexCount && !has(vAt, size_t(m.vertexCount - 1) * vstride + used)) return fail(error, "geom vertices outside the file");
        if (nodeAt) m.node = u32(nodeAt);
        m.strip.resize(indices);
        if (indices) std::memcpy(m.strip.data(), &f[iAt], size_t(indices) * 2);
        for (uint16_t ix : m.strip)
            if (ix >= m.vertexCount) return fail(error, "geom index past the vertices");
        for (unsigned k = 0; k < attrs; ++k) {
            const size_t d = aAt + 8 * size_t(k);
            const int sem = f[d], rawNorm = f[d + 1], count = u16(d + 2), rawType = u16(d + 4), offset = u16(d + 6);
            // GXM attribute formats -> the GL types they match (and normalisation): 0 U8, 1 S8, 2 U16,
            // 3 S16, 4 U8N, 5 S8N, 6 U16N, 7 S16N, 8 F16, 9 F32
            static const int kGxmType[10] = {0x1401, 0x1400, 0x1403, 0x1402, 0x1401, 0x1400, 0x1403, 0x1402, 0x140b, 0x1406};
            int type = rawType, norm = rawNorm;
            if (gxm) {
                if (rawType > 9) return fail(error, "geom attribute format not understood");
                type = kGxmType[rawType];
                norm = (rawType >= 4 && rawType <= 7) || ((sem == kColour) && rawType == 0); // (colours: U8, read as 0..1)
            }
            const int size = type == 0x1406 ? 4 : type == 0x1402 || type == 0x1403 || type == 0x140b ? 2 : type == 0x1400 || type == 0x1401 ? 1 : 0;
            if (!size || count < 1 || count > 4 || size_t(offset) + size_t(size) * count > stride) return fail(error, "geom attribute not understood");
            std::vector<float>* dst = sem == kPosition ? &m.position : sem == kNormal ? &m.normal : sem == kUv0 ? &m.uv
                                    : sem == kUv1 ? &m.uv1 : sem == kColour ? &m.colour : sem == kWeights ? &m.weights : nullptr;
            const int want = sem == kPosition || sem == kNormal ? 3 : sem == kUv0 || sem == kUv1 ? 2 : 4;
            if (sem == kJoints) m.joints.assign(size_t(m.vertexCount) * 4, 0);
            if (dst) dst->assign(size_t(m.vertexCount) * want, sem == kColour ? 1.0f : 0.0f);
            if (!dst && sem != kJoints) continue; // (tangents, binormals: not used yet)
            for (uint32_t v = 0; v < m.vertexCount; ++v) {
                const uint8_t* p = &f[vAt + size_t(v) * vstride + offset];
                for (int c = 0; c < count && c < want; ++c) {
                    float x;
                    switch (type) {
                    case 0x1406: std::memcpy(&x, p + 4 * c, 4); break;
                    case 0x140b: { uint16_t hf; std::memcpy(&hf, p + 2 * c, 2); x = halfToFloat(hf); break; }
                    case 0x1402: { int16_t s; std::memcpy(&s, p + 2 * c, 2); x = norm ? std::max(-1.0f, s / 32767.0f) : float(s); break; }
                    case 0x1403: { uint16_t s; std::memcpy(&s, p + 2 * c, 2); x = norm ? s / 65535.0f : float(s); break; }
                    case 0x1400: x = norm ? std::max(-1.0f, int8_t(p[c]) / 127.0f) : float(int8_t(p[c])); break;
                    default: x = norm ? p[c] / 255.0f : float(p[c]); break;
                    }
                    if (sem == kJoints) m.joints[size_t(v) * 4 + c] = p[c];
                    // texture coordinates as shorts are 1024ths (the vertex shader divides); as halves, final
                    else (*dst)[size_t(v) * want + c] = (sem == kUv0 || sem == kUv1) && type != 0x140b ? x / 1024.0f : x;
                }
            }
        }
        if (m.position.empty()) return fail(error, "geom mesh without positions");
        for (uint32_t v = 0; v < m.vertexCount; ++v)
            for (int c = 0; c < 3; ++c) {
                out.boundsMin[c] = std::min(out.boundsMin[c], m.position[size_t(v) * 3 + c]);
                out.boundsMax[c] = std::max(out.boundsMax[c], m.position[size_t(v) * 3 + c]);
            }
        out.meshes.push_back(std::move(m));
    }
    size_t p = materialAt;
    for (unsigned i = 0; i < materials; ++i) {
        if (!has(p, 0x18)) return fail(error, "geom materials outside the file");
        const unsigned n = unsigned(f[p + 0x14]) + f[p + 0x15];
        if (!has(p + 0x18, size_t(n) * 0x14)) return fail(error, "geom material entries outside the file");
        GeomMaterial mat;
        mat.hash = u32(p);
        for (unsigned k = 0; k < n; ++k) {
            const uint8_t* e = &f[p + 0x18 + 0x14 * size_t(k)];
            const int id = e[0], kind = e[1];
            if (kind == 0) mat.textures[id] = std::string(reinterpret_cast<const char*>(e + 4), strnlen(reinterpret_cast<const char*>(e + 4), 12));
            else if (kind == 1 || kind == 3 || kind == 4) {
                std::vector<float> v(static_cast<size_t>(kind));
                std::memcpy(v.data(), e + 4, 4 * size_t(kind));
                mat.params[id] = std::move(v);
            }
        }
        out.materials.push_back(std::move(mat));
        p += 0x18 + 0x14 * size_t(n);
    }
    if (out.meshes.empty())
        for (int c = 0; c < 3; ++c) out.boundsMin[c] = out.boundsMax[c] = 0;
    return true;
}

} // namespace cr2
