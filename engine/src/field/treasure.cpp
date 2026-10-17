#include "field/treasure.hpp"

#include <algorithm>
#include <iterator>
#include <numeric>
#include <vector>

namespace cr2 {

namespace {

#include "field/boxtable.inc"

constexpr int kTables = int(std::size(kBoxItems));

} // namespace

void TreasureBoxes::choose(int difficulty, const int party[2], uint32_t (*random)()) {
    *this = TreasureBoxes();
    table = std::clamp(difficulty * 3 + int(random() % 3), 0, kTables - 1); // (choiceTable)
    // choiceBox: `chosen` of the `n` at random (Cr3UtilGenerateUniqueRandNumber's order: the first ones)
    auto pick = [&](int kind, int chosen) {
        const int n = count(kind);
        std::vector<int> order(static_cast<size_t>(n));
        std::iota(order.begin(), order.end(), 0);
        for (int k = n - 1; k > 0; --k) std::swap(order[size_t(k)], order[random() % uint32_t(k + 1)]);
        for (int k = 0; k < chosen; ++k) set[kind][order[size_t(k)]] = true;
    };
    auto unsetEmpty = [&](int kind) { // (unSet: in order, those whose item is none)
        for (int k = 0; k < 3; ++k)
            if (item(kind, k, party) == -1) set[kind][k] = false;
    };
    pick(0, 7);
    pick(1, 2), unsetEmpty(1);
    pick(2, 3), unsetEmpty(2);
    if (party[1] < 0) set[2][2] = false;
}

int TreasureBoxes::item(int kind, int n, const int party[2]) const {
    if (!isSet(kind, n)) return -1;
    int pos = -1; // (getTablePos: the place among those set up to it)
    for (int k = 0; k <= n; ++k) pos += set[kind][k];
    const int t = std::clamp(table, 0, kTables - 1);
    if (kind == 0) return pos >= 0 && pos < 7 ? kBoxItems[t][pos] : -1;
    if (kind == 1) return pos >= 0 && pos < 2 ? kBoxItems[t][7 + pos] : -1;
    if (pos == 0) return kBoxItems[t][9];
    if (pos == 1 || pos == 2) {
        const int c = party[pos - 1];
        return c >= 0 && c < 7 ? kBoxCharaItems[t][c] : -1;
    }
    return 1;
}

} // namespace cr2
