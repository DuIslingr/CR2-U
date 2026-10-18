#pragma once
// The awakenings', advents' and monsters' special scenes (BtlSpecialCommandList, built in the
// game's code: tools/gen_special_table.py -> specialtable.inc; docs/BATTLE.md, "Special scenes")
// and the battle effects' settings (BtlEffectTable: tools/gen_effect_table.py -> effecttable.inc).
//
// A scene is a list of steps, each at its time from the scene's start (BtlActionCtrl::
// ActionSpecialCommand: each once the scene's clock reaches it, in order; the scene over once all
// have run) - its effects, cameras, the fighters shown or hidden, its hits and sounds.

#include <string>

namespace cr2 {

// a step's op: BtlActionCtrl's handler table (+0x608: ActionSpecialSetMotion ...)
enum SpecialOp : short {
    kSpecialMotionTable = -1, // BuildAwakeFromMotionTable: its figure's motion's hits, dash, return, turn
    kSpecialSetMotion = 0,    // i: id (a motion), blend
    kSpecialSetPosition = 1,  // (the actor back at its dpos)
    kSpecialSetRotation = 2,  // i: target
    kSpecialSetVisibility = 3, // i: id (0 the actor's side, 1 the other, 3 its partner, 4 the map), visible
    kSpecialSetAlpha = 4,     // i: id (0 the actor's side, else the other); f: start, end, time
    kSpecialSetColor = 5,     // i: target (3 the party, 4 the monsters, 7 the map); f: start, end, time
    kSpecialSetEffect = 6,    // i: id (a slot), show, connect; f: time (its end; 0 none), start; s: name
    kSpecialSetEffectTarget = 7, // i: id, target (1 the actor, 2 its target)
    kSpecialSetEffectPosition = 8, // i: id, target, apos, dpos, tpos; v: shift
    kSpecialSetEffectRotation = 9, // i: id, target, apos, dpos, tpos; f: shift (degrees about y)
    kSpecialSetEffectAnimation = 10, // i: id; s: the animation (the figure's, "bz01")
    kSpecialSetEffectAlpha = 11, // i: id; f: start, end, time
    kSpecialSetCamera = 12,   // s: the camera
    kSpecialSetCameraPosition = 13, // i: target, apos, dpos, tpos; v: shift
    kSpecialSetCameraRotation = 14, // i: target, apos, dpos, tpos; f: shift
    kSpecialSetTargetCamera = 15,
    kSpecialLoadCamera = 16,  // s: the camera's figure
    kSpecialWaitTime = 17,    // (nothing: the scene's last)
    kSpecialHit = 18,         // i: finish (the last)
    kSpecialDash = 19,        // f: time (to the target's aPos)
    kSpecialReturn = 20,      // f: time (back)
    kSpecialReturnDirection = 21, // f: time
    kSpecialPlaySE = 22,      // i: id (-1 the launch's, -2 the hit's, -3 the shot's, -4 the post's); f: delay
    kSpecialPlayEventSE = 23, // i: id (a clip); f: delay
    kSpecialFade = 24,        // i: mode (0 in from white, 1 out to white, 2 in from black, 3 out to black), frames
    // the few scenes' own (no arguments)
    kSpecialDarwin3Standby = 25,      // Darwin's third awakening: its map, d1154b, made (BtlMap::LoadSpecialMap)
    kSpecialDarwin3DarwinOffset = 26, // Darwin put by the one struck (its dpos, the pair's if paired) and its figure's offset
    kSpecialDarwin3CameraOffset = 27, // the camera's place moved by the same table's offset
    kSpecialDarwin3CameraOffset2 = 28, // the camera's place the size pattern's offset, Darwin moved from his by it
    kSpecialDarwin3ResetMap = 29,     // the map d1154b no longer drawn (SetRenderSpecialMap false)
    kSpecialLesica1Standby = 30,      // the one acting back at its place, its first pose, its partner hidden
    kSpecialLesica2Standby = 31,
    kSpecialRuleFixPosition = 32,
    kSpecialGlassFragment = 33,       // the screen breaking away (BtlMain::PostEffectGlassFragments), d1154b drawn
    kSpecialSetSkipEnable = 34,
};

struct SpecialStep {
    short op;
    float time;
    int i[6];
    float f[4];
    const char* s;
    float v[3];
};
struct SpecialScene {
    int count;
    const SpecialStep* steps;
};

// the scene `command` shows (SetActionTable: by the command, `own` its +0x48 set - the party's
// awakenings and advents; else a monster's special): nullptr none; `advent` whether BuildAdvent's
const SpecialScene* specialScene(int command, bool own, bool* advent = nullptr);

// an effect's settings (BtlEffectTable::GetEffectSetting: by its name, else "common's")
struct EffectSetting {
    const char* name;
    float shift[3], scale[3];
    short locator;        // what it follows of its model (BtlTposTable: 0 RootLocator .. 8 DummyCP, 9 the model)
    bool billboard, loop; // (turned to the camera; looping)
    unsigned char constrain, kind; // (1 it follows the position, 2 the rotation, 4 the joint's scale)
    unsigned short delay; // (frames: it starts that much later, hidden till then)
    short pairLocator = 0; // (a pair's action's: 7 the side's middle)
    short placement = 0;   // (0 placed by its target - SetCommandEffectPosition -, else not)
    bool scaled = false;   // (scaled to its target)
    unsigned char shotPath = 0; // (a shot's: 0 straight, 1 falling - BtlEffectCtrl::ControlShot)
};
const EffectSetting& effectSetting(const std::string& name);

// BtlTposTable: the joints a tpos / an effect's locator names (9: the model's own place)
const char* tposJoint(int tpos);

// Darwin's third awakening's offsets (BtlActionTable, in hundredths: tools/gen_darwin_table.py):
// by the one struck's figure (its first four characters; a monster's `j` figure as `m5..`), and
// by the monsters' size pattern (13 / 14 one of the party, solo / paired); null: none
struct DarwinOffset {
    const char* name;
    float v[3];
};
struct DarwinPatternOffset {
    int key;
    float v[3];
};
const float* darwinFigureOffset(const std::string& figure);
// how far above a dying monster its death effect goes (GetEffectDeadOffsetByModelName; 0 none)
float deadOffset(const std::string& figure);
const float* darwinPatternOffset(int key);

} // namespace cr2
