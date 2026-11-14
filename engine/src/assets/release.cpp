#include "assets/release.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
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
    std::string top = path;
    while (top.size() > 1 && (top.back() == '/' || top.back() == '\\')) top.pop_back();
    // the folder itself, then where each release keeps its archive under its own folder: the
    // Vita's PCSG00498 (CR2/resources/), the iPad's unpacked .ipa (Payload/ChaosRings2enHD.app/)
    for (const char* sub : {"", "/unpacked", "/resources", "/CR2/resources", "/ChaosRings2enHD.app", "/Payload/ChaosRings2enHD.app"}) {
        const std::string dir = top + sub;
        if (*sub && !fs::is_directory(dir, ec)) continue;
        const std::vector<std::string> files = filesIn(dir);
        for (const std::string& n : files)
            if (n.compare(0, 5, "main.") == 0 && endsWith(n, kObbPackage)) return dir + "/" + n;
        for (const std::string& n : files)
            if (n.compare(0, 5, "CRDB.") == 0 && endsWith(n, ".mvgl")) return dir + "/" + n;
    }
    return path;
}

std::string defaultArchive(const std::vector<std::string>& places) {
    std::error_code ec;
    if (const char* env = std::getenv("CR2_DATA"); env && *env) return resolveArchive(env);
    // a folder, then the folders below it (two deep, sorted) - not another game's of the Prequel
    // Trilogy (its PCSG00457 holds CR1/, CRO/, CR3/ beside CR2/, each with a CRDB.psp2.mvgl of its
    // own; the add-ons DIESEL1000ADDCON, DIESELOM00ADDCON are CR1's and Omega's)
    auto otherGame = [](const fs::path& p) {
        std::string n = p.filename().string();
        std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return n == "cr1" || n == "cro" || n == "cr3" || n == "diesel1000addcon" || n == "dieselom00addcon";
    };
    auto below = [&](const std::string& dir) {
        std::vector<std::string> out{dir};
        std::vector<std::string> one;
        for (const fs::directory_entry& e : fs::directory_iterator(dir, ec))
            if (e.is_directory(ec) && !otherGame(e.path())) one.push_back(e.path().string());
        std::sort(one.begin(), one.end());
        for (const std::string& a : one) {
            out.push_back(a);
            std::vector<std::string> two;
            for (const fs::directory_entry& e : fs::directory_iterator(a, ec))
                if (e.is_directory(ec) && !otherGame(e.path())) two.push_back(e.path().string());
            std::sort(two.begin(), two.end());
            out.insert(out.end(), two.begin(), two.end());
        }
        return out;
    };
    for (const char* kind : {"vita", "android", "ios"})
        for (std::string base : places) {
            if (base.empty()) continue;
            if (base.back() != '/' && base.back() != '\\') base += '/';
            if (!fs::is_directory(base + kind, ec)) continue;
            for (const std::string& d : below(base + kind)) {
                const std::string a = resolveArchive(d);
                if (fs::is_regular_file(a, ec)) return a;
            }
        }
    for (std::string base : places) { // (the older places)
        if (base.empty()) continue;
        if (base.back() != '/' && base.back() != '\\') base += '/';
        for (const char* sub : {"ipad", "PCSG00498", "CR2", "ChaosRings2enHD.app", "Payload", "data", ""}) {
            const std::string a = resolveArchive(*sub ? base + sub : base);
            if (fs::is_regular_file(a, ec)) return a;
        }
    }
    return {};
}

std::string defaultArchive(const std::string& baseDir) { return defaultArchive(std::vector<std::string>{baseDir}); }

namespace {

std::string lowerCase(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string baseOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
bool seekTo(std::FILE* f, uint64_t at) {
#ifdef _WIN32
    return _fseeki64(f, int64_t(at), SEEK_SET) == 0;
#else
    return fseeko(f, off_t(at), SEEK_SET) == 0;
#endif
}
uint64_t sizeOf(std::FILE* f) {
#ifdef _WIN32
    _fseeki64(f, 0, SEEK_END);
    return uint64_t(_ftelli64(f));
#else
    fseeko(f, 0, SEEK_END);
    return uint64_t(ftello(f));
#endif
}
uint64_t le64(const uint8_t* p) { return uint64_t(le32(p)) | uint64_t(le32(p + 4)) << 32; }

struct ZipItem {
    std::string name;
    uint16_t method = 0;
    uint64_t packed = 0, size = 0, local = 0;
};
// a zip's central directory, zip64 too (an .xapk with the OBB in it can pass 4 GB)
bool zipList(std::FILE* f, std::vector<ZipItem>& out) {
    out.clear();
    const uint64_t size = sizeOf(f);
    const uint64_t tail = std::min<uint64_t>(size, 0x10000 + 22);
    std::vector<uint8_t> t(static_cast<size_t>(tail));
    if (t.size() < 22 || !seekTo(f, size - tail) || std::fread(t.data(), 1, t.size(), f) != t.size()) return false;
    size_t end = t.size() - 22 + 1;
    while (end-- > 0)
        if (le32(&t[end]) == 0x06054b50) break;
    if (end == size_t(-1)) return false;
    uint64_t count = le16(&t[end + 10]), dirSize = le32(&t[end + 12]), dirOffset = le32(&t[end + 16]);
    if ((count == 0xffff || dirSize == 0xffffffffu || dirOffset == 0xffffffffu) && end >= 20 && le32(&t[end - 20]) == 0x07064b50) {
        uint8_t z[56];
        if (!seekTo(f, le64(&t[end - 20 + 8])) || std::fread(z, 1, sizeof z, f) != sizeof z || le32(z) != 0x06064b50) return false;
        count = le64(z + 32), dirSize = le64(z + 40), dirOffset = le64(z + 48);
    }
    if (dirSize > (uint64_t(64) << 20)) return false;
    std::vector<uint8_t> d(static_cast<size_t>(dirSize));
    if (!seekTo(f, dirOffset) || std::fread(d.data(), 1, d.size(), f) != d.size()) return false;
    for (size_t p = 0, k = 0; k < count && p + 46 <= d.size(); ++k) {
        if (le32(&d[p]) != 0x02014b50) break;
        const uint16_t nameLen = le16(&d[p + 28]), extraLen = le16(&d[p + 30]), commentLen = le16(&d[p + 32]);
        if (p + 46 + nameLen + extraLen > d.size()) break;
        ZipItem e;
        e.method = le16(&d[p + 10]);
        e.packed = le32(&d[p + 20]), e.size = le32(&d[p + 24]), e.local = le32(&d[p + 42]);
        e.name.assign(reinterpret_cast<const char*>(&d[p + 46]), nameLen);
        for (size_t x = p + 46 + nameLen, xe = x + extraLen; x + 4 <= xe;) { // (zip64's extra: the fields that overflowed, in order)
            const uint16_t id = le16(&d[x]), len = le16(&d[x + 2]);
            if (id == 1) {
                size_t q = x + 4;
                if (e.size == 0xffffffffu && q + 8 <= x + 4 + len) e.size = le64(&d[q]), q += 8;
                if (e.packed == 0xffffffffu && q + 8 <= x + 4 + len) e.packed = le64(&d[q]), q += 8;
                if (e.local == 0xffffffffu && q + 8 <= x + 4 + len) e.local = le64(&d[q]);
            }
            x += 4 + size_t(len);
        }
        p += 46 + size_t(nameLen) + extraLen + commentLen;
        if (!e.name.empty() && e.name.back() != '/') out.push_back(std::move(e));
    }
    return true;
}
// where an entry's data begins (past its local header)
bool zipData(std::FILE* f, const ZipItem& e, uint64_t& at) {
    uint8_t h[30];
    if (!seekTo(f, e.local) || std::fread(h, 1, sizeof h, f) != sizeof h || le32(h) != 0x04034b50) return false;
    at = e.local + 30 + le16(h + 26) + le16(h + 28);
    return true;
}
bool isBundle(const std::string& name) {
    const std::string n = lowerCase(name);
    return endsWith(n, ".apks") || endsWith(n, ".xapk") || endsWith(n, ".zip");
}
bool isGameObb(const std::string& name) {
    const std::string n = baseOf(name);
    return n.compare(0, 5, "main.") == 0 && endsWith(n, kObbPackage);
}
// what of a bundle the port needs: the OBB and the APKs (a .zip only when it holds the OBB - not
// any zip lying there)
std::vector<ZipItem> bundleParts(const std::string& bundle) {
    std::vector<ZipItem> all, out;
    std::FILE* f = std::fopen(bundle.c_str(), "rb");
    if (!f) return out;
    const bool listed = zipList(f, all);
    std::fclose(f);
    if (!listed) return out;
    const bool obb = std::any_of(all.begin(), all.end(), [](const ZipItem& e) { return isGameObb(e.name); });
    if (endsWith(lowerCase(bundle), ".zip") && !obb) return out;
    for (const ZipItem& e : all)
        if ((isGameObb(e.name) || endsWith(lowerCase(e.name), ".apk")) && (e.method == 0 || e.method == 8)) out.push_back(e);
    return out;
}

} // namespace

bool androidBundleOk(const std::string& folder) {
    bool obb = false, apk = false;
    for (const std::string& n : filesIn(folder)) {
        if (isGameObb(n)) obb = true;
        else if (endsWith(lowerCase(n), ".apk")) apk = true;
        else if (isBundle(n))
            for (const ZipItem& e : bundleParts(folder + "/" + n)) (isGameObb(e.name) ? obb : apk) = true;
    }
    return obb && apk;
}

bool unpackAndroidBundles(const std::string& folder, const std::function<void(const std::string&, double)>& progress,
                          std::vector<std::string>* failed) {
    struct Pending {
        std::string bundle;
        ZipItem entry;
        fs::path to;
    };
    std::vector<Pending> pending;
    uint64_t total = 0;
    std::error_code ec;
    for (const std::string& n : filesIn(folder)) {
        if (!isBundle(n)) continue;
        for (const ZipItem& e : bundleParts(folder + "/" + n)) {
            const fs::path to = fs::path(folder) / "unpacked" / baseOf(e.name);
            if (fs::exists(to, ec) && fs::file_size(to, ec) == e.size) continue; // (unpacked before)
            if (std::any_of(pending.begin(), pending.end(), [&](const Pending& q) { return q.to == to; })) continue;
            pending.push_back({folder + "/" + n, e, to}), total += e.size;
        }
    }
    uint64_t done = 0;
    bool all = true;
    for (const Pending& p : pending) {
        const std::string name = baseOf(p.entry.name);
        auto told = [&](uint64_t n) {
            if (progress && total) progress(name, double(done + n) / double(total));
        };
        told(0);
        bool ok = false;
        std::FILE* f = std::fopen(p.bundle.c_str(), "rb");
        uint64_t at = 0;
        fs::create_directories(p.to.parent_path(), ec);
        const fs::path part = p.to.string() + ".part";
        std::FILE* o = f && zipData(f, p.entry, at) && seekTo(f, at) ? std::fopen(part.string().c_str(), "wb") : nullptr;
        if (o) {
            std::vector<uint8_t> in(1 << 20), buf(1 << 20);
            uint64_t left = p.entry.packed, written = 0;
            ok = true;
            if (p.entry.method == 0) {
                while (ok && left > 0) {
                    const size_t n = size_t(std::min<uint64_t>(left, in.size()));
                    ok = std::fread(in.data(), 1, n, f) == n && std::fwrite(in.data(), 1, n, o) == n;
                    left -= n, written += n, told(written);
                }
            } else {
#ifdef CR2_HAVE_ZLIB
                z_stream z{};
                ok = inflateInit2(&z, -MAX_WBITS) == Z_OK;
                int r = Z_OK;
                while (ok && r != Z_STREAM_END) {
                    if (z.avail_in == 0 && left > 0) {
                        const size_t n = size_t(std::min<uint64_t>(left, in.size()));
                        ok = std::fread(in.data(), 1, n, f) == n;
                        z.next_in = in.data(), z.avail_in = uInt(n), left -= n;
                    }
                    z.next_out = buf.data(), z.avail_out = uInt(buf.size());
                    r = inflate(&z, Z_NO_FLUSH);
                    ok = ok && (r == Z_OK || r == Z_STREAM_END) && !(r == Z_OK && z.avail_in == 0 && left == 0 && z.avail_out != 0);
                    const size_t got = buf.size() - z.avail_out;
                    ok = ok && std::fwrite(buf.data(), 1, got, o) == got;
                    written += got, told(written);
                }
                inflateEnd(&z);
#else
                ok = false;
#endif
            }
            ok = std::fclose(o) == 0 && ok && written == p.entry.size;
        }
        if (f) std::fclose(f);
        if (ok) fs::rename(part, p.to, ec), ok = !ec;
        if (!ok) {
            fs::remove(part, ec);
            if (failed) failed->push_back(name);
            all = false;
        }
        done += p.entry.size;
    }
    return all;
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

bool androidFileIn(const std::string& dir, const std::string& name, std::vector<uint8_t>& out, std::string* from);

bool androidFile(const std::string& archivePath, const std::string& name, std::vector<uint8_t>& out, std::string* from) {
    const std::string top = folderOf(resolveArchive(archivePath));
    // (the folder, and what a bundle there was unpacked into - or, the OBB itself unpacked, the
    // folder above it, where its APKs may lie loose)
    std::vector<std::string> dirs{top, top + "/unpacked"};
    if (baseOf(top) == "unpacked") dirs.push_back(folderOf(top));
    for (const std::string& dir : dirs)
        if (androidFileIn(dir, name, out, from)) return true;
    return false;
}

bool androidFileIn(const std::string& dir, const std::string& name, std::vector<uint8_t>& out, std::string* from) {
    for (const std::string& p : {dir + "/" + name, dir + "/extracted/" + name})
        if (readWhole(p, out)) {
            if (from) *from = p;
            return true;
        }
    // (the game's own APK first - the one with its library -, then any other beside it that has the
    // entry: a split install's base.apk holds the assets while a config split holds the library, and a
    // later build's library may not be the 32-bit one)
    for (int pass = 0; pass < (name == kAndroidLibrary ? 1 : 2); ++pass)
        for (const std::string& n : filesIn(dir)) {
            if (!endsWith(n, ".apk")) continue;
            std::vector<uint8_t> lib;
            if (pass == 0 && name != kAndroidLibrary && !readZipEntry(dir + "/" + n, kAndroidLibrary, lib)) continue;
            if (readZipEntry(dir + "/" + n, name, out)) {
                if (from) *from = dir + "/" + n + ":" + name;
                return true;
            }
        }
    return false;
}

namespace {
// Shift-JIS's row 0x81 (its punctuation: the iPad's and the Vita's lines have 「」（）：, which
// their AllTextTextureGeneration swaps for UTF-8 copies), from 0x8140
const char* const kSjisRow81[189] = {
    "　", "、", "。", "，", "．", "・", "：", "；", "？", "！", "゛", "゜", "´", "｀", "¨", "＾", "￣", "＿", "ヽ",
    "ヾ", "ゝ", "ゞ", "〃", "仝", "々", "〆", "〇", "ー", "―", "‐", "／", "＼", "～", "∥", "｜", "…", "‥", "‘",
    "’", "“", "”", "（", "）", "〔", "〕", "［", "］", "｛", "｝", "〈", "〉", "《", "》", "「", "」", "『", "』",
    "【", "】", "＋", "－", "±", "×", "", "÷", "＝", "≠", "＜", "＞", "≦", "≧", "∞", "∴", "♂", "♀", "°",
    "′", "″", "℃", "￥", "＄", "￠", "￡", "％", "＃", "＆", "＊", "＠", "§", "☆", "★", "○", "●", "◎", "◇",
    "◆", "□", "■", "△", "▲", "▽", "▼", "※", "〒", "→", "←", "↑", "↓", "〓", "", "", "", "", "", "",
    "", "", "", "", "", "∈", "∋", "⊆", "⊇", "⊂", "⊃", "∪", "∩", "", "", "", "", "", "", "", "",
    "∧", "∨", "￢", "⇒", "⇔", "∀", "∃", "", "", "", "", "", "", "", "", "", "", "", "∠", "⊥", "⌒",
    "∂", "∇", "≡", "≒", "≪", "≫", "√", "∽", "∝", "∵", "∫", "∬", "", "", "", "", "", "", "", "Å",
    "‰", "♯", "♭", "♪", "†", "‡", "¶", "", "", "", "", "◯"};
// a line as UTF-8: row 0x81's pairs decoded; false if what is left is not text (well-formed
// UTF-8, no control characters)
bool asText(const std::string& in, std::string& out) {
    out.clear();
    for (size_t i = 0; i < in.size(); ++i) {
        const uint8_t c = uint8_t(in[i]), d = i + 1 < in.size() ? uint8_t(in[i + 1]) : 0;
        if (c == 0x81 && d >= 0x40 && d <= 0xfc && *kSjisRow81[d - 0x40]) out += kSjisRow81[d - 0x40], ++i;
        else out += char(c);
    }
    for (size_t i = 0; i < out.size();) {
        const uint8_t c = uint8_t(out[i]);
        const size_t n = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 0;
        if (n == 0 || (n == 1 && c < 0x20 && c != '\t') || i + n > out.size()) return false;
        for (size_t k = 1; k < n; ++k)
            if ((uint8_t(out[i + k]) & 0xc0) != 0x80) return false;
        i += n;
    }
    return true;
}
// the table in a 32-bit executable: its four figure names' strings, then four words pointing at
// them (the same distances apart: the pointers' base less the file's, the delta), and the lines:
// Android's 544 pointers before them, or the iPad's 438 after them (AllTextTextureGeneration's
// 0x1b6) - the layout whose every line is text
bool findStaffRoll(const std::vector<uint8_t>& b, std::vector<std::string>& lines, std::string* figures) {
    static const char* const kNames[4] = {"end2_BG", "end1_BG", "end3_BG", "end4_BG"};
    auto find = [&](const char* s) -> int64_t {
        const size_t n = std::strlen(s) + 1;
        for (size_t o = 0; o + n <= b.size(); ++o)
            if (std::memcmp(&b[o], s, n) == 0 && (o == 0 || b[o - 1] == 0)) return int64_t(o);
        return -1;
    };
    int64_t at[4];
    for (int k = 0; k < 4; ++k)
        if ((at[k] = find(kNames[k])) < 0) return false;
    auto word = [&](size_t o) { return uint32_t(b[o] | b[o + 1] << 8 | b[o + 2] << 16 | uint32_t(b[o + 3]) << 24); };
    for (size_t o = 544 * 4; o + 16 <= b.size(); o += 4) {
        const int64_t delta = int64_t(word(o)) - at[0];
        bool ok = true;
        for (int k = 1; k < 4 && ok; ++k) ok = int64_t(word(o + 4 * size_t(k))) - at[k] == delta;
        if (!ok) continue;
        const struct {
            size_t from, count;
        } layouts[2] = {{o - 544 * 4, 544}, {o + 16, 438}};
        for (const auto& l : layouts) {
            if (l.from + l.count * 4 > b.size()) continue;
            lines.clear();
            bool text = true;
            for (size_t i = 0; i < l.count && text; ++i) { // (a run of words that only looks so: not every one a string)
                const int64_t s = int64_t(word(l.from + 4 * i)) - delta;
                if (s < 0 || size_t(s) >= b.size()) {
                    text = false;
                    break;
                }
                std::string raw, t;
                for (size_t c = size_t(s); c < b.size() && b[c]; ++c) raw += char(b[c]);
                text = asText(raw, t);
                lines.push_back(t);
            }
            if (!text) continue;
            if (figures)
                for (int k = 0; k < 4; ++k) figures[k] = kNames[k];
            return true;
        }
    }
    lines.clear();
    return false;
}
} // namespace

bool releaseStaffRoll(const std::string& archivePath, const std::string& binary, std::vector<std::string>& lines, std::string figures[4]) {
    const std::string archive = resolveArchive(archivePath);
    std::vector<std::string> candidates;
    if (!binary.empty()) candidates.push_back(binary);
    for (const std::string& c : mapTableCandidates(archive)) candidates.push_back(c);
    std::vector<uint8_t> bytes;
    for (const std::string& c : candidates)
        if (readWhole(c, bytes) && findStaffRoll(bytes, lines, figures)) return true;
    return androidFile(archive, kAndroidLibrary, bytes) && findStaffRoll(bytes, lines, figures);
}

bool releaseEncountTable(const std::string& archivePath, const std::string& binary, EncountTable& table) {
    const std::string archive = resolveArchive(archivePath);
    std::vector<std::string> candidates;
    if (!binary.empty()) candidates.push_back(binary);
    for (const std::string& c : mapTableCandidates(archive)) candidates.push_back(c);
    std::vector<uint8_t> bytes;
    for (const std::string& c : candidates)
        if (readWhole(c, bytes) && findEncountTable(bytes, table)) return true;
    return androidFile(archive, kAndroidLibrary, bytes) && findEncountTable(bytes, table);
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
