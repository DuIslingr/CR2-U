# CR2-U's engine: building and tools

## Building

```sh
cd engine
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
```

C++17; SDL3 (the system's, else downloaded and built by CMake), FreeType for the text (else built
here: `CR_FETCH_DEPS`), zlib for the APK (the same), and for Vulkan the Vulkan loader + glslc -
without them OpenGL only. The build is Ninja's: `ninja -C build` (`cmake --build build` runs it).
Options: `-DCR2_GLES=ON` (the GL device on OpenGL ES 3.0, as Android has it), `-DCR_DLSS_SDK=<the
DLSS SDK>` (DLSS: README, Platforms), `-DCR2_BUNDLED_FAAD2=ON` (the bundled FAAD2 over the
system's), `-DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw-x86_64.cmake` (Windows from Linux).

## Tools

| | |
|---|---|
| `cr2view --data <archive> [--model name] [--anim name --time s] [--lights figure\|none] [--renderer vulkan\|opengl] [--capture out.bmp]` | the model viewer: drag to turn, the wheel to zoom, Left / Right for the other models, Up / Down for the figure's animations, Space to pause |
| `cr2view --data <archive> --event <function> [--vista map] [--page s]` | an event: Return or a click turns a message's page |
| `cr2view ... [--resolution WxH] [--upscaler off\|fsr3] [--quality native\|quality\|balanced\|performance\|ultra] [--sharpness 0..1] [--size WxH] [--fps n] [--run s]` | the output in any mode: 60 frames a second (`--fps`), frames of a resolution up to 3840 x 2160 (F1 steps through the window's size and 720p .. 2160p), FSR 3.1 (F2 steps through its modes), the window's size; a capture `--run`s that long first |
| `cr2view --data <archive> --field <map> [--model c001] [--at point] [--binary file] [--walk sx,sy,s]` | a field: the arrow keys run the character (Shift walks) on the floors and against the walls; the maps' table comes from the executable beside the iPad's / Vita's archive or the APK beside Android's OBB, or `--binary` |
| `cr2view --data <archive> --play [map] [--call function] [--tap] [--skip-battles] [--binary file]` | a new game (or from a map, `--call` one of its events in place of its entry - without what would call it, so no fade back in after): the field's scripts, events and vistas one after another as the game chains them; Return or a click turns a page or talks to what has a balloon (`--tap`: once, after `--walk`); the battles it reaches fought by the rules, the party attacking as a pair (`--skip-battles`: taken as won) |
| `cr2view --data <archive> --game-save <game.dat>` | as `--continue`, from the game's own save (its `system.dat` beside it read too); `cr2tool save-import <game.dat> [system.dat]` prints it as a CR2-U save, `cr2tool save-export <archive> <save> <game.dat>` writes one |
| `cr2view --data <archive> --continue` | as `--play`, from the continue data each area's entry writes (`cont.txt`), else the save the field's Save wrote (CR2-U's own format: the story's state, the map and the place, the options - in the user's app folder, `DuIslingr/CR2-U/save1.txt`) |
| `cr2view --data <archive> --battle <coupling> [--party a,b] [--fight solo\|pair] [--seed n] [--state f:s] [--charge n] [--equip c:slot:id] [--time s]` | a battle's stage: its map, the party (characters a and b: Darwin 1, Orlando 2, Marie 3, ...) and the monsters, through the commands' camera; `--fight`: played by its rules, the party attacking each turn; `--state`: fighter f (0, 1 the party, 2.. the monsters) holding state s to begin with, one use; `--charge`: the gauge to begin with; `--equip`: character c's equipment slot (3..7 its sopia); `--item id:count`, `--difficulty n` (the world select's level - 1), `--members a,b` (the field's two, from 0: Darwin 0, Orlando 1); the field's `--input` also takes `tap`, `change`, `unlock`, `unlockfail` (tests) |
| `cr2tool check / scripts / run / images / models / compare-models / animations / pose / shaders / walk / exits / fieldscript / battle / fight` | checks on an archive: every file decodes, every script loads and runs its `main`, a script run with the natives stubbed, every texture / mesh decodes, two releases' meshes compared, the shader keys against their GLSL, the field player on every map, every map's exits, every map's entry script, every encounter's set-up, a battle played by its rules (an archive may be given as its folder; `walk` / `exits` / `fieldscript` take the executable, or `-` for the release's own beside the archive) |
| `tools/mvgl.py` | `list` / `stats` / `check` / `extract` an MDB1 archive |
| `tools/nut.py` | disassemble a compiled Squirrel script (a file, or out of an archive); `names`: the keys / calls the scripts use |
| `tools/geom.py` | a mesh file's meshes and materials, a check of them all, a flat-shaded preview |
| `tools/msg.py` | the text: `check` every message, `show` one in your terminal |
| `tools/anim.py` | Edge animations: evaluate a pose at any frame; `check` every animation |
| `tools/skel.py` | Edge skeletons: base pose, hierarchy, world matrices; `check` against the inverse binds |
| `tools/gen_camera_table.py` | the battle cameras' table (BtlCameraTable and their names) from the decompilation and the library, as `engine/src/battle/cameratable.inc` |
| `tools/gen_dotnum_table.py` | the interface's digits (DotNumParamPackTable: each glyph's size and place in hud_dotnum) from the library, as `engine/src/gfx/dotnumtable.inc` |
| `tools/gen_level_table.py` | the EXP tables (BtlData's lvup_exp and the sopia's) from the library, as `engine/src/battle/leveltable.inc` |
| `tools/gen_awake_table.py` | the characters' awakenings (their commands, the flags they need, their charge costs) and the pair's advents (their flags, sopia, charge and commands by level) from the decompilation and the library, as `engine/src/battle/awaketable.inc` |
| `tools/gen_special_table.py` | the special scenes (BtlSpecialCommandList::BuildAwake / BuildAdvent: their steps, by an ARM interpreter over the library) and which command shows which (SetActionTable, compiled from the decompilation), as `engine/src/battle/specialtable.inc` |
| `tools/gen_effect_table.py` | the battle effects' settings (BtlEffectTable's static initializer) from the decompilation, as `engine/src/battle/effecttable.inc` |
| `tools/gen_motion_table.py` | the battle motions' timing table (BtlMotionTable, built in the game's code) from the decompilation, as `engine/src/battle/motiontable.inc` |
| `tools/gen_box_table.py` | the treasure boxes' items by table (Cr3TreasureBox::GetItemID's: the normal, key and fixed boxes', the fixed ones' by character) from the decompilation and the library, as `engine/src/field/boxtable.inc` |
| `tools/sq_bindings.py` | the scripts' native API, from the Android library's decompilation |
| `tools/re_setup.sh` | Ghidra groundwork in `re/` (never committed): the Android library (x86 and armv7, named), the iPad and Vita builds named from it |
| `tools/vita_self.py` | the Vita's eboot (a fake-signed SELF) to an ELF plus its imports |
| `tools/ghidra/` | headless scripts: export decompilation and features, match functions across builds, apply names |
