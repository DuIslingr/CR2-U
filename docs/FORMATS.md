# Chaos Rings II: file formats

Notes on the game's data as read so far. "iPad" is 2.1.0 (`ChaosRings2enHD.app`), "Android"
2.1.2 (`com.square_enix.chaosrings2gp`), "Vita" PCSG00498.

## The releases

| | iPad 2.1.0 | Android 2.1.2 | Vita |
|---|---|---|---|
| data | `CRDB.ios.mvgl` (371,582,259 B) | `main.212.com.square_enix.chaosrings2gp.obb` (391,618,202 B used of 1,011,429,376) | `CR2/resources/CRDB.psp2.mvgl` (567,313,685 B) |
| files | 12,001 | 11,711 | 12,233 |
| sound | `Sound.ios/sound/`: `bgm01..` `.caf`, `sndeff.bin`, `voice.bin` | (in the OBB / APK: not yet looked at) | `Sound.psp2/sound/`: `sndeff.sxd`, `voice.sxd2` |
| code | `ChaosRings2enHD`: fat armv7 / armv7s Mach-O, decrypted (cryptid 0), 463 symbols (Squirrel's) | `lib/{armeabi-v7a,x86}/libChaosRings2.so`: ~18,400 exported names | `eboot.bin`: fake-signed SELF |
| text | `*.lproj/Localizable.strings` (8 languages: system dialogs) | | |

The OBB is itself an MDB1 archive; its tail beyond the archive's own size is not read.

## MDB1 (`.mvgl`)

Media.Vision's archive (MVGL's `Utilities::Database`). Little-endian.

| Offset | Size | |
|---|---|---|
| 0x00 | 4 | `MDB1` |
| 0x04 | 2 | tree entries (T) |
| 0x06 | 2 | names (= T) |
| 0x08 | 4 | data entries (D = T - 1) |
| 0x0c | 4 | data start |
| 0x10 | 4 | the archive's total size |
| 0x14 | 8 T | tree: i16 compare bit, u16 data id, u16 left, u16 right - a Patricia trie over the names for lookup; entry 0 is the root (data id 0xffff) |
| | 64 T | names: a 4-byte extension (NUL or space padded: `img `, `nut `) then a 60-byte name; tree entry i's name is name i |
| | 12 D | data: u32 offset from the data start, u32 size, u32 stored size |

Names use `\` for folders: `images\`, `message\`, `shaders\`, `script\`, `data\`; most files
are at the root.

A file whose stored size is below its size is **doboz**-compressed (Attila Afra's LZ77 variant):

- a header byte: bits 3..5 = the size fields' width - 1 (so 1, 2 or 4 bytes), bit 7 = stored as is;
  then the size and the compressed size, each that wide;
- then 32-bit control words, read low bit first, 31 flags each (the top set bit marks the end):
  a flag of 0 is one literal byte, 1 a match;
- a match is 1..4 bytes picked by the low 3 bits of the next 32-bit word:

| low bits | bytes | offset | length |
|---|---|---|---|
| x00 | 1 | (w & 0xff) >> 2 | 3 |
| x01 | 2 | (w & 0xffff) >> 2 | 3 |
| x10 | 2 | (w & 0xffff) >> 6 | 3 + ((w >> 2) & 15) |
| 011 | 3 | (w & 0xffffff) >> 8 | 3 + ((w >> 3) & 31) |
| 111 | 4 | w >> 11 | 3 + ((w >> 3) & 255) |

The offset counts back from the end of the output so far (no -1). Every file of the three
archives decodes to its size (`tools/mvgl.py check`).

## File types

Counts are the iPad's (Android / Vita alike unless noted).

| Type | Files | What it is |
|---|---|---|
| `anim` | 3514 | an Edge Animation animation; magic `EA04` (`40AE` as bytes) |
| `img` | 1873 (Android 1605, Vita 1950) | a texture: the iPad a legacy PVR file, Android PVR v3, the Vita GXT (below) |
| `geom` | 1724 | a figure's meshes and materials (below) |
| `skel` | 1724 | an Edge Animation skeleton; magic `ES02` (below) |
| `name` | 1259 | a figure's names: u32 joints, u32 materials, then an offset per name (not terminated: each runs to the next) - the joints' and materials' names, whose hashes the figure uses |
| `dat` | 880 | tables: `message\*.dat` (874) the text (below), `data\*.dat` (6) |
| `shad` | 466 | a GLSL ES shader's source (`shaders\<4 hex words>_fp` / `_vp`) |
| `phys` | 419 | a Bullet scene: the fields' walls and floors, their triggers (below); magic `SYHP` ("PHYS" as a u32) |
| `nut` | 139 | a compiled Squirrel script: `0xfafa`, `SQIR`, char / int / float sizes, then `PART`-separated parts; the source name (`D12_01_0206_evt.i`) and `main` |
| `emta` | 3 (Android 2) | `eff267`, `eff280`, the two figures with a particle system (below): u32 1, f32 2, then a short list of values. Nothing in the game reads them (no `emta` in the library, as text or as a word) |

## Textures (`img`)

`engine/src/assets/image.cpp` decodes all of them (`cr2tool images`: 1873 / 1605 / 1950, none
failing); the same texture decoded from the three releases differs by 0 to 5 levels in 255 on
average (each release's own lossy compression), orientation and alpha alike.

| Release | Container | Formats (count) |
|---|---|---|
| iPad | legacy PVR: 52-byte header (u32 0x34, height, width, mips, flags, data size, bpp, masks, `PVR!`, surfaces); flags' low byte the OGL pixel type, 0x10000 = rows bottom-up | PVRTC 4bpp 0x19 (1629), RGBA4444 0x10 (64), RGBA8888 0x12 (54), RGBA5551 0x11 (50), AI88 0x17 (34), A8 0x1b (33), I8 0x16 (5), RGB888 0x15 (3), RGB565 0x13 (1) |
| Android | PVR v3 (`PVR\x03`, u64 pixel format: 4 channel names + 4 bit counts, or an enum when the high half is 0; metadata key 3 = orientation) | ETC1 = enum 6 (836), rgba 5.5.5.1 (518), 4.4.4.4 (153), la 8.8 (33), 8.8.8.8 (22); legacy PVR A8 / I8 / AI88 / RGB888 (43) |
| Vita | Sony GXT (as CR1 / Omega's Vita releases: data offset 0x20, type 0x30, format 0x34, size 0x38), swizzled (Morton order, y bit lowest, square tiles of the smaller side; per 4x4 block for the BC formats) | UBC2 / DXT3 0x86000000 (1858), U8U8 GRRR 0x07002000 = luminance + alpha (44), U8 R000 0x00006000 = alpha (38), UBC1 / DXT1 0x85000000 (5), U8U8U8U8 ARGB 0x0c001000 (2); legacy PVR (3) |

PVR v3's channels of 8 bits each are stored byte by byte in order; its packed ones (4444, 5551)
from the top bit down.

Row order: every Android PVR and all but 2 of the iPad's are stored bottom row first (the GL
releases upload the rows as stored, so texture v = 0 is the picture's bottom); every GXT top row
first. The meshes' texture coordinates are the same in all three releases, so the decoder turns
the GXTs bottom-up: the same coordinates then reach the same texels everywhere.

How a texture is sampled is the material's (its texture entry's flags, below), not the file's.

## Meshes (`geom`)

`tools/geom.py` (`info`, `check`, `render`) and `engine/src/assets/geom.cpp` (`cr2tool models`):
every file parses in all three releases, all indices within their meshes' vertices; the textures
the materials name are all in the archive but 10 references on the iPad (3 names). From `MVGL::Draw::Figure::Build`, `BuildMesh` and `DrawMesh`.

The iPad's files are version 0x3c, Android's and the Vita's 0x50: the newer header is 0x10 bytes
longer, so every offset moves by 0x10; the records are alike. The Vita's keep Android's layout
but in GXM terms: index type 0 (U16) and primitive 0 (the indices are still the same strips),
attribute types SceGxmAttributeFormat (0 U8, 1 S8, 2 U16, 3 S16, 4..7 their normalised forms, 8
F16, 9 F32): positions F32, normals / tangents / binormals / texcoords / weights F16 (texcoords
final, not 1024ths), colours and joints U8. Some say a vertex stride 2 bytes over what their
attributes use and pack their vertices at the used size - the 427 proxy meshes (`p` files: 24 for
22) and one more - while 347 others are padded to the stride they say; only the positions tell
(read at the wrong stride they leave the mesh's bounding sphere). Decoded (`cr2tool
compare-models`), the iPad's and Android's agree exactly in the 1716 files where both have the
same meshes, Android's and the Vita's to 0.0005 in normals; Android's texcoords wrap at the 16-bit
range (±32) where the Vita's don't (a whole number of repeats: the same sampling).

| Header | |
|---|---|
| +0x00 u32 | version (0x3c / 0x50) |
| +0x04 u16 | meshes |
| +0x06 u16 | materials |
| +0x08 u16 | lights (below) |
| +0x0a u16 | cameras (below) |
| +0x10 u16 | relocations (the list at +0x48: offsets of pointers to make absolute) |
| +0x18 | the bounds |
| +0x30 u32 | the meshes (0x54 bytes each) |
| +0x34 u32 | the materials |
| +0x38 u32 | the lights (0x34 bytes each) |
| +0x3c u32 | the cameras (0x24 bytes each) |
| +0x40 u32 | the inverse bind matrices, one per skeleton joint: 3x4 row-major (0x30 bytes) on Android / the Vita, 4x4 column-major (0x40) on the iPad |

| Mesh | |
|---|---|
| +0x02 u16 | attributes |
| +0x04 u16 | vertex stride |
| +0x06 u16 | index type: 0x1403 (u16) in all |
| +0x0a u16 | primitive: 5 (triangle strip) in all; degenerate triangles join the strips |
| +0x10 u32 | material |
| +0x14 u32 | vertices |
| +0x18 u32 | indices (all of them drawn; some skinned meshes have more vertices than indices) |
| +0x24, +0x28 | vertex data, index data |
| +0x00 u16 | the matrix palette's size |
| +0x2c | the palette: that many u32 joint indices (a rigid mesh: one) |
| +0x34 | attribute descriptors, 8 bytes each: u8 semantic, u8 normalised, u16 components, u16 GL type, u16 offset |
| +0x38 | bounds: centre (3 floats), half extents (3; not exact), radius (+0x50) |

Semantics (the shaders' attributes): 1 Position (3 floats), 2 Normal, 3 Tangent, 4 Binormal (3
normalised shorts each), 5 / 6 Texcoord0 / 1 (2 shorts, divided by 1024 in the vertex shader),
9 Color (4 normalised bytes), 10 / 11 blend indices / weights (bytes). Skinning: a palette of 56
joints (`MatrixPalette[56*3]`). The releases store a vertex's joint differently, and the vertex
shader says how it reads it:

- Android: blend indices as MatrixPalette's rows (slot x 3; `idx = Indices`), on every skinned mesh.
- The iPad: blend indices as palette slots (`idx = Indices * 3.0`); a mesh on one joint a vertex
  has no blend indices but a four-component Position whose w is the slot (`idx = Position.w * 3.0`;
  3 shaders take `Position.w` as the row). Meshes on the built-in figure shader (key 0) store slots.
- The Vita: as the iPad's meshes, but the stored values are rows (slot x 3) - its programs are
  compiled.

Read so, the three releases' 714 skinned meshes all index inside their palettes (107 on the
iPad and the Vita through the position's w: the interface's masks, as the HP / MP bars' that
uncover the gauge, and figures' rigid parts).

A material: u32 hash, +0x04..+0x13 its **shader key** (4 u32: below), +0x14 u8 parameters, +0x15
u8 render states, then 0x14-byte entries: u8 parameter, u8 kind (0 texture, 1..4 that many
floats, 100 a render state), u16, then the texture's name (12 characters: `images\<name>.img`)
and at +0x10 its sampler flags, or the values. The parameter ids index MVGL's shader parameter
table (48-byte entries of a name and its size, in the library): 0x32 ColorSampler, 0x33
DiffuseColor, 0x35 NormalSampler, 0x36 Bumpiness, 0x38 SpecularStrength, 0x39 SpecularPower,
0x3e SurfaceColor, 0x3f FuzzySpecColor, 0x40 SubColor, 0x41 RollOff, 0x42 VelvetStrength, 0x43
LightSampler, 0x44 OverlayColorSampler, 0x47 OverlayStrength, 0x48 CLUTSampler, 0x54 Time, 0x55
ScrollSpeedSet1, 0x58 ScrollSpeedSet2, 0x5e OffsetSet1, 0x61 OffsetSet2, 0x64
DistortionStrength (and, set by the engine, 0x09 / 0x0b DirLamp01Color / Dir, 0x2e AmbientColor,
0x2f SkyDir, 0x31 GroundColor, 0x53 CameraPosition, 0x65 FogParams, 0x69 FogColor, 0x6a
AlphaRef). The UI is figures too: the title screen is `title.geom` (7 textured quads, its own
skeleton) played with `title_in` / `title_wait` / `title_loop` / `title_out`; its colour comes
from LightSampler (an A8 mask beside an opaque colour texture: `hud_logoTtlC` + `hud_logoTtlA`)
and its DiffuseColor from the animations (user channels, below).

A texture entry's sampler flags (`Texture::Build` copies them to the texture; `FinishInitialize`
sets them): bits 0-2 wrap S, 3-5 wrap T (the table at 0x53b6a4 in the Android library: 2
GL_MIRRORED_REPEAT, 3 GL_CLAMP_TO_EDGE, else GL_REPEAT), 6-8 the min filter's function
(`AdjustTextureFilter`: 1, 3, 4 nearest, else linear; with mips, the mipmapped forms), 9-11 the
mag filter (the table at 0x53b688: 1 nearest, else linear). All three releases agree: 1579
repeat / repeat, 1359 mirrored / repeat (the characters: their meshes mirror the left half's
texture coordinates past u = 0), 123 mirrored both, 117 + 97 clamped both, 26 mixed.

A render state's entry holds 4 u32 GL values; the ids are `RenderContext`'s table (its names, in
order from 0x78): `alpha_func` (func, ref as a float - GREATER), `alpha_test_enable`,
`blend_func` (src, dst), `blend_equation`, `blend_enable`, `cull_face`, `cull_face_enable`,
`depth_func`, `depth_mask`, `depth_test_enable`, `polygon_offset`, `polygon_offset_fill_enable`,
`color_mask`, `stencil_func`, `stencil_mask`, `stencil_op`, `stencil_test_enable` (0x88). Those
a material leaves out keep the defaults: no alpha test, no blending, back faces culled, depth test
and writes on. The textures' alpha is not transparency unless the material tests or blends (the
characters' carries other data).

### Lights

A figure can carry lights (`Figure::SetupLights`, `RenderContaindLights`): +0 u32 the joint
whose name hash it is (that joint places it), +4 u16 type (0 point, 2 ambient, 3 directional, 4
fog), +6 u16 which of its kind (DirLamp01..03, PointLamp01..07), then floats from +8:

| Type | +0x08 | +0x0c | +0x10.. | +0x1c | +0x20.. |
|---|---|---|---|---|---|
| directional | intensity | | colour (4) | | |
| point | intensity | range (unused) | colour | | |
| ambient | intensity | | sky colour (3) | | ground colour (3; 0 in every map) |
| fog | far | near | colour (3) | alpha | |

The engine's uniforms: DirLamp01Dir = the joint's +Z axis, normalised (the direction towards the
light), DirLamp01Color = intensity x colour; AmbientColor / GroundColor = intensity x the colours,
SkyDir = the joint's position, normalised ((0, 1, 0) at the origin); FogParams = (far / (far -
near), -1 / (far - near), alpha), FogColor. Without a light figure, RenderContext's defaults: the
light (0.531, -0.758, 0.379) at 0.5 grey, ambient 0.5, ground 0.3, sky (0, -1, 0), fog (1.5,
-0.005, 1) white. The maps' lights are figures of their own: `<map>c` lights the characters (118
with ambient + directional + fog, ...), `<map>b` battles, `*_evt_light` events.

### Cameras

`Figure::SetupCameras` / `GetContaindCamera`: +0 u32 the joint whose name hash it is (the camera
looks down that joint's -Z, its +Y up), +4 f32 the vertical field of view in degrees, +0xc near,
+0x10 far, +0x14 an orthographic half height, +0x18 u32 orthographic. The maps' `<map>c` figures
hold their field camera (`CAM_d0101c`: 13.2 degrees, 12 units away), with the lights, a
`defaultPos` joint (where the player starts) and the walls (`MTR_kabe`); `<map>p` the entrances
(`d0101_p1`, `ragdoll_d0103_p1`, ...) and boxes (`evt021_box00`, `c208_box00_scale080`).

Names are hashed with `MVGL::GenerateNameHash`: CRC-32 (reflected, 0xedb88320) from 0xffffffff,
not inverted at the end.

### Particles (never run)

The header's +0xe (u16) counts particle systems and +0x44 is the offset of their data. Only
`eff267` and `eff280` have one, a single system each and no meshes.

A system is 0xbc bytes, then its curves:

- u32 n, then n × (t, r, g, b): the colour.
- u32 n, then n × (t, alpha).
- u32 n, then n × (t, x size).
- u32 n, then n × (t, y size).
- u16 n, u16 stride (20), then n × 8 bytes: its vertex layout (position 3 floats, colour 4
  bytes, texture coordinates).

Each curve is linear between its keys (Particle::GetValue). Fields of the 0xbc bytes, as
`MVGL::Draw::Particle` reads them:

- +0x08: the emission's spread, 0 over a sphere, 1 about one axis, else none.
- +0x0c / +0x10: the distance from the emitter.
- +0x14..+0x1c: the direction.
- +0x24 / +0x28: the speed and its random part.
- +0x2c: the most alive at once.
- +0x34 / +0x38: the life and its random part.
- +0x40 / +0x44 and +0x48 / +0x4c: the size and its range.
- +0x58..+0x60: an acceleration added to the velocity each update.
- +0x68: the damping the velocity is multiplied by.
- +0x78: the sprite's twist (1 none).

The game never runs any of it. Figure::SetupParticles, Figure::RenderParticle and
Particle::Update are in the library, but nothing calls them: no branch, no pointer and no
relocation names them. MVGL's other effect system, Spark, needs a `.sprk` beside the figure, and
there is none. So `eff267` draws nothing. It is commands 252 .. 255's shot, the breaths of the
dragons and Wyverns, between their launch effect `eff266` and the mark `eff268`. `eff280` is only
named in the battle effect table. The port draws neither.

## Fields

`FldMap::ChangeArea` builds a map from a table in the **executable** (225 records of 0x18 bytes,
from `t0101`; the iPad's executable, the Vita's `eboot.bin` and Android's library each have one -
the iPad's and the Vita's set +0x17 on 8 maps where Android's doesn't):

| Record | |
|---|---|
| +0x00 | the name (`t0101`, `d0101`, ...) |
| +0x08 i32 | the painting: 0 = `f1` (drawn at half scale), else `f2`; for the scroll, bit 1 = 2048 units wide (else 1024), bit 0 = 2048 tall, in units of a 960-wide screen |
| +0x0c u8, +0x0d u8 | a midground, a foreground layer |
| +0x10 i32 | 1: the foreground at twice the scroll, 1152 units to the left (one map) |
| +0x14 u8, +0x17 u8 | a 3D figure `<map>f` (and its animator) |
| +0x15 u8 | the painting is the shared `d5051f2_hdbg` (a sky: 0.4 of the scroll, not clamped) |
| +0x16 u8 | no painting (a 3D map: the camera unscaled, unscrolled) |

The painted layers are `images\<map><f1|f2>_hd<bg|mg|fg>.img`: 2048 x 2048 textures (the painting
in their middle, alpha for the foreground's cut-outs), each drawn as a 4096 x 4096-unit quad
(`CustomFigure`: the background at z -5000, the others -0.5, blending src alpha) with an
orthographic camera in MVGL's virtual pixels (`DisplayInfo`: 1920 wide, the height the screen's
aspect gives; `Camera::GetProjectionMatrix` with the half height = half the larger side) and
the painting's scale (+0x260 / +0x264: 1, or 0.5 for `f1`). The 3D figures use the `<map>c`
camera (near x 0.5, at least 0.1; far x 5, at most 5000; the aspect the virtual screen's),
magnified on screen by `2.13 x width / height` x the painting's scale: the painting's height is
the camera's field of view, the two line up by construction.

Draw order (`FldMain`): the background, the midground (half the scroll), the 3D passes (opaque,
alpha-tested, transparent), the foreground. Scrolling (`CalcNewScreenPos`, `SetScrollStop`,
`CalcScrollStop`): the offset = -(the player's NDC in the map camera) x the screen's size x the
scale, clamped to +-(the painting's size x width / 960 - the screen's size), and applied to both
cameras' screen matrices (`GetScreenMatrix`: offset / the screen's size, in NDC) - the player
stays centred until the painting's edge.

Collision (`PhysicsSceneLoader::LoadScene`, `<map>c.phys` the walls and floors, `<map>p.phys`
the exits' trigger volumes): a header (+4 version 1, +8 bodies, +0xc shapes, +0x10 surface names,
then the bodies', the shape pointers' and the names' offsets), bodies of 0x4c bytes (position,
rotation quaternion, friction / restitution, +0x2c the shape, +0x30 contact response - 0 a
trigger, +0x34 the name), shapes (u32 kind: 0 a box - half extents, surface; 1 a sphere - radius,
surface; 2 a triangle mesh - +4 vertices, +8 triangles, +0x24 / +0x28 / +0x2c the indices', the
vertices' and a surface id per triangle's offsets) and 64-byte surface names: the floors name
their footstep sound (`se02`), the walls `MTR_kabe` (0.5 units tall). The player is Bullet
2.81's `btKinematicCharacterController` (`MVGL::Physics::CharacterController`): a sphere of
radius 0.02, step height 0.01, Bullet's defaults otherwise (gravity 29.4, fall speed 55, 45-degree
slopes), the figure drawn 0.02 below its centre. `FldPlayer::Update` turns the stick (the touch's
drag from where it started, over the virtual screen's size, x 0.01: under 0.0002 idle, under
0.0005 walking, else running) into a direction along the ground from the camera and passes
`direction x speed x dt x 60` (walking 0.09, running 0.48) to `setVelocityForTimeInterval` for
0.05 s each of the game's 20 updates a second, and FldMain steps the world once
(`stepSimulation(dt, 1, 1/60)`: one substep of 1/60 s, the rest of the 0.05 s dropped) - so 0.09
and 0.48 units a second, the world's gravity at a third of real time. The engine takes that 1/60 s
of world time as three 1/180 s steps, one each 1/60 s of real time: the same speeds and gravity,
a position for each of 60 frames a second, and the walls met finely (at the game's one step of
0.024 units the sphere can wedge past a wall's plane in a crease: `cr2tool walk` finds 2 runs of
4032 that end beyond a wall, none in three steps).

Exits: `<map>p.phys`'s bodies, each named for the pop point it leads to - the destination map is
the name's first five characters (`FldMap::GetFieldMapNumFromPopPointName`), the arrival a joint of
that map's `<map>p` figure (`FldMain::ChangeAreaExecute`): d0101's `d0103_p1` leads to d0103's
point `d0103_p1`. They are solid (contact response on: the player stops at them) and the contact
(within Bullet's breaking threshold, 0.02 - the controller rests up to a step short of what stops
it) changes the area (`CollisionEventExec`: a sound, a fade). `cr2tool exits`: all 427 exits of
the 217 maps lead to a map that has the point.

Gimmicks (`FldGimmickDataManager::SetData`): `<map>p`'s joints, each placing one by its
position, rotation and scale, and saying by its name what it is - `ragdoll_*` an exit's marker,
`<map>_p<n>` a pop point, `c<nnn>` / `j<nnn>` an NPC (`FldNPC`: the figure `c<nnn>`, idling on
`c<nnn>fn01`), `obj<nnn>` an object (the figure), `eff<nnn>` an effect (the figure),
`tbox` / `fbox` / `obox` a treasure box (`obj011`, `obox` `obj012`), `sprint` (`obj151`), `hjump`,
`mystic`, `iai`, `aim`, `marts` the characters' field abilities, `spot<nn>` / `POP` / `JUMP`
spots, `evt<nnn>` an event box - and with `_scaleNNN` the figure's scale (NNN / 100), `_check`
a thing to tap, `_box` / `_ball` / `_cap` its collision shape: a solid body (NPCs, objects,
boxes) sized by the joint's scale / 100 (a box's half extents; a ball's radius, x; a capsule's
radius, x, and height, y). The scripts name a gimmick by its name's start (5 characters for NPCs and boxes, 7 for objects,
effects and event boxes) and the two digits after `_box` / `_ball` / `_cap`
(`Cr3UtilGetScriptFunctionName`): `c208_box00_scale080` is `c208_00`, `tbox_check_cap07`
`tbox_07`, `evt021_box00` `evt021_00` (its script function `evt021`). Some places hold a character more than once (t0101's `c003_check_cap00`
/ `_cap01` / `_cap09`): alternative spots the story chooses between. Across the maps: 503 pop
points, 427 exit markers, 310 objects, 244 spots, 178 effects, 123 event boxes, 96 sprint spots,
93 NPCs, 130 boxes.

The player (`FldPlayer::CreatePlayer`): `c<id>` with the motions `c<id>` + `fn01` (idle), `ff01`
(walk), `ff02` (run), `fj01..03` (jumps), `fp01..06` (the character's own).

## Shaders (`shad`) and shading

Every material with a non-zero shader key draws with the GLSL ES 1.00 pair
`shaders\<%08x_%08x_%08x_%08x>_vp.shad` / `_fp.shad` (`Figure::SetupMaterials`: the key's words
printed in order; Android 219 keys, the iPad 204 (233 pairs in its archive), the Vita the same
219 as Android, compiled - its files are GXP programs). Key 0 (668 materials) uses MVGL's
built-in `Figure<attributes>` shaders, in the library: texture x vertex colour x DiffuseColor.

The pairs are generated from the key, so they share one vocabulary: about 60 distinct fragment
lines in 103 distinct fragment programs. `engine/src/gfx/shaders/shading.glsl` is all of them as
one, chosen by bits (`shade_flags.h`), the same formulas in the same order:

- colour = ColorSampler (or 1) x DiffuseColor; LightSampler's alpha replaces the texture's
  first; the vertex colour tints rgb or rgba
- lit (130 keys): colour x (light x obscure + ambient) + specular, with light = N.L (clamped) x
  DirLamp01Color, or a CLUT: CLUTSampler at (N.L / 2 + 1/2, N.H) gives the light (rgb) and the
  specular's power (a); ambient flat (AmbientColor) or a hemisphere: mix(GroundColor,
  AmbientColor, (N.SkyDir + 1) / 2); specular = SpecularStrength (or the vertex alpha, or on the
  iPad the normal map's alpha) x Schlick's d / (SpecularPower (1 - d) + d) of d = N.H x the light;
  velvet: light x SurfaceColor + SubColor x VelvetStrength x (smoothstep(-RollOff, 1, N.L) -
  smoothstep(0, 1, N.L)), and with specular a rim (1 - N.V) x FuzzySpecColor x VelvetStrength
- **obscure**: the vertex colour's red, on the light only (and the specular where the key says) -
  the characters' vertex colours are this, ambient occlusion, not a tint
- the normal map bumps N by (rgb - 1/2) x Bumpiness along the tangent and binormal
- fog: mix by clamp(1 - (FogParams.x + depth x FogParams.y)) x FogParams.z; alpha test: the
  texture's alpha < AlphaRef discarded (the material's alpha_func reference)
- texture coordinates + OffsetSet1 + fract(ScrollSpeedSet1 x Time)

Billboards (25 keys - nearly every effect, a few objects and figures): the vertices turned by
ViewInverse's 3x3 (the camera's axes), or - word 2's 0x8, 5 keys - by camPos (the view's third
row with y 0, normalised; y x it; their cross: upright, turning about y alone), then scaled by
MatrixPalette[0]'s columns' lengths and moved to its translation - its rotation and the vertices'
joints unused. The second texture coordinate set (18 keys: Texcoord1 / 1024, + OffsetSet2 or +
fract(ScrollSpeedSet2 x Time)) is the distortion's (16) - NormalSampler read there, the colour's
coordinates + (its rgb - 1/2).xy x DistortionStrength, the bump from the same texel - or an
overlay's (2; the third reads it at the first set): the colour's rgb mixed toward
OverlayColorSampler's by OverlayStrength x its alpha, before DiffuseColor. d0252b's overlay names
no texture: its sampler, never set, reads unit 0 - the colour's own, over itself. Point lights (2
keys - the 40 sky dungeons' `<map>f` figures, obj101 / obj102): per vertex the direction to
PointLamp01Pos and max(radius - distance, 0) / radius, then max(0, N.L) x PointLamp01Color x that
on the light (the direction as interpolated); RenderContaindLights puts the point light's joint's
place and, for its radius, the joint's x scale (d0303's 50; +0x0c's range, 0.5, unused), colour
intensity x colour. FldMap::Draw, on the maps whose +0x15 is set (the d5051f2 sky's), moves it by
the leader's DummyEP04 + 0.1 up: a light carried. Android's / the Vita's key bits (word 0 first):
word 0 - 0x1 DirLamp01, 0x4 PointLamp01, 0x20 hemisphere, 0x40 flat ambient, 0x80 CLUT, 0x100
specular, 0xe00 its strength from the vertex alpha, 0x4000 obscure, 0x8000 the vertex colour
tints, 0x10000 ColorSampler, 0x20000 NormalSampler, 0x80000 overlay; word 1 - 0x4000 velvet,
0x10000 the tint includes alpha, 0x01000000 alpha test, 0x02000000 ScrollSpeedSet1,
0x04000000 ScrollSpeedSet2, 0x10000000 OffsetSet1, 0x20000000 distortion; word 2 - 0x1 OffsetSet2, 0x4
billboard, 0x10 fog, 0x700 LightSampler, 0x8000 skinned (0x10000 two weights). Combinations:
obscure on the specular = 0x100 and 0x4000, the rim = 0x100 and word 1's 0x4000. `cr2tool
shaders`: on Android the decoder agrees with every key's GLSL, bit for bit. The iPad's older
generator lays words 1 and 2 out differently (alpha test 0x00800000, fog 0x8, ...), so its
shaders are read from their GLSL; the Vita's keys, Android's, are decoded.

Name prefixes: `d` dungeon maps (832), `eff` effects (281), `t` towns (148), `obj` objects (72),
`m` monsters (64), `cam_z` cameras (63), `c` characters (42), `v` vistas (36), `gmk` gimmicks.

## Skeletons (`skel`) and animations (`anim`)

MVGL animates with Sony's **Edge Animation** (the PS3 SDK's library, ported: `edgeAnimProcessBlendTree`,
`_edgeAnimEvaluate`, `edgeAnimLocalJointsToWorldMatrices3x4`, ... are in the Android library by
name). The skeletons are the same bytes in all three releases.

| Skeleton | |
|---|---|
| +0x00 | `ES02` (`20SE` as bytes), u32 size |
| +0x10 u16 | joints |
| +0x12 u16 | user channels |
| +0x14 u16 | hierarchy groups |
| +0x18 | self-relative offset: the base pose, a JointTransform per joint (0x30 bytes: rotation quaternion x y z w, translation x y z + pad, scale x y z + pad) |
| +0x20 | self-relative offset: joint name hashes (u32 each) |
| +0x24 | self-relative offset: the user channels' names (u32 each): parameter id << 4 \| component (0x330..0x333: DiffuseColor r g b a) |
| +0x28 | self-relative offset: the user channels' targets (u32 each): a material's hash |
| +0x40 | the hierarchy: groups of 4 (joint, parent) u16 pairs, parents first (groups padded with repeats); parent & 0x7fff >= 0x4000: a root (the figure's root transform); bit 0x8000 clear: scale compensation |

World matrices (`edgeAnimLocalJointsToWorldMatrices3x4`): world = parent world * local, except
that with compensation the child's rotation and scale use the parent's columns divided by the
parent's own local scale (its translation the parent's whole matrix). Skinning matrices
(`Figure::Pose`) = world * the geom's inverse bind, 3x4 row-major, the translation in the 4th
column: `MatrixPalette`'s rows. `tools/skel.py check`: in every release, 73.7% of the joints the
meshes skin to have base pose * inverse bind = identity - the base pose is the bind pose but where
a figure's is posed otherwise (effects and map pieces mostly: theirs is animated).

### Animations

Every animation in the game uses Edge's fixed formats (no packing specs at +0x4c; the decoder for
packed ones - per-channel bit specs: sign, exponent and mantissa widths per component - is in the
library but unused). From `_edgeAnimEvaluate` and the frame search it calls:

| Header | |
|---|---|
| +0x00 | `EA04` (`40AE` as bytes) |
| +0x04 f, +0x08 f | duration (s), sample rate (20 Hz) |
| +0x0c u16 | header size |
| +0x0e u16, +0x10 u16, +0x12 u16 | joints, frames, frame sets |
| +0x16..+0x24 u16 | channel counts: constant rotation, translation, scale, user; animated rotation, translation, scale, user |
| +0x34, +0x38 | self-relative: the frame sets' DMA array (u16, u16 size, u32 offset) and info (u16 start frame, u16 count) |
| +0x3c..+0x48 | self-relative: the constant rotation / translation / scale / user data |
| +0x60 | each channel's joint (u16), in the counts' order; the first table padded to 8 entries, the others to 4 |

A rotation is 6 bytes, the smallest three of a unit quaternion: three 15-bit values, (v - 16383.5)
/ 23169.768, the dropped one's index in the low 2 bits (rebuilt as sqrt(1 - the others' squares));
a translation or scale 3 little-endian floats.

A frame set covers its local frames 0 .. count: 8 u16 sizes (initial rotation, translation, scale,
user; key rotation, translation, scale, user - padded to 4), the initial values (local frame 0),
one keyframe bit per animated channel per frame (frames 1 .. count, MSB first; rotation channels,
then translation, scale, user), the keys (each channel's in turn), and - aligned to 16 - the next
set's header and initial values (local frame count + 1), so a set reaches the next one alone.
**Motions are relative to the figure's base pose**: their constant channels hold 0 /
identity where a joint stays put (19,238 constant translations are 0 where the base's isn't, none
equals the base; 1,078 constant rotations are identity, one equals the base's), so a motion
evaluated over an identity pose is composed onto the base as `edgeAnimBlendJointsRelative` does:
rotation base * delta (Hamilton product), translation base + delta, scale base * delta. Each
figure has a `<figure>.anim` with no channels (892 of them: the base layer). That is
Figure::Pose's blend tree: a figure has five animator slots - slot 0 the base layer, 1 + 2 and
3 + 4 cross-faded (op 0, linear) - and slot 0 is joined to the rest under a relative branch (ops
1 - 6: edgeAnimBlendPose's table). The characters', NPCs', objects' and boxes' motions are bound
to slots 1 / 2 (FldNPC, FldObject, FldBox, the motion managers). An animation alone in slot 0 -
a map's figure (FldMap::ChangeArea), the vista's map, camera and light, an event's scene - is the
tree's one leaf, **absolute**: evaluated over the base pose, its channels replacing the base's
values (edgeAnimProcessBlendTree; the cameras' animations start at their base poses).

User channels are floats (constant ones at +0x48, animated ones' initial values after the
scales', their keys after the scale keys aligned to 4) and absolute: MVGL's are material
parameters (the skeleton's user channel tables) - the UI's fades and colours (`title`'s
DiffuseColor is 0.01 placeholders in the file, the animations set it).

Between the keys P and Q around frame i + f: t = (i - P + f) / (Q - P); rotations slerp on the
short arc (lerp from a dot of 0.999), the rest lerp; channels without keys in the set run from
the initial value to the next set's. `tools/anim.py check`: in all three releases every frame set
is as its sizes say, every driven rotation is unit length, and all 10742..12340 boundaries are
continuous to 1e-8 (the Vita splits its animations into more frame sets).

## Text (`message\*.dat`)

From `EvtTextData` and `EvtWindowMessage::ReadPage`; `tools/msg.py check`: all 874 files in the
three releases, 75,490 of the 75,504 strings UTF-8 (the 14 others, all in `instructions.dat`,
start inside another string's character: offsets into a shared tail, for entries not shown in that
language).

| | |
|---|---|
| header | u32 records' offset (0x10), u32 strings' offset, u32 records, u32 0 |
| records | 0x20 bytes: 8 u32 offsets into the strings, one per language (`Cr3UtilOptionLanguage`); messages count from 1 |
| strings | UTF-8, NUL-terminated |
| trailer | optional (853 files): a u32 voice id per record, then `voic`, u32 count - the last 8 bytes |

A message's commands: `\f` the page ends, `\n` the next line, `\c` the line is centred, `\#`
digit the line's colour (1 red, 2 (0, 0.8, 1), 3 green, 4 (1, 0.8, 0), 5 magenta, 6 white, 7
black; else white), `\d` and 3 digits the speaker's tag (one of 21: `d001`..`d007`, `d101`..`d103`,
`d301`..`d312`, `d407`, `d509`, `d510`, `d999` - its index picks the name from the window's name
data), `\w` digits a wait, `\@` a wait mark (each page gets one at its end), `\*` the message
ends, `\m` a page flag, `\\` a backslash.

The fonts: Android's APK carries TypeBank Gothic Pro Bold and Regular (`assets/00000`, `00001`:
CFF OpenType), the Vita's `CR2/resources/font` the same Bold plus DynaFont faces for Chinese and
Korean (`DFGB_H9.ttc`, `DFT_C5.ttc`, `DFMINBD.TTF`); the iPad's release has none (iOS's own).
Android draws text in Java (`local.mediav.Text2Bitmap`): white, antialiased, the typeface by
index - 0 `00000` (Bold: Japanese), 1 `00001` (Regular: English), 2 the system's (the other
languages; MenuText::SetTextRender: the option's language 0 / 1, else 2) - into a bitmap the
advance's width (plus twice a stroke) by the font's top-to-bottom extent, the baseline (size - 1)
from its top.

## The interface

The menus and windows are figures too (MVGL::Interface::PartsBase), drawn through the camera of
`hud_camera2D` (InterfaceMain::Initialize: interface 11's contained camera): orthographic, aspect
1.5, size 0.48 - a 0.96 x 0.64 space, 3:2, the camera at z 0.881, depth +-2.56 - fitted to the
screen and centred (on Android's 1.87:1 game area: by its height; measured against the reference
video within a pixel). A figure's animators besides its own are relative to its base pose
(`vstWin_talk` moves the name line +0.512). Joints place things: `call_<figure>` another figure,
`type<size>_<align><slot>` text (Cr3UtilSearchTextPosition: size S 28, M 32, else 64 pixels of a
1000-a-unit layout, drawn 4 smaller - SetTextRender; align L, C, R; the line's left edge half the
size left of the joint, its middle on it - SetTextOffsetData).

The vista's message window (VistaConversationWindowMenu): `vstWin` - `GEO_vstWin`, a black band
from y -0.4 to -0.192 (past the foot of any screen), `GEO_nameLine` (`hud_vstMsgTC` / `A8`, the
name's bar) - stretched across screens wider than 3:2 (SetScale aspect / 1.5); its slots `01` the
speaker's name (`typeS_L01`), `11` / `12` a page's lines (`typeM_L11`, `L12`), `21` / `22`
centred ones; `sendCsr_loop` the page cursor at `call_sendCsr_loop`. A line types a character an
update (MenuText::Step: + speed, EvtWindow::UpdatePage's 1 - 0 while skipping); the name at once.

The cut-ins' message window (ConversationWindowMenu, Cr3Event's window 0): `msgWin` - its
open and close animators `msgWin_in` / `msgWin_out` - stretched as the vista's on screens wider
than 3:2, 0.0403125 lower on narrower ones; slots `01` the name (`typeS_L01`), `02`..`04` a page's
three lines (EvtCutinWindow::GetLinePerPage), `05`..`07` centred ones; `msgCsr_loop` its page
cursor. Behind it (depth -0.064 against -0.048) the portraits (CutInWindowMenu): `cutIn`, one
material whose colour map is the face (`cutInNNN_LMM`, 512 x 256, set by EvtFace.SetTexture /
ChangeTexture) and whose DiffuseColor is its brightness (SetColor: 0.5 for who isn't speaking);
five layouts (EvtFace.Create 0..4: interfaces 24, 23, 22, 99, 100), each its io animation -
`cutInR_io`, `cutInLR_io`, `cutInL_io`, `cutInPty_io`, `cutInSgl_io`, 0.2 s, forward on opening
and back from where it is on closing - with the other side's `GEO_face*_L` / `_R` joints hidden.
On screens wider than 3:2 a one-sided layout moves toward its side by the margin over the 3:2
area's width (PositionCorrection: int((W - s 960) / 2) / (s 960), s = min(W / 960, H / 640); the
fit parameter's side - Cr3UtilGetFitXOffset's 3 x 3 table, 1 / 4 / 7 left, 3 / 6 / 9 right).

The narration (NarrationWindowMenu): `narrationPos`, no meshes - ten rows 0.048 apart, columns
`typeM_C01`..`C10` (x 0), `L11`..`L20` (-0.368), `R21`..`R30` (0.368), the cursor at
`call_sendCsr_loop50` - its text over the screen (the scripts fade it to black first). A page's
line i takes slot i (EvtWindow::ShowMessage; with a name tag, i + 1), always in the centred
column (SetStringData's alignment 1); the messages put blank lines first to set where it sits
(the prologue's: four, then the line at C05, the screen's middle). The windows' depths put them
in front of the fade windows' (GetWindowDepth: their z).

## Battles (`data\battledata.dat`)

BtlData's tables. The header: u32 offsets - +0x00 the characters, +0x04 the party, +0x08 the
monsters, +0x0c the couplings (encounters), +0x10 the areas, +0x14 the commands, ..., +0x30 the
strings - then s16 counts from +0x34 (characters 9, monsters 274, couplings 355 - one past the
records before the areas -, areas 63, commands 462, ...). A record's names are u32 offsets into
the strings, one per language (Android / iPad / Vita alike: +0 Japanese, +4 English).

| Record | Size | Fields |
|---|---|---|
| character | 0x98 | +0x08 its battle figure (`j001` Darwin .. `j007`; 9 `c102` Piupiu) |
| monster | 0x110 | +0x10 its figure - four characters - and colour (`m206a`: `m206`, images `m206a01`); +0x1c HP (GetMonsterHP: + 25 a level); +0x53 size (0 small, 1 medium, 2 large, 3 extra large, 4 human-sized) |
| coupling | 0x1c | +0x00 music; +0x02 the map (BtlMapTable, from 1: `d0151b` .. `d1154v`); +0x04 a flag; +0x08 three monsters (0 none) |

`BattleIN(id)` fights coupling `id` (FldEncount::SetBattle). The stage (BtlMainCtrl::StepInit):
the map's figure, its own lights (BtlMap: RenderContaindLights) with the directional one turned
to come from the camera (BtlMain::SetLights); the monsters' sizes pick a pattern
(BtlWork::SetSizePattern over BtlCouplingTable: S, SS, SSS, M, MS, MSS, MM, L, XL, XLXL, Hsolo,
Hpair; m310 its own, `neron`), the pattern where each stands - a joint of `battledpos`
(BtlDposTable: `dPos_sssL`, `dPos_mC`, ...; its names only hashed, no `.name`) whose place and
turn the figure takes (BtlModel::SetPosRot) - the party a pair on `dPos_pPairA` / `B`. They start
on their first motions (BtlMotionTable's 25: 0 `bb01` the stance, 1 `bb02`, `bg01`, `bd01`, ...,
`bz03`; the party bb02 then bb01). The cameras are `BattleCamera_move`'s and `_nomove`'s by name
(`Cam_Start_<pattern>_00` as it opens, `Cam_Getset_<pattern>_00` while commands are chosen,
`Cam_Win_*`, ...: BtlCameraCtrl::Start; near halved - at least 0.1 - far five times, at most
5000). `cr2tool battle <archive> all`: the 340 couplings with monsters set up, everything they use
in the archive, in all three releases.

The battle's interface (BtlInterface): the party's windows (BattleStatusMenu) `pc01win` /
`pc02win`, their `call_` joints placing `btlHpBar_per` / `btlMpBar_per` (posed at HP x 5 / max
seconds, at least 0.07: its `JNT_percent` moves an alpha-0 mask over the lost part, the depth test
hiding the gradient behind it), `btlFace` (images `btlFaceC00<n>` / `btlFaceA00<n>` alpha, n the
character), the labels (StatusId2: `hud_statusAI` by StatusIdParamPackTable, the joint
`call_status_frNN` naming the sprite - 01 HP, 02 MP) and the numbers (`pc0NhpNum` / `mpNum`'s
`call_dotNum_num1`: DotNumId2, five / four digits of DotNumParamPackTable's set 90 from
`hud_dotnum`, right to left, w / 1000 apart); the break gauge (BreakWindowMenu) `brkWin`,
`brkGauge` (posed at (gauge + 4) / 2 seconds), `brkInfo` (`brkInfoEV_io` / `PL` / `EN`); the
command's name in `btlMsg` (`typeM_C01`, `btlMsg_in`); the damage (DamageInfo00) `btlNum` with
`btlNum_fgrN` for N digits, a digit of set 20 (30, 40, 50, 60 for the other kinds:
damageinfodata01) at each `call_dotNum_idNN`, id01 the ones. An interface figure's own animation
(SetParameterDataBase's, alone in slot 0) is absolute; the animators added beside it relative.

## Sound

MVGL::Sound (over OpenSL ES on Android). 37 music tracks (`Sound/bgm01` .. `bgm37`) and two banks,
the effects (`Sound/sndeff`) and the voices (`Sound/voice`).

- **Where.** The Android OBB holds them after its MDB1 archive (which ends at 391 MB): the
  library's `obbtable_pos` / `obbtable_size` (40 u32 each: MVGL::Utilities::Fios::GetObbPos /
  GetObbSize) - 0 the archive, 1..37 the tracks, 0x26 sndeff, 0x27 voice; each piece at a 4 KiB
  boundary. The iPad keeps the same banks as files (`Sound.ios/sound/sndeff.bin`, `voice.bin` -
  byte for byte the OBB's) and its music as AAC in CAF (`bgmNN.caf`).
- **A track** (Android): an `MCA_` header of 0x400 bytes, then Ogg Vorbis - 44.1 kHz stereo,
  46 s .. 5 min. It loops back to its repeat frame at its end (Cr3UtilSoundPlayBGM:
  Cr3SoundSetRepeatFrame from the library's table, a sample; bgm27..29's lie past their ends:
  once). `cr2tool sound` decodes them all.
- **The iPad's tracks**: AAC-LC in CAF, 48 kHz stereo. The chunks are `desc`, `kuki` (an ESDS
  holding the AudioSpecificConfig), `pakt` (the packets' sizes, 7-bit big-endian runs, after the
  valid frames, the priming of 2112 and the remainder) and `data` (after a u32 edit count).
  - The iPad executable has its own repeat table (its Cr3UtilSoundPlayBGM's, `kBgmLoopIpad`). It
    counts AAC packets of 1024 samples, -1 for bgm27..29.
  - Packet p is p x 1024 less the priming in the decoded stream. Its 2000832 for bgm01 is the
    Vita's 2003653: the Vita's content runs some 2,400..3,000 frames later than the iPad's.
- **The Vita's** (`Sound.psp2/sound/`): SXD files (MVGL::Sound's PSP2 build) - `bgmNN.sxd` the
  music; `sndeff.sxd` the effects, `voice.sxd1` the voices' entries over `voice.sxd2` (their
  streams after a 0x60-byte "SXDS" header). Cr3UtilSoundPlaySE(n) / PlayVoice(n) play wave n
  (Cr3SoundPlay_Package): 391 effects and 3249 voices, Android's first 391 / 3249 in order.
  - An SXD is "SXDF", a 0x60-byte header (+0x20 its name), then chunks {char[4] id, u32 size,
    body}: REQD, NAME, WAVE, DATA.
  - WAVE: +4 its entries' count, then a u32 each from +8, the entry that far from it.
  - An entry: +0 flags (bit 1: streamed - +0x24 an offset into the `.sxd2`); +4 u8 the codec
    (0x42 ATRAC9, 0x21 HE-VAG) and +5 the channels; +8 the rate; +0x14 the frames;
    +0x18 / +0x1c the loop's start and end, both inclusive (Android's repeat frame at 48 kHz);
    +0x20 the stream's size; +0x24 its offset from there; +0x2c the ATRAC9 config as a
    little-endian word (0xFE7409F0: 48 kHz stereo, superframes of 1024 samples).
  - The music's tracks run on past their loop's end.
  - The effects: HE-VAG, 16-byte frames of 28 samples (a stereo wave's channels a frame each in
    turn), mostly 44.1 kHz where Android's are 22.05 kHz. The 33 stereo ones' left channel is
    Android's clip. The voices: ATRAC9 mono, 48 kHz (config 0xFE7005F0).
- **A bank** (CPackage): a table of {u32 offset, u32 size} - its length the first offset (394
  effects, 3250 voices) - over headerless signed 16-bit mono PCM at 22050 Hz
  (COpenSLESAudioPlayer::initialize(2, 1, 22050000 mHz)). Cr3UtilSoundPlaySE(n) /
  Cr3UtilSoundPlayVoice(n) play entry n (none at a volume of 0).
- **The game's numbers.** The scripts' Cr3Sound (PlayBGM(track, frames), StopBGM(frames),
  ReleaseBGM, GetState(0: the music's - 2 playing, -2 none), PlaySE(clip), PlaySE_Repeat(clip:
  looping, its slot), StopSE(slot, frames), StopSEAll, PlayVoice(clip), StopVoiceAll) take clips
  and tracks directly; a fade's frames are the game's updates (CSndNode::_FadeVolumeProc, 20 a
  second). A message's voice is its text's trailer id (EvtWindow::PlayVoice as its page starts).
  The battles' (BtlSound) go through tables (tools/gen_sound_table.py -> audio/soundtable.inc):
  PlaySE(n) a clip from a 300-entry s16 table; a voice by Cr3VoiceTable(figure, kind) - 22 figures'
  first four characters ("j001", "m309") x 1000 kinds -, the kind one of three at random from
  battledata's voice records (+0x2c, +0x46 of them: BtlSound::PlayVoiceFromData). The menus' own:
  0xb4 decide, 0xb5 back.
