#include "assets/maptable.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace cr2 {

std::string MapInfo::layer(const char* kind) const {
    if (sharedBackground && std::strcmp(kind, "bg") == 0) return "d5051f2_hdbg";
    return name + (f2() ? "f2" : "f1") + "_hd" + kind;
}

bool findMapTable(const std::vector<uint8_t>& d, std::vector<MapInfo>& out) {
    out.clear();
    static const char kFirst[] = "t0101", kSecond[] = "t0102";
    size_t at = 0;
    for (; at + 0x20 <= d.size(); at += 4) // (records are word-aligned)
        if (std::memcmp(&d[at], kFirst, 6) == 0 && std::memcmp(&d[at + 0x18], kSecond, 6) == 0) break;
    if (at + 0x20 > d.size()) return false;
    auto i32 = [&](size_t p) { int32_t v; std::memcpy(&v, &d[p], 4); return v; };
    for (; at + 0x18 <= d.size(); at += 0x18) {
        const char* n = reinterpret_cast<const char*>(&d[at]);
        const size_t len = strnlen(n, 8);
        if (len < 4 || len == 8 || !std::isalpha(static_cast<unsigned char>(n[0]))) break;
        MapInfo m;
        m.name.assign(n, len);
        m.variant = i32(at + 8);
        m.midground = d[at + 0xc] != 0;
        m.foreground = d[at + 0xd] != 0;
        m.foregroundShift = i32(at + 0x10);
        m.figure = d[at + 0x14] != 0;
        m.sharedBackground = d[at + 0x15] != 0;
        m.noBackground = d[at + 0x16] != 0;
        m.figure2 = d[at + 0x17] != 0;
        out.push_back(std::move(m));
    }
    return !out.empty();
}

const EncountCell* EncountTable::at(const std::string& map) const {
    if (map.size() < 5 || (map[0] != 't' && map[0] != 'd')) return nullptr;
    const int row = map[0] == 't' ? 0 : std::atoi(map.substr(1, 2).c_str()), area = std::atoi(map.substr(3, 2).c_str()) - 1;
    const size_t i = size_t(row) * 48 + size_t(area);
    if (row < 0 || row >= 14 || area < 0 || area >= 48 || i >= cells.size() || cells[i].area <= 0) return nullptr;
    return &cells[i];
}

bool findEncountTable(const std::vector<uint8_t>& d, EncountTable& out) {
    out.cells.clear();
    constexpr size_t kTable = 14 * 48 * 4; // (each table's bytes)
    auto i32 = [&](size_t p) { int32_t v; std::memcpy(&v, &d[p], 4); return v; };
    for (size_t at = 0; at + 3 * kTable <= d.size(); at += 4) {
        bool town = true;
        for (size_t k = 0; k < 16 && town; ++k)
            town = i32(at + k * 4) == -1 && i32(at + 2 * kTable + k * 4) == 20 && i32(at + kTable + k * 4) == 0;
        if (!town || i32(at + 16 * 4) != 0 || i32(at + 48 * 4) == 0) continue; // (the row's end; d01's first area)
        for (size_t k = 0; k < 14 * 48; ++k) out.cells.push_back({i32(at + k * 4), i32(at + kTable + k * 4), i32(at + 2 * kTable + k * 4)});
        return true;
    }
    return false;
}

std::vector<std::string> mapTableCandidates(const std::string& archivePath) {
    const size_t slash = archivePath.find_last_of("/\\");
    const std::string dir = slash == std::string::npos ? "." : archivePath.substr(0, slash);
    std::vector<std::string> out = {dir + "/ChaosRings2enHD"};
    const size_t up = dir.find_last_of("/\\");
    if (up != std::string::npos) {
        const std::string parent = dir.substr(0, up);
        const size_t up2 = parent.find_last_of("/\\");
        if (up2 != std::string::npos) { // <app>/CR2/resources/: the Prequel Trilogy's CR2.self, else the game's eboot.bin - each
            // decrypted (<name>.elf: the first start's setup's, from an encrypted one) first
            for (const char* n : {"/CR2.elf", "/CR2.self", "/eboot.elf", "/eboot.bin"}) out.push_back(parent.substr(0, up2) + n);
        }
    }
    return out;
}

} // namespace cr2
