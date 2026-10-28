#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#ifdef SDL_PLATFORM_ANDROID
#include <jni.h>
#endif

namespace mobile {
// Screen fractions, so the same gesture works with letterboxing and high-DPI windows.
struct MenuSwipe {
    bool down = false, moved = false;
    int context = 0;
    float x0 = 0, y0 = 0, lastY = 0, remainder = 0;
    void begin(float x, float y, int menu) {
        *this = MenuSwipe();
        down = true, context = menu, x0 = x, y0 = lastY = y;
    }
    int move(float x, float y) {
        if (!down) return 0;
        moved |= std::hypot(x - x0, y - y0) >= 0.012f;
        if (!moved) return 0;
        remainder += lastY - y;
        lastY = y;
        const int rows = int(remainder / 0.1f);
        remainder -= rows * 0.1f;
        return rows;
    }
};
inline int scrollCursor(int cursor, int count, int rows) {
    return count > 0 ? std::clamp(cursor + rows, 0, count - 1) : 0;
}
inline void orientation(SDL_Window* window, bool locked, bool& flipped, bool capture = false) {
    if (capture && window) {
        const auto o = SDL_GetCurrentDisplayOrientation(SDL_GetDisplayForWindow(window));
        if (o == SDL_ORIENTATION_LANDSCAPE || o == SDL_ORIENTATION_LANDSCAPE_FLIPPED)
            flipped = o == SDL_ORIENTATION_LANDSCAPE_FLIPPED;
    }
    SDL_SetHint(SDL_HINT_ORIENTATIONS, !locked ? "LandscapeLeft LandscapeRight" : flipped ? "LandscapeRight" : "LandscapeLeft");
#ifdef SDL_PLATFORM_ANDROID
    if (!window) return;
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!env) return;
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!activity) return;
    auto cls = env->GetObjectClass(activity);
    auto method = cls ? env->GetMethodID(cls, "setOrientationLock", "(ZZ)V") : nullptr;
    if (method) env->CallVoidMethod(activity, method, jboolean(locked), jboolean(flipped));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "Could not apply the orientation lock");
    }
    if (cls) env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
#endif
}
}
