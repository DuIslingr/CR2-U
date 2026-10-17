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
// in CAF (bgmNN.caf: not decoded here).

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cr2 {

class SoundData {
public:
    // the release's sound beside the archive `archivePath` (resolveArchive's): false - none found
    bool open(const std::string& archivePath, std::string* error = nullptr);
    bool opened() const { return !bank_[0].path.empty(); }
    std::string describe() const; // where it was found (for the log)

    static constexpr int kTracks = 37;
    // a track's Ogg Vorbis (the MCA_ header skipped); false: none (or not Vorbis: the iPad's AAC)
    bool trackOgg(int track, std::vector<uint8_t>& out) const;
    // a track's loop start (a sample; -1: it doesn't loop - audio/soundtable.inc)
    static int trackLoop(int track);
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
};

// Ogg Vorbis decoding (stb_vorbis): a track opened, read in pieces, sought
class OggStream {
public:
    ~OggStream();
    bool open(std::vector<uint8_t> data);
    int rate() const { return rate_; }
    int channels() const { return channels_; }
    // up to `frames` frames of interleaved s16 into `out`; 0 at the end
    int read(int16_t* out, int frames);
    bool seek(uint32_t frame);
    uint32_t frames() const; // the whole length, in frames

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
