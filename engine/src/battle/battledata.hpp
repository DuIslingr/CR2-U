#pragma once
// The battles' tables (`data\battledata.dat`; BtlData, docs/FORMATS.md): a header of table offsets
// and counts, then the records - the characters (0x98 bytes), the party (0x18), the monsters
// (0x110), the encounters ("couplings", 0x1c: what BattleIN(id) fights), the areas, the commands,
// ... - and the strings. A record's names are offsets into the strings, one a language (the
// option's: 0 Japanese, 1 English, ...).

#include <cstdint>
#include <string>
#include <vector>

namespace cr2 {

class BattleData {
public:
    bool parse(const std::vector<uint8_t>& file, std::string* error = nullptr);

    // a character (BtlData::GetCharaDataPointer, from 1): Darwin 1, Orlando 2, Marie 3, ...
    struct Character {
        int id = 0;
        std::string name[2]; // (Japanese, English)
        std::string figure;  // +0x08 its battle figure ("j001")
    };
    // a monster (GetMonsterDataPointer, from 1)
    struct Monster {
        int id = 0;
        std::string name[2];
        std::string figure; // +0x10: the figure's four characters and its colour ("m206a": m206,
                            // the images m206a01...)
        int hp = 0;         // +0x1c (GetMonsterHP: + 25 a level)
        int size = 0;       // +0x53: 0 small, 1 medium, 2 large, 3 extra large, 4 human-sized
    };
    // an encounter (GetCouplingDataPointer, from 1; FldEncount::SetBattle)
    struct Coupling {
        int id = 0;
        int music = 0;       // +0x00 (cr3_battle_post +2)
        int map = 0;         // +0x02 the battle map, from 1 (BtlMapTable)
        bool flag = false;   // +0x04 (BtlWork +0x18)
        int monsters[3] = {0, 0, 0}; // +0x08 (0: none)
    };

    const Character* character(int id) const;
    const Monster* monster(int id) const;
    const Coupling* coupling(int id) const;
    int couplings() const { return int(couplings_.size()); }

private:
    std::vector<Character> characters_;
    std::vector<Monster> monsters_;
    std::vector<Coupling> couplings_;
};

// A battle's set-up (BtlMainCtrl::StepInit, BtlWork::SetStartWork): the map, the monsters' size
// pattern (BtlWork::SetSizePattern, BtlCouplingTable) and so where each stands (a battledpos
// joint: BtlModel::SetPosRot), the party's places, the camera the commands are chosen under
struct BattleSetup {
    int coupling = 0;
    std::string map;     // BtlMapTable: "d0151b", ...
    int pattern = -1;    // 0 S, 1 SS, 2 SSS, 3 M, 4 MS, 5 MSS, 6 MM, 7 L, 8 XL, 9 XLXL, 10 Hsolo,
                         // 11 Hpair, 12 the boss m310's own (neron)
    std::string patternName;
    struct Unit {
        int id = 0;          // the character's / monster's
        std::string figure;  // "j001", "m206"
        std::string colour;  // the monster's images' letter ("a"), "" its own
        std::string position; // the battledpos joint ("dPos_pPairA", "dPos_sssL")
        std::string idle;    // its motion as the battle starts (BtlMotionTable: bb02, then bb01)
    };
    std::vector<Unit> party, monsters;
    std::string camera;      // "Cam_Getset_SSS_00"
    std::string startCamera; // "Cam_Start_SSS_00"
};

// `party`: the characters fighting (cr3_common_data 1, 2; 0 none)
bool setupBattle(const BattleData& data, int coupling, const int party[2], BattleSetup& out, std::string* error = nullptr);

} // namespace cr2
