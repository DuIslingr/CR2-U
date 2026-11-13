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

Round 20, the same video, the screen's mean brightness frame by frame:

- **The battle's way out (rounds 9, 12): confirmed.** The result window starts out at about
  542.43 s; the white fade runs 542.6 - 543.6 s (1.0 s: 20 updates); the field fades in from white
  544.1 - 545.1 s (1.0 s). The white held between them, 0.5 s, is the original's unloading and
  loading (`Cr3ResourceManager::IsEmpty`, the field's set-up); the port holds it about 0.2 s.
- **Walking off an area (round 7): confirmed.** Black over about 0.25 s (553.87 - 554.13 s: 5
  updates), the next area fading in over about 1.0 s (554.57 - 555.53 s); 0.43 s of black between,
  again with its loading.
- **The typing (round 5): confirmed.** A cut-in line types 10 characters from 474.0 to 474.5 s:
  one an update.

### A battle's opening: from the uncovering to the first turn

`BtlMainCtrl::StepBattleIn`, from its `Cr3Fade::FadeIn(3, 20)` (the uncovering): step 6 passes the
update after (the fade's state is 1, not over 1); 7, with no first strike, goes straight to 9; with
one it waits 0.5 s (+0x14, 0.05 an update) and puts up the banner - the leader's team (1, or 0x5b
alone) or "Monster assault!!" (2) - with `SetAnnounceTime(1.0)` (the value in `r1` at both calls,
read from the ARM code; `BtlAnnounce::Update` drops it after that long); 8 waits over 1.0 s; 9 waits
for the fade's 20 updates; 10 calls `Battle_Start`; 11 waits for the script; 12 the voice; 13; 14
the first turn (the menus, or the monsters' assault) the update after. Battle_Start comes 21 updates
after the uncovering (35 with a banner), the turn 5 after it.

The reference videos: `2dy_mefNhEI`, a battle uncovered from 485.0 s, its command menu at 486.25 s
(1.25 s); `BEf8y6MgOoE`, an assault uncovered from 498.83 s, "Monster assault!!" at 499.40 s (0.57 s)
until about 500.1 s. The port waited a flat 3.0 s for the opening, then (with a banner) 1.5 s more.

| | The original (and the video) | CR2-U before | CR2-U now |
|---|---|---|---|
| The first turn, no first strike | 26 updates after the uncovering (the video's menu 1.25 s) | 3.05 s | 1.35 s (`turn 1` logged; the menu between 1.2 and 1.5 s in frames) |
| A first strike's banner | 12 updates on, 1.0 s long (the video's 0.57 s on) | 3.0 s on, up to 3 s | 0.60 s on, 1.0 s |
| The first turn after a banner | about 40 updates | about 4.5 s | 2.05 s |

The voice (`PlayVoiceBattleIn`, step 12) now comes with `Battle_Start`, two updates early.

### The dungeon's name against the reference video

`BEf8y6MgOoE` enters the first forest at 161 s (its fade-in) and plays there to about 413 s: no
dungeon name shows at its start, after its tutorial battle or as control comes back. The scripts'
`MapNameDisp(map)` (in each dungeon's event script) puts it up once - unless story flag 1007 is set
- 15 updates after an event calls it, and sets the flag; the forest's opening events do not call it
there. A port run started on the map itself (`--play d0104`, no story flags) did show "The End
Begins"; that start is not the story's, and the story's own run takes the same scripts and flags.
No change.

### An attack's end, and a pair attack's name

`BtlActionCtrl` ends a plain attack with `ActionAttackReturn` (the move back over the motion's
second move: `GetMotionTimeMoveStart2` .. `End2`), `ActionAttackWaitFinishReturn`
(`WaitStep(5, GetMotionTimeMoveEnd2)`: on once the motion reaches the second move's end),
`ActionAttackWaitFinishRotate` (the turn back, `IsEndMoveRotation`; `SetMoveRotation`'s 0.2 s), then
`ActionCommandDone` - `JumpStep(0xb)`: `ActionCommonCommandEnd`, `ActionCommonEventCommandEnd`
(`Command_End`), `ActionCommonDone`, `ActionDone`, one an update. The rest of the motion plays on
under the next actor. The port waited for the whole motion and 0.3 s more.

The reference video (`BEf8y6MgOoE`, camera cuts found frame by frame): a Mantis's Lockjaw from its
cut at 522.90 s to the next actor's at 525.50 s - 2.60 s; its motion's second move ends 2.35 s in
(the motion is 3.00 s). Darwin's Slash 520.93 - 522.90 s, 1.97 s; second move's end 1.60 s (2.00 s
long). The port now ends an attack 0.3 s after the second move's end (the 0.2 s turn and the steps),
the motion's length plus 0.3 s where it has none: Lockjaw 2.68 s (was 3.38 s).

`ActionCommonAnnounce` names a pair's attack (`IsPairAction` and `IsAttackCommand`) by the acting
character's +0x46 - its pair command - not the attack's: "Pair Attack" in the video (535.7 s); the
port put up "Slash". It now takes the character's `pairAttack`.

| | The original / video | CR2-U before | CR2-U now |
|---|---|---|---|
| Lockjaw, start to the next actor | 2.60 s | 3.38 s | 2.68 s |
| A party pair attack's banner | "Pair Attack" | "Slash" | "Pair Attack" |
| A pair attack's first hit, from its camera | about 0.9 s | 0.58 s | unchanged (open) |

### A physical attack's lead

Round 24's end was measured from the action's start; the swing itself does not start there.
`ActionAttackStandby` sets the attack's camera, turns the actor to its target and asks for the
attack motion (a pair's both actors' commands swapped for their +0x46 first);
`ActionAttackStandbyMotion` starts the 0.2 s turn (`SetMoveRotation`) and the stance blend;
`ActionAttackWaitAttackMotion` waits until the attack motion is the one playing (or 10 s), then the
move, the launch SE and effects, the voice and `WaitStep` to the hit. Three updates, an update each.

The reference video: Lockjaw's hit (its flash, "66") about 1.2 s after its camera, the motion's hit
at 1.05 s; Darwin's Slash's flash at 521.6 s, 0.67 s after its camera at 520.93 s, the motion's at
0.55 s. The port now starts a physical attack's motion (`magic` 0, a motion that moves out and
back) three updates after the action begins and ends it three updates after the return - round
24's 0.3 s now 0.15 s, the lead before it:

| | The video | CR2-U (round 24) | CR2-U now |
|---|---|---|---|
| Lockjaw: the hit / the next actor | 1.2 s / 2.60 s | 1.05 s / 2.68 s | 1.25 s / 2.68 s |
| Darwin's Slash: the hit / the next actor | 0.67 s / 1.97 s | 0.55 s / 1.90 s | about 0.7 s / 1.90 s |
| A pair attack's first hit from its camera | about 0.9 s | 0.58 s | 0.72 s |

Round 26: the command's voice (`PlayVoiceCommand`, after the command's +0x6a updates) and its
launch effects (`ShowCommandLaunchEffect`, the j007 / m507 pair's eff110 and plain attack's) come in
`ActionAttackWaitAttackMotion`, at the lead's end: a physical attack's now wait for it (the voice
by its delay, the effects through a small queue run with the show); a scene's, Defend's and one
not done's start with the action as before. The battle 57 pair fight still voices each action
(2371, 2306, 2274 / 2409 ...).

### The action step tables

`BtlActionCtrl::SetActionTable` gives each actor a list of step numbers (`BtlActionTable::
action_table_*`, pointers at 0x5afb28, sizes at 0x5af9c8, read from the file); `ActionCommandMain`
calls step n through the member table the constructor fills at +0x390 (n x 8), and `ActionMain`
runs the next step in the same update whenever one advances - steps cost no update of their own,
only their waits do. The lists:

| Table | Steps |
|---|---|
| attack_solo | 1 Init, 2 Standby, 10 CommandStart, 7 Command_Start, 9 the script, 6 Announce, 15 AttackStandby, 16 WaitAttackMotion, 5 WaitMotion (to the hit), 17 AttackCamera, 19 WaitHit, 18 Hit, 5, 21 Return, 23 WaitFinishReturn, 5 (to the return's end), 22 WaitRotate, 26 StandbyMotion (the turn back, the stance), 12 the announcements gone, 8 Command_End, 9, 24 WaitNextStart, 25 WaitFinishRotate, 11, 14 Done, 78 End |
| attack_pair (the partner's) | 1, 2, 10, 6, 29 PairWaitDelayTime, 3 WaitTime, 27 PairStandby, 28 PairWaitAttackMotion, 19, 18, 5, 21, 23, 22, 5, 26, 12, 24, 25, 11, 14, 78 |
| dont_action | 1, 2, 10, 7, 9, 77 DontActionAnnounce, 12, 8, 9, 11, 14, 78 |
| item | 1, 2, 10, 7, 9, 6, 30 ItemStandby, 32 WaitLaunch, 33 WaitHit, 3, 34 WaitMotion, 3, 12, 11, 8, 9, 14, 78 |
| sopia | 1, 2, 10, 7, 9, 6, 35 .. 40, 43 .. 49 (the mirror's), 13 Join, 12, 41, 42, 11, 8, 9, 14, 78 |
| awake / advent | 1, 2, 10, 7, 9, 6, 50 .. 55 / 56 .. 66, 13, 12, ..., 8, 9, 11, 14, 78 |
| dead | 67 .. 76 (the downed and the revivals), 78 |
| empty | 1, 2, 10, 11, 14, 78 |

What that settles:
- **An attack's end** is `ActionAttackWaitNextStart`'s 0.1 s after the return (1.6 s for command
  0x130, 2.1 s for 0x145 and 0x1a8 .. 0x1aa), then action state 3 and the next actor, while the
  turn back finishes: the port's "three updates after the return" is that 0.1 s and an update,
  and it now takes the special commands' times too. Round 25's account of the lead was wrong in
  its steps: `StandbyMotion` (26) is the end's turn back, not the lead's; the lead is
  `WaitAttackMotion` (16) waiting for the attack motion to be the one playing. The measured times
  stand.
- **One not done** shows its line for 2.0 s and step 12 (`ActionCommonCommandAnnounce`) waits till
  no announcement is left: the port held it 1.5 s, now 2.0 s and an update.
- **A pair:** the partner's list waits (29) for the leader's attack motion, then the pair delay
  (3), then its own motion (27, 28) - as the port's `pairDelay`. Which of the two leads in the
  video's pair attack, and so its 0.2 s, is still open.

### A revival, and the turn's end

The dead table's revival (73 `ActionReviveStandby`: "Regenerate", 0x5d, for 3.0 s; 74 the
`Cam_Unit_PCrevival_00` camera; 75 `ActionReviveMain`: motion 8, the HP back at DummyDP01, the
voice, SE 0x8b, eff216; 76 `ActionReviveWait`: on once that motion has played out, its idle, done)
lasts its motion; the port held the longer of the motion and 1.0 s, and 0.3 s more - now the
motion and an update (its line still the 3.0 s the port's banner gives any).

`BtlTurnEnd::Update`: 1 the turn's script, 2 its wait, 3 the poison and the pools applied (their
numbers, the HUD) and - when any was - 4, a wait of 1.5 s, else straight to 5. The port waited
1.2 s; now 1.5 s. Battle 57 with Darwin poisoned (`--state 0:0`): the turn's end with "52 poison"
and turn 2 at 7.97 s, against 6.48 s unpoisoned - 1.49 s. The revival was not run headless.

### An item's use

`action_table_item`: 30 `ActionItemStandby` (the item motion, the camera), 32
`ActionItemWaitLaunch` - 2.0 s, then the target's camera and its wait motion -, 33
`ActionItemWaitHit` - 0.1 s, then the hit, its SE, `WaitStep(3, 0.5)` (the float in `r2`; the step
waits on the command's own time, +0x30 from `ActionCommonCommandStart`, so 0.5 s has long passed) -,
34 `ActionItemWaitMotion` till the item's hit effect (+0x48) has played out and the user stands
(the revivals' kinds 0x18 / 0x19 wait for the target's motion; `WaitStep(3, 0.5 / 3.0)` read the same
way), then 12, 11, 8, 9, 14. The port landed an item's effect with the action's first update and
ended it after its motion and 0.3 s; it now lands it 2.1 s in and ends an update after its effects
are gone.

Battle 57, Darwin using Chocolate (`--item 1:3`, the tutorials' flags set, `--input "solo items
i1 0 attack 2"`): the action at 8.75 s, "+89 HP" at 10.9 s (was 8.8 s); frames at 9.6 s ("Chocolate"
on the banner) and 11.0 s (the 89 on Darwin, his HP 515). No reference video shows an item used, so
the 2.0 s rests on the code. (Round 31 found one: below.)

### An item's use against the reference video

Video 02 (`2dy_mefNhEI`), Marie using Chocolate on herself, read frame by frame (1/30 s, a frame
difference over the whole picture and over the banner): the camera cuts to her, close, with
"Chocolate" on the banner at 616.90 s; she raises her arm (618.25 s); the camera cuts to a wider
shot of her at 618.90 s - 2.00 s on; the green "500" shows at her chest between 619.03 and
619.10 s - 2.17 s on - with a cyan swirl and then a glow over her until about 620.2 s; she stands
still from 620.35 s; the banner turns to the next action's ("Healing", an Executioner's) at
621.23 s, 4.33 s after hers, and the camera cuts to it at 621.53 s. (A note of mine before the
frames were read had the heal as 600; the frames show 500.)

Against the code:
- **The cameras.** `ActionItemStandby`, for the party: `BtlCameraTable::GetUseItem(GetPlayerCamNo)`
  - 2 alone, 3 a pair, else which of the two -, the table's short `0x107 + n` (`this + (n + 0x108)
  * 2 + 2`, the shorts from `this + 4`). `ActionItemWaitLaunch` adds the frame's time to +0x24 till
  2.0, then `SetTargetCamera(false)`: a target of the party (not a group's, +0x4b 3 / 6) -
  `GetItemSopiaTarget` by the same numbering (short `0x115 + n`), or `Cam_Unit_PCrevival_00` at
  its place for a revival (the action's kind 0xc or command 0x67; the port takes a revived hit).
  The port gave an item an attack's camera and never cut; it now films the user with
  `Cam_Item_PCfighter_00` and cuts to `Cam_Unit_PCfighter_00` 2.01 s on (battle 57, Darwin's
  Chocolate: 8.72 -> 10.73 s), as the video does at 2.00 s. A monster using an item, or one used on
  a monster or a group, keeps the attack's cameras (the monster branch moves the camera to it and
  turns it - not followed).
- **The hit's effect.** `BtlActionCtrl::HitEffect` shows the command's hit effect (+0x18) on every
  target hit (+0xf78), a restore's too; the port left the restores out. Chocolate's is `eff213` -
  the video's swirl and glow, now drawn on Darwin from 11.3 s (frames at 11.3 / 11.8 s).
- **The end.** `ActionItemWaitMotion`, not a revival: while the hit's effect (+0x48) is of kind 1
  or 2 it waits; then with the user's motion of type 1 `WaitStep(3, 3.0f)` (r2 `0x40400000` at
  0x36f5f0; a revival's `0.5f`) - step 3, `ActionCommonWaitTime`, passes once +0x30, the time since
  `ActionCommonCommandStart` (summed each update in `ActionCommandMain`), reaches it. The port
  ended an update after the hit's effects were gone (none, for a heal: 10.9 s, 2.2 s in); it now
  waits for the effects and the 3.0 s: 12.45 s, 3.7 s in, the heal 1.65 s before the end. The video
  has 2.16 s from the heal to the next banner; the 0.5 s between is not accounted for (the user's
  motion of type 1, `eff213`'s length in the original, and the next action's own steps were not
  timed).
- **The numbers' place.** `BtlEtcCtrl::CreateDispNumber` places a number by
  `BtlModel::GetPosition("DummyDP01")` - the joint where the motion has it. The port read the
  skeleton's base pose, which leaves the joint at the model's origin (y 0 for Darwin and a Mantis):
  every damage and heal number showed at the feet. It now reads the posed skeleton: Lockjaw's 89
  and Chocolate's +89 at Darwin's chest (frames at 7.4 and 11.0 s), as the video's 500 on Marie and
  693 on the Executioner (614.5 s).

### A skill with no motion of its own

`BtlActionCtrl::SetActionControl` sets an action's kind from `BtlUtilData::GetMotionTypeFromCommand`
(1, 9, 11 a sopia's table; 5 .. 7 an attack's; 10 an item's; 2 a sopia's for a monster, none for the
party; 12 a special's), and `SetActionTable` picks the table by it - an attack's (1) before an
item's (3) or a sopia's (2). `GetMotionTypeFromCommand` reads the command's motion (+0x38); with
none it takes the acting one's plain attack's - the party's +0x44, a monster's +0x4c. So Darwin's
"Break Beat", "Dáinsleif", "Preemptive Strike" and "Seal Strike" (no motion of their own) swing his
`bo01` through the attack table. The port played nothing for them and landed their hit with the
action's first update (8.75 -> 8.8 s); it now takes the plain attack's motion: "Break Beat" in battle
57 hits at 9.5 s, `bo01`'s lead and hit. The reference video's skills (`2dy_mefNhEI`'s "Blitz", 489 s)
are swung at the target likewise; a sopia of its own motion ("Cocytus": `bm01`) keeps the port's
timing - the sopia table's steps (`ActionSopiaWaitLaunch` 2.0 s, `ActionSopiaLaunch` 0.8 s,
`ActionSopiaWaitHit` the effect's +0x40 updates, `ActionSopiaWaitMotion` 0.2 / 0.7 s, under
conditions) are not ported.

### A sopia cast against the reference video

Video 02 (`2dy_mefNhEI`), Marie casting Banish on the Executioner, read frame by frame: "Banish" on
the banner at about 638.60 s (between 638.57 and 638.63); the camera cuts to her at 638.77 s -
0.17 s on -, to the Executioner at 640.80 s - 2.03 s after that - with the spell's flash already
on it; "424" at its chest between 640.83 and 640.87 s - 2.27 s after the banner -; the next
action's banner ("Healing") at 642.80 s, 1.93 s after the number.

`action_table_sopia` (the step functions by `BtlActionCtrl`'s table at +0x390: 35 Standby, 36
WaitMagicMotion, 37 WaitLaunch, 38 Launch, 39 WaitHit, 40 WaitEffect, 41 WaitMotion, 42 WaitVoice,
43 Analyze, 44 .. 49 the Mirror's): 1, 2, 10, 7, 9, 6, 35 .. 40, 43 .. 49, 13, 12, 41, 42, 11, 8, 9, 14.
- 35 `ActionSopiaStandby`: the voice, the launch SE; for motion 0x11 (bm01) that motion asked for
  and the command's launch effect on the caster at once.
- 36 `ActionSopiaWaitMagicMotion`: till the model's motion is `bm01` (`bg01` for motion 4), then
  `GetUseSopia(GetPlayerCamNo)` - the table's short `0xf9 + n` (`this + (n + 0xf8) * 2 + 6`). The
  motion's change takes its updates: the video's 0.17 s.
- 37 `ActionSopiaWaitLaunch`: +0x24 summed to 2.0 s (motions 0, 0x11, 4, or kind 0x1e; else 0),
  then for one target: kind 0x10 the pattern's `GetGetSet`, kind 3 none, else - motion types 2 and
  0xb aside - `SetTargetCamera(false)`; for more than one, `SetTargetCamera(false)`.
- 38 `ActionSopiaLaunch`: 0.8 s more for kind 0x1f, else none; `CalcCommandAction` and `HitEffect` -
  the hit's effect on each target.
- 39 `ActionSopiaWaitHit`: the hit effect's setting's +0x40 (u16) frames of 0.05 s, then
  `ExecCommandAction` - the numbers. Of the table's 166 settings only four have one: eff210 20,
  eff282 30, eff283 150, eff415 82 (the effect table now carries it: `hitWait`).
- 40 `ActionSopiaWaitEffect`: while the hit's effect (+0x48) is playing; then the stance, and on
  to 13 (43, the Analyze, for kind 0x1a).
- 41 `ActionSopiaWaitMotion`: 0.2 s (0.7 for command 0x3e). 42 `ActionSopiaWaitVoice`: while the
  command's voice plays, up to 5 s.

`SetTargetCamera(false)`, read in full: by the command's +0x4b - 3 (the allies)
`GetItemSopiaTargetGroup(+0x523 == 1)`; 6 (the foes) by the party
`GetItemSopiaTargetGroup(pattern + 2; 12 / 13 for the patterns 10 / 11)` (short `0x123 + n`), by a
monster as 3; else the one target - the party's `GetItemSopiaTarget(2 alone, 3 a pair, else which)`
or `Cam_Unit_PCrevival_00` for a revival, a monster's `GetItemSopiaTarget` by the pattern (11 a
pair; 10; 8 / 9 for pattern 11; 12; 13 for pattern 9; else its size's 4 + 0..3) moved to its
`DummyCP` (the string at 0x4fe338) and turned with it, except under the patterns 7, 8, 10 .. 12
(and 8 cm nearer under pattern 4 by the party).

The port swung a party member's bm01 sopia like an attack: Cocytus (battle 57, `--input "solo
sopia s55 2 attack 2"`, flags 53 / 54 / 55 / 61 / 62) hit 0.8 s after its banner under an attack's
camera. It now follows the table for a party member's bm01 (not bm02, which `GetMotionTypeFromCommand`
gives the item's table, nor a pair's): the caster's camera 3 updates in (`Cam_Magic_PCfighter_00`,
8.75 -> 8.92 s), the target's 2.0 s after (`Cam_Unit_S_00`, 10.92 s), the hit then plus the
effect's +0x40 frames (10.92 s: 2.17 s after the banner, the video's 2.27), the end once the hit's
effect has played out (eff205's 2.0 s) and 0.2 s. Frames at 8.8 (the banner), 9.6 (Darwin, close),
10.95 (the Mantis, 205) and 11.6 s (eff205's ice). Its end there is the battle's (won, 13.2 s:
2.33 s after the hit; the video's next banner came 1.93 s after Banish's, whose effect is another).
The item's target camera now goes through the same `SetTargetCamera` (a monster's, the foes'), and
the item's end, like the sopia's, waits on the hit's effect alone (+0x48), not on the launch's.

### The sopia table in full, the Mirror, and the banner's hold

Round 33 takes the rest of `action_table_sopia`.

**Which actions use it.** `GetMotionTypeFromCommand` by the motion's second letter (`GetMotionFromCommand`
the motion number: `b` n - 1, `g` n + 3, `m` n + 0x10, `i` n + 0x11, `x` n + 0x12, `s` n + 0xe, `z` n
+ 0x15): m (bm01) 9 - bm02 10, the item table's -, b 1, x 11, g 2 (a monster's sopia table; the
party's Defend keeps its own), o / a / s attacks, p a pair's, z a special's scene. The commands' data:
bm01 the spells (Banish, Cocytus, Healing, Steal HP ...), bm02 the items, bb01 the monsters' waits and
taunts ("...", "Watching", "Judging the situation": no target), bx01 the Executioner's Poses, bg01
guarding (Defend, Self-Preservation), bz01 the specials. The port now runs every bm01 / bb01 / bx01
action, and a monster's bg01, through the table, not only a party member's bm01; kinds 0x1e / 0x1f
(the escape's command, an awakening's) keep their own.

**Standby and the cameras.** `ActionSopiaStandby`: bm01 asked for (with the partner's in a pair, the
launch effect on each), except for commands 0x8f, 0x90 and 0x92 (`+0x18` 0: no motion, no wait);
a monster's bg01 asked for; bb01 / bx01 nothing (bx01's motion comes in `ActionSopiaLaunch`: 0x13,
then 0x14). `ActionSopiaWaitMagicMotion` waits only for bm01 / bg01, then the caster's camera:
`GetUseSopia` - the party's by `GetPlayerCamNo`, a monster's by the pattern as its target's would be
(0xb a pair's), moved to its `DummyCP` in its initial pose (`SetInitialPose`, the string at 0x4fe338)
unless its size byte is 4. `ActionSopiaWaitLaunch`: 2.0 s for motions 0x11, 0 and 4 (and kind 0x1e),
none for bx01; then one target of kind 0x10 the pattern's `GetGetSet`, of kind 3 or by motion types
2 / 11 none, else - and for more than one - `SetTargetCamera(false)`.

The reference video, `2dy_mefNhEI`: the Executioner's Healing (a monster's bm01, on itself) - the
banner at 642.80 s, the camera on it at 643.07 s (0.27 s; Marie's Banish 0.17 s), the cut to it as
the target at 645.13 s (2.06 s on) with the 333 there, the banner cleared at about 647.3 s (2.17 s
after the number), the next turn's menu at 648.1 s. The motion's change took five updates for the
monster, three for Marie; the blend's length (`BtlUtil::IsStartMotionBlend`'s motion-table flag,
`SetNextMotionBlend`) was not followed into `BtlMotionCtrl`, so the port uses the two measured
counts. In the port, coupling 76: Zanthoxylum's Steal HP (kind 0x10) - its camera 0.27 s after the
banner (`Cam_Magic_S_00`, framed on it), the pattern's `Cam_Getset_MSS_00` 2.0 s later with the 197
on Orlando (frames at 20.6 / 22.2 s).

**No one struck.** `ActionSopiaLaunch` with no target goes to 42 at once: `ActionSopiaWaitVoice` holds
while the command's voice plays (`IsPlaySound(2)`), 5 s at most - and only then (`+0x52e` 0). The
port ended such an action with its motion; it now waits the launch and the voice (its slot from
`playVoice`; a headless run has none playing).

**An effect with a hit wait.** `ActionSopiaLaunch` calls `HitEffect` at the launch; `ActionSopiaWaitHit`
puts up the numbers the effect's +0x40 frames later. The port now draws those four effects at the
launch.

**The Mirror.** `ActionSopiaLaunch`, finding a target with state 0x1d (a magic barrier; the port's
state 29) and the command not piercing (`CheckCommandSpecial(.., 3)`), sets the wait motion and jumps
to 44: `ActionSopiaMirror` (`CreateMirrorEffect` on each holding it, one use of it),
`MirrorWaitEffect` past 1.5 s (`> 1.5`: 31 updates), `MirrorCamera` - `SetTargetCamera(true)`, the
caster's own -, `MirrorEffect` (the hit's effect on the caster, or its side for one on all),
`MirrorWaitHit` (+0x40, the numbers), `MirrorWaitHitEffect`, then 13. The port drew eff227 as the
action began and struck the caster at the spell's own hit; it now draws it at the launch with SE 0x92,
keeps the camera on the one cast on, and 1.55 s later cuts to the caster and strikes it. Battle 57,
`--state 2:29 --state 3:29`, Darwin's Cocytus: the Mantis at 11.35 s in the barrier's sphere, Darwin
at 12.90 s with his own 163 (frames at 11.7 / 13.3 s).

**The banner.** `BtlActionCtrl::StepSetAction` clears `BtlAnnounce` +0x131 as a turn's actions begin;
`BtlAnnounce::Update` hides the window when its queue empties only with +0x131 set, so the last text
stays till another replaces it; `CheckAllActionDone` sets +0x131 and hides it. The reference video
agrees ("Chocolate" 4.3 s, "Banish" 4.2 s, "Healing" till its turn's end). The port took every
banner down 3.0 s after it went up; it now keeps it while the turn's actions run.

**The rest of the round.**
- *A pair attack's number.* Frame by frame (`BEf8y6MgOoE`): the cut at 535.57 s, "965" between
  536.30 and 536.37 s - 0.77 s, not round 25's "about 0.9 s"; the port's 0.70 s (Darwin and
  Orlando, as there). Within about an update. `ActionAttackPairWaitDelayTime` (the second's
  controller) waits for the first's `ActionAttackWaitAttackMotion` flag (`+0x33c` by side), resets its
  +0x30 and `WaitStep(3, GetPairAttackDelayTime)` (`cr3_common_data`'s numbers, the table at 0x5362f4 /
  20); `ActionAttackPairWaitAttackMotion` then waits for the partner's motion (or 15 s). Which of the
  two runs which table was not settled here; round 40 settles it (the port's order holds).
- *An escape's update count.* `BtlMain::Init` registers the task modules in order: `BtlMainCtrl`
  (+0x278) first, `BtlAnnounce`, `BtlActionCtrl`, `BtlMoveCtrl`, `BtlMotionCtrl`, the effects, the
  sound, `BtlCommand` (+0x34c), `BtlTurnEnd`, `BtlGameover`, `BtlResult`, the cameras ...;
  `UpdateScene` runs them in that order. `BtlCommand::Update` adds the update's time before its
  step. So `StepEscapeSuccessLoop` reaches 2.0 s 40 updates after `StepEscapeSuccess`, and
  `BtlMainCtrl` sees its flag the update after: the port's 2.15 s stands, now read.
- *The result window's close.* `BEf8y6MgOoE` 542.45 s: the tap, the parts out by 542.65 s (about
  0.2 s), the white rising from there (full by about 543.6 s). The port's close - each part
  backwards over its opening's time, with the way out's fade - agrees.
- *The touch action mark's distance.* `CheckTouchActionMarkOne`: the leader's place (+0x50 .. +0x58)
  to the mark's spot (a mystic spot's, else the action's own - its +0x3c call), the 3D length. The
  port measures the same.
- *The marks' second list.* `CheckTouchActionMark`'s list at +0x178, counted at +0xbf80, is the map's
  `FldBox` event boxes (filled by `GimmickDataCreate` and the collision callbacks).
- *The windows' tap areas.* `Cr3UtilCheckHitCollision` takes each `tapAreaS..` joint with its
  `tapAreaE..`, their world transforms as the figure stands, the point between them - as the port.
- *+0x2169.* `EvtWindowPage::ReadPage` writes the message's voice (`GetVoice`, -1 none) as an int at
  the page's +0x2168, `Clear` -1. That the window's byte +0x2169 is that int's second byte would
  silence every voice over 255, so it is likely not; still not read.

### Round 34: rounds 31 - 33 rechecked in REA, an attack's approach, the win pose

Rounds 31 - 33 read the repo's decompilation and `llvm-objdump`; this round opened the library in
REA (Ghidra 12.1.2, `re/opening-audit-evidence-21.json`) and decompiled the same functions afresh.
Confirmed as the port has them: the item's 2.0 s / 0.1 s and `WaitStep(3, 3.0)` (0.5 for a revival:
`0x3f000000`); the camera table's four getters (`0x107`, `0x115`, `0x123`, `0xf9` from the shorts'
base); `SetTargetCamera`'s branches, patterns, `DummyCP` frame and 8 cm; `ActionSopiaWaitLaunch`'s
2.0 s and its camera rules; `ActionSopiaWaitHit`'s +0x40 x 0.05; `WaitMotion`'s 0.2 / 0.7;
`WaitVoice`'s count-0-only 5 s; the Mirror's `> 1.5`; `WaitMagicMotion`'s cameras; `HitEffect`
without a restore exception; `CreateDispNumber`'s posed `DummyDP01`; the banner's +0x131;
`BtlMain::Init`'s task order and the escape's 2.0 s; the motion letters. The effect table's +0x40
values were not re-read (the initializer's stores go through a base register REA's profile leaves
unresolved); they stand on the repo's Ghidra output.

Corrected:
- **The revival camera** goes by what is used - `SetTargetCamera`'s test is the acting one's command
  0x67 (Resurrection) or item 0xc (Vin Brulee), not "a hit revived": Anastasis and the VDQS take the
  ordinary target camera. The port now tests the same.
- **A monster's bg01 with a target** (`ActionSopiaLaunch`, motion type 2) runs Pre / Post
  CommandAction and the hit SE, then jumps to 42 - no hit wait, no effect wait. The port gave it a
  spell's waits; it now ends it like one with no target.
- **A bm01 with no target**: `ActionSopiaStandby` asks for the motion but leaves +0x18 at 0, so no
  wait for it - the caster's camera at once. The port waited three updates.
- **The numbers' nudges** (`CreateDispNumber`, by `BtlMainCtrl`'s step +8): in `StepAction` (3) one of
  a pair 3 cm right and up (fighters 0, 3) or 3 cm left (the other); in `StepTurnEnd` (4) fighter 0
  right and up, fighter 3 left. Not ported before; now in. The random jitter for +0x327 (8 mm) is not.
- **The turn end's numbers** (`showNumber`) still read the base pose - the feet; now posed, as round
  31 did for the hits'.
- `GetPlayerCamNo`'s +0xfc7 is `StepNextCheck`'s pair-turn flag for both fighters: the port's
  `a.partner >= 0` agrees.

**An attack's approach** (the user saw Darwin not reaching the Mantis). `GetAposName` takes the
places from `BtlAposTable` by number; the port builds the `battleapos` joint's name from the
fighters' `dPos` places. Listed against the skeleton's 184 hashes, the names are
`aPos_<the monster's place><pSoloA | pSoloB | pPairA | pPairB>` (the party striking) and
`aPos_<pSoloA | pSoloB | pPair><the attacker's place>` (a monster; `s` / `m` alone), every place
named as in `battledpos` but the two-small pattern's: `dPos_sL / sR`, `aPos_..ssL / ssR`. So in an SS
battle - battle 57, the Mantises - no one moved to strike (the log at the hit: every attacker at
home). The port now maps sL / sR to ssL / ssR: Darwin runs in, strikes beside the Mantis and leaps
back (frames at 9.0 - 10.1 s).

**The attack's second camera.** `ActionAttackWaitAttackMotion` ends with `WaitStep(5, ..)` to
`GetMotionTimeHitStart(9, 0, -4.0)` - the attack motion's hit less 4 frames -, then 17
`ActionAttackCamera` (`SetAttackCamera2`: the pair's next), then the hit. The port cut at the hit
itself, onto the target alone; it now cuts 0.2 s before (battle 57: Darwin's Slash 9.30 s, the hit
9.5 s), as the reference video does (`BEf8y6MgOoE`: the cut at 521.4 s, the flash at 521.6 s, the
striker in the shot).

**The win pose** (the user saw it play on behind the results). `BtlResult::SetVictoryMotion`:
`SetMotion(2 - bb03 -, the motion table's flag bit 3)`; every character's bb03 has flags 0xb and a
range (j001 77 .. 137 frames). The port played bb03 and then bb03 again from its start - the raise
over and over. A looping motion with the flag and a range now plays to the range's end and then the
range alone (bg01's guard, bd02's last frames the same way; bb01, flags 1, still whole): the sword
raised by 1.1 s and held (frames at 13.0 / 16.5 s), as the video's Orlando holds his through the
window's close (542.65 s).

Not settled: the battle's opening starts at slightly different times run to run and frame rate to
frame rate (turn 1 at 3.50 / 3.60 s at 20 fps, 3.80 s at 60), so headless captures of the same
moment differ by about 0.1 s; not traced.

### Round 35: the win's camera, and the result's pace

**Who the win shows.** `BtlResult::SetVictoryCamera`: one alone (+0x1b) `Cam_Win_PCsolo_00`; after a
Pair turn (+0x523 - `BtlCommand::StepGetsetLoop` sets it to 1 when Pair is chosen with both
standing, 0 for Solo) the two standing `Cam_Win_PCpair_00`; else one member alone, by +0x16 -
`PCfighter` for the first, `PCpartner` for the other. +0x16 is the last blow's: `CheckDead` stores
the acting one when no monster is left standing, and `FixTodome` (before the camera) moves it to the
one standing - 0 when alone. `PlayVoiceVictory` speaks by the same +0x16. The port showed the first
member unless down, and voiced the last of the party to act; both now follow the last blow (battle
57, Orlando's last Slash: `Cam_Win_PCpartner_00` on him alone, frame at 27 s). The reference video's
win (`BEf8y6MgOoE` 539.3 s) followed a Pair Attack and shows both, as the code says.

**The result's pace.** `BtlResult::Update`'s steps: 4 waits past 1.1 s after the music stops, 6 a
0.1 s more and the pose, 7 a tap or past 5.0 s, 8 the finger up, 9 the window; `ProcessLvup` counts
60 updates (3 s), each adding a sixtieth of the EXP and the HP back, a tap ending it at once. The
port has the same (the pose at 11.9 s, the window at 17.0 s; the count 3 s). The reference video is
faster because its player taps: the window 0.47 s into the pose (539.77 s, its parts in by
540.0 s), Orlando's HP counting a point an update (765, 769 at 540.4 s, 772 at 540.55 s) and then
full at 540.85 s - a tap -, the close at 542.45 s. Without taps the original holds as long as the
port.

Also: a hit effect's wait (rounds 31 - 33) now goes by the effect's own animation time, not its
being drawn and erased: one never drawn, looping, kept or without an animation can no longer hold
an item or a sopia forever. And the banner's hold is dropped when a battle ends mid-turn.

### Round 36: a death starts with its blow

The reference video (`BEf8y6MgOoE`): the Pair Attack's 965 on the plant at 536.33 s; the plant glows
and is gone by about 536.9 s - during the attack, not after it -; the numbers fade by about 537.4 s;
the banner closes 537.5 - 537.8 s (the actions done); the win's camera at 539.1 s.

`BtlActionCtrl::CheckDead` is called from `Hit`, `Damage` and `ExecCommandAction` - at the blow. For
the one downed it sets its action state (+0x10) to 6 and +0x320, starts its damage motion (6, or 5),
and for a monster its voice, its drops and - with no monster left - +0x16. `StepAction` runs
`CheckAllActionDone`, `ActionMain`, `ActionDead` - the dead table for every fighter in state 6, each
update - and `CheckStop`, which clears +0x320 and calls `CheckBattleState` once none is left in
state 6; `ActionMain` starts no next action while +0x320 is set. `ActionDeadStandby` lets a monster
with its +0x3a (its own death motion) finish the damage motion first; any other goes on at once.

The port ran the deaths only once the whole action had played (`ActionEnd`), so a monster stood
after its killing blow till the attacker was back (battle 57's last Mantis at 10.1 s, Darwin's hit at
9.5 s). A death now starts when its blow lands, goes on through the rest of the action, and the
action's end waits for it as before; a monster with a death motion waits for its damage motion first.
Battle 57: the Mantis gone in eff201's glow by 9.6 s (frames at 9.6 - 10.3 s); the win at 10.7 s as
before (the action's end), the victory 1.3 s later - the video's banner close to its victory 1.45 s.
A party member's fall now also starts at its blow; no run here downed one.

### Round 37: a loss

`BtlGameover::Update`: step 1 clears the announcements and effects, loops `Cam_Lose_SoloPair_00`,
fades the music over 20 updates and shows the game over's figure (`ShowBattleGameOver`) only in an
event battle (+0x18) or once flag 0x7f9 (2041) is set, the banner ("%s was defeated..." alone, else
"%s's team was defeated...") either way; 2 past 1.1 s; 3 music 18; 4 waits for a tap - then with no
game over (neither) or no continue data nor save, step 6 (`StepBattleOut`: back to the field, no game
over), else the fade out and step 5 .. 8's "Would you like to try again?". The port has the same. In
a battle run on its own (`--battle 41`, no 2041) a loss therefore shows the banner alone, and after
the tap there was no field to go back to: the battle held. A battle on its own now plays its way out
and ends after a loss, a win's result or an escape (as the game over's "no", to the title - which
that mode has none of). With `--flag 2041` the figure shows (frame at 20 s: "Game Over" over
Ophan Marie, "Darwin's team was defeated...").

### Round 38: an awakening's start, and the result window's look

**An awakening.** The reference video (`2dy_mefNhEI`): Darwin's Executioner - the banner and the cut at
609.77 s, the glowing thrust 613.2 - 613.4 s, the dash (Darwin unseen) 613.6 - 614.0 s, the slash at
the Executioner 614.2 s, the 693 between 614.3 and 614.4 s (4.6 s in), the next action 616.90 s.
`ActionAwakeStandby` (REA) runs the scene's commands at time 0 at once (+0x30 zeroed, then
`ActionSpecialCommand` - every command whose time +0x30 has reached), asks for the motion (bz01,
then the stance); `ActionAwakeSyncMotion` waits till the model's motion is it (1 s at most) and
zeroes +0x30 again - the rest of the scene runs from there. The port started the scene and the
motion with the action: its hit 4.45 s in. It now runs the time-0 commands at once and the motion
and the rest three updates later (as an attack's or a sopia's motion change): the 739 at 4.6 s
(battle 57, `--charge 100 --flag 3001`). Checked through the scene: bz01 (6.5 s long) plays
throughout, no other motion; at the hit Darwin stands at his aPos 0.29 in front of the Mantis.

**The result window's look** (the user found it dark). Element by element against `BEf8y6MgOoE`
541.6 s: the status cards (median luminance 184 / port 199) and the EXP plates (142 / 207, the
video's blurred) as bright; the message window darkens what is behind it alike (video 62 -> 20,
76 -> 40; port 146 -> 35 - `hud_syswintc` about 22 under `hud_syswina8`'s alpha 228). The darker
scene behind comes from the camera: a solo win's `Cam_Win_PCfighter_00` frames the tree line, the
video's Pair win `Cam_Win_PCpair_00` the grass. The bars (`cmphpbar_per` etc.) have no textures -
their vertex colours, drawn as they are.

### Round 39: the motions' changes, an actor's readiness, the banner's queue

Read in the repo's Ghidra output and `llvm-objdump` of the ARM library (Ghidra's text addresses are
the library's plus 0x10000: `SetMotionBlendStart` is 0x333bfc in the file, 0x343bfc in Ghidra).

**The update's order.** `BtlMain::Init` registers the modules (`RegisterModule`) as `BtlMainCtrl`
(+0x278), `BtlUtil`, `BtlAnnounce` (+0x330), ..., `BtlMoveCtrl` (+0x338), `BtlMotionCtrl` (+0x33c),
`BtlActionCtrl` (+0x340), the effects, `BtlEtcCtrl`, `BtlCommand` (+0x34c), ..., `BtlModelCtrl`
(+0x2e8: every fighter's `BtlModel::Update`) near the end; `UpdateScene` runs them in that order.
Round 33 put the action controller before the move and motion controllers: it comes after them.
(The escape's count there is unaffected: `BtlMainCtrl` first, `BtlCommand` later.)

**A motion's change.** `BtlMotionCtrl` keeps per fighter a next motion (+0x14), its start-blend flag
(+0x28), its change time (+0x2c) and a "now" bit (+0x10 bit 0, `SetMotionBlendStart`). `Update`
changes when the bit is set, else once the playing motion reaches the change time - or, with none,
its end less the blend. With the blend flag it starts the motion in the model's second slot and
`SetAnimationBlend(0.3)` (`0x3e99999a` at 0x333860); without, it cuts. `BtlModel::UpdateBlendAnimation`
ends the blend once its time passes 0.3 s. The flag is the motion table's bit 1 (`IsStartMotionBlend`;
bit 2 the end's, bit 3 the range loop; bit 0 the motion exists). The actions ask with the bit set:
`ActionSopiaStandby` - loop off, `EndAnimation`, `ResetAnimationEndTime`, `SetNextMotion`, the blend
flag, `SetMotionBlendStart` - so the change comes in the next update's `BtlMotionCtrl`.
`ActionAttackWaitAttackMotion` waits only for the motion's name (`strcmp` of the model's +0xbc);
`ActionSopiaWaitMagicMotion` for the name and the blend's end (+0x68 clear, 5 s at most). Every
figure's `bm01` but m232's (7) and m312's (none) has flags 15 or 14, the start blend included, so the
code gives a party member's cast and a monster's the same wait: the change the update after the
request, then the blend's six or seven updates. The video's banner-to-camera gaps (Marie 0.17 s,
the Executioner 0.27 s) are shorter and differ; the banner's own delay in the video was not
measured, so the port keeps its measured counts.

**An actor's readiness.** The action tables (`BtlActionTable`'s `action_table_*_data`, read from
the library's data):

| table | steps |
|---|---|
| attack (solo) | 1 2 10 7 9 6 15 16 5 17 19 18 5 21 23 5 22 26 12 8 9 24 25 11 14 78 |
| attack (pair) | 1 2 10 6 29 3 27 28 19 18 5 21 23 22 5 26 12 24 25 11 14 78 |
| item | 1 2 10 7 9 6 30 32 33 3 34 3 12 11 8 9 14 78 |
| sopia | 1 2 10 7 9 6 35 36 37 38 39 40 43 .. 49 13 12 41 42 11 8 9 14 78 |
| awake | 1 2 10 7 9 6 50 51 55 52 53 13 12 54 8 9 11 14 78 |
| advent | 1 2 10 7 9 6 56 57 58 59 64 65 62 63 13 12 66 8 9 11 14 78 |
| dont action | 1 2 10 7 9 77 12 8 9 11 14 78 |
| empty | 1 2 10 11 14 78 |

(the step functions by `BtlActionCtrl`'s table at +0x390: 1 `Init`, 2 `Standby`, 3 `WaitTime`, 5
`WaitMotion`, 6 `Announce`, 7 / 8 the event's Command_Start / Command_End, 9 `Event`, 10
`CommandStart`, 11 `CommandEnd`, 12 `CommandAnnounce`, 13 `Join`, 14 `Done`.) Step 2,
`ActionCommonStandby`, goes on only once `BtlUtilStatus::CheckActionReady` holds for the actor and
a pair's partner - the model not blending, its motion `bb01`, `bb04`, `bg01` or `bx02` - or after
5 s (+0x30). A blow's reaction (`CheckThrowBack`) cuts to `bd01` (or `bx03` in the Executioner's
Pose) with `bb01` (`bx02`) queued after it and no change time, so it plays whole and blends back over
its last 0.3 s. So a monster struck just before its turn waits for its damage motion before its
banner and its Command_Start. The port started the next action as soon as the last was over; it now
waits the same way, before Command_Start (a fighter down counts as ready; the port's motions cut,
so "ready" is the motion's end). Battle 221: the boss's "Flames of destruction kindled..." waits
1.30 s for its `bd01` after Darwin's Slash, its Gehenna Flame 0.80 s for `bx03`; battle 57, solo:
a Mantis struck by Orlando waits 0.20 s. The first action of a battle waits for the party's
opening `bb02` (0.15 s in an auto fight; a player choosing takes longer). Against the video
(round 25): Darwin's Slash to the next actor 1.97 s, the port's 1.90 s before - a wait of this kind
would close it if the Mantis acting next was the one struck, which the clip does not show.

**The banner's queue.** `ActionCommonAnnounce` queues the action's name with `SetAnnounceTime(1.0)`
(`mov r1, #0x3f800000` at 0x36cfe8); `BtlAnnounce::Update` shows an entry on its first update (no
time added), removes it once its time is reached - 21 updates, 1.05 s - and shows the next the update
after. `CalcCommand` sets 1.0 s again and queues the status notes (`ShowAnnounce(false)`: `ShowAnnounceStatus1`
.. `3`, each by `ShowAnnounceWindow` with the time set); `ActionSopiaWaitLaunch` calls it with
`true`, skipping the entry shown. Step 12, `ActionCommonCommandAnnounce`, waits till the queue is
empty (+0x128 null) - the last note's time run out. The default time is 2.0 s (`Initialize`); the
others: `StepBattleIn` 1.0, `BtlResult::Update` 2.0, `ActionReviveStandby` 3.0,
`ActionDontActionAnnounce` 2.0, `ActionSopiaAnalyze`, the escape's and the target's steps and the
game over's -1 (held). The port put the action's name up without starting the queue's clock, so a
note at the hit replaced it at once, and gave each note 1.5 s (from the states' first commit, not
read); an action's end did not wait for them. Now the name holds the queue for its 1.05 s, the notes
follow at 1.05 s each, a sopia's notes skip the name (at the hit, where the port lands them - the
original's `CalcCommand` for a sopia comes at the launch), and the action's end waits till the
queue is empty, after its own waits (the attack table's 0.1 s `WaitNextStart` comes after step 12
in the original; the port does not reorder them). Battle 245, Megiddo's Blessing: "Orlando is hit
with status effects!" at 5.04 s into the action as before, the next action 4.33 s after it (4.45 s
before). No reference clip with a status note was checked: the 1.05 s rests on the code.

### Round 40: which of a pair runs which table, and the pair's delay

Read in the repo's Ghidra output and `llvm-objdump` of the ARM library (the delay's caller is
0x36e094 in the file).

**Which of the two leads.** `SetActionOrder` walks the five slots and adds each one that can act
(`CheckActionable`: valid, not dead, not the side that lost the first turn); after a slot in a pair
(`IsPair`) it skips the next one, so only the lower of a pair - fighter 0, or monster slot 2 - is in
the order. `SetActionQueue` puts that slot's task in the queue and, for a pair, its partner's
(+0xb02) straight after. In `ActionCommand` the first of the pair to come (its side's +0x338 still 0)
sets the side's +0x338 to 1 and +0x339 to itself and makes the partner's task active; its partner,
coming next, finds +0x338 at 1 and raises it to 2 (and is cancelled instead for a command that is
not an attack and is magic-type, or whose data's +0x48 is 4). `SetActionTable` then gives the one in +0x339
`action_table_attack_solo` and the other `action_table_attack_pair`. The port's pair action has
fighter 0 (or monster 2) as its actor, whose moves run first, and its partner after the delay:
the same split. Nothing changed.

**The pair's delay.** `ActionAttackPairWaitDelayTime` (the second's controller) calls
`GetPairAttackDelayTime(cr3_common_data[leader + 1], cr3_common_data[second + 1])`: the literal pool
gives the GOT slot 0x5ab660, an `R_ARM_RELATIVE` to 0x5c3978, `cr3_common_data` itself, and
`ldrsb` reads byte `slot + 1`. Bytes 1 and 2 are the battle's two characters (from 0; set by the
map select's party (`MapSelectMenu`), or `VistaMain::SetBattle` from `FldUtilGetBattleParty` for a battle a script
calls), bytes 3 and 4 the active members (`Cr3UtilSetActivePartyMember`, set with 1 and 2 by the map
select), byte 5 the first party set's. Nothing writes a monster's number there. `GetPairAttackDelayTime`
takes the 9 x 9 table at 0x5362f4 for numbers 0 - 9 and, past 9, returns 0 unless both are 205 or
less (`cmp r5, #205; cmple r4, #205; bgt`). So a monster pair's delay - the Shadows' (pattern 11,
battles 47 - 51 and 305 - 309; the only monster pairs) in slots 2 and 3 - is the table's entry for
the active members, bytes 3 and 4. The port took the monsters' own numbers less 205 (Hurt Shadow is
205, Tough Shadow 206: 0.30 s); it now reads `game.party`, the port's bytes 3 and 4. A `--battle`
run keeps the new game's members (0 and 2: 0.25 s) unless `--members` gives others; in the story it
is whoever walks the field. Battle 47 (seed 12) runs the Shadows' Break Beat through as before. No
reference clip of a Shadow pair attack was checked: this rests on the code.

### Round 41: an item's motion, its launch effect, and its end

`action_table_item` (the library's `action_table_item_data`, 0x5afa98): 1, 2, 10, 7, 9, 6, 30,
32, 33, 3, 34, 3, 12, 11, 8, 9, 14, 78.

**The motion.** `ActionItemStandby` asks for motion 0x12 (`SetNextMotion(.., 0x12, true)`): bi01,
whatever the command's own motion. The sopia step's 0x11 (bm01) and a monster's 4 (bg01) confirm the
numbering. The item's command carries bm02, which no fighter has, so the port played nothing: the
user stood through the whole action. It now plays bi01 (Darwin and Marie 2.60 s, Orlando 3.35 s).
The motion table gives bi01 a loop (flags 0xf; Marie 1.1 - 2.1 s), which `ActionItemWaitLaunch`'s
`SetWaitMotion` ends at 2.0 s (`SetMotionLoop(false)`, bb01 next, `EndAnimation`). That is before
the loop's end for every user, so the port plays the motion once.

**The launch effect.** `ActionItemStandby` passes `GetMotionTimeEffectStart(0x12, 0)` - `0 / 20 +`
the motion table's effect start, its +0x4c (`vldr s15, [r3, #400]` at 0x36c85c): 1.1 s, Orlando's
1.15 s - to `ShowCommandLaunchEffect`, which makes the effect wait that long. It is the only caller.
The port showed Chocolate's `eff104` at the banner; it now shows it at that mark, as the arm goes up.
Battle 57 with Marie (`--party 1,3 --item 1:3 --flag 62 --input "solo attack 2 items i1 1"`):
frames 0.3 s in (Marie standing), 1.25 s (her arm up, the glow in her hand) and 2.4 s (the swirl and
"182"). The video has her arm up 1.35 s after the banner.

**The end.** `ActionItemWaitMotion` (not a revival) waits while the hit's effect is of kind 1 or 2.
It then waits for the user's motion type to be 1 - `BtlModel::GetMotionType`, by the motion name's
second letter: `b` and `B` give 1, so bb01 - bb04 -, and only then does `WaitStep(3, 3.0)`. The
port now waits for the user's stance too. None of the three's bi01 outlasts `eff213`, so the times
do not change: Marie's Chocolate ends 3.63 s in, and the turn's next banner comes 3.70 s after hers
(Orlando, battle 57: 3.83 -> 7.50 s).

**The video's 0.5 s, not found.** `ActionMain` runs a task's steps in one update while the step
changes (`do { ActionCommand } while (step changed && step != 0x4e)`), and the next task starts at
that update's end once none is in action state 2. So 12, 11, 8, 9, 14 and the next task's 1, 2, 10,
7, 9, 6 take one update together, and the next banner comes an update or two after step 34
passes - as in the port. The video's 4.33 s from banner to banner therefore puts the end of step 34
about 4.2 s in. The user's stance does not explain it: bi01, started at the banner, is over by about
2.7 s. Left: `eff213`'s length in the original (the port's 1.5 s, the effect gone from the picture
about 1.15 s after the heal); a script on `Command_End` / `Command_Start` in that battle (step 9 waits
while one runs); the Executioner's readiness. Not settled.

### Round 42: a boss battle's script calls, and the item's 0.5 s

**The video's fight.** `2dy_mefNhEI` around 616 s is a boss: one large beast, Darwin and Marie, Darwin's
Executioner (the awakening) landing its 693. `battle_221.nut`'s `Command_End` sets the awakening flags
3001 / 3002 after command 352: the port's battle 221 (pattern `neron`, `m310`), whose script
is its own (`Cr3BattleEvent_SetScript`, +0x237a; 49 battles, 221 - 349, the bosses).

**What the picture does.** Frame differences over the whole picture, 1/30 s apart (the clip is 30
frames a second over the game's 20): the swirl and glow change the picture until 620.37 s, about
1.4 s after the heal - `eff213`'s animation is 1.50 s (`anim.py info`), and a normal effect
(`BtlEffectTable` +0x3b 0) is destroyed when its time reaches its end (`ControlNormal`) - then
nothing but the stance's sway (0.1 - 0.2) till the next banner at 621.23 s: 0.8 s still.

**Where it goes.** With +0x237a set, step 7 (`ActionCommonEventCommandStart`) and step 8
(`ActionCommonEventCommandEnd`) call `BtlEvent::CallScript` ("Command_Start", "Command_End"), which
sets the event's state to 1; step 9 (`ActionCommonEvent`) waits while `BtlEvent::IsRunning` (the
state `Update` last took, +0xc, or the asked one, +0x10). `BtlEvent::Update` - registered after the
action controller, so later in the same update - calls the function at state 1 and sets 2; at 2,
once `Cr3Event::GetState` is 4, sets 3; at 3 sets 0. Step 9 sees +0xc at 0 the update after that:
four updates a call, the script's own work aside. No boss script's `Command_Start` / `Command_End`
waits (221's and 349's set flags and parameters; 301, 318, 331 have none). So an action in a boss
battle ends four updates later and the next one's banner comes four more after its start: 0.4 s.
The port called the scripts synchronously, at no cost; it now holds four updates after each call in a
battle with its own script. Battle 221 (`--fight solo`): Darwin's Slash 1.53 -> 1.72 s, the boss's
Hammer of Tyranny 4.78 -> 5.12 s. Battle 57 (no script of its own) is as before.

For the video's Chocolate the code now gives: the effect gone about 3.65 s after the banner, step 34
the update after, Command_End's four, the next task added an update later, Command_Start's four - the
next banner about 4.2 s after Chocolate's, against the video's 4.33 s. The last update or two
(`Cr3Event`'s state 4 may take its own update) were not followed. The item itself could not be run in
battle 221: its script keeps the Items command off early on (script switch 45).
Not followed: `Turn_Start`, `Turn_End` and `Dead` go through the same `BtlEvent` calls in a boss
battle and should cost the same four updates; the port still makes them at once.

### Round 43: a boss battle's turn and defeat calls

The other `BtlEvent` calls a battle with its own script makes: `Turn_Start` from
`BtlCommand::StepInit` (then step 2, `StepEvent`, waits while `IsRunning`), `Turn_End` from
`BtlTurnEnd::Update`'s case 1 (case 2 waits), `Dead` from the dead table's 68 `ActionDeadEvent`
(`action_table_dead_data` 0x5af9f4: 67, 69, 68, 9, 70 .. 76; its 9 waits). `BtlCommand` and
`BtlTurnEnd` take one step an update (`Update` dispatches the current one) and `ActionDead` runs
`ActionCommandMain` once an update for each one downed; all three are registered before `BtlEvent`.
So the waiting step, first tried the update after the call, passes once `BtlEvent` has gone 1, 2, 3,
0 - the fourth update after the call instead of the first: three updates more, against the action
table's four (it loops its steps within an update). The port made these calls at once; it now holds
three updates after each in a battle with its own script: the menu after `Turn_Start`, the next turn
(and the poison's 1.5 s) after `Turn_End`, a defeat's `DeadStart` after `Dead`. The poison's and the
pools' numbers and the poison's SE come with case 3, after that wait (a later fix: they had come at
the turn's end at once; battle 221 with Darwin poisoned, `--state 0:0`: the turn over at 11.08 s,
his 305 at 11.25 s; battle 57 as before, the number with the turn's end, turn 2 1.5 s on).
Battle 221 (`--fight solo`): turn 1 at 1.50 s (was 1.35), turn 2 at 12.95 s (12.63). Battle 57
unchanged. `Battle_Start` (`StepBattleIn`) and `Battle_End` were not followed.

### Round 44: a sopia's motion change, measured again

Earlier rounds read Marie's Banish banner at 638.60 s and the boss's Healing at 642.80 s, and
the port waited 3 updates (the party) and 5 (a monster) before the caster's camera. Read again
frame by frame, a frame difference over the banner's strip and over the whole picture, 1/30 s
apart: the banner turns from "Break Beat" to "Banish" between 638.45 and 638.48 s (frames at 638.42
and 638.55 s show the two names) and the camera cuts to Marie at 638.78 s - 0.30 s; "Healing"
comes between 642.75 and 642.78 s and the camera cuts at 643.05 s - 0.28 s. The two are the same.

That is the code's wait (round 39): `ActionSopiaStandby` asks for bm01 (a monster's bg01) with the
start blend and `SetMotionBlendStart`, so the next update's `BtlMotionCtrl` starts it blending
(`SetAnimationBlend(0.3)`, +0x68 set); `ActionSopiaWaitMagicMotion` waits for the model's name and
+0x68 clear, which `BtlModel::UpdateBlendAnimation` (`BtlModel::Update`, the model controller near
the update's end) does once its summed time passes 0.3 s; the camera starts as the step moves on.
The banner, queued the update the request is made, shows the next. Every party member's bm01 and
the bosses' have the blend bit (flags 0xf). The port now waits six updates for both: Darwin's
Cocytus (battle 57, `--flag 61 --input "solo sopia s55 2 attack 2"`) - the caster's camera 0.32 s
after the banner (was 0.17), the target's 2.00 s after that. The hit follows 2.0 s after the
caster's camera, as the video's 424 (2.38 s after the banner) and 333 (2.36 s). Chocolate's banner (round 41's 616.90 s) reads
the same again: it and the camera change together at 616.87 s.

## Original-code evidence

Fresh REA / Ghidra static inspection of the Android 2.1.2 library (`provider_id: ghidra`,
Ghidra 12.1.2); functions by their exported names. Evidence IDs are the REA sessions' (twenty-four: the
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
`FadeWindowMenu` row, the twentieth - the battle's opening - the `BtlAnnounce` row, the twenty-first - rounds 31 - 34 rechecked, the attack's approach and the win pose - from the `ActionItemWaitLaunch` row on, the twenty-second - the win's camera and the result's pace - from the `BtlResult::Update` row on, the twenty-third - a death at its blow - the `BtlActionCtrl::CheckDead` row, the twenty-fourth - a loss - the `BtlGameover::Update` row).
`re/opening-audit-evidence.json` (never committed) holds the second session's records,
`re/opening-audit-evidence-3.json` the third's, `-4.json` the fourth's, `-5.json` the fifth's, `-6.json` the sixth's, `-7.json` the seventh's, `-8.json` the eighth's, `-9.json` the ninth's, `-10.json` the tenth's, `-11.json` the eleventh's, `-12.json` the twelfth's, `-13.json` the thirteenth's, `-14.json` the fourteenth's, `-15.json` the fifteenth's, `-16.json` the sixteenth's, `-17.json` the seventeenth's, `-18.json` the eighteenth's, `-19.json` the nineteenth's, `-20.json` the twentieth's, `-21.json` the twenty-first's, `-22.json` the twenty-second's, `-23.json` the twenty-third's and `-24.json` the twenty-fourth's; the first's bundle was overwritten by the second,
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
| `BtlAnnounce::SetAnnounceTime / Update / SetAnnounce / ShowAnnounceWindow`; every `SetAnnounceTime` call's `r1` (1.0, 2.0, 3.0, -1) from `llvm-objdump` of the same file; `BtlMainCtrl::StepBattleIn`'s steps read in the repo's own decompilation | `ev_96c5721e6917557791c7a5c1e0b6966ed85e193c6c4c233a96ba403100c4a47e` |
| `ActionItemWaitLaunch / WaitHit`, `ActionSopiaWaitMotion / WaitVoice / MirrorWaitEffect / WaitHit` (round 34's recheck of rounds 31 - 33) | `ev_23df37b8aee1066bf0dd770b9386067fcca77dafd4b76c76df45178a162c397d` |
| `ActionItemWaitMotion` assembly (`WaitStep(3, 3.0)`, a revival's 0.5) | `ev_7a570b4750e5c7f6bf4f8c7c26a13d07d34f5e5c11eac27953da9a3b51e630d6` |
| `BtlCameraTable::GetUseItem / GetItemSopiaTarget / GetItemSopiaTargetGroup / GetUseSopia / GetAttackToEnemy`, `GetPlayerCamNo` | `ev_580ec5a6013e0047701a7454643a364c0a6c89700a6a7e44069956bd5fc7fd60` |
| `SetTargetCamera`, `ActionSopiaWaitLaunch` | `ev_d918f1955c59cb4d5e2ceb1be8c7b6388734ea108b4dda397bdc0c24f1140516` |
| `ActionSopiaWaitMagicMotion`, `HitEffect`, `BtlEtcCtrl::CreateDispNumber` | `ev_0f083acb98af6137483d2aad8568df7adf5b745699de903c702690c7ea8ecbe6` |
| `StepSetAction`, `CheckAllActionDone`, `BtlAnnounce::Update`, `BtlCommand::StepEscapeSuccess / Loop` | `ev_350377398ce8aa4efc5055924ae4f463526bda4f35d8def041f04ace82eda477` |
| `BtlMain::Init` (the modules' and task modules' order) | `ev_1a9b2aa110425ea0f8d3ea6ea31605cd41e12a11375bde654f36477f771284b0` |
| `ActionSopiaLaunch` | `ev_fe3da5b53821a628df28ae7ffeb479ad4b40f97b76ffcd337154d0fa891303c9` |
| `ActionSopiaStandby` | `ev_2139454635f094994c12771e23e4dc8fcdac3c3d8a5a251f4aa2d8f29e5e9177` |
| `BtlUtilData::GetMotionTypeFromCommand / GetMotionFromCommand` | `ev_c2355ade4fb84fca68bfe4f86e1af1a4228eca1a25a332b32cf28ff53e8d649a` |
| `BtlResult::SetVictoryMotion` | `ev_b4a480a63748c0070a6ced238120520209be3ba86e8ccc8588e972362aedf326` |
| `ActionAttackCamera`, `ActionAttackWaitAttackMotion` (and its assembly: `GetMotionTimeHitStart(9, 0, -4.0)`, `WaitStep(5, ..)`), `SetAttackCamera2`, `GetMotionTimeHitStart`, `ActionCommonWaitMotion`, `GetAposName` | `ev_2991a0dbb4639d38a923f142af80931730623c9c9ed7e5ad133ea6a9e296ffda`, `ev_002b2091376fd4ec588c54ed9fab23d6b44f068e1290e0fab0cdf3a7af69d0f2`, `ev_2c5182091c6165b3b9d693534078f188b710fee746e6615a59409f21d33d6da5`, `ev_629d33d396ae8dffd21e2289fbed17f59d376443576ebca24566c744e1ffb7ec` |
| `BtlResult::Update`, `SetVictoryCamera`, `UpdateResult` | `ev_9393b2e0ee96b67aaa2fd8bd38f69dcbf5dd9600918c357c507d89b563d199d2` |
| `BtlSound::PlayVoiceVictory`; `BtlResult::ProcessLvup` (`StepGetsetLoop`, `CheckDead`, `FixTodome` read in the repo's own decompilation) | `ev_134d035e166b40336c71ffdb49d1becfdfdbd26eb1b512eb5edc74033d5d8439`, `ev_2d2266c42c5fdc60e1089832d043d949fc054c7c7657df67aecf575636e771db` |
| `BtlActionCtrl::CheckDead` and its callers (`Hit`, `Damage`, `ExecCommandAction`), `ActionDead`, `ActionDeadStandby`; `CheckStop`, `ActionMain`'s +0x320 test, `StepAction`'s order read in the repo's own decompilation | `ev_4d88c0290f413b2691b087ad5ee8da0e89ffaa7849683852affc8578f1b6bd6d`, `ev_fe68925f8e9211dde1223d7e2f7f308bebc0386e4b924214d5ca110fe7e63e5d` |
| `BtlGameover::Update` | `ev_d3cc15d0f765ca0b0568bf8a4aea4998e6cddac85a5e3d76817a25961d946cd1` |

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
- **The action controller's other waits.** Read and followed (rounds 27 - 33): the attack's, a
  pair's, the one not done's, the revival's, the turn's end, an item's, the whole sopia table with
  the Mirror; the actor's readiness (round 39). Left: an item's end in the video (round 42: a boss
  battle's `Command_End` and `Command_Start` calls, four updates each - now about 4.2 s against the
  video's 4.33); a boss battle's turn and defeat calls (round 43: three updates each). (Round 44: a
  sopia's motion change - six updates, the video's banner times read again. Round 35 read +0x523:
  the Pair turn's flag; round 40 which of a pair runs which table.)
- **The camera's units.** The ease's one-pixel hold is the original's device pixel; the port
  holds within one pixel of the window it draws to. `GetPos_X / _Y` in the port still read the
  clamped scroll (`shownX_`), the original's the unclamped +0x30.
- **A finger left down at an event's end** holds control in step 1; the port's touches start a
  walk only on a press, so one held from the event never walks, and a press during the three
  updates is refused rather than held.
- **The map name's last update.** The original deletes the menu in the `InterfaceMain::Update`
  whose step reaches the animation's end; the port shows it while `ticks x 0.05 s` is under its
  length. Within an update of each other; not counted exactly.
- **The battle's last update before the next main.** `Exit` waits for `Cr3ResourceManager` to be
  empty; the port counts 24 updates from the end, as if the resources went at once. A slow unload
  would hold the original's white a little longer.
- **The skipped voice, run.** No headless run held a skip across a voiced message: the opening's
  two are skipped whole, and `--input fast` takes hold only once a button shows, after the
  message's voice. Checked by reading only; the opening without a skip still voices 624 and 625.
- **+0x2169.** `ShowMessage`'s second voice test is a window flag; the page's +0x2168 is the
  message's voice (round 33), and the flag likely not part of it; what sets it was not read. The
  port takes it as clear.

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
