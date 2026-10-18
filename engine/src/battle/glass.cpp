#include "battle/glass.hpp"

#include <algorithm>
#include <cmath>

namespace cr2 {

std::vector<int> delaunay(const std::vector<float>& xs, const std::vector<float>& ys) {
    struct Tri {
        int a, b, c;
        double cx, cy, r2; // its circumscribed circle
    };
    const int n = int(std::min(xs.size(), ys.size()));
    std::vector<double> px(xs.begin(), xs.begin() + n), py(ys.begin(), ys.begin() + n);
    if (n < 3) return {};
    double x0 = px[0], x1 = px[0], y0 = py[0], y1 = py[0];
    for (int k = 1; k < n; ++k) x0 = std::min(x0, px[k]), x1 = std::max(x1, px[k]), y0 = std::min(y0, py[k]), y1 = std::max(y1, py[k]);
    // a triangle about them all, its triangles left out at the end (delaunay::DelaunayTriangles::
    // getSuperTriangle: equilateral about a circle 1.2 times the distance from the least corner to
    // a middle taken as half the size - the points from 0 - so tight that the hull's thinnest
    // triangles go with it: the screen's edges a little ragged)
    const double mx = (x1 - x0) * 0.5, my = (y1 - y0) * 0.5;
    const double r = std::sqrt((y0 - my) * (y0 - my) + (x0 - mx) * (x0 - mx)) * 1.2, side = r * 1.7320508;
    px.push_back(mx - side), py.push_back(my - r);
    px.push_back(mx + side), py.push_back(my - r);
    px.push_back(mx), py.push_back(my + r + r);
    auto make = [&](int a, int b, int c) {
        Tri t{a, b, c, 0, 0, -1};
        const double ax = px[a], ay = py[a], bx = px[b], by = py[b], cx = px[c], cy = py[c];
        const double d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
        if (d == 0) return t;
        const double a2 = ax * ax + ay * ay, b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
        t.cx = (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / d;
        t.cy = (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / d;
        t.r2 = (ax - t.cx) * (ax - t.cx) + (ay - t.cy) * (ay - t.cy);
        return t;
    };
    std::vector<Tri> tris{make(n, n + 1, n + 2)};
    for (int p = 0; p < n; ++p) {
        std::vector<std::pair<int, int>> edges;
        std::vector<Tri> kept;
        kept.reserve(tris.size() + 4);
        for (const Tri& t : tris) {
            const double dx = px[p] - t.cx, dy = py[p] - t.cy;
            if (t.r2 >= 0 && dx * dx + dy * dy < t.r2) {
                const int e[3][2] = {{t.a, t.b}, {t.b, t.c}, {t.c, t.a}};
                for (const auto& v : e) {
                    const auto it = std::find_if(edges.begin(), edges.end(), [&](const std::pair<int, int>& q) {
                        return (q.first == v[0] && q.second == v[1]) || (q.first == v[1] && q.second == v[0]);
                    });
                    if (it != edges.end()) edges.erase(it); // (shared by two of them: inside the hole)
                    else edges.push_back({v[0], v[1]});
                }
            } else {
                kept.push_back(t);
            }
        }
        for (const auto& e : edges) kept.push_back(make(e.first, e.second, p));
        tris.swap(kept);
    }
    std::vector<int> out;
    for (const Tri& t : tris)
        if (t.a < n && t.b < n && t.c < n) out.insert(out.end(), {t.a, t.b, t.c});
    return out;
}

void GlassFragments::build(int halfW, int halfH, Rand48& rng) {
    w = halfW, h = halfH;
    const float fw = float(w), fh = float(h);
    // GlassFragmentEffect: (0, 0), one along the bottom, one up the side, one inside, then 230 -
    // all on the screen's half size, y down
    std::vector<float> xs{0, fw * rng.unit(), 0}, ys{0, 0, 0};
    ys[2] = fh * rng.unit();
    xs.push_back(fw * rng.unit());
    ys.push_back(fh * rng.unit());
    for (int k = 0; k < kPoints; ++k) {
        xs.push_back(fw * rng.unit());
        ys.push_back(fh * rng.unit());
    }
    // (fixEdge then snaps the hull's points between two corners to the screen's edge - but only
    // between the corners 0 and 1 or 0 and 2, where they already are: the cases it meant for the
    // corner 3 ask for a smaller corner after a larger, never true)
    const std::vector<int> tri = delaunay(xs, ys);
    shards.clear();
    for (size_t t = 0; t + 2 < tri.size(); t += 3) {
        GlassShard s{};
        float cx = 0, cy = 0;
        for (int c = 0; c < 3; ++c) cx += xs[size_t(tri[t + c])], cy += ys[size_t(tri[t + c])];
        cx = cx / 3 - fw * 0.5f, cy = cy / 3 - fh * 0.5f;
        const float d = std::sqrt(cx * cx + cy * cy), inv = d > 0 ? 1.0f / d : 0.0f;
        const float speed = d * 0.06f + 8.7f, gain = d * 0.02f + 0.55f;
        s.move[0] = speed * cx * inv, s.move[1] = -(cy * inv * speed), s.move[2] = 0;
        s.moveGain[0] = gain * cx * inv, s.moveGain[1] = -(cy * inv * gain), s.moveGain[2] = 0;
        const float turn = d * 0.0003f + 0.18f;
        for (int c = 0; c < 3; ++c) s.turn[c] = turn * rng.unit();
        for (int c = 0; c < 3; ++c) s.turnGain[c] = 0.0f * rng.unit();
        const float grow = 0.004f * rng.unit() + 1.0f;
        s.scale[0] = s.scale[1] = grow, s.scale[2] = 1;
        s.scaleGain[0] = s.scaleGain[1] = 0.004f, s.scaleGain[2] = 0;
        // Setup: on the screen, centred, y up; the texture's place by it
        for (int c = 0; c < 3; ++c) {
            const size_t i = size_t(tri[t + c]);
            s.x[c] = xs[i] * 2 - fw, s.y[c] = fh - ys[i] * 2, s.z[c] = 0;
            s.u[c] = (fw + s.x[c]) / (fw * 2), s.v[c] = (fh + s.y[c]) / (fh * 2);
        }
        shards.push_back(s);
    }
    clock = 0;
    built = true;
}

void GlassFragments::step() {
    for (GlassShard& s : shards) {
        float c[3] = {0, 0, 0};
        for (int k = 0; k < 3; ++k) c[0] += s.x[k], c[1] += s.y[k], c[2] += s.z[k];
        for (float& v : c) v /= 3;
        // translate(c + move) * scale * rotationZYX(turn) * translate(-c)
        const float sx = std::sin(s.turn[0]), cx = std::cos(s.turn[0]);
        const float sy = std::sin(s.turn[1]), cy = std::cos(s.turn[1]);
        const float sz = std::sin(s.turn[2]), cz = std::cos(s.turn[2]);
        const float r[3][3] = {{cy * cz, sx * sy * cz - cx * sz, cx * sy * cz + sx * sz},
                               {cy * sz, sx * sy * sz + cx * cz, cx * sy * sz - sx * cz},
                               {-sy, sx * cy, cx * cy}};
        for (int k = 0; k < 3; ++k) {
            const float p[3] = {s.x[k] - c[0], s.y[k] - c[1], s.z[k] - c[2]};
            float q[3];
            for (int i = 0; i < 3; ++i) q[i] = (r[i][0] * p[0] + r[i][1] * p[1] + r[i][2] * p[2]) * s.scale[i] + c[i] + s.move[i];
            s.x[k] = q[0], s.y[k] = q[1], s.z[k] = q[2];
        }
        for (int i = 0; i < 3; ++i) s.move[i] += s.moveGain[i], s.turn[i] += s.turnGain[i], s.scale[i] += s.scaleGain[i];
    }
}

void GlassFragments::advanceTo(float t) {
    while (built && clock <= kLength && clock + kFrame * 0.5f <= t) {
        step();
        clock += kFrame;
    }
}

} // namespace cr2
