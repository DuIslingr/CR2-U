# CR2-U v0.5.0

A from-scratch reimplementation of *Chaos Rings II* for Windows, macOS, Linux and Android.

## What's new

### Controller, as on the Vita

- **Battles:** Up / Down move through the commands and the targets, with the cursor sound.
- **Camp:** L / R switch the item category and the equipment's character. Up / Down move through
  the camp's buttons and skip greyed-out ones.
- **Shop:** in the equipment lists L / R change the character and Left / Right the kind (weapons,
  armour, accessories).
- **Yes / No questions:** a cursor starts on Yes; Up / Down move it, ○ answers, × is No.
- **Game over:** its question now takes keys, buttons and clicks. Before, nothing answered it.
- **Pub board:** the d-pad moves over the posters and the Check button.
- Cursors light up once you move them with a key or button. `docs/CONTROLS.md` has the full layout.

### Closer to the original

The opening, the field and the battles were checked step by step against the original's code and
reference videos, and their timing now matches:

- **Cutscenes:** fades, camera moves, message windows and portraits open and close on the
  original's frames. Taps turn pages as in the original, and centred lines (tutorial titles,
  announcements) are centred again.
- **Field:** talking and events start and end on the original's frames, and the leader no longer
  snaps round to face someone. Leaving an area fades to black first. The camera eases after the
  leader, and characters turn smoothly.
- **Encounters:** the leader takes two more steps before the wipe, which takes 0.25 s; the battle
  starts 1.4 s after the encounter.
- **Battles:** the first turn comes 1.3 s in. Attacks, items, spells and sopia skills are paced and
  framed as in the original, and damage numbers appear at the chest. A battle now fades out with its
  music before the field fades back in.
- **Results:** EXP fills over 3 s, and level-ups and rewards wait for a tap. Escaping takes about
  2 s.
- A line's voice stays quiet while you fast-forward or skip.

### Graphics

- **FSR 4** (AMD RX 6000 or later, Linux and Windows): a new Upscaler choice. AMD's files can't be
  included; put them in `fsr4/` (its `README.txt` lists them). On Linux they run natively through
  ffx-vk-linux, without Wine.
- **FSR 3.1.6:** the built-in FSR is updated to AMD's FSR SDK 2.4.
- **Fewer hitches with Vulkan:** compiled shaders are kept between runs (`vulkan-pipelines.bin` in
  the profile folder), so starts and new screens are smoother.
- **Phones:** the picture fills the whole screen, around a camera hole too; menus and buttons stay
  inside the safe area.

### Setup and platforms

- **Android:** the first-start setup now runs on the phone too. Pick your APK and OBB, `.ipa` or
  Vita archive, and the files are copied in.
- **Vita dumps:** a NoNpDrm dump copied into `vita/` is decrypted on the next start, on PC and
  phone.
- **Vita, Prequel Trilogy:** the port no longer picks up Chaos Rings III's files from the trilogy
  folder, which closed the app.
- **New Game Plus:** the opening now plays. Before, you were left on the lava field with nothing to
  do (reported by [asbel12](https://github.com/asbel12)).
- **Android:** no more green frame around the screen after using a controller or keyboard.
- **Windows:** the "Swap ○ and ×" setting and the staff roll's symbols show correctly.

## Downloads

| File | Platform |
|---|---|
| `CR2-U-windows-x64.zip` | Windows 10+ x64 |
| `CR2-U-macos-universal.zip` | macOS 11+ (Apple silicon and Intel) |
| `CR2-U-x86_64.AppImage` / `CR2-U-linux-x86_64.tar.gz` | Linux x86-64 |
| `CR2-U-android.apk` | Android 7+ |

## Getting started

1. Download the file for your system and unzip it.
2. Start `cr2view`. If it can't find your copy of the game, it asks you for one and sets it up.
3. Play.

- **Windows:** if SmartScreen warns about an unsigned program, choose *More info*, *Run anyway*.
- **macOS:** right-click `cr2view.app` and choose *Open* (it isn't signed).
- **Linux:** `chmod +x` the AppImage, then run it.
- **Android:** install the APK and start it; the setup asks for your game files. Or copy them over
  USB into `Android/data/local.preservation.chaosrings2/files/`, in an `ios/`, `android/` or `vita/`
  folder.

The README's "What you need" section lists which releases of the game work and what each needs.

## Good to know

- **Android, updating:** from this version on, each release installs over the one before it.
  Earlier versions were each signed with a different key, so uninstall the old one first - one
  last time. That deletes the game files you copied in and your saves, so keep a copy of them.
- **No game data is included.** You need your own copy; the port reads it at run time.
- **Tested on:** Linux (played), Windows (under Wine). The macOS and Android builds are made by CI
  but haven't been run on real hardware yet. Reports are welcome.
- **DLSS** isn't in these builds (NVIDIA's library can't ship with GPL code); you can build it
  yourself (README, Building from source).
- Licence GPL-2.0-or-later (the macOS app is GPL-3.0 because of MoltenVK). A fan preservation
  project, not affiliated with Square Enix or Media.Vision.
