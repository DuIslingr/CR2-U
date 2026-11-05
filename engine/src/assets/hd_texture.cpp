#include "assets/hd_texture.hpp"

#include "stb/stb_image.h"
#include "stb/stb_image_write.h"

#include <algorithm>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef CR2_HAVE_ZLIB
#include <zlib.h>
#endif

namespace cr2 {

namespace {

void put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i)));
}

uint32_t get32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

bool readAll(const std::string& path, std::vector<uint8_t>& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? size_t(n) : 0);
    const bool ok = n > 0 && std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

// a whole file, through <path>.part
bool writeFile(const std::string& path, const std::vector<uint8_t>& out) {
    std::FILE* f = std::fopen((path + ".part").c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    if (std::fclose(f) != 0 || !ok) return std::remove((path + ".part").c_str()), false;
    std::remove(path.c_str()); // (Windows' rename won't replace)
    return std::rename((path + ".part").c_str(), path.c_str()) == 0;
}

} // namespace

int imageStorage(const Image& img) {
    const std::string& f = img.format;
    if (f.find("PVRTC") != std::string::npos || f.find("ETC1") != std::string::npos || f.find("UBC") != std::string::npos) return 2;
    if (f.find("4444") != std::string::npos || f.find("5551") != std::string::npos || f.find("565") != std::string::npos) return 3;
    if (f.rfind("PVR3 ", 0) == 0) { // ("PVR3 rgba 4.4.4.4": its channels' bits)
        const size_t sp = f.find(' ', 5);
        if (sp == std::string::npos) return 0;
        for (const char* p = f.c_str() + sp + 1; *p;) {
            char* end = nullptr;
            const long b = std::strtol(p, &end, 10);
            if (end == p) break;
            if (b < 8) return 3;
            p = *end == '.' ? end + 1 : end;
        }
        return 1;
    }
    return f.empty() ? 0 : 1;
}

uint64_t imageHash(const Image& img) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](const uint8_t* p, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            h ^= p[i];
            h *= 1099511628211ull;
        }
    };
    const uint32_t head[3] = {uint32_t(img.width), uint32_t(img.height), img.alphaOnly ? 1u : 0u};
    mix(reinterpret_cast<const uint8_t*>(head), sizeof head);
    mix(img.rgba.data(), img.rgba.size());
    return h;
}

std::string hashName(uint64_t hash) {
    char s[17];
    std::snprintf(s, sizeof s, "%016llx", static_cast<unsigned long long>(hash));
    return s;
}

bool saveRawImage(const std::string& path, const Image& img) {
    std::vector<uint8_t> out = {'C', 'R', 'R', 'A', 'W', '1'};
    put32(out, uint32_t(img.width));
    put32(out, uint32_t(img.height));
    put32(out, (img.alphaOnly ? 1u : 0u) | uint32_t(imageStorage(img)) << 1);
    out.insert(out.end(), img.rgba.begin(), img.rgba.end());
    return writeFile(path, out);
}



namespace {

// A replacement left out: why ("<hash>.png: ..."), and false
bool leftOut(std::string* why, const std::string& file, const std::string& what) {
    if (why) *why = file + ": " + what;
    return false;
}

std::string sizesText(int w, int h, int originalW, int originalH) {
    return std::to_string(w) + "x" + std::to_string(h) + " for a " + std::to_string(originalW) + "x" + std::to_string(originalH) +
           " picture (its shape, at least its size)";
}

// The rows the other way up (a bottom-up Image's, as a picture is seen: the top first)
void flipRows(std::vector<uint8_t>& px, int w, int h) {
    const size_t row = size_t(w) * 4;
    for (int y = 0; y < h / 2; ++y) std::swap_ranges(px.begin() + std::ptrdiff_t(row * size_t(y)), px.begin() + std::ptrdiff_t(row * size_t(y + 1)),
                                                     px.begin() + std::ptrdiff_t(row * size_t(h - 1 - y)));
}

// PNG's chunk check (CRC-32, the IEEE polynomial)
uint32_t crc32Of(const uint8_t* p, size_t n) {
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
    }
    return ~c;
}

void putBe32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 3; i >= 0; --i) out.push_back(uint8_t(v >> (8 * i)));
}

} // namespace

bool hdShapeFits(int w, int h, int originalW, int originalH) {
    if (originalW <= 0 || originalH <= 0 || w < originalW || h < originalH || w > 8192 || h > 8192) return false;
    // (one scale s for both: w = originalW s + a, h = originalH s + b, each rounded, |a|, |b| <= 1/2)
    const int64_t off = int64_t(w) * originalH - int64_t(h) * originalW;
    return 2 * (off < 0 ? -off : off) <= int64_t(originalW) + originalH;
}

bool savePngImage(const std::string& path, const Image& img) {
    const size_t n = size_t(std::max(img.width, 0)) * size_t(std::max(img.height, 0)) * 4;
    if (!n || img.rgba.size() < n) return false;
    std::vector<uint8_t> rows(img.rgba.begin(), img.rgba.begin() + std::ptrdiff_t(n));
    if (img.bottomUp) flipRows(rows, img.width, img.height);
    std::vector<uint8_t> png;
    stbi_write_png_to_func(
        [](void* to, void* data, int size) {
            auto* v = static_cast<std::vector<uint8_t>*>(to);
            v->insert(v->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
        },
        &png, img.width, img.height, 4, rows.data(), img.width * 4);
    if (png.size() < 33 || std::memcmp(png.data() + 12, "IHDR", 4) != 0) return false;
    // the .crraw's flags for tools/hd_textures.py (which upscaler), after IHDR
    const std::string text = std::string("cr-flags") + '\0' + std::to_string((img.alphaOnly ? 1u : 0u) | uint32_t(imageStorage(img)) << 1);
    std::vector<uint8_t> chunk;
    putBe32(chunk, uint32_t(text.size()));
    chunk.insert(chunk.end(), {'t', 'E', 'X', 't'});
    chunk.insert(chunk.end(), text.begin(), text.end());
    putBe32(chunk, crc32Of(chunk.data() + 4, chunk.size() - 4));
    png.insert(png.begin() + 33, chunk.begin(), chunk.end());
    return writeFile(path, png);
}

bool loadHdImage(const std::string& dir, const Image& original, Image& out, std::string* why) {
    if (why) why->clear();
    if (dir.empty() || original.width <= 0) return false;
    const std::string name = hashName(imageHash(original));
    std::vector<uint8_t> file;
#ifdef CR2_HAVE_ZLIB
    if (readAll(dir + name + ".crhd", file)) {
        const size_t size = file.size();
        const uint8_t* p = file.data();
        if (size <= 26 || std::memcmp(p, "CRHD1", 6) != 0) return leftOut(why, name + ".crhd", "not a .crhd");
        const uint32_t w = get32(p + 6), h = get32(p + 10);
        if (get32(p + 14) != uint32_t(original.width) || get32(p + 18) != uint32_t(original.height))
            return leftOut(why, name + ".crhd", "made for a picture of another size");
        if (!hdShapeFits(int(std::min(w, 65536u)), int(std::min(h, 65536u)), original.width, original.height))
            return leftOut(why, name + ".crhd", sizesText(int(std::min(w, 65536u)), int(std::min(h, 65536u)), original.width, original.height));
        out = Image();
        out.width = int(w);
        out.height = int(h);
        out.alphaOnly = (get32(p + 22) & 1) != 0;
        out.bottomUp = original.bottomUp;
        out.format = original.format + " (HD)";
        out.rgba.resize(size_t(w) * h * 4);
        uLongf len = uLongf(out.rgba.size());
        if (uncompress(out.rgba.data(), &len, p + 26, uLong(size - 26)) != Z_OK || len != out.rgba.size())
            return leftOut(why, name + ".crhd", "its pixels don't unpack");
        return true;
    }
#endif
    if (readAll(dir + name + ".png", file)) { // (any upscale or edit: upright, the original's shape)
        int w = 0, h = 0, channels = 0;
        stbi_uc* px = stbi_load_from_memory(file.data(), int(std::min(file.size(), size_t(1) << 30)), &w, &h, &channels, 4);
        if (!px) return leftOut(why, name + ".png", "not a PNG this reads");
        if (!hdShapeFits(w, h, original.width, original.height)) {
            stbi_image_free(px);
            return leftOut(why, name + ".png", sizesText(w, h, original.width, original.height));
        }
        out = Image();
        out.width = w;
        out.height = h;
        out.alphaOnly = original.alphaOnly;
        out.bottomUp = original.bottomUp;
        out.format = original.format + " (HD)";
        out.rgba.assign(px, px + size_t(w) * size_t(h) * 4);
        stbi_image_free(px);
        if (out.bottomUp) flipRows(out.rgba, w, h); // (the PNG the top first, the Image in its own order)
        return true;
    }
    return false;
}

} // namespace cr2
