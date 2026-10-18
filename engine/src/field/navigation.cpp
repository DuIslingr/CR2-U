#include "field/navigation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace cr2 {

namespace {

constexpr int kDx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
constexpr int kDz[8] = {0, 0, 1, -1, 1, -1, 1, -1};
constexpr int kBack[8] = {1, 0, 3, 2, 7, 6, 5, 4};

// the floor under (x, z), searched from `fromY` + `above` down to `fromY` - `below`: NaN none
float floorAt(const CollisionWorld& w, float x, float z, float fromY, float above, float below) {
    CollisionWorld::Hit h;
    const Vec3 a{x, fromY + above, z}, b{x, fromY - below, z};
    if (!w.sweep(a, b, 0.001f, {0, 1, 0}, 0.7f, h)) return std::numeric_limits<float>::quiet_NaN();
    return a[1] + (b[1] - a[1]) * h.fraction;
}

} // namespace

bool NavGrid::cellOf(const Vec3& p, int& x, int& z) const {
    x = int(std::floor((p[0] - x0_) / cell_)), z = int(std::floor((p[2] - z0_) / cell_));
    return x >= 0 && z >= 0 && x < w_ && z < h_;
}

void NavGrid::build(const CollisionWorld& world, const Vec3& start, float radius, float step, float cell) {
    reached_ = 0;
    cell_ = cell;
    floor_.clear(), links_.clear(), zone_.clear();
    Vec3 lo, hi;
    if (!world.bounds(lo, hi)) return;
    x0_ = lo[0], z0_ = lo[2];
    w_ = std::max(1, int(std::ceil((hi[0] - lo[0]) / cell)) + 1), h_ = std::max(1, int(std::ceil((hi[2] - lo[2]) / cell)) + 1);
    if (size_t(w_) * size_t(h_) > 4000000) return; // (a map too wide for a grid this fine)
    floor_.assign(size_t(w_) * size_t(h_), std::numeric_limits<float>::quiet_NaN());
    links_.assign(floor_.size(), 0);
    grow(world, start, radius, step);
}

bool NavGrid::reaches(const Vec3& p) const {
    int x, z;
    return !floor_.empty() && cellOf(p, x, z) && !std::isnan(floor_[size_t(index(x, z))]);
}

bool NavGrid::grow(const CollisionWorld& world, const Vec3& start, float radius, float step) {
    if (floor_.empty()) return false;
    const float cell = cell_;
    int sx, sz;
    if (!cellOf(start, sx, sz) || !std::isnan(floor_[size_t(index(sx, sz))])) return false;
    const float y0 = floorAt(world, x0_ + (float(sx) + 0.5f) * cell, z0_ + (float(sz) + 0.5f) * cell, start[1], 0.1f, 0.3f);
    if (std::isnan(y0)) return false;
    floor_[size_t(index(sx, sz))] = y0;
    ++reached_;
    std::queue<std::pair<int, int>> open;
    open.push({sx, sz});
    // a neighbour joined: its floor within a step (and the slope's own rise), the sphere's way there
    // clear of walls at the higher floor's height plus a step, and the sphere fitting there
    const float rise = step + cell * 1.2f;
    while (!open.empty()) {
        const auto [x, z] = open.front();
        open.pop();
        const float y = floor_[size_t(index(x, z))];
        const Vec3 here{x0_ + (float(x) + 0.5f) * cell, y, z0_ + (float(z) + 0.5f) * cell};
        for (int d = 0; d < 8; ++d) {
            const int nx = x + kDx[d], nz = z + kDz[d];
            if (nx < 0 || nz < 0 || nx >= w_ || nz >= h_) continue;
            const Vec3 there{x0_ + (float(nx) + 0.5f) * cell, 0, z0_ + (float(nz) + 0.5f) * cell};
            const float ny = floorAt(world, there[0], there[2], y, rise, rise);
            if (std::isnan(ny) || std::fabs(ny - y) > rise) continue;
            const float across = std::max(y, ny) + step + radius;
            CollisionWorld::Hit hit;
            if (world.sweep({here[0], across, here[2]}, {there[0], across, there[2]}, radius * 0.9f, {0, 1, 0}, -2.0f, hit) &&
                std::fabs(hit.normal[1]) < 0.7f)
                continue;
            bool fits = true;
            for (const CollisionWorld::Contact& c : world.overlaps({there[0], ny + radius + step, there[2]}, radius * 0.9f))
                if (std::fabs(c.normal[1]) < 0.7f) fits = false;
            if (!fits) continue;
            links_[size_t(index(x, z))] |= uint8_t(1u << d);
            const size_t ni = size_t(index(nx, nz));
            links_[ni] |= uint8_t(1u << kBack[d]);
            if (std::isnan(floor_[ni])) {
                floor_[ni] = ny;
                ++reached_;
                open.push({nx, nz});
            }
        }
    }
    return true;
}

void NavGrid::markZones(const std::function<int(const Vec3&)>& zoneOf) {
    zone_.assign(floor_.size(), -1);
    for (int z = 0; z < h_; ++z)
        for (int x = 0; x < w_; ++x) {
            const float y = floor_[size_t(index(x, z))];
            if (!std::isnan(y)) zone_[size_t(index(x, z))] = int16_t(zoneOf({x0_ + (float(x) + 0.5f) * cell_, y, z0_ + (float(z) + 0.5f) * cell_}));
        }
}

bool NavGrid::nearest(const Vec3& to, Vec3& at, float& distance) const {
    distance = std::numeric_limits<float>::max();
    bool any = false;
    for (int z = 0; z < h_; ++z)
        for (int x = 0; x < w_; ++x) {
            const float y = floor_[size_t(index(x, z))];
            if (std::isnan(y)) continue;
            const float cx = x0_ + (float(x) + 0.5f) * cell_, cz = z0_ + (float(z) + 0.5f) * cell_;
            const float dd = std::hypot(cx - to[0], cz - to[2]);
            if (dd < distance) distance = dd, at = {cx, y, cz}, any = true;
        }
    return any;
}

bool NavGrid::reachedCell(size_t n, Vec3& at) const {
    if (reached_ == 0) return false;
    n %= reached_;
    for (int z = 0; z < h_; ++z)
        for (int x = 0; x < w_; ++x) {
            const float y = floor_[size_t(index(x, z))];
            if (std::isnan(y) || n-- > 0) continue;
            at = {x0_ + (float(x) + 0.5f) * cell_, y, z0_ + (float(z) + 0.5f) * cell_};
            return true;
        }
    return false;
}

bool NavGrid::path(const Vec3& from, const Vec3& to, std::vector<Vec3>& out, int allowZone) const {
    out.clear();
    int fx, fz;
    if (!cellOf(from, fx, fz)) return false;
    if (std::isnan(floor_[size_t(index(fx, fz))])) { // (just off it: the nearest cell)
        Vec3 at;
        float d;
        if (!nearest(from, at, d) || d > cell_ * 3 || !cellOf(at, fx, fz)) return false;
    }
    Vec3 goal;
    float gd;
    int gx, gz;
    if (!nearest(to, goal, gd) || !cellOf(goal, gx, gz)) return false;
    const size_t n = floor_.size();
    std::vector<float> cost(n, std::numeric_limits<float>::max());
    std::vector<int> from_(n, -1);
    using Item = std::pair<float, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    const int start = index(fx, fz), end = index(gx, gz);
    cost[size_t(start)] = 0;
    open.push({0, start});
    while (!open.empty()) {
        const auto [f, i] = open.top();
        open.pop();
        if (i == end) break;
        const int x = i % w_, z = i / w_;
        for (int d = 0; d < 8; ++d) {
            if (!(links_[size_t(i)] & (1u << d))) continue;
            const int ni = index(x + kDx[d], z + kDz[d]);
            if (!zone_.empty() && zone_[size_t(ni)] >= 0 && zone_[size_t(ni)] != allowZone && ni != end) continue; // (another exit)
            const float c = cost[size_t(i)] + (d < 4 ? 1.0f : 1.4142f);
            if (c >= cost[size_t(ni)]) continue;
            cost[size_t(ni)] = c, from_[size_t(ni)] = i;
            const float hx = float(gx - (x + kDx[d])), hz = float(gz - (z + kDz[d]));
            open.push({c + std::sqrt(hx * hx + hz * hz), ni});
        }
    }
    if (cost[size_t(end)] == std::numeric_limits<float>::max()) return false;
    for (int i = end; i >= 0; i = from_[size_t(i)]) {
        const int x = i % w_, z = i / w_;
        out.push_back({x0_ + (float(x) + 0.5f) * cell_, floor_[size_t(i)], z0_ + (float(z) + 0.5f) * cell_});
        if (i == start) break;
    }
    std::reverse(out.begin(), out.end());
    return true;
}

} // namespace cr2
