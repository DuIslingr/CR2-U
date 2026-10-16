#pragma once
// The game's textures (`images\*.img`; docs/FORMATS.md), to 8-bit RGBA, the top mip level:
//   iPad     a legacy PVR (v2) file: 52-byte header, `PVR!` at 0x2c; OGL_* pixel types (PVRTC 4bpp
//            mostly, RGBA 4444 / 5551 / 8888, RGB 565 / 888, I8, AI88, A8)
//   Android  a PVR v3 file (ETC1 mostly, rgba 5551 / 4444 / 8888, la 88), a few legacy ones
//   Vita     a Sony GXT file: swizzled UBC1 / UBC2 (DXT1 / DXT3) mostly, U8U8 (GRRR: luminance and
//            alpha), U8 (R000: alpha only), U8U8U8U8

#include <cstdint>
#include <string>
#include <vector>

namespace cr2 {

struct Image {
    int width = 0, height = 0;
    // 4 bytes a pixel, the rows in the file's order - the order the game uploads them in, so the
    // first row is at texture v = 0
    std::vector<uint8_t> rgba;
    bool alphaOnly = false;    // (A8 / U8 R000: a mask, no colour of its own)
    bool bottomUp = false;     // the first row is the picture's bottom (PVR's flip flag / orientation)
    std::string format;        // for tools: "PVR2 PVRTC4", "PVR3 ETC1", "GXT UBC2", ...
    bool mipmaps = false;      // upload with a mip chain
};

bool decodeImg(const std::vector<uint8_t>& file, Image& out, std::string* error = nullptr);

// single-format decoders (for tests): `src` holds the whole top level
void decodeEtc1(const uint8_t* src, int w, int h, uint8_t* rgba);
void decodePvrtc4(const uint8_t* src, int w, int h, uint8_t* rgba);
void decodeBc(const uint8_t* src, int w, int h, int bc, bool vitaSwizzle, uint8_t* rgba); // bc 1 / 2 / 3

} // namespace cr2
