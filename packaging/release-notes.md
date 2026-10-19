# CR2-U v0.1.1

A from-scratch reimplementation of *Chaos Rings II* (iOS / Vita / Android): builds for Windows,
macOS, Linux and Android. A hotfix over v0.1.0.

**Fixed in this release**
- Opened with no arguments, v0.1.0 stopped on the title's logo; it now goes on to the title screen
  and the game.
- Random encounters were missing from the field: they now happen as in the original - each map's
  area, steps and chance read from your copy's executable, the monsters drawn by the area's weights,
  fought on the area's map and music.
- The field menu's encounters switch is now kept in the save (a save made while a scene had them
  off no longer leaves them off).
- A controller plays as the Vita does (○ / × / △ / □, L / R, START, the D-pad and stick; Options,
  Game, "Swap ○ and ×").

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

**Also in it (from v0.1.0):** the game from its title screen; the Options window's new **Graphics** button
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
