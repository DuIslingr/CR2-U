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

## Original-code evidence

Fresh REA / Ghidra static inspection of the Android 2.1.2 library (`provider_id: ghidra`,
Ghidra 12.1.2); functions by their exported names. Evidence IDs are the REA sessions' (three:
the second from the `FldMain::Update` row on, the third from the `FldUtilSetPopPoint` row on).
`re/opening-audit-evidence.json` (never committed) holds the second session's records and
`re/opening-audit-evidence-3.json` the third's; the first's bundle was overwritten by the second,
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
| The fade colour tables at 0x51e208 (48 bytes) | `ev_e855b7b8c9e85282a529f88d42e04c330c84f2b6e9fa41df062d5857225339c9` |

These are static observations of the shipped library, not traces of the original running: the
update order and counts follow from the code, the visual result from the fade figures'
animations being linear (assumed, as before).

## Not settled

- **The first character of each line.** `NarrationWindowMenu::SetStringData` makes a `MenuText` per
  line of a block and steps each once; whether that shows each line's first character at once
  (the typing then line after line) depends on `MenuText`'s renderer (`CustomFigure`), not read.
  The port types the block from its first character.
- **A tap while typing** in the other windows: the vista's, the help (tutorial) and the system
  windows' handlers have the narration's shape (`VistaConversationWindowMenu` /
  `HelpWindowMenu` / `SystemWindowMenu::TouchGestureTap`: the block whole, given a permit byte
  whose setting at their open was not traced); the announcement window ignores taps
  (`AnnouncementWindowMenu::TouchGestureTap` returns 0); the cut-ins' conversation window takes
  a tap only on its own tap area (`ConversationWindowMenu::TouchGestureTap`:
  `Cr3UtilCheckHitTapCollision` on its figure). The port treats a tap anywhere alike for all.
- The battle's own end (`BtlMain`'s result steps and when it says it is done): not counted; the
  port returns to the field as its battle ends.
- A voice is not played while the skip button is held (`ShowMessage`'s `IsSkip`); the port plays
  it. Not in the opening's reach (its two narrations are skipped whole).

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

(The second narration is `... \w60 \*`.) The captures are frames of the port, compared with the
counts above, not with the original running.
