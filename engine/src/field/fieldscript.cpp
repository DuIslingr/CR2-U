#include "field/fieldscript.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace cr2 {

FieldScript::FieldScript(const MvglArchive& archive, GameState& state)
    : archive_(archive), state_(state), events_(std::make_unique<EventRuntime>(archive, state)) {}

void FieldScript::bind() {
    ScriptVM& vm = *vm_;
    bindGameState(vm, state_);
    events_->bindNatives(vm); // (the field's events: their scene, windows, fades)
    vm.setNative("Cr3System", "GetDungeonNum", [this](NativeCall& c) { c.result(dungeon_); });
    vm.setNative("Cr3System", "GetAreaNum", [this](NativeCall& c) { c.result(area_); });
    vm.setNative("Cr3System", "GetSelectDungeonNum", [this](NativeCall& c) { c.result(dungeon_); });
    vm.setNative("Cr3System", "SetDifficulty", [this](NativeCall& c) { state_.difficulty = c.integer(0); }); // (Cr3UtilSetDifficulty)
    vm.setNative("Cr3System", "GetDifficulty", [this](NativeCall& c) { c.result(state_.difficulty); });
    vm.setNative("Cr3GimmickData", "GetItemID", [this](NativeCall& c) { c.result(boxItem); });
    vm.setNative("Cr3GimmickData", "SetTouchActionEnable", [this](NativeCall& c) {
        if (TouchAction* a = findAction(c.integer(0), c.string(1))) a->enabled = c.boolean(2);
    });
    vm.setNative("Cr3GimmickData", "AddSpot", [this](NativeCall& c) {
        TouchAction* a = findAction(c.integer(0), c.string(1));
        const std::string spot = c.string(2);
        if (!a || spot.empty()) return;
        for (const Gimmick& g : fieldGimmicks_)
            if (g.kind == Gimmick::Spot && g.name.find(spot) != std::string::npos) {
                a->spots.push_back({g.position[0], g.position[1], g.position[2]});
                a->spotNames.push_back(spot.substr(0, 6));
                return;
            }
    });
    vm.setNative("Cr3GimmickData", "SetHakkeiTouchCnt", [this](NativeCall& c) {
        if (TouchAction* a = findAction(6, c.string(0))) a->touches = std::max(c.integer(1), 1);
    });
    vm.setNative("Cr3GimmickData", "ResetSpot", [this](NativeCall& c) {
        if (TouchAction* a = findAction(c.integer(0), c.string(1))) a->spots.clear(), a->spotNames.clear();
    });
    vm.setNative("Cr3GimmickData", "SetMysticActiveSpotName", [this](NativeCall& c) {
        if (TouchAction* a = findAction(3, c.string(0))) a->activeSpot = c.string(1).substr(0, 6);
    });
    vm.setNative("Cr3GimmickData", "GetMysticActiveSpotName", [this](NativeCall& c) {
        const TouchAction* a = findAction(3, c.string(0));
        c.result(a ? a->activeSpot : std::string());
    });
    vm.setNative("Cr3GimmickData", "GetMysticTapSpotName", [this](NativeCall& c) { c.result(mysticTapSpot); });
    auto gimmick = [this](NativeCall& c) -> GimmickState* {
        const auto it = gimmicks_.find(c.string(0));
        return it == gimmicks_.end() ? nullptr : &it->second;
    };
    vm.setNative("Cr3GimmickData", "Disp", [gimmick](NativeCall& c) {
        if (GimmickState* g = gimmick(c)) g->visible = c.boolean(1);
    });
    vm.setNative("Cr3GimmickData", "IsDisp", [gimmick](NativeCall& c) {
        const GimmickState* g = gimmick(c);
        c.result(g && g->visible);
    });
    vm.setNative("Cr3GimmickData", "SetCollision", [gimmick](NativeCall& c) {
        if (GimmickState* g = gimmick(c)) g->collision = c.boolean(1);
    });
    vm.setNative("Cr3GimmickData", "SetEventEnable", [gimmick](NativeCall& c) {
        if (GimmickState* g = gimmick(c)) g->event = c.boolean(1);
    });
    vm.setNative("Cr3GimmickData", "GetEventEnable", [gimmick](NativeCall& c) {
        const GimmickState* g = gimmick(c);
        c.result(g && g->event);
    });
    // the field's camera (FldUtilSetFieldMapCameraMode / Pos / MoveCalcParameter, GetPos_*)
    vm.setNative("Cr3FieldMapCamera", "SetMode", [this](NativeCall& c) {
        camera_.mode = c.integer(0);
        if (camera_.mode == 0) camera_.direct = false, camera_.frames = 0;
    });
    vm.setNative("Cr3FieldMapCamera", "SetPos", [this](NativeCall& c) {
        camera_.direct = true, camera_.frames = 0;
        camera_.x = c.number(0), camera_.y = c.number(1);
    });
    vm.setNative("Cr3FieldMapCamera", "CalcParameter", [this](NativeCall& c) {
        camera_.direct = true;
        camera_.frames = std::max(c.integer(0), 1), camera_.t = 0;
        camera_.fromX = c.number(1), camera_.fromY = c.number(2), camera_.toX = c.number(3), camera_.toY = c.number(4);
        camera_.x = camera_.fromX, camera_.y = camera_.fromY;
    });
    vm.setNative("Cr3FieldMapCamera", "GetPos_X", [this](NativeCall& c) { c.result(camera_.scripted() ? camera_.x : shownX_); });
    vm.setNative("Cr3FieldMapCamera", "GetPos_Y", [this](NativeCall& c) { c.result(camera_.scripted() ? camera_.y : shownY_); });
    vm.setNative("Cr3FieldMapCharacter", "SetVisible", [this](NativeCall& c) { playerVisible_ = c.boolean(0, true); });
    auto alpha = [](GimmickState& g, NativeCall& c, int at) {
        g.alphaFrom = c.number(at), g.alphaTo = c.number(at + 1), g.alphaUpdates = c.integer(at + 2), g.alphaTick = 0;
    };
    vm.setNative("Cr3FieldMapCharacter", "ChangeAlpha", [this, alpha](NativeCall& c) { alpha(playerAlpha_, c, 0); });
    vm.setNative("Cr3GimmickData", "ChangeAlpha", [this, alpha](NativeCall& c) {
        if (auto it = gimmicks_.find(c.string(0)); it != gimmicks_.end()) alpha(it->second, c, 1);
    });
    auto face = [this](GimmickState& g, const std::string& target, int updates) {
        g.faceTarget = target, g.faceUpdates = std::max(updates, 1), ++g.faceSerial, g.faceTick = ticks_;
    };
    vm.setNative("Cr3GimmickData", "ChangeDirection", [this, face](NativeCall& c) { // (FldUtilChangeNPCDirection)
        if (auto it = gimmicks_.find(c.string(0)); it != gimmicks_.end()) face(it->second, c.string(1), c.integer(2));
    });
    vm.setNative("Cr3FieldMapCharacter", "ChangeDirection", [this, face](NativeCall& c) { face(playerAlpha_, c.string(0), c.integer(1)); });
    vm.setNative("Cr3GimmickData", "SetMotion", [this](NativeCall& c) {
        if (auto it = gimmicks_.find(c.string(0)); it != gimmicks_.end())
            it->second.motion = c.string(1), it->second.motionLoop = true, ++it->second.motionSerial, it->second.motionTick = ticks_;
    });
    vm.setNative("Cr3GimmickData", "SetMotionLoop", [this](NativeCall& c) {
        if (auto it = gimmicks_.find(c.string(0)); it != gimmicks_.end()) it->second.motionLoop = c.boolean(1);
    });
    vm.setNative("Cr3FieldMapCharacter", "ChangeModel", [this](NativeCall& c) { // (FldPlayer::ChangeModel)
        const int slot = c.integer(0, -1) == -1 ? state_.active : c.integer(0);
        if (slot == 0 || slot == 1) fieldCharacters_[slot] = c.integer(1);
    });
    // the character change (FldChangeCharacter): its button allowed, the leader, a change at once
    // (FldMain::ChangeCharacterQuick: the other of the two, unless it leads already)
    vm.setNative("Cr3ChangeCharacter", "SetEnable", [this](NativeCall& c) { state_.changeEnable = c.boolean(0); });
    vm.setNative("Cr3TabMenu", "SetEnable", [this](NativeCall& c) { state_.tabEnable = c.boolean(0); }); // (FldUtilSetTabMenuEnable)
    vm.setNative("Cr3ChangeCharacter", "GetEnable", [this](NativeCall& c) { c.result(state_.changeEnable); });
    vm.setNative("Cr3ChangeCharacter", "GetActive", [this](NativeCall& c) { c.result(state_.active); });
    vm.setNative("Cr3ChangeCharacter", "ExeQuick", [this](NativeCall& c) {
        if (c.integer(0) != state_.active) state_.active = state_.active == 0 ? 1 : 0;
    });
    vm.setNative("Cr3FieldMapCharacter", "SetPos_p000", [this](NativeCall&) {
        Mat34 w;
        if (!events_->scene().p000(w)) return;
        placement_.position = true;
        placement_.at[0] = w.m[3], placement_.at[1] = w.m[7], placement_.at[2] = w.m[11];
    });
    vm.setNative("Cr3FieldMapCharacter", "SetRot_p000", [this](NativeCall&) {
        Mat34 w;
        if (!events_->scene().p000(w)) return;
        placement_.heading = true;
        placement_.yaw = std::atan2(w.m[2], w.m[10]); // (where its +z faces)
    });
    vm.setNative("Cr3FieldMap", "SetVisibleBG", [this](NativeCall& c) { layers_[0] = c.boolean(0); });
    vm.setNative("Cr3FieldMap", "SetVisibleFG", [this](NativeCall& c) { layers_[1] = c.boolean(0); });
    vm.setNative("Cr3FieldMap", "SetVisibleMG", [this](NativeCall& c) { layers_[2] = c.boolean(0); });
    vm.setNative("Cr3FieldMap", "SetVisible3D", [this](NativeCall& c) { layers_[3] = c.boolean(0); });
}

bool FieldScript::enter(const std::string& map, const std::vector<Gimmick>& gimmicks, const std::string& entry) {
    error_.clear();
    playerVisible_ = true, placement_ = Placement(), playerAlpha_ = GimmickState(); // (FldMain makes the leader again)
    fieldCharacters_[0] = state_.party[0], fieldCharacters_[1] = state_.party[1];
    if (state_.party[1] < 0) state_.active = 0;
    if (map.size() < 5) return error_ = map + ": not a map's name", false;
    // the dungeon table (FldMap::GetCr3FieldDataDungeonNumFromFieldMapNum): the town 0, dNN NN; the
    // area the map's last two digits - 1
    dungeon_ = map[0] == 't' ? 0 : std::atoi(map.substr(1, 2).c_str());
    area_ = std::atoi(map.substr(3, 2).c_str()) - 1;
    const std::string script = map[0] == 't' ? "t01" : "d" + map.substr(1, 2);
    if (!vm_ || script != script_) {
        vm_ = std::make_unique<ScriptVM>();
        bind();
        std::vector<uint8_t> bytecode;
        if (!archive_.read("script\\" + script + ".nut", bytecode, &error_) || !vm_->load(bytecode, script) || !vm_->runMain()) {
            if (error_.empty()) error_ = script + ": " + vm_->error();
            vm_.reset();
            script_.clear();
            return false;
        }
        script_ = script;
    }
    gimmicks_.clear();
    actions_.clear();
    fieldGimmicks_ = gimmicks;
    camera_ = Camera();
    for (int k = 0; k < 4; ++k) layers_[k] = true;
    for (const Gimmick& g : gimmicks) {
        GimmickState s;
        s.visible = g.kind != Gimmick::Npc;
        gimmicks_[g.id] = s;
    }
    events_->reset();
    if (!vm_->hasFunction(entry)) return true; // (a dungeon without one)
    return call(entry);
}

FieldScript::TouchAction* FieldScript::findAction(int kind, const std::string& name) {
    static const char* const kKinds[7] = {"sprint", "hjump", nullptr, "mystic", "iai", "aim", "marts"};
    if (kind < 0 || kind > 6 || !kKinds[kind] || name.empty()) return nullptr;
    for (const Gimmick& g : fieldGimmicks_)
        if (g.kind == Gimmick::Ability && g.name.find(kKinds[kind]) != std::string::npos && g.name.find(name) != std::string::npos)
            return &actions_[g.name];
    return nullptr;
}

bool FieldScript::call(const std::string& function) {
    if (!vm_) return error_ = "no script", false;
    if (!vm_->call(function)) {
        error_ = script_ + "." + function + ": " + vm_->error();
        return false;
    }
    return true;
}

bool FieldScript::tick() {
    ++ticks_;
    for (auto& [id, g] : gimmicks_)
        if (g.alphaTick < g.alphaUpdates) ++g.alphaTick; // (FldAlphaManager::Update)
    if (playerAlpha_.alphaTick < playerAlpha_.alphaUpdates) ++playerAlpha_.alphaTick;
    if (camera_.frames > 0 && camera_.t < camera_.frames) { // (a slide: a frame an update)
        const float k = float(++camera_.t) / float(camera_.frames);
        camera_.x = camera_.fromX + (camera_.toX - camera_.fromX) * k;
        camera_.y = camera_.fromY + (camera_.toY - camera_.fromY) * k;
    }
    if (!events_->update(vm_.get())) return error_ = events_->error(), false;
    return true;
}

bool FieldScript::runToEnd(int maxUpdates) {
    const float keep = events_->autoPage;
    events_->autoPage = std::max(keep, 0.05f);
    bool ok = true;
    int k = 0;
    for (; ok && running() && k < maxUpdates; ++k) ok = tick();
    // (and what it set going: a fade, a camera slide, still moving on the updates after)
    const auto moving = [this] {
        const EventFade& f = events_->fade();
        return f.alpha != f.target || (camera_.frames > 0 && camera_.t < camera_.frames);
    };
    for (; ok && !running() && moving() && k < maxUpdates; ++k) ok = tick();
    events_->autoPage = keep;
    return ok && !running();
}

bool FieldScript::visible(const Gimmick& g) const {
    const auto it = gimmicks_.find(g.id);
    if (g.kind == Gimmick::Box && !state_.boxes.isSet(g.boxKind, g.boxNumber)) return false; // (FldBox::Pose / Draw: one placed)
    return it == gimmicks_.end() ? g.kind != Gimmick::Npc : it->second.visible;
}

std::map<std::string, int> FieldScript::stubCalls() const { return vm_ ? vm_->stubCalls() : std::map<std::string, int>(); }

} // namespace cr2
