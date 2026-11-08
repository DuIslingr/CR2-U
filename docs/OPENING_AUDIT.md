# CR2 opening audit

Targeted follow-up, 2026-11-07, in the spirit of CRO-U's and CR1:U's opening-cutscene audits:
the new game's opening - the field event `D10_01_0104` on d1020, the prologue vista
`D10_01_0101_evt` in d1053v and its hand-over to battle 221 - checked call by call against the
Android 2.1.2 library's code (`libChaosRings2.so`, SHA-256
`5534b905725b276887fecb2ac5c06f9d733a551a28841212c5e9c66c04d250ec`), the scripts' compiled helpers
(`tools/nut.py dis`) read alongside. CR2 is not CR1 / Omega's engine: its cutscenes are Squirrel
scripts over native classes (SCRIPTS.md), so none of CRO-U's event-VM fixes apply as they are;
the natives the opening calls were compared one by one instead.

## What the opening calls

`D10_01_01_set` (area 19, a new game): `NewGameFlgSet`, `PartySet(0, -1)`, `Check_FadeIn` (flags
1004 set, 1005 cleared), flags 1007 and 1501, then `D10_01_0104` and `D10_01_0101`.

`D10_01_0104` (a field event): `IfDisp(false)`, `EvtScene.Init('D10_01_0104')`, `WaitFrame(10)`,
the leader and the map's layers hidden (`Cr3FieldMapCharacter.SetVisible`, `Cr3FieldMap.SetVisible*`),
`Cr3System.SetBGColor(0, 0, 0)`, `Cr3Fade.FadeIn(0, 1)`, `WaitFrame(20)`, `EvtScene.PlayEffect('eff601')`,
`WaitFrame(240)`, `Cr3Fade.FadeOut(0, 1)`, `WaitFrame(10)`, everything shown again, the scene
dropped (`SetDrawSkip`, 5 frames, `Terminate`), `IfDisp(true)`.

`D10_01_0101` reserves the vista (`SetVistaFuncName` / `SetVistaMapName`). Its script:
`EvtScene.Init`, the text (`LoadSharedTextData('D10_01_0101')`, `LoadSharedNameData('name_tag')`),
six `SetAnimator`s, `SetFaceTexture(0, 'j001a15')`, `PlayBGM(7, 20)`, `PlaySE_Repeat(6)`,
`PlayEffectLoop('eff284')`, `EvtSkip.ButtonCreate`, `FadeIn(0, 30)`, `SetMove`s and
`CameraMove / LightMove(0, 200)`, the models hidden, `WaitFrame(180)`, `FadeOut(0, 20)`,
`WaitFrame(20)`, the timelines at 500 .. 620, the models shown, `FadeIn(0, 20)`,
`ChangeAnimator(v001, e706)` / `(v310, e817)`, `ChangeModelFace(v001, 0)`, `EvtWindow.Open(6)`,
`WaitFrame(120)`, `FadeOut(0, 1)`, `WaitFrame(1)`, the models hidden, SE 264, `SetMessage(6, 1)`
and `CheckMessage` until read, `Close(6)` (the helper's `WaitFrame(5)`), `FadeIn(0, 5)`, the
timelines at 1000 .. 1200, SE 314, the models shown, `FadeIn(1, 20)`, `WaitFrame(130)`,
`FadeOut(1, 3)`, `WaitFrame(5)`, SE 245, `FadeIn(1, 10)`, `WaitFrame(45)`, `Open(6)`,
`WaitFrame(55)`, `FadeOut(0, 1)`, `WaitFrame(1)`, SE 264, message 2, `Close`, `FadeIn(0, 5)`, the
timelines at 1500 .. 1750, SE 315, `ChangeAnimator(v310, e818)`, `WaitFrame(60)`, `(v001, e707)`,
`WaitFrame(20)`, `(v001, e708)`, `WaitFrame(70)`, SE 267, `SetEffectLine`, SE 204, `WaitFrame(20)`,
`FadeOut(1, 30)`, `StopSE(0, 30)`, `WaitFrame(50)`, `(v310, e080)`, the models hidden,
`DeleteEffectLine`, `EvtSkip.ButtonDelete`, `StopSEAll(10)`, `UnloadAll`, the scene dropped,
`WaitFrame(10)`, flag 2039, `SetComeFromBattleToField`, `BattleIN(221)`.

Each helper is the script's own (`Msg_Open(6)` and `Msg_Close` play no sound for the narration
window; `PlaySE` is quiet while the skip button is held; `ModelAnim` passes `ChangeAnimator` for
these motions), so the port runs them as they are: what was compared is the native side.

## The original's behaviour, and what changed

The game updates 20 times a second; one update (`GameMain::Update`) is: the main's update
(`VistaMain::Update`: the scene), `InterfaceMain::Update` (the menus: the fade windows, the text
typing), `Cr3Event::Update` (the scripts, then the eight `EvtWindow`s), `Cr3Fade::Update`. The
port's `EventRuntime::update` is one of those: the scene, the script, the typing, the fade.

| | The original | CR2-U before | CR2-U now |
|---|---|---|---|
| A fade out (`Cr3FadeOut` -> `InterfaceMain::SetFadeOut` -> the fade window's vtable +0x1c, `FadeWindowMenu::InterfaceOpenAnimeSet`: animator 1 from 0) | its first step in the update after the call (`InterfaceMain::Update` runs before the scripts), then one a update, speed 10 / frames over the fade figure's half | stepped in the update of the call | the first step the update after |
| A fade in (`Cr3FadeIn` -> `SetFadeIn` -> vtable +0x20, `InterfaceCloseAnimeSet`: animator 2 from 0.5, `+0x18 = 3`) | `FadeWindowMenu::Update` counts the hold down three updates before any step: the screen starts clearing at the fourth update after the call; `Cr3Fade`'s own count (a vista's end waits for it) runs from the call | stepped at once | held four updates (`EventFade::hold`), then stepped |
| `CameraMove(from, to)` / `LightMove` (`EvtUtilVistaCameraMove`: the end (to + 1) / 20 s; `VistaMain::Update`: the time + 0.05 first, posed only while under the end, stopped without posing) | posed at frames from + 1 .. to, resting on `to` | rested on to + 1 | rests on `to` |
| `ChangeAnimatorBlend(model, motion, loop, x)` (`Cr3EvtSceneChangeAnimatorBlend` passes x through to `EvtModel::SetEventAnimDirect` -> `EvtMotionManager::SetMotion`, which counts x / 0.05 updates; `ChangeAnimator` passes -1.0: none) | x is seconds (`ModelAnim`'s table: 3, 8 and 10 s for the idle motions e001 .. e007, e500 .. e506); the weight is 0 the update after, then k / frames | x taken as frames (x / 20 s) | seconds, the weight an update behind |
| A page's last tap (`EvtWindow::UpdatePage`: `NextBlock`; the next update's `UpdatePage` sees the page's end, `EvtWindow::Update` -> `EvtWindowMessage::SkipMessage`; the script's `CheckMessage` runs before the windows) | the script goes on two updates after the tap, the text shown till then | at once | two updates after, the text shown |
| A tap stop's taps (`UpdatePage`: its timer from the block's start - `EvtWindowPage::NextBlock` - must pass 0.15 s; 0 when skipping) | a tap within 150 ms of the block's start is ignored | taken | ignored (`EventRuntime::advance(tap)`; buttons held and pages turning by themselves wait for nothing) |
| An event's pop point or area change (`FldUtilSetPopPoint` reserves a dungeon change as `ReserveChangeDungeon` does; `BranchAfterEvent` sends it to step 0x10: the fade waited for, 0x11 counts three updates, 0x12 `ChangeAreaExecute` - one call - and `CallEvent` the area's entry, run by `EventExecute` the update after) | the new area's entry script starts eight updates after the event's last (the prologue's `ppJump('d0104_p1')` after battle 221) | at once | held eight (`--play`) |
| A tap while the text types (`NarrationWindowMenu::TouchGestureTap`, with its +0x49 - set by `InterfaceMain::SetNarrationWindow`'s parameter 0xf at the open - and +0x4d: `MenuText::SetSkipFlag` on the block's lines) | the block shows whole, up to its next stop; a timed stop (`\w`) then waits its frames as it would | the page to its next tap mark, the timed stops skipped (the narration's 2 s cut short) | the block to its next stop |
| A `\*` message - the two narrations are ones (`EvtWindow::CheckMessage`: the page's end, the text typed - `CheckDisplayEnd` in the update of its last character - and `EvtWindowMessage::CheckMessage`'s byte past the page, the text's end) | done for the script's `CheckMessage` in the update after its last character types; shown till the next message or `Close` | done one update later than that | the same update |
| The vista's end (`VistaMain::EventExecute` sees the script's state 4 the update after and reports it the one after that; state 200 waits for `Cr3Fade`; 10000 counts `+0x24 = 2` down; `BranchAfterVista` sets the next main, `GameMain::Update` makes and updates it the update after) | the battle's (the field's, the credits') first update is the sixth after the script's last, another vista's function is called in the seventh; the scene, the windows and the fade go on meanwhile | the battle begun in the script's last update | six (seven) updates after, the scene going on (`--play`) |
| A field event's way out (`FldMain::EventExecute` reports the script's end the update after seeing its state 4; `BranchAfterEvent`: a vista or the title go to step 0x1d, a `BattleIN` to 0x1c - no wipe, `+0xa0` is 0 -; 0x1d and 0x1e wait for `Cr3Fade`'s count, 0x1f counts three updates, 0x20 waits again and sets the next main; `GameMain::Update` makes it the update after, `VistaMain` loading its script in that one and calling the function in the next) | a vista's function starts ten updates after the event's last, an event's battle eight, the title nine; the field's fade and windows go on meanwhile | the next main started in the event's last update | held that long (`--play`; the pop points' and area changes' way, step 0x10, as before) |

### The first area's dialogue: the cut-ins

d0104's `D01_01_0401` (`Msg_Open(0)`, `Cutin_Start`, two messages, `Cutin_End_L / R`,
`Msg_Close`): the conversation window (`EvtCutinWindow` -> interface 0xc, `ConversationWindowMenu`:
`msgWin` with `msgWin_in` 0.15 s and `msgWin_out` 0.25 s) and the portraits (`EvtFace.Create(n)`
-> `InterfaceMain::SetCutInWindow`: interfaces 0x18 / 0x17 / 0x16 / 99 / 100, `CutInWindowMenu`
on `cutIn` with `cutInR_io` / `cutInLR_io` / `cutInL_io` / `cutInPty_io` / `cutInSgl_io`, 0.2 s
each).

| | The original | CR2-U before | CR2-U now |
|---|---|---|---|
| A message set while the window's in animation plays (`ConversationWindowMenu::Update`: state 0 until `IsEndCurrentAnime`, then 1; its texts are stepped and drawn - `CustomDraw`, `Pose` - in state 1 alone; `SetStringData` steps the new text once when made) | nothing drawn until the animation is over; the first character exists from the set, the rest type from the update after the one that ended the animation (msgWin_in: the fourth after the open; the vista's `vstWin_io` and the narration's `narrationPos` have no length: the second) | drawn and typed from the set | the same wait (`EventRuntime::messageWindowUp`, `openSeconds`) - the help / announcement / tutorial windows 0.2 s, the system window 0.15, the item window 0.2 |
| The update of an `Open` / `Close` / `Create` / `Delete` itself (the menu made or its close animation set during the script's turn; `InterfaceMain::Update` steps it from the next) | the window or portrait stands at its first frame till the next update | stepped in that update | ages start at -1 update (`windowAge` -kTick, `Portrait::fresh`) |
| A tap while the cut-ins' text types (`ConversationWindowMenu::TouchGestureTap`: `Cr3UtilCheckHitTapCollision` on `msgWin`'s tap area first, then `MenuText::SetSkipFlag`; a typed page turns on a tap anywhere - `EvtWindow::UpdatePage` reads the touch itself) | the typing skipped only by a tap on the window | anywhere | on the window's `tapAreaS01 .. E01` (`onConversationWindow`); the vista's and the narration's anywhere, as theirs are |
| A tap while the window's animation plays (`TouchGestureTap`: `IsEndCurrentAnime` first) | nothing | the typing skipped | nothing |

Also as the original already: `EndCutInWindow` turns the portrait's io animation back from where it
is (`CutInWindowMenu::InterfaceCloseAnimeSet` keeps the time, +0x5c = 2), gone at its start;
`SetColor` the material's diffuse (`ChangeDiffuseColor`), `ChangeTexture` its colour map
(`SetTexture`); the other side's joints hidden and the layout moved toward its side on a wide
screen (`PositionCorrection`); three lines a page (`EvtCutinWindow::GetLinePerPage`); the window's
close through `msgWin_out`, its text gone with it (state 2).

Also checked, and the same already: `WaitFrame(n)` (`WaitProcessor::ProcessWait`: n updates, 0 as
1); a message's first character in the update of `SetMessage` (`NextPage` -> `ShowMessage` ->
`NarrationWindowMenu::SetStringData` steps the text once), then one code point an update
(`MenuText::Step`, speed 1.0, from `InterfaceMain::Update`, line after line); a `\w` wait counted
as frames x 20 against the elapsed time; the voice played with the first page and stopped at
`Close`; `PlayEffect` / `PlayEffectLoop` (visible, rewound, the loop flag); `SetMove` (the
model's own time + 0.05 to the end, no + 1); `PlayBGM(7, 20)` a 0 .. 100 fade over 20;
`StopSE(slot, 30)` a 100 .. 0 fade over 30 then stopped; `StopSEAll(10)`; `Cr3FadeIn`'s colour
tables (the four at 0x51e208: 1, 2, 3 in each - the type as it is, else black); the narration
window's open (`NarrationWindowMenu::InterfaceOpenAnimeSet` is empty) and close (its
`narrationPos` figure's animation backwards - it has no meshes); the field's fade-in on entering
an area, which is the scripts' own (`Field_FadeIn` / `Battle_FadeIn` / `Vista_FadeIn` in each
dungeon's script: flag 1004 - `Check_FadeIn` sets it - means none, 1005 the dungeon's first entry
with the leader's materialisation, SE 336 and a white fade-in of 30, else a black one of 20) - the
port runs them; the battle's return to the field (`BtlMainCtrl::StepBattleOut`: `NextMode` 2 the
vista with flag 1003, else 0 the field; `FldMain::Init` mode 1 `CallEvent` evt100, run in its first
update). `FldMain::Init`'s mode 0 - no entry script - fades in from white over 20 on its own; the
opening never enters that way. The title (`TitleMain::Update`): five updates, then the title
sequence (`InterfaceMain::SetTitleSequenceParameter`, the interface's own - `title.nut` with it);
when `CheckTitleSeqenceReaction` says it is over, `FldMain` is next, made and updated the update
after, its entry event with it. The port's title is its own flow (SCRIPTS.md) and was not
compared further here.

### The first field: its name, the vista window and the tutorials

The first field's `D01_01_05_set`: `MapNameDisp(1)` (`EvtIntroduction.SetDungeonNum(1)`, then
`GetVisible` each update), then `Tutorial_Field` (flag 66, area 4: `WaitFrame(20)`, SE 192,
`Tutorial_016 / 018 / 019` - each `LoadTexture('tuto016_01')`, `SetTexture(0)`, `Tutorial_Start`
(`LoadMessageData(4, 'AnnounceTutorialTxt')`, `Open(4)`, SE 184), `SetWindow(true)`, `Msg(78)`,
`SetWindow(false)`, `Tutorial_End` (`Close(4)`, SE 185, `WaitFrame(10)`, `UnloadData(4)`)). The
field's talks (`Msg_Open(0)`) use the cut-ins' window; the vistas' dialogue (`Msg_Open(1)`) the
vista window (`EvtVistaWindow` -> interface 0x21, `VistaConversationWindowMenu`: `vstWin` with
`vstWin_io`, 0 s, and `vstWin_talk`, 0.05 s).

| | The original | CR2-U before | CR2-U now |
|---|---|---|---|
| A centred line (`EvtWindowMessage::ReadPage` keeps a `\c` byte per line; `EvtWindow::ShowMessage` sends each line as its own pack, the line's byte at +0x31; `InterfaceMain::ChangeHelpWindowMessage` makes it param 5 - `HelpWindowMenu::SetStringData` with the slot + 3, `typeM_C11 ..` -, `VistaConversationWindowMenu::SetStringData` takes the slot + 2 (21 / 22), `ConversationWindowMenu`'s + 3 (05 .. 07) for any slot but the name's) | each line in its own slot by its own `\c`: message 78's page 1 is `\n \c Moving` - an empty first line, the title in the second centred slot | the page's first line decided for all its lines: "Moving" at the left, in `typeM_L02`; so every tutorial title and the announcements' second lines ("Lv. 51 to 60 unlocked!") | per line (`drawWindow`) |
| The vista window's name line (`SetStringData` slot 0 plays `vstWin_talk` forward from 0 and sets +0x30; `EvtVistaWindow::SetMessage` calls `DeleteNamePlate` when the pack's name is empty: `vstWin_talk` back from where it is; the window's own first animation is `vstWin_io` backwards) | `vstWin_talk`'s two frames are the same pose (joint 2 at x 0.512, `vstWin_io`'s at 0): the line is there from the message's update, gone with the first nameless one; the text is stepped and drawn in state 1 alone - the update after `Open` (`vstWin_io` has no length) | `vstwin_talk` posed with a speaker, `vstwin_io` without; the text from the update after the open | the same |
| The tutorial's picture (`FldUtilSetTutorialWindowCreate` -> interface 0x66, `InstructionsGraphicWindowMenu` on `tutorialPic`, 0 s: param 3 loads a texture - five at most -, 4 sets one as the material's colour sampler, 6 / 5 show / hide, 1 ends it; `Draw` renders while shown; `Update` steps it and is done once ended and its animation over) | shown at `Visible` as it is, the texture set before it | the same (`tutorialPicture`: drawn once shown and a texture number set; the scripts always set one first) | the same |
| The dungeon's name (`Cr3SetIntroductionDungeonNum` -> `SetMapNameMenuParameter`: the table at 0x5343f0 maps dungeons 0 .. 13 to interfaces 0x54 .. 0x60, 0 and 1 both to 0x54; `MapTitleMenu::Initialize` makes the language's figure - 0x5af508: `mapName`, `mapName`, `mapNameFre`, `Ger`, `Spa`, `Chs`, `Cht`, `Kor` - with the animator `mapName` + `D01` .. `D13` by the interface, `ChangeAnime`; `Update` steps it and returns `IsEndCurrentAnime`, on which `InterfaceMain::Update` deletes it; `Cr3IntroductionGetVisible` is `SurvivalMapNameMenu`, param 2: whether it still exists) | the name for 1.95 s (`mapNameD01.anim`), `GetVisible` false once it is deleted | the same (`drawIntro`, `introSeconds` from the animation) | the same |

Also as the original already: the help window's and the system window's tap-skip permits
(`HelpWindowMenu::HelpWindowMenu` and `SystemWindowMenu::SystemWindowMenu` set +0x2e / +0x2f /
+0x30 to 1 - the typing may be skipped by a tap from the start, which settles the item below);
`MapNameDisp` is called by the dungeon scripts d01 .. d11 and d13 alone, each with its own number,
so the hub d12 - whose `mapNameD12.anim` does not exist - never asks for one (if it did, the
original's menu would end in its first update, an uninitialised animator's length being 0 for
`IsEndCurrentAnime`, the port's after 2 s); the pack's +0x30 byte (`GetTheme`'s +0x40, 0 for
every window) is `MenuText::Initialize`'s bordered-text choice, not an alignment - the alignment
is the slot joint's letter (`Cr3UtilSearchTextPosition`, `MenuText::SetTextOffsetData`), as the
port has it.

### The first field's talks: the camera, the leader, the choices, the autosave

A talk in the first field (`D01_01_0103`: `IfDisp(false)`, `StopBGM(30)`, `Msg_Load`,
`Cutin_Load`, `PlayerDisp(false)`, `EvtSceneLoad`, `ModelDisp_Event`, `CameraSetMode(1)`,
`CameraSetPos(1088, -384)`, `WaitFrame(30)`, `PlayBGM(22, 20)`, `FadeIn(1, 20)`, the cut-in
dialogue, `FadeOut(0, 30)`, `CameraSetMode(0)`, `PlayerSet`) and what the field's helpers reach:
`Cr3FieldMapCamera.SetMode / SetPos / CalcParameter / GetPos_X / _Y` (`FldUtilSetFieldMapCameraMode`
/ `Pos` / `MoveCalcParameter`, `FldUtilGetFieldMapCameraPos_X / _Y`), `Cr3FieldMapCharacter.SetVisible /
SetPos_p000 / SetRot_p000 / ChangeDirection` (`FldUtilSetFieldMapPlayerVisible`,
`Cr3EvtSceneSetFieldMapPos / Rot` -> `FldUtilSetFieldMapPlayerPos / Rot`,
`FldUtilChangePCDirection`), `Cr3GimmickData.ChangeDirection` (`FldUtilChangeNPCDirection`), the
choices (`EvtWindow.ShowYesNoButton / CheckYesNoButton / HideYesNoButton` ->
`InterfaceMain::SetYNChoiceWindowParameter` - interface 7, `BattleYNChoice` on `choice_io` with
`circleBtn_fr` 11 / 12 -, `GetBattleYNChoicePushBottom`, `EndBattleYNChoice`) and the autosave
(`SaveCont`: `Cr3Save.CommitCont / SaveCont / CommitSystem / SaveSystem` ->
`Cr3Util_Commit_ContData` / `Save_ContData` / `Commit_SystemData` / `Save_SystemData`).

| | The original | CR2-U before | CR2-U now |
|---|---|---|---|
| A camera slide (`CalcParameter(frames, from, to)`: `FldMap::SetCameraMove_*`, the step `(to - from) / frames`; `FldMap::ExecuteCameraMove`, run by `FldMap::Update` in mode 1 - after the script's turn in `FldMain::Update` -, puts the scroll at `start + step x (frames - remaining)` and counts `remaining` down from `frames`, done when it reaches 0) | the scroll at `from` in the call's update, `from + step x n` n updates on, done after `frames - 1` of them: it stops - and stays - a step short of `to` (a slide of 1 frame never moves); `GetPos` reports that | reached `to` in the update `frames` after the call | the step count stops at `frames - 1` (`FieldScript::tick`, the viewer's between-update interpolation with it) |
| `SetMode(1)` with no `SetPos` (`Gimmick_CameraMove`, `pub_*Choice`, `evt022` and 30 more: it then slides from `GetPos_X / _Y`) | mode 1 leaves `FldMap +0x30 / +0x34` as `CalcScroll` last put them following the leader; `GetPos_X / _Y` return that (negated, over the width / 960) | the script's camera jumped to its last scripted place (0, 0 at first) and `GetPos` said so | the place it followed to is kept (`shownX_ / shownY_`) when a mode other than 0 is set from following |
| The yes / no buttons' close (`EndBattleYNChoice` -> `BattleYNChoice::InterfaceCloseAnimeSet`: `choice_io` back from where it is, state 2; `Update` ends it when the animation does) | the buttons slide out over 0.2 s (stepped from the update after the hide), no tap taken meanwhile | gone at once | `yesNoClosing()`: the viewer poses `choice_io` backwards from where it was, its tap areas dropped |

Also as the original already: `SetPos(x, y)` and the slide's ends are the script's pixels times
the width / 960, negated into the scroll (the `eor 0x80000000`s in `FldUtilSetFieldMapCameraPos`
and `MoveCalcParameter`; `Cr3UtilGetPlatform` is 1, so `ConvertFieldMapCameraPosForScriptInput`
changes nothing) and clamped to the painting (`SetScrollStop`: 1024 or 2048 units an axis by the
map's kind, less the screen; `CalcScrollStop`'s 0.4 on the shared-background maps) - `frameAt`;
the leader's and an NPC's turn (`FldDirectionManager::SetDirection`: a quaternion slerp from where
it stands, `(n + 1) / updates` at its n-th update, done at the last); `PlayerSet` puts the leader at
the event scene's `p000` (`EvtScene +0x610`: its position and rotation); `PlayerDisp` hides the
leader and its shadow; the buttons answer on the release (`TouchSimpleRelease`: 11 yes, 12 no, SE
0xb4 while permitted, +0x59 set from construction), from the show on, the open animation
notwithstanding; `CheckYesNoButton` returns 0, 1 or 2; `Cr3Util_Commit_ContData` copies the game,
common, player, field, flag, work, box, stats and achievement data into the continue record and
`Save_ContData` writes `cont.dat` at once (the port's `onSave(1)`).

### The first field's events: a tap, an event box, an exit, and control back

`FldMain::Update` reads the first finger event at the update's start; in step 2 (the leader's
control, +0x28 = 1) it runs `PlayerMove`, then `CheckCollisionEvent` (+0xcd90, set by
`CollisionAdded` in the physics after the step switch) - `CollisionEventExec` - else a tap's
`CheckTouchEventExec_Tap` (`CheckGimmickStart`: a sprint, a high jump, a key box's mark, the
touch actions, else the nearest balloon - `CalcNearestBalloonNum`). Step 0x1b's `EventExecute`
calls the function (`Cr3Event::Call`, run by `Cr3Event::Update` in the same update) and reports
the end; `BranchAfterEvent` picks what follows; `CheckBalloon` runs after the step switch, only
with +0x28 = 1.

| | The original | CR2-U before | CR2-U now |
|---|---|---|---|
| A balloon's tap (`CheckTouchEventExec_Tap` cases 7 .. 0xb: step 0x1b, +0x28 = 0x11, +0xa1 = 0 - no control -, `SetEventStep(false)`: `FldController::TouchFinish`, no fade; a box opened there, `FldBox::Open`; `EventExecute` in step 0x1b of the next update: `RemoveCharacterController`, `Cr3Event::Call`) | the function's first update is the one after the update that read the tap; nothing moves or is tapped meanwhile; the leader is not turned (`CheckGimmickStart` turns it only to a sprint, a high jump or a key box's mark - `FldPlayer::SetDirection` over 10 updates; a talk's script turns it, `ModelDir_Field('player', ...)`) | called at the tap, between updates - a script starting an update early; the leader snapped to face the balloon | queued (`queueEvent`) and called at the end of the next update's tick; no turn |
| An event box walked into (`CollisionAdded`: not a `_check` box, its event on, the leader in control - +0xcd94 = 0xc; `CollisionEventExec` in the next update's step 2: step 0x1b, control off, `SetEventStep(false, 5)` - no fade) | the function's first update is the second after the contact's | the first | the second (`touchEvents` queues it) |
| The script's end (`EventExecute`: state 100 until `Cr3Event::GetState` is 4 - `CollectGarbage`, 1000 -, then 1000: `AddCharacterController`, done; `BranchAfterEvent` with nothing reserved: step 1, +0x28 = 0; step 1 gives control - +0x28 = 1, +0xa1 = 1, the touch actions valid - once `GetFirstSingleFingerEvent` has none, step 2 from the next update) | the balloons back in the third update after the script's last, taps and the walk read from the fourth; a finger left down holds it | at once, in the script's last update | `controlHold` (3 updates); `fieldControl()` gates the taps, the walk's start, the balloons, the event boxes and the exits |
| An exit walked into (`CollisionAdded`: a jump point - +0xcd94 = 0; `CollisionEventExec` case 0: control off, step 0x10, +0x28 = 4, SE 0x152 to or from a sky map, `SetEventStep(true, 5)` - `Cr3FadeOut(0, 5)`; 0x10 waits for the fade, 0x11 counts three updates, 0x12 `ChangeAreaExecute` and the entry's `CallEvent`) | a fade to black over 5 frames; the new area's entry runs in the twelfth update after the contact's | the area changed in the contact's own update, no fade-out | the fade (`EventRuntime::fadeOut`) and the exit hold of an event's area change, counted from the fade's end (`fieldExitHold` 6); exits only with control |

Also as the original already: the nearest balloon by distance (`CalcNearestBalloonNum`, from
30000); a balloon for what is shown, valid, its event on and within its check length, an opened
box none (`CheckBalloonOne`); a key box tapped by anyone but Orlando runs `Message_obox`, by
Orlando the lock (`FldUnlock`); a box opened at the tap; an event box's function its name
(`EventExecute` case 0xc: `Cr3Sprintf` to 7 bytes - the first six characters, `evt021`).

### The first dungeon's encounters: from the hit to the battle

`FldEncount::Update` (after `FldMap::Update` in each `FldMain::Update`): with the encounters on
(`cr3_field_data +0x30`) and, once the field menu has their switch (flags 0x40e and 0x40f), it
on (`CheckEncountInterface`), the leader's x / z distance adds up while not paused (+9, set by an
event's start); each 0.17 a step, each area's steps (`SetData`'s table, 2 after `Reset`) a draw
(`Pickout`: `lrand48() % 100` at most the chance a hit - `MakeCouplingID` -, else the chance + 5).
A hit sets the state 10000; the next update's sets +0x1c and `SetBattle(-1)`; step 2 of the
update after sees +0x1c (when no tap or press is read first) and goes to step 0x1c with SE 0xc9;
0x1c saves the leader's place (`SaveWorkBeforeBattle`), stops its animation
(`FldPlayer::m_bAnimationStop`) and falls into 0x1d: `Cr3FadeOut(3, 20)` - the wipe - and
`StopBGM(20)` unless flag 0x7f7; 0x1e waits for `Cr3Fade` (`FadeOut`: +1/20 an update from 0,
done at 1 - `WaitFadeFinish` is its state 0), 0x1f counts three updates, 0x20 makes `BtlMain`,
which `GameMain::Update` runs from the next.

| | The original | CR2-U before | CR2-U now |
|---|---|---|---|
| The hit's way to the battle | the leader walks two more updates (`PlayerMove` in step 2 before the check); SE 0xc9 in the second update after the hit; the wipe called in the third (its window's first step the fourth, covered 0.25 s later - `encount_in` at its own speed, as the reference video shows; see round 19) with the music's 20-update fade; the battle's first update the 28th after the hit (1.4 s) | SE, wipe and music at the hit, the walk stopped there; the battle 0.65 s after the hit (the wipe's 0.5 s and three updates) | counted on the hit's updates (`encountStep`: `encount.updates`): walk to the second, SE then, wipe and music in the third, the battle started after the 27th |

Also as the original already: the distance in x and z only, the 0.17 step, the draw every area's
steps, the + 5 after a miss, the area's ten weighted couplings, the switch's flags, no count while
an event runs; `SaveWorkBeforeBattle` keeps the leader's place and rotation for the return.

### The battle's way out: from its end to the field

`BtlMainCtrl::StepResult` waits for `BtlResult`'s steps to reach their end (step code 0x12) and
sets step 7; `StepBattleOut` copies the battle's results into the game's data, picks the next main
(`BtlMain::NextMode`: 0 the field, 1 the title, 2 a vista with flag 0x3eb - cleared -) and sets
step 8, where `BtlMainCtrl` does nothing more. `Cr3BattleEvent_Exit` (a script's own end) calls
`NextMode` the same way. `NextMode` only sets `BtlMain +0x10 = 1`; `BtlMain::Update` runs
`UpdateStep` before the scene: step 1, once `Cr3Fade` is idle, `Cr3Fade::FadeOut` over 20 updates -
white (colour 1) unless `BtlWork +0x1a` is set (black: `BtlGameover::Update` sets it when it
reloads the save or the continue data), none for coupling 282 (0x11a) - and `BtlSound::StopBgm(20)`;
step 2 counts its time past 1.0 s (21 updates) and releases the music; step 3 waits for the fade,
hides the battle's status and ends the scene (`Term`, `+0x18 = 0`); step 4 waits for
`Cr3ResourceManager::IsEmpty` and calls `Exit`, which makes the next main - `GameMain` runs it from
the update after. The field's `FldMain::Init` (mode 1) calls `evt100`, whose `Battle_FadeIn` fades
in from white over 20 (`FadeIn(1, 20)`).

| | The original | CR2-U before | CR2-U now |
|---|---|---|---|
| A battle's end, its result over (or its script's `Exit`, or a loss with no game over) | the battle - still moving - fades to white over 20 updates from the update after, its music fading over 20; the next main about 24 updates after the end; the field then fades in from white | the field at once: a cut from the battle to the field's white, then its fade-in | `beginBattleOut`: `battleFade` white from the update after (none for coupling 282), `stopBgm(20)`, the field after `kBattleOutSeconds` (24 updates) |

Also as the original already: `BtlResult::Update`'s victory - `Battle_End` called when the script
has one, the music stopped over 20 and 1.1 s waited, the victory theme (BGM 15) unless the kind is
3, the announcement 6 with the victory motion and camera 0.1 s on, then the result window after a
tap or 5 s (`result.stage` 1 .. 3); the game over's black fade of 20 when it reloads.

### A battle's end: the last action, a win or a loss, a script's `Exit`

`BtlMainCtrl::StepAction` runs the action controller until its state 5 (the action shown, the
script's `Command_End` called) and decides in that same update: the party all down, step 5
(`StepGameover`, or the field again when the battle allows a loss); the monsters all down, step 6 -
`BtlResult +0x10 = 1`, its steps from the next update - after `+0x15 = 1`; else the next actor, or
`StepTurnEnd` for the next turn. There is no pause of its own between the last action and the
result. A script's `Exit` (`Cr3BattleEvent_Exit`, in `Command_End` for battles 245, 279 and 282,
each after one command) calls `BtlMain::NextMode` there and then: the way out above starts at once,
before any result or game over.

| | The original | CR2-U before | CR2-U now |
|---|---|---|---|
| A win | the result's steps from the update after the last action's end | the same (`beginResult` once the action's show is over) | unchanged |
| A script's `Exit` | the way out from the update after `Command_End` | the show's end and the script awaited, then 1.5 s more in story play before the way out | the way out at once: `stepBattleShow` no longer waits on the show or the script once the rules have exited, and the flow's 1.5 s hold is skipped |

The port calls `Command_End` (`BattleRules::endAction`) only in its `ActionEnd` step, after the
action has been shown, so the exit never cuts an action short.

### The result window: its steps, update by update

`TestApp::onUpdate` calls `GameMain::Update` once each 0.05 s at most - the battle too runs at 20
updates a second. `BtlResult::Update` runs a step a update from a table (`BtlResult::BtlResult`:
0x5bf95c, 27 steps): 0 .. 6 the victory (above), then 7 a tap or past 5 s, 8 the finger up, 9
`ShowResult` (the HUD gone, the window, 0x10 "Victory!", the EXP and SP, the first of
`ProcessLvup`'s 60 counts), 10 past 0.3 s, 11 SE 0xc5 looping, 12 `ProcessLvup` (a 60th of the EXP
and HP each update, a tap all at once; then the SE stopped), 7, 8, 13 `ProcessLvup1` (the first
member's levels, 0x11 / 0x12 and SE 0xc3 - none: straight on to 14), 7, 8, 14 `ProcessLvup2` the
same for the second, 7, 8, 15 `ProcessResult` (the line cleared; the SP, Oz and drops given), 7, 8,
16 the messages (each: the finger up, `ClearResultMessage`, the line and its SE, then a tap - no
time limit), 17 `HideBattleResult`, 18 the end, which `StepResult` sees. Step 7's time counts from
its last reset (steps 9, 10 and 7 itself reset it), so the first wait after the count ends 5 s after
the 0.3 s, not after the count. A loss (`BtlGameover::Update`) was already as the original: the
lose camera, the music stopped, 1.1 s, BGM 18, a tap, then the field or the black fade and the
question.

| | The original | CR2-U before | CR2-U now |
|---|---|---|---|
| The window | two updates after the victory's tap (steps 8, 9) | at the tap | two updates after |
| The EXP going in | a 60th with the window, the SE 0.35 s on, the rest from 0.4 s, a 60th each update (3 s in all) | after the window's opening animation, over 2 s ("60 frames at 30 a second") | `resultStep`: the table's steps one each update, the count 60 updates |
| After the count | a tap or 5 s from the 0.3 s' end, the level-up lines each a tap or 5 s, the line cleared for a tap or 5 s, then the messages | a tap or 5 s from the count's end, then the level-up lines and messages together, each a tap or 3 s | as the original |
| The messages | a tap each, no time limit | a tap or 3 s | a tap each |

### An escape, and the result window's close

`BtlCommand::StepEscapeLoop` rolls `BtlCalc::Escape` once the yes / no is answered: away, the
announcement 4 and `PlayVoiceEscapeSucceeded`, step 10; `StepEscapeSuccess` (the update after)
zeroes its time, and `StepEscapeSuccessLoop` waits for 2 s, clears the line and sets step 0x1f
(`cr3_battle_post[8] = 1`); `BtlMainCtrl::StepSetCommand` sends 0x1f to step 7, `StepBattleOut`, whose
`NextMode` starts the way out. A failure waits 1 s (`StepEscapeFailLoop`) and the turn goes on.
Neither has a pause after; nor have a win (`StepResult` at the result's step 18) or a loss
(`StepGameover` at `BtlGameover`'s step 6): each goes to `StepBattleOut` the update after.

`HideBattleResult` calls `InterfaceMain::EndResultWindow`, which hands every window
`SetParamNumber(0x22, 3)`: `ResultMenu` takes it - `InterfaceCloseAnimeSet` (its `menurslt` and
`seatrslt` parts backwards, `+0x5c 2`, from the frame they stand at; the EXP and SP numbers
deleted) - and passes its children theirs: `SystemWindowMenu` 4 and `CharaStatusWindowMenu` 1, each
its own `InterfaceCloseAnimeSet` the same way.

| | The original | CR2-U before | CR2-U now |
|---|---|---|---|
| Away | the way out about 2.15 s after "got away" (the update after, 2 s, step 0x1f, `StepBattleOut`) | 2 s, then the port's own 1.5 s | 2 s and three updates; no pause of its own |
| A win, a loss | `StepBattleOut` the update after the result's end or the loss's tap | the same, behind a 1.5 s hold that had always run out by then | no hold |
| The result window's close | each part backwards from its frame, the numbers gone at once | the window kept whole through the way out's fade | the same: each part's time `min(open time, its length) - time since the close` |

### The leader's turns, and its stop under the wipe

`FldDirectionManager::Update` (from `FldPlayer::Update`, `FldNPC`'s and `EvtModel`'s alike) slerps
the model's rotation from where it stands toward the target by `i / n` at its i-th update, the
shorter way round: the share of the whole turn done is 0.1, 0.28, 0.50, 0.70, 0.85, 0.94 .. after
one, two, three .. of 10 updates - quick, then easing in. The port turned an NPC, the leader and an
event model in a straight line over the updates.

Round 6 read the 10-update turn as the marks' own (`CheckGimmickStart`). It is not:
`FldMain::SetPlayerRotationToBalloon`, called each update after `CalcNearestTouchActionMarkNum`,
takes the nearer of the nearest balloon (`FldBalloon::m_nNearestBalloonNum`, its distance at
+0x28) and the nearest touch action's mark (+0x804, its distance at +0x18), and when
`FldPlayer::m_bIdleTrig` is set - `SetIdleAnim`, the leader just stopped, cleared by the next
`FldPlayer::Update` - or `FldMain +0xa2` - `ChangeCharacterExecute` as the new leader's lighting
begins - turns the leader to it with `FldPlayer::SetDirection(.., 10)`. A walk begun stops it
(`StopDirection`). The sprint sets the leader's rotation outright at each hop (`FldSprint::Execute`),
as the port does. (The same nearest kind also moves the camera halfway toward a balloon or mark of
kinds 0, 1, 3 and 5 - `FldMain::Update`'s tail -, not looked at here.)

Step 0x1c sets `FldPlayer::m_bAnimationStop`, and `FldMain::Update` then skips `FldPlayer::Update`
whole - the motion, the ground effects, the turn - until the battle.

| | The original | CR2-U before | CR2-U now |
|---|---|---|---|
| A scripted turn (`ChangeDirection`, the leader's, an NPC's, an event model's) | 0.1, 0.28, 0.5 .. of the turn after one, two, three updates | in a straight line | `directionShare`, shared by the three |
| The leader stopping by a balloon or a mark | turned to the nearer over 10 updates | not turned | `startIdleTurn`: the same, from the update it stops; a walk, a scripted turn or a new area stops it |
| A change of leader | the new leader turned to the nearest | not turned | turned (from the swap: the original's two updates later) |
| Under the encounter's wipe | the leader frozen from step 0x1c | its idle motion going on | its motion time held from the hit's third update |

### The field camera's ease

`FldMap::CalcScroll`'s decompilation drops a block as unreachable; its assembly (`0x3bf634`)
does not: the target from `CalcNewScreenPos`, the scroll (`+0x30 / +0x34`, unclamped) moves by
`(target - scroll) x FldMap +0x10` when the distance is over 1 (a screen pixel), else by 0 (the
constant at `0x3bf6f0`), then `SetDirectScroll` and `CalcScrollStop` as before. `FldMain::Update`'s
tail sets the target and +0x10 each update: the leader's model 0.045 above its feet, 0.2; or, when
`SetPlayerRotationToBalloon`'s nearer target (+0x84) is a touch action's mark of kind 1, 3 or 5
(`CheckTouchActionMark`: 1 the high jumps, 2 a list counted at +0xbf80, 3 the mystics, 4 the iais, 5
the aims, 6 the hakkeis; a balloon's kinds are 7 .. 11) and +0x80 is set, the point halfway from the
leader to the mark (y from the feet), 0.1. +0x80 is cleared with no target only while the leader
has control, so an event keeps it. `FldMain::SetMapCameraAtNewArea` (from `Init` and
`ChangeAreaExecute`) puts the scroll on the target at once; a script's camera sets it outright
(`FldUtilSetFieldMapCameraPos`, `ExecuteCameraMove`), so mode 0 eases back from where that left it.

| | The original | CR2-U before | CR2-U now |
|---|---|---|---|
| Following the leader | 0.2 of the way to the target each update, held within a pixel; 0.045 above the feet | centred on the feet every frame | `updateFieldCamera`: the same, at 20 updates a second, drawn on between them |
| By a high jump's, mystic's or aim's mark | halfway to the mark, 0.1 an update | the leader | the same |
| A script's camera let go (`SetMode(0)`) | eased back to the leader | at the leader at once | eased back |
| A new area | on the leader at once | the same | the same |

### The menus' and touch actions' calls

`FldMain::CallEvent(name, fade)` sets step 0x1b and +0x28 = 0x11 (no control), calls
`SetEventStep(fade, -1)` (`TouchFinish`; with `fade` a `Cr3FadeOut(0, 20)`) and queues the name for
`EventExecute` - the function runs from the update after. The decompiler shows its call sites as
`CallEvent(this, true)`: the name in `r1` read as the flag. The ARM code (`FldMain::Init`'s four
and `FldMain::Update`'s fifteen, disassembled from the same library with `llvm-objdump`, one site's
bytes checked against Ghidra's) passes `r2 = 0` at every one - no `CallEvent` fades. The world
select (step 10, `WarpTask`), the chapter select (0xe), the shop and camp's, the pub's
(`pub_checkA / G`), the party's (`party_talk`, `party_end`) and the touch actions' successes (steps
0x16 .. 0x1a: the mark's gimmick's own function) all end through it. The library does not:
`FldLibrary::Execute` ends in `CallAtMenuFinished` (step 1), and an entry's `library_disp` is called
from the menu itself (`InterfaceEvent::CallScript`), at once.

| | The original | CR2-U before | CR2-U now |
|---|---|---|---|
| A menu's or a touch action's function | from the update after (`EventExecute`), no control meanwhile | called at once | `queueEvent`, as a balloon's tap |
| The library's `library_disp` | at once, from the menu | at once | unchanged |

### A choice's aftermath, and a message's voice under the skip

`EndBattleYNChoice` hands `BattleYNChoice::SetParamNumber` its 2: the buttons' close
(`InterfaceCloseAnimeSet`) and `InterfaceMain +0x1c = 20`. `InterfaceMain::Update` counts it down
into `BtlInterface +0x1b50` - 1 while it runs, 2 on its last update, 0 after. Only the battle reads
that flag (`BtlCommand`'s option, advent, awake, item, getset, sopia, command, escape and target
loops; `BtlGameover::Update`: the answer acted on while it is 1); nothing in the field does. The
field's choice holds nothing off: as the port has it.

`EvtWindow::ShowMessage` plays the message's voice (`PlayVoice(+0x216c)`) only when `IsSkip` is
false and +0x2169 clear; `IsSkip` is the window's own fast-forward held, or `EvtSkip`'s button
visible and touched. (The same test types the lines - 1.0 - or puts them up whole - 0.)

| | The original | CR2-U before | CR2-U now |
|---|---|---|---|
| A voiced message set with the fast-forward or the skip button held | no voice | its voice | no voice (the test `UpdatePage`'s race already used) |

### An event box's last step

`FldMain::Update`'s step 2 runs `PlayerMove` (the stick into its locals) and then
`CheckCollisionEvent`: a contact the physics found at the end of the last update runs
`CollisionEventExec`, which takes the control away (+0x28 = 0x11). The tail then zeroes those locals
whenever +0x28 is not 1, before `FldPlayer::Update` moves the leader - so the leader does not move
in the update after the contact. The port's queue takes the control from the contact's own update,
so its next update moves nothing either: the same. (Round 7's "one more step" was wrong.)

| | The original | CR2-U before | CR2-U now |
|---|---|---|---|
| An event box's contact | no move the update after | the same | unchanged |

### The lines not yet typed: against the reference video

Rounds 17 and 18 read `NarrationWindowMenu`, `VistaConversationWindowMenu`,
`ConversationWindowMenu`, `SystemWindowMenu` and `HelpWindowMenu`: `SetStringData` steps a new
line's `MenuText` once (`AnnouncementWindowMenu`'s does not), and `Update` types the lines in turn,
stopping at the first not done. From that they took every line of a page to show its first
character from the start, and the port was made to. The reference video says otherwise: its
first video (`BEf8y6MgOoE`, a cut-in talk on Android) at 473.0 s shows "Now let's g" on the first
line and nothing on the second; at 473.5 s the first is whole and the second reads "We're gonn".
`EvtWindow::ShowMessage` hands the window a line only once the typing has reached it (its loop
stops past the current block), so the lines below do not exist yet. The change is undone: the
port's lines below the one typing stay empty, as they always were. (Round 18 also found that
round 17's version never took effect - it asked for the narration by a window id its layout does
not set.)

| | The reference video | CR2-U (rounds 17 - 18) | CR2-U now |
|---|---|---|---|
| A two-line cut-in page typing its first line | the second line empty | the second line's first character | the second line empty |

### Earlier rounds against the reference video

Round 19 checked code-read changes against the reference playlist's first video (`BEf8y6MgOoE`,
Android): frames sought in the built-in browser, read as pixels, the background's shift between
frames found by block matching.

- **The camera's ease (round 14): confirmed.** A walk from 556.2 s: the scroll ramps up over about
  0.3 s to 8 - 10 pixels an update, and after the joystick goes (between 558.85 and 559.0 s) it
  tails off over about 0.6 s, halving each 0.1 s or so - not a dead stop; the leader, still,
  drifts back to the screen's middle as it does. A camera fixed on the leader would stop at once.
- **The encounter's wipe (round 8): corrected.** The hit at about 496.9 s: the black sweeps in
  right to left, streaked, from 496.97 to 497.23 s; the battle's uncovering does the same from
  498.83 to 499.1 s - each about 0.27 s, `encount_in` / `encount_out`'s own 0.25 s. Round 8 read
  `FadeWindowMenu`'s speed (`+0x60 = 10 / frames`, 0.5 for 20) as half speed and made each 0.5 s;
  the port's `kWipeSeconds` is now 0.25. (Black between them 1.6 s in the video: the battle's
  loading, which the port has not.) The port's own frames (`--battle 57`, 0.06 - 0.3 s): the same
  sweep, clear by 0.3 s.
- **The leader frozen under the wipe (round 13): not verifiable here.** The camera still scrolls
  into the wipe, and the black covers the middle within about 0.2 s; no frames separate the
  leader's motion from it.
- **The result window's close (round 12): consistent.** At 542.5 s the window slides out as the
  battle goes white. The EXP's 3 s count (round 11) is not visible: the player taps it short.

| | The reference video | CR2-U before | CR2-U now |
|---|---|---|---|
| The encounter's wipe, each way | about 0.27 s | 0.5 s | 0.25 s |

## Original-code evidence

Fresh REA / Ghidra static inspection of the Android 2.1.2 library (`provider_id: ghidra`,
Ghidra 12.1.2); functions by their exported names. Evidence IDs are the REA sessions' (nineteen: the
second from the `FldMain::Update` row on, the third from the `FldUtilSetPopPoint` row on, the
fourth - the cut-ins - from the `InterfaceMain::SetCutInWindow` row on, the fifth - the first
field - from the `VistaConversationWindowMenu::SetParamNumber` row on, the sixth - its talks -
from the `Cr3EvtWindowShowYesNoButton` row on, the seventh - its events - from the
`FldMain::CheckTouchEventExec_Tap` row on, the eighth - the encounters - from the
`FldEncount::Update` row on, the ninth - the battle's way out - from the `BtlMainCtrl::StepBattleOut`
row on, the tenth - a battle's end - the `BtlMainCtrl::StepAction` row, the eleventh - the result window -
from the `BtlMainCtrl::StepAction / StepGameover` row on, the twelfth - an escape, the window's close -
from the `BtlCommand::StepEscape` row on, the thirteenth - the leader's turns - from the
`FldPlayer::SetDirection` row on, the fourteenth - the camera's ease - the `FldMap::CalcScroll` row, the fifteenth - the menus' calls -
the `FldMain::CallEvent` row, the sixteenth - the choice and the voice - the `EvtWindow::ShowMessage` row, the seventeenth - the
lines' typing - the `MenuText::Step` row, the eighteenth - the other windows' lines - from the
`VistaConversationWindowMenu` row on; the conclusion both drew was overturned by the reference video; the nineteenth - the wipe - the
`FadeWindowMenu` row).
`re/opening-audit-evidence.json` (never committed) holds the second session's records,
`re/opening-audit-evidence-3.json` the third's, `-4.json` the fourth's, `-5.json` the fifth's, `-6.json` the sixth's, `-7.json` the seventh's, `-8.json` the eighth's, `-9.json` the ninth's, `-10.json` the tenth's, `-11.json` the eleventh's, `-12.json` the twelfth's, `-13.json` the thirteenth's, `-14.json` the fourteenth's, `-15.json` the fifteenth's, `-16.json` the sixteenth's, `-17.json` the seventeenth's, `-18.json` the eighteenth's and `-19.json` the nineteenth's; the first's bundle was overwritten by the second,
so those IDs name records no longer on disk - each is a plain decompile of the named function,
reproducible with `open_binary` and `batch_decompile` on the same library.

| Functions | Evidence ID |
|---|---|
| `GameMain::Update`, `Cr3FadeIn / Out / In2 / Out2`, `Cr3Fade::FadeIn / FadeOut / Update`, `WaitProcessor::StartWait / ProcessWait`, `MVGL::SqUtility::ScriptManager::Update`, `Cr3UtilGetTouchState` | `ev_24e0f0b656fd6ff2ddeac287e11197d75355c8db322ae34eee82735c498d610e` |
| `EvtWindow::Open / Close / CheckMessage / IsSkip / UpdatePage / ShowMessage / SetMessage / Update / PlayVoice`, `Cr3EvtWindowOpen / Close / CheckMessage` | `ev_df0ebf41057ffc0ee952f708bd1b54f07fdcc07637f5fe74a20ca4e44b4c462d` |
| `EvtWindow::NextPage / CheckDisplayEnd / StopVoice / SetSkipButtonEnable / SetWaitButtonEnable`, `EvtWindowMessage::CheckMessage / NextMessage / ReadPage / SkipMessage / LoadMessage`, `EvtWindowPage::NextBlock / Skip / GetBlock` | `ev_d750382cbf3f607a3faf3108d24bac1ab0b99a424d91f4f9beb24e80d2bc9b50` |
| `Cr3EvtSceneChangeAnimatorBase / ChangeAnimator / ChangeAnimatorBlend`, `EvtMotionManager::SetMotion / Update`, `Cr3Face*`, `Cr3Event::Call / Update`, `VistaMain::Update` | `ev_e67a47687356745868bee7af106f5c5a0db3788c533bebc0a64d769e6aac107b` |
| `Cr3EvtSceneChangeAnimator` / `ChangeAnimatorBlend` assembly (r2 = -1.0f, or the script's float) | `ev_811b68d8a48d062583c5cb56d9e3441c62a08211fdfc20f8cc779a96fa54e972`, `ev_ed49e112303f5bba6d09a483ca6fa65a991c3cec18f88097de13d91183c7cdb4` |
| `EvtModel::SetEventAnimDirect / SetEventAnimLoop`, `Cr3EvtScenePlayEffect / PlayEffectLoop / SetVisible / SetMove / SetFaceTexture / ChangeModelFace`, `EvtUtilVistaCameraMove`, `VistaMain::EventExecute / BranchAfterVista / LoadScript`, `Cr3UtilSoundPlayBGM / PlaySE_Repeat / StopSE`, `Cr3UtilSetEffectLine` | `ev_124bf5fd58025e74b89ed71ff876d7efcde502b8699bcbfd9a801b04446bfa1b` |
| `EvtScene::Update`, `EvtModel::Update`, `Cr3EvtSceneSetAnimRewind / SetMotionLoop`, `EvtUtilVistaLightMove`, `MVGL::Interface::PartsBase::Step` | `ev_aeffe75fc52b1dea404102f4558b0df239be9103e86a41389639b1d2b0f1d12b` |
| `MVGL::SqUtility::ScriptObject::Update / CallExecution`, `Cr3EvtSkipButtonCreate / GetVisible / CheckTouch`, `FldUtilReserveBattleIN`, `FldUtilSetVistaFuncName`, `Cr3UtilSoundPlaySE / StopBGM` | `ev_4decf6b8e100939050964e181b7925381e006409056a4c70add78d4f3953d016` |
| `MenuText::Step / SetFontDisplaySpeed / SetSkipFlag`, `NarrationWindowMenu::SetStringData / Update / CheckStringDisplayEnd / CheckStringDisplayEndSend / TouchGestureTap / InterfaceCloseAnimeSet / EndStringDisplay / ClearStringDisplay / SetPageSendPermit / SetFastForwardPermit / Pose` | `ev_09a0fb09dbfeaeca410b79a6a3a56126a3d48708cc6873ca1beac0bed83c0a46` |
| `EvtNarrationWindow::*`, `InterfaceMain::SetNarrationWindow / EndNarrationWindowStringDisplay / SkipMessageWindowStringDisplay / SetFadeIn / SetFadeOut`, `FadeWindowMenu::Update / SetParamNumber / InterfaceOpenAnimeSet / InterfaceCloseAnimeSet`, `Cr3SoundSetFadeIn`, `Cr3UtilSoundStopSEAll / GetState` | `ev_62cd9fc5c40c0335907d406e6f54a3bac0eb7f005f7aa42e355f088475f12830` |
| `FadeWindowMenu::InterfaceCloseAnimeSet` / `InterfaceOpenAnimeSet` assembly (ChangeAnime 2 / 1, time 0.5 / 0, the hold 3) | `ev_1dd67c53ec90b711d3f6932c087c70df4e8f0ffd8d4185596ba6e310f648c36e`, `ev_539ec43f20d647fab0259cb02c0a0fab5dac467c6462ee4fa4bd90beac16b918` |
| Their vtable slots (references at 0x5b21e8 and 0x5b21e4; `FadeWindowMenu::FadeWindowMenu` sets the vtable 0x5b21c8: +0x20 / +0x1c) | `ev_27dae79ba13de53180c2791b6ad4f97bb7378fc5a3141c42e99a4bdd7b927766`, `ev_1f50d68409c019bd9d78730b5a458201518f3463185706e18a32e532398141cb`, `ev_cb4a72f35919c63b3348231318daeacdedb387ccfc7f236a98f6f93f1d1cb8f0` |
| `InterfaceMain::_setParamNumber / CreateInterface / GetFadeWindow / ChangeNarrationWindowMessage / CheckNarrationWindowStringDisplayEnd`, `NarrationWindowMenu::SetParamNumber / Draw / CustomDraw / Initialize`, `PartsBase::IsEndCurrentAnime / ChangeAnimeTime`, `FadeWindowMenu::Initialize` | `ev_92704f9396614f365f834e0709ce9a324ecc45548a3f4f670bd88f4ba452cab2` |
| `MVGL::Interface::PartsBase::ChangeAnime`, `MVGL::Draw::Animator::Step`, `VistaMain::Init`, `InterfaceMain::Update` | `ev_cb4a72f35919c63b3348231318daeacdedb387ccfc7f236a98f6f93f1d1cb8f0` |
| `MenuText::Pose / Render / Initialize / SetTextRender` | `ev_1304653dab8238c76384d1a6597990412a64397214f1e042265ffa6089aa1342` |
| `FldMain::Update` (dossier; its steps 0x1b .. 0x20 read in the repo's own `re/` decompilation of the same library) | `ev_66e02c3f3d21a76896408705075f7faeaf7ccdf9844676d089f764a7f6d54129` |
| `FldMain::Init` (dossier; its mode switch: 0 `Cr3FadeIn(1, 20)`, 1 .. 4 and -2 / -1 `CallEvent`) | `ev_16cc8f45175dc861e50294b4bfca9b4be6581b1f24bff0eaf56d3f3adb1185b7` |
| `FldMain::BranchAfterEvent / EventExecute / WaitFadeFinish / CallEvent` | `ev_1bae0e10210676505d72e555328f0963caed2619606e8669e28d530ef3ee708f` |
| `BtlMainCtrl::StepBattleIn / StepBattleOut / StepEnd`, `Cr3BattleEvent_Exit` | `ev_84d2de9a02350b8e40792ef17e5428f922fd05d68299225df2db4026ed2d0de7` |
| `NarrationWindowMenu::NarrationWindowMenu / TouchSimpleRelease`, `InterfaceMain::IsNarrationMessageWindowFastForward` | `ev_e97798cb339467d2a1c0a7a3e0b5ecb882a5f3506df7b8bd7a1bfe0130731149` |
| `FldUtilSetPopPoint / ReserveChangeDungeon / ReserveChangeArea / GetChangeDungeon / GetChangeArea / ClearPopPoint / GetPopPoint` (with `FldMain::Update`'s steps 0x10 .. 0x12 and `ChangeAreaExecute` in the repo's decompilation) | `ev_7f28871e2b85ab7aa6a6ca16d71d4a1f1f43c01fe9dd115f0652e8eafac1edcb` |
| `VistaConversationWindowMenu / ConversationWindowMenu / HelpWindowMenu / AnnouncementWindowMenu / SystemWindowMenu::TouchGestureTap` | `ev_4763b23ca1e1ae91b8c10c0da52d6599c6064da2ff0b1bc6f0be124569dd4dad` |
| `TitleMain::Update / Init` | `ev_3f6ef182c41c007647af49a74b3b884be04fe70326ac4eaef168d1857eadccbd` |
| `InterfaceMain::SetCutInWindow / EndCutInWindow / ChangeCutInWindowTexture / ChangeBrightCutInWindowTexture`, `CutInWindowMenu::Initialize / Update / Pose / InterfaceOpenAnimeSet / InterfaceCloseAnimeSet / SetParamNumber / SetTexture / ChangeDiffuseColor / PositionCorrection` | `ev_fbfcf1d930795173abbd7023113a4a27917449e811d21a50f9bc300b1f173b9a` |
| `ConversationWindowMenu::Initialize / Update / InterfaceOpenAnimeSet / InterfaceCloseAnimeSet / SetParamNumber / EndStringDisplay` | `ev_e4e9ae3fc1d421d6f9da59b17beb135fa5a1d106c73144c4d16a23952fc71661` |
| `ConversationWindowMenu::SetStringData / CheckStringDisplayEnd / CheckStringDisplayEndSend / Pose / CustomDraw`, `EvtCutinWindow::Open / Close / IsSkip / SetMessage / GetLinePerPage / GetTheme` | `ev_1907bb34814a71c653cf9681996d6f8f6d4ba4557c685131879d70b81647abb4` |
| `InterfaceMain::SetMessageWindowParameterEmpty / EndMessageWindowStringDisplay / ChangeMessageWindowMessage / IsMessageWindowFastForward`, `Cr3UtilCheckHitTapCollision`, `VistaConversationWindowMenu::Initialize / Update / InterfaceOpenAnimeSet / InterfaceCloseAnimeSet` | `ev_6022cb605dfb1f3b30d86fe58f82bcf741dab7aba6f5ccd82bd3e85a904419d9` |
| The fade colour tables at 0x51e208 (48 bytes) | `ev_e855b7b8c9e85282a529f88d42e04c330c84f2b6e9fa41df062d5857225339c9` |
| `VistaConversationWindowMenu::SetParamNumber / SetStringData / CheckStringDisplayEnd / CheckStringDisplayEndSend / EndStringDisplay / ClearStringDisplay / TouchSimplePress / CustomDraw` | `ev_d4cb574606669f099bae4dfc2ee7e5e7685f0696495538c0c991149fb5a1dcb2` |
| `EvtVistaWindow::Open / Close / SetMessage / IsSkip / GetLinePerPage / HasNameTag`, `InterfaceMain::ChangeVistaMessageWindowMessage / EndVistaMessageWindowStringDisplay / ChangeVistaMessageWindowString`, `VistaConversationWindowMenu::Pose` | `ev_0dd8341350cfb5b2e5e15ade79b95ff9a8cde681c0493b728de01cec1c23bd1c` |
| `HelpWindowMenu::Initialize / Update / SetParamNumber / InterfaceOpenAnimeSet / InterfaceCloseAnimeSet / EndStringDisplay`, `InterfaceMain::SetHelpWindowParameter / EndHelpWindowStringDisplay / ChangeHelpWindowMessage`, `EvtTutoWindow::Open / Close / IsSkip / GetLinePerPage` | `ev_31483907d746d26e7a8ab2ea0a27ac26888031230c3b4741556636b39e66f1af` |
| `FldUtilSetTutorialWindowCreate / Delete / Visible`, `FldUtilLoadTutorialTexture`, `FldUtilSetTutorialTexture`, `InterfaceMain::SetTutorialTexture / SetTutorialTextureNumber`, `CR3InterfaceEvent_*`, `Cr3SetIntroductionDungeonNum`, `Cr3IntroductionGetVisible` | `ev_fe266d31bbd39e46c83eb3dd28fc435288fc883d44db7ccbf922eb9ac62a224e` |
| `InterfaceMain::SetVistaMessageWindowParameter / DeleteNamePlateVistaMessageWindow / SetMapNameMenuParameter / SurvivalMapNameMenu / CreateInterface`, `VistaConversationWindowMenu::DeleteNamePlate / VistaConversationWindowMenu`, `HelpWindowMenu::HelpWindowMenu` | `ev_d7d4bcd0949f97de5388d9b216d641320aa134b08f415fd43af0525bbe0baa3d` |
| `MapTitleMenu::Initialize / Update / SetParamNumber / MapTitleMenu`, `SystemWindowMenu::SystemWindowMenu` | `ev_236c02124e43ca577b9ec0883721aeef7a308b27ce3855ebf9b7ca86915aea50` |
| `InstructionsGraphicWindowMenu::InstructionsGraphicWindowMenu / Initialize / Update / SetParamNumber / Draw / CustomDraw`, `VistaConversationWindowMenu::Initialize / Update`, `InterfaceMain::IsVistaMessageWindowFastForward`, `MapTitle::MapTitle` | `ev_0a3af3f78379faa90b669104ae97af633510804f67b3517324626d8244fd38b7` |
| `VistaConversationWindowMenu::Draw`, `InstructionsWindow::InstructionsWindow`, `InstructionsGraphicWindowMenu::SetTexture`, `InterfaceMain::VisibleTutorialTexture / UnVisibleTutorialTexture` | `ev_fba12c881fb62dc972a9a6e8d7e67370770f1ca62dc9bf52b7553217efbb8eb8` |
| `MapTitleMenu::Draw`; `MapTitleMenu::Update` assembly (it returns `IsEndCurrentAnime`) | `ev_31a5d559b0a9f014e7bd8a352e2f53f331dc2ef8bf7bd1ce15d72efe075edfcb`, `ev_34d07dfdb11bd597b1d8592c920756f8d3e6e57ac004684f7c247998b76f9249` |
| `SetMapNameMenuParameter` / `MapTitleMenu::Initialize` assembly (their tables by literal pool), the dungeon table at 0x5343f0 (56 bytes), the figure and suffix pointers at 0x5af508 (84 bytes), the strings at 0x50a300 (132 bytes) | `ev_3c797e0ba9a36dfe19b57d3596e954add6a1c36e944b646dd2da0955a4da057f`, `ev_6db27395a776c06307e37903bb5e32c7664d1ffd7ac24c4522d79f25d9bfa7a4`, `ev_036501e5427eb1a2e8ecd2447cd34576f20bb97a588405451d58cba1c78f42e0`, `ev_14d2761bece9b2053480b5526b50bbb928b12898f6832a0fba3106bd401fbd99`, `ev_7f5b3cb49f1b51c646f5aa95806d62fa30c622bfffce84d7fe8dd4c005b5066f` |
| `InterfaceMain::Update` (an interface whose `Update` returns 1 is marked and deleted) | `ev_0c1ff9839f42e851e0b67ff97e989e99905edbe6d9686dfbf3796b610f686477` |
| `MVGL::Interface::PartsBase::AddAnimator / IsEndCurrentAnime / ChangeAnime` | `ev_46bf45fb1662ad71678b209887e4c05b7f1616b00286b24f24f0da680b91b45c` |
| `HelpWindowMenu::SetStringData`, `MenuText::Initialize / SetTextOffsetData / SetPosition` | `ev_f5bcd7f46cb8c7a6ffe3ec2b9e04d618cfb76f0fad6783ae986702a7b59a0578` |
| `EvtWindow::NextPage / UpdatePage` | `ev_2e5df66f3aea72a734cf89e67c0feac63ca8f7519954afc90a1becf90f528abd` |
| `EvtWindow::ShowMessage`, `EvtWindowPage::NextBlock`, `EvtTutoWindow / EvtAnnounceAWindow / EvtCutinWindow / EvtNarrationWindow::SetMessage`, `InterfaceMain::ChangeHelpWindowMessage / ChangeAnnouncementWindowMessage` | `ev_6d366c6a4a69109147b9867a31bfcf9eb0473fca3aba692a0fe8c63bd9daccfe` |
| `EvtTutoWindow / EvtVistaWindow / EvtCutinWindow / EvtNarrationWindow::GetTheme`, `ConversationWindowMenu::SetStringData` | `ev_715dfadfdb946b2c6026b4c97504c227901f3d8d6242d0811958c53d32c86430` |
| Procedure and string searches (the menus' names, `Tutorial_*`, the `dNN` suffixes), the `SetMapNameMenuParameter` reference | `ev_da625937e8a1d1435cdf172a6b693f4a00395f9c9c625fa630cc6a4f68563a4b`, `ev_93911e3803cd0b83f17d25688331607f04e12fa27c9236cd2502f55a51664a9b`, `ev_233c95d388de9f859445db5a42a01c3c00d98e72798ce9de02a80f3d0217d3d7`, `ev_34846b7b5c20236832a4602af2d7af28b95adf54a7fa93359f3aa86e85568030`, `ev_9b85ec1829acacd78611ae4cf1d986ab90bb3d512d2d0dacc539024d9006b530`, `ev_ba615bbd95aea9f925d20d5737ddf05575416362ac704d78a84095296a8566ad`, `ev_dafbf52e0c9a5bd2851e9b685839f6b97196fce7bb0e65c0ed1ecec418237f2a`, `ev_f2b27739daa943003e4778236291da6b36b9993dd7e979fe81d1d5c19c50fccb`, `ev_102fbdfcb409aa329e5e876bd8295261a74ffa1755719fd6c343f198019608b7`, `ev_118826059e0a3942feeeb224075785a568e75426e350edb0504df15d08704b68` |
| `Cr3EvtWindowShowYesNoButton / HideYesNoButton / CheckYesNoButton`, `FldUtilSetFieldMapCameraMode / SetFieldMapCameraPos / ConvertFieldMapCameraPosForScriptInput / SetFieldMapPlayerVisible / SetFieldMapPlayerPos / SetFieldMapPlayerRot / ChangeFieldMapPlayerDirection / SetFieldMapCameraMoveCalcParameter` | `ev_0df2463e7aaa884d9d7b06cb3da4f5e2df8086b66e9f2d4403cbf59081843dae` |
| `Cr3EvtSceneSetFieldMapPos / Rot`, `FldPlayer::SetDirection`, `FldUtilChangeNPCDirection / ChangePCDirection`, `FldMap::SetDirectScroll`, `InterfaceMain::SetYNChoiceWindowParameter / EndBattleYNChoice / GetBattleYNChoicePushBottom`, `Cr3Util_Save_ContData / Commit_ContData / Save_SystemData` | `ev_21d48e1d84f3095ee3569bbf301d1920ab730defe621872afbcbcaf8f99dcac8` |
| `BattleYNChoice::Initialize / Update / SetParamNumber / TouchSimplePress / TouchSimpleRelease / InterfaceCloseAnimeSet / Pose`, `BattleYNChoiceControl::Update`, `FldDirectionManager::SetDirection / Update`, `FldMap::CalcScrollStop` | `ev_47ab0d732fc121b9b444eba5098822d79ba944b73091d8736a3345e51f852f65` |
| `FldMap::CalcScroll / ExecuteCameraMove / SetScrollStop / InitCamera / CalcCameraMove_Param`, `BattleYNChoice::BattleYNChoice`, `CircleBtn::CheckTap` | `ev_117e19eddbc9c4c234cecd71a94e8603755704a1587cfb65879dd211e3aea465` |
| `FldMap::CalcNewScreenPos / Update`, `Cr3UtilGetPlatform` (1) | `ev_7d12c359ea9e1b680764202053f561f9badce92930ac1705914e25bf1aed331e` |
| `FldMap::SetCameraMove_Frame / InitCameraMoveWork / SetCameraMove_StartPos`; `FldMap::ExecuteCameraMove` assembly (`start + step x (frames - remaining)`, then `remaining - 1`) | `ev_2cafdf08fc2fab7dd3b69fab058740489521993dcad49a2fd03e6b1e8ce28e95`, `ev_d58f528edafa9d01136ebbd04aa1312fca86d1800d57ec7d522266c6c9daee95` |
| `FldUtilGetFieldMapCameraPos_X / _Y`; `FldUtilSetFieldMapCameraPos` and `MoveCalcParameter` assembly (the negations) | `ev_ff113d4ff6dc7d77aff7a8fe603a9d76abb8820c30fdcbb695bef57d7757fd2b`, `ev_35bb82489d3df8bc8079b5c279e5d4c0afdacb0df51ebc73c81e7e12132eceb3`, `ev_bcd48d7d7e6aaa692f45f10de79bd0249cb0abbad9863ccac570cdea4b9d9efd` |
| Procedure searches (the save, camera, player and choice natives; `FldMap`'s camera methods; `BattleYNChoice`, `FldDirectionManager`) | `ev_ce49c2dd1440032cc91d60062a96637b45be21a55c72a08a199f48ee66780bdb`, `ev_194c6cfd1efe763e206e44d5ab3cb68090fab983994c48844ded520d997615e9`, `ev_e7eeec6d10d2a6e257bb6245ee4107a806fd163022b5ca851f205fdd750d6206` |
| `FldMain::CheckTouchEventExec_Tap / CallEvent / CollisionAdded (and its thunk) / CheckBalloonOne / CheckBalloon / CalcNearestBalloonNum` | `ev_a0b2a2fcea4e06d3c26b3c466ffc00ff5ae3d9597ea90f5319182b39868040f5` |
| `FldMain::EventExecute / BranchAfterEvent / CheckCollisionEvent / CollisionEventExec / SetEventStep / CheckGimmickStart` | `ev_e1ca27669aa58f5fb54a86858c96a3a1ceacceffd070678016d0b8dd9edea534` |
| Procedure search (`FldMain`'s touch, balloon, collision and event methods); `FldMain::Update`'s step switch read in the repo's own decompilation of the same library | `ev_39411471f573f9d842cd79b5a692c583187e3c890bd25cfb0e07ea6e95d2638c` |
| `FldEncount::Update / Pickout / SetData / Reset / SetPosition / SetBattle / CheckEncountInterface / SetInterfaceMark`, `FldMain::SaveWorkBeforeBattle` | `ev_b9be37d33dd8f8c17f7b7bd27a768953c8e7dc198c6d9cc14f9c4648332f0759` |
| `Cr3FadeOut`, `Cr3Fade::FadeOut / Update`, `FldMain::WaitFadeFinish`, `FadeWindowMenu::SetParamNumber` (speed 10 / frames; type 0x14's 0.6) | `ev_a991701993bbd7b6880d03d55a4c0cefd92699563791f06673ab32e1438129e9` |
| Procedure search (`FldEncount`, `SaveWorkBeforeBattle`); `FldMain::Update`'s steps 0x1c .. 0x20 and its call of `FldEncount::Update` read in the repo's own decompilation | `ev_1e4a3345aba4a363569c163370e794f3ff18e4e3210b24804a5052353bfc6cfb` |
| `BtlMainCtrl::StepBattleOut / StepEnd / Update / StepResult`, `BtlMain::Exit / UpdateStep / Update` | `ev_70246b38943dcebe2e9bd44ff2bf5677377da103e857851ea7ed32f75d93e9f1` |
| `BtlMain::NextMode`, `BtlMainCtrl::SetNextStep`, `BtlResult::Update / UpdateResult` | `ev_16c722dd1c8c0edc6f2a55e96469a2f223fbe94c3c0279b5455e4266a2b36c7c` |
| `BtlWork::InitializeVariables / Initialize` (+0x1a starts 0), `Cr3BattleEvent_Exit`; `Cr3BattleEvent_SetParamI` (no +0x1a); `BtlGameover::Update`'s +0x1a = 1 read in the repo's decompilation | `ev_ecf88876626d0e2ea8231bb2b7243f4cda497c63be1c9b718f847ef5c50bc3f1`, `ev_0e0a54279243a90104c380956f92ffce55f9f6af55242e312fb99a60a595b910` |
| Procedure searches (`BtlMainCtrl`'s steps, `BtlMain`, `BtlResult`; `BtlWork`'s set-up, `Cr3BattleEvent_*`) | `ev_1456ddd23f43bc6c706a6bd3197ed1e145cfd648824069cd324df4ac1a484716`, `ev_4af47a3bc0b309317076611a34bce82b12fea577a167f0ef71987c326cd7ebf3` |
| `BtlMainCtrl::StepAction / StepTurnEnd / StepGameover` (the win, the loss and the next turn decided at the action controller's state 5) | `ev_7285beee371ea0bb77870c089477df77938107125cd30772fd8e7d3d6eda145f` |
| `BtlMainCtrl::StepAction / StepGameover / StepResult / Update`, `BtlResult::Update / UpdateResult`, `BtlGameover::Update` | `ev_c56db03aa6af187aeb8bc9b8f4a04ac0680c7ae2b2d86d308f768257709f9686`, `ev_a4e64dff97db82ccf3ef91f1b642b6bda76c522a65fc97eaaa62cf1eff2cdddc` |
| `BtlResult::BtlResult` (the table's address and length), `Initialize`, `ProcessLvup / ProcessLvup1 / ProcessLvup2 / ProcessResult / ShowResult`; the table's 27 steps read at 0x5bf95c | `ev_b1bfb069c74fc812d6efdfd1148052f8f0600a1f0d14bd84db390bf356da4d8e`, `ev_8ce0f222fe27b4c8a301efae53c2f2746b2239ffb7e0b2b2905ed26b80fb0cc5` |
| `GameMain::Update`, `BtlMain::Update`, and their caller `TestApp::onUpdate` (an update each 0.05 s at most); procedure searches (`BtlMainCtrl`, `BtlResult`, `BtlGameover`, the main updates) | `ev_68da5eccf0c861bbb3ec1c0a417850c9d7b3800c585aff41d4c40dc9a833ed71`, `ev_67e1d29e92637d050b5bc4c23f316d6ba8c465d95f379e367bf16aa9be76dd04`, `ev_d632771cff2af237e1d85688771db511cef311e0961b9838562b32a2b0f63ee0`, `ev_32e0ca568c2b856de62325c42c3b1dc6713c74a9f6580fbd6f4c8bce4c4325f9`, `ev_91b0c08b5c727de5b0bb49c2a6849a08f69f3a2213ecc065f0a8fd9b09f7f55a` |
| `BtlCommand::StepEscape / StepEscapeLoop / StepEscapeSuccess / StepEscapeSuccessLoop / StepEscapeFail / StepEscapeFailLoop / StepEscapeEnd`, `BtlInterfaceCtrl::ShowBattleResult / HideBattleResult` | `ev_ef32cf11bfefd7767e2aa91084cebe491b5bcea66b4f0f18008b1f669ec41b13` |
| `BtlMainCtrl::StepSetCommand / StepBattleOut` | `ev_9a8114037369a7d328ef961f9e11b76f49e038228b833ae212eb92fcae05a314` |
| `InterfaceMain::EndResultWindow / SetResultWindowParameter / _setParamNumber`, `ResultMenu::InterfaceCloseAnimeSet / InterfaceOpenAnimeSet / SetParamNumber / Update` | `ev_8a19771c1b9f723da1f030702ccb053b28f169ed06f1a3e60c53823f1407e8ce`, `ev_1d95b7cf0f4a65e03cfaed92e5f45784734bc670a860c8806a11820f25b182bf`, `ev_370fdcf20eaf076c49edd4b4789d0b54cd8bf1f7a7dee866b1da3220dfe1de96`, `ev_acc72254cac2c60c127f2a7f05cbd5ed0005683ce4943252f1b30f3be8134381` |
| `SystemWindowMenu::SetParamNumber / InterfaceCloseAnimeSet`, `CharaStatusWindowMenu::SetParamNumber / InterfaceCloseAnimeSet`, `CharaStatusWindowBMenu::SetParamNumber` | `ev_b358579b3fda7f6c16f99dff0ee11235b8afb6fa938fd3fc9d6ac128c937e1f2` |
| Procedure searches (the escape's steps, the result window's show and hide; `InterfaceMain`'s and `ResultMenu`'s; the system and status windows') | `ev_5b65c9e4c2bc1a92961aab42be452f5f2186f40102983607375c336f9734b1eb`, `ev_c74c8fe6aeaf35e1a2a6c33dc4202f5cea50362c821bfb3c48ebc28e890382cd`, `ev_57f1be8e693e79fbd3340ad390b7bd2b4e7b9ea8ad1b1424fe06fddf506cd67d` |
| `FldPlayer::SetDirection / StopDirection / Update`, `FldDirectionManager::SetDirection / Update`; `FldMain::SetPlayerRotationToBalloon`, `ChangeCharacterExecute`, `FldPlayer::SetIdleAnim`, `FldSprint::Execute` and `FldMain::Update`'s step 0x1c and its tail read in the repo's own decompilation | `ev_abe9efd4757aa4b90cfc2b5e949a9253c5163ff699ef9f2a6f2a935211fc727e` |
| Procedure search (`FldPlayer`, `FldDirectionManager`); references to `FldPlayer::m_bAnimationStop` (none direct: through the GOT) | `ev_ea47da234de35d0a5f1beec7928c719dc685323363203bef375084d222cd75c9`, `ev_d619f5c88494c40f3a3052122c4adcf02563ab30684eac1ae3a14cd060e67269` |
| `FldMap::CalcScroll` assembly (the ease), the constant at `0x3bf6f0` (0); `FldMap::Update / CalcNewScreenPos / SetDirectScroll`, `FldMain::SetMapCameraAtNewArea`, `FldMain::Update`'s tail, `CheckTouchActionMark` and the gimmick lists' set-up read in the repo's own decompilation | `ev_43aae0a9701d8771873db84b7a70255fc08ca137a5280af649ba2375e7c8c6f1`, `ev_f92a6c94551f5e3f6899e179ac71743ec17fbe4d66aff4576a9b45adefd17812` |
| `FldMain::CallEvent`'s references (19 call sites) and one site's bytes (`0x3bd4f8`: `ldr r1` the name, `mov r2, #0`, `bl CallEvent`); `CallEvent`, `SetEventStep`, `CallAtMenuFinished`, `FldLibrary::Execute` and `FldMain::Update`'s steps read in the repo's own decompilation; every site's `r2` from `llvm-objdump` of the same file. (Also in the bundle: an assembly listing read at the symbol table's address, which in Ghidra's image - 0x10000 higher - is another function; not used.) | `ev_09d1bb918d791e8b263082a5ee53e4c85bc98435bb4412754a5843108505f3d4`, `ev_7c832d843f26cb5ca2ae050df3955bde737525bcef8bd6c119a8f53fe256f9c2`, `ev_c3d8032c24c8fd63157d87fad664ed7580ca48700fa25cc8e20747412983c0f9` |
| `EvtWindow::ShowMessage / IsSkip / PlayVoice`, `BattleYNChoice::SetParamNumber`, `InterfaceMain::EndBattleYNChoice`; `InterfaceMain::Update`'s countdown and the `+0x1b50` readers read in the repo's own decompilation | `ev_73f8b3f12d14a099be5c8235b5842cd4ef8ecb743c6181d6dc507e7e784a497b` |
| `MenuText::Step / Initialize / SetSkipFlag`, `NarrationWindowMenu::SetStringData / Update`; `FldMain::Update`'s step 2 and tail (`PlayerMove`'s locals zeroed off control) and `MenuText::Render / MakeFontSpeedSupportTexture` read in the repo's own decompilation | `ev_8d6c02ce2476fc97b84422d0464e85843128c2ee04f0e7f5cf735804e82079b8` |
| `VistaConversationWindowMenu / ConversationWindowMenu / SystemWindowMenu / HelpWindowMenu::SetStringData / Update`; `AnnouncementWindowMenu::SetStringData / Update` | `ev_ed22a6a097860806f8206195e164024e5c5073598133de2bd18a8492eecf0203`, `ev_3f881bfe982bba58e1dca2579ef1281d209a730a4c3542f80548baf3c1dfc428` |
| `FadeWindowMenu::SetParamNumber / Update`, `Cr3Fade::FadeOut / Update` (the speed `10 / frames`, read again; the video decides its effect). (Also in the bundle: the same request made before the session was open, failed; not used.) | `ev_6924fc7c03adb80158f72486cc463cdad2893f5cff61b7759326a85f78ef5e19`, `ev_0e050e604344dcfa5889357d30f56d6c86c2870f9a2028187a0dbfb37b2e6751` |

These are static observations of the shipped library, not traces of the original running: the
update order and counts follow from the code, the visual result from the fade figures'
animations being linear (assumed, as before).

## Not settled

- **The vista window without a name.** `DeleteNamePlate` (and the close with a name up) turn
  `vstWin_talk` back and put the window's own texture resource (`+0x38`'s `+0x48`) into the
  figure's `+0x44 / +0x3c`, which `SetStringData` slot 0 had set to 0; what that draws for the
  plate was not read. The port poses `vstwin_io` (the plate's joint at x 0) for a nameless line.
- **The camera slide's hold.** That `ExecuteCameraMove` leaves the scroll a step short of its end
  is read from its assembly and `FldMain::Update`'s order (the script's `EventExecute` before
  `FldMap::Update`, in the repo's decompilation); no run of the original confirmed the frame.
  Nothing else moves the scroll in mode 1, so it holds there till `SetMode(0)`.
- **The camera's units.** The ease's one-pixel hold is the original's device pixel; the port
  holds within one pixel of the window it draws to. `GetPos_X / _Y` in the port still read the
  clamped scroll (`shownX_`), the original's the unclamped +0x30.
- **The marks' kind 2.** `CheckTouchActionMark`'s second list (counted at +0xbf80, its pointers at
  +0x178) was not named; its marks take no halfway camera either way.
- **The stop turn's mark distance.** The mark's distance (+0x18 of its +0x400 record) is taken as
  the leader's to the mark's spot; how `CheckTouchActionMarkOne` measures it was not read.
- **A finger left down at an event's end** holds control in step 1; the port's touches start a
  walk only on a press, so one held from the event never walks, and a press during the three
  updates is refused rather than held.
- **The map name's last update.** The original deletes the menu in the `InterfaceMain::Update`
  whose step reaches the animation's end; the port shows it while `ticks x 0.05 s` is under its
  length. Within an update of each other; not counted exactly.
- **The windows' tap areas as the figures stand**: the cut-ins' window's area is read from
  `msgWin` posed as drawn; the original's `Cr3UtilCheckHitCollision` was not read.
- **The battle's last update before the next main.** `Exit` waits for `Cr3ResourceManager` to be
  empty; the port counts 24 updates from the end, as if the resources went at once. A slow unload
  would hold the original's white a little longer.
- **An escape's update count.** `BtlCommand`'s time is taken as counted before its step, as
  `BtlResult`'s and `BtlGameover`'s are, and `BtlCommand` as updated before `BtlMainCtrl`; neither
  order was read, so the way out may start an update or two either side of the port's 2.15 s.
  No story run escaped (the explorer never does); the battle-only view has no way out to time.
- **The result window's close, timed.** It runs during the way out's white fade; no frame of it
  was captured (the explorer's runs take none), only the window's opening, whose parts slide in
  from the sides in about 0.2 s.
- **The skipped voice, run.** No headless run held a skip across a voiced message: the opening's
  two are skipped whole, and `--input fast` takes hold only once a button shows, after the
  message's voice. Checked by reading only; the opening without a skip still voices 624 and 625.
- **+0x2169.** `ShowMessage`'s second voice test is a window flag copied in with +0x2168 (an
  `EvtWindow` set-up's four bytes); what sets it was not read. The port takes it as clear.

## Checks

`ctest --test-dir engine/build -R event-reset` (the tap's 0.15 s and the two updates added to its
script), `audio-player`, `viewer-scene-audio`, `viewer-new-session`, `viewer-session-ui`,
`viewer-field-restore`, `input-cancel`, `lifecycle-clock`: all pass.

Headless, with the Android copy (`XDG_DATA_HOME` on a scratch folder):
`cr2view --data <obb> --play --fps 60 --run <s> --capture <bmp>` - the field event ends at
14.25 s (285 updates: its waits), the vista's function starts ten updates later, at 14.75 s, with
its `FadeIn(0, 30)` from black:

| `--run` | the frame |
|---|---|
| 14.95 s | still black (mean brightness 7 of 255): the fade-in's hold |
| 15.45 s | a third of the way up (59): 0.45 s of the 1.5 s fade after the 0.2 s hold |
| 16.25 s | the scene (142) |
| 31.1 s | black: `FadeOut(0, 1)` at the vista's frame 322, the narration set at 323 (30.95 s) |
| 31.5 s | "I have been wa" - eleven characters typed in 0.55 s, one an update |
| 32.4 .. 33.5 s | the whole line held: the message is `\m \n \n \n \n \c I have been waiting for you... \w40 \*` - a 2 s wait, then its own end |
| 35.1 s | the white `FadeIn(1, 20)` under way: the script went on at the message's end |

(Before the hand-over's ten updates were added, the same frames fell 0.5 s earlier, identical
otherwise.)

The first field's tutorial, `cr2view --data <obb> --play d0104 --call Tutorial_016 --fps 60 --run <s>
--capture <bmp>` (the function run on the map's start point, which is where `D01_01_05_set` has
it):

| `--run` | the frame |
|---|---|
| 0.15 s | the picture (`tuto016_01`, the field itself) up at once, the help window still opening: no text |
| 0.6 s | "Moving" typed, in the middle of the window's band - at its left before the per-line centring |
| 3.0 s | the page held with its send cursor, the fast-forward button at the top right |

The yes / no buttons' close is checked in `event-reset-tests` (`ShowYesNoButton`, four updates, the
answer, `HideYesNoButton`: a closing age of -0.05 s in the hide's own update, 0 the update after,
over within a second); no headless frame of a choice was taken - the first field has none, and the
tutorial dungeon's `c004_evt` branches on its flags before reaching one.

The field's events, `cr2view --data <obb> --play t0101 --explore <dir> --skip-battles --fps 60`,
read from the log the viewer now writes (the field script's update count at the queue, at the
call, and when control is back):

| What | Logged |
|---|---|
| `evt002_box00` walked into | queued at update 47, called after 48 - its script runs from 49, the second after the contact |
| `obj002_check_cap00` tapped | queued at update 204 (the tap read in 205), called after 205 - its script runs from 206 |
| An event's end | control back three updates after the script's last (`t0101: control back after update 56`) |
| `t0101_p3` walked into on t0113 | the exit taken through the fade and the branch (`exit t0101_p3`, then `play: to t0101_p3`), no longer at once |

The encounters, `cr2view --data <obb> --play d0105 --explore <dir> --hours 0.03 --skip-battles`
(bounded: `timeout -s KILL`): `d0106: an encounter (area 4, roll 6 of 20%): coupling 58`, then
`play: battle 58 - an encounter ..., 27 updates after the hit` - the battle's first update the 28th.

The battle's way out, the same bounded explore: battle 57 won, its result over at about 25.45 s,
`play: the battle's way out over at 26.65 s` (1.2 s later), then `d0107: evt100 running`. The fade
itself was not captured.

A script's `Exit`, `cr2view --data <obb> --battle 279 --fight pair --fps 60 --run 30`: Amon's
"Now bow before me." lands at 20.7 s, its action ends and `Command_End` exits the battle, logged as
`battle over: its script's exit (26.3 s)`. The battle-only view logs 26.3 s with the old wait as
well, since the action's show is over by `Command_End`; the 1.5 s saved is the story flow's hold,
which a battle-only run does not reach. No story run up to battles 245, 279 or 282 was made.

The result window, `cr2view --data <obb> --battle 57 --fight pair --fps 60 --run <s>` (no taps):
`battle over: won (13.8 s)`, `result: victory at 15.0 s`, `result: the window at 20.1 s` (5 s and
two updates after); at 30 s the line is blank (`ProcessResult`'s wait), at 32 s "Obtained 20 OZ!"
held for a tap. A bounded explore from d0105 (a tap a second): battle 58 won at 15.1 s, the window
at 17.4 s, `result: over at 21.52 s`, the way out over at 22.73 s.

An escape, `cr2view --battle 57 --fight pair --seed 1 --input "escape yes"`: `escape: got away`
(seeds 1 and 2; 3 failed). A bounded explore from d0105 after the hold's removal: battle 57 won,
the window at 21.5 s, `result: over at 26.63 s`, the way out over at 27.85 s (24 updates). The
window's first frames (`--battle 57`, 20.2 s and 20.35 s against its opening at 20.1 s): its
parts sliding in from both sides, then whole.

The stop turn, `cr2view --play t0101 --skip-battles --walk 0,1,1 --run <s>`: `t0101: the leader
turns to the nearest balloon over 10 updates (-84 degrees)`; frames at 1.0, 1.25 and 2.0 s show
the leader from walking up the room to facing the balloon. `--walk -1,0,1` turns it 109 degrees;
four other short walks there and six on d0104 stopped with nothing near (no turn). The opening
(`--play`, 40 s) runs as before. The wipe's freeze was not captured.

The camera's ease, `cr2view --play t0101 --skip-battles --walk 0,1,1 --run 2` with a passing log of
each update's scroll and target (960-wide units): the target moves off while the leader walks, the
scroll follows a fifth of the gap an update (15.2 -> 47.7 -> 95.7 -> 129.2 -> 142.9 -> 148.5 ->
150.8 -> 151.7 toward 152.4) and holds 0.7 short, under a pixel. A bounded explore from d0105
crossed four areas (each started on the leader) as before.

The menus' calls, a bounded explore of t0101 (`--hours 0.05 --skip-battles`): the world select's
`WarpTask` logged as queued at update 727 and called after 728 (again at 1119 / 1120, its script
then ending and control back three updates after), as a balloon's tap's call is.

The lines' typing, after undoing the change: the reference video's cut-in page (`BEf8y6MgOoE`,
frames sampled every 0.5 s from 471 s, cropped to the window) types line after line with the line
below empty; the port's `D01_01_0101_evt` lines type as before.

(The second narration is `... \w60 \*`.) The captures are frames of the port, compared with the
counts above, not with the original running.
