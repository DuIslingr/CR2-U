# Chaos Rings II: the scripts

The events, the fields' gimmicks, the title and the staff roll are **Squirrel 2.2.4** scripts
(the library says `Squirrel 2.2.4 stable`), compiled: the 139 `script\*.nut` of the archive. The
native side binds its functions with **Sqrat** and runs them through
`MVGL::SqUtility::ScriptManager` (`Init`, `Load`, `Run`, `Update`, waits and debug hooks per
script handle).

## The bytecode

`tools/nut.py` parses and disassembles it (the stream's layout is in its header): every script
parses, 1,635 distinct names are used as table keys / calls across them. Squirrel 2.2's opcode
numbering holds: checked against the game's `SQVM::Execute`, whose case bodies call the
matching helpers (0x06 CALL: `StartCall`, 0x11 ARITH: `ARITH_OP`, 0x13 RETURN, 0x1e NEWTABLE,
0x30 CLOSURE: `CLOSURE_OP`, 0x33 FOREACH: `FOREACH_OP`, 0x3b CLASS: `CLASS_OP`, ...).

Each script carries the whole helper library compiled in - its `main` defines `SetVar`, `GetVar`,
`FlgOn`, `Flg`, `Msg_Open`, `Msg`, `WaitMessage`, `PlaySE`, `PlayBGM`, `CameraMove`, `ModelAnim`,
`Cutin_Start`, `BattleIn`, `ReserveShop`, ... (some 300 of them; even the title's script is
155 KB) - then its own functions, so the scripts can be read and run on their own.

## The native API

`Cr3BindNativeClosure` calls 24 bind functions: 23 tables into the root table, and the vector
classes (`tools/sq_bindings.py` lists them from the Android library's decompilation: 222
functions, each with the native function it calls and its argument types; three are registered
through a helper call Ghidra prints without its template argument - `Cr3Sound.PlayBGM`,
`Cr3GimmickData.ChangeModelColor`, `Cr3Battle.SetScript` - which the extractor takes too):

| Table | Functions | What for |
|---|---|---|
| `Cr3System` | 40 | dungeon / area changes, battles, Oz, items, Sopia, the party |
| `Cr3Save` | 7 | saving |
| `Cr3Flag` | 3 | `Set` / `Clear` / `Check` |
| `Cr3Work` | 2 | variables |
| `Cr3Sound` | 9 | BGM, sound effects, voices |
| `Cr3Fade` | 6 | fades |
| `Cr3Touch` | 1 | touch |
| `EvtScene` | 20 | event scenes: models, positions, locators |
| `VistaEvent` | 16 | "vista" scenes: camera, lights, map animation |
| `EvtFace` | 7 | faces |
| `EvtSkip` | 4 | skipping events |
| `EvtIntroduction` | 2 | introductions |
| `Cr3GimmickData` | 21 | field gimmicks |
| `Cr3FieldMap`, `Cr3FieldMapCamera`, `Cr3FieldMapCharacter`, `Cr3FieldMapEffectResident` | 4, 7, 7, 2 | the field |
| `Cr3ChangeCharacter` | 4 | changing the leader |
| `Cr3TabMenu` | 2 | the field's tab menu |
| `Cr3FieldTutorial` | 5 | tutorials |
| `Cr3Battle` | 21 | battle setup (`SetParamI`, ...) |
| `EvtWindow` | 18 | the message window: message / name data per window, open, close, yes / no |
| `InterfaceWindow` | 9 | other windows |

(`Cr3BindVectorStruct` adds the vector classes `tagCr3Vector3` / `tagCr3Vector4`.)

`BindNativeClosure` adds one root function, `WaitFrame(n)`: the calling script's state becomes 3
(waiting) with a count of n, and the VM suspends (`sq_suspendvm`) - each script runs as a
coroutine the manager wakes again. The scripts' other waits (`WaitMessage`, `WaitTouch`, ...)
are helpers looping on a native check and `WaitFrame(1)`.

## Running them

The engine runs the scripts on Squirrel 2.2.4 itself (`engine/third_party/squirrel2`,
`engine/src/script/vm.cpp`), as the game does:

- The game's compiler was 32-bit: every `SQInteger` in the stream is 4 bytes. Squirrel 2.2.4
  makes `SQInteger` 64-bit on 64-bit hosts and reads the stream at that width, so the loader
  first rewrites the stream with those fields widened (`Widener`: the fields of
  `SQFunctionProto::Load`; instructions, floats and object types keep their sizes).
- A script's `main` defines its functions; the one the game calls is named after the script
  (its source name: `D01_01_0101_evt` for `script\d01_01_0101_evt.nut`, `title`, `credit`).
- `engine/src/script/native_api.inc` (`tools/gen_native_api.py`) lists the 222 functions; those
  not implemented yet are stubs that count their calls. `ScriptVM::setNative` puts an
  implementation in a stub's place.

## Fields

Each dungeon has one script (`t01`, `d01` .. `d13`: `Cr3EventScriptFileNameTbl`), loaded when
the dungeon changes (`FldMain::LoadScriptFile`). On entering an area the engine calls its
`evt000` (`FldMain::Update` after `ChangeAreaExecute`, and `FldMain::Init` by how the field was
entered: modes -2, -1, 3 `evt000`, 1 `evt100`, 2 `evt200`, 0 none), which sets the dungeon's flags
up and calls `<dungeon>_gim`: per area (`Cr3System.GetAreaNum`: the map's last two digits - 1),
from the story's flags (`Cr3Flag.Check`), which gimmicks show (`Cr3GimmickData.Disp`), collide
(`SetCollision`) and react (`SetEventEnable`) - d0101's beast `c208_00` until flag 1508 - and then
the area's own set-up (`D01_01_01_set`: an event when its flags say). Gimmicks are named by
their joints (FORMATS.md: `c208_00`, `eff104_01`, `tbox_07`, `evt021_00`); NPCs start hidden,
objects, effects and boxes shown. A gimmick's touch / tap calls the script function of its id
(an event box's: its first six characters, `evt021`). An event box without `_check` in its name
fires on touch: the player's body entering its shape (a box, ball or capsule in its joint's frame,
sized by the joint's scale / 100) calls the function once, until the player has left it again.

A `_check` gimmick is tapped (FldMain::CheckBalloon, CheckGimmickStart): each update the leader
has control, every one shown with its event on and within its check length of the leader - NPCs
and objects 0.2 (FldNPC / FldObject +0x14), event boxes and effects 0.15 - gets a balloon (the
`balloon` figure through the interface's camera, over the gimmick by a height from a table by
name - c001 0.21, obj002 0.23, ... - else NPCs 0.2, objects and events 0.25, effects 0.1), and
a tap anywhere calls the nearest one's function (EventExecute: `c003_01`, `obj002_00`, `evt023`)
with the leader turned to it. From a new game's state, `cr2tool fieldscript`: 136 to tap across
the maps (the treasure boxes aside: none drawn), every one's function there and playing to its
end, 100 of them saying something.

Treasure boxes (Cr3TreasureBox, FldBox) are drawn as a dungeon is entered anew (FldMain::
ChangeDungeonFunc; not while flag 0x7fb is set): a table, the difficulty (cr3_common_data[0] -
`Cr3System.SetDifficulty`, the world select's level - 1) x 3 + a random 0..2, of the 33 in the
game's code (`tools/gen_box_table.py` -> `engine/src/field/boxtable.inc`). Of the dungeon's ten
`tbox` 7 are set at random (each holding the table's item by its place among those set); of the
three `obox` (key boxes) 2, and of the three `fbox` (fixed) all - but a key box or a fixed box
whose item is none (-1) is unset, and the third fixed box without a second party member. The
first fixed box holds the table's own, the others one by the party member (`kBoxCharaItems`).
An unset box isn't shown. A box (`obj011`, a key box `obj012`; `fn01` closed, `fa01` opening,
`fn02` open) has a balloon at 0.2 until opened; its tap opens it (`FldBox::Open`: the item one
more, SE 0x153) and calls its function (`tbox_03` -> `box_msg`: `Cr3GimmickData.GetItemID`, then
window 7 - GetItemWindowMenu's `getItem`, sliding in from above - with `SetItemMessage(7, id)`:
announce 0x75 with the item's name, the item's icon left of the text by half its width + 0.032).
A key box opens only for Orlando leading (FldPlayer +0x1c 1); for anyone else FldMain runs
`Message_obox` (the System window, 5: SystemWindowMenu's `sysWin_io`, three lines at
typeM_L01..03, its animator back to close). The leader at an unopened key box gets a mark over it,
0.125 above (FldBox's new mark, an interface "field gimmick effect" - FieldGimmickEffectToInterface:
kinds 0..8 interfaces 62..69, 124): for Orlando kind 2 (FieldGimickEffect02Menu: `gmk2ring`, its
`_io` then `_wait`, and the button `gmkBtn_io`), else kind 0 (`gmk0rng_io`). Orlando's tap starts
FldUnlock (FldMain step 0x16): the button goes out (`gmkBtn_out`, then the guide `gmk2csr_loop`
looping); two fingers put down within 450 of the mark and spread 1000 apart (platform 1's: the
interface's units x 2048 - Cr3UtilSetBalloonPos; the ring scaled 1 + spread / 1000 meanwhile)
open it - SE 0x164, the ring out, 20 updates on its function called and FldBox::Open (SE 0x153),
SE 0x154. One finger, or letting go early, fails it (SE 0x165, the mark anew). The viewer takes
the mouse as one finger, the other mirrored about the mark (`--input unlock` / `unlockfail`).
The boxes' table and which are set and open go in the save.

The field has the party's two (FldMain +0xac: two FldPlayers, their characters - +0x1c - the
party's as the area is made, `Cr3FieldMapCharacter.ChangeModel(slot, character)` changing one,
slot -1 the leader's), one leading (cr3_field_data +8, `Cr3ChangeCharacter.GetActive`), its
figure `c` + 001..007 by its character (FldPlayer::CreatePlayer); the other stands 30000 below.
The d01 script, for one, makes it Orlando and Darwin. The change (FldChangeCharacter) is allowed
by `Cr3ChangeCharacter.SetEnable` (cr3_field_data +0x32) and shown once flag 0x3fb is set
(SetAllUIVisible: CharaTradeMenu's `chrTrade` button, its tapArea; SE 0xb4): the leader darkened
over 16 frames, the other put in its place and facing, lit over 32 (ChangeCharacterExecute);
`ExeQuick(n)` changes at once unless n leads already. The leader and the change go in the save.

The touch actions - sprint, high jump, mystic, iai, aim, hakkei (FldSprint .. FldHakkei; their
gimmicks `sprintNN`, `hjumpNN`, `mysticNN`, `iaiNN`, `aimNN`, `martsNN`, kinds 0, 1, 3, 4, 5, 6
in the natives) - are off as an area is made; the area's script turns them on
(`Cr3GimmickData.SetTouchActionEnable(kind, gimmick, on)`) and gives each the spots it goes by
(`AddSpot(kind, gimmick, spot)`: the `spotNN` joint's place appended; `ResetSpot` empties them).
A sprint (its marker `obj151` on its place) starts when the leader is within 0.15 of it and the
player taps within 128 of the marker (the interface's units x 2048): crouched (`fj01`), then a
hop to each spot in turn (`fj02`, SE 0x155) on a parabola under 0.98 - rising 0.04 over the higher
end (Cr3CalculateVelocityArrivesAtHeight: v = sqrt(1.96 h); ...TimeArrivesAtHeight: the later
root) -, `fj03` ending as it lands and `fj01` 0.2 s before, SE 0x156 at each landing, then idle.

The others are each one character's: the high jump Darwin's (0), the mystic 2's, the iai 3's,
the aim 4's, the hakkei 5's. While the leader's body touches an enabled one's place its mark
stands on its first spot (FldMain::CollisionAdded; FieldGimickEffect0NMenu's for its own
character - the high jump's `gmk1rng`, the others' `gmkNrng` with the button `gmkBtn` -, anyone
else's `gmk0rng_io`), gone as the leader leaves; a tap within 128 of it starts the action
(FldMain::CheckTouchActionStartOne) - for anyone else SE 0xb6. A high jump hops as a sprint does
(the voice 0x8fa with each launch). An iai (FldIai) takes the button out (then its guide
`gmk4csr_loop`) and waits for a slash: a press, then within 10 frames the line from where it
began must pass the mark - the mark's foot on it, within 128 (Cr3UtilCheckCuttingCircle) - SE
0x164, the ring out, and the gimmick's own function called (FldMain::CallEvent: `iai01` - its
flag, its obstacle cut, the action off); else SE 0x165 and the mark anew. An aim (FldAim) takes
the button out too, then shows its set (FieldGimickEffect05Menu::SetAimingSet: `gmk5mark_io` on the
mark, `gmk5goal_io` on the leader, `gmk5line` from the mark to the touch, its length / 0.112):
a press within 128 of the mark drags the ring, and letting go within 128 of the leader succeeds
(SE 0x164, the function called); a press away from the mark, or letting go elsewhere, fails.
A hakkei (FldHakkei) takes the button out, then shows a ball for each tap it wants
(`SetHakkeiTouchCnt`; 5 unless the script says - FieldGimickEffect06Menu::SetCube: `gmk5ball` at
the ring's `call_gmk5ball` joints, the triangle's for up to three, the pentagon's from four):
each tap within 128 of the mark is a punch (`fp02` .. `fp06`, SE 0x160 or 0x161) and takes a ball;
all of them within 60 frames of the first succeed (SE 0x164, the function called), a tap away
from the mark or the time running out fails. A mystic's mark stands on its active spot
(`SetMysticActiveSpotName`: FldMystic::SetActiveSpotNumber, the spot so named); started, the
button goes out and a ring (FieldGimickEffect03bMenu's `gmk3rng`) stands on each of its other
spots: a tap within 128 of one names it (`GetMysticTapSpotName`), SE 0x164 and the function
called - the script moves its object there (`Mystic_Warp`) and makes that spot the active one -;
a tap elsewhere fails. `SkipMessage` shows the page whole
(the typing skipped); the message stays until a tap or the window's Close.

An area's script runs on the game's 20 updates a second like an event's, and uses the same
natives (the scene, the windows, the fades: Events below); while it runs the player stands. The
windows, the fade and the scene go on updating after it ends. A
field event's scene (`EvtSceneLoad('D01_01_0502')`: EvtScene.Init on the figure named after it -
no camera or lights of its own) puts its models among the field's figures, seen through the
field's camera; the scripts hide the leader meanwhile (`Cr3FieldMapCharacter.SetVisible`) and at
the end put it where the scene's `ev_p000` joint is (`SetPos_p000` / `SetRot_p000`). The event
ends on a fade to black; what called it (the area's set-up, `D01_01_05_set`) fades back in. The
field's camera is the script's when it says (`Cr3FieldMapCamera`, in pixels of a 960-wide screen,
the scroll negated): `SetPos(x, y)` puts it there, `CalcParameter(frames, fromX, fromY, toX,
toY)` slides it a frame an update, `GetPos_X` / `GetPos_Y` read it, `SetMode(0)` gives it back to
following the player.

`engine/src/field/fieldscript.cpp` implements those natives (`Cr3Flag`, `Cr3Work`, the dungeon
and area, `Cr3GimmickData`'s display / collision / event switches, `Cr3FieldMap`'s layers, the
camera); `cr2tool fieldscript <archive> <executable>`: from a new game's state `evt000` runs to
its end on all 217 maps in all three releases (an event's pages turning by themselves), and
every event box's function is in its dungeon's script.

## The game's flow

What comes after a field's or an event's script is what it reserved, in `cr3_script_work`
(FldUtil): `Cr3System.SetVistaFuncName(function)` / `SetVistaMapName(map)` (a vista event),
`BattleIN(id)` (a battle; `SetBattleParty`), `SetPopPoint(point)` (where the player arrives),
`SetComeFromBattleToField()` / `SetComeFromVistaToField()` (the next area's entry: `evt100` /
`evt200`). When a field's script ends, FldMain::BranchAfterEvent takes the first reserved of: an
area change, a battle, a vista, then the world select, the camp, a shop, ..., the title. When a
vista's ends, VistaMain::BranchAfterVista: a battle, another vista, the credits, else back to the
field.

The field's tab and the camp. While nothing runs, the field shows its tab (FldTabMenu ->
InterfaceMain interface 0x28, FieldMenuTabMenu: `fldMenu_tab`, its own animation played forward to
open, back to close - Cr3UtilSoundPlaySE 0xba / 0xbb). Its tap areas: 01 the tab itself, 02 Camp
(+0x1b, Cr3TabMenu's), 03 the map (not in dungeon 12), 04 Save (off with flag 0x3ea), 05 the
encounters' switch (shown with flag 0x40e, on with 0x40f); each but the tab only once it is open,
0xb4 as it is pressed. FldMain takes the button: 1 the camp (step 5, FldCamp), 2 the dungeon's map
.... The camp (InterfaceMain::SetCampWindowParameter): CharaStatusWindowMenu (0x23 for one - campWin
with campWin1_io, the third window's joints; 0x24 for two - campWin2_io), CampTitleMenu (0x2c: `menu`
with menuCamp_io), CampMenu (0x29: campBtn_io's circle buttons 16 Items - not selectable with no
item -, 17 Equipment, 18 Sopia, 20 Options; btlBack; campLnk - campLnk1_io with GEO_pub hidden before
flag 0x4b0, campLnk2_io after - its 201 / 202 links), the battery, the Oz (0x52: ozWin_io, its seven
digits) and the clock (0x51 PlayTimeWindowMenu: timeWin_io - the device's time, GetCurrentHourTime:
the hours at call_dotNum_id04, glyph 163's ":" at 03, the minutes at 01, set 90). A button's result
(InterfaceMain::Update's camp step): 1 CampItemListMenu (0x2d), 2 the equipment (0x2f, 0x30, 0x3a,
0x33), 3 CampSopiaListMenu (0x36), 4 CampOptionMenu (0x2a: the options' window, Title Screen and
Manual as the battle's), 5 / 6 the camp closed. The items' list (CampItemListMenu): by its category
(the item's +0x10, a byte: 0 / 1 the first tab, 4 the second), item 36 first, each with its count,
lit when the camp can use it (Cr3UtilUseItemByCamp: 4 .. 7, 36 .. 41 - the MP ones and the field's;
the HP is whole again after each battle). The sopia's (CampSopiaListMenu::SetSopiaList): each had
(cr3_common_data +0x268's bits), by number, the focused one's windows beside it.

Using an item (CampItemListMenu 9 / 10): CharaSelectButtonMenu - a face button (faceWin_io, faceBtn
with faceBtn_00<n>) per character who has joined (Cr3UtilValidCharacter: flags 0x3fd .. 0x403),
faceSlct on the focused, campHelp_io's "on whom" (announce 0x84), CharaStatusWindowBMenu (campWin
with campWin3_io: the one's level, HP, MP, an arrow where the item changes them) and
CharaStatusCapacityMenu (statusWin_io: the five parameters, the change at id<n>11 of set 140 with
sign 5 or 6, the sopia's combination's name). A tap on another face focuses it (0xb3), on the
focused uses it (0xb4): one used up (+0x2d) asks how many (shopNum_io: tap 01 one fewer, 02 one
more) and yes / no (announce 0x74: the item, the count had), then Cr3UtilParameterUpByItem - +0x14
HP, +0x18 MP (at most 9999), +0x1a .. +0x22 to the parameters for good (cr3_player_data +0x1e ..)
- and Cr3UpdatePlayerData; 0xc6 when it raises MP or a parameter. Emergency Exit (36; not with
flag 0x3e9) asks (0x73), then FldMain's camp result 2: SE 0x14f, flag 0x3ed, the music out, on to
t0101_p1.

Cr3UpdatePlayerData (a character's most HP / MP and parameters; engine/src/battle/rules.cpp
playerStats): the level's (BtlData::GetCharaHP / MP / Param) plus the seeds', +10 HP, +1 MP and
+1 each parameter a level past 20 (Cr3UpdatePlayerBonusData), the weapon's, armour's and
accessory's, then each of the five sopia's auto skills up to its level - their percents added up
(+0x10 HP, +0x11 MP, +0x12 .. the parameters; BtlCalcValue: v + v x p / 100) - and the
combination's (Cr3UtilGetSopiaComboSkillID: the kinds, +0x11, of the sopia in places 3 .. 5 against
eight combinations at 0x534bb8 - {the item needed, the auto skill, three kinds, the third 0 any});
then within 1 .. 99999, 0 .. 9999.

The equipment (CampEquipmentListMenu): CampEyeButtonMenu's buttons (eyeBtnPos: eyeBtn_id at the
character's number / 20 s at call_eyeBtn_id<n>, eyeSlct and eyeCsr_loop on the one shown; 0xb7);
the view - the weapon (cr3_player_data +0x3c), armour (+0x3e), accessory (+0x40; menu name 0x4b
"none", half with none to wear), the own sopia (+0x42, fixed), Darwin's human sopia (+0x44; 0x4d),
the three monster sopia (+0x46 .. +0x4a; 0x4e .. 0x50), the skills (0x54) - no tab; a row its list
(tabs 1 weapons, 3 armour, 5 accessories, 9 the sopia and skills): the items of its kind (+0x10: 1
2 3) had that the character may wear (+0x12's bit), the worn one fixed, those all worn half
(Cr3UtilGetNotEquippedItemNumber: the count less the joined characters' worn); the human sopia (the
character sopia had, the n-th past the first behind flag 0xbe1 + n); the monster sopia (+0x10 1 -
one another wears taken from them after asking, announce 0x8a with their name, menu name 0x29 +
n); Remove (0x53) where one is. The windows by it: the character's with the change the focused item
makes (SetEquipStatusParamerter), or the sopia's (CampSopiaStatusMenu: sopiaWin_io - sopiaPic with
sopiaPic_NNN, its name, its rank cost_id at the level + 6 / 20 s, its kind's sign, the next level's
EXP and bar; CampSopiaSkillMenu: skillWin_io, a level's command or auto skill each, "???" past it).
A choice: Cr3UtilChangeEquipItem / Sopia, 199.

The camp's links (CampPubLink): Scenarios (ScenarioFlowMenu: scenariosBG's panels shown by their
flags, the chapters' covers until theirs and then their names, dragged up and down 0 .. 0.704 -
1.472 with flag 1 -, the part opened: flags 1 .. 4 and 151 .. 192 of 46); the pub's request
(CampPubDetailMenu: cr3_global_work 7, the request the pub took - PUBTxt's text; none, the link at
half and inert).

The tab's others: the map (FldDungeonMap -> DungeonMapMenu: mapGuide_io with guide_<t01 | dNN>,
mapInfo_<..> at the area + 1 / 20 s for mapSymbol, the goals - the dungeon's entries at 0x535794 with
their flags set - as mapGoal; a tap closes it; not in dungeon 12), saving (SaveConfirmationMenu:
"menu" with menuSave_io, the status windows, yes / no - announce 0x69 -, 0xbd, "saved" 0x6a; not with
flag 0x3ea) and the encounters' switch (cr3_field_data +0x31; shown with 0x40e, usable with 0x40f).
The dungeon and area (cr3_field_data +0 / +4) are the map's name's: t01MM 0, dNNMM NN; MM - 1.

A new game starts on map d1020 (area 19), its set-up `D10_01_01_set`: `NewGameFlgSet`,
`PartySet`, the field event `D10_01_0104`, then `D10_01_0101` - the prologue's vista
(`D10_01_0101_evt` in `d1053v`) and its battle. `engine/src/script/gamestate.cpp` holds the
flags, the work values and the reservations; `cr2view --play` follows them from d1020, fighting
the battles it reaches (the party SetBattleParty set, else the party now). The party: the pair
fighting now (cr3_common_data +3, +4; Cr3System.SetActivePartyMember / GetActivePartyMember) and
three kept (SetPartyMember, CopyPartyMember) - a new game's Darwin and Marie.

## Events

A vista event is a script of its own, `script\<event>.nut` (`d01_01_0101_evt.nut`, its function
`D01_01_0101_evt`), set by a field's script (`SetVistaEvent(function, map)`: `D01_01_0101` sets
`('D01_01_0101_evt', 'd0151b')`, the map a battle environment's figure). VistaMain loads the map's
figure, the event's scene - three figures named after it: `<event>` (its joints `ev_<model>` the
models' locators: `ev_v001`, `ev_v206_00`, ...), `<event>_camera` (an animated camera) and
`<event>_light` - and calls the function (Cr3Event::Call). The game updates 20 times a second
(TestApp::onUpdate: no sooner than 0.05 s after the last; the skip button runs 5 updates a
frame), in GameMain::Update's order: the scene (EvtScene::Update, the camera, the lights), the
scripts (ScriptManager::Update: a `WaitFrame(n)` counts n updates down - WaitProcessor - and the
script resumes), the windows, the fade (Cr3Fade::Update). The engine draws 60 frames a second
over those updates: its timelines being linear in time, a frame between updates shows the
camera, the moves, the animations and the fades where they are by then. The scripts drive:

- **the scene** (`EvtScene`): `Init(event)`, `Visible(model, on)`, `ChangeAnimator(model,
  "e826", loop)` (the model plays `<figure>e826`, its figure the name up to `_NN`, a motion
  relative to its base pose), `ChangeAnimatorBlend(model, "e007", loop, frames)` (the same,
  faded in from the motion before over the frames: EvtMotionManager's two slots),
  `PlayEffect` / `PlayEffectLoop(model)` (shown, its own animation - alone in slot 0, absolute -
  from the start, once or looping: an effect's, `eff284`),
  `SetFaceTexture(slot, image)` / `ChangeModelFace(model, slot)` (the colour texture of the
  model's `j<NNN>face` material: EvtModel::ChangeTexture_Face), `SetDrawSkip`, `Terminate`;
- **the timelines** (`VistaEvent`, in the scene's frames - 20 a second): `SetMove(model, from,
  to)` (the model along its locator), `CameraMove(from, to)` / `LightMove` (to + 1: the frame's
  end). The vista camera's near plane is at least 0.05, its far 5 x the camera's (at most 5000;
  VistaCamera::Init, but for D11_06_0102 and D12_03_0106a). `ExecutionStart` /
  `ExecutionEndStart` wait for the player's slash (`WaitPlayerSlash`: `CheckExecution` each
  frame);
- **the windows** (`EvtWindow`): Cr3Event's seven - 0 a cut-in (portraits), 1 the vista's, 2 / 3
  announcements, 4 a tutorial's, 5 the system's, 6 the narration - `Open(n)`, `SetMessage(n,
  message)`; `LoadSharedTextData(name)` (`message\<name>.dat`),
  `LoadSharedNameData('name_tag')`, `Open`, `SetMessage(window, n)`, then `CheckMessage` each
  frame until the player has read it (`WaitMessage`), `Close` - the window drawn as the game's
  (FORMATS.md, the interface), its lines typing a character an update;
- **the portraits** (`EvtFace`, through the dungeon scripts' `Cutin_*` helpers): `SetTexture(file,
  id)` loads a face, `Create(layout)` opens a layout (0 right, 1 both, 2 left, 3 the party's, 4 a
  single one), `ChangeTexture(layout, id)` shows a face in it, `SetColor(layout, 0.5)` dims who
  isn't speaking, `Delete(n)` closes (InterfaceMain::EndCutInWindow's table: 0, 1, 2, every one,
  3, 4), `DispOff` closes them all, `DeleteTexture` drops the faces;
- **the fades** (`Cr3Fade.FadeIn / FadeOut(type, frames)`, the `2`s with from / to %): the colour
  0 black (the interface's `fadeB`), 1 white (`fadeW`), 2 `fadeF`, 3 the encounter's wipe
  (Cr3FadeIn's table; the colours the fade figures' DiffuseColor), linear over the frames;
- **the end**: `Cr3System.BattleIN(id)` and the like (the game's flow above).

`engine/src/event/` implements them (`scene.cpp`: the figures and timelines; `runtime.cpp`: the
script, the window's messages, the fades); `cr2tool play <archive> all` plays every event script
to its end (pages turning by themselves) - all 70, in all three releases; `cr2view --event
D01_01_0101_evt --vista d0151b` shows one.

`cr2tool scripts <archive>` loads every script and runs its `main` (all 139, in all three
releases); `cr2tool run <archive> <script>` then calls the script's function with every native
a stub, listing what it reached - an event plays as far as its first message (scene, animators,
camera, lights, the window opened, the message set) and then waits on `EvtWindow.CheckMessage`.
