#include "viewer/viewer.hpp"

#include "assets/gltf_figure.hpp"
#include "assets/hd_texture.hpp"
#include "assets/smooth.hpp"
#include "gfx/cache_trim.hpp"
#include "platform/atomic_file.hpp"

namespace cr2_viewer {

#include "gfx/dotnumtable.inc"

// a place (rotation and translation, no scale) as a quaternion x y z w
void placeQuat(const cr2::Mat34& a, float q[4]) {
    const float* m = a.m;
    const float t = m[0] + m[5] + m[10];
    if (t > 0) {
        const float s = std::sqrt(t + 1) * 2;
        q[3] = s / 4, q[0] = (m[9] - m[6]) / s, q[1] = (m[2] - m[8]) / s, q[2] = (m[4] - m[1]) / s;
    } else if (m[0] > m[5] && m[0] > m[10]) {
        const float s = std::sqrt(1 + m[0] - m[5] - m[10]) * 2;
        q[3] = (m[9] - m[6]) / s, q[0] = s / 4, q[1] = (m[1] + m[4]) / s, q[2] = (m[2] + m[8]) / s;
    } else if (m[5] > m[10]) {
        const float s = std::sqrt(1 + m[5] - m[0] - m[10]) * 2;
        q[3] = (m[2] - m[8]) / s, q[0] = (m[1] + m[4]) / s, q[1] = s / 4, q[2] = (m[6] + m[9]) / s;
    } else {
        const float s = std::sqrt(1 + m[10] - m[0] - m[5]) * 2;
        q[3] = (m[4] - m[1]) / s, q[0] = (m[2] + m[8]) / s, q[1] = (m[6] + m[9]) / s, q[2] = s / 4;
    }
}

// between two places at t: the translation straight, the rotation by slerp
cr2::Mat34 blendPlace(const cr2::Mat34& a, const cr2::Mat34& b, float t) {
    float qa[4], qb[4];
    placeQuat(a, qa), placeQuat(b, qb);
    float d = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];
    if (d < 0) d = -d, qb[0] = -qb[0], qb[1] = -qb[1], qb[2] = -qb[2], qb[3] = -qb[3];
    float wa = 1 - t, wb = t;
    if (d < 0.9995f) {
        const float th = std::acos(std::min(d, 1.0f)), s = std::sin(th);
        wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
    }
    cr2::JointTransform j;
    float l = 0;
    for (int k = 0; k < 4; ++k) j.rotation[k] = qa[k] * wa + qb[k] * wb, l += j.rotation[k] * j.rotation[k];
    l = std::sqrt(l);
    for (int k = 0; k < 4; ++k) j.rotation[k] /= l;
    for (int k = 0; k < 3; ++k) j.translation[k] = a.m[3 + 4 * k] + (b.m[3 + 4 * k] - a.m[3 + 4 * k]) * t;
    return j.matrix();
}

Mat4 perspective(float fovY, float aspect, float zn, float zf) {
    const float f = 1.0f / std::tan(fovY / 2);
    Mat4 r{};
    r[0] = f / aspect;
    r[5] = f;
    r[10] = (zf + zn) / (zn - zf);
    r[11] = -1;
    r[14] = 2 * zf * zn / (zn - zf);
    return r;
}

// a camera at `eye` looking at `at`, y up
Mat4 lookAt(const float* eye, const float* at) {
    float f[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
    const float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (float& x : f) x /= fl;
    float s[3] = {-f[2], 0, f[0]}; // f x up
    const float sl = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    for (float& x : s) x /= sl;
    const float u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
    Mat4 r{};
    r[0] = s[0], r[4] = s[1], r[8] = s[2];
    r[1] = u[0], r[5] = u[1], r[9] = u[2];
    r[2] = -f[0], r[6] = -f[1], r[10] = -f[2];
    r[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
    r[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    r[14] = f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2];
    r[15] = 1;
    return r;
}

void packVertex(uint8_t* o, const float* p, const float* n, const float* uv, const float* c, const float* t, const float* b,
                const float* uv1) {
    std::memcpy(o, p, 12);
    std::memcpy(o + 12, uv, 8);
    std::memcpy(o + 60, uv1, 8);
    for (int k = 0; k < 4; ++k) o[20 + k] = uint8_t(std::clamp(c[k], 0.0f, 1.0f) * 255.0f + 0.5f);
    std::memcpy(o + 24, n, 12);
    std::memcpy(o + 36, t, 12);
    std::memcpy(o + 48, b, 12);
}


const char* Viewer::settingsFile() { return "settings.ini"; }
void Viewer::loadSettings() {
    std::FILE* f = std::fopen(savePath(settingsFile()).c_str(), "rb");
    if (!f) return;
    char line[256];
    Settings& s = settings;
    while (std::fgets(line, sizeof line, f)) {
        char key[64];
        int v = 0;
        if (std::sscanf(line, " %63[a-z_] = %d", key, &v) != 2) continue;
        const std::string k = key;
        if (k == "display") s.display = std::clamp(v, 0, 2);
        else if (k == "resolution") s.resolution = std::clamp(v, 0, int(std::size(kResolutions)) - 1);
        else if (k == "vsync") s.vsync = v != 0;
        else if (k == "fps") s.fps = std::clamp(v, 0, 1000);
        else if (k == "renderer") s.renderer = std::clamp(v, 0, 1);
        else if (k == "upscaler") s.upscaler = std::clamp(v, 0, 4);
        else if (k == "quality") s.quality = std::clamp(v, 0, int(std::size(kQualities)) - 1);
        else if (k == "sharpness") s.sharpness = std::clamp(v, 0, 100);
        else if (k == "msaa") s.msaa = v >= 8 ? 8 : v >= 4 ? 4 : v >= 2 ? 2 : 1;
        else if (k == "anisotropy") s.anisotropy = v >= 16 ? 16 : v >= 8 ? 8 : v >= 4 ? 4 : v >= 2 ? 2 : 1;
        else if (k == "sharp_textures") s.sharpTextures = v != 0;
        else if (k == "hd_textures") s.hdTextures = v != 0, s.hdSet = true;
        else if (k == "hd_models") s.hdModels = v != 0;
        else if (k == "smooth_models") s.smoothModels = v == 2 || v == 3 ? v : 0;
        else if (k == "lock_orientation") s.lockOrientation = v != 0;
        else if (k == "landscape_flipped") s.landscapeFlipped = v != 0;
        else if (k == "pad_swap") s.padSwap = v != 0;
        else if (k == "field_speed") s.fieldSpeed = std::clamp(v, 1, 5);
        else if (k == "battle_speed") s.battleSpeed = std::clamp(v, 1, 5);
    }
    std::fclose(f);
}
void Viewer::saveSettings() const {
    if (!settingsPersist) return;
    const std::string keepData = storedData(); // (the setup's copy, kept - read before the file is rewritten)
    char* serialized = nullptr;
    const Settings& s = settings;
    if (SDL_asprintf(&serialized, "# CR2-U: the options' Display & Graphics (the command line's flags go over these)\n"
                    "display=%d\nresolution=%d\nvsync=%d\nfps=%d\nrenderer=%d\nupscaler=%d\nquality=%d\nsharpness=%d\n"
                    "msaa=%d\nanisotropy=%d\nsharp_textures=%d\nhd_textures=%d\nhd_models=%d\nsmooth_models=%d\npad_swap=%d\nfield_speed=%d\nbattle_speed=%d\n",
                 s.display, s.resolution, s.vsync ? 1 : 0, s.fps, s.renderer, s.upscaler, s.quality, s.sharpness, s.msaa,
                 s.anisotropy, s.sharpTextures ? 1 : 0, s.hdTextures ? 1 : 0, s.hdModels ? 1 : 0, s.smoothModels, s.padSwap ? 1 : 0, s.fieldSpeed,
                 s.battleSpeed) < 0 || !serialized) return;
    std::string text = serialized;
    SDL_free(serialized);
    text += "lock_orientation=" + std::to_string(int(s.lockOrientation)) + "\nlandscape_flipped=" + std::to_string(int(s.landscapeFlipped)) + "\n";
    if (!keepData.empty()) text += "data=" + keepData + "\n";
    std::string error;
    if (!crstorage::replaceFile(savePath(settingsFile()), text, &error)) SDL_Log("settings: %s", error.c_str());
}
// The settings onto the device, the window and the textures (each only as it changes)
void Viewer::applySettings() {
    if (!dev) return;
    Settings& s = settings;
    if (!vsyncApplied || appliedVsync != s.vsync) dev->setVSync(s.vsync), appliedVsync = s.vsync, vsyncApplied = true;
    const int r = std::clamp(s.resolution, 0, int(std::size(kResolutions)) - 1);
    if (s.resolution >= 0 && (resW != kResolutions[r][0] || resH != kResolutions[r][1])) {
        resW = kResolutions[r][0], resH = kResolutions[r][1];
        dev->resetHistory();
    }
    cr2::UpscaleSettings u;
    u.mode = cr2::Upscaler(std::clamp(s.upscaler, 0, 4));
    u.ratio = kQualities[std::clamp(s.quality, 0, int(std::size(kQualities)) - 1)].ratio;
    u.sharpness = float(s.sharpness) / 100.0f;
    if (u.mode != cr2::Upscaler::Off && !dev->upscalerProblem(u.mode).empty()) u.mode = cr2::Upscaler::Off;
    if (u.mode != upscale.mode || u.ratio != upscale.ratio || u.sharpness != upscale.sharpness) upscale = u, dev->setUpscaler(u);
    const int samples = upscale.mode == cr2::Upscaler::Off ? s.msaa : 1;
    if (samples != appliedMsaa) dev->setSamples(samples), appliedMsaa = samples;
    cr2::TextureFiltering tf;
    tf.anisotropy = s.anisotropy;
    tf.sharp = s.sharpTextures;
    // (an upscaler's scene is smaller: the mips its output would take - DLSS's and FSR's advice)
    tf.lodBias = upscale.mode != cr2::Upscaler::Off ? -std::log2(upscale.ratio) : 0.0f;
    dev->setFiltering(tf);
    if (window && s.display != appliedDisplay) {
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
        SDL_SetWindowFullscreen(window, true); // (a phone's or tablet's: the whole screen, the system's bars hidden - SDL's immersive mode)
#else
        if (s.display == 1) SDL_SetWindowFullscreenMode(window, SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(window)));
        else SDL_SetWindowFullscreenMode(window, nullptr);
        SDL_SetWindowFullscreen(window, s.display != 0);
#endif
        appliedDisplay = s.display;
    }
    if (s.hdTextures != hdTextures) hdTextures = s.hdTextures, reloadTextures();
    if (!orientationApplied || appliedLock != s.lockOrientation || appliedFlip != s.landscapeFlipped) {
        mobile::orientation(window, s.lockOrientation, s.landscapeFlipped);
        appliedLock = s.lockOrientation, appliedFlip = s.landscapeFlipped, orientationApplied = true;
    }
    hdModels = s.hdModels; // (from the next figure loaded)
}
bool Viewer::loadHd(const cr2::Image& img, cr2::Image& hd) {
    std::string why;
    if (cr2::loadHdImage(hdDir, img, hd, &why)) return true;
    if (!why.empty() && hdRefused.insert(why).second) SDL_Log("hd textures: %s - left out", why.c_str());
    return false;
}
void Viewer::reloadTexture(const std::string& name, cr2::Texture& t) {
    cr2::Image img;
    if (!decode(name, img)) return;
    if (hdTextures && !hdDir.empty()) {
        cr2::Image hd;
        if (loadHd(img, hd)) hd.logicalW = img.width, hd.logicalH = img.height, img = std::move(hd), ++hdLoaded;
        else ++hdMissing;
    }
    img.mipmaps = textureMips.count(name) ? textureMips[name] : true;
    cr2::Texture old = t;
    t = dev->upload(img);
    dev->destroy(old);
}
// Every cached texture again (HD Textures turned on or off): in place, so what holds one sees the new
void Viewer::reloadTextures() {
    hdLoaded = hdMissing = 0;
    for (auto& [name, t] : textures)
        if (t.id) reloadTexture(name, t);
    SDL_Log("hd textures %s: %d replaced, %d without one", hdTextures ? "on" : "off", hdLoaded, hdMissing);
}

// The list's rows: its label and value, its help (the list's window), what Left / Right / a
// tap does (+1 / -1)
// a folder as the settings' help shows it: its last two names (".../models/hd/")
std::string Viewer::shortDir(const std::string& dir) {
    size_t at = dir.size() > 1 ? dir.find_last_of("/\\", dir.size() - 2) : std::string::npos;
    if (at != std::string::npos && at > 0) at = dir.find_last_of("/\\", at - 1);
    return at == std::string::npos ? dir : "..." + dir.substr(at);
}
auto Viewer::settingRows() -> std::vector<SettingRow> {
    Settings& s = settings;
    std::vector<SettingRow> rows;
    auto step = [](int& v, const std::vector<int>& values, int dir) {
        size_t i = 0;
        while (i < values.size() && values[i] != v) ++i;
        const int n = int(values.size());
        v = values[size_t(((i < values.size() ? int(i) : 0) + dir + n) % n)];
    };
    auto onOff = [](bool b) { return std::string(b ? "On" : "Off"); };
    static const char* const kDisplay[] = {"Window", "Fullscreen", "Borderless"};
    rows.push_back({"Display", kDisplay[std::clamp(s.display, 0, 2)], "A window, fullscreen at the display's own mode, or borderless over the desktop",
                    true, [&s](int d) { s.display = (s.display + d + 3) % 3; }});
    {
        const int r = std::clamp(s.resolution, 0, int(std::size(kResolutions)) - 1);
        const std::string v = s.resolution < 0 ? std::to_string(resW) + " x " + std::to_string(resH) // (--resolution's own)
                              : r == 0          ? "Window"
                                                : std::to_string(kResolutions[r][0]) + " x " + std::to_string(kResolutions[r][1]);
        rows.push_back({"Resolution", v, "The frames' size (Window: the window's own), scaled to the window",
                        true, [&s](int d) { const int n = int(std::size(kResolutions)); s.resolution = s.resolution < 0 ? 0 : (s.resolution + d + n) % n; }});
    }
    rows.push_back({"VSync", onOff(s.vsync), "Each frame waits for the display's refresh: no tearing", true, [&s](int) { s.vsync = !s.vsync; }});
    rows.push_back({"Frame Rate", s.fps ? std::to_string(s.fps) : std::string("Unlimited"),
                    "Frames drawn a second at most; the game itself runs at 20 a second, shown moving between them",
                    true, [&s, step](int d) { step(s.fps, {30, 60, 120, 144, 165, 240, 0}, d); }});
    {
        static const char* const kRenderer[] = {"Vulkan", "OpenGL"};
        std::string v = kRenderer[std::clamp(s.renderer, 0, 1)];
        if (s.renderer != rendererInUse) v += " (next start)";
        rows.push_back({"Renderer", v, "Vulkan: the upscalers, MSAA and the faster path; OpenGL where Vulkan won't start",
                        true, [&s](int) { s.renderer = 1 - s.renderer; }});
    }
    const bool vk = dev && dev->offscreen(); // (the Vulkan device renders offscreen: the upscalers', MSAA's)
    static const char* const kUpscaler[] = {"Off", "FSR 3.1", "DLSS", "MetalFX", "FSR 4"};
    {
        std::string v = kUpscaler[std::clamp(s.upscaler, 0, 4)];
        std::string why = s.upscaler && dev ? dev->upscalerProblem(cr2::Upscaler(s.upscaler)) : std::string();
        if (!why.empty()) v += " (n/a)";
        rows.push_back({"Upscaler", v, why.empty() ? "The scene renders smaller and is rebuilt at full size; the menus stay sharp" : kUpscaler[s.upscaler] + std::string(": ") + why,
                        vk, [this, &s](int d) {
                            // (the ones this device can run, Off always)
                            for (int k = 1; k <= 5; ++k) {
                                const int u = ((s.upscaler + d * k) % 5 + 5) % 5;
                                if (u == 0 || (dev && dev->upscalerProblem(cr2::Upscaler(u)).empty())) {
                                    s.upscaler = u;
                                    return;
                                }
                            }
                        }});
    }
    {
        static const char* const kModes[] = {"Native AA", "Quality", "Balanced", "Performance", "Ultra Performance"};
        const int q = std::clamp(s.quality, 0, int(std::size(kQualities)) - 1);
        char h[96];
        std::snprintf(h, sizeof h, "The scene at %d%% of the frame per side%s", int(100.0f / kQualities[q].ratio + 0.5f),
                      q == 0 ? " (DLSS: DLAA)" : "");
        rows.push_back({"Upscale Mode", q == 0 && s.upscaler == 2 ? "DLAA" : kModes[q], h, vk && s.upscaler != 0,
                        [&s](int d) { const int n = int(std::size(kQualities)); s.quality = (s.quality + d + n) % n; }});
    }
    rows.push_back({"Sharpness", std::to_string(s.sharpness) + "%", "FSR's sharpening pass (RCAS); DLSS and MetalFX have none",
                    vk && (s.upscaler == 1 || s.upscaler == 4), [&s](int d) { s.sharpness = std::clamp(s.sharpness + d * 5, 0, 100); }});
    {
        const int most = dev ? dev->maxSamples() : 1;
        rows.push_back({"Anti-aliasing", s.msaa > 1 ? "MSAA " + std::to_string(s.msaa) + "x" : std::string("Off"),
                        s.upscaler ? "Off while an upscaler is on (its own anti-aliasing)" : "Multisampling: smoother edges",
                        vk && most > 1 && s.upscaler == 0, [&s, step, most](int d) {
                            std::vector<int> v{1};
                            for (int k = 2; k <= std::min(most, 8); k *= 2) v.push_back(k);
                            step(s.msaa, v, d);
                        }});
    }
    {
        const int most = dev ? dev->maxAnisotropy() : 1;
        rows.push_back({"Anisotropic Filtering", s.anisotropy > 1 ? std::to_string(std::min(s.anisotropy, std::max(most, 1))) + "x" : std::string("Off"),
                        most > 1 ? "Textures stay sharp on floors and walls seen at an angle" : "Not on this GPU",
                        most > 1, [&s, step](int d) { step(s.anisotropy, {1, 2, 4, 8, 16}, d); }});
    }
    rows.push_back({"Texture Filtering", s.sharpTextures ? "Sharp" : "Smooth",
                    "Sharp: the world's textures magnified by the nearest texel (square pixels up close); Smooth: blended",
                    true, [&s](int) { s.sharpTextures = !s.sharpTextures; }});
    {
        SDL_PathInfo info;
        const bool have = !hdDir.empty() && SDL_GetPathInfo(hdDir.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
        rows.push_back({"HD Textures", onOff(s.hdTextures),
                        have ? "Upscaled textures (waifu2x / Real-ESRGAN: tools/hd_textures.py) from " + shortDir(hdDir)
                             : "None in " + shortDir(hdDir) + " yet: --export-textures, then tools/hd_textures.py",
                        true, [&s](int) { s.hdTextures = !s.hdTextures; }});
    }
    {
        SDL_PathInfo info;
        const bool have = !modelsDir.empty() && SDL_GetPathInfo(modelsDir.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
        rows.push_back({"HD Models", onOff(s.hdModels),
                        have ? "Replacement models (glTF) from " + shortDir(modelsDir) + " - from the next scene on"
                             : "None in " + shortDir(modelsDir) + " yet: --export-models, edit, save as models/hd/<figure>.glb",
                        true, [&s](int) { s.hdModels = !s.hdModels; }});
    }
    {
        static const char* const kSmooth[] = {"Off", "", "Smooth", "Smoother"};
        rows.push_back({"Smooth Models", kSmooth[std::clamp(s.smoothModels, 0, 3)],
                        s.smoothModels ? "Rounder models (4x / 9x triangles), hard edges kept - from the next scene" : "The models as the game built them",
                        true, [&s](int d) { // (Off, Smooth, Smoother)
                            static const int kOrder[3] = {0, 2, 3};
                            int i = s.smoothModels == 2 ? 1 : s.smoothModels == 3 ? 2 : 0;
                            s.smoothModels = kOrder[(i + (d < 0 ? 2 : 1)) % 3];
                        }});
    }
    rows.push_back({"Lock Orientation", onOff(s.lockOrientation),
                    "Hold the current landscape direction and restore it on restart", true, [this, &s](int) {
                        s.lockOrientation = !s.lockOrientation;
                        mobile::orientation(window, s.lockOrientation, s.landscapeFlipped, s.lockOrientation);
                        orientationApplied = false;
                    }});
    auto speed = [](int x) { return x > 1 ? std::to_string(x) + "x" : std::string("Off"); };
    rows.push_back({"Field Speed", speed(s.fieldSpeed), "Walking the field 2 .. 5 times as fast - its scenes, talk and menus as they are", true,
                    [&s](int d) { s.fieldSpeed = (s.fieldSpeed - 1 + d + 5) % 5 + 1; }});
    rows.push_back({"Battle Speed", speed(s.battleSpeed), "The battles 2 .. 5 times as fast", true,
                    [&s](int d) { s.battleSpeed = (s.battleSpeed - 1 + d + 5) % 5 + 1; }});
    rows.push_back({"Swap \u25cb and \u00d7", onOff(s.padSwap),
                    s.padSwap ? "A controller's \u00d7 (the bottom button) confirms, \u25cb (the right one) goes back"
                              : "A controller's \u25cb (the right button) confirms, \u00d7 (the bottom one) goes back - as on the Vita",
                    true, [&s](int) { s.padSwap = !s.padSwap; }});
    if (titleOptions()) // (the saves: only before a game is loaded - its confirmation over the options)
        rows.push_back({"Import Vita Saves", "", "Import your decrypted Vita saves (docs/VITA_SAVES.md) - or press I in the options", true,
                        [this](int) { settingsOpen = false, optionTap(3111); }});
    rows.push_back({"Defaults", "", "Every setting here back to its default", true, [&s](int) { s = Settings(); }});
    return rows;
}
void Viewer::settingChange(int row, int dir) {
    std::vector<SettingRow> rows = settingRows();
    if (row < 0 || row >= int(rows.size()) || !rows[size_t(row)].enabled || !rows[size_t(row)].change) return;
    rows[size_t(row)].change(dir);
    applySettings();
    saveSettings();
    const std::vector<SettingRow> now = settingRows();
    SDL_Log("settings: %s %s", now[size_t(row)].label.c_str(), now[size_t(row)].value.c_str());
    audio.playSe(0xb4, false);
}
// (over the options, which stay open beneath: what waits for them closing waits on)
void Viewer::openSettings() { settingsOpen = true, settingsStart = uiClock, settingsCursor = 0; }
void Viewer::closeSettings() { settingsOpen = false, optionsStart = uiClock; }
void Viewer::drawSettings(const Mat4& view, int w, int h) {
    tapRects.clear();
    const std::vector<SettingRow> rows = settingRows();
    std::vector<ListRow> list;
    for (size_t k = 0; k < rows.size(); ++k) {
        ListRow r;
        r.id = 6000 + int(k);
        r.name = rows[k].value.empty() ? rows[k].label : rows[k].label + ":  " + rows[k].value;
        r.number = -1;
        r.selectable = rows[k].enabled;
        r.help = rows[k].help;
        list.push_back(r);
    }
    listRows = list;
    drawSkillList(list, 6000 + std::clamp(settingsCursor, 0, int(rows.size()) - 1), uiClock - settingsStart, view, w, h, -1, -1);
}
bool Viewer::settingsKey(SDL_Keycode k) {
    if (!settingsOpen) return false;
    const int n = int(settingRows().size());
    if (k == SDLK_UP) settingsCursor = (settingsCursor + n - 1) % n;
    else if (k == SDLK_DOWN) settingsCursor = (settingsCursor + 1) % n;
    else if (k == SDLK_LEFT) settingChange(settingsCursor, -1);
    else if (k == SDLK_RIGHT || k == SDLK_RETURN || k == SDLK_KP_ENTER) settingChange(settingsCursor, 1);
    else if (k == SDLK_BACKSPACE) closeSettings();
    return true;
}
bool Viewer::settingsTapped(int id) { // (a tap's TapRect id)
    if (!settingsOpen) return false;
    if (id == 100) closeSettings();
    else if (id >= 6000) settingChange(id - 6000, 1);
    else if (id >= 1000) settingsCursor = id - 1000;
    return true;
}

bool Viewer::decode(const std::string& name, cr2::Image& img) {
    if (auto it = modelImages.find(name); it != modelImages.end()) return img = it->second, true; // (an HD model's own)
    std::vector<uint8_t> data;
    std::string err;
    if (archive.read("images\\" + name + ".img", data, &err) && cr2::decodeImg(data, img, &err)) return true;
    SDL_Log("texture %s: %s", name.c_str(), err.c_str());
    return false;
}

// A material's texture (`mipmaps` off for lookup tables: CLUTs)
cr2::Texture* Viewer::texture(const std::string& name, bool mipmaps) {
    if (name.empty()) return nullptr;
    if (auto it = textures.find(name); it != textures.end()) return it->second.id ? &it->second : nullptr;
    cr2::Image img;
    cr2::Texture& t = textures[name];
    if (!decode(name, img)) return nullptr;
    if (hdTextures && !hdDir.empty()) { // (drawn at the original's size, sharper)
        cr2::Image hd;
        if (loadHd(img, hd)) {
            hd.logicalW = img.width, hd.logicalH = img.height;
            img = std::move(hd);
            ++hdLoaded;
        } else {
            ++hdMissing;
        }
    }
    img.mipmaps = mipmaps;
    textureMips[name] = mipmaps;
    t = dev->upload(img);
    return t.id ? &t : nullptr;
}

// The scene's lights from a light figure (a map's <name>c): false when it has none
bool Viewer::loadLights(const std::string& figure) {
    std::vector<uint8_t> data;
    cr2::Geom g;
    cr2::Skeleton sk;
    if (!archive.read(figure + ".geom", data) || !cr2::parseGeom(data, g) || !archive.read(figure + ".skel", data) ||
        !cr2::parseSkeleton(data, sk))
        return false;
    cr2::SceneLighting l;
    if (!cr2::lightsFromFigure(g, sk, l)) return false;
    lighting = l;
    return true;
}

void Viewer::unload(ViewFigure& f) {
    for (ViewMesh& m : f.meshes) dev->destroy(m.gpu);
    f.meshes.clear();
}

// An HD model's pictures as its figure loads: figures being drawn hold these textures by their
// address, so the entry stays - as it is for the same picture, its texture replaced for a new one
void Viewer::takeModelPictures(std::vector<std::pair<std::string, cr2::Image>>& pictures) {
    for (auto& [k, img] : pictures) {
        const auto known = modelImages.find(k);
        if (known != modelImages.end() && known->second.width == img.width && known->second.height == img.height && known->second.rgba == img.rgba)
            continue;
        modelImages[k] = std::move(img);
        const auto t = textures.find(k);
        if (t == textures.end()) continue;
        if (t->second.id) reloadTexture(k, t->second);
        else textures.erase(t); // (none could be made before: tried again when asked for)
    }
}

// HD Models: models/hd/<figure>.glb (or .gltf) in place of the figure's meshes - its skeleton,
// animations and materials kept (assets/gltf_figure.hpp)
void Viewer::replaceFigure(const std::string& name, cr2::Geom& g) {
    if (!hdModels || modelsDir.empty() || name.size() < 6) return;
    const std::string base = name.substr(0, name.size() - 5);
    for (const char* ext : {".glb", ".gltf"}) {
        const std::string path = modelsDir + base + ext;
        if (!SDL_GetPathInfo(path.c_str(), nullptr)) continue;
        std::vector<uint8_t> d;
        cr2::Skeleton sk;
        if (archive.read(base + ".skel", d)) cr2::parseSkeleton(d, sk);
        std::vector<std::pair<std::string, cr2::Image>> pictures;
        std::string why;
        cr2::GltfStats st;
        if (cr2::applyFigureReplacement(g, sk, path, "gltf\\" + base + "\\", pictures, &why, &st)) {
            takeModelPictures(pictures);
            ++hdModelsLoaded;
            SDL_Log("%s: replaced by %s (%d meshes, %d triangles%s)", name.c_str(), path.c_str(), st.meshes, st.triangles,
                    pictures.empty() ? "" : ", pictures of its own");
        } else {
            SDL_Log("%s: %s left out: %s", name.c_str(), path.c_str(), why.c_str());
        }
        return;
    }
}

bool Viewer::loadFigure(const std::string& name, ViewFigure& f, std::string* err) {
    unload(f);
    const size_t keepAnim = f.animIndex;
    const cr2::Mat34 keepPlace = f.place;
    f = ViewFigure();
    f.name = name;
    f.animIndex = keepAnim;
    f.place = keepPlace;
    std::vector<uint8_t> data;
    cr2::Geom g;
    if (!archive.read(name, data, err) || !cr2::parseGeom(data, g, err)) return false;
    replaceFigure(name, g);
    static const float kWhite[4] = {1, 1, 1, 1}, kZero[3] = {0, 0, 0};
    cr2::SmoothStats smoothed;
    for (const cr2::GeomMesh& raw : g.meshes) {
        ViewMesh vm;
        vm.source = raw;
        if (raw.material < g.materials.size()) { // (the joints as rows: the iPad's slots, a joint in the position's w)
            const std::array<uint32_t, 4>& key = g.materials[raw.material].shaderKey;
            const bool builtIn = !key[0] && !key[1] && !key[2] && !key[3]; // (no shader of its own: its file's form says)
            vm.source.resolveJoints(builtIn ? (g.version == 0x3c ? 3.0f : 1.0f) : shaders->jointScale(key));
            if (!builtIn) vm.placing = shaders->features(key).vertex;
        }
        // Smooth Models: every figure in the world - not the menus' (uiFig) nor a billboard
        if (settings.smoothModels >= 2 && !loadingUi && vm.placing == 0) cr2::smoothMesh(vm.source, settings.smoothModels, 60.0f, &smoothed);
        const cr2::GeomMesh& m = vm.source;
        vm.vertices.resize(size_t(m.vertexCount) * cr2::kVertexStride);
        for (uint32_t i = 0; i < m.vertexCount; ++i) {
            static const float kNoUv[2] = {0, 0};
            auto at3 = [&](const std::vector<float>& v) { return v.empty() ? kZero : &v[size_t(i) * 3]; };
            packVertex(&vm.vertices[size_t(i) * cr2::kVertexStride], &m.position[size_t(i) * 3], at3(m.normal),
                       m.uv.empty() ? kNoUv : &m.uv[size_t(i) * 2], m.colour.empty() ? kWhite : &m.colour[size_t(i) * 4],
                       at3(m.tangent), at3(m.binormal), m.uv1.empty() ? kNoUv : &m.uv1[size_t(i) * 2]);
        }
        const std::vector<uint32_t> idx = m.triangles();
        vm.gpu = dev->upload(vm.vertices, idx, -1);
        vm.lit = !m.normal.empty();
        if (m.material < g.materials.size()) {
            vm.material = vm.baseMaterial = g.materials[m.material];
            if (SDL_getenv("CR2_MATERIALS")) { // (debugging: the materials' keys and states)
                const cr2::GeomMaterial& gm = g.materials[m.material];
                std::string st;
                for (const auto& [id, v] : gm.states) st += " " + std::to_string(id) + "=" + std::to_string(v[0]) + "/" + std::to_string(v[1]);
                SDL_Log("%s mesh %zu: material %u key %08x %08x %08x %08x states%s textures %s / %s / %s", name.c_str(), f.meshes.size(),
                        m.material, gm.shaderKey[0], gm.shaderKey[1], gm.shaderKey[2], gm.shaderKey[3], st.c_str(),
                        vm.material.diffuse().c_str(), vm.material.texture(0x43).c_str(), vm.material.texture(0x48).c_str());
            }
            vm.texture = texture(vm.material.diffuse());
            vm.normalMap = texture(vm.material.texture(0x35));
            vm.extraMap = vm.material.textures.count(0x48)   ? texture(vm.material.texture(0x48), false)
                          : vm.material.textures.count(0x44) ? texture(vm.material.texture(0x44)) // (OverlayColorSampler)
                                                             : texture(vm.material.texture(0x43));
            // an overlay without its texture (d0252b's): the sampler never set, so unit 0's -
            // the colour's own, laid over itself
            if (!vm.material.textures.count(0x44) && (shaders->features(vm.material.shaderKey).shade & SHADE_OVERLAY))
                vm.extraMap = vm.texture;
        }
        f.vertices += m.vertexCount;
        f.triangles += idx.size() / 3;
        f.meshes.push_back(std::move(vm));
    }
    if (smoothed.meshes && SDL_getenv("CR2_SMOOTH_LOG"))
        SDL_Log("%s smoothed: %d meshes, %d -> %d triangles", name.c_str(), smoothed.meshes, smoothed.trianglesBefore, smoothed.trianglesAfter);
    float r2 = 0;
    for (int c = 0; c < 3; ++c) {
        f.centre[c] = (g.boundsMin[c] + g.boundsMax[c]) / 2;
        const float h = (g.boundsMax[c] - g.boundsMin[c]) / 2;
        r2 += h * h;
    }
    f.radius = std::max(std::sqrt(r2), 1e-3f);
    // the skeleton and the figure's animations (<figure>*.anim: c217.anim, c217e080.anim, ...)
    const std::string base = name.substr(0, name.size() - 5);
    f.skinned = archive.read(base + ".skel", data) && cr2::parseSkeleton(data, f.skeleton) &&
                g.inverseBind.size() == f.skeleton.basePose.size();
    f.inverseBind = g.inverseBind;
    if (f.skinned)
        for (const auto& e : archive.entries())
            if (e.name.size() > 5 && e.name.compare(0, base.size(), base) == 0 && e.name.compare(e.name.size() - 5, 5, ".anim") == 0)
                f.anims.push_back(e.name);
    std::sort(f.anims.begin(), f.anims.end());
    if (!f.anims.empty()) loadAnim(f, std::min(f.animIndex, f.anims.size() - 1));
    return true;
}

// the model viewer's figure: the archive's model `index`
bool Viewer::load(size_t index) {
    current = index;
    dev->resetHistory();
    const std::string& name = models[index];
    std::string err;
    if (!loadFigure(name, fig, &err)) {
        status = name + ": " + err;
        SDL_Log("%s", status.c_str());
        return false;
    }
    char buf[256];
    std::snprintf(buf, sizeof buf, "%s (%zu/%zu): %zu meshes, %zu vertices, %zu triangles", name.c_str(), index + 1,
                  models.size(), fig.meshes.size(), fig.vertices, fig.triangles);
    status = buf;
    SDL_Log("%s", status.c_str());
    return true;
}

void Viewer::loadAnim(ViewFigure& f, size_t index) {
    f.animIndex = index;
    std::vector<uint8_t> data;
    std::string err;
    f.animLoaded = archive.read(f.anims[index], data, &err) && f.anim.parse(data, &err) &&
                   f.anim.joints() == int(f.skeleton.basePose.size());
    if (!f.animLoaded) SDL_Log("%s: %s", f.anims[index].c_str(), err.empty() ? "not this skeleton's" : err.c_str());
    time = 0;
}
void Viewer::drawShadow(const cr2::Mat34& place, const float* mvp) {
    if (!shadowTried) {
        shadowTried = true;
        std::string err;
        if (!loadFigure("obj001.geom", shadowFig, &err)) SDL_Log("shadow: %s", err.c_str());
        shadowFig.absolute = true;
        const auto it = std::find(shadowFig.anims.begin(), shadowFig.anims.end(), "obj001.anim");
        if (it != shadowFig.anims.end()) {
            const float keep = time;
            loadAnim(shadowFig, size_t(it - shadowFig.anims.begin()));
            time = keep;
        }
    }
    if (shadowFig.meshes.empty()) return;
    shadowFig.place = place;
    const float keep = time;
    pose(shadowFig, shadowFig.animLoaded && shadowFig.anim.duration() > 0 ? std::fmod(time, shadowFig.anim.duration()) : 0.0f, false); // (its animation: a single frame)
    time = keep;
    drawFigure(shadowFig, mvp);
}
void Viewer::playMotion(ViewFigure& f, const char* motion) {
    const std::string want = f.name.substr(0, f.name.size() - 5) + motion + ".anim";
    const auto it = std::find(f.anims.begin(), f.anims.end(), want);
    if (it != f.anims.end() && (!f.animLoaded || f.anims[f.animIndex] != want)) loadAnim(f, size_t(it - f.anims.begin()));
}

void Viewer::leaderPlay(const char* motion, float blend) {
    const std::string want = fig.name.substr(0, fig.name.size() - 5) + motion + ".anim";
    if (blend > 0 && fig.animLoaded && fig.anims[fig.animIndex] != want) {
        fig.blendAnim = fig.anim, fig.blendLoaded = true, fig.blendAbsolute = fig.absolute, fig.blendLoop = true;
        fig.blendTime = time, fig.blendWeight = 0, leaderBlend = blend, leaderBlendClock = 0;
    } else if (blend <= 0) {
        fig.blendWeight = 1, fig.blendLoaded = false, leaderBlend = 0;
    }
    playMotion(fig, motion);
    time = 0; // (the animator rewound: +0x3c from +0x44)
}
void Viewer::stepLeaderBlend(float dt) {
    if (leaderBlend <= 0) return;
    leaderBlendClock += dt, fig.blendTime += dt;
    fig.blendWeight = std::clamp((leaderBlendClock - cr2::EventRuntime::kTick) / leaderBlend, 0.0f, 1.0f);
    if (fig.blendWeight >= 1) leaderBlend = 0, fig.blendLoaded = false;
}

std::string Viewer::animStatus() const {
    if (!fig.skinned) return "";
    if (!fig.animLoaded) return " - base pose";
    char buf[160];
    std::snprintf(buf, sizeof buf, " - %s (%zu/%zu) %.2f / %.2f s", fig.anims[fig.animIndex].c_str(), fig.animIndex + 1,
                  fig.anims.size(), time, fig.anim.duration());
    return buf;
}

// A figure's pose at time t, the skinning matrices (place * world * inverse bind), the meshes'
// vertices through their palettes (as the game's vertex shaders: MatrixPalette[index / 3 slot],
// the weights as stored)
void Viewer::pose(ViewFigure& f, float t, bool loop) {
    if (!f.skinned) return;
    std::vector<cr2::JointTransform> p = f.skeleton.basePose;
    if (f.animLoaded) {
        const float frames = float(std::max(f.anim.frames() - 1, 1));
        float fr = t * f.anim.sampleRate();
        if (fr > frames) fr = loop ? std::fmod(fr, frames) : frames;
        // (relative to the base pose); the user channels: material parameters (UI fades, colours)
        std::vector<float> user(f.skeleton.userChannels.size(), std::nanf(""));
        p = f.absolute ? f.anim.poseAbsolute(f.skeleton.basePose, fr, f.anim.hasUserChannels() ? &user : nullptr)
                       : f.anim.poseOver(f.skeleton.basePose, fr, f.anim.hasUserChannels() ? &user : nullptr);
        if (f.blendWeight < 1 && f.blendLoaded) { // (fading in from the motion before)
            const float bframes = float(std::max(f.blendAnim.frames() - 1, 1));
            float bfr = f.blendTime * f.blendAnim.sampleRate();
            if (bfr > bframes) bfr = f.blendLoop ? std::fmod(bfr, bframes) : bframes;
            const std::vector<cr2::JointTransform> from = f.blendAbsolute ? f.blendAnim.poseAbsolute(f.skeleton.basePose, bfr)
                                                                          : f.blendAnim.poseOver(f.skeleton.basePose, bfr);
            const float w = std::clamp(f.blendWeight, 0.0f, 1.0f);
            for (size_t j = 0; j < p.size() && j < from.size(); ++j) {
                cr2::JointTransform& to = p[j];
                const cr2::JointTransform& a = from[j];
                float dot = 0;
                for (int c = 0; c < 4; ++c) dot += a.rotation[c] * to.rotation[c];
                const float sgn = dot < 0 ? -1.0f : 1.0f;
                float len = 0;
                for (int c = 0; c < 4; ++c) to.rotation[c] = a.rotation[c] * (1 - w) + to.rotation[c] * sgn * w, len += to.rotation[c] * to.rotation[c];
                len = len > 0 ? 1.0f / std::sqrt(len) : 1.0f;
                for (int c = 0; c < 4; ++c) to.rotation[c] *= len;
                for (int c = 0; c < 3; ++c) {
                    to.translation[c] = a.translation[c] + (to.translation[c] - a.translation[c]) * w;
                    to.scale[c] = a.scale[c] + (to.scale[c] - a.scale[c]) * w;
                }
            }
        }
        for (ViewMesh& vm : f.meshes) {
            vm.material = vm.baseMaterial;
            for (size_t k = 0; k < user.size() && k < f.skeleton.userChannels.size(); ++k) {
                const cr2::Skeleton::UserChannel& c = f.skeleton.userChannels[k];
                if (std::isnan(user[k]) || c.material != vm.material.hash) continue;
                std::vector<float>& v = vm.material.params[c.param];
                const bool fresh = v.size() <= c.component;
                if (fresh) v.resize(size_t(c.component) + 1, c.param == 0x33 ? 1.0f : 0.0f); // (DiffuseColor's default white)
                // the figure's own animation sets the value; an animator beside it adds to the
                // material's (brkInfo's colour -0.5 .. +0.43 .. 0 about its 0.99: a flash, then as
                // it is; bonus_crtcl's fade -1 .. 0 about 1)
                v[c.component] = f.absolute ? user[k] : v[c.component] + user[k];
            }
        }
    }
    const std::vector<cr2::Mat34> world = f.skeleton.world(p, f.place);
    f.posed = p;
    std::vector<cr2::Mat34> skin(world.size());
    for (size_t j = 0; j < world.size(); ++j) skin[j] = world[j] * f.inverseBind[j];
    for (ViewMesh& vm : f.meshes) {
        const cr2::GeomMesh& m = vm.source;
        std::vector<const cr2::Mat34*> pal;
        for (uint32_t j : m.palette) pal.push_back(j < skin.size() ? &skin[j] : nullptr);
        if (vm.placing & cr2::kBillboard) {
            billboard(vm, pal.empty() || !pal[0] ? f.place : *pal[0]);
            continue;
        }
        for (uint32_t i = 0; i < m.vertexCount; ++i) {
            const float* sp = &m.position[size_t(i) * 3];
            const float* sn = m.normal.empty() ? nullptr : &m.normal[size_t(i) * 3];
            const float* st = m.tangent.empty() ? nullptr : &m.tangent[size_t(i) * 3];
            const float* sb = m.binormal.empty() ? nullptr : &m.binormal[size_t(i) * 3];
            float op[3] = {0, 0, 0}, on[3] = {0, 0, 0}, ot[3] = {0, 0, 0}, ob[3] = {0, 0, 0};
            auto add = [&](const cr2::Mat34& M, float w) {
                auto rot = [&](const float* v, float* o, int r) {
                    o[r] += w * (M.m[r * 4] * v[0] + M.m[r * 4 + 1] * v[1] + M.m[r * 4 + 2] * v[2]);
                };
                for (int r = 0; r < 3; ++r) {
                    op[r] += w * (M.m[r * 4] * sp[0] + M.m[r * 4 + 1] * sp[1] + M.m[r * 4 + 2] * sp[2] + M.m[r * 4 + 3]);
                    if (sn) rot(sn, on, r);
                    if (st) rot(st, ot, r);
                    if (sb) rot(sb, ob, r);
                }
            };
            if (m.joints.empty()) {
                add(pal.empty() || !pal[0] ? f.place : *pal[0], 1.0f);
            } else {
                for (int k = 0; k < 4; ++k) {
                    const float w = m.weights.empty() ? (k == 0 ? 1.0f : 0.0f) : m.weights[size_t(i) * 4 + k];
                    const size_t slot = m.joints[size_t(i) * 4 + k] / 3;
                    if (w != 0.0f && slot < pal.size() && pal[slot]) add(*pal[slot], w);
                }
            }
            // (normalised, as the game's vertex shaders do)
            auto unit = [](float* v) {
                const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
                if (l > 0) v[0] /= l, v[1] /= l, v[2] /= l;
            };
            unit(on);
            unit(ot);
            unit(ob);
            uint8_t* o = &vm.vertices[size_t(i) * cr2::kVertexStride];
            std::memcpy(o, op, 12);
            if (sn) std::memcpy(o + 24, on, 12);
            if (st) std::memcpy(o + 36, ot, 12);
            if (sb) std::memcpy(o + 48, ob, 12);
        }
        dev->updateVertices(vm.gpu, vm.vertices);
    }
}

// the model viewer's camera: where it is (orbiting the figure by yaw / pitch / zoom), how far
float Viewer::modelEye(float* eye) const {
    const float yr = yaw * 3.14159265f / 180, pr = pitch * 3.14159265f / 180, d = fig.radius * 2.6f * zoom;
    eye[0] = fig.centre[0] + d * std::cos(pr) * std::sin(yr);
    eye[1] = fig.centre[1] + d * std::sin(pr);
    eye[2] = fig.centre[2] + d * std::cos(pr) * std::cos(yr);
    return d;
}

// A billboard mesh's vertices (the game's vertex shaders with ViewInverse): turned as the
// camera is - kUpright: by camPos, the view's third row flattened (y 0), then y x it and the
// two's cross - then scaled by MatrixPalette[0]'s columns' lengths and moved to its translation
// (its rotation and the vertices' joints left out)
void Viewer::billboard(ViewMesh& vm, const cr2::Mat34& p0) {
    const cr2::GeomMesh& m = vm.source;
    float r[3][3]; // (the rows: vertex v -> r v)
    for (int i = 0; i < 3; ++i)
        for (int c = 0; c < 3; ++c) r[i][c] = poseView.m[i * 4 + c];
    auto unit = [](float* v) {
        const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (l > 0) v[0] /= l, v[1] /= l, v[2] /= l;
    };
    for (int c = 0; c < 3; ++c) { // (the camera's axes as unit columns)
        float col[3] = {r[0][c], r[1][c], r[2][c]};
        unit(col);
        for (int i = 0; i < 3; ++i) r[i][c] = col[i];
    }
    if (vm.placing & cr2::kUpright) {
        float z[3] = {r[2][0], 0, r[2][2]};
        unit(z);
        float x[3] = {z[2], 0, -z[0]}; // (0 1 0) x z
        unit(x);
        const float y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
        for (int c = 0; c < 3; ++c) r[0][c] = x[c], r[1][c] = y[c], r[2][c] = z[c];
    }
    float scale[3], trans[3];
    for (int c = 0; c < 3; ++c) {
        scale[c] = std::sqrt(p0.m[c] * p0.m[c] + p0.m[4 + c] * p0.m[4 + c] + p0.m[8 + c] * p0.m[8 + c]);
        trans[c] = p0.m[c * 4 + 3];
    }
    auto turn = [&](const std::vector<float>& from, uint32_t i, uint8_t* to, bool place) {
        if (from.empty()) return;
        const float* v = &from[size_t(i) * 3];
        float o[3];
        for (int k = 0; k < 3; ++k) o[k] = r[k][0] * v[0] + r[k][1] * v[1] + r[k][2] * v[2];
        if (place)
            for (int k = 0; k < 3; ++k) o[k] = o[k] * scale[k] + trans[k];
        std::memcpy(to, o, 12);
    };
    for (uint32_t i = 0; i < m.vertexCount; ++i) {
        uint8_t* o = &vm.vertices[size_t(i) * cr2::kVertexStride];
        turn(m.position, i, o, true);
        turn(m.normal, i, o + 24, false);
        turn(m.tangent, i, o + 36, false);
        turn(m.binormal, i, o + 48, false);
    }
    dev->updateVertices(vm.gpu, vm.vertices);
}

void Viewer::pose() {
    { // (this frame's camera, for the billboards)
        cr2::Mat34 cw;
        float fov, zn, zf;
        if (battleMode) {
            if (battleView(cw, fov, zn, zf)) poseView = cw;
        } else if (eventMode) {
            if (event->sceneShown() && event->scene().camera(cw, fov, zn, zf, eventLead())) poseView = cw;
        } else if (fieldMode) {
            poseView = field.cameraPlace();
        } else { // (lookAt's inverse)
            float eye[3];
            modelEye(eye);
            const Mat4 v = cr2::mat4RigidInverse(lookAt(eye, fig.centre));
            for (int row = 0; row < 3; ++row)
                for (int c = 0; c < 4; ++c) poseView.m[row * 4 + c] = v[size_t(c * 4 + row)];
        }
    }
    if (battleMode) {
        poseBattle();
        return;
    }
    if (eventMode) {
        poseEvent();
        return;
    }
    if (fieldMode) {
        // the character stands at `player`, facing `heading` (radians about y, 0 = +z)
        const float c = std::cos(heading), s = std::sin(heading);
        fig.place = cr2::Mat34();
        fig.place.m[0] = c, fig.place.m[2] = s, fig.place.m[8] = -s, fig.place.m[10] = c;
        fig.place.m[3] = player[0], fig.place.m[7] = player[1], fig.place.m[11] = player[2];
        pose(mapFigure, time);
        for (size_t k = 0; k < gimmicks.size(); ++k) { // (a box opening: fa01 once, then fn02 - FldBox::Open)
            ViewFigure& g = gimmicks[k];
            const auto op = k < shownGimmicks.size() ? boxOpening.find(shownGimmicks[k]) : boxOpening.end();
            if (op != boxOpening.end()) {
                playMotion(g, "fa01");
                const float t = uiClock - op->second;
                if (g.animLoaded && t < g.anim.duration()) {
                    pose(g, t, false);
                    continue;
                }
                boxOpening.erase(op);
                playMotion(g, "fn02");
            }
            // the script's: its event motion (SetMotion: from when it was set, once unless
            // looping), its alpha (ChangeAlpha)
            const auto st = k < gimmickIds.size() ? script->gimmicks().find(gimmickIds[k]) : script->gimmicks().end();
            if (st != script->gimmicks().end()) {
                g.tint[3] = st->second.alpha();
                if (st->second.color != gimmickColor[k]) { // (ChangeModelColor: its variant's textures)
                    gimmickColor[k] = st->second.color;
                    const std::string id = gimmickIds[k];
                    const std::string tex = st->second.color < 0 ? std::string() : "m" + id.substr(1, 3) + "abcd"[st->second.color] + "01";
                    cr2::Texture* t = tex.empty() ? nullptr : texture(tex);
                    cr2::Texture* n = tex.empty() ? nullptr : texture(tex + "n");
                    if (!tex.empty() && !t) SDL_Log("%s: no %s", id.c_str(), tex.c_str());
                    for (ViewMesh& vm : g.meshes) {
                        if (t) vm.texture = t;
                        if (n) vm.normalMap = n;
                    }
                }
                if (st->second.faceUpdates > 0) { // (ChangeDirection: turned about y toward its target, in place)
                    if (gimmickFace[k].serial != st->second.faceSerial) {
                        float tx, tz;
                        gimmickFace[k].serial = st->second.faceSerial;
                        gimmickFace[k].from = gimmickFace[k].yaw;
                        if (gimmickTarget(st->second.faceTarget, tx, tz)) gimmickFace[k].to = std::atan2(tx - g.place.m[3], tz - g.place.m[11]);
                    }
                    const float u = float(script->ticks() - st->second.faceTick) + fieldClock / cr2::EventRuntime::kTick;
                    gimmickFace[k].yaw = turnBetween(gimmickFace[k].from, gimmickFace[k].to, directionShare(u, st->second.faceUpdates));
                    const float sc = std::hypot(g.place.m[0], g.place.m[4], g.place.m[8]), c = std::cos(gimmickFace[k].yaw), sn = std::sin(gimmickFace[k].yaw);
                    g.place.m[0] = c * sc, g.place.m[1] = 0, g.place.m[2] = sn * sc;
                    g.place.m[4] = 0, g.place.m[5] = sc, g.place.m[6] = 0;
                    g.place.m[8] = -sn * sc, g.place.m[9] = 0, g.place.m[10] = c * sc;
                }
                if (!st->second.motion.empty()) {
                    if (gimmickSerial[k] != st->second.motionSerial) {
                        gimmickSerial[k] = st->second.motionSerial;
                        const auto a = std::find(g.anims.begin(), g.anims.end(), st->second.motion + ".anim");
                        if (a != g.anims.end()) {
                            const float keep = time;
                            loadAnim(g, size_t(a - g.anims.begin()));
                            time = keep;
                            g.absolute = false; // (SetAnime / SetEventAnim: slot 1, a motion)
                        }
                    }
                    pose(g, float(script->ticks() - st->second.motionTick) * cr2::EventRuntime::kTick + fieldClock, st->second.motionLoop);
                    continue;
                }
            }
            if (k < gimmickFidget.size() && gimmickFidget[k].party) {
                Fidget& fd = gimmickFidget[k];
                if (!fd.playing && uiClock >= fd.due) {
                    if (game.flag(1930)) fd.due = INFINITY;
                    else fd.playing = true, fd.start = uiClock, playMotion(g, "fn02");
                }
                if (fd.playing) {
                    const float t = uiClock - fd.start;
                    if (g.animLoaded && t < g.anim.duration()) {
                        pose(g, t, false);
                        continue;
                    }
                    fd.playing = false, fd.due = uiClock + fidgetWait();
                    playMotion(g, "fn01");
                }
            }
            pose(g, time);
        }
        if (script) fig.tint[3] = script->playerAlpha();
        if (script && script->playerState().faceUpdates > 0) { // (the leader turned: Cr3FieldMapCharacter.ChangeDirection)
            const cr2::GimmickState& ps = script->playerState();
            if (leaderFace.serial != ps.faceSerial) {
                float tx, tz;
                leaderFace.serial = ps.faceSerial, leaderFace.from = heading, leaderFace.done = false;
                idleTurn.on = false; // (the same FldDirectionManager: the script's turn in its place)
                leaderFace.to = gimmickTarget(ps.faceTarget, tx, tz) ? std::atan2(tx - player[0], tz - player[2]) : heading;
            }
            const float u = float(script->ticks() - ps.faceTick) + fieldClock / cr2::EventRuntime::kTick;
            if (!leaderFace.done) heading = turnBetween(leaderFace.from, leaderFace.to, directionShare(u, ps.faceUpdates)), leaderFace.done = u >= float(ps.faceUpdates);
        }
        if (script && script->events().scene().loaded()) poseEventModels(script->events());
    }
    pose(fig, leaderHeldAt >= 0 && encount.coupling ? leaderHeldAt : time);
}

// A figure's meshes with their materials' render states; opaque meshes first, then those blending
void Viewer::drawFigure(const ViewFigure& f, const float* mvp) {
    if (f.tint[3] <= 0) return;
    std::vector<const ViewMesh*> order;
    for (const ViewMesh& m : f.meshes) order.push_back(&m);
    std::stable_sort(order.begin(), order.end(), [](const ViewMesh* a, const ViewMesh* b) {
        uint32_t ba = 0, bb = 0;
        a->material.state(0x7c, 0, ba);
        b->material.state(0x7c, 0, bb);
        return ba < bb;
    });
    const char* only = SDL_getenv("CR2_MESHES"); // (debugging: "1,3" draws meshes 1 and 3 only)
    for (const ViewMesh* mp : order) {
        const ViewMesh& m = *mp;
        if (m.hidden) continue;
        if (only && !std::strstr((std::string(",") + only + ",").c_str(),
                                 (',' + std::to_string(mp - f.meshes.data()) + ',').c_str()))
            continue;
        cr2::DrawState st;
        cr2::MeshShading sh;
        uint32_t v = 0, v2 = 0;
        if (m.material.state(0x79, 0, v) && v) { // alpha test: GREATER (the game's only func) than ref
            uint32_t bits = 0;
            m.material.state(0x78, 0, bits, 1);
            std::memcpy(&sh.alphaRef, &bits, 4);
        }
        m.material.state(0x7c, 0, v);
        st.blend = v != 0;
        m.material.state(0x7a, kGlSrcAlpha, v, 0);
        m.material.state(0x7a, kGlOneMinusSrcAlpha, v2, 1);
        st.blendSrc = cr2::blendCode(v, false);
        st.blendDst = cr2::blendCode(v2, true);
        m.material.state(0x7b, 0x8006, v); // blend_equation
        st.blendOp = v == 0x800a ? 1 : v == 0x800b ? 2 : v == 0x8007 ? 3 : v == 0x8008 ? 4 : 0;
        m.material.state(0x7e, 1, v);
        m.material.state(0x7d, kGlBack, v2);
        st.cull = v && !SDL_getenv("CR2_NO_CULL") ? (v2 == kGlFront ? 1 : 2) : 0;
        m.material.state(0x80, 1, v);
        st.depthWrite = v != 0;
        m.material.state(0x81, 1, v);
        st.depthTest = v != 0;
        // colour_mask (0x84), the stencil (0x85 func ref mask, 0x86 write mask, 0x87 sfail dpfail
        // dppass, 0x88 enable) and polygon_offset (0x82 factor units, 0x83 enable): eight fields'
        // masks (d0203f, d1123f ...: two shells counting into the stencil unseen, then a layer
        // where it is 1) and six effects drawn just above the ground
        uint32_t mask[4] = {1, 1, 1, 1};
        for (int c = 0; c < 4; ++c) m.material.state(0x84, 1, mask[c], c);
        st.colorWrite = mask[0] || mask[1] || mask[2] || mask[3];
        if (m.material.state(0x88, 0, v) && v) {
            st.stencilTest = true;
            uint32_t f = 0x207, ref = 0, fm = 0xff, wm = 0xff, op[3] = {0x1e00, 0x1e00, 0x1e00};
            m.material.state(0x85, 0x207, f, 0), m.material.state(0x85, 0, ref, 1), m.material.state(0x85, 0xff, fm, 2);
            m.material.state(0x86, 0xff, wm, 0);
            for (int k = 0; k < 3; ++k) m.material.state(0x87, 0x1e00, op[k], k);
            st.stencilFunc = uint16_t(f), st.stencilRef = uint8_t(ref), st.stencilMask = uint8_t(fm), st.stencilWrite = uint8_t(wm);
            for (int k = 0; k < 3; ++k) st.stencilOps[k] = uint16_t(op[k]);
        }
        if (m.material.state(0x83, 0, v) && v) {
            uint32_t fb = 0, ub = 0;
            m.material.state(0x82, 0, fb, 0), m.material.state(0x82, 0, ub, 1);
            st.polygonOffset = true;
            std::memcpy(&st.offsetFactor, &fb, 4), std::memcpy(&st.offsetUnits, &ub, 4);
        }
        // each texture with its own sampler (the material's texture entries)
        st.sampling = cr2::sampling(m.material.sampler(0x32));
        sh.texMode = m.texture ? 4 : -1; // (the built-in Figure shaders: modulate the vertex colour)
        sh.lit = m.lit;
        cr2::applyMaterial(m.material, shaders->features(m.material.shaderKey), sh); // (the game's shading: keyed materials)
        for (int c = 0; c < 4; ++c) sh.color[c] *= f.tint[c];
        if (f.tint[3] < 1 && !st.blend)
            st.blend = true, st.blendSrc = cr2::blendCode(kGlSrcAlpha, false), st.blendDst = cr2::blendCode(kGlOneMinusSrcAlpha, true);
        sh.normalMap = m.normalMap;
        sh.normalSampling = cr2::sampling(m.material.sampler(0x35));
        sh.extraMap = m.extraMap;
        sh.extraSampling = cr2::sampling(m.material.sampler(m.material.textures.count(0x48)   ? 0x48
                                                            : m.material.textures.count(0x44) ? 0x44
                                                            : (sh.shade & SHADE_OVERLAY)     ? 0x32
                                                                                              : 0x43));
        dev->drawMesh(m.gpu, m.texture, mvp, sh, st);
    }
}

// A painted layer: the 4096-unit quad with its image
void Viewer::drawLayer(const std::string& image, const Mat4& proj, float shiftX) {
    cr2::Texture* t = texture(image);
    if (!t) return;
    float v[24];
    cr2::FieldMap::layerQuad(v, shiftX);
    static const float kWhite[4] = {1, 1, 1, 1};
    dev->drawQuad(*t, v, proj.data(), kWhite);
}

// A frame of fw x fh: the game's screen all of it (an offscreen device's frames are the
// chosen resolution, letterboxed into the window when shown), or - drawing straight into the
// window (GL) - the resolution's shape letterboxed in it. The scene (smaller while an
// upscaler is on), then the UI over the resolved frame at full size.
void Viewer::draw(int fw, int fh) {
    listTrack.on = false; // (set again by the list drawn this frame, if any)
    if (battleMode && glassPending) grabGlass(fw, fh);
    dev->beginFrame(fw, fh);
    placeScreen(fw, fh);
    if ((game.bgColor[0] > 0 || game.bgColor[1] > 0 || game.bgColor[2] > 0) && white.id) { // (SetBGColor: GameMain::Draw's clear colour)
        static const float kScreen[24] = {-1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1, -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
        const Mat4 id = cr2::mat4Identity();
        const float rgba[4] = {game.bgColor[0], game.bgColor[1], game.bgColor[2], 1};
        dev->drawQuad(white, kScreen, id.data(), rgba);
        dev->clearDepth();
    }
    drawScene(frameW, frameH); // (the scene: all of the frame)
    dev->endScene();
    dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH)); // (the UI: inside the safe area)
    drawEventUi(); // (the vista's, or the field's events')
}
// a quad over the whole frame - a fade, a dimming -, not the UI's screen alone; the UI's viewport back after it
void Viewer::fullFrameQuad(const cr2::Texture& t, const float* v, const float* rgba) {
    static const Mat4 id = cr2::mat4Identity();
    dev->setViewport(float(frameX), float(frameY), float(frameW), float(frameH));
    dev->drawQuad(t, v, id.data(), rgba);
    dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
}
// The display's safe area in a frame of fw x fh: SDL lays the window out under a camera hole
// (LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS), so the scene can fill all of it while the UI stays
// clear - the insets, each axis's larger on both its sides so the UI's screen stays centred, in
// the frame's pixels (an offscreen device's frame is scaled into the window to fit: the insets
// less the bars around it, in its own pixels). CR2_SAFE_INSETS "l,t,r,b" in the frame's pixels: tests
void Viewer::safeInsets(int fw, int fh, int& ix, int& iy) const {
    ix = iy = 0;
    int l = 0, t = 0, r = 0, b = 0;
    if (const char* e = SDL_getenv("CR2_SAFE_INSETS"); e && std::sscanf(e, "%d,%d,%d,%d", &l, &t, &r, &b) == 4) {
    } else if (SDL_Rect safe; window && SDL_GetWindowSafeArea(window, &safe)) {
        int ww = 0, wh = 0, pw = 0, ph = 0;
        if (!SDL_GetWindowSize(window, &ww, &wh) || ww <= 0 || wh <= 0 || !SDL_GetWindowSizeInPixels(window, &pw, &ph) || pw <= 0 || ph <= 0) return;
        const float sx = float(pw) / float(ww), sy = float(ph) / float(wh); // (the insets in the window's pixels)
        float fl = float(safe.x) * sx, ft = float(safe.y) * sy, fr = float(ww - safe.x - safe.w) * sx, fb = float(wh - safe.y - safe.h) * sy;
        if (dev->offscreen()) { // (the frame scaled to fit the window, centred: the bars around it count against the insets)
            const float k = std::min(float(pw) / float(fw), float(ph) / float(fh));
            const float ox = (float(pw) - float(fw) * k) * 0.5f, oy = (float(ph) - float(fh) * k) * 0.5f;
            fl = (fl - ox) / k, fr = (fr - ox) / k, ft = (ft - oy) / k, fb = (fb - oy) / k;
        } else { // (the frame is the window's pixels - a chosen resolution letterboxed in it: placeScreen)
            fl *= float(fw) / float(pw), fr *= float(fw) / float(pw), ft *= float(fh) / float(ph), fb *= float(fh) / float(ph);
        }
        l = int(std::ceil(std::max(fl, 0.0f))), t = int(std::ceil(std::max(ft, 0.0f)));
        r = int(std::ceil(std::max(fr, 0.0f))), b = int(std::ceil(std::max(fb, 0.0f)));
    }
    ix = std::clamp(std::max(l, r), 0, fw / 4), iy = std::clamp(std::max(t, b), 0, fh / 4);
}
// The frame (frameX ...): all of fw x fh, or - drawing straight into the window (GL) - a chosen
// resolution's shape letterboxed in it; the scene's viewport on it. The UI's screen (screenX ...):
// the frame inside the safe area
void Viewer::placeScreen(int fw, int fh) {
    frameX = frameY = 0, frameW = fw, frameH = fh;
    if (!dev->offscreen() && resW > 0 && resH > 0) {
        if (int64_t(resW) * fh > int64_t(fw) * resH) frameH = std::max(int(int64_t(fw) * resH / resW), 1);
        else frameW = std::max(int(int64_t(fh) * resW / resH), 1);
        frameX = (fw - frameW) / 2, frameY = (fh - frameH) / 2;
    }
    int ix = 0, iy = 0;
    safeInsets(fw, fh, ix, iy);
    const int x0 = std::max(frameX, ix), y0 = std::max(frameY, iy);
    const int x1 = std::min(frameX + frameW, fw - ix), y1 = std::min(frameY + frameH, fh - iy);
    screenX = x0, screenY = y0, screenW = std::max(x1 - x0, 1), screenH = std::max(y1 - y0, 1);
    int rw = fw, rh = fh;
    dev->sceneSize(fw, fh, rw, rh);
    const float kx = float(rw) / float(fw), ky = float(rh) / float(fh);
    dev->setViewport(frameX * kx, frameY * ky, frameW * kx, frameH * ky);
}

void Viewer::drawScene(int w, int h) {
    if (credits.stage || titleMenu.stage) return; // (the staff roll: its figure over black, in the interface's pass; the title)
    if (battleMode) {
        drawBattle(w, h);
        return;
    }
    if (eventMode) {
        drawEvent(w, h);
        return;
    }
    if (fieldMode) {
        // FldMain's order: the background (and midground), the 3D figures, the foreground
        // the scroll: following the leader, else the script's camera (a slide drawn on between updates)
        cr2::FieldFrame fr;
        field.setQuake(script ? script->quakeOffset() : 0.0f); // (Cr3FieldMapCamera.QuakeStart)
        if (script && script->camera().scripted()) {
            const cr2::FieldScript::Camera& c = script->camera();
            float cx = c.x, cy = c.y;
            if (c.frames > 0 && c.t < c.frames - 1) { // (the step under way; the slide stops a step short)
                const float k = std::clamp(fieldClock / cr2::EventRuntime::kTick, 0.0f, 1.0f) / float(c.frames);
                cx += (c.toX - c.fromX) * k, cy += (c.toY - c.fromY) * k;
            }
            fr = field.frameScrolled(w, h, player, cx, cy);
        } else if (fieldCam.set) { // (the eased scroll, drawn on between updates)
            const float a = std::clamp(fieldCam.clock / cr2::EventRuntime::kTick, 0.0f, 1.0f);
            fr = field.frameScrolled(w, h, player, -(fieldCam.prev[0] + (fieldCam.cur[0] - fieldCam.prev[0]) * a),
                                     -(fieldCam.prev[1] + (fieldCam.cur[1] - fieldCam.prev[1]) * a));
        } else {
            const float focus[3] = {player[0], player[1] + 0.045f, player[2]};
            fr = field.frame(w, h, focus);
        }
        camW = w, camH = h;
        if (script) script->setShownScroll(-fr.scroll[0] / (float(w) / 960.0f), -fr.scroll[1] / (float(w) / 960.0f));
        fieldScene = fr.scene;
        dev->setCamera(fr.zNear, fr.zFar, fr.fovY * 3.14159265f / 180);
        cr2::SceneLighting l = field.lighting();
        std::memcpy(l.camera, fr.camera, sizeof l.camera);
        // FldMap::Draw on the sky's maps (+0x15): the point light carried - the light figure's
        // place + the leader's DummyEP04 + 0.1 up
        cr2::Mat34 ep;
        if (field.info().sharedBackground && jointPlace(fig, "DummyEP04", ep))
            l.pointPos[0] += ep.m[3], l.pointPos[1] += ep.m[7] + 0.1f, l.pointPos[2] += ep.m[11];
        l.time = time;
        dev->setLighting(l);
        // (the layers the script hid - Cr3FieldMap.SetVisibleBG / MG / 3D / FG - left out)
        auto shown = [this](int layer) { return !script || script->layerVisible(layer); };
        if (shown(0)) drawLayer(field.backgroundImage(), fr.background);
        if (shown(2)) drawLayer(field.midgroundImage(), fr.midground);
        if (shown(3)) drawFigure(mapFigure, fr.scene.data());
        // (FldNPC / FldBox: their FldShadow at the figure's place - an NPC's on while it's shown,
        // FldUtilSetGimmickVisible's SetShadowVisible; a box's always)
        for (size_t k = 0; k < gimmicks.size(); ++k) {
            if (k < gimmickShadow.size() && gimmickShadow[k]) drawShadow(gimmicks[k].place, fr.scene.data());
            drawFigure(gimmicks[k], fr.scene.data());
        }
        drawResidents(fr.scene.data());
        drawGroundFx(fr.scene.data());
        // an event's scene in the field (EvtScene in FldMain: its models among the field's
        // figures, through the field's camera), the leader unless the script hid it
        if (script && script->events().sceneShown()) {
            const std::vector<cr2::EventModel>& models = script->events().scene().models();
            for (size_t i = 0; i < models.size() && i < eventFigures.size(); ++i)
                if (models[i].visible) {
                    if (models[i].hasShadow() && models[i].shadow) drawShadow(eventFigures[i].place, fr.scene.data());
                    drawFigure(eventFigures[i], fr.scene.data());
                }
        }
        if (!script || script->playerVisible()) drawShadow(fig.place, fr.scene.data()), drawFigure(fig, fr.scene.data());
        if (shown(1)) drawLayer(field.foregroundImage(), fr.foreground, field.info().foregroundShift == 1 ? -1152.0f : 0.0f);
        return;
    }
    float eye[3];
    const float d = modelEye(eye);
    cr2::SceneLighting l = lighting;
    std::memcpy(l.camera, eye, sizeof l.camera);
    l.time = time;
    dev->setLighting(l);
    const float zn = fig.radius * 0.01f * zoom, zf = fig.radius * 20 * zoom + d;
    dev->setCamera(zn, zf, 0.7f);
    const Mat4 mvp = cr2::mat4Multiply(perspective(0.7f, float(w) / float(std::max(h, 1)), zn, zf), lookAt(eye, fig.centre));
    drawFigure(fig, mvp.data());
}

// the interface's projection over a w x h screen: UI units to clip space (z: the camera's
// near / far, 2.56 either way)
Mat4 Viewer::uiProjection(int w, int h) {
    const float k = std::min(float(w) / 0.96f, float(h) / 0.64f); // pixels a unit
    Mat4 m = cr2::mat4Identity();
    m[0] = k / (float(w) * 0.5f);
    m[5] = k / (float(h) * 0.5f);
    m[10] = -1.0f / 2.56f;
    return m;
}
auto Viewer::uiFig(const std::string& name) -> UiFig& {
    UiFig& u = uiFigs[name];
    if (!u.loaded) {
        u.loaded = true;
        std::string err;
        const float keep = time;
        loadingUi = true; // (the menus' parts as built: Smooth Models is the world's)
        if (!loadFigure(name + ".geom", u.fig, &err)) SDL_Log("ui %s: %s", name.c_str(), err.c_str());
        loadingUi = false;
        time = keep;
        std::vector<uint8_t> d;
        if (archive.read(name + ".name", d)) u.joints = cr2::parseJointNames(d), u.materials = cr2::parseMaterialNames(d);
    }
    return u;
}
void Viewer::poseUi(UiFig& u, const std::string& anim, float t, bool loop) {
    // (the figure's own animation - SetParameterDataBase's, alone in slot 0 - is absolute;
    // the animators added beside it relative)
    u.fig.absolute = anim + ".geom" == u.fig.name;
    const auto it = std::find(u.fig.anims.begin(), u.fig.anims.end(), anim + ".anim");
    if (it != u.fig.anims.end() && (!u.fig.animLoaded || u.fig.anims[u.fig.animIndex] != *it)) {
        const float keep = time;
        loadAnim(u.fig, size_t(it - u.fig.anims.begin()));
        time = keep;
    }
    // (the interface's camera, not the scene's: its billboards - mainicon_id's, cost_id's,
    // sopia_id's quads - face the screen; posed under a battle's camera they turned edge-on)
    const cr2::Mat34 keepView = poseView;
    poseView = cr2::Mat34();
    poseView.m[11] = 0.881f; // (camera[14] = -0.881: the eye 0.881 in front)
    pose(u.fig, t, loop);
    poseView = keepView;
}
// the right edge of a posed figure's meshes (UI units: their vertices as last posed)
float Viewer::uiRight(const UiFig& u) {
    float r = -1e9f;
    for (const ViewMesh& vm : u.fig.meshes) {
        if (vm.hidden) continue;
        for (size_t o = 0; o + 12 <= vm.vertices.size(); o += cr2::kVertexStride) {
            float x;
            std::memcpy(&x, &vm.vertices[o], 4);
            r = std::max(r, x);
        }
    }
    return r;
}
// a posed joint's place in UI units (false: none)
bool Viewer::uiJoint(const UiFig& u, const std::string& joint, float& x, float& y) {
    const auto it = std::find(u.joints.begin(), u.joints.end(), joint);
    if (it == u.joints.end() || !u.fig.skinned) return false;
    std::vector<cr2::JointTransform> p = u.fig.skeleton.basePose;
    if (!u.fig.posed.empty()) p = u.fig.posed; // (where its animation has it now)
    else if (u.fig.animLoaded) p = u.fig.anim.poseOver(u.fig.skeleton.basePose, 0);
    const std::vector<cr2::Mat34> w = u.fig.skeleton.world(p, u.fig.place);
    const size_t j = size_t(it - u.joints.begin());
    if (j >= w.size()) return false;
    x = w[j].m[3], y = w[j].m[7];
    return true;
}
void Viewer::loadDotNum() {
    if (!dotNumGlyphs.empty()) return;
    // (DotNumParamPackTable: tools/gen_dotnum_table.py -> gfx/dotnumtable.inc)
    for (const auto& g : kDotNum) dotNumGlyphs.push_back({g[0], g[1], g[2], g[3], g[4], g[5]});
}
// (`skip`: the places left empty on the right first - DotNumId2::SetOZNumber's start)
void Viewer::drawDigits(int value, int set, int digits, float x, float y, const Mat4& proj, float alpha, float scale, bool zeros, int skip) {
    loadDotNum();
    cr2::Texture* tex = texture("hud_dotnum", false);
    if (!tex) return;
    int place = 1;
    for (int i = 0; i < digits; ++i, place *= 10) {
        const int d = (i == 0 && value == 0) || zeros ? value / place % 10 : value < place ? -1 : value / place % 10; // (Cr3UtilGetNumberDisplay; SetZeroNumber: padded)
        if (d < 0) continue;
        if (size_t(set + d) >= dotNumGlyphs.size()) continue;
        const Glyph& g = dotNumGlyphs[size_t(set + d)];
        const float w = g.w / 1000.0f * scale, h = g.h / 1000.0f * scale;
        const float cx = x - float(i + skip) * w;
        const float x0 = cx - w / 2, x1 = cx + w / 2, y0 = y - h / 2, y1 = y + h / 2;
        const float v[24] = {x0, y0, g.u0, g.v1, x1, y0, g.u1, g.v1, x1, y1, g.u1, g.v0,
                             x0, y0, g.u0, g.v1, x1, y1, g.u1, g.v0, x0, y1, g.u0, g.v0};
        const float rgba[4] = {1, 1, 1, alpha};
        dev->drawQuad(*tex, v, proj.data(), rgba);
    }
}
// `left`: from x (typeS_L: the text's left), else centred on it; `shade` its colour's
// `maxWidth` (UI units, 0 none): narrowed to fit it (MenuText's width limit)
// A text's width as drawUiText draws it (UI units: 0 none)
float Viewer::uiTextWidth(const std::string& text, float size, int w, int h) {
    if (text.empty() || !font.loaded()) return 0;
    const float k = std::min(float(w) / 0.96f, float(h) / 0.64f);
    const int px = std::max(1, int(size / 1000.0f * k + 0.5f));
    int tw = 0, th = 0;
    const std::string key = text + "\n" + std::to_string(px);
    if (auto it = uiTexts.find(key); it != uiTexts.end()) return it->second.w;
    font.render(text, px, tw, th);
    return float(tw) * (size / 1000.0f / float(px));
}
float Viewer::drawUiText(const std::string& text, float size, float x, float y, int w, int h, bool left, float shade, float maxWidth) {
    if (text.empty() || !font.loaded()) return 0;
    const float k = std::min(float(w) / 0.96f, float(h) / 0.64f);
    const int px = std::max(1, int(size / 1000.0f * k + 0.5f));
    const int border = uiTextBorder ? std::max(1, int(0.002f * k + 0.5f)) : 0;
    const std::string key = text + "\n" + std::to_string(px) + (border ? "b" + std::to_string(border) : "");
    auto it = uiTexts.find(key);
    if (it == uiTexts.end()) {
        TextQuad q;
        int tw = 0, th = 0;
        cr2::Image img = font.render(text, px, tw, th, nullptr, border);
        if (img.rgba.empty()) return 0;
        q.tex = dev->upload(img);
        const float unitsPerPx = size / 1000.0f / float(px);
        q.w = float(tw) * unitsPerPx, q.h = float(th) * unitsPerPx;
        it = uiTexts.emplace(key, q).first;
    }
    it->second.used = ++uiTextUses;
    cr2::trimLeastUsed(uiTexts, kUiTextLimit, [this](TextQuad& t) { dev->destroy(t.tex); });
    const TextQuad& q = it->second;
    const float qw = maxWidth > 0 ? std::min(q.w, maxWidth) : q.w;
    if (border) x -= 0.002f, y += 0.002f;
    const float x0 = left ? x : x - qw / 2, x1 = x0 + qw, y0 = y - q.h / 2, y1 = y + q.h / 2;
    const float v[24] = {x0, y0, 0, 1, x1, y0, 1, 1, x1, y1, 1, 0, x0, y0, 0, 1, x1, y1, 1, 0, x0, y1, 0, 0};
    const float rgba[4] = {shade * uiTextTint[0], shade * uiTextTint[1], shade * uiTextTint[2], 1};
    dev->drawQuad(q.tex, v, uiProjection(w, h).data(), rgba);
    return qw;
}
float Viewer::uiDuration(const std::string& anim, const std::string& fig) {
    UiFig& f = uiFig(fig.empty() ? anim : fig);
    poseUi(f, anim, 0);
    return f.fig.animLoaded ? f.fig.anim.duration() : 0.5f;
}

} // namespace cr2_viewer
