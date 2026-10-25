#include "assets/smooth.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace cr2 {

namespace {

struct V3 {
    float x = 0, y = 0, z = 0;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 normalize(V3 a) {
    const float l = std::sqrt(dot(a, a));
    return l > 0 ? a * (1.0f / l) : a;
}
V3 load3(const std::vector<float>& v, size_t i) { return {v[i * 3], v[i * 3 + 1], v[i * 3 + 2]}; }

uint64_t edgeKey(uint32_t a, uint32_t b) {
    if (a > b) std::swap(a, b);
    return uint64_t(a) << 32 | b;
}

struct Patch {
    V3 P[3], e[3][2], c; // corners; edge i (corner i -> i+1): e[i][0] near i, e[i][1] near i+1; centre
};

V3 evalPatch(const Patch& pt, const float* w) {
    const float a = w[0], b = w[1], c = w[2];
    return pt.P[0] * (a * a * a) + pt.P[1] * (b * b * b) + pt.P[2] * (c * c * c) + pt.e[0][0] * (3 * a * a * b) +
           pt.e[0][1] * (3 * a * b * b) + pt.e[1][0] * (3 * b * b * c) + pt.e[1][1] * (3 * b * c * c) +
           pt.e[2][0] * (3 * c * c * a) + pt.e[2][1] * (3 * c * a * a) + pt.c * (6 * a * b * c);
}

struct WeldHash {
    size_t operator()(const std::array<uint32_t, 3>& k) const { return (size_t(k[0]) * 73856093u) ^ (size_t(k[1]) * 19349663u) ^ (size_t(k[2]) * 83492791u); }
};

} // namespace

bool smoothMesh(GeomMesh& m, int level, float creaseDegrees, SmoothStats* stats) {
    const int L = std::min(level, 4);
    if (L < 2) return false;
    const float cosCrease = std::cos(creaseDegrees * 3.14159265f / 180.0f);
    const size_t nRender = m.vertexCount;
    if (nRender == 0 || m.position.size() < nRender * 3) return false;
    const std::vector<uint32_t> tris = m.triangles();
    const size_t nTri = tris.size() / 3;
    if (nTri == 0) return false;
    for (uint32_t i : tris)
        if (i >= nRender) return false; // (malformed: left)
    auto has = [&](const std::vector<float>& v, size_t n) { return v.size() >= nRender * n; };
    const bool hasNormal = has(m.normal, 3), hasTangent = has(m.tangent, 3), hasBinormal = has(m.binormal, 3);
    const bool hasUv = has(m.uv, 2), hasUv1 = has(m.uv1, 2), hasColour = has(m.colour, 4);
    const bool skinned = m.joints.size() >= nRender * 4 && m.weights.size() >= nRender * 4;

    // geometric ids: vertices welded by position (a seam's duplicates one point)
    std::vector<uint32_t> geo(nRender);
    std::vector<float> geoPos;
    {
        std::unordered_map<std::array<uint32_t, 3>, uint32_t, WeldHash> weld;
        weld.reserve(nRender);
        for (size_t r = 0; r < nRender; ++r) {
            std::array<uint32_t, 3> k;
            std::memcpy(k.data(), &m.position[r * 3], 12);
            auto it = weld.emplace(k, uint32_t(weld.size()));
            if (it.second) geoPos.insert(geoPos.end(), {m.position[r * 3], m.position[r * 3 + 1], m.position[r * 3 + 2]});
            geo[r] = it.first->second;
        }
    }
    const size_t nGeo = geoPos.size() / 3;
    auto P = [&](uint32_t g) { return load3(geoPos, g); };
    std::vector<V3> faceN(nTri), unitN(nTri); // (area-weighted; unit)
    for (size_t f = 0; f < nTri; ++f) {
        const V3 a = P(geo[tris[f * 3]]), b = P(geo[tris[f * 3 + 1]]), c = P(geo[tris[f * 3 + 2]]);
        faceN[f] = cross(b - a, c - a) * 0.5f;
        unitN[f] = normalize(faceN[f]);
    }
    std::vector<std::vector<uint32_t>> around(nGeo);
    std::unordered_map<uint64_t, std::vector<uint32_t>> edgeFaces;
    edgeFaces.reserve(nTri * 2);
    for (size_t f = 0; f < nTri; ++f)
        for (int c = 0; c < 3; ++c) {
            const uint32_t a = geo[tris[f * 3 + size_t(c)]], b = geo[tris[f * 3 + size_t((c + 1) % 3)]];
            around[a].push_back(uint32_t(f));
            edgeFaces[edgeKey(a, b)].push_back(uint32_t(f));
        }
    { // (flat - a menu's panel, a decal, a floor tile -: nothing to round)
        bool flat = true;
        const V3* first = nullptr;
        for (size_t f = 0; f < nTri && flat; ++f)
            if (dot(faceN[f], faceN[f]) >= 1e-12f) {
                if (!first) first = &unitN[f];
                else flat = dot(*first, unitN[f]) > 0.9999f;
            }
        if (flat) return false;
    }
    auto degenerate = [&](uint32_t f) { return dot(faceN[f], faceN[f]) < 1e-12f; };
    // a face's corner normal at point g: the faces around g within the crease
    auto cornerNormal = [&](uint32_t f, uint32_t g) {
        V3 n;
        for (uint32_t h : around[g])
            if (!degenerate(h) && dot(unitN[f], unitN[h]) >= cosCrease) n = n + faceN[h];
        return normalize(n);
    };
    // an edge curves when exactly two faces share it, smoothly
    auto smoothEdge = [&](uint32_t a, uint32_t b, uint32_t& f1, uint32_t& f2) {
        const auto it = edgeFaces.find(edgeKey(a, b));
        if (it == edgeFaces.end() || it->second.size() != 2) return false;
        f1 = it->second[0], f2 = it->second[1];
        return !degenerate(f1) && !degenerate(f2) && dot(unitN[f1], unitN[f2]) >= cosCrease;
    };

    // the output: the original vertices first (unchanged), new ones after
    GeomMesh out = m;
    out.positionW.clear(); // (resolveJoints has the joints from it)
    std::vector<uint32_t> outTris;
    auto blend = [&](std::vector<float>& to, const std::vector<float>& from, size_t n, const float* w, const uint32_t* r, bool unit) {
        float v[4] = {};
        for (size_t i = 0; i < n; ++i)
            for (int c = 0; c < 3; ++c) v[i] += w[c] * from[r[c] * n + i];
        if (unit) {
            const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (l > 0) v[0] /= l, v[1] /= l, v[2] /= l;
        }
        to.insert(to.end(), v, v + n);
    };
    auto newVertex = [&](V3 p, const float* w, const uint32_t* r) {
        const uint32_t id = out.vertexCount++;
        out.position.insert(out.position.end(), {p.x, p.y, p.z});
        if (hasNormal) blend(out.normal, m.normal, 3, w, r, true);
        if (hasTangent) blend(out.tangent, m.tangent, 3, w, r, true);
        if (hasBinormal) blend(out.binormal, m.binormal, 3, w, r, true);
        if (hasUv) blend(out.uv, m.uv, 2, w, r, false);
        if (hasUv1) blend(out.uv1, m.uv1, 2, w, r, false);
        if (hasColour) blend(out.colour, m.colour, 4, w, r, false);
        if (skinned) { // (the corners' weights by joint, the heaviest four)
            float acc[256] = {};
            for (int c = 0; c < 3; ++c)
                for (int i = 0; i < 4; ++i) acc[m.joints[r[c] * 4 + size_t(i)]] += w[c] * m.weights[r[c] * 4 + size_t(i)];
            uint8_t js[4] = {};
            float ws[4] = {}, total = 0;
            int k = 0;
            for (; k < 4; ++k) {
                int best = -1;
                for (int b = 0; b < 256; ++b)
                    if (acc[b] > 0 && (best < 0 || acc[b] > acc[best])) best = b;
                if (best < 0) break;
                js[k] = uint8_t(best), ws[k] = acc[best], total += acc[best];
                acc[best] = 0;
            }
            for (int i = 0; i < 4; ++i) {
                out.joints.push_back(js[i]);
                out.weights.push_back(i < k && total > 0 ? ws[i] / total : 0.0f);
            }
        }
        return id;
    };
    // edge points: their position once per geometric edge (the same for every face on it), their
    // vertex once per vertex pair (a seam's two sides their own UVs)
    auto stepKey = [&](uint32_t a, uint32_t b, int s) { // (s steps from a towards b)
        if (a > b) std::swap(a, b), s = L - s;
        return uint64_t(a) << 34 | uint64_t(b) << 4 | uint64_t(s);
    };
    std::unordered_map<uint64_t, V3> edgeGeom;
    std::unordered_map<uint64_t, uint32_t> edgeVertex;

    std::vector<uint32_t> grid(size_t((L + 1) * (L + 1)));
    for (size_t f = 0; f < nTri; ++f) {
        const uint32_t r[3] = {tris[f * 3], tris[f * 3 + 1], tris[f * 3 + 2]};
        const uint32_t g[3] = {geo[r[0]], geo[r[1]], geo[r[2]]};
        Patch pt;
        for (int i = 0; i < 3; ++i) pt.P[i] = P(g[i]);
        V3 sum;
        for (int i = 0; i < 3; ++i) {
            const int j = (i + 1) % 3;
            uint32_t f1, f2;
            const V3 Pi = pt.P[i], Pj = pt.P[j];
            if (smoothEdge(g[i], g[j], f1, f2)) { // (the edge's own normals - both faces' corners averaged: the same from either side)
                const V3 Ni = normalize(cornerNormal(f1, g[i]) + cornerNormal(f2, g[i]));
                const V3 Nj = normalize(cornerNormal(f1, g[j]) + cornerNormal(f2, g[j]));
                pt.e[i][0] = (Pi * 2 + Pj - Ni * dot(Pj - Pi, Ni)) * (1.0f / 3);
                pt.e[i][1] = (Pj * 2 + Pi - Nj * dot(Pi - Pj, Nj)) * (1.0f / 3);
            } else { // (a crease or a border: straight)
                pt.e[i][0] = (Pi * 2 + Pj) * (1.0f / 3);
                pt.e[i][1] = (Pj * 2 + Pi) * (1.0f / 3);
            }
            sum = sum + pt.e[i][0] + pt.e[i][1];
        }
        const V3 E = sum * (1.0f / 6), V = (pt.P[0] + pt.P[1] + pt.P[2]) * (1.0f / 3);
        pt.c = E + (E - V) * 0.5f;
        // grid vertex (i, j): i steps towards corner 1, j towards corner 2
        for (int i = 0; i <= L; ++i)
            for (int j = 0; i + j <= L; ++j) {
                const int k0 = L - i - j;
                const float w[3] = {float(k0) / float(L), float(i) / float(L), float(j) / float(L)};
                uint32_t& v = grid[size_t(i * (L + 1) + j)];
                const int zeros = (k0 == 0) + (i == 0) + (j == 0);
                if (zeros == 2) { // (a corner: the original vertex)
                    v = k0 ? r[0] : i ? r[1] : r[2];
                    continue;
                }
                if (zeros == 1) { // (on an edge: shared with the neighbour)
                    int a, b, s;
                    if (j == 0) a = 0, b = 1, s = i;
                    else if (k0 == 0) a = 1, b = 2, s = j;
                    else a = 2, b = 0, s = k0;
                    const uint64_t gk = stepKey(g[a], g[b], s);
                    auto git = edgeGeom.find(gk);
                    if (git == edgeGeom.end()) git = edgeGeom.emplace(gk, evalPatch(pt, w)).first;
                    const uint64_t rk = stepKey(r[a], r[b], s);
                    auto rit = edgeVertex.find(rk);
                    v = rit != edgeVertex.end() ? rit->second : (edgeVertex[rk] = newVertex(git->second, w, r));
                } else { // (inside: this face's own)
                    v = newVertex(evalPatch(pt, w), w, r);
                }
            }
        for (int i = 0; i < L; ++i)
            for (int j = 0; i + j < L; ++j) {
                const uint32_t a = grid[size_t(i * (L + 1) + j)], b = grid[size_t((i + 1) * (L + 1) + j)], c = grid[size_t(i * (L + 1) + j + 1)];
                outTris.insert(outTris.end(), {a, b, c});
                if (i + j < L - 1) outTris.insert(outTris.end(), {b, grid[size_t((i + 1) * (L + 1) + j + 1)], c});
            }
    }
    out.list = std::move(outTris);
    if (stats) stats->trianglesBefore += int(nTri), stats->trianglesAfter += int(out.list.size() / 3), ++stats->meshes;
    m = std::move(out);
    return true;
}

} // namespace cr2
