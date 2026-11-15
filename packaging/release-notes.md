# CR2-U v0.7.2

A fix release for [v0.7.1](https://github.com/DuIslingr/CR2-U/releases/tag/v0.7.1): Flydigi controllers
on Windows, the cheats in the battles the story has you lose, and the title screen on wide screens.
Everything in v0.7.0's and v0.7.1's notes still applies.

## Fixed

- **Flydigi controllers on Windows, take two:** v0.7.1's fix didn't bring the Vader 4 Pro back. The
  Windows build now uses SDL's development version from 2026-11-14, which has SDL's later Flydigi
  fixes - the setup metastigma's fork uses and its user confirmed works with this controller, wired
  and through its receiver. v0.7.1's workaround is gone. Other platforms keep SDL 3.4.20. Every
  controller SDL finds is still written to `cr2view.log`.
- **Cheats in the battles the story has you lose:** in the six battles whose story carries on after
  the party falls (Neron's twice, Araki, the Herald's twice, Amon), Infinite HP no longer keeps you
  standing forever: it holds while a foe can still fall, and lapses once none can (Neron's HP comes
  all the way back every turn, Amon blocks every blow), so the defeat and the story come. One-Hit
  Kills still works there - Araki and the Herald can be beaten - except in the opening's battle with
  Neron, which can't be won in the original either, so the story starts as it should.
- **The title screen fills wide screens:** on a phone or a 16:9 monitor the title's painting stayed
  in the middle between black bars. It now stretches across the screen as the Android original's
  does, with every release of the game (iPad, Android, Vita).

### Thanks to metastigma

[metastigma](https://github.com/metastigma) reported the Flydigi problem and worked out the Windows
SDL setup in their fork ([CRO-U-metastigma#42](https://github.com/metastigma/CRO-U-metastigma/pull/42)).

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

- **Android, updating:** v0.7.2 installs over v0.7.1, v0.7.0 and v0.5.0, keeping your game files and saves.
- **No game data is included.** You need your own copy; the port reads it at run time.
- **Tested on:** Linux (played), Windows (under Wine). The macOS, Android and iOS builds are made
  by CI. Reports are welcome.
- **DLSS** isn't in these builds (NVIDIA's library can't ship with GPL code); you can build it
  yourself (README, Building from source).
- Licence GPL-2.0-or-later (the macOS and iOS apps are GPL-3.0 because of MoltenVK). A fan
  preservation project, not affiliated with Square Enix or Media.Vision.
