#pragma once
// The story's state the scripts read and write - the save data's flags and work values - and what
// they have asked for next (cr3_script_work: FldUtilSet* / Reserve*), which the engine acts on
// when the script running has finished (FldMain::BranchAfterEvent, VistaMain::BranchAfterVista).

#include <map>
#include <string>
#include <vector>

namespace cr2 {

class ScriptVM;

struct GameState {
    std::vector<bool> flags; // Cr3Flag: Set / Clear / Check
    std::map<int, int> work; // Cr3Work: Set / Get
    // the party (cr3_common_data): the two fighting now (+3, +4: characters from 0 - Darwin 0,
    // Orlando 1, Marie 2 ...) and three pairs kept (+5..+10) - a new game's
    // (Cr3InitWorkFromBattleData): Darwin and Marie, the pairs 0 2, 1 3, 4 5
    int party[2] = {0, 2};
    int partySets[3][2] = {{0, 2}, {1, 3}, {4, 5}};
    // the sopia's levels and EXP (cr3_common_data +0x26c, a byte each, by the sopia's number; +0x28c,
    // 32 integers) - what Cr3Battle.SetParamI2 / GetParamI2 (0 / 1) read and set. A new game's
    // (Cr3InitWorkFromBattleData): each sopia's starting level (initSopia)
    // the items (cr3_common_data +0xc: a count by the item's number, 0..299) and the Oz (+0x264): a
    // new game's from the party's record (initItems)
    short itemCount[300] = {};
    int oz = 0;
    bool itemsReady = false;
    template <class Data> void initItems(const Data& data) {
        if (itemsReady) return;
        itemsReady = true;
        const auto& p = data.party();
        for (int k = 0; k < 5; ++k)
            if (p.items[k] > 0 && p.items[k] < 300) itemCount[p.items[k]] = static_cast<short>(p.counts[k]);
        oz = p.oz;
    }
    // the characters' equipment (cr3_player_data +0x3c, 0x4c bytes a character from 0): weapon,
    // armour, accessory, then the five sopia - a new game's the battle data's (initEquip);
    // Cr3System.SetEquip / GetEquip
    // and their levels and EXP (cr3_player_data +0, +4): a new game's the records' levels, with
    // the EXP of that level (the battle data's kLevelExp, `levelExp`)
    short equip[9][8] = {};
    int level[9] = {}, exp[9] = {};
    bool equipReady = false;
    template <class Data> void initEquip(const Data& data, const int* levelExp) {
        if (equipReady) return;
        equipReady = true;
        for (int c = 0; c < 9; ++c)
            if (const auto* ch = data.character(c + 1)) {
                for (int e = 0; e < 8; ++e) equip[c][e] = static_cast<short>(ch->equip[e]);
                level[c] = ch->level, exp[c] = ch->level >= 1 && ch->level <= 200 ? levelExp[ch->level - 1] : 0;
            }
    }
    signed char sopiaLevel[32] = {};
    int sopiaExp[32] = {};
    bool sopiaReady = false;
    template <class Data> void initSopia(const Data& data) {
        if (sopiaReady) return;
        sopiaReady = true;
        for (int k = 0; k < 32; ++k) {
            const auto* s = k < data.sopias() ? data.sopia(k) : nullptr;
            sopiaLevel[k] = static_cast<signed char>(s ? s->startLevel : 1);
        }
    }
    bool flag(int n) const { return n >= 0 && size_t(n) < flags.size() && flags[size_t(n)]; }
    void setFlag(int n, bool on) {
        if (n < 0) return;
        if (size_t(n) >= flags.size()) flags.resize(size_t(n) + 1);
        flags[size_t(n)] = on;
    }

    // What the scripts reserved (FldUtil's work): a vista event (SetVistaFuncName /
    // SetVistaMapName), a battle (BattleIN, the pair SetBattleParty set), an area to go to
    // (SetPopPoint: the pop point, its map its first five letters), the title; and how the field
    // is entered next (FldMain::Init's mode: SetComeFromBattleToField 1 - evt100, ...FromVista...
    // 2 - evt200; else evt000)
    struct Next {
        bool vista = false;
        std::string vistaFunction, vistaMap = "null";
        int battle = -1;
        int battleParty[2] = {-1, -1};
        std::string popPoint;
        bool title = false;
        int fieldMode = -1; // -1: evt000
    } next;
};

// the natives on the story's state (Cr3Flag, Cr3Work) and the reservations (Cr3System's
// SetVistaFuncName / SetVistaMapName, BattleIN / SetBattleParty, SetPopPoint, SetComeFrom*), for
// any script
void bindGameState(ScriptVM& vm, GameState& state);

} // namespace cr2
