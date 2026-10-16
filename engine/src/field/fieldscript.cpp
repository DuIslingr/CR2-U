#include "field/fieldscript.hpp"

#include <cstdlib>

namespace cr2 {

FieldScript::FieldScript(const MvglArchive& archive, GameState& state) : archive_(archive), state_(state) {}

void FieldScript::bind() {
    ScriptVM& vm = *vm_;
    vm.setNative("Cr3Flag", "Set", [this](NativeCall& c) { state_.setFlag(c.integer(0), true); });
    vm.setNative("Cr3Flag", "Clear", [this](NativeCall& c) { state_.setFlag(c.integer(0), false); });
    vm.setNative("Cr3Flag", "Check", [this](NativeCall& c) { c.result(state_.flag(c.integer(0))); });
    vm.setNative("Cr3Work", "Set", [this](NativeCall& c) { state_.work[c.integer(0)] = c.integer(1); });
    vm.setNative("Cr3Work", "Get", [this](NativeCall& c) {
        const auto it = state_.work.find(c.integer(0));
        c.result(it == state_.work.end() ? 0 : it->second);
    });
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
    vm.setNative("Cr3FieldMap", "SetVisibleBG", [this](NativeCall& c) { layers_[0] = c.boolean(0); });
    vm.setNative("Cr3FieldMap", "SetVisibleFG", [this](NativeCall& c) { layers_[1] = c.boolean(0); });
    vm.setNative("Cr3FieldMap", "SetVisibleMG", [this](NativeCall& c) { layers_[2] = c.boolean(0); });
    vm.setNative("Cr3FieldMap", "SetVisible3D", [this](NativeCall& c) { layers_[3] = c.boolean(0); });
}

bool FieldScript::enter(const std::string& map, const std::vector<Gimmick>& gimmicks, const std::string& entry) {
    error_.clear();
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
    for (int k = 0; k < 4; ++k) layers_[k] = true;
    for (const Gimmick& g : gimmicks) {
        GimmickState s;
        s.visible = g.kind != Gimmick::Npc;
        gimmicks_[g.id] = s;
    }
    if (!vm_->hasFunction(entry)) return true; // (a dungeon without one)
    if (!vm_->call(entry)) {
        error_ = script + "." + entry + ": " + vm_->error();
        return false;
    }
    return true;
}

bool FieldScript::visible(const Gimmick& g) const {
    const auto it = gimmicks_.find(g.id);
    return it == gimmicks_.end() ? g.kind != Gimmick::Npc : it->second.visible;
}

std::map<std::string, int> FieldScript::stubCalls() const { return vm_ ? vm_->stubCalls() : std::map<std::string, int>(); }

} // namespace cr2
