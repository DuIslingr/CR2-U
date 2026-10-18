#include "audio/sounddata.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "assets/release.hpp"

#define STB_VORBIS_HEADER_ONLY
#include "../third_party/stb/stb_vorbis.c"

namespace cr2 {

namespace {

#include "audio/soundtable.inc"

namespace fs = std::filesystem;

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }

bool fail(std::string* error, const std::string& why) {
    if (error) *error = why;
    return false;
}

// An ELF32 library's symbol (.dynsym, else .symtab) and the file offset of its address
struct Elf {
    const std::vector<uint8_t>& d;
    bool fileOffset(uint32_t va, size_t& out) const {
        if (d.size() < 0x34) return false;
        const uint32_t phoff = le32(&d[0x1c]);
        const uint16_t phnum = le16(&d[0x2c]);
        for (uint16_t i = 0; i < phnum; ++i) {
            const size_t p = phoff + size_t(i) * 32;
            if (p + 32 > d.size() || le32(&d[p]) != 1) continue; // (PT_LOAD)
            const uint32_t off = le32(&d[p + 4]), vaddr = le32(&d[p + 8]), filesz = le32(&d[p + 16]);
            if (va >= vaddr && va < vaddr + filesz) return out = off + (va - vaddr), out < d.size();
        }
        return false;
    }
    bool symbol(const char* name, uint32_t& value) const {
        if (d.size() < 0x34 || std::memcmp(d.data(), "\x7f" "ELF", 4) != 0 || d[4] != 1) return false;
        const uint32_t shoff = le32(&d[0x20]);
        const uint16_t shnum = le16(&d[0x30]);
        for (uint32_t want : {11u, 2u}) // (SHT_DYNSYM, SHT_SYMTAB)
            for (uint16_t i = 0; i < shnum; ++i) {
                const size_t s = shoff + size_t(i) * 40;
                if (s + 40 > d.size() || le32(&d[s + 4]) != want) continue;
                const uint32_t off = le32(&d[s + 16]), size = le32(&d[s + 20]), link = le32(&d[s + 24]);
                const size_t str = shoff + size_t(link) * 40;
                if (str + 40 > d.size()) continue;
                const uint32_t stroff = le32(&d[str + 16]), strsize = le32(&d[str + 20]);
                for (uint32_t e = off; e + 16 <= off + size && e + 16 <= d.size(); e += 16) {
                    const uint32_t n = le32(&d[e]);
                    if (n >= strsize || stroff + n >= d.size()) continue;
                    if (std::strcmp(reinterpret_cast<const char*>(&d[stroff + n]), name) == 0) return value = le32(&d[e + 4]), true;
                }
            }
        return false;
    }
};

} // namespace

SoundHooks& soundHooks() {
    static SoundHooks hooks;
    return hooks;
}

bool SoundData::readPiece(const Piece& p, std::vector<uint8_t>& out) const {
    if (p.path.empty() || p.size == 0) return false;
    std::ifstream f(p.path, std::ios::binary);
    if (!f) return false;
    f.seekg(std::streamoff(p.offset));
    out.resize(size_t(p.size));
    f.read(reinterpret_cast<char*>(out.data()), std::streamsize(out.size()));
    return size_t(f.gcount()) == out.size();
}

bool SoundData::loadBank(BankFile& b, const std::string& path, uint64_t base, uint64_t size) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(std::streamoff(base));
    uint8_t head[8];
    if (!f.read(reinterpret_cast<char*>(head), 8)) return false;
    const uint32_t tableSize = le32(head); // (the first clip's offset: the table's end)
    if (tableSize < 8 || tableSize % 8 || tableSize > size) return false;
    std::vector<uint8_t> t(tableSize);
    f.seekg(std::streamoff(base));
    if (!f.read(reinterpret_cast<char*>(t.data()), std::streamsize(t.size()))) return false;
    b.path = path, b.base = base, b.table.clear();
    for (size_t k = 0; k + 8 <= t.size(); k += 8) b.table.push_back({le32(&t[k]), le32(&t[k + 4])});
    return true;
}

bool SoundData::open(const std::string& archivePath, std::string* error) {
    const std::string archive = resolveArchive(archivePath);
    const fs::path dir = fs::path(archive).parent_path();
    std::error_code ec;
    // the Android release: the OBB itself, its table in the APK's library
    if (archive.size() > 4 && archive.compare(archive.size() - 4, 4, ".obb") == 0) {
        std::vector<uint8_t> lib;
        if (!androidFile(archive, "lib/armeabi-v7a/libChaosRings2.so", lib)) return fail(error, "the APK's library is not beside the OBB");
        const Elf elf{lib};
        uint32_t posVa = 0, sizeVa = 0;
        size_t posAt = 0, sizeAt = 0;
        if (!elf.symbol("obbtable_pos", posVa) || !elf.symbol("obbtable_size", sizeVa) || !elf.fileOffset(posVa, posAt) ||
            !elf.fileOffset(sizeVa, sizeAt) || posAt + 160 > lib.size() || sizeAt + 160 > lib.size())
            return fail(error, "no obbtable in the library");
        auto pos = [&](int k) { return le32(&lib[posAt + size_t(k) * 4]); };
        auto size = [&](int k) { return le32(&lib[sizeAt + size_t(k) * 4]); };
        for (int t = 0; t < kTracks; ++t) tracks_[t] = Piece{archive, pos(t + 1), size(t + 1)};
        if (!loadBank(bank_[Effects], archive, pos(0x26), size(0x26)) || !loadBank(bank_[Voices], archive, pos(0x27), size(0x27)))
            return fail(error, "the OBB's sound banks don't read");
        from_ = archive + " (the OBB's table from the APK's library)";
        kind_ = Kind::Android;
        return opened_ = true;
    }
    // the iPad's: Sound.ios/sound beside its archive
    const fs::path snd = dir / "Sound.ios" / "sound";
    if (fs::exists(snd / "sndeff.bin", ec)) {
        auto fileSize = [&](const fs::path& p) { return uint64_t(fs::file_size(p, ec)); };
        if (!loadBank(bank_[Effects], (snd / "sndeff.bin").string(), 0, fileSize(snd / "sndeff.bin")) ||
            !loadBank(bank_[Voices], (snd / "voice.bin").string(), 0, fileSize(snd / "voice.bin")))
            return fail(error, "the iPad's sound banks don't read");
        for (int t = 0; t < kTracks; ++t) {
            char n[16];
            std::snprintf(n, sizeof n, "bgm%02d.caf", t + 1);
            if (fs::exists(snd / n, ec)) tracks_[t] = Piece{(snd / n).string(), 0, fileSize(snd / n)};
        }
#ifdef CR2_HAVE_FAAD2
        from_ = snd.string();
#else
        from_ = snd.string() + " (the music AAC: built without FAAD2)";
#endif
        kind_ = Kind::Ipad;
        return opened_ = true;
    }
    // the Vita's: Sound.psp2/sound beside its archive - the music (bgmNN.sxd); its banks not read
    const fs::path vita = dir / "Sound.psp2" / "sound";
    if (fs::exists(vita / "bgm01.sxd", ec)) {
        for (int t = 0; t < kTracks; ++t) {
            char n[16];
            std::snprintf(n, sizeof n, "bgm%02d.sxd", t + 1);
            if (fs::exists(vita / n, ec)) tracks_[t] = Piece{(vita / n).string(), 0, uint64_t(fs::file_size(vita / n, ec))};
        }
        // the banks: sndeff.sxd, voice.sxd1's entries over voice.sxd2
        auto bank = [&](int b, const char* head, const char* data) {
            std::ifstream f(vita / head, std::ios::binary);
            if (!f) return false;
            const std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (!parseSxdWaves(d, vitaWaves_[b])) return false;
            vitaFile_[b] = (vita / data).string();
            return true;
        };
        const bool banks = bank(Effects, "sndeff.sxd", "sndeff.sxd") && bank(Voices, "voice.sxd1", "voice.sxd2");
        from_ = vita.string() + (banks ? "" : " (its banks don't read)");
        kind_ = Kind::Vita;
        return opened_ = true;
    }
    return fail(error, "no sound beside " + archive);
}

std::string SoundData::describe() const { return from_; }

bool SoundData::trackOgg(int track, std::vector<uint8_t>& out) const {
    if (track < 1 || track > kTracks) return false;
    std::vector<uint8_t> d;
    if (!readPiece(tracks_[track - 1], d) || d.size() < 0x404 || std::memcmp(&d[0x400], "OggS", 4) != 0) return false;
    out.assign(d.begin() + 0x400, d.end()); // (the MCA_ header)
    return true;
}

int SoundData::trackLoop(int track) { return track >= 1 && track <= kTracks ? kBgmLoop[track - 1] : -1; }

std::unique_ptr<MusicStream> SoundData::openTrack(int track, std::string* error) const {
    std::vector<uint8_t> d;
    if (track < 1 || track > kTracks || !readPiece(tracks_[track - 1], d)) {
        if (error) *error = "no such track";
        return nullptr;
    }
    if (kind_ == Kind::Ipad) { // (its own table: an AAC packet, the priming counted)
        CafAac caf;
        if (!parseCafAac(d, caf, error)) return nullptr;
        std::unique_ptr<MusicStream> s = openAac(std::move(d), error);
        const int packet = kBgmLoopIpad[track - 1];
        const int64_t at = int64_t(packet) * caf.framesPerPacket - caf.priming;
        if (s && packet >= 0 && at >= 0 && at < int64_t(s->frames())) s->loopStart = at;
        return s;
    }
    if (kind_ == Kind::Vita) return openSxdAt9(std::move(d), error); // (its WAVE entry's loop)
    auto o = std::make_unique<OggStream>();
    if (d.size() < 0x404 || std::memcmp(&d[0x400], "OggS", 4) != 0 || !o->open(std::vector<uint8_t>(d.begin() + 0x400, d.end()))) {
        if (error) *error = "not Ogg Vorbis";
        return nullptr;
    }
    const int loop = kBgmLoop[track - 1];
    if (loop >= 0 && uint32_t(loop) < o->frames()) o->loopStart = loop; // (bgm27..29's: past their ends - once)
    return o;
}

int SoundData::battleSe(int n) { return n >= 1 && n < int(sizeof kBattleSe / sizeof kBattleSe[0]) ? kBattleSe[n] : -1; }

int SoundData::voiceClip(const std::string& figure, int kind) {
    if (figure.size() < 4 || kind < 0 || kind > 999) return -1;
    for (const VoiceEntry& e : kVoices)
        if (e.kind == kind && figure.compare(0, 4, kVoiceFigures[e.figure]) == 0) return e.clip;
    return -1;
}

int SoundData::clips(Bank b) const { return kind_ == Kind::Vita ? int(vitaWaves_[b].size()) : int(bank_[b].table.size()); }

bool SoundData::clip(Bank b, int n, std::vector<int16_t>& out, ClipFormat* format) const {
    if (kind_ == Kind::Vita) { // (the Vita's: wave n of the bank's SXD)
        if (n < 0 || n >= int(vitaWaves_[b].size())) return false;
        const SxdWave& w = vitaWaves_[b][size_t(n)];
        std::vector<uint8_t> d;
        if (!readPiece(Piece{vitaFile_[b], w.at, w.size}, d) || !decodeSxdWave(w, d.data(), d.size(), out)) return false;
        if (format) format->rate = w.rate, format->channels = w.channels;
        return true;
    }
    if (format) *format = ClipFormat();
    const BankFile& f = bank_[b];
    if (n < 0 || n >= int(f.table.size())) return false;
    const auto [offset, size] = f.table[size_t(n)];
    std::vector<uint8_t> d;
    if (!readPiece(Piece{f.path, f.base + offset, size}, d)) return false;
    out.resize(d.size() / 2);
    for (size_t k = 0; k < out.size(); ++k) out[k] = int16_t(le16(&d[k * 2]));
    return true;
}

OggStream::~OggStream() {
    if (v_) stb_vorbis_close(static_cast<stb_vorbis*>(v_));
}

bool OggStream::open(std::vector<uint8_t> data) {
    if (v_) stb_vorbis_close(static_cast<stb_vorbis*>(v_)), v_ = nullptr;
    data_ = std::move(data);
    int err = 0;
    stb_vorbis* v = stb_vorbis_open_memory(data_.data(), int(data_.size()), &err, nullptr);
    if (!v) return false;
    const stb_vorbis_info info = stb_vorbis_get_info(v);
    v_ = v, rate_ = int(info.sample_rate), channels_ = info.channels;
    return true;
}

int OggStream::read(int16_t* out, int frames) {
    if (!v_) return 0;
    return stb_vorbis_get_samples_short_interleaved(static_cast<stb_vorbis*>(v_), channels_, out, frames * channels_);
}

bool OggStream::seek(uint32_t frame) { return v_ && stb_vorbis_seek(static_cast<stb_vorbis*>(v_), frame); }

uint32_t OggStream::frames() const { return v_ ? stb_vorbis_stream_length_in_samples(static_cast<stb_vorbis*>(v_)) : 0; }

} // namespace cr2
