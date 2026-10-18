# Third-party code

CR2-U's own code is licensed under the GNU General Public License, version 2 or (at your option) any
later version - see `LICENSE`. These libraries are included under their own licences, kept beside
them in `engine/third_party/`:

| Folder | What | Licence |
|---|---|---|
| `fidelityfx/` | AMD FidelityFX SDK 1.1.4's FSR 3.1 GPU passes (as in CR1:U / CRO-U) | MIT (`LICENSE.txt`) |
| `stb/stb_vorbis.c` | stb_vorbis 1.22 by Sean Barrett (the copy CR1:U vendors) - decodes the Android release's music (Ogg Vorbis) | public domain / MIT (in the file) |
| `faad2/` | FAAD2 2.11.1's `libfaad` (the copy CR1:U vendors) - decodes the iPad release's music (AAC in CAF) when the system has no FAAD2. Code from FAAD2 is copyright (c) Nero AG, www.nero.com | GPL-2.0-or-later (`COPYING`) |
| `libatrac9/` | LibAtrac9 by Alex Barney, commit efca2e3 (the copy CR1:U vendors) - decodes the Vita release's music (ATRAC9 in SXD) | MIT (`LICENSE`) |
| `squirrel2/` | Squirrel 2.2.4 stable by Alberto Demichelis (the VM and its standard library, unmodified; `squirrel_2.2.4_stable.tar.gz` from the Squirrel project on SourceForge) - the version the game embeds, which runs its compiled scripts | zlib (`COPYRIGHT`) |

Not included: the **NVIDIA DLSS SDK** (proprietary). Its support builds only when you place the
SDK's `include/` and `lib/libnvsdk_ngx.a` in `engine/third_party/dlss/` yourself; a build with it
links proprietary code into a GPL program, so it is for your own use, not for redistribution.

No game data is included or distributed: *Chaos Rings II* and its assets are the property of
Square Enix and Media.Vision. The port loads them at runtime from a copy you own.
