#pragma once
// The story's state the scripts read and write - the save data's flags and work values - and what
// they have asked for next (cr3_script_work: FldUtilSet* / Reserve*), which the engine acts on
// when the script running has finished (FldMain::BranchAfterEvent, VistaMain::BranchAfterVista).

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "field/treasure.hpp"

namespace cr2 {

class ScriptVM;

struct GameState {
    std::vector<bool> flags; // Cr3Flag: Set / Clear / Check
    std::map<int, int> work; // Cr3Work: Set / Get
    // the party (cr3_common_data): the two fighting now (+3, +4: characters from 0 - Darwin 0,
    // Orlando 1, Marie 2 ...) and three pairs kept (+5..+10) - a new game's
    // (Cr3InitWorkFromBattleData): Darwin and Marie, the pairs 0 2, 1 3, 4 5
    int party[2] = {0, 2};
    // the field's leader of the two (cr3_field_data +8: FldUtilGetActiveCharacter, 0 / 1), the
    // character change allowed (+0x32: Cr3ChangeCharacter.SetEnable - its button) and the tab
    // (+0x33: Cr3TabMenu.SetEnable) - a new game's both (Cr3InitFieldData)
    int active = 0;
    bool changeEnable = true, tabEnable = true;
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
    // their MP between battles (cr3_player_data +0x10; -1 the most: MPFullRecovery, a new game)
    int mp[9] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};
    // what items have added to their parameters for good (cr3_player_data +0x1e..: the seeds,
    // Cr3UtilParameterUpByItem)
    short seed[9][5] = {};
    // cr3_global_work (Cr3UtilGlobalWorkGet / Set: 32 values - 7 the pub's request taken)
    int globalWork[32] = {};
    // the difficulty (cr3_common_data[0]: Cr3UtilSetDifficulty, the world select's level - 1) and the
    // dungeon's treasure boxes (cr3_treasureBox)
    int difficulty = 0;
    // the dungeon the world select chose last (FldWorldSelect::m_nSelectDungeonNum: GetSelectDungeonNum)
    int selectDungeon = 0;
    int selectChapter = -1; // (the chapter select's pick: InterfaceWindow.GetChapterID - not saved)
    // the library's shelf (InterfaceWindow.SetArchiveID: InterfaceMain +0xd8, its category) and the
    // entry being read (GetLibraryID: ArchiveWindowMenu +0x2c) - not saved
    int libraryArchive = 0, libraryId = 0;
    TreasureBoxes boxes;
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
    // which are had (cr3_common_data +0x268, a bit by number: Cr3System.SopiaGet / SopiaCheck) - a
    // new game's Darwin's (1); a new game's EXP that of the starting level (its growth's table,
    // `expAt`: battle/battledata.hpp's sopiaExp)
    uint32_t sopiaOwned = 0;
    bool sopiaReady = false;
    template <class Data> void initSopia(const Data& data, int (*expAt)(bool, int) = nullptr) {
        if (sopiaReady) return;
        sopiaReady = true;
        for (int k = 0; k < 32; ++k) {
            const auto* s = k < data.sopias() ? data.sopia(k) : nullptr;
            sopiaLevel[k] = static_cast<signed char>(s ? s->startLevel : 1);
            sopiaExp[k] = s && expAt ? expAt(s->growth != 0, s->startLevel) : 0;
        }
        sopiaOwned = 1u << 1;
    }
    // CR2-U's own save (not the game's format - cr3_savedata_game): "CR2-U save 1", then a line a
    // value ("flags 3 17 ...", "item 1 5", "equip 0 81 201 ..."); `extra` the caller's own lines
    // (the map, the place) - load gives back the lines it doesn't know there
    std::string save(const std::map<std::string, std::string>& extra = {}) const;
    bool load(const std::string& text, std::map<std::string, std::string>* extra = nullptr, std::string* error = nullptr);
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
        bool worldSelect = false; // (ReserveWorldSelect: cr3_script_work[4] - FldMain step 10)
        bool shop = false;        // (ReserveShop - FldMain step 0xb)
        bool party = false;       // (ReserveParty - FldMain step 0xc)
        bool chapter = false;     // (ReserveChapter - FldMain step 0xe)
        bool library = false;     // (ReserveLibrary - FldMain step 0xf)
    } next;
};

// the natives on the story's state (Cr3Flag, Cr3Work) and the reservations (Cr3System's
// SetVistaFuncName / SetVistaMapName, BattleIN / SetBattleParty, SetPopPoint, SetComeFrom*), for
// any script
void bindGameState(ScriptVM& vm, GameState& state);

} // namespace cr2
