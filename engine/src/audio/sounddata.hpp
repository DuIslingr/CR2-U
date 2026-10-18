#pragma once
// The game's sound (MVGL::Sound; docs/FORMATS.md, "Sound"): 37 music tracks and two banks - the
// effects (`Sound/sndeff`) and the voices (`Sound/voice`).
//
// The Android release keeps them in its OBB after the MDB1 archive, where the library's
// obbtable_pos / obbtable_size (40 u32 each, MVGL::Utilities::Fios::GetObbPos / GetObbSize) say:
// 0 the archive, 1..37 bgm01..bgm37, 0x26 sndeff, 0x27 voice. A track is an "MCA_" header of 0x400
// bytes then Ogg Vorbis (CBgmPlayer: the stream's own rate, stereo), looping back to its repeat
// frame (Cr3UtilSoundPlayBGM: Cr3SoundSetRepeatFrame from the library's table, a sample; -1 none)
// at its end. A bank (CPackage) is a table of {u32 offset, u32 size} (its first offset its
// length) over headerless signed 16-bit mono PCM at 22050 Hz (COpenSLESAudioPlayer::initialize
// 2, 1, 22050000 mHz); Cr3UtilSoundPlaySE / PlayVoice play entry n of theirs.
// The iPad's are the same banks as files (Sound.ios/sound/sndeff.bin, voice.bin), its music AAC
// in CAF (bgmNN.caf: FAAD2), looping at its own table's AAC packet. The Vita's music is ATRAC9 in
// SXD (Sound.psp2/sound/bgmNN.sxd: LibAtrac9), looping between its WAVE entry's points; its banks
// (sndeff.sxd, voice.sxd1 / .sxd2) are not read yet.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cr2 {

// A music track decoding: interleaved s16, sought by frame
class MusicStream {
public:
    virtual ~MusicStream() = default;
    virtual int rate() const = 0;
    virtual int channels() const = 0;
    // up to `frames` frames into `out`; 0 at the end
    virtual int read(int16_t* out, int frames) = 0;
    virtual bool seek(uint32_t frame) = 0;
    virtual uint32_t frames() const = 0; // the whole length
    // its loop, in its frames: back to loopStart once it reaches loopEnd (its end at -1); -1 none
    int64_t loopStart = -1, loopEnd = -1;
};

class SoundData {
public:
    // the release's sound beside the archive `archivePath` (resolveArchive's): false - none found
    bool open(const std::string& archivePath, std::string* error = nullptr);
    bool opened() const { return opened_; }
    std::string describe() const; // where it was found (for the log)

    static constexpr int kTracks = 37;
    // a track's Ogg Vorbis (the MCA_ header skipped); false: none (or not Vorbis: the iPad's AAC)
    bool trackOgg(int track, std::vector<uint8_t>& out) const;
    // a track's loop start (a sample; -1: it doesn't loop - audio/soundtable.inc)
    static int trackLoop(int track);
    // a track opened to play, its loop set (the release's: Android's sample, the iPad's AAC packet,
    // the Vita's WAVE entry); null: none, or no decoder for it (`error` says)
    std::unique_ptr<MusicStream> openTrack(int track, std::string* error = nullptr) const;
    // BtlSound::PlaySE(n)'s clip (-1 none)
    static int battleSe(int n);
    // Cr3VoiceTable(figure, kind): the voice clip of a figure ("j001", "m309": its first four
    // characters) for a kind (0..999; -1 none)
    static int voiceClip(const std::string& figure, int kind);

    enum Bank { Effects = 0, Voices = 1 };
    int clips(Bank b) const { return int(bank_[b].table.size()); }
    // a clip's samples (s16 mono, 22050 Hz); false: no such clip
    bool clip(Bank b, int n, std::vector<int16_t>& out) const;
    static constexpr int kRate = 22050;

private:
    struct Piece {
        std::string path;
        uint64_t offset = 0, size = 0;
    };
    struct BankFile {
        std::string path;
        uint64_t base = 0;
        std::vector<std::pair<uint32_t, uint32_t>> table; // {offset, size}
    };
    bool readPiece(const Piece& p, std::vector<uint8_t>& out) const;
    bool loadBank(BankFile& b, const std::string& path, uint64_t base, uint64_t size);
    Piece tracks_[kTracks];
    BankFile bank_[2];
    std::string from_;
    bool opened_ = false;
    enum class Kind { Android, Ipad, Vita } kind_ = Kind::Android;
};

// AAC in CAF (the iPad's bgmNN.caf: desc, kuki - the ESDS's AudioSpecificConfig -, pakt - the
// packets' sizes, the encoder's priming and remainder -, data)
struct CafAac {
    int rate = 44100, channels = 2, framesPerPacket = 1024;
    std::vector<uint8_t> config;
    std::vector<uint32_t> offset, size; // (the packets, in `data`)
    int64_t validFrames = 0;
    int priming = 0, remainder = 0;
    std::vector<uint8_t> data;
};
bool parseCafAac(const std::vector<uint8_t>& file, CafAac& out, std::string* error = nullptr);
// a CAF's AAC decoding (FAAD2): the priming dropped, cut to its valid frames; null without FAAD2
std::unique_ptr<MusicStream> openAac(std::vector<uint8_t> file, std::string* error = nullptr);

// SXD (the Vita's MVGL::Sound files): "SXDF", a 0x60-byte header, then chunks {id, u32 size,
// body} - REQD, NAME, WAVE (entries: +0x04 the codec / channels, +0x08 the rate, +0x14 the frames,
// +0x18 / +0x1c the loop's start and end, +0x20 the stream's size, +0x24 its offset from there,
// +0x2c the ATRAC9 config, a little-endian word), DATA
struct SxdWave {
    int rate = 48000, channels = 2;
    int64_t frames = 0, loopStart = -1, loopEnd = -1;
    uint8_t config[4] = {};
    size_t at = 0, size = 0; // (the stream, in the file)
};
bool parseSxdWaves(const std::vector<uint8_t>& file, std::vector<SxdWave>& out, std::string* error = nullptr);
// an SXD's first wave's ATRAC9 decoding (LibAtrac9); null without it
std::unique_ptr<MusicStream> openSxdAt9(std::vector<uint8_t> file, std::string* error = nullptr);

// Ogg Vorbis decoding (stb_vorbis): a track opened, read in pieces, sought
class OggStream : public MusicStream {
public:
    ~OggStream() override;
    bool open(std::vector<uint8_t> data);
    int rate() const override { return rate_; }
    int channels() const override { return channels_; }
    int read(int16_t* out, int frames) override;
    bool seek(uint32_t frame) override;
    uint32_t frames() const override;

private:
    std::vector<uint8_t> data_;
    void* v_ = nullptr;
    int rate_ = 0, channels_ = 0;
};

// What the scripts' Cr3Sound natives (and the battles' and menus' own sounds) do: set by a player
// (the viewer's); unset, they do nothing and say so as the game would with no sound - GetState
// -2 ("none")
struct SoundHooks {
    std::function<void(int track, int fadeFrames)> playBgm;
    std::function<void(int fadeFrames)> stopBgm;
    std::function<void()> releaseBgm;
    std::function<int(int which)> state; // (0 the music: 2 playing, -2 none; 1 the effects)
    std::function<int(int clip, bool loop)> playSe; // (a slot, -1 none)
    std::function<void(int slot, int fadeFrames)> stopSe;
    std::function<void(int fadeFrames)> stopAllSe;
    std::function<int(int clip)> playVoice;
    std::function<void(int fadeFrames)> stopAllVoices;
};
SoundHooks& soundHooks();

} // namespace cr2
