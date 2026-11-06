# CR2-U v0.4.5

A from-scratch reimplementation of *Chaos Rings II* for Windows, macOS, Linux and Android.

## What's new

- **Battles:** the results of a won battle no longer stay on screen over the next one. The next
  battle used to lose its HUD while the last results' panels came in over the fight (reported by
  [metastigma](https://github.com/metastigma)).
- **Vita copies:** text is in the language you chose again. The Vita's message files hold 7
  languages, not 8, so much of the text - the tutorial, battle announcements - came out in Japanese
  or Chinese.
- **Windows:** settings, saves and the first-start setup work with a folder or user name in another
  alphabet - a Cyrillic user name on a Russian Windows, for example.
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

- **No game data is included.** You need your own copy; the port reads it at run time.
- **Tested on:** Linux (played), Windows (under Wine). The macOS and Android builds are made by CI
  but haven't been run on real hardware yet. Reports are welcome.
- **DLSS** isn't in these builds (NVIDIA's library can't ship with GPL code); you can build it
  yourself (README, Platforms).
- Licence GPL-2.0-or-later (the macOS app is GPL-3.0 because of MoltenVK). A fan preservation
  project, not affiliated with Square Enix or Media.Vision.
