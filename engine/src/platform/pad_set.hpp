#pragma once
#include <SDL3/SDL.h>
#include <cstdlib>
#include <vector>

namespace crinput {
// The controllers: every gamepad that comes is opened - SDL sends a pad's buttons and sticks only once it is - and
// one is the active one, whose buttons and sticks the game reads. A button, or a stick or trigger pushed past half
// way, on another open pad makes that one active; the one before stays open. As CRO-U's 0e413d1: a Flydigi Vader 4
// Pro on Windows comes as two "Xbox 360 Controller" devices, its input on the second - with only the first opened
// it was found and dead (CRO-U #4, metastigma's log). The active pad gone, another open one takes over.
class PadSet {
public:
    SDL_Gamepad* active() const { return pad_; }
    // The pads a test takes, by joystick id - its own virtual ones - while it runs (none: every pad, as in play). Another
    // session's virtual "Xbox 360 Controller" (Vita3K driven through uinput) cost CR3-U's suite three battle cases; as
    // CRO-U's 80f34cb, CR3-U's 537c560: an outside pad is never opened, taken nor handed over to
    std::vector<SDL_JoystickID> allow;
    bool allowed(SDL_JoystickID id) const {
        if (allow.empty()) return true;
        for (SDL_JoystickID a : allow)
            if (a == id) return true;
        return false;
    }
    // SDL_EVENT_GAMEPAD_ADDED: opened; the active one if none is (or the one held went quiet: a wireless pad's
    // receiver keeps a dead handle). True when the active pad changed
    bool added(SDL_JoystickID id) {
        if (!allowed(id) || find(id)) return false;
        SDL_Gamepad* g = SDL_OpenGamepad(id);
        if (!g) return false;
        if (!pad_ || !SDL_GamepadConnected(pad_)) {
            if (pad_) SDL_CloseGamepad(pad_);
            pad_ = g;
            return true;
        }
        more_.push_back(g);
        return false;
    }
    // SDL_EVENT_GAMEPAD_REMOVED: closed; the active one gone, the first other still open takes over (else one SDL still
    // has, opened). True when the active pad changed
    bool removed(SDL_JoystickID id) {
        for (size_t k = 0; k < more_.size(); ++k)
            if (SDL_GetGamepadID(more_[k]) == id) {
                SDL_CloseGamepad(more_[k]);
                more_.erase(more_.begin() + std::ptrdiff_t(k));
                return false;
            }
        if (!pad_ || SDL_GetGamepadID(pad_) != id) return false;
        SDL_CloseGamepad(pad_), pad_ = nullptr;
        if (!more_.empty()) {
            pad_ = more_.front();
            more_.erase(more_.begin());
        } else {
            int count = 0;
            if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
                for (int k = 0; k < count && !pad_; ++k)
                    if (ids[k] != id && allowed(ids[k])) pad_ = SDL_OpenGamepad(ids[k]);
                SDL_free(ids);
            }
        }
        return true;
    }
    // a button pressed, or a stick or trigger past half way, on another open pad: that one the active one. True when
    // the active pad changed (before the event's own handling)
    bool activity(const SDL_Event& e) {
        SDL_JoystickID id = 0;
        if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) id = e.gbutton.which;
        else if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && std::abs(int(e.gaxis.value)) > 16000) id = e.gaxis.which;
        if (!id || !allowed(id) || (pad_ && SDL_GetGamepadID(pad_) == id)) return false;
        for (size_t k = 0; k < more_.size(); ++k)
            if (SDL_GetGamepadID(more_[k]) == id) {
                SDL_Gamepad* was = pad_;
                pad_ = more_[k];
                if (was) more_[k] = was;
                else more_.erase(more_.begin() + std::ptrdiff_t(k));
                return true;
            }
        return false;
    }
    size_t open() const { return (pad_ ? 1 : 0) + more_.size(); }
    void closeAll() {
        if (pad_) SDL_CloseGamepad(pad_), pad_ = nullptr;
        for (SDL_Gamepad* g : more_) SDL_CloseGamepad(g);
        more_.clear();
    }

private:
    bool find(SDL_JoystickID id) const {
        if (pad_ && SDL_GetGamepadID(pad_) == id) return true;
        for (SDL_Gamepad* g : more_)
            if (SDL_GetGamepadID(g) == id) return true;
        return false;
    }
    SDL_Gamepad* pad_ = nullptr;
    std::vector<SDL_Gamepad*> more_;
};
} // namespace crinput
