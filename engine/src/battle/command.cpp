#include "battle/command.hpp"

#include <algorithm>
#include <utility>

#include "battle/awaketable.inc"

namespace cr2 {

void CommandInput::begin(const BattleRules& rules, bool eventBattle, std::function<bool(int)> flag,
                         std::function<int(int)> sopiaLevel) {
    rules_ = &rules, eventBattle_ = eventBattle, flag_ = std::move(flag), sopiaLevel_ = std::move(sopiaLevel);
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
        out.push_back({Options, true}); // (its window the viewer's: press() takes it as nothing here)
    } else if (menu_ == Menu::Command) {
        // CheckCommandEnable: Attack switch 43 (not state 22), Sopia 44 (not 2 sealed or 23), Items
        // 45 (not 25), Defend 46 (not 24); Awake / Advent shown when one can be (+0x3e / +0x3f)
        const Fighter& me = r.fighter(actor_);
        out.push_back({Attack, r.scriptSwitch(43) && !me.hasState(22)});
        out.push_back({Sopia, r.scriptSwitch(44) && !me.hasState(2) && !me.hasState(23)});
        out.push_back({Items, r.scriptSwitch(45) && !items().empty() && !me.hasState(25)});
        out.push_back({Defend, r.scriptSwitch(46) && !me.hasState(24)});
        // CheckCommand: with the charge less what this turn has set aside at 100 or more, a pair's
        // Advent when one can be called (CheckCommandAdvent), else Awake when one of the party has
        // an awakening it can pay for (CheckCommandAwake)
        if (r.charge() - reserved() >= 100) {
            if (pair_) {
                if (!advents().empty()) out.push_back({Advent, true});
            } else {
                bool awake = false;
                for (int k = 0; k < 2; ++k)
                    for (const Awakening& a : awakeningsOf(k)) awake |= a.selectable;
                if (awake) out.push_back({Awake, true});
            }
        }
    }
    return out;
}

std::vector<CommandInput::SopiaCommand> CommandInput::sopiaCommands() const {
    std::vector<SopiaCommand> out;
    if (!rules_) return out;
    const BattleRules& r = *rules_;
    std::vector<int> who = {actor_};
    if (pair_) who = {0, 1};
    for (int f : who) {
        const Fighter& fi = r.fighter(f);
        if (!fi.present) continue;
        for (int s : fi.sopia) {
            const BattleData::Sopia* so = r.data().sopia(s);
            const int level = so ? (sopiaLevel_ ? sopiaLevel_(s) : so->startLevel) : 0;
            if (!so || level <= 0) continue;
            for (int k = 0; k < level && k < 5; ++k) {
                const int id = so->commands[k];
                if (id <= 0 || std::any_of(out.begin(), out.end(), [id](const SopiaCommand& x) { return x.command == id; })) continue;
                const BattleData::Command* c = r.data().command(id);
                if (!c) continue;
                SopiaCommand sc;
                sc.command = id;
                sc.mp = r.useMp(pair_ ? 0 : actor_, pair_ ? 1 : -1, c->mp);
                sc.selectable = r.fighter(pair_ ? 0 : actor_).mp >= sc.mp && (!pair_ || r.fighter(1).mp >= sc.mp);
                sc.lacking = !sc.selectable;
                const bool partnerDown = !r.fighter(1 - actor_).alive && r.fighter(1 - actor_).present;
                if ((c->kind == 24 || c->kind == 25) && !pair_ && !partnerDown) sc.selectable = false;
                out.push_back(sc);
            }
        }
    }
    return out;
}

std::vector<CommandInput::ItemEntry> CommandInput::items() const {
    std::vector<ItemEntry> out;
    if (!rules_) return out;
    const BattleRules& r = *rules_;
    for (int id = 1; id < 300; ++id) {
        int n = r.itemCount(id);
        for (const BattleRules::Order& o : orders_) n -= o.item == id;
        const BattleData::Item* it = r.data().item(id);
        if (n <= 0 || !it || !it->battle) continue;
        const BattleData::Command* c = r.data().command(it->command);
        ItemEntry e;
        e.item = id, e.command = it->command, e.count = n;
        e.selectable = c != nullptr;
        const bool partnerDown = !r.fighter(1 - actor_).alive && r.fighter(1 - actor_).present;
        if (c && (c->kind == 24 || c->kind == 25) && !pair_ && !partnerDown) e.selectable = false;
        out.push_back(e);
    }
    return out;
}

bool CommandInput::pickItem(int item) {
    if (menu_ != Menu::Items) return false;
    for (const ItemEntry& e : items())
        if (e.item == item && e.selectable) {
            pendingItem_ = item;
            toTarget(e.command, 0);
            return true;
        }
    return false;
}

bool CommandInput::pickSopia(int command) {
    if (menu_ != Menu::Sopia) return false;
    for (const SopiaCommand& sc : sopiaCommands())
        if (sc.command == command && sc.selectable) {
            toTarget(command, 0);
            return true;
        }
    return false;
}

void CommandInput::toTarget(int command, int cost) {
    // StepTarget's list by the command's target (+0x4b): 4 / 7 a foe, 2 one of the party (the
    // revivals: one down), else none to choose (itself, the allies, the foes)
    const BattleData::Command* c = rules_->data().command(command);
    const int t = c ? c->target : 0;
    targets_.clear();
    if (t == 4 || t == 7) {
        for (int m = 2; m < 5; ++m)
            if (rules_->fighter(m).alive) targets_.push_back(m);
    } else if (t == 2) {
        const bool revive = c && (c->kind == 24 || c->kind == 25);
        for (int k = 0; k < 2; ++k)
            if (rules_->fighter(k).present && rules_->fighter(k).alive != revive) targets_.push_back(k);
    }
    if (targets_.empty()) {
        orders_.push_back({actor_, command, t == 1 ? actor_ : -1, std::exchange(pendingItem_, 0)});
        costs_.push_back(cost);
        next();
        return;
    }
    pending_ = command, pendingCost_ = cost, pendingFrom_ = menu_;
    menu_ = Menu::Target;
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
        a.lacking = !a.selectable;
        out.push_back(a);
    }
    return out;
}

std::vector<CommandInput::Awakening> CommandInput::awakenings() const { return awakeningsOf(actor_); }

bool CommandInput::pickAwakening(int command) {
    if (menu_ != Menu::Awake) return false;
    for (const Awakening& a : awakenings())
        if (a.command == command && a.selectable) {
            toTarget(command, a.cost); // (StepAwakeLoop: on to the target, step 0x10)
            return true;
        }
    return false;
}

std::vector<CommandInput::AdventEntry> CommandInput::advents() const {
    std::vector<AdventEntry> out;
    if (!rules_ || !pair_) return out;
    const BattleRules& r = *rules_;
    const int charge = r.charge() - reserved();
    for (const auto& e : kAdvent) {
        if (!(flag_ && flag_(e.flag)) || charge < e.charge) continue;
        bool worn = false; // (CheckCommandAdventAvailable: either one's sopia in its first two places)
        for (int k = 0; k < 2 && !worn; ++k) {
            const Fighter& f = r.fighter(k);
            for (int slot = 0; slot < 2 && f.present; ++slot) {
                const BattleData::Sopia* so = f.sopia[slot] > 0 ? r.data().sopia(f.sopia[slot]) : nullptr;
                worn = worn || (so && so->advent == e.sopia);
            }
        }
        if (!worn) continue;
        AdventEntry a;
        a.level = std::min(charge / 100, 3);
        a.command = e.command[a.level - 1];
        a.selectable = a.command > 0 && r.data().command(a.command) != nullptr;
        out.push_back(a);
    }
    return out;
}

bool CommandInput::pickAdvent(int command) {
    if (menu_ != Menu::Advent) return false;
    for (const AdventEntry& a : advents())
        if (a.command == command && a.selectable) {
            toTarget(command, rules_->charge() - reserved()); // (StepAdventLoop: all of it)
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
        if (id == Options) return false;
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
    if (id == Advent) { // (StepAdvent: the list)
        menu_ = Menu::Advent;
        return true;
    }
    if (id == Sopia) { // (StepSopia: the list)
        menu_ = Menu::Sopia;
        return true;
    }
    if (id == Items) { // (StepItem: the list)
        menu_ = Menu::Items;
        return true;
    }
    if (id == Defend) { // (StepNextCheck: state 4 for it - both, a pair)
        orders_.push_back({actor_, kDefend, actor_});
        costs_.push_back(0);
        next();
        return true;
    }
    // Attack: the character's +0x44 (a pair's swapped for +0x46 as it acts), on a foe
    toTarget(r.fighter(actor_).attack, 0);
    return true;
}

bool CommandInput::pick(int fighter) {
    if (menu_ != Menu::Target || std::find(targets_.begin(), targets_.end(), fighter) == targets_.end()) return false;
    orders_.push_back({actor_, pending_, fighter, std::exchange(pendingItem_, 0)});
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
    case Menu::Target: menu_ = pendingFrom_, pendingItem_ = 0; break;
    case Menu::Awake:
    case Menu::Advent:
    case Menu::Items:
    case Menu::Sopia: menu_ = Menu::Command; break;
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
