#pragma once
// A field's script: the dungeon's (`script\t01.nut`, `script\dNN.nut`: Cr3EventScriptFileNameTbl,
// loaded when the dungeon changes - FldMain::LoadScriptFile) in the game's Squirrel VM, with the
// natives a map's set-up needs - the story's flags and work values, the dungeon and area, which
// gimmicks show, collide and react (Cr3Flag, Cr3Work, Cr3System, Cr3GimmickData, Cr3FieldMap).
// On entering an area the engine calls its `evt000` (FldMain::Update after ChangeAreaExecute, and
// FldMain::Init for a new game / a loaded one; `evt100` after a battle, `evt200` after a vista),
// which runs <dungeon>_gim: the gimmicks for the story so far.

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "assets/mvgl.hpp"
#include "field/gimmicks.hpp"
#include "script/vm.hpp"

namespace cr2 {

// The story's state the scripts read and write (the save data's flags and work values)
struct GameState {
    std::vector<bool> flags; // Cr3Flag: Set / Clear / Check
    std::map<int, int> work; // Cr3Work: Set / Get
    bool flag(int n) const { return n >= 0 && size_t(n) < flags.size() && flags[size_t(n)]; }
    void setFlag(int n, bool on) {
        if (n < 0) return;
        if (size_t(n) >= flags.size()) flags.resize(size_t(n) + 1);
        flags[size_t(n)] = on;
    }
};

// the natives on the story's state (Cr3Flag, Cr3Work), for any script
void bindGameState(ScriptVM& vm, GameState& state);

// A gimmick as the scripts leave it (by its id: Gimmick::id)
struct GimmickState {
    bool visible = false;   // NPCs start hidden (FldNPC +0x10); objects, effects and boxes shown
    bool collision = true;  // Cr3GimmickData.SetCollision
    bool event = true;      // Cr3GimmickData.SetEventEnable (touching / tapping it does something)
};

class FieldScript {
public:
    FieldScript(const MvglArchive& archive, GameState& state);

    // An area of a map ("d0101"): the dungeon's script loaded if it changed, the gimmicks' states
    // reset to their defaults, then `entry` (evt000) run. False: the script or the call failed
    // (error() says).
    bool enter(const std::string& map, const std::vector<Gimmick>& gimmicks, const std::string& entry = "evt000");

    const std::map<std::string, GimmickState>& gimmicks() const { return gimmicks_; }
    bool visible(const Gimmick& g) const;
    int dungeon() const { return dungeon_; }
    int area() const { return area_; }
    bool layerVisible(int layer) const { return layers_[layer]; } // 0 bg, 1 fg, 2 mg, 3 the 3D figure
    const std::string& error() const { return error_; }
    // the natives the scripts called that are still stubs ("Table.Function" -> calls)
    std::map<std::string, int> stubCalls() const;

private:
    void bind();
    const MvglArchive& archive_;
    GameState& state_;
    std::unique_ptr<ScriptVM> vm_;
    std::string script_;
    int dungeon_ = 0, area_ = 0;
    std::map<std::string, GimmickState> gimmicks_;
    bool layers_[4] = {true, true, true, true};
    std::string error_;
};

} // namespace cr2
