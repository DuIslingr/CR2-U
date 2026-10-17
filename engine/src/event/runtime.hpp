#pragma once
// An event playing (Cr3Event, VistaMain; docs/SCRIPTS.md): its script (`script\<event>.nut`, the
// function named after it) in the game's VM, with the natives an event drives - the scene's models,
// moves, camera and lights (EvtScene, VistaEvent: event/scene.hpp), the message window (EvtWindow,
// the text `message\<name>.dat`), the screen's fades (Cr3Fade), the story's state (Cr3Flag,
// Cr3Work) - and where it goes when it ends (Cr3System.BattleIN).
//
// The game updates 20 times a second (TestApp::onUpdate: no sooner than 0.05 s after the last), in
// GameMain::Update's order: the scene (VistaMain: EvtScene::Update, the camera, the lights), the
// script (ScriptManager::Update: a WaitFrame(n) - WaitProcessor - counts n updates down, then the
// script resumes), the windows, the fade. tick() is one of those updates.

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "assets/mvgl.hpp"
#include "assets/text.hpp"
#include "event/scene.hpp"
#include "field/fieldscript.hpp"
#include "script/vm.hpp"

namespace cr2 {

// The screen's fade (Cr3Fade over InterfaceMain's fade windows): a colour at an opacity moving
// linearly to its target over the frames the script gave
struct EventFade {
    int colour = 0;      // 0 black (fadeB), 1 white (fadeW), 2 fadeF, 3 the encounter's wipe
    float alpha = 0, target = 0, step = 0;
    void start(int colour, int frames, float from, float to);
    void update();
    // the opacity `fraction` (0..1) of an update on from the last
    float at(float fraction) const {
        const float d = step * std::clamp(fraction, 0.0f, 1.0f);
        return alpha < target ? std::min(alpha + d, target) : std::max(alpha - d, target);
    }
};

class EventRuntime {
public:
    static constexpr float kTick = 0.05f; // (one update)

    EventRuntime(const MvglArchive& archive, GameState& state);

    // the event `function` ("D01_01_0101_evt"): its script loaded and run up to its first wait.
    // False: no script, or it failed (error() says)
    bool start(const std::string& function);
    // one update; false: the script failed
    bool tick();
    bool running() const;     // the script hasn't ended
    int frame() const { return frame_; }

    EventScene& scene() { return scene_; }
    const EventScene& scene() const { return scene_; }
    bool sceneShown() const { return scene_.loaded() && !terminated_ && !drawSkip_; }
    const EventFade& fade() const { return fade_; }
    // the face textures EvtScene.SetFaceTexture set (slot -> image)
    const std::map<int, std::string>& faces() const { return faces_; }

    // the message windows (Cr3Event's: 0 cut-in, 1 vista, 2 / 3 announcements, 4 tutorial, 5 system,
    // 6 narration): which are open (EvtWindow.Open), which the message is on (SetMessage's first)
    enum Window { CutIn = 0, Vista = 1, AnnounceA = 2, AnnounceB = 3, Tutorial = 4, System = 5, Narration = 6 };
    bool windowOpen() const { return !openWindows_.empty(); }
    bool windowOpen(int w) const { return openWindows_.count(w) != 0; }
    int messageWindow() const { return messageWindow_; }
    bool messageShown() const { return message_ > 0; }
    const ParsedMessage& message() const { return parsed_; }
    int page() const { return page_; }
    // the page's characters shown so far (its lines' code points in turn): MenuText types a line
    // a character an update (EvtWindow::UpdatePage: speed 1, unless skipping); typed() - all of it
    int revealed() const { return revealed_; }
    bool typed() const;
    std::string speakerName() const; // the speaker's name tag (name data), "" none
    // the scene waits for the player's slash (VistaEvent.ExecutionStart)
    bool awaitingSlash() const { return execution_; }
    // a tap (a swipe): the slash awaited, else the page typed at once, else the message's next
    // page, or its end (CheckMessage then says it's done)
    void advance();
    // pages turn (and slashes happen) by themselves after this many seconds (0: only advance())
    float autoPage = 3.0f;
    int language = 1; // (Cr3UtilOptionLanguage: 1 English)

    // where the event went at its end: BattleIN's battle (-1 none)
    int battle() const { return battle_; }

    const std::string& error() const { return error_; }
    std::map<std::string, int> stubCalls() const;

private:
    void bind();
    bool loadText(const std::string& name, TextData& into);
    const MvglArchive& archive_;
    GameState& state_;
    std::unique_ptr<ScriptVM> vm_;
    EventScene scene_;
    EventFade fade_;
    std::map<int, std::string> faces_;
    TextData text_, names_;
    std::set<int> openWindows_;
    int messageWindow_ = Vista;
    int message_ = 0, page_ = 0, revealed_ = 0;
    float pageTime_ = 0;
    ParsedMessage parsed_;
    bool execution_ = false;
    bool terminated_ = false, drawSkip_ = false;
    int wait_ = 0, frame_ = 0, battle_ = -1;
    std::string error_;
};

} // namespace cr2
