# CR2-U v0.7.4

The port's own menus now come in nine languages, and a fan translation of the game's text can be added
as a language of its own. Everything in v0.7.0's - v0.7.3's notes still applies.

## New

- **Menu Language:** what CR2-U adds to the game - its settings list and Cheats (each row, value and
  help line), the first start's setup and the Vita save import - is no longer English only. The
  first row of *Display & Graphics* picks the language: English, Japanese, French, German, Spanish,
  Simplified and Traditional Chinese, Korean or Russian. *Auto* (the default) follows the game's own
  Language option; a first start uses your system's language. The translations use the game's own
  terms in each language (the Break and Charge gauges, sopia, awakenings, advents, OZ).
  - You can fix or add a translation without a new build: put a `lang/<code>.po` file (see
    `engine/lang/README.md`) in your game folder or the profile folder, and it is read first.
  - Only the shared strings' Russian has been written by a person (metastigma's); the rest of each
    language is a machine translation, not yet reviewed by native speakers. Corrections are welcome.
- **Fan translations:** a translation of the game's text - the game's files it changed, `message/*.dat`
  with the translated text in English's place and any redrawn art, `images/*.img` - goes in
  `translation/ru/` (named by its language) in the game folder or the profile folder. It is then a
  language of its own after the game's, e.g. "Русский" in Options > Language; English stays, and
  *Menu Language*'s Auto follows it. The `.zip`, `.rar` or `.7z` a translation comes in can be dropped
  into `translation/` as it is: it is unpacked on the next start. A file the translation doesn't
  have stays the game's English. The README and `translation/README.txt` have the details.

## Fixed

- **Russian letters drawn full-width:** the game's fonts draw Cyrillic as wide as a kanji, so Russian
  came out letter-spaced and ran past its row. Cyrillic now comes from a proportional system font
  (Noto Sans, DejaVu Sans, Segoe UI, Arial, Roboto, whichever the system has), and a settings row too
  long for its space is squeezed to fit. The game's own text has no Cyrillic, so nothing of the
  original looks different.
- **Linux, AMD (RADV):** FSR 4 runs about 0.3 ms faster per frame at 4K.

### Thanks to metastigma

[metastigma](https://github.com/metastigma) asked for the menus in every language (CRO-U #10) and
wrote the Russian of the strings CRO-U shares
([CRO-U-metastigma#51](https://github.com/metastigma/CRO-U-metastigma/pull/51)). They also asked for
the fan translation folder and sent the first files of their Russian translation to build it with
(CRO-U #11).

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

- **Android, updating:** v0.7.4 installs over v0.7.3, v0.7.2, v0.7.1, v0.7.0 and v0.5.0, keeping your game files and saves.
- **No game data is included.** You need your own copy; the port reads it at run time.
- **Tested on:** Linux (played), Windows (under Wine). The macOS, Android and iOS builds are made
  by CI. Reports are welcome.
- **DLSS** isn't in these builds (NVIDIA's library can't ship with GPL code); you can build it
  yourself (README, Building from source).
- Licence GPL-2.0-or-later (the macOS and iOS apps are GPL-3.0 because of MoltenVK). A fan
  preservation project, not affiliated with Square Enix or Media.Vision.
