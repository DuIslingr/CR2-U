#pragma once
// A battle's rules (docs/BATTLE.md): the five fighters (BtlWork: 0, 1 the party, 2..4 the monsters)
// with what the battle reads of them, a turn's commands - the party's given, the monsters' their
// own choice (BtlEnemy) - played in their order (BtlCalc::OrderPoint), each command's hits,
// criticals and damage (BtlCalc), until one side is down. No drawing: what happened is a list of
// events, for the stage to show and for checks.
//
// What isn't here yet: the break and charge gauges and their bonuses, states other than guarding,
// sopia (magic) and items, awakening and advent, escape, the auto skills sopia give.

#include <cstdint>
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
    int turn() const { return turn_; }
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
        bool hit = false, critical = false, defeated = false;
        int damage = 0;
    };
    // One turn: the party solo (each its order) or a pair (the first order the pair's), the
    // monsters' own choices, all in their order. Returns the events.
    std::vector<Event> playTurn(bool pair, const std::vector<Order>& party);

    // the formulas, for checks
    int param(int f, int n) const;
    int damage(int actor, int target, const BattleData::Command& c, bool critical);
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
};

} // namespace cr2
