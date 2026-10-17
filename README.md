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
- [ ] The rest: `name`, `phys`, `emta`
- [x] The scripts surveyed (docs/SCRIPTS.md): Squirrel 2.2.4 bytecode, all 139 parsed and
      disassembled (`tools/nut.py`); the native API they call - 23 tables, 222 functions - read
      out of the Android library (`tools/sq_bindings.py`)
- [x] The script VM: the game's own Squirrel 2.2.4 (`engine/third_party/squirrel2`); every
      script loads and runs its `main` in all three releases (`cr2tool scripts`), events run up to
      their first wait with the native API stubbed (`cr2tool run`)
- [ ] The native API (222 functions) for real: the field's set-up natives (flags, work values,
      the area, the gimmicks' display / collision / events, the layers) and the events' (the
      scene, its timelines, the message window, the fades) so far
- [x] A model viewer (`cr2view`): any figure of any of the three releases, textured; rendered with
      Vulkan 1.3 by default, OpenGL 3.3 where Vulkan can't start (`--renderer opengl`) - CR1:U's
      device layer, with CR2's vertices and the game's shading
- [x] 60 frames a second (`--fps`, 0 the display's rate) over the game's 20 updates a second
      (its scripts, fades, physics and timing kept): an event's camera, moves, animations and
      fades shown where they are between updates, the field's player stepped each 1/60 s of the
      world's time it takes
- [x] The output: any resolution up to 3840 x 2160 (16:9 presets, or the window's own shape),
      shown at its shape in any window (letterboxed); AMD FSR 3.1 temporal upscaling (native AA,
      quality, balanced, performance, ultra performance; Vulkan) with the scene's motion vectors -
      skinned figures, the camera, the fields' scrolling paintings - and the UI drawn after it
- [x] Skeletons (`skel`: Sony Edge Animation's, ported) and the meshes' inverse bind matrices
- [x] Animations (`anim`): Edge Animation's frame sets and keyframes, decoded and checked
      (`tools/anim.py`)
- [x] Skinning and animation in the engine: the viewer plays every figure's animations (motions
      relative to the base pose, a map's or a camera's absolute, as the game's blend tree has
      them); materials' render states (alpha test, blending, culling, depth writes)
- [x] The text (`message\*.dat`): eight languages, voice ids, the window's commands
      (`tools/msg.py`)
- [x] The game's own shading: its generated GLSL shaders (`shad`, one per material shader key) as
      one shader, the same formulas - lights from the maps' light figures, hemisphere ambient,
      the vertex colours' occlusion, specular, velvet, CLUT toon shading, normal maps, fog, alpha
      test, light-map masks, scrolling textures; the materials' wrap modes and filters; the
      animations' user channels (material colours: the UI's fades). `cr2tool shaders` checks the
      key decoder against every shader's source
- [ ] Shading left out: point lights, overlays, distortion, billboards (effects)
- [x] Fields: the maps' table (read from your copy's executable), the painted layers, the map's 3D
      figure, its camera, lights and points; the viewer walks a character around a map
      (`cr2view --field d0101`), scrolling as the game does, idle / walk motions
- [x] Field collision: the maps' Bullet scenes (`phys`) and the player as Bullet's kinematic
      character controller, ported - `cr2tool walk`: on every map of all three releases the
      character lands on a floor and runs in 8 directions without leaving it or passing a wall
- [x] Field exits: touching one loads the map it leads to, at its pop point (`cr2tool exits`: all
      427 lead somewhere real)
- [x] Gimmicks: the maps' NPCs, objects, effects and treasure boxes placed and drawn (NPCs idling),
      solid as in the game; `cr2tool walk` runs from every map's arrival points
- [x] Which gimmicks the story shows: each area's entry (`evt000`) runs in the dungeon's script on
      entering it (`cr2tool fieldscript`: to its end on all 217 maps from a new game's state)
- [x] The game's flow: what a script reserves (a vista, a battle, a pop point, the next area's
      entry) followed as the game branches, the event boxes firing on touch, the field camera as
      the scripts move it - `cr2view --play` plays a new game from its first map (battles taken as
      won)
- [x] Vista events: the event's script at the game's 20 updates a second - its scene's models on
      their locators with their animations and faces, the animated camera and lights, the fades,
      the messages' pages (`cr2view --event D01_01_0101_evt --vista d0151b`; `cr2tool play all`:
      all 70 event scripts play to their end in all three releases)
- [x] The events' message windows - the vista's and the narration's: the game's own interface
      figures, fitted as the game fits them
      (checked against the reference video within a pixel), the speaker's name, the lines typing
      in your release's font (`--font`: Android's TypeBank faces, the Vita's; else a system
      font; FreeType), the page cursor
- [x] The cut-ins' conversations: the portraits (their five layouts sliding in and out, the faces,
      the listener dimmed) behind the conversation window, fitted as the game fits them (checked
      against the reference video: `cr2view --play d0105 --call D01_01_0502`)
- [x] Field event scenes: the event's models among the field's figures, the leader hidden and
      put back where the scene ends
- [x] Talking: the balloons over what the leader can tap, a tap (Return, a click) calling the
      nearest one's script (`cr2tool fieldscript`: all 137 a new game can tap play to their end)
- [x] Battles' stage: the encounters' tables (`data\battledata.dat`), the map, the party and the
      monsters on their places for the monsters' size pattern, the commands' camera (`cr2view
      --battle 222`; `cr2tool battle all`: all 340 encounters' figures, places and cameras there)
- [x] Battles' rules, the core: the fighters' stats (a new game's party as the reference video
      shows it), hits, criticals, damage, the monsters' choices, the turn order - `cr2tool fight
      222 pair` plays a battle by them (break gauge, sopia, items, states: not yet)
- [x] Battles playing (`cr2view --battle 222 --fight pair`): each turn's actions as the game moves
      them - the command's motion, to the target's attack place and back on BtlMotionTable's
      times, the damage motion at the hit, the downed gone; the game's cameras - the opening's
      sweep, each attack's two shots, the monsters', the commands'
- [x] The battle's HUD: the party's windows (faces, HP / MP bars and numbers), the break gauge,
      the command's name, the damage numbers - as the reference video shows them
- [x] Battles' scripts (`script\battle.nut`, the encounters' own `battle_221` ..): run at the
      battle's steps as the game runs them - the prologue's set party and its scripted end, the
      first battle's talk and the status windows hidden for it, the turns they give, the charge
      they fill (`cr2tool fight 221`: all 49 scripted encounters run); the charge gauge filling
      by the hits and its ring on the HUD
- [x] Choosing the commands: the game's menus (Solo / Pair, Attack / Defend, the targets with
      their levels and HP; Escape by BtlCalc::Escape; the awakenings, paid with the charge; the
      sopia's commands by their levels, paid with MP, and the items, on the game's scrolling
      lists with their help), tapped or with the keys (Up / Down, Left / Right, Return, Backspace
      back), the menus' and the targets' cameras, the elements' signs on the targets and the
      party (the sopia's elements and auto skills, the scripts' equipment) - as the reference
      video shows them (`cr2view --battle 222 --fight manual`, `--play`; `--input "solo attack 2
      defend"` gives the choices)
- [x] Options in battle: the game's window - the volumes, language, orientation, quality,
      Title Screen and Manual shown, the volumes set by a tap or the keys, the language and the
      quality by their arrows (the messages then in that language, a system CJK font for what the
      game's lacks), Title Screen's yes / no, Manual, Back (`--input "options language+ title no back"`)
- [x] States: poison, blind, seal, the action cancelled, raised and lowered parameters, time
      stopped, the seals, the special ones - taken by the game's chances and resistances, their
      effects (counters, the magic barrier turning a sopia back, revived once, strength doubled,
      blocks, sure criticals, provoking, the result's doubling), their turns, the poison and the
      life / mana pools at the turn's end, the sealed actions not done, their signs on the party
      and the targets, their announcements
- [x] A battle won: "Victory!", the party's victory motion and camera, the result window (the
      EXP and SP gained, the levels, HP, MP and NEXT, the EXP going in, the levels kept), its
      messages - levels gained, the sopia's SP and what they learn, the Oz, the items and sopia
      left (rolled as each monster falls) - as the reference video's first battle
- [x] Tutorials: the manual (the options' Manual - its list, each tutorial's pictures and
      pages), the battles' first-time ones (armour, the sopia / items lists, defend, elements,
      states, the options) and the encounters' own, the field's pictures - tutorialPic, the
      tutorial and announcement windows
- [x] Sound: the music (the Android release's Ogg Vorbis, looping at the game's repeat frames,
      faded), the effects and voices (both releases' banks) - the scripts' Cr3Sound, the
      messages' voices, the battles' music, fanfare and game over, their hits' effects and the
      fighters' voices, the menus' decide / back; the options' volumes (`--mute`; silent under
      `--capture`; `cr2tool sound` decodes all of it). Not yet: the iPad's AAC music, the Vita's
- [x] The awakenings', advents' and monsters' special scenes (BtlSpecialCommandList, built in
      the game's code: read by an ARM interpreter over the library) - their effects (by the
      effects' table), summoned figures, cameras, the fighters hidden and faded, the screen's
      fades, hits and sounds; the pair's Advent list (`--input "pair advent v136"`)
- [ ] Battles on screen: the actions' effects past the hits' (an advent's launch effect too), a scene's skip
- [ ] Treasure boxes, the other windows (announcements, items), the HUD
- [ ] Engine: events, battles, menus

## What you need

One or more of:

| Release | What the port reads |
|---|---|
| iPad 2.1.0 (`ChaosRings2enHD.app`) | `CRDB.ios.mvgl`, `Sound.ios/sound/` |
| Android 2.1.2 (`com.square_enix.chaosrings2gp`) | `main.212.com.square_enix.chaosrings2gp.obb` and its APK beside it (or the APK unpacked there): `--data <that folder>` - the maps' table from the APK's `lib/armeabi-v7a/libChaosRings2.so`, the text in its own font (`assets/00001`), read from inside the APK |
| PS Vita (PCSG00498) | `--data <its PCSG00498 folder>` (or `CR2/`, or the archive itself): `CR2/resources/CRDB.psp2.mvgl`, the maps' table from `eboot.bin`, the text in its font (`CR2/resources/font/TBGoPro-Bold.otf`); `Sound.psp2/` |

Any one of them is the whole game by itself. Without `--data` the viewer takes `CR2_DATA`, else a
copy the build carries beside its executable: `vita/` (the PCSG00498 folder), `android/` (the OBB
and the APK) or `ipad/` (the `.app` or the unpacked `.ipa`).

## Tools

| | |
|---|---|
| `cr2view --data <archive> [--model name] [--anim name --time s] [--lights figure\|none] [--renderer vulkan\|opengl] [--capture out.bmp]` | the model viewer: drag to turn, the wheel to zoom, Left / Right for the other models, Up / Down for the figure's animations, Space to pause |
| `cr2view --data <archive> --event <function> [--vista map] [--page s]` | an event: Return or a click turns a message's page |
| `cr2view ... [--resolution WxH] [--upscaler off\|fsr3] [--quality native\|quality\|balanced\|performance\|ultra] [--sharpness 0..1] [--size WxH] [--fps n] [--run s]` | the output in any mode: 60 frames a second (`--fps`), frames of a resolution up to 3840 x 2160 (F1 steps through the window's size and 720p .. 2160p), FSR 3.1 (F2 steps through its modes), the window's size; a capture `--run`s that long first |
| `cr2view --data <archive> --field <map> [--model c001] [--at point] [--binary file] [--walk sx,sy,s]` | a field: the arrow keys run the character (Shift walks) on the floors and against the walls; the maps' table comes from the executable beside the iPad's / Vita's archive or the APK beside Android's OBB, or `--binary` |
| `cr2view --data <archive> --play [map] [--call function] [--tap] [--skip-battles] [--binary file]` | a new game (or from a map, `--call` one of its events in place of its entry - without what would call it, so no fade back in after): the field's scripts, events and vistas one after another as the game chains them; Return or a click turns a page or talks to what has a balloon (`--tap`: once, after `--walk`); the battles it reaches fought by the rules, the party attacking as a pair (`--skip-battles`: taken as won) |
| `cr2view --data <archive> --battle <coupling> [--party a,b] [--fight solo\|pair] [--seed n] [--state f:s] [--charge n] [--equip c:slot:id] [--time s]` | a battle's stage: its map, the party (characters a and b: Darwin 1, Orlando 2, Marie 3, ...) and the monsters, through the commands' camera; `--fight`: played by its rules, the party attacking each turn; `--state`: fighter f (0, 1 the party, 2.. the monsters) holding state s to begin with, one use; `--charge`: the gauge to begin with; `--equip`: character c's equipment slot (3..7 its sopia) (tests) |
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
| `tools/sq_bindings.py` | the scripts' native API, from the Android library's decompilation |
| `tools/re_setup.sh` | Ghidra groundwork in `re/` (never committed): the Android library (x86 and armv7, named), the iPad and Vita builds named from it |
| `tools/vita_self.py` | the Vita's eboot (a fake-signed SELF) to an ELF plus its imports |
| `tools/ghidra/` | headless scripts: export decompilation and features, match functions across builds, apply names |

## Layout

| Path | Purpose |
|---|---|
| `engine/` | the C++17 engine (`cr2core`), `cr2tool` and `cr2view` (`cmake -S engine -B engine/build && cmake --build engine/build`; SDL3, FreeType for the text, and for Vulkan the Vulkan loader + glslc - without them OpenGL only) |
| `docs/` | format and engine notes |
| `tools/` | analysis / extraction tools; they work on your own copy |
| `re/`, `extracted/` | (not in the repository) local RE output and decoded assets - never commit or share |

## Licence

GPL-2.0-or-later (`LICENSE`); the bundled libraries keep their own (`THIRD_PARTY.md`). Chaos Rings II is © Square Enix; this project is not affiliated with
or endorsed by Square Enix or Media.Vision.
