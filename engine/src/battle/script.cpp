#include "battle/script.hpp"

#include <algorithm>
#include <utility>

namespace cr2 {

BattleScript::BattleScript(const MvglArchive& archive, GameState& state, BattleRules& rules, int coupling, const int party[2],
                           const int monsters[3])
    : archive_(archive), state_(state), rules_(rules), coupling_(coupling), events_(std::make_unique<EventRuntime>(archive, state)) {
    for (int k = 0; k < 2; ++k) party_[k] = party[k];
    for (int k = 0; k < 3; ++k) monsters_[k] = monsters[k];
}

bool BattleScript::load(const std::string& name) {
    if (vm_)
        for (const auto& [f, n] : vm_->stubCalls()) stubs_[f] += n;
    vm_ = std::make_unique<ScriptVM>(); // (Cr3Event: ReleaseObject, CreateNewObject, Load, Run)
    bind();
    std::vector<uint8_t> bytecode;
    if (!archive_.read("script\\" + name + ".nut", bytecode, &error_) || !vm_->load(bytecode, name) || !vm_->runMain()) {
        if (error_.empty()) error_ = name + ": " + vm_->error();
        vm_.reset();
        return false;
    }
    name_ = name;
    events_->reset();
    return true;
}

bool BattleScript::begin() {
    error_.clear();
    state_.setFlag(0xbeb, true), state_.setFlag(0xbec, true); // (StepBattleIn, before Battle_Init)
    return load("battle") && call("Battle_Init");
}

bool BattleScript::call(const std::string& function) {
    if (!vm_ || !vm_->hasFunction(function)) return true;
    calls_.push_back(function);
    if (!vm_->call(function)) return error_ = name_ + "." + function + ": " + vm_->error(), false;
    if (!vm_->waiting()) return tick(); // (over at once: what it named loaded now)
    return true;
}

bool BattleScript::running() const { return vm_ && vm_->waiting(); }

bool BattleScript::tick() {
    if (!vm_) return false;
    if (!events_->update(vm_.get())) return error_ = events_->error(), false;
    if (running()) return true;
    // BtlEvent::Update, the call over: JumpScript's script and function, else SetScript's script
    if (!jumpScript_.empty()) {
        const std::string s = std::exchange(jumpScript_, ""), f = std::exchange(jumpCall_, "");
        pendingScript_.clear();
        return load(s) && call(f);
    }
    if (!pendingScript_.empty()) return load(std::exchange(pendingScript_, ""));
    return true;
}

bool BattleScript::runToEnd(int maxUpdates) {
    const float keep = events_->autoPage;
    events_->autoPage = std::max(keep, 0.05f);
    bool ok = true;
    for (int k = 0; ok && running() && k < maxUpdates; ++k) ok = tick();
    events_->autoPage = keep;
    return ok && !running();
}

std::map<std::string, int> BattleScript::stubCalls() const {
    std::map<std::string, int> out = stubs_;
    if (vm_)
        for (const auto& [f, n] : vm_->stubCalls()) out[f] += n;
    return out;
}

void BattleScript::bind() {
    ScriptVM& vm = *vm_;
    bindGameState(vm, state_);
    events_->bindNatives(vm); // (the battles' talks: cut-ins, messages, fades)
    BattleRules& r = rules_;
    vm.setNative("Cr3Battle", "GetCoupling", [this](NativeCall& c) { c.result(coupling_); });
    vm.setNative("Cr3Battle", "GetTurn", [&r](NativeCall& c) { c.result(r.turn() + 1); });
    vm.setNative("Cr3Battle", "GetCommandActor", [&r](NativeCall& c) { c.result(r.scriptCommandActor()); });
    vm.setNative("Cr3Battle", "GetCommand", [&r](NativeCall& c) { c.result(r.commandOf(r.scriptCommandActor())); });
    vm.setNative("Cr3Battle", "GetPlayer", [this](NativeCall& c) {
        const int k = c.integer(0);
        c.result(k >= 0 && k < 2 ? party_[k] : -1);
    });
    vm.setNative("Cr3Battle", "GetMonster", [this](NativeCall& c) {
        const int k = c.integer(0);
        c.result(k >= 0 && k < 3 ? monsters_[k] : 0);
    });
    vm.setNative("Cr3Battle", "JumpScript", [this](NativeCall& c) { jumpScript_ = c.string(0), jumpCall_ = c.string(1); });
    vm.setNative("Cr3Battle", "SetScript", [this](NativeCall& c) {
        pendingScript_ = c.string(0);
        own_ = true; // (+0x237a)
    });
    vm.setNative("Cr3Battle", "SetCommand", [&r](NativeCall& c) { r.setScriptCommand(c.integer(0), c.integer(1)); });
    vm.setNative("Cr3Battle", "SetCommandTarget", [&r](NativeCall& c) { r.setScriptTarget(c.integer(0), c.integer(1)); });
    vm.setNative("Cr3Battle", "SetPair", [&r](NativeCall& c) { r.setScriptPair(c.integer(0)); });
    vm.setNative("Cr3Battle", "ReserveCommand", [&r](NativeCall&) { r.reserveScriptTurn(); });
    vm.setNative("Cr3Battle", "Charge", [&r](NativeCall& c) { r.addCharge(c.integer(0)); });
    vm.setNative("Cr3Battle", "Exit", [&r](NativeCall&) { r.exitBattle(); });
    vm.setNative("Cr3Battle", "Debug", [](NativeCall&) {});
    vm.setNative("Cr3Battle", "SetParamI", [&r](NativeCall& c) { r.setParamI(c.integer(0), c.integer(1)); });
    vm.setNative("Cr3Battle", "GetParamI", [&r](NativeCall& c) { c.result(r.paramI(c.integer(0))); });
    vm.setNative("Cr3Battle", "SetParamI2", [this](NativeCall& c) {
        const int kind = c.integer(0), i = c.integer(1), v = c.integer(2);
        if (i < 0 || i >= 32) return;
        if (kind == 0) state_.sopiaLevel[i] = static_cast<signed char>(v);
        else if (kind == 1) state_.sopiaExp[i] = v;
    });
    vm.setNative("Cr3Battle", "GetParamI2", [this](NativeCall& c) {
        const int kind = c.integer(0), i = c.integer(1);
        c.result(i < 0 || i >= 32 ? 0 : kind == 0 ? int(state_.sopiaLevel[i]) : kind == 1 ? state_.sopiaExp[i] : 0);
    });
    vm.setNative("Cr3Battle", "SetParamF", [](NativeCall&) {});
    vm.setNative("Cr3Battle", "SetParamS", [](NativeCall&) {});
    // Invoke: 0 the states cleared; 1 the status windows shown, 2 hidden (and the announcement
    // cleared and hidden), 3 / 4 the announcement shown / hidden; 5 and 6 switches (+0x237b,
    // +0x237c) - not read here yet
    vm.setNative("Cr3Battle", "Invoke", [this, &r](NativeCall& c) {
        switch (c.integer(0)) {
        case 0: r.clearStates(); break;
        case 1: statusShown_ = true; break;
        case 2: statusShown_ = false; break;
        default: break;
        }
    });
}

} // namespace cr2
