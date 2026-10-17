#include "battle/rules.hpp"

#include <algorithm>
#include <cmath>

namespace cr2 {

namespace {

// BtlCalc::DamageResist: a fighter's resistance value (1 weak, 2 resists) as a factor
float resistFactor(int v) { return v == 1 ? 1.25f : v == 2 ? 0.75f : 1.0f; }

// BtlCalc::CheckResult's steps (Hit, Critical): the first threshold the difference doesn't pass
int step(int diff) {
    static const int kThresholds[5] = {-101, -50, -1, 49, 100};
    int i = 0;
    while (i < 5 && kThresholds[i] < diff) ++i;
    return i;
}

} // namespace

BattleRules::BattleRules(const BattleData& data, uint32_t seed) : data_(data) {
    if (seed) rand_.seed48(seed);
}

bool BattleRules::start(int coupling, const int party[2], int dungeonLevel, std::string* error, const signed char* sopiaLevel, const short (*equip)[8], const int* level, const int* mp) {
    BattleSetup setup;
    if (!setupBattle(data_, coupling, party, setup, error)) return false;
    for (Fighter& f : fighters_) f = Fighter();
    turn_ = 0;
    // the party (Cr3InitWorkFromBattleData, Cr3UpdatePlayerData): the level's HP, MP and
    // parameters, plus what the weapon, armour and accessory add and the auto skills they give
    int levels = 0, count = 0;
    for (int k = 0; k < 2; ++k) {
        if (party[k] <= 0) continue;
        const BattleData::Character* c = data_.character(party[k]);
        if (!c) continue;
        int eq[8];
        for (int e = 0; e < 8; ++e) eq[e] = equip && c->id >= 1 && c->id <= 9 ? equip[c->id - 1][e] : c->equip[e];
        Fighter& f = fighters_[size_t(k)];
        f.present = f.alive = true;
        f.side = 0, f.id = c->id, f.name = c->name[1], f.level = level && c->id >= 1 && c->id <= 9 && level[c->id - 1] > 0 ? level[c->id - 1] : c->level;
        f.maxHp = std::clamp(c->hp + c->hpGrowth * f.level * 50 / 100, 1, 99999);
        f.maxMp = std::clamp(c->mp + c->mpGrowth * f.level * 5 / 100, 0, 9999);
        for (int n = 0; n < 5; ++n) f.param[n] = std::clamp(c->param[n] + c->paramGrowth[n] * f.level * 5 / 100, 0, 9999);
        for (int e = 0; e < 3; ++e) {
            const BattleData::Item* it = data_.item(eq[e]);
            if (!it) continue;
            f.maxHp += it->hp, f.maxMp += it->mp;
            for (int n = 0; n < 5; ++n) f.param[n] += it->param[n];
            for (int s : it->skills)
                if (const BattleData::AutoSkill* a = data_.autoSkill(s)) {
                    for (int g = 0; g < 7; ++g) f.skills[g] |= a->groups[g];
                    for (int r = 0; r < 7; ++r) f.resist[r] = std::max(f.resist[r], a->resist[r]);
                    for (int r = 0; r < 5; ++r) f.stateResist[r] = std::max(f.stateResist[r], a->stateResist[r]);
                }
        }
        f.hp = f.maxHp, f.mp = f.maxMp;
        if (mp && c->id >= 1 && c->id <= 9 && mp[c->id - 1] >= 0) f.mp = std::min(mp[c->id - 1], f.maxMp); // (cr3_player_data +0x10)
        f.attack = c->attack, f.pairAttack = c->pairAttack;
        for (int k = 0; k < 5; ++k) f.sopia[k] = eq[3 + k];
        // BtlWork::SetupPlayerSkill: the element of the first sopia with one, and each sopia's auto
        // skills up to its level - the element they lend in its place, if they agree on one
        for (int s : f.sopia)
            if (const BattleData::Sopia* so = data_.sopia(s); so && so->element) {
                f.element = so->element;
                break;
            }
        int attach = 0;
        bool agree = true;
        for (int s : f.sopia) {
            const BattleData::Sopia* so = data_.sopia(s);
            if (!so) continue;
            const int level = sopiaLevel && s >= 0 && s < 32 ? sopiaLevel[s] : so->startLevel;
            for (int k = 0; k < level && k < 5; ++k)
                if (const BattleData::AutoSkill* a = data_.autoSkill(so->skills[k])) {
                    for (int g = 0; g < 7; ++g) f.skills[g] |= a->groups[g];
                    for (int r = 0; r < 7; ++r) f.resist[r] = std::max(f.resist[r], a->resist[r]);
                    for (int r = 0; r < 5; ++r) f.stateResist[r] = std::max(f.stateResist[r], a->stateResist[r]);
                    if (agree && a->attach) {
                        if (!attach) attach = a->attach;
                        else if (attach != a->attach) agree = false, attach = 0;
                    }
                }
        }
        if (attach) f.element = attach;
        levels += f.level, ++count;
    }
    // BtlWork::CheckChargeSkill: the charge at 100 when the first standing has the skill (5, 4)
    charge_ = 0;
    exp_ = sp_ = oz_ = 0;
    std::fill(std::begin(drops_), std::end(drops_), short(0)), std::fill(std::begin(sopiaDrops_), std::end(sopiaDrops_), false); // (InitializeDrop)
    for (int k = 0; k < 2; ++k)
        if (fighters_[size_t(k)].alive) {
            if (fighters_[size_t(k)].has(5, 4)) charge_ = 100;
            break;
        }
    const int average = count ? levels / count : 1;
    // the monsters (BtlWork::SetupEnemy): their own level, else near the party's within the
    // dungeon level's band
    const int lo0 = dungeonLevel == 10 ? 101 : dungeonLevel * 10 + 1, hi0 = dungeonLevel == 10 ? 200 : (dungeonLevel + 1) * 10;
    const BattleData::Coupling* cp = data_.coupling(coupling);
    for (size_t k = 0; k < setup.monsters.size() && k < 3; ++k) {
        const BattleData::Monster* m = data_.monster(setup.monsters[k].id);
        Fighter& f = fighters_[2 + k];
        f.present = f.alive = true;
        f.side = 1, f.id = m->id, f.name = m->name[1], f.monster = m;
        int level = m->level;
        if (level == 0) {
            const int avg = std::clamp(average, 1, 200);
            int lo = lo0, hi = hi0;
            if (avg < lo) level = lo;
            else if (avg >= hi) level = hi;
            else {
                if (lo < avg - 4) lo = avg - 5;
                if (avg + 1 < hi) hi = avg + 2;
                level = lo + rand_.below(hi - lo);
            }
        }
        f.level = level;
        static const int kGrowth[5] = {7, 6, 4, 6, 6}; // (0x535d90; 0x535d84 the same)
        const bool own = cp && cp->flag; // (the coupling's flag: no limits)
        f.maxHp = m->hp + level * 25;
        if (!own) f.maxHp = std::clamp(f.maxHp, 1, 99999);
        for (int n = 0; n < 5; ++n) {
            f.param[n] = m->param[n] + level * kGrowth[n];
            if (!own) f.param[n] = std::clamp(f.param[n], 0, 9999);
        }
        f.hp = f.maxHp;
        for (int r = 0; r < 7; ++r) f.resist[r] = m->resist[r];
        for (int r = 0; r < 5; ++r) f.stateResist[r] = m->stateResist[r];
        f.element = m->element;
        // SetupEnemySkill: its flags as skill bits
        const uint32_t s = m->skills;
        const struct {
            uint32_t flag;
            int group, bit;
        } kMap[] = {{1, 5, 9},     {2, 5, 2},     {4, 1, 3},     {8, 1, 4},     {0x10, 1, 0},    {0x20, 1, 1},  {0x40, 1, 2},
                    {0x80, 1, 5},  {0x100, 2, 1}, {0x200, 2, 4}, {0x400, 5, 6}, {0x800, 5, 10}, {0x1000, 5, 11}};
        for (const auto& e : kMap)
            if (s & e.flag) f.skills[e.group + 1] |= 1u << e.bit;
    }
    return true;
}

int BattleRules::outcome() const {
    bool party = false, monsters = false;
    for (const Fighter& f : fighters_)
        if (f.present && f.alive) (f.side == 0 ? party : monsters) = true;
    return !party ? 2 : !monsters ? 1 : 0;
}

// BtlUtilStatus::GetCharaParam: raised (state 5 + n) x 1.25, lowered (13 + n) x 0.75 - a use each
int BattleRules::param(int f, int n) const {
    const Fighter& x = fighters_[size_t(f)];
    const int v = x.param[n];
    if (n < 0 || n > 4) return v;
    if (x.hasState(5 + n)) return x.useState(5 + n), v * 125 / 100;
    if (x.hasState(13 + n)) return x.useState(13 + n), v * 75 / 100;
    return v;
}

bool BattleRules::isPairAction(int f) const {
    const Fighter& a = fighters_[size_t(f)];
    return a.alive && a.partner >= 0 && fighters_[size_t(a.partner)].alive;
}

bool BattleRules::hit(int actor, int target, const BattleData::Command& c) {
    if (c.magic == 1 && !fighters_[size_t(target)].has(1, 5)) return true; // (a sopia sure but on one dodging them, skill 1, 5)
    if (c.specials & 1) return true;
    int speed = param(actor, 3);
    if (isPairAction(actor)) speed += param(fighters_[size_t(actor)].partner, 3);
    static const int kChance[6] = {60, 70, 80, 90, 95, 99};
    int chance = kChance[step(speed - param(target, 4))];
    if (c.kind == 15) chance = 75;
    // blind (state 1, the actor's or its partner's in a pair: CheckCharaStatePair) halves it
    const Fighter& a = fighters_[size_t(actor)];
    const bool pairBlind = isPairAction(actor) && fighters_[size_t(a.partner)].hasState(1);
    if (a.hasState(1) || pairBlind) {
        a.useState(1);
        if (isPairAction(actor)) fighters_[size_t(a.partner)].useState(1);
        chance /= 2;
    }
    return rand_.below(100) < chance;
}

bool BattleRules::critical(int actor, int target, const BattleData::Command& c) {
    const Fighter& a = fighters_[size_t(actor)];
    auto hasPair = [&](int group, int bit) {
        return a.has(group, bit) || (isPairAction(actor) && fighters_[size_t(a.partner)].has(group, bit));
    };
    if (c.magic == 1 && !hasPair(2, 4)) return false;
    if (!(c.specials >> 8 & 1)) return false;
    if (c.specials >> 9 & 1) return true;
    // state 32 (the actor's, or its partner's in a pair): sure, a use
    if (a.hasState(32)) return a.useState(32), true;
    if (isPairAction(actor) && fighters_[size_t(a.partner)].hasState(32)) return fighters_[size_t(a.partner)].useState(32), true;
    auto luck = [&](int f) {
        const Fighter& x = fighters_[size_t(f)];
        return isPairAction(f) ? (x.param[4] + fighters_[size_t(x.partner)].param[4]) / 2 : x.param[4];
    };
    static const int kChance[6] = {5, 10, 15, 20, 25, 50};
    int chance = kChance[step(luck(actor) - luck(target))] * 100 >> 8;
    if (c.kind == 4 || c.kind == 15) chance = 100;
    if (hasPair(2, 2)) chance = chance < 91 ? chance + 10 : 100;
    return rand_.below(100) < chance;
}

int BattleRules::damage(int actor, int target, const BattleData::Command& c, bool crit, bool* weak) {
    if (weak) *weak = false;
    const Fighter& a = fighters_[size_t(actor)];
    const Fighter& t = fighters_[size_t(target)];
    const bool pair = isPairAction(actor);
    auto hasPair = [&](int group, int bit) { return a.has(group, bit) || (pair && fighters_[size_t(a.partner)].has(group, bit)); };
    const int y = commandY(actor, crit);
    // Damage1
    float d;
    if (c.kind == 7) {
        d = float(t.maxHp - t.hp) * float(c.power + y) / 100.0f - float(param(target, 1) * 3 / 2);
    } else {
        int atk = param(actor, c.magic ? 1 : 0);
        if (pair) atk += param(a.partner, c.magic ? 1 : 0);
        int def = c.magic ? param(target, 1) * 2 / 3 : param(target, 2);
        if (crit && hasPair(2, 0)) def = 0;
        if (c.specials & 2) def = 0;
        d = float(atk) * float(c.power + y) / 100.0f - float(def);
    }
    // Damage2: the elements (DamageAttribute)
    float m = 1;
    int elem = c.element;
    if (elem == 0 && (c.specials >> 5 & 1)) elem = a.element;
    if (elem >= 1 && elem <= 3 && hasPair(0, elem - 1)) m = 1.25f;
    if (c.magic == 0 && hasPair(0, 5)) m *= 1.25f;
    if (c.magic == 1 && hasPair(0, 6)) m *= 1.25f;
    if (elem == 4 && hasPair(0, 3)) m *= 1.25f;
    if (elem == 5 && hasPair(0, 4)) m *= 1.25f;
    static const int kBeats[4] = {0, 3, 1, 2}, kLoses[4] = {0, 2, 3, 1}; // (the cycle: 1 on 3, 2 on 1, 3 on 2)
    if (elem >= 1 && elem <= 3) {
        if (t.element == kLoses[elem]) m *= 0.75f;
        else if (t.element == kBeats[elem]) {
            m *= 1.5f;
            if (weak) *weak = true; // (+0xf7d)
        }
    }
    if (elem >= 1 && elem <= 5) m *= resistFactor(t.resist[elem - 1]);
    if (c.magic == 0) m *= resistFactor(t.resist[5]);
    if (c.magic == 1) m *= resistFactor(t.resist[6]);
    d *= m;
    // Damage3
    if (c.kind == 5) return int(std::ceil(float(c.power * t.hp) / 100.0f));
    if (c.kind == 12) return t.hp - 1;
    if (d < 0) d = 0;
    if (c.spread) d = d * float((100 - c.spread) + rand_.below(c.spread * 2 + 1)) / 100.0f;
    if (t.guarding || t.hasState(4)) d *= 0.5f;
    if (breakBonus(target)) d *= 0.5f;
    if (!pair && t.has(5, 9) && !a.has(5, 0) && !(c.specials >> 2 & 1)) d *= 0.1f; // (armour)
    if (hasPair(0, 10) && c.mp > 0) d *= 1.5f; // (the power boost)
    // state 35, physical strength doubled (CheckCharaStatePair): a physical command's damage
    // twice, a use of it - the partner's too in a pair
    if (c.magic == 0 && (a.hasState(35) || (pair && fighters_[size_t(a.partner)].hasState(35)))) {
        a.useState(35);
        if (pair) fighters_[size_t(a.partner)].useState(35);
        d += d;
    }
    return int(std::ceil(d));
}

int BattleRules::commandY(int actor, bool crit) const {
    const Fighter& a = fighters_[size_t(actor)];
    const bool pair = isPairAction(actor);
    auto hasPair = [&](int group, int bit) { return a.has(group, bit) || (pair && fighters_[size_t(a.partner)].has(group, bit)); };
    int y = 0;
    if (breakBonus(actor)) y += 100;
    if (pair) y += 25;
    if (actor > 1) y += 50;
    if (crit) y += hasPair(2, 3) ? 100 : 50;
    return y;
}

void BattleRules::restore(int actor, int target, const BattleData::Command& c, Event& e) {
    Fighter& a = fighters_[size_t(actor)];
    Fighter& t = fighters_[size_t(target)];
    const bool pair = isPairAction(actor);
    auto hasPair = [&](int group, int bit) { return a.has(group, bit) || (pair && fighters_[size_t(a.partner)].has(group, bit)); };
    int magic = param(actor, 1);
    if (pair) magic += param(a.partner, 1);
    auto formula = [&] {
        float v = float(magic) * float(c.power + commandY(actor, false)) / 100.0f;
        if (c.spread) v = v * float((100 - c.spread) + rand_.below(c.spread * 2 + 1)) / 100.0f; // (CommandSwing)
        return int(std::ceil(v));
    };
    int hp = -1, mp = -1;
    bool doubles = true;
    switch (c.kind) {
    case 18: hp = formula(); break;
    case 19: mp = formula(); break;
    case 20: hp = c.power; break;
    case 21: mp = c.power; break;
    case 22: case 25: hp = t.maxHp, doubles = false; break;
    case 23: hp = t.maxHp, mp = t.maxMp, doubles = false; break;
    case 24: {
        const int y = commandY(actor, false);
        const int p = y < -49 ? 1 : std::min(y + 50, 100);
        hp = p * t.maxHp / 100;
        break;
    }
    default: return;
    }
    if (c.mp > 0 && hasPair(0, 10)) { // (the power boost)
        if (hp >= 0) hp += hp * 50 / 100;
        if (mp >= 0) mp += mp * 50 / 100;
    }
    if (doubles && hasPair(0, 9)) {
        if (hp >= 0) hp *= 2;
        if (mp >= 0) mp *= 2;
    }
    const bool revive = c.kind == 24 || c.kind == 25;
    if (revive && !t.alive && t.present) t.alive = true, t.hp = 0, e.revived = true;
    if (!t.alive) return;
    if (hp > 0) e.heal = std::min(hp, t.maxHp - t.hp), t.hp += e.heal;
    if (mp > 0) e.healMp = std::min(mp, t.maxMp - t.mp), t.mp += e.healMp;
    e.hit = true;
}

int BattleRules::orderPoint(int f, int partner) const {
    int p = param(f, 3);
    if (partner >= 0) p = (p + param(partner, 3)) / 2;
    if (turn_ == 0 && (fighters_[size_t(f)].has(5, 2) || (partner >= 0 && fighters_[size_t(partner)].has(5, 2)))) p += 5000;
    return p;
}

int BattleRules::liveTarget(int side, int wanted) {
    if (wanted >= 0 && wanted < 5 && fighters_[size_t(wanted)].alive && fighters_[size_t(wanted)].side == side) return wanted;
    for (int i = side == 0 ? 0 : 2; i < (side == 0 ? 2 : 5); ++i)
        if (fighters_[size_t(i)].alive) return i;
    return -1;
}

// BtlEnemy::SetActionCommand / SetCommandTarget
BattleRules::Order BattleRules::enemyChoice(int mi) {
    Order o;
    o.actor = mi;
    const Fighter& f = fighters_[size_t(mi)];
    const BattleData::Monster* m = f.monster;
    if (!m) return o;
    bool ok[10] = {};
    int chosen = -1;
    for (int k = 0; k < 10; ++k) {
        const BattleData::Command* c = data_.command(m->commands[k]);
        if (!c || c->kind == 4 || (c->specials & 0x80)) continue;
        bool cond = false;
        switch (m->condition[k]) {
        case 0: cond = true; break;
        case 11: cond = turn_ + 1 == m->conditionValue[k]; break;
        default: cond = false; break; // (Cond1..16: not read yet)
        }
        ok[k] = cond;
        if (cond && m->must[k] == 1) chosen = k;
    }
    if (chosen < 0) {
        int sum = 0;
        for (int k = 0; k < 10; ++k)
            if (ok[k]) sum += m->weight[k];
        if (sum > 0) {
            const int r = rand_.below(sum);
            int acc = 0;
            for (int k = 0; k < 10 && chosen < 0; ++k)
                if (ok[k] && r < (acc += m->weight[k])) chosen = k;
        }
    }
    if (chosen < 0) return o;
    o.command = m->commands[chosen];
    const BattleData::Command* c = data_.command(o.command);
    int partyCount = 0;
    for (int i = 0; i < 2; ++i) partyCount += fighters_[size_t(i)].present;
    if (c->target == 4 || c->target == 7) o.target = m->targetRule[chosen] == 1 ? mi : rand_.below(std::max(partyCount, 1)); // (Target0 / Target1)
    // CheckProvoke: a single target of the party - the one of the two standing provoking (state
    // 30), if only one is; a pair struck as one
    if (o.target >= 0 && o.target < 2 && fighters_[size_t(o.target)].partner < 0) {
        const bool p0 = fighters_[0].alive && fighters_[0].hasState(30), p1 = fighters_[1].alive && fighters_[1].hasState(30);
        if (p0 != p1) o.target = p0 ? 0 : 1;
    }
    if (c->kind == 2) fighters_[size_t(mi)].guarding = true; // (SetCommand: kind 2 guards at once)
    return o;
}

void BattleRules::act(const Order& o, std::vector<Event>& out) {
    Fighter& a = fighters_[size_t(o.actor)];
    if (!a.alive) return;
    if (a.canceled || a.hasState(21)) return; // (its action cancelled this turn; time stopped)
    const BattleData::Command* c = data_.command(o.command);
    if (!c) return;
    // an action that can't be done (SetActionTable, then ActionDontActionAnnounce): its kind
    // sealed - an attack by state 22, a sopia by 2 or 23, an item by 25 (none on an awakening) -,
    // or its MP short (below), on the banner
    auto fail = [&](int note) {
        Event e;
        e.actor = o.actor, e.command = o.command, e.target = o.actor, e.failed = true;
        e.notes.push_back({note, o.actor});
        out.push_back(e);
    };
    if (c->kind != 2 && c->awaken == 0) {
        const bool attack = o.actor < 2 ? o.command == a.attack || o.command == a.pairAttack : c->magic == 0;
        if (o.item > 0 && a.hasState(25)) return fail(0x55);
        if (o.item <= 0 && attack && a.hasState(22)) return fail(0x53);
        if (o.item <= 0 && !attack && (a.hasState(2) || a.hasState(23))) return fail(0x48);
    }
    if (c->kind == 2) { // defend
        a.guarding = true;
        Event e;
        e.actor = o.actor, e.command = o.command, e.target = o.actor;
        out.push_back(e);
        return;
    }
    const int foes = 1 - a.side;
    std::vector<int> targets;
    if (c->target == 6) {
        for (int i = 0; i < 5; ++i)
            if (fighters_[size_t(i)].alive && fighters_[size_t(i)].side == foes) targets.push_back(i);
    } else if (c->target == 4 || c->target == 7) {
        const int t = liveTarget(foes, o.target);
        if (t >= 0) {
            targets.push_back(t);
            // a pair struck: both (CalcCommandAction: the target and its partner)
            const int p = fighters_[size_t(t)].partner;
            if (p >= 0 && fighters_[size_t(p)].alive) targets.push_back(p);
        }
    } else if (c->target == 1) {
        targets.push_back(o.actor);
    } else if (c->target == 2 || c->target == 3) { // (an ally, the allies: the revivals' those down)
        const bool revive = c->kind == 24 || c->kind == 25;
        for (int i = 0; i < 5; ++i) {
            const Fighter& f = fighters_[size_t(i)];
            if (!f.present || f.side != a.side || f.alive == revive) continue;
            if (c->target == 2 && i != o.target) continue;
            targets.push_back(i);
        }
        if (targets.empty() && c->target == 2 && !revive) targets.push_back(o.actor);
    }
    // an item's: one gone (short of it, nothing)
    if (o.item > 0) {
        const BattleData::Item* it = data_.item(o.item);
        if (itemCount(o.item) <= 0) return;
        if (it && it->usedUp) --items_[o.item];
    }
    // its MP (ActionCommonCommandStart: the party's, both a pair's - short of it, the action fails)
    if (o.actor < 2 && c->mp > 0) {
        const int partner = isPairAction(o.actor) ? a.partner : -1;
        const int cost = useMp(o.actor, partner, c->mp);
        if (a.mp < cost || (partner >= 0 && fighters_[size_t(partner)].mp < cost)) return fail(0x4c);
        a.mp -= cost;
        if (partner >= 0) fighters_[size_t(partner)].mp -= cost;
    }
    // an awakening's charge (BtlActionCtrl::ActionAwakeStandby: 100 / 200 / 300 by its level, none
    // with the script's switch 42 on - the prologue's)
    if (o.actor < 2 && c->awaken >= 1 && c->awaken <= 3 && !scriptSwitch(42)) {
        static const int kCost[3] = {100, 200, 300};
        charge_ = std::max(charge_ - kCost[c->awaken - 1], 0);
    }
    // an advent's: all of it (ActionAdventStandby, the same switch)
    if (o.actor < 2 && c->awaken == 4 && !scriptSwitch(42)) charge_ = 0;
    // state 29, a magic barrier (BtlActionCtrl::ActionSopiaLaunch, ActionSopiaMirror*): a sopia
    // on one holding it - or its partner - unless it pierces (+0x4e bit 3) turns back on the
    // caster (and its partner; the caster's side for one on all), a use of it on each holding it
    bool mirrored = false;
    if (c->magic == 1 && c->target != 1 && !(c->specials >> 3 & 1u)) {
        for (int t : targets) {
            const Fighter& tf = fighters_[size_t(t)];
            mirrored = mirrored || tf.hasState(29) || (tf.partner >= 0 && fighters_[size_t(tf.partner)].hasState(29));
        }
        if (mirrored) {
            for (int t : targets) {
                const Fighter& tf = fighters_[size_t(t)];
                if (tf.hasState(29)) tf.useState(29);
                if (tf.partner >= 0 && fighters_[size_t(tf.partner)].hasState(29)) fighters_[size_t(tf.partner)].useState(29);
            }
            targets.clear();
            if (c->target == 6) {
                for (int i = 0; i < 5; ++i)
                    if (fighters_[size_t(i)].alive && fighters_[size_t(i)].side == a.side) targets.push_back(i);
            } else {
                targets.push_back(o.actor);
                if (isPairAction(o.actor) && fighters_[size_t(a.partner)].alive) targets.push_back(a.partner);
            }
        }
    }
    if (c->kind >= 18 && c->kind <= 25) { // the restores
        for (int t : targets) {
            Event e;
            e.actor = o.actor, e.partner = isPairAction(o.actor) ? a.partner : -1, e.command = o.command, e.target = t;
            restore(o.actor, t, *c, e);
            out.push_back(e);
        }
        return;
    }
    // CalcCommand: kinds 1, 2, 3, 26, 28 states alone (a sure "hit", CalcStatus); 0, 4..17, 27,
    // 29, 32 damage, CalcStatus after a hit
    if (c->kind == 1 || c->kind == 3 || c->kind == 26 || c->kind == 28) {
        for (int t : targets) {
            Fighter& tf = fighters_[size_t(t)];
            if (!tf.alive) continue;
            Event e;
            e.actor = o.actor, e.partner = isPairAction(o.actor) ? a.partner : -1, e.command = o.command, e.target = t;
            e.hit = true; // (+0xf78: sure)
            calcStatus(o.actor, t, *c, e);
            out.push_back(e);
        }
        return;
    }
    if (!(c->kind == 0 || (c->kind >= 4 && c->kind <= 17) || c->kind == 27 || c->kind == 29 || c->kind == 32)) return;
    for (int t : targets) {
        Fighter& tf = fighters_[size_t(t)];
        if (!tf.alive) continue;
        Event e;
        e.actor = o.actor, e.partner = isPairAction(o.actor) ? a.partner : -1, e.command = o.command, e.target = t;
        e.counter = o.counter;
        e.hit = hit(o.actor, t, *c);
        if (e.hit) {
            // CheckSkillAutoGuard: at a quarter of its HP or under, not defending, its skill 1, 0
            // guards it a turn (state 4) at 20% (40% in its side's turn break) - "Auto Defend"
            const int chance = breakBonus(t) ? 20 : 10;
            if (!tf.guarding && tf.hp * 100 / std::max(tf.maxHp, 1) <= 25 && tf.has(1, 0) && int(rand_.below(100)) < chance * 2)
                tf.addState(4, 1, 0), e.notes.push_back({0x57, t});
            // CheckAttackBlock / CheckMagicBlock: a blow on one with state 31, either on one with
            // skill 5, 11 (a full guard), else by its blocking skill (1, 3 / 1, 4: 10%, 20% in its
            // side's turn break) unless the command gets through (+0x4e bit 6) - no damage
            if (c->magic == 0)
                e.blocked = tf.hasState(31) || tf.has(5, 11) || (!(c->specials >> 6 & 1u) && tf.has(1, 3) && int(rand_.below(100)) < chance);
            else if (c->magic == 1)
                e.blocked = tf.has(5, 11) || (!(c->specials >> 6 & 1u) && tf.has(1, 4) && int(rand_.below(100)) < chance);
            if (e.blocked) e.notes.push_back({c->magic ? 0x46 : 0x45, t}); // ("Neutralized physical / magical damage!")
            if (!e.blocked) e.critical = critical(o.actor, t, *c);
            if (!e.blocked) e.damage = damage(o.actor, t, *c, e.critical, &e.weak);
            tf.hp = std::max(tf.hp - e.damage, 0);
            if (tf.hp > 0) calcStatus(o.actor, t, *c, e);
            if (tf.hp > 0) checkCounter(o, *c, t);
            if (tf.hp == 0) {
                tf.alive = false, e.defeated = true;
                // (CheckDead: its states gone - but for 21, time stopped, 33, revived once, 34
                // and 36..40)
                tf.states.erase(std::remove_if(tf.states.begin(), tf.states.end(), [](const Fighter::State& st) {
                                    return !(st.id == 21 || st.id == 33 || st.id == 34 || st.id >= 36);
                                }), tf.states.end());
                tf.element = tf.monster ? tf.monster->element : tf.element;
                if (const BattleData::Monster* m = tf.side == 1 ? data_.monster(tf.id) : nullptr) {
                    exp_ = std::min(exp_ + tf.level * tf.level + m->exp, 9999999);
                    sp_ = std::min(sp_ + m->sp, 9999999);
                    oz_ = std::min(oz_ + tf.level * 10 + m->oz, 9999999);
                    checkDrops(tf, *m);
                }
            }
        }
        moveGauge(e, *c);
        chargeFor(e, *c, t == targets.front());
        out.push_back(e);
    }
}

// BtlActionCtrl::UpdateChargeGauge, for each one struck (Damage: a hit on the first target; the
// others' each time): the party striking a monster 15 a hit, 30 a critical (none on one guarding);
// the party struck by the damage it took - half its HP or more 30, over a fifth 20, else 15 (half
// for one guarding); the party's awakenings and advents none. (State 12's x 1.5: no states yet.)
void BattleRules::chargeFor(const Event& e, const BattleData::Command& c, bool first) {
    if (c.awaken && e.actor < 2) return;
    const Fighter& t = fighters_[size_t(e.target)];
    int n = 0;
    if (e.target >= 2) {
        if (e.actor >= 2 || t.guarding) return;
        n = e.critical ? 30 : e.hit ? 15 : 0;
    } else {
        if (!e.hit && first) return;
        n = e.damage >= t.maxHp / 2 ? 30 : e.damage > t.maxHp / 5 ? 20 : 15;
        if (t.guarding) n = n / 2;
    }
    addCharge(n);
}

void BattleRules::moveGauge(const Event& e, const BattleData::Command& c) {
    if (break_ != 0) return; // (a break now, or one coming)
    int points = (e.hit && c.kind == 27) + e.weak + e.critical + e.defeated;
    if (fighters_[size_t(e.target)].guarding) points = 0;
    if (e.target == e.actor) points = -points;
    gauge_ = std::clamp(gauge_ + (e.actor < 2 ? points : -points), -4, 4);
    if (gauge_ == 4 || gauge_ == -4) break_ = 3;
}

void BattleRules::beginTurn() {
    for (Fighter& f : fighters_) f.guarding = false, f.canceled = false;
    scripted_ = Scripted(); // (BtlCommand::StepInit: none given yet)
    callScript("Turn_Start");
}

void BattleRules::chooseTurn(bool pair, const std::vector<Order>& party) {
    // the turn the script gave (BtlCommand::StepEvent: ReserveCommand's commands, targets, pair)
    std::vector<Order> given = party;
    if (scripted_.reserved) {
        pair = scripted_.pair;
        given.clear();
        for (int k = 0; k < 2; ++k)
            if (scripted_.command[k] > 0) given.push_back({k, scripted_.command[k], scripted_.target[k]});
    }
    // the party's formation this turn
    const bool canPair = pair && fighters_[0].alive && fighters_[1].alive;
    fighters_[0].partner = canPair ? 1 : -1, fighters_[1].partner = canPair ? 0 : -1;
    std::vector<Turn> turns;
    for (int f = 0; f < 5; ++f) current_[f] = 0;
    for (const Order& o : given) {
        if (o.actor < 0 || o.actor > 1 || !fighters_[size_t(o.actor)].alive) continue;
        if (canPair && o.actor == 1) continue; // (a pair acts once)
        const BattleData::Command* c = data_.command(o.command);
        if (c && c->kind == 2) fighters_[size_t(o.actor)].guarding = true;
        int point = orderPoint(o.actor, canPair ? 1 - o.actor : -1);
        if (c && (c->specials >> 7 & 1)) point += 10000;
        if (c && c->kind == 4) point -= 5000;
        turns.push_back({o, point});
        current_[o.actor] = o.command;
    }
    for (int m = 2; m < 5; ++m) {
        if (!fighters_[size_t(m)].alive) continue;
        Order o = enemyChoice(m);
        if (scripted_.reserved && scripted_.command[m] > 0) o = {m, scripted_.command[m], scripted_.target[m]};
        if (o.command <= 0) continue;
        const BattleData::Command* c = data_.command(o.command);
        int point = orderPoint(m, -1);
        if (c && (c->specials >> 7 & 1)) point += 10000;
        if (c && c->kind == 4) point -= 5000;
        turns.push_back({o, point});
        current_[m] = o.command;
    }
    std::stable_sort(turns.begin(), turns.end(), [](const Turn& x, const Turn& y) { return x.point > y.point; });
    turnOrder_.clear();
    for (const Turn& t : turns) turnOrder_.push_back(t.o);
    next_ = 0;
}

void BattleRules::startAction() {
    commandActor_ = nextAction().actor;
    callScript("Command_Start");
}

// BtlActionCtrl::CheckCounter, on one struck and standing (Damage): a physical blow on one with
// state 26 or 27, or its counter skill (Has 1, 1: 10%, 20% in its side's turn break); a sopia on
// one with 28 (a use of it), or its skill (Has 1, 2: the same chances) - none on a command with
// +0x4e bit 4, on one stunned (3), its action cancelled or its time stopped, nor on a counter. It
// strikes back with its attack (a character's +0x44, a monster's +0x4c) first in the turn, once
// an action.
void BattleRules::checkCounter(const Order& o, const BattleData::Command& c, int target) {
    const Fighter& t = fighters_[size_t(target)];
    if (o.counter || !t.alive || t.side == fighters_[size_t(o.actor)].side || (c.specials >> 4 & 1u)) return;
    if (t.hasState(3) || t.canceled || t.hasState(21)) return;
    for (const Order& k : counters_)
        if (k.target == o.actor) return; // (+0xf8f: once)
    const int chance = breakBonus(target) ? 20 : 10;
    bool counter = false;
    if (c.magic == 0) {
        counter = t.hasState(26) || t.hasState(27);
        if (!counter && t.has(1, 1)) counter = int(rand_.below(100)) < chance;
    } else if (c.magic == 1) {
        if (t.hasState(28)) t.useState(28), counter = true;
        else if (t.has(1, 2)) counter = int(rand_.below(100)) < chance;
    }
    if (!counter) return;
    Order k;
    k.actor = target, k.target = o.actor, k.counter = true;
    k.command = t.monster ? t.monster->counter : t.attack;
    if (k.command > 0) counters_.push_back(k);
}

void BattleRules::doAction(std::vector<Event>& out) {
    const size_t first = out.size();
    counters_.clear();
    act(nextAction(), out);
    // (BtlActionCtrl::PostCommandAction, CheckAttachSelf: the command's +0x71 1..3 lends the one
    // acting - and its partner in a pair - that element, 4 takes it; none for an action not done)
    const BattleData::Command* done = data_.command(nextAction().command);
    const Fighter& doer = fighters_[size_t(nextAction().actor)];
    if (done && done->attachSelf >= 1 && done->attachSelf <= 4 && !(out.size() > first && out[first].failed) && !doer.canceled &&
        !doer.hasState(21))
        for (int f : {nextAction().actor, isPairAction(nextAction().actor) ? fighters_[size_t(nextAction().actor)].partner : -1})
            if (f >= 0 && fighters_[size_t(f)].alive) fighters_[size_t(f)].element = done->attachSelf == 4 ? 0 : done->attachSelf;
    for (size_t k = first; k < out.size(); ++k) // (BtlActionCtrl::ActionDeadEvent: each downed)
        if (out[k].defeated) {
            commandActor_ = out[k].target;
            callScript("Dead");
            // (ActionDeadWaitDeadMotion: one of the party with state 33 - a use of it - back with a
            // quarter of its most HP, ActionReviveMain)
            Fighter& t = fighters_[size_t(out[k].target)];
            if (out[k].target < 2 && !t.alive && t.hasState(33)) {
                t.useState(33);
                t.alive = true, t.hp = t.maxHp / 4;
                out[k].regenerated = true, out[k].regenHp = t.hp;
            }
        }
    commandActor_ = nextAction().actor;
}

void BattleRules::endAction() {
    commandActor_ = nextAction().actor;
    callScript("Command_End");
    ++next_;
    turnOrder_.insert(turnOrder_.begin() + std::ptrdiff_t(next_), counters_.begin(), counters_.end()); // (the counters next)
    counters_.clear();
}

void BattleRules::endTurn() {
    callScript("Turn_End");
    // BtlTurnEnd: each standing one's HP by the poison's damage (ceil a tenth of the most HP, one
    // left) less its life pool's (skill 5, 6: ceil a twentieth), all of it back with the full one
    // (5, 10) - a heal if that comes to less than none -, its MP up by its mana pool (5, 7: ceil a
    // twentieth of the most), then each standing one's states a turn on (BtlStateManager::Turn:
    // those counting turns, gone at none left)
    turnEnd_.clear();
    for (int k = 0; k < 5; ++k) {
        Fighter& f = fighters_[size_t(k)];
        if (!f.alive) continue;
        int d = f.hasState(0) ? (f.maxHp + 9) / 10 : 0;
        if (f.has(5, 6)) d -= (f.maxHp + 19) / 20;
        if (f.has(5, 10)) d = -f.maxHp;
        Event e;
        e.target = k;
        if (d > 0) e.hit = true, e.damage = d, f.hp = std::max(f.hp - d, 1);
        else if (d < 0) e.heal = -d, f.hp = std::min(f.hp - d, f.maxHp); // (the number the whole of it)
        if (f.has(5, 7) && f.maxMp > 0) {
            e.healMp = (f.maxMp + 19) / 20;
            f.mp = std::min(f.mp + e.healMp, f.maxMp);
        }
        if (d != 0 || e.healMp > 0) turnEnd_.push_back(e);
    }
    for (Fighter& f : fighters_) {
        if (!f.alive) continue;
        for (size_t s = 0; s < f.states.size();) {
            Fighter::State& st = f.states[s];
            if (st.turns > 0 && --st.turns == 0) f.states.erase(f.states.begin() + std::ptrdiff_t(s));
            else ++s;
        }
    }
    // BtlTurnEnd: a break ends, or one coming begins
    if (break_ == 1 || break_ == 2) break_ = 0, gauge_ = 0;
    else if (break_ == 3) break_ = gauge_ == 4 ? 1 : 2;
    ++turn_;
}

std::vector<BattleRules::Event> BattleRules::playTurn(bool pair, const std::vector<Order>& party) {
    std::vector<Event> out;
    beginTurn();
    chooseTurn(pair, party);
    while (actionsLeft()) {
        startAction();
        doAction(out);
        endAction();
    }
    if (!exited()) endTurn();
    return out;
}

// the fighter record a SetParamI / GetParamI id names, and the field in it: 0 HP, 1 MP, 2..6 the
// parameters, 7 max HP, 8 max MP (-1: none)
static bool paramSlot(int id, int& f, int& field) {
    if (id >= 10 && id <= 23) return f = (id - 10) / 7, field = (id - 10) % 7, true;
    if (id >= 24 && id <= 41) {
        f = 2 + (id - 24) / 6, field = (id - 24) % 6;
        field = field == 0 ? 0 : field + 1; // (no MP)
        return true;
    }
    if (id >= 48 && id <= 51) return f = (id - 48) / 2, field = 7 + (id - 48) % 2, true;
    if (id >= 52 && id <= 54) return f = 2 + id - 52, field = 7, true;
    return false;
}

int BattleRules::paramI(int id) const {
    int f, field;
    if (id >= 10 && id <= 41 && paramSlot(id, f, field)) {
        const Fighter& x = fighters_[size_t(f)];
        return field == 0 ? x.hp : field == 1 ? x.mp : x.param[field - 2];
    }
    if (id == 55) return gauge_;
    if (id == 56) return break_ == 1;
    if (id == 57) return break_ == 2;
    return 0; // (the rest read nothing back: Cr3BattleEvent_GetParamI)
}

void BattleRules::setParamI(int id, int value) {
    int f, field;
    if (id >= 5 && id <= 9) ints_[id - 5] = value;
    else if (id >= 42 && id <= 47) switches_[id - 42] = value == 1;
    else if (id == 58) exitTimer_ = float(value) * 0.05f;
    else if (paramSlot(id, f, field)) {
        Fighter& x = fighters_[size_t(f)];
        switch (field) {
        case 0: x.hp = value < 0 ? x.maxHp : value; break;
        case 1: x.mp = (value & 0x8000) ? x.maxMp : value; break;
        case 7: x.maxHp = value; break;
        case 8: x.maxMp = value; break;
        default: x.param[field - 2] = value; break;
        }
    }
}

int BattleRules::useMp(int f, int partner, int mp) const {
    auto has = [&](int group, int bit) {
        return fighters_[size_t(f)].has(group, bit) || (partner >= 0 && fighters_[size_t(partner)].has(group, bit));
    };
    if (has(5, 3)) mp -= mp * 50 / 100; // (BtlCalcValue(mp, -50))
    if (has(0, 10)) mp += mp * 50 / 100;
    return mp;
}

bool BattleRules::tryEscape() {
    bool sure = break_ == 1;
    for (int k = 0; k < 2; ++k)
        if (fighters_[size_t(k)].alive && fighters_[size_t(k)].has(4, 3)) sure = true;
    if (!sure) {
        int levels = 0, count = 0, monsters = 0;
        for (int k = 0; k < 5; ++k) {
            const Fighter& f = fighters_[size_t(k)];
            if (!f.present) continue;
            if (k < 2) levels += f.level, ++count;
            else monsters += f.level;
        }
        const int mean = count ? levels / count : 0;
        static const int kThresholds[3] = {-50, -1, 49}, kChance[4] = {40, 50, 60, 70};
        int i = 0;
        while (i < 3 && kThresholds[i] < mean - monsters) ++i; // (CheckResult)
        sure = rand_.below(100) < kChance[i];
    }
    return escaped_ = sure;
}

void BattleRules::checkDrops(const Fighter& monster, const BattleData::Monster& m) {
    int levels = 0, count = 0, monsters = 0;
    for (int k = 0; k < 5; ++k) {
        const Fighter& f = fighters_[size_t(k)];
        if (!f.present) continue;
        if (k < 2) levels += f.level, ++count;
        else monsters += f.level;
    }
    static const int kThresholds[3] = {-5, 0, 4}, kChance[4] = {20, 30, 40, 50};
    int i = 0;
    while (i < 3 && kThresholds[i] < (count ? levels / count : 0) - monsters) ++i; // (CheckResult)
    const int band = monster.level < 20 ? 0 : monster.level < 40 ? 1 : 2; // (BtlUtilData::GetDropIndex)
    // the item: ItemUp (skill 3, 3) of either +20 in 100; RareGetter (3, 4) the first's - either's,
    // a pair - 45 in 90 the rare one
    const Fighter &a = fighters_[0], &b = fighters_[1];
    const bool itemUp = (a.present && a.has(3, 3)) || (b.present && b.has(3, 3));
    const bool rare = (a.present && a.has(3, 4)) || (a.partner >= 0 && b.present && b.has(3, 4));
    if (rand_.below(100) < kChance[i] + (itemUp ? 20 : 0)) {
        const int item = rand_.below(90) < (rare ? 45 : 30) ? m.rareDrops[band] : m.drops[band];
        if (item > 0 && item < 300 && itemCount(item) + drops_[item] < 99) ++drops_[item];
    }
    // the sopia
    const int s = m.sopia;
    if (s > 0 && s < 32 && monster.level >= m.sopiaLevel && !(sopiaOwned_ >> s & 1) && !sopiaDrops_[s] && rand_.below(100) < kChance[i])
        sopiaDrops_[s] = true;
}

// BtlCalc::BadStatus: whether bad state k (0 poison, 1 blind, 2 seal, 3 the action's cancel, 4 the
// fifth) takes - the command's (+0x50 + k is 1), or for a physical one the attacker's (its
// partner's in a pair) auto skill lending it (skill group -1, bit k); `chance` in 100,
// `breakChance` in its side's turn break; the target resisting halves it, immune none
bool BattleRules::badStatus(int actor, int target, const BattleData::Command& c, int k, int breakChance, int chance) {
    const Fighter& a = fighters_[size_t(actor)];
    const bool inflicts = c.bad[k] == 1;
    if (!inflicts) {
        if (c.magic != 0) return false;
        const bool lent = (a.skills[0] >> k & 1u) || (isPairAction(actor) && (fighters_[size_t(a.partner)].skills[0] >> k & 1u));
        if (!lent) return false;
    }
    if (breakBonus(actor)) chance = breakChance;
    const int r = k < 5 ? fighters_[size_t(target)].stateResist[k] : 0; // (+0x43: the first five)
    if (r == 2) return false;
    if (r == 1) chance /= 2;
    return int(rand_.below(100)) < chance;
}

// BtlActionCtrl::CalcStatus on one hit (or a states' command's target): CheckBadStatus,
// CheckCureStatus, CheckParamStatus, CheckSpecialStatus, CheckAttachTarget - each noting its
// announcement (announcebattle: the one hit's name the %s; a state on the party, one on a monster)
void BattleRules::calcStatus(int actor, int target, const BattleData::Command& c, Event& e) {
    Fighter& t = fighters_[size_t(target)];
    if (!t.alive) return;
    const Fighter& a = fighters_[size_t(actor)];
    const bool party = target < 2;
    const int bonus = (a.has(5, 5) || (isPairAction(actor) && fighters_[size_t(a.partner)].has(5, 5))) ? 1 : 0; // (HasPair 5, 5)
    auto note = [&](int id) { if (id > 0) e.notes.push_back({id, target}); };
    // the states' length by the command (+0x6c / +0x6d): one use, a turn, turns, for good
    auto length = [&](int& turns, int& count) {
        turns = count = 0;
        if (c.durationMode == 1) count = 1;
        else if (c.durationMode == 2) turns = 1;
        else if (c.durationMode == 3) turns = c.turns + bonus;
    };
    // CheckBadStatus: the action cancelled (70 / 50), then poison, blind, seal (100 / 80; the
    // command's turns, else 3), the next turn's action cancelled (state 3, a turn), then time
    // stopped and the seals (100 / 100: the command's turns)
    bool took[5] = {}, resisted[3] = {};
    if (badStatus(actor, target, c, 3, 70, 50)) t.canceled = took[3] = true;
    for (int k = 0; k < 3; ++k)
        if (!t.hasState(k)) {
            if (t.stateResist[k] == 2 && c.bad[k] == 1) {
                resisted[k] = true;
                continue;
            }
            if (badStatus(actor, target, c, k, 100, 80)) t.addState(k, (c.bad[k] == 1 ? c.turns : 3) + bonus, 0), took[k] = true;
        }
    if (!t.hasState(3) && badStatus(actor, target, c, 4, 100, 80)) t.addState(3, 1, 0), took[4] = true;
    // (ShowAnnounceStatus1: more than one of poison, blind, seal "hit with status effects", else
    // the first of poison, blind, (the cancel: none, nor the rest), seal, state 3 - the party's
    // announcement or a monster's -, then the first resisted)
    if (took[0] + took[1] + took[2] > 1) note(0x58);
    else {
        static const int kBadNote[3][2] = {{0x1e, 0x19}, {0x1f, 0x1a}, {0x20, 0x1b}};
        if (took[0]) note(kBadNote[0][party ? 0 : 1]);
        else if (took[1]) note(kBadNote[1][party ? 0 : 1]);
        else if (took[3]) {
        } else if (took[2]) note(kBadNote[2][party ? 0 : 1]);
        else if (took[4]) note(party ? 0x1d : 0x18);
        if (resisted[0]) note(0x21);
        else if (resisted[1]) note(0x22);
        else if (resisted[2]) note(0x23);
    }
    // (0x525d38: the command's byte, the state, ShowAnnounceStatus3's announcement)
    static const int kMore[5][3] = {{5, 21, 0x59}, {6, 22, 0x52}, {7, 23, 0x20}, {8, 25, 0x54}, {9, 24, 0x56}};
    for (const auto& m : kMore)
        if (!t.hasState(m[1]) && badStatus(actor, target, c, m[0], 100, 100))
            t.addState(m[1], c.turns + bonus, 0), note(m[1] == 23 && !party ? 0x1b : m[2]);
    // CheckCureStatus: +0x50 + k of 2 cures it
    for (int k = 0; k < 3; ++k)
        if (c.bad[k] == 2 && t.hasState(k)) t.clearState(k);
    // CheckParamStatus: +0x5c + k - 1 lowers (a raise there undone), 2 raises (a lowering undone)
    static const int kUp[8] = {0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0, 0x2f, 0x2e}, kDown[8] = {0x31, 0x32, 0x33, 0x34, 0x35, 0, 0, 0},
                     kBack[8] = {0x36, 0x37, 0x38, 0x39, 0x3a, 0, 0, 0};
    for (int k = 0; k < 8; ++k) {
        const int up = 5 + k, down = 13 + k;
        int turns, count;
        length(turns, count);
        if (c.change[k] == 1) {
            if (t.hasState(up)) t.clearState(up);
            else if (!t.hasState(down)) t.addState(down, turns, count), note(kDown[k]);
        } else if (c.change[k] == 2) {
            if (t.hasState(down)) t.clearState(down), note(kBack[k]);
            else if (!t.hasState(up)) t.addState(up, turns, count), note(kUp[k]);
        }
    }
    // CheckSpecialStatus: +0x64's bit b the state 26 + b (0x535c08, with its announcement's bit -
    // ShowAnnounceStatus3's: 26..28 counters, 29 a magic barrier, 30 provoking, 31 blocking
    // attacks, 32 sure criticals, 33 revived once, 35 physical strength doubled, 37 thrown back,
    // 38..40 EXP / SP / Oz doubled)
    static const int kSpecialNote[15] = {0, 0, 0, 0x3e, 0x3d, 0, 0x30, 0x3c, 0, 0x3b, 0, 0, 0x26, 0x27, 0x28};
    for (int b = 0; b < 15; ++b)
        if (c.special >> b & 1u) {
            int turns, count;
            length(turns, count);
            t.addState(26 + b, turns, count), note(kSpecialNote[b]);
        }
    // CheckAttachTarget: +0x70 1..3 lends that element, 4 takes it
    if (c.attach >= 1 && c.attach <= 3) t.element = c.attach, note(0x41 + c.attach);
    else if (c.attach == 4) t.element = 0;
}

void BattleRules::clearStates() {
    for (Fighter& f : fighters_)
        if (f.alive) f.guarding = false; // (the only state kept yet)
}

} // namespace cr2
