#include "event/runtime.hpp"

#include <algorithm>
#include <cctype>

namespace cr2 {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// the script's fade colours (Cr3FadeIn's table: 1..3 as they are, anything else black)
int fadeColour(int type) { return type >= 1 && type <= 3 ? type : 0; }

} // namespace

void EventFade::start(int c, int frames, float from, float to) {
    colour = c;
    alpha = from, target = to;
    step = frames > 0 ? std::abs(to - from) / float(frames) : 1.0f;
}

void EventFade::update() {
    if (alpha < target) alpha = std::min(alpha + step, target);
    else if (alpha > target) alpha = std::max(alpha - step, target);
}

EventRuntime::EventRuntime(const MvglArchive& archive, GameState& state) : archive_(archive), state_(state) {}

bool EventRuntime::loadText(const std::string& name, TextData& into) {
    std::vector<uint8_t> d;
    return archive_.read("message\\" + lower(name) + ".dat", d) && into.parse(d);
}

void EventRuntime::bindNatives(ScriptVM& vm) {
    // WaitFrame(n): the script waits n updates (ScriptLib::WaitFrame; WaitProcessor)
    vm.setNative("", "WaitFrame", [this](NativeCall& c) {
        wait_ = std::max(1, c.integer(0, 1));
        c.suspend();
    });
    // the scene
    ScriptVM* owner = &vm;
    vm.setNative("EvtScene", "Init", [this, owner](NativeCall& c) {
        std::string err;
        if (!scene_.load(archive_, c.string(0), &err)) owner->appendError("EvtScene.Init: " + err + "\n");
        terminated_ = drawSkip_ = false;
    });
    vm.setNative("EvtScene", "Terminate", [this](NativeCall&) { terminated_ = true; });
    vm.setNative("EvtScene", "SetDrawSkip", [this](NativeCall& c) { drawSkip_ = c.boolean(0); });
    vm.setNative("EvtScene", "SetAnimator", [](NativeCall&) {}); // (loads an animation ahead)
    vm.setNative("EvtScene", "Visible", [this](NativeCall& c) {
        if (EventModel* m = scene_.model(c.string(0))) m->visible = c.boolean(1);
    });
    vm.setNative("EvtScene", "IsVisible", [this](NativeCall& c) {
        const EventModel* m = scene_.model(c.string(0));
        c.result(m && m->visible);
    });
    // ChangeAnimator(model, "e826", loop): the model plays <figure>e826 from its start
    vm.setNative("EvtScene", "ChangeAnimator", [this](NativeCall& c) {
        if (EventModel* m = scene_.model(c.string(0))) {
            m->animation = m->figure + c.string(1);
            m->loop = c.boolean(2, true);
            m->animTime = 0;
            m->blendSeconds = 0;
        }
    });
    // ChangeAnimatorBlend(model, "e007", loop, frames): the same, faded in from the motion before
    vm.setNative("EvtScene", "ChangeAnimatorBlend", [this](NativeCall& c) {
        if (EventModel* m = scene_.model(c.string(0))) {
            m->blendFrom = m->animation.empty() ? m->figure : m->animation;
            m->blendFromTime = m->animTime;
            m->blendSeconds = float(std::max(c.integer(3), 0)) * kTick;
            m->animation = m->figure + c.string(1);
            m->loop = c.boolean(2, true);
            m->animTime = 0;
        }
    });
    // PlayEffect / PlayEffectLoop (Cr3EvtScenePlayEffect): shown, its own animation from the start,
    // once or looping
    auto effect = [this](NativeCall& c, bool loop) {
        if (EventModel* m = scene_.model(c.string(0))) m->visible = true, m->animTime = 0, m->loop = loop, m->blendSeconds = 0;
    };
    vm.setNative("EvtScene", "PlayEffect", [effect](NativeCall& c) { effect(c, false); });
    vm.setNative("EvtScene", "PlayEffectLoop", [effect](NativeCall& c) { effect(c, true); });
    vm.setNative("EvtScene", "SetFaceTexture", [this](NativeCall& c) { faces_[c.integer(0)] = c.string(1); });
    vm.setNative("EvtScene", "ChangeModelFace", [this](NativeCall& c) {
        if (EventModel* m = scene_.model(c.string(0))) m->face = c.integer(1, -1);
    });
    // the vista's timelines (in the scene's frames)
    vm.setNative("VistaEvent", "SetMove", [this](NativeCall& c) { scene_.setMove(c.string(0), c.number(1), c.number(2)); });
    vm.setNative("VistaEvent", "CameraMove", [this](NativeCall& c) { scene_.cameraMove(c.number(0), c.number(1)); });
    vm.setNative("VistaEvent", "LightMove", [this](NativeCall& c) { scene_.lightMove(c.number(0), c.number(1)); });
    // the player's slash (WaitPlayerSlash: ExecutionStart, then CheckExecution each frame until
    // the swipe; ExecutionEndStart the finishing one)
    auto execution = [this](NativeCall&) { execution_ = true, pageTime_ = 0; };
    vm.setNative("VistaEvent", "ExecutionStart", execution);
    vm.setNative("VistaEvent", "ExecutionEndStart", execution);
    vm.setNative("VistaEvent", "CheckExecution", [this](NativeCall& c) { c.result(!execution_); });
    // the message window
    auto textData = [this](NativeCall& c) { loadText(c.string(0), text_); };
    auto nameData = [this](NativeCall& c) { loadText(c.string(0), names_); };
    vm.setNative("EvtWindow", "LoadSharedTextData", textData);
    // (LoadMessageData(name), or (window, name): the tutorials' AnnounceTutorialTxt for 3 / 4)
    vm.setNative("EvtWindow", "LoadMessageData", [this](NativeCall& c) {
        if (!c.string(0).empty() || c.count() < 2) loadText(c.string(0), text_);
        else loadText(c.string(1), windowText_[c.integer(0)]);
    });
    vm.setNative("EvtWindow", "UnloadData", [this](NativeCall& c) { windowText_.erase(c.integer(0, -1)); });
    // the tutorials' pictures (TutorialPicture)
    auto pictureAdd = [this](NativeCall& c) {
        if (tutorialPicture_.images.size() < 5) tutorialPicture_.images.push_back(c.string(0));
    };
    auto pictureShow = [this](NativeCall& c) { tutorialPicture_.shown = c.integer(0, -1); };
    vm.setNative("Cr3FieldTutorial", "Create", [this](NativeCall&) { tutorialPicture_ = TutorialPicture(); });
    vm.setNative("Cr3FieldTutorial", "Delete", [this](NativeCall&) { tutorialPicture_ = TutorialPicture(); });
    vm.setNative("Cr3FieldTutorial", "LoadTexture", pictureAdd);
    vm.setNative("Cr3FieldTutorial", "SetTexture", pictureShow);
    vm.setNative("Cr3FieldTutorial", "Visible", [this](NativeCall& c) { tutorialPicture_.visible = c.boolean(0); });
    vm.setNative("InterfaceWindow", "GetTutorialID", [this](NativeCall& c) { c.result(tutorialId); });
    vm.setNative("InterfaceWindow", "SetTutorialTexture", pictureAdd);
    vm.setNative("InterfaceWindow", "SetTutorialTextureNumber", pictureShow);
    vm.setNative("InterfaceWindow", "VisibleTutorialTexture", [this](NativeCall&) { tutorialPicture_.visible = true; });
    vm.setNative("InterfaceWindow", "UnVisibleTutorialTexture", [this](NativeCall&) { tutorialPicture_.visible = false; });
    vm.setNative("EvtWindow", "LoadSharedNameData", nameData);
    vm.setNative("EvtWindow", "LoadNameData", nameData);
    // (a window argument of null: the vista's, Cr3Work 2's usual value)
    vm.setNative("EvtWindow", "Open", [this](NativeCall& c) {
        const int w = c.integer(0, Vista);
        if (openWindows_.insert(w).second) opened_[w] = 0, closing_.erase(w);
    });
    auto close = [this](int w) {
        if (openWindows_.erase(w)) opened_.erase(w), closing_[w] = 0;
        if (w == messageWindow_) message_ = 0;
    };
    vm.setNative("EvtWindow", "Close", [close](NativeCall& c) { close(c.integer(0, Vista)); });
    vm.setNative("EvtWindow", "UnloadAll", [this](NativeCall&) { openWindows_.clear(), opened_.clear(), closing_.clear(), message_ = 0; });
    // the cut-ins' portraits
    vm.setNative("EvtFace", "SetTexture", [this](NativeCall& c) { cutInImages_.push_back({c.integer(1), c.string(0)}); });
    vm.setNative("EvtFace", "DeleteTexture", [this](NativeCall&) { cutInImages_.clear(); });
    vm.setNative("EvtFace", "Create", [this](NativeCall& c) {
        const int k = c.integer(0, -1);
        if (k >= 0 && k < kCutIns) cutIns_[size_t(k)] = Portrait{true, false, 0, -1, 1};
    });
    auto endCutIn = [this](int n) {
        static const int kEnd[6] = {0, 1, 2, -1, 3, 4}; // (EndCutInWindow: 3 every one)
        if (n < 0 || n > 5) return;
        for (int k = 0; k < kCutIns; ++k)
            if ((n == 3 || kEnd[n] == k) && cutIns_[size_t(k)].open) cutIns_[size_t(k)].closing = true;
    };
    vm.setNative("EvtFace", "Delete", [endCutIn](NativeCall& c) { endCutIn(c.integer(0, -1)); });
    vm.setNative("EvtFace", "DispOff", [endCutIn](NativeCall&) { endCutIn(3); });
    vm.setNative("EvtFace", "ChangeTexture", [this](NativeCall& c) {
        const int k = c.integer(0, -1), id = c.integer(1, -1);
        if (k >= 0 && k < kCutIns && !cutInImage(id).empty()) cutIns_[size_t(k)].texture = id;
    });
    vm.setNative("EvtFace", "SetColor", [this](NativeCall& c) {
        const int k = c.integer(0, -1);
        if (k >= 0 && k < kCutIns) cutIns_[size_t(k)].bright = c.number(1, 1);
    });
    vm.setNative("EvtWindow", "SetMessage", [this](NativeCall& c) {
        messageWindow_ = c.integer(0, Vista);
        message_ = c.integer(1);
        const auto own = windowText_.find(messageWindow_);
        parsed_ = parseMessage((own != windowText_.end() ? own->second : text_).message(message_, language));
        page_ = 0, pageTime_ = 0, revealed_ = 0;
        if (parsed_.pages.empty()) message_ = 0;
        else ++messagesSet_;
    });
    vm.setNative("EvtWindow", "CheckMessage", [this](NativeCall& c) { c.result(message_ == 0); });
    vm.setNative("EvtWindow", "SkipMessage", [this](NativeCall&) { message_ = 0; });
    // the fades: (type, frames), the 2s (type, frames, from %, to %)
    vm.setNative("Cr3Fade", "FadeIn", [this](NativeCall& c) { fade_.start(fadeColour(c.integer(0)), c.integer(1), 1, 0); });
    vm.setNative("Cr3Fade", "FadeOut", [this](NativeCall& c) { fade_.start(fadeColour(c.integer(0)), c.integer(1), 0, 1); });
    auto fade2 = [this](NativeCall& c) {
        fade_.start(fadeColour(c.integer(0)), c.integer(1), c.number(2) / 100.0f, c.number(3) / 100.0f);
    };
    vm.setNative("Cr3Fade", "FadeIn2", fade2);
    vm.setNative("Cr3Fade", "FadeOut2", fade2);
    // the skip button: never touched here
    vm.setNative("EvtSkip", "CheckButtonTouch", [](NativeCall& c) { c.result(false); });
}

void EventRuntime::reset() {
    scene_ = EventScene();
    fade_ = EventFade();
    faces_.clear();
    text_ = TextData(), names_ = TextData();
    windowText_.clear(), tutorialPicture_ = TutorialPicture();
    openWindows_.clear(), opened_.clear(), closing_.clear();
    cutIns_ = {}, cutInImages_.clear();
    messageWindow_ = Vista, message_ = page_ = revealed_ = 0, pageTime_ = 0;
    execution_ = false;
    terminated_ = drawSkip_ = false;
    wait_ = frame_ = 0;
}

bool EventRuntime::start(const std::string& function) { return start(function, function); }

bool EventRuntime::start(const std::string& name, const std::string& function) {
    error_.clear();
    reset();
    state_.next.battle = -1;
    vm_ = std::make_unique<ScriptVM>();
    bindGameState(*vm_, state_);
    bindNatives(*vm_);
    std::vector<uint8_t> bytecode;
    const std::string script = "script\\" + lower(name) + ".nut";
    if (!archive_.read(script, bytecode, &error_) || !vm_->load(bytecode, script) || !vm_->runMain()) {
        if (error_.empty()) error_ = script + ": " + vm_->error();
        return false;
    }
    const std::string fn = vm_->findFunction(function);
    if (fn.empty()) return error_ = script + ": no function " + function, false;
    if (!vm_->call(fn)) return error_ = fn + ": " + vm_->error(), false;
    return true;
}

bool EventRuntime::running() const { return vm_ && vm_->waiting(); }

bool EventRuntime::tick() { return update(vm_.get()); }

bool EventRuntime::update(ScriptVM* vm) {
    ++frame_;
    scene_.update(kTick);
    if (vm && vm->waiting() && --wait_ <= 0) {
        wait_ = 0;
        if (!vm->resume()) return error_ = vm->error(), false;
    }
    if (message_ > 0 && !typed()) ++revealed_; // (a character an update; the page's time counts once it's typed)
    else if ((message_ > 0 || execution_) && autoPage > 0 && (pageTime_ += kTick) >= autoPage) advance();
    fade_.update();
    for (auto& [w, t] : opened_) t += kTick;
    for (auto it = closing_.begin(); it != closing_.end();) // (out animations: a second at most)
        it = (it->second += kTick) > 1.0f ? closing_.erase(it) : std::next(it);
    for (Portrait& c : cutIns_) {
        if (!c.open) continue;
        if (!c.closing) c.time = std::min(c.time + kTick, kCutInSeconds);
        else if ((c.time -= kTick) <= 0) c = Portrait(); // (back to its start: gone)
    }
    return true;
}

float EventRuntime::windowAge(int w) const {
    if (auto it = opened_.find(w); it != opened_.end()) return it->second;
    if (auto it = closing_.find(w); it != closing_.end()) return it->second;
    return -1;
}

std::string EventRuntime::cutInImage(int id) const {
    for (const auto& [i, image] : cutInImages_)
        if (i == id) return image;
    return {};
}

void EventRuntime::advance() {
    pageTime_ = 0;
    if (execution_) {
        execution_ = false;
        return;
    }
    if (message_ <= 0) return;
    if (!typed()) {
        revealed_ = 1 << 20;
        return;
    }
    revealed_ = 0;
    if (++page_ >= int(parsed_.pages.size())) message_ = 0, page_ = 0;
}

bool EventRuntime::typed() const {
    if (message_ <= 0 || page_ >= int(parsed_.pages.size())) return true;
    int n = 0;
    for (const TextLine& l : parsed_.pages[size_t(page_)].lines)
        for (unsigned char c : l.text) n += (c & 0xc0) != 0x80; // (code points)
    return revealed_ >= n;
}

std::string EventRuntime::speakerName() const {
    if (parsed_.speaker < 0) return {};
    const ParsedMessage n = parseMessage(names_.message(parsed_.speaker + 1, language));
    return n.pages.empty() || n.pages[0].lines.empty() ? std::string() : n.pages[0].lines[0].text;
}

std::map<std::string, int> EventRuntime::stubCalls() const { return vm_ ? vm_->stubCalls() : std::map<std::string, int>(); }

} // namespace cr2
