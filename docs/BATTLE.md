# Chaos Rings II: the battles' rules

What the Android library (`libChaosRings2.so`, armv7 with symbols) does in a battle, as far as
it has been read. The tables are `data\battledata.dat` (FORMATS.md, Battles); the stage - map,
places, cameras - is FORMATS.md's too. This is the rules: who has what, how much a command does.

## The fighters (BtlWork)

BtlWork holds five fighters' records, 0x4c8 bytes each from +0xb00: 0 and 1 the party, 2..4 the
monsters (BtlUtilStatus::IsPlayerId / IsEnemyId).

| Offset | |
|---|---|
| +0xb00 | s16: 1 alive (BtlUtilStatus::IsAlive), else down or absent |
| +0xb02 | s8: the partner fighting with it (-1: solo) - a pair action needs both alive and neither in state 3 |
| +0xb08 | s32: maximum HP |
| +0xb10 | s32: HP |
| +0xb14 | s16: MP |
| +0xb16 | s16 x 5: the parameters (GetCharaParam's n) |
| +0xb2c | its figure's name; +0xb34 its model |
| +0xf58.. | a command's result on it: +0xf58 struck, +0xf5c the damage to take (BtlActionCtrl::Damage subtracts it), +0xf78 hit, +0xf79 critical |
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
| +0x4b | u8: its targets (1 itself, 2 a monster, 3 every monster, 4 one of the party, 6 the party - both of a pair -, 7 anyone) |
| +0x4c | u8: its element (0 none, 1, 2, 3 the cycle below, 4, 5) |
| +0x4e | u16: its "specials" (BtlUtilStatus::CheckCommandSpecial, with the fighter's own extra bits): 0 never misses, 1 the defence not counted, 2 pierces armour, 5 the fighter's element, 6, 7 first, 8 can be critical, 9 always critical |
| +0x38 | its motion (`bb01`, `bx01` ...) |
| +0x6f | u8: what it does (0 damage, 1 a state, 5 a share of HP, 7, 8, 12 down to 1 HP, 16, 17, 32 ...) |

## Damage (BtlCalc::Damage = ceil(Damage3(Damage2(Damage1))))

- Damage1: (the attacker's Attack - Magic for magic - plus its partner's in a pair action) x
  (power + CommandY) / 100 - the target's Defense (magic: two thirds of its Magic; none with flag
  2). CommandY: +100 in a turn-break bonus, +25 a pair action, +50 a monster attacking, +50 (+100
  with pair skill 3) when it is a critical (the target's +0xf79). (Kind 7: (max HP - HP) x (power +
  CommandY) / 100 - 1.5 x the target's Magic.)
- Damage2: x the elements (DamageAttribute): the attack's element against the target's, a
  cycle - 1 on 3, 2 on 1, 3 on 2 x 1.5 (the target "weak"); 1 on 2, 2 on 3, 3 on 1 x 0.75 - x 1.25
  for the pair skills of that element or kind, x the target's resistances (DamageResist).
- Damage3: kinds 5 (power % of the target's HP), 12 (to 1 HP), 32 and 8 by HP; else x the spread
  ((100 - s) + rand(2s + 1)) / 100; x 0.5 at a guarding target (state 4); x 0.5 in a turn break's
  bonus for the target; x 0.1 at armour without anti-armour; x 1.5 with pair skill 10; x 2 with
  state 35 (spent).

- Hit (BtlCalc::Hit): magic always (but a dodging skill's); special 0 always; else by the
  attacker's Speed (plus its partner's in a pair action) less the target's fifth parameter: up to
  -101 60%, -50 70%, -1 80%, 49 90%, 100 95%, more 99% (kind 15 75%; blinded - state 1 - half).
- Critical (BtlCalc::Critical), only with special 8 (magic: with pair skill 4): special 9 always;
  else by the fifth parameters' difference (each side a pair's mean), the same steps to 5, 10, 15,
  20, 25, 50 - times 100 / 256 - percent; +10 with pair skill 2, +50 in state 11 (spent). A
  critical adds 50 (100 with pair skill 3) to the command's power (CommandY).

BtlActionCtrl::CalcDamage(attacker, target): the hit, then (if it hit) the auto-guard and blocks,
the critical, the damage into +0xf5c, the states; BtlActionCtrl::Damage takes it off the HP.

## The turn (BtlMainCtrl)

Steps: 1 the commands (BtlCommand: solo or pair, then each command), 2 their order, 3 the actions
(BtlActionCtrl), then - the party all down: 5 game over; the monsters all down: 6 the result -
else 4 the turn's end (BtlTurnEnd: the states run down) and the turn count (+0x10) on, back to 1.

## The monsters' choice (BtlEnemy)

A monster record holds ten commands (+0x58 s16), each with a condition (+0x6c u8), a target rule
(+0x76), a weight (+0x80) and a "must" flag (+0x8a), and per-condition values (+0x94 s16 ...).
SetActionCommand: of the usable commands (not kind 4, not special bit 7 of +0x4e's 0x80, not used
already where it may be once, ...) whose condition holds, the last with "must" set; else one by
weight (BtlRand over the weights' sum). Conditions: 0 always, 11 the turn (+0x10, from 0) + 1
equals its value, ... (Cond0..Cond16). Targets by the command's +0x4b; a single one by the rule
(Target0: one of the party at random; 2 a monster; 7 anyone), then CheckProvoke (a provoking
fighter draws it). The Mantis: Lockjaw, always, at one of the party. The prologue's boss (m310):
three commands on turns given by its +0x94 values.

The turn order (OrderPoint): Speed (a pair: the two's mean), +10000 a command with special 7,
+5000 with the skill "ahead", -5000 for kind 4.
