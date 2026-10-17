#include "battle/command.hpp"

#include <algorithm>

namespace cr2 {

void CommandInput::begin(const BattleRules& rules, bool eventBattle) {
    rules_ = &rules, eventBattle_ = eventBattle;
    menu_ = Menu::First, actor_ = 0, pair_ = false, escape_ = false, pending_ = 0;
    targets_.clear(), orders_.clear();
}

std::vector<CommandInput::Shown> CommandInput::buttons() const {
    std::vector<Shown> out;
    if (!rules_) return out;
    const BattleRules& r = *rules_;
    if (menu_ == Menu::First) {
        // SetGetsetUI: Pair with switch 47 and both standing (+0x44); Escape not in an event
        // battle (+0x1fb4); Options always
        const bool both = r.fighter(0).alive && r.fighter(1).alive;
        out.push_back({Solo, true});
        out.push_back({Pair, both && r.scriptSwitch(47)});
        out.push_back({Escape, !eventBattle_});
        out.push_back({Options, false});                 // (not yet)
    } else if (menu_ == Menu::Command) {
        // CheckCommandEnable: Attack switch 43, Sopia 44, Items 45, Defend 46 (and the states
        // that forbid them - none kept yet); Awake / Advent shown when one can be (+0x3e / +0x3f)
        out.push_back({Attack, r.scriptSwitch(43)});
        out.push_back({Sopia, false && r.scriptSwitch(44)}); // (not yet)
        out.push_back({Items, false && r.scriptSwitch(45)}); // (not yet)
        out.push_back({Defend, r.scriptSwitch(46)});
    }
    return out;
}

void CommandInput::commandMenu(int actor) {
    menu_ = Menu::Command, actor_ = actor;
}

bool CommandInput::press(int id) {
    const std::vector<Shown> b = buttons();
    const auto it = std::find_if(b.begin(), b.end(), [id](const Shown& s) { return s.id == id; });
    if (it == b.end() || !it->selectable) return false;
    const BattleRules& r = *rules_;
    if (menu_ == Menu::First) { // (StepGetsetLoop: 0 solo - the first standing chooses first; 1 pair; 2 escape)
        if (id == Escape) {
            escape_ = true, orders_.clear(), menu_ = Menu::Done;
            return true;
        }
        pair_ = id == Pair;
        orders_.clear();
        commandMenu(r.fighter(0).alive || pair_ ? 0 : 1);
        return true;
    }
    if (id == Defend) { // (StepNextCheck: state 4 for it - both, a pair)
        orders_.push_back({actor_, kDefend, actor_});
        next();
        return true;
    }
    // Attack: the character's +0x44 (a pair's swapped for +0x46 as it acts), on a foe
    pending_ = r.fighter(actor_).attack;
    targets_.clear();
    for (int m = 2; m < 5; ++m)
        if (r.fighter(m).alive) targets_.push_back(m);
    menu_ = Menu::Target;
    return true;
}

bool CommandInput::pick(int fighter) {
    if (menu_ != Menu::Target || std::find(targets_.begin(), targets_.end(), fighter) == targets_.end()) return false;
    orders_.push_back({actor_, pending_, fighter});
    next();
    return true;
}

void CommandInput::next() {
    // StepNextCheck: the second's turn to choose, solo and standing; else done
    if (!pair_ && actor_ == 0 && rules_->fighter(1).alive) return commandMenu(1);
    menu_ = Menu::Done;
}

void CommandInput::back() {
    switch (menu_) {
    case Menu::Target: menu_ = Menu::Command; break;
    case Menu::Command: // (StepCommandLoop: the second's back to the first's, else the first menu)
        if (!pair_ && actor_ == 1 && rules_->fighter(0).alive) {
            if (!orders_.empty()) orders_.pop_back();
            commandMenu(0);
        } else {
            orders_.clear();
            menu_ = Menu::First;
        }
        break;
    default: break;
    }
}

} // namespace cr2
