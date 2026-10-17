#pragma once
// The battle motions' timing (BtlMotionTable, built in the game's code: tools/gen_motion_table.py
// -> motiontable.inc): for each figure's motion when it moves to its target and back, when its
// hits land, when it turns - in seconds from the motion's start, -1 where the table has none.

#include <string>

namespace cr2 {

struct MotionTimes {
    unsigned flags = 0;
    float loopStart = -1, loopEnd = -1;
    float moveStart = -1, moveEnd = -1;   // (to the target: BtlMoveCtrl::SetMovePosition)
    float move2Start = -1, move2End = -1; // (back)
    float hit[10] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    float turnStart = -1, turnEnd = -1, effectStart = -1;
    int effectLocation = 9;
};

// BtlMotionTable's motions (0 bb01 .. 24 bz03); -1: not one
int motionIndex(const std::string& name);
const char* motionName(int index);
// `figure`'s (its first four characters) motion `index`; false: the table doesn't hold the figure
bool motionTimes(const std::string& figure, int index, MotionTimes& out);

} // namespace cr2
