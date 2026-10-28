#include "script/gamesave.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace cr2 {

namespace {

// the save buffer's places (Cr3Util_Commit_GameData)
constexpr size_t kGameData = 0x10, kCommon = 0x34, kPlayers = 0x340, kPlayerSize = 0x4c, kField = 0x5f0, kFlags = 0x630,
                 kFlagBytes = 500, kGlobal = 0x824, kTreasure = 0x8a4;
constexpr size_t kSystemFlags = 4, kSystemFlagBytes = 0x7c, kSystemOptions = 0x80;

uint32_t rd32(const std::vector<uint8_t>& d, size_t o) {
    return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24;
}
int16_t rd16(const std::vector<uint8_t>& d, size_t o) { return int16_t(d[o] | d[o + 1] << 8); }
float rdf(const std::vector<uint8_t>& d, size_t o) {
    const uint32_t u = rd32(d, o);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
void wr32(std::vector<uint8_t>& d, size_t o, uint32_t v) {
    for (int k = 0; k < 4; ++k) d[o + size_t(k)] = uint8_t(v >> (8 * k));
}
void wr16(std::vector<uint8_t>& d, size_t o, int v) { d[o] = uint8_t(v), d[o + 1] = uint8_t(uint32_t(v) >> 8); }
void wrf(std::vector<uint8_t>& d, size_t o, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    wr32(d, o, u);
}

bool checkHeader(const std::vector<uint8_t>& d, size_t size, bool* crcOk, std::string* error) {
    if (d.size() != size) {
        if (error) *error = "not its size (" + std::to_string(d.size()) + " bytes)";
        return false;
    }
    if (uint16_t(rd16(d, 2)) != kSaveVersion) {
        if (error) *error = "not its version";
        return false;
    }
    if (crcOk) *crcOk = uint16_t(rd16(d, 0)) == saveCrc(d.data() + 2, size - 2);
    return true;
}

void seal(std::vector<uint8_t>& d) {
    wr16(d, 2, kSaveVersion);
    wr16(d, 0, saveCrc(d.data() + 2, d.size() - 2));
}

} // namespace

uint16_t saveCrc(const uint8_t* data, size_t size) {
    uint16_t table[256];
    for (int i = 0; i < 256; ++i) {
        uint16_t v = uint16_t(i << 8);
        for (int b = 0; b < 8; ++b) v = uint16_t(v & 0x8000 ? (v << 1) ^ 0x1021 : v << 1);
        table[i] = v;
    }
    uint16_t c = 0xffff;
    for (size_t k = 0; k < size; ++k) c = uint16_t(table[(data[k] ^ (c >> 8)) & 0xff] ^ (c << 8));
    return uint16_t(~c);
}

std::string saveMapName(int dungeon, int area) {
    char n[16];
    if (dungeon == 0) std::snprintf(n, sizeof n, "t01%02d", area + 1);
    else std::snprintf(n, sizeof n, "d%02d%02d", dungeon, area + 1);
    return n;
}

bool saveMapPlace(const std::string& map, int& dungeon, int& area) {
    int a = 0, b = 0;
    if (map.size() != 5 || std::sscanf(map.c_str() + 1, "%2d%2d", &a, &b) != 2 || b < 1) return false;
    if (map[0] == 't' && a == 1) return dungeon = 0, area = b - 1, true;
    if (map[0] == 'd' && a >= 1) return dungeon = a, area = b - 1, true;
    return false;
}

bool readGameSave(const std::vector<uint8_t>& d, GameState& g, GameSavePlace& place, bool* crcOk, std::string* error) {
    if (!checkHeader(d, kGameSaveSize, crcOk, error)) return false;
    place.playFrames = rd32(d, kGameData);
    // cr3_common_data
    const size_t c = kCommon;
    g.difficulty = d[c];
    g.party[0] = int8_t(d[c + 3]), g.party[1] = int8_t(d[c + 4]);
    for (int s = 0; s < 3; ++s) g.partySets[s][0] = int8_t(d[c + 5 + size_t(s) * 2]), g.partySets[s][1] = int8_t(d[c + 6 + size_t(s) * 2]);
    for (int id = 0; id < 300; ++id) g.itemCount[id] = rd16(d, c + 0xc + size_t(id) * 2);
    g.oz = int32_t(rd32(d, c + 0x264));
    g.sopiaOwned = rd32(d, c + 0x268);
    for (int s = 0; s < 32; ++s) g.sopiaLevel[s] = int8_t(d[c + 0x26c + size_t(s)]), g.sopiaExp[s] = int32_t(rd32(d, c + 0x28c + size_t(s) * 4));
    // cr3_player_data
    for (int ch = 0; ch < 9; ++ch) {
        const size_t p = kPlayers + size_t(ch) * kPlayerSize;
        g.level[ch] = int32_t(rd32(d, p)), g.exp[ch] = int32_t(rd32(d, p + 4));
        const int mp = rd16(d, p + 0x10), most = rd16(d, p + 0x12);
        g.mp[ch] = mp >= most ? -1 : mp; // (CR2-U's -1: the most)
        for (int k = 0; k < 5; ++k) g.seed[ch][k] = rd16(d, p + 0x1e + size_t(k) * 2);
        for (int e = 0; e < 8; ++e) g.equip[ch][e] = rd16(d, p + 0x3c + size_t(e) * 2);
    }
    // cr3_field_data
    const size_t f = kField;
    place.map = saveMapName(int32_t(rd32(d, f)), int32_t(rd32(d, f + 4)));
    saveMapPlace(place.map, g.dungeon, g.area);
    g.active = rd32(d, f + 8) == 1 ? 1 : 0;
    for (int k = 0; k < 3; ++k) place.position[k] = rdf(d, f + 0x10 + size_t(k) * 4);
    const float qy = rdf(d, f + 0x24), qw = rdf(d, f + 0x2c); // (the turn: a quaternion about y)
    place.heading = 2.0f * std::atan2(qy, qw);
    g.encountEnable = d[f + 0x30] != 0;
    g.changeEnable = d[f + 0x32] != 0, g.tabEnable = d[f + 0x33] != 0;
    // the flags past 1000 (those below are system.dat's: kept as they are)
    for (int n = 1001; n < int(kFlagBytes) * 8; ++n) g.setFlag(n, (d[kFlags + size_t(n >> 3)] >> (n & 7)) & 1);
    for (int k = 0; k < 32; ++k) g.globalWork[k] = int32_t(rd32(d, kGlobal + size_t(k) * 4));
    // cr3_treasureBox: the table, then a set / opened pair a box (ten, three, three)
    g.boxes.table = int32_t(rd32(d, kTreasure));
    for (int kind = 0; kind < 3; ++kind)
        for (int n = 0; n < TreasureBoxes::count(kind); ++n) {
            const size_t o = kTreasure + (kind == 0 ? 4 + size_t(n) * 2 : kind == 1 ? 0x18 + size_t(n) * 2 : 0x1e + size_t(n) * 2);
            g.boxes.set[kind][n] = d[o] != 0, g.boxes.open[kind][n] = d[o + 1] != 0;
        }
    g.itemsReady = g.equipReady = g.sopiaReady = true;
    return true;
}

std::vector<uint8_t> writeGameSave(const GameState& g, const GameSavePlace& place, const SaveStats& stats) {
    std::vector<uint8_t> d(kGameSaveSize, 0);
    wr32(d, kGameData, place.playFrames);
    const size_t c = kCommon;
    d[c] = uint8_t(g.difficulty);
    d[c + 1] = uint8_t(g.party[0]), d[c + 2] = uint8_t(g.party[1]); // (the battle's pair: the party's)
    d[c + 3] = uint8_t(g.party[0]), d[c + 4] = uint8_t(g.party[1]);
    for (int s = 0; s < 3; ++s) d[c + 5 + size_t(s) * 2] = uint8_t(g.partySets[s][0]), d[c + 6 + size_t(s) * 2] = uint8_t(g.partySets[s][1]);
    for (int id = 0; id < 300; ++id) wr16(d, c + 0xc + size_t(id) * 2, g.itemCount[id]);
    wr32(d, c + 0x264, uint32_t(g.oz));
    wr32(d, c + 0x268, g.sopiaOwned);
    for (int s = 0; s < 32; ++s) d[c + 0x26c + size_t(s)] = uint8_t(g.sopiaLevel[s]), wr32(d, c + 0x28c + size_t(s) * 4, uint32_t(g.sopiaExp[s]));
    for (int ch = 0; ch < 9; ++ch) {
        const size_t p = kPlayers + size_t(ch) * kPlayerSize;
        int hp = 1, mp = 0, param[5] = {0, 0, 0, 0, 0};
        if (stats) stats(ch, hp, mp, param);
        wr32(d, p, uint32_t(g.level[ch])), wr32(d, p + 4, uint32_t(g.exp[ch]));
        wr32(d, p + 8, uint32_t(hp)), wr32(d, p + 0xc, uint32_t(hp)); // (the HP back to the most after a battle)
        wr16(d, p + 0x10, g.mp[ch] < 0 ? mp : std::min(g.mp[ch], mp)), wr16(d, p + 0x12, mp);
        for (int k = 0; k < 5; ++k) wr16(d, p + 0x14 + size_t(k) * 2, param[k]), wr16(d, p + 0x1e + size_t(k) * 2, g.seed[ch][k]);
        for (int e = 0; e < 8; ++e) wr16(d, p + 0x3c + size_t(e) * 2, g.equip[ch][e]);
    }
    const size_t f = kField;
    int dungeon = 10, area = 19;
    saveMapPlace(place.map, dungeon, area);
    wr32(d, f, uint32_t(dungeon)), wr32(d, f + 4, uint32_t(area));
    wr32(d, f + 8, uint32_t(g.active));
    wr32(d, f + 0xc, 0xfffffffeu); // (Commit_GameData's: entered from a save)
    for (int k = 0; k < 3; ++k) wrf(d, f + 0x10 + size_t(k) * 4, place.position[k]);
    wrf(d, f + 0x24, std::sin(place.heading * 0.5f)), wrf(d, f + 0x2c, std::cos(place.heading * 0.5f));
    d[f + 0x30] = g.encountEnable, d[f + 0x31] = 1, d[f + 0x32] = g.changeEnable, d[f + 0x33] = g.tabEnable;
    for (int n = 1001; n < int(kFlagBytes) * 8; ++n)
        if (g.flag(n)) d[kFlags + size_t(n >> 3)] |= uint8_t(1u << (n & 7));
    for (int k = 0; k < 32; ++k) wr32(d, kGlobal + size_t(k) * 4, uint32_t(g.globalWork[k]));
    wr32(d, kTreasure, uint32_t(g.boxes.table));
    for (int kind = 0; kind < 3; ++kind)
        for (int n = 0; n < TreasureBoxes::count(kind); ++n) {
            const size_t o = kTreasure + (kind == 0 ? 4 + size_t(n) * 2 : kind == 1 ? 0x18 + size_t(n) * 2 : 0x1e + size_t(n) * 2);
            d[o] = g.boxes.set[kind][n], d[o + 1] = g.boxes.open[kind][n];
        }
    seal(d);
    return d;
}

bool readSystemSave(const std::vector<uint8_t>& d, GameState& g, uint32_t options[6], bool* crcOk, std::string* error) {
    // Vita omits the final 0x20-byte achievement block; flags/options have the same offsets.
    if (!checkHeader(d, d.size() == 0x98 ? 0x98 : kSystemSaveSize, crcOk, error)) return false;
    for (int n = 0; n < int(kSystemFlagBytes) * 8; ++n) g.setFlag(n, (d[kSystemFlags + size_t(n >> 3)] >> (n & 7)) & 1);
    for (int k = 0; k < 6; ++k) options[k] = rd32(d, kSystemOptions + size_t(k) * 4);
    return true;
}

std::vector<uint8_t> writeSystemSave(const GameState& g, const uint32_t options[6]) {
    std::vector<uint8_t> d(kSystemSaveSize, 0);
    for (int n = 0; n < int(kSystemFlagBytes) * 8; ++n)
        if (g.flag(n)) d[kSystemFlags + size_t(n >> 3)] |= uint8_t(1u << (n & 7));
    for (int k = 0; k < 6; ++k) wr32(d, kSystemOptions + size_t(k) * 4, options[k]);
    seal(d);
    return d;
}

} // namespace cr2
