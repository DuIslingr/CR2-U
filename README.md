# CR2-U

A from-scratch reimplementation of **Chaos Rings II** (Square Enix / Media.Vision, 2012), from the
iPad release (2.1.0), the Android release (2.1.2) and the PS Vita release (PCSG00498). The third of
the series' ports, after [CR1:U](https://github.com/DuIslingr/CR1-U) and
[CRO-U](https://github.com/DuIslingr/CRO-U).

**No game data is included, and none ever will be.** Bring your own copy; the port reads it where
it is and never changes it.

## Status

Groundwork. Chaos Rings II is not on CR1 / Omega's engine: it is built on Media.Vision's own
library, MVGL (`MVGL::Draw`, `MVGL::Sound`, `MVGL::Utilities::Database`, ...), with the game's
logic split between native classes (`FldMain`, `BtlActionCtrl`, `InterfaceMain` and about a
hundred menus) and compiled Squirrel scripts (`.nut`, the events). So the engine is new; CR1:U's
platform layers (SDL3, the renderers, audio decoding, upscaling) can carry over as it grows.

- [x] The data archive (MDB1 `.mvgl`, and the Android OBB, which is one too): read, every file
      listed and decoded (doboz) - `tools/mvgl.py`
- [x] The file types surveyed (docs/FORMATS.md)
- [x] Reverse-engineering setup (`tools/re_setup.sh`): the Android library's 18,000 exported names
      carried to the iPad and Vita builds
- [x] Textures: every `img` decodes in all three releases (PVRTC, ETC1, DXT1 / DXT3, the plain
      formats; `cr2tool images`), and agrees across them
- [x] Meshes (`geom`): the layout, vertex formats and materials; all parse (`tools/geom.py`)
- [ ] The rest: `skel` / `anim`, `name`, `dat` (the text), `phys`, `shad` (GLSL ES), `emta`
- [x] The scripts surveyed (docs/SCRIPTS.md): Squirrel 2.2.4 bytecode, all 139 parsed and
      disassembled (`tools/nut.py`); the native API they call - 23 tables, 219 functions - read
      out of the Android library (`tools/sq_bindings.py`)
- [x] The script VM: the game's own Squirrel 2.2.4 (`engine/third_party/squirrel2`); every
      script loads and runs its `main` in all three releases (`cr2tool scripts`), events run up to
      their first wait with the native API stubbed (`cr2tool run`)
- [ ] The native API (219 functions) for real
- [x] A model viewer (`cr2view`): any figure of any of the three releases, textured
- [x] Skeletons (`skel`: Sony Edge Animation's, ported) and the meshes' inverse bind matrices
- [x] Animations (`anim`): Edge Animation's frame sets and keyframes, decoded and checked
      (`tools/anim.py`)
- [x] Skinning and animation in the engine: the viewer plays every figure's animations (relative
      to the base pose, as the game blends them); materials' render states (alpha test, blending,
      culling, depth writes)
- [ ] The game's own shaders (lighting, normal maps), effects
- [ ] Engine: the field, events, battles, menus

## What you need

One or more of:

| Release | What the port reads |
|---|---|
| iPad 2.1.0 (`ChaosRings2enHD.app`) | `CRDB.ios.mvgl`, `Sound.ios/sound/` |
| Android 2.1.2 (`com.square_enix.chaosrings2gp`) | `main.212.com.square_enix.chaosrings2gp.obb` (the APK for its code, for RE) |
| PS Vita (PCSG00498) | `CR2/resources/CRDB.psp2.mvgl`, `Sound.psp2/` |

## Tools

| | |
|---|---|
| `cr2view --data <archive> [--model name] [--anim name --time s] [--capture out.bmp]` | the model viewer: drag to turn, the wheel to zoom, Left / Right for the other models, Up / Down for the figure's animations, Space to pause |
| `cr2tool check / scripts / run / images / models / compare-models / animations / pose` | checks on an archive: every file decodes, every script loads and runs its `main`, a script run with the natives stubbed, every texture / mesh decodes, two releases' meshes compared |
| `tools/mvgl.py` | `list` / `stats` / `check` / `extract` an MDB1 archive |
| `tools/nut.py` | disassemble a compiled Squirrel script (a file, or out of an archive); `names`: the keys / calls the scripts use |
| `tools/geom.py` | a mesh file's meshes and materials, a check of them all, a flat-shaded preview |
| `tools/anim.py` | Edge animations: evaluate a pose at any frame; `check` every animation |
| `tools/skel.py` | Edge skeletons: base pose, hierarchy, world matrices; `check` against the inverse binds |
| `tools/sq_bindings.py` | the scripts' native API, from the Android library's decompilation |
| `tools/re_setup.sh` | Ghidra groundwork in `re/` (never committed): the Android library (x86 and armv7, named), the iPad and Vita builds named from it |
| `tools/vita_self.py` | the Vita's eboot (a fake-signed SELF) to an ELF plus its imports |
| `tools/ghidra/` | headless scripts: export decompilation and features, match functions across builds, apply names |

## Layout

| Path | Purpose |
|---|---|
| `engine/` | the C++17 engine (`cr2core`), `cr2tool` and `cr2view` (`cmake -S engine -B engine/build && cmake --build engine/build`; SDL3) |
| `docs/` | format and engine notes |
| `tools/` | analysis / extraction tools; they work on your own copy |
| `re/`, `extracted/` | (not in the repository) local RE output and decoded assets - never commit or share |

## Licence

GPL-2.0-or-later (`LICENSE`); the bundled libraries keep their own (`THIRD_PARTY.md`). Chaos Rings II is © Square Enix; this project is not affiliated with
or endorsed by Square Enix or Media.Vision.
