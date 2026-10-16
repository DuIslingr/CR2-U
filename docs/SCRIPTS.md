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
`FlgOn`, `Flg`, `Msg_Open`, `Msg`, `WaitFrame`, `PlaySE`, `PlayBGM`, `CameraMove`, `ModelAnim`,
`Cutin_Start`, `BattleIn`, `ReserveShop`, ... (some 300 of them; even the title's script is
155 KB) - then its own functions, so the scripts can be read and run on their own.

## The native API

`Cr3BindNativeClosure` binds 24 tables into the root table (`tools/sq_bindings.py` lists them
from the Android library's decompilation: 219 functions, each with the native function it calls
and its argument types):

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

So the port can run the scripts unchanged on a Squirrel 2.2.4 VM - the library is public, zlib
licence - with these 219 functions implemented natively.
