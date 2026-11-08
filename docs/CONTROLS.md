# The Vita's controls, and CR2-U's controller

The iPad's and Android's releases are touch alone. The Vita's (PCSG00498) adds its buttons in its
own code - there are no names for it in the Android library, so this is read from the Vita build's
decompilation (`re/vita`, `tools/re_setup.sh`):

- **The pad** (0x810efbe6): `sceCtrlReadBufferPositive`, the sticks to -1 .. 1 with a 0.25 dead zone;
  sixteen buttons by the table at 0x812fdcac - □ ×  ○ △, L R, -, -, SELECT START, -, -, up right down
  left (SCE_CTRL 0x8000, 0x4000, 0x2000, 0x1000, 0x100, 0x200, 1, 8, 0x10 .. 0x80) - into
  MVGL::Input's device record (+0x404.. the sticks, +0x42c.. the buttons).
- **InputEquipment** (Setup at 0x810ec...: Android's same channels, mpSquare .. mpR2, mpUp .. mpRight,
  the four axes): each channel keeps +0x20 held, +0x21 pressed this update, +0x22 released, +0x28
  repeating (pressed, then after 0.6 s every 0.08 s: 0x810eda7c). On the Vita the four directions
  are the d-pad *or* the left stick pushed all the way (0x810ed90e).
- **The queries** (Android's ButtonType: 0 up, 1 down, 2 left, 3 right, 4 □, 5 ×, 6 △, 7 ○, 8 L1,
  9 L2, 10 R1, 11 R2, 12 START, 13 SELECT) and four masks over them (0x81044ebc pressed, 0x81044fa6
  held, 0x81045090 released, 0x8104517a repeating: bit 1 << (type + 1) - 0x2 up .. 0x100 ○, 0x200
  L, 0x400 R, 0x800 START, 0x1000 SELECT); 0x810452ba / 0x810452fe step a cursor by up / down,
  playing SE 0xb3.

What the game does with them (the 38 functions that call the masks):

| Button | The Vita |
|---|---|
| ○ | confirm; on the field the nearest touch action or balloon (FldMain::CheckTouchEventExec_Tap), a message's page (Cr3UtilGetTouchState ORs it into the touch), a result's or a special scene's tap |
| × | back, cancel |
| □ held | the messages' fast-forward, the staff roll's (VistaConversationWindowMenu, StaffrollMenu, the menus' texts) |
| d-pad / left stick | the menus' cursors (repeating; a step SE 0xb3 where a circle button takes the focus - the lists scroll without one); on the field the stick walks - FldMain::PlayerMove takes its push as a finger's drag; the shop's equipment lists: Left / Right the kind, weapons, armour, accessories round (FUN_810c4650 / FUN_810c54ca, SE 0xb7) |
| L / R | a scroll list's switch (ScrollListMenu::PartsUpdate: 0x200 / 0x400 its ScrollListSwitchButton's left / right) - the items' category (CampItemListMenu::Update), the equipment's character (CampEquipmentListMenu::Update_EquipView), the shop's equipment's character (ShopWepon Buy / SellListMenu, with ShopCutInPiupiuMenu's face), SE 0xb7; a list without a switch: nothing |
| START | the title's "Please Touch" (with ○) |
| △, SELECT | nothing |

The screens' cursors, by the Vita's own updates:

- **Battle** (BattleSoloMenu / the pair's, FUN_81078726; BattleSelectMenu, FUN_810771c2): the command
  buttons and the targets both Up / Down round over those selectable (FUN_810452fe), SE 0xb3; ○ the
  one in focus (0xb4), × back (0xb5).
- **The camp** (CampTopMenu, FUN_81084c8e): Items, Equipment, Sopia, Options Up / Down round, past
  Items greyed with none held; SE 0xb3.
- **A question** (BattleYNChoice, FUN_8107beec - the System window's and the events' Yes / No): Yes
  in focus as it opens, Up / Down the other (FUN_810452ba: SE 0xb3), ○ the one in focus, × No. The
  quantity beside it (CampChooseQuantityMenu, FUN_8107d648) takes Left / Right alone.
- **The pub's board** (PubTopMenu, FUN_810b7b62): the posters three a row and Check - Left / Right
  along a row, past its end (or the last poster) Check, from Check Left the sixth (or the last) and
  Right the fourth (or the first); Up / Down a row on, round, where a poster is; SE 0xb3. ○ on Check
  the list, on a poster picks it.

The field's tab (FieldMenuTabMenu, 0x81098da8) is opened by a touch even on the Vita; once open the
d-pad steps round camp, encounters, save, map (the encounters left out while off), the one lit in
diffuse colour (1, 2, 3), ○ choosing it.

CR2-U maps a controller onto that by position (○ the right button, × the bottom one; the options'
Graphics list has Swap ○ and ×), as the keys the menus take (`Viewer::padButtonKey`, then
`gameKey`): ○ Return, × Backspace, the directions the arrows with the same repeat, L / R PageUp /
PageDown - a list's switch where it has one, else (CR2-U's) four rows on -, START the title, □ held
the fast-forward. The focus of a question, the camp's buttons, the battle's commands and targets and
the pub's board is lit once a key has moved one (a touch shows none). △ opens the field's tab - the port's own, as a controller has no touch. Where the
touch releases have no cursor of the keyboard's either (the title's buttons and questions, the
shop's first two menus, the party's), the keys and the controller now move a focus over the buttons
in the game's own focused look. `CR2_PAD_SCRIPT` (tests) drives a virtual controller (`shot=<file.bmp>` saves the next frame, `quit`
closes the window); the `viewer-pad-controls` CTest (`cr2view --selftest-pad-controls`) presses a
virtual controller's buttons through the same mapping on the camp, the shop, a question and the pub
without the game's files.

On the field the mouse (or a finger) walks as the touch releases' do (FldMain::PlayerMove,
FldPlayer::Update): put down where nothing else takes it and dragged, the leader goes along the line
from where it went down to where it is - under 2% of the screen (its width and height) standing,
under 5% walking, else running; let go without moving, it is the leader's tap. The stick
(FldController: `moveWin` where it went down, `moveCsr` towards the finger, at most 128 / 2048 of
the interface's units away) shows once it moves or after 0.5 s. `CR2_MOUSE_SCRIPT` (tests) drives
the mouse; `--drag x0,y0,x1,y1,s` drags before a capture.
