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

The party's come from the save's player records (cr3_player_data, 0x4c each: +0 level, +8 HP,
+0xc max HP, +0x10 MP, +0x12 max MP, +0x14 the five parameters, +0x3c weapon, +0x3e armour,
+0x40 accessory, +0x42.. sopia) - Cr3UpdatePlayerData: the level's values, plus bonuses, plus what
the weapon, armour and accessory add (an item: +0x14 HP, +0x18 MP, +0x1a five parameters, +0x30 /
+0x32 auto skills; 0x38 bytes) - and a new game's from the characters' records (+0x20 the level,
+0x28 the equipment; Cr3InitWorkFromBattleData). Darwin starts at level 1 with a Bronze Longsword
and Familiar Light Armor: HP 515, MP 86; Orlando at 5: HP 830, MP 142 - as the reference video's
first battle shows them. "Attack" is the character's +0x44 command (Darwin: Slash), solo or in a
pair alike (BtlCommand::StepCommandLoop).

The scripts change the equipment (Cr3System.SetEquip / GetEquip, Cr3UtilSetCharaEquip: the
character from 0, the slot - 0 weapon, 1 armour, 2 accessory, 6 the second sopia, 3..5 the third
to fifth -, the id): the prologue gives Darwin the sopia 30 and 31 for the opening battle (221)
and takes them back after it, so in the forest (222) he has no element. A fighter's element
(BtlWork::SetupPlayerSkill, +0xfba) is that of the first of its five sopia with one (a sopia's
+0x16: Orlando's 2 wind), else none; each sopia gives its auto skills (+0x22, one a level) up to
its level, and those that lend an element (an auto skill's +0x34, GetAutoSkillAttach) set it in
its place if they agree on one.

A monster's level is its own (+0x3b), else near the party's mean (BtlWork::SetupEnemy): within
the dungeon level's band (cr3_common_data[0] d: d x 10 + 1 .. (d + 1) x 10), from mean - 5 to mean
+ 2 at random. The coupling's flag lifts the limits on its HP and parameters.

BtlRand(n) is lrand48() % n.

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
| +0x4b | u8: its targets (from the user's side: 1 itself, 2 an ally, 3 every ally, 4 a foe - and its partner, a pair being struck together -, 6 every foe, 7 anyone) |
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

## The break gauge

BtlWork +0x1e, -4 .. 4 ("Even" at 0): after each struck target (BtlActionCtrl::UpdateBreakGauge)
a point each for a weakness hit (the element cycle's x 1.5: +0xf7d), a critical (+0xf79), the
target downed (+0xf7c, CheckDead) and kind 27's hits - none when the target guards or blocked,
negative on oneself - up for the party's actions, down for the monsters'. Reaching 4 or -4 makes
a turn break come (+0x1f 3); at the turn's end (BtlTurnEnd) it begins (+0x1f 1 the party's, 2 the
monsters'), and at the next turn's end it is over, the gauge back to 0. While it lasts the gauge
doesn't move, the breaking side's commands get +100 power (CommandY) and take half damage
(Damage3; also its blocks, counters and auto-guard, BadStatus).

## The charge gauge

BtlWork +0x28, 0..300 (Cr3Battle.Charge adds to it, at most 300). BtlActionCtrl::UpdateChargeGauge
fills it for each fighter struck (BtlActionCtrl::Damage: the first target on a hit, the others
each time): the party striking a monster 15 a hit, 30 a critical, none on one guarding (state 4);
the party struck by what it took this action (+0xf70, the damage added up) - half its maximum HP
or more 30, over a fifth 20, else 15, half that guarding; state 12 (the pair's or its own) x 1.5
once (BtlCalcValue(n, 50)), the state's count down by one. A party's command with +0x48 set - 1..3
the awakenings (Darwin: Executioner 121, Collbrande 122, Godslayer 123), 4 the advents (Gale
Roar ...) - fills none. A battle starts at 0, or 100 when the first of the party standing has skill
(5, 4) (BtlWork::CheckChargeSkill). On the screen: `chggauge` (fit 7, by the break gauge: its ring)
posed at min(charge, 300) / 10 / 20 s - 1.5 s full (BreakWindowMenu::Update), played there forwards
or back.

The awakenings (BtlCommand::CheckCommandAwake / SetCommandAwakeList / StepAwakeLoop; three tables
in the library, tools/gen_awake_table.py -> engine/src/battle/awaketable.inc): each character
(from 0) has up to three awakening commands, each behind a story flag (9999: none) - Darwin's
Executioner 121 (flag 3001), Collbrande 122 (3002), Godslayer 123 (3003); the eighth row, 121 and
123 without flags. Its cost by the command's +0x48: level 1 100, 2 200, 3 300. The command menu
shows Awake (+0x3e) when one of the party has one whose flag is set and whose cost the charge less
what this turn has set aside (+0x2c: the first's choice) covers; the list (the select menu's
buttons: its name and level) offers the character's, those it can't pay for not selectable; one
on a single foe goes on to its target. The charge is spent as it acts (ActionAwakeStandby) -
not with the script's switch 42 on: the prologue (221) sets it, and its Battle_Start the flags
3001 / 3002 for the length of the battle.

The advents (BtlCommand::CheckCommandAdvent / CheckCommandAdventAvailable / SetCommandAdventList /
StepAdventLoop; four more tables beside the awakenings', the same tool): in a pair's turn, with the
charge less what is set aside at 100 or more (CheckCommand), the menu shows Advent (+0x3f) in
place of Awake. Each of the six is offered when its story flag (3016 .. 3021) is set, a sopia
either of the pair wears in its first two places (cr3_player_data +0x42 / +0x44) carries its
number (the sopia's +0x14: Gale Roar's 136 .. Divine Punishment's 141 - sopia 2 .. 7), and the
charge is at its least (100; the sixth 300). Its command goes by the charge level, min(charge /
100, 3): 136 / 391 / 392, 137 / 393 / 394, 138 / 395 / 396, 139 / 397 / 398, 140 / 399 / 400, and the
sixth's -1 / -1 / 141 (a -1 shown, not selectable). The list: the command's name, the level its
number, its +0x72 the icon (the tab 13 "Advent", the mark 13 "Charge Lv."). An advent takes all
the charge (ActionAdventStandby: +0x28 to 0, again not with switch 42).

The sopia (BtlCommand::GetSopia / GetPairSopia / SetCommandSopiaList / StepSopiaLoop): a sopia
record (0x2c bytes, GetSopiaDataPointer; the header's +0x1c, count +0x40) gives a command at each
level from +0x18 (s16, 0 where a level gives none: Marie's Banish at 1, Healing Hand at 3); +0x12
the level it starts at, +0x10 which level-up table. A character carries five (cr3_player_data
+0x42..+0x4a; a new game's from its record's +0x2e..+0x36 - Darwin's own and the prologue's OP1
and OP2, at level 5), their levels and EXP in the save (cr3_common_data +0x26c by the sopia's
number, +0x28c - what Cr3Battle.SetParamI2 / GetParamI2 read and set). The list offers each
command once, the pair's both lists; one is selectable when the MP covers it (CalcUseMp: half with
skill (5, 3), half again as much with (0, 10), either of a pair's - both pay it), the revivals
(kinds 24, 25) only with the partner down. The MP is taken as it acts (ActionCommonCommandStart),
the action lost short of it. Its target: a foe (4, 7), one of the party (2: the revivals' one
down), none (1 itself, 3 the allies, 6 the foes).

The restores (BtlCalc::Restore): kinds 18 / 19 HP / MP ceil(spread(Magic - the pair's both - x
(power + CommandY) / 100)), 20 / 21 HP / MP the power, 22 / 25 all the HP, 23 all of both, 24 a
revival with (CommandY + 50) % of the HP (1 .. 100); x 1.5 for a command with MP and skill (0, 10),
x 2 with (0, 9) but for 22, 23, 25. Their numbers on the screen: set 50 (green) HP, 60 MP.

The items (BtlCommand::GetItem / SetCommandItemList / StepItemLoop): a count by the item's
number, 0..299 (cr3_common_data +0xc; the battle's own copy at BtlWork +0x52); a new game's from
the party's record (the header's +4, 0x18 bytes: five items at +0, their counts at +0xa, the Oz
at +0x14 - Chocolate x 5, Vin Brulee x 1). An item for battles (+0x11) is its command (+0x2e:
Chocolate 11, kind 20 - 500 HP...), one gone each use when +0x2d is set; its icon +0x34. The
list (BattleItemListMenu: the tab's frame 7, the count the number, no mark) offers those the
battle has less what the turn has chosen; the revivals only with the partner down.

The lists (Sopia, Awake; BattleSkillListMenu, a ScrollListMenu - GetModelDataName's scrlMtr_per,
scrlBar, scrlWin, scrlBtn ...): scrlBar (scrlBar_io) the frame; a scrlBtn (scrlBtn_io) a row, 0.08
apart (GetColumnSpacing), centred on the focused one, two either side (PartsPose) - its name
(typeS_L00; half grey not selectable), its number (DotNumId2 set 90 at call_dotNum_id01: the MP,
the awakening's level), its icon (mainIcon_id at call_mainIcon_id11, its frame / 20 s: the
command's +0x72 - the awakenings' 70, the advents' 71, whose frames lie swapped, 70's "Awake" at
3.55 s as the reference video shows it, as 28 / 29's do), the number's mark (cost_id at call_cost_id13: 12
"MP/", 13 "Charge Lv."), the focused one's sBtnSlct (call_sBtnSlct14); the tab (ListTag:
tabWin_io, tabName_id at call_tabName_id01 at frame 9 "Sopia", 11 "Awake", 13 "Advent"); scrlBack
to go back. The focused row's help over them (scrlWin, scrlWin_io: the command's or item's +0xc,
its two lines - split at a "\n" in the text - at typeS_L01 / typeS_L02). The bar's thumb
(scrlMtr_per, in place): SetScrollBarLength poses it at the part of the list shown (5 rows of
the count, at most all) x 100 / 20 s; SetScrollBarPosition lowers it by the rows above the first
shown, of those that do not fit. sBtnSlct (0.15 s, once) lights the focused row across. The help is
near black (SetStringData's colour 0.01), narrowed to fit inside its window. As the
reference video shows them (the prologue's Sopia list: Preemptive Strike focused, its help on
two lines, the thumb the bar's length).

A turn the script reserves (ReserveCommand) is played without the menus (BtlCommand::StepEvent):
the first battle with an advent (226) gives the pair Gale Roar so.

## Special scenes (BtlSpecialCommandList)

An awakening, an advent or a monster's special plays a scene of its own in place of the
command's motion. SetActionTable picks it: a switch on the command (tools/gen_special_table.py
compiles the decompilation's ifs and gotos as they stand and runs them) - by whether its +0x48 is
set (the party's: awakenings 121 .. 142 -> BuildAwake 0 .. 14, the advents 136 .. 141 and 391 ..
400 -> BuildAdvent 0 .. 5) or not (the monsters' specials, 321 .. 359, 415, 455 -> BuildAwake 15 ..
39). BuildAwake / BuildAdvent build each in code - a run of AddXxx calls, which the tool reads by
running each case through a small ARM interpreter over the library's code (-> specialtable.inc).
A step: its time (seconds from the scene's start), the handler it calls (the number its AddXxx
stores: BtlActionCtrl's table at +0x608 - 0 SetMotion .. 34 SetSkipEnable; AddCommand's its
argument), its arguments as a BtlDict ("id", "name", "shift.x" ...).

ActionSpecialCommand runs every step whose time the scene's clock (+0x30) has reached, in the
list's order, each once; the scene is over once all have run (the last a WaitTime, 17). The steps:

| op | step | what it does |
|---|---|---|
| 0 | SetMotion (id, blend) | the actor's motion (BtlMotionTable's number) |
| 2 | SetRotation (target) | the actor back at its dpos (SetPosRot) |
| 3 | SetVisibility (id, visible) | 0 the actor's side (the monsters' standing), 1 the other side, 3 its partner, 4 the map |
| 4 | SetAlpha (id, start, end, time) | 0 the party, else the monsters standing: their opacity from start to end over the time |
| 5 | SetColor (target, start, end, time) | 3 the party, 4 the monsters, 7 the map: their colour (StartDiffuseAnimation) |
| 6 | SetEffect (id, name, time, show, connect, start) | an effect (or a summoned figure, s401 ..) in slot id (+0x46: per actor): its settings' delay (+0x3e frames) on top of start; time > 0 its end (SetEndTime); connect: the actor's model its own |
| 7 | SetEffectTarget (id, target) | its model the actor's (1) or the target's (2) |
| 8 / 9 | SetEffectPosition / Rotation (id, target, apos, dpos, tpos, shift) | where it is: GetPosition / GetRotation (below) plus the shift (a Vector3; the rotation's degrees about y) |
| 10 | SetEffectAnimation (id, name) | its figure's animation `<figure><name>` (the summons' bz01) |
| 11 | SetEffectAlpha (id, start, end, time) | its opacity |
| 12 | SetCamera (name) | BtlCameraCtrl::Start: the camera of that name in a loaded camera figure, from its start, its frame reset ("GetSet..." the pattern's) |
| 13 / 14 | SetCameraPosition / Rotation (target, apos, dpos, tpos, shift) | the camera figure's place and turn |
| 15 | SetTargetCamera | the target's camera |
| 16 | LoadCamera (name) | LoadSpecialCamera: the figure `cam_z01_j001_00` .. (six at most) |
| 18 | Hit (finish) | BtlActionCtrl::Hit: the next of the action's hits |
| 19 / 20 / 21 | Dash / Return / ReturnDirection (time) | to the aPos by the target and back, over the time |
| 22 | PlaySE (id, delay) | BtlSound::PlaySE (-1 the command's launch sound, +0x76; -2 its hit's, -3 its shot's, -4 the post's); a delay: CreatePlaySEJob |
| 23 | PlayEventSE (id) | Cr3UtilSoundPlaySE: the clip itself |
| 24 | Fade (mode, frames) | Cr3Fade: 0 in from white, 1 out to white, 2 in from black, 3 out to black |
| 25 .. 33 | (AddCommand) | the few scenes' own (Dawin3's offsets, Lesika's stand-by, the glass fragments) |

BuildAwakeFromMotionTable(figure, motion) adds, from the motion table: a Hit at each hit time
(finish where the next is none), a Dash at the move's start (its length the time), a Return at
the second move's, a ReturnDirection at the turn's.

ActionSpecialGetPosition (r2 target, r3 apos, then dpos, tpos on the stack): with apos, target 1
the aPos joint the actor strikes the target from (GetAposName(actor, target), battleapos), 2 the
other way; with dpos, the battledpos joint of the actor (1) or the target (2: its pair's place in
a pair); else target 3 the party's middle (BtlWork +0xad0, CalcPlayersCenterPosition), 4 the
monsters' (+0xae0 - CalcMonstersCenterPosition divides their sum by the party's count), 1 / 2
the actor's / target's joint by tpos (BtlTposTable: 0 RootLocator, 1..4 DummyEP01..04, 5 / 6
DummyDP01 / 02, 7 DummyCPS, 8 DummyCP; 9 the model's own place), 0 nothing. GetRotation the same,
turns.

An effect's settings (BtlEffectTable: 166 entries of 0x50 bytes, by name, else "common"): +0x10
a shift, +0x30 the locator it follows of its model (as tpos; 9 the model's place, 7 none), +0x36
turned to the camera, +0x37 looping, +0x39 what it follows (1 the position, 2 the rotation, 4 the
joint's scale), +0x3b its kind (2 a summoned figure), +0x3e the delay. BtlEffect::Pose follows
the model only with those bits set: connect alone places nothing.

An awakening (action_table_awake): ActionAwakeStandby takes its charge, sets the command's motion
(bz01 ..), builds the scene and runs it; ActionAwakeCleanup deletes its effects, unloads the
cameras. An advent (action_table_advent): ActionAdventLaunch - the pair's motion 17 (bm01), the
launch effect, one of the two's voice, the launch sound - for 1.25 s, ActionAdventHide fades the
pair out over 1 s and hides them, ActionAdventStandby (the status windows off, the scene), the
scene, ActionAdventCleanup (its effects gone, the pair shown at full opacity, the windows back).
A tap skips a scene while its skip is on (SetSkipEnable; BtlMain::IsEnabled(0x104)): not here.

## The screen breaking away (BtlPostEffectGlassFragment)

Darwin's third awakening's step 33 sets BtlPostEffectGlassFragment +0x2c. At the frame's render
(BtlMain::Render pass 1, PostEffectGlassFragments), while the effect isn't running (its clock
+0x28 at 0 or past 1.5), BtlMap alone - its passes 2, 4 and 8, the fighters and effects left out -
is drawn into the post-effect buffer's render target 5. BtlPostEffectGlassFragment::Setup cuts
that texture into shards, and BtlMap::SetRenderSpecialMap(true) puts d1154b in the map's place.
So the forest breaks away over the dome, with the fighters drawn as ever.

**GlassFragmentEffect(w, h, 230)**, where w and h are half the screen's size (RenderContext
+0x428's +0xc / +0x10):

- The points, with y down and each random value lrand48 × 2⁻³¹: (0, 0), (w·r, 0), (0, h·r),
  (w·r, h·r), then 230 × (w·r, h·r).
- delaunay::DelaunayTriangles::triangulate is Bowyer-Watson. Its super triangle (getSuperTriangle)
  is equilateral about a circle whose radius is 1.2 × the distance from the least corner to "the
  middle", taken as half the points' extent. That is so tight that the hull's thinnest triangles
  go with it: about 98% of the hull is covered, and the screen's edges are ragged.
- fixEdge walks the hull's edges (getEdgeList: the edges of one triangle only) to a corner each
  way. Between corners 0 and 1 it sets their y to corner 0's; between 0 and 2, their x. Those
  points are already there. The cases meant for corner 3 test for a smaller corner after a larger
  one, which is never true, so it changes nothing.

**Each triangle a shard** (GlassFragment, 0xa0 bytes):

- Its middle less (w/2, h/2) gives a direction and a distance d.
- +0x40, a frame's shift: (d·0.06 + 8.7) × the direction, with y negated (up).
- +0x50, what that shift gains each frame: (d·0.02 + 0.55) × the direction.
- +0x60, a frame's turn about x, y and z: (d·0.0003 + 0.18) × r each. Its gain +0x70 is 0 × r,
  which still draws three lrand48 values.
- +0x80, a frame's growth: (0.004·r + 1) on x and y, 1 on z. Its gain +0x90 is (0.004, 0.004, 0).

Setup puts the corners on the screen, centred and y up: (2x − w, h − 2y). Their texture
coordinates are ((w + x′) / 2w, (h + y′) / 2h), each shard's own three vertices, drawn as a
CustomFigure of triangles blended src alpha / one minus src alpha (0x302 / 0x303).

**Update, each frame while the clock is ≤ 1.5**: each shard's corners are taken through
translate(middle + shift) · scale(growth) · rotationZYX(turn) · translate(−middle), in place. The
shift, turn and growth then add their gains. The figure's alpha (+0x11c) is 1 − clock / 1.5, then
the clock gains the frame's time. Past 1.5, Cleanup.

Shards that keep growing and speeding up are mostly off the screen within 0.3 s. Over 200 seeds
there are 444 shards on average, covering 94% of the screen, and none is left on it at the end
(`cr2tool glass 200`).

The port (battle/glass.cpp, the viewer's grabGlass / drawGlass) reads the map-only frame back as
the texture. It takes the screen's height as the iPhone's 640 (h = 320, w by the aspect), since
the shards' speeds are in its pixels. It seeds lrand48 with the fight's seed (the game seeds it
with the clock) and steps 60 times a second.

## The downed (BtlActionCtrl's dead actions)

After the command (ActionDeadStandby .. ActionDeadWaitDeadMotion), each downed: one of the party
its motion 7 (`bd02`), kept at its end, its end waited for (then a revival, state 33); a monster
with its +0x3a (125 of them) the defeat camera - its size 2 `Cam_defeat_L_00`, the boss's pattern
12 `Cam_defeat_neron_00`, size 3 `_XL_`, pattern 11 `_Hpair_`, size 4 `_Hsolo_` (at its place) -
and `bd02`, waited for (ActionDeadWaitBossMotion: 5 s at most), then unless its +0x38 (34) eff202
above it, the figure fading 1 -> 0 over 1 s after 0.25 s and gone (StartAlphaAnimation,
SetAlphaAnimationAutoHide), SE 0x66, then 0.95 s (ActionDeadWaitDeadMotion); any other monster at
once - unless its +0x38 - fading over 0.5 s and gone, eff201 above it, SE 0x65, the next action not
waiting. "Above": BtlEffectTable::GetEffectDeadOffsetByModelName, by the figure's first four
characters (0x5c061c: 53 heights, `tools/gen_dead_table.py`).

## Stealing (BtlActionCtrl::CheckSteal)

Steal Item (command 72, kind 29: a blow, then PostCommandAction's CheckSteal; sopia 13's first
level, 18's second): of its two targets, in the order BtlRand(100) under 50 keeps, each monster
struck (+0xf78) and not blocked (+0xf7b), while nothing has been taken this action: one with
nothing (its +0xa8 0) is marked stolen from (+0xf8c); one stolen from already "Nothing to steal!"
(announcement 64); else the Luck apart - the actor's (a pair's mean) less the target's, past
-101 / -50 / -1 / 49 / 100 (BtlCalc::CheckResult) - gives 40, 50, 60, 70, 80 or 90 in 100, +20
with skill (5, 1) on either of a pair; a miss "Steal failed!" (65). Taken: the rare one (+0xae)
BtlRand(90) under 45 with RareGetter (3, 4) on either of the party, else 30, else the common one
(+0xa8), each by the monster's level as the drops (under 20, under 40, more); added to the items
had short of 99 - "Stole %s!" (63, the item's name) -, else "Nothing to steal!". BtlAnnounce
shows the first of those, after the action's own. 199 of the 274 monsters carry something.

## The monsters' choice (BtlEnemy)

A monster record holds ten commands (+0x58 s16), each with a condition (+0x6c u8), a target rule
(+0x76), a weight (+0x80 s8) and a "must" flag (+0x8a), and per-slot values (+0x94 s16), and its
actions a turn (+0x4e u16: 245 monsters 1, 28 two, one three).

When the turn is set (BtlActionCtrl::StepSetAction -> SetPreEnemyAction, not in the first turn of
a battle the party starts - BtlWork +0x14 1 - nor for one whose time stopped, state 21): of the
commands first or last in the turn (kind 4, +0x4e bit 7), the first whose condition holds - a
sopia none under state 2 - that is a "must", or that BtlRand(100) puts under its weight (command
410 weightless but for the monster pair). That is its first action's command; its others (the
queue: straight after the first, SetActionQueue) and a first without one are chosen as each comes
(SetActive -> SetEnemyAction -> SetActionCommand): of the rest - not one taken this turn (+0xfc0,
cleared each turn by ClearEnemyAction), no sopia under state 2, a `bx01` motion's only as its last
action - those whose condition holds, the last "must"; else by weight (BtlRand over their sum).
Choosing clears state 37 on it. Conditions (Cond0..16; v the slot's value; party = fighters 0 ..
BtlWork +0x1c - 1, monsters 2 .. +0x1d + 1): 0 always; 1 a monster standing at v% of its HP or
less; 2 no monster with a raised parameter (states 5..12); 3 none of the party with a lowered one
(0..2, 13..20); 4 a monster with a lowered; 5 one of the party with a raised; 6 itself with state
35; 7 one monster left; 8 the party's mean level and the monsters' levels added up v or more apart;
9 the turn (from 0) v or later; 10 each v-th turn; 11 turn v; 12 itself at v% or less; 13 no monster
standing with an element lent (+0xfba), 14 none of the party; 15 itself with state 37 (War's
Executioner's Pose, then Decapitation); 16 the monsters' turn break. Of the slots with a command:
1039 always, 83 its own HP, 72 a turn, 48 every few turns, 35 from a turn, 21 alone, 19 state 37,
15 a monster's HP, 7 the turn break, 5 state 35, 3 the party's lowered.

Targets by the command's +0x4b (SetCommandTarget): 1 itself; 2 and 7 by the slot's rule, 4 by it
then CheckProvoke (kept on a pair standing together, else the one of the two provoking - state 30 -
if only one is); 3 / 6 the groups. Rules (Target0..14), of those that qualify BtlRand(n - 1) when
several (never the last of them), the one, else any of that side at random: 0 at random (+0x4b 4 the
party, 2 a monster, 7 either - BtlRand(both counts) under the party's: the party); 1 itself; 2 / 4
the party's most / least HP (the first of equals); 3 the monsters' most; 5 the monster most hurt
(the least HP, passing over one down, if short of its most; else the first short of its most); 6 of
the party raised, 7 not lowered; 8 a monster lowered, 9 not raised; 10 of the party with no element
lent, 11 a monster with none, 12 .. 14 of the party with element 1 .. 3. The Mantis: Lockjaw,
always, at one of the party. Conquest (two actions): its seals, one each, then Divine Shackles.
The huge pair (BtlEnemy::SetPreEnemyAction(): size pattern 11 Hpair - BtlWork +0x51 -, the turn
not the script's - +0x22f7 -, both standing, and no "must" of the first's holding,
CheckPairAction): the first's pre-action weighed with command 410 too (+0x111); short of 410 the
second's own pre-action, the third none; with 410 the two one pair this turn (+0xb02 each other's,
+0xaf0 bit 0): the first's choice of the commands it may take with its partner (+0x110: their
+0x73), else its attack (+0x4c, at the 410 slot's target, state 37 cleared), the attack taken as
the pair's (ActionAttackStandby: +0x54), the second with it - its own action none, the two's
Speed meaned for the order and their power together, as the party's pair. Couplings 47 .. 51 and
305 .. 309 (the shadows); over 40 seeds a pair in 6, 10, 4, 1 and 0 of them for 47 .. 51.
Once a monster chooses command 363 (Amon's, monster 175), SetActionCommand writes motion `bg01`
and +0x40 6 into the command's data. Those are the values its data already holds, so it changes
nothing.

The battle's start (BtlCalc::StartState, at StepBattleIn's set-up, BtlWork +0x14): 1 the party
first - BtlRand(100) under 10 with the party's mean level less the monsters' levels added up under
-4, 20 under 5, else 30, +10 with skill (4, 1) on either -, else 2 the monsters' assault -
BtlRand(100) under 10, none with skill (4, 2) on either -, else 0; drawn always, then 0 in a
coupling with its +4 flag (BtlWork +0x18: the scripted ones). The announcement after 0.5 s: 1
"%s's team attacks first!" (91 "%s attacks first!" for a party of one; the leader's name), 2
"Monster assault!!". In the first turn (BtlActionCtrl::CheckActionable) 1 leaves the monsters out,
2 the party - no menus (StepBattleIn's step 2). Coupling 5 over 200 seeds: 28 first, 17 assaults.

The turn order (OrderPoint): Speed (a pair: the two's mean), +10000 a command with special 7,
+5000 with the skill "ahead", -5000 for kind 4. SetActionOrder sorts each against those after it, swapped for a
greater: equals may change places.

## Showing an attack (BtlActionCtrl)

ActionAttackStandby (a pair's commands swapped for the characters' +0x46 - Pair Attack, motion
`bp01`), WaitAttackMotion (MoveToAttackPosition), Camera, WaitHit, Hit, Return, ... The times are
BtlMotionTable's, built in code (tools/gen_motion_table.py -> engine/src/battle/motiontable.inc:
69 figures' 25 motions, in frames / 20): +0x0c..+0x10 the move to the target, +0x14..+0x18 the
move back, ten hit times from +0x1c, the turn, the effect. Where the attacker goes is a joint of
`battleapos` (GetAposName: by a monster the party's `aPos_<its place><pSoloA | pSoloB | pPairA |
pPairB>`, by the party a monster's `aPos_<pSoloA | pSoloB | pPair><s, ssL, m, msL ...>`; the
fighters' indices +0xb48 / +0xb4c, +0xb50 / +0xb54 into BtlAposTable's names). Darwin's Slash
(`j001bo01`): there over frames 4 - 10, the hit at 11, back over 25 - 32. The windows change with
the hit: the HP, the break gauge and the charge stay as they were until it lands (the reference
video: "Even" through Slash on a mantis that stands). The number at the target's DummyDP01
(BtlActionCtrl::ShowDamage -> BtlEtcCtrl::CreateDispNumber, its kind's set by damageinfodata01:
20, 30, 40 a weakness's, 50 / 60 the restores'), and a mark there (CreateDispState ->
DamageInfo01: `bonus` with damageinfodata02's animator - 0 `bonus_miss` for a miss, 2
`bonus_crtcl` for a critical, 1 `bonus_block`, 3 `bonus_blow`; over 1.45 s): the reference video's
"1342 Critical!". The hit's effect (BtlActionCtrl::HitEffect -> ShowCommandHitEffect):
the command's +0x18 (Slash `eff105`; `eff106` for a critical) - an effNNN figure playing its
animation once at the one hit, at its DummyDP01 as its motion has it (the locators lie at the
origin in the bind pose), turned to the camera. The effects' settings (BtlEffectTable, built by a
static initializer: tools/gen_effect_table.py -> engine/src/battle/effecttable.inc) the special
scenes' (below) follow; the hits' still always turned to the camera. An animator's material channels (beside a figure's own animation) add to the
material's value: bonus_crtcl's fade -1 .. 0 about 1, brkInfo's colour -0.5 .. +0.43 .. 0 about its
0.99 (a flash, then the cream "Even" / "Player" / "Enemy"). A pair's attack: the second of the two after GetPairAttackDelayTime
(ActionAttackPairWaitDelayTime: 0x5362f4, 9 x 9 floats in frames of 0.05 s - the first's character
(cr3_common_data, from 0) the row, the second's the column; Darwin then Orlando 6, Orlando then
Darwin 8; the shadows' pair, 205 .., the same table), its moves as late.

The cameras: BtlCameraTable, also built in code (tools/gen_camera_table.py ->
engine/src/battle/cameratable.inc), numbers into btl_camera_name_table (462 names: the joints of
BattleCamera_move - 55 animated cameras on one timeline, each run from its start - and _nomove).
An attack by the party (SetAttackCamera1): GetAttackToEnemy(kind - 0 / 1 which, 2 alone, 3 a pair
-, the pattern's base (GetPatternToCamera) + the target - 2; Hpair's pair 21), + 0 or 2 at random,
and at the hit the next number (SetAttackCamera2: `Cam_BtlPair_SSS1_00`, `..._01`); by a monster
GetAttackFromEnemy(the base + which - 2, its target: 0, 1, 3 a pair) (`Cam_Btlenemy_SSS2_20`). The
battle opens on `Cam_Start_<pattern>_00` and the commands are chosen under `Cam_Getset_..._00`.

## Choosing the commands (BtlCommand)

The menus are one figure, `btlBtn` (fit 6), with an animator for each - `btlBtn1st_io`
(BattleFirstMenu), `btlBtn2nd_io` (BattleSoloMenu), `btlBtn3rd_io` (BattleSelectMenu) - and a
round button, `circleBtn_fr`, at each of its `call_circleBtn_frNN` joints that the menu takes (the
ids in ValidIDNumber: 26, 27, 28, 20 the first; 29, 30, 31, 18, 16, 33 the second; 43..47 the
targets), posed at frame NN (its own animation: NN / 20 s), which picks its icon and label:

| id | | | id | |
|---|---|---|---|---|
| 26 | Solo | | 31 | Attack (+0x40) |
| 27 | Pair (+0x44: switch 47, both standing) | | 18 | Sopia (+0x41) |
| 28 | Escape (not in an event battle, +0x1fb4) | | 16 | Items (+0x42 and items to use) |
| 20 | Options | | 33 | Defend (+0x43) |
| 43..47 | a target, n of them in the last n places (43 + 5 - n ..): its name (typeM_C01), level (DotNumId2, 3 digits of set 120 at call_dotNum_id30, the ones), HP bar (btlHpBar_per at call_hpBar_per20, HP x 5 / max s, at least 0.07), its signs (SetStatusIcon: the element's at call_enchant_id00, the states' from call_enchant_id01) | | 29 / 30 | Awake (+0x3e) / Advent (+0x3f), in one place |

CheckCommandEnable sets the second menu's switches: +0x40 from the script's switch 43 (and not
state 22), +0x41 switch 44 (not 2 or 23), +0x42 switch 45 (not 25), +0x43 switch 46 (not 24) -
the switches on at the battle's start but 42. A tap hits a button inside its tapAreaS01 /
tapAreaE01 joints (Cr3UtilCheckHitCollision); `btlBack` (fit 3, `btlBack_in`) goes back. A button
not selectable is drawn at half its colour (CircleBtn::ChangeTapButtonColor 0.5: every
material's DiffuseColor), one focused with its PRG_shiftR material's texture half across
(OffsetSet1 U 0.5; 2.0) - on a touch screen only the button pressed; among the targets the one
chosen, the first at first, always (the reference video).

The signs (BattleWindowEnchant2: SetSign's n a 0.048-unit square of hud_enchant at
EnchantParamPackTable's 16-byte UVs, the icons of a row 0.04 apart; GetCharaUIState's bit k sign
k + 1): 1 a white "i" (no element), 2 red, 3 cyan, 4 green (the elements 1, 2, 3), the states
after, 25 a clock. The party's on pc01info / pc02info (BattleStatusMenu::SetPlayerStatusIcon:
call_enchant_id00 the element, id01 and id07 six states each, id13, id14 two) - the reference
video's Orlando a green one, Darwin white.

The steps: the first menu (StepGetsetFirst / StepGetset: Cam_Getset, the party on its solo
places) - its result 0 solo, 1 pair, 2 escape, 3 options; then the command menu for the first
standing (solo: each in turn; the pair: once - SetCommandCamera 0x1a the first's, 0x1b the
second's, 0x1d the pair's) - its result 0 attack (the character's +0x44, a pair's swapped for
+0x46 as it acts), 1 sopia, 2 items, 3 defend (StepNextCheck: state 4, both for a pair; the
data's command 5), 4 awake / advent, 99 back (the second's back to the first's, else the first
menu); for one on a single foe the target (StepTarget: the target buttons, the camera on the one
chosen - SetCommandTargetCamera). The party chooses on its solo places. The target's camera is
the one StepBattleIn_Setup keeps for each fighter (BtlWork +0x580, 0x30 each: GetCameraInfo - the
camera figure at that frame and posed, its first frame held): a party member's GetTarget 0 / 1
(PCfighter / PCpartner; 2 PCsolo for one alone, 3 PCpair for a pair's command), a monster's GetTarget
4 + its size (Cam_Command_S / M / L / XL) in the monster's own frame - its DummyCP joint's world
position and turn (BtlModel::GetPosition / GetRotation("DummyCP")) on its stance's first frame at its
battledpos, not its feet (Cam_Command_M_00 sits 0.1 below its frame) -, the human patterns' 8 / 9
and 10, the boss's 12, XLXL's 13 in the battle's. The first target's camera is a cut
(StartCtrlCamera); from one target to another on the same side the camera moves there over 0.5 s
(BtlMoveCtrl::SetMovePosition / SetMoveRotation, interpolation 1 with k 10: u + u (1 - u) k / 10 =
2u - u²), to the other side's at once (time 0).

Options (StepOption / StepOptionLoop: the battle's windows off, BattleOptionMenu - an OptionMenu
with the battle's labels): the title (`menu`, menuOpt_io), the seat (seatOpt_io) with its labels
at typeM_L01..L07 - BGM, Sound Effects, Voiceovers, Language, Lock Orientation, Title Screen,
Manual (announcemenuname 103, 104, 105, 102, 106, 108, 101: the table at InterfaceMain::m_IdCounter
+ 4) - and the quality at typeM_C01 (143 High, 144 Middle, 145 Low) over circleBtn_fr 98 at frame 0;
the three volumes (VolumControlBar: vlmBar_per at call_vlmBar_per13..15, posed at the volume,
0..100, / 20 s - GetOptionWork; a tap on its slide area sets it), the language's button (99) at
0x534728's frame by the language (41 Japanese, 42 English, 44 French, 45 German, 46 Spanish, 47 /
48 simplified / traditional Chinese, 40 Korean), Lock Orientation's On / Off (36 / 37), the main
icons, btlBack to leave. The taps (OptionMenu::TouchSimpleRelease): the seat's tap areas 1 Title
Screen, 2 Manual, 3 / 4 the quality back / on (cr3_system_data +0x11: 0 High, 1 Middle, 2 Low;
circle 98 on), 5 / 6 the language back / on (+0xc, 0..7 - the messages' own order, Japanese,
English, French, German, Spanish, the two Chinese, Korean; circle 99 on: the messages then in it,
the code points the game's TBGothic lacks from a system font, as Android's Paint falls back);
Back (100) out. Title Screen (BattleOptionMenu::Update 3, 5, 6): the window closed, a system
window (syswin_io, its typeM_L01) asking "Return to title screen?" (announcebattle 0x6d) over
choice_io's circle buttons (call_cBtn_fr11 Yes, fr12 No) - yes to the title (8, the music
stopped; the viewer: `--play` from a new game again, else its end), no back to the options (7).
Manual (4, 9: InterfaceMain's 0x22 -> 0x61, InstructionsWindowMenu): `message\instructions` (u32
the records' offset, the strings', the count; 0x28 a record - the eight languages' titles, +0x20 a
story flag it waits for, 0 none, +0x24 its tutorial), those whose flag is on (SetTutorialList)
listed in InstructionsListMenu (0x62, a ScrollListMenu - the skill lists' scrlBtn rows, Back, no
help, no tab); one picked plays title.nut's manual_disp - flag 1122 on (the pictures through
InterfaceWindow), InterfaceWindow.GetTutorialID the pick, its Tutorial_NNN - then the list again;
Back the options again. The Sound Effects bar let go (TouchSimpleRelease: Cr3OptionData::SetSE)
plays SE 0xc6 at its new volume.

The tutorials (`script\battle_tuto.nut`, title.nut: the same functions): Tutorial_NNN loads up to
three pictures (LoadTexture: Cr3FieldTutorial, or InterfaceWindow.SetTutorialTexture in the manual
- `images\tutoNNN_MM`) shown one at a time on tutorialPic (its PRG_rep; SetTexture / SetWindow),
and AnnounceTutorialTxt's messages (EvtWindow.LoadMessageData(window, name)): Tutorial_Start /
End open window 4 - EvtTutoWindow, HelpWindowMenu's interface 0x1a, annoWin across the top -,
Tutorial_Msg window 3 - EvtAnnounceBWindow, 0xe, helpWin in the middle -, white text, three lines
a page, a tap a page. In a battle without a script of its own each is shown once by its story
flag (BtlEvent::LoadScript("battle_tuto"), CallScript): Tutorial_Armor at a turn's start with an
armoured monster standing (StepInit, 54); Tutorial_ComSopia / ComItem the first time their lists
open, Tutorial_ComDefende the first Defend (StepCommandLoop -> StepTutoEventInit: 61, 62, 63);
after an action Tutorial_Element for a command lending an element (+0x70 / +0x71 1..3: 53), else
Tutorial_Status with one standing showing a state (55) (ActionCommonEventCommandEnd); title.nut's
Tutorial_Option the first time the options open (OptionMenu::PartsUpdate, 84). The encounters'
own scripts show theirs themselves.

Escape (StepEscape: a yes / no; StepEscapeLoop): BtlCalc::Escape - sure in the party's turn
break, or with skill (4, 3) (HasEncountAbsoluteEscape: the one escaping's, or either of the party
standing's), or at the first turn with +0x14; else the party's mean level (+0x24: their levels
added up, +0x20, over their count) less the monsters' levels added up (+0x26, BtlWork::SetupEnemy)
through CheckResult's -50 / -1 / 49 to 40, 50, 60 or 70 in 100 (BtlRand). Away: the battle over
after 2 s (cr3_battle_post[8] 1); else, after 1 s, the turn the monsters' alone.

Defend (command 5; BtlCommand::StepGuard, as it is chosen):

- The one choosing takes motion 4 (`bg01`) as its stance; in a pair, both do.
- Character 6 (Li Hua, `j006`) also gets her guard, obj303, on her model. It is the solo one
  choosing, or the pair's second with `cr3_common_data[2]` 6. BtlEffectCtrl::CreateEffect is
  called with (4, hidden, 0.2): ControlWait shows it after 0.2 s, and type 4 then leaves it alone,
  so it stays until deleted (fighter +0xfc4).
- Taking the choice back (StepCommandLoop) puts motion 0 back and deletes obj303.
- In the turn, Defend's action is action_table_empty (ActionCommonInit, Standby, CommandStart,
  CommandEnd, Done, End): no announcement and no motion. The stance has been held since the
  choice, along with state 4 from StepNextCheck.
- Attacking (SetAttackMotion: a counter) hides obj303 (HideAttachObject). Back in the stance
  (SetNextMotion 4), ShowAttachObject deletes it and makes it again 0.5 s on.
- Falling (ActionDeadStart) deletes it. BtlTurnEnd's DeleteAll takes it with the turn's other
  effects.

## States (BtlStateManager)

Each fighter keeps a list of states (BtlWork +0x2384 + 8 x its number): an id, the turns left (0:
for good) and the uses left (0: not counted). 0 poison, 1 blind, 2 seal, 3 its next action
cancelled, 4 guard, 5..12 raised and 13..20 lowered Attack, Magic, Defense, Speed, Luck and three
more, 21 time stopped, 22..25 attack / sopia / defend / items sealed, 26.. the special ones (by
ShowAnnounceStatus3's announcements and their users: 26 / 27 counters, 28 a sopia counter, 29 a
magic barrier, 30 provoking, 31 blocking blows, 32 sure criticals, 33 revived once, 35 physical
strength doubled, 37 charging, 38..40 EXP / SP / Oz doubled - the commands Counter, Beng Quan,
Devotion, Mental Wall, Provoke, Stone Wall, Brave Horn, Regenerate, Demonic Strength, the Paradise
Apples).
They come with a command (BtlActionCtrl::CalcCommand - kinds 1, 2, 3, 26, 28 states alone, a sure
"hit"; 0, 4..17, 27, 29, 32 damage, the states after a hit - CalcStatus):
- CheckBadStatus (BtlCalc::BadStatus: the command's +0x50 + k of 1, or for a physical one the
  attacker's - its partner's in a pair - auto skill lending it, skill group -1 bit k; for k under 5
  the target's resistance +0xfa3 + k halving it, 2 none): the action cancelled (k 3, 50 in 100, 70
  in its side's turn break); poison, blind, seal (k 0..2: 80 / 100 - the command's +0x6d turns,
  else 3; +1 with skill 5, 5); state 3 for a turn (k 4); then (0x525d38, 100 in 100, +0x6d turns)
  k 5 time stopped, 6 attacks, 7 sopia, 8 items, 9 defending sealed. CheckCureStatus: +0x50 + k of
  2 ends it.
- CheckParamStatus: +0x5c + k of 1 lowers (13 + k; a raise there just undone), 2 raises (5 + k; a
  lowering undone); CheckSpecialStatus: +0x64's bit b the state 26 + b (0x535c08); both lasting by
  +0x6c - 1 one use, 2 a turn, 3 +0x6d turns, else for good; CheckAttachTarget: +0x70 1..3 lends
  that element, 4 takes it; after the command +0x71 the same for the one acting and its partner
  (PostCommandAction, CheckAttachSelf).

What they do: a raised parameter x 1.25, a lowered x 0.75 (GetCharaParam: a use each read); blind
halves the hit chance (a use); 32 makes a critical sure (a use); a cancelled or time-stopped one
does not act; a provoking one draws a monster's single attack (CheckProvoke, when only one of the
two provokes); 35 doubles a physical command's damage (Damage3, a use - both of a pair's); 4
halves the damage taken; 38..40 double the result's EXP / SP / Oz.
- Seals grey the party's commands (CheckCommandEnable); an action of a sealed kind - an attack by
  22, a sopia by 2 or 23, an item by 25, not an awakening - is not done (SetActionTable, then
  ActionDontActionAnnounce: "Attack power sealed!", "Skills sealed!", "Items sealed!"; "You don't
  have enough MP!" for one short of MP).
- 29, a magic barrier (ActionSopiaLaunch / ActionSopiaMirror*): a sopia at one holding it - or at
  its partner - unless the command pierces (+0x4e bit 3) turns back on the caster and its partner
  (the caster's side for one on all), a use of it on each holding it.
- The blocks (BtlActionCtrl::CalcDamage, after a hit): CheckSkillAutoGuard - at a quarter of its HP
  or under, not defending, skill 1, 0 guards that blow (state 4, cleared by CheckThrowBack) at 20% (40% in its side's turn
  break), "Auto Defend"; CheckAttackBlock - a blow on one with 31 or skill 5, 11 (full guard), or
  its skill 1, 3 at 10% (20%) unless +0x4e bit 6; CheckMagicBlock - a sopia on one with 5, 11, or
  skill 1, 4 at 10% (20%) unless bit 6: no critical, no damage ("Neutralized physical / magical
  damage!"), the states still. BtlCalc::Hit: a sopia always hits, but on one with skill 1, 5.
- The counters (CheckCounter, on one struck and standing - Damage, after CheckDead): a blow on one
  with 26 or 27, or its skill 1, 1 at 10% (20% in its side's turn break); a sopia on one with 28
  (a use) or its skill 1, 2 at the same chances - none for a command with +0x4e bit 4, on one with
  state 3, its action cancelled or its time stopped. It strikes back at once with its attack (a
  character's +0x44, a monster's +0x4c), put first in the turn (BtlActionTaskQueue::Insert), once
  an action, "Counter" on the banner.
- 33, revived once (ActionDeadWaitDeadMotion, ActionRevive*): one of the party downed with it - a
  use - stands again after its death motion with a quarter of its most HP: "Regenerate" (0x5d),
  Cam_Unit_PCrevival_00 at it, motion 8 (bh01), its HP in green at DummyDP01, eff216.

At the turn's end (BtlTurnEnd) each standing one's HP goes by poison's ceil(a tenth) of the most HP
(one left at least) less the life pool's (skill 5, 6) ceil(a twentieth) - all of it back with the
full life pool (5, 10), a heal when that comes to less than none -, its MP up by the mana pool's
(5, 7) ceil(a twentieth) of the most, then each standing one's states a turn on (Turn: gone at
none left). One downed loses them (CheckDead) but for 21, 33, 34 and 36..40.
The signs (GetCharaUIState: 0x536648's states as bits - poison 4, blind 5, seal 6, 12 7, 35 9, 30
10, 29 11, 33 12, raised 14..18, lowered 19..23, time stopped 24, 11 or 32 bit 8; the sign the bit +
1) on the targets' buttons from call_enchant_id01 and the party's windows (six at id01, six at
id07, one at id13, two at id14), 0.04 apart. Each change is announced on the banner (btlMsg,
AnnouncementWindowMenu), 1.5 s each (announcebattle's ids, the records from 1): "You poisoned
%s!" on a monster, "%s has been poisoned!" on the party, "Poison does not affect %s!" - one of them
(ShowAnnounceStatus1: the first of poison, blind, seal, state 3; none after a cancel), "%s is hit
with status effects!" for more than one of poison, blind and seal -, the seals' and time's
(ShowAnnounceStatus3), the parameters' and the special states'.

A blow's reaction (CheckThrowBack, as it lands):
- One of the party blocking it (+0xf7b), not defending: `bg01` once, then the stance.
- Auto Defend (+0xf7f): `bg01` once, and its state 4 is cleared, so it guards that blow alone.
  The flag is the action's (ClearCommandResult).
- No damage: nothing.
- Defending: `bg02`, where the motion table has it, back to `bg01`.
- Time stopped (21): nothing.
- State 37: `bx03`, back to `bx02`.
- Otherwise: `bd01`.

The `bx01` commands (Executioner's Pose, the dragons' charging, ...) set their stance to `bx02`
(ActionSopiaLaunch: SetNextNextMotion 20) while state 37 holds. A defender's stance is `bg01`.

## The result (BtlResult)

A monster downed adds its rewards (BtlActionCtrl::CheckDead: BtlWork +0xabc EXP, +0xac0 SP,
+0xac4 Oz, each at most 9999999): EXP its level squared + its +0x2c, SP its +0x30, Oz its level x
10 + its +0x34 (GetMonsterEXP / SP / OZ) - the prologue's three mantises at level 1: 6 EXP, 9 SP,
as the reference video's result shows. A win (BtlResult::Update): Battle_End, the music fading
(1.1 s), then "Victory!" (BtlAnnounce 6: `message\announcebattle.dat`, u32 the records' offset,
the texts', the count; 0x20 bytes a record - eight languages' text offsets) on the battle's
banner, the party standing in its victory motion (bb03) under `Cam_Win_PCsolo_00` (one alone),
`_PCpair_00` (a pair, both standing), `_PCfighter_00` or `_PCpartner_00`; a tap or 5 s; then the
battle's windows go (HideBattleStatus) for the result window (InterfaceMain::
SetResultWindowParameter: ResultMenu, CharaStatusWindowMenu, SystemWindowMenu - see
`drawResult` for their parts). Each member gets the EXP (one down: half - CheckLvUp), and over 60
frames (ProcessLvup; a tap: at once) its EXP goes in - the level rising as it passes the next's
(lvup_exp: tools/gen_level_table.py -> engine/src/battle/leveltable.inc; Darwin 0 at 1, 15 at 2;
Orlando 495 at 5, 885 at 6) - and its HP back to the most (Cr3RestorePlayerHP); NEXT the EXP to
the next level, its bar the part of this level's gone. Then the window's messages, a tap each
(announcebattle; the names from message\\charaname / sopianame / commandname / autoskillname /
itemname, by number): ProcessLvup1 / 2 a member's levels gained (0x11 "%s leveled up!", 0x12 by
%d); ProcessResult - CheckSpUp: each sopia the party carries, once, gets the SP (cr3_common_data
+0x28c), rising a level each time it reaches its growth's next (chara_sopia_lvup_exp 0 100 300 800
1500, mons_ 0 50 150 400 800; 5 at most), each level's command learned (0x16) or, with none, its
auto skill (0x17); CheckOz (0xf "Obtained %d OZ!"); CheckDropItem (0x14 / 0x15 with a count);
CheckDropSopia (0x13: had from then on, at level 1 - cr3_common_data +0x268's bit, the scripts'
SopiaGet / SopiaCheck; a new game has Darwin's, 1). The drops are rolled at each monster down
(BtlActionCtrl::CheckDead -> CheckDropItem, CheckDropSopia): BtlCalc's chance by the party's mean
level less the monsters' added up through -5 / 0 / 4 - 20, 30, 40, 50 in 100 -, then 30 in 90 its
rare item (+0xba) else its item (+0xb4), each of three by its level (under 20, under 40, more:
GetDropIndex), at most 99 had; its sopia (+0x18) the same chance if not had nor left already and
it is at +0x52's level. The reference video's first result: "Obtained 30 OZ!", "Obtained Chocolate
×2!". The bonuses (CheckBonus): either member's ExpUp / SpUp / OzUp (skill group 3, bits 0 / 1 /
2) +20 in 100 of it (BtlCalcValue); ItemUp (3, 3) +20 in 100 to the item's chance, RareGetter (3,
4: the first's, either's in a pair) 45 in 90 its rare one; states 38 / 39 / 40 (0x26..0x28) on either
of the party double them first, once whether one or both has it. After it the HP is back to the most and the MP kept as it is (cr3_player_data
+0x10; the most with RestoreMp, 3, 5, standing - and after the scripts' MPFullRecovery).

## A battle lost (BtlGameover; BtlMainCtrl::StepBattleOut)

1. The announcements and the effects are cleared. Cam_Lose_SoloPair_00 runs looping, and the
   music fades over 20 updates.
2. In an event battle (the coupling's +0x18), or any once flag 0x7f9 (2041) is set, it is a game
   over: BattleGameOverMenu's figure `dead`.
3. The banner reads "%s was defeated..." for one fighting alone (announce 0x5c, BtlWork +0x1c 1),
   else "%s's team was defeated..." (7).
4. 1.1 s on, track 18 plays, and a tap is waited for.
5. With no game over, or no continue data and no save, that is the end.
6. Otherwise the screen fades out over 20 updates. The HUD, the figure and the party go, it fades
   back in, and 0.5 s on the System window asks "Would you like to try again?" (0x87), yes / no.
   - Yes: the continue data is loaded, else the save (Cr3Util_Load_ContData / GameData; BtlWork
     +0x1a 1).
   - No: on to the end.
7. StepBattleOut on the loss:
   - no game over: battle_post[8] 0, flag 0x7f8 (2040) set, back to the field;
   - else battle_post[8] 2, then the field with the data loaded (+0x1a 1), else the title.

The viewer takes `--input tap`, then `yes` / `no`.

## The battle's script (BtlEvent)

Every battle runs `script\battle.nut` (BtlEvent::Initialize; the same VM and helper library as
the events). The game calls its functions by name (BtlEvent::CallScript; the call runs on the
game's updates and the battle's steps wait while it runs - BtlEvent::IsRunning):

| Function | Called by | When |
|---|---|---|
| `Battle_Init` | BtlMainCtrl::StepBattleIn (after flags 0xbeb, 0xbec set) | always |
| `Battle_Start` | StepBattleIn, after the opening's fade | the encounter's own script |
| `Turn_Start` | BtlCommand::StepInit, before the commands are chosen | own script (else battle_tuto's Tutorial_Armor, once) |
| `Command_Start` / `Command_End` | BtlActionCtrl::ActionCommonEventCommandStart / End, around each action | own script (else battle_tuto's Tutorial_Element / _Status) |
| `Dead` | BtlActionCtrl::ActionDeadEvent, a fighter downed | own script |
| `Turn_End` | the turn's end (case 1) | own script |
| `Battle_End` | BtlResult, a battle won | own script |

`battle.nut`'s Battle_Init picks the encounter's own script by its coupling -
`Cr3Battle.SetScript("battle_221")` for 49 of them (221 .. 349) - which is loaded once the call
is over (a new VM: Cr3Event ReleaseObject / CreateNewObject / Load / Run) and sets +0x237a, "it
has its own"; JumpScript(script, function) loads one and calls a function in it. Battle_Init
also sets the prologue's party (221: HP 3050, MP 340, Attack 651 ...).

The natives (`Cr3Battle`, Cr3BattleEvent_*): GetCoupling, GetTurn (the turn + 1), GetCommandActor
(+0x235c: the fighter whose action Command_Start / _End / Dead is about), GetCommand (that
fighter's command this turn, +0x54c), GetPlayer(k) (the character fighting, from 0),
GetMonster(k); SetCommand(f, command) / SetCommandTarget(f, target) / SetPair / ReserveCommand -
the turn the script gives (BtlCommand::StepEvent: for all five fighters, in place of the choices);
Charge(n) (+0x28, the charge gauge, at most 300); Exit (the battle over, BtlMain::NextMode 0 - no
game over; flag 0x3eb: mode 2); SetParamI / GetParamI(id):

| id | |
|---|---|
| 5..9 | values of the script's own (+0x2360..) |
| 10 + 7k, 11 + 7k, 12 + 7k .. 16 + 7k | the party's fighter k: HP (-1: the maximum), MP (0x8000: the maximum), the five parameters |
| 24 + 6m, 25 + 6m .. 29 + 6m | monster m: HP, the five parameters |
| 42..47 | switches (+0x2374..): 42 read by the awakening and the advent, 43..46 by CheckCommandEnable (the commands offered), 47 by the set-up's interface |
| 48 + 2k, 49 + 2k; 52 + m | the maximum HP, MP; a monster's maximum HP |
| 55, 56, 57 (Get) | the break gauge; the party's / the monsters' turn break |
| 58 (Set) | the exit's timer: Exit after n / 20 s (BtlEvent::Update) |

SetParamI2 / GetParamI2(0 | 1, i) keep 32 bytes / 32 integers in the save (cr3_common_data
+0x26c / +0x28c). Invoke(n): 0 every fighter standing rid of its states (BtlStateManager::Clear
0x21), 1 / 2 the party's status windows shown / hidden (BtlInterfaceCtrl::SetBattleStatusDisplay -
2 also clears and hides the announcement), 3 / 4 the announcement shown / hidden, 5 / 6 switches
(+0x237b, +0x237c).

The prologue (221): the boss's AI is three turns (condition 11: the turn), Gehenna Flame on the
third; its script's Command_Start sets the boss's Magic to 9999 for it, Command_End the exit's
timer (0.5 s) - a battle lost that ends as the story goes on. The first battle (222): Turn_Start
of turn 1 hides the status windows, plays a cut-in conversation, the solo / pair tutorial, shows
them again.

`engine/src/battle/script.cpp` (BattleScript) runs them: `cr2tool fight` (the calls to their
ends, the pages turning by themselves) and the viewer's fights (each step after the call is over,
its talks on the screen). All 49 encounters' scripts run without an error; the natives left as
stubs are the battles' tutorials' pictures (Cr3FieldTutorial), sounds and the field menus'
switches.
