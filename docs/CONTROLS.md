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
| d-pad / left stick | the menus' cursors (repeating); on the field the stick walks - FldMain::PlayerMove takes its push as a finger's drag |
| L / R | a scroll list's page (ScrollListMenu::PartsUpdate), the shop's character (ShopCutInPiupiuMenu) |
| START | the title's "Please Touch" (with ○) |
| △, SELECT | nothing |

The field's tab (FieldMenuTabMenu, 0x81098da8) is opened by a touch even on the Vita; once open the
d-pad steps round camp, encounters, save, map (the encounters left out while off), the one lit in
diffuse colour (1, 2, 3), ○ choosing it.

CR2-U maps a controller onto that by position (○ the right button, × the bottom one; the options'
Graphics list has Swap ○ and ×), as the keys the menus take: ○ Return, × Backspace, the directions
the arrows with the same repeat, L / R a page (the shop: its character), START the title, □ held
the fast-forward. △ opens the field's tab - the port's own, as a controller has no touch. Where the
touch releases have no cursor of the keyboard's either (the title's buttons and questions, the
shop's first two menus, the party's), the keys and the controller now move a focus over the buttons
in the game's own focused look. `CR2_PAD_SCRIPT` (tests) drives a virtual controller.

On the field the mouse (or a finger) walks as the touch releases' do (FldMain::PlayerMove,
FldPlayer::Update): put down where nothing else takes it and dragged, the leader goes along the line
from where it went down to where it is - under 2% of the screen (its width and height) standing,
under 5% walking, else running; let go without moving, it is the leader's tap. The stick
(FldController: `moveWin` where it went down, `moveCsr` towards the finger, at most 128 / 2048 of
the interface's units away) shows once it moves or after 0.5 s. `CR2_MOUSE_SCRIPT` (tests) drives
the mouse; `--drag x0,y0,x1,y1,s` drags before a capture.
