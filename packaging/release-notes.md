# CR2-U v0.4.2

A from-scratch reimplementation of *Chaos Rings II* for Windows, macOS, Linux and Android.

## What's new

- **Menu scrolling:** swipe or use the mouse wheel in item, Sopia, equipment, manual and graphics lists. Swipes move the selection without activating it.
- **Dialogue audio:** advancing past a completed message stops its voice, and a new story voice replaces the previous one. Revealing text keeps its voice playing.
- **Orientation:** Lock Orientation now applies on Android and remembers its landscape direction on restart. The original Options On/Off buttons use the same preference.
- **Vita audio:** fix ATRAC9 bit operations and compressed-frame lookahead bounds. Added input, dialogue and decoder regression tests.

Checked with Android, iOS and Vita game data. Physical Android touch and rotation testing remains pending; the reported early crystal-room crash has not been confirmed.

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
