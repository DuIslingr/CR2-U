#include "event/runtime.hpp"
#include "script/vm.hpp"
#include <squirrel.h>
#include <cstdio>
#include <cstring>
namespace game = TEST_NAMESPACE;

int main() {
    game::MvglArchive archive;
    game::GameState state;
    game::EventRuntime events(archive, state);
    game::ScriptVM vm;
    events.bindNatives(vm);
    int problems = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { ++problems; std::printf("scene reset FAILED: %s\n", what); }
    };
    auto script = [&](const char* source, const char* function) {
        if (SQ_FAILED(sq_compilebuffer(vm.vm(), source, SQInteger(std::strlen(source)), "scene-reset", SQTrue)) || !vm.runMain() || !vm.call(function)) {
            std::printf("scene reset script failed: %s\n", vm.error().c_str());
            ++problems;
            return false;
        }
        return true;
    };
    events.autoPage = 0;
    events.askYesNo = true;
    events.language = 3;
    events.tutorialId = 12;
    events.touch();
    const char* dirty = R"(
function dirty() {
    VistaEvent.SetMapAnimPause(true);
    Cr3Fade.SetEffectLine();
    EvtSkip.ButtonCreate();
    EvtWindow.Open(1);
    EvtWindow.SetMessageText(1, "Previous scene");
    EvtWindow.SetSkipButtonEnable(1, false);
    EvtWindow.SetWaitButtonEnable(1, false);
    VistaEvent.ExecutionEndStart();
}
)";
    if (!script(dirty, "dirty")) return 1;
    check(events.mapAnimPaused && events.effectLine && events.skipButtonShown() && events.finishingSlash(), "old scene setup");
    events.fastForward = true;
    events.reset();
    check(!events.mapAnimPaused && !events.effectLine, "new scene resumes the map and removes the old screen effect");
    check(!events.skipButtonShown() && !events.fastForward, "old skip controls and held state are gone");
    check(!events.awaitingSlash() && !events.finishingSlash(), "execution finale does not survive the scene");
    check(!events.windowOpen() && !events.messageShown() && events.messagesSet() == 0, "old dialogue and its counters are gone");
    check(events.autoPage == 0 && events.askYesNo && events.language == 3 && events.tutorialId == 12, "host playback policy and language/manual selection survive");
    script("function noTouch(){if(Cr3Touch.GetTouchState()!=0)throw \"old tap reached the new scene\";}", "noTouch");
    if (!script("function nextMessage(){EvtWindow.SetMessageText(1,\"Fresh dialogue\");}", "nextMessage")) return 1;
    check(events.fastForwardShown() && events.messagesSet() == 1, "fresh dialogue has normal skip permission");
    events.advance(); // reveal
    events.advance(); // dismiss, without needing a new Open to repair stale permissions
    check(!events.messageShown(), "fresh dialogue advances with a tap");
    // a page waits for the player with autoPage 0, however long; a \\* line that ends by itself
    // stays on screen, done for the script, until the next message or its window closes
    if (!script("function said(){EvtWindow.Open(1);EvtWindow.SetMessageText(1,\"Waits\");}", "said")) return 1;
    for (int k = 0; k < 2000; ++k) events.update(&vm);
    check(events.messageShown(), "a page waits for a tap");
    script("function waiting(){if(EvtWindow.CheckMessage(1))throw \"a page turned by itself\";}", "waiting");
    if (!script("function auto(){EvtWindow.SetMessageText(1,\"Ends\\\\*\");}", "auto")) return 1;
    for (int k = 0; k < 200; ++k) events.update(&vm);
    check(events.messageShown() && events.message().ends, "a line that ended by itself stays on screen");
    check(!events.fastForwardShown(), "no fast-forward over a line that has ended");
    script("function ended(){if(!EvtWindow.CheckMessage(1))throw \"the script still waits\";}", "ended");
    events.advance();
    check(events.messageShown(), "a tap leaves it for the script to replace");
    if (!script("function closing(){EvtWindow.Close(1);}", "closing")) return 1;
    check(!events.messageShown(), "its window's close removes it");
#ifdef TEST_CR3
    const char* menus = R"(
function menus() {
    VistaEvent.LoadSecondMap("old_map");
    VistaEvent.ChangeMap();
    InterfaceWindow.CreateSelectThreeChoiceMenu();
    InterfaceWindow.CreateFastForwardWindow();
    InterfaceWindow.SetVisibleFastForwardWindow(true);
    InterfaceWindow.CreateLevelUpMenu();
    InterfaceWindow.CreateFieldKeyBuyMenu();
    InterfaceWindow.CreateClearQuestDetailMenu(7);
    Cr3System.ShowInputDialog(1);
    EvtIntroduction.SetDungeonNum(3);
    EvtIntroduction.SetDungeonLevel(4);
}
)";
    if (!script(menus, "menus")) return 1;
    events.openGeneGet(1, 2, true, false);
    events.collectListOpen(1, 2, 3);
    events.openWorldSelect(false);
    events.openGeneLab("clerk", false);
    check(events.mapChanged && !events.secondMap.empty() && events.choiceShown() == 3 && events.skipButtonShown() && events.inputShown(), "old secondary map and menus setup");
    events.reset();
    check(!events.mapChanged && events.secondMap.empty(), "new vista uses its own map");
    check(!events.choiceShown() && !events.skipButtonShown() && !events.inputShown(), "old choice, fast-forward window and text input are closed");
    check(!events.geneGet().open && !events.collectList().open && !events.worldSelect().open && !events.geneLab().open, "old reward, collection and destination menus are closed");
    check(events.levelUpShown() < 0 && !events.keyBuyShown() && !events.clearQuestShown() && events.mapName() < 0, "old banners and quest overlays are gone");
#endif
    // reset is idempotent and leaves existing native bindings usable.
    events.reset();
    script("function finalMessage(){EvtWindow.Open(1);EvtWindow.SetMessageText(1,\"Still bound\");}", "finalMessage");
    check(events.windowOpen(1) && events.messageShown() && events.messagesSet() == 1, "native bindings still work after repeated reset");
    std::printf("scene reset: %d problems\n", problems);
    return problems ? 1 : 0;
}
