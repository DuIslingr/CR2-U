# Chaos Rings II: the battles' rules

What the Android library (`libChaosRings2.so`, armv7 with symbols) does in a battle, as far as
it has been read. The tables are `data\battledata.dat` (FORMATS.md, Battles); the stage - map,
places, cameras - is FORMATS.md's too. This is the rules: who has what, how much a command does.

## The fighters (BtlWork)

BtlWork holds five fighters' records, 0x4c8 bytes each from +0xb00: 0 and 1 the party, 2..4 the
monsters (BtlUtilStatus::IsPlayerId / IsEnemyId).

| Offset | |
|---|---|
| +0xb00 | s16: present (0 none) |
| +0xb02 | s8: the partner fighting with it (-1 none) |
| +0xb08 | s32: maximum HP |
| +0xb10 | s32: HP |
| +0xb14 | s16: MP |
| +0xb16 | s16 x 5: the parameters (GetCharaParam's n) |
| +0xb2c | its figure's name; +0xb34 its model |
| +0xf9c | u8 per element: its resistance (DamageResist: 1, 2 the table at 0x535b48; else x1) |
| +0xfba | u8: its element |

The parameters: 0 Attack, 1 Magic (also magic defence: two thirds of it), 2 Defense, 3 Speed
(the turn order), 4 a fifth. States scale them (GetCharaParam: up x 1.25 for its turns - states
5..9 -, down - states 13..17).

A character's (BtlData): HP = +0x10 + growth(+0x21) x level x 50 / 100 (1 .. 99999); MP from
+0x14; parameter n = +0x16 + 2n + growth(+0x23 + n) x level x 5 / 100 (0 .. 9999). Darwin at level
0: HP 450, MP 80, Attack 255, Magic 180, Defense 110, 100, 100. A monster's: HP = +0x1c + 25 a
level; parameter n = +0x20 + 2n + level x a table's (0x535d90; 0x535d84 for the other kind).

## Commands (0x7c each)

| Offset | |
|---|---|
| +0x04 | its name (u32 offsets a language, as everywhere) |
| +0x0c | its help |
| +0x44 | s32: power (Slash 150) |
| +0x49 | u8: 0 physical, 1 magic |
| +0x4a | u8: the spread, +- percent (Slash 5) |
| +0x4c | u8: its element (0 none, 1 fire, 2, 3, 4, 5) |
| +0x4e | u16: flags (2: the defence not counted) |
| +0x6f | u8: what it does (0 damage, 1 a state, 5 a share of HP, 7, 8, 12 down to 1 HP, 16, 17, 32 ...) |

## Damage (BtlCalc::Damage = ceil(Damage3(Damage2(Damage1))))

- Damage1: (the attacker's Attack - Magic for magic - plus its partner's in a pair action) x
  (power + CommandY) / 100 - the target's Defense (magic: two thirds of its Magic; none with flag
  2). CommandY: +100 in a turn-break bonus, +25 a pair action, +50 a monster attacking, +50 (+100
  with a pair skill) at a target whose guard is broken. (Kind 7: (max HP - HP) x (power +
  CommandY) / 100 - 1.5 x the target's Magic.)
- Damage2: x the elements (DamageAttribute): the attack's element against the target's, a
  cycle - 1 on 3, 2 on 1, 3 on 2 x 1.5 (the target "weak"); 1 on 2, 2 on 3, 3 on 1 x 0.75 - x 1.25
  for the pair skills of that element or kind, x the target's resistances (DamageResist).
- Damage3: kinds 5 (power % of the target's HP), 12 (to 1 HP), 32 and 8 by HP; else x the spread
  ((100 - s) + rand(2s + 1)) / 100; x 0.5 at a guarding target (state 4); x 0.5 in a turn break's
  bonus for the target; x 0.1 at armour without anti-armour; x 1.5 with pair skill 10; x 2 with
  state 35 (spent).

The turn order (OrderPoint): Speed (a pair: the two's mean), +10000 a command with special 7,
+5000 with the skill "ahead", -5000 for kind 4.
