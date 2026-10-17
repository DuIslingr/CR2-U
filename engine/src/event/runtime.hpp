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
#include <array>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "assets/mvgl.hpp"
#include "assets/text.hpp"
#include "event/scene.hpp"
#include "script/gamestate.hpp"
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
    // another script's function (`script\<script>.nut`: title's manual_disp) - the same
    bool start(const std::string& script, const std::string& function);
    // one update; false: the script failed
    bool tick();
    bool running() const;     // the script hasn't ended

    // The same natives for another script (a field's: its events use the scene, the windows, the
    // fades too), and one update for it - the scene, its wait counted down (and it resumed), the
    // windows, the fade. reset(): every state back to none (a new scene).
    void bindNatives(ScriptVM& vm);
    bool update(ScriptVM* vm);
    void reset();
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
    // how long a window has been open, or (closing) since it closed - its in / out animations'
    // time (seconds, as of the last update); -1: neither
    float windowAge(int w) const;
    bool windowClosing(int w) const { return closing_.count(w) != 0; }
    int messageWindow() const { return messageWindow_; }
    int itemShown() const { return itemShown_; } // (window 7's item: EvtWindow::SetItemMessage)
    bool messageShown() const { return message_ > 0; }
    const ParsedMessage& message() const { return parsed_; }
    int page() const { return page_; }
    // the page's characters shown so far (its lines' code points in turn): MenuText types a line
    // a character an update (EvtWindow::UpdatePage: speed 1, unless skipping); typed() - all of it
    int revealed() const { return revealed_; }
    bool typed() const;
    std::string speakerName() const; // the speaker's name tag (name data), "" none
    int messagesSet() const { return messagesSet_; } // (SetMessages since the start: checks)
    // the scene waits for the player's slash (VistaEvent.ExecutionStart)
    bool awaitingSlash() const { return execution_; }
    // a tap (a swipe): the slash awaited, else the page typed at once, else the message's next
    // page, or its end (CheckMessage then says it's done)
    void advance();
    // pages turn (and slashes happen) by themselves after this many seconds (0: only advance())
    float autoPage = 3.0f;
    // The yes / no buttons (EvtWindow.ShowYesNoButton: BattleYNChoice - choice_io, circleBtn_fr 11
    // and 12 -, CheckYesNoButton its answer, 1 yes, 2 no, 0 none yet; HideYesNoButton). Asked
    // only when `askYesNo` (a player to answer: the viewer); else the check answers nothing (the
    // headless runs' scripts go on as without the buttons). With autoPage, yes after that long.
    bool askYesNo = false;
    // The fast-forward button (FastForward: `speedUp` on the message windows 0 .. 6, shown with a
    // page while EvtWindow.SetSkipButtonEnable allows - each Open allows it again): held
    // (EvtWindow::IsSkip), a page shows whole and turns at once, without a tap
    bool fastForward = false;
    bool fastForwardShown() const {
        return message_ > 0 && messageWindow_ >= 0 && messageWindow_ <= 6 && !skipOff_.count(messageWindow_);
    }
    bool yesNoShown() const { return yesNoShown_; }
    float yesNoAge() const { return yesNoAge_; }
    void answerYesNo(bool yes) {
        if (yesNoShown_) yesNo_ = yes ? 1 : 2;
    }
    int language = 1; // (Cr3UtilOptionLanguage: 1 English)

    // The cut-ins' portraits (EvtFace over InterfaceMain's CutInWindowMenus): five layouts -
    // Create(kind) 0 `cutInR_io` (InterfaceMain::SetCutInWindow: interface 24), 1 `cutInLR_io`
    // (23), 2 `cutInL_io` (22), 3 `cutInPty_io` (99), 4 `cutInSgl_io` (100) - each a face image
    // (SetTexture(file, id) loads it, ChangeTexture(kind, id) puts it on), its brightness
    // (SetColor: the one not speaking 0.5), its io animation forward on opening and back on
    // closing (Delete: EndCutInWindow's table - 0, 1, 2, every one, 3, 4)
    static constexpr int kCutIns = 5;
    static constexpr float kCutInSeconds = 0.2f; // (the io animators' range)
    struct Portrait {
        bool open = false, closing = false;
        float time = 0; // into the io animation
        int texture = -1;
        float bright = 1;
    };
    const Portrait& cutIn(int kind) const { return cutIns_[size_t(kind)]; }
    std::string cutInImage(int id) const; // a loaded face's image ("" none)

    // The tutorials' pictures (Cr3FieldTutorial in the field and the battles; InterfaceWindow's
    // SetTutorialTexture ... in the manual - InstructionsWindowMenu's tutorialPic): the images
    // loaded (LoadTexture / SetTutorialTexture, five at most), the one shown (SetTexture /
    // SetTutorialTextureNumber: its number), shown at all (Visible / VisibleTutorialTexture)
    struct TutorialPicture {
        std::vector<std::string> images;
        int shown = -1;
        bool visible = false;
    };
    const TutorialPicture& tutorialPicture() const { return tutorialPicture_; }
    int tutorialId = 0; // (InterfaceWindow.GetTutorialID: the manual's choice - its +0x24)

    // where the event went at its end: BattleIN's battle (-1 none)
    int battle() const { return state_.next.battle; }

    const std::string& error() const { return error_; }
    std::map<std::string, int> stubCalls() const;

private:
    bool loadText(const std::string& name, TextData& into);
    const MvglArchive& archive_;
    GameState& state_;
    std::unique_ptr<ScriptVM> vm_;
    EventScene scene_;
    EventFade fade_;
    std::map<int, std::string> faces_;
    TextData text_, names_;
    std::map<int, TextData> windowText_; // (LoadMessageData(window, name): that window's own)
    TextData itemNames_;                 // (message\ItemName: SetItemMessage's)
    bool yesNoShown_ = false;
    int yesNo_ = 0;
    float yesNoAge_ = 0;
    int itemShown_ = 0;                  // (the item SetItemMessage named: its icon)
    TutorialPicture tutorialPicture_;
    std::set<int> openWindows_;
    std::set<int> skipOff_; // (SetSkipButtonEnable false)
    std::map<int, float> opened_, closing_; // (window -> seconds since)
    std::array<Portrait, kCutIns> cutIns_;
    std::vector<std::pair<int, std::string>> cutInImages_; // (id, image: the first of an id counts)
    int messageWindow_ = Vista;
    int message_ = 0, page_ = 0, revealed_ = 0, messagesSet_ = 0;
    float pageTime_ = 0;
    ParsedMessage parsed_;
    bool execution_ = false;
    bool terminated_ = false, drawSkip_ = false;
    int wait_ = 0, frame_ = 0;
    std::string error_;
};

} // namespace cr2
