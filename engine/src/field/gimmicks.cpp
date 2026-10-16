#include "field/gimmicks.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace cr2 {

const char* Gimmick::kindName() const {
    static const char* kNames[] = {"npc", "object", "effect", "box", "ability", "event", "pop point", "exit", "spot", "other"};
    return kNames[kind];
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
        g.shape = has(n, "_box") ? Gimmick::ShapeBox : has(n, "_cap") ? Gimmick::Capsule : Gimmick::Ball;
        out.push_back(std::move(g));
    }
    return out;
}

} // namespace cr2
