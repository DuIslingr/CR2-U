#pragma once
// The treasure boxes (Cr3TreasureBox; docs/SCRIPTS.md, "Treasure boxes"): which of a dungeon's
// boxes hold something and what, drawn again each time a dungeon is entered (FldMain::
// ChangeDungeonFunc, not with flag 0x7fb). Three kinds by the locator's name: 0 tbox (ten, seven
// of them filled), 1 obox (the key boxes: three, two - opened with the unlocking), 2 fbox (three:
// one fixed, one for each of the party). The contents: tools/gen_box_table.py -> boxtable.inc.

#include <cstdint>

namespace cr2 {

struct TreasureBoxes {
    int table = 0; // (the difficulty x 3 + 0..2)
    bool set[3][10] = {}, open[3][10] = {};

    // Cr3TreasureBox::Choice: `difficulty` cr3_common_data[0], `party` the two (from 0; -1 none);
    // `random` a source of numbers (lrand48's)
    void choose(int difficulty, const int party[2], uint32_t (*random)());
    // GetItemID: the box's item (-1 empty, or not one of those placed)
    int item(int kind, int n, const int party[2]) const;
    bool isSet(int kind, int n) const { return valid(kind, n) && set[kind][n]; }
    bool isOpen(int kind, int n) const { return valid(kind, n) && open[kind][n]; }
    void markOpen(int kind, int n) {
        if (valid(kind, n)) open[kind][n] = true;
    }
    static int count(int kind) { return kind == 0 ? 10 : 3; }

private:
    static bool valid(int kind, int n) { return kind >= 0 && kind < 3 && n >= 0 && n < count(kind); }
};

} // namespace cr2
