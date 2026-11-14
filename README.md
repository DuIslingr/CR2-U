# CR2-U - Chaos Rings II, native

CR2-U is a from-scratch native port of *Chaos Rings II* (Square Enix / Media.Vision, 2012) for
Windows, macOS, Linux and Android. It plays the whole game, from the title screen to the staff
roll, using the files from **your own copy**: nothing of the game is included.

It is new code, written by reverse-engineering the Android release (whose library keeps its
function names) and checked against the iPad and Vita releases and reference playthroughs. It
follows [CR1:U](https://github.com/DuIslingr/CR1-U) and [CRO-U](https://github.com/DuIslingr/CRO-U)
but has its own engine, because Chaos Rings II runs on Media.Vision's MVGL library with compiled
Squirrel scripts.

**[Download the latest release](https://github.com/DuIslingr/CR2-U/releases/latest)**

## What you need

A copy of the game from one of these releases - any one of them is the whole game:

| Release | What to give the port |
|---|---|
| **Android** 2.1.2 | the APK and its `main.212.com.square_enix.chaosrings2gp.obb` - or the `.xapk` (it holds both), or an `.apks` with the OBB |
| **iPad** 2.1.0 | the `.ipa` (or its unpacked `.app`). iPhone copies aren't supported yet. |
| **PlayStation Vita** (PCSG00498) | the installed game (Vita3K's folder, or the `PCSG00498` folder), or a NoNpDrm dump - a folder or a `.zip` / `.rar` / `.7z` / `.vpk`. The Prequel Trilogy (PCSG00457) works too. |

A PC needs Vulkan 1.3 or OpenGL 3.3; a phone, Android 7 or later.

## Install and first start

1. Download the file for your system from the [releases page](https://github.com/DuIslingr/CR2-U/releases)
   and unzip it.
2. Start `cr2view`.
3. If it doesn't find a copy of the game, it asks for one: pick the file or folder, and the port
   copies what it needs into its game folder (or your profile folder if that can't be written).
   You can delete or move the original afterwards.

A Vita dump is decrypted during setup, keeping only Chaos Rings II's files.

| System | Notes |
|---|---|
| Windows 10+ | If SmartScreen warns about an unsigned program, choose *More info*, *Run anyway*. |
| macOS 11+ | Right-click `cr2view.app` and choose *Open* (it isn't signed). |
| Linux | `chmod +x` the AppImage and run it, or use the `.tar.gz`. |
| Android 7+ | Install the APK. On first start it offers the same setup: pick the APK and OBB (or the `.xapk`, or the `.apks` and OBB), the `.ipa` or a Vita archive. Folders can't be picked on Android; copy them over USB instead (below). |

### Putting the files in by hand

You can also copy your game into the **game folder** yourself. The port finds it on the next
start. The game folder is the folder holding `cr2view` (or `cr2view.exe`, `cr2view.app`, the
AppImage). On Android it is `Android/data/local.preservation.chaosrings2/files/`, reached over USB.

```
<game folder>/
├── android/   the 2.1.2 APK and main.212.com.square_enix.chaosrings2gp.obb (or the .xapk, or the .apks and OBB)
├── ios/       the iPad 2.1.0 .ipa, or its app (ChaosRings2enHD.app, or Payload/ChaosRings2enHD.app as unzipped)
├── vita/      PCSG00498/ as installed (eboot.bin, sce_sys/, CR2/) - or a NoNpDrm dump as dumped
├── fsr4/      optional: AMD's FSR 4 files (see Settings)
├── textures/  optional: export/ and hd/ for HD textures
└── models/    optional: export/ and hd/ for HD models
```

- Any one release folder is enough. With several, the port uses the first of `vita/`, `android/`,
  `ios/`; `--data <folder, archive or OBB>` (or `CR2_DATA`) picks another, or a copy elsewhere.
- A release can sit one or two folders down, as it was unpacked; folder names below `ios/`,
  `android/` and `vita/` don't matter.
- A still-encrypted Vita dump in `vita/` (its `app/`, and `addcont/` for the Prequel Trilogy) is
  decrypted on the next start, on a phone too, once there is room for the decrypted copy. Delete
  the dump afterwards.
- An `.xapk`, an `.apks` or a `.zip` of the APK and OBB in `android/` is unpacked once on the next
  start into `android/unpacked/` (about **1 GB** of free space for the OBB; that start takes a while,
  with a progress window). Later starts reuse it; the bundle itself can be deleted afterwards.
- An `.ipa` in `ios/` is unpacked once on the next start into its `ChaosRings2enHD.app` there (with a
  progress window); the `.ipa` can be deleted afterwards.
- The release downloads already contain these folders, each with a `README.txt` saying what goes
  in it.
- Older layouts still work: `ipad/`, `PCSG00498/`, `CR2/`, `ChaosRings2enHD.app/`, `Payload/` or
  `data/` straight in the game folder.

### Saves and settings

Saves and `settings.ini` go to your profile folder, not the game folder:

| System | Profile folder |
|---|---|
| Linux | `~/.local/share/DuIslingr/CR2-U/` |
| Windows | `%APPDATA%\DuIslingr\CR2-U\` |
| macOS | `~/Library/Application Support/DuIslingr/CR2-U/` |

It holds CR2-U's save (`save1.txt`), the continue data (`cont.txt`) and, with Vulkan,
`vulkan-pipelines.bin` (a shader cache that makes later starts quicker; safe to delete).

**Vita saves:** on the title screen, open Options, then the Graphics list, and choose **Import
Vita Saves** (or press I in the options). Pick the folder of your decrypted saves. Existing saves
are backed up first. Details: [docs/VITA_SAVES.md](docs/VITA_SAVES.md). The game's own save
format can also be read and written: `--game-save game.dat`, `cr2tool save-import` / `save-export`.

## Controls

The original iPad and Android releases are touch only, so the mouse or a finger works everywhere:
buttons, lists, volume bars, balloons and the touch actions' drags and taps. On the field, drag to
walk (drag further to run) and tap a balloon or mark to act.

### Controller

A controller plays as the Vita does. Buttons are by position: on an Xbox pad ○ is B, × is A, △ is Y
and □ is X. ○ confirms, as on the Vita. To confirm with the bottom button instead, turn on
**Swap ○ and ×** in Options, Graphics.

| Button | Field | Menus, battles, messages |
|---|---|---|
| Left stick / D-pad | walk (push the stick fully to run) | move the cursor (holding repeats) |
| ○ | talk, use the touch action in reach | confirm, turn the page |
| × | - | back, cancel; No in a question |
| □ (hold) | - | fast-forward messages and the staff roll |
| △ | open / close the field tab (Camp, Map, Save) - the port's addition, as the Vita uses touch for it | - |
| L / R | - | switch the list: item category, equipment's character, shop's character. A list without a switch pages instead. |
| START | - | "Please Touch" on the title |

How the cursors move, as on the Vita:

- **Battle:** Up / Down step through the commands and through the targets.
- **Camp:** Up / Down step through Items, Equipment, Sopia and Options, skipping greyed ones.
- **Shop equipment:** L / R change the character, Left / Right change the kind (weapons, armour,
  accessories).
- **Yes / No questions:** Yes is selected first; Up / Down move to the other, ○ answers, × is No.
  Quantities use Left / Right.
- **Pub board:** the d-pad moves over the posters and Check.

These cursors are highlighted once you've moved them with a key or button (touch shows none).
Full details, with the original's code references: [docs/CONTROLS.md](docs/CONTROLS.md).

### Keyboard

| Key | Action |
|---|---|
| Arrow keys / W A S D | run on the field (hold Shift to walk); move the cursor in menus |
| Return | talk, turn a page, confirm |
| Backspace (Android: Back) | back, cancel |
| Page Up / Page Down | as L / R |
| Y / N | answer a question |
| Tab | the field tab (Camp, Map, Save); Graphics in the options |
| C | Camp |
| G | Graphics, in the options |
| F (hold) | fast-forward messages, skip a cutscene |
| 1 / 2 | the party's two slots (organizing the party in town) |
| Space | pause |
| F1 / F2 | cycle output resolution / upscaler mode |
| Esc | quit |

## Settings

The title's **Options** has the game's own settings. Its **Graphics** button (or G / Tab) opens
CR2-U's list, saved in `settings.ini`. Most also have a command-line flag for a single run.

| Setting | What it does |
|---|---|
| Display, Resolution, VSync, Frame Rate | window / fullscreen / borderless, output size (up to 3840 x 2160), frame cap. The game runs at 20 updates a second; the port draws 60 or more frames in between. |
| Renderer | Vulkan (default) or OpenGL; takes effect on the next start |
| Upscaler, Upscale Mode, Sharpness | FSR 3.1, FSR 4, DLSS or MetalFX. The scene renders smaller and is upscaled; menus stay sharp. |
| Anti-aliasing, Anisotropic / Texture Filtering | MSAA, sharper textures at an angle, sharp or smooth texels |
| HD Textures, HD Models | your own replacement textures and models (below) |
| Smooth Models | rounds out every 3D model (4x or 9x triangles), keeping hard edges |
| Field Speed, Battle Speed | Off or 2x to 5x |
| Swap ○ and × | which controller button confirms |
| Lock Orientation | keep the current landscape direction (phones) |

**FSR 4** (AMD RX 6000 or later, on Linux and Windows) needs AMD's own files, which can't be
included. Put these in `fsr4/` in the game folder (its `README.txt` has the links):

- `amd_fidelityfx_loader_vk.dll` and `amd_fidelityfx_upscaler_vk.dll` from AMD's
  [FSR SDK 2.4](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases/tag/v2.4.0)
  (`Kits/FidelityFX/signedbin/`)
- `amdvkffx64.dll` from the AMD Software: Adrenalin Edition 26.10.1 (or later) driver package
- on Linux also `libffx_vk_linux.so` from [ffx-vk-linux](https://github.com/DuIslingr/ffx-vk-linux),
  which runs AMD's DLLs natively, without Wine

Without them the option shows "(n/a)" with the reason, and FSR 3.1 is used. `CR2_FSR4_DIR` points
elsewhere.

**DLSS** isn't in the release builds: NVIDIA's library can't ship with GPL code. You can build it
yourself (see [Building from source](#building-from-source)).

### HD textures

Upscale the game's textures yourself, from your own copy:

1. `cr2view --export-textures` writes every picture the game draws to `textures/export/` as
   ordinary upright PNGs.
2. Upscale them into `textures/hd/` with the same file names. `python3 tools/hd_textures.py <game
   folder>` does it with waifu2x and Real-ESRGAN (needs Python 3, NumPy and Pillow; `--help` for
   options), or use any upscaler or image editor.
3. Start the game. With `textures/hd/` present, HD Textures switches on by itself.

Keep each picture's proportions and orientation (one scale both ways, at least the original size,
at most 8192 a side). A replacement that doesn't fit is skipped and the log says why. One
`textures/hd/` can hold replacements for every release. `--no-hd-textures` runs without them.

### HD models

1. `cr2view --export-models` writes every model to `models/export/` as glTF (`.glb`) with its
   skeleton, textures and animations. Blender imports them directly.
2. Edit it, keeping the armature (bones named `node_<n>...`) and material names (`cr_mat_<n>...`).
   New textures can be packed into the `.glb`.
3. Export as glTF to `models/hd/<model>.glb` (for example `c001.glb` for Darwin) and turn on HD
   Models in Options, Graphics. It's used from the next scene.

The game plays its own animations; the ones in the file are for preview. `cr2tool gltfcheck`
checks a round trip.

Exported textures and models are made from the game's art: keep them, and anything made from them,
for personal use.

## Troubleshooting

- **"No copy found":** the port lists every place it looked. Check the folder names above, or give
  the copy with `--data`.
- **Vulkan won't start:** the port falls back to OpenGL by itself; `--renderer opengl` forces it.
- **Menus or pictures look wrong with HD textures:** try with HD Textures off (Options, Graphics,
  or `--no-hd-textures`). The log names any replacement it skipped.
- **Android, updating from an older version:** releases before v0.5.0 were each signed with a
  different key. Uninstall the old version once first. This deletes the game files and saves you
  copied in, so keep a copy of them. From v0.5.0 on, each release installs over the last.
- **Tested on:** Linux (played) and Windows (under Wine). The macOS and Android builds come from CI
  and haven't been tried on real hardware yet. Reports are welcome in the
  [issues](https://github.com/DuIslingr/CR2-U/issues): please say which copy of the game you use
  (Android, iPad or Vita), your system, and what you did. On a PC, attach `cr2view.log` from the
  profile folder (`cr2view.prev.log` is the start before it).

## Building from source

```sh
cd engine
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
build/cr2view
```

Needs a C++17 compiler, CMake and Ninja. SDL3, FreeType and zlib are used from the system or
downloaded and built by CMake. Vulkan needs the Vulkan loader and `glslc`; without them the build
is OpenGL only. `ctest --test-dir build` runs the tests (none need the game's files).

| Platform | How | Renderers and upscalers |
|---|---|---|
| Linux | as above | Vulkan, OpenGL 3.3; FSR 3.1, FSR 4, DLSS (with the SDK) |
| Windows 10+ x64 | MSVC and Ninja from a Developer prompt, with the [Vulkan SDK](https://vulkan.lunarg.com/); or from Linux with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw): `-DCMAKE_TOOLCHAIN_FILE=engine/cmake/llvm-mingw-x86_64.cmake` | Vulkan, OpenGL 3.3; FSR 3.1, FSR 4, DLSS (MSVC, with the SDK) |
| macOS 11+ | Xcode command-line tools, Ninja and the [Vulkan SDK](https://vulkan.lunarg.com/) (MoltenVK is copied into the `.app`); `"-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"` for a universal build | Vulkan (MoltenVK), OpenGL 3.3; FSR 3.1, MetalFX (macOS 13+) |
| Android 7+ (arm64, x86-64) | Android SDK and NDK: `git clone -b release-3.2.0 https://github.com/libsdl-org/SDL android/SDL`, then `gradle assembleRelease` in `android/` | OpenGL ES 3.0 |

- `-DCR2_GLES=ON` builds the desktop version on OpenGL ES 3.0, as Android runs it.
- **DLSS:** add `-DCR_DLSS_SDK=<NVIDIA's DLSS SDK checkout>` (or put it in
  `engine/third_party/dlss/`) and copy its runtime next to the game. NGX is proprietary, so a DLSS
  build is for your own use only (see `THIRD_PARTY.md`).
- Missing glyphs fall back to system fonts (Noto CJK on Linux, Malgun Gothic / Microsoft YaHei on
  Windows, Apple SD Gothic Neo / PingFang on macOS).
- Every push is built for all four platforms by `.github/workflows/build.yml`; a `v*` tag publishes
  a release.

Build options, `cr2view`'s debug modes and `cr2tool`'s checks: [docs/ENGINE.md](docs/ENGINE.md).

## Documentation

| | |
|---|---|
| [docs/STATUS.md](docs/STATUS.md) | each part of the port, what it follows and what checks it |
| [docs/ENGINE.md](docs/ENGINE.md) | building in detail, `cr2view`'s modes, `cr2tool`'s checks, the tools |
| [docs/CONTROLS.md](docs/CONTROLS.md) | the Vita's buttons as its code reads them, and how a controller maps onto them |
| [docs/OPENING_AUDIT.md](docs/OPENING_AUDIT.md) | the opening, field and battle timing checked call by call against the original |
| [docs/FORMATS.md](docs/FORMATS.md) | the game's file formats |
| [docs/SCRIPTS.md](docs/SCRIPTS.md) | the Squirrel scripts, the native API they call, the game's saves |
| [docs/BATTLE.md](docs/BATTLE.md) | the battle rules and staging |
| [docs/VITA_SAVES.md](docs/VITA_SAVES.md) | importing Vita saves |

| Path | Contents |
|---|---|
| `engine/` | the C++17 / SDL3 engine (`cr2core`), `cr2view` and `cr2tool` |
| `tools/` | analysis and extraction tools; they work on your own copy |
| `android/`, `packaging/` | the Android project; icons, desktop entry, release notes |
| `re/`, `extracted/` | not in the repository: local reverse-engineering output and decoded assets - never commit or share |

## Licence and credits

- CR2-U is GPL-2.0-or-later (`LICENSE`). The macOS app is GPL-3.0, because of the bundled
  MoltenVK. Bundled libraries keep their own licences: see `THIRD_PARTY.md`.
- **No game data is in this repository or the downloads** - no art, audio, text, scripts or code
  of the game, nor anything generated from them. The source does contain facts read from the
  original (file names, flag numbers, screen positions, formula constants, tables the
  `tools/gen_*.py` scripts extract) where the engine needs them.
- *Chaos Rings II* is by Square Enix and Media.Vision. CR2-U is a fan preservation project, not
  affiliated with or endorsed by them.
