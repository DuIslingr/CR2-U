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
| `name` | 1259 | a string table: count, ?, then offsets to NUL-terminated names (node / camera names) |
| `dat` | 880 | tables: offsets then records (`message\*.dat`: 874 of them, the text) |
| `shad` | 466 | a GLSL ES shader's source (`shaders\<4 hex words>_fp` / `_vp`) |
| `phys` | 419 | collision / physics (Bullet); magic `PHYS` |
| `nut` | 139 | a compiled Squirrel script: `0xfafa`, `SQIR`, char / int / float sizes, then `PART`-separated parts; the source name (`D12_01_0206_evt.i`) and `main` |
| `emta` | 3 | effect parameters |

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
| +0x10 u16 | relocations (the list at +0x48: offsets of pointers to make absolute) |
| +0x18 | the bounds |
| +0x30 u32 | the meshes (0x54 bytes each) |
| +0x34 u32 | the materials |
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
joints (`MatrixPalette[56*3]`).

A material: u32 hash, ..., +0x14 u8 parameters, +0x15 u8 render states, then 0x14-byte entries:
u8 parameter, u8 kind (0 texture, 1 / 3 / 4 a float / vec3 / vec4, 100 a render state), u16, then
the texture's name (12 characters: `images\<name>.img`) or the values. Parameters seen: 0x32
diffuse map, 0x35 normal map, 0x33 colour (vec4), 0x36 / 0x38 / 0x39 / 0x41 / 0x42 floats, 0x3e /
0x3f / 0x40 vec3s.

A render state's entry holds 4 u32 GL values; the ids are `RenderContext`'s table (its names, in
order from 0x78): `alpha_func` (func, ref as a float - GREATER), `alpha_test_enable`,
`blend_func` (src, dst), `blend_equation`, `blend_enable`, `cull_face`, `cull_face_enable`,
`depth_func`, `depth_mask`, `depth_test_enable`, `polygon_offset`, `polygon_offset_fill_enable`,
`color_mask`, `stencil_func`, `stencil_mask`, `stencil_op`, `stencil_test_enable` (0x88). Those
a material leaves out keep the defaults: no alpha test, no blending, back faces culled, depth test
and writes on. The textures' alpha is not transparency unless the material tests or blends (the
characters' carries other data).

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
**The animations are relative to the figure's base pose**: their constant channels hold 0 /
identity where a joint stays put (19,238 constant translations are 0 where the base's isn't, none
equals the base; 1,078 constant rotations are identity, one equals the base's), so an animation
evaluated over an identity pose is composed onto the base as `edgeAnimBlendJointsRelative` does:
rotation base * delta (Hamilton product), translation base + delta, scale base * delta. Each
figure has a `<figure>.anim` with no channels (892 of them: the base layer).

Between the keys P and Q around frame i + f: t = (i - P + f) / (Q - P); rotations slerp on the
short arc (lerp from a dot of 0.999), the rest lerp; channels without keys in the set run from
the initial value to the next set's. `tools/anim.py check`: in all three releases every frame set
is as its sizes say, every driven rotation is unit length, and all 10742..12340 boundaries are
continuous to 1e-8 (the Vita splits its animations into more frame sets).
