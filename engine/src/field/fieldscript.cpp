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
    vm.setNative("Cr3System", "GetDifficulty", [](NativeCall& c) { c.result(0); });
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
    playerVisible_ = true, placement_ = Placement(); // (FldMain makes the leader again)
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

bool FieldScript::call(const std::string& function) {
    if (!vm_) return error_ = "no script", false;
    if (!vm_->call(function)) {
        error_ = script_ + "." + function + ": " + vm_->error();
        return false;
    }
    return true;
}

bool FieldScript::tick() {
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
    return it == gimmicks_.end() ? g.kind != Gimmick::Npc : it->second.visible;
}

std::map<std::string, int> FieldScript::stubCalls() const { return vm_ ? vm_->stubCalls() : std::map<std::string, int>(); }

} // namespace cr2
