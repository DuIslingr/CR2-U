#pragma once
// Choosing the party's commands (BtlCommand; docs/BATTLE.md, "Choosing the commands"): the first
// menu (BattleFirstMenu: Solo, Pair, Escape, Options), then the command menu (BattleSoloMenu:
// Attack, Sopia, Items, Defend, Awake / Advent) - for each of the two in turn solo, once for the
// pair - and for a command on one, its target (BattleSelectMenu: the target buttons). Back goes up
// a step (from the second's command menu to the first's). Over, the turn's orders.
//
// What isn't here yet: Sopia, Items, Awake / Advent (their lists), Options - their
// buttons are there, not selectable.
#include <vector>
#include "battle/rules.hpp"
namespace cr2 {
class CommandInput {
public:
    // the round buttons' ids (circleBtn_fr's frames, the joints call_circleBtn_frNN)
    enum Button {
        Items = 16, Sopia = 18, Options = 20, Solo = 26, Pair = 27, Escape = 28, Awake = 29, Advent = 30, Attack = 31,
        Defend = 33, TargetFirst = 43, // (43..47: the target buttons)
    };
    enum class Menu { First, Command, Target, Done };
    struct Shown {
        int id = 0;
        bool selectable = true;
    };
    // a turn's choosing begins (BtlCommand::StepGetsetFirst / SetGetsetUI); `eventBattle` the
    // coupling's (+0x18: no escape)
    void begin(const BattleRules& rules, bool eventBattle);
    Menu menu() const { return menu_; }
    int actor() const { return actor_; }   // whose command is being chosen (0, 1; the pair's: 0)
    bool pairTurn() const { return pair_; }
    // the menu's buttons (First / Command)
    std::vector<Shown> buttons() const;
    // the targets offered (Target: the fighters, in the order of the buttons)
    const std::vector<int>& targets() const { return targets_; }
    // a button pressed (false: not there, or not selectable)
    bool press(int id);
    // a target picked (false: not one offered)
    bool pick(int fighter);
    void back();
    // the turn's orders (Done); escape(): Escape chosen in place of them (BtlCommand::StepEscape:
    // its yes / no asked - taken as yes here)
    const std::vector<BattleRules::Order>& orders() const { return orders_; }
    bool escape() const { return escape_; }
    static constexpr int kDefend = 5; // (the battle data's command 5: Defend, kind 2)
private:
    void commandMenu(int actor);
    void next(); // (the next one to choose for, or Done)
    const BattleRules* rules_ = nullptr;
    bool eventBattle_ = false;
    Menu menu_ = Menu::Done;
    int actor_ = 0;
    bool pair_ = false, escape_ = false;
    int pending_ = 0; // (the command waiting for its target)
    std::vector<int> targets_;
    std::vector<BattleRules::Order> orders_;
};
} // namespace cr2
