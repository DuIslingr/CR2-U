# CR2-U v0.7.0

A from-scratch reimplementation of *Chaos Rings II* for Windows, macOS, Linux, Android, and now
iPhone and iPad.

## What's new

### iPhone and iPad

- **A new iOS build** (iOS 15 or later): an unsigned `.ipa` to sideload with AltStore, Sideloadly
  or similar. It draws through Vulkan (MoltenVK), with MetalFX as an upscaler from iOS 16, and falls
  back to OpenGL ES. It runs at up to 120 Hz on ProMotion screens.
- **Your game files** go in the app's own folder, *Chaos Rings II* in the Files app (or in Finder
  with the phone connected). On the first start the setup asks for them and copies them in: the
  iPad version's `.ipa`, an Android APK and OBB, or a Vita dump.
- **Foldables** like the iPhone Duo: the picture follows when you fold or unfold. The new **Camera
  Cutout** option keeps menus and text clear of the notch or punch hole (Avoid) or lets them use
  the whole screen (Ignore).
- Text falls back to the system's fonts where the game's own lack a character.
- Switching to another app and back no longer leaves the picture frozen.

### Cheats

A new **Cheats** page in the options (the port's own, all off by default):

- **Infinite HP** and **Infinite MP:** the party is never felled and pays nothing for skills and
  sopia. MP is also kept full between battles.
- **One-Hit Kills:** any party blow that lands fells the monster. A miss is still a miss.
- **Full Break Gauge:** the party gets a turn break every turn (+100 power, half the damage taken).
- **Full Charge Gauge:** the charge gauge stays at 300, so awakenings and advents are always
  ready.
- **Always First Strike:** the party moves first in every random battle; no more monster
  assaults.
- **No Random Battles:** no encounters while walking. The story's battles still come.
- **EXP Multiplier** and **SP Multiplier:** 2, 3, 5 or 10 times as much.
- **Max Oz:** 9,999,999 Oz.

Story battles keep their own rules. The cheats are saved with your settings.

### Setup and files

- **Android, missing text** (issue #4): the font is now found in any APK next to the OBB. Without
  one (an iPad or Vita copy on a phone), Android's own fonts are used, whichever the phone has.
- **`font/` folder:** put your own fonts in the game folder's `font/` and they're used in place of
  the game's.
- **Android bundles** (`.xapk`, `.apks`, or a `.zip` of the APK and OBB) and an `.ipa` you put in
  `ios/` yourself are unpacked on the first start. A progress window shows while that runs.
- **Vita dumps:** `.rar` archives that failed to unpack before now work, and a dropped archive is
  tried again if the first attempt fails. Vita dumps are decrypted by the port's own code.
- **Vita copies, random battles:** monsters now appear while you walk the dungeons. Before, a Vita
  copy had no random battles at all, on any platform.
- **A log for bug reports:** `cr2view.log` in the profile folder (in the app's folder on iOS), with
  the previous start's kept as `cr2view.prev.log`.

### Controls and display

- **Controllers** can be plugged in or out while you play.
- **F11** or **Alt+Enter** switches between a window and fullscreen.

### Closer to the original

Another long pass against the original's code and reference videos, rounds 33 to 106:

- **Menus:** the camp, shop, pub, library, party organization, world and chapter select, title,
  options and manual open, close and answer on the original's steps. Lists close before what you
  picked opens, and rows play out and back in when you change the category or character.
- **Lists and buttons:** every list can be dragged, and a press on the scroll bar's track jumps
  there. Buttons light up while your finger is on them. A greyed-out button or row plays the
  original's "can't" sound and does nothing else.
- **Battles:** boss battles take their scripted pauses; sopia and item motions, launch effects and
  waits are timed as in the original; the attacker faces its target from the start; and the
  breaking-glass ending runs at the original's rate. The damage formulas, instant death, status
  rolls, healing, criticals and pair rules were checked line by line, along with the order in which
  a blow's targets are worked out.
- **Field:** the map name, the dungeon map, saving, the leader's turns and the party members'
  fidgets follow the original's timing. A finger left down at the end of an event no longer moves
  the leader.
- **Text:** Oz amounts, the clock and item counts are formatted as in the original, and pages
  break and wait as the original's do, fast-forward included.
- **Saves:** clear-data carry-over, the level bonus past level 20, the play-time cap, the
  encounter switch and the Vita's options are kept as the original keeps them.

### Thanks to metastigma

Thanks to [metastigma](https://github.com/metastigma), whose Chaos Rings Omega fork
([CRO-U-metastigma](https://github.com/metastigma/CRO-U-metastigma)) led to part of this release:

- **From reviewing their fork:** controller hot-plug, after their Windows controller work; F11 /
  Alt+Enter for fullscreen, which their fork had first; and the move to SDL 3.4.20
  ([e402211](https://github.com/DuIslingr/CR2-U/commit/e402211)).

## Downloads

| File | Platform |
|---|---|
| `CR2-U-windows-x64.zip` | Windows 10+ x64 |
| `CR2-U-macos-universal.zip` | macOS 11+ (Apple silicon and Intel) |
| `CR2-U-x86_64.AppImage` / `CR2-U-linux-x86_64.tar.gz` | Linux x86-64 |
| `CR2-U-android.apk` | Android 7+ |
| `CR2-U-ios-unsigned.ipa` | iPhone and iPad, iOS 15+ (sideload it) |

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
- **iPhone and iPad:** sideload the `.ipa` (AltStore, Sideloadly or similar) and start it; the
  setup asks for your game files. Or copy them into *Chaos Rings II* in the Files app, in an
  `ios/`, `android/` or `vita/` folder.

The README's "What you need" section lists which releases of the game work and what each needs.

## Good to know

- **Android, updating:** v0.7.0 installs over v0.5.0, keeping your game files and saves.
- **No game data is included.** You need your own copy; the port reads it at run time.
- **Tested on:** Linux (played), Windows (under Wine). The macOS, Android and iOS builds are made
  by CI. Reports are welcome.
- **DLSS** isn't in these builds (NVIDIA's library can't ship with GPL code); you can build it
  yourself (README, Building from source).
- Licence GPL-2.0-or-later (the macOS and iOS apps are GPL-3.0 because of MoltenVK). A fan
  preservation project, not affiliated with Square Enix or Media.Vision.
