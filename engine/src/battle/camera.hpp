#pragma once
// The battle cameras' table (BtlCameraTable, built in the game's code: tools/gen_camera_table.py
// -> cameratable.inc): which camera (a joint of BattleCamera_move / _nomove, by number into
// btl_camera_name_table) shows what.

namespace cr2 {

const char* battleCameraName(int number); // nullptr: none
int cameraStart(int k);                   // GetStart (the battle's opening: by pattern ...)
int cameraPattern(int pattern);           // GetPatternToCamera: the pattern's base
int cameraAttackToEnemy(int kind, int n); // GetAttackToEnemy (kind: 0, 1 the party's one, 2 alone, 3 a pair)
int cameraAttackFromEnemy(int n, int t);  // GetAttackFromEnemy (t: its target 0, 1, 3 a pair)
int cameraUseItem(int n);                 // GetUseItem (an item's user: 0, 1 the party's one, 2 alone, 3 a pair)
int cameraItemSopiaTarget(int n);         // GetItemSopiaTarget (an item's / a sopia's one target, the same)
int cameraItemSopiaTargetGroup(int n);    // GetItemSopiaTargetGroup (a side's: the foes by the pattern + 2, 12 / 13 the human ones)
int cameraUseSopia(int n);                // GetUseSopia (a sopia's caster: 0, 1 the party's one, 2 alone, 3 a pair)
int cameraTarget(int k);                  // GetTarget (a target's while it is chosen: 0, 1 the party's,
                                          // 2 the one alone, 3 a pair, 4..7 a monster S..XL, 8.. humans)

} // namespace cr2
