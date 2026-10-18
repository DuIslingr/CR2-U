# CR2-U's status in detail

Each part of the port, with the commands that check it. Checked against the Android release's
named library (`libChaosRings2.so`: Ghidra decompiles, its names carried to the iPad's and the
Vita's builds - `tools/re_setup.sh`), the game's own scripts and a reference playthrough.

- [x] The data archive (MDB1 `.mvgl`, and the Android OBB, which is one too): read, every file
      listed and decoded (doboz) - `tools/mvgl.py`
- [x] The file types surveyed (docs/FORMATS.md)
- [x] Reverse-engineering setup (`tools/re_setup.sh`): the Android library's 18,000 exported names
      carried to the iPad and Vita builds
- [x] Textures: every `img` decodes in all three releases (PVRTC, ETC1, DXT1 / DXT3, the plain
      formats; `cr2tool images`), and agrees across them
- [x] Meshes (`geom`): the layout, vertex formats and materials; all parse (`tools/geom.py`)
- [x] The rest: `name` (the figures' joint and material names), `phys` (the fields' Bullet
      scenes), `emta` and the figures' particle systems (`eff267`, `eff280`: never read or run
      by the game - docs/FORMATS.md)
- [x] The scripts surveyed (docs/SCRIPTS.md): Squirrel 2.2.4 bytecode, all 139 parsed and
      disassembled (`tools/nut.py`); the native API they call - 23 tables, 222 functions - read
      out of the Android library (`tools/sq_bindings.py`)
- [x] The script VM: the game's own Squirrel 2.2.4 (`engine/third_party/squirrel2`); every
      script loads and runs its `main` in all three releases (`cr2tool scripts`), events run up to
      their first wait with the native API stubbed (`cr2tool run`)
- [x] The native API (222 functions): every one the scripts call - the field's, the events', the
      vistas', the menus' reservations - all but `InterfaceWindow.SetEffectRockBreak`, which no
      script calls (`cr2tool play all` / `fieldscript` list any stub a run reaches: none)
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
- [x] Billboards: the effects' sprites (and the few objects' and figures' parts) turned to the
      camera, or upright about y, as their vertex shaders do
- [x] The effects' shading: the second texture coordinate set (its scroll and offset), distortion
      (the colour's coordinates moved by the normal map's), overlays
- [x] The sky dungeons' point light: carried by the leader (FldMap::Draw), lighting the maps'
      3D figures
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
- [x] The messages' fast-forward button (speedUp, where the scripts allow it) and the vista
      events' skip button (the same figure, while the event has it): held - the mouse on it, or F -
      the pages race by, a vista at five times its speed and its sounds quiet (`--input fast`: two
      seconds of it, each)
- [x] Field event scenes: the event's models among the field's figures, the leader hidden and
      put back where the scene ends
- [x] Talking: the balloons over what the leader can tap, a tap (Return, a click) calling the
      nearest one's script (`cr2tool fieldscript`: all 136 a new game can tap play to their end)
- [x] Random encounters (FldEncount): each field map's area, steps and chance from the release's
      binary (three tables of 14 dungeons x 48 areas, found in the iPad's, Android's or Vita's
      executable), the walk counted in steps of 0.17, a draw every area's steps, the chance 5 more
      after each miss, the coupling picked by the area's weights (`battledata.dat`'s areas); SE
      0xc9, the fade, the battle on the area's map and music, back to the field where it began;
      the field menu's switch (flags 0x40e, 0x40f) and SetEncountEnable (`cr2tool encounters`:
      181 maps with them, every area and coupling there)
- [x] Battles' stage: the encounters' tables (`data\battledata.dat`), the map, the party and the
      monsters on their places for the monsters' size pattern, the commands' camera (`cr2view
      --battle 222`; `cr2tool battle all`: all 340 encounters' figures, places and cameras there)
- [x] Battles' rules, the core: the fighters' stats (a new game's party as the reference video
      shows it), hits, criticals, damage, the monsters' choices, the turn order - `cr2tool fight
      222 pair` plays a battle by them; the monsters' choices as BtlEnemy makes them (their 17
      conditions, 15 target rules, actions a turn, the commands first or last in it), the
      battle's start - the party striking first, the monsters' assault - by StartState
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
- [x] The field's tab (Camp, Map, Save) and the camp: the party's status windows, the title,
      its buttons and links, the Oz and the clock; its items' list and their use (whom, how many,
      their effect; Emergency Exit), the equipment (each place's list, the stats' change, the
      sopia's windows), the sopia's list, Scenarios, the pub's request, its options and the manual;
      the dungeon's map; saving (CR2-U's own save file - `cr2view --continue` resumes it) and the
      encounters' switch (a tap, Tab / C and the keys, or `--input "camp items u37 enter yes back"`,
      `map`, `save yes` in the field). The party's stats now as Cr3UpdatePlayerData makes them (the
      sopia's percents, the seeds, past level 20). The game's own saves too: `game.dat` / `cont.dat`
      (and `system.dat`) read and written - `cr2view --game-save game.dat` plays from one, `cr2tool
      save-import` / `save-export` convert; the system data (the flags 0..991, the options) kept
      across games as the game does
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
- [x] A battle lost: the lose camera, "... was defeated", the game over's figure and music, then
      "Would you like to try again?" - the continue data or the save loaded, else the title; before
      the story makes it a game over, back to the field (`--input tap`, `yes` / `no`)
- [x] Tutorials: the manual (the options' Manual - its list, each tutorial's pictures and
      pages), the battles' first-time ones (armour, the sopia / items lists, defend, elements,
      states, the options) and the encounters' own, the field's pictures - tutorialPic, the
      tutorial and announcement windows
- [x] Sound: the music (the Android release's Ogg Vorbis, looping at the game's repeat frames,
      faded), the effects and voices (both releases' banks) - the scripts' Cr3Sound, the
      messages' voices, the battles' music, fanfare and game over, their hits' effects and the
      fighters' voices, the menus' decide / back; the options' volumes (`--mute`; silent under
      `--capture`; `cr2tool sound` decodes all of it). The iPad's music too (AAC in CAF, FAAD2 -
      the system's or the bundled copy - looping at its own table's packets) and the Vita's
      (ATRAC9 in SXD, LibAtrac9, looping between its points), and the Vita's effects and voices
      (HE-VAG and ATRAC9 in `sndeff.sxd`, `voice.sxd1` / `.sxd2`)
- [x] The awakenings', advents' and monsters' special scenes (BtlSpecialCommandList, built in
      the game's code: read by an ARM interpreter over the library) - their effects (by the
      effects' table), summoned figures, cameras, the fighters hidden and faded, the screen's
      fades, hits and sounds; the pair's Advent list (`--input "pair advent v136"`)
- [x] A special scene's skip: a tap (`--input skip`) while it may be skipped - its sounds stopped,
      everyone back in place, its hits landed at once
- [x] The actions' effects by their settings: the hits' (element bursts, criticals, a group's
      once), the launch effects (the sopia's casting), shots flying to the one hit and their marks
- [x] Battles on screen: an advent's launch effect, the j007 / m507 warp, the magic barrier's
      mirror (eff227) as a sopia is turned back
- [x] The few scenes' own steps: Darwin's third awakening's space (d1154b in the map's place) and
      offsets (BtlActionTable's, `tools/gen_darwin_table.py`), Lesica's partner hidden, Rule's
      paired camera
- [x] The title screen (`cr2view --title`): the logos, the title and its "Please Touch", its buttons
      (Start, Scenarios, Options), the continue data's question, the panel - New Game, Load, the
      boss icons, the save's calendar (its date, the play time) -, the questions, then the game;
      the options' Title Screen and a script's ReserveTitle back to it
- [x] The map's own effect: eff284 with it in battles 221, 271, 272, 317
- [x] The screen breaking away (BtlPostEffectGlassFragment) before Darwin's third awakening's
      space: the map alone cut into 230 points' Delaunay shards, flying out, turning, growing
- [x] Treasure boxes: drawn as a dungeon is entered (the difficulty's tables), opened with their
      item and its window ("Obtained ...", the item's icon), the key boxes for Orlando alone - their
      mark and lock (two fingers spread on it: the mouse dragged from it) - the System window's
      message for the others
- [x] The world select: the town's transporter - the dungeons and levels open to you, their
      pictures and names, the question, then the warp there (`--input pick` / `up` / `down` /
      `back` / `yes` / `no`)
- [x] The chapter select: the town's book - the chapters open to you, their pictures, the question,
      then the jump to the chapter (the same `--input` as the world select)
- [x] The library: a shelf's entries open to you, their "new" marks, each read in its window
      (`--input up` / `down` / `pick` / `back`)
- [x] The pub: the request board (the posters posted, the one taken, the new ones), a request's
      details, accepting it or giving it up, the list of every request and the share done
      (`--input next` / `pick` / `accept` / `list` / `yes` / `no` / `back`; `--work 7:n` takes one, for tests)
- [x] The staff roll: an ending's credits over its picture, its music, held fast-forward, then the
      field again (`cr2view --play t0101 --credits 1` starts one at once, for tests; its lines are
      read from your release's executable)
- [x] The shop: Piupiu's buy and sell - the items and equipment for sale by the shop's rank,
      their prices, how many, the Oz (`--input buy` / `sell` / `talk`, then `items` / `weapons` /
      `armour` / `accessories`, `up` / `down` / `char`, `pick`, `more` / `less`, `yes` / `no`, `back`;
      `--oz n` for tests)
- [x] The party: the town's organizing - the two, their portraits, who can join, a face for a
      slot (`--input organize` / `talk`, `first` / `second`, `left` / `right`, `pick`, `back`)
- [x] The field's HUD: the tab, the character change (its button, the leader darkened and the other
      lit in its place; the leader's figure the field's character - `ChangeModel`); the
      announcements' window (annoWin, as the tutorials')
- [x] Touch actions: their set-up by the scripts (on / off, their spots), their marks for their
      characters; the sprint's and the high jump's hops, the iai's slash, the aim's drag, the
      hakkei's taps, the mystic's choice of spot (a tap on the marker / the mark, Return, `--input
      sprint` / `action`, then `slash` / `slashfail`, `drag` / `aim` / `aimfail`, `punch` /
      `punchfail`, `tapspot` / `tapspotfail`)
- [x] The engine whole: the events, the battles and the menus above run the game end to end
      (`cr2tool play all`: every event to its end, in all three releases; no native stub reached)
- [x] Platforms: Windows (MSVC, or llvm-mingw from Linux), macOS (universal, Vulkan through
      MoltenVK), Linux (AppImage) and Android (an APK) - README, Platforms
- [x] CR2-U's own settings: the options' Graphics list (display, resolution, VSync, frame rate,
      renderer, FSR 3.1 / DLSS / MetalFX, MSAA, anisotropic and texture filtering, HD textures)
