#pragma once
// Media.Vision's MDB1 archives (`.mvgl`): the iPad's CRDB.ios.mvgl, the Android release's OBB, the
// Vita's CRDB.psp2.mvgl (docs/FORMATS.md). A table of names and (offset, size, stored size); a
// file stored smaller than its size is doboz-compressed.

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace cr2 {

// doboz (Attila Afra's LZ77 variant): `in` is a whole compressed block, header included; false
// when it is malformed or does not decode to the size its header gives
bool undoboz(const uint8_t* in, size_t inSize, std::vector<uint8_t>& out);

class MvglArchive {
public:
    struct Entry {
        std::string name; // as stored, folders with '\', the extension after a '.' ("images\a.img")
        uint64_t offset = 0;
        uint32_t size = 0, stored = 0;
    };

    MvglArchive() = default;
    MvglArchive(const MvglArchive&) = delete;
    MvglArchive& operator=(const MvglArchive&) = delete;
    ~MvglArchive();

    bool open(const std::string& path, std::string* error = nullptr);
    void close();
    bool isOpen() const { return file_ != nullptr; }
    const std::string& path() const { return path_; }

    const std::vector<Entry>& entries() const { return entries_; }
    // by name, case-insensitive, '/' or '\' alike
    const Entry* find(const std::string& name) const;
    bool contains(const std::string& name) const { return find(name) != nullptr; }
    // the file decoded (decompressed when it is stored compressed)
    bool read(const Entry& e, std::vector<uint8_t>& out, std::string* error = nullptr) const;
    bool read(const std::string& name, std::vector<uint8_t>& out, std::string* error = nullptr) const;

    // Loose files read in place of the archive's by read(name): a fan translation's (#11) - its name in
    // the archive ("message\\t01_02_c005_0101a.dat", any case, '/' or '\\') -> the file's path. Empty: none
    void setOverlay(const std::unordered_map<std::string, std::string>& files);
    const std::unordered_map<std::string, std::string>& overlay() const { return overlay_; }
    // its key, as setOverlay's map and overlay() have them
    static std::string key(const std::string& name);

private:
    std::string path_;
    std::FILE* file_ = nullptr;
    std::vector<Entry> entries_;
    std::unordered_map<std::string, size_t> byName_;
    std::unordered_map<std::string, std::string> overlay_;
};

} // namespace cr2
