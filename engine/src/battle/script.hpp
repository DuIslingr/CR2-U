#pragma once
// A battle's script (BtlEvent; docs/BATTLE.md, "The battle's script"): `script\battle.nut` loaded as
// the battle begins and its Battle_Init called - which, for an encounter of the story's, names the
// encounter's own script (Cr3Battle.SetScript "battle_221"), loaded once Battle_Init is over. That
// one's functions are called at the battle's steps (BattleRules::script): Battle_Start after the
// opening, Turn_Start, Command_Start / Command_End around each action, Dead for each one downed,
// Turn_End, Battle_End. The game waits while one runs (BtlEvent::IsRunning), as the turn's steps
// here do. The scripts talk through cut-ins and messages (event/runtime.hpp's natives) and act on
// the battle through Cr3Battle: the fighters' HP, MP and parameters, the charge gauge, the turn's
// commands, the exit.
#include <memory>
#include <string>
#include "assets/mvgl.hpp"
#include "battle/rules.hpp"
#include "event/runtime.hpp"
#include "script/gamestate.hpp"
#include "script/vm.hpp"
namespace cr2 {
class BattleScript {
public:
    // `rules`: the battle started (BattleRules::start), its coupling `coupling`; `party`: the
    // characters fighting (from 0: cr3_common_data +1, +2; -1 none); `monsters` the monsters' ids
    BattleScript(const MvglArchive& archive, GameState& state, BattleRules& rules, int coupling, const int party[2],
                 const int monsters[3]);
    // battle.nut and Battle_Init (BtlMainCtrl::StepBattleIn), started; false: it failed (error())
    bool begin();
    // the script's function, started if it has it (up to its first wait)
    bool call(const std::string& function);
    // a tutorial (BtlEvent::LoadScript("battle_tuto"), CallScript: in an encounter without a script
    // of its own - battle.nut its script, the steps call nothing of it): battle_tuto loaded, its
    // function called (each its own story flag: shown once)
    bool tutorial(const std::string& function) { return (name_ == "battle_tuto" || load("battle_tuto")) && call(function); }
    // one of the game's updates (BtlEvent::Update): the call's wait and its windows, the exit's
    // timer; once a call ends, the script SetScript / JumpScript named loaded (and called)
    bool tick();
    bool running() const;
    // ticks (pages turning by themselves) until it is over, at most `maxUpdates`
    bool runToEnd(int maxUpdates = 20 * 60 * 5);
    // the encounter has a script of its own (+0x237a): the turn's steps call it
    bool own() const { return own_; }
    const std::string& name() const { return name_; }
    EventRuntime& events() { return *events_; }
    const std::string& error() const { return error_; }
    std::map<std::string, int> stubCalls() const;
    // the party's status windows shown (Invoke 1 / 2: BtlInterfaceCtrl::SetBattleStatusDisplay -
    // the scripts hide them while their talks are on)
    bool statusShown() const { return statusShown_; }
    // the calls made, in order ("Battle_Init", "Command_Start" ...: for checks)
    const std::vector<std::string>& calls() const { return calls_; }
private:
    bool load(const std::string& name);
    void bind();
    const MvglArchive& archive_;
    GameState& state_;
    BattleRules& rules_;
    int coupling_;
    int party_[2], monsters_[3];
    std::unique_ptr<ScriptVM> vm_;
    std::unique_ptr<EventRuntime> events_;
    std::string name_, error_;
    bool own_ = false;
    bool statusShown_ = true;
    std::string pendingScript_;          // (SetScript: loaded when the call is over)
    std::string jumpScript_, jumpCall_;  // (JumpScript: loaded and called then)
    std::vector<std::string> calls_;
    std::map<std::string, int> stubs_;   // (of the scripts replaced)
};
} // namespace cr2
