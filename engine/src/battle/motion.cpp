#include "battle/motion.hpp"

#include <cstring>

namespace cr2 {

namespace {

#include "battle/motiontable.inc"

const char* const kMotions[25] = {"bb01", "bb02", "bb03", "bb04", "bg01", "bg02", "bd01", "bd02", "bh01",
                                  "bo01", "bo02", "bo03", "bc01", "ba01", "ba02", "bs01", "bp01", "bm01",
                                  "bi01", "bx01", "bx02", "bx03", "bz01", "bz02", "bz03"};

float seconds(unsigned bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f / 20.0f; // (Initialize's last pass: frames to seconds)
}

} // namespace

int motionIndex(const std::string& name) {
    for (int i = 0; i < 25; ++i)
        if (name == kMotions[i]) return i;
    return -1;
}

const char* motionName(int index) { return index >= 0 && index < 25 ? kMotions[index] : ""; }

bool motionTimes(const std::string& figure, int index, MotionTimes& out) {
    out = MotionTimes();
    int fig = -1;
    for (int i = 0; i < 80 && fig < 0; ++i)
        if (kMotionFigures[i] && figure.compare(0, 4, kMotionFigures[i]) == 0) fig = i;
    if (fig < 0 || index < 0 || index >= 25) return false;
    for (const auto& s : kMotionStores) {
        if (s.figure != fig || s.motion != index) continue;
        switch (s.field) {
        case 0x00: out.flags = s.value; break;
        case 0x04: out.loopStart = seconds(s.value); break;
        case 0x08: out.loopEnd = seconds(s.value); break;
        case 0x0c: out.moveStart = seconds(s.value); break;
        case 0x10: out.moveEnd = seconds(s.value); break;
        case 0x14: out.move2Start = seconds(s.value); break;
        case 0x18: out.move2End = seconds(s.value); break;
        case 0x44: out.turnStart = seconds(s.value); break;
        case 0x48: out.turnEnd = seconds(s.value); break;
        case 0x4c: out.effectStart = seconds(s.value); break;
        case 0x52: out.effectLocation = int(short(s.value)); break;
        default:
            if (s.field >= 0x1c && s.field < 0x44 && (s.field - 0x1c) % 4 == 0) out.hit[(s.field - 0x1c) / 4] = seconds(s.value);
            break;
        }
    }
    return true;
}

} // namespace cr2
