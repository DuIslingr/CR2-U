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
        int hp = 0, mp = 0, param[5] = {0, 0, 0, 0, 0}; // +0x10, +0x14, +0x16 at level 0
        int hpGrowth = 0, mpGrowth = 0, paramGrowth[5] = {0, 0, 0, 0, 0}; // +0x21, +0x22, +0x23
        int level = 1;       // +0x20 a new game's
        int equip[8] = {0, 0, 0, 0, 0, 0, 0, 0}; // +0x28 a new game's: weapon, armour, accessory, then
                                                 // its five sopia (+0x2e..: Cr3InitWorkFromBattleData)
        int specials[3] = {0, 0, 0}; // +0x38 its own skills (Darwin: Executioner ...)
        int attack = 0, pairAttack = 0; // +0x44 the command "attack" gives, +0x46 the pair's
    };
    // a monster (GetMonsterDataPointer, from 1)
    struct Monster {
        int id = 0;
        std::string name[2];
        std::string figure; // +0x10: the figure's four characters and its colour ("m206a": m206,
                            // the images m206a01...)
        int hp = 0;         // +0x1c (GetMonsterHP: + 25 a level)
        int param[5] = {0, 0, 0, 0, 0}; // +0x20 (GetMonsterParam: + 7, 6, 4, 6, 6 a level)
        int level = 0;      // +0x3b its own (0: the party's, BtlWork::SetupEnemy)
        int resist[7] = {0, 0, 0, 0, 0, 0, 0}; // +0x3c: elements 1..5, physical, magic (1 weak, 2 resists)
        int stateResist[5] = {0, 0, 0, 0, 0};  // +0x43
        uint32_t skills = 0; // +0x48 (SetupEnemySkill: bit 0 armour, 1 first, ...)
        int element = 0;    // +0x50
        int size = 0;       // +0x53: 0 small, 1 medium, 2 large, 3 extra large, 4 human-sized
        // its choice (BtlEnemy): ten commands, each with a condition, a target rule, a weight, a
        // must-take flag and the condition's value
        int commands[10] = {}, condition[10] = {}, targetRule[10] = {}, weight[10] = {}, must[10] = {}, conditionValue[10] = {};
    };
    // a command (GetCommandDataPointer, from 1)
    struct Command {
        int id = 0;
        std::string name[2];
        std::string motion;  // +0x38 ("bo01")
        int power = 0;       // +0x44
        int magic = 0;       // +0x49: 0 physical, 1 magic
        int spread = 0;      // +0x4a: +- percent
        int awaken = 0;      // +0x48: 1..3 an awakening of that level, 4 an advent (they fill no charge)
        int target = 0;      // +0x4b: 1 itself, 2 an ally, 3 the allies, 4 a foe, 6 the foes, 7 anyone
        int element = 0;     // +0x4c
        unsigned specials = 0; // +0x4e (CheckCommandSpecial's bits)
        int kind = 0;        // +0x6f: 0 damage, 2 defend, ...
        int mp = 0;          // +0x42: its MP (BtlUtilStatus::CalcUseMp)
        int icon = 0;        // +0x72: its icon (mainIcon_id's frame)
    };
    // a sopia (GetSopiaDataPointer, from 1; 0x2c bytes): the command it gives at each level (+0x18,
    // s16 each: the first `level` of them), the level it starts at (+0x12), its level-up table
    // (+0x10: 0 the characters' own, 1 the others')
    struct Sopia {
        int id = 0;
        std::string name[2];
        int growth = 0, startLevel = 1;
        int commands[10] = {};
    };
    // an item (GetItemDataPointer, from 1): what it adds worn (Cr3UpdatePlayerData)
    struct Item {
        int id = 0;
        std::string name[2];
        int type = 0;        // +0x10
        int hp = 0, mp = 0, param[5] = {0, 0, 0, 0, 0}; // +0x14, +0x18, +0x1a
        int skills[2] = {-1, -1}; // +0x30, +0x32: auto skills it gives
        bool battle = false;  // +0x11: usable in battle (BtlCommand::GetItem)
        bool usedUp = false;  // +0x2d: one gone each use
        int command = 0;      // +0x2e: the command it is
        int icon = 0;         // +0x34: its icon (mainIcon_id's frame)
    };
    // the party's record (GetPartyDataPointer, the first): a new game's five items and their
    // counts (+0, +0xa), its Oz (+0x14) - Cr3InitWorkFromBattleData
    struct Party {
        int items[5] = {}, counts[5] = {};
        int oz = 0;
    };
    const Party& party() const { return party_; }
    // an auto skill (GetAutoSkillDataPointer, from 1; BtlWork::SetupSkill)
    struct AutoSkill {
        int id = 0;
        int resist[7] = {}, stateResist[5] = {}; // +0x17, +0x1e
        unsigned groups[7] = {}; // +0x26..+0x32: the fighter's skill bits (+0xfac..+0xfb8)
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
    const Command* command(int id) const;
    const Item* item(int id) const;
    const AutoSkill* autoSkill(int id) const;
    const Sopia* sopia(int id) const;
    int sopias() const { return int(sopias_.size()); }
    int couplings() const { return int(couplings_.size()); }

private:
    std::vector<Character> characters_;
    std::vector<Monster> monsters_;
    std::vector<Coupling> couplings_;
    std::vector<Command> commands_;
    std::vector<Item> items_;
    std::vector<AutoSkill> autoSkills_;
    std::vector<Sopia> sopias_;
    Party party_;
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
