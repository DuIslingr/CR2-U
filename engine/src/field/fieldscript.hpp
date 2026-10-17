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

#include <array>
#include <functional>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <utility>
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
    // an event's motion (SetMotion: FldObject / FldNPC::SetEventAnim - the animator named, over
    // its idle; looping unless SetMotionLoop says not), `motionSerial` counting each one set
    std::string motion;
    bool motionLoop = true;
    int motionSerial = 0;
    int motionTick = 0; // (when: FieldScript::ticks())
    // its alpha (ChangeAlpha(from, to, updates): FldAlphaManager - from by a step an update to `to`)
    float alphaFrom = 1, alphaTo = 1;
    int alphaUpdates = 0, alphaTick = 0;
    // turned to something (ChangeDirection(gimmick, target, updates): FldDirectionManager - from
    // where it faces to `faceTarget`, "player" or a gimmick, a 1 / updates step an update)
    std::string faceTarget;
    // an NPC's colours (ChangeModelColor(gimmick, n): FldNPC::ChangeTexture_Color - every material's
    // colour and normal textures m<its number><a01 b01 c01 d01> / ..n; -1 its own)
    int color = -1;
    int faceUpdates = 0, faceSerial = 0, faceTick = 0;
    float alpha() const { return alphaUpdates <= 0 || alphaTick >= alphaUpdates ? alphaTo : alphaFrom + (alphaTo - alphaFrom) * float(alphaTick) / float(alphaUpdates); }
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
    // Cr3FieldMapCamera.QuakeStart(width, from %, to %, frames) (FldUtilFieldMapCameraQuakeStart:
    // the rates x 0.01, the frames x 0.05 s; FldMap::UpdateQuake): each update the scroll moved
    // width x the rate - from .. to over the time, then held, or stopped when it ends at 0 -, one
    // way then the other (in a 960-wide screen's pixels); QuakeStop
    float quakeOffset() const { return quake_.on ? quake_.offset : 0.0f; }
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
    // the box tapped last's item (Cr3GimmickData.GetItemID: FldMain's box - its box_msg's)
    int boxItem = -1;
    int dungeon() const { return dungeon_; }
    int area() const { return area_; }
    bool layerVisible(int layer) const { return layers_[layer]; } // 0 bg, 1 fg, 2 mg, 3 the 3D figure
    // the leader as the script has it (Cr3FieldMapCharacter): shown (SetVisible), and where an
    // event's scene put it (SetPos_p000 / SetRot_p000: its p000 joint) - taken once
    bool playerVisible() const { return playerVisible_; }
    // (the leader's alpha: Cr3FieldMapCharacter.ChangeAlpha, as a gimmick's)
    float playerAlpha() const { return playerAlpha_.alpha(); }
    // (the leader turned to a gimmick: Cr3FieldMapCharacter.ChangeDirection(target, updates))
    const GimmickState& playerState() const { return playerAlpha_; }
    int ticks() const { return ticks_; } // (the updates since it was made)
    // the field's two characters (FldPlayer +0x1c: made from the party as the area is - FldMain::
    // Init -, Cr3FieldMapCharacter.ChangeModel(slot, character) changing one, -1 the leader's);
    // the leader the one GameState::active names
    int fieldCharacter(int slot) const { return slot == 0 || slot == 1 ? fieldCharacters_[slot] : -1; }
    int leader() const { return fieldCharacter(state_.active); }
    // The touch actions (FldSprint, FldHighJump, FldMystic, FldIai, FldAim, FldHakkei - kinds 0, 1,
    // 3, 4, 5, 6 in the natives: sprint, hjump, mystic, iai, aim, marts gimmicks): on or not
    // (Cr3GimmickData.SetTouchActionEnable - off as the area is made), and the spots they go by
    // (AddSpot(kind, gimmick, spot): the spot gimmick's place appended - FldGimmickDataManager::
    // AddSpotLink; ResetSpot emptying them)
    struct TouchAction {
        bool enabled = false;
        std::vector<std::array<float, 3>> spots;
        int touches = 5; // (a hakkei's taps: FldHakkei +0x44, SetHakkeiTouchCnt)
        std::vector<std::string> spotNames; // (AddSpot's: a mystic's names them)
        std::string activeSpot;             // (a mystic's: SetMysticActiveSpotName - where its mark is)
        // the spot the mark is on: a mystic's active one (FldMystic::SetActiveSpotNumber: the first
        // named so, else 0), else the first
        size_t markSpot() const {
            for (size_t i = 0; i < spotNames.size(); ++i)
                if (spotNames[i] == activeSpot) return i;
            return 0;
        }
    };
    // The flag marks (FldFlagMark: Cr3GimmickData.SetFlagMark / ClearFlagMark / AllClearFlagMark -
    // the gimmicks with the `flag` figure over them, where their balloon would be, while they have
    // none; emptied as an area is made) and the balloons and marks shown at all (SetBoxVisible:
    // FldMain +0xa4, InterfaceMain::GimmickAllViewOn / Off)
    const std::set<std::string>& flagMarks() const { return flagMarks_; }
    bool boxVisible() const { return boxVisible_; }
    // The dungeon's name, shown as it is entered (EvtIntroduction.SetDungeonNum(n): InterfaceMain::
    // SetMapNameMenuParameter - MapTitleMenu, `mapName` (mapNameFre, ...: the language's) on its
    // mapNameD01.. animator, n 0 and 1 the first; GetVisible: till that animation ends): the
    // dungeon (1.., 0 none) and since when (ticks()); -1 none
    // The resident effects (FldEffectResident, made with the field: eff030, eff031, eff041 .. -
    // FldMain::Init's table): Cr3FieldMapEffectResident.SetPosRot(n, target) - on the leader
    // ("player") or a gimmick -, SetScale(n, x, y, z), Play(n) - its animator from its start, once
    struct Resident {
        std::string target = "player";
        float scale[3] = {1, 1, 1};
        int tick = -1; // (when played: ticks(); -1 not)
        int serial = 0;
    };
    static const char* residentFigure(int n);
    // The game's saves (Cr3Save: Cr3Util_Save_GameData - SaveGame, as the field's Save writes it -,
    // Save_ContData - SaveCont, each area's entry: the continue data -, Delete_ContData; the system
    // data's and the Commits nothing here): what saves them (0 the game, 1 the continue data, 2 the
    // continue data gone) - the viewer's
    std::function<void(int)> onSave;
    const std::map<int, Resident>& residents() const { return residents_; }
    int introDungeon() const { return introDungeon_; }
    int introTick() const { return introTick_; }
    float introSeconds = 0; // (the animator's length: the viewer's, as it loads it)
    // the spot a mystic's tap chose (FldMystic::SetTapSpotName: its game's last)
    std::string mysticTapSpot;
    const TouchAction* touchAction(const std::string& gimmick) const {
        const auto it = actions_.find(gimmick);
        return it == actions_.end() ? nullptr : &it->second;
    }
    TouchAction* touchAction(const std::string& gimmick) {
        const auto it = actions_.find(gimmick);
        return it == actions_.end() ? nullptr : &it->second;
    }
    struct Placement {
        bool position = false, heading = false;
        float at[3] = {0, 0, 0};
        float yaw = 0; // (radians about y, 0 = +z)
    };
    Placement takePlacement() { return std::exchange(placement_, Placement()); }
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
    struct Quake {
        bool on = false;
        float width = 0, from = 0, to = 0, seconds = 0, time = 0, offset = 0;
        int parity = 0;
    } quake_;
    bool playerVisible_ = true;
    GimmickState playerAlpha_;
    int ticks_ = 0;
    int fieldCharacters_[2] = {0, -1};
    std::vector<Gimmick> fieldGimmicks_;
    std::map<std::string, TouchAction> actions_;
    std::set<std::string> flagMarks_;
    int introDungeon_ = -1, introTick_ = 0;
    std::map<int, Resident> residents_;
    bool boxVisible_ = true;
    TouchAction* findAction(int kind, const std::string& name);
    Placement placement_;
    Camera camera_;
    float shownX_ = 0, shownY_ = 0;
    std::string error_;
};

} // namespace cr2
