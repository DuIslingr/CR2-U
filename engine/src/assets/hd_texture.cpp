#include "assets/hd_texture.hpp"

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
    std::FILE* f = std::fopen((path + ".part").c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    if (std::fclose(f) != 0 || !ok) return std::remove((path + ".part").c_str()), false;
    std::remove(path.c_str()); // (Windows' rename won't replace)
    return std::rename((path + ".part").c_str(), path.c_str()) == 0;
}

bool loadHdImage(const std::string& dir, const Image& original, Image& out) {
#ifdef CR2_HAVE_ZLIB
    if (dir.empty() || original.width <= 0) return false;
    std::vector<uint8_t> file;
    if (!readAll(dir + hashName(imageHash(original)) + ".crhd", file)) return false;
    const size_t size = file.size();
    const uint8_t* p = file.data();
    bool ok = size > 26 && std::memcmp(p, "CRHD1", 6) == 0;
    const uint32_t w = ok ? get32(p + 6) : 0, h = ok ? get32(p + 10) : 0;
    ok = ok && get32(p + 14) == uint32_t(original.width) && get32(p + 18) == uint32_t(original.height) && w >= 1 &&
         h >= 1 && w <= 8192 && h <= 8192 && w >= uint32_t(original.width) && h >= uint32_t(original.height);
    if (ok) {
        out = Image();
        out.width = int(w);
        out.height = int(h);
        out.alphaOnly = (get32(p + 22) & 1) != 0;
        out.bottomUp = original.bottomUp;
        out.format = original.format + " (HD)";
        out.rgba.resize(size_t(w) * h * 4);
        uLongf len = uLongf(out.rgba.size());
        ok = uncompress(out.rgba.data(), &len, p + 26, uLong(size - 26)) == Z_OK && len == out.rgba.size();
    }
    return ok;
#else
    (void)dir;
    (void)original;
    (void)out;
    return false; // built without zlib
#endif
}

} // namespace cr2
