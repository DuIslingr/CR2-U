#pragma once
// The game's own saves (docs/SCRIPTS.md, "The game's saves"): game.dat / cont.dat - 0x910 bytes,
// Cr3Util_Commit_GameData's cr3_savedata_game - and system.dat (0xb8, the flags 0..991 and the
// options), each a CRC-16 / CCITT and version 0x3f6 first. Read into a GameState and back.
//
// What CR2-U keeps of them: the flags, the global work, the party and its pairs, the items and
// the Oz, the sopia (had, levels, EXP), each character's level, EXP, MP, equipment and seeds, the
// difficulty, the treasure boxes, the field's switches, the map and the place, the play time.
// Written back: those, with the HP, MP and parameters at their most (the caller's `stats`); the
// stats, the achievements and the player records' +0x28..+0x3b as zeros.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "script/gamestate.hpp"

namespace cr2 {

struct GameSavePlace {
    std::string map = "d1020"; // (cr3_field_data +0 / +4: "t01" + the area + 1, else "d" + the dungeon + it)
    float position[3] = {0, 0, 0};
    float heading = 0; // (+0x20: a turn about y)
    uint32_t playFrames = 0; // (cr3_game_data +0: of 0.05 s)
};

constexpr size_t kGameSaveSize = 0x910, kSystemSaveSize = 0xb8;
constexpr uint16_t kSaveVersion = 0x3f6;
// SAVEDATA_crc: CRC-16 (0x1021, from 0xffff, inverted)
uint16_t saveCrc(const uint8_t* data, size_t size);

// game.dat / cont.dat into `g` (its flags past 1000, the rest of its state) and `place`; false:
// not one (its size, its version). `crcOk` says whether its CRC holds.
bool readGameSave(const std::vector<uint8_t>& d, GameState& g, GameSavePlace& place, bool* crcOk = nullptr,
                  std::string* error = nullptr);
// a character's most HP, MP and parameters (from 0) - for the records written
using SaveStats = std::function<void(int character, int& maxHp, int& maxMp, int param[5])>;
std::vector<uint8_t> writeGameSave(const GameState& g, const GameSavePlace& place, const SaveStats& stats);

// system.dat: the flags 0..991 into `g`; its options (cr3_system_data, 6 words) as they are
bool readSystemSave(const std::vector<uint8_t>& d, GameState& g, uint32_t options[6], bool* crcOk = nullptr,
                    std::string* error = nullptr);
std::vector<uint8_t> writeSystemSave(const GameState& g, const uint32_t options[6]);

// a map's name from the field data's dungeon and area, and back (false: not such a name)
std::string saveMapName(int dungeon, int area);
bool saveMapPlace(const std::string& map, int& dungeon, int& area);

} // namespace cr2
