# CR2-U v0.7.3

A fix release for [v0.7.2](https://github.com/DuIslingr/CR2-U/releases/tag/v0.7.2): Flydigi controllers
on Windows, and the story carrying on after the opening battle. Everything in v0.7.0's, v0.7.1's and
v0.7.2's notes still applies.

## Fixed

- **Flydigi controllers on Windows, take three:** v0.7.2's SDL update found the Vader 4 Pro, but it
  shows up as two "Xbox 360 Controller" devices, and its input comes on the second, which was never
  opened, so the pad looked found but did nothing. Now every controller is opened, and the one you
  press or move takes over. Every controller SDL finds is still written to `cr2view.log`.
- **The story goes on after the opening battle:** losing to Neron in the opening, as the story has
  it, sent v0.7.0 - v0.7.2 back to the title screen instead of on into the story
  ([#5](https://github.com/DuIslingr/CR2-U/issues/5)). A battle's way out was also taken for the one
  of a battle run on its own, which ends at the title. Escapes and the story's other battles could
  hit the same path. They all go back to the story now.

### Thanks to metastigma

[metastigma](https://github.com/metastigma) sent the log that showed the Vader 4 Pro's two devices
(CRO-U #4).

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

- **Android, updating:** v0.7.3 installs over v0.7.2, v0.7.1, v0.7.0 and v0.5.0, keeping your game files and saves.
- **No game data is included.** You need your own copy; the port reads it at run time.
- **Tested on:** Linux (played), Windows (under Wine). The macOS, Android and iOS builds are made
  by CI. Reports are welcome.
- **DLSS** isn't in these builds (NVIDIA's library can't ship with GPL code); you can build it
  yourself (README, Building from source).
- Licence GPL-2.0-or-later (the macOS and iOS apps are GPL-3.0 because of MoltenVK). A fan
  preservation project, not affiliated with Square Enix or Media.Vision.
