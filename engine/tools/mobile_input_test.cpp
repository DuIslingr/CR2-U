#include "viewer/mobile_input.hpp"
#include "event/runtime.hpp"
#include "script/vm.hpp"
#include "audio/sounddata.hpp"
#include <squirrel.h>
#include <cstdio>
#include <cstring>
namespace game = TEST_NAMESPACE;
int main() {
    int problems = 0;
    mobile::MenuSwipe g;
    g.begin(0.5f, 0.7f, 1);
    problems += g.move(0.5f, 0.705f) != 0 || g.moved;
    problems += g.move(0.5f, 0.45f) != 2 || !g.moved;
    problems += mobile::scrollCursor(0, 7, -9) != 0;
    problems += mobile::scrollCursor(6, 7, 9) != 6;
    g.begin(0.5f, 0.5f, 2);
    problems += g.move(0.7f, 0.5f) != 0 || !g.moved; // a drag never becomes a tap
    bool flip = false;
    for (bool lock : {false, true}) for (bool direction : {false, true}) {
        flip = direction;
        mobile::orientation(nullptr, lock, flip);
        const char* expected = !lock ? "LandscapeLeft LandscapeRight" : direction ? "LandscapeRight" : "LandscapeLeft";
        problems += std::strcmp(SDL_GetHint(SDL_HINT_ORIENTATIONS), expected) != 0;
    }
    game::MvglArchive archive;
    game::GameState state;
    game::EventRuntime events(archive, state);
    events.autoPage = 0;
    game::ScriptVM vm;
    events.bindNatives(vm);
    int voices = 0, stops = 0;
    auto& hooks = game::soundHooks();
    hooks.stopAllVoices = [&](int) { voices = 0; ++stops; };
    hooks.playVoice = [&](int) { return ++voices; };
#ifdef TEST_CR3
    hooks.playVoiceNamed = [&](const std::string&) { return ++voices; };
    const char* script = "function line(){EvtWindow.SetMessageText(0,\"A voiced page\");Cr3Sound.PlayVoice(\"voice\");} function nextvoice(){Cr3Sound.PlayVoice(\"next\");}";
#else
    const char* script = "function line(){EvtWindow.SetMessageText(0,\"A voiced page\");Cr3Sound.PlayVoice(1);} function nextvoice(){Cr3Sound.PlayVoice(2);}";
#endif
    if (SQ_FAILED(sq_compilebuffer(vm.vm(), script, SQInteger(std::strlen(script)), "mobile-test", SQTrue)) || !vm.runMain() || !vm.call("line")) return 1;
    problems += voices != 1;
    events.advance(); // the first tap reveals the text, keeping the voice
    problems += voices != 1;
    events.advance(); // the second skips the completed message and its voice
    problems += voices != 0;
    vm.call("nextvoice"); vm.call("nextvoice");
    problems += voices != 1;
    const char* fade = "function fadeout(){Cr3Fade.FadeOut(1,0);}";
    if (SQ_FAILED(sq_compilebuffer(vm.vm(), fade, SQInteger(std::strlen(fade)), "fade-test", SQTrue)) || !vm.runMain() || !vm.call("fadeout")) return 1;
    problems += events.fade().target != 1;
    events.reset(); // both games discard a previous area's white fade on entry/reload
    problems += events.fade().alpha != 0 || events.fade().target != 0;
    hooks = {};
    std::printf("mobile input/audio: gesture thresholds, clamped list ends, orientation directions, text reveal and voice interruption (%d stops); %d problems\n", stops, problems);
    return problems ? 1 : 0;
}
