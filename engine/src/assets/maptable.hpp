#pragma once
// The field maps' table (FldMap::ChangeArea; docs/FORMATS.md): 225 records of 0x18 bytes in the
// game's executable, not its data - read from your copy's binary (the iPad's executable beside
// its archive, the Vita's eboot.bin, Android's libChaosRings2.so). Each release has its own.

#include <cstdint>
#include <string>
#include <vector>

namespace cr2 {

struct MapInfo {
    std::string name;          // +0x00 "t0101", "d0101", ...
    int variant = 0;           // +0x08 the painting: 0 "f1" at half scale, 1024 x 1024 reference
                               //       units; else "f2" - bit 1: 2048 wide, bit 0: 2048 tall
    bool midground = false;    // +0x0c a <map>..._hdmg layer
    bool foreground = false;   // +0x0d a <map>..._hdfg layer
    int foregroundShift = 0;   // +0x10 1: the foreground moves at twice the scroll (one map)
    bool figure = false;       // +0x14 a 3D figure <map>f drawn with the characters
    bool sharedBackground = false; // +0x15 the painting is d5051f2_hdbg (a sky, at 0.4 of the scroll)
    bool noBackground = false; // +0x16 no painting: a 3D map
    bool figure2 = false;      // +0x17 (the 3D figure too)

    bool f2() const { return variant != 0; }
    float sceneScale() const { return variant ? 1.0f : 0.5f; } // the painting's (and scene's) scale
    // a layer's image ("bg", "fg", "mg"): images\<name>.img
    std::string layer(const char* kind) const;
};

// The table in an executable (Mach-O, ELF, the Vita's SELF): found by its first two records.
// False: not there.
bool findMapTable(const std::vector<uint8_t>& binary, std::vector<MapInfo>& out);

// A field map's encounters (FldEncount::SetData: three tables of 14 rows - the town, then d01 ..
// d13 - by 48 areas, a map's row and area its name's: t0101 0 / 0, d0903 9 / 2): the battle data's
// area its couplings are drawn from (-1 none), the steps (0.17 of walking each) between draws, the
// chance of one in 100 (5 more after each miss)
struct EncountCell {
    int area = -1, steps = 0, chance = 0;
};
struct EncountTable {
    std::vector<EncountCell> cells; // (row * 48 + area)
    const EncountCell* at(const std::string& map) const; // (null: none for that map)
};
// The tables in an executable: found by the town's row (16 areas of -1, their chance 20)
bool findEncountTable(const std::vector<uint8_t>& binary, EncountTable& out);

// The binaries an archive's release keeps the table in, looked for beside it: the iPad's
// executable (ChaosRings2enHD in the .app), the Vita's eboot.bin (two folders up)
std::vector<std::string> mapTableCandidates(const std::string& archivePath);

} // namespace cr2
