# CR2-U v0.4.3

A from-scratch reimplementation of *Chaos Rings II* for Windows, macOS, Linux and Android.

## What's new

- **Invisible character:** a game started from the title showed only a shadow where the player should be. The character now appears.
- **Dialogue:** pages and yes / no questions wait for you instead of moving on after a few seconds, battle tutorials take your tap first, and timed lines stay up until the next message.
- **Saves:** written safely and loaded all or nothing. A save cut short is refused, the title stays open with a notice, and damaged numbers are held in range.
- **Controller:** a button held while the window loses focus, or as a session ends, does nothing until it's let go.
- **Windows:** a PC without Vulkan no longer refuses to start the game; it goes on to OpenGL.

The autoplayer played from the title into the late game, with one puzzle skipped and later bosses taken as won; the ending wasn't reached in this pass. A real Windows PC without Vulkan and physical Android devices are still untested.

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
