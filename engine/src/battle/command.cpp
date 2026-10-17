#include "battle/command.hpp"

#include <algorithm>

#include "battle/awaketable.inc"

namespace cr2 {

void CommandInput::begin(const BattleRules& rules, bool eventBattle, std::function<bool(int)> flag) {
    rules_ = &rules, eventBattle_ = eventBattle, flag_ = std::move(flag);
    costs_.clear();
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
        // CheckCommandAwake: Awake when one of the party has an awakening it can pay for
        bool awake = false;
        for (int k = 0; k < 2; ++k)
            for (const Awakening& a : awakeningsOf(k)) awake |= a.selectable;
        if (awake) out.push_back({Awake, true});
    }
    return out;
}

int CommandInput::awakeCost(int level) { return level >= 1 && level <= 3 ? kAwakeCost[level - 1] : 100; }

int CommandInput::reserved() const {
    int n = 0;
    for (int c : costs_) n += c;
    return n;
}

std::vector<CommandInput::Awakening> CommandInput::awakeningsOf(int fighter) const {
    std::vector<Awakening> out;
    if (!rules_) return out;
    const Fighter& f = rules_->fighter(fighter);
    const int character = f.id - 1; // (cr3_common_data's, from 0)
    if (!f.present || !f.alive || character < 0 || character >= int(std::size(kAwake))) return out;
    for (const auto& e : kAwake[character]) {
        if (e.command < 0 || (e.flag != 9999 && !(flag_ && flag_(e.flag)))) continue;
        const BattleData::Command* c = rules_->data().command(e.command);
        Awakening a;
        a.command = e.command, a.level = c ? c->awaken : 1, a.cost = awakeCost(a.level);
        a.selectable = a.cost + reserved() <= rules_->charge();
        out.push_back(a);
    }
    return out;
}

std::vector<CommandInput::Awakening> CommandInput::awakenings() const { return awakeningsOf(actor_); }

bool CommandInput::pickAwakening(int command) {
    if (menu_ != Menu::Awake) return false;
    for (const Awakening& a : awakenings())
        if (a.command == command && a.selectable) {
            const BattleData::Command* c = rules_->data().command(command);
            if (c && (c->target == 4 || c->target == 7)) { // (StepAwakeLoop: on to the target, step 0x10)
                pending_ = command, pendingCost_ = a.cost;
                targets_.clear();
                for (int m = 2; m < 5; ++m)
                    if (rules_->fighter(m).alive) targets_.push_back(m);
                menu_ = Menu::Target;
                return true;
            }
            orders_.push_back({actor_, command, -1}); // (all the foes, or the allies)
            costs_.push_back(a.cost);
            next();
            return true;
        }
    return false;
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
    if (id == Awake) {
        menu_ = Menu::Awake;
        return true;
    }
    if (id == Defend) { // (StepNextCheck: state 4 for it - both, a pair)
        orders_.push_back({actor_, kDefend, actor_});
        costs_.push_back(0);
        next();
        return true;
    }
    // Attack: the character's +0x44 (a pair's swapped for +0x46 as it acts), on a foe
    pending_ = r.fighter(actor_).attack, pendingCost_ = 0;
    targets_.clear();
    for (int m = 2; m < 5; ++m)
        if (r.fighter(m).alive) targets_.push_back(m);
    menu_ = Menu::Target;
    return true;
}

bool CommandInput::pick(int fighter) {
    if (menu_ != Menu::Target || std::find(targets_.begin(), targets_.end(), fighter) == targets_.end()) return false;
    orders_.push_back({actor_, pending_, fighter});
    costs_.push_back(pendingCost_);
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
    case Menu::Target: menu_ = pendingCost_ ? Menu::Awake : Menu::Command; break;
    case Menu::Awake: menu_ = Menu::Command; break;
    case Menu::Command: // (StepCommandLoop: the second's back to the first's, else the first menu)
        if (!pair_ && actor_ == 1 && rules_->fighter(0).alive) {
            if (!orders_.empty()) orders_.pop_back();
            if (!costs_.empty()) costs_.pop_back();
            commandMenu(0);
        } else {
            orders_.clear(), costs_.clear();
            menu_ = Menu::First;
        }
        break;
    default: break;
    }
}

} // namespace cr2
