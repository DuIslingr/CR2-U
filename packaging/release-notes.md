# CR2-U v0.4.0

A from-scratch reimplementation of *Chaos Rings II* for Windows, macOS, Linux and Android.

## What's new

- **Easy first start:** with no game found, it asks for your copy (an iOS `.ipa`, an Android
  APK + OBB, or a Vita / Vita3K folder) and copies what it needs next to the program. Vita
  NoNpDrm dumps are decrypted, packed or not (`.rar`, `.7z`, `.zip`, `.vpk`), and the Prequel
  Trilogy works too.
- **Smooth Models** (new): rounder models, characters, stages and props alike (Options).
- **HD Models:** export the models as glTF 2.0 and load your own replacements (off by default).
- **Field Speed and Battle Speed:** 2x to 5x (Options, Display & Graphics).
- **Title screen fixes:** the painting now shows on the iPad and Vita releases, and the menu
  button no longer flashes.
- Settings scroll with the mouse wheel; long help text wraps.

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

- **No game data is included.** You need your own copy; the port reads it at run time.
- **Tested on:** Linux (played), Windows (under Wine). The macOS and Android builds are made by CI
  but haven't been run on real hardware yet. Reports are welcome.
- **DLSS** isn't in these builds (NVIDIA's library can't ship with GPL code); you can build it
  yourself (README, Platforms).
- Licence GPL-2.0-or-later (the macOS app is GPL-3.0 because of MoltenVK). A fan preservation
  project, not affiliated with Square Enix or Media.Vision.
