#include "assets/mvgl.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace cr2 {

namespace {

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }

void fail(std::string* error, const std::string& what) {
    if (error) *error = what;
}

} // namespace

bool undoboz(const uint8_t* in, size_t inSize, std::vector<uint8_t>& out) {
    if (inSize < 3) return false;
    const unsigned attr = in[0];
    const unsigned width = ((attr >> 3) & 7) + 1; // the size fields' bytes: 1, 2, 4
    if (width > 4 || inSize < 1 + 2 * width) return false;
    uint32_t size = 0, packed = 0;
    for (unsigned i = 0; i < width; ++i) size |= uint32_t(in[1 + i]) << (8 * i), packed |= uint32_t(in[1 + width + i]) << (8 * i);
    if (packed > inSize) return false;
    size_t p = 1 + 2 * width;
    out.clear();
    if (attr & 0x80) { // stored
        if (p + size > inSize) return false;
        out.assign(in + p, in + p + size);
        return true;
    }
    out.reserve(size);
    // match forms by the word's low 3 bits: mask, offset shift, length mask, length shift, bytes
    static const struct { uint32_t mask; uint8_t offShift, lenMask, lenShift, bytes; } kMatch[8] = {
        {0xff, 2, 0, 0, 1}, {0xffff, 2, 0, 0, 2}, {0xffff, 6, 15, 2, 2}, {0xffffff, 8, 31, 3, 3},
        {0xff, 2, 0, 0, 1}, {0xffff, 2, 0, 0, 2}, {0xffff, 6, 15, 2, 2}, {0xffffffff, 11, 255, 3, 4},
    };
    uint32_t control = 1;
    while (out.size() < size) {
        if (control == 1) { // 31 flags a word; the top set bit marks its end
            if (p + 4 > inSize) return false;
            control = le32(in + p);
            p += 4;
        }
        if ((control & 1) == 0) {
            if (p >= inSize) return false;
            out.push_back(in[p++]);
        } else {
            uint8_t w4[4] = {};
            std::memcpy(w4, in + p, std::min<size_t>(4, inSize - p));
            const uint32_t w = le32(w4);
            const auto& m = kMatch[w & 7];
            const size_t back = (w & m.mask) >> m.offShift; // from the output's end (no - 1)
            const size_t length = ((w >> m.lenShift) & m.lenMask) + 3;
            p += m.bytes;
            if (p > inSize || back > out.size()) return false;
            const size_t src = out.size() - back;
            for (size_t k = 0; k < length; ++k) out.push_back(out[src + k]); // (may overlap itself)
        }
        control >>= 1;
    }
    out.resize(size);
    return true;
}

MvglArchive::~MvglArchive() { close(); }

void MvglArchive::close() {
    if (file_) std::fclose(file_);
    file_ = nullptr;
    entries_.clear();
    byName_.clear();
    path_.clear();
}

std::string MvglArchive::key(const std::string& name) {
    std::string k = name;
    for (char& c : k) c = c == '/' ? '\\' : char(std::tolower(static_cast<unsigned char>(c)));
    return k;
}

bool MvglArchive::open(const std::string& path, std::string* error) {
    close();
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return fail(error, path + ": cannot open"), false;
    uint8_t h[0x14];
    if (std::fread(h, 1, sizeof h, f) != sizeof h || std::memcmp(h, "MDB1", 4) != 0) {
        std::fclose(f);
        return fail(error, path + ": not an MDB1 archive"), false;
    }
    const uint32_t trees = le16(h + 4), names = le16(h + 6), datas = le32(h + 8), dataStart = le32(h + 12);
    std::vector<uint8_t> t(size_t(trees) * 8 + size_t(names) * 64 + size_t(datas) * 12);
    if (std::fread(t.data(), 1, t.size(), f) != t.size()) {
        std::fclose(f);
        return fail(error, path + ": truncated table"), false;
    }
    const uint8_t* tree = t.data();
    const uint8_t* nameTab = tree + size_t(trees) * 8;
    const uint8_t* dataTab = nameTab + size_t(names) * 64;
    for (uint32_t i = 0; i < trees && i < names; ++i) {
        const uint16_t id = le16(tree + 8 * i + 2);
        if (id == 0xffff || id >= datas) continue; // the trie's root
        const char* ext = reinterpret_cast<const char*>(nameTab + 64 * i);
        const char* nm = ext + 4;
        std::string e(ext, strnlen(ext, 4)), n(nm, strnlen(nm, 60));
        while (!e.empty() && e.back() == ' ') e.pop_back(); // ("img ", "nut ")
        Entry en;
        en.name = e.empty() ? n : n + "." + e;
        en.offset = uint64_t(dataStart) + le32(dataTab + 12 * id);
        en.size = le32(dataTab + 12 * id + 4);
        en.stored = le32(dataTab + 12 * id + 8);
        byName_[key(en.name)] = entries_.size();
        entries_.push_back(std::move(en));
    }
    file_ = f;
    path_ = path;
    return true;
}

const MvglArchive::Entry* MvglArchive::find(const std::string& name) const {
    const auto it = byName_.find(key(name));
    return it == byName_.end() ? nullptr : &entries_[it->second];
}

bool MvglArchive::read(const Entry& e, std::vector<uint8_t>& out, std::string* error) const {
    if (!file_) return fail(error, "no archive"), false;
    std::vector<uint8_t> raw(e.stored);
    if (std::fseek(file_, long(e.offset), SEEK_SET) != 0 || std::fread(raw.data(), 1, raw.size(), file_) != raw.size())
        return fail(error, e.name + ": cannot read"), false;
    if (e.stored == e.size) {
        out = std::move(raw);
        return true;
    }
    if (!undoboz(raw.data(), raw.size(), out) || out.size() != e.size) return fail(error, e.name + ": bad doboz data"), false;
    return true;
}

bool MvglArchive::read(const std::string& name, std::vector<uint8_t>& out, std::string* error) const {
    const Entry* e = find(name);
    if (!e) return fail(error, name + ": not in the archive"), false;
    return read(*e, out, error);
}

} // namespace cr2
