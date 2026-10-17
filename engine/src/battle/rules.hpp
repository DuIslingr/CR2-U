#pragma once
// A battle's rules (docs/BATTLE.md): the five fighters (BtlWork: 0, 1 the party, 2..4 the monsters)
// with what the battle reads of them, a turn's commands - the party's given, the monsters' their
// own choice (BtlEnemy) - played in their order (BtlCalc::OrderPoint), each command's hits,
// criticals and damage (BtlCalc), until one side is down. No drawing: what happened is a list of
// events, for the stage to show and for checks.
//
// The battle's own script (battle/script.hpp) runs at the turn's steps the game runs it at: a turn
// is beginTurn (Turn_Start: it may give the turn's commands), chooseTurn, then each action's
// startAction (Command_Start), doAction (Dead for each one downed), endAction (Command_End), and
// endTurn (Turn_End) - playTurn does them all, the script's calls finishing at once.
//
// The charge gauge (+0x28, 0..300) fills by the hits (BtlActionCtrl::UpdateChargeGauge). What isn't
// here yet: its use (awakening and advent), states other than guarding, sopia (magic) and items, escape, the auto skills sopia give.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "battle/battledata.hpp"

namespace cr2 {

// lrand48 (BtlRand: lrand48() % n)
class BattleRandom {
public:
    explicit BattleRandom(uint64_t seed = 0x1234abcd330eull) : x_(seed & 0xffffffffffffull) {}
    void seed48(uint32_t s) { x_ = (uint64_t(s) << 16 | 0x330e) & 0xffffffffffffull; } // (srand48)
    long next() {
        x_ = (0x5deece66dull * x_ + 0xb) & 0xffffffffffffull;
        return long(x_ >> 17);
    }
    int below(int n) { return n > 0 ? int(next() % n) : 0; }

private:
    uint64_t x_;
};

struct Fighter {
    bool present = false, alive = false;
    int side = 0;       // 0 the party, 1 the monsters
    int id = 0;         // its character's / monster's
    std::string name;
    int level = 1;
    int hp = 0, maxHp = 0, mp = 0, maxMp = 0;
    int param[5] = {0, 0, 0, 0, 0}; // Attack, Magic, Defense, Speed, the fifth
    int resist[7] = {0, 0, 0, 0, 0, 0, 0};
    int element = 0;
    unsigned skills[7] = {0, 0, 0, 0, 0, 0, 0}; // (BtlWork +0xfac..+0xfb8: groups -1..5)
    int partner = -1;   // (+0xb02: -1 solo)
    bool guarding = false; // (state 4: this turn)
    int attack = 0, pairAttack = 0; // a character's commands
    const BattleData::Monster* monster = nullptr;
    bool has(int group, int bit) const { return group >= 0 && group <= 5 && (skills[group + 1] >> bit & 1u) != 0; }
};

class BattleRules {
public:
    BattleRules(const BattleData& data, uint32_t seed = 0);

    // a new game's party in `coupling` (the characters at their first levels and equipment);
    // `dungeonLevel` the monsters' band (cr3_common_data[0])
    bool start(int coupling, const int party[2], int dungeonLevel = 0, std::string* error = nullptr);

    const Fighter& fighter(int i) const { return fighters_[size_t(i)]; }
    const BattleData& data() const { return data_; }
    int turn() const { return turn_; }
    // The break gauge (BtlWork +0x1e, -4 .. 4: the party's way up): a hit on a weakness, a
    // critical, a fighter downed move it a point each (BtlActionCtrl::UpdateBreakGauge; none on a
    // guarding target); at 4 or -4 the next turn is that side's turn break (+0x1f: 1 the party's,
    // 2 the monsters', 3 one coming), +100 to its power and half the damage to it, then even again
    int gauge() const { return gauge_; }
    int breakState() const { return break_; }
    bool breakBonus(int f) const { return (break_ == 1 && f < 2) || (break_ == 2 && f >= 2); }
    int outcome() const; // 0 on, 1 won, 2 lost

    // what a fighter does this turn
    struct Order {
        int actor = -1;
        int command = 0;
        int target = -1; // (a single target's fighter)
    };
    // what happened: a command's result on one fighter
    struct Event {
        int actor = -1, partner = -1, command = 0, target = -1;
        bool hit = false, critical = false, weak = false, defeated = false;
        int damage = 0;
    };
    // One turn: the party solo (each its order) or a pair (the first order the pair's), the
    // monsters' own choices, all in their order. Returns the events.
    std::vector<Event> playTurn(bool pair, const std::vector<Order>& party);
    // ... step by step (BtlCommand / BtlActionCtrl), the script's calls between
    void beginTurn();
    void chooseTurn(bool pair, const std::vector<Order>& party);
    bool actionsLeft() const { return !exited() && !escaped_ && !outcome() && next_ < turnOrder_.size(); }
    const Order& nextAction() const { return turnOrder_[next_]; }
    void startAction();
    void doAction(std::vector<Event>& out);
    void endAction();
    void endTurn();

    // What the battle's script reads and sets (Cr3Battle: BtlWork)
    // SetParamI / GetParamI's ids: 10 + 7k (k the fighter: 0, 1 the party) HP (-1: its maximum), 11 + 7k
    // MP (0x8000: the maximum), 12 + 7k .. 16 + 7k the parameters, 48 + 2k / 49 + 2k the maximum HP /
    // MP; the monsters 24 + 6m HP, 25 + 6m .. 29 + 6m the parameters, 52 + m the maximum HP; 5..9
    // values of its own (+0x2360..), 42..47 switches (+0x2374..: 42 the awakening's, 43..46 which
    // commands are offered, 47 the set-up's interface), 58 the exit's timer (n / 20 s); GetParamI
    // 55 the break gauge, 56 / 57 the party's / the monsters' turn break
    int paramI(int id) const;
    void setParamI(int id, int value);
    int charge() const { return charge_; }       // +0x28: the charge gauge, 0..300
    void addCharge(int n) { charge_ = std::clamp(charge_ + n, 0, 300); } // (Cr3BattleEvent_Charge: at most 300)
    bool scriptSwitch(int n) const { return n >= 42 && n <= 47 && switches_[n - 42]; }
    int scriptCommandActor() const { return commandActor_; }   // +0x235c
    int commandOf(int f) const { return f >= 0 && f < 5 ? current_[f] : 0; } // (+0x54c: the fighter's command this turn)
    // the turn the script gives (ReserveCommand; SetCommand / SetCommandTarget / SetPair): in place of the orders
    void setScriptCommand(int f, int command) { if (f >= 0 && f < 5) scripted_.command[f] = command; }
    void setScriptTarget(int f, int target) { if (f >= 0 && f < 5) scripted_.target[f] = target; }
    void setScriptPair(int f) { if (f >= 0 && f < 2) scripted_.pair = true; }
    void reserveScriptTurn() { scripted_.reserved = true; }
    bool scriptTurnReserved() const { return scripted_.reserved; } // (BtlCommand::StepEvent: no menus then)
    // Invoke(0): every fighter standing rid of its states (BtlStateManager::Clear)
    void clearStates();
    // BtlCalc::Escape for the party: sure in its turn break or with skill (4, 3) - its own, or
    // either standing's; else the party's mean level (+0x24) less the monsters' levels added up
    // (+0x26) against -50 / -1 / 49: 40, 50, 60, 70 in 100. A success ends the battle (escaped()),
    // a failure leaves the party's turn to the monsters
    bool tryEscape();
    bool escaped() const { return escaped_; }
    // Cr3Battle.Exit: the battle over, on as the game goes (BtlMain::NextMode 0: no game over);
    // exitTimer() the time a timer gives it first (-1: none)
    void exitBattle() { exited_ = true; }
    bool exited() const { return exited_; }
    float exitTimer() const { return exitTimer_; }
    void clearExitTimer() { exitTimer_ = -1; }
    // the timer counted down (BtlEvent::Update: by the frame's time); at 0 the exit
    void runExitTimer(float dt) {
        if (exitTimer_ < 0) return;
        exitTimer_ -= dt;
        if (exitTimer_ <= 0) exitTimer_ = -1, exited_ = true;
    }
    // the battle's own script, called at the steps (the game: only an encounter with a script of
    // its own, +0x237a, has Turn_Start ... Dead)
    std::function<void(const std::string& function)> script;

    // the formulas, for checks
    int param(int f, int n) const;
    int damage(int actor, int target, const BattleData::Command& c, bool critical, bool* weak = nullptr);
    bool hit(int actor, int target, const BattleData::Command& c);
    bool critical(int actor, int target, const BattleData::Command& c);

private:
    bool isPairAction(int f) const;
    Order enemyChoice(int m);
    int orderPoint(int f, int partner) const;
    void act(const Order& o, std::vector<Event>& out);
    int liveTarget(int side, int wanted);
    const BattleData& data_;
    BattleRandom rand_;
    Fighter fighters_[5];
    int turn_ = 0;
    // the turn being played
    struct Turn {
        Order o;
        int point;
    };
    std::vector<Order> turnOrder_;
    size_t next_ = 0;
    int current_[5] = {0, 0, 0, 0, 0};
    std::vector<int> downed_; // (this action's: Dead for each)
    // the script's
    int charge_ = 0, ints_[5] = {0, 0, 0, 0, 0}, commandActor_ = -1;
    bool switches_[6] = {false, true, true, true, true, true}; // (BtlWork::InitializeVariables: 42 off, 43..47 on)
    struct Scripted {
        bool reserved = false, pair = false;
        int command[5] = {-1, -1, -1, -1, -1}, target[5] = {-1, -1, -1, -1, -1};
    } scripted_;
    bool exited_ = false, escaped_ = false;
    float exitTimer_ = -1;
    void callScript(const char* f) { if (script) script(f); }
    int gauge_ = 0, break_ = 0;
    void moveGauge(const Event& e, const BattleData::Command& c);
    void chargeFor(const Event& e, const BattleData::Command& c, bool first);
};

} // namespace cr2
