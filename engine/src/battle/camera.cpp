#include "battle/camera.hpp"

namespace cr2 {

namespace {

#include "battle/cameratable.inc"

int at(int k) { return k >= 0 && k < int(sizeof kCameraShorts / sizeof kCameraShorts[0]) ? kCameraShorts[k] : -1; }

} // namespace

const char* battleCameraName(int n) { return n >= 0 && n < int(sizeof kCameraNames / sizeof kCameraNames[0]) ? kCameraNames[n] : nullptr; }
int cameraStart(int k) { return at(k); }
int cameraPattern(int p) { return at(0xec + p); }
int cameraAttackToEnemy(int kind, int n) { return at(0x94 + kind * 22 + n); }
int cameraAttackFromEnemy(int n, int t) { return at(0x3c + n * 4 + t); }
int cameraTarget(int k) { return at(0x1e + k); }

} // namespace cr2
