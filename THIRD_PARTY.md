# Third-party code

CR2-U's own code is licensed under the GNU General Public License, version 2 or (at your option) any
later version - see `LICENSE`. These libraries are included under their own licences, kept beside
them in `engine/third_party/`:

| Folder | What | Licence |
|---|---|---|
| `fidelityfx/` | AMD FSR SDK 2.4.0's FSR 3.1.6 GPU passes (SDK 1.1.4's GLSL core) (as in CR1:U / CRO-U) | MIT (`LICENSE.txt`) |
| `ffx/` | AMD FSR SDK 2.4.0's FSR API headers: the interface of AMD's Vulkan DLLs (FSR 4; the DLLs aren't here) | MIT (each file's header) |
| `stb/stb_vorbis.c` | stb_vorbis 1.22 by Sean Barrett (the copy CR1:U vendors) - decodes the Android release's music (Ogg Vorbis) | public domain / MIT (in the file) |
| `cgltf/` | cgltf 1.13 by Johannes Kuhlmann - reads the HD models (glTF 2.0) | MIT (in the file) |
| `stb/stb_image.h`, `stb/stb_image_write.h` | stb_image 2.30 (PNG and JPEG only) and stb_image_write 1.16 by Sean Barrett - the HD models' pictures, read and written (`gltf_libs.c` builds them with cgltf) | public domain / MIT (in the files) |
| `faad2/` | FAAD2 2.11.1's `libfaad` (the copy CR1:U vendors) - decodes the iPad release's music (AAC in CAF) when the system has no FAAD2. Code from FAAD2 is copyright (c) Nero AG, www.nero.com | GPL-2.0-or-later (`COPYING`) |
| `libatrac9/` | LibAtrac9 by Alex Barney, commit efca2e3 (the copy CR1:U vendors) - decodes the Vita release's music (ATRAC9 in SXD) | MIT (`LICENSE`) |
| `hevag/` | The HE-VAG prediction filter table from vgmstream (the copy CR1:U vendors) - the Vita release's sound effects | ISC (`COPYING`) |
| `squirrel2/` | Squirrel 2.2.4 stable by Alberto Demichelis (the VM and its standard library, unmodified; `squirrel_2.2.4_stable.tar.gz` from the Squirrel project on SourceForge) - the version the game embeds, which runs its compiled scripts | zlib (`COPYRIGHT`) |

The first start's setup decrypts a PS Vita NoNpDrm dump itself. Its PFS decryption (the dump's
files, `engine/src/assets/vita_pfs.cpp`) is the project's own code, written from a functional
description of the format, on the project's own crypto (`engine/src/crypto/`: AES, SHA-1, SHA-256,
HMAC). Its SELF decryption (the game's program, `engine/src/viewer/setup.cpp`) follows Vita3K's
`packages/sce_utils.cpp` (github.com/Vita3K/Vita3K) and uses its retail app keys -
GPL-2.0-or-later, as the port.

Both embed PS Vita console keys: the SELF decryption's retail app keys, and the PFS decryption's
fixed unwrap key (the F00D security processor's `key_id` 0 key, which turns a dump's klicensee
into its data key). They are not the project's work and not secrets it holds: the Vita scene
extracted them from the console's F00D processor years ago, and they are published in many
emulators and tools (Vita3K, VitaSharp, vitaslop, rom-converto and others). They are facts, used
only to read a copy of the game its owner supplies; the port ships no game data.

Built from source by CMake when the system has none (fetched at build time, not in the
repository; in the CI artifacts):

| What | Licence |
|---|---|
| [SDL 3.4.20](https://github.com/libsdl-org/SDL) (linked in statically; on Windows its development branch at 59ccf612) | zlib |
| [zlib 1.3.1](https://github.com/madler/zlib) (Windows, Android) | zlib |
| [libarchive 3.7.7](https://github.com/libarchive/libarchive) (Windows, macOS: the setup reads a Vita dump's .rar, .7z, .zip, .vpk) | BSD-2-Clause |
| [FreeType 2.13.3](https://gitlab.freedesktop.org/freetype/freetype) (Windows, macOS, Android: the text) | FreeType License or GPL-2.0 - used here under the GPL |

Carried by the macOS `.app` (Contents/Frameworks, from the LunarG Vulkan SDK; their licence texts in
Contents/Resources/licenses): the [Vulkan loader](https://github.com/KhronosGroup/Vulkan-Loader) and
[MoltenVK](https://github.com/KhronosGroup/MoltenVK), both Apache-2.0. Apache-2.0 meets the GPL in
its version 3, which "or any later version" allows: a `.app` with them is under GPL-3.0.

Not included: the **NVIDIA DLSS SDK** (proprietary, NVIDIA RTX SDKs licence). DLSS support
builds only with the SDK's headers and NGX library: `-DCR_DLSS_SDK=<a checkout of
github.com/NVIDIA/DLSS>` (Linux x86-64, Windows x64 with MSVC), or its `include/` and
`lib/libnvsdk_ngx.a` in `engine/third_party/dlss/`; without it the build has FSR 3.1 only
(and MetalFX on macOS). A build with DLSS links proprietary code into a GPL program, so it is for
your own use, not for redistribution - CI builds it to prove it, and uploads only the builds
without it.

No game data is included or distributed: *Chaos Rings II* and its assets are the property of
Square Enix and Media.Vision. The port loads them at runtime from a copy you own.
