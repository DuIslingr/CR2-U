#include "battle/battledata.hpp"

#include <cstring>

namespace cr2 {

namespace {

uint32_t u32(const std::vector<uint8_t>& d, size_t o) {
    return o + 4 <= d.size() ? uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24 : 0;
}
int16_t s16(const std::vector<uint8_t>& d, size_t o) { return o + 2 <= d.size() ? int16_t(d[o] | d[o + 1] << 8) : 0; }

std::string cstr(const std::vector<uint8_t>& d, size_t o, size_t max) {
    std::string s;
    for (size_t k = 0; k < max && o + k < d.size() && d[o + k]; ++k) s += char(d[o + k]);
    return s;
}

// BtlMapTable::Initialize: the battle maps, by the coupling's map (from 1; entry 0 unused)
const char* const kMaps[] = {"d0151b", "d0151b", "d0251b", "d0252b", "d0351b", "d0352b", "d0451b", "d0551b",
                             "d0651b", "d0652b", "d0751b", "d0752b", "d0851b", "d0951b", "d0952b", "d1051b",
                             "d1052b", "d1053b", "d1151b", "d1152b", "d1153b", "d1154b", "d1251b", "d1351b",
                             "d1352b", "t0151b", "d5051b", "d1251v", "d0152v", "d0751v", "d1053v", "d1154v"};

// BtlDposTable::Initialize: the battledpos joints (entry 6 empty)
const char* const kDpos[] = {"dPos_hPairA", "dPos_hPairB", "dPos_hSoloA", "dPos_hSoloB", "dPos_lC",   "dPos_mC",
                             "",            "dPos_mmL",    "dPos_mmR",    "dPos_msL",    "dPos_msR",  "dPos_mssC",
                             "dPos_mssL",   "dPos_mssR",   "dPos_pPairA", "dPos_pPairB", "dPos_pSoloA",
                             "dPos_pSoloB", "dPos_sC",     "dPos_sL",     "dPos_sR",     "dPos_sssC", "dPos_sssL",
                             "dPos_sssR",   "dPos_xlC",    "dPos_pSoloC", "dPos_hSoloC", "dPos_xlL",  "dPos_xlR"};

// BtlCouplingTable::Initialize: each pattern's monsters' sizes (-1 none) and their dpos entries
struct Pattern {
    int size[3];
    int dpos[3];
    const char* start;  // its cameras' names (Cam_Start_<>_00, Cam_Getset_<>_00)
    const char* getset;
};
const Pattern kPatterns[13] = {
    {{0, -1, -1}, {18, -1, -1}, "S", "S"},          {{0, 0, -1}, {19, 20, -1}, "SS", "SS"},
    {{0, 0, 0}, {22, 21, 23}, "SSS", "SSS"},        {{1, -1, -1}, {5, -1, -1}, "M", "M"},
    {{1, 0, -1}, {9, 10, -1}, "MS", "MS"},          {{1, 0, 0}, {12, 11, 13}, "MSS", "MSS"},
    {{1, 1, -1}, {7, 8, -1}, "MM", "MM"},           {{2, -1, -1}, {4, -1, -1}, "L", "L"},
    {{3, -1, -1}, {24, -1, -1}, "XL", "XL"},        {{3, 3, -1}, {27, 28, -1}, "XLXL", "XLXL"},
    {{4, -1, -1}, {26, -1, -1}, "Hsolo", "Hsolo"},  {{4, 4, -1}, {2, 3, -1}, "Hpair", "HHpair"},
    {{3, -1, -1}, {24, -1, -1}, "neron", "neron"},
};

bool fail(std::string* error, const std::string& why) {
    if (error) *error = why;
    return false;
}

} // namespace

bool BattleData::parse(const std::vector<uint8_t>& d, std::string* error) {
    characters_.clear(), monsters_.clear(), couplings_.clear(), commands_.clear(), items_.clear(), autoSkills_.clear(), sopias_.clear();
    if (d.size() < 0x40) {
        if (error) *error = "battledata: too short";
        return false;
    }
    const uint32_t charas = u32(d, 0x00), monsters = u32(d, 0x08), couplings = u32(d, 0x0c), strings = u32(d, 0x30);
    const int nChara = s16(d, 0x34), nMonster = s16(d, 0x36), nCoupling = s16(d, 0x38);
    auto text = [&](size_t recordField) { return cstr(d, size_t(strings) + u32(d, recordField), 256); };
    for (int i = 0; i < nChara; ++i) {
        const size_t o = charas + size_t(i) * 0x98;
        if (o + 0x98 > d.size()) break;
        Character c;
        c.id = i + 1;
        c.name[0] = text(o), c.name[1] = text(o + 4);
        c.figure = cstr(d, o + 8, 8);
        c.hp = int(u32(d, o + 0x10)), c.mp = s16(d, o + 0x14);
        c.hpGrowth = d[o + 0x21], c.mpGrowth = d[o + 0x22], c.level = d[o + 0x20];
        for (int k = 0; k < 5; ++k) c.param[k] = s16(d, o + 0x16 + size_t(k) * 2), c.paramGrowth[k] = d[o + 0x23 + size_t(k)];
        for (int k = 0; k < 8; ++k) c.equip[k] = s16(d, o + 0x28 + size_t(k) * 2);
        for (int k = 0; k < 3; ++k) c.specials[k] = s16(d, o + 0x38 + size_t(k) * 2);
        c.attack = s16(d, o + 0x44), c.pairAttack = s16(d, o + 0x46);
        characters_.push_back(c);
    }
    for (int i = 0; i < nMonster; ++i) {
        const size_t o = monsters + size_t(i) * 0x110;
        if (o + 0x110 > d.size()) break;
        Monster m;
        m.id = i + 1;
        m.name[0] = text(o), m.name[1] = text(o + 4);
        m.figure = cstr(d, o + 0x10, 12);
        m.hp = int(u32(d, o + 0x1c));
        m.size = int(int8_t(d[o + 0x53]));
        for (int k = 0; k < 5; ++k) m.param[k] = s16(d, o + 0x20 + size_t(k) * 2);
        m.level = d[o + 0x3b];
        for (int k = 0; k < 7; ++k) m.resist[k] = d[o + 0x3c + size_t(k)];
        for (int k = 0; k < 5; ++k) m.stateResist[k] = d[o + 0x43 + size_t(k)];
        m.skills = u32(d, o + 0x48);
        m.element = d[o + 0x50];
        for (int k = 0; k < 10; ++k) {
            m.commands[k] = s16(d, o + 0x58 + size_t(k) * 2);
            m.condition[k] = d[o + 0x6c + size_t(k)], m.targetRule[k] = d[o + 0x76 + size_t(k)];
            m.weight[k] = int8_t(d[o + 0x80 + size_t(k)]), m.must[k] = d[o + 0x8a + size_t(k)];
            m.conditionValue[k] = s16(d, o + 0x94 + size_t(k) * 2);
        }
        monsters_.push_back(m);
    }
    for (int i = 0; i < nCoupling; ++i) {
        const size_t o = couplings + size_t(i) * 0x1c;
        if (o + 0x1c > d.size() || o + 0x1c > u32(d, 0x10)) break; // (the count runs one past: the areas follow)
        Coupling c;
        c.id = i + 1;
        c.music = s16(d, o), c.map = s16(d, o + 2);
        c.flag = d[o + 4] != 0;
        for (int k = 0; k < 3; ++k) c.monsters[k] = s16(d, o + 8 + size_t(k) * 2);
        couplings_.push_back(c);
    }
    const uint32_t commands = u32(d, 0x14), autoSkills = u32(d, 0x18), items = u32(d, 0x24);
    for (int i = 0, n = s16(d, 0x3c); i < n; ++i) {
        const size_t o = commands + size_t(i) * 0x7c;
        if (o + 0x7c > d.size()) break;
        Command c;
        c.id = i + 1;
        c.name[0] = text(o), c.name[1] = text(o + 4); // (+0 Japanese, +4 English)
        c.motion = cstr(d, o + 0x38, 6);
        c.power = int(u32(d, o + 0x44));
        c.magic = d[o + 0x49], c.spread = d[o + 0x4a], c.target = d[o + 0x4b], c.element = d[o + 0x4c];
        c.specials = uint16_t(s16(d, o + 0x4e));
        c.kind = d[o + 0x6f];
        c.awaken = d[o + 0x48];
        c.mp = s16(d, o + 0x42), c.icon = d[o + 0x72];
        commands_.push_back(c);
    }
    const uint32_t sopias = u32(d, 0x1c);
    for (int i = 0, n = s16(d, 0x40); i < n; ++i) {
        const size_t o = sopias + size_t(i) * 0x2c;
        if (o + 0x2c > d.size()) break;
        Sopia s;
        s.id = i + 1;
        s.name[0] = text(o), s.name[1] = text(o + 4);
        s.growth = d[o + 0x10], s.startLevel = d[o + 0x12];
        for (int k = 0; k < 10; ++k) s.commands[k] = s16(d, o + 0x18 + size_t(k) * 2);
        sopias_.push_back(s);
    }
    for (int i = 0, n = s16(d, 0x3e); i < n; ++i) {
        const size_t o = autoSkills + size_t(i) * 0x38;
        if (o + 0x38 > d.size()) break;
        AutoSkill a;
        a.id = i + 1;
        for (int k = 0; k < 7; ++k) a.resist[k] = d[o + 0x17 + size_t(k)];
        for (int k = 0; k < 5; ++k) a.stateResist[k] = d[o + 0x1e + size_t(k)];
        for (int k = 0; k < 7; ++k) a.groups[k] = uint16_t(s16(d, o + 0x26 + size_t(k) * 2));
        autoSkills_.push_back(a);
    }
    for (int i = 0, n = s16(d, 0x44); i < n; ++i) {
        const size_t o = items + size_t(i) * 0x38;
        if (o + 0x38 > d.size()) break;
        Item it;
        it.id = i + 1;
        it.name[0] = text(o), it.name[1] = text(o + 4);
        it.type = s16(d, o + 0x10);
        it.hp = int(u32(d, o + 0x14)), it.mp = s16(d, o + 0x18);
        for (int k = 0; k < 5; ++k) it.param[k] = s16(d, o + 0x1a + size_t(k) * 2);
        it.skills[0] = s16(d, o + 0x30), it.skills[1] = s16(d, o + 0x32);
        items_.push_back(it);
    }
    if (characters_.empty() || monsters_.empty() || couplings_.empty()) {
        if (error) *error = "battledata: no records";
        return false;
    }
    return true;
}

const BattleData::Character* BattleData::character(int id) const {
    return id >= 1 && id <= int(characters_.size()) ? &characters_[size_t(id - 1)] : nullptr;
}
const BattleData::Monster* BattleData::monster(int id) const {
    return id >= 1 && id <= int(monsters_.size()) ? &monsters_[size_t(id - 1)] : nullptr;
}
const BattleData::Coupling* BattleData::coupling(int id) const {
    return id >= 1 && id <= int(couplings_.size()) ? &couplings_[size_t(id - 1)] : nullptr;
}
const BattleData::Command* BattleData::command(int id) const {
    return id >= 1 && id <= int(commands_.size()) ? &commands_[size_t(id - 1)] : nullptr;
}
const BattleData::Item* BattleData::item(int id) const {
    return id >= 1 && id <= int(items_.size()) ? &items_[size_t(id - 1)] : nullptr;
}
const BattleData::Sopia* BattleData::sopia(int id) const {
    return id >= 1 && id <= int(sopias_.size()) ? &sopias_[size_t(id - 1)] : nullptr;
}
const BattleData::AutoSkill* BattleData::autoSkill(int id) const {
    return id >= 1 && id <= int(autoSkills_.size()) ? &autoSkills_[size_t(id - 1)] : nullptr;
}

bool setupBattle(const BattleData& data, int id, const int party[2], BattleSetup& out, std::string* error) {
    out = BattleSetup();
    out.coupling = id;
    const BattleData::Coupling* c = data.coupling(id);
    if (!c) return fail(error, "no coupling " + std::to_string(id));
    if (c->map < 0 || c->map >= int(sizeof kMaps / sizeof kMaps[0]))
        return fail(error, "coupling " + std::to_string(id) + ": map " + std::to_string(c->map));
    out.map = kMaps[c->map];
    // the monsters in their slots (BtlWork::InitializeEnemy: up to three, the first absent one ends them)
    const BattleData::Monster* mons[3] = {nullptr, nullptr, nullptr};
    int sizes[3] = {-1, -1, -1};
    for (int k = 0; k < 3 && c->monsters[k] > 0; ++k) {
        mons[k] = data.monster(c->monsters[k]);
        if (!mons[k]) return fail(error, "coupling " + std::to_string(id) + ": no monster " + std::to_string(c->monsters[k]));
        sizes[k] = mons[k]->size;
    }
    // SetSizePattern: the first pattern of those sizes (none: 0); a medium with smalls swaps its
    // first two; the boss m310 its own
    out.pattern = 0;
    for (int p = 0; p < 13; ++p)
        if (kPatterns[p].size[0] == sizes[0] && kPatterns[p].size[1] == sizes[1] && kPatterns[p].size[2] == sizes[2]) {
            out.pattern = p;
            break;
        }
    if (out.pattern == 4 || out.pattern == 5) std::swap(mons[0], mons[1]);
    if (mons[0] && mons[0]->figure == "m310") out.pattern = 12;
    const Pattern& pat = kPatterns[out.pattern];
    out.patternName = pat.start;
    out.startCamera = std::string("Cam_Start_") + pat.start + "_00";
    out.camera = std::string("Cam_Getset_") + pat.getset + "_00";
    for (int k = 0; k < 3; ++k) {
        if (!mons[k]) continue;
        BattleSetup::Unit u;
        u.id = mons[k]->id;
        u.figure = mons[k]->figure.substr(0, 4); // (BtlMain::CreateMonster: four characters)
        u.colour = mons[k]->figure.size() > 4 ? mons[k]->figure.substr(4, 1) : std::string();
        u.position = pat.dpos[k] >= 0 ? kDpos[pat.dpos[k]] : "";
        u.idle = "bb01";
        out.monsters.push_back(u);
    }
    // the party: a pair side by side, one alone in the middle
    const int fighters = (party[0] > 0) + (party[1] > 0);
    for (int k = 0; k < 2; ++k) {
        if (party[k] <= 0) continue;
        const BattleData::Character* ch = data.character(party[k]);
        if (!ch) return fail(error, "no character " + std::to_string(party[k]));
        BattleSetup::Unit u;
        u.id = ch->id;
        u.figure = ch->figure;
        u.position = fighters == 1 ? "dPos_pSoloC" : k == 0 ? "dPos_pPairA" : "dPos_pPairB";
        u.idle = "bb02"; // (StepInit: motion 1, then 0 looping)
        out.party.push_back(u);
    }
    return true;
}

} // namespace cr2
