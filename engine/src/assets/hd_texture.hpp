#pragma once
// HD textures: upscaled replacements for the game's textures, made offline (tools/hd_textures.py:
// waifu2x or Real-ESRGAN) and kept outside the game data - CR1:U's scheme and files. Each is found
// by the hash of the original's decoded pixels, so the same picture in several releases, or in
// several of the archive's images, gets one replacement without a name table.
//
// <hash>.crraw: an original as exported for upscaling ("CRRAW1", u32 width, height, flags (bit 0
//               alpha-only; bits 1-2 how the release stored it: imageStorage), then RGBA8 rows)
// <hash>.crhd:  its replacement ("CRHD1\0", u32 width, height, original width, original height,
//               flags, zlib-compressed RGBA8 rows)
// The rows in the Image's own order (the game's texture coordinates': v = 0 the first row).

#include "assets/image.hpp"

#include <cstdint>
#include <string>

namespace cr2 {

// How the release stored a picture, which picks the upscaler (tools/hd_textures.py): 1 lossless (8
// bits a channel: RGBA8888, I8, AI88, A8, the Vita's U8...), 2 block-compressed (PVRTC, ETC1, the
// Vita's UBC: block noise), 3 16-bit (RGBA4444 / 5551 / RGB565: dithered or banded), 0 unknown
int imageStorage(const Image& img);

// 64-bit FNV-1a over the size, the alpha-only flag and the pixels
uint64_t imageHash(const Image& img);
std::string hashName(uint64_t hash); // 16 hex digits

bool saveRawImage(const std::string& path, const Image& img);
// The replacement for `original` in `dir` (with its trailing slash), if there is one: same
// original size, larger or equal size
bool loadHdImage(const std::string& dir, const Image& original, Image& out);

} // namespace cr2
