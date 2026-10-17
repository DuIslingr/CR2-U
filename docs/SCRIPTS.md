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
with the leader turned to it. From a new game's state, `cr2tool fieldscript`: 137 to tap across
the maps, every one's function there and playing to its end, 55 of them saying something.
(Treasure boxes - FldBox, opening by themselves - aren't yet.)

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

A new game starts on map d1020 (area 19), its set-up `D10_01_01_set`: `NewGameFlgSet`,
`PartySet`, the field event `D10_01_0104`, then `D10_01_0101` - the prologue's vista
(`D10_01_0101_evt` in `d1053v`) and its battle. `engine/src/script/gamestate.cpp` holds the
flags, the work values and the reservations; `cr2view --play` follows them from d1020 (battles,
not in yet, taken as won).

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
