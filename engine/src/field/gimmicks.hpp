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
    std::string figure; // the figure it shows ("" none)
    float position[3] = {0, 0, 0};
    std::array<float, 4> rotation{0, 0, 0, 1}; // quaternion x y z w
    float jointScale[3] = {1, 1, 1};          // (the collision box's size x 100)
    float scale = 1;                           // the figure's: _scaleNNN / 100
    bool check = false;                        // _check: tapped
    enum Shape { Ball, ShapeBox, Capsule } shape = Ball;
    const char* kindName() const;
};

// The gimmicks of a <map>p figure: its skeleton, the joints' names (the .name file's order)
std::vector<Gimmick> readGimmicks(const Skeleton& p, const std::vector<std::string>& jointNames);

} // namespace cr2
