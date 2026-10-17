#pragma once
// A field's script: the dungeon's (`script\t01.nut`, `script\dNN.nut`: Cr3EventScriptFileNameTbl,
// loaded when the dungeon changes - FldMain::LoadScriptFile) in the game's Squirrel VM, with the
// natives a map's set-up needs - the story's flags and work values, the dungeon and area, which
// gimmicks show, collide and react (Cr3Flag, Cr3Work, Cr3System, Cr3GimmickData, Cr3FieldMap).
// On entering an area the engine calls its `evt000` (FldMain::Update after ChangeAreaExecute, and
// FldMain::Init for a new game / a loaded one; `evt100` after a battle, `evt200` after a vista),
// which runs <dungeon>_gim: the gimmicks for the story so far - and the area's own set-up, which
// may play an event there and then (the field's events use the scene, the windows and the fades
// as a vista's do: event/runtime.hpp's natives, under the field's own) and reserve what comes
// next (GameState::next). The entry runs as the game runs it: a coroutine, its WaitFrames counted
// in the game's updates (tick()).

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "assets/mvgl.hpp"
#include "event/runtime.hpp"
#include "field/gimmicks.hpp"
#include "script/gamestate.hpp"
#include "script/vm.hpp"

namespace cr2 {

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
    // reset to their defaults, then `entry` (evt000) started - running up to its first wait.
    // False: the script or the call failed (error() says).
    bool enter(const std::string& map, const std::vector<Gimmick>& gimmicks, const std::string& entry = "evt000");
    // a call of another of the script's functions (a gimmick's: touched, tapped), started the same way
    bool call(const std::string& function);
    // one of the game's updates: the field's event state, the script resumed when its wait is up.
    // False: it failed
    bool tick();
    bool running() const { return vm_ && vm_->waiting(); }
    bool hasFunction(const std::string& f) const { return vm_ && vm_->hasFunction(f); }
    // ticks until the script finishes (its pages turning by themselves) and the fade or camera
    // slide it started settles, at most `maxUpdates`
    bool runToEnd(int maxUpdates = 20 * 60 * 10);
    EventRuntime& events() { return *events_; }

    // The field's camera as the script has it (Cr3FieldMapCamera; x, y in a 960-wide screen's
    // pixels): following the leader (mode 0, no SetPos since), else at a place or sliding to one
    // (CalcParameter: over its frames, one an update). scrolled: the scroll shown now, for
    // GetPos_X / _Y while it follows
    struct Camera {
        int mode = 0;
        bool direct = false;
        float x = 0, y = 0;
        float fromX = 0, fromY = 0, toX = 0, toY = 0;
        int frames = 0, t = 0;
        bool scripted() const { return direct || mode != 0; }
    };
    const Camera& camera() const { return camera_; }
    void setShownScroll(float x, float y) { shownX_ = x, shownY_ = y; }

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
    std::unique_ptr<EventRuntime> events_;
    std::string script_;
    int dungeon_ = 0, area_ = 0;
    std::map<std::string, GimmickState> gimmicks_;
    bool layers_[4] = {true, true, true, true};
    Camera camera_;
    float shownX_ = 0, shownY_ = 0;
    std::string error_;
};

} // namespace cr2
