#include "assets/release.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

#ifdef CR2_HAVE_ZLIB
#include <zlib.h>
#endif

namespace cr2 {

namespace {

namespace fs = std::filesystem;

constexpr const char* kObbPackage = ".com.square_enix.chaosrings2gp.obb";
constexpr const char* kAndroidLibrary = "lib/armeabi-v7a/libChaosRings2.so";

uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

bool endsWith(const std::string& s, const std::string& sfx) {
    return s.size() >= sfx.size() && s.compare(s.size() - sfx.size(), sfx.size(), sfx) == 0;
}

bool readWhole(const std::string& path, std::vector<uint8_t>& out) {
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

bool fail(std::string* error, const std::string& why) {
    if (error) *error = why;
    return false;
}

// the folder an archive path lies in ("." for a bare name)
std::string folderOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

std::vector<std::string> filesIn(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const fs::directory_entry& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file(ec)) out.push_back(e.path().filename().string());
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

std::string resolveArchive(const std::string& path) {
    std::error_code ec;
    if (!fs::is_directory(path, ec)) return path;
    std::string dir = path;
    while (dir.size() > 1 && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();
    const std::vector<std::string> files = filesIn(dir);
    for (const std::string& n : files)
        if (n.compare(0, 5, "main.") == 0 && endsWith(n, kObbPackage)) return dir + "/" + n;
    for (const std::string& n : files)
        if (n.compare(0, 5, "CRDB.") == 0 && endsWith(n, ".mvgl")) return dir + "/" + n;
    return path;
}

bool readZipEntry(const std::string& zip, const std::string& name, std::vector<uint8_t>& out, std::string* error) {
    std::FILE* f = std::fopen(zip.c_str(), "rb");
    if (!f) return fail(error, zip + ": cannot open");
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    // the end record (in the last 64 KB + 22: its comment's at most 65535 bytes)
    const long tail = std::min<long>(size, 0x10000 + 22);
    std::vector<uint8_t> t(size_t(std::max<long>(tail, 0)));
    std::fseek(f, size - tail, SEEK_SET);
    if (t.size() < 22 || std::fread(t.data(), 1, t.size(), f) != t.size()) return std::fclose(f), fail(error, zip + ": not a zip");
    size_t end = t.size() - 22 + 1;
    while (end-- > 0)
        if (le32(&t[end]) == 0x06054b50) break;
    if (end == size_t(-1)) return std::fclose(f), fail(error, zip + ": not a zip");
    const uint32_t count = le16(&t[end + 10]), dirSize = le32(&t[end + 12]), dirOffset = le32(&t[end + 16]);
    std::vector<uint8_t> d(dirSize);
    std::fseek(f, long(dirOffset), SEEK_SET);
    if (std::fread(d.data(), 1, d.size(), f) != d.size()) return std::fclose(f), fail(error, zip + ": truncated");
    for (size_t p = 0, k = 0; k < count && p + 46 <= d.size(); ++k) {
        if (le32(&d[p]) != 0x02014b50) break;
        const uint16_t method = le16(&d[p + 10]), nameLen = le16(&d[p + 28]), extraLen = le16(&d[p + 30]), commentLen = le16(&d[p + 32]);
        const uint32_t packed = le32(&d[p + 20]), unpacked = le32(&d[p + 24]), local = le32(&d[p + 42]);
        const std::string n(reinterpret_cast<const char*>(&d[p + 46]), std::min<size_t>(nameLen, d.size() - p - 46));
        p += 46 + size_t(nameLen) + extraLen + commentLen;
        if (n != name) continue;
        uint8_t h[30];
        std::fseek(f, long(local), SEEK_SET);
        if (std::fread(h, 1, sizeof h, f) != sizeof h || le32(h) != 0x04034b50) return std::fclose(f), fail(error, zip + ": bad entry");
        std::fseek(f, long(local + 30 + le16(h + 26) + le16(h + 28)), SEEK_SET);
        std::vector<uint8_t> raw(packed);
        const bool read = std::fread(raw.data(), 1, raw.size(), f) == raw.size();
        std::fclose(f);
        if (!read) return fail(error, zip + ": truncated");
        if (method == 0) return out = std::move(raw), true;
        if (method != 8) return fail(error, name + ": compressed in a way not read here");
#ifdef CR2_HAVE_ZLIB
        out.assign(unpacked, 0);
        z_stream z{};
        if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return fail(error, "zlib could not start");
        z.next_in = raw.data(), z.avail_in = uInt(raw.size());
        z.next_out = out.data(), z.avail_out = uInt(out.size());
        const int r = inflate(&z, Z_FINISH);
        inflateEnd(&z);
        if (r != Z_STREAM_END || z.total_out != unpacked) return fail(error, name + ": does not inflate");
        return true;
#else
        return fail(error, name + ": deflated (built without zlib)");
#endif
    }
    std::fclose(f);
    return fail(error, zip + ": no " + name);
}

bool androidFile(const std::string& archivePath, const std::string& name, std::vector<uint8_t>& out, std::string* from) {
    const std::string dir = folderOf(resolveArchive(archivePath));
    for (const std::string& p : {dir + "/" + name, dir + "/extracted/" + name})
        if (readWhole(p, out)) {
            if (from) *from = p;
            return true;
        }
    for (const std::string& n : filesIn(dir)) {
        if (!endsWith(n, ".apk")) continue;
        std::vector<uint8_t> lib; // (the game's own APK: the one with its library)
        if (name != kAndroidLibrary && !readZipEntry(dir + "/" + n, kAndroidLibrary, lib)) continue;
        if (readZipEntry(dir + "/" + n, name, out)) {
            if (from) *from = dir + "/" + n + ":" + name;
            return true;
        }
    }
    return false;
}

bool releaseMapTable(const std::string& archivePath, const std::string& binary, std::vector<MapInfo>& table, std::string* from) {
    const std::string archive = resolveArchive(archivePath);
    std::vector<std::string> candidates;
    if (!binary.empty()) candidates.push_back(binary);
    for (const std::string& c : mapTableCandidates(archive)) candidates.push_back(c);
    std::vector<uint8_t> bytes;
    for (const std::string& c : candidates)
        if (readWhole(c, bytes) && findMapTable(bytes, table)) {
            if (from) *from = c;
            return true;
        }
    std::string where;
    if (androidFile(archive, kAndroidLibrary, bytes, &where) && findMapTable(bytes, table)) {
        if (from) *from = where;
        return true;
    }
    return false;
}

} // namespace cr2
