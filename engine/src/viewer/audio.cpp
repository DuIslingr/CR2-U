#include "viewer/audio.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

namespace cr2 {

namespace {

constexpr int kEffectSlots = 16, kVoiceSlots = 16;
constexpr float kUpdates = 20.0f; // (the game's updates a second: what fades count)
constexpr float kBgmAhead = 0.5f; // (seconds of the music kept queued)

} // namespace

AudioPlayer::~AudioPlayer() { close(); }

bool AudioPlayer::open(const std::string& archivePath, bool device, std::string* error) {
    close();
    if (!data_.open(archivePath, error)) return false;
    slots_.assign(kEffectSlots + kVoiceSlots, Voice());
    if (device) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            if (error) *error = std::string("SDL audio: ") + SDL_GetError();
        } else {
            device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
            if (!device_ && error) *error = std::string("no audio device: ") + SDL_GetError();
        }
    }
    SoundHooks& h = soundHooks();
    h.playBgm = [this](int t, int f) { playBgm(t, f); };
    h.stopBgm = [this](int f) { stopBgm(f); };
    h.releaseBgm = [this] { releaseBgm(); };
    h.state = [this](int which) { return which == 0 ? bgmState() : 2; };
    h.playSe = [this](int c, bool loop) { return playSe(c, loop); };
    h.stopSe = [this](int s, int f) { stopSe(s, f); };
    h.stopAllSe = [this](int f) { stopAllSe(f); };
    h.playVoice = [this](int c) { return playVoice(c); };
    h.stopAllVoices = [this](int f) { stopAllVoices(f); };
    return true;
}

void AudioPlayer::close() {
    if (!slots_.empty()) soundHooks() = SoundHooks();
    for (Voice& v : slots_) endVoice(v);
    slots_.clear();
    releaseBgm();
    bgmPos_ = 0, bgmFade_ = 1, bgmFadeStep_ = 0;
    if (device_) SDL_CloseAudioDevice(device_), device_ = 0;
    data_ = SoundData();
}

void AudioPlayer::setVolumes(float bgm, float effects, float voices) {
    volume_[0] = std::clamp(bgm, 0.0f, 1.0f), volume_[1] = std::clamp(effects, 0.0f, 1.0f), volume_[2] = std::clamp(voices, 0.0f, 1.0f);
    applyGains();
}

void AudioPlayer::applyGains() {
    if (bgm_) SDL_SetAudioStreamGain(bgm_, volume_[0] * bgmFade_);
    for (const Voice& v : slots_)
        if (v.stream) SDL_SetAudioStreamGain(v.stream, volume_[v.voice ? 2 : 1] * v.fade);
}

// Cr3UtilSoundPlayBGM: the track attached (Sound/bgmNN), its repeat frame, played, faded in
void AudioPlayer::playBgm(int track, int fadeFrames) {
    log.push_back("bgm " + std::to_string(track));
    releaseBgm();
    std::string err;
    std::unique_ptr<MusicStream> o = data_.openTrack(track, &err);
    if (!o) {
        SDL_Log("sound: bgm%02d - none to play (%s)", track, err.c_str());
        return;
    }
    track_ = track, bgmPlaying_ = true, bgmPos_ = 0;
    SDL_Log("sound: bgm%02d%s", track, device_ ? "" : " (silent)");
    bgmFade_ = fadeFrames > 0 ? 0.0f : 1.0f;
    bgmFadeStep_ = fadeFrames > 0 ? kUpdates / float(fadeFrames) : 0.0f;
    if (device_) {
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_S16, spec.channels = o->channels(), spec.freq = o->rate();
        bgm_ = SDL_CreateAudioStream(&spec, nullptr);
        if (bgm_ && !SDL_BindAudioStream(device_, bgm_)) SDL_DestroyAudioStream(bgm_), bgm_ = nullptr;
    }
    music_ = std::move(o);
    applyGains();
    feedBgm();
}

void AudioPlayer::feedBgm() {
    if (!music_ || !bgm_ || !bgmPlaying_) return;
    MusicStream& m = *music_;
    const int ch = m.channels(), rate = m.rate();
    const int want = int(kBgmAhead * float(rate)) * ch * 2;
    std::vector<int16_t> buf(4096 * size_t(ch));
    while (SDL_GetAudioStreamQueued(bgm_) < want) {
        int n = 4096;
        if (m.loopStart >= 0 && m.loopEnd >= 0) n = int(std::clamp<int64_t>(m.loopEnd + 1 - bgmPos_, 0, 4096)); // (up to the loop's end)
        int got = n > 0 ? m.read(buf.data(), n) : 0;
        if (got <= 0) { // (its end, or its loop's: back to the repeat frame, else over)
            if (m.loopStart < 0 || !m.seek(uint32_t(m.loopStart))) {
                SDL_FlushAudioStream(bgm_);
                bgmPlaying_ = false;
                return;
            }
            bgmPos_ = m.loopStart;
            continue;
        }
        bgmPos_ += got;
        SDL_PutAudioStreamData(bgm_, buf.data(), got * ch * 2);
    }
}

// Cr3UtilSoundStopBGM: faded out (SetFade 100 -> 0 over the frames), then stopped
void AudioPlayer::stopBgm(int fadeFrames) {
    log.push_back("stop bgm");
    if (fadeFrames <= 0) {
        if (bgm_) SDL_ClearAudioStream(bgm_);
        bgmPlaying_ = false;
        return;
    }
    bgmFadeStep_ = -kUpdates / float(fadeFrames);
}

void AudioPlayer::releaseBgm() {
    if (bgm_) SDL_DestroyAudioStream(bgm_), bgm_ = nullptr;
    music_.reset();
    bgmPlaying_ = false, track_ = 0;
}

int AudioPlayer::bgmState() const { return bgmPlaying_ ? 2 : -2; }

void AudioPlayer::endVoice(Voice& v) {
    if (v.stream) SDL_DestroyAudioStream(v.stream);
    v = Voice();
}

void AudioPlayer::startClip(Voice& v, SoundData::Bank bank, int clip, bool loop) {
    endVoice(v);
    SoundData::ClipFormat fmt;
    if (!data_.clip(bank, clip, v.pcm, &fmt) || v.pcm.empty()) return;
    v.rate = fmt.rate, v.channels = fmt.channels;
    v.active = true, v.loop = loop, v.clip = clip, v.voice = bank == SoundData::Voices;
    if (!device_) return;
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16, spec.channels = v.channels, spec.freq = v.rate;
    v.stream = SDL_CreateAudioStream(&spec, nullptr);
    if (!v.stream || !SDL_BindAudioStream(device_, v.stream)) {
        endVoice(v);
        return;
    }
    SDL_SetAudioStreamGain(v.stream, volume_[v.voice ? 2 : 1]);
    SDL_PutAudioStreamData(v.stream, v.pcm.data(), int(v.pcm.size() * 2));
    if (!loop) SDL_FlushAudioStream(v.stream);
}

// Cr3UtilSoundPlaySE (none at a volume of 0), PlaySE_Repeat (Cr3SoundSetRepeatOnce: looping)
int AudioPlayer::playSe(int clip, bool loop) {
    log.push_back("se " + std::to_string(clip) + (loop ? " (looping)" : ""));
    SDL_LogDebug(SDL_LOG_CATEGORY_AUDIO, "sound: se %d%s", clip, loop ? " (looping)" : "");
    if (volume_[1] <= 0) return -1;
    for (int k = 0; k < std::min(kEffectSlots, int(slots_.size())); ++k)
        if (!slots_[size_t(k)].active) {
            startClip(slots_[size_t(k)], SoundData::Effects, clip, loop);
            return slots_[size_t(k)].active ? k : -1;
        }
    return -1; // (all busy)
}

void AudioPlayer::stopSe(int slot, int fadeFrames) {
    if (slot < 0 || slot >= kEffectSlots || size_t(slot) >= slots_.size()) return;
    Voice& v = slots_[size_t(slot)];
    if (fadeFrames <= 0) endVoice(v);
    else v.fadeStep = -kUpdates / float(fadeFrames);
}

void AudioPlayer::stopAllSe(int fadeFrames) {
    for (int k = 0; k < std::min(kEffectSlots, int(slots_.size())); ++k) stopSe(k, fadeFrames);
}

// Cr3UtilSoundPlayVoice (none at a volume of 0)
int AudioPlayer::playVoice(int clip) {
    log.push_back("voice " + std::to_string(clip));
    SDL_LogDebug(SDL_LOG_CATEGORY_AUDIO, "sound: voice %d", clip);
    if (volume_[2] <= 0) return -1;
    for (int k = kEffectSlots; k < std::min(kEffectSlots + kVoiceSlots, int(slots_.size())); ++k)
        if (!slots_[size_t(k)].active) {
            startClip(slots_[size_t(k)], SoundData::Voices, clip, false);
            return slots_[size_t(k)].active ? k : -1;
        }
    return -1;
}

void AudioPlayer::stopAllVoices(int fadeFrames) {
    for (int k = kEffectSlots; k < std::min(kEffectSlots + kVoiceSlots, int(slots_.size())); ++k) {
        Voice& v = slots_[size_t(k)];
        if (fadeFrames <= 0) endVoice(v);
        else v.fadeStep = -kUpdates / float(fadeFrames);
    }
}

bool AudioPlayer::playing(int slot) const { return slot >= 0 && slot < int(slots_.size()) && slots_[size_t(slot)].active; }

void AudioPlayer::update(float dt) {
    // the music's fade (in, or out to a stop)
    if (bgmFadeStep_ != 0) {
        bgmFade_ += bgmFadeStep_ * dt;
        if (bgmFade_ >= 1) bgmFade_ = 1, bgmFadeStep_ = 0;
        if (bgmFade_ <= 0) {
            bgmFade_ = 0, bgmFadeStep_ = 0;
            if (bgm_) SDL_ClearAudioStream(bgm_);
            bgmPlaying_ = false;
        }
    }
    feedBgm();
    for (Voice& v : slots_) {
        if (!v.active) continue;
        if (v.fadeStep != 0) {
            v.fade += v.fadeStep * dt;
            if (v.fade <= 0) {
                endVoice(v);
                continue;
            }
        }
        if (!v.stream) { // (silent: over after its length)
            if (!v.loop) {
                const float left = float(v.pcm.size()) / float(v.rate * v.channels);
                v.pcm.resize(size_t(std::max(0.0f, left - dt) * float(v.rate)) * size_t(v.channels));
                if (v.pcm.empty()) endVoice(v);
            }
            continue;
        }
        if (v.loop) {
            if (SDL_GetAudioStreamQueued(v.stream) < int(v.pcm.size())) SDL_PutAudioStreamData(v.stream, v.pcm.data(), int(v.pcm.size() * 2));
        } else if (SDL_GetAudioStreamQueued(v.stream) == 0 && SDL_GetAudioStreamAvailable(v.stream) == 0) {
            endVoice(v);
        }
    }
    applyGains();
}

} // namespace cr2
