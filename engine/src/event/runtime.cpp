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

void EventRuntime::bind() {
    ScriptVM& vm = *vm_;
    bindGameState(vm, state_);
    // WaitFrame(n): the script waits n updates (ScriptLib::WaitFrame; WaitProcessor)
    vm.setNative("", "WaitFrame", [this](NativeCall& c) {
        wait_ = std::max(1, c.integer(0, 1));
        c.suspend();
    });
    // the scene
    vm.setNative("EvtScene", "Init", [this](NativeCall& c) {
        std::string err;
        if (!scene_.load(archive_, c.string(0), &err)) vm_->appendError("EvtScene.Init: " + err + "\n");
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
        }
    });
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
    vm.setNative("EvtWindow", "LoadMessageData", textData);
    vm.setNative("EvtWindow", "LoadSharedNameData", nameData);
    vm.setNative("EvtWindow", "LoadNameData", nameData);
    vm.setNative("EvtWindow", "Open", [this](NativeCall&) { windowOpen_ = true; });
    vm.setNative("EvtWindow", "Close", [this](NativeCall&) { windowOpen_ = false, message_ = 0; });
    vm.setNative("EvtWindow", "UnloadAll", [this](NativeCall&) { windowOpen_ = false, message_ = 0; });
    vm.setNative("EvtWindow", "SetMessage", [this](NativeCall& c) {
        message_ = c.integer(1);
        parsed_ = parseMessage(text_.message(message_, language));
        page_ = 0, pageTime_ = 0;
        if (parsed_.pages.empty()) message_ = 0;
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
    // where the event leads
    vm.setNative("Cr3System", "BattleIN", [this](NativeCall& c) { battle_ = c.integer(0); });
}

bool EventRuntime::start(const std::string& function) {
    error_.clear();
    scene_ = EventScene();
    fade_ = EventFade();
    faces_.clear();
    text_ = TextData(), names_ = TextData();
    windowOpen_ = false, message_ = page_ = 0, pageTime_ = 0;
    execution_ = false;
    terminated_ = drawSkip_ = false;
    wait_ = frame_ = 0, battle_ = -1;
    vm_ = std::make_unique<ScriptVM>();
    bind();
    std::vector<uint8_t> bytecode;
    const std::string script = "script\\" + lower(function) + ".nut";
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

bool EventRuntime::tick() {
    ++frame_;
    scene_.update(kTick);
    if (vm_ && vm_->waiting() && --wait_ <= 0) {
        wait_ = 0;
        if (!vm_->resume()) return error_ = vm_->error(), false;
    }
    if ((message_ > 0 || execution_) && autoPage > 0 && (pageTime_ += kTick) >= autoPage) advance();
    fade_.update();
    return true;
}

void EventRuntime::advance() {
    pageTime_ = 0;
    if (execution_) {
        execution_ = false;
        return;
    }
    if (message_ <= 0) return;
    if (++page_ >= int(parsed_.pages.size())) message_ = 0, page_ = 0;
}

std::string EventRuntime::speakerName() const {
    if (parsed_.speaker < 0) return {};
    const ParsedMessage n = parseMessage(names_.message(parsed_.speaker + 1, language));
    return n.pages.empty() || n.pages[0].lines.empty() ? std::string() : n.pages[0].lines[0].text;
}

std::map<std::string, int> EventRuntime::stubCalls() const { return vm_ ? vm_->stubCalls() : std::map<std::string, int>(); }

} // namespace cr2
