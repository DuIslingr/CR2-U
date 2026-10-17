#pragma once
// A field map's gimmicks: the joints of its <map>p figure, read by name
// (FldGimmickDataManager::SetData / SetGimmickDataOne; docs/FORMATS.md). Each joint places one -
// its position, rotation and scale - and its name says what it is and how it collides:
//   c<nnn>... / j<nnn>...  an NPC (FldNPC): the figure c<nnn>, idling on c<nnn>fn01
//   obj<nnn>...            an object (FldObject): the figure obj<nnn>
//   eff<nnn>...            an effect (FldEffect): the figure eff<nnn>
//   tbox / fbox, obox      a treasure box (FldBox): obj011, obj012
//   sprint, hjump, mystic, iai, aim, marts   the characters' field abilities' spots (sprint: obj151)
//   evt<nnn>...            an event box (FldEvent): a script runs on touching it
//   <map>_p<n>             a pop point (where an exit leads); ragdoll_<map>_p<n> an exit's marker
//   spot<nn>, POP, JUMP    spots (FldGimmickDataManager::CreateSpotList)
// and "_scaleNNN" scales the figure by NNN / 100, "_check" makes it a thing to tap, "_box" /
// "_ball" / "_cap" its collision shape.

#include <array>
#include <string>
#include <vector>

#include "assets/skeleton.hpp"

namespace cr2 {

struct Gimmick {
    enum Kind { Npc, Object, Effect, Box, Ability, Event, PopPoint, Exit, Spot, Other } kind = Other;
    std::string name;   // the joint's
    // the scripts' name for it (Cr3UtilGetScriptFunctionName): the name's start - 5 characters
    // (NPCs, boxes) or 7 (objects, effects) - and the two digits after its _box / _ball / _cap:
    // "c208_box00_scale080" -> "c208_00", "eff104_check_box01" -> "eff104_01", "tbox_check_cap07" ->
    // "tbox_07", "evt021_box00" -> "evt021_00"
    std::string id;
    std::string function; // the script function touching / tapping it calls: its id, an event box's
                          // first six characters ("evt021")
    std::string figure; // the figure it shows ("" none)
    float position[3] = {0, 0, 0};
    std::array<float, 4> rotation{0, 0, 0, 1}; // quaternion x y z w
    float jointScale[3] = {1, 1, 1};          // (the collision box's size x 100)
    float scale = 1;                           // the figure's: _scaleNNN / 100
    bool check = false;                        // _check: tapped
    // a treasure box's kind (0 tbox, 1 obox - a key box -, 2 fbox) and number (the name's two digits:
    // Cr3UtilGetNumberFromLocatorName); -1 not one
    int boxKind = -1, boxNumber = -1;
    enum Shape { Ball, ShapeBox, Capsule } shape = Ball;
    const char* kindName() const;
    // a sphere of radius r at p meets the gimmick's shape (its joint's scale / 100: a ball's
    // radius, a box's half extents - turned by the joint - a capsule's radius and half height)
    bool touches(const float* p, float r) const;
    // Tapping it (FldMain::CheckBalloon / CheckGimmickStart): a _check gimmick shown, its event on,
    // within its check length of the leader gets a balloon - NPCs, objects 0.2 (FldNPC / FldObject
    // +0x14), event boxes and effects 0.15 - and a tap anywhere calls the nearest one's function.
    // treasure boxes 0.2 (FldBox +0x14); 0: not one to tap
    float checkLength() const;
    // the balloon's height over it (FldUtilGetBalloonPosOffset: the table by name - c001 0.21,
    // obj002 0.23, ... - else NPCs 0.2, objects and events 0.25, effects 0.1)
    float balloonHeight() const;
};

// The gimmicks of a <map>p figure: its skeleton, the joints' names (the .name file's order)
std::vector<Gimmick> readGimmicks(const Skeleton& p, const std::vector<std::string>& jointNames);

} // namespace cr2
