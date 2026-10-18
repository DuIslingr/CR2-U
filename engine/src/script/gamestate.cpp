#include "script/gamestate.hpp"

#include <algorithm>

#include <sstream>

#include "script/vm.hpp"

namespace cr2 {

void bindGameState(ScriptVM& vm, GameState& state) {
    vm.setNative("Cr3Flag", "Set", [&state](NativeCall& c) { state.setFlag(c.integer(0), true); });
    vm.setNative("Cr3Flag", "Clear", [&state](NativeCall& c) { state.setFlag(c.integer(0), false); });
    vm.setNative("Cr3Flag", "Check", [&state](NativeCall& c) { c.result(state.flag(c.integer(0))); });
    // (Cr3Work is the global work: Cr3BindWork's Set / Get are Cr3UtilGlobalWorkSet / Get - 32
    // values, others ignored, read as 0)
    vm.setNative("Cr3Work", "Set", [&state](NativeCall& c) {
        const int k = c.integer(0);
        if (k >= 0 && k < 32) state.globalWork[k] = c.integer(1);
    });
    vm.setNative("Cr3Work", "Get", [&state](NativeCall& c) {
        const int k = c.integer(0);
        c.result(k >= 0 && k < 32 ? state.globalWork[k] : 0);
    });
    GameState::Next& next = state.next;
    // FldUtilSetVistaFuncName: the event, and that a vista is to come; its map ("null": none)
    vm.setNative("Cr3System", "SetVistaFuncName", [&next](NativeCall& c) { next.vistaFunction = c.string(0), next.vista = true; });
    vm.setNative("Cr3System", "SetVistaMapName", [&next](NativeCall& c) { next.vistaMap = c.string(0); });
    vm.setNative("Cr3System", "BattleIN", [&next](NativeCall& c) { next.battle = c.integer(0); }); // (FldUtilReserveBattleIN)
    vm.setNative("Cr3System", "SetBattleParty", [&next](NativeCall& c) {
        const int k = c.integer(0);
        if (k == 0 || k == 1) next.battleParty[k] = c.integer(1, -1);
    });
    // the party (Cr3UtilSetActivePartyMember / Get..., SetPartyMember, CopyPartyMember)
    vm.setNative("Cr3System", "SetActivePartyMember", [&state](NativeCall& c) {
        const int k = c.integer(0);
        if (k == 0 || k == 1) state.party[k] = c.integer(1, -1);
    });
    vm.setNative("Cr3System", "GetActivePartyMember", [&state](NativeCall& c) {
        const int k = c.integer(0);
        c.result(k == 0 || k == 1 ? state.party[k] : -1);
    });
    vm.setNative("Cr3System", "SetPartyMember", [&state](NativeCall& c) {
        const int s = c.integer(0), k = c.integer(1);
        if (s >= 0 && s < 3 && (k == 0 || k == 1)) state.partySets[s][k] = c.integer(2, -1);
    });
    vm.setNative("Cr3System", "CopyPartyMember", [&state](NativeCall& c) {
        const int s = c.integer(0);
        if (s >= 0 && s < 3) state.party[0] = state.partySets[s][0], state.party[1] = state.partySets[s][1];
    });
    // Cr3UtilSetCharaEquip / GetCharaEquip: the character, the slot (0 weapon, 1 armour, 2
    // accessory, 6 the second sopia, 3..5 the third to fifth - the first not by a script), the id
    static const int kEquipSlot[7] = {0, 1, 2, 5, 6, 7, 4};
    vm.setNative("Cr3System", "SetEquip", [&state](NativeCall& c) {
        const int ch = c.integer(0), s = c.integer(1);
        if (ch >= 0 && ch < 9 && s >= 0 && s < 7) state.equip[ch][kEquipSlot[s]] = static_cast<short>(c.integer(2));
    });
    vm.setNative("Cr3System", "GetEquip", [&state](NativeCall& c) {
        const int ch = c.integer(0), s = c.integer(1);
        c.result(ch >= 0 && ch < 9 && s >= 0 && s < 7 ? int(state.equip[ch][kEquipSlot[s]]) : 0);
    });
    // Cr3UtilGetSopia (had, at level 1, no EXP) / CheckSopia / GetSopiaLv
    vm.setNative("Cr3System", "SopiaGet", [&state](NativeCall& c) {
        const int s = c.integer(0);
        if (s >= 0 && s < 32) state.sopiaOwned |= 1u << s, state.sopiaLevel[s] = 1, state.sopiaExp[s] = 0;
    });
    vm.setNative("Cr3System", "SopiaCheck", [&state](NativeCall& c) {
        const int s = c.integer(0);
        c.result(s >= 0 && s < 32 && (state.sopiaOwned >> s & 1) != 0);
    });
    vm.setNative("Cr3System", "GetSopiaLv", [&state](NativeCall& c) {
        const int s = c.integer(0);
        c.result(s >= 0 && s < 32 ? int(state.sopiaLevel[s]) : 0);
    });
    // Cr3UtilGetItem(item, n): its count n more, 0..99; Cr3UtilCheckItem: its count
    vm.setNative("Cr3System", "ItemGet", [&state](NativeCall& c) {
        const int id = c.integer(0);
        if (id > 0 && id < 300) state.itemCount[id] = static_cast<short>(std::clamp(state.itemCount[id] + c.integer(1, 1), 0, 99));
    });
    vm.setNative("Cr3System", "ItemCheck", [&state](NativeCall& c) {
        const int id = c.integer(0);
        c.result(id > 0 && id < 300 ? int(state.itemCount[id]) : 0);
    });
    vm.setNative("Cr3System", "MPFullRecovery", [&state](NativeCall&) {
        for (int& m : state.mp) m = -1;
    });
    vm.setNative("Cr3System", "SetPopPoint", [&next](NativeCall& c) { next.popPoint = c.string(0); }); // (FldUtilSetPopPoint)
    vm.setNative("Cr3System", "SetComeFromBattleToField", [&next](NativeCall&) { next.fieldMode = 1; });
    vm.setNative("Cr3System", "ReserveWorldSelect", [&next](NativeCall&) { next.worldSelect = true; }); // (FldUtilReserveWorldSelect)
    vm.setNative("Cr3System", "ReserveShop", [&next](NativeCall&) { next.shop = true; });
    vm.setNative("Cr3System", "ReserveParty", [&next](NativeCall&) { next.party = true; });
    vm.setNative("Cr3System", "ReserveChapter", [&next](NativeCall&) { next.chapter = true; });
    vm.setNative("InterfaceWindow", "GetChapterID", [&state](NativeCall& c) { c.result(state.selectChapter); }); // (CR3InterfaceEvent_GetSelectChapterID)
    vm.setNative("Cr3System", "GetDungeonNum", [&state](NativeCall& c) { c.result(state.dungeon); });
    vm.setNative("Cr3System", "GetAreaNum", [&state](NativeCall& c) { c.result(state.area); });
    // the character change (FldChangeCharacter): its button allowed, the leader, a change at once
    // (FldMain::ChangeCharacterQuick: the other of the two, unless it leads already); the tab
    vm.setNative("Cr3ChangeCharacter", "SetEnable", [&state](NativeCall& c) { state.changeEnable = c.boolean(0); });
    vm.setNative("Cr3TabMenu", "SetEnable", [&state](NativeCall& c) { state.tabEnable = c.boolean(0); }); // (FldUtilSetTabMenuEnable)
    vm.setNative("Cr3TabMenu", "GetEnable", [&state](NativeCall& c) { c.result(state.tabEnable); });
    vm.setNative("Cr3ChangeCharacter", "GetEnable", [&state](NativeCall& c) { c.result(state.changeEnable); });
    vm.setNative("Cr3ChangeCharacter", "GetActive", [&state](NativeCall& c) { c.result(state.active); });
    vm.setNative("Cr3ChangeCharacter", "ExeQuick", [&state](NativeCall& c) {
        if (c.integer(0) != state.active) state.active = state.active == 0 ? 1 : 0;
    });
    vm.setNative("Cr3System", "SetBGColor", [&state](NativeCall& c) {
        for (int k = 0; k < 3; ++k) state.bgColor[k] = c.number(k);
    });
    vm.setNative("Cr3System", "ReserveLibrary", [&next](NativeCall&) { next.library = true; });
    vm.setNative("Cr3System", "ReserveCamp", [&next](NativeCall&) { next.camp = true; });
    vm.setNative("Cr3System", "ReservePUB", [&next](NativeCall&) { next.pub = true; });
    vm.setNative("Cr3System", "ReserveTitle", [&next](NativeCall&) { next.title = true; });
    vm.setNative("Cr3System", "ChangeDungeon", [&next](NativeCall& c) { next.changeDungeon = c.integer(0), next.changeArea = c.integer(1); });
    vm.setNative("Cr3System", "ChangeArea", [&next, &state](NativeCall& c) { next.changeDungeon = state.dungeon, next.changeArea = c.integer(0); });
    // the Oz (Cr3UtilSetOZ: held at 0 .. 9,999,999) and the encounters' switch
    vm.setNative("Cr3System", "OZSet", [&state](NativeCall& c) { state.oz = std::clamp(c.integer(0), 0, 9999999); });
    vm.setNative("Cr3System", "OZCheck", [&state](NativeCall& c) { c.result(state.oz); });
    vm.setNative("Cr3System", "SetEncountEnable", [&state](NativeCall& c) { state.encountEnable = c.boolean(0); });
    vm.setNative("Cr3System", "ReserveCredit", [&next](NativeCall& c) { next.credit = c.integer(0); }); // (FldUtilReserveCredit)
    vm.setNative("InterfaceWindow", "SetArchiveID", [&state](NativeCall& c) { state.libraryArchive = c.integer(0); });
    vm.setNative("InterfaceWindow", "GetLibraryID", [&state](NativeCall& c) { c.result(state.libraryId); });
    vm.setNative("Cr3System", "SetComeFromVistaToField", [&next](NativeCall&) { next.fieldMode = 2; });
}

} // namespace cr2

namespace cr2 {

std::string GameState::save(const std::map<std::string, std::string>& extra) const {
    std::ostringstream o;
    o << "CR2-U save 1\n";
    o << "flags";
    for (size_t k = 0; k < flags.size(); ++k)
        if (flags[k]) o << ' ' << k;
    o << '\n';
    o << "party " << party[0] << ' ' << party[1] << '\n';
    o << "active " << active << ' ' << int(changeEnable) << ' ' << int(tabEnable) << '\n';
    for (int s = 0; s < 3; ++s) o << "pairs " << s << ' ' << partySets[s][0] << ' ' << partySets[s][1] << '\n';
    for (int id = 1; id < 300; ++id)
        if (itemCount[id]) o << "item " << id << ' ' << itemCount[id] << '\n';
    o << "oz " << oz << '\n';
    for (int c = 0; c < 9; ++c) {
        o << "chara " << c << ' ' << level[c] << ' ' << exp[c] << ' ' << mp[c];
        for (int e = 0; e < 8; ++e) o << ' ' << equip[c][e];
        for (int k = 0; k < 5; ++k) o << ' ' << seed[c][k];
        o << '\n';
    }
    for (int s = 0; s < 32; ++s) o << "sopia " << s << ' ' << int(sopiaLevel[s]) << ' ' << sopiaExp[s] << '\n';
    o << "owned " << sopiaOwned << '\n';
    o << "difficulty " << difficulty << '\n';
    o << "select " << selectDungeon << '\n';
    o << "boxes " << boxes.table;
    for (int k = 0; k < 3; ++k)
        for (int n = 0; n < 10; ++n) o << ' ' << int(boxes.set[k][n]) * 1 + int(boxes.open[k][n]) * 2;
    o << '\n';
    o << "global";
    for (int v : globalWork) o << ' ' << v;
    o << '\n';
    for (const auto& [k, v] : extra) o << k << ' ' << v << '\n';
    return o.str();
}

bool GameState::load(const std::string& text, std::map<std::string, std::string>* extra, std::string* error) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line != "CR2-U save 1") {
        if (error) *error = "not a CR2-U save";
        return false;
    }
    flags.clear();
    std::vector<std::pair<int, int>> oldWork;
    for (short& n : itemCount) n = 0;
    while (std::getline(in, line)) {
        std::istringstream l(line);
        std::string key;
        if (!(l >> key)) continue;
        if (key == "flags") {
            for (int n; l >> n;) setFlag(n, true);
        } else if (key == "work") {
            int k = 0, v = 0;
            if (l >> k >> v && k >= 0 && k < 32) oldWork.push_back({k, v}); // (an older save's: the same values, set after)
        } else if (key == "party") {
            l >> party[0] >> party[1];
        } else if (key == "active") {
            int e = 1, t = 1;
            l >> active >> e >> t;
            active = active == 1 ? 1 : 0, changeEnable = e != 0, tabEnable = t != 0;
        } else if (key == "pairs") {
            int s = -1;
            if (l >> s && s >= 0 && s < 3) l >> partySets[s][0] >> partySets[s][1];
        } else if (key == "item") {
            int id = 0, n = 0;
            if (l >> id >> n && id > 0 && id < 300) itemCount[id] = static_cast<short>(n);
        } else if (key == "oz") {
            l >> oz;
        } else if (key == "chara") {
            int c = -1;
            if (!(l >> c) || c < 0 || c >= 9) continue;
            l >> level[c] >> exp[c] >> mp[c];
            for (short& e : equip[c]) l >> e;
            for (short& k : seed[c]) l >> k;
        } else if (key == "sopia") {
            int s = -1, lv = 0, e = 0;
            if (l >> s >> lv >> e && s >= 0 && s < 32) sopiaLevel[s] = static_cast<signed char>(lv), sopiaExp[s] = e;
        } else if (key == "owned") {
            l >> sopiaOwned;
        } else if (key == "select") {
            l >> selectDungeon;
        } else if (key == "difficulty") {
            l >> difficulty;
        } else if (key == "boxes") {
            l >> boxes.table;
            for (int k = 0; k < 3; ++k)
                for (int n = 0; n < 10; ++n) {
                    int v = 0;
                    l >> v;
                    boxes.set[k][n] = v & 1, boxes.open[k][n] = (v & 2) != 0;
                }
        } else if (key == "global") {
            for (int& v : globalWork) l >> v;
        } else if (extra) {
            std::string rest;
            std::getline(l >> std::ws, rest);
            (*extra)[key] = rest;
        }
    }
    for (const auto& [k, v] : oldWork) globalWork[k] = v;
    itemsReady = equipReady = sopiaReady = true; // (a new game's set-up not over it)
    return true;
}

} // namespace cr2
