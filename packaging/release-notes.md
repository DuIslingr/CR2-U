# CR2-U v0.7.1

A fix release for [v0.7.0](https://github.com/DuIslingr/CR2-U/releases/tag/v0.7.0): a controller
Windows stopped finding, text on more Android phones, and clearer setup messages. Everything in
v0.7.0's notes still applies.

## Fixed

- **Flydigi controllers on Windows:** a Vader 4 Pro, wired or through its receiver, wasn't found since
  v0.7.0's SDL update. SDL 3.4 added its own Flydigi driver, which takes the controller and then gets
  no input from it; CR2-U now leaves that driver off on Windows, so the controller is read through
  XInput as before. Setting the environment variable `SDL_JOYSTICK_HIDAPI_FLYDIGI=1` turns SDL's
  driver back on. Every controller SDL finds is now written to `cr2view.log`, so if one still isn't
  picked up, the log shows why.
- **Text on Android phones with their maker's own fonts:** when neither the game's font nor
  Android's usual ones are there (an iPad or Vita copy on such a phone), CR2-U now reads the phone's
  own font list and uses its default font, then the fonts it names for Japanese, Chinese and Korean.
  v0.7.0 only looked through the font folder.
- **Setup messages:** when a picked Vita archive couldn't be unpacked because a folder couldn't be
  made or a file written, the message gave no reason. It now names the folder or file and why.

### Thanks to metastigma

[metastigma](https://github.com/metastigma) reported the Flydigi problem and investigated it in their
fork ([CRO-U-metastigma#42](https://github.com/metastigma/CRO-U-metastigma/pull/42),
[#43](https://github.com/metastigma/CRO-U-metastigma/pull/43)); this release takes the narrower fix
above instead of their development build of SDL.

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

- **Android, updating:** v0.7.1 installs over v0.7.0 and v0.5.0, keeping your game files and saves.
- **No game data is included.** You need your own copy; the port reads it at run time.
- **Tested on:** Linux (played), Windows (under Wine). The macOS, Android and iOS builds are made
  by CI. Reports are welcome.
- **DLSS** isn't in these builds (NVIDIA's library can't ship with GPL code); you can build it
  yourself (README, Building from source).
- Licence GPL-2.0-or-later (the macOS and iOS apps are GPL-3.0 because of MoltenVK). A fan
  preservation project, not affiliated with Square Enix or Media.Vision.
