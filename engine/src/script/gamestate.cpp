#include "script/gamestate.hpp"

#include "script/vm.hpp"

namespace cr2 {

void bindGameState(ScriptVM& vm, GameState& state) {
    vm.setNative("Cr3Flag", "Set", [&state](NativeCall& c) { state.setFlag(c.integer(0), true); });
    vm.setNative("Cr3Flag", "Clear", [&state](NativeCall& c) { state.setFlag(c.integer(0), false); });
    vm.setNative("Cr3Flag", "Check", [&state](NativeCall& c) { c.result(state.flag(c.integer(0))); });
    vm.setNative("Cr3Work", "Set", [&state](NativeCall& c) { state.work[c.integer(0)] = c.integer(1); });
    vm.setNative("Cr3Work", "Get", [&state](NativeCall& c) {
        const auto it = state.work.find(c.integer(0));
        c.result(it == state.work.end() ? 0 : it->second);
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
    vm.setNative("Cr3System", "SetPopPoint", [&next](NativeCall& c) { next.popPoint = c.string(0); }); // (FldUtilSetPopPoint)
    vm.setNative("Cr3System", "SetComeFromBattleToField", [&next](NativeCall&) { next.fieldMode = 1; });
    vm.setNative("Cr3System", "SetComeFromVistaToField", [&next](NativeCall&) { next.fieldMode = 2; });
}

} // namespace cr2
