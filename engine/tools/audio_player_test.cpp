#include "viewer/audio.hpp"
#include <SDL3/SDL.h>
#include <cstdio>
namespace game = cr2;
int main(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_EVENTS)) return 1;
    int problems = 0;
    game::AudioPlayer audio;
    auto closed = [&] {
        problems += audio.opened();
        problems += audio.playSe(0, false) != -1;
        problems += audio.playVoice(0) != -1;
        audio.stopSe(0, 0);
        audio.stopAllSe(0);
        audio.stopAllVoices(0);
        audio.update(0.05f);
        problems += audio.playing(0) || audio.playing(16);
    };
    closed();
    audio.close();
    closed();
    std::string error;
    problems += audio.open("/no-such-chaos-rings-audio-archive", false, &error);
    closed();
    audio.close();
    if (argc>1) {
        error.clear();
        if (!audio.open(argv[1], true, &error)) return std::fprintf(stderr,"audio source: %s\n",error.c_str()),1;
        int voice = -1;
        for (int clip=0;clip<32 && voice<0;++clip) voice = audio.playVoice(clip);
        int effect = -1;
        for (int clip=0;clip<32 && effect<0;++clip) effect = audio.playSe(clip,true);
        problems += !error.empty(); // the dummy playback device must actually open
        problems += voice<0 || effect<0;
        audio.stopAllVoices(0);
        problems += audio.playing(voice) || !audio.playing(effect);
        audio.stopAllSe(0);
        problems += audio.playing(effect);
        audio.playBgm(1,0);
        problems += audio.bgmState()!=2;
        audio.close();
        problems += audio.bgmState()!=-2 || audio.bgmTrack()!=0;
        closed();
        std::printf("audio player: real voice/effect decode, independent stops and closed music state; %d problems\n",problems);
    }
    SDL_Quit();
    std::printf("audio player: unopened, closed and failed-open playback are safe; %d problems\n", problems);
    return problems ? 1 : 0;
}
