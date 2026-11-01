// A cache kept within a limit: past it, the entries least lately used (their `used` stamps) go
#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace cr2 {

template <class Map, class Free>
void trimLeastUsed(Map& cache, size_t limit, Free free) {
    if (cache.size() <= limit) return;
    std::vector<typename Map::iterator> entries;
    entries.reserve(cache.size());
    for (auto it = cache.begin(); it != cache.end(); ++it) entries.push_back(it);
    const size_t drop = cache.size() - limit * 3 / 4; // (a batch: not a search at each new entry)
    std::nth_element(entries.begin(), entries.begin() + std::ptrdiff_t(drop), entries.end(),
                     [](const auto& a, const auto& b) { return a->second.used < b->second.used; });
    for (size_t i = 0; i < drop; ++i) {
        free(entries[i]->second);
        cache.erase(entries[i]);
    }
}

} // namespace cr2
