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
| `anim` | 3514 | animation; magic `EA04` (`40AE` as bytes) |
| `img` | 1873 (Android 1605, Vita 1950) | a texture: iPad a PVR v2 file (52-byte header, `PVR!` at 0x2c; PVRTC 2 / 4 bpp mostly, flags 0x18 / 0x19, some RGBA4444 / 5551 / 8888 / 565 0x10..0x13); Android PVR v3 (`PVR\x03`; `rgba` 8888 / 4444, `la` 88, ...) and a few v2; Vita GXT (as CR1 / Omega's Vita releases) |
| `geom` | 1724 | mesh (no magic: a header of counts and a bounding box) |
| `skel` | 1724 | skeleton; magic `ES02` |
| `name` | 1259 | a string table: count, ?, then offsets to NUL-terminated names (node / camera names) |
| `dat` | 880 | tables: offsets then records (`message\*.dat`: 874 of them, the text) |
| `shad` | 466 | a GLSL ES shader's source (`shaders\<4 hex words>_fp` / `_vp`) |
| `phys` | 419 | collision / physics (Bullet); magic `PHYS` |
| `nut` | 139 | a compiled Squirrel script: `0xfafa`, `SQIR`, char / int / float sizes, then `PART`-separated parts; the source name (`D12_01_0206_evt.i`) and `main` |
| `emta` | 3 | effect parameters |
