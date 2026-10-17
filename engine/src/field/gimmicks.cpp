#include "field/gimmicks.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace cr2 {

const char* Gimmick::kindName() const {
    static const char* kNames[] = {"npc", "object", "effect", "box", "ability", "event", "pop point", "exit", "spot", "other"};
    return kNames[kind];
}

float Gimmick::checkLength() const {
    if (!check) return 0;
    switch (kind) {
    case Npc:
    case Box:
    case Object: return 0.2f;
    case Event:
    case Effect: return 0.15f;
    default: return 0;
    }
}

float Gimmick::balloonHeight() const {
    static const struct {
        const char* name;
        float height;
    } kTable[] = {{"c001", 0.21f},   {"c002", 0.21f},   {"c004", 0.22f},   {"c006", 0.21f},   {"c103", 0.32f},
                  {"obj002", 0.23f}, {"obj004", 0.2f},  {"obj028", 0.03f}, {"obj029", 0.1f},  {"obj030", 0.1f},
                  {"obj031", 0.0f},  {"obj041", 0.2f},  {"obj042", 0.15f}, {"obj043", 0.15f}, {"obj044", 0.2f},
                  {"obj045", 0.2f},  {"obj141", 0.12f}, {"eff104", 0.05f}};
    // (matched in the name's start: GetGimmickDataStrSimple - 4 characters of an NPC's, 6 of others')
    const std::string start = name.substr(0, kind == Npc ? 4 : 6);
    for (const auto& e : kTable)
        if (start.find(e.name) != std::string::npos) return e.height;
    return kind == Npc ? 0.2f : kind == Effect ? 0.1f : 0.25f;
}

bool Gimmick::touches(const float* p, float r) const {
    // into the joint's frame (the rotation undone)
    const float d[3] = {p[0] - position[0], p[1] - position[1], p[2] - position[2]};
    const float x = -rotation[0], y = -rotation[1], z = -rotation[2], w = rotation[3];
    const float tx = 2 * (y * d[2] - z * d[1]), ty = 2 * (z * d[0] - x * d[2]), tz = 2 * (x * d[1] - y * d[0]);
    const float l[3] = {d[0] + w * tx + (y * tz - z * ty), d[1] + w * ty + (z * tx - x * tz), d[2] + w * tz + (x * ty - y * tx)};
    const float sx = jointScale[0] / 100.0f, sy = jointScale[1] / 100.0f, sz = jointScale[2] / 100.0f;
    if (shape == ShapeBox) return std::fabs(l[0]) <= sx + r && std::fabs(l[1]) <= sy + r && std::fabs(l[2]) <= sz + r;
    const float half = shape == Capsule ? sy : 0.0f; // (the capsule along its y)
    const float cy = std::max(-half, std::min(half, l[1]));
    const float dx = l[0], dy = l[1] - cy, dz = l[2];
    return dx * dx + dy * dy + dz * dz <= (sx + r) * (sx + r);
}

namespace {

bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

// a rotation matrix's quaternion (Shepperd)
std::array<float, 4> quaternion(const float r[3][3]) {
    std::array<float, 4> q;
    const float tr = r[0][0] + r[1][1] + r[2][2];
    if (tr > 0) {
        const float s = std::sqrt(tr + 1.0f) * 2;
        q = {(r[2][1] - r[1][2]) / s, (r[0][2] - r[2][0]) / s, (r[1][0] - r[0][1]) / s, 0.25f * s};
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        const float s = std::sqrt(1.0f + r[0][0] - r[1][1] - r[2][2]) * 2;
        q = {0.25f * s, (r[0][1] + r[1][0]) / s, (r[0][2] + r[2][0]) / s, (r[2][1] - r[1][2]) / s};
    } else if (r[1][1] > r[2][2]) {
        const float s = std::sqrt(1.0f + r[1][1] - r[0][0] - r[2][2]) * 2;
        q = {(r[0][1] + r[1][0]) / s, 0.25f * s, (r[1][2] + r[2][1]) / s, (r[0][2] - r[2][0]) / s};
    } else {
        const float s = std::sqrt(1.0f + r[2][2] - r[0][0] - r[1][1]) * 2;
        q = {(r[0][2] + r[2][0]) / s, (r[1][2] + r[2][1]) / s, 0.25f * s, (r[1][0] - r[0][1]) / s};
    }
    return q;
}

} // namespace

std::vector<Gimmick> readGimmicks(const Skeleton& p, const std::vector<std::string>& names) {
    std::vector<Gimmick> out;
    const std::vector<Mat34> world = p.world(p.basePose);
    for (size_t j = 0; j < names.size() && j < world.size(); ++j) {
        const std::string& n = names[j];
        if (n.empty()) continue;
        Gimmick g;
        g.name = n;
        // SetData's order of tests
        if (has(n, "ragdoll")) g.kind = Gimmick::Exit;
        else if ((n[0] == 'd' || (n[0] == 't' && has(n, "t01"))) && has(n, "_p")) g.kind = Gimmick::PopPoint;
        else if (n[0] == 'c' || n[0] == 'j') g.kind = Gimmick::Npc, g.figure = n.substr(0, 4);
        else if (has(n, "obj")) g.kind = Gimmick::Object, g.figure = n.substr(0, 6);
        else if (has(n, "eff")) g.kind = Gimmick::Effect, g.figure = n.substr(0, 6);
        else if (has(n, "sprint")) g.kind = Gimmick::Ability, g.figure = "obj151";
        else if (has(n, "hjump") || has(n, "mystic") || has(n, "iai") || has(n, "aim") || has(n, "marts")) g.kind = Gimmick::Ability;
        else if (n.compare(0, 4, "spot") == 0) g.kind = Gimmick::Spot;
        else if (has(n, "evt")) g.kind = Gimmick::Event;
        else if (has(n, "tbox") || has(n, "obox") || has(n, "fbox")) g.kind = Gimmick::Box, g.figure = has(n, "obox") ? "obj012" : "obj011";
        else if (n == "POP" || n == "JUMP") g.kind = Gimmick::Spot;
        // the joint's transform: position, the rotation and scale its matrix holds
        const Mat34& m = world[j];
        float r[3][3];
        for (int c = 0; c < 3; ++c) {
            const float len = std::sqrt(m.m[c] * m.m[c] + m.m[4 + c] * m.m[4 + c] + m.m[8 + c] * m.m[8 + c]);
            g.jointScale[c] = len;
            for (int row = 0; row < 3; ++row) r[row][c] = len > 0 ? m.m[row * 4 + c] / len : (row == c ? 1.0f : 0.0f);
        }
        g.rotation = quaternion(r);
        g.position[0] = m.m[3], g.position[1] = m.m[7], g.position[2] = m.m[11];
        const size_t sc = n.find("scale");
        if (sc != std::string::npos) g.scale = float(std::atoi(n.substr(sc + 5, 3).c_str())) / 100.0f;
        g.check = has(n, "check");
        {
            const size_t keep = g.kind == Gimmick::Object || g.kind == Gimmick::Effect || g.kind == Gimmick::Event ? 7 : 5;
            size_t tag = n.find("_box"), skip = 4;
            if (tag == std::string::npos) tag = n.find("_cap");
            if (tag == std::string::npos) tag = n.find("_ball"), skip = 5;
            const bool named = g.kind == Gimmick::Npc || g.kind == Gimmick::Object || g.kind == Gimmick::Effect ||
                               g.kind == Gimmick::Box || g.kind == Gimmick::Event;
            g.id = tag != std::string::npos && named ? n.substr(0, keep) + n.substr(tag + skip, 2) : n;
            g.function = g.kind == Gimmick::Event ? n.substr(0, 6) : g.id;
        }
        if (g.kind == Gimmick::Box) // (FldBox::Init: tbox 0, obox 1, fbox 2; its number)
            g.boxKind = has(n, "tbox") ? 0 : has(n, "obox") ? 1 : 2, g.boxNumber = g.id.size() > 5 ? std::atoi(g.id.c_str() + 5) : 0;
        g.shape = has(n, "_box") ? Gimmick::ShapeBox : has(n, "_cap") ? Gimmick::Capsule : Gimmick::Ball;
        out.push_back(std::move(g));
    }
    return out;
}

} // namespace cr2
