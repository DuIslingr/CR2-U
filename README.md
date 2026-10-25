# CR2-U — a Chaos Rings II reimplementation

A from-scratch native port of *Chaos Rings II* (Square Enix / Media.Vision, 2012), built by
reverse-engineering the Android release (2.1.2, whose library keeps its names), with the iPad
release (2.1.0) and the PlayStation Vita release (PCSG00498) - any one of the three runs as the
game. It follows **[CR1:U](https://github.com/DuIslingr/CR1-U)** and
**[CRO-U](https://github.com/DuIslingr/CRO-U)**, but not on their engine: Chaos Rings II is built on
Media.Vision's own library, MVGL, with its logic split between native classes and compiled Squirrel
scripts, so the engine here is its own; CR1:U's platform layers (SDL3, the renderers, audio
decoding, upscaling, HD textures) carried over. It loads the game from **your own copy** at
runtime.

**No game data is in this repository or distributed with it** - no art, audio, text, scripts or
code of the game, and nothing generated from them. You need your own copy of the game. The
engine's source does hold facts read out of the originals (file names, flag numbers, screen
positions, formula constants, the tables `tools/gen_*.py` read out of the library) where it needs
them.

Licence: GPL-2.0-or-later (`LICENSE`); the bundled libraries keep their own (`THIRD_PARTY.md`).
Not affiliated with or endorsed by Square Enix or Media.Vision.

## Quick start

```sh
cd engine
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
build/cr2view
```

C++17, SDL3 (downloaded and built by CMake when it isn't installed) and FreeType; Vulkan 1.3 by
default, OpenGL 3.3 / ES 3.0 with `--renderer opengl` or when Vulkan can't start. See
[Platforms](#platforms) for Windows, macOS and Android, and `docs/ENGINE.md` for the build's options
and the tools.

Opened with no arguments at all (a double-click, Android's launcher) it starts the game at its
title screen, your copy found in the game folder - for this build `engine/build/`, or the folder
you start it from - as `vita/`, `android/` or `ios/` ([Your copy](#your-copy)). Any of the three
releases works as the game: the Vita's PCSG00498 (the maps' table from its `eboot.bin`, the text in
its font `CR2/resources/font/TBGoPro-Bold.otf`), the Android 2.1.2 (the maps' table from the APK's
`lib/armeabi-v7a/libChaosRings2.so`, its font `assets/00001`, read from inside the APK) or the iPad
2.1.0 (the maps' table from its executable).

Saves and settings go to the profile folder (`~/.local/share/DuIslingr/CR2-U/` on Linux): CR2-U's
own save (`save1.txt`, the continue data `cont.txt`) and `settings.ini`. The game's
own saves are read and written too (`--game-save game.dat`, `cr2tool save-import` / `save-export`).

## Your copy

The easiest way: just start the program. With no copy found, it asks where yours is (one copy is the game: the first given is used) - pick
any of them in the window it opens, and it takes the files from there:

- **iOS** - the `.ipa` (or any file in its unpacked `.app`): unpacked into `ios/` in the game folder
  (the profile folder's if that one can't be written), found there from then on;
- **Android** - the APK and its `main.*.obb` (in one folder): read where they are;
- **Vita** - Vita3K's folder (its own, the folder its `config.yml` names as `pref-path`, or `ux0`)
  or the game's own `PCSG00498` folder: read where it is.

The Android and Vita copies are remembered in `settings.ini` (in the profile folder). (Windows,
Linux and macOS; on Android, copy the files over USB as below.)

Or, as before, nothing given on the command line: put your copy in the **game folder** and start
the program - it finds it by itself. The game folder is:

| Platform | The game folder |
|---|---|
| Windows, Linux | the folder holding `cr2view.exe` / `cr2view` |
| Linux AppImage | the folder holding the `.AppImage` |
| macOS | the folder holding `cr2view.app` (its `Contents/Resources/` works too) |
| Android | `Android/data/local.preservation.chaosrings2/files/` on the shared storage (copy your files there over USB) |

(then the working directory too). In it, a folder for each release you have - any one is enough:

```
<game folder>/
├── cr2view                          (or cr2view.exe, cr2view.app, the .AppImage)
├── ios/
│   └── ChaosRings2enHD.app/            the iPad 2.1.0's .app: CRDB.ios.mvgl, Sound.ios/, its executable (the maps' table)
│                                       - or Payload/ChaosRings2enHD.app/ (the .ipa unpacked as it is)
├── android/
│   ├── main.212.com.square_enix.chaosrings2gp.obb
│   └── <the 2.1.2 APK>.apk             (the maps' table and the font are read from inside it)
├── vita/
│   └── PCSG00498/                      the installed title: CR2/resources/CRDB.psp2.mvgl, Sound.psp2/, eboot.bin
└── textures/
    ├── export/                         written by --export-textures: the pictures to upscale (<hash>.crraw)
    └── hd/                             read by the game: the upscaled pictures (<hash>.crhd)
```

A release's folder may hold the release itself or have it one or two folders down, as above
(`ios/Payload/....app`, `vita/PCSG00xxx/`), so a copy can be dropped in as it was unpacked or
installed; the names of the folders below `ios/`, `android/` and `vita/` don't matter. A Vita copy
has to be the installed, decrypted title (an encrypted NoNpDrm dump: install it first, e.g. with
Vita3K). The release downloads come with these folders made, each with a `README.txt` saying what
goes in it.

CR2-U plays one release at a time: with several, **the first of `vita/`, `android/`, `ios/`** (the
same order as CR1:U and CRO-U take one alone); `--data` picks another.

If nothing is found the program says where it looked. The older places still work: `ipad/`, `PCSG00498/`, `CR2/`, `ChaosRings2enHD.app/`, `Payload/` or `data/` straight in the game folder.
To use a copy elsewhere: `--data <a release: its folder, archive, OBB>` (or `CR2_DATA`). Settings and saves go to the profile folder
(`~/.local/share/DuIslingr/CR2-U/` on Linux; the same folders under `%APPDATA%\` on Windows and
`~/Library/Application Support/` on macOS), not the game folder. All four ports (CR1:U, CRO-U, CR2-U, CR3-U) look for their copies the same way.

## Field and battle speed

Options, Display & Graphics: **Field Speed** and **Battle Speed**, each Off or 2x .. 5x - walking the
field (its scenes, talk and menus as they are) and the battles that many times as fast. Kept in
settings.ini (`field_speed`, `battle_speed`); `--field-speed n` / `--battle-speed n` for one run.

## HD textures

Upscaled textures you make yourself, from your own copy - waifu2x or Real-ESRGAN, drawn in place of
the originals:

1. `cr2view --export-textures` writes every picture the game draws to `<game folder>/textures/export/`
   (`<hash>.crraw`; the profile folder's `textures/export/` if the game folder can't be written;
   `--export-textures <folder>` elsewhere). Each release exports its own pictures.
2. `python3 tools/hd_textures.py <game folder>` upscales them into `<game folder>/textures/hd/`
   (`<hash>.crhd`): waifu2x (`waifu2x-ncnn-vulkan` on the PATH, or `--waifu2x <it>`) for the
   lossless and block-compressed pictures, Real-ESRGAN (`--esrgan <the folder with
   realesrgan-ncnn-vulkan and models/>`) for the 16-bit ones - or `--engine waifu2x` for all. It
   needs Python 3 with NumPy and Pillow; `--help` lists the rest (`--scale`, `--noise`, `--only` ...).
3. Start the game: with `textures/hd/` there, HD Textures is on by itself (Options, Graphics, HD Textures switches
   it; `--no-hd-textures` for a run without). One `textures/hd/` can hold every release's.

`CR2_HD_TEXTURES=<folder>` reads them from elsewhere; the older `hd-textures/` beside the program or in
the profile folder is still read while there is no `textures/hd/`. The `tools/hd_textures.py` is the
same in all four ports. Personal use: the outputs are made from the game's art - keep them out of
anything shared.

## HD models

Models you make or improve yourself, drawn in place of the game's - more detailed, smoother,
retextured - moving with the game's own skeleton and animations:

1. `cr2view --export-models` writes every figure to `<game folder>/models/export/` as glTF 2.0
   (`<name>.glb`: its skeleton, meshes, materials, pictures and animations; `--export-models <folder>`
   elsewhere; `--no-animations` leaves the animations out for small files). Blender (File, Import,
   glTF 2.0) opens them as they are, animations included.
2. Edit it - sculpt, subdivide, rebuild, re-weight, paint - keeping the armature: its bones are named
   `node_<n>...` and the game finds them by that (a new bone goes with its nearest original parent).
   A material keeps the game's render state by its name `cr_mat_<n>...`; give it a picture of your own
   (PNG or JPEG, any size, packed in the `.glb`) and that picture replaces the figure's.
3. Export it from Blender as glTF 2.0 (`.glb`, the defaults) to `models/hd/<figure>.glb` (`c001.glb` for Darwin's `c001`). With HD Models on
   (Options, Display & Graphics; off until you switch it on) it is drawn instead of the original from the next scene on.

What it reads: triangles, the first UV set and the second and colour set, four weights a vertex; meshes on more than 85 joints are split; normals, and tangents for normal-mapped materials, are worked out when missing; the skin weights come back summing to 1 (the game's own are 255ths). The
animations in the file are for previewing - the game plays its own. `cr2tool gltfcheck <archive> <figure|all> [file.glb]`
checks a round trip (the export loaded back: every figure lands exactly on the original in every
animation). `CR2_HD_MODELS=<folder>` reads them from elsewhere. Personal use: the exports are the
game's models - keep them, and what you make from them, out of anything shared.

## Smooth models

Options, Display & Graphics, **Smooth Models**: Off, Smooth (4x triangles) or Smoother (9x) - every
model in the world (characters, monsters, stages and props; not the menus) rounded out at load time
with PN triangles, as CR1-U's: the original vertices stay put, edges sharper than 60 degrees stay
hard, and skinning and animation work as before. From the next scene on; kept in settings.ini
(`smooth_models` 0, 2 or 3), `--smooth-models n` for one run.

## Platforms

Every push is built for Linux, Windows, macOS and Android by `.github/workflows/build.yml`; the
binaries are that run's artifacts (nothing of the game is in them): an AppImage, a Windows folder,
a universal macOS `.app` and an APK. The Windows build is checked under Wine (Vulkan and OpenGL, the
same frames as Linux's); macOS and Android are built in CI but not yet played on a real machine -
MetalFX, in particular, is compiled but untested.

| Platform | Build | Renderer | The game folder (its `vita/`, `android/`, `ios/`, `textures/`) |
|---|---|---|---|
| Linux | `cmake -S engine -B build -G Ninja && ninja -C build` | Vulkan, OpenGL 3.3; FSR 3.1, DLSS (with the SDK) | the folder holding `cr2view`, or the AppImage |
| Windows 10+ (x64) | MSVC and Ninja from a Developer prompt, the [Vulkan SDK](https://vulkan.lunarg.com/) for Vulkan - or from Linux with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw): `-DCMAKE_TOOLCHAIN_FILE=engine/cmake/llvm-mingw-x86_64.cmake` (runs under Wine). SDL3, zlib and FreeType are built by CMake | Vulkan, OpenGL 3.3; FSR 3.1, DLSS (MSVC, with the SDK) | the folder holding `cr2view.exe` |
| macOS 11+ (Apple silicon and Intel) | Xcode's command-line tools, Ninja and the [Vulkan SDK](https://vulkan.lunarg.com/) (MoltenVK, copied into the `.app`); add `"-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"` for both | Vulkan (MoltenVK), OpenGL 3.3; FSR 3.1, MetalFX (macOS 13+) | the folder holding `cr2view.app` (or its `Contents/Resources/`) |
| Android 7+ (arm64, x86-64) | the Android SDK and NDK: `git clone -b release-3.2.0 https://github.com/libsdl-org/SDL android/SDL`, then `gradle assembleRelease` in `android/` | OpenGL ES 3.0 | `Android/data/local.preservation.chaosrings2/files/` on the shared storage (copy your files there over USB) |

The text falls back on the system's fonts where the release's own is missing: Noto CJK on Linux,
Malgun Gothic and Microsoft YaHei on Windows, Apple SD Gothic Neo and PingFang on macOS.
`-DCR2_GLES=ON` builds the desktop viewer on OpenGL ES 3.0, as Android runs it (Mesa has it): a
way to test that path without a device. Saves go to SDL's profile folder (`DuIslingr/CR2-U/`).

DLSS: the NGX headers and library from NVIDIA's [DLSS SDK](https://github.com/NVIDIA/DLSS)
(`-DCR_DLSS_SDK=<its checkout>`, or `engine/third_party/dlss/`), its runtime copied next to the
game. CI builds and links it on Linux and Windows to prove it, but never uploads those builds: NGX
is proprietary and the port is GPL, so a DLSS build is for your own use (THIRD_PARTY.md).

## Controls

A controller plays as the Vita does (its own code: `docs/CONTROLS.md`). The face buttons are by
position, so on an Xbox pad ○ is B, × is A, △ is Y and □ is X; ○ confirms, as on the Vita - the
options' Graphics list has "Swap ○ and ×". Everything the original takes by touch also takes a click
or a tap: the buttons, the lists, the volume bars, the balloons, the touch actions' drags and taps.

| Button | On the field | In menus and messages |
|---|---|---|
| Left stick / D-pad | walk (the stick pushed far runs) | move the cursor (held, it repeats) |
| ○ | talk, the touch action in reach | confirm, turn the page |
| × | - | back, cancel |
| □ (held) | - | fast-forward the messages |
| △ | open / close the tab (Camp, Map, Save) - the port's own, the Vita's is a touch | - |
| L / R | - | a list's page; the shop's character |
| START | - | the title's "Please Touch" |

| Keyboard | |
|---|---|
| Arrow keys / W A S D | run on the field (Shift walks); in menus, the cursor - also over the title's, the shop's and the party's buttons |
| Return | talk to what has a balloon, turn a message's page, pick |
| Backspace (Android: Back) | back, cancel |
| Y / N | answer a question |
| Tab | the field's tab (Camp, Map, Save); in the options, Graphics |
| C | the Camp |
| G | in the options: Graphics |
| F (held) | the messages' fast-forward, a vista's skip (as holding their button) |
| 1 / 2 | the party's two slots (the town's organizing) |
| Space | pause |
| F1 / F2 | the next output resolution / upscaler mode |
| Esc | quit |

## Status

The game plays from its title screen to the staff roll, in all three releases (released as
[v0.1.0](https://github.com/DuIslingr/CR2-U/releases/tag/v0.1.0)). Checked against the Android
release's named code and a reference playthrough (`docs/STATUS.md` has each part and what checks it):

- **Data**: the MDB1 archives (the Android OBB is one too), every texture, mesh, skeleton, animation
  and message of the three releases; the game's own shaders, as one shader with the same formulas
- **Fields**: the maps' tables (read from your copy's executable), the painted layers and 3D figures,
  Bullet collision, exits, the gimmicks and NPCs, treasure boxes, talking, the touch actions, the
  field's HUD and tab
- **Events**: the game's own Squirrel 2.2.4 running its compiled scripts and every native they call;
  vistas, message windows in your release's font, cut-in conversations, field scenes, fast-forward
  and skip
- **Battles**: the rules (stats, damage, criticals, the break gauge, states, the monsters' choices),
  the commands' menus (solo / pair, the sopia, items, awakenings, advents), the special scenes,
  effects, cameras and HUD, the battles' scripts, victory and its results, defeat and the game over
- **Menus**: the title screen, the camp (status, items, equipment, sopia, Scenarios, the map, saving),
  the options and the manual, the tutorials, the shop, the party, the pub, the library, the world and
  chapter selects, the staff roll
- **Sound**: the music (Android's Ogg Vorbis, the iPad's AAC, the Vita's ATRAC9), the effects and the
  voices, the options' volumes
- **Presentation**: 60 frames a second over the game's own 20; the options' **Graphics** button (or
  G / Tab there) opens CR2-U's own list - display mode, resolution, VSync, frame rate, renderer,
  FSR 3.1 / DLSS / MetalFX and their modes, sharpness, MSAA, anisotropic and texture filtering, HD
  textures - kept in `settings.ini`, the command line's flags over it. HD textures you make yourself
  ([HD textures](#hd-textures)): `cr2view --export-textures`, then `tools/hd_textures.py` (waifu2x
  or Real-ESRGAN) into `textures/hd/`; each replacement is found by the original's pixels, so every
  release can have its own in the same folder
- **Testing**: `cr2tool play all` plays every event to its end in all three releases (no native
  stubbed), `cr2tool fieldscript` / `walk` / `exits` / `battle` / `fight` check every map and
  encounter, `cr2view --capture` renders any moment and `--input` drives the menus

## Documentation

| | |
|---|---|
| `docs/STATUS.md` | each part of the port, what it follows and what checks it |
| `docs/ENGINE.md` | building in detail, `cr2view`'s modes and `cr2tool`'s checks, the tools |
| `docs/FORMATS.md` | the game's file formats as reverse-engineered |
| `docs/SCRIPTS.md` | the Squirrel scripts, the native API they call, the game's saves |
| `docs/BATTLE.md` | the battles' rules and staging as reverse-engineered |
| `docs/CONTROLS.md` | the Vita's buttons as its code reads them, and how a controller maps onto them |

## Layout

| Path | Purpose |
|---|---|
| `engine/` | the C++17 / SDL3 engine (`cr2core`), `cr2view` and `cr2tool` |
| `docs/` | engine and format notes |
| `tools/` | analysis / extraction tools; they work on your own copy |
| `android/`, `packaging/` | the Android project; the icons, desktop entry and release notes |
| `re/`, `extracted/` | (not in the repository) local RE output and decoded assets - never commit or share |
