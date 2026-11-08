# CR2-U v0.4.5

A from-scratch reimplementation of *Chaos Rings II* for Windows, macOS, Linux and Android.

## What's new

- **FSR 3.1.6:** the built-in FSR is updated from 3.1.4 to AMD's FSR SDK 2.4 passes.
- **FSR 4 (Linux and Windows, AMD GPUs):** a new Upscaler choice. AMD ships FSR 4 for Vulkan only as
  its own Windows DLLs, so they aren't included: put AMD's two SDK DLLs, the driver's
  `amdvkffx64.dll` and (on Linux) ffx-vk-linux's `libffx_vk_linux.so` into `fsr4/` in the game
  folder - its `README.txt` says where each comes from. On Linux the DLLs run natively through
  ffx-vk-linux, no Wine. Without the files FSR 3.1 stays as it is.
- **Cutscenes closer to the original** (the opening checked call by call against the Android
  library's code): a fade-in starts three frames later and a fade-out one frame later, as the
  original's fade windows do; a scripted camera (and light) move rests on its last frame instead of
  one past it; a page's last tap reaches the script two frames on, with the text still shown, and a
  tap within 150 ms of a page's start no longer turns it; a narration that ends by itself hands
  over to the script the frame its last character types; a motion blend (`ChangeAnimatorBlend`) takes the seconds
  the script names - 3, 8 or 10 s for the idle motions - not that many frames; and the game's own
  pauses between scenes are kept: a cutscene's battle begins six frames after its script ends, a
  field event's cutscene ten frames after the event's and the area it warps to eight, the scene
  going on meanwhile, as the original's `VistaMain` and `FldMain` have it. A message window's text
  waits for the window's opening animation (the cut-ins' 0.15 s, the tutorials' 0.2 s), windows
  and portraits start moving the frame after they are called for, and a tap skips the cut-in
  dialogue's typing only on the window itself (a tap anywhere still turns a page), as in the
  original. A message line is centred by its own `\c`, not its page's first line's: the
  tutorials' titles ("Moving") and the announcements ("Lv. 51 to 60 unlocked!") sit in the middle
  of their window again instead of at its left. In the field, a scripted camera slide stops where
  the original's does (a step short of its end), a script that takes the camera without placing it
  keeps the view where it was instead of jumping, and the yes / no buttons slide out over 0.2 s
  instead of vanishing. Talking to someone no longer snaps the leader round to face them (the
  scenes turn the leader themselves), a talk or an event box starts on the original's frame, and
  the leader can move again a few frames after an event ends, as in the original. Walking off an
  area now fades to black before the next one, as the original does. A random encounter keeps the
  original's rhythm: the leader takes two more steps, then the sound and the wipe, and the battle
  starts 1.4 s after the encounter instead of 0.65 s. A battle's end fades to white with its music
  before the field fades back in from white, instead of cutting to a white screen. A battle that
  its script ends (three of Amon's and the Tentacle's) leaves at once, as the original does,
  instead of holding 1.5 s first. The result window keeps the original's pace: the EXP goes in
  over 3 s instead of 2, each level-up line waits for a tap or 5 s, and the Oz, items and sopia
  messages wait for a tap instead of moving on after 3 s. Getting away from a battle leaves after
  about 2 s as in the original, not 3.5, and the result window slides away as the battle fades
  out instead of staying whole. Characters turn as the original's do, quickly and then easing in,
  instead of at an even pace; the leader turns toward a nearby balloon or mark on stopping, as in
  the original, and stands still under an encounter's wipe. The field camera follows the leader
  with the original's easing instead of being locked to it, leans toward a high jump's, mystic's
  or aim's mark, and eases back after a scripted camera move. Closing a menu or finishing a touch
  action starts its scene on the next update, as the original does. A line's voice stays quiet
  while it is fast-forwarded or skipped, as in the original. An encounter's wipe, in and out,
  takes a quarter of a second as in the original, not half a second. A battle's first turn comes
  1.3 s after it appears instead of 3 s, and a first strike's banner shows for a second at the
  original's moment, as the reference videos show. An attack starts its swing a moment after its
  camera and hands over to the next fighter once the attacker is back in place, not after its whole
  animation (a Mantis's attack 0.7 s shorter), as the reference video times them; a pair attack is
  announced as "Pair Attack". An action that can't be
  carried out holds its message for 2 seconds, an item takes effect 2 seconds after it is used, a
  revival lasts its animation and poison at a turn's end holds 1.5 seconds, as the original's code
  has them. Sopia skills without an animation of their own (Break Beat, Preemptive Strike, Seal
  Strike, Dáinsleif ...) are swung with the character's attack, as in the original, instead of
  landing at once with no movement. Using an item now looks the way it does in the reference
  video: the camera is on the user, cuts to the one it's used on after 2 seconds, the item's
  effect plays over them (a heal had none before), and the battle waits for it. Damage and heal
  numbers appear at the chest, not at the feet. Casting a spell (Cocytus, ...) is timed as in the
  reference video: the camera turns to the caster, then to the target 2 seconds later, where the
  spell lands, and the battle waits for its effect, instead of landing within a second under an
  attack's camera.
- **Vulkan: fewer hitches.** The renderer keeps the driver's compiled pipelines between runs
  (`vulkan-pipelines.bin` in the profile folder), so the start and each new screen don't compile
  them again. Measured on a scene here: 6.7 ms of pipeline compiling the first time, 0.1 ms the
  next; slower drivers save more. A cache from another GPU or driver is ignored and rebuilt.
- **Battles:** the results of a won battle no longer stay on screen over the next one. The next
  battle used to lose its HUD while the last results' panels came in over the fight (reported by
  [metastigma](https://github.com/metastigma)).
- **Vita copies:** text is in the language you chose again. The Vita's message files hold 7
  languages, not 8, so much of the text - the tutorial, battle announcements - came out in Japanese
  or Chinese.
- **Windows:** settings, saves and the first-start setup work with a folder or user name in another
  alphabet - a Cyrillic user name on a Russian Windows, for example. The Graphics list's "Swap ○
  and ×" row and its help show their symbols again (they came out "Swap ? and"), and so do the
  iPad's and the Vita's staff-roll symbols: the Windows build now keeps its text in UTF-8.
- **Options:** Import Vita Saves moved into the Graphics list (it used to cover Lock Orientation's
  button), and the title stays behind the Options.
- **Exit:** closing the window no longer crashes on the way out.

Thanks to [metastigma](https://github.com/metastigma) for the report and for testing. The battle fix was reproduced with the Vita data and
checked over several battles in a row.

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
- **Android:** install the APK, then copy your game files over USB into
  `Android/data/local.preservation.chaosrings2/files/`, in an `ios/`, `android/` or `vita/` folder.

The README's "Your copy" section lists exactly which files each release needs.

## Good to know

- **Android, updating:** from this version on, each release installs over the one before it. Every
  earlier version was signed with a different key, so coming from one, uninstall it first - one
  last time. That deletes the game files you copied in and your saves: keep a copy of the game
  files to copy back.
- **No game data is included.** You need your own copy; the port reads it at run time.
- **Tested on:** Linux (played), Windows (under Wine). The macOS and Android builds are made by CI
  but haven't been run on real hardware yet. Reports are welcome.
- **DLSS** isn't in these builds (NVIDIA's library can't ship with GPL code); you can build it
  yourself (README, Platforms).
- Licence GPL-2.0-or-later (the macOS app is GPL-3.0 because of MoltenVK). A fan preservation
  project, not affiliated with Square Enix or Media.Vision.
