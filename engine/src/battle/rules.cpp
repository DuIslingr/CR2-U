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

bool BattleRules::start(int coupling, const int party[2], int dungeonLevel, std::string* error) {
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
        Fighter& f = fighters_[size_t(k)];
        f.present = f.alive = true;
        f.side = 0, f.id = c->id, f.name = c->name[1], f.level = c->level;
        f.maxHp = std::clamp(c->hp + c->hpGrowth * f.level * 50 / 100, 1, 99999);
        f.maxMp = std::clamp(c->mp + c->mpGrowth * f.level * 5 / 100, 0, 9999);
        for (int n = 0; n < 5; ++n) f.param[n] = std::clamp(c->param[n] + c->paramGrowth[n] * f.level * 5 / 100, 0, 9999);
        for (int e = 0; e < 3; ++e) {
            const BattleData::Item* it = data_.item(c->equip[e]);
            if (!it) continue;
            f.maxHp += it->hp, f.maxMp += it->mp;
            for (int n = 0; n < 5; ++n) f.param[n] += it->param[n];
            for (int s : it->skills)
                if (const BattleData::AutoSkill* a = data_.autoSkill(s)) {
                    for (int g = 0; g < 7; ++g) f.skills[g] |= a->groups[g];
                    for (int r = 0; r < 7; ++r) f.resist[r] = std::max(f.resist[r], a->resist[r]);
                }
        }
        f.hp = f.maxHp, f.mp = f.maxMp;
        f.attack = c->attack, f.pairAttack = c->pairAttack;
        levels += f.level, ++count;
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

int BattleRules::param(int f, int n) const { return fighters_[size_t(f)].param[n]; }

bool BattleRules::isPairAction(int f) const {
    const Fighter& a = fighters_[size_t(f)];
    return a.alive && a.partner >= 0 && fighters_[size_t(a.partner)].alive;
}

bool BattleRules::hit(int actor, int target, const BattleData::Command& c) {
    if (c.magic == 1) return true;
    if (c.specials & 1) return true;
    int speed = param(actor, 3);
    if (isPairAction(actor)) speed += param(fighters_[size_t(actor)].partner, 3);
    static const int kChance[6] = {60, 70, 80, 90, 95, 99};
    int chance = kChance[step(speed - param(target, 4))];
    if (c.kind == 15) chance = 75;
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
    // CommandY: the power's additions
    int y = 0;
    if (breakBonus(actor)) y += 100;
    if (pair) y += 25;
    if (actor > 1) y += 50;
    if (crit) y += hasPair(2, 3) ? 100 : 50;
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
    if (t.guarding) d *= 0.5f;
    if (breakBonus(target)) d *= 0.5f;
    if (!pair && t.has(5, 9) && !a.has(5, 0) && !(c.specials >> 2 & 1)) d *= 0.1f; // (armour)
    return int(std::ceil(d));
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
    if (c->kind == 2) fighters_[size_t(mi)].guarding = true; // (SetCommand: kind 2 guards at once)
    return o;
}

void BattleRules::act(const Order& o, std::vector<Event>& out) {
    Fighter& a = fighters_[size_t(o.actor)];
    if (!a.alive) return;
    const BattleData::Command* c = data_.command(o.command);
    if (!c) return;
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
    }
    if (c->kind != 0 && c->kind != 7 && c->kind != 5 && c->kind != 12) return; // (states, restores: not yet)
    for (int t : targets) {
        Fighter& tf = fighters_[size_t(t)];
        if (!tf.alive) continue;
        Event e;
        e.actor = o.actor, e.partner = isPairAction(o.actor) ? a.partner : -1, e.command = o.command, e.target = t;
        e.hit = hit(o.actor, t, *c);
        if (e.hit) {
            e.critical = critical(o.actor, t, *c);
            e.damage = damage(o.actor, t, *c, e.critical, &e.weak);
            tf.hp = std::max(tf.hp - e.damage, 0);
            if (tf.hp == 0) tf.alive = false, e.defeated = true;
        }
        moveGauge(e, *c);
        out.push_back(e);
    }
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
    for (Fighter& f : fighters_) f.guarding = false;
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

void BattleRules::doAction(std::vector<Event>& out) {
    const size_t first = out.size();
    act(nextAction(), out);
    for (size_t k = first; k < out.size(); ++k) // (BtlActionCtrl::ActionDeadEvent: each downed)
        if (out[k].defeated) {
            commandActor_ = out[k].target;
            callScript("Dead");
        }
    commandActor_ = nextAction().actor;
}

void BattleRules::endAction() {
    commandActor_ = nextAction().actor;
    callScript("Command_End");
    ++next_;
}

void BattleRules::endTurn() {
    callScript("Turn_End");
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

void BattleRules::clearStates() {
    for (Fighter& f : fighters_)
        if (f.alive) f.guarding = false; // (the only state kept yet)
}

} // namespace cr2
