# CR2-U v0.3.0

A from-scratch reimplementation of *Chaos Rings II* (iOS / Vita / Android): builds for Windows,
macOS, Linux and Android.

**New in this release: much closer to the original**, from an audit against the game's code and a
full playthrough on video.

Battles
- The monsters' level follows the world select's level (they were always fought at the lowest).
- First-attack criticals, auto regenerate, state 12's charge bonus, a sure escape on the first turn
  of a first strike, the Herald's silence; the battle's voices, cries and sound effects as the game
  queues them; shadows; the encounter's wipe.
- The HUD: the faces' choosing pulse, the announcement window opening once, a cost there's too
  little for in red, the lists' icons (they were invisible), the targets' status signs.

The camp, shops and menus
- Names and help in the option's language; the level cap of 100, 150 or 200 by the story.
- The camp's lists as the game draws them: icons, rank stars, the worn item in yellow, the switch
  arrows, the skill and sopia detail views, the New! badges, the Camp title; what a used item
  raised.
- The shop: "/OZ" prices, its Oz window, asking to equip what was bought; the world select's icons,
  stars and seals; the party swap's help; Reset Data in the title's options; the game's own yes / no
  buttons where it uses them.
- Lists, circle buttons, the pub and the staff roll have the game's dark text border.

The field and events
- Walking by dragging; footsteps and ground effects; NPCs' and boxes' shadows; the party's NPCs
  fidget; objects' own animations; message pauses and self-ending lines.

**Fixed since v0.2.0**
- The staff roll was garbled with the iPad's or the Vita's data; it now reads their own lines.
- The library and the pub let the field walk and take taps under them.
- A pub request's lines began with a box.
- After some battles the scene that follows played when it shouldn't.
- The AppImage now runs on any current Linux (built on Ubuntu 22.04, its glibc checked).

| File | Platform |
|---|---|
| `CR2-U-windows-x64.zip` | Windows 10+ x64 (Vulkan or OpenGL; FSR 3.1) |
| `CR2-U-macos-universal.zip` | macOS 11+, Apple silicon and Intel (Vulkan through MoltenVK, OpenGL; FSR 3.1, MetalFX on macOS 13+) |
| `CR2-U-x86_64.AppImage`, `CR2-U-linux-x86_64.tar.gz` | Linux x86-64 (Vulkan or OpenGL; FSR 3.1) |
| `CR2-U-android.apk` | Android 7+, arm64 / x86-64 (OpenGL ES 3.0) |

**The game's data** - nothing to set: put your copy in the folder holding the program (the folder
holding the `.app` on macOS, the AppImage's on Linux; on Android
`Android/data/local.preservation.chaosrings2/files/` on the shared storage), in a folder named after its release:
`ios/` - the iPad 2.1.0's `ChaosRings2enHD.app` (or the unpacked `.ipa`); `android/` - the 2.1.2 release's `main.212.com.square_enix.chaosrings2gp.obb` and its APK; `vita/` - the installed `PCSG00498` folder. Any one is enough, and each
may hold it as it was unpacked or installed (one or two folders down). With several, the first of `vita/`, `android/`, `ios/` is played. If none is found it
says where it looked; the README's "Your copy" has the details.

**Also in it (from v0.2.0 and before):** your copy found by itself in `ios/`, `android/` or `vita/` beside the program; the game from its title screen; the Options window's new **Graphics** button
(or G / Tab) with display, resolution, VSync, frame rate, renderer, upscaler (FSR 3.1 / DLSS /
MetalFX), MSAA, anisotropic filtering, texture filtering and HD textures, kept in `settings.ini`;
HD textures you make yourself (`cr2view --export-textures` into `textures/export/`, then
`tools/hd_textures.py <the game folder>` into `textures/hd/`, read by itself: the README's "HD textures").

**Not included:** any of the game. You need your own copy; the port reads it at run time. DLSS is
not in these builds either (NVIDIA's NGX library is proprietary and the port is GPL): build it
yourself with the SDK for your own use (README, Platforms).

**First run**
- **Windows:** unzip and run `cr2view.exe`. SmartScreen may warn about an unsigned program: *More info*, *Run anyway*.
- **macOS 11+:** unzip, then right-click `cr2view.app` and choose *Open* (it is not signed or notarized), or run `xattr -dr com.apple.quarantine cr2view.app`.
- **Linux:** `chmod +x CR2-U-x86_64.AppImage` and run it, or unpack the tar.gz.
- **Android 7+:** allow installing from your browser or file manager, install the APK, then copy the data over USB as below.

**Status of this build:** the Linux build is the one played and tested; the Windows build is checked
under Wine (Vulkan and OpenGL, the same frames as Linux's). The macOS and Android builds are built
by CI but have not yet been run on a real Mac or device - MetalFX, especially, is untested. Reports
welcome.

Licence: GPL-2.0-or-later; the macOS `.app` carries MoltenVK and the Vulkan loader (Apache-2.0), so
it is distributed under GPL-3.0 (THIRD_PARTY.md). Personal, non-commercial preservation project, not
affiliated with Square Enix or Media.Vision.
