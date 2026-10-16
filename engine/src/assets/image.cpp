#include "assets/image.hpp"

#include <algorithm>
#include <cstring>

namespace cr2 {

namespace {

uint32_t rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
uint16_t rd16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
// n-bit -> 8-bit by bit replication
uint8_t rep(uint32_t v, int bits) {
    uint32_t out = 0;
    for (int have = 0; have < 8; have += bits) out = (out << bits) | v;
    return uint8_t(out >> ((8 + bits - 1) / bits * bits - 8));
}

bool fail(std::string* error, const char* what) {
    if (error) *error = what;
    return false;
}

// ------------------------------------------------------------------ PVRTC 4bpp (CR1:U's decoder)

uint32_t twiddle(uint32_t bx, uint32_t by, uint32_t bw, uint32_t bh) {
    const uint32_t minDim = bw < bh ? bw : bh;
    uint32_t out = 0;
    int shift = 0;
    for (uint32_t bit = 1; bit < minDim; bit <<= 1, ++shift) {
        out |= ((by >> shift) & 1) << (2 * shift);
        out |= ((bx >> shift) & 1) << (2 * shift + 1);
    }
    const uint32_t rest = (bh > bw ? by : bx) >> shift;
    return out | (rest << (2 * shift));
}

void pvrtcColours(uint32_t c, uint8_t* a, uint8_t* b) {
    if (c & 0x8000) { // colour A opaque: RGB 554
        a[0] = rep((c >> 10) & 31, 5);
        a[1] = rep((c >> 5) & 31, 5);
        a[2] = rep((((c >> 1) & 15) << 1) | ((c >> 4) & 1), 5);
        a[3] = 255;
    } else {          // colour A translucent: ARGB 3443
        a[0] = rep((c >> 8) & 15, 4);
        a[1] = rep((c >> 4) & 15, 4);
        a[2] = rep((c >> 1) & 7, 3);
        a[3] = rep((c >> 12) & 7, 3);
    }
    if (c & 0x80000000u) { // colour B opaque: RGB 555
        b[0] = rep((c >> 26) & 31, 5);
        b[1] = rep((c >> 21) & 31, 5);
        b[2] = rep((c >> 16) & 31, 5);
        b[3] = 255;
    } else {               // colour B translucent: ARGB 3444
        b[0] = rep((c >> 24) & 15, 4);
        b[1] = rep((c >> 20) & 15, 4);
        b[2] = rep((c >> 16) & 15, 4);
        b[3] = rep((c >> 28) & 7, 3);
    }
}

// ------------------------------------------------------------------ the Vita's swizzle

// GXT swizzled order (as CR1:U's gxt.cpp): Morton order with the y bit lowest within square tiles
// of the smaller side, the tiles in row order. For block formats it applies to the 4x4 blocks
size_t vitaSwizzle(int x, int y, int w, int h) {
    const int n = w < h ? w : h;
    const int lx = x % n, ly = y % n;
    size_t m = 0;
    for (int b = 0; (1 << b) < n; ++b) m |= size_t((ly >> b) & 1) << (2 * b) | size_t((lx >> b) & 1) << (2 * b + 1);
    return (size_t(y / n) * size_t(w / n) + size_t(x / n)) * size_t(n) * n + m;
}

void colour565(uint16_t c, uint8_t* o) {
    o[0] = rep(c >> 11, 5);
    o[1] = rep((c >> 5) & 63, 6);
    o[2] = rep(c & 31, 5);
    o[3] = 255;
}

void bcColourBlock(const uint8_t* b, bool alwaysFour, uint8_t (*px)[4]) {
    const uint16_t c0 = rd16(b), c1 = rd16(b + 2);
    uint8_t pal[4][4];
    colour565(c0, pal[0]);
    colour565(c1, pal[1]);
    if (c0 > c1 || alwaysFour) {
        for (int k = 0; k < 3; ++k) {
            pal[2][k] = uint8_t((2 * pal[0][k] + pal[1][k] + 1) / 3);
            pal[3][k] = uint8_t((pal[0][k] + 2 * pal[1][k] + 1) / 3);
        }
        pal[2][3] = pal[3][3] = 255;
    } else {
        for (int k = 0; k < 3; ++k) pal[2][k] = uint8_t((pal[0][k] + pal[1][k]) / 2), pal[3][k] = 0;
        pal[2][3] = 255;
        pal[3][3] = 0;
    }
    const uint32_t idx = rd32(b + 4);
    for (int i = 0; i < 16; ++i) std::memcpy(px[i], pal[(idx >> (2 * i)) & 3], 4);
}

// a decoded pixel of a plain (uncompressed) format, from its bits: channel order R G B A
struct Packed {
    int bytes;
    int shift[4], bits[4]; // per channel (R, G, B, A); bits 0: the channel is absent
};

void decodePacked(const uint8_t* src, int w, int h, const Packed& f, bool lumAlpha, bool alphaOnly, uint8_t* out) {
    for (size_t i = 0, n = size_t(w) * h; i < n; ++i) {
        uint32_t v = 0;
        std::memcpy(&v, src + i * size_t(f.bytes), size_t(f.bytes));
        uint8_t* o = out + i * 4;
        uint8_t c[4] = {0, 0, 0, 255};
        for (int k = 0; k < 4; ++k)
            if (f.bits[k]) c[k] = rep((v >> f.shift[k]) & ((1u << f.bits[k]) - 1), f.bits[k]);
        if (lumAlpha) c[1] = c[2] = c[0];
        if (alphaOnly) c[0] = c[1] = c[2] = 255;
        std::memcpy(o, c, 4);
    }
}

} // namespace

void decodePvrtc4(const uint8_t* src, int w, int h, uint8_t* dst) {
    const int bw = w / 4 < 2 ? 2 : w / 4, bh = h / 4 < 2 ? 2 : h / 4;
    std::vector<uint8_t> colA(size_t(bw) * bh * 4), colB(size_t(bw) * bh * 4);
    std::vector<uint32_t> mod(size_t(bw) * bh), col(size_t(bw) * bh);
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            const uint8_t* blk = src + size_t(twiddle(uint32_t(bx), uint32_t(by), uint32_t(bw), uint32_t(bh))) * 8;
            const size_t i = size_t(by) * bw + bx;
            mod[i] = rd32(blk);
            col[i] = rd32(blk + 4);
            pvrtcColours(col[i], &colA[i * 4], &colB[i * 4]);
        }
    static const int kStd[4] = {0, 3, 5, 8};
    static const int kPunch[4] = {0, 4, 4, 8};
    for (int y = 0; y < h; ++y) {
        const int fy = y - 2; // block colours sit at block centres
        const int y0 = ((fy >> 2) % bh + bh) % bh, y1 = (y0 + 1) % bh, ty = fy & 3;
        for (int x = 0; x < w; ++x) {
            const int fx = x - 2;
            const int x0 = ((fx >> 2) % bw + bw) % bw, x1 = (x0 + 1) % bw, tx = fx & 3;
            const int w00 = (4 - tx) * (4 - ty), w10 = tx * (4 - ty), w01 = (4 - tx) * ty, w11 = tx * ty;
            const size_t i00 = (size_t(y0) * bw + x0) * 4, i10 = (size_t(y0) * bw + x1) * 4;
            const size_t i01 = (size_t(y1) * bw + x0) * 4, i11 = (size_t(y1) * bw + x1) * 4;
            const size_t bi = size_t(y >> 2) * bw + (x >> 2);
            const int m = (mod[bi] >> (2 * ((y & 3) * 4 + (x & 3)))) & 3;
            const bool punch = col[bi] & 1;
            const int wt = punch ? kPunch[m] : kStd[m];
            uint8_t* o = dst + (size_t(y) * w + x) * 4;
            for (int c = 0; c < 4; ++c) {
                const int a = colA[i00 + c] * w00 + colA[i10 + c] * w10 + colA[i01 + c] * w01 + colA[i11 + c] * w11;
                const int b = colB[i00 + c] * w00 + colB[i10 + c] * w10 + colB[i01 + c] * w01 + colB[i11 + c] * w11;
                o[c] = uint8_t((a * (8 - wt) + b * wt + 64) / 128);
            }
            if (punch && m == 2) o[3] = 0;
        }
    }
}

void decodeEtc1(const uint8_t* src, int w, int h, uint8_t* out) {
    static const int kMod[8][4] = {{2, 8, -2, -8},     {5, 17, -5, -17},   {9, 29, -9, -29},   {13, 42, -13, -42},
                                   {18, 60, -18, -60}, {24, 80, -24, -80}, {33, 106, -33, -106}, {47, 183, -47, -183}};
    const int bw = (w + 3) / 4, bh = (h + 3) / 4;
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            const uint8_t* b = src + (size_t(by) * bw + bx) * 8;
            const uint32_t hi = uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3];
            const uint32_t lo = uint32_t(b[4]) << 24 | uint32_t(b[5]) << 16 | uint32_t(b[6]) << 8 | b[7];
            int base[2][3];
            if (hi & 2) { // differential: 5-bit base + signed 3-bit delta
                for (int k = 0; k < 3; ++k) {
                    const int c = int(hi >> (27 - 8 * k)) & 31;
                    int d = int(hi >> (24 - 8 * k)) & 7;
                    if (d >= 4) d -= 8;
                    base[0][k] = rep(uint32_t(c), 5);
                    base[1][k] = rep(uint32_t(std::clamp(c + d, 0, 31)), 5);
                }
            } else { // individual: two 4-bit colours
                for (int k = 0; k < 3; ++k) {
                    base[0][k] = int((hi >> (28 - 8 * k)) & 15) * 17;
                    base[1][k] = int((hi >> (24 - 8 * k)) & 15) * 17;
                }
            }
            const int table[2] = {int(hi >> 5) & 7, int(hi >> 2) & 7};
            const bool flip = hi & 1;
            for (int x = 0; x < 4; ++x)
                for (int y = 0; y < 4; ++y) {
                    const int px = bx * 4 + x, py = by * 4 + y;
                    if (px >= w || py >= h) continue;
                    const int i = x * 4 + y; // pixel indices run down the columns
                    const int sel = int((lo >> (i + 16)) & 1) << 1 | int((lo >> i) & 1);
                    const int sub = flip ? (y >= 2) : (x >= 2);
                    const int m = kMod[table[sub]][sel == 0 ? 0 : sel == 1 ? 1 : sel == 2 ? 2 : 3];
                    uint8_t* o = out + (size_t(py) * w + px) * 4;
                    for (int k = 0; k < 3; ++k) o[k] = uint8_t(std::clamp(base[sub][k] + m, 0, 255));
                    o[3] = 255;
                }
        }
}

void decodeBc(const uint8_t* src, int w, int h, int bc, bool swizzled, uint8_t* out) {
    const int bw = (w + 3) / 4, bh = (h + 3) / 4, bytes = bc == 1 ? 8 : 16;
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            const size_t idx = swizzled ? vitaSwizzle(bx, by, bw, bh) : size_t(by) * bw + bx;
            const uint8_t* b = src + idx * size_t(bytes);
            uint8_t px[16][4];
            bcColourBlock(bc == 1 ? b : b + 8, bc != 1, px);
            if (bc == 2) { // explicit 4-bit alpha
                for (int i = 0; i < 16; ++i) px[i][3] = uint8_t(((b[i / 2] >> (4 * (i & 1))) & 15) * 17);
            } else if (bc == 3) { // interpolated alpha
                const int a0 = b[0], a1 = b[1];
                int pal[8] = {a0, a1};
                for (int k = 2; k < 8; ++k)
                    pal[k] = a0 > a1 ? ((8 - k) * a0 + (k - 1) * a1) / 7 : k < 6 ? ((6 - k) * a0 + (k - 1) * a1) / 5 : k == 6 ? 0 : 255;
                uint64_t bits = 0;
                for (int k = 0; k < 6; ++k) bits |= uint64_t(b[2 + k]) << (8 * k);
                for (int i = 0; i < 16; ++i) px[i][3] = uint8_t(pal[(bits >> (3 * i)) & 7]);
            }
            for (int i = 0; i < 16; ++i) {
                const int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x < w && y < h) std::memcpy(out + (size_t(y) * w + x) * 4, px[i], 4);
            }
        }
}

bool decodeImg(const std::vector<uint8_t>& f, Image& out, std::string* error) {
    out = Image();
    if (f.size() >= 0x34 && rd32(f.data()) == 0x34 && std::memcmp(&f[0x2c], "PVR!", 4) == 0) { // legacy PVR
        const int h = int(rd32(&f[4])), w = int(rd32(&f[8]));
        const uint32_t flags = rd32(&f[0x10]), type = flags & 0xff;
        const uint8_t* d = f.data() + 0x34;
        const size_t avail = f.size() - 0x34;
        if (w <= 0 || h <= 0) return fail(error, "bad PVR size");
        out.width = w, out.height = h;
        out.bottomUp = flags & 0x10000;
        out.rgba.assign(size_t(w) * h * 4, 255);
        const size_t px = size_t(w) * h;
        // OGL_*: 0x10 RGBA4444, 0x11 RGBA5551, 0x12 RGBA8888, 0x13 RGB565, 0x15 RGB888, 0x16 I8,
        // 0x17 AI88, 0x19 PVRTC 4bpp, 0x1b A8
        static const struct { uint32_t type; const char* name; int bytes; Packed p; bool la, a; } kPlain[] = {
            {0x10, "RGBA4444", 2, {2, {12, 8, 4, 0}, {4, 4, 4, 4}}, false, false},
            {0x11, "RGBA5551", 2, {2, {11, 6, 1, 0}, {5, 5, 5, 1}}, false, false},
            {0x12, "RGBA8888", 4, {4, {0, 8, 16, 24}, {8, 8, 8, 8}}, false, false},
            {0x13, "RGB565", 2, {2, {11, 5, 0, 0}, {5, 6, 5, 0}}, false, false},
            {0x15, "RGB888", 3, {3, {0, 8, 16, 0}, {8, 8, 8, 0}}, false, false},
            {0x16, "I8", 1, {1, {0, 0, 0, 0}, {8, 0, 0, 0}}, true, false},
            {0x17, "AI88", 2, {2, {0, 0, 0, 8}, {8, 0, 0, 8}}, true, false},
            {0x1b, "A8", 1, {1, {0, 0, 0, 0}, {0, 0, 0, 8}}, false, true},
        };
        if (type == 0x19) {
            if (avail < std::max<size_t>(px / 2, 32)) return fail(error, "PVRTC data truncated");
            decodePvrtc4(d, w, h, out.rgba.data());
            out.format = "PVR2 PVRTC4";
            return true;
        }
        for (const auto& k : kPlain)
            if (k.type == type) {
                if (avail < px * size_t(k.bytes)) return fail(error, "PVR data truncated");
                decodePacked(d, w, h, k.p, k.la, k.a, out.rgba.data());
                out.alphaOnly = k.a;
                out.format = std::string("PVR2 ") + k.name;
                return true;
            }
        return fail(error, "unsupported PVR pixel type");
    }
    if (f.size() >= 52 && std::memcmp(f.data(), "PVR\x03", 4) == 0) { // PVR v3
        const uint64_t pf = uint64_t(rd32(&f[8])) | uint64_t(rd32(&f[12])) << 32;
        const int h = int(rd32(&f[0x18])), w = int(rd32(&f[0x1c]));
        const uint32_t meta = rd32(&f[0x30]);
        if (w <= 0 || h <= 0 || 52 + size_t(meta) > f.size()) return fail(error, "bad PVR3 header");
        // metadata: FourCC 'PVR\3', key 3 = orientation (x, y, z bytes; y set: the rows run bottom-up)
        for (size_t p = 52; p + 12 <= 52 + size_t(meta);) {
            const uint32_t key = rd32(&f[p + 4]), size = rd32(&f[p + 8]);
            if (std::memcmp(&f[p], "PVR\x03", 4) == 0 && key == 3 && size >= 2 && p + 12 + size <= f.size()) out.bottomUp = f[p + 13] != 0;
            p += 12 + size_t(size);
        }
        const uint8_t* d = f.data() + 52 + meta;
        const size_t avail = f.size() - 52 - meta, px = size_t(w) * h;
        out.width = w, out.height = h;
        out.rgba.assign(px * 4, 255);
        if ((pf >> 32) == 0) {
            if (pf == 6) { // ETC1
                if (avail < size_t((w + 3) / 4) * size_t((h + 3) / 4) * 8) return fail(error, "ETC1 data truncated");
                decodeEtc1(d, w, h, out.rgba.data());
                out.format = "PVR3 ETC1";
                return true;
            }
            return fail(error, "unsupported PVR3 compressed format");
        }
        // channel names (4 chars) and their bits; packed formats keep the first channel highest
        char names[4];
        int bits[4];
        int total = 0;
        for (int k = 0; k < 4; ++k) names[k] = char(f[8 + k]), bits[k] = f[12 + k], total += bits[k];
        if (total % 8) return fail(error, "unsupported PVR3 bit layout");
        // channels of 8 bits each are stored byte by byte in order; packed ones from the top bit down
        Packed p{total / 8, {0, 0, 0, 0}, {0, 0, 0, 0}};
        bool all8 = true, lum = false;
        for (int k = 0; k < 4 && names[k]; ++k) all8 &= bits[k] == 8;
        for (int k = 0, shift = total; k < 4 && names[k]; ++k) {
            shift -= bits[k];
            const int c = names[k] == 'r' || names[k] == 'l' ? 0 : names[k] == 'g' ? 1 : names[k] == 'b' ? 2 : names[k] == 'a' ? 3 : -1;
            if (c < 0) return fail(error, "unsupported PVR3 channel");
            lum |= names[k] == 'l';
            p.shift[c] = all8 ? 8 * k : shift;
            p.bits[c] = bits[k];
        }
        if (avail < px * size_t(p.bytes)) return fail(error, "PVR3 data truncated");
        decodePacked(d, w, h, p, lum, false, out.rgba.data());
        out.format = std::string("PVR3 ") + std::string(names, strnlen(names, 4));
        for (int k = 0; k < 4 && names[k]; ++k) out.format += (k ? "." : " ") + std::to_string(bits[k]);
        return true;
    }
    if (f.size() >= 0x40 && std::memcmp(f.data(), "GXT", 3) == 0) { // Sony GXT
        if (rd32(&f[8]) < 1) return fail(error, "GXT without a texture");
        const uint32_t data = rd32(&f[0x20]), type = rd32(&f[0x30]), format = rd32(&f[0x34]);
        const int w = rd16(&f[0x38]), h = rd16(&f[0x3a]);
        if (w <= 0 || h <= 0 || data >= f.size()) return fail(error, "bad GXT header");
        const bool swizzled = (type & 0xF0000000u) == 0;
        if (swizzled && ((w & (w - 1)) || (h & (h - 1)))) return fail(error, "swizzled GXT not a power of two");
        const uint8_t* d = f.data() + data;
        const size_t avail = f.size() - data, px = size_t(w) * h;
        out.width = w, out.height = h;
        out.rgba.assign(px * 4, 255);
        const uint32_t base = format & 0xff000000u, swz = format & 0x0000f000u;
        if (base == 0x85000000u || base == 0x86000000u || base == 0x87000000u) { // UBC1 / UBC2 / UBC3
            const int bc = base == 0x85000000u ? 1 : base == 0x86000000u ? 2 : 3;
            if (avail < size_t((w + 3) / 4) * size_t((h + 3) / 4) * (bc == 1 ? 8 : 16)) return fail(error, "GXT BC data truncated");
            decodeBc(d, w, h, bc, swizzled, out.rgba.data());
            out.format = "GXT UBC" + std::to_string(bc);
            return true;
        }
        int bytes;
        if (base == 0x0c000000u && swz == 0x1000) bytes = 4;      // U8U8U8U8 ARGB: bytes B G R A
        else if (base == 0x07000000u && swz == 0x2000) bytes = 2; // U8U8 GRRR: luminance R, alpha G
        else if (base == 0x00000000u && swz == 0x6000) bytes = 1; // U8 R000: alpha
        else return fail(error, "unsupported GXT format");
        if (avail < px * size_t(bytes)) return fail(error, "GXT data truncated");
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const size_t i = swizzled ? vitaSwizzle(x, y, w, h) : size_t(y) * w + x;
                const uint8_t* s = d + i * size_t(bytes);
                uint8_t* o = &out.rgba[(size_t(y) * w + x) * 4];
                if (bytes == 4) o[0] = s[2], o[1] = s[1], o[2] = s[0], o[3] = s[3];
                else if (bytes == 2) o[0] = o[1] = o[2] = s[0], o[3] = s[1];
                else o[0] = o[1] = o[2] = 255, o[3] = s[0];
            }
        out.alphaOnly = bytes == 1;
        out.format = bytes == 4 ? "GXT U8U8U8U8" : bytes == 2 ? "GXT U8U8" : "GXT U8";
        return true;
    }
    return fail(error, "not a PVR / GXT texture");
}

} // namespace cr2
