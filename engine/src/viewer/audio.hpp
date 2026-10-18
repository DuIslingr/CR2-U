#pragma once
// The game's sound played (MVGL::Sound's nodes over SDL3 audio streams): the music - one track at
// a time, streamed (the release's: Ogg Vorbis, AAC, ATRAC9), looping back to its repeat frame, faded in / out - and the
// effects' and voices' clips, 16 at a time each kind (the original's slots), an effect looping
// when PlaySE_Repeat asked. Volumes per kind (the options' BGM / Sound Effects / Voiceovers:
// cr3_system_data +0, +4, +8). Fades count the game's updates (CSndNode::_FadeVolumeProc, once an
// update - 20 a second). Without a device (`--capture`, `--mute`) it plays nothing but keeps the
// same state and logs what was asked.

#include <memory>
#include <string>
#include <vector>

#include "audio/sounddata.hpp"

struct SDL_AudioStream;

namespace cr2 {

class AudioPlayer {
public:
    ~AudioPlayer();
    // `device`: SDL's default playback (else silent); installs soundHooks()
    bool open(const std::string& archivePath, bool device, std::string* error = nullptr);
    bool opened() const { return data_.opened(); }
    void close();
    // every frame: the music's stream topped up, fades on by `dt`, finished clips freed
    void update(float dt);
    // 0..1 each
    void setVolumes(float bgm, float effects, float voices);

    void playBgm(int track, int fadeFrames);
    void stopBgm(int fadeFrames);
    void releaseBgm();
    int bgmState() const; // 2 playing, -2 none
    int bgmTrack() const { return track_; }
    int playSe(int clip, bool loop);
    void stopSe(int slot, int fadeFrames);
    void stopAllSe(int fadeFrames);
    int playVoice(int clip);
    void stopAllVoices(int fadeFrames);
    bool playing(int slot) const;

    std::vector<std::string> log; // (what was asked, in turn: checks)

private:
    struct Voice {
        SDL_AudioStream* stream = nullptr;
        std::vector<int16_t> pcm;
        bool loop = false, active = false, voice = false;
        int clip = -1;
        float fade = 1, fadeStep = 0; // (a stop's fade: its gain falling by fadeStep a second)
    };
    void startClip(Voice& v, SoundData::Bank bank, int clip, bool loop);
    void endVoice(Voice& v);
    void applyGains();
    SoundData data_;
    unsigned device_ = 0;
    // the music
    std::unique_ptr<MusicStream> music_;
    SDL_AudioStream* bgm_ = nullptr;
    int track_ = 0;
    int64_t bgmPos_ = 0; // (the music's next frame)
    bool bgmPlaying_ = false;
    float bgmFade_ = 1, bgmFadeStep_ = 0; // (gain, its change a second)
    void feedBgm();
    std::vector<Voice> slots_; // 0..15 effects, 16..31 voices
    float volume_[3] = {1, 1, 1};
};

} // namespace cr2
