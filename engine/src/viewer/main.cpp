// cr2view: CR2-U's model and field viewer.
//
// Models: a figure (`.geom`) from your copy's archive, its meshes with their materials' textures,
// render states and the game's shading (its generated shaders, as shading.glsl), lit by a map's
// character lights (`<map>c.geom`; d0101c unless --lights says), skinned to its skeleton (`.skel`)
// and playing its animations (`<figure>*.anim`).
//
// Fields (--field <map>): the map's painted layers and 3D figure with a character (--model, c001
// unless given) at one of its points (--at; defaultPos), through the map's camera, scrolling to
// keep the character centred (field/fieldmap.hpp). The maps' table is in the game's executable:
// found beside the iPad's or the Vita's archive, in the APK beside Android's OBB (its
// libChaosRings2.so; assets/release.hpp), or given with --binary.
//
// Events (--event <function> --vista <map>): an event's script played by the runtime (event/
// runtime.hpp) in its vista - the map's figure (d0151b for the opening's), the scene's models on
// their locators with their animations, through the scene's camera, under its lights, with the
// screen's fades - 20 updates a second. A message's pages turn on Return or a click (or by
// themselves after --page seconds; 0 never). --time runs the event that far first.
//
// The output: --resolution WxH (up to 3840 x 2160; F1 steps through the window's own size and the
// 16:9 ones) renders frames of that size, shown in the window at their shape (letterboxed; GL
// draws into the window, the resolution's shape letterboxed in it). --upscaler fsr3 (F2 steps
// through its modes) renders the scene smaller - --quality native (AA at full size), quality
// (1.5x), balanced (1.7x), performance (2x), ultra (3x) - and AMD FSR 3.1 resolves it into the
// frame, with --sharpness (0..1, 0.3) for its RCAS pass; Vulkan only.
//
// Frames: 60 a second (--fps; 0 as many as the display shows). The game itself updates 20 times
// a second (its scripts, fades, physics); the frames between show its timelines where they are
// by then - an event's camera, moves, animations and fades - and the field's player between its
// last two updates.
//
// Vulkan by default, OpenGL where Vulkan can't start (or with --renderer opengl).
//
//   cr2view --data <CRDB.ios.mvgl | main.212...obb | CRDB.psp2.mvgl, or a folder holding one:
//           the Vita's PCSG00498, Android's OBB + APK, an unpacked .ipa> [--model <name>]
//           [--anim <name>] [--time s] [--size WxH] [--yaw deg] [--pitch deg]
//           [--lights <figure> | none] [--field <map> [--at <point>] [--binary <file>]
//           [--walk sx,sy,seconds]] [--event <function> [--vista <map>] [--page s]]
//           [--resolution WxH] [--upscaler off|fsr3 [--quality q] [--sharpness s]] [--run s] [--fps n]
//           [--renderer vulkan|opengl] [--capture out.bmp]
//
// Models: drag to turn, the wheel to zoom, Left / Right (or PageUp / PageDown) for the archive's
// other models, Up / Down for the figure's animations. Fields: the arrow keys (or WASD) run,
// with Shift walk, on the map's floors and against its walls;
// PageUp / PageDown for the character's animations. Space pauses, Esc quits. --capture draws
// one frame (at --time; --run plays that many seconds more first, 60 frames a second) to a BMP
// and exits.
#include "platform/vita_import_ui.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <map>
#include <set>
#include <filesystem>
#include <sstream>
#include <memory>
#include <random>
#include <string>
#include <tuple>
#include <vector>

#include "assets/geom.hpp"
#include "assets/smooth.hpp"
#include "assets/hd_texture.hpp"
#include "assets/gltf_figure.hpp"
#include "field/navigation.hpp"
#include "assets/image.hpp"
#include "assets/maptable.hpp"
#include "assets/release.hpp"
#include "assets/mvgl.hpp"
#include "assets/skeleton.hpp"
#include "battle/battledata.hpp"
#include "battle/camera.hpp"
#include "battle/motion.hpp"
#include "battle/command.hpp"
#include "battle/rules.hpp"
#include "battle/script.hpp"
#include "battle/special.hpp"
#include "battle/glass.hpp"
#include "script/gamesave.hpp"
#include "event/runtime.hpp"
#include "viewer/audio.hpp"
#include "viewer/mobile_input.hpp"
#include "viewer/setup.hpp"

std::string storedData(); // (settings.ini's data=: below)
#include "field/character.hpp"
#include "field/fieldmap.hpp"
#include "field/fieldscript.hpp"
#include "gfx/material.hpp"
#include "gfx/render_device.hpp"
#include "gfx/text.hpp"

namespace {

#include "gfx/dotnumtable.inc"

using cr2::Mat4;

// GL enums the materials' render states use
constexpr uint32_t kGlSrcAlpha = 0x0302, kGlOneMinusSrcAlpha = 0x0303, kGlFront = 0x0404, kGlBack = 0x0405;

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

struct ViewMesh {
    cr2::GpuMesh gpu;
    bool lit = false;
    cr2::Texture* texture = nullptr;
    cr2::Texture* normalMap = nullptr; // 0x35
    cr2::Texture* extraMap = nullptr;  // 0x48 CLUT or 0x43 light map
    cr2::GeomMesh source;              // (for skinning)
    cr2::GeomMaterial material;        // (its render states; its parameters as the animation leaves them)
    cr2::GeomMaterial baseMaterial;    // (as the file has it)
    std::vector<uint8_t> vertices;     // kVertexStride bytes each
    bool hidden = false;               // (PartsBase::SetVisible(joint, false): its joint's meshes)
    uint32_t placing = 0;              // its shader's cr2::VertexPlacing (billboards)
};

// A figure on screen: its meshes, skeleton, animations and where it stands
struct ViewFigure {
    std::string name; // "c005.geom"
    std::vector<ViewMesh> meshes;
    cr2::Skeleton skeleton;
    bool skinned = false;
    std::vector<cr2::Mat34> inverseBind;
    std::vector<std::string> anims; // the figure's animations
    size_t animIndex = 0;
    cr2::Animation anim;
    bool animLoaded = false;
    float centre[3] = {0, 0, 0}, radius = 1;
    cr2::Mat34 place; // in the world (the skinning matrices are place * world * inverse bind)
    bool absolute = false; // its animation alone in slot 0 (map figures): not a motion over the base
    // a cross-fade (EvtMotionManager's two slots): the animation it fades from, where in it, how
    // far the fade is (1: none)
    std::string blendName;
    cr2::Animation blendAnim;
    bool blendLoaded = false, blendAbsolute = false, blendLoop = true;
    float blendTime = 0, blendWeight = 1;
    size_t vertices = 0, triangles = 0;
    std::vector<cr2::JointTransform> posed; // (the pose last drawn)
    // its colour and opacity over its materials' (BtlModel::StartDiffuseAnimation /
    // StartAlphaAnimation: the special scenes'); short of 1 opaque meshes drawn blended
    float tint[4] = {1, 1, 1, 1};
};

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

// The output resolutions F1 steps through: the window's own size, then 16:9 up to 3840 x 2160
const int kResolutions[][2] = {{0, 0}, {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3200, 1800}, {3840, 2160}};
constexpr int kMaxResW = 3840, kMaxResH = 2160;

// FSR 3.1's quality modes (the output / render ratio per axis; native AA renders at full size)
struct Quality {
    const char* name;
    float ratio;
};
const Quality kQualities[] = {{"native", 1.0f}, {"quality", 1.5f}, {"balanced", 1.7f}, {"performance", 2.0f}, {"ultra", 3.0f}};

struct Viewer {
    cr2::RenderDevice* dev = nullptr;
    cr2::MvglArchive archive;
    cr2::AudioPlayer audio; // (the music, effects and voices: viewer/audio.hpp)
    std::vector<std::string> models; // the archive's .geom names, sorted
    size_t current = 0;
    ViewFigure fig;                  // the model (fields: the character)
    std::map<std::string, cr2::Texture> textures; // by name
    // HD textures (tools/hd_textures.py's): replacements found by the original's pixels' hash in
    // hdDir - CR2_HD_TEXTURES, else textures/hd/ in the game folder (the older hd-textures/ beside
    // the executable or in the profile folder while there is none)
    bool hdTextures = false;
    std::string hdDir;
    int hdLoaded = 0, hdMissing = 0;
    // HD models (the model bridge, assets/gltf_figure.hpp): replacements for figures' meshes from
    // modelsDir - CR2_HD_MODELS, else models/hd/ in the game folder - and the pictures they bring
    bool hdModels = false;
    std::string modelsDir;
    int hdModelsLoaded = 0;
    std::map<std::string, cr2::Image> modelImages; // "gltf\\<figure>\\<n>" -> its pixels (decode's first look)
    std::map<std::string, bool> textureMips; // (each cached texture's mip chain: a reload's)

    // CR2-U's own settings - the options' Display & Graphics list (settingsOpen), kept in
    // settings.ini in the profile folder: read at the start, written on each change (a capture
    // neither reads nor writes it); the command line's flags over them
    struct Settings {
        int display = 0;          // 0 a window, 1 fullscreen (the display's own mode), 2 borderless (the desktop)
        int resolution = 0;       // kResolutions: 0 the window's own size
        bool vsync = true;
        int fps = 60;             // frames drawn a second at most (0: as many as the display takes)
        int renderer = 0;         // 0 Vulkan, 1 OpenGL - from the next start
        int upscaler = 0;         // cr2::Upscaler: 0 off, 1 FSR 3.1, 2 DLSS, 3 MetalFX
        int quality = 1;          // kQualities
        int sharpness = 30;       // % (FSR's sharpening pass, RCAS)
        int msaa = 1;             // 1, 2, 4, 8 (Vulkan; none while an upscaler is on)
        int anisotropy = 16;      // samples (1: off)
        bool sharpTextures = false;
        bool hdTextures = false;
        bool hdSet = false;       // (settings.ini says which: else on when there are HD textures)
        bool hdModels = false;    // replacement models from models/hd (what loads next)
        int smoothModels = 0;     // smoothed models (assets/smooth.hpp): 0 off, 2 / 3 the PN triangles' level (what loads next)
        bool lockOrientation = false, landscapeFlipped = false;
        bool padSwap = false;     // a controller's: × (south) confirms, ○ (east) goes back - else as the Vita
        int fieldSpeed = 1;       // the field while the player walks it (nothing running there), this many times as fast: 1, 2 .. 5
        int battleSpeed = 1;      // the battles, this many times as fast: 1, 2 .. 5
    } settings;
    bool settingsPersist = false;
    int rendererInUse = 0;        // (the device that started: the Renderer row's "next start")
    SDL_Window* window = nullptr;
    int appliedDisplay = -1, appliedMsaa = -1;
    bool appliedVsync = true, vsyncApplied = false;
    bool orientationApplied = false, appliedLock = false, appliedFlip = false;

    static const char* settingsFile() { return "settings.ini"; }
    void loadSettings() {
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
            else if (k == "upscaler") s.upscaler = std::clamp(v, 0, 3);
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
    void saveSettings() const {
        if (!settingsPersist) return;
        const std::string keepData = storedData(); // (the setup's copy, kept - read before the file is rewritten)
        std::FILE* f = std::fopen(savePath(settingsFile()).c_str(), "wb");
        if (!f) return;
        const Settings& s = settings;
        std::fprintf(f, "# CR2-U: the options' Display & Graphics (the command line's flags go over these)\n"
                        "display=%d\nresolution=%d\nvsync=%d\nfps=%d\nrenderer=%d\nupscaler=%d\nquality=%d\nsharpness=%d\n"
                        "msaa=%d\nanisotropy=%d\nsharp_textures=%d\nhd_textures=%d\nhd_models=%d\nsmooth_models=%d\npad_swap=%d\nfield_speed=%d\nbattle_speed=%d\n",
                     s.display, s.resolution, s.vsync ? 1 : 0, s.fps, s.renderer, s.upscaler, s.quality, s.sharpness, s.msaa,
                     s.anisotropy, s.sharpTextures ? 1 : 0, s.hdTextures ? 1 : 0, s.hdModels ? 1 : 0, s.smoothModels, s.padSwap ? 1 : 0, s.fieldSpeed,
                     s.battleSpeed);
        std::fprintf(f, "lock_orientation=%d\nlandscape_flipped=%d\n", int(s.lockOrientation), int(s.landscapeFlipped));
        if (!keepData.empty()) std::fprintf(f, "data=%s\n", keepData.c_str());
        std::fclose(f);
    }
    // The settings onto the device, the window and the textures (each only as it changes)
    void applySettings() {
        if (!dev) return;
        Settings& s = settings;
        if (!vsyncApplied || appliedVsync != s.vsync) dev->setVSync(s.vsync), appliedVsync = s.vsync, vsyncApplied = true;
        const int r = std::clamp(s.resolution, 0, int(std::size(kResolutions)) - 1);
        if (s.resolution >= 0 && (resW != kResolutions[r][0] || resH != kResolutions[r][1])) {
            resW = kResolutions[r][0], resH = kResolutions[r][1];
            dev->resetHistory();
        }
        cr2::UpscaleSettings u;
        u.mode = cr2::Upscaler(std::clamp(s.upscaler, 0, 3));
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
            if (s.display == 1) SDL_SetWindowFullscreenMode(window, SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(window)));
            else SDL_SetWindowFullscreenMode(window, nullptr);
            SDL_SetWindowFullscreen(window, s.display != 0);
            appliedDisplay = s.display;
        }
        if (s.hdTextures != hdTextures) hdTextures = s.hdTextures, reloadTextures();
        if (!orientationApplied || appliedLock != s.lockOrientation || appliedFlip != s.landscapeFlipped) {
            mobile::orientation(window, s.lockOrientation, s.landscapeFlipped);
            appliedLock = s.lockOrientation, appliedFlip = s.landscapeFlipped, orientationApplied = true;
        }
        hdModels = s.hdModels; // (from the next figure loaded)
    }
    // Every cached texture again (HD Textures turned on or off): in place, so what holds one sees the new
    void reloadTextures() {
        hdLoaded = hdMissing = 0;
        for (auto& [name, t] : textures) {
            if (!t.id) continue;
            cr2::Image img;
            if (!decode(name, img)) continue;
            if (hdTextures && !hdDir.empty()) {
                cr2::Image hd;
                if (cr2::loadHdImage(hdDir, img, hd)) hd.logicalW = img.width, hd.logicalH = img.height, img = std::move(hd), ++hdLoaded;
                else ++hdMissing;
            }
            img.mipmaps = textureMips.count(name) ? textureMips[name] : true;
            cr2::Texture old = t;
            t = dev->upload(img);
            dev->destroy(old);
        }
        SDL_Log("hd textures %s: %d replaced, %d without one", hdTextures ? "on" : "off", hdLoaded, hdMissing);
    }

    // The list's rows: its label and value, its help (the list's window), what Left / Right / a
    // tap does (+1 / -1)
    // a folder as the settings' help shows it: its last two names (".../models/hd/")
    static std::string shortDir(const std::string& dir) {
        size_t at = dir.size() > 1 ? dir.find_last_of("/\\", dir.size() - 2) : std::string::npos;
        if (at != std::string::npos && at > 0) at = dir.find_last_of("/\\", at - 1);
        return at == std::string::npos ? dir : "..." + dir.substr(at);
    }
    struct SettingRow {
        std::string label, value, help;
        bool enabled = true;
        std::function<void(int)> change;
    };
    bool settingsOpen = false;
    int settingsCursor = 0;
    float settingsStart = 0;
    std::vector<SettingRow> settingRows() {
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
        static const char* const kUpscaler[] = {"Off", "FSR 3.1", "DLSS", "MetalFX"};
        {
            std::string v = kUpscaler[std::clamp(s.upscaler, 0, 3)];
            std::string why = s.upscaler && dev ? dev->upscalerProblem(cr2::Upscaler(s.upscaler)) : std::string();
            if (!why.empty()) v += " (n/a)";
            rows.push_back({"Upscaler", v, why.empty() ? "The scene renders smaller and is rebuilt at full size; the menus stay sharp" : kUpscaler[s.upscaler] + std::string(": ") + why,
                            vk, [this, &s](int d) {
                                // (the ones this device can run, Off always)
                                for (int k = 1; k <= 4; ++k) {
                                    const int u = ((s.upscaler + d * k) % 4 + 4) % 4;
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
                        vk && s.upscaler == 1, [&s](int d) { s.sharpness = std::clamp(s.sharpness + d * 5, 0, 100); }});
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
        rows.push_back({"Defaults", "", "Every setting here back to its default", true, [&s](int) { s = Settings(); }});
        return rows;
    }
    void settingChange(int row, int dir) {
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
    void openSettings() { settingsOpen = true, settingsStart = uiClock, settingsCursor = 0; }
    void closeSettings() { settingsOpen = false, optionsStart = uiClock; }
    void drawSettings(const Mat4& view, int w, int h) {
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
    bool settingsKey(SDL_Keycode k) {
        if (!settingsOpen) return false;
        const int n = int(settingRows().size());
        if (k == SDLK_UP) settingsCursor = (settingsCursor + n - 1) % n;
        else if (k == SDLK_DOWN) settingsCursor = (settingsCursor + 1) % n;
        else if (k == SDLK_LEFT) settingChange(settingsCursor, -1);
        else if (k == SDLK_RIGHT || k == SDLK_RETURN || k == SDLK_KP_ENTER) settingChange(settingsCursor, 1);
        else if (k == SDLK_BACKSPACE) closeSettings();
        return true;
    }
    bool settingsTapped(int id) { // (a tap's TapRect id)
        if (!settingsOpen) return false;
        if (id == 100) closeSettings();
        else if (id >= 6000) settingChange(id - 6000, 1);
        else if (id >= 1000) settingsCursor = id - 1000;
        return true;
    }
    cr2::SceneLighting lighting;                  // the light figure's (--lights)
    std::unique_ptr<cr2::ShaderLibrary> shaders;  // the archive's (its GLSL, or the keys decoded)
    float yaw = 30, pitch = 15, zoom = 1;
    std::string status;
    bool paused = false;
    float time = 0;
    // the field
    bool fieldMode = false;
    cr2::FieldMap field;
    ViewFigure mapFigure; // <map>f
    std::vector<ViewFigure> gimmicks; // the figures <map>p's NPCs, objects, boxes and effects show
    cr2::GameState game;                        // the story's flags and work values (a new game's)
    std::unique_ptr<cr2::FieldScript> script;   // the dungeon's script
    float player[3] = {0, 0, 0}, heading = 0;
    std::unique_ptr<cr2::CharacterController> controller; // (on the map's walls and floors)
    float stepClock = 0;

    // FldPlayer::Update: the stick (screen right / up, -1..1) as a direction along the ground from
    // the camera's view, walking (0.09 units a second) or running (0.48). The game's update (20 a
    // second) is the controller's three substeps (character.hpp), one each 1/60 s - the stick read
    // at each - with the exits checked at the update's end as the game does. A frame between
    // substeps shows the player between the last two; the figure stands 0.02 below the sphere.
    float playerPrev[3] = {0, 0, 0}, playerCur[3] = {0, 0, 0};
    int substep = 0;

    // The event boxes the leader walks into (Gimmick::Event, not _check - those are tapped):
    // entering one with its event on (Cr3GimmickData.SetEventEnable) calls its function, the
    // script's coroutine then running as the area's entry does (FldMain::CallEvent)
    std::set<std::string> insideEvents;
    bool pendingBranch = false;
    void touchEvents(const cr2::Vec3& p) {
        if (!script) return;
        std::set<std::string> now;
        for (const cr2::Gimmick& g : field.gimmicks()) {
            if (g.kind != cr2::Gimmick::Event || g.check) continue;
            const float at[3] = {p[0], p[1], p[2]};
            if (!g.touches(at, controller->radius)) continue;
            now.insert(g.name);
            const auto st = script->gimmicks().find(g.id);
            const bool enabled = st == script->gimmicks().end() || st->second.event;
            if (insideEvents.count(g.name) || !enabled || script->running()) continue;
            SDL_Log("%s: touched %s - %s", field.info().name.c_str(), g.name.c_str(), g.function.c_str());
            if (!script->call(g.function)) SDL_Log("%s", script->error().c_str());
            else if (!script->running() && playMode) pendingBranch = true; // (acted on at the next update: not mid-walk)
        }
        insideEvents.swap(now);
    }
    // an event's scene putting the leader at its p000 (FldUtilSetFieldMapPlayerPos / Rot)
    void placePlayer(const cr2::FieldScript::Placement& pl) {
        if (pl.heading) heading = pl.yaw;
        if (!pl.position || !controller) return;
        controller->setPosition({pl.at[0], pl.at[1] + 0.17f, pl.at[2]}); // (dropped onto the floor, as on arriving)
        for (int k = 0; k < 10; ++k) controller->update({0, 0, 0});
        const cr2::Vec3& p = controller->position();
        player[0] = p[0], player[1] = p[1] - 0.02f, player[2] = p[2];
        std::memcpy(playerPrev, player, sizeof player);
        std::memcpy(playerCur, player, sizeof player);
        stepClock = 0, substep = 0;
    }

    // The balloons over what the leader can tap (FldMain::CheckBalloon, each update it has control:
    // Gimmick::checkLength), the nearest the one a tap talks to (CalcNearestBalloonNum)
    std::vector<size_t> balloons; // (field.gimmicks() indices)
    int nearestBalloon = -1;
    std::string boxesDungeon;               // (the dungeon the boxes were drawn for: "d01", "t01")
    std::map<std::string, float> boxOpening; // (a box's gimmick name -> when it opened: its fa01)
    static uint32_t boxRandom() { // (lrand48's part: a simple generator of the viewer's own)
        static uint32_t x = 0x2545f491u;
        x ^= x << 13, x ^= x >> 17, x ^= x << 5;
        return x;
    }
    void checkBalloons() {
        balloons.clear(), nearestBalloon = -1;
        if (!script || script->running()) return;
        float best = 30000;
        const std::vector<cr2::Gimmick>& gs = field.gimmicks();
        for (size_t i = 0; i < gs.size(); ++i) {
            const cr2::Gimmick& g = gs[i];
            const float reach = g.checkLength();
            if (reach <= 0 || !script->visible(g)) continue;
            if (g.kind == cr2::Gimmick::Box && game.boxes.isOpen(g.boxKind, g.boxNumber)) continue; // (an opened box: none)
            const auto st = script->gimmicks().find(g.id);
            if (st != script->gimmicks().end() && !st->second.event) continue;
            const float d = std::hypot(g.position[0] - playerCur[0], g.position[1] - playerCur[1], g.position[2] - playerCur[2]); // (as of the update)
            if (d > reach) continue;
            balloons.push_back(i);
            if (d < best) best = d, nearestBalloon = int(i);
        }
    }

    // A tap (Return, a click): the vista's page; the field's - its event's page, else the nearest
    // balloon's function (FldMain::CheckTouchEventExec_Tap, EventExecute), the leader turned to it
    // The touch releases' walk (FldMain::PlayerMove, FldController): a finger put down on the field
    // with nothing else under it, dragged - the move is the line from where it went down to where
    // it is (FldPlayer::Update: as fractions of the screen's width and height, under 0.02 standing,
    // under 0.05 walking, else running); let go without moving, it's the leader's tap (on its
    // release, as FldMain's tap event). The stick shows once it moves or after 0.5 s held (FldController
    // ::Update): moveWin where it went down, moveCsr towards the finger - at most 128 away (x 2048,
    // the interface's units).
    struct WalkDrag {
        bool down = false, moved = false;
        float fx0 = 0, fy0 = 0, fx = 0, fy = 0, at = 0;
    } walkDrag;
    bool walkStart(float fx, float fy) {
        if (!fieldTabShown() || fieldTabOpen) return false;
        walkDrag = WalkDrag();
        walkDrag.down = true, walkDrag.fx0 = walkDrag.fx = fx, walkDrag.fy0 = walkDrag.fy = fy, walkDrag.at = uiClock;
        return true;
    }
    void walkMove(float fx, float fy) {
        walkDrag.fx = fx, walkDrag.fy = fy;
        if (std::hypot(fx - walkDrag.fx0, fy - walkDrag.fy0) > 0.01f) walkDrag.moved = true;
    }
    void walkEnd() {
        if (walkDrag.down && !walkDrag.moved) tap();
        walkDrag = WalkDrag();
    }
    bool walkStick(float& sx, float& sy, bool& run) const {
        if (!walkDrag.down || !fieldTabShown()) return false;
        const float dx = walkDrag.fx - walkDrag.fx0, dy = walkDrag.fy0 - walkDrag.fy, len = std::hypot(dx, dy);
        if (len < 0.02f) return false;
        sx = dx / len, sy = dy / len, run = len >= 0.05f;
        return true;
    }
    void drawWalkStick(const Mat4& view) {
        if (!walkDrag.down || (!walkDrag.moved && uiClock - walkDrag.at <= 0.5f)) return;
        const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
        const float x0 = (walkDrag.fx0 - 0.5f) * float(screenW) / k, y0 = (0.5f - walkDrag.fy0) * float(screenH) / k;
        const float x1 = (walkDrag.fx - 0.5f) * float(screenW) / k, y1 = (0.5f - walkDrag.fy) * float(screenH) / k;
        const float len = std::hypot(x1 - x0, y1 - y0), reach = std::min(len, 128.0f / 2048.0f);
        const float cx = len > 0 ? x0 + (x1 - x0) / len * reach : x0, cy = len > 0 ? y0 + (y1 - y0) / len * reach : y0;
        for (auto [name, x, y] : {std::tuple<const char*, float, float>{"movewin", x0, y0}, {"movecsr", cx, cy}}) {
            UiFig& u = uiFig(name);
            u.fig.place = cr2::Mat34(); // (before the pose: the skinning places the vertices)
            u.fig.place.m[3] = x, u.fig.place.m[7] = y;
            poseUi(u, name, uiClock, true);
            drawFigure(u.fig, view.data());
        }
    }
    struct MenuList { int* cursor = nullptr; int count = 0, context = 0; };
    mobile::MenuSwipe menuSwipe;
    MenuList menuList() {
        if (saveImport.active()) return {};
        if (settingsOpen) return {&settingsCursor, int(settingRows().size()), 1};
        if (optionsOpen || optionsAsk) return {};
        if (manualStage == 1) return {&manualCursor, int(manualEntries.size()), 2};
        if (campOpen && !campUse && !campTakeSopia && campLink != 201) {
            if (campList == 17) return {&campEquipCursor, int(equipRows().size()), 30 + campEquipMode};
            if ((campList == 16 || campList == 18) && !campSopiaDetail)
                return {&campCursor, int(campRows().size()), 100 + campList * 10 + campCategory};
        }
        return {};
    }
    bool menuScroll(int rows) {
        const MenuList list = menuList();
        if (!list.cursor || list.count <= 0) return false;
        const int old = *list.cursor;
        *list.cursor = mobile::scrollCursor(old, list.count, rows);
        if (*list.cursor != old && audio.opened()) audio.playSe(0xb3, false);
        return true;
    }
    bool menuPointerDown(float x, float y) {
        const MenuList list = menuList();
        if (!list.cursor || list.count <= 0 || x < 0.1f || x > 0.98f || y < 0.12f || y > 0.88f) return false;
        menuSwipe.begin(x, y, list.context);
        return true;
    }
    void menuPointerMove(float x, float y) {
        if (!menuSwipe.down) return;
        const auto list = menuList();
        if (!list.cursor || list.context != menuSwipe.context) { menuSwipe = {}; return; }
        menuScroll(menuSwipe.move(x, y));
    }
    bool menuPointerUp(float x, float y) {
        if (!menuSwipe.down) return false;
        menuPointerMove(x, y);
        const auto gesture = menuSwipe;
        menuSwipe = {};
        if (gesture.down && !gesture.moved) pointerActivate(x, y);
        return true;
    }
    void pointerActivate(float fx, float fy) {
        if (overFast(fx, fy)) fastHeld = true; // (the fast-forward button: held, not a tap)
        else if (titleMenu.stage) titleClick(fx, fy);
        else if (yesNoClick(fx, fy)) {}
        else if (battleMode && choosing) battleClick(fx, fy);
        else if (vslash.stage == 1) vslashPointer(fx, fy, 0);
        else if (fieldMode && !eventMode && keyLock.stage != 0) unlockPointer(fx, fy, 0);
        else if (fieldMode && !eventMode && iai.stage) iaiPointer(fx, fy, 0);
        else if (fieldMode && !eventMode && aim.stage) aimPointer(fx, fy, 0);
        else if (fieldMode && !eventMode && hakkei.stage) hakkeiPointer(fx, fy);
        else if (fieldMode && !eventMode && mystic.stage) mysticPointer(fx, fy);
        else if (fieldMode && !eventMode && (sprintTap(fx, fy) || actionTap(fx, fy))) {}
        else if (fieldMode && !eventMode && fieldClick(fx, fy)) {
        } else if (fieldMode && !eventMode && walkStart(fx, fy)) { // (a walk, or the tap on its release)
        } else if (eventMode || fieldMode || battleMode) tap();
    }
    void tap() {
        if (titleMenu.stage) return titleTap();
        if (vslash.stage) return; // (the slash: a swipe, not a tap)
        if (battleMode) {
            if (skipScene()) return;
            if (battleScript && battleScript->running()) battleScript->events().advance();
            else if (analyze.stage == 1 || analyze.stage == 2) analyze.tapped = true;
            else if (result.stage > 0) result.tapped = true;
            else if (gameOver.stage > 0) gameOver.tapped = true;
            return;
        }
        if (eventMode) {
            if (event) event->touch();
            return;
        }
        if (!fieldMode || !script) return;
        if (script->running()) {
            script->events().touch();
            return;
        }
        if (const int s = sprintAt(); s >= 0) return startSprint(s); // (FldMain: a sprint's before the balloons)
        if (actionMark.gimmick >= 0 && actionMark.startedAt < 0) return actionTapped(); // (then a touch action's)
        if (nearestBalloon < 0 || keyLock.stage != 0) return;
        const cr2::Gimmick& g = field.gimmicks()[size_t(nearestBalloon)];
        heading = std::atan2(g.position[0] - player[0], g.position[2] - player[2]);
        SDL_Log("%s: tapped %s - %s", field.info().name.c_str(), g.name.c_str(), g.function.c_str());
        std::string function = g.function;
        if (g.kind == cr2::Gimmick::Box) { // (FldMain's balloon of a box: opened, then its function - box_msg)
            if (g.boxKind == 1 && script->leader() != 1) function = "Message_obox"; // (a key box: only Orlando opens it)
            else if (g.boxKind == 1) return startUnlock(nearestBalloon); // (FldUnlock first)
            else openBox(g);
        }
        callGimmick(function);
    }
    void callGimmick(const std::string& function) {
        if (!script->call(function)) SDL_Log("%s", script->error().c_str());
        else if (!script->running() && playMode) pendingBranch = true;
        checkBalloons();
    }
    // FldBox::Open: the item one more (Cr3UtilGetItem), the box opening (fa01, then fn02), SE 0x153
    void openBox(const cr2::Gimmick& g) {
        const int item = game.boxes.item(g.boxKind, g.boxNumber, game.party);
        game.boxes.markOpen(g.boxKind, g.boxNumber);
        if (item > 0 && item < 300) game.itemCount[item] = static_cast<short>(std::min(game.itemCount[item] + 1, 99));
        audio.playSe(0x153, false);
        boxOpening[g.name] = uiClock;
        script->boxItem = item;
        SDL_Log("%s: box %s - item %d (%d now)", field.info().name.c_str(), g.id.c_str(), item, item > 0 && item < 300 ? game.itemCount[item] : 0);
    }

    // A key box's lock (FldBox's new mark, FldUnlock). The leader at an unopened key box gets its
    // mark over it (0.125 above it; FieldGimickEffect02Menu for Orlando - gmk2ring, its io then its
    // wait, and gmkBtn's io - else 00's gmk0rng_io). Tapped with Orlando leading, the lock: gmkBtn
    // out, then gmk2csr_loop; two fingers put down within 450 of the mark (the interface's units x
    // 2048: Cr3UtilSetBalloonPos) and spread 1000 apart (platform 1's; the ring scaled 1 + spread /
    // 1000) open it - SE 0x164, the ring out, 20 updates on the box opened (FldMain step 0x16:
    // its function called, FldBox::Open, SE 0x154); anything else fails it (SE 0x165, the mark
    // anew). The mouse is one finger, the other mirrored about the mark.
    struct KeyLock {
        int gimmick = -1;       // (the key box marked: field.gimmicks()'s; -1 none)
        bool orlando = false;   // (the mark's kind: 2, else 0)
        int stage = 0;          // 0 the mark, 1 the lock (fingers awaited), 2 opened (counting down)
        float markAt = 0, stageAt = 0;
        float spread = 0;       // (the fingers' distance, x 2048)
        bool pressing = false;
        int countdown = 0;
    } keyLock;
    static constexpr float kUnlockReach = 450, kUnlockSpread = 1000;
    void startUnlock(int gimmick) {
        keyLock.gimmick = gimmick, keyLock.orlando = true;
        keyLock.stage = 1, keyLock.stageAt = uiClock, keyLock.spread = 0, keyLock.pressing = false;
        audio.playSe(0x157, false); // (CheckTouchEventExec_Tap: FldUnlock's start)
        SDL_Log("%s: the lock of %s", field.info().name.c_str(), field.gimmicks()[size_t(gimmick)].id.c_str());
    }
    void unlockEnd(bool success) {
        if (keyLock.stage != 1) return;
        keyLock.pressing = false;
        if (success) {
            audio.playSe(0x164, false);
            keyLock.stage = 2, keyLock.stageAt = uiClock, keyLock.countdown = 20;
        } else {
            audio.playSe(0x165, false);
            keyLock.stage = 0, keyLock.markAt = uiClock, keyLock.spread = 0;
        }
        SDL_Log("%s: unlock %s", field.info().name.c_str(), success ? "succeeded" : "failed");
    }
    void updateKeyLock() {
        KeyLock& k = keyLock;
        if (k.stage == 2) {
            if (--k.countdown > 0) return;
            k.stage = 0;
            if (k.gimmick < 0 || size_t(k.gimmick) >= field.gimmicks().size()) return;
            const cr2::Gimmick& g = field.gimmicks()[size_t(k.gimmick)];
            k.gimmick = -1;
            openBox(g);
            audio.playSe(0x154, false);
            callGimmick(g.function);
            return;
        }
        if (k.stage == 1) return;
        int at = -1; // (the key box the leader is at)
        if (!script->running())
            for (size_t i : balloons) {
                const cr2::Gimmick& g = field.gimmicks()[i];
                if (g.kind == cr2::Gimmick::Box && g.boxKind == 1) at = int(i);
            }
        if (at != k.gimmick) k.gimmick = at, k.markAt = uiClock;
        k.orlando = script->leader() == 1;
    }
    // the mark's place in the interface's units (the box's, 0.125 above, through the field's camera)
    bool keyMarkPlace(float& x, float& y) {
        if (keyLock.gimmick < 0 || size_t(keyLock.gimmick) >= field.gimmicks().size()) return false;
        const cr2::Gimmick& g = field.gimmicks()[size_t(keyLock.gimmick)];
        const float p[3] = {g.position[0], g.position[1] + 0.125f, g.position[2]};
        return uiPlace(p, x, y);
    }
    // a field point's place in the interface's units (Cr3UtilSetBalloonPos, / 2048)
    bool uiPlace(const float p[3], float& x, float& y) {
        const float* m = fieldScene.data();
        const float cw = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
        if (cw <= 0) return false;
        const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
        x = (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]) / cw * float(screenW) * 0.5f / k;
        y = (m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]) / cw * float(screenH) * 0.5f / k;
        return true;
    }
    // the mouse on the lock (fx, fy: the screen's fractions): pressed, dragged, let go
    void unlockPointer(float fx, float fy, int phase) {
        if (keyLock.stage != 1) return;
        if (phase == 2) {
            if (keyLock.pressing) unlockEnd(false);
            return;
        }
        float mx, my;
        if (!keyMarkPlace(mx, my)) return;
        const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
        const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
        const float d = std::hypot(x - mx, y - my) * 2048.0f;
        if (phase == 0) {
            if (d <= kUnlockReach) keyLock.pressing = true, keyLock.spread = 2 * d;
            else unlockEnd(false);
        } else if (keyLock.pressing) {
            keyLock.spread = 2 * d;
            if (keyLock.spread >= kUnlockSpread) unlockEnd(true);
        }
    }
    // A sprint (FldSprint): the leader within 0.15 of an enabled one's place, a tap within 128 (x
    // 2048: FldUtilCheckTouchPosCollision) of its marker (obj151) - or Return - starts it
    // (FldMain step 0x15): crouched (fj01), then a hop to each of its spots in turn (fj02, SE
    // 0x155: a parabola under 0.98 - up 0.04 above the higher of the two -, Cr3Calculate...ArrivesAt;
    // fj03 to land, fj01 0.2 s before it, SE 0x156), then idle (fn01)
    struct Sprint {
        int stage = 0; // 0 none, 1 crouched, 2 a hop
        std::vector<std::array<float, 3>> to;
        size_t k = 0;
        float t = 0, T = 0, v = 0, vx = 0, vz = 0;
        float from[3] = {0, 0, 0};
        int air = 0; // (the hop's motions: 0 fj02, 1 fj03, 2 fj01)
        bool jump = false; // (a high jump's: the voice 0x8fa at each launch)
    } sprint;
    // The touch actions' marks (FldMain::CollisionAdded: the leader's body touching an enabled
    // one's place) on its first spot: its own character leading (kind 1 high jump - Darwin, 0 -,
    // 3 mystic - 2 -, 4 iai - 3 -, 5 aim - 4 -, 6 hakkei - 5), FieldGimickEffect0NMenu's (1:
    // gmk1rng, its io; 3..6: gmkNrng, its io then its wait, and gmkBtn's io), anyone else 00's
    // gmk0rng_io; gone as the leader leaves. A tap within 128 of it (FldMain::
    // CheckTouchActionStartOne) starts the action - for anyone else SE 0xb6. A high jump hops as
    // a sprint does (FldHighJump: the voice 0x8fa with each launch; its mark out, gmk1rng_out);
    // an iai (FldIai) takes the button out (then gmk4csr_loop) and waits for a slash: a press,
    // then within 10 frames its line from where it began past the mark - the mark's foot on the
    // line within 128 of it (Cr3UtilCheckCuttingCircle) - SE 0x164, the ring out, the gimmick's
    // function called (FldMain::CallEvent); else SE 0x165, the mark anew. An aim (FldAim) takes
    // the button out too (then FieldGimickEffect05Menu::SetAimingSet: gmk5mark_io on the mark,
    // gmk5goal_io on the leader, gmk5line between): a press within 128 of the mark, the ring
    // dragged with it (the line to it, scaled by its length / 0.112), let go within 128 of the
    // leader - SE 0x164, the function called -; let go elsewhere, or a press away from the mark,
    // SE 0x165 and the mark anew. A hakkei (FldHakkei) takes the button out, then its balls show
    // (FieldGimickEffect06Menu::SetCube: gmk5ball at the ring's call_gmk5ball joints - the
    // triangle's three, the pentagon's five from four on), one for each tap it wants (SetHakkei
    // TouchCnt; 5): each tap within 128 of the mark a punch (fp02 .. fp06), SE 0x160 or 0x161, a
    // ball gone; all of them, from the first, within 60 frames - SE 0x164, the function called -;
    // a tap away from the mark, or the time out, SE 0x165 and the mark anew. A mystic's mark is on
    // its active spot (SetMysticActiveSpotName); started (FldMystic), the button out and a ring
    // on each of its other spots (FieldGimickEffect03bMenu: gmk3rng); a tap within 128 of one
    // names it (SetTapSpotName - GetMysticTapSpotName: the script moves its object there), SE
    // 0x164, its ring out, the function called; a tap elsewhere SE 0x165 and the mark anew
    struct ActionMark {
        int gimmick = -1, kind = 0;
        bool able = false;
        float at = 0;         // (shown: uiClock)
        float startedAt = -1; // (begun)
        float endedAt = -1;   // (succeeded: the ring out)
    } actionMark;
    static int actionKind(const std::string& n) {
        return n.find("hjump") != std::string::npos ? 1 : n.find("mystic") != std::string::npos ? 3 : n.find("iai") != std::string::npos ? 4
             : n.find("aim") != std::string::npos ? 5 : n.find("marts") != std::string::npos ? 6 : 0;
    }
    static int actionCharacter(int kind) { return kind == 1 ? 0 : kind >= 3 && kind <= 6 ? kind - 1 : -1; }
    struct Iai {
        int stage = 0; // 0 none, 1 the slash awaited, 2 pressed
        float x0 = 0, y0 = 0, pressedAt = 0;
    } iai;
    struct Aim {
        int stage = 0; // 0 none, 1 a press on the mark awaited, 2 dragging
        float x = 0, y = 0; // (the touch: the interface's units)
    } aim;
    struct Hakkei {
        int stage = 0; // 0 none, 1 the taps awaited
        int wanted = 5;
        std::vector<float> tappedAt;
    } hakkei;
    struct Mystic {
        int stage = 0;  // 0 none, 1 a spot's tap awaited
        int chose = -1; // (the spot tapped: its ring out)
        float choseAt = 0;
    } mystic;
    bool actionRunning() const { return sprint.stage != 0 || iai.stage != 0 || aim.stage != 0 || hakkei.stage != 0 || mystic.stage != 0; }
    void updateActionMark() {
        if (actionRunning()) return;
        if (actionMark.startedAt >= 0 && (actionMark.endedAt < 0 || uiClock - actionMark.endedAt > 0.5f)) actionMark = ActionMark();
        if (actionMark.endedAt >= 0) return;
        int at = -1;
        if (controller && fieldTabShown()) {
            const std::vector<cr2::Gimmick>& gs = field.gimmicks();
            for (size_t i = 0; i < gs.size() && at < 0; ++i) {
                const cr2::Gimmick& g = gs[i];
                if (g.kind != cr2::Gimmick::Ability || actionKind(g.name) == 0 || !script->visible(g)) continue;
                const cr2::FieldScript::TouchAction* a = script->touchAction(g.name);
                if (a && a->enabled && !a->spots.empty() && g.touches(controller->position().data(), controller->radius)) at = int(i);
            }
        }
        if (at != actionMark.gimmick) {
            actionMark = ActionMark(), actionMark.gimmick = at, actionMark.at = uiClock;
            if (at >= 0) actionMark.kind = actionKind(field.gimmicks()[size_t(at)].name);
        }
        actionMark.able = actionMark.kind && script->leader() == actionCharacter(actionMark.kind);
    }
    bool actionMarkPlace(float& x, float& y) {
        if (actionMark.gimmick < 0) return false;
        const cr2::FieldScript::TouchAction* a = script->touchAction(field.gimmicks()[size_t(actionMark.gimmick)].name);
        if (!a || a->spots.empty()) return false;
        return uiPlace(a->spots[a->markSpot()].data(), x, y);
    }
    void actionTapped() {
        if (actionMark.gimmick < 0 || actionMark.startedAt >= 0) return;
        if (!actionMark.able) {
            audio.playSe(0xb6, false);
            return;
        }
        const cr2::Gimmick& g = field.gimmicks()[size_t(actionMark.gimmick)];
        const std::vector<std::array<float, 3>>& spots = script->touchAction(g.name)->spots;
        const size_t ms = script->touchAction(g.name)->markSpot();
        heading = std::atan2(spots[ms][0] - player[0], spots[ms][2] - player[2]); // (FldPlayer::SetDirection to its mark)
        if (actionMark.kind == 1) {
            sprint = Sprint();
            sprint.to = spots;
            sprint.stage = 1, sprint.jump = true;
            playMotion(fig, "fj01"), time = 0;
        } else if (actionMark.kind == 4) {
            iai = Iai();
            iai.stage = 1;
        } else if (actionMark.kind == 5) {
            aim = Aim();
            aim.stage = 1;
        } else if (actionMark.kind == 3) {
            mystic = Mystic();
            mystic.stage = 1;
        } else if (actionMark.kind == 6) {
            hakkei = Hakkei();
            hakkei.stage = 1, hakkei.wanted = std::min(script->touchAction(g.name)->touches, 8);
        } else {
            SDL_Log("%s: %s - its game isn't in yet", field.info().name.c_str(), g.name.c_str());
            return;
        }
        if (actionMark.kind != 1) audio.playSe(0x157, false); // (FldMain::CheckTouchEventExec_Tap: the abilities' start, not a sprint's)
        actionMark.startedAt = uiClock;
        SDL_Log("%s: %s started - %zu spots", field.info().name.c_str(), g.name.c_str(), spots.size());
    }
    bool actionTap(float fx, float fy) { // (a click: on the mark)
        float mx, my;
        if (actionMark.gimmick < 0 || actionMark.startedAt >= 0 || !fieldTabShown() || !actionMarkPlace(mx, my)) return false;
        const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
        const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
        if (std::hypot(x - mx, y - my) * 2048.0f > 128.0f) return false;
        actionTapped();
        return true;
    }
    void iaiEnd(bool success) {
        if (!iai.stage && !aim.stage && !hakkei.stage && !mystic.stage) return;
        const char* what = iai.stage ? "iai" : aim.stage ? "aim" : hakkei.stage ? "hakkei" : "mystic";
        iai = Iai(), aim = Aim(), hakkei = Hakkei(), mystic.stage = 0;
        const std::string function = field.gimmicks()[size_t(actionMark.gimmick)].name;
        SDL_Log("%s: %s %s", field.info().name.c_str(), what, success ? "succeeded" : "failed");
        if (!success) {
            audio.playSe(0x165, false);
            actionMark.startedAt = -1, actionMark.at = uiClock; // (the mark anew)
            return;
        }
        audio.playSe(0x164, false);
        actionMark.endedAt = uiClock;
        callGimmick(function);
    }
    // the mouse in an iai (fx, fy: the screen's fractions; phase 0 pressed, 1 dragged)
    void iaiPointer(float fx, float fy, int phase) {
        const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
        const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
        if (iai.stage == 1 && phase == 0) {
            iai.stage = 2, iai.x0 = x, iai.y0 = y, iai.pressedAt = uiClock;
            return;
        }
        float mx, my;
        if (iai.stage != 2 || phase != 1 || !actionMarkPlace(mx, my)) return;
        const float dx = x - iai.x0, dy = y - iai.y0, l2 = dx * dx + dy * dy;
        if (l2 <= 0) return;
        const float t = ((mx - iai.x0) * dx + (my - iai.y0) * dy) / l2;
        if (t < 0 || t > 1) return;
        if (std::hypot(iai.x0 + t * dx - mx, iai.y0 + t * dy - my) * 2048.0f < 128.0f) iaiEnd(true);
    }
    // the leader's place in the interface's units (FldUtilGet2DPos)
    bool leaderPlace(float& x, float& y) { return uiPlace(player, x, y); }
    // the mouse in an aim (phase 0 pressed, 1 dragged, 2 let go)
    void aimPointer(float fx, float fy, int phase) {
        const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
        const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
        float mx, my, lx, ly;
        if (!actionMarkPlace(mx, my) || !leaderPlace(lx, ly)) return;
        if (aim.stage == 1 && phase == 0) {
            if (std::hypot(x - mx, y - my) * 2048.0f <= 128.0f) aim.stage = 2, aim.x = x, aim.y = y;
            else iaiEnd(false);
        } else if (aim.stage == 2 && phase == 1) {
            aim.x = x, aim.y = y;
        } else if (aim.stage == 2 && phase == 2) {
            iaiEnd(std::hypot(x - lx, y - ly) * 2048.0f <= 128.0f);
        }
    }
    // a hakkei's tap (on the mark, or not)
    void hakkeiTap(bool onMark) {
        if (!hakkei.stage) return;
        if (!onMark) return iaiEnd(false);
        const int n = int(hakkei.tappedAt.size());
        hakkei.tappedAt.push_back(uiClock);
        playMotion(fig, ("fp0" + std::to_string(2 + std::min(n, 4))).c_str()), time = 0;
        audio.playSe(uiClock * 997.0f - std::floor(uiClock * 997.0f) < 0.5f ? 0x160 : 0x161, false); // (lrand48 % 2)
        if (n + 1 >= hakkei.wanted) iaiEnd(true);
    }
    void hakkeiPointer(float fx, float fy) {
        const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
        const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
        float mx, my;
        if (actionMarkPlace(mx, my)) hakkeiTap(std::hypot(x - mx, y - my) * 2048.0f <= 128.0f);
    }
    // a mystic's tap: on one of its other spots (its number), or not (-1)
    void mysticTap(int spot) {
        if (!mystic.stage) return;
        const cr2::FieldScript::TouchAction* a = script->touchAction(field.gimmicks()[size_t(actionMark.gimmick)].name);
        if (spot < 0 || !a || size_t(spot) >= a->spotNames.size()) return iaiEnd(false);
        script->mysticTapSpot = a->spotNames[size_t(spot)];
        mystic.chose = spot, mystic.choseAt = uiClock;
        SDL_Log("%s: the mystic's tap - %s", field.info().name.c_str(), script->mysticTapSpot.c_str());
        iaiEnd(true);
    }
    void mysticPointer(float fx, float fy) {
        const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
        const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
        const cr2::FieldScript::TouchAction* a = script->touchAction(field.gimmicks()[size_t(actionMark.gimmick)].name);
        if (!a) return;
        for (size_t i = 0; i < a->spots.size(); ++i) {
            float sx, sy;
            if (i != a->markSpot() && uiPlace(a->spots[i].data(), sx, sy) && std::hypot(x - sx, y - sy) * 2048.0f <= 128.0f) return mysticTap(int(i));
        }
        mysticTap(-1);
    }
    void updateIai() {
        if (hakkei.stage && !hakkei.tappedAt.empty() && uiClock - hakkei.tappedAt[0] > 60.0f / 60.0f) iaiEnd(false); // (60 frames)
        if (iai.stage == 2 && uiClock - iai.pressedAt > 10.0f / 60.0f) iaiEnd(false); // (10 frames)
    }
    void drawActionMark(const Mat4& view) {
        float x, y;
        if (actionMark.gimmick < 0 || !actionMarkPlace(x, y)) return;
        auto at = [&](UiFig& u) {
            u.fig.place = cr2::Mat34();
            u.fig.place.m[3] = x, u.fig.place.m[7] = y, u.fig.place.m[11] = -0.2f;
            drawFigure(u.fig, view.data());
        };
        auto duration = [&](UiFig& u, const std::string& anim) {
            poseUi(u, anim, 0);
            return u.fig.animLoaded ? u.fig.anim.duration() : 0.0f;
        };
        const float t = uiClock - actionMark.at, ts = uiClock - actionMark.startedAt, te = uiClock - actionMark.endedAt;
        if (!actionMark.able) {
            UiFig& m = uiFig("gmk0rng_io");
            poseUi(m, "gmk0rng_io", t);
            return at(m);
        }
        if (actionMark.kind == 1) {
            UiFig& m = uiFig("gmk1rng");
            if (actionMark.startedAt >= 0) poseUi(m, "gmk1rng_out", ts);
            else poseUi(m, "gmk1rng_io", t);
            return at(m);
        }
        const std::string ring = "gmk" + std::to_string(actionMark.kind) + "rng";
        UiFig& r = uiFig(ring);
        const float io = duration(r, ring + "_io");
        if (actionMark.kind == 5 && aim.stage == 2) { // (dragged)
            const float keepX = x, keepY = y;
            x = aim.x, y = aim.y;
            poseUi(r, ring + "_wait", t - io, true);
            at(r);
            x = keepX, y = keepY;
        } else if (actionMark.endedAt >= 0) poseUi(r, ring + "_out", te);
        else if (t < io) poseUi(r, ring + "_io", t);
        else poseUi(r, ring + "_wait", t - io, true);
        if (!(actionMark.kind == 5 && aim.stage == 2)) at(r);
        if (actionMark.endedAt >= 0 && actionMark.kind != 3) return;
        UiFig& b = uiFig("gmkbtn");
        if (actionMark.startedAt < 0) {
            poseUi(b, "gmkbtn_io", t);
            return at(b);
        }
        const float out = duration(b, "gmkbtn_out");
        if (ts < out) {
            poseUi(b, "gmkbtn_out", ts);
            at(b);
        } else if (actionMark.kind == 4) { // (then the iai's guide, looping)
            UiFig& c = uiFig("gmk4csr_loop");
            poseUi(c, "gmk4csr_loop", ts - out, true);
            at(c);
        } else if (actionMark.kind == 3 && (mystic.stage || mystic.chose >= 0)) { // (the mystic's other spots' rings)
            const cr2::FieldScript::TouchAction* a = script->touchAction(field.gimmicks()[size_t(actionMark.gimmick)].name);
            UiFig& o = uiFig("gmk3rng");
            for (size_t i = 0; a && i < a->spots.size(); ++i) {
                float sx, sy;
                if (i == a->markSpot() || !uiPlace(a->spots[i].data(), sx, sy)) continue;
                const float to = ts - out, ioO = duration(o, "gmk3rng_io");
                if (!mystic.stage && int(i) != mystic.chose) continue;
                if (!mystic.stage) poseUi(o, "gmk3rng_out", uiClock - mystic.choseAt);
                else if (to < ioO) poseUi(o, "gmk3rng_io", to);
                else poseUi(o, "gmk3rng_wait", to - ioO, true);
                o.fig.place = cr2::Mat34();
                o.fig.place.m[3] = sx, o.fig.place.m[7] = sy, o.fig.place.m[11] = -0.2f;
                drawFigure(o.fig, view.data());
            }
        } else if (actionMark.kind == 6 && hakkei.stage) { // (the hakkei's balls)
            static const char* const kTriangle[3] = {"call_gmk5ball01", "call_gmk5ball02", "call_gmk5ball03"};
            static const char* const kPentagon[5] = {"call_gmk5ball01_cpy", "call_gmk5ball02_cpy", "call_gmk5ball03_cpy", "call_gmk5ball04", "call_gmk5ball05"};
            const int n = hakkei.wanted;
            UiFig& ball = uiFig("gmk5ball");
            const float d = duration(ball, "gmk5ball");
            for (int i = 0; i < n && i < (n < 4 ? 3 : 5); ++i) {
                float bx, by;
                if (!uiJoint(r, n < 4 ? kTriangle[i] : kPentagon[i], bx, by)) continue;
                const bool tapped = size_t(i) < hakkei.tappedAt.size();
                const float bt = tapped ? uiClock - hakkei.tappedAt[size_t(i)] : std::max(d - (ts - out), 0.0f); // (appearing: played back; tapped: on)
                if (tapped && bt >= d) continue;
                poseUi(ball, "gmk5ball", bt);
                ball.fig.place = cr2::Mat34();
                ball.fig.place.m[3] = bx, ball.fig.place.m[7] = by, ball.fig.place.m[11] = -0.2f;
                drawFigure(ball.fig, view.data());
            }
        } else if (actionMark.kind == 5 && aim.stage) { // (the aim's: the mark, the goal, the line)
            float lx, ly;
            const float ta = ts - out;
            UiFig& mk = uiFig("gmk5mark_io");
            poseUi(mk, "gmk5mark_io", ta);
            at(mk);
            if (leaderPlace(lx, ly)) {
                UiFig& g = uiFig("gmk5goal_io");
                poseUi(g, "gmk5goal_io", ta);
                g.fig.place = cr2::Mat34();
                g.fig.place.m[3] = lx, g.fig.place.m[7] = ly, g.fig.place.m[11] = -0.2f;
                drawFigure(g.fig, view.data());
            }
            const float tx = aim.stage == 2 ? aim.x : x, ty = aim.stage == 2 ? aim.y : y;
            const float len = std::hypot(tx - x, ty - y), ang = std::atan2(tx - x, ty - y);
            UiFig& ln = uiFig("gmk5line");
            poseUi(ln, "gmk5line", ta);
            const float c = std::cos(ang), sn = std::sin(ang), sy = std::max(len / 0.112f, 0.1f);
            ln.fig.place = cr2::Mat34(); // (turned to the touch about z, its y scaled)
            ln.fig.place.m[0] = c, ln.fig.place.m[1] = sn * sy, ln.fig.place.m[4] = -sn, ln.fig.place.m[5] = c * sy;
            ln.fig.place.m[3] = x, ln.fig.place.m[7] = y, ln.fig.place.m[11] = -0.2f;
            drawFigure(ln.fig, view.data());
        }
    }
    int sprintAt() const {
        if (!fieldTabShown() || sprint.stage || !script) return -1;
        const std::vector<cr2::Gimmick>& gs = field.gimmicks();
        for (size_t i = 0; i < gs.size(); ++i) {
            const cr2::Gimmick& g = gs[i];
            if (g.kind != cr2::Gimmick::Ability || g.name.find("sprint") == std::string::npos || !script->visible(g)) continue;
            const cr2::FieldScript::TouchAction* a = script->touchAction(g.name);
            if (!a || !a->enabled || a->spots.empty()) continue;
            if (std::hypot(g.position[0] - playerCur[0], g.position[1] - playerCur[1], g.position[2] - playerCur[2]) <= 0.15f) return int(i);
        }
        return -1;
    }
    bool sprintTap(float fx, float fy) { // (a click: on the marker)
        const int i = sprintAt();
        if (i < 0) return false;
        float mx, my;
        if (!uiPlace(field.gimmicks()[size_t(i)].position, mx, my)) return false;
        const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
        const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
        if (std::hypot(x - mx, y - my) * 2048.0f > 128.0f) return false;
        startSprint(i);
        return true;
    }
    void startSprint(int i) {
        const cr2::Gimmick& g = field.gimmicks()[size_t(i)];
        sprint = Sprint();
        sprint.to = script->touchAction(g.name)->spots;
        sprint.stage = 1;
        heading = std::atan2(g.position[0] - player[0], g.position[2] - player[2]); // (FldPlayer::SetDirection to it)
        playMotion(fig, "fj01"), time = 0;
        SDL_Log("%s: sprint %s - %zu spots", field.info().name.c_str(), g.name.c_str(), sprint.to.size());
    }
    void sprintHop() {
        Sprint& s = sprint;
        const std::array<float, 3>& to = s.to[s.k];
        std::memcpy(s.from, player, sizeof s.from);
        const float h = to[1] >= s.from[1] ? to[1] - s.from[1] + 0.04f : 0.04f, dy = to[1] - s.from[1];
        s.v = std::sqrt(1.96f * h);
        const float r = std::sqrt(std::max(s.v * s.v - 1.96f * dy, 0.0f));
        s.T = std::max((r - s.v) / -0.98f, (-s.v - r) / -0.98f);
        if (s.T <= 0) s.T = 0.1f;
        s.vx = (to[0] - s.from[0]) / s.T, s.vz = (to[2] - s.from[2]) / s.T;
        heading = std::atan2(to[0] - s.from[0], to[2] - s.from[2]);
        s.t = 0, s.air = 0, s.stage = 2;
        playMotion(fig, "fj02"), time = 0;
        audio.playSe(0x155, false);
        if (s.jump) audio.stopAllVoices(0), audio.playVoice(0x8fa);
    }
    void updateSprint(float dt) {
        Sprint& s = sprint;
        if (!s.stage) return;
        s.t += dt;
        if (s.stage == 1) {
            if (s.t >= 0.15f) sprintHop(); // (the crouch's blend)
            return;
        }
        const float t = std::min(s.t, s.T);
        player[0] = s.from[0] + s.vx * t, player[1] = s.from[1] + s.v * t - 0.49f * t * t, player[2] = s.from[2] + s.vz * t;
        if (s.air == 0 && s.t >= 0.15f) { // (fj03, ending as it lands)
            s.air = 1;
            playMotion(fig, "fj03");
            const float d = fig.animLoaded ? fig.anim.duration() : 0.0f;
            time = std::max(d - (s.T - s.t - 0.2f), 0.0f);
        } else if (s.air == 1 && s.t >= s.T - 0.2f) {
            s.air = 2;
            playMotion(fig, "fj01"), time = 0;
        }
        if (s.t < s.T) {
            std::memcpy(playerPrev, player, sizeof player), std::memcpy(playerCur, player, sizeof player);
            return;
        }
        const std::array<float, 3>& to = s.to[s.k];
        player[0] = to[0], player[1] = to[1], player[2] = to[2];
        std::memcpy(playerPrev, player, sizeof player), std::memcpy(playerCur, player, sizeof player);
        audio.playSe(0x156, false);
        if (++s.k < s.to.size()) {
            sprintHop();
            return;
        }
        s.stage = 0; // (on the last: idle, the controller there)
        playMotion(fig, "fn01");
        controller->setPosition({player[0], player[1] + 0.02f, player[2]});
        for (int k = 0; k < 4; ++k) controller->update({0, 0, 0});
        const cr2::Vec3& p = controller->position();
        player[0] = p[0], player[1] = p[1] - 0.02f, player[2] = p[2];
        std::memcpy(playerPrev, player, sizeof player), std::memcpy(playerCur, player, sizeof player);
        checkBalloons();
    }

    void drawKeyMark(int w, int h) {
        if (script && !script->boxVisible()) return; // (SetBoxVisible: GimmickAllViewOff)
        if (fieldMode && !eventMode && actionMark.gimmick >= 0) {
            const Mat4 proj = uiProjection(w, h);
            Mat4 camera = cr2::mat4Identity();
            camera[14] = -0.881f;
            cr2::SceneLighting l;
            dev->setLighting(l);
            drawActionMark(cr2::mat4Multiply(proj, camera));
        }
        if (!fieldMode || eventMode || keyLock.gimmick < 0) return;
        const cr2::Gimmick& g = field.gimmicks()[size_t(keyLock.gimmick)];
        if (keyLock.stage == 0 && game.boxes.isOpen(g.boxKind, g.boxNumber)) return;
        float x, y;
        if (!keyMarkPlace(x, y)) return;
        const Mat4 proj = uiProjection(w, h);
        Mat4 camera = cr2::mat4Identity();
        camera[14] = -0.881f;
        const Mat4 view = cr2::mat4Multiply(proj, camera);
        cr2::SceneLighting l;
        dev->setLighting(l);
        auto at = [&](UiFig& u, float scale) {
            u.fig.place = cr2::Mat34();
            u.fig.place.m[0] = u.fig.place.m[5] = u.fig.place.m[10] = scale;
            u.fig.place.m[3] = x, u.fig.place.m[7] = y, u.fig.place.m[11] = -0.2f; // (SetParamNumber 4: z -0.2)
            drawFigure(u.fig, view.data());
        };
        auto duration = [&](UiFig& u, const char* anim) {
            poseUi(u, anim, 0);
            return u.fig.animLoaded ? u.fig.anim.duration() : 0.0f;
        };
        const float t = uiClock - keyLock.markAt, ts = uiClock - keyLock.stageAt;
        if (!keyLock.orlando) {
            UiFig& m = uiFig("gmk0rng_io");
            poseUi(m, "gmk0rng_io", t);
            at(m, 1);
            return;
        }
        UiFig& ring = uiFig("gmk2ring");
        const float io = duration(ring, "gmk2ring_io");
        if (keyLock.stage == 2) poseUi(ring, "gmk2ring_out", ts);
        else if (t < io) poseUi(ring, "gmk2ring_io", t);
        else poseUi(ring, "gmk2ring_wait", t - io, true);
        at(ring, keyLock.stage == 1 ? 1 + keyLock.spread / kUnlockSpread : 1);
        if (keyLock.stage == 0) {
            UiFig& b = uiFig("gmkbtn");
            poseUi(b, "gmkbtn_io", t);
            at(b, 1);
        } else if (keyLock.stage == 1) {
            UiFig& b = uiFig("gmkbtn");
            const float out = duration(b, "gmkbtn_out");
            if (ts < out) {
                poseUi(b, "gmkbtn_out", ts);
                at(b, 1);
            } else { // (then the guide, looping: gmk2csr_loop)
                UiFig& c = uiFig("gmk2csr_loop");
                poseUi(c, "gmk2csr_loop", ts - out, true);
                at(c, 1 + keyLock.spread / kUnlockSpread);
            }
        }
    }

    // The field's encounters (FldEncount; the map's area, steps and chance from the release's
    // binary: cr2::findEncountTable). Set as each area starts (FldMain::Init / ChangeAreaExecute:
    // Reset, SetData, SetPosition); each update the leader's walk across the ground adds up, each
    // 0.17 of it a step, and every area's steps a draw (Pickout): a roll of 100 at most the
    // chance a hit - else 5 more to the chance. A hit picks one of the area's ten couplings by
    // weight (MakeCouplingID); the field then (step 0x1c) SE 0xc9, black over 20 updates with the
    // music stopped (unless flag 0x7f7), the battle - on the area's map and music, the pair now
    // fighting (SetBattle(-1)). Drawn only with the encounters on (Cr3UtilSetEncountEnable) and,
    // once the field menu has their switch (flags 0x40e and 0x40f), it on; none while an event
    // runs (the walk still followed)
    cr2::EncountTable encountTable;
    struct Encount {
        bool on = false; // (SetData: the map has an area)
        int area = -1, steps = 2, chance = 0, count = 0;
        float x = 0, z = 0, walked = 0;
        int coupling = 0;  // (a hit's)
        float fadeAt = -1; // (the hit's fade begun: uiClock)
    } encount;
    std::mt19937 encountRandom{std::random_device{}()};
    void encountSet(const float* p) {
        encount = Encount();
        if (const cr2::EncountCell* e = encountTable.at(field.info().name)) encount.on = true, encount.area = e->area, encount.steps = e->steps, encount.chance = e->chance;
        encount.x = p[0], encount.z = p[2];
    }
    void encountUpdate(const cr2::Vec3& p, bool paused) {
        if (!encount.on || encount.coupling || !game.encountEnable) return;
        if (game.flag(0x40e) && game.flag(0x40f) && encountersOff) return; // (the field menu's switch off)
        const float dx = p[0] - encount.x, dz = p[2] - encount.z;
        encount.x = p[0], encount.z = p[2];
        if (paused) return;
        encount.walked += std::sqrt(dx * dx + dz * dz);
        if (encount.walked < 0.17f) return;
        encount.walked = 0;
        if (++encount.count < encount.steps) return;
        encount.count = 0;
        const int roll = int(encountRandom() % 100u);
        if (roll > encount.chance) {
            encount.chance += 5;
            return;
        }
        if (battleData.couplings() == 0) { // (the battle data, before the first battle)
            std::vector<uint8_t> d;
            if (!archive.read("data\\battledata.dat", d) || !battleData.parse(d)) return;
        }
        const cr2::BattleData::Area* a = battleData.area(encount.area);
        int sum = 0;
        for (int k = 0; a && k < 10; ++k) sum += a->rates[k];
        if (!a || sum <= 0) return;
        const int r = int(encountRandom() % unsigned(sum));
        int at = 0, pick = -1;
        for (int k = 0; k < 10 && pick < 0; ++k)
            if (r <= (at += a->rates[k])) pick = k;
        if (pick < 0 || !battleData.coupling(a->couplings[pick])) return;
        encount.coupling = a->couplings[pick];
        SDL_Log("%s: an encounter (area %d, roll %d of %d%%): coupling %d", field.info().name.c_str(), encount.area, roll, encount.chance, encount.coupling);
    }
    // the hit: the field held, SE 0xc9 and the fade, then the battle (FldMain steps 0x1c .. 0x20)
    void encountStep() {
        if (encount.fadeAt < 0) {
            encount.fadeAt = uiClock;
            audio.playSe(0xc9, false);
            if (!game.flag(0x7f7)) audio.stopBgm(20);
            return;
        }
        if (uiClock - encount.fadeAt < kWipeSeconds + 0.15f) return; // (the wipe's end - WaitFadeFinish -, then the field's three)
        int party[2];
        for (int k = 0; k < 2; ++k) party[k] = game.party[k] >= 0 ? game.party[k] + 1 : 0;
        const int id = encount.coupling, area = encount.area;
        encount.coupling = 0, encount.fadeAt = -1;
        returnMap = field.info().name, std::memcpy(returnPlayer, player, sizeof player), returnHeading = heading; // (SaveWorkBeforeBattle)
        SDL_Log("play: battle %d - an encounter (characters %d, %d)", id, party[0], party[1]);
        if (ex.on) exploreLog("  an encounter: coupling " + std::to_string(id) + (ex.grind > 0 ? " (" + std::to_string(ex.grind - 1) + " more to fight first)" : "")),
                    ex.grind = std::max(ex.grind - 1, 0);
        if (startBattle(id, party, area)) {
            startFight(2, 1u + uint32_t(battlesSkipped));
            fightingInFlow = true, randomBattle = true;
        }
    }

    // FldPlayer::Update's steps: walking one each 12th update (0.6 s), running each 7th, counted from
    // the move's start (FldPlayer +0x28). SetSEAndEffect: the floor's surface under DummyEP04 (a ray
    // from 1 above to 1 below: FldUtilGetFldMapMaterialName), "seNN", plays one of its two SEs at
    // random (the table at 0x53ad0c) and starts its ground effect there (FldGroundEffect: eff001,
    // 002, 004 .. 007, five of each at once, its animator once from its start, unturned)
    int stepState = 0, stepCount = 0;
    float groundClock = 0; // (the field's time: walk()'s)
    struct GroundFx {
        bool on = false;
        float at = 0, p[3] = {0, 0, 0};
    };
    GroundFx groundFx[6][5];
    std::map<int, ViewFigure> groundFigs; // (kind x 5 + slot)
    void stepUpdate(int moving) { // (0 standing, 1 walking, 2 running)
        if (moving != stepState) return void((stepState = moving, stepCount = 0));
        if (moving == 0 || ++stepCount <= (moving == 1 ? 11 : 6)) return;
        stepCount = 0;
        static const int kSteps[11][3] = {{358, 359, 1}, {361, 362, 2}, {364, 365, -1}, {367, 368, 3}, {370, 371, 4}, {373, 374, 1},
                                          {376, 377, -1}, {379, 380, 0}, {382, 383, -1}, {384, 385, 4}, {358, 359, 5}};
        float p[3] = {player[0], player[1], player[2]};
        cr2::Mat34 ep;
        if (jointPlace(fig, "DummyEP04", ep)) // (its offset from the figure's place, where the leader is now)
            p[0] += ep.m[3] - fig.place.m[3], p[1] += ep.m[7] - fig.place.m[7], p[2] += ep.m[11] - fig.place.m[11];
        const std::string surface = field.collision().surfaceBelow({p[0], p[1] + 1, p[2]}, 2);
        if (surface.size() < 4 || surface.find("se") == std::string::npos) return;
        const int n = std::atoi(surface.substr(2, 2).c_str());
        if (n < 0 || n > 10) return;
        audio.playSe(kSteps[n][boxRandom() % 2], false);
        if (kSteps[n][2] < 0) return;
        for (GroundFx& g : groundFx[kSteps[n][2]])
            if (!g.on) {
                g.on = true, g.at = groundClock, std::memcpy(g.p, p, sizeof p);
                break;
            }
    }
    void drawGroundFx(const float* mvp) {
        static const char* const kFigures[6] = {"eff001", "eff002", "eff004", "eff005", "eff006", "eff007"};
        for (int k = 0; k < 6; ++k)
            for (int i = 0; i < 5; ++i) {
                GroundFx& g = groundFx[k][i];
                if (!g.on) continue;
                ViewFigure& f = groundFigs[k * 5 + i];
                std::string err;
                if (f.meshes.empty() && f.name.empty() && !loadFigure(std::string(kFigures[k]) + ".geom", f, &err)) {
                    SDL_Log("ground effect %s: %s", kFigures[k], err.c_str());
                    f.name = "-";
                }
                const float t = groundClock - g.at;
                if (f.meshes.empty() || !f.animLoaded || t > f.anim.duration()) {
                    g.on = false; // (played out)
                    continue;
                }
                f.place = cr2::Mat34();
                f.place.m[3] = g.p[0], f.place.m[7] = g.p[1], f.place.m[11] = g.p[2];
                f.absolute = true; // (its animator alone in slot 0)
                pose(f, t, false);
                drawFigure(f, mvp);
            }
    }

    void walk(float dt, float sx, float sy, bool run) {
        if (!controller) return;
        groundClock += dt;
        cr2::Vec3 right, forward;
        field.groundAxes(right, forward);
        cr2::Vec3 dir{right[0] * sx + forward[0] * sy, 0, right[2] * sx + forward[2] * sy};
        const float l = std::sqrt(dir[0] * dir[0] + dir[2] * dir[2]);
        const float speed = l > 0 ? (run ? 0.48f : 0.09f) : 0.0f;
        if (l > 0) dir = {dir[0] / l * speed, 0, dir[2] / l * speed}, heading = std::atan2(dir[0], dir[2]);
        constexpr float kSub = cr2::CharacterController::kUpdate / cr2::CharacterController::kSubsteps; // (1/60 s)
        stepClock = std::min(stepClock + dt, 15 * kSub);
        std::string exitTo;
        for (; stepClock >= kSub && exitTo.empty(); stepClock -= kSub) {
            std::memcpy(playerPrev, playerCur, sizeof playerCur);
            controller->move(dir);
            controller->substep();
            const cr2::Vec3& p = controller->position();
            playerCur[0] = p[0], playerCur[1] = p[1] - 0.02f, playerCur[2] = p[2];
            if (++substep < cr2::CharacterController::kSubsteps) continue;
            substep = 0;
            stepUpdate(speed == 0 ? 0 : run ? 2 : 1);
            // an exit: the area changes to the map its pop point names (FldMain::ChangeAreaExecute)
            const std::string exit = field.exitTouched(p, controller->radius);
            if (exit.empty()) exitsArmed = true;
            else if (exitsArmed && exit.size() >= 5) exitTo = exit;
            touchEvents(p);
            checkBalloons();
            encountUpdate(p, fieldScriptRunning() || actionRunning() || changeStage != 0 || keyLock.stage != 0);
        }
        if (script && script->playerState().motionSerial != leaderMotionSerial) // (Cr3FieldMapCharacter.SetMotion)
            leaderMotionSerial = script->playerState().motionSerial, leaderMotion = script->playerState().motion,
            SDL_Log("leader: motion %s", leaderMotion.c_str());
        if (speed != 0) leaderMotion.clear(); // (moving again: its own motions)
        playMotion(fig, !leaderMotion.empty() ? leaderMotion.c_str() : speed == 0 ? "fn01" : run ? "ff02" : "ff01");
        if (!exitTo.empty()) {
            SDL_Log("exit %s", exitTo.c_str());
            // FldMain::CollisionEventExec: SE 0x152 leaving or entering a sky map (the table's +0x15 -
            // FldMap +0x79, IsCommonMap of where it goes)
            const auto to = std::find_if(table.begin(), table.end(), [&](const cr2::MapInfo& m) { return m.name == exitTo.substr(0, 5); });
            if (field.info().sharedBackground || (to != table.end() && to->sharedBackground)) audio.playSe(0x152, false);
            enterMap(exitTo.substr(0, 5), exitTo);
            return;
        }
        const float a = std::clamp(stepClock / kSub, 0.0f, 1.0f);
        for (int c = 0; c < 3; ++c) player[c] = playerPrev[c] + (playerCur[c] - playerPrev[c]) * a;
    }

    bool decode(const std::string& name, cr2::Image& img) {
        if (auto it = modelImages.find(name); it != modelImages.end()) return img = it->second, true; // (an HD model's own)
        std::vector<uint8_t> data;
        std::string err;
        if (archive.read("images\\" + name + ".img", data, &err) && cr2::decodeImg(data, img, &err)) return true;
        SDL_Log("texture %s: %s", name.c_str(), err.c_str());
        return false;
    }

    // A material's texture (`mipmaps` off for lookup tables: CLUTs)
    cr2::Texture* texture(const std::string& name, bool mipmaps = true) {
        if (name.empty()) return nullptr;
        if (auto it = textures.find(name); it != textures.end()) return it->second.id ? &it->second : nullptr;
        cr2::Image img;
        cr2::Texture& t = textures[name];
        if (!decode(name, img)) return nullptr;
        if (hdTextures && !hdDir.empty()) { // (drawn at the original's size, sharper)
            cr2::Image hd;
            if (cr2::loadHdImage(hdDir, img, hd)) {
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
    bool loadLights(const std::string& figure) {
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

    void unload(ViewFigure& f) {
        for (ViewMesh& m : f.meshes) dev->destroy(m.gpu);
        f.meshes.clear();
    }

    // HD Models: models/hd/<figure>.glb (or .gltf) in place of the figure's meshes - its skeleton,
    // animations and materials kept (assets/gltf_figure.hpp)
    void replaceFigure(const std::string& name, cr2::Geom& g) {
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
                for (auto& [k, img] : pictures) {
                    modelImages[k] = std::move(img);
                    textures.erase(k); // (a new picture of the same name: decoded again)
                }
                ++hdModelsLoaded;
                SDL_Log("%s: replaced by %s (%d meshes, %d triangles%s)", name.c_str(), path.c_str(), st.meshes, st.triangles,
                        pictures.empty() ? "" : ", pictures of its own");
            } else {
                SDL_Log("%s: %s left out: %s", name.c_str(), path.c_str(), why.c_str());
            }
            return;
        }
    }

    bool loadFigure(const std::string& name, ViewFigure& f, std::string* err) {
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
    bool load(size_t index) {
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

    void loadAnim(ViewFigure& f, size_t index) {
        f.animIndex = index;
        std::vector<uint8_t> data;
        std::string err;
        f.animLoaded = archive.read(f.anims[index], data, &err) && f.anim.parse(data, &err) &&
                       f.anim.joints() == int(f.skeleton.basePose.size());
        if (!f.animLoaded) SDL_Log("%s: %s", f.anims[index].c_str(), err.empty() ? "not this skeleton's" : err.c_str());
        time = 0;
    }

    // A field motion of the character (FldPlayer::CreatePlayer: c<id> + "fn01" idle, "ff01" walk,
    // "ff02" run, "fj01".. jumps, "fp01".. its own), if it has it and isn't playing it already
    // A character's shadow (FldShadow: obj001 on its own animation, given the character's place -
    // FldPlayer::Pose - and drawn before it in the transparent pass): the leader's, and an event's
    // c / v models' (EvtModel::Init) unless EvtScene.ShadowVisible hid it
    ViewFigure shadowFig;
    bool shadowTried = false;
    void drawShadow(const cr2::Mat34& place, const float* mvp) {
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
    std::string leaderMotion; // (a script's: held while the leader stands)
    int leaderMotionSerial = 0;
    void playMotion(ViewFigure& f, const char* motion) {
        const std::string want = f.name.substr(0, f.name.size() - 5) + motion + ".anim";
        const auto it = std::find(f.anims.begin(), f.anims.end(), want);
        if (it != f.anims.end() && (!f.animLoaded || f.anims[f.animIndex] != want)) loadAnim(f, size_t(it - f.anims.begin()));
    }

    std::string animStatus() const {
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
    void pose(ViewFigure& f, float t, bool loop = true) {
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

    // The camera the figures are posed for (its place in the world): billboards turn to it
    cr2::Mat34 poseView;

    // the model viewer's camera: where it is (orbiting the figure by yaw / pitch / zoom), how far
    float modelEye(float* eye) const {
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
    void billboard(ViewMesh& vm, const cr2::Mat34& p0) {
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

    void pose() {
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
                        const float a = std::clamp((float(script->ticks() - st->second.faceTick) + fieldClock / cr2::EventRuntime::kTick) / float(st->second.faceUpdates), 0.0f, 1.0f);
                        gimmickFace[k].yaw = turnBetween(gimmickFace[k].from, gimmickFace[k].to, a);
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
                    leaderFace.to = gimmickTarget(ps.faceTarget, tx, tz) ? std::atan2(tx - player[0], tz - player[2]) : heading;
                }
                const float a = (float(script->ticks() - ps.faceTick) + fieldClock / cr2::EventRuntime::kTick) / float(ps.faceUpdates);
                if (!leaderFace.done) heading = turnBetween(leaderFace.from, leaderFace.to, std::clamp(a, 0.0f, 1.0f)), leaderFace.done = a >= 1;
            }
            if (script && script->events().scene().loaded()) poseEventModels(script->events());
        }
        pose(fig, time);
    }

    // A figure's meshes with their materials' render states; opaque meshes first, then those blending
    void drawFigure(const ViewFigure& f, const float* mvp) {
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
    void drawLayer(const std::string& image, const Mat4& proj, float shiftX = 0) {
        cr2::Texture* t = texture(image);
        if (!t) return;
        float v[24];
        cr2::FieldMap::layerQuad(v, shiftX);
        static const float kWhite[4] = {1, 1, 1, 1};
        dev->drawQuad(*t, v, proj.data(), kWhite);
    }

    // The output: a chosen resolution (0 x 0: the window's), and the temporal upscaler (Vulkan)
    int resW = 0, resH = 0;
    cr2::UpscaleSettings upscale;
    int screenX = 0, screenY = 0, screenW = 1, screenH = 1; // the game's screen in the frame

    // A frame of fw x fh: the game's screen all of it (an offscreen device's frames are the
    // chosen resolution, letterboxed into the window when shown), or - drawing straight into the
    // window (GL) - the resolution's shape letterboxed in it. The scene (smaller while an
    // upscaler is on), then the UI over the resolved frame at full size.
    void draw(int fw, int fh) {
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
        drawScene(screenW, screenH);
        dev->endScene();
        dev->setViewport(float(screenX), float(screenY), float(screenW), float(screenH));
        drawEventUi(); // (the vista's, or the field's events')
    }
    // The game's screen in a frame of fw x fh (screenX ...), the scene's viewport on it
    void placeScreen(int fw, int fh) {
        screenX = screenY = 0, screenW = fw, screenH = fh;
        if (!dev->offscreen() && resW > 0 && resH > 0) {
            if (int64_t(resW) * fh > int64_t(fw) * resH) screenH = std::max(int(int64_t(fw) * resH / resW), 1);
            else screenW = std::max(int(int64_t(fh) * resW / resH), 1);
            screenX = (fw - screenW) / 2, screenY = (fh - screenH) / 2;
        }
        int rw = fw, rh = fh;
        dev->sceneSize(fw, fh, rw, rh);
        const float kx = float(rw) / float(fw), ky = float(rh) / float(fh);
        dev->setViewport(screenX * kx, screenY * ky, screenW * kx, screenH * ky);
    }

    void drawScene(int w, int h) {
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
                if (c.frames > 0 && c.t < c.frames) {
                    const float k = std::clamp(fieldClock / cr2::EventRuntime::kTick, 0.0f, 1.0f) / float(c.frames);
                    cx += (c.toX - c.fromX) * k, cy += (c.toY - c.fromY) * k;
                }
                fr = field.frameScrolled(w, h, player, cx, cy);
            } else {
                fr = field.frame(w, h, player);
            }
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

    // The maps' table from the game's executable (--binary, or beside the iPad's / Vita's archive)
    std::vector<cr2::MapInfo> table;
    std::string tableFrom;
    bool exitsArmed = false; // (an exit counts once the character has left them all since arriving)

    std::string dataArg, binaryArg; // (the release's: the staff roll's table from its executable too)
    bool loadTable(const std::string& binary, const std::string& dataPath) {
        dataArg = dataPath, binaryArg = binary;
        if (cr2::releaseMapTable(dataPath, binary, table, &tableFrom)) {
            if (!cr2::releaseEncountTable(dataPath, binary, encountTable)) SDL_Log("the encounters' table: not found in %s", tableFrom.c_str());
            return true;
        }
        SDL_Log("the maps' table: not found beside %s (the iPad's executable, the Vita's eboot.bin, the Android "
                "release's APK) - give the game's executable with --binary", dataPath.c_str());
        return false;
    }

    // the gimmicks shown with a figure: placed by their joints, NPCs idling (FldNPC: <figure>fn01) -
    // as the script has them now
    // the resident effects the script played (FldEffectResident: its figure on the leader - its
    // place and facing - or a gimmick's, scaled, its animator once from its start)
    std::map<int, ViewFigure> residentFigs;
    std::map<int, int> residentSerial;
    void drawResidents(const float* mvp) {
        if (!script) return;
        for (const auto& [n, r] : script->residents()) {
            if (r.tick < 0) continue;
            const char* name = cr2::FieldScript::residentFigure(n);
            ViewFigure& f = residentFigs[n];
            std::string err;
            if (f.meshes.empty() && f.name.empty() && !loadFigure(std::string(name) + ".geom", f, &err)) {
                SDL_Log("resident %s: %s", name, err.c_str());
                f.name = "-";
                continue;
            }
            if (f.meshes.empty()) continue;
            const float t = float(script->ticks() - r.tick) * cr2::EventRuntime::kTick + fieldClock;
            if (f.animLoaded && t > f.anim.duration()) continue; // (played out)
            float p[3] = {player[0], player[1], player[2]}, yaw = heading;
            for (const cr2::Gimmick& g : field.gimmicks())
                if (r.target != "player" && (g.id == r.target || g.name == r.target)) {
                    std::memcpy(p, g.position, sizeof p);
                    yaw = 2 * std::atan2(g.rotation[1], g.rotation[3]); // (its turn about y)
                    break;
                }
            const float c = std::cos(yaw), sn = std::sin(yaw);
            f.place = cr2::Mat34();
            f.place.m[0] = c * r.scale[0], f.place.m[2] = sn * r.scale[2], f.place.m[5] = r.scale[1];
            f.place.m[8] = -sn * r.scale[0], f.place.m[10] = c * r.scale[2];
            f.place.m[3] = p[0], f.place.m[7] = p[1], f.place.m[11] = p[2];
            f.absolute = true; // (FldEffectResident::CreateModel: BindAnimator(figure, 0, ...) alone)
            pose(f, t, false);
            drawFigure(f, mvp);
        }
    }
    std::vector<std::string> shownGimmicks;
    std::vector<std::string> gimmickIds; // (the figures' gimmicks: the script's names for them)
    std::vector<int> gimmickSerial;      // (the event motion each has loaded: its SetMotion's count)
    struct Facing {
        int serial = 0;
        float yaw = 0, from = 0, to = 0;
        bool done = true; // (the leader's: its turn's end applied - the player's to steer then)
    };
    std::vector<Facing> gimmickFace; // (each one's turn: ChangeDirection)
    std::vector<int> gimmickColor;   // (the colours each wears: ChangeModelColor's, -1 its own)
    std::vector<bool> gimmickShadow; // (an NPC's or a box's: its FldShadow)
    // FldNPC::CheckIdleAnime: an NPC that is one of the party (c001 .. c006) plays its fn02 once
    // each 400 .. 1399 frames (lrand48 % 1000 + 400, 30 a second), then its fn01 again - none once
    // flag 1930 is on as its time comes (its counter run past)
    struct Fidget {
        bool party = false, playing = false;
        float due = 0, start = 0;
    };
    std::vector<Fidget> gimmickFidget;
    float fidgetWait() { return float(encountRandom() % 1000 + 400) / 30.0f; }
    Facing leaderFace;
    // where a turn's target is ("player", else a gimmick's id or name)
    bool gimmickTarget(const std::string& target, float& x, float& z) const {
        if (target == "player") return x = player[0], z = player[2], true;
        for (const cr2::Gimmick& g : field.gimmicks())
            if (g.id == target || g.name == target) return x = g.position[0], z = g.position[2], true;
        return false;
    }
    static float turnBetween(float from, float to, float a) { // (the shorter way round)
        float d = std::remainder(to - from, 6.28318531f);
        return from + d * a;
    }
    void refreshGimmicks() {
        std::vector<std::string> want;
        for (const cr2::Gimmick& gm : field.gimmicks())
            if (!gm.figure.empty() && archive.contains(gm.figure + ".geom") && script->visible(gm)) want.push_back(gm.name);
        if (want == shownGimmicks) return;
        shownGimmicks = want;
        std::string err;
        for (ViewFigure& g : gimmicks) unload(g);
        gimmicks.clear(), gimmickIds.clear(), gimmickSerial.clear(), gimmickFace.clear(), gimmickColor.clear(), gimmickShadow.clear(), gimmickFidget.clear();
        for (const cr2::Gimmick& gm : field.gimmicks()) {
            if (gm.figure.empty() || !archive.contains(gm.figure + ".geom") || !script->visible(gm)) continue;
            ViewFigure g;
            cr2::JointTransform t;
            std::memcpy(t.rotation, gm.rotation.data(), sizeof t.rotation);
            std::memcpy(t.translation, gm.position, sizeof t.translation);
            t.scale[0] = t.scale[1] = t.scale[2] = gm.scale;
            g.place = t.matrix();
            if (!loadFigure(gm.figure + ".geom", g, &err)) continue;
            // (FldObject / FldEffect: their own animator alone in slot 0 - over the base pose, not a motion)
            g.absolute = gm.kind == cr2::Gimmick::Object || gm.kind == cr2::Gimmick::Effect;
            if (gm.kind == cr2::Gimmick::Npc) playMotion(g, "fn01");
            if (gm.kind == cr2::Gimmick::Box) playMotion(g, game.boxes.isOpen(gm.boxKind, gm.boxNumber) ? "fn02" : "fn01"); // (FldBox: closed fn01, open fn02)
            gimmicks.push_back(std::move(g));
            gimmickIds.push_back(gm.id), gimmickSerial.push_back(0), gimmickColor.push_back(-1);
            gimmickShadow.push_back(gm.kind == cr2::Gimmick::Npc || gm.kind == cr2::Gimmick::Box);
            Fidget fd;
            fd.party = gm.kind == cr2::Gimmick::Npc && gm.figure.size() >= 4 && gm.figure.compare(0, 3, "c00") == 0 && gm.figure[3] >= '1' && gm.figure[3] <= '6';
            fd.due = uiClock + fidgetWait();
            gimmickFidget.push_back(fd);
            Facing fc; // (its joint's yaw)
            fc.yaw = std::atan2(g.place.m[2], g.place.m[0]);
            gimmickFace.push_back(fc);
        }
    }

    // A map: its table row, layers and figures, the character dropped at the point `at` (empty:
    // where it stood, the field re-entered after a vista or a battle), the area's `entry` started
    std::string fieldArrival; // (the point the leader last came onto this map at: its pop point, or the map's name)
    bool enterMap(const std::string& map, const std::string& at, const std::string& entry = "evt000") {
        const auto row = std::find_if(table.begin(), table.end(), [&](const cr2::MapInfo& m) { return m.name == map; });
        if (row == table.end()) {
            SDL_Log("%s: not one of the %zu maps (%s)", map.c_str(), table.size(), tableFrom.c_str());
            return false;
        }
        std::string err;
        if (!field.load(archive, *row, &err)) {
            SDL_Log("%s: %s", map.c_str(), err.c_str());
            return false;
        }
        unload(mapFigure);
        mapFigure = ViewFigure();
        if (!field.figureName().empty() && !loadFigure(field.figureName() + ".geom", mapFigure, &err))
            SDL_Log("%s: %s", field.figureName().c_str(), err.c_str());
        mapFigure.absolute = true; // (FldMap::ChangeArea: BindAnimator(figure, 0, ...))
        // a dungeon entered anew: its treasure boxes drawn (FldMain::ChangeDungeonFunc; not with flag 0x7fb)
        const std::string dungeon = map.substr(0, 3);
        if (dungeon != boxesDungeon && !game.flag(0x7fb)) {
            game.boxes.choose(game.difficulty, game.party, [] { return uint32_t(boxRandom()); });
            int placed = 0;
            for (int k = 0; k < 3; ++k)
                for (int n = 0; n < cr2::TreasureBoxes::count(k); ++n) placed += game.boxes.isSet(k, n);
            SDL_Log("%s: the treasure boxes drawn - table %d, %d placed", dungeon.c_str(), game.boxes.table, placed);
        }
        boxesDungeon = dungeon;
        keyLock = KeyLock();
        actionMark = ActionMark(), sprint = Sprint(), iai = Iai(), aim = Aim(), hakkei = Hakkei(), mystic = Mystic();
        // the dungeon's script sets the area up (evt000: which gimmicks show, which collide)
        if (!script) script = std::make_unique<cr2::FieldScript>(archive, game), script->onSave = [this](int k) { scriptSave(k); };
        script->events().autoPage = pageSeconds, script->events().askYesNo = true;
        if (!script->enter(map, field.gimmicks(), entry)) SDL_Log("%s's script: %s", map.c_str(), script->error().c_str());
        if (!playMode && script->running() && !script->runToEnd()) // (--field: the area as its set-up leaves it; --play: as it runs)
            SDL_Log("%s's %s: %s", map.c_str(), entry.c_str(), script->error().empty() ? "still running" : script->error().c_str());
        SDL_Log("%s: %s %s", map.c_str(), entry.c_str(), script->running() ? "running" : "done");
        if (playMode && !script->running()) pendingBranch = true; // (ended at once: what it reserved, as after any event)
        if (!script->flagMarks().empty()) {
            std::string marks;
            for (const std::string& f : script->flagMarks()) marks += " " + f;
            SDL_Log("%s: flag marks%s", map.c_str(), marks.c_str());
        }
        fieldClock = 0;
        field.setSolidGimmicks([this](const cr2::Gimmick& g) {
            const auto it = script->gimmicks().find(g.id);
            return script->visible(g) && (it == script->gimmicks().end() || it->second.collision);
        });
        shownGimmicks.clear();
        refreshGimmicks();
        if (!at.empty() && at.rfind(map, 0) == 0) fieldArrival = at;
        else if (fieldArrival.rfind(map, 0) != 0) fieldArrival = map;
        if (at.empty()) {
            // (where the leader stood)
        } else if (!field.point(at, player)) {
            SDL_Log("%s: no point %s; the points:", map.c_str(), at.c_str());
            for (const std::string& n : field.pointNames()) SDL_Log("  %s", n.c_str());
        }
        fieldMode = true;
        exitsArmed = false;
        dev->resetHistory();
        controller = std::make_unique<cr2::CharacterController>(field.collision());
        controller->setPosition({player[0], player[1] + 0.17f, player[2]}); // (dropped onto the floor)
        for (int k = 0; k < 10; ++k) controller->update({0, 0, 0});
        const cr2::Vec3& p = controller->position();
        player[0] = p[0], player[1] = p[1] - 0.02f, player[2] = p[2];
        std::memcpy(playerPrev, player, sizeof player);
        std::memcpy(playerCur, player, sizeof player);
        stepClock = 0, substep = 0;
        insideEvents.clear();
        { // (where the leader stands as the area starts: those boxes don't fire till it leaves them)
            for (const cr2::Gimmick& g : field.gimmicks())
                if (g.kind == cr2::Gimmick::Event && !g.check && g.touches(player, 0.02f)) insideEvents.insert(g.name);
        }
        checkBalloons();
        encountSet(player);
        std::string exits;
        for (const std::string& e : field.exitNames()) exits += " " + e;
        SDL_Log("%zu gimmicks, %zu with figures; layers bg [%s] fg [%s]", field.gimmicks().size(), gimmicks.size(), field.backgroundImage().c_str(), field.foregroundImage().c_str());
        SDL_Log("field %s (table from %s): variant %d, layers %s%s%s%s, %zu collision triangles, exits%s, at %s (%.3f %.3f %.3f)",
                map.c_str(), tableFrom.c_str(), row->variant, field.backgroundImage().empty() ? "" : "bg ",
                field.midgroundImage().empty() ? "" : "mg ", field.foregroundImage().empty() ? "" : "fg ",
                field.figureName().empty() ? "" : "+ 3D figure", field.collision().triangles(), exits.empty() ? " none" : exits.c_str(),
                at.c_str(), player[0], player[1], player[2]);
        return true;
    }

    bool loadField(const std::string& map, const std::string& at, const std::string& binary, const std::string& dataPath,
                   const std::string& entry = "evt000") {
        return loadTable(binary, dataPath) && enterMap(map, at, entry);
    }

    // The field's script, a coroutine stepped at the game's 20 updates a second (its events'
    // scene, windows and fades with it); the leader held while it runs, as FldMain holds it
    float fieldClock = 0, pageSeconds = 3.0f;
    bool fieldScriptRunning() const { return script && script->running(); }
    // --input's tokens the field takes (fieldToken: the tab, the camp), one a 0.4 s while it is idle
    void fieldInput() {
        if (!inputScript.empty() && mystic.stage && uiClock - actionMark.startedAt > 1.0f && uiClock >= inputNext &&
            inputScript.front().rfind("tapspot", 0) == 0) { // (a mystic's: the first other spot, the n-th (tapspotN), or none)
            const cr2::FieldScript::TouchAction* a = script->touchAction(field.gimmicks()[size_t(actionMark.gimmick)].name);
            int other = a && a->spots.size() > 1 ? (a->markSpot() == 0 ? 1 : 0) : -1;
            const std::string& t = inputScript.front();
            if (t.size() > 7 && std::isdigit(static_cast<unsigned char>(t[7])) && a && size_t(std::atoi(t.c_str() + 7)) < a->spots.size())
                other = std::atoi(t.c_str() + 7);
            mysticTap(t == "tapspotfail" ? -1 : other);
            inputScript.pop_front();
            inputNext = uiClock + 0.4f;
            return;
        }
        if (!inputScript.empty() && hakkei.stage && uiClock - actionMark.startedAt > 1.0f && uiClock >= inputNext &&
            (inputScript.front() == "punch" || inputScript.front() == "punchfail")) { // (a hakkei's tap: on the mark, or away)
            hakkeiTap(inputScript.front() == "punch");
            inputScript.pop_front();
            inputNext = uiClock + 0.1f;
            return;
        }
        if (!inputScript.empty() && uiClock >= inputNext && keyLock.stage == 1 && uiClock - keyLock.stageAt > 1.0f &&
            (inputScript.front() == "unlock" || inputScript.front() == "unlockfail")) { // (the lock: spread, or let go)
            unlockEnd(inputScript.front() == "unlock");
            inputScript.pop_front();
            inputNext = uiClock + 0.4f;
            return;
        }
        if (!inputScript.empty() && uiClock >= inputNext && (iai.stage || aim.stage) && uiClock - actionMark.startedAt > 1.0f &&
            (inputScript.front() == "slash" || inputScript.front() == "slashfail" || inputScript.front() == "aim" ||
             inputScript.front() == "aimfail" || inputScript.front() == "drag")) { // (the iai: a slash past the mark, or not; the aim: drawn back to the leader, or not)
            if (inputScript.front() == "drag") { // (an aim's press on the mark, dragged halfway: tests)
                float mx, my, lx, ly;
                if (aim.stage && actionMarkPlace(mx, my) && leaderPlace(lx, ly)) aim.stage = 2, aim.x = (mx + lx) / 2, aim.y = (my + ly) / 2;
                inputScript.pop_front();
                inputNext = uiClock + 0.4f;
                return;
            }
            iaiEnd(inputScript.front() == "slash" || inputScript.front() == "aim");
            inputScript.pop_front();
            inputNext = uiClock + 0.4f;
            return;
        }
        if (!inputScript.empty() && uiClock >= inputNext && party.stage && uiClock - party.at > 0.5f) { // (the party: organize, talk, first, second, left, right, pick, back)
            const std::string t = inputScript.front();
            static const std::map<std::string, int> kIds = {{"organize", 25}, {"talk", 13}, {"first", 1}, {"second", 2}, {"back", 100}};
            if (auto i = kIds.find(t); i != kIds.end()) partyTap(i->second);
            else if (t == "left" || t == "right") partyKey(t == "left" ? SDLK_LEFT : SDLK_RIGHT);
            else if (t == "pick") partyKey(SDLK_RETURN);
            inputScript.pop_front();
            inputNext = uiClock + 0.4f;
            return;
        }
        if (!inputScript.empty() && uiClock >= inputNext && shop.stage && uiClock - shop.at > 0.5f) { // (the shop: its buttons by name, the list's, how many, yes / no)
            shopToken(inputScript.front());
            inputScript.pop_front();
            inputNext = uiClock + 0.3f;
            return;
        }
        if (!inputScript.empty() && uiClock >= inputNext && pub.stage && uiClock - pub.at > 0.5f) { // (the pub: next, pick, accept, back, yes, no)
            const std::string t = inputScript.front();
            if (t == "next") pubKey(SDLK_RIGHT);
            else if (t == "list") pubTap(35);
            else if (t == "pick" || t == "accept") pubKey(SDLK_RETURN);
            else if (t == "back") pubKey(SDLK_BACKSPACE);
            else if (t == "yes" || t == "no") pubKey(t == "yes" ? SDLK_Y : SDLK_N);
            inputScript.pop_front();
            inputNext = uiClock + 0.4f;
            return;
        }
        if (!inputScript.empty() && uiClock >= inputNext && library.stage == 1 && uiClock - library.at > 0.5f) { // (the library: up, down, pick, back)
            const std::string t = inputScript.front();
            libraryKey(t == "up" ? SDLK_UP : t == "down" ? SDLK_DOWN : t == "pick" ? SDLK_RETURN : t == "back" ? SDLK_BACKSPACE : SDLK_UNKNOWN);
            inputScript.pop_front();
            inputNext = uiClock + 0.4f;
            return;
        }
        if (!inputScript.empty() && uiClock >= inputNext && world.stage && uiClock - world.at > 0.5f) { // (the world select: up, down, pick, back, yes, no)
            const std::string t = inputScript.front();
            if (t == "up" || t == "down") worldKey(t == "up" ? SDLK_UP : SDLK_DOWN);
            else if (t == "pick") worldKey(SDLK_RETURN);
            else if (t == "back") worldKey(SDLK_BACKSPACE);
            else if (t == "yes" || t == "no") worldKey(t == "yes" ? SDLK_Y : SDLK_N);
            inputScript.pop_front();
            inputNext = uiClock + 0.4f;
            return;
        }
        if (inputScript.empty() || uiClock < inputNext || (!campOpen && !fieldTabShown() && !mapOpen && !saveStage) || manualStage == 2) return;
        if (!fieldToken(inputScript.front())) return; // (not the field's: left for a battle)
        inputScript.pop_front();
        inputNext = uiClock + 0.4f;
    }
    void fieldUpdate(float dt) {
        if (!script) return;
        fieldClock = std::min(fieldClock + dt, 0.25f);
        for (; fieldClock >= cr2::EventRuntime::kTick; fieldClock -= cr2::EventRuntime::kTick) {
            if (pendingBranch) {
                pendingBranch = false;
                branchAfterField();
                if (!fieldMode) return;
                continue;
            }
            // (the windows, the fade, the scene go on updating once the script has ended: GameMain's
            // update runs them whether or not a script waits)
            const bool wasRunning = script->running();
            if (!script->tick()) SDL_Log("%s's script: %s", field.info().name.c_str(), script->error().c_str());
            updateKeyLock();
            updateActionMark();
            refreshLeader(); // (ChangeModel, ExeQuick)
            if (!wasRunning) continue;
            refreshGimmicks();
            if (script->events().scene().loaded()) syncEventFigures(script->events());
            placePlayer(script->takePlacement());
            checkBalloons();
            if (!script->running()) {
                SDL_Log("%s: the script ended", field.info().name.c_str());
                if (library.stage == 2) library.stage = 1, library.at = uiClock; // (an entry read: the shelf again)
                else if (playMode) branchAfterField();
                if (!fieldMode) return; // (gone to a vista)
            }
        }
    }

    // A frame's time: the vista's event, else the field (its script, then the leader - held while
    // the script runs)
    float uiClock = 0; // (the menus' seconds: the options', the manual's, the camp's - in a battle or out)
    // the fast-forward button's place as last drawn (id 0: not shown), whether it is held (the
    // mouse on it, or F), and --input fast's two seconds of it
    struct { int id = 0; float x0 = 0, y0 = 0, x1 = 0, y1 = 0; } fastRect;
    bool fastHeld = false;
    float fastUntil = -1;
    bool overFast(float fx, float fy) const {
        if (fastRect.id == 0) return false;
        const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
        const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
        return x >= std::min(fastRect.x0, fastRect.x1) && x <= std::max(fastRect.x0, fastRect.x1) && y >= std::min(fastRect.y0, fastRect.y1) &&
               y <= std::max(fastRect.y0, fastRect.y1);
    }
    void advance(float dt, float sx, float sy, bool run) {
        uiClock += dt;
        if (cr2::EventRuntime* ev = activeEvents()) {
            if (!inputScript.empty() && inputScript.front() == "fast" && uiClock >= fastUntil && (ev->fastForwardShown() || ev->skipButtonShown()))
                inputScript.pop_front(), fastUntil = uiClock + 2.0f; // (each two seconds of it, one after the other)
            ev->fastForward = fastHeld || uiClock < fastUntil;
        }
        if (!inputScript.empty() && (inputScript.front() == "yes" || inputScript.front() == "no") && yesNoShown() &&
            activeEvents()->yesNoAge() > 0.5f) { // (--input: an event's yes / no)
            answerYesNo(inputScript.front() == "yes");
            inputScript.pop_front();
        }
        audio.setVolumes(float(optionVolume[0]) / 100.0f, float(optionVolume[1]) / 100.0f, float(optionVolume[2]) / 100.0f);
        saveImport.poll();
        audio.update(dt);
        updateVistaSlash();
        if (optionsWasOpen && !optionsOpen && scriptSaves) writeSystem(); // (OptionMenu's end: Cr3Util_Save_SystemData)
        if (campOpen) campUseUpdate();
        optionsWasOpen = optionsOpen;
        if (credits.stage) return updateCredits(dt);
        if (titleMenu.stage) return updateTitle(dt);
        if (playMode && (fieldMode || eventMode || battleMode) && !paused) playSeconds += dt; // (the save's play time)
        if (battleMode) {
            const float realDt = dt;
            dt *= float(optionsOpen || settingsOpen ? 1 : settings.battleSpeed); // (Battle Speed)
            battleClock += dt;
            if (glass.running()) glass.advanceTo(battleClock - glassStart); // (its task: a frame each update)
            if (!inputScript.empty() && inputScript.front() == "skip" && scene.active && battleClock > scene.start + 1.0f) { // (--input: a tap on the scene)
                inputScript.pop_front();
                SDL_Log("input skip%s", skipScene() ? "" : " (not now)");
            }
            updateBattleScript(dt);
            updateManual(realDt);
            updateBattleSounds();
            updateBattleShow();
            updateResult();
            // a fight of the flow over: its last action shown, a moment after (a win: its result)
            updateGameOver();
            if (fightingInFlow && battleStep == BattleStep::Over && !battleScriptRunning() && battleClock > showEnd + 1.5f &&
                (result.stage == 0 || result.stage >= 4) && gameOver.stage == 0) {
                fightingInFlow = false;
                SDL_Log("play: battle %s", battleRules->exited() ? "ended by its script" : battleRules->escaped() ? "escaped" : battleRules->outcome() == 1 ? "won" : "lost (back to the field: no game over yet in the story)");
                if (ex.on && battleRules->outcome() == 1 && !randomBattle && ex.grind > 0) ex.grind = 0, exploreLog("  (won: no more fighting first)");
                ++battlesSkipped;
                afterBattle();
            }
        } else if (eventMode) {
            updateEvent(event && event->skipHeld() ? dt * 5 : dt); // (the skip button held: five updates a frame)
        } else if (fieldMode && (campOpen || mapOpen || saveStage || world.stage || shop.stage || party.stage || library.stage == 1 || pub.stage)) { // (FldMain steps 5 .. 7, 10: the camp, the map, saving, the world select - the field held)
            updateManual(dt);
            if (saveStage == 2 && uiClock - saveStart > 1.0f) saveStage = 0; // (a second, then closed)
            if (script && !script->running()) { // (the fade, the windows go on under a menu: GameMain's update - the library's fade back in)
                fieldClock = std::min(fieldClock + dt, 0.25f);
                for (; fieldClock >= cr2::EventRuntime::kTick; fieldClock -= cr2::EventRuntime::kTick) script->tick();
            }
            fieldInput();
        } else if (fieldMode) {
            // (Field Speed: the field faster while the player walks it - no script, change, lock or encounter running)
            if (!fieldScriptRunning() && !eventMode && keyLock.stage == 0 && changeStage == 0 && !encount.coupling && !optionsOpen && !settingsOpen)
                dt *= float(settings.fieldSpeed);
            fieldUpdate(script && script->events().skipHeld() ? dt * 5 : dt);
            updateChange(dt);
            updateSprint(dt);
            updateIai();
            fieldInput();
            if (fieldMode && !eventMode && encount.coupling && !fieldScriptRunning() && !actionRunning()) encountStep();
            else if (fieldMode && !eventMode) {
                const bool held = fieldScriptRunning() || keyLock.stage != 0 || changeStage != 0 || actionRunning();
                walk(dt, held ? 0 : sx, held ? 0 : sy, run);
            }
        }
    }

    // The events' runtime the screen shows: the vista's, else the field's
    cr2::EventRuntime* activeEvents() {
        if (titleMenu.stage && titleEvents) return titleEvents.get(); // (title.nut's)
        if ((battleMode || campOpen) && manualStage == 2 && manualEvents) return manualEvents.get(); // (the manual's tutorial)
        if (battleMode && battleScript) return &battleScript->events(); // (the battle's talks)
        return eventMode ? event.get() : script ? &script->events() : nullptr;
    }

    // --play: the game's flow - the field's entry, then what it reserved (GameState::next):
    // another area (SetPopPoint), a battle, a vista event (FldMain::BranchAfterEvent); after a
    // vista, its battle, another vista, else the field again (VistaMain::BranchAfterVista: evt200
    // after SetComeFromVistaToField); after a battle the field (evt100). Battles aren't in yet:
    // one is taken as won.
    bool playMode = false;
    std::string returnMap;
    float returnPlayer[3] = {0, 0, 0}, returnHeading = 0;
    // FldMain::BranchAfterEvent's order: an area change, a battle, a vista, then the world select,
    // the camp, the shop, the party, the pub, the chapter select, the library, the title
    void branchAfterField() {
        cr2::GameState::Next& n = game.next;
        if (!n.popPoint.empty()) {
            const std::string pp = n.popPoint;
            n.popPoint.clear();
            SDL_Log("play: to %s", pp.c_str());
            enterMap(pp.substr(0, 5), pp);
            return;
        }
        if (n.changeDungeon >= 0 && n.changeArea >= 0) { // (ChangeDungeon / ChangeArea: the area's own entry place)
            char map[16]; // (the town dungeon 0: t01NN; else dDDNN - the area from 0)
            if (n.changeDungeon == 0) std::snprintf(map, sizeof map, "t01%02d", n.changeArea + 1);
            else std::snprintf(map, sizeof map, "d%02d%02d", n.changeDungeon, n.changeArea + 1);
            n.changeDungeon = n.changeArea = -1;
            SDL_Log("play: to %s", map);
            enterMap(map, "");
            return;
        }
        if (n.battle >= 0) {
            returnMap = field.info().name, std::memcpy(returnPlayer, player, sizeof player), returnHeading = heading;
            return skipBattle();
        }
        if (n.vista) {
            returnMap = field.info().name, std::memcpy(returnPlayer, player, sizeof player), returnHeading = heading;
            n.vista = false;
            SDL_Log("play: vista %s in %s", n.vistaFunction.c_str(), n.vistaMap.c_str());
            if (!startVista(n.vistaFunction, n.vistaMap)) returnToField(-1);
            return;
        }
        if (n.worldSelect) { // (FldMain step 10: the world select, then WarpTask)
            n.worldSelect = false;
            openWorldSelect();
            return;
        }
        if (n.camp) { // (FldMain step 5: the camp)
            n.camp = false;
            return openCamp();
        }
        if (n.shop) { // (FldMain step 0xb: the shop)
            n.shop = false;
            openShop();
            return;
        }
        if (n.party) { // (FldMain step 0xc: the party)
            n.party = false;
            party = PartyMenu(), party.stage = 1, party.at = uiClock;
            SDL_Log("party: open");
            return;
        }
        if (n.pub) { // (FldMain step 0xd: the pub)
            n.pub = false;
            return openPub();
        }
        if (n.chapter) { // (FldMain step 0xe: the chapter select)
            n.chapter = false;
            openChapterSelect();
            return;
        }
        if (n.library) { // (FldMain step 0xf: the library)
            n.library = false;
            openLibrary();
            return;
        }
        if (n.title) { // (FldMain 0x1d, 2: the title - none here: the options' Title Screen's way)
            n.title = false;
            toTitle = true;
        }
    }
    // a battle the flow reached: fought (its party SetBattleParty's, else the party now; the party
    // attacking as a pair each turn), then on as the game goes after it (BtlMain::Exit) - or,
    // with --skip-battles, taken as won at once
    bool skipBattles = false;
    void skipBattle() {
        cr2::GameState::Next& n = game.next;
        if (!skipBattles) {
            int party[2];
            for (int k = 0; k < 2; ++k) {
                const int c = n.battleParty[k] >= 0 ? n.battleParty[k] : game.party[k];
                party[k] = c >= 0 ? c + 1 : 0; // (characters from 1 in the battle's data)
            }
            SDL_Log("play: battle %d (characters %d, %d)", n.battle, party[0], party[1]);
            const int id = n.battle;
            n.battle = -1, n.battleParty[0] = n.battleParty[1] = -1;
            if (startBattle(id, party)) {
                startFight(2, 1u + uint32_t(battlesSkipped));
                fightingInFlow = true, randomBattle = false;
                return;
            }
        }
        SDL_Log("play: battle %d - taken as won", n.battle);
        n.battle = -1;
        ++battlesSkipped;
        afterBattle();
    }
    bool fightingInFlow = false, randomBattle = false; // (randomBattle: an encounter's)
    // the battle dropped (the options' Title Screen)
    void leaveBattle() {
        battleMode = fightingInFlow = choosing = optionsOpen = optionsAsk = false;
        battleScript.reset(), battleRules.reset();
        battleFight = 0, battleStep = BattleStep::Over;
        showActions.clear(), showIndex = 0, regenPending.clear(), regenStarted = false;
        announceQueue.clear(), commandShown.clear();
    }
    // BtlMainCtrl::StepBattleOut / Cr3BattleEvent_Exit: with flag 1003 (the event's FlgOn(1003)
    // before BattleIn and SetVistaEvent) - cleared - on to the vista reserved (mode 2), else the
    // field (evt100; a vista reserved kept for the field's branch)
    void afterBattle() {
        cr2::GameState::Next& n = game.next;
        battleMode = false;
        if (game.flag(1003)) {
            game.setFlag(1003, false);
            if (n.vista) {
                n.vista = false;
                if (startVista(n.vistaFunction, n.vistaMap)) return;
            }
        }
        returnToField(1);
    }
    int battlesSkipped = 0;
    void branchAfterVista() {
        cr2::GameState::Next& n = game.next;
        if (n.battle >= 0) skipBattle();
        else if (n.vista) {
            n.vista = false;
            SDL_Log("play: vista %s in %s", n.vistaFunction.c_str(), n.vistaMap.c_str());
            if (!startVista(n.vistaFunction, n.vistaMap)) returnToField(n.fieldMode);
        } else if (n.credit >= 0) { // (VistaMain::BranchAfterVista: 3, the credits)
            eventMode = false;
            startCredits(n.credit);
            n.credit = -1;
        } else returnToField(n.fieldMode);
    }

    // The staff roll (ReserveCredit(n) - CreditMain; InterfaceMain::SetStaffrollParameter n + 1:
    // StaffrollMenu, interface 0x76 + n): from black (Cr3FadeIn 60 frames), the ending's music
    // (0x51e0e8: none, 17, 28, 29 - its fade 20), the ending's figure (end2_BG, end1_BG, end3_BG,
    // end4_BG: the table's four after its lines) on its own animation - scaled aspect / 1.5 across
    // a screen wider than 3:2 -, its typeS_L01 rising: line i (the executable's 544 - 44 .. 49 only
    // with flag 201, 438 .. 533 smaller) i x 0.0322 (scaled) below it, shown once it has risen so
    // far and gone above 0.4; the speedUp button, held, the figure five times as fast. All shown
    // and the animation ended: 120 updates, then black (60) with the music stopped, 62 more, and the
    // field (mode 4: the area's `credit`).
    struct Credits {
        int stage = 0; // 0 none, 1 rolling, 2 the wait after it, 3 fading out
        int number = 0;
        float animTime = 0, stageTime = 0, startY = 0;
        bool started = false;
    } credits;
    std::vector<std::string> staffLines;
    std::string staffFigures[4];
    bool staffTried = false;
    void startCredits(int n) {
        if (!staffTried) {
            staffTried = true;
            if (!cr2::releaseStaffRoll(dataArg, binaryArg, staffLines, staffFigures)) SDL_Log("credits: the staff roll's table not found in the executable");
        }
        credits = Credits();
        credits.stage = 1, credits.number = std::clamp(n, 0, 3);
        static const int kMusic[4] = {-1, 17, 28, 29};
        if (kMusic[credits.number] >= 0) audio.playBgm(kMusic[credits.number], 20);
        SDL_Log("play: credits %d (%zu lines)", n, staffLines.size());
    }
    std::string creditFigure() const {
        static const char* const kDefault[4] = {"end2_BG", "end1_BG", "end3_BG", "end4_BG"};
        std::string f = staffFigures[credits.number].empty() ? kDefault[credits.number] : staffFigures[credits.number];
        std::transform(f.begin(), f.end(), f.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return f;
    }
    float creditScale() const {
        const float aspect = float(screenW) / float(std::max(screenH, 1));
        return aspect < 1.5f ? 1.0f : aspect / 1.5f;
    }
    bool creditsLineShown(size_t i) const {
        return i < staffLines.size() && !staffLines[i].empty() && (i < 44 || i > 49 || game.flag(201));
    }
    void updateCredits(float dt) {
        if (!inputScript.empty() && inputScript.front() == "fast" && uiClock >= fastUntil) inputScript.pop_front(), fastUntil = uiClock + 2.0f;
        credits.stageTime += dt;
        if (credits.stage == 1) {
            credits.animTime += dt * (fastHeld || uiClock < fastUntil ? 5.0f : 1.0f); // (TouchSimpleMove: the figure's speed 5)
            UiFig& f = uiFig(creditFigure());
            const float dur = f.fig.animLoaded ? f.fig.anim.duration() : 0;
            float x, y;
            const float lineH = creditScale() * 0.032205883f;
            poseUi(f, creditFigure(), credits.animTime);
            if (uiJoint(f, "typeS_L01", x, y)) {
                if (!credits.started) credits.startY = y, credits.started = true;
                const bool allIn = (y - credits.startY) / lineH >= float(staffLines.empty() ? 0 : staffLines.size() - 1);
                if (allIn && credits.animTime >= dur) credits.stage = 2, credits.stageTime = 0;
            } else if (credits.animTime >= dur) credits.stage = 2, credits.stageTime = 0;
        } else if (credits.stage == 2 && credits.stageTime >= 120 * cr2::EventRuntime::kTick) {
            credits.stage = 3, credits.stageTime = 0;
            audio.stopBgm(60);
        } else if (credits.stage == 3 && credits.stageTime >= 62 * cr2::EventRuntime::kTick) {
            credits.stage = 0;
            SDL_Log("play: credits over");
            cr2::GameState::Next& n = game.next;
            const std::string pp = n.popPoint;
            n.popPoint.clear(), n.fieldMode = -1;
            if (!pp.empty()) enterMap(pp.substr(0, 5), pp, "credit"); // (FldMain::Init mode 4: CallEvent("credit"))
            else enterMap(returnMap, "", "credit");
        }
    }
    void drawCredits() {
        Mat4 camera = cr2::mat4Identity();
        camera[14] = -0.881f;
        const Mat4 view = cr2::mat4Multiply(uiProjection(screenW, screenH), camera);
        const float k = creditScale();
        UiFig& f = uiFig(creditFigure());
        f.fig.place = cr2::Mat34();
        f.fig.place.m[0] = f.fig.place.m[5] = k;
        poseUi(f, creditFigure(), credits.animTime);
        drawFigure(f.fig, view.data());
        float jx, jy;
        if (uiJoint(f, "typeS_L01", jx, jy)) {
            const float lineH = k * 0.032205883f;
            const float risen = credits.started ? (jy - credits.startY) / lineH : 0;
            for (size_t i = 0; i < staffLines.size(); ++i) {
                if (float(i) > risen) break;
                const float y = jy - float(i) * lineH;
                if (y >= 0.4f || !creditsLineShown(i)) continue;
                drawBorderedText(staffLines[i], i >= 438 && i < 534 ? 18.0f : 28.0f, jx, y, screenW, screenH, true); // (SetTextRender: the size + 4, or - 6, drawn 4 less; typeS_L: from the joint)
            }
        }
        fastRect.id = 0;
        if (credits.stage == 1) {
            UiFig& ff = uiFig("speedup");
            ff.fig.place = cr2::Mat34();
            poseUi(ff, "speedup", uiClock, true);
            drawFigure(ff.fig, view.data());
            float sx, sy, ex, ey;
            if (uiJoint(ff, "tapAreaS01", sx, sy) && uiJoint(ff, "tapAreaE01", ex, ey))
                fastRect.id = 1, fastRect.x0 = sx, fastRect.y0 = sy, fastRect.x1 = ex, fastRect.y1 = ey;
        }
        // the fades: in over 60 frames, out over 60 after the wait
        const float a = credits.stage == 1 ? std::max(0.0f, 1.0f - credits.stageTime / (60 * cr2::EventRuntime::kTick))
                        : credits.stage == 3 ? std::min(1.0f, credits.stageTime / (60 * cr2::EventRuntime::kTick)) : 0.0f;
        if (a > 0 && white.id) {
            static const float kScreen[24] = {-1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1, -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
            const Mat4 id = cr2::mat4Identity();
            const float rgba[4] = {0, 0, 0, a};
            dev->drawQuad(white, kScreen, id.data(), rgba);
        }
    }
    void returnToField(int mode) {
        cr2::GameState::Next& n = game.next;
        const std::string entry = mode == 1 ? "evt100" : mode == 2 ? "evt200" : "evt000";
        n.fieldMode = -1;
        eventMode = false;
        dev->resetHistory();
        if (!n.popPoint.empty()) {
            const std::string pp = n.popPoint;
            n.popPoint.clear();
            SDL_Log("play: to %s (%s)", pp.c_str(), entry.c_str());
            enterMap(pp.substr(0, 5), pp, entry);
        } else {
            SDL_Log("play: back to %s (%s)", returnMap.c_str(), entry.c_str());
            std::memcpy(player, returnPlayer, sizeof player), heading = returnHeading;
            enterMap(returnMap, "", entry);
        }
    }

    // A battle's stage (--battle: BtlMainCtrl::StepInit): the coupling's map (its own lights - the
    // directional one turned to come from the camera, BtlMain::SetLights' default), the party and
    // the monsters each on its battledpos joint (BtlModel::SetPosRot: the joint's place and turn)
    // playing its first motion (BtlMotionTable: the party bb02 then bb01, the monsters bb01), seen
    // through the commands' camera (Cam_Getset_<pattern>_00, a camera of BattleCamera_nomove /
    // _move; BtlCamera::Start: near halved - at least 0.1 - far five times, at most 5000)
    bool battleMode = false;
    cr2::BattleData battleData;
    cr2::BattleSetup battle;
    ViewFigure battleMap;
    ViewFigure specialMap;        // (BtlMap +0x13c: d1154b, Darwin's third awakening's)
    bool specialMapShown = false; // (+0x144)
    ViewFigure mapEffect;         // (+0x140: eff284, drawn with the map - +0x145)
    float specialMapStart = 0;
    // the screen breaking away (BtlPostEffectGlassFragment: scene op 33): the map alone drawn
    // into a texture at the next frame (BtlMain::PostEffectGlassFragments), its shards over the scene
    cr2::GlassFragments glass;
    bool glassPending = false;
    float glassStart = 0;
    cr2::Texture glassTexture;
    cr2::Rand48 glassRng;
    struct Ramp { // (a value from `from` to `to` over `time` s from `start`: StartAlphaAnimation's)
        float from = 1, to = 1, start = 0, time = 0;
        float at(float t) const {
            if (t <= start) return from;
            if (time <= 0 || t >= start + time) return to;
            return from + (to - from) * (t - start) / time;
        }
        void begin(float t, float f, float e, float d) { from = f, to = e, start = t, time = d; }
    };
    struct BattleUnit {
        ViewFigure fig;
        std::string figure;      // ("j001", "m206")
        std::string position;    // its battledpos joint
        cr2::Mat34 home;         // (there)
        // its motion: `motion` (a suffix: "bb02") from `motionStart` (battle seconds), once unless
        // `loop`, then `then` looping
        std::string motion = "bb01", then = "bb01";
        float motionStart = 0;
        bool loop = true;
        bool hidden = false;
        bool hold = false;        // (its motion kept at its end: the downed's bd02)
        bool guarding = false;    // (Defend chosen: its stance bg01 - motion 4 - till the turn's end)
        float hideAt = -1;        // (SetAlphaAnimationAutoHide: gone once its fading is over)
        bool sceneHidden = false; // (a special scene's: SetVisibility)
        Ramp alpha, colour;       // (its opacity, its brightness: SetAlpha, SetColor)
        // the frame its target camera is in (StepBattleIn_Setup's: its DummyCP on its stance's
        // first frame there), once found
        cr2::Mat34 targetFrame;
        bool targetFrameSet = false;
    };
    std::vector<BattleUnit> battleUnits; // (the party, then the monsters)
    struct CameraFigure {
        cr2::Geom geom;
        cr2::Skeleton skel;
        cr2::Animation anim;
        bool animated = false;
    } battleCameras[2];
    std::string battleCamera;
    float battleClock = 0, battleCameraStart = 0; // (the camera's animation runs from its start)
    // (`place`: the frame it is in - a target's camera its monster's -, `still`: its first frame
    // held, BtlCameraCtrl::GetCameraInfo's for StartCtrlCamera)
    cr2::Mat34 battleCameraPlace;
    bool battleCameraStill = false;
    // a move from `battleCameraFrom` (the camera's world when it began) to the camera over 0.5 s
    // from `battleCameraMoveStart` (< 0: none) - BtlMoveCtrl's interpolation 1, k 10
    cr2::Mat34 battleCameraFrom;
    float battleCameraMoveStart = -1;
    int targetCameraFighter = -1; // (whose target camera it is; -1 another)
    void setBattleCamera(const std::string& name, const cr2::Mat34& place = cr2::Mat34(), bool still = false) {
        if (name.empty() || (name == battleCamera && std::equal(place.m, place.m + 12, battleCameraPlace.m))) return;
        battleCamera = name, battleCameraStart = battleClock, battleCameraPlace = place, battleCameraStill = still, battleCameraLoop = false;
        battleCameraMoveStart = -1, targetCameraFighter = -1;
        dev->resetHistory(); // (a cut)
    }
    bool battleCameraLoop = false; // (BtlCameraCtrl::SetLooping)
    void setBattleCamera(int number) {
        if (const char* n = cr2::battleCameraName(number)) setBattleCamera(std::string(n));
    }

    // a new game's party state (Cr3InitWorkFromBattleData): the sopia's levels, the items, the
    // equipment - before the scripts change them
    // The system data (Cr3Util_Commit / Save / Load_SystemData: system.dat - the flags 0..991,
    // cr3_flag_system_work, and the options - kept across games: a new game's Cr3UtilResetWork
    // leaves them): CR2-U's system.txt beside the saves, written as the game saves (SaveConfirmationMenu)
    // and as the options close (OptionMenu's end), laid over every game begun or loaded
    static constexpr int kSystemFlags = 992;
    bool systemRead = false, systemFound = false;
    std::vector<int> systemFlags;
    int systemOptions[5] = {-1, -1, -1, -1, -1};
    void readSystem() {
        systemRead = true;
        std::FILE* f = std::fopen(savePath("system.txt").c_str(), "rb");
        if (!f) return;
        std::string text;
        char buf[4096];
        for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
        std::fclose(f);
        std::istringstream in(text);
        std::string line;
        if (!std::getline(in, line) || line != "CR2-U system 1") return;
        systemFound = true;
        while (std::getline(in, line)) {
            std::istringstream l(line);
            std::string key;
            l >> key;
            if (key == "flags")
                for (int n; l >> n;) systemFlags.push_back(n);
            else if (key == "options")
                for (int& o : systemOptions) l >> o;
        }
    }
    void applySystem() {
        if (!systemRead) readSystem();
        if (!systemFound) return; // (none yet: the save's own as they were)
        for (int n = 0; n < kSystemFlags; ++n) game.setFlag(n, false);
        for (int n : systemFlags) game.setFlag(n, true);
        if (systemOptions[0] >= 0)
            optionLanguage = systemOptions[0], optionQuality = systemOptions[1], optionVolume[0] = systemOptions[2],
            optionVolume[1] = systemOptions[3], optionVolume[2] = systemOptions[4];
    }
    void writeSystem() {
        systemRead = systemFound = true;
        systemFlags.clear();
        for (int n = 0; n < kSystemFlags; ++n)
            if (game.flag(n)) systemFlags.push_back(n);
        const int o[5] = {optionLanguage, optionQuality, optionVolume[0], optionVolume[1], optionVolume[2]};
        std::memcpy(systemOptions, o, sizeof o);
        std::string text = "CR2-U system 1\nflags";
        for (int n : systemFlags) text += " " + std::to_string(n);
        text += "\noptions";
        for (int v : o) text += " " + std::to_string(v);
        text += "\n";
        if (std::FILE* f = std::fopen(savePath("system.txt").c_str(), "wb")) std::fwrite(text.data(), 1, text.size(), f), std::fclose(f);
    }
    void newGame() {
        applySystem();
        std::vector<uint8_t> d;
        if (!archive.read("data\\battledata.dat", d) || !battleData.parse(d)) return;
        game.initSopia(battleData, cr2::sopiaExp), game.initItems(battleData), game.initEquip(battleData, cr2::levelExpTable());
    }
    bool startBattle(int id, const int party[2], int area = 0) {
        std::vector<uint8_t> d;
        std::string err;
        if (!archive.read("data\\battledata.dat", d, &err) || !battleData.parse(d, &err) ||
            !cr2::setupBattle(battleData, id, party, battle, &err, area > 0 ? battleData.area(area) : nullptr)) {
            SDL_Log("battle %d: %s", id, err.c_str());
            return false;
        }
        game.initSopia(battleData, cr2::sopiaExp); // (a new game's levels, the first time)
        game.initItems(battleData); // (and items)
        game.initEquip(battleData, cr2::levelExpTable()); // (and equipment, levels)
        cr2::Skeleton dpos;
        if (!archive.read("battledpos.skel", d, &err) || !cr2::parseSkeleton(d, dpos, &err)) {
            SDL_Log("battledpos: %s", err.c_str());
            return false;
        }
        const std::vector<cr2::Mat34> places = dpos.world(dpos.basePose);
        unload(battleMap);
        battleMap = ViewFigure();
        if (!loadFigure(battle.map + ".geom", battleMap, &err)) SDL_Log("battle map %s: %s", battle.map.c_str(), err.c_str());
        // (BtlMainCtrl::LoadResources: couplings 221, 271, 272, 317 - the map's eff284 too, drawn with it)
        unload(mapEffect), mapEffect = ViewFigure();
        if (id == 221 || id == 271 || id == 272 || id == 317)
            if (!loadFigure("eff284.geom", mapEffect, &err)) SDL_Log("eff284: %s", err.c_str());
        battleMap.absolute = true;
        cr2::Geom mg;
        cr2::Skeleton ms;
        lighting = cr2::SceneLighting();
        if (archive.read(battle.map + ".geom", d) && cr2::parseGeom(d, mg) && archive.read(battle.map + ".skel", d) && cr2::parseSkeleton(d, ms))
            cr2::lightsFromFigure(mg, ms, lighting);
        for (BattleUnit& u : battleUnits) unload(u.fig);
        battleUnits.clear();
        for (BattleEffect& fx : battleEffects) unload(*fx.fig);
        battleEffects.clear();
        for (SceneEffect& fx : commandEffects) unload(*fx.fig);
        commandEffects.clear();
        for (int f = 0; f < 2; ++f) dropGuardObject(f);
        for (auto& [id, fx] : scene.effects)
            if (fx.fig) unload(*fx.fig);
        scene = SceneState(), sceneCameras.clear(), battleFade = BattleFade(), battleMapHidden = false, mapColour = Ramp();
        glass = cr2::GlassFragments(), glassPending = false;
        std::string log;
        auto add = [&](const cr2::BattleSetup::Unit& su) {
            BattleUnit u;
            if (!loadFigure(su.figure + ".geom", u.fig, &err)) SDL_Log("battle %s: %s", su.figure.c_str(), err.c_str());
            // a monster's colour: its images' letter (m206 + a: m206a01)
            if (!su.colour.empty())
                for (ViewMesh& vm : u.fig.meshes) {
                    std::string t = vm.baseMaterial.diffuse();
                    if (t.size() > su.figure.size() && t.compare(0, su.figure.size(), su.figure) == 0 && std::isalpha(static_cast<unsigned char>(t[su.figure.size()]))) {
                        t[su.figure.size()] = su.colour[0];
                        if (cr2::Texture* tex = texture(t)) vm.texture = tex;
                    }
                }
            const int j = dpos.joint(cr2::nameHash(su.position.c_str()));
            if (j >= 0 && size_t(j) < places.size()) {
                cr2::Mat34 m = places[size_t(j)];
                for (int c = 0; c < 3; ++c) { // (its turn without the joint's scale)
                    const float l = std::sqrt(m.m[c] * m.m[c] + m.m[4 + c] * m.m[4 + c] + m.m[8 + c] * m.m[8 + c]);
                    if (l > 0) m.m[c] /= l, m.m[4 + c] /= l, m.m[8 + c] /= l;
                }
                u.fig.place = m;
            } else {
                SDL_Log("battle: no battledpos joint %s", su.position.c_str());
            }
            u.figure = su.figure, u.position = su.position, u.home = u.fig.place;
            u.motion = su.idle, u.then = "bb01", u.loop = su.idle == "bb01", u.motionStart = 0;
            log += " " + su.figure + su.colour + "@" + su.position;
            battleUnits.push_back(std::move(u));
        };
        for (const auto& su : battle.party) add(su);
        for (const auto& su : battle.monsters) add(su);
        const char* camFigures[2] = {"battlecamera_nomove", "battlecamera_move"};
        battleCamera.clear();
        for (int k = 0; k < 2; ++k) {
            CameraFigure& c = battleCameras[k];
            c = CameraFigure();
            if (archive.read(std::string(camFigures[k]) + ".geom", d)) cr2::parseGeom(d, c.geom);
            if (archive.read(std::string(camFigures[k]) + ".skel", d)) cr2::parseSkeleton(d, c.skel);
            c.animated = archive.read(std::string(camFigures[k]) + ".anim", d) && c.anim.parse(d) && c.anim.joints() == int(c.skel.basePose.size());
        }
        battleCamera = battle.camera;
        battleClock = 0;
        battleMode = true, msgOpen = false, msgCloseAt = -1;
        fieldMode = eventMode = false;
        dev->resetHistory();
        status = "battle " + std::to_string(id) + " in " + battle.map;
        SDL_Log("battle %d: map %s, pattern %s,%s; camera %s", id, battle.map.c_str(), battle.patternName.c_str(), log.c_str(), battleCamera.c_str());
        return true;
    }

    // the battle's camera now: the named camera's joint in whichever figure has it
    bool battleView(cr2::Mat34& world, float& fovY, float& zn, float& zf) {
        const uint32_t h = cr2::nameHash(battleCamera.c_str());
        std::vector<const CameraFigure*> figures = {&battleCameras[0], &battleCameras[1]};
        for (const auto& [name, c] : sceneCameras) figures.push_back(&c); // (a scene's loaded ones)
        for (const CameraFigure* cp : figures)
            for (const cr2::GeomCamera& gc : cp->geom.cameras) {
                const CameraFigure& c = *cp;
                if (gc.hash != h) continue;
                const int j = c.skel.joint(h);
                if (j < 0) continue;
                std::vector<cr2::JointTransform> p = c.skel.basePose;
                if (c.animated) {
                    const float frames = float(std::max(c.anim.frames() - 1, 1));
                    const float at = battleCameraStill ? 0.0f : (battleClock - battleCameraStart) * c.anim.sampleRate();
                    p = c.anim.poseAbsolute(c.skel.basePose, battleCameraLoop ? std::fmod(std::max(at, 0.0f), frames) : std::clamp(at, 0.0f, frames));
                }
                const std::vector<cr2::Mat34> w = c.skel.world(p);
                if (size_t(j) >= w.size()) continue;
                world = battleCameraPlace * w[size_t(j)];
                if (battleCameraMoveStart >= 0) {
                    const float u = std::max((battleClock - battleCameraMoveStart) / 0.5f, 0.0f);
                    if (u >= 1) battleCameraMoveStart = -1;
                    else world = blendPlace(battleCameraFrom, world, u * (2 - u)); // (u + u (1 - u) k / 10)
                }
                fovY = gc.fovY;
                zn = std::max(gc.near * 0.5f, 0.1f), zf = std::min(gc.far * 5.0f, 5000.0f);
                zf = std::max(zf, zn * 2);
                return true;
            }
        return false;
    }

    // a unit's figure on one of its motions (<figure><suffix>.anim; false: it has none)
    bool useMotion(BattleUnit& u, const std::string& suffix) {
        const std::string want = u.figure + suffix + ".anim";
        const auto it = std::find(u.fig.anims.begin(), u.fig.anims.end(), want);
        if (it == u.fig.anims.end()) return false;
        if (!u.fig.animLoaded || u.fig.anims[u.fig.animIndex] != want) {
            const float keep = time;
            loadAnim(u.fig, size_t(it - u.fig.anims.begin()));
            time = keep;
        }
        return true;
    }
    float motionLength(BattleUnit& u, const std::string& suffix) { return useMotion(u, suffix) ? u.fig.anim.duration() : 0.0f; }
    void playMotion(BattleUnit& u, const std::string& suffix, float at, const std::string& then = "bb01") {
        u.motion = suffix, u.then = then == "bb01" ? stanceOf(u) : then, u.motionStart = at, u.loop = false, u.hold = false;
    }
    // a fighter's stance: bg01 guarding (Defend), bx02 in War's Executioner's Pose (state 37 -
    // ActionSopiaLaunch: bx01, then bx02), else bb01
    std::string stanceOf(const BattleUnit& u) const {
        if (u.guarding) return "bg01";
        const int f = fighterOfUnit(u);
        if (f >= 0 && battleRules && battleRules->fighter(f).hasState(37) && battleRules->fighter(f).alive) return "bx02";
        return "bb01";
    }
    int fighterOfUnit(const BattleUnit& u) const {
        for (int f = 0; f < 5; ++f)
            if (fighterUnit[f] >= 0 && size_t(fighterUnit[f]) < battleUnits.size() && &battleUnits[size_t(fighterUnit[f])] == &u) return f;
        return -1;
    }

    void poseBattle() {
        pose(battleMap, battleClock);
        if (!specialMap.meshes.empty()) pose(specialMap, battleClock - specialMapStart); // (its animation from its making)
        if (!mapEffect.meshes.empty()) pose(mapEffect, battleClock);
        poseBattleShow();
        const float mc = mapColour.at(battleClock);
        for (int c = 0; c < 3; ++c) battleMap.tint[c] = mc;
        for (BattleUnit& u : battleUnits) {
            const float k = u.colour.at(battleClock);
            for (int c = 0; c < 3; ++c) u.fig.tint[c] = k;
            u.fig.tint[3] = u.alpha.at(battleClock);
            if (!u.fig.skinned) continue;
            if (u.hideAt >= 0 && battleClock >= u.hideAt) u.hidden = true, u.hideAt = -1;
            float t = battleClock - u.motionStart;
            if (!u.loop) { // (once, then its stance)
                const float d = motionLength(u, u.motion);
                if (u.hold && t >= d) t = d; // (kept at its end)
                else if (d <= 0 || t >= d) u.motionStart += std::max(d, 0.0f), u.motion = u.then, u.loop = true, t = battleClock - u.motionStart;
            }
            if (!useMotion(u, u.motion) && !useMotion(u, "bb01")) continue;
            pose(u.fig, std::max(t, 0.0f), u.loop);
        }
    }

    // --fight solo | pair: the battle played by its rules (battle/rules.cpp), each turn's actions
    // one after another as BtlActionCtrl shows an attack - the command's motion
    // (BtlMotionTable's), the attacker moving to its aPos joint by the target over the table's
    // move times and back over the second's, the targets' damage motion (bd01) at the first hit
    // time, the downed hidden - the party attacking every turn
    std::unique_ptr<cr2::BattleRules> battleRules;
    int battleFight = 0; // 0 off, 1 solo, 2 pair
    int fighterUnit[5] = {-1, -1, -1, -1, -1};
    cr2::Skeleton battleApos;
    std::vector<cr2::Mat34> battleAposWorld;
    struct ShowAction {
        int actor = -1, partner = -1, command = 0;
        std::vector<cr2::BattleRules::Event> hits;
    };
    std::vector<ShowAction> showActions;
    size_t showIndex = 0;
    float showStart = 0; // (battle seconds)
    float showHitAt = 0, showEnd = 0;
    int showCamera2 = -1;
    bool showHitDone = false;
    struct Move {
        int unit = -1;
        cr2::Mat34 to;
        float start = -1, end = -1, back = -1, backEnd = -1;
        bool active = false;
    } showMoves[2];

    // the battleapos joint `name`'s place (false: none)
    bool aposPlace(const std::string& name, cr2::Mat34& out) {
        const int j = battleApos.joint(cr2::nameHash(name.c_str()));
        if (j < 0 || size_t(j) >= battleAposWorld.size()) return false;
        out = battleAposWorld[size_t(j)];
        for (int c = 0; c < 3; ++c) {
            const float l = std::sqrt(out.m[c] * out.m[c] + out.m[4 + c] * out.m[4 + c] + out.m[8 + c] * out.m[8 + c]);
            if (l > 0) out.m[c] /= l, out.m[4 + c] /= l, out.m[8 + c] /= l;
        }
        return true;
    }
    // where an attacker goes to strike a target (BtlActionCtrl::GetAposName): by the target the
    // party's "aPos_<its place><the attacker's: pSoloA, pPairA...>", by one of the party a
    // monster's "aPos_<pSoloA | pSoloB | pPair><its place: s, ssL, m, msL...>"
    std::string aposName(int actor, int target) {
        auto key = [&](int f) {
            const int u = fighterUnit[f];
            std::string k = u >= 0 ? battleUnits[size_t(u)].position.substr(5) : "";
            return k;
        };
        const bool pair = battleRules && battleRules->fighter(actor).partner >= 0;
        if (actor < 2) return "aPos_" + key(target) + std::string(pair ? (actor == 0 ? "pPairA" : "pPairB") : (actor == 0 ? "pSoloA" : "pSoloB"));
        std::string mk = key(actor);
        if (mk == "sC") mk = "s";
        else if (mk == "mC") mk = "m";
        const bool targetPair = battleRules && battleRules->fighter(target).partner >= 0;
        return "aPos_" + std::string(targetPair ? "pPair" : target == 0 ? "pSoloA" : "pSoloB") + mk;
    }

    // The fight's steps (BtlMainCtrl / BtlCommand / BtlActionCtrl), the battle's script between:
    // the opening, Battle_Start; each turn Turn_Start, its order, then for each action
    // Command_Start, the action shown, Command_End; the end - each step after the script's call
    // is over (BtlEvent::IsRunning), its talks on the screen meanwhile
    enum class BattleStep { Opening, TurnBegin, TurnChoose, ActionStart, ActionDo, ActionEnd, Over };
    // The party's commands chosen on the screen (BtlCommand: the menus, a tap or the keys - Up /
    // Down and Return, Backspace back, Left / Right among the targets), or given (--input: "pair
    // attack 2 ...", one a 0.4 s); else (a capture, --fight) the party attacking each turn
    bool manualCommands = false;
    bool choosing = false;
    cr2::CommandInput commandInput;
    float menuStart = 0;
    int menuCursor = 0;
    std::deque<std::string> inputScript;
    float inputNext = 0;
    struct TapRect {
        int id;
        float x0, y0, x1, y1; // (UI units)
    };
    std::vector<TapRect> tapRects;
    // ---- The keys' (and a controller's) focus where the iPad's and Android's game is touch alone and
    // the Vita moves a cursor (GameTitleMenu's buttons and questions, the shop's first two menus, the
    // party's): the arrows to the next button that way (by their tap areas' middles, round past the
    // last), Return its tap, the focused one in the game's own look (CircleBtn::SetForcusTarget: PRG_shiftR
    // half across, as a focused command button) - and the field's tab, which the Vita steps round
    // (FieldMenuTabMenu: camp, encounters, save, map; ○ the one lit)
    int keyFocus = -1; // (a tap area's id; -1 none yet)
    int tabFocus = 0;  // (the tab's: its area - 2 camp, 5 encounters, 4 save, 3 map; 0 none)
    bool focusScreen() const {
        return titleMenu.stage == 5 || titleMenu.stage == 6 || titleMenu.stage == 7 || shop.stage == 1 || shop.stage == 2 ||
               party.stage == 1 || party.stage == 2;
    }
    bool focusKey(SDL_Keycode k) {
        if (tabFocusKey(k)) return true;
        if (!focusScreen() || (titleMenu.stage && titleRunning())) return false;
        if (k == SDLK_BACKSPACE && titleMenu.stage == 6) return titleButton(100), true;
        if (k == SDLK_BACKSPACE && titleMenu.stage == 7) return titleAnswer(false), true;
        std::vector<TapRect> rs;
        for (const TapRect& r : tapRects)
            if (r.id >= 0) rs.push_back(r);
        if (rs.empty()) return false;
        auto mid = [](const TapRect& r, float& x, float& y) { x = (r.x0 + r.x1) * 0.5f, y = (r.y0 + r.y1) * 0.5f; };
        const auto cur = std::find_if(rs.begin(), rs.end(), [&](const TapRect& r) { return r.id == keyFocus; });
        const bool arrow = k == SDLK_UP || k == SDLK_DOWN || k == SDLK_LEFT || k == SDLK_RIGHT;
        if (arrow && cur == rs.end()) {
            keyFocus = rs.front().id;
            audio.playSe(0xb3, false); // (the cursor's: the Vita's ScrollList / CircleBtn move)
            return true;
        }
        if (arrow) {
            const float dx = k == SDLK_RIGHT ? 1.0f : k == SDLK_LEFT ? -1.0f : 0.0f, dy = k == SDLK_UP ? 1.0f : k == SDLK_DOWN ? -1.0f : 0.0f;
            float cx, cy;
            mid(*cur, cx, cy);
            int best = -1, wrap = -1;
            float bestScore = 1e9f, wrapScore = 1e9f;
            for (size_t i = 0; i < rs.size(); ++i) {
                if (rs[i].id == cur->id) continue;
                float x, y;
                mid(rs[i], x, y);
                const float along = (x - cx) * dx + (y - cy) * dy, across = std::abs((x - cx) * dy - (y - cy) * dx);
                if (along > 0.01f) {
                    if (along + 2 * across < bestScore) bestScore = along + 2 * across, best = int(i);
                } else if (along < -0.01f && across < 0.08f && along < wrapScore) { // (round: the farthest back in line)
                    wrapScore = along, wrap = int(i);
                }
            }
            const int to = best >= 0 ? best : wrap;
            if (to >= 0) keyFocus = rs[size_t(to)].id, audio.playSe(0xb3, false), SDL_Log("focus: %d", keyFocus);
            return true;
        }
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
            if (cur == rs.end()) return keyFocus = rs.front().id, true;
            float x, y;
            mid(*cur, x, y);
            const float s = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
            const float fx = x * s / float(std::max(screenW, 1)) + 0.5f, fy = 0.5f - y * s / float(std::max(screenH, 1));
            SDL_Log("focus: tap %d", cur->id);
            if (titleMenu.stage) titleClick(fx, fy);
            else fieldClick(fx, fy);
            return true;
        }
        return false;
    }
    // the open tab's ring (FUN_81098da8: Up / Left back, Down / Right on - the encounters' left out
    // while off; ○ the one lit)
    bool tabFocusKey(SDL_Keycode k) {
        if (!fieldTabShown() || !fieldTabOpen) return tabFocus = 0, false;
        const bool encounters = game.flag(0x40f);
        static const int kRing[4] = {2, 5, 4, 3};
        int at = 0;
        while (at < 4 && kRing[at] != tabFocus) ++at;
        if (k == SDLK_DOWN || k == SDLK_RIGHT || k == SDLK_UP || k == SDLK_LEFT) {
            const int step = k == SDLK_DOWN || k == SDLK_RIGHT ? 1 : 3;
            at = at >= 4 ? (step == 1 ? 0 : 3) : (at + step) % 4;
            if (kRing[at] == 5 && !encounters) at = (at + step) % 4;
            tabFocus = kRing[at];
            audio.playSe(0xb3, false);
            SDL_Log("field: the tab's %d lit", tabFocus);
            return true;
        }
        if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && tabFocus) return fieldTabTap(tabFocus), true;
        return false;
    }
    // a list shown (a page of it a press of L / R: ScrollListMenu's)
    bool listShown() const {
        using CI = cr2::CommandInput;
        const bool battleList = battleMode && choosing &&
                                (commandInput.menu() == CI::Menu::Items || commandInput.menu() == CI::Menu::Advent ||
                                 commandInput.menu() == CI::Menu::Awake || commandInput.menu() == CI::Menu::Sopia);
        return battleList || manualStage == 1 || settingsOpen || (campOpen && campList != 0) || library.stage == 1;
    }
    bool inMenu() const {
        return campOpen || (battleMode && choosing) || shop.stage || world.stage || pub.stage || library.stage == 1 || party.stage;
    }
    // a step of the choosing begun: its camera (StepGetset: Cam_Getset; SetCommandCamera: 0x1a the
    // first's, 0x1b the second's, 0x1d the pair's command, 0x1c the one alone's - the target's camera
    // BtlCommand::SetCommandTargetCamera's - setTargetCamera)
    void menuChanged() {
        menuStart = battleClock, menuCursor = 0;
        using CI = cr2::CommandInput;
        if (commandInput.menu() == CI::Menu::First) setBattleCamera(battle.camera);
        else if (commandInput.menu() == CI::Menu::Command || commandInput.menu() == CI::Menu::Awake || commandInput.menu() == CI::Menu::Advent ||
                 commandInput.menu() == CI::Menu::Sopia || commandInput.menu() == CI::Menu::Items)
            setBattleCamera(fighterUnit[1] < 0 ? 0x1c : commandInput.pairTurn() ? 0x1d : commandInput.actor() == 0 ? 0x1a : 0x1b);
        // (the sopia's and the items' lists the first time - flags 61, 62: their tutorials first)
        if (commandInput.menu() == CI::Menu::Sopia) battleTutorial("Tutorial_ComSopia", 61);
        else if (commandInput.menu() == CI::Menu::Items) battleTutorial("Tutorial_ComItem", 62);
    }
    // the camera a target is chosen under (BtlCommand::SetCommandTargetCamera: the fighter's
    // camera, StepBattleIn_Setup's BtlWork +0x580 - a party member's GetTarget 0 / 1, 2 for the one
    // alone, 3 for a pair's; a monster's by its size, 4 + 0..3, in its own frame, the human
    // patterns' 8 / 9 (Hpair), 10 (Hsolo), the boss's 12, XLXL's 13 in the battle's)
    void setTargetCamera(int f) {
        if (f < 0 || f >= 5) return;
        cr2::Mat34 place;
        int k;
        if (f < 2) k = !commandInput.pairTurn() ? (battleRules->fighter(1).present ? f : 2) : 3;
        else {
            const int p = battle.pattern;
            const cr2::BattleData::Monster* mo = battleData.monster(battleRules->fighter(f).id);
            k = p == 10 ? 10 : p == 11 ? (f == 2 ? 8 : 9) : p == 12 ? 12 : p == 9 ? 13 : (mo ? mo->size : 0) + 4;
            if (p != 10 && p != 11 && fighterUnit[f] >= 0) place = targetFrame(battleUnits[size_t(fighterUnit[f])]);
        }
        const char* n = cr2::battleCameraName(cr2::cameraTarget(k));
        if (!n || f == targetCameraFighter) return;
        // (another target on the same side: the camera moves there - SetMovePosition /
        // SetMoveRotation over 0.5 s; the other side's, or the first: a cut, StartCtrlCamera)
        cr2::Mat34 from;
        float fov, zn, zf;
        const bool move = targetCameraFighter >= 0 && (targetCameraFighter < 2) == (f < 2) && battleView(from, fov, zn, zf);
        setBattleCamera(std::string(n), place, true);
        targetCameraFighter = f;
        if (move) battleCameraFrom = from, battleCameraMoveStart = battleClock;
    }
    // a monster's target camera's frame (StepBattleIn_Setup: BtlModel::GetPosition / GetRotation
    // ("DummyCP") - the joint's world place, its turn without scale - on the pose then, its stance's
    // first frame at its battledpos; its feet if it has none)
    const cr2::Mat34& targetFrame(BattleUnit& bu) {
        if (bu.targetFrameSet) return bu.targetFrame;
        bu.targetFrameSet = true;
        cr2::Mat34& place = bu.targetFrame;
        place = bu.home;
        const int cj = bu.fig.skeleton.joint(cr2::nameHash("DummyCP"));
        if (cj >= 0) {
            std::vector<cr2::JointTransform> pose = bu.fig.skeleton.basePose;
            std::vector<uint8_t> d;
            cr2::Animation a;
            if (archive.read(bu.figure + "bb01.anim", d) && a.parse(d) && a.joints() == int(pose.size())) pose = a.poseAbsolute(pose, 0);
            const std::vector<cr2::Mat34> w = bu.fig.skeleton.world(pose, bu.home);
            if (size_t(cj) < w.size()) place = w[size_t(cj)];
        }
        for (int c = 0; c < 3; ++c) {
            const float l = std::sqrt(place.m[c] * place.m[c] + place.m[4 + c] * place.m[4 + c] + place.m[8 + c] * place.m[8 + c]);
            if (l > 0) place.m[c] /= l, place.m[4 + c] /= l, place.m[8 + c] /= l;
        }
        return place;
    }
    // a token of --input as a choice
    void inputToken(const std::string& t) {
        using CI = cr2::CommandInput;
        static const std::pair<const char*, int> kNames[] = {{"solo", CI::Solo}, {"pair", CI::Pair}, {"escape", CI::Escape},
                                                             {"attack", CI::Attack}, {"defend", CI::Defend}, {"sopia", CI::Sopia},
                                                             {"items", CI::Items}};
        if (t == "options") {
            choose(CI::Options);
            SDL_Log("input options");
            return;
        }
        bool ok = false;
        static const std::pair<const char*, SDL_Keycode> kKeys[] = {{"up", SDLK_UP}, {"down", SDLK_DOWN}, {"left", SDLK_LEFT},
                                                                    {"right", SDLK_RIGHT}, {"enter", SDLK_RETURN}};
        for (const auto& [n, key] : kKeys)
            if (t == n) {
                battleKey(key);
                SDL_Log("input %s", t.c_str());
                return;
            }
        // (the options' own: the Title Screen button, its question's answers, the language's and the
        // quality's arrows)
        static const std::pair<const char*, int> kOptionTaps[] = {{"title", 3101}, {"manual", 3102}, {"quality-", 3103}, {"quality+", 3104},
                                                                  {"language-", 3105}, {"language+", 3106}, {"yes", 3201}, {"no", 3202},
                                                                  {"graphics", 3110}};
        for (const auto& [n, id] : kOptionTaps)
            if (t == n && (optionsOpen || optionsAsk)) {
                optionTap(id);
                SDL_Log("input %s", t.c_str());
                return;
            }
        if (t == "back" && settingsOpen) closeSettings(), ok = true;
        else if (t == "back" && manualStage == 1) closeManual(), ok = true;
        else if (t == "back" && optionsOpen) optionsOpen = false, ok = true;
        else if (t == "back") commandInput.back(), audio.playSe(0xb5, false), ok = true;
        else if (t.size() > 1 && t[0] == 'a' && std::isdigit(static_cast<unsigned char>(t[1]))) ok = commandInput.pickAwakening(std::atoi(t.c_str() + 1));
        else if (t.size() > 1 && t[0] == 's' && std::isdigit(static_cast<unsigned char>(t[1]))) ok = commandInput.pickSopia(std::atoi(t.c_str() + 1));
        else if (t.size() > 1 && t[0] == 'i' && std::isdigit(static_cast<unsigned char>(t[1]))) ok = commandInput.pickItem(std::atoi(t.c_str() + 1));
        else if (t.size() > 1 && t[0] == 'v' && std::isdigit(static_cast<unsigned char>(t[1]))) ok = commandInput.pickAdvent(std::atoi(t.c_str() + 1));
        else if (t == "awake") ok = commandInput.press(cr2::CommandInput::Awake);
        else if (t == "advent") ok = commandInput.press(cr2::CommandInput::Advent);
        else if (!t.empty() && std::isdigit(static_cast<unsigned char>(t[0]))) ok = commandInput.pick(std::atoi(t.c_str()));
        else
            for (const auto& [n, id] : kNames)
                if (t == n) {
                    if (commandInput.menu() == CI::Menu::Command && id == CI::Defend) battleTutorial("Tutorial_ComDefende", 63);
                    ok = commandInput.press(id);
                }
        SDL_Log("input %s%s", t.c_str(), ok ? "" : " (not taken)");
        if (!ok && commandInput.menu() == cr2::CommandInput::Menu::Sopia) // (what it offers)
            for (const auto& sc : commandInput.sopiaCommands()) SDL_Log("  sopia %d: MP %d%s", sc.command, sc.mp, sc.selectable ? "" : " (not selectable)");
        menuChanged();
    }
    // the menu's choices in order (the buttons selectable, or the targets)
    std::vector<int> menuChoices() const {
        std::vector<int> out;
        if (commandInput.menu() == cr2::CommandInput::Menu::Target) return commandInput.targets();
        if (commandInput.menu() == cr2::CommandInput::Menu::Awake) {
            for (const auto& a : commandInput.awakenings())
                if (a.selectable) out.push_back(a.command);
            return out;
        }
        if (commandInput.menu() == cr2::CommandInput::Menu::Advent) {
            for (const auto& a : commandInput.advents())
                if (a.selectable) out.push_back(a.command);
            return out;
        }
        if (commandInput.menu() == cr2::CommandInput::Menu::Sopia) {
            for (const auto& sc : commandInput.sopiaCommands())
                if (sc.selectable) out.push_back(sc.command);
            return out;
        }
        if (commandInput.menu() == cr2::CommandInput::Menu::Items) {
            for (const auto& e : commandInput.items())
                if (e.selectable) out.push_back(e.item);
            return out;
        }
        for (const auto& b : commandInput.buttons())
            if (b.selectable) out.push_back(b.id);
        return out;
    }
    void choose(int idOrFighter) {
        audio.playSe(0xb4, false); // (the buttons' decide: Cr3UtilSoundPlaySE 0xb4)
        if (commandInput.menu() == cr2::CommandInput::Menu::Target) commandInput.pick(idOrFighter);
        else if (commandInput.menu() == cr2::CommandInput::Menu::Awake) commandInput.pickAwakening(idOrFighter);
        else if (commandInput.menu() == cr2::CommandInput::Menu::Advent) commandInput.pickAdvent(idOrFighter);
        else if (commandInput.menu() == cr2::CommandInput::Menu::Sopia) commandInput.pickSopia(idOrFighter);
        else if (commandInput.menu() == cr2::CommandInput::Menu::Items) commandInput.pickItem(idOrFighter);
        else if (commandInput.menu() == cr2::CommandInput::Menu::First && idOrFighter == cr2::CommandInput::Options) {
            optionsOpen = true, optionsStart = uiClock; // (BtlCommand::StepOption: the window, the HUD gone)
            // (OptionMenu::PartsUpdate: the first time, title.nut's Tutorial_Option - flag 84)
            if (!game.flag(84)) optionsOpen = false, playTutorial("Tutorial_Option", 0, false);
            return;
        } else {
            // (StepCommandLoop: Defend the first time - flag 63 - its tutorial first, StepTutoEventInit)
            if (commandInput.menu() == cr2::CommandInput::Menu::Command && idOrFighter == cr2::CommandInput::Defend)
                battleTutorial("Tutorial_ComDefende", 63);
            commandInput.press(idOrFighter);
        }
        menuChanged();
    }
    // a first-time tutorial of battle_tuto's (BattleScript::tutorial) - in an encounter without a
    // script of its own, its story flag still off (BtlWork +0x18, which would turn them off, never
    // set here); the battle's steps wait for it as for any of its script's calls
    // The battles' sounds (BtlSound): an effect by its number (PlaySE: soundtable's clip), a
    // fighter's voice by its kind (PlayVoiceFromData -> Cr3VoiceTable: its figure's four
    // characters) - one voice at a time, queued for when it's due (CreatePlayVoiceJob)
    void battleSe(int n) {
        const int clip = cr2::SoundData::battleSe(n);
        if (clip >= 0) audio.playSe(clip, false);
    }
    struct VoiceJob {
        float at = 0;
        int clip = -1;
    };
    std::vector<VoiceJob> voiceJobs;
    // (`kind`: the data's - one of its three table kinds at random; `tableKind`: Cr3VoiceTable's
    // own, as a command's +0x68)
    void battleVoice(int f, int kind, float delay = 0, bool tableKind = false) {
        const int u = f >= 0 && f < 5 ? fighterUnit[f] : -1;
        if (u < 0) return;
        // (BtlSound::PlayVoiceRequest: none for the monster m309 while flag 3053 is set -
        // battle_234's and battle_265's Battle_Start set it)
        if (f >= 2 && battleUnits[size_t(u)].figure == "m309" && game.flag(3053)) return;
        if (!tableKind) {
            const std::array<int, 3>* choices = battleData.voice(kind);
            if (!choices) return;
            kind = (*choices)[size_t(int(battleClock * 997.0f) % 3)];
        }
        const int clip = cr2::SoundData::voiceClip(battleUnits[size_t(u)].figure, kind);
        if (clip >= 0) voiceJobs.push_back({battleClock + delay, clip});
    }
    // BtlSound::PlayVoiceRandomPlayer: one of the party standing (both: either, evenly) says it
    void partyVoice(int kind, bool tableKind = false, float delay = 0) {
        const bool a = battleRules->fighter(0).alive, b = battleRules->fighter(1).present && battleRules->fighter(1).alive;
        const int who = a && b ? (int(boxRandom() % 100) > 49 ? 1 : 0) : a ? 0 : b ? 1 : -1;
        if (who >= 0) battleVoice(who, kind, delay, tableKind);
    }
    int partyDeaths = 0, lastPartyActor = 0;
    // (CharaStatusWindowMenu / BMenu::Initialize: the name plate by the option's language - Japanese
    // pcNameJP_id, the two Chineses pcNameCHN1 / 2_id, Korean pcNameKOR_id, all at campwin's
    // call_pcNameJP_id..22; else - English, French, German, Spanish - pcNameEN_id at ..21; the
    // character's frame the same in each)
    struct NamePlate {
        std::string figure, joint; // (the joint's start: + the place's digits + its end)
        std::string end = "21"; // (campwin's: call_pcNameEN_id021, call_pcNameJP_id022 ...)
    };
    NamePlate namePlate() const {
        switch (optionLanguage) {
        case 0: return {"pcnamejp_id", "call_pcNameJP_id", "22"};
        case 5: return {"pcnamechn1_id", "call_pcNameJP_id", "22"};
        case 6: return {"pcnamechn2_id", "call_pcNameJP_id", "22"};
        case 7: return {"pcnamekor_id", "call_pcNameJP_id", "22"};
        default: return {"pcnameen_id", "call_pcNameEN_id", "21"};
        }
    }
    int faceState[2] = {0, 0};          // (the HUD's faces: 0 as they are, 1 choosing, 2 down)
    float faceStateAt[2] = {0, 0}; // (BtlWork fighters' +0x48e, the deaths so far; +0x16, the last of the party to act)
    std::vector<VoiceJob> seJobs; // (BtlSound::CreatePlaySEJob: an effect's clip later)
    void updateBattleSounds() {
        for (size_t k = 0; k < seJobs.size();)
            if (battleClock >= seJobs[k].at) {
                audio.playSe(seJobs[k].clip, false);
                seJobs.erase(seJobs.begin() + std::ptrdiff_t(k));
            } else ++k;
        for (size_t k = 0; k < voiceJobs.size();)
            if (battleClock >= voiceJobs[k].at) {
                audio.stopAllVoices(0);
                audio.playVoice(voiceJobs[k].clip);
                voiceJobs.erase(voiceJobs.begin() + std::ptrdiff_t(k));
            } else ++k;
        if (pendingBgm > 0 && battleClock >= pendingBgmAt) { // (BtlResult / BtlGameover: 1.1 s on)
            audio.releaseBgm();
            audio.playBgm(pendingBgm, 0);
            pendingBgm = 0;
        }
    }
    int pendingBgm = 0;
    float pendingBgmAt = 0;
    // a turn break beginning (BtlTurnEnd: the break due, +0x1f 3 -> 1 / 2): SE 2, then 1 s on 3 for
    // the party's, 4 the monsters'; turnBrk / turnBrkEnemy (Battle / EnemyBattleBreakEffectMenu)
    // played once over the HUD
    int breakShown = 0;
    float breakShownAt = 0;
    void battleTutorial(const char* function, int flag) {
        if (const cr2::BattleData::Coupling* cp = battleData.coupling(battle.coupling); cp && cp->flag) return; // (+0x18)
        if (!battleScript || !battleScriptOk || battleScript->own() || battleScriptRunning() || game.flag(flag)) return;
        SDL_Log("tutorial: %s", function);
        if (!(battleScriptOk = battleScript->tutorial(function))) SDL_Log("battle script: %s", battleScript->error().c_str());
    }
    bool keysUsed = false; // (a command button's focus shown once a key moves it: a touch shows none)
    // the options' window, its question and the manual (in a battle or the camp) taking a key:
    // false - none of them open
    bool optionsKey(SDL_Keycode k) {
        if (saveImport.active()) {
            if (saveImport.state == vitaimport::Session::Picking) {
                if (k == SDLK_ESCAPE || k == SDLK_BACKSPACE) saveImport.cancel();
            } else if (k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_UP || k == SDLK_DOWN) saveImport.focus ^= 1;
            else if (k == SDLK_ESCAPE || k == SDLK_BACKSPACE || k == SDLK_N) saveImportAnswer(false);
            else if (k == SDLK_Y) saveImportAnswer(true);
            else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) saveImportAnswer(saveImport.focus == 0);
            return true;
        }

        if (settingsKey(k)) return true;
        if (manualStage == 2) { // (the tutorial: Return a page)
            if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && manualEvents) manualEvents->advance();
            return true;
        }
        if (manualStage == 1) { // (the list: Up / Down, Return plays, Backspace back)
            const int n = std::max(int(manualEntries.size()), 1);
            if (k == SDLK_UP) manualCursor = (manualCursor + n - 1) % n;
            else if (k == SDLK_DOWN) manualCursor = (manualCursor + 1) % n;
            else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) playManual(manualCursor);
            else if (k == SDLK_BACKSPACE) closeManual();
            return true;
        }
        if (optionsAsk) { // (the question: Return yes, Backspace no)
            if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_Y) optionTap(3201);
            else if (k == SDLK_BACKSPACE || k == SDLK_N) optionTap(3202);
            return true;
        }
        if (optionsOpen) { // (the options: Backspace / Return back, Left / Right the focused bar; G / Tab
                           // CR2-U's Display & Graphics)
            if (k == SDLK_I || ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && optionFocus == 3)) optionTap(3111);
            else if (k == SDLK_G || k == SDLK_TAB) openSettings();
            else if (k == SDLK_BACKSPACE || k == SDLK_RETURN || k == SDLK_KP_ENTER) optionsOpen = false;
            else if (k == SDLK_UP) optionFocus = (optionFocus + 3) % 4;
            else if (k == SDLK_DOWN) optionFocus = (optionFocus + 1) % 4;
            else if (k == SDLK_LEFT && optionFocus < 3) optionVolume[optionFocus] = std::max(optionVolume[optionFocus] - 5, 0), volumeSet(optionFocus);
            else if (k == SDLK_RIGHT && optionFocus < 3) optionVolume[optionFocus] = std::min(optionVolume[optionFocus] + 5, 100), volumeSet(optionFocus);
            return true;
        }
        return false;
    }
    // a volume set (OptionMenu::TouchSimpleRelease: the effects' bar let go - Cr3OptionData::SetSE -
    // plays SE 0xc6 at it)
    void volumeSet(int bar) {
        audio.setVolumes(float(optionVolume[0]) / 100.0f, float(optionVolume[1]) / 100.0f, float(optionVolume[2]) / 100.0f);
        if (bar == 1) audio.playSe(0xc6, false);
    }
    // a tap on the options' window, its question or the manual (`x`: across, UI units): false -
    // none of them open
    bool optionsTapped(const TapRect& r, float x) {
        if (saveImport.active()) {
            if (r.id == 3112) saveImportAnswer(true);
            else if (r.id == 3113 || r.id == 3114) saveImportAnswer(false);
            return true;
        }
        if (settingsTapped(r.id)) return true;
        if (manualStage == 1) {
            if (r.id == 100) closeManual();
            else if (r.id >= 5000) playManual(r.id - 5000);
            else if (r.id >= 1000) manualCursor = r.id - 1000; // (another row: the focus)
            return true;
        }
        if (optionsAsk) {
            if (r.id == 3201 || r.id == 3202) optionTap(r.id);
            return true;
        }
        if (optionsOpen) { // (Back; a bar's slide area: its volume by the point across it)
            if (r.id == 100) optionsOpen = false;
            else if (r.id == 3110) openSettings();
            else if ((r.id >= 3100 && r.id < 3300) || r.id == 4036 || r.id == 4037 || r.id == 4098 || r.id == 4099) optionTap(r.id);
            else if (r.id >= 2000 && r.id < 2003) {
                const float a = std::min(r.x0, r.x1), b = std::max(r.x0, r.x1);
                optionVolume[r.id - 2000] = std::clamp(int(std::lround((x - a) / std::max(b - a, 1e-4f) * 20.0f)) * 5, 0, 100);
                optionFocus = r.id - 2000;
                volumeSet(optionFocus);
            }
            return true;
        }
        return false;
    }
    void battleKey(SDL_Keycode k) {
        if (gameOver.stage == 8) { // (the question: Y / Return yes, N / Backspace no)
            if (k == SDLK_Y || k == SDLK_RETURN || k == SDLK_KP_ENTER) gameOverAnswer(true);
            else if (k == SDLK_N || k == SDLK_BACKSPACE) gameOverAnswer(false);
            return;
        }
        if (!choosing) return;
        keysUsed = true;
        const std::vector<int> c = menuChoices();
        const bool target = commandInput.menu() == cr2::CommandInput::Menu::Target;
        // (the awakenings' list goes up and down)
        if (optionsKey(k)) return;
        if (k == SDLK_BACKSPACE) commandInput.back(), audio.playSe(0xb5, false), menuChanged(); // (InterfaceMain: 0xb5 back)
        else if (c.empty()) return;
        else if (k == (target ? SDLK_RIGHT : SDLK_DOWN)) menuCursor = (menuCursor + 1) % int(c.size());
        else if (k == (target ? SDLK_LEFT : SDLK_UP)) menuCursor = (menuCursor + int(c.size()) - 1) % int(c.size());
        else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) choose(c[size_t(std::clamp(menuCursor, 0, int(c.size()) - 1))]);
    }
    // a click at the screen's (fx, fy) - 0..1 across, down: a button's tap area (UI units)
    void battleClick(float fx, float fy) {
        if (gameOver.stage == 8) { // (the question's Yes / No)
            const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
            const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
            for (const TapRect& r : tapRects)
                if ((r.id == 7701 || r.id == 7702) && x >= std::min(r.x0, r.x1) && x <= std::max(r.x0, r.x1) && y >= std::min(r.y0, r.y1) &&
                    y <= std::max(r.y0, r.y1))
                    return gameOverAnswer(r.id == 7701);
            return;
        }
        if (!choosing) return;
        if (manualStage == 2) { // (a tap: the tutorial's page)
            if (manualEvents) manualEvents->advance();
            return;
        }
        const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
        const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
        for (const TapRect& r : tapRects)
            if (x >= std::min(r.x0, r.x1) && x <= std::max(r.x0, r.x1) && y >= std::min(r.y0, r.y1) && y <= std::max(r.y0, r.y1)) {
                if (optionsTapped(r, x)) return;
                if (r.id == 100) commandInput.back(), audio.playSe(0xb5, false), menuChanged();
                else if (r.id >= 1000) { // (another row: the focus to it - ScrollListMenu)
                    const std::vector<int> c = menuChoices();
                    const size_t row = size_t(r.id - 1000);
                    if (row < listRows.size()) {
                        const auto it = std::find(c.begin(), c.end(), listRows[row].id);
                        if (it != c.end()) menuCursor = int(it - c.begin());
                    }
                } else if (r.id >= 0) choose(r.id);
                return;
            }
    }
    BattleStep battleStep = BattleStep::Over;
    // The explorer's turn (--explore), through the menus as a player goes: a pair while both stand
    // above half their HP, else solo; for each, one of them down revived (a sopia, else an item),
    // one under 40% healed, else an awakening the charge covers, else the sopia that strikes
    // hardest for its MP (keeping a third back for healing) where it beats the attack - on the
    // weakest foe
    bool exploreOrders(bool& pair, std::vector<cr2::BattleRules::Order>& out) {
        const cr2::BattleRules& r = *battleRules;
        cr2::CommandInput in;
        const cr2::BattleData::Coupling* cp = battleData.coupling(battle.coupling);
        in.begin(r, cp && cp->flag, [this](int f) { return game.flag(f); },
                 [this](int s) { return s >= 0 && s < 32 ? int(game.sopiaLevel[s]) : 0; });
        if (in.menu() != cr2::CommandInput::Menu::First) return false;
        auto share = [](const cr2::Fighter& f) { return f.maxHp > 0 ? float(f.hp) / float(f.maxHp) : 0.0f; };
        pair = r.fighter(0).alive && r.fighter(1).alive && share(r.fighter(0)) > 0.5f && share(r.fighter(1)) > 0.5f;
        if (!in.press(pair ? cr2::CommandInput::Pair : cr2::CommandInput::Solo)) {
            pair = !pair;
            if (!in.press(pair ? cr2::CommandInput::Pair : cr2::CommandInput::Solo)) return false;
        }
        int weakest = -1;
        for (int m = 2; m < 5; ++m)
            if (r.fighter(m).alive && (weakest < 0 || r.fighter(m).hp < r.fighter(weakest).hp)) weakest = m;
        int foes = 0;
        for (int m = 2; m < 5; ++m) foes += r.fighter(m).alive;
        for (int guard = 0; guard < 12 && in.menu() != cr2::CommandInput::Menu::Done; ++guard) {
            if (in.menu() == cr2::CommandInput::Menu::Target) {
                const std::vector<int>& t = in.targets();
                if (t.empty()) return false;
                int pick = t[0];
                for (int f : t) {
                    const cr2::Fighter& tf = r.fighter(f);
                    if (tf.side == 1 && f == weakest) pick = f;
                    if (tf.side == 0 && share(tf) < share(r.fighter(pick))) pick = f;
                }
                if (!in.pick(pick)) return false;
                continue;
            }
            if (in.menu() != cr2::CommandInput::Menu::Command) return false;
            const cr2::Fighter& me = r.fighter(in.actor());
            int low = -1;
            for (int k = 0; k < 2; ++k)
                if (r.fighter(k).present && (low < 0 || !r.fighter(k).alive || share(r.fighter(k)) < share(r.fighter(low))) && (low < 0 || r.fighter(low).alive)) low = k;
            const bool down = low >= 0 && !r.fighter(low).alive, hurt = low >= 0 && r.fighter(low).alive && share(r.fighter(low)) < 0.4f;
            auto kindOf = [&](int command) { const cr2::BattleData::Command* c = battleData.command(command); return c ? c->kind : -1; };
            bool done = false;
            if (down || hurt) { // (a sopia, else an item)
                for (const cr2::CommandInput::SopiaCommand& sc : in.sopiaCommands()) {
                    const int k = kindOf(sc.command);
                    if (sc.selectable && (down ? (k == 24 || k == 25) : (k >= 18 && k <= 23)) && in.press(cr2::CommandInput::Sopia) && in.pickSopia(sc.command)) {
                        done = true;
                        break;
                    }
                    if (in.menu() == cr2::CommandInput::Menu::Sopia) in.back();
                }
                if (!done)
                    for (const cr2::CommandInput::ItemEntry& it : in.items()) {
                        const int k = kindOf(it.command);
                        const cr2::BattleData::Item* item = battleData.item(it.item);
                        if (it.selectable && (down ? (k == 24 || k == 25) : (item && item->hp > 0 && k != 24 && k != 25)) && in.press(cr2::CommandInput::Items) && in.pickItem(it.item)) {
                            done = true;
                            break;
                        }
                        if (in.menu() == cr2::CommandInput::Menu::Items) in.back();
                    }
            }
            if (!done)
                for (const cr2::CommandInput::Awakening& aw : in.awakenings())
                    if (aw.selectable && in.press(cr2::CommandInput::Awake) && in.pickAwakening(aw.command)) {
                        done = true;
                        break;
                    } else if (in.menu() == cr2::CommandInput::Menu::Awake) in.back();
            if (!done) { // (the hardest sopia strike, against the attack)
                auto strength = [&](int command) {
                    const cr2::BattleData::Command* c = battleData.command(command);
                    if (!c || !(c->kind == 0 || (c->kind >= 4 && c->kind <= 17) || c->kind == 27 || c->kind == 29 || c->kind == 32) || c->target < 4) return 0.0f;
                    return float(c->power) * float(c->target == 6 ? foes : 1) * float(c->magic ? me.param[1] : me.param[0]);
                };
                const float attack = strength(me.attack);
                int best = 0;
                float bestStrength = attack * 1.2f;
                for (const cr2::CommandInput::SopiaCommand& sc : in.sopiaCommands())
                    if (sc.selectable && me.mp - sc.mp >= me.maxMp / 3 && strength(sc.command) > bestStrength) best = sc.command, bestStrength = strength(sc.command);
                if (best && in.press(cr2::CommandInput::Sopia) && in.pickSopia(best)) done = true;
                else if (in.menu() == cr2::CommandInput::Menu::Sopia) in.back();
            }
            if (!done && !in.press(cr2::CommandInput::Attack)) return false;
        }
        if (in.menu() != cr2::CommandInput::Menu::Done || in.escape()) return false;
        out = in.orders();
        return !out.empty();
    }
    std::unique_ptr<cr2::BattleScript> battleScript;
    float battleScriptClock = 0;
    bool battleScriptOk = true;
    // one of the game's updates for the script (20 a second), and the exit's timer
    void updateBattleScript(float dt) {
        if (!battleRules) return;
        battleRules->runExitTimer(dt);
        if (!battleScript || !battleScriptOk) return;
        battleScript->events().language = optionLanguage; // (the options' language)
        battleScriptClock = std::min(battleScriptClock + dt, 0.25f);
        for (; battleScriptClock >= cr2::EventRuntime::kTick; battleScriptClock -= cr2::EventRuntime::kTick)
            if (!(battleScriptOk = battleScript->tick())) SDL_Log("battle script: %s", battleScript->error().c_str());
    }
    bool battleScriptRunning() const { return battleScript && battleScriptOk && battleScript->running(); }

    std::vector<std::pair<int, int>> stateArg; // (--state fighter:state)
    int chargeArg = -1;                        // (--charge: the gauge to begin with)
    int gaugeArg = 0;
    void startFight(int mode, uint32_t seed) {
        battleFight = mode;
        glassRng.seed(seed); // (the game's lrand48: seeded by the clock)
        battleRules = std::make_unique<cr2::BattleRules>(battleData, seed);
        partyDeaths = 0, lastPartyActor = 0;
        battleWipeAt = battleClock; // (StepBattleIn's FadeIn(3, 20): the wipe off the battle)
        std::string err;
        int party[2] = {0, 0};
        for (size_t k = 0; k < battle.party.size() && k < 2; ++k) party[k] = battle.party[k].id;
        if (!battleRules->start(battle.coupling, party, game.difficulty, &err, game.sopiaLevel, game.equip, game.level, game.mp, game.seed, game.itemCount)) {
            SDL_Log("fight: %s", err.c_str());
            battleRules.reset();
            return;
        }
        // its script: battle.nut's Battle_Init now (BtlMainCtrl::StepBattleIn), the encounter's own after
        const int chars[2] = {party[0] - 1, party[1] - 1};
        const int monsters[3] = {battleRules->fighter(2).id, battleRules->fighter(3).id, battleRules->fighter(4).id};
        battleScript = std::make_unique<cr2::BattleScript>(archive, game, *battleRules, battle.coupling, chars, monsters);
        battleScript->events().askYesNo = true;
        battleScriptClock = 0;
        battleScriptOk = battleScript->begin() && battleScript->runToEnd(100);
        if (!battleScriptOk) SDL_Log("battle script: %s", battleScript->error().c_str());
        else SDL_Log("battle script: %s%s", battleScript->name().c_str(), battleScript->own() ? " (its own)" : "");
        for (int id = 1; id < 300; ++id) battleRules->setItemCount(id, game.itemCount[id]); // (BtlWork +0x52)
        // the music (BtlMainCtrl::StepBattleIn: the encounter's +0 - but with story flag 0x7f7, the
        // script's own)
        voiceJobs.clear(), seJobs.clear(), pendingBgm = 0;
        if (!game.flag(0x7f7)) audio.playBgm(battle.music, 0);
        for (const auto& [f, id] : stateArg) // (--state: one use of it, tests)
            if (f >= 0 && f < 5) battleRules->fighter(f).states.push_back({id, 0, 1});
        if (chargeArg >= 0) battleRules->setCharge(chargeArg); // (--charge, tests)
        if (gaugeArg != 0) battleRules->setGauge(gaugeArg); // (--gauge, tests)
        battleRules->setSopiaOwned(game.sopiaOwned); // (the drops: a sopia not had yet)
        battleRules->script = [this](const std::string& f) {
            if (battleScript && battleScriptOk && battleScript->own() && !(battleScriptOk = battleScript->call(f)))
                SDL_Log("battle script: %s", battleScript->error().c_str());
        };
        battleStep = BattleStep::Opening;
        int u = 0;
        for (int k = 0; k < 2; ++k) fighterUnit[k] = party[k] > 0 ? u++ : -1;
        for (int k = 0; k < 3; ++k) fighterUnit[2 + k] = k < int(battle.monsters.size()) ? int(battle.party.size()) + k : -1;
        std::vector<uint8_t> d;
        if (archive.read("battleapos.skel", d) && cr2::parseSkeleton(d, battleApos)) battleAposWorld = battleApos.world(battleApos.basePose);
        showActions.clear(), showIndex = 0;
        // the opening: Cam_Start_<pattern>_00 (BtlMainCtrl::StepBattleIn), then the turns
        setBattleCamera(battle.startCamera);
        showEnd = battleClock + 3.0f;
        SDL_Log("fight: %s", mode == 2 ? "pair" : "solo");
    }

    // a battledpos joint's place (BtlModel::GetPosition(dpos, name))
    bool dposPosition(const char* name, float* out) {
        static cr2::Skeleton dpos;
        static std::vector<cr2::Mat34> w;
        if (w.empty()) {
            std::vector<uint8_t> d;
            if (!archive.read("battledpos.skel", d) || !cr2::parseSkeleton(d, dpos)) return false;
            w = dpos.world(dpos.basePose);
        }
        const int j = dpos.joint(cr2::nameHash(name));
        if (j < 0 || size_t(j) >= w.size()) return false;
        for (int c = 0; c < 3; ++c) out[c] = w[size_t(j)].m[c * 4 + 3];
        return true;
    }

    // the party's places for its formation (BtlUtil::SetPairPosRot)
    void placeParty(bool pair) {
        cr2::Skeleton dpos;
        std::vector<uint8_t> d;
        if (!archive.read("battledpos.skel", d) || !cr2::parseSkeleton(d, dpos)) return;
        const std::vector<cr2::Mat34> w = dpos.world(dpos.basePose);
        for (int k = 0; k < 2; ++k) {
            const int u = fighterUnit[k];
            if (u < 0) continue;
            const bool alone = fighterUnit[1 - k] < 0;
            const std::string name = alone ? "dPos_pSoloC" : pair ? (k == 0 ? "dPos_pPairA" : "dPos_pPairB") : (k == 0 ? "dPos_pSoloA" : "dPos_pSoloB");
            const int j = dpos.joint(cr2::nameHash(name.c_str()));
            if (j < 0) continue;
            cr2::Mat34 m = w[size_t(j)];
            for (int c = 0; c < 3; ++c) {
                const float l = std::sqrt(m.m[c] * m.m[c] + m.m[4 + c] * m.m[4 + c] + m.m[8 + c] * m.m[8 + c]);
                if (l > 0) m.m[c] /= l, m.m[4 + c] /= l, m.m[8 + c] /= l;
            }
            BattleUnit& bu = battleUnits[size_t(u)];
            bu.home = bu.fig.place = m, bu.position = name;
        }
    }

    // the fight's next step once the last is over and the script's call is
    void stepBattleShow() {
        if (!battleRules || battleFight == 0) return;
        if (battleScriptRunning() || battleClock < showEnd) return;
        if (battleRules->exitTimer() >= 0) return; // (its exit coming: before a win or a loss is shown)
        if (battleStep != BattleStep::Over && (battleRules->exited() || (battleRules->outcome() && battleStep != BattleStep::ActionEnd))) {
            if (!battleRules->exited() && battleRules->outcome() == 1) {
                battleRules->script("Battle_End"); // (BtlResult)
                beginResult();
                // (its music faded over 20 updates, then the fanfare - 15 - 1.1 s on, unless the
                // encounter's +0x15 is 3; the victory voice, kind 22)
                const cr2::BattleData::Coupling* cp = battleData.coupling(battle.coupling);
                if (!cp || cp->resultKind != 3) audio.stopBgm(20), pendingBgm = 15, pendingBgmAt = battleClock + 1.1f;
                // (PlayVoiceVictory, after FixTodome: the last of the party to act - else the other
                // standing - says the coupling's victory voice (+0x1a), else 22 after a hard one - one of
                // the party down at some time, or alone under 25% HP -, else 21; 24 / 23 the same in an
                // event's battle (BtlWork +0x18))
                const int sp = battleRules->fighter(lastPartyActor).alive ? lastPartyActor : 1 - lastPartyActor;
                if (battleRules->fighter(sp).alive) {
                    const cr2::Fighter& f0 = battleRules->fighter(0);
                    const bool hard = partyDeaths > 0 || (!battleRules->fighter(1).present && f0.hp * 100 / std::max(f0.maxHp, 1) < 25);
                    const bool event = cp && cp->flag;
                    if (cp && cp->victoryVoice > 0) battleVoice(sp, cp->victoryVoice, 1.2f, true);
                    else battleVoice(sp, event ? (hard ? 24 : 23) : (hard ? 22 : 21), 1.2f);
                }
            } else if (!battleRules->exited() && battleRules->outcome() == 2) { // (BtlGameover: 18)
                audio.stopBgm(20), pendingBgm = 18, pendingBgmAt = battleClock + 1.1f;
                beginGameOver();
            }
            for (int id = 1; id < 300; ++id) game.itemCount[id] = static_cast<short>(battleRules->itemCount(id)); // (the items used)
            for (int k = 0; k < 2; ++k) { // (the MP kept - the most with RestoreMp, skill 3, 5, standing; the HP back: Cr3RestorePlayerHP)
                const cr2::Fighter& f = battleRules->fighter(k);
                if (f.present && f.id >= 1 && f.id <= 9) game.mp[f.id - 1] = f.alive && f.has(3, 5) ? -1 : f.mp;
            }
            battleStep = BattleStep::Over;
            showIndex = showActions.size();
            setBattleCamera(battle.camera);
            return;
        }
        switch (battleStep) {
        case BattleStep::Opening: // (the opening over: StepBattleIn's Battle_Start)
            battleStep = BattleStep::TurnBegin;
            // (StepBattleIn: the start's announcement - the leader's name; 91 a party of one)
            if (battleRules->advantage() == 1) {
                const bool solo = !battleRules->fighter(1).present;
                announceQueue.push_back(formatAnnounce(announce(solo ? 91 : 1), {fighterName(0)}));
            } else if (battleRules->advantage() == 2) {
                announceQueue.push_back(announce(2));
            }
            if (battleRules->advantage()) SDL_Log("start: %s", battleRules->advantage() == 1 ? "the party first" : "the monsters' assault");
            battleRules->script("Battle_Start");
            // (BtlSound::PlayVoiceBattleIn, with flag 0xbeb: the coupling's own voice, else 19 the party
            // first, 20 the assault, else by the means of the levels - the monsters' over the party's
            // by more than 4, 17; under by more than 4, 18; else 16 -, said by one standing at random)
            if (game.flag(0xbeb)) {
                const cr2::BattleData::Coupling* cp = battleData.coupling(battle.coupling);
                int party = 0, np = 0, mons = 0, nm = 0;
                for (int f = 0; f < 5; ++f) {
                    const cr2::Fighter& x = battleRules->fighter(f);
                    if (!x.present) continue;
                    (f < 2 ? party : mons) += x.level, ++(f < 2 ? np : nm);
                }
                party /= std::max(np, 1), mons /= std::max(nm, 1);
                const int kind = battleRules->advantage() == 1 ? 19 : battleRules->advantage() == 2 ? 20 : mons > party + 4 ? 17 : mons < party - 4 ? 18 : 16;
                cp && cp->voice > 0 ? partyVoice(cp->voice, true) : partyVoice(kind);
            }
            break;
        case BattleStep::TurnBegin:
            // (StepBattleIn: the start's announcement up its time before the first turn)
            if (battleRules->turn() == 0 && battleRules->advantage() && (!announceQueue.empty() || battleClock - announceAt < 1.5f)) break;
            battleRules->beginTurn();
            battleStep = BattleStep::TurnChoose;
            // (BtlCommand::StepInit: an armoured monster standing - its +0x48 bit 0 - the first time,
            // flag 54)
            for (int m = 2; m < 5; ++m) {
                const cr2::Fighter& f = battleRules->fighter(m);
                if (f.alive && f.monster && (f.monster->skills & 1u)) {
                    battleTutorial("Tutorial_Armor", 54);
                    break;
                }
            }
            break;
        case BattleStep::TurnChoose: { // the party attacking the first monster standing
            if (battleRules->turn() == 0 && battleRules->advantage() == 2) { // (the assault: no menus - StepBattleIn's step 2)
                battleRules->chooseTurn(false, {});
                SDL_Log("turn 1 (the monsters' assault)");
                battleStep = BattleStep::ActionStart;
                break;
            }
            if (manualCommands && !battleRules->scriptTurnReserved()) {
                if (!choosing) {
                    commandInput.begin(*battleRules, battleData.coupling(battle.coupling) && battleData.coupling(battle.coupling)->flag,
                                       [this](int f) { return game.flag(f); },
                                       [this](int s) { return s >= 0 && s < 32 ? int(game.sopiaLevel[s]) : 0; });
                    choosing = true;
                    battleSe(1); // (BtlCommand::StepGetsetFirst)
                    placeParty(false); // (the choosing in the solo places: BtlCommand's)
                    menuChanged();
                    inputNext = battleClock + 0.4f;
                }
                if (!inputScript.empty() && battleClock >= inputNext && manualStage != 2) { // (a tutorial over first)
                    inputToken(inputScript.front());
                    inputScript.pop_front();
                    inputNext = battleClock + 0.4f;
                }
                updateGuards();
                if (commandInput.menu() == cr2::CommandInput::Menu::Target) { // (the focused target's camera)
                    const std::vector<int> c = menuChoices();
                    if (!c.empty()) setTargetCamera(c[size_t(std::clamp(menuCursor, 0, int(c.size()) - 1))]);
                }
                if (commandInput.menu() != cr2::CommandInput::Menu::Done) break;
                choosing = false;
                if (commandInput.escape()) { // (BtlCommand::StepEscapeLoop: away after 2 s, else the turn the monsters')
                    const bool away = battleRules->tryEscape();
                    SDL_Log("escape: %s", away ? "got away" : "failed");
                    // (StepEscapeLoop: announcement 4 / 5, PlayVoiceEscapeSucceeded / Failed - voices 1 / 2)
                    announceQueue.clear(), commandShown = announce(away ? 4 : 5), commandShownStart = announceAt = battleClock;
                    partyVoice(away ? 1 : 2);
                    showEnd = battleClock + (away ? 2.0f : 1.0f);
                    if (away) {
                        battleStep = BattleStep::Over;
                        setBattleCamera(battle.camera);
                        break;
                    }
                }
                battleRules->chooseTurn(commandInput.pairTurn(), commandInput.orders());
                placeParty(battleRules->fighter(0).partner >= 0);
                SDL_Log("turn %d (%s)", battleRules->turn() + 1, commandInput.pairTurn() ? "pair" : "solo");
                battleStep = BattleStep::ActionStart;
                break;
            }
            bool pair = battleFight == 2;
            std::vector<cr2::BattleRules::Order> orders;
            if (ex.on && exploreOrders(pair, orders)) { // (the explorer's: as a careful player chooses)
                battleRules->chooseTurn(pair, orders);
                placeParty(battleRules->fighter(0).partner >= 0);
                SDL_Log("turn %d (%s)", battleRules->turn() + 1, pair ? "pair" : "solo");
                battleStep = BattleStep::ActionStart;
                break;
            }
            pair = battleFight == 2, orders.clear();
            for (int k = 0; k < 2; ++k) {
                const cr2::Fighter& f = battleRules->fighter(k);
                if (!f.alive) continue;
                cr2::BattleRules::Order o;
                o.actor = k, o.command = f.attack;
                for (int m = 2; m < 5; ++m)
                    if (battleRules->fighter(m).alive) {
                        o.target = m;
                        break;
                    }
                orders.push_back(o);
            }
            battleRules->chooseTurn(pair, orders);
            placeParty(battleRules->fighter(0).partner >= 0);
            SDL_Log("turn %d", battleRules->turn() + 1);
            battleStep = BattleStep::ActionStart;
            break;
        }
        case BattleStep::ActionStart:
            if (!battleRules->actionsLeft()) {
                const int breakBefore = battleRules->breakState();
                battleRules->endTurn();
                if (breakBefore == 3 && (battleRules->breakState() == 1 || battleRules->breakState() == 2)) {
                    battleSe(2);
                    seJobs.push_back({battleClock + 1.0f, cr2::SoundData::battleSe(battleRules->breakState() == 1 ? 3 : 4)});
                    breakShown = battleRules->breakState(), breakShownAt = battleClock;
                    SDL_Log("turn break: %s (%.1f s)", breakShown == 1 ? "the party's" : "the monsters'", battleClock);
                }
                for (int f = 0; f < 2; ++f) setGuard(f, false), dropGuardObject(f); // (BtlTurnEnd: DeleteAll; the stance back)
                if (announceQueue.empty()) commandShown.clear(); // (BtlTurnEnd: ClearAnnounce)
                for (const cr2::BattleRules::Event& e : battleRules->turnEndEvents()) { // (BtlTurnEnd: the poison's, the pools')
                    if (e.damage > 0) showNumber(e.target, e.damage, 20), battleSe(0x13); // (BtlTurnEnd: the poison's SE)
                    if (e.heal > 0) showNumber(e.target, e.heal, 50);
                    if (e.healMp > 0) showNumber(e.target, e.healMp, 60);
                    SDL_Log("    %s: %d poison, %d HP back, %d MP back", battleRules->fighter(e.target).name.c_str(), e.damage, e.heal, e.healMp);
                }
                if (!battleRules->turnEndEvents().empty()) showEnd = battleClock + 1.2f;
                SDL_Log("turn over; gauge %d, charge %d; MP %d %d", battleRules->gauge(), battleRules->charge(), battleRules->fighter(0).mp, battleRules->fighter(1).mp);
                battleStep = BattleStep::TurnBegin;
                break;
            }
            battleRules->startAction();
            battleStep = BattleStep::ActionDo;
            break;
        case BattleStep::ActionDo: {
            hudBefore.hp.clear();
            for (int k = 0; k < 5; ++k) hudBefore.hp.push_back(battleRules->fighter(k).hp);
            hudBefore.gauge = battleRules->gauge(), hudBefore.charge = battleRules->charge();
            std::vector<cr2::BattleRules::Event> events;
            battleRules->doAction(events);
            showActions.clear(), showIndex = 0;
            for (const auto& e : events) {
                if (showActions.empty()) showActions.push_back({e.actor, e.partner, e.command, {}});
                showActions.back().hits.push_back(e);
                if (e.regenerated) regenPending.push_back(e.target);
            }
            battleStep = BattleStep::ActionEnd;
            if (!showActions.empty()) beginShowAction(showActions[0]);
            break;
        }
        case BattleStep::ActionEnd:
            showIndex = showActions.size();
            for (int f = 0; f < 2; ++f) // (back in its stance: ShowAttachObject - made again)
                if (guardReturn[f] && guardObjects[f] && battleRules->fighter(f).alive) guardObject(f, 0.5f), guardReturn[f] = false;
            if (stepAnalyze()) break;
            if (stepDeaths()) break;
            if (!regenPending.empty() || battleScriptRunning()) { // (a revival, a tutorial first)
            } else if (!showActions.empty()) {
                // (ActionCommonEventCommandEnd: a command lending an element - +0x70 / +0x71 1..3 -
                // the first time, flag 53 (CheckTutoScript_Element); else one standing with a state
                // shown, flag 55 (CheckTutoScript_Status))
                const cr2::BattleData::Command* c = battleData.command(showActions.front().command);
                const bool element = c && ((c->attach >= 1 && c->attach <= 3) || (c->attachSelf >= 1 && c->attachSelf <= 3));
                bool status = false;
                static const int kShown[] = {0, 1, 2, 12, 35, 30, 29, 33, 5, 6, 7, 8, 9, 13, 14, 15, 16, 17, 21, 11, 32};
                for (int f = 0; f < 5 && !status; ++f)
                    if (battleRules->fighter(f).alive)
                        for (int st : kShown) status = status || battleRules->fighter(f).hasState(st);
                if (element && !game.flag(53)) battleTutorial("Tutorial_Element", 53);
                else if (status) battleTutorial("Tutorial_Status", 55);
                if (battleScriptRunning()) break;
            }
            if (!regenPending.empty()) { // (the downed with state 33 back first)
                showRegen();
                break;
            }
            battleRules->endAction();
            analyze.stage = 0, deathsQueued = false;
            battleStep = BattleStep::ActionStart;
            break;
        case BattleStep::Over: break;
        }
    }

    // the HUD's as the show has it: until an action's hits land, the HP, the break gauge and the
    // charge before it (the windows change with the hit, as the reference video's)
    struct HudValues {
        std::vector<int> hp;
        int gauge = 0, charge = 0;
    } hudBefore;
    bool hudHeld() const { return showIndex < showActions.size() && !showHitDone && hudBefore.hp.size() == 5; }
    int shownHp(int f) const {
        if (std::find(regenPending.begin(), regenPending.end(), f) != regenPending.end()) return 0; // (down until its revival shows)
        return hudHeld() ? hudBefore.hp[size_t(f)] : battleRules->fighter(f).hp;
    }
    // state 33's revivals still to show, in turn (BtlActionCtrl: ActionReviveStandby - the
    // announcement 0x5d, "Regenerate" -, ActionReviveCamera - Cam_Unit_PCrevival_00 at the one
    // revived -, ActionReviveMain - its motion 8 (bh01), its HP back in green at DummyDP01, eff216 -,
    // ActionReviveWait - the motion's end)
    std::deque<int> regenPending;
    bool regenStarted = false;
    void showRegen() {
        const int f = regenPending.front();
        const int u = fighterUnit[f];
        if (!regenStarted) {
            regenStarted = true;
            announceQueue.clear();
            commandShown = announce(0x5d), commandShownStart = announceAt = battleClock;
            SDL_Log("    announce: %s", commandShown.c_str());
            const char* cam = "Cam_Unit_PCrevival_00";
            bool found = false;
            for (const CameraFigure& c : battleCameras)
                for (const cr2::GeomCamera& gc : c.geom.cameras) found = found || gc.hash == cr2::nameHash(cam);
            if (found && u >= 0) {
                cr2::Mat34 at; // (SetPosition: the camera's frame moved there, not turned)
                for (int r = 0; r < 3; ++r) at.m[r * 4 + 3] = battleUnits[size_t(u)].home.m[r * 4 + 3];
                setBattleCamera(cam, at);
            }
            showEnd = battleClock + 0.2f; // (the camera's cut drawn before the number is placed by it)
            return;
        }
        regenStarted = false;
        regenPending.pop_front();
        if (u < 0) return;
        BattleUnit& bu = battleUnits[size_t(u)];
        bu.hidden = false;
        playMotion(bu, "bh01", battleClock);
        showNumber(f, battleRules->fighter(f).hp, 50);
        float at[3] = {bu.home.m[3], bu.home.m[7] + 0.15f, bu.home.m[11]};
        const int dj = bu.fig.skeleton.joint(cr2::nameHash("DummyDP01"));
        if (dj >= 0 && !bu.fig.posed.empty()) {
            const std::vector<cr2::Mat34> w = bu.fig.skeleton.world(bu.fig.posed, bu.fig.place);
            if (size_t(dj) < w.size()) at[0] = w[size_t(dj)].m[3], at[1] = w[size_t(dj)].m[7], at[2] = w[size_t(dj)].m[11];
        }
        spawnEffect("eff216", at);
        battleVoice(f, 10), battleSe(0x8b); // (ActionReviveMain: PlayVoiceRevive, SE 0x8b)
        SDL_Log("    %s revived: %d HP", battleRules->fighter(f).name.c_str(), battleRules->fighter(f).hp);
        showEnd = battleClock + std::max(motionLength(bu, "bh01"), 1.0f) + 0.3f;
    }
    int shownGauge() const { return hudHeld() ? hudBefore.gauge : battleRules->gauge(); }
    int shownCharge() const { return hudHeld() ? hudBefore.charge : battleRules->charge(); }
    // BtlResult, a battle won: a moment (Battle_End, the music fading: 1.2 s), then "Victory!"
    // (announce 6) with the party standing in its victory motion (bb03) under Cam_Win_*, a tap
    // (or 5 s), then the result window (ResultMenu) - over 60 frames the EXP goes in and the HP
    // back (ProcessLvup; a tap: at once) -, then a tap (or 5 s): its messages in the window, a tap
    // (or 3 s) each (ProcessLvup1 / 2: the levels gained; ProcessResult: CheckSpUp - the sopia's
    // SP, what they learn -, CheckOz, CheckDropItem, CheckDropSopia), and the battle is over
    struct ResultMember {
        int ch = -1, id = 0, level = 1, exp0 = 0, gain = 0, hp0 = 0, hpMax = 1, mp = 0, mpMax = 0;
    };
    struct BattleResultShow {
        int stage = 0; // 0 none, 1 the moment, 2 victory, 3 the window, 4 over
        float start = 0, windowStart = 0, transferStart = 0, transferEnd = 0;
        bool tapped = false;
        int exp = 0, sp = 0, oz = 0;
        ResultMember member[2];
        std::vector<std::string> messages; // (once the EXP is in)
        std::vector<int> messageSe;        // (each one's SE as it shows: BtlResult::Update's by its kind)
        int message = -1;                  // (the one shown; -1 "Victory!")
        float messageAt = 0;
        int loopSe = -1, levelShown[2] = {0, 0}; // (the transfer's SE 0xc5, looping; the levels chimed for)
        bool counting = false, counted = false;
    } result;
    static constexpr float kTransfer = 2.0f; // (ProcessLvup's 60 frames at 30 a second)
    // the battle's announcements (BtlAnnounceData, message\announcebattle.dat: u32 the records'
    // offset, the texts', the count; a record 0x20 bytes - eight languages' text offsets)
    // - and the names', the same: message\\charaname, sopianame, commandname, autoskillname,
    // itemname (by number, from 1)
    std::map<std::string, std::vector<uint8_t>> messageFiles;
    std::string announce(int id) { return messageText("announcebattle", id); }
    // the names and help in the option's language (Cr3UtilGetItemName / ItemHelpText, GetCommandName /
    // HelpText, GetSopiaName / HelpText, GetAutoSkillHelpText; GameMain's CharaName, MonsterName):
    // message\itemname, itemhelp, commandname, commandhelp, sopianame, sopiahelp, autoskillhelp,
    // charaname, monstername by number - battledata's English where one has none
    std::string localText(const char* file, int id, const std::string& fallback) {
        std::string t = messageText(file, id);
        return t.empty() ? fallback : t;
    }
    std::string itemName(int id) { const cr2::BattleData::Item* it = battleData.item(id); return localText("itemname", id, it ? it->name[1] : ""); }
    std::string itemHelp(int id) { const cr2::BattleData::Item* it = battleData.item(id); return localText("itemhelp", id, it ? it->text[1] : ""); }
    std::string commandName(int id) { const cr2::BattleData::Command* c = battleData.command(id); return localText("commandname", id, c ? c->name[1] : ""); }
    std::string commandHelp(int id) { const cr2::BattleData::Command* c = battleData.command(id); return localText("commandhelp", id, c ? c->text[1] : ""); }
    std::string sopiaName(int id) { const cr2::BattleData::Sopia* so = battleData.sopia(id); return localText("sopianame", id, so ? so->name[1] : ""); }
    std::string sopiaHelp(int id) { return messageText("sopiahelp", id); }
    std::string autoSkillHelp(int id) { return messageText("autoskillhelp", id); }
    std::string fighterName(int i) {
        const cr2::Fighter& f = battleRules->fighter(i);
        return localText(f.side == 0 ? "charaname" : "monstername", f.id, f.name);
    }
    std::string messageText(const std::string& file, int id) {
        std::vector<uint8_t>& d = messageFiles[file];
        if (d.empty() && !archive.read("message\\" + file + ".dat", d)) return {};
        auto u32 = [&](size_t o) { return o + 4 <= d.size() ? uint32_t(d[o] | d[o + 1] << 8 | d[o + 2] << 16 | uint32_t(d[o + 3]) << 24) : 0u; };
        if (id < 1 || uint32_t(id) > u32(8)) return {};
        const size_t o = size_t(u32(4)) + u32(size_t(u32(0)) + size_t(id - 1) * 0x20 + 4u * size_t(std::clamp(optionLanguage, 0, 7))); // (the option's language: English the second)
        std::string s;
        for (size_t k = o; k < d.size() && d[k]; ++k) s += char(d[k]);
        return s;
    }
    // BtlAnnounce::SetAnnounce's %s / %d, in turn
    static std::string formatAnnounce(std::string t, const std::vector<std::string>& args) {
        size_t a = 0;
        for (size_t p = 0; (p = t.find('%', p)) != std::string::npos;) {
            if (p + 1 < t.size() && (t[p + 1] == 's' || t[p + 1] == 'd')) {
                const std::string v = a < args.size() ? args[a++] : "";
                t.replace(p, 2, v);
                p += v.size();
            } else ++p;
        }
        return t;
    }
    // ProcessLvup1 / 2, ProcessResult: the result's messages - and the party's state after it: the
    // levels and EXP, the sopia's SP and levels, the Oz, the items and sopia left
    void finishResult() {
        std::vector<std::string>& out = result.messages;
        for (const ResultMember& r : result.member) {
            if (r.ch < 0 || r.ch >= 9) continue;
            const ResultNow n = resultNow(r);
            const int up = n.level - r.level;
            const std::string name = messageText("charaname", r.id);
            if (up == 1) out.push_back(formatAnnounce(announce(0x11), {name})), result.messageSe.push_back(0xc3); // (ProcessLvup1 / 2: SE 0xc3)
            else if (up > 1) out.push_back(formatAnnounce(announce(0x12), {name, std::to_string(up)})), result.messageSe.push_back(0xc3);
            game.level[r.ch] = n.level, game.exp[r.ch] = n.exp;
        }
        // CheckSpUp: each sopia the party carries (once), its SP, a level each time it passes its
        // table's next (5 at most) - each level's command (or auto skill) learned
        if (result.sp > 0) {
            std::vector<int> seen;
            for (const ResultMember& r : result.member)
                for (int k = 3; r.ch >= 0 && r.ch < 9 && k < 8; ++k) {
                    const int s = game.equip[r.ch][k];
                    if (s <= 0 || s >= 32 || std::find(seen.begin(), seen.end(), s) != seen.end()) continue;
                    seen.push_back(s);
                    const cr2::BattleData::Sopia* so = battleData.sopia(s);
                    if (!so) continue;
                    const int from = std::max(int(game.sopiaLevel[s]), 1);
                    int level = from;
                    game.sopiaExp[s] = std::min(game.sopiaExp[s] + result.sp, 99999999);
                    while (level < 5 && game.sopiaExp[s] >= cr2::sopiaExp(so->growth != 0, level + 1)) ++level;
                    for (int l = from; l < level; ++l) {
                        const int cmd = so->commands[l];
                        out.push_back(formatAnnounce(announce(cmd > 0 ? 0x16 : 0x17),
                                                     {messageText("sopianame", s), cmd > 0 ? messageText("commandname", cmd) : messageText("autoskillname", so->skills[l])}));
                        result.messageSe.push_back(0xc4); // (CheckSpUp's kind 1)
                    }
                    game.sopiaLevel[s] = static_cast<signed char>(level);
                }
        }
        if (result.oz > 0) out.push_back(formatAnnounce(announce(0xf), {std::to_string(result.oz)})), result.messageSe.push_back(0xc1); // (CheckOz: 2)
        game.oz = std::min(game.oz + result.oz, 99999999);
        for (int id = 1; id < 300; ++id) {
            const int n = battleRules->dropped(id);
            if (n <= 0) continue;
            out.push_back(n == 1 ? formatAnnounce(announce(0x14), {messageText("itemname", id)})
                                 : formatAnnounce(announce(0x15), {messageText("itemname", id), std::to_string(n)}));
            result.messageSe.push_back(0xc1); // (CheckDropItem: 3)
            game.itemCount[id] = static_cast<short>(std::min(game.itemCount[id] + n, 99));
        }
        for (int s = 1; s < 32; ++s)
            if (battleRules->sopiaDropped(s)) { // (Cr3UtilGetSopia: had, at level 1)
                out.push_back(formatAnnounce(announce(0x13), {messageText("sopianame", s)}));
                result.messageSe.push_back(0xc2); // (CheckDropSopia: 4)
                game.sopiaOwned |= 1u << s, game.sopiaLevel[s] = 1, game.sopiaExp[s] = 0;
            }
        for (const std::string& s : out) SDL_Log("result: %s", s.c_str());
    }
    bool announceHeld = false; // (the banner kept: the victory's)

    // The downed, after the command (BtlActionCtrl's dead actions, ActionDeadStart ..): one of
    // the party its motion 7 (bd02), kept, waited for (ActionDeadWaitDeadMotion); a monster with
    // its +0x3a its size's defeat camera and bd02, waited for (ActionDeadWaitBossMotion: 5 s at
    // most), then - unless its +0x38 - eff202 above it (its figure's height,
    // GetEffectDeadOffsetByModelName), fading 1 -> 0 over 1 s after 0.25 s and gone, SE 0x66, then
    // 0.95 s; any other at once - unless its +0x38 - fading over 0.5 s and gone, eff201 above it,
    // SE 0x65, not waited for
    struct Death {
        int f = -1, stage = 0;
        float start = 0;
    };
    std::vector<Death> deaths;
    bool deathsQueued = false;
    void deathEffect(const char* name, int f, BattleUnit& u) {
        commandEffect(name, f, false);
        if (commandEffects.empty()) return;
        SceneEffect& fx = commandEffects.back();
        fx.follows = -1;
        fx.place = u.home;
        unscaled(fx.place);
        fx.place.m[7] += cr2::deadOffset(u.figure);
    }
    bool stepDeaths() {
        if (!deathsQueued) {
            deathsQueued = true;
            deaths.clear();
            for (const ShowAction& a : showActions)
                for (const auto& e : a.hits)
                    if (e.defeated && !e.regenerated && std::none_of(deaths.begin(), deaths.end(), [&](const Death& d) { return d.f == e.target; }))
                        deaths.push_back({e.target});
        }
        bool wait = false;
        for (Death& d : deaths) {
            BattleUnit* u = unitOf(d.f);
            const cr2::BattleData::Monster* m = d.f >= 2 ? battleRules->fighter(d.f).monster : nullptr;
            if (!u || d.stage >= 9) continue;
            if (d.stage == 0) {
                d.start = battleClock;
                if (d.f < 2) dropGuardObject(d.f), u->guarding = false; // (ActionDeadStart: its guard gone)
                if (d.f < 2 || (m && m->deathMotion)) {
                    playMotion(*u, "bd02", battleClock), u->hold = true;
                    d.stage = d.f < 2 ? 3 : 1;
                    if (m) { // (the defeat camera: by its size, the size pattern)
                        const char* cam = m->size == 2 ? "Cam_defeat_L_00" : battle.pattern == 12 ? "Cam_defeat_neron_00"
                                        : m->size == 3 ? "Cam_defeat_XL_00" : battle.pattern == 11 ? "Cam_defeat_Hpair_00"
                                        : m->size == 4 ? "Cam_defeat_Hsolo_00" : nullptr;
                        if (cam) {
                            setBattleCamera(cam);
                            if (m->size == 4 && battle.pattern != 11)
                                for (int c = 0; c < 3; ++c) battleCameraPlace.m[c * 4 + 3] = u->home.m[c * 4 + 3];
                        }
                    }
                } else {
                    if (!m || !m->deathPlain) {
                        u->alpha.begin(battleClock, 1, 0, 0.5f), u->hideAt = battleClock + 0.5f;
                        deathEffect("eff201", d.f, *u);
                    }
                    battleSe(0x65);
                    d.stage = 9;
                    continue;
                }
            }
            const float len = motionLength(*u, "bd02");
            if (d.stage == 3) { // (one of the party: its motion's end)
                if (battleClock - d.start < len) wait = true;
                else d.stage = 9;
                continue;
            }
            if (d.stage == 1) {
                if (battleClock - d.start < std::min(len, 5.0f)) {
                    wait = true;
                    continue;
                }
                if (!m->deathPlain) {
                    deathEffect("eff202", d.f, *u);
                    u->alpha.begin(battleClock + 0.25f, 1, 0, 1.0f), u->hideAt = battleClock + 1.25f;
                    battleSe(0x66);
                }
                d.stage = 2, d.start = battleClock;
            }
            if (d.stage == 2) {
                if (battleClock - d.start < 0.95f) wait = true;
                else d.stage = 9;
            }
        }
        return wait;
    }

    // Analyze (command kind 26; BtlActionCtrl::ActionSopiaWaitEffect -> ActionSopiaAnalyze): its
    // effect over, the one struck's "HP: %d / %d" on the banner until a tap, then its MonsterHelp
    // (the monster's number), until a tap - 5 s each for a fight nobody taps
    struct {
        int stage = 0; // 1 the HP, 2 the help, 3 done
        float start = 0;
        bool tapped = false;
    } analyze;
    bool stepAnalyze() {
        if (showActions.empty() || analyze.stage >= 3) return false;
        const cr2::BattleData::Command* c = battleData.command(showActions.front().command);
        const int target = showActions.front().hits.empty() ? -1 : showActions.front().hits.front().target;
        if (!c || c->kind != 26 || target < 0) return false;
        const bool tapped = analyze.tapped;
        analyze.tapped = false;
        const cr2::Fighter& t = battleRules->fighter(target);
        if (analyze.stage == 0) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "HP: %d / %d", t.hp, t.maxHp);
            announceQueue.clear();
            commandShown = buf, commandShownStart = announceAt = battleClock, announceHeld = true;
            analyze.stage = 1, analyze.start = battleClock;
            SDL_Log("    analyze: %s", buf);
        } else if (tapped || battleClock - analyze.start > 5.0f) {
            if (analyze.stage == 1 && t.monster) {
                commandShown = messageText("monsterhelp", t.id), commandShownStart = announceAt = battleClock;
                analyze.stage = 2, analyze.start = battleClock;
                SDL_Log("    analyze: the help (%zu bytes)", commandShown.size());
            } else {
                analyze.stage = 3, announceHeld = false, commandShown.clear();
                return false;
            }
        }
        return true;
    }
    void beginResult() {
        result = BattleResultShow();
        result.stage = 1, result.start = battleClock;
        result.exp = battleRules->expGained(), result.sp = battleRules->spGained(), result.oz = battleRules->ozGained();
        // CheckBonus: states 38 / 39 / 40 on either of the party double the EXP / SP / Oz (once,
        // both or one); ExpUp / SpUp / OzUp (skill 3, 0 / 1 / 2) of either +20 in 100
        auto either = [&](int st) { return battleRules->fighter(0).hasState(st) || battleRules->fighter(1).hasState(st); };
        if (either(38)) result.exp *= 2;
        if (either(39)) result.sp *= 2;
        if (either(40)) result.oz *= 2;
        auto bonus = [&](int bit, int& v) {
            for (int k = 0; k < 2; ++k)
                if (battleRules->fighter(k).present && battleRules->fighter(k).has(3, bit)) {
                    v += v * 20 / 100;
                    return;
                }
        };
        bonus(0, result.exp), bonus(1, result.sp), bonus(2, result.oz);
        for (int k = 0; k < 2; ++k) {
            const cr2::Fighter& f = battleRules->fighter(k);
            if (!f.present) continue;
            ResultMember& r = result.member[k];
            r.ch = f.id - 1, r.id = f.id;
            r.level = r.ch >= 0 && r.ch < 9 && game.level[r.ch] > 0 ? game.level[r.ch] : f.level;
            r.exp0 = r.ch >= 0 && r.ch < 9 ? game.exp[r.ch] : cr2::levelExp(r.level);
            r.gain = f.alive ? result.exp : result.exp * 50 / 100; // (CheckLvUp: the downed half)
            r.gain = std::clamp(r.gain, 0, std::max(cr2::levelExp(maxLevel()) - r.exp0, 0)); // (no more than the most level's EXP)
            r.hp0 = f.alive ? f.hp : 0, r.hpMax = f.maxHp, r.mp = f.mp, r.mpMax = f.maxMp;
        }
    }
    // Cr3UtilGetMaxCharaLevel: the most level - 100, 150 with flag 3038, 200 with flag 3039 (the
    // EXP gained stops at its level's: BtlResult::CheckLvUp; the windows' next none at it)
    int maxLevel() const { return game.flag(3039) ? 200 : game.flag(3038) ? 150 : 100; }
    // a member's EXP, level, next and HP as the window has them now
    struct ResultNow {
        int exp, level, next, percent, hp;
    };
    ResultNow resultNow(const ResultMember& r) const {
        const float p = result.stage >= 4 ? 1.0f : result.transferEnd > result.transferStart
                                                       ? std::clamp((battleClock - result.transferStart) / (result.transferEnd - result.transferStart), 0.0f, 1.0f)
                                                       : 0.0f;
        ResultNow n;
        n.exp = r.exp0 + int(float(r.gain) * p);
        n.level = std::max(r.level, 1);
        const int most = maxLevel();
        while (n.level < most && cr2::levelExp(n.level + 1) <= n.exp) ++n.level;
        const int lo = cr2::levelExp(n.level), hi = cr2::levelExp(n.level + 1);
        n.next = n.level >= most ? 0 : hi - n.exp;
        n.percent = n.level >= most || hi <= lo ? 0 : (n.exp - lo) * 100 / (hi - lo);
        n.hp = r.hp0 + int(float(r.hpMax - r.hp0) * p);
        return n;
    }
    // A battle lost (BtlGameover::Update; BtlMainCtrl::StepBattleOut): the announcements and
    // effects cleared, Cam_Lose_SoloPair_00 looping, the music faded over 20 updates and 18 1.1 s
    // on; the game over's figure (`dead`: BattleGameOverMenu) where it is one - an event battle
    // (+0x18), else once flag 0x7f9 is set - and the banner ("%s was defeated..." alone, 0x5c, else
    // "%s's team was defeated...", 7). A tap: no game over, or no continue data nor save - the end
    // (StepBattleOut: no game over - flag 0x7f8 set, back to the field -, else the title); else
    // faded out over 20 updates, the HUD and the party gone, faded in, 0.5 s on "Would you like to
    // try again?" (0x87) yes / no - yes the continue data, else the save, loaded (+0x1a 1: the
    // field); no the title
    struct GameOverState {
        int stage = 0; // 0 none, 1 the banner (a tap ends it), 5 the fade, 7 the wait, 8 the question
        float start = 0;
        bool over = false, tapped = false, hidden = false;
    } gameOver;
    void beginGameOver() {
        gameOver = GameOverState();
        gameOver.stage = 1, gameOver.start = battleClock;
        const cr2::BattleData::Coupling* cp = battleData.coupling(battle.coupling);
        gameOver.over = (cp && cp->flag) || game.flag(0x7f9);
        announceQueue.clear();
        for (SceneEffect& fx : commandEffects) unload(*fx.fig);
        commandEffects.clear();
        for (BattleEffect& fx : battleEffects) unload(*fx.fig);
        battleEffects.clear();
        for (int f = 0; f < 2; ++f) dropGuardObject(f);
        setBattleCamera("Cam_Lose_SoloPair_00");
        battleCameraLoop = true;
        const bool alone = !battleRules->fighter(1).present;
        commandShown = formatAnnounce(announce(alone ? 0x5c : 7), {fighterName(0)});
        commandShownStart = battleClock, announceHeld = true;
        SDL_Log("game over%s", gameOver.over ? "" : " (none yet: back to the field)");
    }
    void endGameOver(bool title) {
        gameOver.stage = 0, announceHeld = false, commandShown.clear();
        battleFade = BattleFade();
        if (title) {
            SDL_Log("game over: the title");
            toTitle = true;
            return;
        }
        game.setFlag(0x7f8, true); // (StepBattleOut: lost with no game over)
    }
    void updateGameOver() {
        if (gameOver.stage == 0) return;
        const bool tapped = gameOver.tapped;
        gameOver.tapped = false;
        const float t = battleClock - gameOver.start;
        if (gameOver.stage == 1 && t > 1.1f) {
            bool tap = tapped;
            if (!inputScript.empty() && (inputScript.front() == "tap" || inputScript.front() == "pick")) inputScript.pop_front(), tap = true;
            if (!tap) return;
            if (!gameOver.over || (!saveExists("cont.txt") && !saveExists("save1.txt"))) return endGameOver(gameOver.over);
            gameOver.stage = 5, gameOver.start = battleClock;
            SDL_Log("game over: faded out, then the question");
            battleFade.colour = 0, battleFade.alpha.begin(battleClock, 0, 1, 1.0f); // (FadeOut 20 updates)
        } else if (gameOver.stage == 5 && t >= 1.0f) { // (HideBattleStatus, HideBattleGameOver, the party hidden; FadeIn at once)
            gameOver.hidden = true, gameOver.stage = 7, gameOver.start = battleClock;
            for (int k = 0; k < 2; ++k)
                if (BattleUnit* u = unitOf(k)) u->hidden = true;
            battleFade = BattleFade();
            announceHeld = false, commandShown.clear();
        } else if (gameOver.stage == 7 && t > 0.5f) {
            gameOver.stage = 8, gameOver.start = battleClock;
        } else if (gameOver.stage == 8 && !inputScript.empty() && (inputScript.front() == "yes" || inputScript.front() == "no") && t > 0.5f) {
            const bool yes = inputScript.front() == "yes";
            inputScript.pop_front();
            gameOverAnswer(yes);
        }
    }
    void gameOverAnswer(bool yes) {
        if (gameOver.stage != 8) return;
        audio.playSe(yes ? 0xb4 : 0xb5, false);
        if (!yes) return endGameOver(true);
        gameOver = GameOverState();
        battleFade = BattleFade();
        leaveBattle();
        titleMenu.action = saveExists("cont.txt") ? 3 : 2; // (Cr3Util_Load_ContData, else Cr3Util_Load_GameData: the main loop loads it)
        SDL_Log("game over: try again (%s)", titleMenu.action == 3 ? "the continue data" : "the save");
    }
    void drawGameOver(const Mat4& view, int w, int h) {
        if (gameOver.stage == 8) return drawAsk(announce(0x87), battleClock - gameOver.start, view, w, h, 7701, 7702);
        if (!gameOver.over || gameOver.hidden) return;
        UiFig& d = uiFig("dead"); // (BattleGameOverMenu: `dead` on its animation)
        d.fig.place = cr2::Mat34();
        poseUi(d, "dead", battleClock - gameOver.start);
        drawFigure(d.fig, view.data());
    }
    void updateResult() {
        if (result.stage == 0 || result.stage >= 4) return;
        const bool tapped = result.tapped;
        result.tapped = false;
        const float t = battleClock - result.start;
        if (result.stage == 1 && t >= 1.2f) { // (SetVictoryMotion / SetVictoryCamera)
            result.stage = 2, result.start = battleClock;
            SDL_Log("result: victory at %.1f s", battleClock);
            commandShown = announce(6), commandShownStart = battleClock, announceHeld = true;
            for (int k = 0; k < 2; ++k)
                if (battleRules->fighter(k).alive && fighterUnit[k] >= 0) playMotion(battleUnits[size_t(fighterUnit[k])], "bb03", battleClock, "bb03");
            const bool both = battleRules->fighter(0).alive && battleRules->fighter(1).alive;
            setBattleCamera(fighterUnit[1] < 0 ? "Cam_Win_PCsolo_00"
                            : battleRules->fighter(0).partner >= 0 && both ? "Cam_Win_PCpair_00"
                            : battleRules->fighter(0).alive ? "Cam_Win_PCfighter_00" : "Cam_Win_PCpartner_00");
        } else if (result.stage == 2 && (tapped || t > 5.0f)) { // (ShowResult: the HUD gone, the window)
            result.stage = 3, result.start = result.windowStart = battleClock;
            SDL_Log("result: the window at %.1f s", battleClock);
            announceHeld = false, commandShown.clear();
            UiFig& seat = uiFig("seatrslt_io");
            poseUi(seat, "seatrslt_io", 0);
            result.transferStart = battleClock + (seat.fig.animLoaded ? seat.fig.anim.duration() : 0.5f);
            result.transferEnd = result.transferStart + kTransfer;
        } else if (result.stage == 3) {
            if (tapped && battleClock < result.transferEnd) result.transferStart = result.transferEnd = battleClock; // (all at once)
            // (ProcessLvup: SE 0xc5 looping while the EXP goes in and the HP back - when there's any -,
            // 0xc3 each level a member passes - CheckLvUp)
            if (!result.counting && !result.counted && battleClock >= result.transferStart) {
                result.counting = true;
                bool any = false;
                for (int k = 0; k < 2; ++k) {
                    const ResultMember& r = result.member[k];
                    result.levelShown[k] = r.level;
                    any = any || (r.ch >= 0 && (r.gain > 0 || r.hp0 < r.hpMax));
                }
                if (any && battleClock < result.transferEnd) result.loopSe = audio.playSe(0xc5, true);
            }
            if (result.counting) {
                for (int k = 0; k < 2; ++k)
                    if (result.member[k].ch >= 0 && resultNow(result.member[k]).level > result.levelShown[k])
                        result.levelShown[k] = resultNow(result.member[k]).level, audio.playSe(0xc3, false);
                if (battleClock >= result.transferEnd) {
                    if (result.loopSe >= 0) audio.stopSe(result.loopSe, 0);
                    result.loopSe = -1, result.counting = false, result.counted = true;
                }
            }
            else if (battleClock >= result.transferEnd && result.message < 0 && (tapped || battleClock - result.transferEnd > 5.0f)) {
                finishResult();
                result.message = 0, result.messageAt = battleClock;
                if (!result.messageSe.empty()) audio.playSe(result.messageSe[0], false);
            } else if (result.message >= 0 && (tapped || battleClock - result.messageAt > 3.0f)) {
                ++result.message, result.messageAt = battleClock;
                if (size_t(result.message) < result.messageSe.size()) audio.playSe(result.messageSe[size_t(result.message)], false);
            }
            if (result.message >= int(result.messages.size()) && result.message >= 0) {
                result.stage = 4;
                SDL_Log("result: EXP %d, SP %d, Oz %d", result.exp, result.sp, result.oz);
            }
        }
    }
    // a number at a fighter's DummyDP01 (its bind pose's), as the hits' (DamageInfo00)
    void showNumber(int f, int value, int set) {
        const int u = f >= 0 && f < 5 ? fighterUnit[f] : -1;
        if (u < 0) return;
        const BattleUnit& bu = battleUnits[size_t(u)];
        float p[3] = {bu.fig.place.m[3], bu.fig.place.m[7] + 0.15f, bu.fig.place.m[11]};
        const int dj = bu.fig.skeleton.joint(cr2::nameHash("DummyDP01"));
        if (dj >= 0) {
            const std::vector<cr2::Mat34> ww = bu.fig.skeleton.world(bu.fig.skeleton.basePose, bu.fig.place);
            if (size_t(dj) < ww.size()) p[0] = ww[size_t(dj)].m[3], p[1] = ww[size_t(dj)].m[7], p[2] = ww[size_t(dj)].m[11];
        }
        DamageShown ds;
        ds.value = value, ds.set = set, ds.start = battleClock;
        for (int c = 0; c < 3; ++c) ds.at[c] = p[c];
        damageShown.push_back(ds);
    }
    void beginShowAction(const ShowAction& a) {
        if (a.actor == 0 || a.actor == 1) lastPartyActor = a.actor;
        showStart = battleClock, showHitDone = false;
        const cr2::BattleData::Command* c = battleData.command(a.command);
        if (c && c->voice > 0) battleVoice(a.actor, c->voice, float(c->voiceDelay) * 0.05f, true); // (PlayVoiceCommand: +0x68 after +0x6a)
        if (!a.hits.empty() && a.hits.front().failed) { // (ActionDontAction*: its announcement alone, a moment)
            for (Move& m : showMoves) m = Move();
            showHitAt = 0, showCamera2 = -1;
            showEnd = battleClock + 1.5f;
            if (c) SDL_Log("  %s: %s - not done", battleRules->fighter(a.actor).name.c_str(), c->name[1].c_str());
            return;
        }
        if (a.actor >= 0 && a.actor < 2 && a.command == cr2::CommandInput::kDefend && !(!a.hits.empty() && a.hits.front().counter)) {
            for (Move& m : showMoves) m = Move();
            showHitAt = 0, showCamera2 = -1;
            showEnd = battleClock; // (action_table_empty: no announcement, no motion - the stance since the choice)
            SDL_Log("  %s: Defend (its stance)", battleRules->fighter(a.actor).name.c_str());
            return;
        }
        for (int f : {a.actor, a.partner}) // (SetAttackMotion: HideAttachObject)
            if (f >= 0 && f < 2 && guardObjects[f]) guardObjects[f]->shown = false, guardReturn[f] = true;
        if (c && startScene(a, *c)) { // (an awakening's, an advent's, a monster's special: its scene)
            SDL_Log("  %s: %s", battleRules->fighter(a.actor).name.c_str(), c->name[1].c_str());
            return;
        }
        std::string motion = c ? c->motion : "bb01";
        if (a.partner >= 0 && a.actor < 2) motion = "bp01"; // (the pair's command: +0x46, bp01)
        float length = 0;
        for (Move& m : showMoves) m = Move();
        const int target = a.hits.empty() ? -1 : a.hits.front().target;
        int slot = 0;
        // (ActionAttackPairWaitDelayTime: the pair's second after GetPairAttackDelayTime - by the two
        // characters, the first's row, in frames of 0.05 s; the shadows' pair by their numbers 205 ..)
        static const unsigned char kPairDelay[9][9] = {{0, 6, 5, 4, 3, 6, 0, 0, 0},       {8, 0, 8, 6, 4, 10, 4, 0, 0},
                                                       {10, 8, 0, 7, 6, 10, 2, 0, 0},     {13, 12, 13, 0, 7, 14, 7, 0, 0},
                                                       {12, 11, 11, 11, 0, 13, 7, 0, 0},  {11, 10, 10, 8, 7, 0, 5, 0, 0},
                                                       {23, 20, 20, 20, 15, 21, 0, 0, 0}, {24, 24, 24, 24, 0, 24, 24, 0, 0},
                                                       {24, 24, 24, 24, 0, 24, 24, 0, 0}};
        auto pairDelay = [&](int first, int second) {
            const cr2::Fighter &x = battleRules->fighter(first), &y = battleRules->fighter(second);
            int i = x.id - 1, j = y.id - 1; // (cr3_common_data's: from 0)
            if (first >= 2) i = x.id - 205, j = y.id - 205;
            return i >= 0 && i < 9 && j >= 0 && j < 9 ? float(kPairDelay[i][j]) / 20.0f : 0.0f;
        };
        for (int f : {a.actor, a.partner}) {
            const int u = f >= 0 ? fighterUnit[f] : -1;
            if (u < 0) continue;
            BattleUnit& bu = battleUnits[size_t(u)];
            const float delay = f == a.partner && a.partner >= 0 ? pairDelay(a.actor, a.partner) : 0.0f;
            playMotion(bu, motion, battleClock + delay);
            length = std::max(length, motionLength(bu, motion) + delay);
            cr2::MotionTimes t;
            cr2::motionTimes(bu.figure, cr2::motionIndex(motion), t);
            Move& m = showMoves[slot++];
            m.unit = u;
            if (target >= 0 && t.moveStart >= 0 && t.moveEnd >= t.moveStart && aposPlace(aposName(f, target), m.to)) {
                m.active = true;
                m.start = t.moveStart + delay, m.end = t.moveEnd + delay;
                m.back = t.move2Start + delay, m.backEnd = t.move2End + delay;
            }
            if (f == a.actor) showHitAt = t.hit[0] >= 0 ? t.hit[0] : length * 0.5f;
        }
        showEnd = battleClock + std::max(length, 0.5f) + 0.3f;
        // its effects as it begins (ActionAttackWaitAttackMotion / ActionSopia / ItemStandby): a
        // shot (CreateShotEffect: the command's +0x40 3 - its plain attack's when it has no motion
        // of its own - +0x28 from the motion's effect place to the one hit, there at the hit, and
        // +0x30 on that one), then its launch effect (+0x20) on the one acting
        if (c && target >= 0) {
            const cr2::BattleData::Command* shooter = c;
            if (c->motion.empty() && a.actor >= 0 && a.actor < 2) shooter = battleData.command(battleRules->fighter(a.actor).attack);
            if (shooter && shooter->shot == 3 && !c->shotEffect.empty()) {
                SceneEffect fx;
                fx.fig = std::make_unique<ViewFigure>();
                std::string err;
                const float keep = time;
                if (loadFigure(c->shotEffect + ".geom", *fx.fig, &err)) {
                    const auto it = std::find(fx.fig->anims.begin(), fx.fig->anims.end(), c->shotEffect + ".anim");
                    if (it != fx.fig->anims.end()) loadAnim(*fx.fig, size_t(it - fx.fig->anims.begin()));
                    time = keep;
                    fx.set = &cr2::effectSetting(c->shotEffect);
                    fx.fig->absolute = true;
                    fx.shot = true, fx.path = fx.set->shotPath, fx.start = battleClock, fx.flight = std::max(showHitAt, 0.1f);
                    cr2::MotionTimes mt;
                    const BattleUnit* au = unitOf(a.actor);
                    const BattleUnit* tu = unitOf(target);
                    if (au && tu) {
                        cr2::motionTimes(au->figure, cr2::motionIndex(motion), mt);
                        cr2::Mat34 j = au->fig.place;
                        if (mt.effectLocation != 9) jointPlace(au->fig, cr2::tposJoint(mt.effectLocation), j);
                        cr2::Mat34 k = tu->fig.place;
                        const short tl = fx.set->locator == 7 || fx.set->locator == 9 ? 6 : fx.set->locator;
                        jointPlace(tu->fig, cr2::tposJoint(tl), k);
                        for (int c3 = 0; c3 < 3; ++c3) fx.from[c3] = j.m[c3 * 4 + 3], fx.to[c3] = k.m[c3 * 4 + 3];
                        commandEffects.push_back(std::move(fx));
                    }
                } else {
                    time = keep;
                    SDL_Log("shot %s: %s", c->shotEffect.c_str(), err.c_str());
                }
                if (!c->shotMark.empty()) commandEffect(c->shotMark, target, a.partner >= 0);
            }
            // (j007's and m507's: CreateWarpEffect - a pair's, eff110 twice on the one acting -
            // and CreateAttackObject - an attack's, its plain or pair attack's launch effect)
            if (const BattleUnit* au = unitOf(a.actor); au && (au->figure.rfind("j007", 0) == 0 || au->figure.rfind("m507", 0) == 0)) {
                if (a.partner >= 0) commandEffect("eff110", a.actor, true), commandEffect("eff110", a.actor, true);
                const cr2::Fighter& af = battleRules->fighter(a.actor);
                int plainId = a.partner >= 0 ? af.pairAttack : af.attack;
                if (a.actor >= 2)
                    if (const cr2::BattleData::Monster* mo = battleData.monster(af.id)) plainId = a.partner >= 0 ? mo->pairAttack : mo->counter;
                const cr2::BattleData::Command* pc = battleData.command(plainId);
                if (a.command == plainId && pc && !pc->launchEffect.empty() && cr2::effectSetting(pc->launchEffect).pairLocator != 7) // (IsAttackAction: the attack's flow)
                    commandEffect(pc->launchEffect, a.actor, a.partner >= 0);
            }
            // (ActionSopiaMirror / CreateMirrorEffect: on each whose magic barrier turns it back,
            // eff227 - at its side's middle when its setting says 7 - and SE 0x92)
            for (int m : a.hits.empty() ? std::vector<int>() : a.hits.front().mirrors) {
                const cr2::EffectSetting& ms = cr2::effectSetting("eff227");
                commandEffect("eff227", m, false, ms.locator == 7 ? (m < 2 ? 0 : 1) : -1);
                battleSe(0x92);
            }
            if (!c->launchEffect.empty()) {
                const cr2::EffectSetting& ls = cr2::effectSetting(c->launchEffect);
                if (ls.pairLocator != 7) commandEffect(c->launchEffect, a.actor, a.partner >= 0); // (ShowCommandLaunchEffect: its +0x32 7 left out - every caller passes true)
            }
        }
        // its cameras (BtlActionCtrl::SetAttackCamera1 / 2): by the party - its kind (0 / 1 the
        // one, 2 alone, 3 a pair), the pattern's base and the target, one of two at random, the
        // next at the hit; by a monster - the base, which it is and its target (3 a pair)
        showCamera2 = -1;
        if (target >= 0) {
            const int base = cr2::cameraPattern(battle.pattern);
            const bool targetPair = battleRules->fighter(target).partner >= 0;
            if (a.actor < 2) {
                const bool alone = fighterUnit[1 - a.actor] < 0;
                const int kind = a.partner >= 0 ? 3 : alone ? 2 : a.actor;
                const int n = battle.pattern == 11 && targetPair ? 21 : base + target - 2;
                int cam = cr2::cameraAttackToEnemy(kind, n);
                if (cam >= 0) {
                    cam += int(battleClock * 1000.0f) % 2 * 2; // (BtlRand(2): either)
                    setBattleCamera(cam);
                    showCamera2 = cam + 1;
                }
            } else {
                const int cam = cr2::cameraAttackFromEnemy(base + a.actor - 2, targetPair ? 3 : target);
                if (cam >= 0) setBattleCamera(cam);
            }
            SDL_Log("    camera %s", battleCamera.c_str());
        }
        if (c)
            SDL_Log("  %s%s%s: %s", battleRules->fighter(a.actor).name.c_str(), a.partner >= 0 ? " + " : "",
                    a.partner >= 0 ? battleRules->fighter(a.partner).name.c_str() : "", c->name[1].c_str());
        if (c) commandShown = commandName(c->id), commandShownStart = battleClock;
        if (!a.hits.empty() && a.hits.front().counter) commandShown = announce(0x47), commandShownStart = battleClock; // ("Counter")
    }

    void poseBattleShow() {
        if (!battleRules || battleFight == 0 || showIndex >= showActions.size()) return;
        const float s = battleClock - showStart;
        // the attackers' way there and back
        for (const Move& m : showMoves) {
            if (m.unit < 0) continue;
            BattleUnit& bu = battleUnits[size_t(m.unit)];
            if (!m.active) {
                bu.fig.place = bu.home;
                continue;
            }
            float k = 0;
            if (s >= m.start) k = m.end > m.start ? std::min((s - m.start) / (m.end - m.start), 1.0f) : 1.0f;
            if (m.back >= 0 && s >= m.back) k = m.backEnd > m.back ? 1.0f - std::min((s - m.back) / (m.backEnd - m.back), 1.0f) : 0.0f;
            if (m.back < 0 && s >= showEnd - showStart - 0.3f) k = 0;
            cr2::Mat34 p = k < 0.5f ? bu.home : m.to; // (the turn: at the move's middle)
            for (int r = 0; r < 3; ++r) p.m[r * 4 + 3] = bu.home.m[r * 4 + 3] + (m.to.m[r * 4 + 3] - bu.home.m[r * 4 + 3]) * k;
            bu.fig.place = p;
        }
    }

    // the fight's time going on: the next action (or turn), the hits as they land
    std::deque<std::string> announceQueue;
    float announceAt = -10;
    void updateBattleShow() {
        if (!battleRules || battleFight == 0) return;
        if (!announceQueue.empty() && battleClock - announceAt > 1.5f) { // (each 1.5 s on the banner)
            commandShown = announceQueue.front(), commandShownStart = announceAt = battleClock;
            SDL_Log("    announce: %s", commandShown.c_str());
            announceQueue.pop_front();
        }
        stepBattleShow();
        if (showIndex >= showActions.size()) return;
        updateScene();
        const ShowAction& a = showActions[showIndex];
        const float s = battleClock - showStart;
        // the hits
        if (!scene.active && !showHitDone && s >= showHitAt) {
            showHitDone = true;
            if (showCamera2 >= 0) setBattleCamera(showCamera2);
            landHits(a, true);
        }
    }

    // an action's hits landing (the targets' damage motion, the sounds, the hit's effect, the
    // numbers); a special scene's later hits (`first` false) the blows alone
    std::vector<int> groupShown; // (the sides a group's effect has had this action)
    // A blow's reaction (BtlActionCtrl::CheckThrowBack): one of the party blocking it, or any one
    // guarded by Auto Defend, bg01 once; one guarding (Defend) bg02 - where its table has it - back
    // to bg01; in time stopped (21) none; in the Executioner's Pose (37) bx03, back to bx02; else
    // bd01 (a monster blocking it: no damage, none)
    void throwBack(BattleUnit& bu, const cr2::BattleRules::Event& e) {
        if (e.blocked && e.target < 2 && !bu.guarding) return playMotion(bu, "bg01", battleClock);
        if (e.autoGuard) return playMotion(bu, "bg01", battleClock);
        if (e.damage <= 0 && e.blocked) return;
        if (bu.guarding) {
            if (motionLength(bu, "bg02") > 0) playMotion(bu, "bg02", battleClock);
            return;
        }
        const cr2::Fighter& f = battleRules->fighter(e.target);
        if (f.hasState(21)) return;
        if (f.hasState(37) && f.alive) return playMotion(bu, "bx03", battleClock);
        playMotion(bu, "bd01", battleClock);
    }
    void landHits(const ShowAction& a, bool first) {
        if (first) groupShown.clear();
        // (BtlSound's batches: the targets' cries queued by tag - SetVoiceTagFromData -, then at the
        // hit's end (BtlActionCtrl::Hit) the deaths' one at random when it wins, the game over's one
        // when it loses (the deaths' dropped), else all; the heals' one at random)
        std::vector<std::pair<int, int>> deaths, overs, heals; // (fighter, kind)
        for (const auto& e : a.hits) {
            const int u = fighterUnit[e.target];
            if (u < 0) continue;
            BattleUnit& bu = battleUnits[size_t(u)];
            const bool restore = e.heal > 0 || e.healMp > 0 || e.revived;
            const cr2::BattleData::Command* sc = battleData.command(e.command);
            const bool statusOnly = e.failed || (sc && (sc->kind == 1 || sc->kind == 3 || sc->kind == 26 || sc->kind == 28)); // (no number, no blow)
            if (first)
                for (const auto& [id, who] : e.notes) // (the announcements: on the banner, in turn - BtlAnnounce)
                    announceQueue.push_back(formatAnnounce(announce(id), {fighterName(who)}));
            if (first && e.stealNote) // (CheckSteal's: after the rest)
                announceQueue.push_back(e.stealNote == 63 ? formatAnnounce(announce(63), {messageText("itemname", e.stolen)}) : announce(e.stealNote));
            if (e.hit && !restore && !statusOnly) throwBack(bu, e);
            // its sounds (BtlSound::PlayHitSE: the command's +0x74 - -1 one of 12..14 -, 15 on a
            // critical, none on a miss or a block; PlayVoiceAttack - the actor's 11 a blow, 12 a
            // critical, 13 a kill, 15 the battle's last, 14 nothing done -; the one struck's
            // PlayVoiceDamage 3 / CriticalDamage 4 (a quarter of its HP left or less), NoDamage 7,
            // Dead 5; the healed's HealHp 8, revived's Revive 10)
            if (!statusOnly && !restore && e.actor >= 0) {
                const cr2::BattleData::Command* hc0 = battleData.command(a.command);
                if (e.hit && !e.blocked) {
                    int se = hc0 ? hc0->hitSe : -1;
                    if (se == -1) se = e.critical ? 15 : 12 + int(battleClock * 1000.0f) % 3;
                    else if (e.critical && se != 0) se = 15;
                    if (hc0 && hc0->magic == 0 && e.burst >= 1 && e.burst <= 3) se = 0x8e + e.burst; // (HitEffect: eff218 .. 220's 0x8f .. 0x91)
                    if (se > 0) battleSe(se);
                }
                if (!first) continue; // (a later hit: its blow and sound alone)
                if (&e == &a.hits.front())
                    battleVoice(e.actor, !e.hit || e.blocked ? 14 : e.defeated ? (battleRules->outcome() == 1 ? 15 : 13) : e.critical ? 12 : e.damage > 0 ? 11 : 14);
                const cr2::Fighter& tf = battleRules->fighter(e.target);
                if (e.defeated && e.target < 2) ++partyDeaths;
                // (CheckDead: the party both down - the game over's 6, but in couplings 221 / 271 nothing)
                const bool wiped = e.defeated && e.target < 2 && !battleRules->fighter(0).alive && !(battleRules->fighter(1).present && battleRules->fighter(1).alive);
                if (e.hit && !e.blocked && !(wiped && (battle.coupling == 221 || battle.coupling == 271))) {
                    const int kind = wiped ? 6 : e.defeated ? 5 : e.damage <= 0 ? 7 : tf.hp * 100 / std::max(tf.maxHp, 1) < 26 ? 4 : 3;
                    if (kind == 5) deaths.push_back({e.target, kind});
                    else if (kind == 6) overs.push_back({e.target, kind});
                    else battleVoice(e.target, kind, 0.5f);
                }
            } else if (restore && first && a.command != 0x90) { // (Restore: revived 10; healed to full 9, else 8 - none for command 0x90's)
                const cr2::Fighter& rf = battleRules->fighter(e.target);
                if (e.revived) battleVoice(e.target, 10, 0.3f);
                else heals.push_back({e.target, rf.hp * 100 / std::max(rf.maxHp, 1) > 99 ? 9 : 8});
            }
            if (!first) continue;
            if (e.revived) bu.hidden = false;
            const cr2::BattleData::Command* hc = battleData.command(e.command);
            if (!statusOnly && (e.hit || restore || !(hc && hc->kind == 2))) { // (the number at its DummyDP01 - else its middle - on the screen, in UI units; none for a guard)
                float p[3] = {bu.fig.place.m[3], bu.fig.place.m[7] + 0.15f, bu.fig.place.m[11]};
                const int dj = bu.fig.skeleton.joint(cr2::nameHash("DummyDP01"));
                if (dj >= 0) {
                    std::vector<cr2::JointTransform> pp = bu.fig.skeleton.basePose;
                    const std::vector<cr2::Mat34> ww = bu.fig.skeleton.world(pp, bu.fig.place);
                    if (size_t(dj) < ww.size()) p[0] = ww[size_t(dj)].m[3], p[1] = ww[size_t(dj)].m[7], p[2] = ww[size_t(dj)].m[11];
                }
                // the hit's effect on it (BtlActionCtrl::HitEffect / ShowCommandHitEffect: the
                // command's +0x18 - an attack's (+0x49 0) whose element beat the target's, eff218 ..
                // eff220; a critical with the plain attack eff106 -, none here for a group's - its
                // setting's locator 7: HitGroupEffect, once)
                if (e.hit && !restore) {
                    const cr2::BattleData::Command* ec = battleData.command(a.command);
                    bool plain = e.actor >= 0 && e.actor < 2 && a.command == battleRules->fighter(e.actor).attack; // (a monster's: its +0x4c)
                    if (e.actor >= 2)
                        if (const cr2::BattleData::Monster* en = battleData.monster(battleRules->fighter(e.actor).id)) plain = a.command == en->counter;
                    std::string fx = ec ? ec->hitEffect : "";
                    if (ec && ec->magic == 0 && e.burst >= 1 && e.burst <= 3) fx = "eff21" + std::to_string(7 + e.burst);
                    if (e.critical && plain) fx = "eff106";
                    if (!fx.empty()) {
                        const cr2::EffectSetting& set = cr2::effectSetting(fx);
                        const bool pairAct = a.partner >= 0;
                        if ((pairAct ? set.pairLocator : set.locator) != 7) commandEffect(fx, e.target, pairAct);
                        else if (std::find(groupShown.begin(), groupShown.end(), e.target < 2 ? 0 : 1) == groupShown.end())
                            groupShown.push_back(e.target < 2 ? 0 : 1), commandEffect(fx, e.target, pairAct, e.target < 2 ? 0 : 1);
                    }
                }
                {
                    DamageShown ds;
                    ds.value = e.damage, ds.set = e.weak ? 40 : 20, ds.start = battleClock; // (the restores: HP green 50, MP 60)
                    if (restore) ds.value = e.heal > 0 || e.healMp == 0 ? e.heal : e.healMp, ds.set = e.heal > 0 || e.healMp == 0 ? 50 : 60;
                    else if (!e.hit) ds.value = -1, ds.mark = 0;
                    else if (e.critical) ds.mark = 2;
                    for (int c = 0; c < 3; ++c) ds.at[c] = p[c];
                    damageShown.push_back(ds);
                }
            }
            {
                const cr2::BattleData::Command* ec = battleData.command(e.command);
                const std::string what = ec && ec->kind == 2 ? std::string("defends")
                                         : restore ? (e.revived ? "revived, " : "") + std::string("+") + std::to_string(e.heal) + " HP" + (e.healMp ? " +" + std::to_string(e.healMp) + " MP" : "")
                                         : e.hit ? std::to_string(e.damage) + (e.critical ? " critical" : "") + (e.defeated ? ", down" : "") : "missed";
                SDL_Log("    %s -> %s: %s (%.1f s)", battleRules->fighter(e.actor).name.c_str(), battleRules->fighter(e.target).name.c_str(), what.c_str(), battleClock);
            }
        }
        auto one = [&](const std::vector<std::pair<int, int>>& v, float delay) {
            if (!v.empty()) {
                const auto& [f, kind] = v[boxRandom() % v.size()];
                battleVoice(f, kind, delay);
            }
        };
        if (battleRules->outcome() == 1) one(deaths, 0.5f);
        else if (battleRules->outcome() == 2) one(overs, 0.5f);
        else
            for (const auto& [f, kind] : deaths) battleVoice(f, kind, 0.5f);
        one(heals, 0.3f);
    }

    // The special scenes - the awakenings, the advents, the monsters' specials (BtlActionCtrl::
    // ActionAwakeStandby, ActionAdventLaunch .. Cleanup, ActionSpecialCommand; battle/special.hpp):
    // each step once the scene's clock reaches it, in the list's order, the scene over once all
    // have run - its effects in their slots, its cameras loaded and started where it puts them,
    // the fighters hidden, shown, faded and tinted, the screen faded, its sounds; its hits the
    // action's (landHits: the numbers at the first). An advent first has the pair call it (the
    // motion bm01, its launch sound and a voice, 1.25 s - ActionAdventLaunch / LaunchWait) and fade
    // away (1 s, ActionAdventHide / HideWait); the HUD is gone till it is over (SetBattleStatusDisplay)
    struct SceneEffect {
        std::unique_ptr<ViewFigure> fig;
        const cr2::EffectSetting* set = nullptr;
        float start = 0, end = -1; // (battle seconds: hidden before it starts, gone at its end - < 0 none)
        bool shown = true;
        int follows = -1; // (the fighter whose model it has: connect, SetEffectTarget)
        short locator = -1; // (else its setting's: a pair's action's command effect - +0x32)
        // a shot (BtlEffectCtrl::SetShot / ControlShot): from `from` to `to` over `flight` s,
        // straight or falling (thrown up at 2 a second, down to arrive), gone once there
        bool shot = false;
        int path = 0;
        float from[3] = {0, 0, 0}, to[3] = {0, 0, 0}, flight = 0;
        cr2::Mat34 place;
        Ramp alpha;
        bool kept = false; // (BtlEffectCtrl's type 4: no control - there till deleted)
    };
    struct SceneState {
        bool active = false, advent = false;
        bool skippable = true; // (ActionAwake / AdventStandby: on at its start; SetSkipEnable)
        float start = 0; // (battle seconds: the scene's clock's 0)
        std::vector<cr2::SpecialStep> steps;
        std::vector<bool> done;
        int actor = -1, partner = -1, target = -1, hits = 0;
        std::map<int, SceneEffect> effects; // (by slot, +0x46's)
    } scene;
    std::vector<std::pair<std::string, CameraFigure>> sceneCameras; // (LoadSpecialCamera's)
    struct BattleFade {
        int colour = 0; // (0 black, 1 white: Cr3Fade's)
        Ramp alpha{0, 0, 0, 0};
    } battleFade;
    bool battleMapHidden = false;
    Ramp mapColour;

    BattleUnit* unitOf(int f) {
        const int u = f >= 0 && f < 5 ? fighterUnit[f] : -1;
        return u >= 0 ? &battleUnits[size_t(u)] : nullptr;
    }
    static void unscaled(cr2::Mat34& m) {
        for (int c = 0; c < 3; ++c) {
            const float l = std::sqrt(m.m[c] * m.m[c] + m.m[4 + c] * m.m[4 + c] + m.m[8 + c] * m.m[8 + c]);
            if (l > 0) m.m[c] /= l, m.m[4 + c] /= l, m.m[8 + c] /= l;
        }
    }
    // a figure's joint in the world as it was last posed (false: none)
    bool jointPlace(const ViewFigure& f, const char* joint, cr2::Mat34& out) {
        const int j = joint ? f.skeleton.joint(cr2::nameHash(joint)) : -1;
        if (j < 0) return false;
        const std::vector<cr2::Mat34> w = f.skeleton.world(f.posed.empty() ? f.skeleton.basePose : f.posed, f.place);
        if (size_t(j) >= w.size()) return false;
        out = w[size_t(j)];
        unscaled(out);
        return true;
    }
    // ActionSpecialGetPosition / GetRotation: a place a scene names - `target` 1 its actor, 2 its
    // target (0 none; 3 the party's middle, 4 the monsters' - BtlWork +0xad0 / +0xae0, the latter
    // CalcMonstersCenterPosition's sum over the party's count); by `apos` the aPos joint the one
    // goes to to strike the other, by `dpos` its battledpos place, else its joint `tpos`
    // (BtlTposTable's; 9 the model's own place)
    cr2::Mat34 scenePlace(int target, bool apos, bool dpos, int tpos) {
        cr2::Mat34 out;
        const int a = scene.actor, b = scene.target;
        if (apos) {
            cr2::Mat34 m;
            if (a >= 0 && b >= 0 && (target == 1 || target == 2) && aposPlace(target == 1 ? aposName(a, b) : aposName(b, a), m)) out = m;
            return out;
        }
        BattleUnit* u = target == 1 ? unitOf(a) : target == 2 ? unitOf(b) : nullptr;
        if (dpos) {
            if (u) out = u->home;
            return out;
        }
        if (target == 3 || target == 4) {
            int party = 0;
            float sum[3] = {0, 0, 0};
            for (int f = 0; f < 5; ++f) {
                BattleUnit* fu = unitOf(f);
                if (f < 2 && fu) ++party;
                if (!fu || (f < 2) != (target == 3)) continue;
                for (int c = 0; c < 3; ++c) sum[c] += fu->fig.place.m[c * 4 + 3];
            }
            for (int c = 0; c < 3; ++c) out.m[c * 4 + 3] = party > 0 ? sum[c] / float(party) : 0.0f;
            return out;
        }
        if (!u) return out;
        if (tpos == 9 || !jointPlace(u->fig, cr2::tposJoint(tpos), out)) {
            out = u->fig.place;
            unscaled(out);
        }
        return out;
    }
    static cr2::Mat34 turnY(float degrees) {
        const float r = degrees * 3.14159265f / 180.0f, c = std::cos(r), s = std::sin(r);
        cr2::Mat34 m;
        m.m[0] = c, m.m[2] = s, m.m[8] = -s, m.m[10] = c;
        return m;
    }
    static void setRotation(cr2::Mat34& m, const cr2::Mat34& from) {
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) m.m[r * 4 + c] = from.m[r * 4 + c];
    }

    // the action's scene, if its command has one (SetActionTable), begun: its steps (those of
    // BuildAwakeFromMotionTable - the motion table's hits, the dash and the way back - in its
    // place); false none
    bool startScene(const ShowAction& a, const cr2::BattleData::Command& c) {
        bool advent = false;
        const cr2::SpecialScene* sc = cr2::specialScene(a.command, c.awaken != 0, &advent);
        if (!sc) return false;
        endScene();
        scene.active = true, scene.advent = advent, scene.skippable = true;
        scene.actor = a.actor, scene.partner = a.partner, scene.target = a.hits.empty() ? -1 : a.hits.front().target;
        scene.steps.clear(), scene.hits = 0;
        for (int k = 0; k < sc->count; ++k) {
            const cr2::SpecialStep& st = sc->steps[k];
            if (st.op != cr2::kSpecialMotionTable) {
                scene.steps.push_back(st);
                continue;
            }
            cr2::MotionTimes t;
            if (!st.s || !cr2::motionTimes(st.s, st.i[0], t)) continue;
            auto add = [&](short op, float at, float f0, int i0) {
                cr2::SpecialStep x{};
                x.op = op, x.time = at, x.f[0] = f0, x.i[0] = i0;
                scene.steps.push_back(x);
            };
            for (int h = 0; h < 10; ++h) // (each a hit, the last whose next is none - the 10th's "next" the turn's start)
                if (t.hit[h] > 0) add(cr2::kSpecialHit, t.hit[h], 0, !((h < 9 ? t.hit[h + 1] : t.turnStart) > 0));
            if (t.moveStart > 0) add(cr2::kSpecialDash, t.moveStart, t.moveEnd - t.moveStart, 0);
            if (t.move2Start > 0) add(cr2::kSpecialReturn, t.move2Start, t.move2End - t.move2Start, 0);
            if (t.turnStart > 0) add(cr2::kSpecialReturnDirection, t.turnStart, t.turnEnd - t.turnStart, 0);
        }
        scene.done.assign(scene.steps.size(), false);
        const float lead = advent ? 2.25f : 0.0f;
        scene.start = showStart = battleClock + lead;
        showHitDone = false, showCamera2 = -1, showEnd = 1e9f; // (till it is over)
        showHitAt = 0;
        for (Move& m : showMoves) m = Move();
        if (advent) { // (the pair calls it: bm01, its launch sound, one of the two's voice; then gone)
            for (int f : {a.actor, a.partner})
                if (BattleUnit* bu = unitOf(f)) {
                    playMotion(*bu, "bm01", battleClock);
                    bu->alpha.begin(battleClock + 1.25f, 1, 0, 1.0f);
                }
            if (c.launchSe > 0) battleSe(c.launchSe);
            if (!c.launchEffect.empty() && cr2::effectSetting(c.launchEffect).pairLocator != 7) // (ActionAdventLaunch: ShowCommandLaunchEffect on the one calling it - the advents' eff302, 7, left out as there)
                commandEffect(c.launchEffect, a.actor, true);
        } else if (BattleUnit* bu = unitOf(a.actor)) { // (ActionAwakeStandby: its command's motion)
            playMotion(*bu, c.motion, battleClock);
        }
        SDL_Log("  scene: %s %d steps", advent ? "advent" : "awakening", int(scene.steps.size()));
        return true;
    }
    // its effect in slot `id` (null: none)
    SceneEffect* sceneEffect(int id) {
        const auto it = scene.effects.find(id);
        return it != scene.effects.end() && it->second.fig ? &it->second : nullptr;
    }
    void runStep(const cr2::SpecialStep& st) {
        using namespace cr2;
        BattleUnit* actorUnit = unitOf(scene.actor);
        const bool party = scene.actor >= 0 && scene.actor < 2;
        switch (st.op) {
        case kSpecialSetMotion:
            if (actorUnit) playMotion(*actorUnit, motionName(st.i[0]), battleClock);
            break;
        case kSpecialSetPosition: // (SetPosRot: back at its dpos)
        case kSpecialSetRotation:
            for (Move& m : showMoves) m.active = false;
            if (actorUnit) actorUnit->fig.place = actorUnit->home;
            break;
        case kSpecialSetVisibility: { // (0 its side, 1 the other's - the monsters' standing -, 3 its partner, 4 the map)
            const bool v = st.i[1] != 0;
            if (st.i[0] == 4) battleMapHidden = !v;
            for (int f = 0; f < 5; ++f) {
                BattleUnit* u = unitOf(f);
                if (!u) continue;
                const bool own = (f < 2) == party;
                const bool pick = st.i[0] == 0 ? own : st.i[0] == 1 ? !own : st.i[0] == 3 ? own && f != scene.actor : false;
                if (pick && (f < 2 || battleRules->fighter(f).alive)) u->sceneHidden = !v;
            }
            break;
        }
        case kSpecialSetAlpha: // (0 the party, else the monsters standing)
            for (int f = 0; f < 5; ++f)
                if (BattleUnit* u = unitOf(f); u && (f < 2) == (st.i[0] == 0) && (f < 2 || battleRules->fighter(f).alive))
                    u->alpha.begin(battleClock, st.f[0], st.f[1], st.f[2]);
            break;
        case kSpecialSetColor: // (3 the party, 4 the monsters, 7 the map)
            if (st.i[0] == 7) mapColour.begin(battleClock, st.f[0], st.f[1], st.f[2]);
            for (int f = 0; f < 5; ++f)
                if (BattleUnit* u = unitOf(f); u && ((st.i[0] == 3 && f < 2) || (st.i[0] == 4 && f >= 2)))
                    u->colour.begin(battleClock, st.f[0], st.f[1], st.f[2]);
            break;
        case kSpecialSetEffect: { // (CreateEffect: its settings' delay on its start; connect: the actor's model)
            if (!st.s) break;
            SceneEffect fx;
            fx.fig = std::make_unique<ViewFigure>();
            std::string err;
            const float keep = time;
            if (!loadFigure(std::string(st.s) + ".geom", *fx.fig, &err)) {
                SDL_Log("scene effect %s: %s", st.s, err.c_str());
                break;
            }
            const auto it = std::find(fx.fig->anims.begin(), fx.fig->anims.end(), std::string(st.s) + ".anim");
            if (it != fx.fig->anims.end()) loadAnim(*fx.fig, size_t(it - fx.fig->anims.begin()));
            time = keep;
            fx.set = &effectSetting(st.s);
            fx.fig->absolute = fx.set->kind != 2; // (a summoned one's motions over its pose)
            fx.shown = st.i[1] != 0;
            fx.follows = st.i[2] ? scene.actor : -1;
            fx.start = battleClock + st.f[1] + float(fx.set->delay) * 0.05f;
            fx.end = st.f[0] > 0 ? battleClock + st.f[0] : -1;
            if (const auto old = scene.effects.find(st.i[0]); old != scene.effects.end() && old->second.fig) unload(*old->second.fig);
            scene.effects[st.i[0]] = std::move(fx);
            break;
        }
        case kSpecialSetEffectTarget:
            if (SceneEffect* fx = sceneEffect(st.i[0])) fx->follows = st.i[1] == 1 ? scene.actor : st.i[1] == 2 ? scene.target : fx->follows;
            break;
        case kSpecialSetEffectPosition:
            if (SceneEffect* fx = sceneEffect(st.i[0])) {
                const Mat34 p = scenePlace(st.i[1], st.i[2] != 0, st.i[3] != 0, st.i[4]);
                for (int c = 0; c < 3; ++c) fx->place.m[c * 4 + 3] = p.m[c * 4 + 3] + st.v[c];
            }
            break;
        case kSpecialSetEffectRotation:
            if (SceneEffect* fx = sceneEffect(st.i[0])) setRotation(fx->place, scenePlace(st.i[1], st.i[2] != 0, st.i[3] != 0, st.i[4]) * turnY(st.f[0]));
            break;
        case kSpecialSetEffectAnimation:
            if (SceneEffect* fx = sceneEffect(st.i[0]); fx && st.s) {
                std::string base = fx->fig->name;
                if (base.size() > 5) base.resize(base.size() - 5); // (".geom")
                const auto it = std::find(fx->fig->anims.begin(), fx->fig->anims.end(), base + st.s + ".anim");
                if (it != fx->fig->anims.end()) {
                    const float keep = time;
                    loadAnim(*fx->fig, size_t(it - fx->fig->anims.begin()));
                    time = keep;
                    fx->start = std::max(fx->start, battleClock);
                }
            }
            break;
        case kSpecialSetEffectAlpha:
            if (SceneEffect* fx = sceneEffect(st.i[0])) fx->alpha.begin(battleClock, st.f[0], st.f[1], st.f[2]);
            break;
        case kSpecialLoadCamera: {
            if (!st.s) break;
            std::string n = st.s;
            for (char& ch : n) ch = char(std::tolower(static_cast<unsigned char>(ch)));
            CameraFigure c;
            std::vector<uint8_t> d;
            if (archive.read(n + ".geom", d)) cr2::parseGeom(d, c.geom);
            if (archive.read(n + ".skel", d)) cr2::parseSkeleton(d, c.skel);
            c.animated = archive.read(n + ".anim", d) && c.anim.parse(d) && c.anim.joints() == int(c.skel.basePose.size());
            if (c.geom.cameras.empty()) SDL_Log("scene camera %s: none", st.s);
            sceneCameras.emplace_back(st.s, std::move(c));
            break;
        }
        case kSpecialSetCamera: // (BtlCameraCtrl::Start: from its start, in place; "GetSet..." the pattern's own)
            if (!st.s) break;
            battleCamera.clear();
            if (std::strncmp(st.s, "GetSet", 6) == 0) setBattleCamera(battle.camera);
            else setBattleCamera(std::string(st.s));
            break;
        case kSpecialSetCameraPosition: {
            const Mat34 p = scenePlace(st.i[0], st.i[1] != 0, st.i[2] != 0, st.i[3]);
            for (int c = 0; c < 3; ++c) battleCameraPlace.m[c * 4 + 3] = p.m[c * 4 + 3] + st.v[c];
            break;
        }
        case kSpecialSetCameraRotation:
            setRotation(battleCameraPlace, scenePlace(st.i[0], st.i[1] != 0, st.i[2] != 0, st.i[3]) * turnY(st.f[0]));
            break;
        case kSpecialSetTargetCamera:
            targetCameraFighter = -1;
            setTargetCamera(scene.target);
            break;
        case kSpecialHit: // (Hit: the next of the action's hits - the numbers at the first)
            if (showIndex < showActions.size()) {
                if (scene.hits == 0) showHitDone = true, showHitAt = battleClock - showStart;
                landHits(showActions[showIndex], scene.hits == 0);
            }
            ++scene.hits;
            break;
        case kSpecialDash: // (SetMovePosition: to its aPos by the target over the time)
            if (actorUnit && scene.target >= 0 && aposPlace(aposName(scene.actor, scene.target), showMoves[0].to)) {
                Move& m = showMoves[0];
                m.unit = fighterUnit[scene.actor], m.active = true;
                m.start = st.time, m.end = st.time + std::max(st.f[0], 0.0f), m.back = -1, m.backEnd = -1;
            }
            break;
        case kSpecialReturn:
            if (showMoves[0].active) showMoves[0].back = st.time, showMoves[0].backEnd = st.time + std::max(st.f[0], 0.0f);
            break;
        case kSpecialPlaySE: { // (-1 the command's launch sound, -2 its hit's, -3 its shot's, -4 the post's; a delay: later)
            const BattleData::Command* c = showIndex < showActions.size() ? battleData.command(showActions[showIndex].command) : nullptr;
            int n = st.i[0];
            if (n == -1) n = c ? c->launchSe : 0;
            else if (n == -2) n = c && c->hitSe > 0 ? c->hitSe : 12;
            else if (n == -3) n = c ? c->shotSe : 0;
            else if (n == -4) n = c ? c->postSe : 0;
            const int clip = n > 0 ? SoundData::battleSe(n) : -1;
            if (clip < 0) break;
            if (st.f[0] > 0) seJobs.push_back({battleClock + st.f[0], clip});
            else audio.playSe(clip, false);
            break;
        }
        case kSpecialPlayEventSE: // (Cr3UtilSoundPlaySE: the clip itself)
            audio.playSe(st.i[0], false);
            break;
        case kSpecialFade: { // (Cr3Fade: 0 in from white, 1 out to white, 2 in from black, 3 out to black; frames of 0.05 s)
            const bool in = st.i[0] == 0 || st.i[0] == 2;
            battleFade.colour = st.i[0] < 2 ? 1 : 0;
            battleFade.alpha.begin(battleClock, in ? 1.0f : 0.0f, in ? 0.0f : 1.0f, float(st.i[1]) * 0.05f);
            break;
        }
        case kSpecialSetSkipEnable: // (ActionSpecialSetSkipEnable: its "enable")
            scene.skippable = st.i[0] != 0;
            break;
        case kSpecialDarwin3Standby: // (BtlMap::LoadSpecialMap: d1154b, not yet drawn)
            if (specialMap.meshes.empty()) {
                std::string err;
                const float keep = time;
                if (!loadFigure("d1154b.geom", specialMap, &err)) SDL_Log("d1154b: %s", err.c_str());
                time = keep;
            }
            specialMapStart = battleClock;
            break;
        case kSpecialGlassFragment: // (the map alone caught at the next frame, broken away over d1154b)
            glassPending = true;
            specialMapShown = !specialMap.meshes.empty();
            break;
        case kSpecialDarwin3ResetMap:
            specialMapShown = false;
            break;
        case kSpecialDarwin3DarwinOffset:   // (Darwin by the one struck: its place, its figure's offset)
        case kSpecialDarwin3CameraOffset: { // (the camera's place by the same)
            BattleUnit* tu = unitOf(scene.target);
            if (!tu) break;
            std::string key = tu->figure;
            const bool flip = scene.target >= 2 && !key.empty() && key[0] == 'j'; // (a monster with a party figure: m5..)
            if (flip && st.op == kSpecialDarwin3DarwinOffset && key.size() >= 2) key[0] = 'm', key[1] = '5';
            const float* off = darwinFigureOffset(key);
            if (!off) break;
            if (st.op == kSpecialDarwin3DarwinOffset) {
                if (actorUnit)
                    for (int c = 0; c < 3; ++c) actorUnit->fig.place.m[c * 4 + 3] = tu->home.m[c * 4 + 3] + off[c] * 0.01f;
            } else {
                const bool paired = battleRules->fighter(scene.target).partner >= 0;
                for (int c = 0; c < 3; ++c) {
                    const float from = paired ? tu->home.m[c * 4 + 3] : battleCameraPlace.m[c * 4 + 3];
                    battleCameraPlace.m[c * 4 + 3] = from + (flip ? -off[c] : off[c]) * 0.01f;
                }
            }
            break;
        }
        case kSpecialDarwin3CameraOffset2: { // (the camera at the size pattern's offset, Darwin moved from his place by it)
            if (scene.target < 0) break;
            const int key = scene.target < 2 ? (battleRules->fighter(scene.target).partner >= 0 ? 14 : 13) : battle.pattern;
            const float* off = darwinPatternOffset(key);
            if (!off) break;
            const float o[3] = {-off[0] * 0.01f, off[1] * 0.01f, -off[2] * 0.01f};
            for (int c = 0; c < 3; ++c) battleCameraPlace.m[c * 4 + 3] = o[c];
            if (actorUnit)
                for (int c = 0; c < 3; ++c) actorUnit->fig.place.m[c * 4 + 3] = actorUnit->home.m[c * 4 + 3] + o[c];
            break;
        }
        case kSpecialLesica1Standby: // (back at its place, its first pose; its partner - 1 - itself, or 5 - itself - hidden)
        case kSpecialLesica2Standby: {
            for (Move& m : showMoves) m.active = false;
            if (actorUnit) actorUnit->fig.place = actorUnit->home;
            if (BattleUnit* p = unitOf(scene.actor < 2 ? 1 - scene.actor : 5 - scene.actor);
                p && battleRules->fighter(scene.actor < 2 ? 1 - scene.actor : 5 - scene.actor).alive)
                p->sceneHidden = true;
            break;
        }
        case kSpecialRuleFixPosition: { // (the party paired - BtlWork +0x523 -: the camera's place and the effect at dPos_pSoloC)
            float p[3];
            if (battleRules->fighter(0).partner < 0 || !dposPosition("dPos_pSoloC", p)) break;
            for (int c = 0; c < 3; ++c) battleCameraPlace.m[c * 4 + 3] = p[c];
            if (SceneEffect* fx = sceneEffect(4))
                for (int c = 0; c < 3; ++c) fx->place.m[c * 4 + 3] = p[c];
            break;
        }
        default: break; // (WaitTime)
        }
    }
    void updateScene() {
        if (!scene.active) return;
        const float t = battleClock - scene.start;
        bool all = true;
        for (size_t k = 0; k < scene.steps.size(); ++k) {
            if (scene.done[k]) continue;
            if (t < scene.steps[k].time) {
                all = false;
                continue;
            }
            scene.done[k] = true;
            runStep(scene.steps[k]);
        }
        if (all) endScene();
    }
    // A tap during a scene (ActionAwakeMain / ActionAdventMain: while it may be skipped - but for
    // coupling 282's command 123 and 221's / 271's 352 -, BtlMain::IsEnabled always so):
    // ActionAwakeSkip / ActionAdventSkip - the moves, voices and sounds stopped, its effects gone,
    // all at their places, then each of its hits not yet landed landed at once, and its end
    bool skipScene() {
        if (!scene.active || !scene.skippable || battleClock < scene.start) return false;
        const int command = showIndex < showActions.size() ? showActions[showIndex].command : 0;
        if ((battle.coupling == 0x11a && command == 0x7b) || ((battle.coupling == 0xdd || battle.coupling == 0x10f) && command == 0x160)) return false;
        SDL_Log("  scene skipped");
        audio.stopAllVoices(0), audio.stopAllSe(0);
        seJobs.clear();
        for (Move& m : showMoves) m.active = false;
        for (BattleUnit& u : battleUnits) u.fig.place = u.home;
        for (size_t k = 0; k < scene.steps.size(); ++k) {
            if (scene.done[k]) continue;
            scene.done[k] = true;
            if (scene.steps[k].op == cr2::kSpecialHit) runStep(scene.steps[k]);
        }
        endScene();
        return true;
    }
    // ActionAwakeCleanup / ActionAdventCleanup: its effects and cameras gone, all shown again at full
    // opacity, the HUD back, the battle's camera
    void endScene() {
        if (!scene.active) return;
        scene.active = false;
        for (auto& [id, fx] : scene.effects)
            if (fx.fig) unload(*fx.fig);
        scene.effects.clear();
        sceneCameras.clear();
        for (BattleUnit& u : battleUnits) u.sceneHidden = false, u.alpha = Ramp(), u.colour = Ramp();
        battleMapHidden = false, mapColour = Ramp();
        specialMapShown = false;
        battleFade = BattleFade();
        for (Move& m : showMoves) m.active = false;
        if (!scene.hits && showIndex < showActions.size()) { // (one without a hit of its own)
            showHitDone = true, showHitAt = battleClock - showStart;
            landHits(showActions[showIndex], true);
        }
        showEnd = battleClock + 0.3f;
        battleCamera.clear();
        setBattleCamera(battle.camera);
    }
    // an effect's frame (BtlEffect::Pose): false once it is over (unloaded)
    bool drawEffect(SceneEffect& fx, const cr2::Mat34& camera, const Mat4& vp) {
        const float t = battleClock - fx.start;
        const bool over = fx.kept ? false : fx.end >= 0 ? battleClock >= fx.end : !fx.set->loop && fx.fig->animLoaded && t > fx.fig->anim.duration();
        if (over) {
            unload(*fx.fig);
            return false;
        }
        if (!fx.shown || t < 0) return true;
        cr2::Mat34 place = fx.place;
        // BtlEffect::Pose: following its model's locator (9 its place, 7 none) and its shift,
        // its turn; turned to the camera
        const short locator = fx.locator >= 0 ? fx.locator : fx.set->locator;
        if (BattleUnit* u = unitOf(fx.follows); u && fx.set->constrain & 3) {
            cr2::Mat34 j = u->fig.place;
            unscaled(j);
            if (locator == 7) j = cr2::Mat34();
            else if (locator != 9) jointPlace(u->fig, cr2::tposJoint(locator), j);
            if (fx.set->constrain & 1)
                for (int c = 0; c < 3; ++c) place.m[c * 4 + 3] = j.m[c * 4 + 3] + fx.set->shift[c];
            if (fx.set->constrain & 2) setRotation(place, j);
        }
        if (fx.shot) {
            if (t > fx.flight) {
                unload(*fx.fig);
                return false;
            }
            const float k = fx.flight > 0 ? t / fx.flight : 1;
            for (int c = 0; c < 3; ++c) place.m[c * 4 + 3] = fx.from[c] + (fx.to[c] - fx.from[c]) * k;
            if (fx.path == 1 && fx.flight > 0) place.m[7] += 2.0f * t - 2.0f / fx.flight * t * t; // (CalcFallMotion: back down at its end)
        }
        if (fx.set->billboard) setRotation(place, camera);
        fx.fig->place = place;
        poseView = camera;
        fx.fig->tint[3] = fx.alpha.at(battleClock);
        pose(*fx.fig, t, fx.set->loop);
        drawFigure(*fx.fig, vp.data());
        return true;
    }
    void drawSceneEffects(const cr2::Mat34& camera, const Mat4& vp) {
        for (auto it = scene.effects.begin(); it != scene.effects.end();)
            it = drawEffect(it->second, camera, vp) ? std::next(it) : scene.effects.erase(it);
        for (size_t k = 0; k < commandEffects.size();)
            if (drawEffect(commandEffects[k], camera, vp)) ++k;
            else commandEffects.erase(commandEffects.begin() + std::ptrdiff_t(k));
        for (auto& g : guardObjects)
            if (g) drawEffect(*g, camera, vp);
    }
    // Defend (BtlCommand::StepGuard, as it's chosen): the one choosing - a pair's both - to its
    // stance bg01 (motion 4), and character 6 its guard, obj303, on its model 0.2 s on
    // (BtlEffectCtrl::CreateEffect(.., 4, hidden, 0.2): ControlWait, then no control - kept); its
    // choice taken back (StepCommandLoop), the stance and obj303 gone. The guard hidden as it
    // attacks (HideAttachObject: a counter), made again 0.5 s after (ShowAttachObject); gone as it
    // falls (ActionDeadStart) and with the turn's effects (BtlTurnEnd: DeleteAll)
    std::unique_ptr<SceneEffect> guardObjects[2];
    bool guardReturn[2] = {false, false};
    void guardObject(int f, float delay) {
        guardObjects[f].reset();
        SceneEffect fx;
        fx.fig = std::make_unique<ViewFigure>();
        std::string err;
        const float keep = time;
        if (!loadFigure("obj303.geom", *fx.fig, &err)) {
            SDL_Log("obj303: %s", err.c_str());
            return;
        }
        const auto it = std::find(fx.fig->anims.begin(), fx.fig->anims.end(), std::string("obj303.anim"));
        if (it != fx.fig->anims.end()) loadAnim(*fx.fig, size_t(it - fx.fig->anims.begin()));
        time = keep;
        fx.set = &cr2::effectSetting("obj303");
        fx.fig->absolute = fx.set->kind != 2;
        fx.start = battleClock + delay, fx.follows = f, fx.kept = true;
        guardObjects[f] = std::make_unique<SceneEffect>(std::move(fx));
    }
    void dropGuardObject(int f) {
        if (guardObjects[f]) unload(*guardObjects[f]->fig);
        guardObjects[f].reset(), guardReturn[f] = false;
    }
    void setGuard(int f, bool on) {
        BattleUnit* u = unitOf(f);
        if (!u || u->guarding == on) return;
        u->guarding = on;
        u->motion = on ? "bg01" : "bb01", u->then = u->motion, u->loop = true, u->hold = false, u->motionStart = battleClock;
        if (on && battleRules->fighter(f).id == 6) guardObject(f, 0.2f);
        else if (!on) dropGuardObject(f);
    }
    // the stances by the orders chosen so far
    void updateGuards() {
        bool want[2] = {false, false};
        for (const cr2::BattleRules::Order& o : commandInput.orders())
            if (o.command == cr2::CommandInput::kDefend && o.actor >= 0 && o.actor < 2) {
                want[o.actor] = true;
                if (commandInput.pairTurn()) want[0] = want[1] = true;
            }
        for (int f = 0; f < 2; ++f)
            if (battleRules->fighter(f).alive) setGuard(f, want[f]);
    }
    // A command's effect (BtlActionCtrl::CreateEffect - its settings' delay - and
    // SetCommandEffectPosition with 9): on the fighter `target` - following its model at the
    // setting's locator (a pair's action's +0x32) while the setting follows the position, else
    // put there once; `side` >= 0: a group's (HitGroupEffect), once at that side's middle
    std::vector<SceneEffect> commandEffects;
    void commandEffect(const std::string& name, int target, bool pair, int side = -1) {
        SceneEffect fx;
        fx.fig = std::make_unique<ViewFigure>();
        std::string err;
        const float keep = time;
        if (!loadFigure(name + ".geom", *fx.fig, &err)) {
            SDL_Log("effect %s: %s", name.c_str(), err.c_str());
            return;
        }
        const auto it = std::find(fx.fig->anims.begin(), fx.fig->anims.end(), name + ".anim");
        if (it != fx.fig->anims.end()) loadAnim(*fx.fig, size_t(it - fx.fig->anims.begin()));
        time = keep;
        fx.set = &cr2::effectSetting(name);
        fx.fig->absolute = fx.set->kind != 2;
        fx.start = battleClock + float(fx.set->delay) * 0.05f;
        if (side >= 0) { // (the side's middle: its standing ones' places)
            float m[3] = {0, 0, 0};
            int n = 0;
            for (int f = 0; f < 5; ++f)
                if (BattleUnit* u = unitOf(f); u && (f < 2 ? 0 : 1) == side && battleRules->fighter(f).alive)
                    for (int c = 0; c < 3; ++c) m[c] += u->home.m[c * 4 + 3], n += c == 0;
            if (n == 0) return;
            for (int c = 0; c < 3; ++c) fx.place.m[c * 4 + 3] = m[c] / float(n) + fx.set->shift[c];
        } else {
            fx.locator = pair ? fx.set->pairLocator : fx.set->locator;
            fx.follows = target;
            if (!(fx.set->constrain & 1)) { // (BtlEffect::SetTargetPosition: put there once)
                if (BattleUnit* u = unitOf(target)) {
                    cr2::Mat34 j = u->fig.place;
                    unscaled(j);
                    if (fx.locator != 9 && fx.locator != 7) jointPlace(u->fig, cr2::tposJoint(fx.locator), j);
                    for (int c = 0; c < 3; ++c) fx.place.m[c * 4 + 3] = j.m[c * 4 + 3] + fx.set->shift[c];
                }
                fx.follows = -1;
            }
        }
        commandEffects.push_back(std::move(fx));
    }

    void drawBattle(int w, int h, bool mapOnly = false) {
        cr2::Mat34 cw;
        float fov = 45, zn = 0.1f, zf = 100;
        if (!battleView(cw, fov, zn, zf)) return;
        dev->setCamera(zn, zf, fov * 3.14159265f / 180);
        const Mat4 world = cr2::mat4FromMat34(cw);
        const Mat4 vp = cr2::mat4Multiply(perspective(fov * 3.14159265f / 180, float(w) / float(std::max(h, 1)), zn, zf),
                                          cr2::mat4RigidInverse(world));
        cr2::SceneLighting l = lighting;
        const float axis[3] = {cw.m[2], cw.m[6], cw.m[10]}; // (the light from the camera)
        const float n = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
        if (n > 0)
            for (int c = 0; c < 3; ++c) l.lightDir[c] = axis[c] / n;
        l.camera[0] = world[12], l.camera[1] = world[13], l.camera[2] = world[14];
        l.time = time;
        dev->setLighting(l);
        battleVP = vp;
        for (DamageShown& d : damageShown) // (the numbers made since the last frame: on the screen by this one's camera)
            if (!d.placed) {
                d.placed = true;
                if (!battleUi(d.at, d.x, d.y)) d.start = -100; // (behind it: not shown)
            }
        if (mapOnly) { // (PostEffectGlassFragments: BtlMap's passes alone, before d1154b replaces it)
            drawFigure(battleMap, vp.data());
            if (!mapEffect.meshes.empty()) drawFigure(mapEffect, vp.data());
            return;
        }
        if (!battleMapHidden) drawFigure(specialMapShown ? specialMap : battleMap, vp.data()); // (BtlMap::Draw: d1154b in its place)
        if (!battleMapHidden && !mapEffect.meshes.empty()) drawFigure(mapEffect, vp.data());
        // BtlShadow (BtlMain::CreatePlayer / CreateMonster: obj001 for each, its target's DummyEP04;
        // Pose / Draw): at the unit's place and turn, 0.0002 up, scaled as that joint is in the
        // world (Cr3GetScaleFromFigure), drawn while the unit shows fully opaque
        const uint32_t ep04 = cr2::nameHash("DummyEP04");
        for (const BattleUnit& u : battleUnits) {
            if (u.hidden || u.sceneHidden || u.fig.tint[3] <= 0.9999f || u.fig.posed.empty()) continue;
            float scale[3] = {1, 1, 1};
            if (const int j = u.fig.skeleton.joint(ep04); j >= 0) {
                const std::vector<cr2::Mat34> w = u.fig.skeleton.world(u.fig.posed, u.fig.place);
                if (size_t(j) < w.size())
                    for (int c = 0; c < 3; ++c)
                        scale[c] = std::sqrt(w[size_t(j)].m[c] * w[size_t(j)].m[c] + w[size_t(j)].m[4 + c] * w[size_t(j)].m[4 + c] +
                                             w[size_t(j)].m[8 + c] * w[size_t(j)].m[8 + c]);
            }
            cr2::Mat34 at = u.fig.place;
            for (int c = 0; c < 3; ++c) { // (the place's turn alone, then the joint's scale)
                const float l = std::sqrt(at.m[c] * at.m[c] + at.m[4 + c] * at.m[4 + c] + at.m[8 + c] * at.m[8 + c]);
                for (int r = 0; r < 3; ++r) at.m[r * 4 + c] *= l > 0 ? scale[c] / l : 0.0f;
            }
            at.m[7] += 0.0002f;
            drawShadow(at, vp.data());
        }
        for (const BattleUnit& u : battleUnits)
            if (!u.hidden && !u.sceneHidden) drawFigure(u.fig, vp.data());
        drawEffects(cw, vp);
        drawSceneEffects(cw, vp);
        drawGlass();
    }

    // The screen's half size in the game's pixels (RenderContext's screen: its height taken as
    // the iPhone's 640 - the shards' speeds are in its pixels)
    void glassSize(int& hw, int& hh) const {
        hh = 320;
        hw = std::max(int(320.0f * float(screenW) / float(std::max(screenH, 1)) + 0.5f), 1);
    }
    // BtlMain::PostEffectGlassFragments: the map alone (passes 2, 4, 8) drawn into render target
    // 5, its texture the shards' (BtlPostEffectGlassFragment::Setup)
    void grabGlass(int fw, int fh) {
        glassPending = false;
        dev->beginFrame(fw, fh);
        placeScreen(fw, fh);
        drawBattle(screenW, screenH, true);
        dev->endScene();
        dev->endFrame(false);
        std::vector<uint8_t> px;
        int w = 0, h = 0;
        if (!dev->readPixels(px, w, h) || screenW <= 0 || screenH <= 0 || screenX + screenW > w || screenY + screenH > h) {
            SDL_Log("the screen breaks away: not caught (%dx%d of %dx%d)", screenW, screenH, w, h);
            return;
        }
        cr2::Image img;
        img.width = screenW, img.height = screenH;
        img.rgba.resize(size_t(screenW) * screenH * 4);
        for (int y = 0; y < screenH; ++y) { // (bottom-up: a render target's v = 0 its bottom)
            const uint8_t* row = &px[(size_t(screenY + screenH - 1 - y) * w + screenX) * 4];
            std::memcpy(&img.rgba[size_t(y) * screenW * 4], row, size_t(screenW) * 4);
        }
        for (size_t k = 3; k < img.rgba.size(); k += 4) img.rgba[k] = 255;
        if (glassTexture.id) dev->destroy(glassTexture);
        glassTexture = dev->upload(img);
        dev->resetHistory();
        int hw = 0, hh = 0;
        glassSize(hw, hh);
        glass.build(hw, hh, glassRng);
        glassStart = battleClock;
        SDL_Log("the screen breaks away: %zu shards (%d x %d)", glass.shards.size(), hw * 2, hh * 2);
    }
    // BtlPostEffectGlassFragment::Draw: the shards (src alpha, one minus), the whole's alpha falling
    void drawGlass() {
        if (!glass.running() || !glassTexture.id) return;
        Mat4 proj = cr2::mat4Identity();
        proj[0] = 1.0f / float(glass.w), proj[5] = 1.0f / float(glass.h);
        const float rgba[4] = {1, 1, 1, glass.alpha()};
        for (const cr2::GlassShard& s : glass.shards) {
            float v[24];
            for (int k = 0; k < 6; ++k) { // (the triangle, then its last corner thrice: nothing)
                const int c = k < 3 ? k : 2;
                v[k * 4 + 0] = s.x[c], v[k * 4 + 1] = s.y[c], v[k * 4 + 2] = s.u[c], v[k * 4 + 3] = s.v[c];
            }
            dev->drawQuad(glassTexture, v, proj.data(), rgba);
        }
    }

    // The battle's effects (BtlEffectCtrl / BtlEffect: an effNNN figure playing its animation once,
    // where it was made - turned to the camera, its quads facing it)
    struct BattleEffect {
        std::unique_ptr<ViewFigure> fig;
        float start = 0, length = 0;
        float at[3] = {0, 0, 0};
    };
    std::vector<BattleEffect> battleEffects;
    void spawnEffect(const std::string& name, const float at[3]) {
        BattleEffect fx;
        fx.fig = std::make_unique<ViewFigure>();
        std::string err;
        const float keep = time;
        if (!loadFigure(name + ".geom", *fx.fig, &err)) {
            SDL_Log("effect %s: %s", name.c_str(), err.c_str());
            return;
        }
        const auto it = std::find(fx.fig->anims.begin(), fx.fig->anims.end(), name + ".anim");
        if (it != fx.fig->anims.end()) loadAnim(*fx.fig, size_t(it - fx.fig->anims.begin()));
        time = keep;
        fx.fig->absolute = true;
        fx.start = battleClock, fx.length = fx.fig->animLoaded ? fx.fig->anim.duration() : 0.5f;
        for (int c = 0; c < 3; ++c) fx.at[c] = at[c];
        battleEffects.push_back(std::move(fx));
    }
    void drawEffects(const cr2::Mat34& camera, const Mat4& vp) {
        for (size_t k = 0; k < battleEffects.size();) {
            BattleEffect& fx = battleEffects[k];
            const float t = battleClock - fx.start;
            if (t > fx.length || t < 0) {
                unload(*fx.fig);
                battleEffects.erase(battleEffects.begin() + std::ptrdiff_t(k));
                continue;
            }
            cr2::Mat34 place = camera;
            for (int c = 0; c < 3; ++c) place.m[c * 4 + 3] = fx.at[c];
            fx.fig->place = place;
            pose(*fx.fig, t, false);
            drawFigure(*fx.fig, vp.data());
            ++k;
        }
    }

    // The event (--event): its runtime, the vista's map figure, a figure for each of the scene's
    // models (in its order)
    std::unique_ptr<cr2::EventRuntime> event;
    bool eventMode = false;
    ViewFigure vistaMap;
    float vistaMapTime = 0; // (its own animation's time: held while SetMapAnimPause says)
    std::vector<ViewFigure> eventFigures;
    std::vector<std::string> eventFigureNames; // (what eventFigures were made for)
    float eventClock = 0;
    float fadeColours[4][3] = {{0, 0, 0}, {1, 1, 1}, {1, 1, 1}, {0, 0, 0}};
    cr2::Texture white;
    bool eventEnded = false, messageLogged = false;

    bool startEvent(const std::string& function, const std::string& map, float page) {
        initEventDrawing();
        pageSeconds = page;
        return startVista(function, map);
    }

    bool eventDrawingReady = false;
    void initEventDrawing() {
        if (eventDrawingReady) return;
        eventDrawingReady = true;
        // the fades' colours (InterfaceMain's fade windows: the figures fadeb, fadew, fadef, their
        // material's DiffuseColor); type 3, the encounter's wipe, is drawEncountWipe's
        const char* fades[3] = {"fadeb", "fadew", "fadef"};
        for (int k = 0; k < 3; ++k) {
            std::vector<uint8_t> d;
            cr2::Geom g;
            if (archive.read(std::string(fades[k]) + ".geom", d) && cr2::parseGeom(d, g) && !g.materials.empty())
                g.materials[0].param(0x33, fadeColours[k], 3);
        }
        cr2::Image px;
        px.width = px.height = 1;
        px.rgba = {255, 255, 255, 255};
        white = dev->upload(px);
    }

    // VistaMain: the map's figure, the event's script (its scene's figures follow it)
    bool startVista(const std::string& function, const std::string& map) {
        std::string err;
        unload(vistaMap);
        vistaMap = ViewFigure();
        for (ViewFigure& f : eventFigures) unload(f);
        eventFigures.clear(), eventFigureNames.clear();
        eventEnded = false, eventClock = 0, messageLogged = false;
        if (map != "null" && !loadFigure(map + ".geom", vistaMap, &err)) SDL_Log("vista %s: %s", map.c_str(), err.c_str());
        vistaMap.absolute = true; // (VistaMain::LoadMapData: slot 0)
        vistaMapTime = 0;
        event = std::make_unique<cr2::EventRuntime>(archive, game);
        event->autoPage = pageSeconds, event->askYesNo = true;
        if (!event->start(function)) {
            SDL_Log("event %s: %s", function.c_str(), event->error().c_str());
            return false;
        }
        eventMode = true;
        dev->resetHistory();
        stepEvent(); // (the update the game starts it in)
        status = function + " in " + map;
        SDL_Log("event %s: vista %s, %zu scene models", function.c_str(), map.c_str(), event->scene().models().size());
        return true;
    }

    // one of the game's updates, the figures following the scene's models
    void stepEvent() {
        if (eventEnded) return;
        if (!event->tick()) {
            SDL_Log("event: %s", event->error().c_str());
            eventEnded = true;
        }
        if (!event->running() && !eventEnded) {
            eventEnded = true;
            SDL_Log("event ended at frame %d (%.2f s)%s", event->frame(), event->frame() * cr2::EventRuntime::kTick,
                    event->battle() >= 0 ? (": battle " + std::to_string(event->battle())).c_str() : "");
            if (playMode) {
                branchAfterVista();
                return;
            }
        }
        if (event->messageShown() != messageLogged) {
            messageLogged = event->messageShown();
            if (messageLogged) SDL_Log("frame %d: a message, %zu pages", event->frame(), event->message().pages.size());
        }
        syncEventFigures(*event);
    }

    // a figure for each of the scene's models, made again when they change (an EvtScene.Init)
    void syncEventFigures(cr2::EventRuntime& ev) {
        std::vector<std::string> names;
        for (const cr2::EventModel& m : ev.scene().models()) names.push_back(m.figure);
        if (names != eventFigureNames) {
            for (ViewFigure& f : eventFigures) unload(f);
            eventFigures.clear();
            for (const std::string& n : names) {
                ViewFigure f;
                std::string err;
                if (!archive.contains(n + ".geom") || !loadFigure(n + ".geom", f, &err)) SDL_Log("event model %s: %s", n.c_str(), err.empty() ? "no figure" : err.c_str());
                eventFigures.push_back(std::move(f));
            }
            eventFigureNames = names;
        }
    }

    // a frame between the event's updates: how far past the last one it is (none once it ended)
    float eventLead() const { return battleMode && battleScript ? battleScriptClock : eventMode ? (eventEnded ? 0.0f : eventClock) : fieldClock; }

    void updateEvent(float dt) {
        if (event && !event->mapAnimPaused) vistaMapTime += dt;
        eventClock = std::min(eventClock + dt, 0.25f);
        for (; eventClock >= cr2::EventRuntime::kTick && eventMode; eventClock -= cr2::EventRuntime::kTick) stepEvent();
    }

    // the models where the scene has them, playing what the script chose (ChangeAnimator: their
    // <figure><suffix>; else the figure's own), their faces as ChangeModelFace left them
    void poseEvent() {
        pose(vistaMap, vistaMapTime);
        poseEventModels(*event);
    }

    void poseEventModels(cr2::EventRuntime& ev) {
        std::vector<cr2::EventModel>& models = ev.scene().models();
        for (size_t i = 0; i < models.size() && i < eventFigures.size(); ++i) {
            ViewFigure& f = eventFigures[i];
            const cr2::EventModel& m = models[i];
            if (!m.visible || f.meshes.empty()) continue;
            // its motion (ChangeAnimator: relative to the base pose), else its own animation alone
            // in slot 0 (absolute: an effect's)
            const std::string want = (m.animation.empty() ? m.figure : m.animation) + ".anim";
            const auto it = std::find(f.anims.begin(), f.anims.end(), want);
            if (it != f.anims.end() && (!f.animLoaded || f.anims[f.animIndex] != want)) {
                const float keep = time;
                loadAnim(f, size_t(it - f.anims.begin()));
                time = keep;
            }
            f.absolute = m.animation.empty();
            // a ChangeAnimatorBlend's fade from the motion before
            f.blendWeight = 1;
            if (m.blendSeconds > 0 && m.animTime + eventLead() < m.blendSeconds && !m.blendFrom.empty()) {
                const std::string from = m.blendFrom + ".anim";
                if (from != f.blendName) {
                    std::vector<uint8_t> d;
                    f.blendName = from;
                    f.blendLoaded = archive.read(from, d) && f.blendAnim.parse(d) && f.blendAnim.joints() == int(f.skeleton.basePose.size());
                }
                f.blendAbsolute = m.blendFrom == m.figure;
                f.blendTime = m.blendFromTime + m.animTime + eventLead();
                f.blendWeight = (m.animTime + eventLead()) / m.blendSeconds;
            }
            // EvtModel::ChangeTexture_Face: the j<NNN>face material's colour texture
            const auto face = ev.faces().find(m.face);
            const uint32_t faceMaterial = cr2::nameHash(("j" + m.figure.substr(1, 3) + "face").c_str());
            // ChangeModelColor: its variant's textures on every material (EvtModel::ChangeTexture_Color)
            const std::string colour = m.color < 0 || m.figure.size() < 4 ? std::string() : "m" + m.figure.substr(1, 3) + "abcd"[m.color] + "01";
            cr2::Texture* ct = colour.empty() ? nullptr : texture(colour);
            cr2::Texture* cn = colour.empty() ? nullptr : texture(colour + "n");
            for (ViewMesh& vm : f.meshes) {
                vm.texture = face != ev.faces().end() && vm.baseMaterial.hash == faceMaterial ? texture(face->second)
                             : ct                                                              ? ct
                                                                                               : texture(vm.baseMaterial.diffuse());
                vm.normalMap = cn ? cn : texture(vm.baseMaterial.texture(0x35)); // (else its own)
            }
            f.place = ev.scene().placeAt(m, eventLead());
            f.tint[3] = m.alpha(eventLead()); // (ChangeAlpha)
            pose(f, m.animTime + eventLead(), m.loop);
        }
    }

    void drawEvent(int w, int h) {
        cr2::Mat34 cw;
        float fov = 45, zn = 0.1f, zf = 100;
        if (event->sceneShown() && event->scene().camera(cw, fov, zn, zf, eventLead())) {
            // a cut (the camera jumps between frames): the upscaler's history says nothing of it
            const float* lp = lastCamera.m;
            const float moved = std::hypot(cw.m[3] - lp[3], cw.m[7] - lp[7], cw.m[11] - lp[11]);
            const float turned = cw.m[2] * lp[2] + cw.m[6] * lp[6] + cw.m[10] * lp[10]; // the view axes' cosine
            if (moved > 0.05f * std::max(zn * 20, 0.1f) || turned < 0.97f) dev->resetHistory();
            lastCamera = cw;
            dev->setCamera(zn, zf, fov * 3.14159265f / 180);
            const Mat4 world = cr2::mat4FromMat34(cw);
            const Mat4 vp = cr2::mat4Multiply(perspective(fov * 3.14159265f / 180, float(w) / float(std::max(h, 1)), zn, zf),
                                              cr2::mat4RigidInverse(world));
            // VistaMain::Draw: the vista light figure's lights for the map and the models
            cr2::SceneLighting l = lighting;
            event->scene().lights(l, eventLead());
            l.camera[0] = world[12], l.camera[1] = world[13], l.camera[2] = world[14];
            l.time = time;
            dev->setLighting(l);
            drawFigure(vistaMap, vp.data());
            const std::vector<cr2::EventModel>& models = event->scene().models();
            for (size_t i = 0; i < models.size() && i < eventFigures.size(); ++i)
                if (models[i].visible) {
                    if (models[i].hasShadow() && models[i].shadow) drawShadow(eventFigures[i].place, vp.data());
                    drawFigure(eventFigures[i], vp.data());
                }
        }
    }

    cr2::Mat34 lastCamera;

    // The message windows, in the interface's space (the hud_camera2D camera: orthographic,
    // 0.96 x 0.64 units, 3:2), fitted to the screen and centred. A window is a figure whose joints
    // type<size>_<align><NN> place its text (Cr3UtilSearchTextPosition: size S 28, M 32, else 64
    // pixels of a 1000-a-unit layout, drawn 4 smaller; L left, C centred, R right) - slot k the
    // joint numbered by the window's table - and call_sendCsr_loop* its page cursor:
    // - the vista's (VistaConversationWindowMenu, vstWin): a black band across the screen's foot
    //   and the name line (vstWin_talk slides it in), stretched across screens wider than 3:2
    //   (SetScale aspect / 1.5); slots 01 the name, 11 / 12 a page's lines, 21 / 22 centred ones;
    // - the narration (NarrationWindowMenu, narrationPos): no figure of its own - the text over the
    //   screen (the fade's black, as the scripts have it), slot k numbered k + 1 in its centred
    //   column (EvtWindow::ShowMessage: a line's slot its place on the page; NarrationWindowMenu
    //   ::SetStringData: column C).
    cr2::TextRasterizer font;
    struct WindowLayout {
        ViewFigure fig;
        std::vector<std::string> joints;
        bool stretch = false; // (across a wide screen)
        bool nameTag = false; // (the name in slot 0, a page's lines from slot 1)
        int linesPerPage = 2; // (with a name tag: slots 1.. the lines, the centred ones after them)
        std::vector<std::string> slots; // slot -> the joint's number
        std::string cursorJoint;
        int window = -1;                // (EventRuntime's)
        std::string inAnim, outAnim;    // (opening / closing animators, posed at the window's age)
        bool lowerBelow3x2 = false;     // (narrower than 3:2: 0.0403125 lower)
        bool centredSlots = false;      // (no name tag: a centred page's lines in the slots after linesPerPage)
        float frame = 0;                // (where its animation was posed: its joints' places)
        bool outBackwards = false;      // (no out animator: the in one played back)
    };
    WindowLayout vistaWindow, narrationWindow, conversationWindow;
    // the tutorials' windows (HelpWindowMenu - SetHelpWindowParameter 0, interface 0x1a: annoWin,
    // the tutorial's - EvtTutoWindow, window 4 -; 1, 0xe: helpWin - EvtAnnounceBWindow, window 3):
    // white text, three lines a page at typeM_L01..L03, centred ones at typeM_C11..C13, their in /
    // out animators, wider across a screen wider than 3:2 (SetScale: aspect / 1.5)
    // (EvtAnnounceAWindow - window 2 - opens annoWin as the tutorials' does: SetHelpWindowParameter 0)
    WindowLayout helpWindow, annoWindow, announceWindow;
    // the system window (EvtSystemWindow, window 5 - SetSystemMessageWindowParameter, interface 0x20:
    // SystemWindowMenu, sysWin_io): three lines at typeM_L01..L03 (its slot table, 0x5afff8), its io
    // animator forward as it opens and back as it closes (InterfaceCloseAnimeSet: ChangeAnime 2)
    WindowLayout systemWindow;
    WindowLayout itemWindow; // (window 7, GetItemWindowMenu: getItem - its text at typeM_C01, the item's icon at call_mainIcon_id00)
    ViewFigure pageCursor;         // sendCsr_loop: once the page is typed
    ViewFigure conversationCursor; // msgCsr_loop: the conversation window's
    struct TextQuad {
        cr2::Texture tex;
        float x = 0, y = 0, w = 0, h = 0; // UI units: the quad's left, centre, size
        float rgb[3] = {1, 1, 1};
        std::vector<float> pen;           // each character's left edge and the end, in UI units
        int from = -1;                    // the page's characters before it (-1: shown whole - the name)
        std::string joint;                // (its place: the joint's as the window's animation has it, + dx, dy)
        float dx = 0, dy = 0;
    };
    std::vector<TextQuad> textQuads;
    std::string shownText; // (what textQuads were made for)
    int shownPx = 0;

    void loadWindow(WindowLayout& w, const char* figure) {
        std::string err;
        if (!loadFigure(std::string(figure) + ".geom", w.fig, &err)) SDL_Log("window %s: %s", figure, err.c_str());
        std::vector<uint8_t> d;
        if (archive.read(std::string(figure) + ".name", d)) w.joints = cr2::parseJointNames(d);
        for (const std::string& j : w.joints)
            if (j.compare(0, 17, "call_sendCsr_loop") == 0 || j.compare(0, 16, "call_msgCsr_loop") == 0) w.cursorJoint = j;
    }

    void loadMessageWindow(const std::string& fontPath, std::vector<uint8_t> fontBytes = {}, const std::string& fontName = {}) {
        loadWindow(vistaWindow, "vstwin");
        vistaWindow.stretch = vistaWindow.nameTag = true;
        vistaWindow.slots = {"01", "11", "12", "21", "22"};
        // the cut-ins' message window (ConversationWindowMenu: msgWin, msgWin_in / msgWin_out; its
        // slots 01 the name, 02..04 a page's three lines - EvtCutinWindow::GetLinePerPage - and
        // 05..07 centred ones)
        loadWindow(conversationWindow, "msgwin");
        conversationWindow.stretch = conversationWindow.nameTag = conversationWindow.lowerBelow3x2 = true;
        conversationWindow.linesPerPage = 3;
        conversationWindow.slots = {"01", "02", "03", "04", "05", "06", "07"};
        conversationWindow.window = cr2::EventRuntime::CutIn;
        conversationWindow.inAnim = "msgwin_in.anim", conversationWindow.outAnim = "msgwin_out.anim";
        loadCutIns();
        for (auto [wl, name, window] : {std::tuple<WindowLayout*, const char*, int>{&helpWindow, "helpwin", cr2::EventRuntime::AnnounceB},
                                         {&annoWindow, "annowin", cr2::EventRuntime::Tutorial},
                                         {&announceWindow, "annowin", cr2::EventRuntime::AnnounceA}}) {
            loadWindow(*wl, name);
            wl->linesPerPage = 3, wl->centredSlots = wl->stretch = true;
            wl->slots = {"01", "02", "03", "11", "12", "13"};
            wl->window = window;
            wl->inAnim = std::string(name) + "_in.anim", wl->outAnim = std::string(name) + "_out.anim";
        }
        loadWindow(systemWindow, "syswin_io");
        systemWindow.linesPerPage = 3, systemWindow.slots = {"01", "02", "03"}, systemWindow.window = cr2::EventRuntime::System;
        systemWindow.inAnim = "syswin_io.anim", systemWindow.outBackwards = true;
        systemWindow.fig.absolute = true; // (the figure's own animator: SetParameterDataBase's)
        loadWindow(itemWindow, "getitem");
        itemWindow.stretch = true, itemWindow.linesPerPage = 1, itemWindow.slots = {"01"}, itemWindow.window = 7;
        itemWindow.inAnim = "getitem_in.anim", itemWindow.outAnim = "getitem_out.anim";
        loadWindow(narrationWindow, "narrationpos");
        for (int k = 1; k <= 30; ++k) {
            char n[4];
            std::snprintf(n, sizeof n, "%02d", k);
            narrationWindow.slots.push_back(n);
        }
        std::string err;
        if (!loadFigure("sendcsr_loop.geom", pageCursor, &err)) SDL_Log("page cursor: %s", err.c_str());
        if (!loadFigure("msgcsr_loop.geom", conversationCursor, &err)) SDL_Log("page cursor: %s", err.c_str());
        if (!fontPath.empty() && !font.load(fontPath, &err)) SDL_Log("font: %s", err.c_str());
        else if (fontPath.empty() && !fontBytes.empty() && !font.loadMemory(std::move(fontBytes), fontName, &err)) SDL_Log("font: %s", err.c_str());
        // (the code points the game's font lacks - Hangul, simplified Chinese - from the system's, as
        // Android's: CR2_FONT_FALLBACK, else a Noto CJK where the distributions put it, else Windows'
        // or macOS's own - one for each script there)
        const char* fb = std::getenv("CR2_FONT_FALLBACK");
        const char* windir = std::getenv("WINDIR");
        const std::string winFonts = std::string(windir ? windir : "C:/Windows") + "/Fonts/";
        std::vector<std::string> fallbacks{cr2::TextRasterizer::firstFont({fb ? fb : "", "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc",
                                                                           "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
                                                                           "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Bold.ttc",
                                                                           "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
                                                                           "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"})};
        if (fallbacks[0].empty())
            fallbacks = {cr2::TextRasterizer::firstFont({winFonts + "malgunbd.ttf", winFonts + "malgun.ttf",
                                                         "/System/Library/Fonts/AppleSDGothicNeo.ttc"}),
                         cr2::TextRasterizer::firstFont({winFonts + "msyhbd.ttc", winFonts + "msyh.ttc",
                                                         "/System/Library/Fonts/PingFang.ttc", "/System/Library/Fonts/Hiragino Sans GB.ttc"})};
        std::string fallback;
        for (const std::string& f : fallbacks) {
            if (f.empty() || !font.loaded()) continue;
            if (!font.addFallback(f, &err)) SDL_Log("font: %s", err.c_str());
            else fallback += (fallback.empty() ? "" : ", ") + f;
        }
        SDL_Log("message windows: vista %zu meshes, narration %zu joints; font %s%s%s", vistaWindow.fig.meshes.size(),
                narrationWindow.joints.size(), font.loaded() ? font.path().c_str() : "none (no text)", fallback.empty() ? "" : ", then ",
                fallback.c_str());
    }

    // the interface's projection over a w x h screen: UI units to clip space (z: the camera's
    // near / far, 2.56 either way)
    static Mat4 uiProjection(int w, int h) {
        const float k = std::min(float(w) / 0.96f, float(h) / 0.64f); // pixels a unit
        Mat4 m = cr2::mat4Identity();
        m[0] = k / (float(w) * 0.5f);
        m[5] = k / (float(h) * 0.5f);
        m[10] = -1.0f / 2.56f;
        return m;
    }

    // a joint of a window's figure, posed, in UI units
    bool windowJoint(const WindowLayout& wl, const std::string& name, float& x, float& y) {
        const auto it = std::find(wl.joints.begin(), wl.joints.end(), name);
        if (it == wl.joints.end() || !wl.fig.skinned) return false;
        std::vector<cr2::JointTransform> p = wl.fig.skeleton.basePose;
        if (wl.fig.animLoaded)
            p = wl.fig.absolute ? wl.fig.anim.poseAbsolute(wl.fig.skeleton.basePose, wl.frame) : wl.fig.anim.poseOver(wl.fig.skeleton.basePose, wl.frame);
        const std::vector<cr2::Mat34> w = wl.fig.skeleton.world(p, wl.fig.place);
        const size_t j = size_t(it - wl.joints.begin());
        if (j >= w.size()) return false;
        x = w[j].m[3], y = w[j].m[7];
        return true;
    }

    // a slot's text joint (type?_?NN, NN the slot's number)
    std::string slotJoint(const WindowLayout& wl, size_t slot) {
        if (slot >= wl.slots.size()) return {};
        for (const std::string& j : wl.joints)
            if (j.size() == 9 && j.compare(0, 4, "type") == 0 && j.compare(7, 2, wl.slots[slot]) == 0) return j;
        return {};
    }

    void drawWindow(WindowLayout& wl, bool showsMessage, int w, int h) {
        cr2::EventRuntime* ev = activeEvents();
        if (!ev) return;
        const float aspect = float(w) / float(std::max(h, 1));
        wl.fig.place = cr2::Mat34();
        if (wl.stretch && aspect >= 1.5f) wl.fig.place.m[0] = aspect / 1.5f;
        else if (wl.lowerBelow3x2) wl.fig.place.m[7] = -0.0403125f;
        const bool closing = wl.window >= 0 && ev->windowClosing(wl.window);
        if (closing) showsMessage = false;
        const std::string speaker = showsMessage && wl.nameTag ? ev->speakerName() : std::string();
        float at = 0; // (where in its animation)
        if (!wl.inAnim.empty()) { // (opening, then closing)
            const std::string want = closing && !wl.outBackwards ? wl.outAnim : wl.inAnim;
            const auto anim = std::find(wl.fig.anims.begin(), wl.fig.anims.end(), want);
            if (anim != wl.fig.anims.end() && (!wl.fig.animLoaded || wl.fig.anims[wl.fig.animIndex] != want)) {
                const float keep = time;
                loadAnim(wl.fig, size_t(anim - wl.fig.anims.begin()));
                time = keep;
            }
            at = std::max(ev->windowAge(wl.window), 0.0f) + eventLead();
            if (closing && wl.outBackwards && wl.fig.animLoaded) at = std::max(wl.fig.anim.duration() - at, 0.0f);
        } else if (wl.nameTag) { // (the vista's: the name line in while someone speaks)
            const std::string want = speaker.empty() ? "vstwin_io.anim" : "vstwin_talk.anim";
            const auto anim = std::find(wl.fig.anims.begin(), wl.fig.anims.end(), want);
            if (anim != wl.fig.anims.end() && (!wl.fig.animLoaded || wl.fig.anims[wl.fig.animIndex] != want)) {
                const float keep = time;
                loadAnim(wl.fig, size_t(anim - wl.fig.anims.begin()));
                time = keep;
            }
        }
        pose(wl.fig, at, false);
        wl.frame = wl.fig.animLoaded ? std::min(at * wl.fig.anim.sampleRate(), float(std::max(wl.fig.anim.frames() - 1, 1))) : 0;
        const Mat4 proj = uiProjection(w, h);
        Mat4 camera = cr2::mat4Identity();
        camera[14] = -0.881f; // (CAM_hud2D's z)
        const Mat4 view = cr2::mat4Multiply(proj, camera);
        cr2::SceneLighting l;
        dev->setLighting(l);
        if (!wl.fig.meshes.empty()) drawFigure(wl.fig, view.data());
        if (!showsMessage) return;
        // the page's text: made again when it changes (or the screen's scale does)
        const float k = std::min(float(w) / 0.96f, float(h) / 0.64f);
        const int px = int(k + 0.5f);
        std::string key = std::to_string(ev->messageWindow()) + "\n" + speaker + "\n" + std::to_string(ev->page()) + "\n";
        for (const cr2::TextLine& ln : ev->message().pages[size_t(ev->page())].lines) key += ln.text + "\n";
        if (key != shownText || px != shownPx) {
            for (TextQuad& q : textQuads) dev->destroy(q.tex);
            textQuads.clear();
            shownText = key, shownPx = px;
            int typedBefore = 0; // (the page's characters before the line: it types after them)
            auto add = [&](const std::string& text, size_t slot, int colour, bool whole) {
                int chars = 0;
                for (unsigned char c : text) chars += (c & 0xc0) != 0x80;
                const int from = whole ? -1 : typedBefore;
                if (!whole) typedBefore += chars;
                const std::string joint = slotJoint(wl, slot);
                float jx, jy;
                if (text.empty() || joint.empty() || !font.loaded() || !windowJoint(wl, joint, jx, jy)) return;
                const int size = joint[4] == 'M' ? 32 : joint[4] == 'S' ? 28 : 64;
                const bool centred = joint[6] == 'C', right = joint[6] == 'R';
                const int drawn = size - 4; // (SetTextRender: the size less 4)
                const int rasterPx = std::max(1, int(float(drawn) / 1000.0f * k + 0.5f));
                int tw = 0, th = 0;
                std::vector<float> pen;
                cr2::Image img = font.render(text, rasterPx, tw, th, &pen);
                if (img.rgba.empty()) return;
                TextQuad q;
                q.tex = dev->upload(img);
                q.from = from;
                const float unitsPerPx = float(drawn) / 1000.0f / float(rasterPx);
                for (float p : pen) q.pen.push_back(p * unitsPerPx);
                q.w = float(tw) * unitsPerPx, q.h = float(th) * unitsPerPx;
                // SetTextOffsetData: left - the joint less half the size; centred - on it; right -
                // ending there; the line's middle (size - height) / 2 from the joint
                q.x = centred ? jx - q.w / 2 : right ? jx - q.w + float(drawn) / 2000.0f : jx - float(drawn) / 2000.0f;
                q.y = jy + (float(drawn) / 1000.0f - q.h) / 2;
                q.joint = joint, q.dx = q.x - jx, q.dy = q.y - jy;
                cr2::textColour(colour, q.rgb);
                textQuads.push_back(q);
            };
            const std::vector<cr2::TextLine>& lines = ev->message().pages[size_t(ev->page())].lines;
            if (wl.nameTag) {
                add(speaker, 0, 0, true);
                const bool centred = !lines.empty() && lines[0].centred;
                for (size_t i = 0; i < lines.size() && i < size_t(wl.linesPerPage); ++i)
                    add(lines[i].text, (centred ? 1 + wl.linesPerPage : 1) + i, lines[i].colour, false);
            } else if (wl.centredSlots) {
                const bool centred = !lines.empty() && lines[0].centred;
                for (size_t i = 0; i < lines.size() && i < size_t(wl.linesPerPage); ++i)
                    add(lines[i].text, (centred ? size_t(wl.linesPerPage) : 0) + i, lines[i].colour, false);
            } else {
                for (size_t i = 0; i < lines.size(); ++i) add(lines[i].text, i, lines[i].colour, false);
            }
        }
        for (TextQuad& q : textQuads) {
            float jx, jy; // (following its joint: a window sliding in)
            if (!q.joint.empty() && windowJoint(wl, q.joint, jx, jy)) q.x = jx + q.dx, q.y = jy + q.dy;
            // as far as it has typed (MenuText: the characters shown so far, the rest cut off)
            float tw = q.w;
            if (q.from >= 0 && !q.pen.empty()) {
                const int n = std::clamp(ev->revealed() - q.from, 0, int(q.pen.size()) - 1);
                if (n == 0) continue;
                tw = std::min(q.w, q.pen[size_t(n)]);
            }
            const float u1 = q.w > 0 ? tw / q.w : 1;
            const float x0 = q.x, x1 = q.x + tw, y0 = q.y - q.h / 2, y1 = q.y + q.h / 2; // (rows top first: v 0 at the top)
            const float v[24] = {x0, y0, 0, 1, x1, y0, u1, 1, x1, y1, u1, 0, x0, y0, 0, 1, x1, y1, u1, 0, x0, y1, 0, 0};
            const float rgba[4] = {q.rgb[0], q.rgb[1], q.rgb[2], 1};
            dev->drawQuad(q.tex, v, proj.data(), rgba);
        }
        // the page cursor, looping, once the page is typed
        float cx, cy;
        ViewFigure& cursor = wl.cursorJoint.compare(0, 16, "call_msgCsr_loop") == 0 ? conversationCursor : pageCursor;
        if (ev->awaitingTap() && !cursor.meshes.empty() && !wl.cursorJoint.empty() && windowJoint(wl, wl.cursorJoint, cx, cy)) {
            cursor.place = cr2::Mat34();
            cursor.place.m[3] = cx, cursor.place.m[7] = cy;
            pose(cursor, time);
            drawFigure(cursor, view.data());
        }
    }

    // The cut-ins' portraits (CutInWindowMenu): `cutIn`, its first material's colour map the face
    // (SetTexture), its DiffuseColor the brightness (ChangeDiffuseColor), posed by the layout's io
    // animation; the other side's joints hidden (GEO_face*_L or _R); on screens wider than 3:2
    // moved toward its side by the margin's width over the 3:2 area's (PositionCorrection: the fit
    // parameter's side - 3 / 6 right, 1 / 4 left - the two-sided layout not moved), narrower ones
    // 0.0403125 lower. Behind the conversation window (their depths: -0.064 against -0.048).
    std::array<ViewFigure, cr2::EventRuntime::kCutIns> cutInFigures;

    void loadCutIns() {
        static const char* kAnim[cr2::EventRuntime::kCutIns] = {"cutinr_io.anim", "cutinlr_io.anim", "cutinl_io.anim",
                                                                "cutinpty_io.anim", "cutinsgl_io.anim"};
        static const char* kHidden[cr2::EventRuntime::kCutIns] = {"_L", "", "_R", "_L", "_R"};
        std::vector<uint8_t> d;
        const std::vector<std::string> joints = archive.read("cutin.name", d) ? cr2::parseJointNames(d) : std::vector<std::string>();
        const float keep = time;
        for (int k = 0; k < cr2::EventRuntime::kCutIns; ++k) {
            ViewFigure& f = cutInFigures[size_t(k)];
            std::string err;
            if (!loadFigure("cutin.geom", f, &err)) {
                SDL_Log("cut-in: %s", err.c_str());
                continue;
            }
            const auto anim = std::find(f.anims.begin(), f.anims.end(), kAnim[k]);
            if (anim != f.anims.end()) loadAnim(f, size_t(anim - f.anims.begin()));
            for (ViewMesh& vm : f.meshes) {
                const uint32_t j = vm.source.palette.empty() ? ~0u : vm.source.palette[0];
                const std::string& name = j < joints.size() ? joints[j] : std::string();
                vm.hidden = *kHidden[k] && name.size() > 2 && name.compare(name.size() - 2, 2, kHidden[k]) == 0;
            }
        }
        time = keep;
    }

    void drawCutIns(int w, int h) {
        cr2::EventRuntime* ev = activeEvents();
        if (!ev) return;
        static const float kSide[cr2::EventRuntime::kCutIns] = {1, 0, -1, 1, -1};
        const float aspect = float(w) / float(std::max(h, 1));
        const Mat4 proj = uiProjection(w, h);
        Mat4 camera = cr2::mat4Identity();
        camera[14] = -0.881f;
        const Mat4 view = cr2::mat4Multiply(proj, camera);
        for (int k = 0; k < cr2::EventRuntime::kCutIns; ++k) {
            const cr2::EventRuntime::Portrait& c = ev->cutIn(k);
            ViewFigure& f = cutInFigures[size_t(k)];
            if (!c.open || f.meshes.empty()) continue;
            const float t = c.closing ? std::max(c.time - eventLead(), 0.0f) : std::min(c.time + eventLead(), cr2::EventRuntime::kCutInSeconds);
            f.place = cr2::Mat34();
            if (aspect < 1.5f) f.place.m[7] = -0.0403125f;
            else {
                const float s = std::min(float(w) / 960.0f, float(h) / 640.0f);
                f.place.m[3] = kSide[k] * float(int((float(w) - s * 960.0f) * 0.5f)) / (s * 960.0f);
            }
            pose(f, t, false);
            cr2::Texture* face = texture(ev->cutInImage(c.texture));
            for (ViewMesh& vm : f.meshes) {
                vm.texture = face ? face : texture(vm.baseMaterial.diffuse());
                std::vector<float>& dc = vm.material.params[0x33];
                if (dc.size() < 4) dc.resize(4, 1.0f);
                dc[0] = dc[1] = dc[2] = c.bright;
            }
            cr2::SceneLighting l;
            dev->setLighting(l);
            drawFigure(f, view.data());
        }
    }

    // The balloons (FldBalloon: the `balloon` figure, looping), drawn after the foreground through
    // the interface's camera: each over its gimmick - its height up - where the field's camera
    // shows that (Cr3UtilSetBalloonPos: projected, then back through the interface's camera)
    Mat4 fieldScene = cr2::mat4Identity();
    ViewFigure balloonFigure;
    bool balloonLoaded = false;
    // The battle's interface (BtlInterface): an interface figure by name, posed by one of its
    // animations; its joints placed
    struct UiFig {
        ViewFigure fig;
        std::vector<std::string> joints, materials;
        bool loaded = false;
    };
    std::map<std::string, UiFig> uiFigs;
    bool loadingUi = false; // (uiFig loading its figure: left unsmoothed)
    UiFig& uiFig(const std::string& name) {
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
    void poseUi(UiFig& u, const std::string& anim, float t, bool loop = false) {
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
    static float uiRight(const UiFig& u) {
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
    bool uiJoint(const UiFig& u, const std::string& joint, float& x, float& y) {
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
    // DotNumId2: a number's digits from hud_dotnum (DotNumParamPackTable: set `set`'s glyph d at
    // set + d, its size w x h pixels of a 1000-a-unit layout and its UVs), right to left from
    // (x, y), `digits` places (the leading ones blank, but a 0)
    struct Glyph {
        float w, h, u0, v0, u1, v1;
    };
    std::vector<Glyph> dotNumGlyphs;
    void loadDotNum() {
        if (!dotNumGlyphs.empty()) return;
        // (DotNumParamPackTable: tools/gen_dotnum_table.py -> gfx/dotnumtable.inc)
        for (const auto& g : kDotNum) dotNumGlyphs.push_back({g[0], g[1], g[2], g[3], g[4], g[5]});
    }
    // (`skip`: the places left empty on the right first - DotNumId2::SetOZNumber's start)
    void drawDigits(int value, int set, int digits, float x, float y, const Mat4& proj, float alpha = 1, float scale = 1, bool zeros = false, int skip = 0) {
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
    // The title screen (`--title`, the options' Title Screen; GameTitleMenu - InterfaceMain's
    // interface 0x3b): logoInc over a fade from black, then `title` (its animators: 1 title_in, 2
    // title_wait, 3 title_loop looping, 4 title_out) coming in over title_bg - stretched past 3:2 -
    // and divaBtn_io from white, BGM 1, looping until a tap anywhere (SE 0xb4): title_wait brings
    // its buttons in (the circle buttons at call_circleBtn_frNN, drawn short of x 0.67 - 08 Start,
    // 20 Options, 24 Scenarios, ScenarioFlowMenu), and title.nut's `title` runs. Leaving them
    // (CloseTitleMenu) title_in runs backwards (PartsBase +0x5c 2). Start: the continue data (cont.txt) - its calendar and
    // "Resume ...?" (104) - else the panel (newlyBtn_io: 09 New Game, 10 Load - only New Game
    // without a save; bossIcon_io's three by the save's flags 1, 3, 4; the save's calendar; btlBack
    // 100 back): New Game "Begin a new game?" (101; with the save's flag 5 102, then 103 for a plain
    // one), Load (menuLoad_io) "Load data?" (107), then "Load complete." (108) a second; then the
    // game. (The site and Game Center links left out.)
    struct TitleMenu {
        int stage = 0; // 0 off; 1 the logo, 2 the title in, 3 waiting for a tap, 4 its buttons
                       // coming in, 5 its buttons, 6 the panel, 7 a question, 8 "Load complete.", 9
                       // options / scenarios, 10 leaving the buttons (then `next`)
        int next = 0;
        float start = 0;
        int ask = 0;        // the question's announcement
        bool tapped = false;
        int action = 0;     // for the main loop: 1 a new game, 2 the save, 3 the continue data
        float doneAt = 0;
        float divaIn = 0, divaOut = -1; // (divaBtn_io: once from the title's start - coming in -, back from the tap - going)
    } titleMenu;
    std::unique_ptr<cr2::EventRuntime> titleEvents; // (title.nut's calls)
    double playSeconds = 0;                         // (the save's play time, cr3_savedata +0x10 / 20)
    static bool saveExists(const char* file) { // (an empty one: DeleteCont's - none)
        std::FILE* f = std::fopen(savePath(file).c_str(), "rb");
        if (!f) return false;
        const bool some = std::fgetc(f) != EOF;
        std::fclose(f);
        return some;
    }
    // a save's flags, its time and play time (its extras)
    struct SaveInfo {
        bool exists = false;
        std::set<int> flags;
        std::time_t saved = 0;
        long playFrames = 0;
    };
    static SaveInfo saveInfo(const char* file) {
        SaveInfo s;
        if (!saveExists(file)) return s;
        std::FILE* f = std::fopen(savePath(file).c_str(), "rb");
        if (!f) return s;
        std::string text;
        char buf[4096];
        for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
        std::fclose(f);
        s.exists = true;
        size_t p = 0;
        while (p < text.size()) {
            const size_t e = text.find('\n', p);
            const std::string line = text.substr(p, e == std::string::npos ? std::string::npos : e - p);
            p = e == std::string::npos ? text.size() : e + 1;
            if (line.compare(0, 6, "flags ") == 0) {
                std::istringstream in(line.substr(6));
                for (int v; in >> v;) s.flags.insert(v);
            } else if (line.compare(0, 6, "saved ") == 0) {
                s.saved = std::time_t(std::atoll(line.c_str() + 6));
            } else if (line.compare(0, 9, "playtime ") == 0) {
                s.playFrames = std::atol(line.c_str() + 9);
            }
        }
        return s;
    }
    void startTitle() {
        titleMenu = TitleMenu();
        titleMenu.stage = 1, titleMenu.start = uiClock;
        titleEvents.reset();
        fieldMode = eventMode = false;
        audio.stopBgm(10);
        SDL_Log("title: the logo");
    }
    bool titleRunning() const { return titleEvents && titleEvents->running(); }
    void titleScript(const char* function) {
        titleEvents = std::make_unique<cr2::EventRuntime>(archive, game);
        titleEvents->language = optionLanguage, titleEvents->autoPage = pageSeconds;
        if (!titleEvents->start("title", function)) {
            SDL_Log("title: %s - %s", function, titleEvents->error().c_str());
            titleEvents.reset();
        }
    }
    float titleAnimLength(const std::string& fig, const std::string& anim) {
        UiFig& u = uiFig(fig);
        poseUi(u, anim, 0);
        return u.fig.animLoaded ? u.fig.anim.duration() : 0.0f;
    }
    // Cr3UtilResetWork(true) / Cr3InitWorkFromBattleData(.., true) - a new game with the clear data:
    // the common data kept (the items, the Oz, the sopia's levels and EXP), each character's weapon
    // and armour (cr3_player_data +0x3c, +0x3e) and seeds, the flags past 3000 and the global work;
    // the levels, the rest of the equipment and the story's flags (0 .. 3000) a new game's
    void carryOver() {
        std::FILE* f = std::fopen(savePath("save1.txt").c_str(), "rb");
        if (!f) return;
        std::string text;
        char buf[4096];
        for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
        std::fclose(f);
        cr2::GameState old;
        if (!old.load(text)) return;
        std::memcpy(game.itemCount, old.itemCount, sizeof game.itemCount);
        game.oz = old.oz;
        std::memcpy(game.sopiaLevel, old.sopiaLevel, sizeof game.sopiaLevel);
        std::memcpy(game.sopiaExp, old.sopiaExp, sizeof game.sopiaExp);
        for (int c = 0; c < 9; ++c) {
            game.equip[c][0] = old.equip[c][0], game.equip[c][1] = old.equip[c][1];
            std::memcpy(game.seed[c], old.seed[c], sizeof game.seed[c]);
        }
        for (size_t k = 3001; k < old.flags.size(); ++k) game.setFlag(int(k), old.flags[k]);
        std::memcpy(game.globalWork, old.globalWork, sizeof game.globalWork);
        SDL_Log("title: the clear data carried over");
    }
    void titleAsk(int id) {
        titleMenu.ask = id, titleMenu.stage = 7, titleMenu.start = uiClock;
        SDL_Log("title: ask %d", id);
    }
    void titleButton(int id) {
        TitleMenu& t = titleMenu;
        if (t.stage == 5) {
            if (id == 8) { // (Start: CloseTitleMenu - then the continue data, else the panel)
                audio.playSe(0xb4, false);
                t.stage = 10, t.start = uiClock, t.next = 6;
            } else if (id == 20) { // (Options: interface 0x3d)
                audio.playSe(0xb4, false);
                campOpen = true, optionsOpen = true, optionsStart = campStart = uiClock;
                t.stage = 9;
            } else if (id == 24) { // (Scenarios: ScenarioFlowMenu, interface 0x73 - the save's progress, Cr3Util_Load_GameData)
                audio.playSe(0xb4, false);
                if (std::FILE* f = std::fopen(savePath("save1.txt").c_str(), "rb")) {
                    std::string text;
                    char buf[4096];
                    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
                    std::fclose(f);
                    game.load(text);
                }
                campOpen = true, campLink = 201, campStart = uiClock, scenarioScroll = 0;
                t.stage = 9;
            }
        } else if (t.stage == 6) {
            if (id == 9) { // (New Game: NewGame's question - with clear data 102)
                audio.playSe(0xb4, false);
                titleAsk(saveInfo("save1.txt").flags.count(5) ? 102 : 101);
            } else if (id == 10 && saveExists("save1.txt")) {
                audio.playSe(0xb4, false);
                titleAsk(107);
            } else if (id == 100) { // (CreateTitleMenu(true): the buttons back)
                audio.playSe(0xb5, false);
                t.stage = 4, t.start = uiClock;
            }
        }
    }
    void titleAnswer(bool yes) {
        TitleMenu& t = titleMenu;
        if (t.stage != 7) return;
        const int ask = t.ask;
        audio.playSe(yes ? 0xbe : 0xb4, false);
        if (ask == 104) { // (the autosave: resumed, else the panel)
            if (yes) t.action = 3, t.stage = 8, t.ask = 0, t.doneAt = uiClock + 1.0f;
            else t.stage = 6, t.start = uiClock;
        } else if (ask == 101 || ask == 103) {
            if (yes) t.action = 1, t.stage = 8, t.ask = 0, t.doneAt = uiClock + 1.0f;
            else t.stage = 6, t.start = uiClock;
        } else if (ask == 102) { // (with the clear data: Cr3UtilResetWork(true) - what it carries over)
            if (yes) t.action = 4, t.stage = 8, t.ask = 0, t.doneAt = uiClock + 1.0f;
            else titleAsk(103);
        } else if (ask == 107) {
            if (yes) t.action = 2, t.stage = 8, t.ask = 108, t.doneAt = uiClock + 2.0f; // ("Load complete." a second, then the fade)
            else t.stage = 6, t.start = uiClock;
        }
    }
    void titleToken(const std::string& tok) {
        if (saveImport.active()) {
            if (tok == "yes") saveImportAnswer(true);
            else if (tok == "no" || tok == "back") saveImportAnswer(false);
            return;
        }

        SDL_Log("title: input %s (stage %d)", tok.c_str(), titleMenu.stage);
        static const std::pair<const char*, int> kButtons[] = {{"start", 8}, {"options", 20}, {"scenarios", 24}, {"new", 9}, {"load", 10}, {"back", 100}};
        if (titleMenu.stage == 3) titleMenu.tapped = true;
        else if (titleMenu.stage == 7 && (tok == "yes" || tok == "no")) titleAnswer(tok == "yes");
        else if (titleMenu.stage == 9) {
            static const std::pair<const char*, int> kOptionTaps[] = {{"import", 3111}, {"reset", 3101}, {"manual", 3102}, {"yes", 3201}, {"no", 3202}};
            for (const auto& [name, id] : kOptionTaps)
                if (tok == name && (optionsOpen || optionsAsk)) return optionTap(id); // (the options' Reset Data and its questions)
            if (tok == "back") optionsOpen = false, campLink = 0;
        } else
            for (const auto& [name, id] : kButtons)
                if (tok == name) titleButton(id);
    }
    void titleTap() {
        if (titleRunning()) return titleEvents->advance();
        if (titleMenu.stage == 3) titleMenu.tapped = true;
    }
    bool titleClick(float fx, float fy) {
        if (titleRunning()) return titleEvents->advance(), true;
        if (titleMenu.stage == 9) return fieldClick(fx, fy);
        if (titleMenu.stage == 3) { // (GameTitleMenu::TouchGestureTap: SE 0xb2, then Update's 0xb4)
            if (!titleMenu.tapped) audio.playSe(0xb2, false);
            return titleMenu.tapped = true;
        }
        const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
        const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
        for (const TapRect& r : tapRects)
            if (x >= std::min(r.x0, r.x1) && x <= std::max(r.x0, r.x1) && y >= std::min(r.y0, r.y1) && y <= std::max(r.y0, r.y1)) {
                if (titleMenu.stage == 7 && (r.id == 7401 || r.id == 7402)) titleAnswer(r.id == 7401);
                else titleButton(r.id);
                return true;
            }
        return true;
    }
    void updateTitle(float dt) {
        TitleMenu& t = titleMenu;
        if (titleEvents) {
            static float clock = 0;
            clock = std::min(clock + dt, 0.25f);
            for (; clock >= cr2::EventRuntime::kTick; clock -= cr2::EventRuntime::kTick)
                if (!titleEvents->tick()) SDL_Log("title: %s", titleEvents->error().c_str());
            if (!titleEvents->running()) titleEvents.reset();
        }
        if (!inputScript.empty() && uiClock >= inputNext && !titleRunning() && (t.stage == 3 || (t.stage >= 5 && t.stage <= 9)) && t.stage != 8 &&
            !(t.stage == 7 && uiClock - t.start < 0.5f)) {
            titleToken(inputScript.front());
            inputScript.pop_front();
            inputNext = uiClock + 0.6f;
        }
        const float age = uiClock - t.start;
        if (t.stage == 1 && age >= std::max(titleAnimLength("logoinc", "logoinc"), 0.1f)) {
            t.stage = 2, t.start = uiClock, t.divaIn = uiClock, t.divaOut = -1;
            audio.playBgm(1, 10);
            SDL_Log("title: the title");
        } else if (t.stage == 2 && age >= titleAnimLength("title", "title_in")) {
            t.stage = 3, t.start = uiClock;
        } else if (t.stage == 3 && t.tapped) {
            t.tapped = false;
            audio.playSe(0xb4, false);
            t.stage = 4, t.start = uiClock, t.next = 1, t.divaOut = uiClock;
        } else if (t.stage == 4 && age >= titleAnimLength("title", "title_wait")) {
            if (t.next == 1) titleScript("title"); // (the first time: Versionup_Title; Tutorial_Credit with flag 5)
            t.stage = 5, t.start = uiClock, t.next = 0;
            SDL_Log("title: the menu");
        } else if (t.stage == 10 && age >= titleAnimLength("title", "title_in")) {
            t.start = uiClock;
            if (saveExists("cont.txt")) { // (the continue data: Tutorial_ContData, its calendar and question)
                titleScript("Tutorial_ContData");
                titleAsk(104);
            } else {
                t.stage = 6;
            }
        } else if (t.stage == 8 && uiClock >= t.doneAt) {
            t.stage = 0; // (the main loop takes it from here: action)
            audio.stopBgm(10);
        } else if (t.stage == 9 && !optionsOpen && !optionsAsk && campLink != 201 && manualStage == 0) {
            campOpen = false; // (the window closed: the title's buttons again)
            t.stage = 5, t.start = uiClock;
        }
        if (t.stage == 9) updateManual(dt);
    }
    void drawCalendar(const SaveInfo& s, const Mat4& view, const Mat4& proj, float t) {
        UiFig& cal = uiFig("calendar_io");
        cal.fig.place = cr2::Mat34();
        poseUi(cal, "calendar_io", t);
        drawFigure(cal.fig, view.data());
        std::tm lt{};
#ifdef _WIN32
        localtime_s(&lt, &s.saved);
#else
        localtime_r(&s.saved, &lt);
#endif
        const long hours = std::min(s.playFrames / 72000, 99L), minutes = std::min(s.playFrames / 1200 - hours * 60, 99L);
        float x, y;
        auto num = [&](const char* j, int v, int digits) { if (uiJoint(cal, j, x, y)) drawDigits(v, 90, digits, x, y, proj, 1, 1, true); };
        auto glyph = [&](const char* j, int g) { if (uiJoint(cal, j, x, y)) drawDigits(0, g, 1, x, y, proj); };
        if (uiJoint(cal, "call_dotNum_id00", x, y)) drawStatusSign(21, x, y, proj);
        glyph("call_dotNum_id01", lt.tm_mon + 1); // (the month's own: glyphs 1 .. 12)
        num("call_dotNum_id03", lt.tm_mday, 2);
        glyph("call_dotNum_id04", 166);
        num("call_dotNum_id08", lt.tm_year + 1900, 4);
        num("call_dotNum_id10", lt.tm_hour, 2);
        glyph("call_dotNum_id11", 163);
        num("call_dotNum_id13", lt.tm_min, 2);
        if (uiJoint(cal, "call_dotNum_id14", x, y)) drawStatusSign(22, x, y, proj);
        num("call_dotNum_id16", int(hours), 2);
        glyph("call_dotNum_id17", 163);
        num("call_dotNum_id19", int(minutes), 2);
    }
    // PartsBase::SetVisible(joint, v): the joint's meshes (their palette's first)
    static void showJoint(UiFig& u, const std::string& joint, bool visible) {
        for (ViewMesh& vm : u.fig.meshes) {
            const uint32_t j = vm.source.palette.empty() ? ~0u : vm.source.palette[0];
            if (j < u.joints.size() && u.joints[j] == joint) vm.hidden = !visible;
        }
    }
    static void hideJoint(UiFig& u, const std::string& joint) { showJoint(u, joint, false); }
    // a circle button drawn at `id`: lit if it has the focus (else as its material is)
    void focusLook(UiFig& c, int id) {
        const bool on = buttonLook ? buttonLook(id) == 2 : focusScreen() && id == keyFocus;
        for (ViewMesh& vm : c.fig.meshes) {
            const uint32_t mi = vm.source.material;
            if (mi >= c.materials.size() || c.materials[mi] != "PRG_shiftR") continue;
            vm.material.params[0x5e] = on ? std::vector<float>{0.5f, 0.0f} : vm.baseMaterial.params[0x5e];
            if (vm.material.params[0x5e].empty()) vm.material.params.erase(0x5e);
        }
    }
    void drawTitleButtons(UiFig& parts, const Mat4& view, int skip) {
        for (const std::string& j : parts.joints) {
            if (j.compare(0, 18, "call_circleBtn_fr") != 0 && j.compare(0, 17, "call_circleBtn_fr") != 0) continue;
            if (skip > 0) { // (CreateCircleButton: the first `skip` left out)
                --skip;
                continue;
            }
            const int id = std::atoi(j.c_str() + 17);
            float x, y;
            if (!uiJoint(parts, j, x, y) || x >= 0.67f) continue; // (GameTitleMenu::Draw: those short of 0.67)
            UiFig& c = uiFig("circlebtn_fr");
            c.fig.place = cr2::Mat34();
            c.fig.place.m[3] = x, c.fig.place.m[7] = y;
            poseUi(c, "circlebtn_fr", float(id) / 20.0f);
            focusLook(c, id);
            drawFigure(c.fig, view.data());
            float sx, sy, ex, ey;
            if (uiJoint(c, "tapAreaS01", sx, sy) && uiJoint(c, "tapAreaE01", ex, ey)) tapRects.push_back({id, sx, sy, ex, ey});
        }
    }
    void drawTitle() {
        TitleMenu& tm = titleMenu;
        tapRects.clear();
        Mat4 camera = cr2::mat4Identity();
        camera[14] = -0.881f;
        const Mat4 proj = uiProjection(screenW, screenH), view = cr2::mat4Multiply(proj, camera);
        cr2::SceneLighting l;
        dev->setLighting(l);
        const float age = uiClock - tm.start;
        static const float kScreen[24] = {-1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1, -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
        const Mat4 id = cr2::mat4Identity();
        if (tm.stage == 1) { // (logoInc, from black over 20 frames)
            UiFig& logo = uiFig("logoinc");
            logo.fig.place = cr2::Mat34();
            poseUi(logo, "logoinc", age);
            drawFigure(logo.fig, view.data());
            const float a = std::max(0.0f, 1.0f - age / (20 * cr2::EventRuntime::kTick));
            const float rgba[4] = {0, 0, 0, a};
            if (a > 0 && white.id) dev->drawQuad(white, kScreen, id.data(), rgba);
            return;
        }
        if (tm.stage == 9) return drawCamp(view, proj, screenW, screenH);
        // the title over its painting: Android's own figure, title_bg (wider than 3:2 stretched) - the iPad's
        // and the Vita's (the trilogy's too) have none, their painting the title's GEO_titleBG
        const float aspect = float(screenW) / float(std::max(screenH, 1));
        UiFig& bg = uiFig("title_bg");
        bg.fig.place = cr2::Mat34();
        if (aspect >= 1.5f) bg.fig.place.m[0] = aspect / 1.5f;
        // (as it rests: posed by an animation of its name it came out all but black - Android's painting is still)
        drawFigure(bg.fig, view.data());
        UiFig& ti = uiFig("title");
        ti.fig.place = cr2::Mat34();
        for (ViewMesh& vm : ti.fig.meshes) vm.hidden = false;
        if (tm.stage == 2) poseUi(ti, "title_in", age);
        else if (tm.stage == 3) poseUi(ti, "title_loop", age, true);
        else if (tm.stage == 4) poseUi(ti, "title_wait", age);
        else if (tm.stage == 5) poseUi(ti, "title_wait", 1e6f); // (its end: the buttons' places)
        else poseUi(ti, "title_in", std::max(titleAnimLength("title", "title_in") - (tm.stage == 10 ? age : 1e6f), 0.0f)); // (backwards: gone)
        if (!bg.fig.meshes.empty()) hideJoint(ti, "GEO_titleBG"); // (title_bg in its place)
        drawFigure(ti.fig, view.data());
        // (GameTitleMenu: divaBtn_io added unlooped - its 0.25 s from nothing to the picture -, played once
        // as the title comes in and held; at the tap played back, gone)
        const float divaLength = titleAnimLength("divabtn_io", "divabtn_io");
        if (tm.stage == 2 || tm.stage == 3 || (tm.stage == 4 && tm.divaOut >= 0 && uiClock - tm.divaOut < divaLength)) {
            UiFig& diva = uiFig("divabtn_io");
            diva.fig.place = cr2::Mat34();
            poseUi(diva, "divabtn_io", tm.stage == 4 ? std::max(divaLength - (uiClock - tm.divaOut), 0.0f) : std::min(uiClock - tm.divaIn, divaLength), false);
            drawFigure(diva.fig, view.data());
        }
        if (tm.stage == 2) { // (from white over 20 frames)
            const float a = std::max(0.0f, 1.0f - age / (20 * cr2::EventRuntime::kTick));
            const float rgba[4] = {1, 1, 1, a};
            if (a > 0 && white.id) dev->drawQuad(white, kScreen, id.data(), rgba);
        }
        if (titleRunning()) return;
        if (tm.stage == 5 || tm.stage == 4) drawTitleButtons(ti, view, 0); // (CircleButtonPose: following their joints)
        const bool panel = tm.stage == 6; // (a question: the panel gone - DeleteCircleButton, its parts, the calendar)
        if (tm.stage == 7 && tm.ask == 107) { // (Load: CreateLoadTitle - menu with menuLoad_io)
            UiFig& mn = uiFig("menu");
            mn.fig.place = cr2::Mat34();
            poseUi(mn, "menuload_io", age);
            drawFigure(mn.fig, view.data());
        }
        if (panel) {
            const SaveInfo s = saveInfo("save1.txt");
            if (s.exists) {
                UiFig& bi = uiFig("bossicon_io");
                bi.fig.place = cr2::Mat34();
                poseUi(bi, "bossicon_io", age);
                for (const char* j : {"GEO_boss1", "GEO_boss2", "GEO_boss3"}) showJoint(bi, j, true);
                if (!s.flags.count(1)) showJoint(bi, "GEO_boss1", false);
                if (!s.flags.count(3)) showJoint(bi, "GEO_boss2", false);
                if (!s.flags.count(4)) showJoint(bi, "GEO_boss3", false);
                drawFigure(bi.fig, view.data());
                drawCalendar(s, view, proj, age);
            }
            UiFig& nb = uiFig("newlybtn_io");
            nb.fig.place = cr2::Mat34();
            poseUi(nb, "newlybtn_io", age);
            drawFigure(nb.fig, view.data());
            if (tm.stage == 6) drawTitleButtons(nb, view, s.exists ? 0 : 1);
            UiFig& bk = uiFig("btlback");
            bk.fig.place = cr2::Mat34();
            poseUi(bk, "btlback_in", age);
            drawFigure(bk.fig, view.data());
            float sx, sy, ex, ey;
            if (tm.stage == 6 && uiJoint(bk, "tapAreaS01", sx, sy) && uiJoint(bk, "tapAreaE01", ex, ey)) tapRects.push_back({100, sx, sy, ex, ey});
        } else if (tm.stage == 7 && tm.ask == 104) {
            drawCalendar(saveInfo("cont.txt"), view, proj, age);
        }
        if (tm.stage == 7) drawAsk(announce(tm.ask), age, view, screenW, screenH, 7401, 7402, true);
        else if (tm.stage == 8 && tm.ask == 108 && uiClock < tm.doneAt - 1.0f) drawAsk(announce(108), age, view, screenW, screenH, 0, 0, true);
        if (tm.stage == 8) { // (Cr3FadeOut(0, 20): to black over the last second)
            const float a = std::clamp(1.0f - (tm.doneAt - uiClock), 0.0f, 1.0f);
            const float rgba[4] = {0, 0, 0, a};
            if (a > 0 && white.id) dev->drawQuad(white, kScreen, id.data(), rgba);
        }
    }

    // BattleWindowEnchant2::SetSign: sign n (EnchantParamPackTable's u0, v0, u1, v1 of
    // hud_enchant) a 0.048-unit square at (x, y) - 1 white "i" (no element), 2 red, 3 cyan, 4 green
    // (the elements 1..3), 25 the clock, the others the states
    void drawEnchant(int sign, float x, float y, const Mat4& proj) {
        static const float kEnchant[26][4] = {
            {0, 0, 0, 0},
            {0.188f, 1, 0.375f, 0.813f}, {0.375f, 1, 0.563f, 0.813f}, {0.563f, 1, 0.75f, 0.813f}, {0.75f, 1, 0.938f, 0.813f},
            {0, 0.813f, 0.188f, 0.625f}, {0.188f, 0.813f, 0.375f, 0.625f}, {0.375f, 0.813f, 0.563f, 0.625f},
            {0.563f, 0.813f, 0.75f, 0.625f}, {0.75f, 0.813f, 0.938f, 0.625f},
            {0, 0.625f, 0.188f, 0.438f}, {0.188f, 0.625f, 0.375f, 0.438f}, {0.375f, 0.625f, 0.563f, 0.438f},
            {0.563f, 0.625f, 0.75f, 0.438f}, {0.75f, 0.625f, 0.938f, 0.438f},
            {0, 0.438f, 0.188f, 0.25f}, {0.188f, 0.438f, 0.375f, 0.25f}, {0.375f, 0.438f, 0.563f, 0.25f},
            {0.563f, 0.438f, 0.75f, 0.25f}, {0.75f, 0.438f, 0.938f, 0.25f},
            {0, 0.25f, 0.188f, 0.062f}, {0.188f, 0.25f, 0.375f, 0.062f}, {0.375f, 0.25f, 0.563f, 0.062f},
            {0.563f, 0.25f, 0.75f, 0.062f}, {0.75f, 0.25f, 0.938f, 0.062f},
            {0, 1, 0.188f, 0.813f}};
        cr2::Texture* tex = texture("hud_enchant", false);
        if (!tex || sign <= 0 || sign >= 26) return;
        const float* g = kEnchant[sign];
        const float s = 0.024f, x0 = x - s, x1 = x + s, y0 = y - s, y1 = y + s;
        const float v[24] = {x0, y0, g[0], g[3], x1, y0, g[2], g[3], x1, y1, g[2], g[1],
                             x0, y0, g[0], g[3], x1, y1, g[2], g[1], x0, y1, g[0], g[1]};
        const float rgba[4] = {1, 1, 1, 1};
        dev->drawQuad(*tex, v, proj.data(), rgba);
    }
    // StatusId2::SetSign: label n of hud_statusAI (StatusIdParamPackTable: its width and height in
    // pixels of a 1000-a-unit layout, u0, v0, u1, v1), from (x, y) on - 1 "HP", 2 "MP", 3 / 7 the
    // status window's others
    void drawStatusSign(int n, float x, float y, const Mat4& proj) {
        static const float kStatus[23][6] = {
            {0, 0, 0, 0, 0, 0}, {40, 24, 0, 1, 0.156f, 0.906f}, {40, 24, 0, 0.906f, 0.156f, 0.813f},
            {72, 24, 0, 0.813f, 0.281f, 0.719f}, {72, 24, 0, 0.719f, 0.281f, 0.625f},
            {24, 24, 0.438f, 1, 0.531f, 0.906f}, {24, 24, 0.438f, 0.906f, 0.531f, 0.813f},
            {48, 24, 0.313f, 0.813f, 0.5f, 0.719f}, {0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0},
            {48, 24, 0.188f, 0.531f, 0.375f, 0.437f}, {112, 24, 0.063f, 0.438f, 0.5f, 0.344f},
            {96, 24, 0.094f, 0.344f, 0.469f, 0.25f}, {60, 24, 0.648f, 1, 0.883f, 0.906f},
            {120, 24, 0.531f, 0.906f, 1, 0.813f}, {120, 24, 0.531f, 0.813f, 1, 0.719f},
            {120, 24, 0.531f, 0.719f, 1, 0.625f}, {120, 24, 0.531f, 0.625f, 1, 0.531f},
            {120, 24, 0.531f, 0.531f, 1, 0.438f}, {120, 24, 0.531f, 0.438f, 1, 0.344f},
            {120, 24, 0.531f, 0.344f, 1, 0.25f}, {112, 32, 0.313f, 0.25f, 0.75f, 0.125f},
            {256, 32, 0, 0.125f, 1, 0}};
        cr2::Texture* tex = texture("hud_statusAI", false);
        if (!tex || n <= 0 || n >= 23 || kStatus[n][0] <= 0) return;
        const float* g = kStatus[n];
        const float x0 = x, x1 = x + g[0] / 1000.0f, y0 = y - g[1] / 2000.0f, y1 = y + g[1] / 2000.0f;
        const float v[24] = {x0, y0, g[2], g[5], x1, y0, g[4], g[5], x1, y1, g[4], g[3],
                             x0, y0, g[2], g[5], x1, y1, g[4], g[3], x0, y1, g[2], g[3]};
        const float rgba[4] = {1, 1, 1, 1};
        dev->drawQuad(*tex, v, proj.data(), rgba);
    }
    // a fighter's element sign (BtlInterfaceCtrl::GetCharaUIState: elements 1..3 bits 1..3, any
    // other bit 0 - SetStatusIcon's bit k sign k + 1)
    static int elementSign(const cr2::Fighter& f) { return f.element >= 1 && f.element <= 3 ? f.element + 1 : 1; }
    // its states' signs after the element's (GetCharaUIState: 0x536648's states as bits - poison 4,
    // blind 5, seal 6, 12 7, 35 9, 30 10, 29 11, 33 12, raised Attack .. Luck 14..18, lowered 19..23,
    // time stopped 24; 11 or 32 bit 8; skill 5, 9 bit 13 - the sign the bit + 1, in the bits' order)
    static std::vector<int> stateSigns(const cr2::Fighter& f) {
        static const int kStates[19] = {0, 1, 2, 12, 35, 30, 29, 33, 5, 6, 7, 8, 9, 13, 14, 15, 16, 17, 21};
        static const int kBits[19] = {4, 5, 6, 7, 9, 10, 11, 12, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24};
        uint32_t bits = 0;
        for (int k = 0; k < 19; ++k)
            if (f.hasState(kStates[k])) bits |= 1u << kBits[k];
        if (f.hasState(11) || f.hasState(32)) bits |= 1u << 8;
        if (f.has(5, 9)) bits |= 1u << 13; // (+0xfb8 bit 9: skill 5, 9 - a monster's flag 1)
        std::vector<int> out;
        for (int b = 4; b < 32; ++b)
            if (bits >> b & 1u) out.push_back(b + 1);
        return out;
    }
    // a line of text at a UI place (centred), made once
    std::map<std::string, TextQuad> uiTexts;
    // `left`: from x (typeS_L: the text's left), else centred on it; `shade` its colour's
    // `maxWidth` (UI units, 0 none): narrowed to fit it (MenuText's width limit)
    // A text's width as drawUiText draws it (UI units: 0 none)
    float uiTextWidth(const std::string& text, float size, int w, int h) {
        if (text.empty() || !font.loaded()) return 0;
        const float k = std::min(float(w) / 0.96f, float(h) / 0.64f);
        const int px = std::max(1, int(size / 1000.0f * k + 0.5f));
        int tw = 0, th = 0;
        const std::string key = text + "\n" + std::to_string(px);
        if (auto it = uiTexts.find(key); it != uiTexts.end()) return it->second.w;
        font.render(text, px, tw, th);
        return float(tw) * (size / 1000.0f / float(px));
    }
    // (its width, UI units: 0 none drawn)
    float uiTextTint[3] = {1, 1, 1}; // (a colour over the text drawn: SetStringData's own, as the worn equipment's yellow)
    // (the text drawn bordered: MenuText::Initialize's 1 / 2 - the lists' rows, the circle buttons'
    // names, the pub's texts, the staff roll -, TextRenderer::SetBorderingEnable 2 on Android and
    // the Vita - 1 on the iPad -: the shadow 2 texture pixels, 0.002, and the line placed 0.002 left
    // and up, MenuText::SetPosition)
    bool uiTextBorder = false;
    float drawUiText(const std::string& text, float size, float x, float y, int w, int h, bool left = false, float shade = 1, float maxWidth = 0) {
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
        const TextQuad& q = it->second;
        const float qw = maxWidth > 0 ? std::min(q.w, maxWidth) : q.w;
        if (border) x -= 0.002f, y += 0.002f;
        const float x0 = left ? x : x - qw / 2, x1 = x0 + qw, y0 = y - q.h / 2, y1 = y + q.h / 2;
        const float v[24] = {x0, y0, 0, 1, x1, y0, 1, 1, x1, y1, 1, 0, x0, y0, 0, 1, x1, y1, 1, 0, x0, y1, 0, 0};
        const float rgba[4] = {shade * uiTextTint[0], shade * uiTextTint[1], shade * uiTextTint[2], 1};
        dev->drawQuad(q.tex, v, uiProjection(w, h).data(), rgba);
        return qw;
    }
    template <typename... A> float drawBorderedText(A&&... a) {
        uiTextBorder = true;
        const float r = drawUiText(std::forward<A>(a)...);
        uiTextBorder = false;
        return r;
    }

    // The battle's HUD: the party's windows (BattleStatusMenu: pc01win / pc02win, their HP and MP
    // bars - btlHpBar_per posed at HP x 5 / max seconds, at least 0.07 - the face, the numbers:
    // pc0NhpNum's call_dotNum_num1, five digits of set 90), the break gauge (BreakWindowMenu:
    // brkWin, brkGauge at (gauge + 4) / 2 seconds - 2 even -, brkInfo's EV / PL / EN), the
    // command's name (btlMsg, typeM_C01) and the damage (DamageInfo00: btlNum, btlNum_fgrN for N
    // digits, a digit of set 20 at each call_dotNum_idNN, at the target's DummyDP01)
    Mat4 battleVP = cr2::mat4Identity();
    // (BtlActionCtrl::ShowDamage: the number - damageinfodata01's set by its kind: 20, 40 a
    // weakness's, 50 / 60 the restores' -, and its mark, DamageInfo01: `bonus` with
    // damageinfodata02's animator - 0 bonus_miss, 2 bonus_crtcl; 1 block, 3 blow - at the same place)
    struct DamageShown {
        int value = 0, set = 20, mark = -1; // (the value -1: the mark alone)
        float x = 0, y = 0, start = 0;
        // where it is in the world: its place on the screen the next frame drawn's (the camera
        // as it is then - a scene's may cut at the hit)
        float at[3] = {0, 0, 0};
        bool placed = false;
    };
    // a point of the battle on the screen in UI units (false: behind the camera)
    bool battleUi(const float* p, float& x, float& y) const {
        const float* mm = battleVP.data();
        const float cw = mm[3] * p[0] + mm[7] * p[1] + mm[11] * p[2] + mm[15];
        if (cw <= 0) return false;
        const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
        x = (mm[0] * p[0] + mm[4] * p[1] + mm[8] * p[2] + mm[12]) / cw * float(screenW) * 0.5f / k;
        y = (mm[1] * p[0] + mm[5] * p[1] + mm[9] * p[2] + mm[13]) / cw * float(screenH) * 0.5f / k;
        return true;
    }
    std::vector<DamageShown> damageShown;
    std::string commandShown;
    float commandShownStart = -10;
    float chargeShown = 0, chargeShownAt = 0; // (the charge gauge's time shown)
    // the command menu (BattleFirstMenu / BattleSoloMenu / BattleSelectMenu: btlBtn and its
    // animator, a circleBtn_fr at each call_circleBtn_frNN joint - its frame NN)
    // `shown`: the buttons to draw (an empty list: every one); each drawn button's tap area kept
    // (tapAreaS01 / tapAreaE01) for clicks; `labels` text at a button's typeM_C01 (the targets')
    void drawCommandMenu(const std::string& anim, float t, const Mat4& view, const std::vector<int>& shown,
                         const std::map<int, std::string>& labels = {}, int w = 0, int h = 0, int frameOf = 0,
                         const std::string& figure = "btlbtn") {
        UiFig& b = uiFig(figure);
        b.fig.place = cr2::Mat34();
        poseUi(b, anim, t);
        drawFigure(b.fig, view.data());
        for (const std::string& j : b.joints) {
            if (j.rfind("call_circleBtn_fr", 0) != 0) continue;
            const int n = std::atoi(j.c_str() + 17);
            if (const char* only = SDL_getenv("CR2_MENU_ONLY"); only && std::atoi(only) != n) continue; // (debugging)
            if (!shown.empty() && std::find(shown.begin(), shown.end(), n) == shown.end()) continue;
            float x, y;
            if (!uiJoint(b, j, x, y)) continue;
            UiFig& c = uiFig("circlebtn_fr");
            c.fig.place = cr2::Mat34();
            c.fig.place.m[3] = x, c.fig.place.m[7] = y;
            poseUi(c, "circlebtn_fr", float(frameOf ? frameOf : n) / 20.0f);
            // CircleBtn::ChangeTapButtonColor: focused (2) PRG_shiftR's texture half across
            // (OffsetSet1 U 0.5), not selectable (0.5) every material's colour at half
            const int look = buttonLook ? buttonLook(n) : focusScreen() && n == keyFocus ? 2 : 1;
            for (ViewMesh& vm : c.fig.meshes) {
                const uint32_t mi = vm.source.material;
                if (mi < c.materials.size() && c.materials[mi] == "PRG_shiftR") vm.material.params[0x5e] = {look == 2 ? 0.5f : 0.0f, 0.0f};
                vm.material.params[0x33] = look == 0 ? std::vector<float>{0.5f, 0.5f, 0.5f, 1.0f} : vm.baseMaterial.params[0x33];
                if (vm.material.params[0x33].empty()) vm.material.params.erase(0x33);
            }
            drawFigure(c.fig, view.data());
            float sx, sy, ex, ey;
            if (uiJoint(c, "tapAreaS01", sx, sy) && uiJoint(c, "tapAreaE01", ex, ey)) tapRects.push_back({n, sx, sy, ex, ey});
            const auto lt = labels.find(n);
            float tx, ty;
            if (lt != labels.end() && w > 0 && uiJoint(c, "typeM_C01", tx, ty)) drawBorderedText(lt->second, 24, tx, ty, w, h);
            float ax, ay;
            if (buttonLevel && w > 0 && uiJoint(c, "call_dotNum_id30", ax, ay)) drawDigits(buttonLevel(n), 120, 3, ax, ay, uiProjection(w, h));
            if (buttonFighter) { // a target's (CircleBtn::SetLevelNumbers / SetHPGauge)
                const int f = buttonFighter(n);
                if (f >= 0 && w > 0) {
                    const cr2::Fighter& fi = battleRules->fighter(f);
                    float lx, ly;
                    if (uiJoint(c, "call_dotNum_id30", lx, ly)) drawDigits(fi.level, 120, 3, lx, ly, uiProjection(w, h));
                    if (uiJoint(c, "call_enchant_id00", lx, ly)) drawEnchant(elementSign(fi), lx, ly, uiProjection(w, h)); // (CircleBtn::SetStatusIcon's first)
                    if (uiJoint(c, "call_enchant_id01", lx, ly)) { // (its states from id01 on, 0.04 apart)
                        const std::vector<int> signs = stateSigns(fi);
                        for (size_t k = 0; k < signs.size() && k < 15; ++k) drawEnchant(signs[k], lx + 0.04f * float(k), ly, uiProjection(w, h));
                    }
                    float hx, hy;
                    if (uiJoint(c, "call_hpBar_per20", hx, hy)) {
                        UiFig& bar = uiFig("btlhpbar_per");
                        bar.fig.place = cr2::Mat34();
                        bar.fig.place.m[3] = hx, bar.fig.place.m[7] = hy;
                        poseUi(bar, "btlhpbar_per", fi.hp > 0 ? std::max(float(fi.hp) * 5.0f / float(std::max(fi.maxHp, 1)), 0.07f) : 0.0f);
                        drawFigure(bar.fig, view.data());
                    }
                }
            }
        }
    }
    // The skill list (BattleSkillListMenu: a ScrollListMenu of scrlMtr_per / scrlBar / scrlWin /
    // scrlBtn ... - GetModelDataName): the frame scrlBar (scrlBar_io), a row scrlBtn (scrlBtn_io)
    // each 0.08 lower (GetColumnSpacing) - its name at typeS_L00 (white; half, not selectable), its
    // number (MP, or the awakening's level: DotNumId2, set 90) at call_dotNum_id01, its icon
    // (mainIcon_id at its frame / 20 s) at call_mainIcon_id11, the focused one's sBtnSlct at
    // call_sBtnSlct14 -, scrlBack (scrlBack_io) to go back. Each row's tap area kept: its id
    struct ListRow {
        int id = 0;
        std::string name;
        int number = 0, icon = 0;
        bool selectable = true;
        std::string help; // (the command's or item's text: "\\n" between its lines)
        int seal = -1; // (ScrollListItem::SetIcon 2: seal_id at call_seal_id12 - 2 "new")
        bool sopiaIcon = false; // (SetIcon 3: `icon` a frame of sopia_id, not mainIcon_id)
        int cost = -1;          // (SetIcon 1: its own cost_id frame - a sopia's level + 6 -, else the list's)
        bool price = false;     // (the shops': its number an OZ price - ScrollListItem::SetOZNumbers)
        bool lacking = false;   // (BattleSkillListMenu: its MP / charge more than there is - SetNumbers' set 100, red)
        bool worn = false;      // (ShopWepon*ListMenu: the one the character wears - its name yellow, 1 1 0)
    };
    // a sopia's row icon (CampSopiaListMenu::SetSopiaList, CampEquipmentListMenu): a human's (its
    // +0x10 0) sopia_id at GetHumanSopiaIconNumber's frame - 1, 4, 3, 2 by its kind (+0x11) -, a
    // monster's mainIcon_id at GetMonsterSopiaIconNumber's - 65, 68, 67, 66
    static void sopiaRowIcon(const cr2::BattleData::Sopia& so, ListRow& r) {
        static const int kHuman[4] = {1, 4, 3, 2}, kMonster[4] = {65, 68, 67, 66};
        const int k = so.kind - 1;
        r.sopiaIcon = so.growth == 0;
        r.icon = k >= 0 && k < 4 ? (r.sopiaIcon ? kHuman[k] : kMonster[k]) : 0;
    }
    // the rows centred on the focused one (ScrollListMenu::PartsPose: two either side shown; the
    // row's own place the one above it), the list's tab (ListTag: tabWin_io, its tabName_id at
    // call_tabName_id posed at the list's frame / 20 s)
    // `tagFrame` / `costIcon`: BattleSkillListMenu::SetListItemParam's by the list - the sopia 9
    // ("Sopia") / 12 ("MP"), the awakenings 11 ("Awake") / 13 ("Charge Lv."), the advents 13 / 13
    // (as the frames read). A row's icon is its command's +0x72 - mainicon_id at icon / 20 s, but
    // for two pairs whose frames lie swapped: 28 / 29, and 70 (the awakenings': its frame "Awake",
    // as the reference video shows) / 71 (the advents')
    // ListTag::SetListTagAnimetion (the shops' equipment: weapons 1 4 6, armour 3 6 2, accessories
    // 5 2 4 - 0x534ec0 -, its own then the other two kinds' small tabs) / SetListTagAnimetionItem
    // (the camp's items: 7 16, the jewels 15 8 - 0x534ecc): the tabs after the list's own (-1 none)
    int listTagExtra[2] = {-1, -1};
    bool listSwitch = false; // (the list's switch arrows: the shops' equipment - its character -, the camp's items - their category)
    void drawSkillList(const std::vector<ListRow>& rows, int focused, float t, const Mat4& view, int w, int h, int tagFrame, int costIcon) {
        UiFig& bar = uiFig("scrlbar");
        bar.fig.place = cr2::Mat34();
        poseUi(bar, "scrlbar_io", t);
        drawFigure(bar.fig, view.data());
        int fi = 0;
        for (size_t k = 0; k < rows.size(); ++k)
            if (rows[k].id == focused) fi = int(k);
        // its thumb (scrlMtr_per, in place: SetScrollBarLength - posed at the part of the list
        // shown, 5 rows, / 20 s by percent; SetScrollBarPosition - lower by the rows above the first
        // shown, of those that do not fit, between the bar's barLimitUp and barLimitLow)
        float ux, uy, lx, ly;
        if (uiJoint(bar, "barLimitUp", ux, uy) && uiJoint(bar, "barLimitLow", lx, ly)) {
            const int n = std::max(int(rows.size()), 1), first = std::clamp(fi - 2, 0, std::max(n - 5, 0));
            const float shown = std::min(5.0f / float(n), 1.0f);
            UiFig& th = uiFig("scrlmtr_per");
            th.fig.place = cr2::Mat34();
            th.fig.place.m[7] = n > 5 ? -(uy - ly) * (1.0f - shown) * float(first) / float(n - 5) : 0.0f;
            poseUi(th, "scrlmtr_per", shown * 100.0f / 20.0f);
            drawFigure(th.fig, view.data());
        }
        // (ScrollItemInitialize / SetLIstBottomItem: four rows or more between two end items - before
        // the first scrlLow, its tap the last row; after the last scrlTop, its tap the first -,
        // fewer without them)
        const int n = int(rows.size());
        const bool ends = n >= 4;
        for (int d = -2; d <= 2; ++d) {
            const int k = fi + d;
            if (k < (ends ? -1 : 0) || k > (ends ? n : n - 1)) continue;
            if (k < 0 || k >= n) {
                const std::string fig = k < 0 ? "scrllow" : "scrltop";
                UiFig& e = uiFig(fig);
                e.fig.place = cr2::Mat34();
                e.fig.place.m[7] = -0.08f * float(d);
                poseUi(e, fig + "_io", t);
                drawFigure(e.fig, view.data());
                float sx, sy, ex, ey;
                if (uiJoint(e, "tapAreaS01", sx, sy) && uiJoint(e, "tapAreaE01", ex, ey)) tapRects.push_back({1000 + (k < 0 ? n - 1 : 0), sx, sy, ex, ey});
                continue;
            }
            const ListRow& r = rows[size_t(k)];
            UiFig& b = uiFig("scrlbtn");
            b.fig.place = cr2::Mat34();
            b.fig.place.m[7] = -0.08f * float(d);
            poseUi(b, "scrlbtn_io", t);
            for (ViewMesh& vm : b.fig.meshes) {
                vm.material.params[0x33] = !r.selectable ? std::vector<float>{0.5f, 0.5f, 0.5f, 1.0f} : vm.baseMaterial.params[0x33];
                if (vm.material.params[0x33].empty()) vm.material.params.erase(0x33);
            }
            drawFigure(b.fig, view.data());
            float x, y;
            // (ScrollListItem::SetStringDataReal: its slot 00's size 2 less in English, French, German,
            // Spanish - Cr3UtilOptionLanguage 1..4 -, then SetTextRender's 4 less)
            const float rowSize = optionLanguage >= 1 && optionLanguage <= 4 ? 22.0f : 24.0f;
            if (r.worn) uiTextTint[2] = 0;
            if (uiJoint(b, "typeS_L00", x, y)) drawBorderedText(r.name, rowSize, x, y, w, h, true, r.selectable || r.worn ? 1.0f : 0.5f);
            uiTextTint[2] = 1;
            if (r.number >= 0 && uiJoint(b, "call_dotNum_id01", x, y)) // (a price: SetOZNumbers' 3 places on, for its "/OZ")
                drawDigits(r.number, r.lacking ? 100 : 90, r.price ? 7 : 4, x, y, uiProjection(w, h), r.selectable || r.lacking ? 1.0f : 0.5f, 1, false, r.price ? 3 : 0);
            if (r.icon > 0 && r.sopiaIcon && uiJoint(b, "call_mainIcon_id11", x, y)) { // (SetIcon 3: sopia_id at the main icon's place)
                UiFig& ic = uiFig("sopia_id");
                ic.fig.place = cr2::Mat34();
                ic.fig.place.m[3] = x, ic.fig.place.m[7] = y;
                poseUi(ic, "sopia_id", float(r.icon) / 20.0f);
                drawFigure(ic.fig, view.data());
            } else if (r.icon > 0 && uiJoint(b, "call_mainIcon_id11", x, y)) {
                UiFig& ic = uiFig("mainicon_id");
                ic.fig.place = cr2::Mat34();
                ic.fig.place.m[3] = x, ic.fig.place.m[7] = y;
                poseUi(ic, "mainicon_id", r.icon == 0x1c ? 1.45f : r.icon == 0x1d ? 1.4f : r.icon == 70 ? 3.55f : r.icon == 71 ? 3.5f : float(r.icon) / 20.0f);
                drawFigure(ic.fig, view.data());
            }
            if (r.seal >= 0 && uiJoint(b, "call_seal_id12", x, y)) {
                UiFig& se = uiFig("seal_id");
                se.fig.place = cr2::Mat34();
                se.fig.place.m[3] = x, se.fig.place.m[7] = y;
                poseUi(se, "seal_id", float(r.seal) / 20.0f);
                drawFigure(se.fig, view.data());
            }
            const int cost = r.cost >= 0 ? r.cost : costIcon;
            if (cost >= 0 && uiJoint(b, "call_cost_id13", x, y)) { // (the number's mark: "MP" ...)
                UiFig& co = uiFig("cost_id");
                co.fig.place = cr2::Mat34();
                co.fig.place.m[3] = x, co.fig.place.m[7] = y;
                poseUi(co, "cost_id", float(cost) / 20.0f);
                drawFigure(co.fig, view.data());
            }
            if (k == fi && uiJoint(b, "call_sBtnSlct14", x, y)) {
                UiFig& sl = uiFig("sbtnslct");
                sl.fig.place = cr2::Mat34();
                sl.fig.place.m[3] = x, sl.fig.place.m[7] = y;
                poseUi(sl, "sbtnslct", t);
                drawFigure(sl.fig, view.data());
            }
            float sx, sy, ex, ey; // (a tap: the focused row's pick, another's focus - its id + 1000)
            if (uiJoint(b, "tapAreaS01", sx, sy) && uiJoint(b, "tapAreaE01", ex, ey))
                tapRects.push_back({k == fi ? (r.selectable ? r.id : -1) : 1000 + k, sx, sy, ex, ey});
        }
        // the focused row's help (scrlWin: its lines at typeS_L01, typeS_L02)
        if (fi < int(rows.size()) && !rows[size_t(fi)].help.empty()) {
            UiFig& win = uiFig("scrlwin");
            win.fig.place = cr2::Mat34();
            poseUi(win, "scrlwin_io", t);
            drawFigure(win.fig, view.data());
            // (BattleSkillListMenu::Update: SetStringData - near black, 0.01; within the window)
            std::string s = rows[size_t(fi)].help; // (its break the text's "\\n")
            const float right = uiRight(win) - 0.03f;
            float x, y;
            // (CR2-U's own longer lines - the settings' -: broken at the last word that fits the window)
            static std::map<std::string, std::string> wrapped; // (by the text and the screen's size)
            if (s.find("\\n") == std::string::npos && uiJoint(win, "typeS_L01", x, y)) {
                const std::string key = s + "\n" + std::to_string(w) + "x" + std::to_string(h);
                auto it = wrapped.find(key);
                if (it == wrapped.end()) {
                    std::string out = s;
                    if (uiTextWidth(s, 26, w, h) > right - x) {
                        size_t cut = std::string::npos; // (the space whose two lines' wider is the narrowest)
                        float best = 1e9f;
                        for (size_t at = s.find(' '); at != std::string::npos; at = s.find(' ', at + 1)) {
                            const float wider = std::max(uiTextWidth(s.substr(0, at), 26, w, h), uiTextWidth(s.substr(at + 1), 26, w, h));
                            if (wider < best) best = wider, cut = at;
                        }
                        if (cut != std::string::npos) out = s.substr(0, cut) + "\\n" + s.substr(cut + 1);
                    }
                    it = wrapped.emplace(key, out).first;
                }
                s = it->second;
            }
            const size_t br = s.find("\\n");
            if (uiJoint(win, "typeS_L01", x, y)) drawUiText(s.substr(0, br), 26, x, y, w, h, true, 0.01f, right - x);
            if (br != std::string::npos && uiJoint(win, "typeS_L02", x, y)) drawUiText(s.substr(br + 2), 26, x, y, w, h, true, 0.01f, right - x);
        }
        UiFig& tag = uiFig("tabwin_io");
        tag.fig.place = cr2::Mat34();
        poseUi(tag, "tabwin_io", t);
        if (tagFrame >= 0) drawFigure(tag.fig, view.data()); // (none for the manual's)
        float x, y;
        for (int k = 0; k < 3 && tagFrame >= 0; ++k) { // (ListTag: its tabName_id at call_tabName_id01 .. 03 - the list's, then the others')
            const int frame = k == 0 ? tagFrame : listTagExtra[k - 1];
            char jn[24];
            std::snprintf(jn, sizeof jn, "call_tabName_id%02d", k + 1);
            if (frame < 0 || !uiJoint(tag, jn, x, y)) continue;
            UiFig& tn = uiFig("tabname_id");
            tn.fig.place = cr2::Mat34();
            tn.fig.place.m[3] = x, tn.fig.place.m[7] = y;
            poseUi(tn, "tabname_id", float(frame) / 20.0f);
            drawFigure(tn.fig, view.data());
            float sx, sy, ex, ey; // (an other's tap area - tapAreaS01 for the second, S02 for the third: 6501, 6502)
            std::snprintf(jn, sizeof jn, "tapAreaS%02d", k), uiJoint(tag, jn, sx, sy);
            char je[24];
            std::snprintf(je, sizeof je, "tapAreaE%02d", k);
            if (k > 0 && uiJoint(tag, jn, sx, sy) && uiJoint(tag, je, ex, ey)) tapRects.push_back({6500 + k, sx, sy, ex, ey});
        }
        if (listSwitch) { // (ScrollListMenu::SetSwitchButton: backCsr - its io, then backCsr_loop - the arrows about Back: tapAreaS01 left 6511, S02 right 6512)
            UiFig& sw = uiFig("backcsr");
            sw.fig.place = cr2::Mat34();
            poseUi(sw, "backcsr_io", t);
            const float io = sw.fig.animLoaded ? sw.fig.anim.duration() : 0.0f;
            if (t >= io) poseUi(sw, "backcsr_loop", t - io, true);
            drawFigure(sw.fig, view.data());
            for (int k = 1; k <= 2; ++k) {
                char sa[16], ea[16];
                std::snprintf(sa, sizeof sa, "tapAreaS%02d", k), std::snprintf(ea, sizeof ea, "tapAreaE%02d", k);
                float sx, sy, ex, ey;
                if (uiJoint(sw, sa, sx, sy) && uiJoint(sw, ea, ex, ey)) tapRects.push_back({6510 + k, sx, sy, ex, ey});
            }
        }
        UiFig& bk = uiFig("scrlback");
        bk.fig.place = cr2::Mat34();
        poseUi(bk, "scrlback_io", t);
        drawFigure(bk.fig, view.data());
        float sx, sy, ex, ey;
        if (uiJoint(bk, "tapAreaS01", sx, sy) && uiJoint(bk, "tapAreaE01", ex, ey)) tapRects.push_back({100, sx, sy, ex, ey});
    }
    // a button's look as drawn: 0 not selectable, 1 as it is, 2 focused (the keys' cursor)
    std::function<int(int)> buttonLook;
    std::function<int(int)> buttonFighter; // (a target button's fighter, -1 none)
    std::function<int(int)> buttonLevel;   // (an awakening button's level)
    std::vector<ListRow> listRows;         // (the skill list as drawn: a tap on a row focuses it)
    // the menu of the choosing's step, the cursor's choice marked (the keys')
    // BattleOptionMenu (an OptionMenu - InterfaceMain's 0x3e - with the battle's labels): the title
    // (`menu` with menuOpt_io), the seat (seatOpt_io): its seven labels at typeM_L01..L07 - BGM,
    // Sound Effects, Voiceovers, Language, Lock Orientation, Title Screen, Manual
    // (announcemenuname 103, 104, 105, 102, 106, 108, 101: InterfaceMain::m_IdCounter + 4) - and
    // the quality (143 High, 144 Middle, 145 Low) at typeM_C01; the three volumes (VolumControlBar:
    // vlmBar_per at call_vlmBar_per13..15, posed at its volume, 0..100, / 20 s - cr3_system_data +0,
    // +4, +8); circleBtn_fr at call_circleBtn_frNN (frame NN), mainIcon_id at call_mainIcon_idNN;
    // btlBack to leave. (No sound here: the volumes kept, the other settings shown.)
    bool optionsOpen = false;
    float optionsStart = 0; // (uiClock's)
    // the language (cr3_system_data +0xc, Cr3UtilOptionLanguage: the messages' own order - 0
    // Japanese, 1 English, 2 French, 3 German, 4 Spanish, 5 / 6 simplified / traditional Chinese,
    // 7 Korean) and the quality (+0x11, SetOffScreenMode: 0 High, 1 Middle, 2 Low - labels 143..145)
    int optionLanguage = 1, optionQuality = 0;
    // the Title Screen's question (BattleOptionMenu::Update 3, 5, 6: the window closed, a system
    // window with announcebattle 0x6d, choice_io's Yes / No - yes to the title, no back)
    bool optionsAsk = false, toTitle = false, optionsWasOpen = false;
    float optionsAskStart = 0;
    bool msgOpen = false;              // (the battle's announcement window: in)
    float msgOpenAt = 0, msgCloseAt = -1; // (its in's start; its out's, -1 none)
    // The title's options (interface 0x3d, TitleOptionMenu): OptionMenu's, but its sixth label Reset
    // Data (announcemenuname 0x6b for 0x6c Title Screen - 0x5be150's labels) and that button asks
    // "erase all game data?" (announce 0x6e), then again (0x6f); yes: Cr3Util_Delete_GameData /
    // ContData / SystemData, Cr3OptionData::Init, Cr3UtilInitWork - the options closed, the title's
    // buttons; no either time, the options again
    int optionsReset = 0; // (1 the first question, 2 the second)
    bool titleOptions() const { return titleMenu.stage == 9; }
    void resetData() {
        for (const char* file : {"save1.txt", "cont.txt", "system.txt"}) { // (CR2-U keeps the last as <file>.bak)
            const std::string path = savePath(file), bak = path + ".bak";
            if (!saveExists(file) && std::strcmp(file, "system.txt") != 0) continue;
            std::remove(bak.c_str());
            std::rename(path.c_str(), bak.c_str());
        }
        systemRead = true, systemFound = false, systemFlags.clear();
        std::fill(std::begin(systemOptions), std::end(systemOptions), -1);
        optionLanguage = 1, optionQuality = 0; // (Cr3OptionData::Init: the volumes full, the language the device's)
        std::fill(std::begin(optionVolume), std::end(optionVolume), 100);
        game = cr2::GameState();
        SDL_Log("options: the game data reset");
    }
    // OptionMenu::TouchSimpleRelease: the seat's tap areas 1 Title Screen, 2 Manual, 3 / 4 the
    // quality back / on (circle 98 on), 5 / 6 the language back / on (circle 99 on); the question's
    // 3201 yes, 3202 no
    void optionTap(int id) {
        switch (id) {
        case 3101:
            optionsOpen = false, optionsAsk = true, optionsAskStart = uiClock;
            if (titleOptions()) optionsReset = 1;
            break;
        case 3102: // (BattleOptionMenu 4 -> 9 -> 0x22: InstructionsWindowMenu, the tutorials' list)
            optionsOpen = false;
            openManual();
            break;
        case 3111: if (titleOptions()) saveImport.begin("cr2", SDL_GetKeyboardFocus()); break;
        case 3112: saveImportAnswer(true); break;
        case 3113: case 3114: saveImportAnswer(false); break;
        case 3110: openSettings(); break; // (CR2-U's Display & Graphics)
        case 4036: case 4037:
            settings.lockOrientation = id == 4036;
            mobile::orientation(window, settings.lockOrientation, settings.landscapeFlipped, settings.lockOrientation);
            orientationApplied = false;
            saveSettings();
            break;
        case 3103: optionQuality = (optionQuality + 2) % 3; break;
        case 3104: case 4098: optionQuality = (optionQuality + 1) % 3; break;
        case 3105: optionLanguage = (optionLanguage + 7) % 8; break;
        case 3106: case 4099: optionLanguage = (optionLanguage + 1) % 8; break;
        case 3201: // (8: the music stopped, InterfaceMain to the title, 0x1d)
            if (optionsReset == 1) { // (TitleOptionMenu 6: the second question)
                optionsReset = 2, optionsAskStart = uiClock;
                break;
            }
            if (optionsReset == 2) { // (8: the data erased; 10, 2: the options closed)
                resetData();
                optionsReset = 0, optionsAsk = false, optionsOpen = false;
                break;
            }
            optionsAsk = false, toTitle = true;
            SDL_Log("options: to the title screen");
            break;
        case 3202: // (7: the options again)
            optionsReset = 0, optionsAsk = false, optionsOpen = true, optionsStart = uiClock;
            break;
        default: break;
        }
        if (id >= 3103 && id <= 3106) SDL_Log("options: language %d, quality %d", optionLanguage, optionQuality);
    }
    // The manual (the options' Manual: BattleOptionMenu 4 -> 9, InterfaceMain's 0x22 -> 0x61,
    // InstructionsWindowMenu): `message\instructions` (u32 the records' offset, the strings', the
    // count; a record 0x28 - the eight languages' titles, +0x20 a story flag it waits for (0 none),
    // +0x24 its tutorial) listed by those whose flag is on (SetTutorialList) in InstructionsListMenu
    // (0x62, a ScrollListMenu: Back, no help). One picked (SetParamNumber 4) plays title.nut's
    // manual_disp - flag 1122 on (the pictures through InterfaceWindow), GetTutorialID the pick:
    // its Tutorial_NNN's pictures (tutorialPic) and AnnounceTutorialTxt's pages in the help window,
    // a tap a page - then the list again (Update 3 -> 0); Back the options again.
    int manualStage = 0; // 0 none, 1 the list, 2 a tutorial playing
    struct ManualEntry {
        std::string title;
        int id = 0;
    };
    std::vector<ManualEntry> manualEntries;
    int manualCursor = 0;
    bool manualToList = true; // (the tutorial over: the list again, else the options - Tutorial_Option's)
    float manualStart = 0, manualClock = 0;
    std::unique_ptr<cr2::EventRuntime> manualEvents;
    void openManual() {
        manualEntries.clear();
        std::vector<uint8_t> d;
        if (archive.read("message\\instructions.dat", d)) {
            auto u32 = [&](size_t o) { return o + 4 <= d.size() ? uint32_t(d[o] | d[o + 1] << 8 | d[o + 2] << 16 | uint32_t(d[o + 3]) << 24) : 0u; };
            const size_t records = u32(0), strings = u32(4);
            for (uint32_t k = 0, n = u32(8); k < n; ++k) {
                const size_t r = records + size_t(k) * 0x28;
                const int flag = int(u32(r + 0x20));
                if (flag != 0 && !game.flag(flag)) continue; // (Cr3UtilFlagCheck)
                ManualEntry e;
                for (size_t c = strings + u32(r + 4u * size_t(std::clamp(optionLanguage, 0, 7))); c < d.size() && d[c]; ++c) e.title += char(d[c]);
                e.id = int(u32(r + 0x24));
                manualEntries.push_back(e);
            }
        }
        manualStage = 1, manualStart = uiClock, manualCursor = 0;
        SDL_Log("manual: %zu entries", manualEntries.size());
    }
    void closeManual() {
        manualStage = 0, manualEvents.reset();
        optionsOpen = true, optionsStart = uiClock;
    }
    void playManual(int k) {
        if (k < 0 || k >= int(manualEntries.size())) return;
        manualCursor = k;
        if (playTutorial("manual_disp", manualEntries[size_t(k)].id, true))
            SDL_Log("manual: tutorial %d (%s)", manualEntries[size_t(k)].id, manualEntries[size_t(k)].title.c_str());
    }
    // title.nut's `function` played over the battle (the manual's manual_disp, the options' own)
    bool playTutorial(const char* function, int id, bool toList) {
        manualEvents = std::make_unique<cr2::EventRuntime>(archive, game);
        manualEvents->language = optionLanguage, manualEvents->autoPage = pageSeconds;
        manualEvents->tutorialId = id;
        if (!manualEvents->start("title", function)) {
            SDL_Log("tutorial: %s", manualEvents->error().c_str());
            manualEvents.reset();
            if (!toList) optionsOpen = true;
            return false;
        }
        manualStage = 2, manualClock = 0, manualToList = toList;
        return true;
    }
    void updateManual(float dt) {
        if (manualStage != 2 || !manualEvents) return;
        manualClock = std::min(manualClock + dt, 0.25f);
        for (; manualClock >= cr2::EventRuntime::kTick; manualClock -= cr2::EventRuntime::kTick)
            if (!manualEvents->tick()) SDL_Log("manual: %s", manualEvents->error().c_str());
        if (!manualEvents->running()) { // (InstructionsWindowMenu 3 -> 0: the list again)
            manualEvents.reset();
            if (manualToList) manualStage = 1, manualStart = uiClock;
            else manualStage = 0, optionsOpen = true, optionsStart = uiClock;
            SDL_Log("tutorial over");
        }
    }
    void drawManualList(const Mat4& view, int w, int h) {
        tapRects.clear();
        std::vector<ListRow> rows;
        for (size_t k = 0; k < manualEntries.size(); ++k) {
            ListRow r;
            r.id = 5000 + int(k), r.name = manualEntries[k].title, r.number = -1;
            rows.push_back(r);
        }
        listRows = rows;
        drawSkillList(rows, 5000 + manualCursor, uiClock - manualStart, view, w, h, -1, -1);
    }
    void drawOptionsAsk(const Mat4& view, int w, int h) { // (Battle / CampOptionMenu: "return to the title?", the title's window)
        tapRects.clear();
        const float t = uiClock - optionsAskStart;
        drawAsk(announce(optionsReset == 1 ? 0x6e : optionsReset == 2 ? 0x6f : 0x6d), t, view, w, h, 3201, 3202, true);
    }
    vitaimport::Session saveImport;
    void saveImportAnswer(bool yes) {
        const bool finished = saveImport.state == vitaimport::Session::Result && saveImport.succeeded;
        const std::string profile = std::filesystem::path(savePath()).parent_path().u8string();
        saveImport.answer(yes, profile);
        if (finished) {
            systemRead = systemFound = false;
            systemFlags.clear();
            optionsOpen = optionsAsk = campOpen = false;
            startTitle();
        }
    }
    int optionVolume[3] = {100, 100, 100}, optionFocus = 0; // (0..100: GetOptionWork's time the volume / 20)
    void drawOptions(const Mat4& view, const Mat4& proj, int w, int h) {
        tapRects.clear();
        const float t = uiClock - optionsStart;
        UiFig& title = uiFig("menu");
        title.fig.place = cr2::Mat34();
        poseUi(title, "menuopt_io", t);
        drawFigure(title.fig, view.data());
        UiFig& seat = uiFig("seatopt_io");
        seat.fig.place = cr2::Mat34();
        poseUi(seat, "seatopt_io", t);
        drawFigure(seat.fig, view.data());
        static const int kLabels[7] = {103, 104, 105, 102, 106, 108, 101};
        float x, y;
        for (int k = 0; k < 7; ++k) {
            char jn[16];
            std::snprintf(jn, sizeof jn, "typeM_L%02d", k + 1);
            const int label = k == 5 && titleOptions() ? 107 : kLabels[k]; // (the title's: Reset Data)
            if (uiJoint(seat, jn, x, y)) drawUiText(messageText("announcemenuname", label), 26, x, y, w, h, true);
        }
        for (const std::string& j : seat.joints) {
            if (j.rfind("call_vlmBar_per", 0) == 0) {
                const int k = std::atoi(j.c_str() + 15) - 13;
                if (k < 0 || k > 2 || !uiJoint(seat, j, x, y)) continue;
                UiFig& bar = uiFig("vlmbar_per");
                bar.fig.place = cr2::Mat34();
                bar.fig.place.m[3] = x, bar.fig.place.m[7] = y;
                poseUi(bar, "vlmbar_per", float(optionVolume[k]) / 20.0f, false);
                drawFigure(bar.fig, view.data());
                float sx, sy, ex, ey;
                if (uiJoint(bar, "slideAreaS01", sx, sy) && uiJoint(bar, "slideAreaE01", ex, ey)) tapRects.push_back({2000 + k, sx, sy, ex, ey});
            } else if (j.rfind("call_circleBtn_fr", 0) == 0 && uiJoint(seat, j, x, y)) {
                UiFig& c = uiFig("circlebtn_fr");
                c.fig.place = cr2::Mat34();
                c.fig.place.m[3] = x, c.fig.place.m[7] = y;
                // (its frame its number - 36 On, 37 Off -; the language's, 99, by the language -
                // UpdateLanguageButton: 0x534750's 41 Japanese, 42 English, ...)
                const int n = std::atoi(j.c_str() + 17);
                static const int kLanguageFrame[8] = {41, 42, 44, 45, 46, 47, 48, 40}; // (0x534728)
                poseUi(c, "circlebtn_fr", float(n == 99 ? kLanguageFrame[std::clamp(optionLanguage, 0, 7)] : n == 98 ? 0 : n) / 20.0f); // (98, the quality's: frame 0, its text over it)
                c.fig.tint[0] = c.fig.tint[1] = c.fig.tint[2] = (n == 36 || n == 37) && ((n == 36) != settings.lockOrientation) ? 0.45f : 1.0f;
                drawFigure(c.fig, view.data());
                c.fig.tint[0] = c.fig.tint[1] = c.fig.tint[2] = 1.0f;
                float sx, sy, ex, ey;
                if ((n == 36 || n == 37 || n == 98 || n == 99) && uiJoint(c, "tapAreaS01", sx, sy) && uiJoint(c, "tapAreaE01", ex, ey)) tapRects.push_back({4000 + n, sx, sy, ex, ey});
            } else if (j.rfind("call_mainIcon_id", 0) == 0 && uiJoint(seat, j, x, y)) {
                UiFig& ic = uiFig("mainicon_id");
                ic.fig.place = cr2::Mat34();
                ic.fig.place.m[3] = x, ic.fig.place.m[7] = y;
                poseUi(ic, "mainicon_id", float(std::atoi(j.c_str() + 16)) / 20.0f);
                drawFigure(ic.fig, view.data());
            }
        }
        if (uiJoint(seat, "typeM_C01", x, y)) drawUiText(messageText("announcemenuname", 143 + optionQuality), 26, x, y, w, h); // (the quality: over its button)

        for (int k = 1; k <= 6; ++k) { // (the seat's own tap areas)
            char sa[16], ea[16];
            std::snprintf(sa, sizeof sa, "tapAreaS%02d", k), std::snprintf(ea, sizeof ea, "tapAreaE%02d", k);
            float sx, sy, ex, ey;
            if (uiJoint(seat, sa, sx, sy) && uiJoint(seat, ea, ex, ey)) tapRects.push_back({3100 + k, sx, sy, ex, ey});
        }
        // CR2-U's own: Display & Graphics - one of the game's oval buttons (circleBtn_fr's frame 0, as the
        // quality's) in the top band, before the quality's back arrow, at its height (G / Tab, or a tap)
        for (const TapRect& q : std::vector<TapRect>(tapRects))
            if (q.id == 3103) {
                UiFig& c = uiFig("circlebtn_fr");
                c.fig.place = cr2::Mat34();
                poseUi(c, "circlebtn_fr", 0);
                float sx, sy, ex, ey;
                if (!uiJoint(c, "tapAreaS01", sx, sy) || !uiJoint(c, "tapAreaE01", ex, ey)) break;
                const float bw = std::abs(ex - sx), cx = std::min(q.x0, q.x1) - 0.015f - bw * 0.5f, cy = (q.y0 + q.y1) * 0.5f;
                const float ox = cx - (sx + ex) * 0.5f, oy = cy - (sy + ey) * 0.5f;
                c.fig.place.m[3] = ox, c.fig.place.m[7] = oy;
                poseUi(c, "circlebtn_fr", 0);
                drawFigure(c.fig, view.data());
                drawUiText("Graphics", 26, cx, cy, w, h, false, 1.0f, bw * 0.9f);
                tapRects.push_back({3110, sx + ox, sy + oy, ex + ox, ey + oy});
                // Import uses another native oval in the open band below the volume bars.
                const float ix = -(sx + ex) * 0.5f, iy = -0.20f - (sy + ey) * 0.5f;
                c.fig.place.m[3] = ix, c.fig.place.m[7] = iy;
                c.fig.tint[0] = c.fig.tint[1] = c.fig.tint[2] = titleOptions() ? 1.0f : 0.4f;
                poseUi(c, "circlebtn_fr", 0);
                drawFigure(c.fig, view.data());
                drawUiText(optionFocus == 3 ? "> Import Vita Saves" : "Import Vita Saves", 22, 0, -0.20f, w, h, false, titleOptions() ? 1.0f : 0.4f, bw * 0.9f);
                c.fig.tint[0] = c.fig.tint[1] = c.fig.tint[2] = 1.0f;
                if (titleOptions()) tapRects.push_back({3111, sx + ix, sy + iy, ex + ix, ey + iy});

                break;
            }
        UiFig& bk = uiFig("btlback");
        bk.fig.place = cr2::Mat34();
        poseUi(bk, "btlback_in", t);
        drawFigure(bk.fig, view.data());
        float sx, sy, ex, ey;
        if (uiJoint(bk, "tapAreaS01", sx, sy) && uiJoint(bk, "tapAreaE01", ex, ey)) tapRects.push_back({100, sx, sy, ex, ey});
        if (saveImport.state == vitaimport::Session::Confirm || saveImport.state == vitaimport::Session::Result) {
            tapRects.clear();
            dev->clearDepth();
            const bool confirm = saveImport.state == vitaimport::Session::Confirm;
            buttonLook = [&](int id) { return !confirm || id == (saveImport.focus == 0 ? 3112 : 3113) ? 2 : 1; };
            drawAsk(saveImport.message, 1.0f, view, w, h, confirm ? 3112 : 3114, confirm ? 3113 : 3114, true, !confirm);
            buttonLook = nullptr;
        } else if (saveImport.state == vitaimport::Session::Picking) tapRects.clear();
        (void)proj;
    }
    void drawChoosing(const Mat4& view, int w, int h) {
        using CI = cr2::CommandInput;
        tapRects.clear();
        const float t = battleClock - menuStart;
        const std::vector<int> c = menuChoices();
        const int cursor = c.empty() ? -1 : c[size_t(std::clamp(menuCursor, 0, int(c.size()) - 1))];
        if (commandInput.menu() == CI::Menu::Target) {
            // BattleSelectMenu: a target button a target, its name on it - n of them the last n of
            // the five places (43 + 5 - n ..)
            std::vector<int> ids;
            std::map<int, std::string> names;
            const int n = std::min(int(commandInput.targets().size()), 5), first = CI::TargetFirst + 5 - n;
            for (int k = 0; k < n; ++k) {
                const int f = commandInput.targets()[size_t(k)];
                ids.push_back(first + k);
                names[first + k] = fighterName(f);
            }
            buttonLook = [&](int id) { return commandInput.targets()[size_t(id - first)] == cursor ? 2 : 1; };
            buttonFighter = [&](int id) { return commandInput.targets()[size_t(id - first)]; };
            drawCommandMenu("btlbtn3rd_io", t, view, ids, names, w, h, CI::TargetFirst);
            buttonLook = nullptr, buttonFighter = nullptr;
            for (TapRect& r : tapRects) r.id = commandInput.targets()[size_t(r.id - first)]; // (a click: the fighter)
        } else if (commandInput.menu() == CI::Menu::Items) {
            // the items (BattleItemListMenu: its icon, its count, the tab's frame 7)
            std::vector<ListRow> rows;
            for (const auto& e : commandInput.items()) {
                const cr2::BattleData::Item* it = battleData.item(e.item);
                rows.push_back({e.item, it ? itemName(e.item) : "?", e.count, it ? it->icon : 0, e.selectable, it ? itemHelp(e.item) : ""});
            }
            listRows = rows;
            drawSkillList(rows, cursor, t, view, w, h, 7, -1);
            return;
        } else if (commandInput.menu() == CI::Menu::Advent) {
            // the advents (SetCommandAdventList / AddCommandAdvent: the charge level its number, the
            // command's +0x72 its icon) on the skill list - "Advent", "Charge Lv."
            std::vector<ListRow> rows;
            for (const auto& a : commandInput.advents()) {
                const cr2::BattleData::Command* c = a.command > 0 ? battleData.command(a.command) : nullptr;
                rows.push_back({a.command, c ? commandName(a.command) : "-", a.level, c ? c->icon : 71, a.selectable, c ? commandHelp(a.command) : ""});
            }
            listRows = rows;
            drawSkillList(rows, cursor, t, view, w, h, 13, 13);
            return;
        } else if (commandInput.menu() == CI::Menu::Awake || commandInput.menu() == CI::Menu::Sopia) {
            // the awakenings (AddCommandAwake: the level its number) or the sopia (AddCommandSopia:
            // its MP) on the skill list
            std::vector<ListRow> rows;
            if (commandInput.menu() == CI::Menu::Awake)
                for (const auto& a : commandInput.awakenings()) {
                    const cr2::BattleData::Command* c = battleData.command(a.command);
                    rows.push_back({a.command, c ? commandName(a.command) : "?", a.level, c ? c->icon : 70, a.selectable, c ? commandHelp(a.command) : ""});
                    rows.back().lacking = a.lacking;
                }
            else
                for (const auto& sc : commandInput.sopiaCommands()) {
                    const cr2::BattleData::Command* c = battleData.command(sc.command);
                    rows.push_back({sc.command, c ? commandName(sc.command) : "?", sc.mp, c ? c->icon : 0, sc.selectable, c ? commandHelp(sc.command) : ""});
                    rows.back().lacking = sc.lacking;
                }
            listRows = rows;
            const bool sopia = commandInput.menu() == CI::Menu::Sopia;
            drawSkillList(rows, cursor, t, view, w, h, sopia ? 9 : 11, sopia ? 12 : 13);
            return; // (its own back button)
        } else {
            std::vector<int> ids;
            for (const auto& b : commandInput.buttons()) ids.push_back(b.id);
            buttonLook = [&](int id) {
                for (const auto& b : commandInput.buttons())
                    if (b.id == id) return !b.selectable ? 0 : id == cursor && keysUsed ? 2 : 1;
                return 1;
            };
            drawCommandMenu(commandInput.menu() == CI::Menu::First ? "btlbtn1st_io" : "btlbtn2nd_io", t, view, ids);
            buttonLook = nullptr;
            for (TapRect& r : tapRects) // (the ones not selectable take no tap)
                for (const auto& b : commandInput.buttons())
                    if (b.id == r.id && !b.selectable) r.id = -1;
        }
        // the back button (btlBack, fit 3) past the first menu
        if (commandInput.menu() != CI::Menu::First) {
            UiFig& bk = uiFig("btlback");
            bk.fig.place = cr2::Mat34();
            poseUi(bk, "btlback_in", t);
            drawFigure(bk.fig, view.data());
            float sx, sy, ex, ey;
            if (uiJoint(bk, "tapAreaS01", sx, sy) && uiJoint(bk, "tapAreaE01", ex, ey)) tapRects.push_back({100, sx, sy, ex, ey});
        }
    }
    // The result window (ResultMenu: ResultTitle - `menu` with menuRslt_io -, ResultSeat -
    // seatRslt_io: once in, the EXP at call_dotNum_id01, 6 digits, and the SP at id11, 4, of set
    // 90 -, the two characters' CharaStatusWindowMenu - campWin with campWin2_io: each one's face
    // (campFace, cmpFaceC00<n> / A00<n>) at call_cmpFace_id000 / 500, name (pcNameEN_id at the
    // character's frame / 20 s) at call_pcNameEN_id021 / 521, labels at call_status_frNNN (the sign
    // NNN, less 500), level (3 digits of set 130) at call_dotNum_id031, HP (5 of 110: the most,
    // glyph 164's "/", the HP - right to left) at id041 / 100 / 051, MP (4 of 110) at 061 / 200 /
    // 071, the EXP to the
    // next level (6 of 110) at 081 - the second's 531 ... 581, 0.01 to the right but its level -,
    // their HP / MP / next bars (cmpHpBar_per ... at its percent / 20 s) -, and the system window
    // (sysWin_io) with "Victory!" (announce 16) at typeM_L01
    // CharaStatusWindowMenu (campWin): the party's windows - campWin2_io for two (`twoAlways`: so
    // for the result's, a lone one in the first), its third - a lone one's - hidden; campWin1_io
    // for one, the third's joints (id9000 ..) - each one's face (campFace, cmpFaceC00<n> / A00<n>)
    // at call_cmpFace_id000 / 500, name (pcNameEN_id at the character's frame / 20 s) at
    // call_pcNameEN_id021 / 521, labels at call_status_frNNN, level (3 digits of set 130) at
    // call_dotNum_id031, HP (5 of 110: the most, glyph 164's "/", the HP) at id041 / 100 / 051, MP
    // (4) at 061 / 200 / 071, the EXP to the next level (6) at 081 - the second's 5NN, 0.01 to the
    // right but its level -; the HP / MP / next bars at their percent / 20 s
    struct StatusShown {
        int id = 0, level = 1, hp = 0, hpMax = 1, mp = 0, mpMax = 0, next = 0, percent = 0;
    };
    void drawStatusWindows(const StatusShown* rows, int count, float t, const Mat4& view, const Mat4& proj, bool twoAlways = false) {
        const bool one = count == 1 && !twoAlways;
        UiFig& win = uiFig("campwin");
        win.fig.place = cr2::Mat34();
        poseUi(win, one ? "campwin1_io" : "campwin2_io", t);
        for (ViewMesh& vm : win.fig.meshes) { // (the parts the window does not use: their joints' meshes)
            const uint32_t j = vm.source.palette.empty() ? ~0u : vm.source.palette[0];
            const std::string jn = j < win.joints.size() ? win.joints[j] : "";
            vm.hidden = one ? jn == "GEO_campWin1" || jn == "GEO_campWin2" : jn == "GEO_campWin3";
        }
        drawFigure(win.fig, view.data());
        auto at = [&](const std::string& joint, const std::string& fig, float pose, bool draw = true) -> UiFig* {
            float x, y;
            if (!uiJoint(win, joint, x, y)) return nullptr;
            UiFig& c = uiFig(fig);
            c.fig.place = cr2::Mat34();
            c.fig.place.m[3] = x, c.fig.place.m[7] = y;
            poseUi(c, fig, pose);
            if (draw) drawFigure(c.fig, view.data());
            return &c;
        };
        float x, y;
        for (int k = 0; k < count && k < 2; ++k) {
            const StatusShown& r = rows[k];
            const std::string b = one ? "900" : k == 0 ? "0" : "5";
            if (UiFig* face = at(one ? std::string("call_cmpFace_id9000") : "call_cmpFace_id" + b + "00", "campface", 0, false)) {
                const std::string id = std::to_string(r.id);
                for (ViewMesh& vm : face->fig.meshes) vm.texture = texture("cmpFaceC00" + id), vm.extraMap = texture("cmpFaceA00" + id);
                drawFigure(face->fig, view.data());
            }
            const std::string p = one ? "90" : b; // (the lone one's joints: id9021, fr9001 ...)
            {
                const NamePlate np = namePlate();
                at(np.joint + p + np.end, np.figure, float(r.id) / 20.0f);
            }
            for (int s : {1, 2, 3, 7})
                if (uiJoint(win, "call_status_fr" + p + "0" + std::to_string(s), x, y)) drawStatusSign(s, x, y, proj);
            const float dx = k == 0 ? 0.0f : 0.01f;
            auto num = [&](const std::string& id, int value, int set, int digits, float off) {
                float nx, ny;
                if (uiJoint(win, "call_dotNum_id" + id, nx, ny)) drawDigits(value, set, digits, nx + off, ny, proj);
            };
            if (one) { // (its level, HP and MP alone)
                num("9031", r.level, 130, 3, 0);
                num("9041", r.hp, 110, 5, 0);
                num("9061", r.mp, 110, 4, 0);
            } else {
                const char* const ids[2][8] = {{"031", "041", "100", "051", "061", "200", "071", "081"},
                                               {"531", "541", "500", "551", "561", "600", "571", "581"}};
                num(ids[k][0], r.level, 130, 3, 0);
                num(ids[k][1], r.hpMax, 110, 5, dx); // (SetParameterNumberID: the first the most, on the right)
                num(ids[k][2], 0, 164, 1, dx);
                num(ids[k][3], r.hp, 110, 5, dx);
                num(ids[k][4], r.mpMax, 110, 4, dx);
                num(ids[k][5], 0, 164, 1, dx);
                num(ids[k][6], r.mp, 110, 4, dx);
                num(ids[k][7], r.next, 110, 6, dx);
            }
            // the bars (their joints in order: the first's, the second's; the lone one's its own)
            const float hpT = r.hp > 0 ? std::max(float(r.hp) * 5.0f / float(std::max(r.hpMax, 1)), 0.07f) : 0.0f;
            const float mpT = r.mpMax > 0 ? float(r.mp) * 5.0f / float(r.mpMax) : 0.0f;
            const struct { const char* prefix; const char* fig; float pose; } bars[3] = {
                {"call_cmpHpBar_per", "cmphpbar_per", hpT}, {"call_cmpMpBar_per", "cmpmpbar_per", mpT}, {"call_cmpNxBar_per", "cmpnxbar_per", float(r.percent) / 20.0f}};
            for (const auto& bar : bars) {
                int seen = 0;
                for (const std::string& j : win.joints) {
                    if (j.rfind(bar.prefix, 0) != 0) continue;
                    const bool lone = j.size() > std::strlen(bar.prefix) && j[std::strlen(bar.prefix)] == '9';
                    if (lone != one) continue;
                    if (seen++ == (one ? 0 : k)) at(j, bar.fig, bar.pose);
                }
            }
        }
    }
    void drawResult(const Mat4& view, const Mat4& proj, int w, int h) {
        const float t = battleClock - result.windowStart;
        auto part = [&](const std::string& fig, const std::string& anim, const char* hide = nullptr) -> UiFig& {
            UiFig& u = uiFig(fig);
            u.fig.place = cr2::Mat34();
            poseUi(u, anim, t);
            if (hide) // (a part the window does not use: its joint's meshes)
                for (ViewMesh& vm : u.fig.meshes) {
                    const uint32_t j = vm.source.palette.empty() ? ~0u : vm.source.palette[0];
                    vm.hidden = j < u.joints.size() && u.joints[j] == hide;
                }
            drawFigure(u.fig, view.data());
            return u;
        };
        part("menu", "menurslt_io");
        UiFig& seat = part("seatrslt_io", "seatrslt_io");
        float x, y;
        if (battleClock >= result.transferStart) {
            if (uiJoint(seat, "call_dotNum_id01", x, y)) drawDigits(result.exp, 90, 6, x, y, proj);
            if (uiJoint(seat, "call_dotNum_id11", x, y)) drawDigits(result.sp, 90, 4, x, y, proj);
        }
        StatusShown rows[2];
        int count = 0;
        for (int k = 0; k < 2; ++k) {
            const ResultMember& r = result.member[k];
            if (r.ch < 0) continue;
            const ResultNow n = resultNow(r);
            rows[count++] = {r.id, n.level, n.hp, r.hpMax, r.mp, r.mpMax, n.next, n.percent};
        }
        drawStatusWindows(rows, count, t, view, proj, true);
        UiFig& sys = part("syswin_io", "syswin_io");
        const std::string& text = result.message >= 0 && result.message < int(result.messages.size()) ? result.messages[size_t(result.message)] : announce(16);
        if (uiJoint(sys, "typeM_L01", x, y)) drawUiText(text, 24, x, y, w, h, true);
    }
    void drawBattleHud(int w, int h) {
        if (!(gameOver.stage > 0 && gameOver.hidden)) drawBattleHudParts(w, h);
        if (gameOver.stage == 0 || !battleMode || !battleRules) return;
        const Mat4 proj = uiProjection(w, h);
        Mat4 camera = cr2::mat4Identity();
        camera[14] = -0.881f;
        const Mat4 view = cr2::mat4Multiply(proj, camera);
        tapRects.clear();
        drawGameOver(view, w, h);
    }
    void drawBattleHudParts(int w, int h) {
        if (!battleMode || !battleRules) return;
        drawBattleHudMain(w, h);
        if (breakShown) { // (the turn break's figure, once)
            const Mat4 proj = uiProjection(w, h);
            Mat4 camera = cr2::mat4Identity();
            camera[14] = -0.881f;
            const Mat4 view = cr2::mat4Multiply(proj, camera);
            const char* name = breakShown == 1 ? "turnbrk" : "turnbrkenemy";
            UiFig& f = uiFig(name);
            f.fig.place = cr2::Mat34();
            const float t = battleClock - breakShownAt;
            poseUi(f, name, t);
            if (!f.fig.animLoaded || t > f.fig.anim.duration()) breakShown = 0;
            else drawFigure(f.fig, view.data());
        }
    }
    void drawBattleHudMain(int w, int h) {
        if (battleScript && !battleScript->statusShown()) return; // (its talk on)
        const Mat4 proj = uiProjection(w, h);
        Mat4 camera = cr2::mat4Identity();
        camera[14] = -0.881f;
        const Mat4 view = cr2::mat4Multiply(proj, camera);
        cr2::SceneLighting l;
        dev->setLighting(l);
        if (result.stage >= 3) { // (BtlInterfaceCtrl::HideBattleStatus: the window alone)
            drawResult(view, proj, w, h);
            return;
        }

        if (choosing && manualStage == 1) {
            drawManualList(view, w, h);
            return;
        }
        if (choosing && manualStage == 2) return; // (the tutorial's picture and window alone)
        if (choosing && settingsOpen) {
            drawSettings(view, w, h);
            return;
        }
        if (choosing && optionsOpen) { // (StepOption: SetBattleStatusDisplay off)
            drawOptions(view, proj, w, h);
            return;
        }
        if (choosing && optionsAsk) {
            drawOptionsAsk(view, w, h);
            return;
        }
        for (int k = 0; k < 2 && !scene.active; ++k) { // (a scene's: SetBattleStatusDisplay off)
            const cr2::Fighter& f = battleRules->fighter(k);
            if (!f.present) continue;
            const std::string n = k == 0 ? "pc01" : "pc02";
            UiFig& win = uiFig(n + "win");
            win.fig.place = cr2::Mat34();
            poseUi(win, n + "win", 0);
            drawFigure(win.fig, view.data());
            auto child = [&](const std::string& joint, const std::string& figure, float t, bool loop) -> UiFig* {
                float x, y;
                if (!uiJoint(win, joint, x, y)) return nullptr;
                UiFig& c = uiFig(figure);
                c.fig.place = cr2::Mat34();
                c.fig.place.m[3] = x, c.fig.place.m[7] = y + 1.0f / float(std::max(h, 1)); // (+ a pixel)
                poseUi(c, figure, t, loop);
                return &c;
            };
            const int hp = shownHp(k);
            const float hpT = hp > 0 ? std::max(float(hp) * 5.0f / float(std::max(f.maxHp, 1)), 0.07f) : 0.0f;
            const float mpT = f.maxMp > 0 ? float(f.mp) * 5.0f / float(f.maxMp) : 0.0f;
            if (UiFig* c = child("call_btlHpBar_per", "btlhpbar_per", hpT, false)) drawFigure(c->fig, view.data());
            if (UiFig* c = child("call_btlMpBar_per", "btlmpbar_per", mpT, false)) drawFigure(c->fig, view.data());
            if (UiFig* c = child("call_btlFace", "btlface", time, true)) {
                const std::string id = std::to_string(f.id);
                for (ViewMesh& vm : c->fig.meshes) {
                    vm.texture = texture("btlFaceC00" + id);
                    vm.extraMap = texture("btlFaceA00" + id);
                }
                // (BattleStatusMenu::Update: its animator by GetMenuSelecter - btlFace_slct while it
                // chooses, the DiffuseColor up to +0.5 and back over 2 s from its change; at no HP
                // btlFace_dark, -0.5; else pc01win, as it is)
                const int state = hp < 1 ? 2 : choosing && (commandInput.menu() == cr2::CommandInput::Menu::First ? k == 0 : commandInput.pairTurn() || commandInput.actor() == k) ? 1 : 0;
                if (state != faceState[k]) faceState[k] = state, faceStateAt[k] = battleClock;
                const float ph = std::fmod(std::max(battleClock - faceStateAt[k], 0.0f), 2.0f);
                const float add = state == 2 ? -0.5f : state == 1 ? 0.5f * (1.0f - std::fabs(ph - 1.0f)) : 0.0f;
                for (int ch = 0; ch < 3; ++ch) c->fig.tint[ch] = 1.0f + add;
                drawFigure(c->fig, view.data());
                for (int ch = 0; ch < 3; ++ch) c->fig.tint[ch] = 1.0f;
            }
            // the labels (call_status_fr01 "HP", fr02 "MP")
            for (int id = 1; id <= 2; ++id) {
                float x, y;
                char jn[24];
                std::snprintf(jn, sizeof jn, "call_status_fr%02d", id);
                if (uiJoint(win, jn, x, y)) drawStatusSign(id, x, y, proj);
            }
            // the signs (BattleStatusMenu::SetPlayerStatusIcon: pcNNinfo's call_enchant_id00 the
            // first - the element -, id01 / id07 six states each, id13, id14)
            UiFig& info = uiFig(n + "info");
            info.fig.place = cr2::Mat34();
            poseUi(info, n + "info", 0);
            float ex, ey;
            if (uiJoint(info, "call_enchant_id00", ex, ey)) drawEnchant(elementSign(f), ex, ey, proj);
            { // (SetPlayerStatusIcon: the states in turn - six at id01, six at id07, one at id13, two at id14)
                const std::vector<int> signs = stateSigns(f);
                static const struct { const char* joint; int first, count; } kSlots[4] = {{"call_enchant_id01", 0, 6}, {"call_enchant_id07", 6, 6}, {"call_enchant_id13", 12, 1}, {"call_enchant_id14", 13, 2}};
                for (const auto& sl : kSlots)
                    if (uiJoint(info, sl.joint, ex, ey))
                        for (int k = 0; k < sl.count && sl.first + k < int(signs.size()); ++k) drawEnchant(signs[size_t(sl.first + k)], ex + 0.04f * float(k), ey, proj);
            }
            for (const char* what : {"hpNum", "mpNum"}) {
                UiFig& num = uiFig(n + (std::string(what) == "hpNum" ? "hpnum" : "mpnum"));
                num.fig.place = cr2::Mat34();
                float x, y;
                if (uiJoint(num, "call_dotNum_num1", x, y))
                    drawDigits(std::string(what) == "hpNum" ? hp : f.mp, 90, std::string(what) == "hpNum" ? 5 : 4, x, y, proj);
            }
        }
        if (!scene.active) { // (the gauges, the menus, the banner: not over a scene)
            // the charge gauge (BreakWindowMenu::Update -> ChargeGaugeWindow::SetChargeGaugeParameter
            // with charge / 10, 0..30: chggauge played towards that / 20 s - 1.5 s full - forwards or
            // back, at its own speed)
            const float chargeT = float(std::clamp(shownCharge(), 0, 300) / 10) / 20.0f;
            const float step = std::max(battleClock - chargeShownAt, 0.0f);
            chargeShownAt = battleClock;
            chargeShown = chargeShown < chargeT ? std::min(chargeShown + step, chargeT) : std::max(chargeShown - step, chargeT);
            UiFig& cg = uiFig("chggauge");
            cg.fig.place = cr2::Mat34();
            poseUi(cg, "chggauge", chargeShown);
            drawFigure(cg.fig, view.data());
            // the break gauge
            UiFig& bw = uiFig("brkwin");
            bw.fig.place = cr2::Mat34();
            poseUi(bw, "brkwin", 0);
            drawFigure(bw.fig, view.data());
            UiFig& bg = uiFig("brkgauge");
            bg.fig.place = cr2::Mat34();
            poseUi(bg, "brkgauge", float(shownGauge() + 4) * 0.5f);
            drawFigure(bg.fig, view.data());
            UiFig& bi = uiFig("brkinfo");
            bi.fig.place = cr2::Mat34();
            const int g = shownGauge();
            poseUi(bi, g > 0 ? "brkinfopl_io" : g < 0 ? "brkinfoen_io" : "brkinfoev_io", 5.0f);
            drawFigure(bi.fig, view.data());
            if (const char* m = SDL_getenv("CR2_MENU")) drawCommandMenu(std::string("btlbtn") + m + "_io", 10.0f, view, {}); // (debugging)
            if (choosing) drawChoosing(view, w, h);
            // the command's name (BtlAnnounce: the window in once - Show - its text changed for each
            // announcement after, btlMsg_out as the last one ends - Hide, EndStringDisplay)
            const bool msgShown = !commandShown.empty() && (battleClock - commandShownStart < 3.0f || announceHeld);
            if (msgShown && !msgOpen) msgOpen = true, msgOpenAt = commandShownStart;
            else if (!msgShown && msgOpen)
                msgOpen = false, msgCloseAt = commandShown.empty() ? battleClock : std::min(battleClock, commandShownStart + 3.0f);
            if (!msgShown && msgCloseAt < 0 && !commandShown.empty() && !announceHeld && battleClock - commandShownStart - 3.0f < 1.0f)
                msgCloseAt = commandShownStart + 3.0f; // (its time run out - known from the clock alone)
            if (msgShown || (msgCloseAt >= 0 && battleClock >= msgCloseAt)) {
                UiFig& m = uiFig("btlmsg");
                m.fig.place = cr2::Mat34();
                if (msgShown) poseUi(m, "btlmsg_in", battleClock - msgOpenAt);
                else {
                    poseUi(m, "btlmsg_out", battleClock - msgCloseAt);
                    if (!m.fig.animLoaded || battleClock - msgCloseAt >= m.fig.anim.duration()) msgCloseAt = -1;
                }
                if (msgShown || msgCloseAt >= 0) drawFigure(m.fig, view.data());
                float x, y;
                if (msgShown && uiJoint(m, "typeM_C01", x, y)) drawUiText(commandShown, 28, x, y, w, h);
            }
        }
        // the damage
        for (const DamageShown& d : damageShown) {
            if (!d.placed) continue;
            const float age = battleClock - d.start;
            if (age < 0 || age > (d.mark >= 0 ? 1.45f : 1.2f)) continue;
            if (d.mark >= 0) {
                static const char* const kMarks[5] = {"bonus_miss", "bonus_block", "bonus_crtcl", "bonus_blow", "bonus_crtcl"};
                UiFig& bm = uiFig("bonus");
                bm.fig.place = cr2::Mat34();
                bm.fig.place.m[3] = d.x, bm.fig.place.m[7] = d.y;
                poseUi(bm, kMarks[d.mark], age);
                drawFigure(bm.fig, view.data());
            }
            if (d.value < 0) continue;
            int digits = 1;
            for (int v = d.value; v >= 10; v /= 10) ++digits;
            UiFig& bn = uiFig("btlnum");
            bn.fig.place = cr2::Mat34();
            bn.fig.place.m[3] = d.x, bn.fig.place.m[7] = d.y;
            poseUi(bn, "btlnum_fgr" + std::to_string(std::min(digits, 5)), age);
            for (int i = 0; i < digits && i < 5; ++i) {
                float x, y;
                char jn[24];
                std::snprintf(jn, sizeof jn, "call_dotNum_id%02d", i + 1);
                if (!uiJoint(bn, jn, x, y)) continue;
                int place = 1; // (id01 the ones)
                for (int k = 0; k < i; ++k) place *= 10;
                drawDigits(d.value / place % 10, d.set, 1, x, y, proj);
            }
        }
    }

    // the dungeon and area the map is in (cr3_field_data +0 / +4: FldMap's table maps them to the
    // maps - t01MM dungeon 0, dNNMM dungeon NN, the area MM - 1)
    int dungeonNum() const {
        const std::string& n = field.info().name;
        return n.size() >= 5 && n[0] == 'd' ? std::atoi(n.substr(1, 2).c_str()) : 0;
    }
    int areaNum() const {
        const std::string& n = field.info().name;
        return n.size() >= 5 ? std::max(std::atoi(n.substr(3, 2).c_str()) - 1, 0) : 0;
    }
    // The dungeon's map (FldDungeonMap -> DungeonMapMenu, interface 0x67): mapGuide_io with the
    // dungeon's guide_<t01 | dNN> on it; mapInfo with mapInfo_<..> at the area + 1 / 20 s, mapSymbol
    // (looping) at its call_mapSymbol - where the party is; the goals - each of the dungeon's
    // entries of the table at 0x535794 (from 0x535774's start) whose flag is set, mapGoal at
    // call_mapGoal with mapInfo at its place within the dungeon / 20 s. A tap closes it (0xb5).
    bool mapOpen = false;
    float mapStart = 0;
    void drawDungeonMap(const Mat4& view) {
        static const short kStart[15] = {0, 14, 21, 38, 53, 69, 83, 98, 114, 129, 144, 164, 212, 213, 220};
        static const short kGoalFlags[222] = {
            2151, 2152, 2153, 2154, 2155, 2156, 2157, 2158, 2159, 2160, 2161, 2162, 2163, 2164, 2165, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 2166, 2167, 0, 0, 0, 0, 0, 2168, 0, 0, 0, 2169, 0, 0, 0, 0, 2170, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 2171, 2172, 0, 0, 0, 2173, 0, 0, 0, 0, 0, 2174, 0, 0, 0, 2175, 0, 0, 0, 0, 0, 0, 2176, 0, 0, 0, 0, 0, 0, 0, 2177, 0, 0,
            0, 0, 0, 0, 0, 0, 2178, 0, 0, 0, 0, 0, 0, 2179, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2180, 0, 0, 0, 0, 2181, 0, 0, 0, 0, 0, 0, 2182,
            0, 0, 0, 0, 0, 0, 0, 2183, 2184, 0, 0, 2185, 0, 0, 2186, 0, 2187, 0, 0, 0, 0, 0, 0, 0, 0, 2188, 0, 2189, 0, 0, 0, 0, 0, 2190,
            0, 0, 0, 0, 2191, 2192, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2193, 0, 0, 0, 2194, 0, 0, 0, 0, 0, 2195, 0, 0, 0, 2196, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2197, 0}; // (the library's, 0x535794)
        const float t = uiClock - mapStart;
        const int d = std::clamp(dungeonNum(), 0, 13);
        char suffix[8];
        std::snprintf(suffix, sizeof suffix, d == 0 ? "t01" : "d%02d", d);
        UiFig& guide = uiFig("mapguide_io");
        guide.fig.place = cr2::Mat34();
        poseUi(guide, "mapguide_io", t);
        if (cr2::Texture* tex = texture(std::string("guide_") + suffix))
            for (ViewMesh& vm : guide.fig.meshes) vm.texture = tex;
        drawFigure(guide.fig, view.data());
        UiFig& info = uiFig("mapinfo");
        info.fig.place = cr2::Mat34();
        const std::string anim = std::string("mapinfo_") + suffix;
        auto mark = [&](float frame, const char* joint, const char* fig, bool loop) {
            poseUi(info, anim, frame / 20.0f);
            float x, y;
            if (!uiJoint(info, joint, x, y)) return;
            UiFig& m = uiFig(fig);
            m.fig.place = cr2::Mat34();
            m.fig.place.m[3] = x, m.fig.place.m[7] = y;
            poseUi(m, fig, t, loop);
            drawFigure(m.fig, view.data());
        };
        if (d < 14)
            for (int k = kStart[d]; k < (d + 1 < 15 ? kStart[d + 1] : 222) && k < 222; ++k)
                if (kGoalFlags[k] && game.flag(kGoalFlags[k])) mark(float(k - kStart[d] + 1), "call_mapGoal", "mapgoal", false);
        mark(float(areaNum() + 1), "call_mapSymbol", "mapsymbol", true);
    }

    // Saving (FldSaveLoad::ExecuteSave -> SaveConfirmationMenu, 0x4b, with the party's status
    // windows): "menu" with menuSave_io, the question (announce 0x69) - yes: 0xbd, "saved" (0x6a),
    // the game's data written (Cr3Util_Save_GameData; here CR2-U's own save, GameState::save, with
    // the map and the place, in the user's folder for the app - `--continue` resumes it), closed
    // after a second
    int saveStage = 0; // (1 asked, 2 saved)
    float saveStart = 0;
    static std::string savePath(const char* file = "save1.txt") {
        char* dir = SDL_GetPrefPath("DuIslingr", "CR2-U");
        std::string p = dir ? std::string(dir) + file : std::string("cr2u-") + file;
        SDL_free(dir);
        return p;
    }
    // the scripts' saves (Cr3Save, FieldScript::onSave): the game's as the field's Save does, the
    // continue data (each area's entry - cont.txt, which --continue takes first), that emptied -
    // only as a game is played (--play / --continue, not a capture's)
    bool scriptSaves = false;
    void scriptSave(int kind) {
        if (!scriptSaves) return;
        if (kind == 0) writeSave();
        else if (kind == 1) writeSave("cont.txt");
        else if (kind == 3) writeSystem(); // (SaveSystem: the system data at once)
        else if (std::FILE* f = std::fopen(savePath("cont.txt").c_str(), "wb")) std::fclose(f); // (DeleteCont: none to continue)
    }
    bool writeSave(const char* file = "save1.txt") {
        std::map<std::string, std::string> extra;
        char pos[96];
        std::snprintf(pos, sizeof pos, "%.6f %.6f %.6f %.6f", player[0], player[1], player[2], heading);
        extra["map"] = field.info().name, extra["place"] = pos;
        extra["saved"] = std::to_string(static_cast<long long>(std::time(nullptr))); // (the calendar's: cr3_savedata's time)
        extra["playtime"] = std::to_string(static_cast<long>(playSeconds * 20.0)); // (+0x10: frames of 0.05 s)
        extra["options"] = std::to_string(optionLanguage) + " " + std::to_string(optionQuality) + " " + std::to_string(optionVolume[0]) + " " +
                           std::to_string(optionVolume[1]) + " " + std::to_string(optionVolume[2]);
        const std::string text = game.save(extra), path = savePath(file);
        std::FILE* f = std::fopen(path.c_str(), "wb");
        const bool ok = f && std::fwrite(text.data(), 1, text.size(), f) == text.size();
        if (f) std::fclose(f);
        SDL_Log("save: %s %s", path.c_str(), ok ? "written" : "not written");
        if (ok && std::strcmp(file, "save1.txt") == 0) writeSystem(); // (SaveConfirmationMenu: Cr3Util_Save_SystemData too)
        return ok;
    }
    // --continue: the save read back, its map entered there
    bool continueSave(const std::string& binary, const std::string& dataPath, const char* only = nullptr) {
        std::string text;
        for (const char* file : {"cont.txt", "save1.txt"}) { // (the continue data, else the save)
            if (only && std::strcmp(file, only) != 0) continue;
            if (std::FILE* f = std::fopen(savePath(file).c_str(), "rb")) {
                char buf[4096];
                for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
                std::fclose(f);
            }
            if (!text.empty()) {
                SDL_Log("continue: from %s", file);
                break;
            }
        }
        std::map<std::string, std::string> extra;
        std::string err;
        if (!game.load(text, &extra, &err)) return SDL_Log("continue: %s (%s)", err.c_str(), savePath().c_str()), false;
        int o[5] = {optionLanguage, optionQuality, optionVolume[0], optionVolume[1], optionVolume[2]};
        if (std::sscanf(extra["options"].c_str(), "%d %d %d %d %d", &o[0], &o[1], &o[2], &o[3], &o[4]) == 5)
            optionLanguage = o[0], optionQuality = o[1], optionVolume[0] = o[2], optionVolume[1] = o[3], optionVolume[2] = o[4];
        applySystem(); // (the system data's flags and options over the save's)
        const std::string map = extra["map"].empty() ? "d1020" : extra["map"];
        playSeconds = double(std::atol(extra["playtime"].c_str())) / 20.0;
        if (!loadField(map, "", binary, dataPath)) return false;
        float p[4];
        if (std::sscanf(extra["place"].c_str(), "%f %f %f %f", &p[0], &p[1], &p[2], &p[3]) == 4) {
            player[0] = p[0], player[1] = p[1], player[2] = p[2], heading = p[3];
            fig.place = cr2::Mat34();
            fig.place.m[3] = p[0], fig.place.m[7] = p[1], fig.place.m[11] = p[2];
        }
        SDL_Log("continue: %s at %.3f %.3f %.3f", map.c_str(), player[0], player[1], player[2]);
        return true;
    }
    // --game-save: the game's own game.dat / cont.dat (script/gamesave.hpp) - and its system.dat
    // beside it, the flags 0..991 - played from its map and place
    bool loadGameSave(const std::string& path, const std::string& binary, const std::string& dataPath) {
        auto read = [](const std::string& p, std::vector<uint8_t>& out) {
            std::FILE* f = std::fopen(p.c_str(), "rb");
            if (!f) return false;
            out.clear();
            uint8_t buf[4096];
            for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) out.insert(out.end(), buf, buf + n);
            std::fclose(f);
            return true;
        };
        std::vector<uint8_t> d;
        cr2::GameSavePlace place;
        bool crc = false;
        std::string err;
        if (!read(path, d) || !cr2::readGameSave(d, game, place, &crc, &err)) return SDL_Log("game save %s: %s", path.c_str(), err.empty() ? "can't read" : err.c_str()), false;
        const std::filesystem::path input(path);
        const bool vita = input.filename() == "SAVEDATA0021.bin" || input.filename() == "SAVEDATA0022.bin";
        const std::string sys = (input.parent_path() / (vita ? "SAVEDATA0020.bin" : "system.dat")).string();
        uint32_t options[6];
        bool sysCrc = false;
        const bool system = read(sys, d) && cr2::readSystemSave(d, game, options, &sysCrc);
        SDL_Log("game save %s%s%s: %s at %.3f %.3f %.3f", path.c_str(), crc ? "" : " (its CRC doesn't hold)", system ? ", with its system.dat" : "",
                place.map.c_str(), place.position[0], place.position[1], place.position[2]);
        playSeconds = double(place.playFrames) / 20.0;
        if (!loadField(place.map, "", binary, dataPath)) return false;
        player[0] = place.position[0], player[1] = place.position[1], player[2] = place.position[2], heading = place.heading;
        fig.place = cr2::Mat34();
        fig.place.m[3] = player[0], fig.place.m[7] = player[1], fig.place.m[11] = player[2];
        return true;
    }
    void saveAnswer(bool yes) {
        if (saveStage != 1) return;
        if (yes) {
            audio.playSe(0xbd, false);
            writeSave();
            saveStage = 2, saveStart = uiClock;
        } else {
            audio.playSe(0xb4, false);
            saveStage = 0;
        }
    }
    void drawSave(const Mat4& view, const Mat4& proj, int w, int h) {
        const float t = uiClock - saveStart;
        UiFig& title = uiFig("menu");
        title.fig.place = cr2::Mat34();
        poseUi(title, "menusave_io", t);
        drawFigure(title.fig, view.data());
        StatusShown rows[2];
        int count = 0;
        for (int k = 0; k < 2; ++k)
            if (game.party[k] >= 0 && game.party[k] < 9) rows[count++] = statusOf(game.party[k]);
        drawStatusWindows(rows, count, t, view, proj);
        if (saveStage == 1) drawAsk(announce(0x69), t, view, w, h, 7401, 7402, true);
        else {
            UiFig& win = uiFig("syswin_io");
            win.fig.place = cr2::Mat34();
            poseUi(win, "syswin_io", t);
            drawFigure(win.fig, view.data());
            float x, y;
            if (uiJoint(win, "typeM_L01", x, y)) drawUiText(announce(0x6a), 26, x, y, w, h, true);
        }
    }
    bool encountersOff = false; // (cr3_field_data +0x31: the tab's 5th, FldMain step 8 / 9)

    // The field's tab (FldTabMenu -> FieldMenuTabMenu: fldMenu_tab, over the field while nothing
    // runs): its own animation played forward to open it, back to close it (Cr3UtilSoundPlaySE
    // 0xba / 0xbb); its tap areas 01 the tab, 02 Camp, 03 the map, 04 Save, 05 the encounters'
    // switch - each but the tab only once it is open (CheckOpenMenu), 0xb4 as one is pressed. Camp
    // opens the camp (FldMain step 5, FldCamp); the others are not here yet.
    bool fieldTabOpen = false;
    float fieldTabAt = -10; // (uiClock: when it last began to open or close)
    bool fieldTabShown() const {
        return fieldMode && !eventMode && !battleMode && script && !script->running() && !campOpen && !mapOpen && !saveStage &&
               keyLock.stage == 0 && changeStage == 0 && !actionRunning() && world.stage == 0 && shop.stage == 0 && party.stage == 0 &&
               library.stage == 0 && pub.stage == 0;
    }
    float fieldTabPose(UiFig& tab) {
        const float d = tab.fig.animLoaded ? tab.fig.anim.duration() : 0.0f;
        const float e = std::clamp(uiClock - fieldTabAt, 0.0f, d);
        return fieldTabOpen ? e : d - e;
    }
    void drawFieldTab(const Mat4& view) {
        UiFig& tab = uiFig("fldmenu_tab");
        tab.fig.place = cr2::Mat34();
        poseUi(tab, "fldmenu_tab", 0);
        poseUi(tab, "fldmenu_tab", fieldTabPose(tab));
        // (the encounters' sign: GEO_encOn / GEO_encOff by flag 0x40e - not had, both away)
        for (ViewMesh& vm : tab.fig.meshes) {
            const uint32_t j = vm.source.palette.empty() ? ~0u : vm.source.palette[0];
            const std::string jn = j < tab.joints.size() ? tab.joints[j] : "";
            vm.hidden = ((jn == "GEO_encOn" || jn == "GEO_encOff") && !game.flag(0x40e)) || (jn == "GEO_encOn" && encountersOff) || (jn == "GEO_encOff" && !encountersOff);
        }
        // (the buttons off at half: the map in dungeon 12, saving with flag 0x3ea, the encounters' without 0x40f)
        for (ViewMesh& vm : tab.fig.meshes) {
            const uint32_t mi = vm.source.material;
            const std::string mn = mi < tab.materials.size() ? tab.materials[mi] : "";
            const bool off = (mn == "PRG_map" && dungeonNum() == 12) || (mn == "PRG_save" && game.flag(0x3ea)) || (mn == "PRG_enc" && !game.flag(0x40f));
            const bool lit = fieldTabOpen && ((tabFocus == 2 && mn == "PRG_camp") || (tabFocus == 5 && mn == "PRG_enc") ||
                                              (tabFocus == 4 && mn == "PRG_save") || (tabFocus == 3 && mn == "PRG_map"));
            vm.material.params[0x33] = off ? std::vector<float>{0.5f, 0.5f, 0.5f, 1.0f} : lit ? std::vector<float>{1.0f, 2.0f, 3.0f, 1.0f} : vm.baseMaterial.params[0x33];
            if (vm.material.params[0x33].empty()) vm.material.params.erase(0x33);
        }
        drawFigure(tab.fig, view.data());
        for (int k = 1; k <= 5; ++k) {
            char sa[16], ea[16];
            std::snprintf(sa, sizeof sa, "tapAreaS%02d", k), std::snprintf(ea, sizeof ea, "tapAreaE%02d", k);
            float sx, sy, ex, ey;
            if (uiJoint(tab, sa, sx, sy) && uiJoint(tab, ea, ex, ey)) tapRects.push_back({6000 + k, sx, sy, ex, ey});
        }
    }
    void fieldTabTap(int area) {
        if (!game.tabEnable) return;
        UiFig& tab = uiFig("fldmenu_tab");
        const bool opened = fieldTabOpen && uiClock - fieldTabAt >= (tab.fig.animLoaded ? tab.fig.anim.duration() : 0.0f);
        if (area == 1) { // (the tab: open or closed, from where its animation is)
            const float d = tab.fig.animLoaded ? tab.fig.anim.duration() : 0.0f;
            const float now = fieldTabPose(tab);
            fieldTabOpen = !fieldTabOpen;
            fieldTabAt = uiClock - (fieldTabOpen ? now : d - now);
            audio.playSe(fieldTabOpen ? 0xba : 0xbb, false);
            SDL_Log("field: the tab %s", fieldTabOpen ? "opened" : "closed");
            return;
        }
        if (!opened) return;
        if (area == 2) {
            audio.playSe(0xb4, false);
            openCamp();
        } else if (area == 3 && dungeonNum() != 12) { // (FldDungeonMap)
            audio.playSe(0xb4, false);
            fieldTabOpen = false, fieldTabAt = -10;
            mapOpen = true, mapStart = uiClock;
            SDL_Log("field: map of dungeon %d, area %d", dungeonNum(), areaNum());
        } else if (area == 4 && !game.flag(0x3ea)) { // (FldSaveLoad)
            audio.playSe(0xb4, false);
            fieldTabOpen = false, fieldTabAt = -10;
            saveStage = 1, saveStart = uiClock;
        } else if (area == 5 && game.flag(0x40e) && game.flag(0x40f)) {
            audio.playSe(0xb4, false);
            encountersOff = !encountersOff;
            SDL_Log("field: encounters %s", encountersOff ? "off" : "on");
        }
    }

    // The camp (FldCamp -> InterfaceMain::SetCampWindowParameter): the party's status windows
    // (CharaStatusWindowMenu: 0x23 one, 0x24 two), the title (CampTitleMenu: menu, menuCamp_io),
    // the buttons (CampMenu: campBtn_io's circle buttons - 16 Items, selectable with any item;
    // 17 Equipment, 18 Sopia, 20 Options -, btlBack, campLnk's links), the Oz (ozWin_io: 7 digits)
    // and the clock (timeWin_io). Items (CampItemListMenu: by category - type 0 / 1, else 4 -,
    // item 36 first, each with its count, those the camp can use - Cr3UtilUseItemByCamp - lit) and
    // Sopia (CampSopiaListMenu: those had, by number) on the list; Options the options' window
    // (CampOptionMenu: as the battle's, its Manual the manual); Back (or Backspace) closes it.
    bool campOpen = false;
    float campStart = 0;
    int campList = 0; // (0 none, 16 the items, 18 the sopia)
    int campCategory = 0, campCursor = 0;
    void openCamp() {
        campOpen = true, campStart = uiClock, campList = 0, campCursor = 0;
        fieldTabOpen = false, fieldTabAt = -10;
        optionsOpen = optionsAsk = false, manualStage = 0;
        SDL_Log("camp: open");
    }
    void closeCamp() {
        campOpen = false, campList = 0, campUse = 0, campItem = 0, campLink = 0;
        optionsOpen = optionsAsk = false, manualStage = 0, manualEvents.reset();
        SDL_Log("camp: closed");
    }
    // a sopia's commands and skills by its levels (SetSopiaListItem - CampSopiaListMenu's detail,
    // CampEquipmentListMenu's mode 8): a command its icon and cost (cost_id 12 "MP" and its MP, an
    // awakening's 13 and its level), an auto skill its icon, a level not reached "???" greyed
    std::vector<ListRow> sopiaSkillRows(int s) {
        std::vector<ListRow> rows;
        const cr2::BattleData::Sopia* so = battleData.sopia(s);
        if (!so) return rows;
        const int level = std::max(int(game.sopiaLevel[s]), 1);
        for (int l = 0; l < 5; ++l) {
            ListRow r;
            r.id = 600 + l, r.number = -1;
            if (l >= level) {
                r.name = menuName(0x3d), r.selectable = false;
            } else if (const cr2::BattleData::Command* c = so->commands[l] > 0 ? battleData.command(so->commands[l]) : nullptr) {
                r.name = commandName(so->commands[l]), r.icon = c->icon, r.help = commandHelp(so->commands[l]);
                r.cost = c->awaken == 0 ? 12 : 13, r.number = c->awaken == 0 ? c->mp : std::min(c->awaken, 3);
            } else if (const cr2::BattleData::AutoSkill* a = so->skills[l] > 0 ? battleData.autoSkill(so->skills[l]) : nullptr) {
                r.name = messageText("autoskillname", so->skills[l]), r.icon = a->icon, r.help = autoSkillHelp(so->skills[l]); // (ChangeSopiaDetailHelpText)
            } else continue;
            rows.push_back(r);
        }
        return rows;
    }
    int campSopiaDetail = 0, campSopiaPrevCursor = 0; // (CampSopiaListMenu: a sopia picked - its detail list)
    void campSopiaPick(int s) {
        if (campList != 18 || campSopiaDetail || s <= 0 || s >= 32) return;
        audio.playSe(0xb4, false); // (Update_Sopia step 5)
        campSopiaDetail = s, campSopiaPrevCursor = campCursor, campCursor = 0, campStart = uiClock;
    }
    std::vector<ListRow> campRows() {
        if (campList == 18 && campSopiaDetail) return sopiaSkillRows(campSopiaDetail);
        std::vector<ListRow> rows;
        if (campList == 16) {
            static const int kCampItems[] = {4, 5, 6, 7, 36, 37, 38, 39, 40, 41}; // (Cr3UtilUseItemByCamp: 4, and the library's list)
            auto add = [&](int id) {
                const cr2::BattleData::Item* it = battleData.item(id);
                const int n = game.itemCount[id];
                if (!it || n <= 0) return;
                const bool inCategory = campCategory == 0 ? it->type <= 1 : it->type == 4; // (CategoryCheck)
                if (!inCategory) return;
                const bool usable = std::find(std::begin(kCampItems), std::end(kCampItems), id) != std::end(kCampItems) && !(id == 36 && game.flag(0x3e9));
                rows.push_back({id, itemName(id), n, it->icon, usable, itemHelp(id)});
            };
            add(36);
            for (int id = 1; id < 300; ++id)
                if (id != 36) add(id);
        } else if (campList == 18) {
            for (int id = 1; id < 32; ++id)
                if (game.sopiaOwned >> id & 1u)
                    if (const cr2::BattleData::Sopia* so = battleData.sopia(id)) {
                        ListRow r{id, sopiaName(id), -1, 0, true, sopiaHelp(id)}; // (ChangeSopiaHelpText)
                        sopiaRowIcon(*so, r);
                        r.cost = std::max(int(game.sopiaLevel[id]), 1) + 6; // (SetSopiaList: SetIcon 1, its level + 6)
                        rows.push_back(r);
                    }
        }
        return rows;
    }
    StatusShown statusOf(int ch) const {
        StatusShown r;
        r.id = ch + 1, r.level = std::max(game.level[ch], 1);
        int eq[8];
        for (int e = 0; e < 8; ++e) eq[e] = game.equip[ch][e];
        const cr2::BattleRules::PlayerStats st = cr2::BattleRules::playerStats(battleData, r.id, r.level, eq, game.sopiaLevel, game.seed[ch], game.itemCount);
        r.hpMax = st.maxHp, r.mpMax = st.maxMp;
        r.hp = r.hpMax; // (Cr3RestorePlayerHP after each battle)
        r.mp = game.mp[ch] < 0 ? r.mpMax : std::min(game.mp[ch], r.mpMax);
        const int lo = cr2::levelExp(r.level), hi = cr2::levelExp(r.level + 1);
        r.next = r.level >= maxLevel() ? 0 : hi - game.exp[ch];
        r.percent = r.level >= maxLevel() || hi <= lo ? 0 : std::clamp((game.exp[ch] - lo) * 100 / (hi - lo), 0, 100);
        return r;
    }
    void drawCamp(const Mat4& view, const Mat4& proj, int w, int h) {
        tapRects.clear();
        if (manualStage == 1) return drawManualList(view, w, h);
        if (manualStage == 2) return; // (the tutorial's picture and windows alone)
        if (optionsAsk) return drawOptionsAsk(view, w, h);
        if (settingsOpen) return drawSettings(view, w, h);
        if (optionsOpen) return drawOptions(view, proj, w, h);
        if (campUse && campUse != 5) return drawItemUse(view, proj, w, h);
        if (campLink == 201) return drawScenarios(view, proj, w, h);
        if (campLink == 202) return drawPubRequest(view, w, h);
        const float t = uiClock - campStart;
        if (campList) { // (CampTitleMenu's "Camp" held over the camp's lists - the reference video's - until EndCampTitle)
            UiFig& title = uiFig("menu");
            title.fig.place = cr2::Mat34();
            poseUi(title, "menucamp_io", 10.0f);
            drawFigure(title.fig, view.data());
        }
        if (campList == 17) return drawEquipment(view, proj, w, h);
        if (campList) { // (the list, its tab - the items' 7, the sopia's 9 - and its own back)
            const std::vector<ListRow> rows = campRows();
            listRows = rows;
            const int focused = rows.empty() ? -1 : rows[size_t(std::clamp(campCursor, 0, int(rows.size()) - 1))].id;
            if (campList == 18) drawSopiaWindows(campSopiaDetail ? campSopiaDetail : focused, t, view, proj, w, h, campSopiaDetail != 0); // (InterfaceMain 0xf / 0x10: its windows beside it)
            if (campList == 16) listTagExtra[0] = campCategory ? 8 : 16, listSwitch = true; // (CampItemListMenu: SetListTagAnimetionItem by its category; ListSwitchButtonShow)
            drawSkillList(rows, focused, t, view, w, h, campList == 16 ? (campCategory ? 15 : 7) : 9, -1);
            listTagExtra[0] = -1, listSwitch = false;
            return;
        }
        UiFig& title = uiFig("menu");
        title.fig.place = cr2::Mat34();
        poseUi(title, "menucamp_io", t);
        drawFigure(title.fig, view.data());
        StatusShown rows[2];
        int count = 0;
        for (int k = 0; k < 2; ++k)
            if (game.party[k] >= 0 && game.party[k] < 9) rows[count++] = statusOf(game.party[k]);
        drawStatusWindows(rows, count, t, view, proj);
        bool anyItem = false;
        for (int id = 1; id < 300; ++id) anyItem = anyItem || game.itemCount[id] > 0;
        buttonLook = [&](int id) { return id == 16 && !anyItem ? 0 : id == campCursorButton() && keysUsed ? 2 : 1; };
        drawCommandMenu("campbtn_io", t, view, {}, {}, w, h, 0, "campbtn_io");
        buttonLook = nullptr;
        for (TapRect& r : tapRects)
            if (r.id == 16 && !anyItem) r.id = -1;
        // the links (CampPubLink: campLnk1_io, the pub hidden, before flag 0x4b0; campLnk2_io after)
        UiFig& lnk = uiFig("camplnk");
        lnk.fig.place = cr2::Mat34();
        const bool pub = game.flag(0x4b0);
        poseUi(lnk, pub ? "camplnk2_io" : "camplnk1_io", t);
        for (ViewMesh& vm : lnk.fig.meshes) {
            const uint32_t j = vm.source.palette.empty() ? ~0u : vm.source.palette[0];
            vm.hidden = !pub && j < lnk.joints.size() && lnk.joints[j] == "GEO_pub";
            const uint32_t mi = vm.source.material; // (MTR_cmpLnkR at half without a request)
            const bool dim = pub && !game.globalWork[7] && mi < lnk.materials.size() && lnk.materials[mi] == "MTR_cmpLnkR";
            vm.material.params[0x33] = dim ? std::vector<float>{0.5f, 0.5f, 0.5f, 1.0f} : vm.baseMaterial.params[0x33];
            if (vm.material.params[0x33].empty()) vm.material.params.erase(0x33);
        }
        drawFigure(lnk.fig, view.data());
        for (int k = 1; k <= (pub ? 2 : 1); ++k) { // (CampPubLink::Initialize: seal_id - "new" - at call_seal_id0k with flag 0x41c + k: a scenario, a request's news)
            float sx, sy;
            if (!game.flag(0x41c + k) || !uiJoint(lnk, "call_seal_id0" + std::to_string(k), sx, sy)) continue;
            UiFig& se = uiFig("seal_id");
            se.fig.place = cr2::Mat34();
            se.fig.place.m[3] = sx, se.fig.place.m[7] = sy;
            poseUi(se, "seal_id", 0.1f); // (SetParameterDataBase's 0.1 s: frame 2, "New!" - frame 0 is empty)
            drawFigure(se.fig, view.data());
        }
        for (int k = 1; k <= (pub ? 2 : 1); ++k) {
            char sa[16], ea[16];
            std::snprintf(sa, sizeof sa, "tapAreaS%02d", k), std::snprintf(ea, sizeof ea, "tapAreaE%02d", k);
            float sx, sy, ex, ey;
            if (uiJoint(lnk, sa, sx, sy) && uiJoint(lnk, ea, ex, ey)) tapRects.push_back({200 + k, sx, sy, ex, ey});
        }
        // the Oz and the clock
        float x, y;
        UiFig& oz = uiFig("ozwin_io");
        oz.fig.place = cr2::Mat34();
        poseUi(oz, "ozwin_io", t);
        drawFigure(oz.fig, view.data());
        if (uiJoint(oz, "call_status_fr13", x, y)) drawStatusSign(13, x, y, proj);
        if (uiJoint(oz, "call_dotNum_id01", x, y)) drawDigits(std::clamp(game.oz, 0, 9999999), 110, 7, x, y, proj); // (id01 the ones, id07 the millions)
        UiFig& tw = uiFig("timewin_io");
        tw.fig.place = cr2::Mat34();
        poseUi(tw, "timewin_io", t);
        drawFigure(tw.fig, view.data());
        if (uiJoint(tw, "call_status_fr14", x, y)) drawStatusSign(14, x, y, proj);
        { // (PlayTimeWindowMenu: the clock's - GetCurrentHourTime - hours at id04, glyph 163's ":" at id03, minutes at id01; set 90)
            const std::time_t now = std::time(nullptr);
            std::tm lt{};
#ifdef _WIN32
            localtime_s(&lt, &now);
#else
            localtime_r(&now, &lt);
#endif
            if (uiJoint(tw, "call_dotNum_id04", x, y)) drawDigits(lt.tm_hour, 90, 2, x, y, proj);
            if (uiJoint(tw, "call_dotNum_id03", x, y)) drawDigits(0, 163, 1, x, y, proj);
            if (uiJoint(tw, "call_dotNum_id01", x, y)) drawDigits(lt.tm_min, 90, 2, x, y, proj);
        }
        UiFig& bk = uiFig("btlback");
        bk.fig.place = cr2::Mat34();
        poseUi(bk, "btlback_in", t);
        drawFigure(bk.fig, view.data());
        float sx, sy, ex, ey;
        if (uiJoint(bk, "tapAreaS01", sx, sy) && uiJoint(bk, "tapAreaE01", ex, ey)) tapRects.push_back({100, sx, sy, ex, ey});
    }
    int campCursorButton() const {
        static const int kButtons[4] = {16, 17, 18, 20};
        return kButtons[std::clamp(campCursor, 0, 3)];
    }
    // The characters the camp offers (Cr3UtilValidCharacter: 0..6 by flags 0x3fd..0x403 - those
    // who have joined), else the party's two
    std::vector<int> validChars() const {
        std::vector<int> out;
        for (int c = 0; c < 7; ++c)
            if (game.flag(0x3fd + c)) out.push_back(c);
        if (out.empty())
            for (int c : game.party)
                if (c >= 0 && c < 9) out.push_back(c);
        return out;
    }
    cr2::BattleRules::PlayerStats statsOf(int ch) const {
        int eq[8];
        for (int e = 0; e < 8; ++e) eq[e] = game.equip[ch][e];
        return cr2::BattleRules::playerStats(battleData, ch + 1, std::max(game.level[ch], 1), eq, game.sopiaLevel, game.seed[ch], game.itemCount);
    }
    int mpOf(int ch) const {
        const int most = statsOf(ch).maxMp;
        return game.mp[ch] < 0 ? most : std::min(game.mp[ch], most);
    }
    // a question on the system window (syswin_io's typeM_L01) with choice_io's Yes / No (call_cBtn_fr11 /
    // 12: circleBtn_fr at those frames) - their taps `yes` / `no`
    // An event's yes / no buttons (EvtWindow.ShowYesNoButton: BattleYNChoice - choice_io, its
    // circleBtn_fr 11 yes, 12 no; a tap SE 0xb4): their tap areas kept for a click
    struct YesNoRect {
        bool yes;
        float x0, y0, x1, y1;
    };
    std::vector<YesNoRect> yesNoRects;
    void drawYesNo(cr2::EventRuntime& ev, int w, int h) {
        yesNoRects.clear();
        if (!ev.yesNoShown()) return;
        Mat4 camera = cr2::mat4Identity();
        camera[14] = -0.881f;
        const Mat4 view = cr2::mat4Multiply(uiProjection(w, h), camera);
        cr2::SceneLighting l;
        dev->setLighting(l);
        UiFig& ch = uiFig("choice_io");
        ch.fig.place = cr2::Mat34();
        poseUi(ch, "choice_io", ev.yesNoAge());
        drawFigure(ch.fig, view.data());
        for (int n : {11, 12}) {
            char jn[24];
            std::snprintf(jn, sizeof jn, "call_cBtn_fr%d", n);
            float x, y;
            if (!uiJoint(ch, jn, x, y)) continue;
            UiFig& c = uiFig("circlebtn_fr");
            c.fig.place = cr2::Mat34();
            c.fig.place.m[3] = x, c.fig.place.m[7] = y;
            poseUi(c, "circlebtn_fr", float(n) / 20.0f);
            drawFigure(c.fig, view.data());
            float sx, sy, ex, ey;
            if (uiJoint(c, "tapAreaS01", sx, sy) && uiJoint(c, "tapAreaE01", ex, ey)) yesNoRects.push_back({n == 11, sx, sy, ex, ey});
        }
    }
    void answerYesNo(bool yes) {
        cr2::EventRuntime* ev = activeEvents();
        if (!ev || !ev->yesNoShown()) return;
        audio.playSe(0xb4, false);
        ev->answerYesNo(yes);
        SDL_Log("yes / no: %s", yes ? "yes" : "no");
    }
    bool yesNoShown() {
        cr2::EventRuntime* ev = activeEvents();
        return ev && ev->yesNoShown();
    }
    bool yesNoClick(float fx, float fy) {
        if (!yesNoShown()) return false;
        const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
        const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
        for (const YesNoRect& r : yesNoRects)
            if (x >= std::min(r.x0, r.x1) && x <= std::max(r.x0, r.x1) && y >= std::min(r.y0, r.y1) && y <= std::max(r.y0, r.y1)) {
                answerYesNo(r.yes);
                break;
            }
        return true; // (the buttons take every tap)
    }
    // `titleStyle`: InterfaceMain::SetYNChoiceTWindowParameter's (interface 0x3c, TitleYNChoice - the
    // title's, the options', SaveConfirmationMenu's): its Yes / No at reopenBtn_io's call_circleBtn_fr11
    // / 12; else SetYNChoiceWindowParameter's (7, BattleYNChoice): choice_io's call_cBtn_fr11 / 12
    void drawAsk(const std::string& message, float t, const Mat4& view, int w, int h, int yes, int no, bool titleStyle = false, bool notice = false) {
        UiFig& win = uiFig("syswin_io");
        win.fig.place = cr2::Mat34();
        poseUi(win, "syswin_io", t);
        drawFigure(win.fig, view.data());
        float x, y;
        // (Cr3UtilNPrint: a line each "\n", typeM_L01 ..; the text's own newline after one - "\n" then
        // a newline, as 0x6f's and the shop's - a line left empty)
        std::string text = message;
        for (size_t at; (at = text.find("\\n")) != std::string::npos;) text.replace(at, 2, "\n");
        size_t from = 0;
        for (int line = 1; line <= 3 && from <= text.size(); ++line) {
            const size_t at = text.find('\n', from);
            char jn[16];
            std::snprintf(jn, sizeof jn, "typeM_L%02d", line);
            if (uiJoint(win, jn, x, y)) drawUiText(text.substr(from, at == std::string::npos ? std::string::npos : at - from), 26, x, y, w, h, true); // (an L joint: from the left)
            if (at == std::string::npos) break;
            from = at + 1;
        }
        const char* buttons = titleStyle ? "reopenbtn_io" : "choice_io";
        UiFig& ch = uiFig(buttons);
        ch.fig.place = cr2::Mat34();
        poseUi(ch, buttons, t);
        drawFigure(ch.fig, view.data());
        for (int n : {11, 12}) {
            if (notice && n == 12) continue;
            char jn[32];
            std::snprintf(jn, sizeof jn, titleStyle ? "call_circleBtn_fr%d" : "call_cBtn_fr%d", n);
            if (!uiJoint(ch, jn, x, y)) continue;
            UiFig& c = uiFig("circlebtn_fr");
            c.fig.place = cr2::Mat34();
            c.fig.place.m[3] = x, c.fig.place.m[7] = y;
            poseUi(c, "circlebtn_fr", float(notice ? 0 : n) / 20.0f);
            focusLook(c, n == 11 ? yes : no);
            drawFigure(c.fig, view.data());
            if (notice) drawUiText("OK", 26, x, y, w, h, false);
            float sx, sy, ex, ey;
            if (uiJoint(c, "tapAreaS01", sx, sy) && uiJoint(c, "tapAreaE01", ex, ey)) tapRects.push_back({n == 11 ? yes : no, sx, sy, ex, ey});
        }
    }
    // CharaStatusWindowBMenu: one character's window (campWin with campWin3_io, the third's joints
    // id90..): its face, name, level (3 of set 130), HP (5 of 110) and MP (4), their bars (btlHpBar_per
    // / btlMpBar_per at the percent / 20 s), its labels, and an arrow by HP / MP (sign 5 up, 6 down)
    // when what is chosen changes them
    void drawStatusB(int ch, int hpChange, int mpChange, float t, const Mat4& view, const Mat4& proj) {
        UiFig& win = uiFig("campwin");
        win.fig.place = cr2::Mat34();
        poseUi(win, "campwin3_io", t);
        for (ViewMesh& vm : win.fig.meshes) {
            const uint32_t j = vm.source.palette.empty() ? ~0u : vm.source.palette[0];
            const std::string jn = j < win.joints.size() ? win.joints[j] : "";
            vm.hidden = jn == "GEO_campWin1" || jn == "GEO_campWin2";
        }
        drawFigure(win.fig, view.data());
        auto at = [&](const std::string& joint, const std::string& fig, float pose) -> UiFig* {
            float x, y;
            if (!uiJoint(win, joint, x, y)) return nullptr;
            UiFig& c = uiFig(fig);
            c.fig.place = cr2::Mat34();
            c.fig.place.m[3] = x, c.fig.place.m[7] = y;
            poseUi(c, fig, pose);
            return &c;
        };
        const int id = ch + 1;
        const cr2::BattleRules::PlayerStats st = statsOf(ch);
        const int mp = mpOf(ch);
        if (UiFig* face = at("call_cmpFace_id9000", "campface", 0)) {
            for (ViewMesh& vm : face->fig.meshes) vm.texture = texture("cmpFaceC00" + std::to_string(id)), vm.extraMap = texture("cmpFaceA00" + std::to_string(id));
            drawFigure(face->fig, view.data());
        }
        const NamePlate np = namePlate();
        if (UiFig* n = at(np.joint + "90" + np.end, np.figure, float(id) / 20.0f)) drawFigure(n->fig, view.data());
        float x, y;
        for (int s : {1, 2, 3, 7})
            if (uiJoint(win, "call_status_fr900" + std::to_string(s), x, y)) drawStatusSign(s, x, y, proj);
        if (hpChange && uiJoint(win, "call_status_fr0005", x, y)) drawStatusSign(hpChange > 0 ? 5 : 6, x, y, proj);
        if (mpChange && uiJoint(win, "call_status_fr0006", x, y)) drawStatusSign(mpChange > 0 ? 5 : 6, x, y, proj);
        if (uiJoint(win, "call_dotNum_id9031", x, y)) drawDigits(std::max(game.level[ch], 1), 130, 3, x, y, proj);
        if (uiJoint(win, "call_dotNum_id9041", x, y)) drawDigits(st.maxHp, 110, 5, x, y, proj); // (the HP: whole between battles)
        if (uiJoint(win, "call_dotNum_id9061", x, y)) drawDigits(mp, 110, 4, x, y, proj);
        if (UiFig* b = at("call_cmpHpBar_per9011", "btlhpbar_per", 5.0f)) drawFigure(b->fig, view.data());
        if (UiFig* b = at("call_cmpMpBar_per9012", "btlmpbar_per", st.maxMp > 0 ? float(mp) * 5.0f / float(st.maxMp) : 0.0f)) drawFigure(b->fig, view.data());
    }
    // CharaStatusCapacityMenu (statusWin_io): the five parameters (4 of set 90 at call_dotNum_id001
    // / 101 / 201 / 301 / 401), what is chosen would change of each (4 of set 140 at id011 .., sign
    // 5 or 6 beside), the labels (call_status_fr16..20), the sopia's combination's name (typeS_L01)
    void drawCapacity(int ch, const int* change, float t, const Mat4& view, const Mat4& proj, int w, int h) {
        UiFig& sw = uiFig("statuswin_io");
        sw.fig.place = cr2::Mat34();
        poseUi(sw, "statuswin_io", t);
        drawFigure(sw.fig, view.data());
        const cr2::BattleRules::PlayerStats st = statsOf(ch);
        float x, y;
        for (int k = 0; k < 5; ++k) {
            const std::string r = std::to_string(k);
            if (uiJoint(sw, "call_dotNum_id" + r + "01", x, y)) drawDigits(st.param[k], 90, 4, x, y, proj);
            if (change && change[k]) {
                if (uiJoint(sw, "call_dotNum_id" + r + "11", x, y)) drawDigits(std::abs(change[k]), 140, 4, x, y, proj);
                if (uiJoint(sw, "call_status_fr" + r + (change[k] > 0 ? "05" : "06"), x, y)) drawStatusSign(change[k] > 0 ? 5 : 6, x, y, proj);
            }
        }
        for (int s = 16; s <= 20; ++s)
            if (uiJoint(sw, "call_status_fr" + std::to_string(s), x, y)) drawStatusSign(s, x, y, proj);
        int eq[8];
        for (int e = 0; e < 8; ++e) eq[e] = game.equip[ch][e];
        const int combo = cr2::BattleRules::sopiaCombo(battleData, eq, game.itemCount);
        if (combo > 0 && uiJoint(sw, "typeS_L01", x, y)) drawUiText(messageText("autoskillname", combo), 22, x, y, w, h, true);
    }

    // Using an item (CampItemListMenu 9 -> CharaSelectButtonMenu, 10 -> CampItemDirectUseMenu):
    // whom - faceWin_io's faceBtn per character (its texture faceBtn_00<n>), faceSlct on the one
    // focused, campHelp_io's "on whom" (announce 0x84), its window and parameters with what the
    // item would add; a tap on another focuses it (0xb3), on the focused uses it (0xb4) - one
    // used up first asks how many (shopNum_io: tap 01 one fewer, 02 one more, 1 .. its count)
    // and Yes / No (announce 0x74); Emergency Exit (36) asks (0x73), then leaves for t0101_p1
    int campItem = 0;  // (the item being used; 0 none)
    int campUse = 0;   // (1 whom, 2 how many and yes / no, 3 Emergency Exit's yes / no, 4 what it raised, 5 the second after)
    int campFace = 0;  // (the one focused: its place among validChars)
    int campCount = 1;
    float campUseStart = 0;
    // what it raised (CharaSelectButtonMenu::CheckEndParamUpText, Update 8): in campHelp_io's window
    // in turn, 3 s each or to a tap - MP restored (announce 0x85: the item's +0x18), Attack .. Luck
    // by so much (0x8d..0x91: +0x1a..+0x22, times the count used) - with the character's name
    // (announcemenuname 0x28 + its number); after the last, a second and back to the list
    std::vector<std::string> campUpText;
    size_t campUpShown = 0;
    float campUpAt = 0;
    void campUseUpdate() { // (Update 8, 9: each 3 s; after the last, a second, then back to the list)
        if (campUse == 4 && uiClock - campUpAt >= 3.0f) campUpShown++, campUpAt = uiClock;
        if (campUse == 4 && campUpShown >= campUpText.size()) campUse = 5, campUseStart = uiClock;
        if (campUse == 5 && uiClock - campUseStart >= 1.0f) campUse = 0, campItem = 0;
    }
    void showRaised(int ch, const cr2::BattleData::Item& it, int n) {
        const std::string who = menuName(0x28 + ch + 1);
        campUpText.clear();
        if (it.mp * n >= 1) campUpText.push_back(formatAnnounce(announce(0x85), {who}));
        for (int k = 0; k < 5; ++k)
            if (it.param[k] * n >= 1) campUpText.push_back(formatAnnounce(announce(0x8d + k), {who, std::to_string(it.param[k] * n)}));
        campUpShown = 0, campUpAt = campUseStart = uiClock;
        campUse = campUpText.empty() ? 5 : 4;
    }
    void useItemOn(int ch, int item, int n) { // (Cr3UtilParameterUpByItem, Cr3UpdatePlayerData; PlaySoundMPRecovery)
        const cr2::BattleData::Item* it = battleData.item(item);
        if (!it || n <= 0) return;
        if (it->usedUp) game.itemCount[item] = static_cast<short>(std::max(game.itemCount[item] - n, 0));
        const int mp0 = mpOf(ch);
        for (int k = 0; k < 5; ++k) game.seed[ch][k] = static_cast<short>(std::clamp(game.seed[ch][k] + it->param[k] * n, -9999, 9999));
        const int most = statsOf(ch).maxMp;
        const int mp = std::clamp(std::min(mp0 + it->mp * n, 9999), 0, most);
        game.mp[ch] = mp >= most ? -1 : mp;
        bool any = it->mp != 0;
        for (int k = 0; k < 5; ++k) any = any || it->param[k] != 0;
        if (any) audio.playSe(0xc6, false);
        SDL_Log("camp: %s x%d on %d: MP %d -> %d", it->name[1].c_str(), n, ch + 1, mp0, mp);
        showRaised(ch, *it, n);
    }
    void campUseTap(int id) {
        const std::vector<int> chars = validChars();
        const cr2::BattleData::Item* it = battleData.item(campItem);
        if (campUse == 1) {
            if (id == 100) { campUse = 0, audio.playSe(0xb5, false); return; }
            const int k = id - 7000;
            if (k < 0 || k >= int(chars.size())) return;
            if (k != campFace) { campFace = k, audio.playSe(0xb3, false); return; }
            audio.playSe(0xb4, false);
            if (it && it->usedUp) campUse = 2, campCount = 1, campUseStart = uiClock;
            else useItemOn(chars[size_t(k)], campItem, 1);
        } else if (campUse == 2) {
            const int most = std::max(int(game.itemCount[campItem]), 1);
            if (id == 7101) campCount = std::max(campCount - 1, 1);
            else if (id == 7102) campCount = std::min(campCount + 1, most);
            else if (id == 7201) { // (yes: used, its effect)
                if (campFace < int(chars.size())) useItemOn(chars[size_t(campFace)], campItem, campCount);
                else campUse = 0, campItem = 0;
            } else if (id == 7202) campUse = 1, audio.playSe(0xb5, false);
        } else if (campUse == 4) {
            if (id == 7300) campUpShown++, campUpAt = uiClock, campUseUpdate(); // (a tap: the next at once - +0xe4)
        } else if (campUse == 3) {
            if (id == 7201) { // (FldMain: the camp's result 2 - its use)
                game.itemCount[36] = static_cast<short>(std::max(game.itemCount[36] - 1, 0));
                audio.playSe(0xb4, false);
                closeCamp();
                audio.stopBgm(20), audio.playSe(0x14f, false);
                game.setFlag(0x3ed, true);
                SDL_Log("camp: Emergency Exit");
                enterMap("t0101", "t0101_p1");
            } else if (id == 7202) campUse = 0, campItem = 0, audio.playSe(0xb5, false);
        }
    }
    void drawItemUse(const Mat4& view, const Mat4& proj, int w, int h) {
        const float t = uiClock - campUseStart;
        const cr2::BattleData::Item* it = battleData.item(campItem);
        const std::string name = it ? itemName(campItem) : "";
        if (campUse == 3) return drawAsk(formatAnnounce(announce(0x73), {name}), t, view, w, h, 7201, 7202);
        const std::vector<int> chars = validChars();
        campFace = std::clamp(campFace, 0, std::max(int(chars.size()) - 1, 0));
        const int ch = chars.empty() ? 0 : chars[size_t(campFace)];
        const bool raised = campUse == 4;
        drawStatusB(ch, raised ? 0 : it ? it->hp : 0, raised ? 0 : it ? it->mp : 0, t, view, proj);
        drawCapacity(ch, raised ? nullptr : it ? it->param : nullptr, t, view, proj, w, h);
        if (campUse == 2) { // (how many, and the question)
            UiFig& num = uiFig("shopnum_io");
            num.fig.place = cr2::Mat34();
            poseUi(num, "shopnum_io", t);
            drawFigure(num.fig, view.data());
            float x, y, sx, sy, ex, ey;
            if (uiJoint(num, "call_dotNum_id01", x, y)) drawDigits(campCount, 90, 2, x, y, proj);
            for (int k = 1; k <= 2; ++k) {
                char sa[16], ea[16];
                std::snprintf(sa, sizeof sa, "tapAreaS%02d", k), std::snprintf(ea, sizeof ea, "tapAreaE%02d", k);
                if (uiJoint(num, sa, sx, sy) && uiJoint(num, ea, ex, ey)) tapRects.push_back({7100 + k, sx, sy, ex, ey});
            }
            drawAsk(formatAnnounce(announce(0x74), {name, std::to_string(game.itemCount[campItem])}), t, view, w, h, 7201, 7202); // (Cr3UtilDPrint: the count had)
            return;
        }
        UiFig& fw = uiFig("facewin_io");
        fw.fig.place = cr2::Mat34();
        poseUi(fw, "facewin_io", t);
        drawFigure(fw.fig, view.data());
        for (size_t k = 0; k < chars.size(); ++k) {
            float x, y;
            if (!uiJoint(fw, "call_faceBtn" + std::to_string(k + 1), x, y)) continue;
            UiFig& b = uiFig("facebtn");
            b.fig.place = cr2::Mat34();
            b.fig.place.m[3] = x, b.fig.place.m[7] = y;
            poseUi(b, "facebtn", 0);
            if (cr2::Texture* tex = texture("faceBtn_00" + std::to_string(chars[k] + 1)))
                for (ViewMesh& vm : b.fig.meshes) vm.texture = tex;
            drawFigure(b.fig, view.data());
            float sx, sy, ex, ey;
            if (!raised && uiJoint(b, "tapAreaS01", sx, sy) && uiJoint(b, "tapAreaE01", ex, ey)) tapRects.push_back({7000 + int(k), sx, sy, ex, ey});
            if (int(k) == campFace && uiJoint(b, "call_faceSlct", sx, sy)) {
                UiFig& sl = uiFig("faceslct");
                sl.fig.place = cr2::Mat34();
                sl.fig.place.m[3] = sx, sl.fig.place.m[7] = sy;
                poseUi(sl, "faceslct", t, true);
                drawFigure(sl.fig, view.data());
            }
        }
        UiFig& help = uiFig("camphelp_io");
        help.fig.place = cr2::Mat34();
        poseUi(help, "camphelp_io", t);
        drawFigure(help.fig, view.data());
        float x, y;
        if (uiJoint(help, "typeM_L01", x, y))
            drawUiText(raised ? campUpText[campUpShown] : formatAnnounce(announce(0x84), {name}), 24, x, y, w, h, true);
        if (raised) return tapRects.push_back({7300, -10, -10, 10, 10}); // (anywhere)
        UiFig& bk = uiFig("btlback");
        bk.fig.place = cr2::Mat34();
        poseUi(bk, "btlback_in", t);
        drawFigure(bk.fig, view.data());
        float sx, sy, ex, ey;
        if (uiJoint(bk, "tapAreaS01", sx, sy) && uiJoint(bk, "tapAreaE01", ex, ey)) tapRects.push_back({100, sx, sy, ex, ey});
    }
    // an item picked on the camp's list (CampItemListMenu::Update 5: 0xb4; 10 for item 36, else 9)
    void campPickItem(int item) {
        const std::vector<ListRow> rows = campRows();
        const auto r = std::find_if(rows.begin(), rows.end(), [&](const ListRow& x) { return x.id == item; });
        if (r == rows.end() || !r->selectable) return (void)SDL_Log("camp: item %d not usable here", item);
        audio.playSe(0xb4, false);
        campItem = item, campUseStart = uiClock, campFace = 0;
        SDL_Log("camp: using %s", r->name.c_str());
        campUse = item == 36 ? 3 : 1;
    }

    // The sopia's windows (CampSopiaStatusMenu, sopiaWin_io: its picture - sopiaPic with
    // sopiaPic_NNN, its number, 26..29 as 301 / 303 / 305 / 307, past 7 + 193 - at call_sopiaPic,
    // its name at typeM_C2, its level (cost_id at the level + 6 / 20 s), its kind's sign (kinds 1 2
    // 3 4 -> 2 1 4 3), the next level's bar and EXP (cmpNxBar_per; 5 at the most); CampSopiaSkillMenu,
    // skillWin_io: its five levels' command or auto skill - "???" (menu name 0x3d) past its level)
    // `detail`: a sopia picked, its skills listed (Update_SopiaDetail: SetParamNumber 3 shows its
    // picture, 4 on leaving hides it - none over the list)
    void drawSopiaWindows(int sopia, float t, const Mat4& view, const Mat4& proj, int w, int h, bool detail = false) {
        const cr2::BattleData::Sopia* so = sopia > 0 ? battleData.sopia(sopia) : nullptr;
        UiFig& sw = uiFig("sopiawin_io");
        sw.fig.place = cr2::Mat34();
        poseUi(sw, "sopiawin_io", t);
        drawFigure(sw.fig, view.data());
        UiFig& kw = uiFig("skillwin_io");
        kw.fig.place = cr2::Mat34();
        poseUi(kw, "skillwin_io", t);
        drawFigure(kw.fig, view.data());
        float x, y;
        if (uiJoint(kw, "call_status_fr11", x, y)) drawStatusSign(11, x, y, proj);
        if (uiJoint(sw, "call_status_fr12", x, y)) drawStatusSign(12, x, y, proj);
        if (uiJoint(sw, "call_status_fr03", x, y)) drawStatusSign(3, x, y, proj);
        if (!so) return;
        const int level = sopia < 32 ? std::max(int(game.sopiaLevel[sopia]), 0) : 1;
        if (detail && uiJoint(sw, "call_sopiaPic", x, y)) {
            const int n = sopia == 26 ? 301 : sopia == 27 ? 303 : sopia == 28 ? 305 : sopia == 29 ? 307 : sopia > 7 ? sopia + 193 : sopia;
            char tn[24];
            std::snprintf(tn, sizeof tn, "sopiaPic_%03d", n);
            UiFig& pic = uiFig("sopiapic");
            pic.fig.place = cr2::Mat34();
            pic.fig.place.m[3] = x, pic.fig.place.m[7] = y;
            poseUi(pic, "sopiapic", 0);
            if (cr2::Texture* tex = texture(tn))
                for (ViewMesh& vm : pic.fig.meshes) vm.texture = tex;
            drawFigure(pic.fig, view.data());
        }
        if (uiJoint(sw, "typeM_C2", x, y)) drawUiText(sopiaName(so->id), 24, x, y, w, h);
        if (uiJoint(sw, "call_cost_id", x, y)) {
            UiFig& co = uiFig("cost_id");
            co.fig.place = cr2::Mat34();
            co.fig.place.m[3] = x, co.fig.place.m[7] = y;
            poseUi(co, "cost_id", float(level + 6) / 20.0f);
            drawFigure(co.fig, view.data());
        }
        static const int kKindSign[5] = {0, 2, 1, 4, 3};
        if (so->kind >= 1 && so->kind <= 4 && uiJoint(sw, "call_enchant_id", x, y)) drawEnchant(kKindSign[so->kind], x, y, proj);
        const bool monster = so->growth != 0;
        const int lo = level > 0 ? cr2::sopiaExp(monster, level) : 0, hi = level < 5 ? cr2::sopiaExp(monster, level + 1) : lo;
        const int exp = sopia < 32 ? game.sopiaExp[sopia] : 0;
        if (uiJoint(sw, "call_cmpNxBar_per", x, y)) {
            UiFig& bar = uiFig("cmpnxbar_per");
            bar.fig.place = cr2::Mat34();
            bar.fig.place.m[3] = x, bar.fig.place.m[7] = y;
            poseUi(bar, "cmpnxbar_per", level >= 5 || hi <= lo ? 5.0f : float(std::clamp((exp - lo) * 100 / (hi - lo), 0, 100)) / 20.0f);
            drawFigure(bar.fig, view.data());
        }
        if (uiJoint(sw, "call_dotNum_id1", x, y)) drawDigits(level >= 5 ? 0 : std::max(hi - exp, 0), 90, 6, x, y, proj);
        for (int k = 0; k < 5; ++k) {
            char jn[16];
            std::snprintf(jn, sizeof jn, "typeS_C%02d", k + 1);
            if (!uiJoint(kw, jn, x, y)) continue;
            std::string n;
            if (k >= level) n = messageText("announcemenuname", 0x3d);
            else if (so->commands[k] > 0) n = messageText("commandname", so->commands[k]);
            else if (so->skills[k] > 0) n = messageText("autoskillname", so->skills[k]);
            drawUiText(n, 22, x, y, w, h);
        }
    }

    // The equipment (CampEquipmentListMenu, with CharaStatusWindowBMenu / CharaStatusCapacityMenu
    // and CampEyeButtonMenu's character buttons - eyeBtnPos, eyeBtn_id at the character's number
    // / 20 s at call_eyeBtn_id<n>, eyeSlct and eyeCsr_loop on the one chosen; a tap another, 0xb7).
    // Its view: the weapon, the armour, the accessory (menu name 0x4b, none to wear: half), the
    // character's own sopia (not to change), Darwin's human sopia (0x4d), the three monster sopia
    // (0x4e .. 0x50) and the skills (0x54); a row its list - the items of its kind (+0x10: 1 2 3)
    // had that the character may wear (+0x12's bit), the one worn lit yellow and fixed, those all
    // worn by others half; the sopia (human: those had past Darwin's own whose flag 0xbe1 + n is
    // set; monster: +0x10 1 - one of another's taken from them after a question, announce 0x8a);
    // "Remove" (0x53) when the place holds one. A choice: Cr3UtilChangeEquipItem / Sopia (199).
    int campEquipChar = 0, campEquipMode = 0, campEquipCursor = 0;
    // (the sopia lists' change button - ScrollListMenu::CreateCampChangeButton, changeBtn: its io,
    // then changeBtn_lp looping - by a focused sopia; tapped, mode 8 that sopia's commands and
    // skills - SetSopiaListItem -, Back to where it was)
    int campEquipViewSopia = 0, campEquipPrevMode = 0, campEquipPrevCursor = 0;
    float campChangeAt = -1;
    int campTakeSopia = 0, campTakeFrom = -1; // (a sopia another wears, asked for)
    struct EquipRow {
        int id = 0; // (300 + the mode a view row opens; an item; 400 + a sopia; 399 Remove)
        std::string name;
        bool selectable = true, worn = false;
        int sopia = 0; // (what the sopia windows show for it)
        int item = 0;  // (the view's row of what's worn: its icon - SetEquipmentList's)
        int command = 0, autoSkill = 0; // (SetSkillList's rows: their icons, the command's cost)
    };
    std::string menuName(int id) { return messageText("announcemenuname", id); }
    std::vector<EquipRow> equipRows() {
        std::vector<EquipRow> rows;
        const int ch = campEquipChar;
        const short* eq = game.equip[ch];
        auto owns = [&](int type) {
            for (int id = 1; id < 300; ++id)
                if (const cr2::BattleData::Item* it = game.itemCount[id] > 0 ? battleData.item(id) : nullptr)
                    if (it->type == type && (it->wearers >> ch & 1)) return true;
            return false;
        };
        auto ownsSopia = [&](bool monster) {
            for (int s = 1; s < 32; ++s)
                if (game.sopiaOwned >> s & 1u)
                    if (const cr2::BattleData::Sopia* so = battleData.sopia(s); so && (so->growth == 1) == monster) return true;
            return false;
        };
        if (campEquipMode == 0) {
            rows.push_back({301, itemName(eq[0]), true, false, 0, eq[0]});
            rows.push_back({302, itemName(eq[1]), true, false, 0, eq[1]});
            if (eq[2] > 0) rows.push_back({303, itemName(eq[2]), true, false, 0, eq[2]});
            else rows.push_back({303, menuName(0x4b), owns(3)});
            rows.push_back({0, sopiaName(eq[3]), false, false, eq[3]});
            if (ch == 0) rows.push_back({304, eq[4] > 0 ? sopiaName(eq[4]) : menuName(0x4d), eq[4] > 0 || !humanSopia().empty(), false, eq[4]});
            for (int s = 0; s < 3; ++s)
                rows.push_back({305 + s, eq[5 + s] > 0 ? sopiaName(eq[5 + s]) : menuName(0x4e + s), eq[5 + s] > 0 || ownsSopia(true), false, eq[5 + s]});
            rows.push_back({309, menuName(0x54)});
        } else if (campEquipMode >= 1 && campEquipMode <= 3) {
            const int slot = campEquipMode - 1;
            for (int id = 1; id < 300; ++id) {
                const cr2::BattleData::Item* it = game.itemCount[id] > 0 ? battleData.item(id) : nullptr;
                if (!it || it->type != campEquipMode || !(it->wearers >> ch & 1)) continue;
                int free = game.itemCount[id]; // (Cr3UtilGetNotEquippedItemNumber)
                for (int c : validChars())
                    for (int e = 0; e < 3; ++e) free -= game.equip[c][e] == id;
                EquipRow r{id, itemName(id)};
                r.worn = eq[slot] == id;
                r.selectable = !r.worn && free > 0;
                rows.push_back(r);
            }
            if (campEquipMode == 3 && eq[2] > 0) rows.push_back({399, menuName(0x53)});
        } else if (campEquipMode == 4) {
            for (int s : humanSopia()) rows.push_back({400 + s, sopiaName(s), eq[4] != s, eq[4] == s, s});
            if (eq[4] > 0) rows.push_back({399, menuName(0x53)});
        } else if (campEquipMode >= 5 && campEquipMode <= 7) {
            for (int s = 1; s < 32; ++s) {
                const cr2::BattleData::Sopia* so = game.sopiaOwned >> s & 1u ? battleData.sopia(s) : nullptr;
                if (!so || so->growth != 1) continue;
                const bool mine = eq[5] == s || eq[6] == s || eq[7] == s;
                rows.push_back({400 + s, sopiaName(s), !mine, mine, s});
            }
            if (eq[campEquipMode] > 0) rows.push_back({399, menuName(0x53)});
        } else if (campEquipMode == 8) { // (SetSopiaListItem(sopia, true): by its levels - those not reached "???", greyed)
            const cr2::BattleData::Sopia* so = battleData.sopia(campEquipViewSopia);
            const int level = std::max(int(game.sopiaLevel[campEquipViewSopia]), 1);
            for (int l = 0; so && l < 5; ++l) {
                if (l >= level) {
                    rows.push_back({0, menuName(0x3d), false});
                    continue;
                }
                EquipRow r{0, so->commands[l] > 0 ? messageText("commandname", so->commands[l]) : messageText("autoskillname", so->skills[l])};
                if (so->commands[l] > 0) r.command = so->commands[l];
                else if (so->skills[l] > 0) r.autoSkill = so->skills[l];
                else continue;
                rows.push_back(r);
            }
        } else if (campEquipMode == 9) {
            // CampEquipmentListMenu::SetSkillList, in its order: the character's awakenings (+0x38, +0x3a,
            // +0x3c - and character 5's 0x83 after the first) whose flag is set (Cr3UtilAwakeUseCheck,
            // 0x5be044), the advents of its first sopia and Darwin's human one (Cr3UtilAdventUseCheck,
            // 0x5be088), those two sopia's commands and skills, the three pieces of equipment's, the
            // monster sopia's, then the sopia's combination's auto skill
            static const short kAwake[16][2] = {{121, 3001}, {122, 3002}, {123, 3003}, {124, 3004}, {125, 3005}, {126, 3006},
                                                {127, 3007}, {128, 3008}, {129, 3009}, {130, 3010}, {131, 3010}, {142, 3011},
                                                {132, 3012}, {133, 3013}, {134, 3014}, {135, 3015}};
            static const short kAdvent[6][4] = {{136, 391, 392, 3016}, {137, 393, 394, 3017}, {138, 395, 396, 3018},
                                                {139, 397, 398, 3019}, {140, 399, 400, 3020}, {141, 141, 141, 3021}};
            auto awake = [&](int c) {
                for (const auto& a : kAwake)
                    if (a[0] == c) return game.flag(a[1]);
                return false;
            };
            auto advent = [&](int c) {
                for (const auto& a : kAdvent)
                    if (a[0] == c || a[1] == c || a[2] == c) return game.flag(a[3]);
                return false;
            };
            auto command = [&](int c) { EquipRow r{0, commandName(c)}; r.command = c; rows.push_back(r); };
            auto skill = [&](int a) { EquipRow r{0, messageText("autoskillname", a)}; r.autoSkill = a; rows.push_back(r); };
            auto sopiaRows = [&](int s) { // (SetSopiaListItem: each level reached, its command or its auto skill)
                if (const cr2::BattleData::Sopia* so = s > 0 ? battleData.sopia(s) : nullptr)
                    for (int l = 0; l < std::min(int(game.sopiaLevel[s]), 5); ++l) {
                        if (so->commands[l] > 0) command(so->commands[l]);
                        else if (so->skills[l] > 0) skill(so->skills[l]);
                    }
            };
            if (const cr2::BattleData::Character* cd = battleData.character(ch + 1))
                for (int k = 0; k < 3; ++k) {
                    if (cd->specials[k] > 0 && awake(cd->specials[k])) command(cd->specials[k]);
                    if (k == 0 && ch + 1 == 5 && awake(0x83)) command(0x83);
                }
            for (int k : {3, 4})
                if (k == 3 || ch == 0)
                    if (const cr2::BattleData::Sopia* so = eq[k] > 0 ? battleData.sopia(eq[k]) : nullptr; so && so->advent > 0 && advent(so->advent)) command(so->advent);
            sopiaRows(eq[3]);
            if (ch == 0) sopiaRows(eq[4]);
            for (int e = 0; e < 3; ++e)
                if (const cr2::BattleData::Item* it = eq[e] > 0 ? battleData.item(eq[e]) : nullptr) {
                    if (it->command > 0) command(it->command);
                    for (int s : it->skills)
                        if (s > 0) skill(s);
                }
            for (int k = 5; k < 8; ++k) sopiaRows(eq[k]);
            int all[8];
            for (int e = 0; e < 8; ++e) all[e] = eq[e];
            if (const int combo = cr2::BattleRules::sopiaCombo(battleData, all, game.itemCount); combo > 0) skill(combo);
        }
        if (campEquipMode >= 8) // (the views' rows told apart: the focus is by id)
            for (size_t k = 0; k < rows.size(); ++k) rows[k].id = 600 + int(k);
        return rows;
    }
    // Darwin's human sopia choices (SetHumanSopiaList: the character sopia had, the n-th past the first with flag 0xbe1 + n)
    std::vector<int> humanSopia() const {
        std::vector<int> out;
        int n = 0;
        for (int s = 1; s < 32; ++s) {
            const cr2::BattleData::Sopia* so = game.sopiaOwned >> s & 1u ? battleData.sopia(s) : nullptr;
            if (!so || so->growth != 0) continue;
            if (n >= 1 && n <= 6 && game.flag(0xbe1 + n)) out.push_back(s);
            ++n;
        }
        return out;
    }
    void campEquipPick(int id) {
        if (campEquipMode >= 8) return; // (a sopia's skills, the skill view: to read)
        const std::vector<EquipRow> rows = equipRows();
        const auto r = std::find_if(rows.begin(), rows.end(), [&](const EquipRow& x) { return x.id == id && x.selectable; });
        if (r == rows.end()) return;
        short* eq = game.equip[campEquipChar];
        if (campEquipMode == 0) {
            if (id == 309 || (id >= 301 && id <= 307)) {
                audio.playSe(0xb4, false);
                campEquipMode = id - 300, campEquipCursor = 0, campStart = uiClock;
            }
            return;
        }
        if (campEquipMode >= 1 && campEquipMode <= 3) {
            eq[campEquipMode - 1] = static_cast<short>(id == 399 ? 0 : id);
        } else if (campEquipMode == 4) {
            eq[4] = static_cast<short>(id == 399 ? 0 : id - 400);
        } else if (campEquipMode >= 5 && campEquipMode <= 7) {
            const int s = id == 399 ? 0 : id - 400;
            for (int c : validChars()) // (another wears it: asked first)
                if (c != campEquipChar && s > 0)
                    for (int k = 5; k < 8; ++k)
                        if (game.equip[c][k] == s) {
                            audio.playSe(0xb4, false);
                            campTakeSopia = s, campTakeFrom = c, campUseStart = uiClock;
                            return;
                        }
            eq[campEquipMode] = static_cast<short>(s);
        } else return;
        audio.playSe(199, false);
        clampMp(campEquipChar);
        SDL_Log("camp: %d equips %d in mode %d", campEquipChar + 1, id, campEquipMode);
        campEquipMode = 0, campEquipCursor = 0, campStart = uiClock;
    }
    // The explorer's camp (--explore), as a player keeps the pair fitted: for each of the two, the
    // best weapon, armour and accessory it can wear and has spare (by what they add, all told),
    // Darwin's human sopia if his place is empty, and the monster sopia had that nobody in the pair
    // wears in its empty places, the highest level first
    void exploreOutfit() {
        if (battleData.couplings() == 0) return;
        for (int ch : game.party) {
            if (ch < 0 || ch >= 9) continue;
            short* eq = game.equip[ch];
            for (int slot = 0; slot < 3; ++slot) {
                auto worth = [&](int id) {
                    const cr2::BattleData::Item* it = id > 0 ? battleData.item(id) : nullptr;
                    if (!it) return -1;
                    int w = it->hp / 10 + it->mp / 5;
                    for (int k = 0; k < 5; ++k) w += it->param[k];
                    return w;
                };
                int best = eq[slot], bestWorth = worth(eq[slot]);
                for (int id = 1; id < 300; ++id) {
                    const cr2::BattleData::Item* it = game.itemCount[id] > 0 ? battleData.item(id) : nullptr;
                    if (!it || it->type != slot + 1 || !(it->wearers >> ch & 1) || id == eq[slot]) continue;
                    int free = game.itemCount[id];
                    for (int c : validChars())
                        for (int e = 0; e < 3; ++e) free -= game.equip[c][e] == id;
                    if (free > 0 && worth(id) > bestWorth) best = id, bestWorth = worth(id);
                }
                if (best != eq[slot]) exploreLog("  camp: character " + std::to_string(ch + 1) + " wears item " + std::to_string(best)), eq[slot] = short(best);
            }
            if (ch == 0 && eq[4] <= 0 && !humanSopia().empty()) eq[4] = short(humanSopia().front()), exploreLog("  camp: Darwin's sopia " + std::to_string(eq[4]));
            for (int k = 5; k < 8; ++k) {
                if (eq[k] > 0) continue;
                int best = 0;
                for (int sp = 1; sp < 32; ++sp) {
                    const cr2::BattleData::Sopia* so = game.sopiaOwned >> sp & 1u ? battleData.sopia(sp) : nullptr;
                    if (!so || so->growth != 1) continue;
                    bool worn = false;
                    for (int c : game.party)
                        for (int e = 5; c >= 0 && c < 9 && e < 8; ++e) worn |= game.equip[c][e] == sp;
                    if (!worn && (!best || game.sopiaLevel[sp] > game.sopiaLevel[best])) best = sp;
                }
                if (!best) break;
                eq[k] = short(best);
                exploreLog("  camp: character " + std::to_string(ch + 1) + " wears sopia " + std::to_string(best));
            }
            clampMp(ch);
        }
    }
    void clampMp(int ch) { // (Cr3UtilAdjustment: the MP within the most)
        if (game.mp[ch] >= 0) game.mp[ch] = std::min(game.mp[ch], statsOf(ch).maxMp);
    }
    void campTakeAnswer(bool yes) {
        if (yes) {
            for (int k = 5; k < 8; ++k)
                if (game.equip[campTakeFrom][k] == campTakeSopia) game.equip[campTakeFrom][k] = 0;
            clampMp(campTakeFrom);
            game.equip[campEquipChar][campEquipMode] = static_cast<short>(campTakeSopia);
            clampMp(campEquipChar);
            audio.playSe(199, false);
            campEquipMode = 0, campEquipCursor = 0, campStart = uiClock;
        } else audio.playSe(0xb4, false);
        campTakeSopia = 0, campTakeFrom = -1;
    }
    void campEquipBack() {
        audio.playSe(0xb5, false);
        if (campTakeSopia) return campTakeAnswer(false);
        if (campEquipMode == 8) campEquipMode = campEquipPrevMode, campEquipCursor = campEquipPrevCursor, campStart = uiClock;
        else if (campEquipMode) campEquipMode = 0, campEquipCursor = 0, campStart = uiClock;
        else campList = 0, campCursor = 0, campStart = uiClock;
    }
    void campEquipView() { // (the change button tapped: Update_*Sopia's step 0xe)
        const std::vector<EquipRow> rows = equipRows();
        if (campEquipMode < 4 || campEquipMode > 7 || campEquipCursor >= int(rows.size()) || rows[size_t(campEquipCursor)].sopia <= 0) return;
        audio.playSe(0xb4, false);
        campEquipViewSopia = rows[size_t(campEquipCursor)].sopia;
        campEquipPrevMode = campEquipMode, campEquipPrevCursor = campEquipCursor;
        campEquipMode = 8, campEquipCursor = 0, campStart = uiClock;
    }
    void campEquipChange(int dir) { // (another character: the eye buttons, Left / Right)
        const std::vector<int> chars = validChars();
        if (chars.empty() || campEquipMode) return;
        const auto it = std::find(chars.begin(), chars.end(), campEquipChar);
        const int k = it == chars.end() ? 0 : int(it - chars.begin());
        campEquipChar = chars[size_t((k + dir + int(chars.size())) % int(chars.size()))];
        audio.playSe(0xb7, false);
    }
    void drawEquipment(const Mat4& view, const Mat4& proj, int w, int h) {
        const float t = uiClock - campStart;
        const std::vector<EquipRow> rows = equipRows();
        campEquipCursor = std::clamp(campEquipCursor, 0, std::max(int(rows.size()) - 1, 0));
        const EquipRow* focused = rows.empty() ? nullptr : &rows[size_t(campEquipCursor)];
        // the right: the sopia's windows for a sopia, else the character's with the change the focused item makes
        const bool sopiaShown = focused && (campEquipMode == 0 ? (focused->id == 0 && focused->sopia > 0) || (focused->id >= 304 && focused->id <= 307)
                                                               : campEquipMode >= 4 && campEquipMode <= 7);
        if (campEquipMode == 8) drawSopiaWindows(campEquipViewSopia, t, view, proj, w, h, true);
        else if (sopiaShown) drawSopiaWindows(focused->sopia, t, view, proj, w, h);
        else {
            int change[5] = {0, 0, 0, 0, 0}, hpChange = 0, mpChange = 0;
            if (campEquipMode >= 1 && campEquipMode <= 3 && focused && focused->id != 0) { // (SetEquipStatusParamerter)
                int eq[8], eq2[8];
                for (int e = 0; e < 8; ++e) eq[e] = eq2[e] = game.equip[campEquipChar][e];
                eq2[campEquipMode - 1] = focused->id == 399 ? 0 : focused->id;
                const int lv = std::max(game.level[campEquipChar], 1);
                const auto a = cr2::BattleRules::playerStats(battleData, campEquipChar + 1, lv, eq, game.sopiaLevel, game.seed[campEquipChar], game.itemCount);
                const auto b = cr2::BattleRules::playerStats(battleData, campEquipChar + 1, lv, eq2, game.sopiaLevel, game.seed[campEquipChar], game.itemCount);
                for (int k = 0; k < 5; ++k) change[k] = b.param[k] - a.param[k];
                hpChange = b.maxHp - a.maxHp, mpChange = b.maxMp - a.maxMp;
            }
            drawStatusB(campEquipChar, hpChange, mpChange, t, view, proj);
            drawCapacity(campEquipChar, change, t, view, proj, w, h);
        }
        // the character buttons
        if (campEquipMode == 0) {
            UiFig& pos = uiFig("eyebtnpos");
            pos.fig.place = cr2::Mat34();
            poseUi(pos, "eyebtnpos", t);
            for (int c : validChars()) {
                float x, y;
                if (!uiJoint(pos, "call_eyeBtn_id" + std::string(c + 1 < 10 ? "0" : "") + std::to_string(c + 1), x, y)) continue;
                UiFig& b = uiFig("eyebtn_id");
                b.fig.place = cr2::Mat34();
                b.fig.place.m[3] = x, b.fig.place.m[7] = y;
                poseUi(b, "eyebtn_id", float(c + 1) / 20.0f);
                drawFigure(b.fig, view.data());
                float sx, sy, ex, ey;
                if (uiJoint(b, "tapAreaS01", sx, sy) && uiJoint(b, "tapAreaE01", ex, ey)) tapRects.push_back({7300 + c, sx, sy, ex, ey});
                if (c == campEquipChar) {
                    for (const char* f : {"eyeslct", "eyecsr_loop"}) {
                        float px, py;
                        if (!uiJoint(b, std::string("call_") + (f[3] == 's' ? "eyeSlct" : "eyeCsr_loop"), px, py)) continue;
                        UiFig& s = uiFig(f);
                        s.fig.place = cr2::Mat34();
                        s.fig.place.m[3] = px, s.fig.place.m[7] = py;
                        poseUi(s, f, t, true);
                        drawFigure(s.fig, view.data());
                    }
                }
            }
        }
        // the list (its tab - SetListItemParam's ListTag: none for the view, 1 the weapons, 3 the
        // armour, 5 the accessories, 9 the sopia and the skills)
        std::vector<ListRow> list;
        for (const EquipRow& r : rows) {
            ListRow lr;
            lr.id = r.id, lr.name = r.name, lr.number = -1, lr.selectable = r.selectable;
            lr.worn = r.worn && campEquipMode >= 1 && campEquipMode <= 7; // (Update_Weapon ..: the one worn named yellow)
            if (campEquipMode == 0 && r.id >= 301 && r.id <= 307 && (r.item > 0 || r.sopia > 0)) lr.worn = true; // (SetEquipmentList: what is worn yellow)
            if (r.id > 0 && r.id < 300)
                if (const cr2::BattleData::Item* it = battleData.item(r.id)) lr.icon = it->icon, lr.help = itemHelp(r.id);
            if (r.item > 0)
                if (const cr2::BattleData::Item* it = battleData.item(r.item)) lr.icon = it->icon;
            // (SetCommandListItem: the command's icon, its cost - not an awakening (+0x48 0) cost_id 12
            // "MP" and its MP (+0x42), else 13 "Charge Lv." and its level (Cr3UtilGetCommandCost);
            // SetSkillList: an auto skill's icon, +0x35)
            if (const cr2::BattleData::Command* c = r.command > 0 ? battleData.command(r.command) : nullptr)
                lr.icon = c->icon, lr.cost = c->awaken == 0 ? 12 : 13, lr.number = c->awaken == 0 ? c->mp : std::min(c->awaken, 3), lr.help = commandHelp(c->id);
            if (const cr2::BattleData::AutoSkill* a = r.autoSkill > 0 ? battleData.autoSkill(r.autoSkill) : nullptr) lr.icon = a->icon;
            if (r.sopia > 0) // (SetEquipmentList / SetHumanSopiaList: a sopia's row its icon)
                if (const cr2::BattleData::Sopia* so = battleData.sopia(r.sopia)) {
                    sopiaRowIcon(*so, lr);
                    if (r.id >= 400) lr.cost = std::max(int(game.sopiaLevel[r.sopia]), 1) + 6; // (Set*SopiaList: SetIcon 1, its level + 6)
                }
            list.push_back(lr);
        }
        listRows = list;
        static const int kTabs[10] = {-1, 1, 3, 5, 9, 9, 9, 9, 9, 9};
        listSwitch = campEquipMode >= 1 && campEquipMode <= 7; // (Update_Weapon .. Update_MonsterSopia: ListSwitchButtonShow)
        drawSkillList(list, focused ? focused->id : -1, t, view, w, h, kTabs[std::clamp(campEquipMode, 0, 9)], -1);
        listSwitch = false;
        if (campEquipMode >= 4 && campEquipMode <= 7 && focused && focused->sopia > 0) { // (CreateCampChangeButton)
            if (campChangeAt < 0) campChangeAt = campStart; // (with the list: its io as the list comes in)
            UiFig& cb = uiFig("changebtn");
            cb.fig.place = cr2::Mat34();
            const float ct = uiClock - campChangeAt;
            poseUi(cb, "changebtn_io", 0);
            const float io = cb.fig.animLoaded ? cb.fig.anim.duration() : 0.0f;
            if (ct < io) poseUi(cb, "changebtn_io", ct);
            else poseUi(cb, "changebtn_lp", ct - io, true);
            drawFigure(cb.fig, view.data());
            float sx, sy, ex, ey;
            if (uiJoint(cb, "tapAreaS01", sx, sy) && uiJoint(cb, "tapAreaE01", ex, ey)) tapRects.push_back({7350, sx, sy, ex, ey});
        } else campChangeAt = -1;
        if (campTakeSopia) {
            const std::string who = menuName(0x29 + campTakeFrom);
            drawAsk(formatAnnounce(announce(0x8a), {who}), uiClock - campUseStart, view, w, h, 7201, 7202);
        }
    }

    // The camp's links (CampPubLink, campLnk: tap 01 Scenarios - 201, CampMenu's result 8, flag
    // 0x41d cleared; tap 02 the pub - 202, only with a request taken, cr3_global_work 7, flag 0x41e
    // cleared - its colour half without one)
    // Scenarios (ScenarioFlowMenu, 0x73): scenariosBG - its panels GEO_panel0<n> shown by their
    // flags (the table at 0x5348c4 / 0x5348f4), its chapters' covers GEO_chapter<n> while their
    // flags are clear (0x534924 / 0x53492c) and their names (menu names 14 .. 17 at typeM_L01 ..)
    // once set -, snrHi_loop / snrLow_loop, "menu" with menuSnr_io, the part opened (percent_io: 3
    // digits of set 80 - flags 1 .. 4 and 151 .. 192 had, of 46), Back
    // The pub's request (CampPubDetailMenu, 0x6d): pubWin_io with the request's text (PUBTxt: its
    // number + 60, a line each "\n" at typeS_L01 ..), pubGirls_io, campHelp_io's line (its number), Back
    int campLink = 0; // (201 the scenarios, 202 the pub's request)
    float scenarioScroll = 0; // (ScenarioFlowMenu::TouchSimpleMove: scenariosBG raised 0 .. 0.704 - 1.472 with flag 1 - by a drag)
    void scrollScenarios(float dy) { scenarioScroll = std::clamp(scenarioScroll + dy, 0.0f, game.flag(1) ? 1.472f : 0.704f); }
    void drawScenarios(const Mat4& view, const Mat4& proj, int w, int h) {
        static const short kPanels[23] = {0, 1, 2, 5, 8, 11, 7, 10, 12, 3, 9, 13, 14, 4, 6, 17, 18, 19, 20, 15, 16, 21, 22};
        static const short kPanelFlags[23] = {151, 155, 158, 160, 166, 172, 168, 169, 171, 159, 167, 173, 175, 161, 165, 1, 178, 179, 180, 1, 2, 3, 4};
        static const short kChapters[4] = {3, 1, 2, 4}, kChapterFlags[4] = {11, 18, 19, 20}, kChapterNames[4] = {14, 15, 16, 17};
        const float t = uiClock - campStart;
        UiFig& bg = uiFig("scenariosbg");
        bg.fig.place = cr2::Mat34();
        bg.fig.place.m[7] = scenarioScroll;
        poseUi(bg, "scenariosbg", t);
        for (ViewMesh& vm : bg.fig.meshes) {
            const uint32_t j = vm.source.palette.empty() ? ~0u : vm.source.palette[0];
            const std::string jn = j < bg.joints.size() ? bg.joints[j] : "";
            vm.hidden = false;
            for (int k = 0; k < 23; ++k)
                if (jn == "GEO_panel0" + std::to_string(kPanels[k])) vm.hidden = !game.flag(kPanelFlags[k]);
            for (int k = 0; k < 4; ++k)
                if (jn == "GEO_chapter" + std::to_string(kChapters[k])) vm.hidden = game.flag(kChapterFlags[k]);
        }
        drawFigure(bg.fig, view.data());
        float x, y;
        for (int k = 0; k < 4; ++k)
            if (game.flag(kChapterFlags[k]) && uiJoint(bg, "typeM_L0" + std::to_string(k + 1), x, y)) drawUiText(menuName(kChapterNames[k]), 24, x, y, w, h, true);
        for (const char* f : {"snrhi_loop", "snrlow_loop"}) { // (the arrows: more above, more below)
            const float most = game.flag(1) ? 1.472f : 0.704f;
            if ((f[3] == 'h' && scenarioScroll <= 0) || (f[3] == 'l' && scenarioScroll >= most)) continue;
            UiFig& l = uiFig(f);
            l.fig.place = cr2::Mat34();
            poseUi(l, f, t, true);
            drawFigure(l.fig, view.data());
        }
        int opened = 0;
        for (int f = 1; f < 5; ++f) opened += game.flag(f);
        for (int f = 0x97; f < 0xc1; ++f) opened += game.flag(f);
        UiFig& pc = uiFig("percent_io");
        pc.fig.place = cr2::Mat34();
        poseUi(pc, "percent_io", t);
        drawFigure(pc.fig, view.data());
        if (uiJoint(pc, "call_dotNum_id1", x, y)) drawDigits(opened * 100 / 46, 80, 3, x, y, proj);
        UiFig& title = uiFig("menu");
        title.fig.place = cr2::Mat34();
        poseUi(title, "menusnr_io", t);
        drawFigure(title.fig, view.data());
        UiFig& bk = uiFig("btlback");
        bk.fig.place = cr2::Mat34();
        poseUi(bk, "btlback_in", t);
        drawFigure(bk.fig, view.data());
        float sx, sy, ex, ey;
        if (uiJoint(bk, "tapAreaS01", sx, sy) && uiJoint(bk, "tapAreaE01", ex, ey)) tapRects.push_back({100, sx, sy, ex, ey});
    }
    void drawPubRequest(const Mat4& view, int w, int h) {
        const float t = uiClock - campStart;
        const int req = game.globalWork[7];
        UiFig& win = uiFig("pubwin_io");
        win.fig.place = cr2::Mat34();
        poseUi(win, "pubwin_io", t);
        drawFigure(win.fig, view.data());
        const std::string text = messageText("pubtxt", req + 60);
        float x, y;
        size_t from = 0;
        for (int line = 1; line <= 8 && from <= text.size(); ++line) {
            const size_t at = text.find("\\n", from);
            if (uiJoint(win, "typeS_L0" + std::to_string(line), x, y))
                drawBorderedText(text.substr(from, at == std::string::npos ? std::string::npos : at - from), 22, x, y, w, h, true);
            if (at == std::string::npos) break;
            from = at + 2;
        }
        UiFig& girls = uiFig("pubgirls_io");
        girls.fig.place = cr2::Mat34();
        poseUi(girls, "pubgirls_io", t);
        drawFigure(girls.fig, view.data());
        UiFig& help = uiFig("camphelp_io");
        help.fig.place = cr2::Mat34();
        poseUi(help, "camphelp_io", t);
        drawFigure(help.fig, view.data());
        if (uiJoint(help, "typeM_L01", x, y)) drawBorderedText(messageText("pubtxt", req), 24, x, y, w, h, true);
        UiFig& bk = uiFig("btlback");
        bk.fig.place = cr2::Mat34();
        poseUi(bk, "btlback_in", t);
        drawFigure(bk.fig, view.data());
        float sx, sy, ex, ey;
        if (uiJoint(bk, "tapAreaS01", sx, sy) && uiJoint(bk, "tapAreaE01", ex, ey)) tapRects.push_back({100, sx, sy, ex, ey});
    }

    // a camp button pressed (CampMenu::TouchSimpleRelease: 0xb4; InterfaceMain's camp step by it)
    void campPress(int id) {
        if (campUse) return campUseTap(id);
        if (campLink) {
            if (id == 100) audio.playSe(0xb5, false), campLink = 0, campStart = uiClock;
            return;
        }
        if (id == 201) scenarioScroll = 0;
        if ((id == 201 || (id == 202 && game.globalWork[7])) && !campList) {
            audio.playSe(0xb4, false);
            game.setFlag(id == 201 ? 0x41d : 0x41e, false);
            campLink = id, campStart = uiClock;
            SDL_Log("camp: %s", id == 201 ? "scenarios" : "the pub's request");
            return;
        }
        if (campList == 17) {
            if (id == 100) campEquipBack();
            else if (campTakeSopia && (id == 7201 || id == 7202)) campTakeAnswer(id == 7201);
            else if (id >= 7300 && id < 7310 && !campEquipMode) { if (id - 7300 != campEquipChar) audio.playSe(0xb7, false); campEquipChar = id - 7300; }
            else if (id == 7350) campEquipView();
            else if (id >= 300 && id < 500) campEquipPick(id);
            return;
        }
        if (id == 100) {
            audio.playSe(0xb5, false);
            if (campList == 18 && campSopiaDetail) campSopiaDetail = 0, campCursor = campSopiaPrevCursor, campStart = uiClock; // (the detail: back to the list)
            else if (campList) campList = 0, campCursor = 0, campStart = uiClock;
            else closeCamp();
            return;
        }
        if (campList) return;
        audio.playSe(0xb4, false);
        if (id == 17) { // (InterfaceMain's camp step 2: the equipment, the first character's)
            const std::vector<int> chars = validChars();
            campList = 17, campEquipMode = 0, campEquipCursor = 0, campStart = uiClock;
            campEquipChar = !chars.empty() ? (std::find(chars.begin(), chars.end(), game.party[0]) != chars.end() ? game.party[0] : chars[0]) : 0;
            SDL_Log("camp: equipment of %d", campEquipChar + 1);
            return;
        }
        if (id == 16 || id == 18) {
            campList = id, campCategory = 0, campCursor = 0, campStart = uiClock, campSopiaDetail = 0;
            SDL_Log("camp: %s - %zu", id == 16 ? "items" : "sopia", campRows().size());
        } else if (id == 20) {
            optionsOpen = true, optionsStart = uiClock; // (CampOptionMenu)
            if (!game.flag(84)) optionsOpen = false, playTutorial("Tutorial_Option", 0, false);
        } else {
            SDL_Log("camp: button %d (not here yet)", id);
        }
    }
    void campKey(SDL_Keycode k) {
        keysUsed = true;
        if (optionsKey(k)) return;
        if (campUse) { // (whom: Left / Right, Return; how many: Left / Right, Return yes, Backspace no)
            const int n = std::max(int(validChars().size()), 1);
            if (campUse == 4) {
                if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_BACKSPACE) campUseTap(7300);
            } else if (campUse == 5) {
            } else if (campUse == 1) {
                if (k == SDLK_LEFT || k == SDLK_UP) campUseTap(7000 + (campFace + n - 1) % n);
                else if (k == SDLK_RIGHT || k == SDLK_DOWN) campUseTap(7000 + (campFace + 1) % n);
                else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) campUseTap(7000 + campFace);
                else if (k == SDLK_BACKSPACE) campUseTap(100);
            } else {
                if (k == SDLK_LEFT || k == SDLK_DOWN) campUseTap(7101);
                else if (k == SDLK_RIGHT || k == SDLK_UP) campUseTap(7102);
                else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_Y) campUseTap(7201);
                else if (k == SDLK_BACKSPACE || k == SDLK_N) campUseTap(7202);
            }
            return;
        }
        if (k == SDLK_BACKSPACE) return campPress(100);
        if (campLink == 201 && (k == SDLK_UP || k == SDLK_DOWN)) return scrollScenarios(k == SDLK_DOWN ? 0.1f : -0.1f);
        if (campList == 17) {
            if (campTakeSopia) {
                if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_Y) campTakeAnswer(true);
                else if (k == SDLK_N) campTakeAnswer(false);
                return;
            }
            const std::vector<EquipRow> rows = equipRows();
            const int n = std::max(int(rows.size()), 1);
            if (k == SDLK_UP) campEquipCursor = (campEquipCursor + n - 1) % n;
            else if (k == SDLK_DOWN) campEquipCursor = (campEquipCursor + 1) % n;
            else if (k == SDLK_LEFT || k == SDLK_RIGHT) campEquipChange(k == SDLK_LEFT ? -1 : 1);
            else if (k == SDLK_TAB) campEquipView(); // (the change button - a controller's triangle)
            else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && !rows.empty()) campEquipPick(rows[size_t(std::clamp(campEquipCursor, 0, n - 1))].id);
            return;
        }
        if (campList) {
            const int n = std::max(int(campRows().size()), 1);
            if (k == SDLK_UP) campCursor = (campCursor + n - 1) % n;
            else if (k == SDLK_DOWN) campCursor = (campCursor + 1) % n;
            else if (k == SDLK_LEFT || k == SDLK_RIGHT) { // (the items' two categories)
                if (campList == 16) campCategory = 1 - campCategory, campCursor = 0;
            } else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && (campList == 16 || campList == 18)) {
                const std::vector<ListRow> rows = campRows();
                if (!rows.empty()) {
                    const int id = rows[size_t(std::clamp(campCursor, 0, int(rows.size()) - 1))].id;
                    campList == 16 ? campPickItem(id) : campSopiaPick(id);
                }
            }
            return;
        }
        if (k == SDLK_UP) campCursor = (campCursor + 3) % 4;
        else if (k == SDLK_DOWN) campCursor = (campCursor + 1) % 4;
        else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) campPress(campCursorButton());
    }
    // a click on the field's UI (the tab, the camp): false - none of it there (the field's own tap)
    bool fieldClick(float fx, float fy) {
        if (mapOpen) { // (any tap: closed)
            audio.playSe(0xb5, false), mapOpen = false;
            return true;
        }
        if (saveStage == 2) return true;
        if (party.stage) { // (the party's buttons, faces)
            const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
            const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
            for (const TapRect& r : tapRects)
                if (x >= std::min(r.x0, r.x1) && x <= std::max(r.x0, r.x1) && y >= std::min(r.y0, r.y1) && y <= std::max(r.y0, r.y1)) {
                    partyTap(r.id);
                    break;
                }
            return true;
        }
        if (shop.stage) { // (the shop's buttons, rows, question)
            const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
            const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
            for (const TapRect& r : tapRects)
                if (x >= std::min(r.x0, r.x1) && x <= std::max(r.x0, r.x1) && y >= std::min(r.y0, r.y1) && y <= std::max(r.y0, r.y1)) {
                    shopTap(r.id);
                    break;
                }
            return true;
        }
        if (world.stage || library.stage == 1 || pub.stage) { // (the world select's rows, its question; the library's rows; the pub's)
            const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
            const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
            for (const TapRect& r : tapRects)
                if (x >= std::min(r.x0, r.x1) && x <= std::max(r.x0, r.x1) && y >= std::min(r.y0, r.y1) && y <= std::max(r.y0, r.y1)) {
                    world.stage ? worldTap(r.id) : pub.stage ? pubTap(r.id) : libraryTap(r.id);
                    break;
                }
            return true;
        }
        if (!campOpen && !fieldTabShown() && !saveStage) return false;
        if (campOpen && manualStage == 2) {
            if (manualEvents) manualEvents->advance();
            return true;
        }
        const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
        const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
        for (const TapRect& r : tapRects)
            if (x >= std::min(r.x0, r.x1) && x <= std::max(r.x0, r.x1) && y >= std::min(r.y0, r.y1) && y <= std::max(r.y0, r.y1)) {
                if (r.id > 6000 && r.id <= 6005) fieldTabTap(r.id - 6000);
                else if (campOpen && campList == 16 && r.id == 6501) campCategory = 1 - campCategory, campCursor = 0, audio.playSe(0xb4, false); // (the items' other category's tab)
                else if (campOpen && campList == 16 && (r.id == 6511 || r.id == 6512)) campCategory = 1 - campCategory, campCursor = 0, audio.playSe(0xb7, false); // (its arrows)
                else if (r.id == 6100) startChange();
                else if (saveStage == 1 && (r.id == 7401 || r.id == 7402)) saveAnswer(r.id == 7401);
                else if (campOpen && optionsTapped(r, x)) {
                } else if (campOpen && campUse) campUseTap(r.id);
                else if (campOpen && campList == 17 && (r.id == 6511 || r.id == 6512)) audio.playSe(0xb7, false), campEquipChange(r.id == 6511 ? -1 : 1); // (its arrows: another character)
                else if (campOpen && campList == 17 && r.id >= 1000 && r.id < 5000) campEquipCursor = r.id - 1000;
                else if (campOpen && campList && r.id >= 1000 && r.id < 5000) campCursor = r.id - 1000; // (a row: the focus)
                else if (campOpen && campList == 16 && r.id > 0 && r.id < 1000) campPickItem(r.id); // (the focused row: picked)
                else if (campOpen && campList == 18 && r.id > 0 && r.id < 32) campSopiaPick(r.id);
                else if (campOpen && r.id >= 0) campPress(r.id);
                return true;
            }
        return campOpen; // (the camp takes every tap)
    }
    // --input in the field: "tab", "camp" (the tab opened, then Camp), "back", "items", "sopia",
    // "equip", "options", "category"; the options' own (as in battle); false - not one of these
    bool fieldToken(const std::string& t) {
        if (saveImport.active()) {
            if (t == "yes") saveImportAnswer(true);
            else if (t == "no" || t == "back") saveImportAnswer(false);
            return true;
        }

        if (t == "tap" && fieldTabShown()) tap(); // (the leader's tap: the nearest balloon's)
        else if (t == "change" && fieldTabShown()) startChange(); // (the character change's button)
        else if (t == "sprint" && sprintAt() >= 0) startSprint(sprintAt());
        else if (t == "action" && fieldTabShown() && actionMark.gimmick >= 0) actionTapped(); // (its mark tapped)
        else if (t == "tab" && fieldTabShown()) fieldTabTap(1);
        else if (t == "camp" && fieldTabShown()) fieldTabOpen = true, fieldTabAt = -10, fieldTabTap(2);
        else if (t == "map" && fieldTabShown()) fieldTabOpen = true, fieldTabAt = -10, fieldTabTap(3);
        else if (t == "save" && fieldTabShown()) fieldTabOpen = true, fieldTabAt = -10, fieldTabTap(4);
        else if (t == "close" && mapOpen) audio.playSe(0xb5, false), mapOpen = false;
        else if (saveStage == 1 && (t == "yes" || t == "no")) saveAnswer(t == "yes");
        else if (!campOpen) return false;
        else if (settingsOpen && (t == "up" || t == "down" || t == "left" || t == "right" || t == "enter"))
            settingsKey(t == "up" ? SDLK_UP : t == "down" ? SDLK_DOWN : t == "left" ? SDLK_LEFT : t == "right" ? SDLK_RIGHT : SDLK_RETURN);
        else if (t == "back" && settingsOpen) closeSettings();
        else if (t == "back") manualStage == 1 ? closeManual() : optionsAsk ? optionTap(3202) : optionsOpen ? void(optionsOpen = false) : campPress(100);
        else if (t == "items") campPress(16);
        else if (t == "scenarios") campPress(201);
        else if (campLink == 201 && (t == "down" || t == "up")) scrollScenarios(t == "down" ? 0.3f : -0.3f);
        else if (t == "pub") campPress(202);
        else if (t == "equip") campPress(17);
        else if (t == "sopia") campPress(18);
        else if (t == "view" && campList == 17) campKey(SDLK_TAB); // (the change button)
        else if (t == "options") campPress(20);
        else if (t == "category") campCategory = 1 - campCategory, campCursor = 0;
        else if (t.size() > 1 && t[0] == 'u' && std::isdigit(static_cast<unsigned char>(t[1]))) campPickItem(std::atoi(t.c_str() + 1)); // (an item used: uNNN)
        else if ((campUse || campList == 17) && (t == "left" || t == "right")) campKey(t == "left" ? SDLK_LEFT : SDLK_RIGHT);
        else if (campTakeSopia && (t == "yes" || t == "no")) campTakeAnswer(t == "yes");
        else if (campUse && (t == "yes" || t == "no")) campUseTap(t == "yes" ? 7201 : 7202);
        else if (t == "down") campKey(SDLK_DOWN);
        else if (t == "up") campKey(SDLK_UP);
        else if (t == "enter") campKey(SDLK_RETURN);
        else {
            static const std::pair<const char*, int> kOptionTaps[] = {{"title", 3101}, {"manual", 3102}, {"quality-", 3103}, {"quality+", 3104},
                                                                      {"language-", 3105}, {"language+", 3106}, {"yes", 3201}, {"no", 3202},
                                                                  {"graphics", 3110}};
            for (const auto& [n, id] : kOptionTaps)
                if (t == n && (optionsOpen || optionsAsk)) {
                    optionTap(id);
                    SDL_Log("input %s", t.c_str());
                    return true;
                }
            return false;
        }
        SDL_Log("input %s", t.c_str());
        return true;
    }
    // ---- The explorer (`--explore <folder>`): the game played by itself from its title, as a player
    // would - so a whole playthrough can be checked against a reference. Where the game waits on
    // the player it chooses: on the field, the event boxes and the things to tap the story has on
    // and it hasn't tried in this state of the story (its flags and work values), then the exits to
    // the maps it hasn't been on since the story last moved; it walks there over a grid of where the
    // leader fits (field/navigation.hpp), the touch actions done as they're asked. The questions
    // take `--choices` (yes / no, in turn - then yes), the world select its newest dungeon at its
    // highest level, the shops and the like are left; battles are fought by their rules. A timeline
    // (timeline.txt: each map, event, battle, the credits, with the game's clock) and a picture at
    // each go to the folder.
    struct Explorer {
        bool on = false;
        std::string dir;
        std::FILE* log = nullptr;
        double clock = 0;                // (the game's seconds played)
        std::string phase;               // (what the timeline last wrote)
        uint64_t key = 0;                // (the story's state: flags and work)
        double movedAt = 0;              // (the story last moved)
        std::set<std::pair<uint64_t, std::string>> tried;
        std::set<std::string> visited;   // (maps since the story moved)
        std::deque<std::string> choices; // (--choices)
        std::string gridMap;
        cr2::NavGrid grid;
        std::string target;              // (a gimmick's name, or "exit:<pop>")
        cr2::Vec3 goal{};
        float reach = 0.02f;
        bool tapIt = false;
        std::vector<cr2::Vec3> path;
        size_t at = 0;
        double since = 0, stillSince = 0;
        cr2::Vec3 last{};
        int shots = 0, stuck = 0;
        std::set<std::string> dungeonsSeen; // (the world select's: its newest the one taken)
        std::string cameFrom;               // (the map before: its way back the last to try)
        std::string visitedMap;
        std::map<std::string, int> taps; // (this visit to the map: a target's taps - two at most, whatever the flags do)
        std::vector<bool> flagsBefore;      // (the timeline's flag changes)
        std::map<size_t, int> flips;        // (a flag's changes: three or more, a passing one - out of the story's state)
        // the maps as the explorer knows them, by where the leader comes onto them (a pop point - a
        // balcony an exit leads to is apart from the floor below it - or the map): each one's ways out
        // (an exit's or a door's target -> where it leads) and whether it had things left to try when
        // last there, in this state
        std::map<std::string, std::map<std::string, std::string>> links;
        std::map<std::string, bool> leftOn;
        std::set<uint64_t> retried;
        std::set<std::pair<uint64_t, std::string>> blocked; // ("map:target" given up on in this state: not picked again) // (the story's states where everything was tried again)
        std::map<std::string, std::set<std::string>> cutOff; // (a map's ways out the leader didn't reach when last there)
        std::set<std::string> everVisited; // (maps been on at all: one never seen first)
        double outUntil = -1;              // (stepping out of an event box, to walk in again)
        double touchingSince = -1;
        float reachPlan = 0.02f;
        std::map<std::string, double> sprintUsed; // (a sprint spot: when the explorer last took it)
        std::map<std::string, std::string> doors; // ("map:box" -> the map it led to: an event box that changes the map, ranked as an exit)
        std::string lastBox;                       // (the event box last touched, "map:box")
        cr2::Vec3 outFrom{};
        double nextTap = 0;
        std::string changedFor; // (the target the leader was last changed for)
        int grind = 0;
        std::string lostOn;
        int needChar = -1; // (a touch action's character not in the pair: taken at the party next) // (the map whose battle was lost: not where the fighting first is done)
        std::string doorTried; double doorTriedAt = -1; // (a door taken: "map:door", when - if the map stays, it doesn't lead now) // (after a game over: encounters to fight, walking about, before the story again)
        std::map<std::string, int> mysticTurn; // (a mystic's uses: the next spot in turn)
        bool done = false;
    } ex;
    uint64_t storyKey() const {
        uint64_t h = 1469598103934665603ull;
        auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
        for (size_t i = 0; i < game.flags.size(); ++i) { // (the flags alone: the work values count taps and the like)
            const auto f = ex.flips.find(i);
            if (game.flags[i] && (f == ex.flips.end() || f->second < 3)) mix(i + 1);
        }
        return h;
    }
    void exploreLog(const std::string& line) {
        if (!ex.log) return;
        const int s = int(ex.clock);
        std::fprintf(ex.log, "%02d:%02d:%02d  %s\n", s / 3600, s / 60 % 60, s % 60, line.c_str());
        std::fflush(ex.log);
        SDL_Log("explore %s", line.c_str());
    }
    std::string explorePhase() const {
        if (titleMenu.stage) return "title";
        if (credits.stage) return "credits";
        if (battleMode) return "battle " + std::to_string(battle.coupling);
        if (eventMode) return "vista " + status;
        if (fieldMode) return "field " + field.info().name;
        return "";
    }
    // the explorer's grid of this map (built on arriving, grown where the leader lands off it - a
    // sprint's or a jump's landing), its exits marked as zones
    void exploreGrid() {
        const cr2::Vec3 p = controller->position();
        const std::vector<std::string> exits = field.exitNames();
        bool changed = false;
        if (ex.gridMap != field.info().name) {
            ex.grid.build(field.collision(), p, controller->radius, controller->stepHeight);
            ex.gridMap = field.info().name, changed = true;
        } else if (!ex.grid.reaches(p)) {
            changed = ex.grid.grow(field.collision(), p, controller->radius, controller->stepHeight);
        }
        if (changed)
            ex.grid.markZones([&](const cr2::Vec3& c) { // (the exits: walked into only when taken)
                for (size_t k = 0; k < exits.size(); ++k)
                    if (field.exitTouched({c[0], c[1] + controller->radius + 0.01f, c[2]}, controller->radius) == exits[k]) return int(k);
                return -1;
            });
    }
    // a way from where the leader stands to `goal` (`target` an exit: its zone allowed), ending within
    // `reach` of it across the ground - false: none from here
    bool explorePath(const std::string& target, const cr2::Vec3& goal, float reach, std::vector<cr2::Vec3>& out) {
        const std::vector<std::string> exits = field.exitNames();
        int allow = -1;
        if (target.rfind("exit:", 0) == 0)
            for (size_t k = 0; k < exits.size(); ++k)
                if ("exit:" + exits[k] == target) allow = int(k);
        if (!ex.grid.path(controller->position(), goal, out, allow) || out.empty()) return false;
        const cr2::Vec3& e = out.back();
        return std::hypot(e[0] - goal[0], e[2] - goal[2]) <= reach + 0.06f;
    }
    bool explorePlan() {
        if (!controller) return false;
        exploreGrid();
        ex.at = 0;
        return explorePath(ex.target, ex.goal, ex.reachPlan, ex.path);
    }
    // the next thing to do on the field (false: nothing left in this state of the story)
    bool explorePick() {
        if (!script || !controller) return false;
        exploreGrid();
        if (ex.grind > 0 && SDL_getenv("CR2_EXPLORE_DEBUG"))
            SDL_Log("EXDEBUG grind %d: encounters %d, on %d, switch %d", ex.grind, int(encount.on), int(game.encountEnable), int(!encountersOff));
        if (ex.grind > 0 && encount.on && game.encountEnable && !(game.flag(0x40e) && game.flag(0x40f) && encountersOff)) { // (walking about where encounters are, till they're fought)
            cr2::Vec3 at;
            for (int k = 0; k < 24; ++k) {
                std::vector<cr2::Vec3> path;
                if (!ex.grid.reachedCell(size_t(encountRandom()), at) || std::hypot(at[0] - controller->position()[0], at[2] - controller->position()[2]) < (k < 12 ? 0.4f : 0.15f) ||
                    !field.exitTouched({at[0], at[1] + controller->radius + 0.01f, at[2]}, controller->radius * 3).empty() || // (not into an exit)
                    !explorePath("wander", at, 0.02f, path))
                    continue;
                bool boxed = false; // (not through an event box: one may be the way on, or a scene)
                for (const cr2::Vec3& q : path) {
                    const float qa[3] = {q[0], q[1] + controller->radius, q[2]};
                    for (const cr2::Gimmick& g : field.gimmicks()) {
                        if (g.kind != cr2::Gimmick::Event || g.check || !g.touches(qa, controller->radius * 2)) continue;
                        const auto st = script->gimmicks().find(g.id);
                        if (st == script->gimmicks().end() || st->second.event) boxed = true;
                    }
                    if (boxed) break;
                }
                if (boxed) continue;
                ex.target = "wander", ex.goal = at, ex.reach = 0.03f, ex.tapIt = false, ex.reachPlan = 0.02f, ex.path = path, ex.at = 0;
                ex.since = ex.clock, ex.stillSince = ex.clock, ex.last = controller->position();
                return true;
            }
        }
        if (ex.grind > 0) // (nowhere here to walk about: to a map with encounters - not the lost battle's - to fight there)
            for (const std::string& e : field.exitNames()) {
                cr2::Vec3 lo, hi;
                std::vector<cr2::Vec3> path;
                if (e.substr(0, 5) == ex.lostOn || !encountTable.at(e.substr(0, 5)) || !field.exitBounds(e, lo, hi)) continue;
                const cr2::Vec3 at{(lo[0] + hi[0]) * 0.5f, lo[1], (lo[2] + hi[2]) * 0.5f};
                const float reach = std::max(hi[0] - lo[0], hi[2] - lo[2]) * 0.5f;
                if (!explorePath("exit:" + e, at, reach, path)) continue;
                ex.target = "exit:" + e, ex.goal = at, ex.reach = 0, ex.tapIt = false, ex.reachPlan = reach, ex.path = path, ex.at = 0;
                ex.since = ex.clock, ex.stillSince = ex.clock, ex.last = controller->position();
                exploreLog("  -> " + ex.target + " (to fight first)");
                return true;
            }
        struct Cand {
            int rank; // 0 an event box, 1 a thing to tap, 2 a treasure box; 3 .. 5 a way out (new, been, back)
            float length = 0;
            std::string name;
            cr2::Vec3 at;
            float reach; // (arriving: a tap's distance; planning: the path's end from it)
            bool tap;
            float planReach;
            bool reachable = false;
            std::vector<cr2::Vec3> path;
        };
        std::vector<Cand> c;
        const bool debug = SDL_getenv("CR2_EXPLORE_DEBUG") != nullptr;
        ex.lastBox.clear(); // (what the last touch or tap began is over, here: not a door)
        const std::string here = field.info().name, node = fieldArrival;
        auto wayRank = [&](const std::string& to) { return to == ex.cameFrom ? 5 : ex.visited.count(to) ? 4 : 3; };
        std::vector<const cr2::Gimmick*> sprints;
        for (const cr2::Gimmick& g : field.gimmicks()) {
            const auto door = ex.doors.find(here + ":" + g.name);
            const bool isDoor = door != ex.doors.end();
            const auto st = script->gimmicks().find(g.id);
            const bool enabled = st == script->gimmicks().end() || st->second.event;
            const bool shown = script->visible(g);
            if (debug && (g.check || g.kind == cr2::Gimmick::Event))
                SDL_Log("EXDEBUG %s: tried %d taps %d enabled %d shown %d door %d", g.name.c_str(), int(ex.tried.count({ex.key, g.name})),
                        ex.taps[g.name], int(enabled), int(shown), int(isDoor));
            const cr2::Vec3 at{g.position[0], g.position[1], g.position[2]};
            const cr2::FieldScript::TouchAction* act = g.kind == cr2::Gimmick::Ability ? script->touchAction(g.name) : nullptr;
            const bool acting = act && act->enabled && !act->spots.empty() && shown;
            if (acting && (g.name.rfind("sprint", 0) == 0 || actionKind(g.name) == 1)) sprints.push_back(&g); // (the ways over: a sprint, a high jump)
            if (acting && actionKind(g.name) > 1 && !ex.blocked.count({ex.key, here + ":" + g.name})) {
                // (the other touch actions: done as taps are - a mystic again and again, its pieces
                // moved in turn, after the ways over have been tried)
                const float half = std::max(std::max(g.jointScale[0], g.jointScale[2]) / 100.0f, 0.04f);
                if (actionKind(g.name) == 3 ? ex.taps[g.name] < 3 * int(act->spots.size()) : !ex.tried.count({ex.key, g.name}))
                    c.push_back({actionKind(g.name) == 3 && ex.taps[g.name] > 0 ? 7 : 1, 0, g.name, at, half * 0.5f, false, half * 0.8f});
                continue;
            }
            if (ex.needChar >= 0 && g.name.rfind("c007_check", 0) == 0 && here.rfind("t01", 0) == 0 && shown && ex.taps[g.name] >= 3)
                ex.needChar = -1, exploreLog("  (the party not to be changed now)");
            if (ex.needChar >= 0 && g.name.rfind("c007_check", 0) == 0 && here.rfind("t01", 0) == 0 && shown && ex.taps[g.name] < 3) { // (the party's: a character wanted)
                c.push_back({0, 0, g.name, at, g.checkLength() * 0.7f, true, g.checkLength() * 0.8f});
                continue;
            }
            if ((!isDoor && ex.tried.count({ex.key, g.name})) || ex.taps[g.name] >= 2 || !enabled || ex.blocked.count({ex.key, here + ":" + g.name})) continue;
            if (g.kind == cr2::Gimmick::Event && !g.check) {
                const float half = std::max(g.jointScale[0], g.jointScale[2]) / 100.0f;
                c.push_back({isDoor ? wayRank(door->second.substr(0, 5)) : 0, 0, g.name, at, half * 0.5f, false, half});
            } else if (g.check && g.checkLength() > 0 && shown) {
                if (g.kind == cr2::Gimmick::Box && (boxOpening.count(g.name) || game.boxes.isOpen(g.boxKind, g.boxNumber))) continue;
                c.push_back({isDoor ? wayRank(door->second.substr(0, 5)) : g.kind == cr2::Gimmick::Box ? 2 : 1, 0, g.name, at, g.checkLength() * 0.7f, true,
                             g.checkLength() * 0.8f});
            }
        }
        for (const std::string& e : field.exitNames()) {
            cr2::Vec3 lo, hi;
            if (!field.exitBounds(e, lo, hi) || ex.blocked.count({ex.key, here + ":exit:" + e})) continue;
            const cr2::Vec3 at{(lo[0] + hi[0]) * 0.5f, lo[1], (lo[2] + hi[2]) * 0.5f};
            c.push_back({wayRank(e.substr(0, 5)), 0, "exit:" + e, at, 0.0f, false, std::max(hi[0] - lo[0], hi[2] - lo[2]) * 0.5f});
        }
        for (Cand& k : c) {
            k.reachable = explorePath(k.name, k.at, k.planReach, k.path);
            float len = 0;
            for (size_t i = 1; i < k.path.size(); ++i) len += std::hypot(k.path[i][0] - k.path[i - 1][0], k.path[i][2] - k.path[i - 1][2]);
            k.length = len;
        }
        // (this map's ways out, as learned; something reachable to do here, or none)
        // (its ways out; one cut off from where the leader stood isn't a way on from there)
        std::map<std::string, std::string> leadsTo;
        for (const std::string& e : field.exitNames()) leadsTo["exit:" + e] = e;
        for (const auto& [box, to] : ex.doors)
            if (box.rfind(here + ":", 0) == 0) leadsTo[box.substr(here.size() + 1)] = to;
        ex.links[node] = leadsTo;
        ex.cutOff[node].clear();
        for (const Cand& k : c)
            if (!k.reachable) ex.cutOff[node].insert(k.name);
        bool local = false, cutOff = false;
        for (const Cand& k : c) {
            if (k.rank <= 2 && k.reachable) local = true;
            if (!k.reachable) cutOff = true;
        }
        ex.leftOn[node] = local;
        auto take = [&](const Cand& k) {
            ex.target = k.name, ex.goal = k.at, ex.reach = k.reach, ex.tapIt = k.tap, ex.reachPlan = k.planReach;
            ex.path = k.path, ex.at = 0;
            ex.since = ex.clock, ex.stillSince = ex.clock, ex.last = controller->position();
            exploreLog("  -> " + k.name + (k.tap ? " (to tap)" : ""));
            return true;
        };
        std::sort(c.begin(), c.end(), [](const Cand& a, const Cand& b) { return a.rank != b.rank ? a.rank < b.rank : a.length < b.length; });
        if (local)
            for (const Cand& k : c)
                if (k.rank <= 2 && k.reachable) return take(k);
        // (something cut off: a sprint not used lately, to go over to it)
        if (cutOff) {
            const Cand* best = nullptr;
            Cand s;
            float bestLen = 1e9f;
            for (const cr2::Gimmick* g : sprints) {
                const auto used = ex.sprintUsed.find(g->name);
                if (used != ex.sprintUsed.end() && ex.clock - used->second < 30) continue;
                // (its marker often over the gap it crosses: near enough is sprintAt's 0.15)
                Cand k{6, 0, "sprint:" + g->name, {g->position[0], g->position[1], g->position[2]}, 0.01f, false, g->name.rfind("sprint", 0) == 0 ? 0.07f : 0.03f};
                const bool ok = explorePath(k.name, k.at, k.planReach, k.path);
                if (debug)
                    SDL_Log("EXDEBUG sprint %s: path %d (%zu points, end %.3f %.3f)", g->name.c_str(), int(ok), k.path.size(),
                            k.path.empty() ? 0.0f : k.path.back()[0], k.path.empty() ? 0.0f : k.path.back()[2]);
                if (!ok) continue;
                float len = 0;
                for (size_t i = 1; i < k.path.size(); ++i) len += std::hypot(k.path[i][0] - k.path[i - 1][0], k.path[i][2] - k.path[i - 1][2]);
                if (len < bestLen) bestLen = len, s = k, best = &s;
            }
            if (best) { // (taken when what's wanted lies over there - a thing to do, a map never been on - and no way here leads anywhere new)
                bool wanted = false, newHere = false;
                for (const Cand& k : c) {
                    const bool fresh = k.rank >= 3 && leadsTo.count(k.name) && !ex.everVisited.count(leadsTo[k.name].substr(0, 5));
                    const bool unseen = k.rank >= 3 && leadsTo.count(k.name) && !ex.leftOn.count(leadsTo[k.name]); // (not been there in this state)
                    if (!k.reachable && (k.rank <= 2 || fresh || unseen)) wanted = true;
                    if (k.reachable && fresh) newHere = true;
                }
                if (wanted && !newHere) return take(*best);
            }
        }
        for (const Cand& k : c) // (a mystic used before: again, its pieces elsewhere)
            if (k.rank == 7 && k.reachable) return ex.leftOn[node] = true, take(k);
        // (nothing here: the way to the nearest map never been on, else one unseen in this state or with things left)
        std::map<std::string, std::string> firstStep;
        std::deque<std::string> open;
        for (const Cand& k : c)
            if (k.rank >= 3 && k.reachable) {
                const std::string to = leadsTo.count(k.name) ? leadsTo[k.name] : "";
                if (!to.empty() && to != node && !firstStep.count(to)) firstStep[to] = k.name, open.push_back(to);
            }
        std::string go;
        if (ex.needChar >= 0) { // (a character wanted: to the town, where the party is changed)
            std::map<std::string, std::string> step = firstStep;
            std::deque<std::string> queue = open;
            while (!queue.empty() && go.empty()) {
                const std::string m = queue.front();
                queue.pop_front();
                if (m.rfind("t0101", 0) == 0) go = step[m];
                for (const auto& [way, to] : ex.links[m])
                    if (!step.count(to) && to != node && !ex.cutOff[m].count(way)) step[to] = step[m], queue.push_back(to);
            }
        }
        for (int pass = 0; pass < 2 && go.empty(); ++pass) {
            std::map<std::string, std::string> step = firstStep;
            std::deque<std::string> queue = open;
            while (!queue.empty() && go.empty()) {
                const std::string m = queue.front();
                queue.pop_front();
                const auto lo = ex.leftOn.find(m);
                if (pass == 0 ? !ex.everVisited.count(m.substr(0, 5)) : (lo == ex.leftOn.end() || lo->second)) { go = step[m]; break; }
                for (const auto& [way, to] : ex.links[m])
                    if (!step.count(to) && to != node && !ex.cutOff[m].count(way) && !ex.blocked.count({ex.key, m.substr(0, 5) + ":" + way}))
                        step[to] = step[m], queue.push_back(to);
            }
        }
        if (debug) {
            std::string known, ways;
            for (const auto& [m, l] : ex.leftOn) known += " " + m + (l ? "+" : "-");
            for (const Cand& k : c) ways += " " + k.name + (k.reachable ? "" : "(cut off)");
            for (const auto& [m, l] : ex.links) {
                std::string w;
                for (const auto& [way, to] : l) w += " " + way + ">" + to + (ex.cutOff[m].count(way) ? "(x)" : "");
                SDL_Log("EXDEBUG   links %s:%s", m.c_str(), w.c_str());
            }
            SDL_Log("EXDEBUG %s: nothing here; known%s; candidates%s; going %s", node.c_str(), known.c_str(), ways.c_str(), go.c_str());
        }
        for (const Cand& k : c)
            if (k.name == go) return take(k);
        // (nowhere: once in this state, everything tried again - an event box tried before may be
        // the door the graph is missing, as t0102's back to t0101 is)
        if (ex.retried.insert(ex.key).second) {
            for (auto it = ex.tried.begin(); it != ex.tried.end();) it = it->first == ex.key ? ex.tried.erase(it) : std::next(it);
            for (auto it = ex.blocked.begin(); it != ex.blocked.end();) it = it->first == ex.key ? ex.blocked.erase(it) : std::next(it);
            ex.taps.clear(), ex.leftOn.clear(), ex.visited.clear();
            exploreLog("  (nothing left: all tried again)");
            return explorePick();
        }
        return false;
    }
    // one update's input: the field's direction, the rest done here
    void exploreStep(float& sx, float& sy) {
        sx = sy = 0;
        ex.clock += 1.0 / 60.0;
        const std::string ph = explorePhase();
        if (ph != ex.phase && !ph.empty()) {
            ex.phase = ph;
            exploreLog(ph);
            exploreShotDue = ex.clock + (ph.rfind("battle", 0) == 0 ? 3.0 : ph.rfind("field", 0) == 0 ? 0.6 : 1.5);
            exploreShotName = ph;
            if (fieldMode && !eventMode) exploreOutfit(); // (what was found, worn)
            if (fieldMode && !eventMode && ex.visitedMap != field.info().name) ex.doorTried.clear();
            if (fieldMode && !eventMode && ex.visitedMap != field.info().name) { // (another map - not back from a battle or a vista)
                if (ex.lastBox.empty() && !ex.target.empty() && ex.target.rfind("exit:", 0) != 0 && ex.target.rfind("sprint:", 0) != 0)
                    ex.lastBox = ex.visitedMap + ":" + ex.target; // (its event ran at once, the map changed before the touch was seen)
                if (!ex.lastBox.empty() && ex.lastBox.rfind(ex.visitedMap + ":", 0) == 0) ex.doors[ex.lastBox] = fieldArrival;
                ex.lastBox.clear();
                if (!ex.target.empty()) ex.tried.insert({ex.key, ex.target}), ex.target.clear(); // (taken - or what lay past an exit)
                if (!ex.visitedMap.empty() && ex.visitedMap != field.info().name) ex.cameFrom = ex.visitedMap;
                if (ex.visitedMap != field.info().name) ex.taps.clear();
                ex.visitedMap = field.info().name;
                ex.visited.insert(field.info().name);
                ex.everVisited.insert(field.info().name);
            } else if (fieldMode && !eventMode) {
                ex.target.clear(); // (back on the same map: the way planned again)
            }
        }
        const uint64_t k = storyKey();
        if (k != ex.key) {
            ex.key = k, ex.movedAt = ex.clock, ex.visited.clear(), ex.stuck = 0, ex.target.clear(), ex.leftOn.clear(), ex.taps.clear();
            std::string on, off;
            for (size_t i = 0; i < std::max(game.flags.size(), ex.flagsBefore.size()); ++i) {
                const bool a = i < ex.flagsBefore.size() && ex.flagsBefore[i], b = game.flag(int(i));
                if (a != b) (b ? on : off) += " " + std::to_string(i), ++ex.flips[i];
            }
            if (!on.empty() || !off.empty()) exploreLog("  flags" + (on.empty() ? "" : " +" + on) + (off.empty() ? "" : " -" + off));
            ex.flagsBefore = game.flags;
        }
        if (credits.stage == 0 && ex.phase == "credits") {} // (the staff roll's end: the field again, or the title)
        if (yesNoShown() && activeEvents()->yesNoAge() > 0.6f) {
            const bool yes = ex.choices.empty() || ex.choices.front() != "no";
            if (!ex.choices.empty()) ex.choices.pop_front();
            exploreLog(std::string("  a question: ") + (yes ? "yes" : "no"));
            return answerYesNo(yes);
        }
        if (titleMenu.stage) {
            if (inputScript.empty() && titleMenu.stage == 3) inputScript = {"tap", "start", "new", "yes"};
            return;
        }
        if (world.stage) { // (the newest dungeon open, its highest level)
            if (ex.clock < ex.nextTap) return;
            ex.nextTap = ex.clock + 0.5;
            const std::vector<ListRow> rows = worldRows();
            if (world.stage == 1 && !world.chapters) {
                int pick = int(rows.size()) - 1;
                for (size_t i = 0; i < rows.size(); ++i)
                    if (!ex.dungeonsSeen.count(rows[i].name)) pick = int(i);
                for (const ListRow& r : rows) ex.dungeonsSeen.insert(r.name);
                world.focus = std::max(pick, 0);
                exploreLog("  the world select: " + (rows.empty() ? std::string("none") : rows[size_t(world.focus)].name));
            } else if (world.stage == 2 || (world.stage == 1 && world.chapters)) {
                world.focus = int(rows.size()) - 1;
            }
            return worldKey(SDLK_RETURN);
        }
        if (shop.stage || pub.stage || library.stage || party.stage || campOpen || mapOpen || saveStage) {
            if (ex.clock < ex.nextTap) return;
            ex.nextTap = ex.clock + 0.5;
            if (saveStage == 1) return saveAnswer(true);
            if (campOpen) return campPress(100);
            if (mapOpen) return void(mapOpen = false);
            if (shop.stage) return exploreShop();
            if (pub.stage) return pubKey(SDLK_BACKSPACE);
            if (library.stage) return libraryKey(SDLK_BACKSPACE);
            if (party.stage) { // (the pair: the one a touch action wants taken in)
                const std::vector<int> c = partyChoices();
                const auto at = std::find(c.begin(), c.end(), ex.needChar);
                if (party.stage == 1) return partyTap(at == c.end() ? 100 : 25);
                if (party.stage == 2) return partyTap(at == c.end() ? 100 : game.flag(0x3fa) || game.party[0] == 0 ? 2 : 1);
                if (at == c.end()) return partyTap(100);
                exploreLog("  party: character " + std::to_string(ex.needChar + 1) + " taken in");
                ex.needChar = -1;
                for (auto it = ex.blocked.begin(); it != ex.blocked.end();) it = it->first == ex.key ? ex.blocked.erase(it) : std::next(it);
                for (auto it = ex.tried.begin(); it != ex.tried.end();) it = it->first == ex.key ? ex.tried.erase(it) : std::next(it);
                ex.leftOn.clear(), ex.cutOff.clear(), ex.visited.clear(), ex.retried.erase(ex.key);
                return partyTap(7000 + int(at - c.begin()));
            }
            return;
        }
        if (battleMode && gameOver.stage == 1 && battleClock - gameOver.start > 1.2f && ex.clock >= ex.nextTap) { // (lost: on)
            ex.nextTap = ex.clock + 1.0;
            if (!gameOver.over) ex.grind += 5, ex.target.clear(), ex.lostOn = returnMap, exploreLog("  lost (no game over yet): 5 encounters fought first");
            gameOver.tapped = true;
            return;
        }
        if (battleMode && gameOver.stage == 8 && ex.clock >= ex.nextTap) { // (a game over: tried again from the continue data - stronger first)
            ex.nextTap = ex.clock + 1.0;
            ex.grind += 5, ex.target.clear(), ex.lostOn = returnMap;
            exploreLog("  game over: tried again, then 5 encounters fought first");
            return gameOverAnswer(true);
        }
        if (battleMode || eventMode || credits.stage || (script && script->running())) { // (pages, results: tapped now and then)
            if (ex.clock >= ex.nextTap) ex.nextTap = ex.clock + 1.0, tap();
            return;
        }
        if (actionRunning() || keyLock.stage) { // (a touch action begun: its success)
            if (inputScript.empty() && ex.clock >= ex.nextTap) {
                ex.nextTap = ex.clock + 0.8;
                if (iai.stage || vslash.stage) inputScript.push_back("slash");
                else if (aim.stage) inputScript.push_back("aim");
                else if (hakkei.stage) inputScript.push_back("punch");
                else if (mystic.stage) { // (the spots in turn, the one it is on left out)
                    const std::string& n = field.gimmicks()[size_t(actionMark.gimmick)].name;
                    const cr2::FieldScript::TouchAction* a = script->touchAction(n);
                    std::vector<size_t> others;
                    for (size_t i = 0; a && i < a->spots.size(); ++i)
                        if (i != a->markSpot()) others.push_back(i);
                    inputScript.push_back(others.empty() ? std::string("tapspot") : "tapspot" + std::to_string(others[size_t(ex.mysticTurn[n]++) % others.size()]));
                }
                else if (keyLock.stage) inputScript.push_back("unlock");
            }
            return;
        }
        if (!fieldTabShown()) return;
        if (!ex.doorTried.empty() && ex.clock - ex.doorTriedAt > 3.0) { // (the door taken, the map the same: shut, in this state)
            if (ex.doorTried.rfind(field.info().name + ":", 0) == 0) ex.blocked.insert({ex.key, ex.doorTried}), exploreLog("  (" + ex.doorTried + " leads nowhere now)");
            ex.doorTried.clear();
        }
        if (actionMark.gimmick >= 0 && actionMark.startedAt < 0 && ex.clock >= ex.nextTap) { // (a touch action offered)
            const std::string& name = field.gimmicks()[size_t(actionMark.gimmick)].name;
            const bool aimed = ex.target == name || ex.target == "sprint:" + name;
            if (actionMark.able && (aimed || (actionMark.kind != 1 && actionMark.kind != 3 && !ex.tried.count({ex.key, name})))) { // (a high jump, a mystic only where meant; the rest passed over once)
                if (!aimed) ex.tried.insert({ex.key, name});
                ex.nextTap = ex.clock + 1.0;
                exploreLog("  a touch action: " + name);
                if (actionMark.kind != 1) // (a puzzle's piece moved - its flags may be passing ones: the maps seen again, a retry allowed)
                    ex.leftOn.clear(), ex.cutOff.clear(), ex.visited.clear(), ex.retried.erase(ex.key);
                if (aimed) {
                    if (actionMark.kind == 1) ex.sprintUsed[name] = ex.clock;
                    else if (actionMark.kind == 3) ++ex.taps[name];
                    else ex.tried.insert({ex.key, name});
                    ex.target.clear();
                }
                return actionTapped();
            }
            if (!actionMark.able && aimed && changeButtonShown() && ex.changedFor != ex.target) { // (its character not leading: the other)
                ex.nextTap = ex.clock + 1.0;
                ex.changedFor = ex.target;
                exploreLog("  the leader changed, for " + name);
                return startChange();
            }
            const int need = actionCharacter(actionMark.kind);
            if (!actionMark.able && aimed && need >= 0 && game.party[0] != need && game.party[1] != need && ex.needChar != need &&
                game.flag(1011 + need) && game.flag(1010)) { // (one who has joined, the party to be had: else a later visit's)
                ex.needChar = need; // (not in the pair: the party to change, in the town)
                exploreLog("  " + name + " wants character " + std::to_string(need + 1) + ", not in the pair");
            }
        }
        if (sprintAt() >= 0 && ex.clock >= ex.nextTap && ex.target == "sprint:" + field.gimmicks()[size_t(sprintAt())].name) { // (only one it meant to take)
            ex.nextTap = ex.clock + 1.0;
            ex.sprintUsed[field.gimmicks()[size_t(sprintAt())].name] = ex.clock;
            if (ex.target.rfind("sprint:", 0) == 0) ex.target.clear();
            return startSprint(sprintAt());
        }
        if (ex.target.empty() && !explorePick()) {
            if (ex.clock - ex.movedAt > exploreStuckSeconds) { // (that long with nothing new: stuck)
                std::string on;
                for (size_t i = 1000; i < game.flags.size(); ++i)
                    if (game.flags[i]) on += " " + std::to_string(i);
                char work[160];
                std::snprintf(work, sizeof work, " - work 0..3: %d %d %d %d", game.globalWork[0], game.globalWork[1], game.globalWork[2], game.globalWork[3]);
                exploreLog("stuck: nothing left to try on " + field.info().name + work);
                exploreLog("  the flags set (1000 up):" + on);
                ex.done = true;
            }
            return;
        }
        if (ex.target.empty()) return;
        const cr2::Vec3 p = controller->position();
        // there: tapped (the balloon's), touched (walked through), or the exit taken
        const float left = std::hypot(ex.goal[0] - p[0], ex.goal[2] - p[2]);
        if (ex.tapIt && nearestBalloon >= 0 && field.gimmicks()[size_t(nearestBalloon)].name == ex.target) {
            ex.tried.insert({ex.key, ex.target});
            ++ex.taps[ex.target];
            ex.lastBox = field.info().name + ":" + ex.target; // (a tap that leads off the map: a door too)
            if (ex.doors.count(ex.lastBox)) ex.doorTried = ex.lastBox, ex.doorTriedAt = ex.clock;
            char work[64];
            std::snprintf(work, sizeof work, " (work %d %d %d %d)", game.globalWork[0], game.globalWork[1], game.globalWork[2], game.globalWork[3]);
            exploreLog("  tapped " + field.gimmicks()[size_t(nearestBalloon)].name + work);
            ex.target.clear();
            return tap();
        }
        if (ex.target == "wander" && left < 0.04f) return ex.target.clear(); // (there: somewhere else next)
        if (ex.clock < ex.outUntil) { // (out of the box: away from its middle)
            float ox = p[0] - ex.outFrom[0], oz = p[2] - ex.outFrom[2];
            const float ol = std::hypot(ox, oz);
            if (ol < 1e-4f) ox = 1, oz = 0;
            else ox /= ol, oz /= ol;
            cr2::Vec3 right, forward;
            field.groundAxes(right, forward);
            sx = ox * right[0] + oz * right[2], sy = ox * forward[0] + oz * forward[2];
            return;
        }
        if (!ex.tapIt && ex.target.rfind("exit:", 0) != 0 && insideEvents.count(ex.target) && ex.clock - ex.since > 0.2 && ex.outUntil < ex.since) {
            ex.outUntil = ex.clock + 0.6, ex.outFrom = ex.goal; // (in it already, so its touch won't come: out, then in again)
            return;
        }
        if (!ex.tapIt && ex.target.rfind("exit:", 0) != 0) { // (an event box: touched as the game touches it)
            const float at[3] = {p[0], p[1], p[2]};
            for (const cr2::Gimmick& g : field.gimmicks())
                if (g.name == ex.target && g.kind == cr2::Gimmick::Event && g.touches(at, controller->radius)) { // (a touch action's mark: done above)
                    if (!insideEvents.count(g.name)) { // (the field looks once an update: in until it has seen it)
                        if (ex.touchingSince < 0) ex.touchingSince = ex.clock;
                        if (ex.clock - ex.touchingSince < 0.5) break;
                    }
                    ex.touchingSince = -1;
                    exploreLog("  touched " + g.name + (insideEvents.count(g.name) ? "" : " (its event didn't run)"));
                    if (!insideEvents.count(g.name) && SDL_getenv("CR2_EXPLORE_DEBUG"))
                        for (const cr2::Gimmick& o : field.gimmicks()) {
                            if (o.kind != cr2::Gimmick::Event) continue;
                            const auto st = script->gimmicks().find(o.id);
                            SDL_Log("DEBUG %s id %s check %d touches %d enabled %d inside %d running %d", o.name.c_str(), o.id.c_str(), int(o.check),
                                    int(o.touches(at, controller->radius)), int(st == script->gimmicks().end() || st->second.event),
                                    int(insideEvents.count(o.name)), int(script->running()));
                        }
                    ex.lastBox = field.info().name + ":" + g.name;
                    if (ex.doors.count(ex.lastBox)) ex.doorTried = ex.lastBox, ex.doorTriedAt = ex.clock;
                    ex.tried.insert({ex.key, ex.target});
                    ex.target.clear();
                    return;
                }
        }
        if (ex.clock - ex.since > 40) { // (too long: given up)
            char why[96];
            std::snprintf(why, sizeof why, " (%.3f from it, the balloon %s)", left,
                          nearestBalloon >= 0 ? field.gimmicks()[size_t(nearestBalloon)].name.c_str() : "none");
            exploreLog("  gave up on " + ex.target + why);
            ex.tried.insert({ex.key, ex.target}), ex.blocked.insert({ex.key, field.info().name + ":" + ex.target}), ex.target.clear();
            return;
        }
        if (std::hypot(p[0] - ex.last[0], p[2] - ex.last[2]) > 0.01f) ex.last = p, ex.stillSince = ex.clock;
        else if (ex.clock - ex.stillSince > 2.0) { // (held up: the way again)
            ex.stillSince = ex.clock;
            if (!explorePlan())
                exploreLog("  (no way to " + ex.target + " from here)"), ex.tried.insert({ex.key, ex.target}),
                    ex.blocked.insert({ex.key, field.info().name + ":" + ex.target}), ex.target.clear();
            return;
        }
        // along the way: the next point not yet passed
        while (ex.at + 1 < ex.path.size() && std::hypot(ex.path[ex.at][0] - p[0], ex.path[ex.at][2] - p[2]) < 0.025f) ++ex.at;
        cr2::Vec3 to = ex.at < ex.path.size() ? ex.path[ex.at] : ex.goal;
        if (ex.at + 1 >= ex.path.size()) to = ex.goal; // (the last stretch: straight in)
        float dx = to[0] - p[0], dz = to[2] - p[2];
        const float l = std::hypot(dx, dz);
        if (l < 1e-4f) return;
        dx /= l, dz /= l;
        cr2::Vec3 right, forward;
        field.groundAxes(right, forward);
        sx = dx * right[0] + dz * right[2], sy = dx * forward[0] + dz * forward[2];
    }
    double exploreShotDue = -1;
    double exploreStuckSeconds = 900; // (CR2_EXPLORE_STUCK: seconds)
    std::string exploreShotName;
    // The leader's figure (FldPlayer::CreatePlayer: c + its character's 001..007), the field's
    // character the script has leading - unless --model gave one
    bool leaderFollows = false;
    void refreshLeader() {
        if (!leaderFollows || !fieldMode || !script) return;
        const int c = script->leader();
        if (c < 0 || c > 6) return;
        char want[16];
        std::snprintf(want, sizeof want, "c%03d.geom", c + 1);
        if (fig.name == want) return;
        const auto it = std::find(models.begin(), models.end(), want);
        if (it == models.end()) return;
        const float keep = time;
        load(size_t(it - models.begin()));
        time = keep;
        playMotion(fig, "fn01");
    }
    // The character change (FldChangeCharacter: CharaTradeMenu's chrTrade button, shown in the
    // field once allowed - Cr3ChangeCharacter.SetEnable - and flag 0x3fb set, the second of the
    // two there; its tap SE 0xb4): FldMain::ChangeCharacterExecute - the leader darkened over 16
    // frames (its materials' diffuse 1 - n / 16), then the other in its place (its idle), lit over
    // 32 (n / 32)
    int changeStage = 0; // 0 none, 1 out, 2 in
    float changeFrames = 0;
    bool changeButtonShown() const {
        return fieldTabShown() && game.changeEnable && game.flag(0x3fb) && script && script->fieldCharacter(1) >= 0;
    }
    void startChange() {
        if (changeStage || !changeButtonShown()) return;
        audio.playSe(0xb4, false);
        changeStage = 1, changeFrames = 0;
    }
    void updateChange(float dt) {
        if (!changeStage) return;
        changeFrames += dt * 60;
        if (changeStage == 1) {
            const float k = std::max(1 - changeFrames / 16, 0.0f);
            for (int c = 0; c < 3; ++c) fig.tint[c] = k;
            if (changeFrames < 16) return;
            game.active = game.active == 0 ? 1 : 0;
            SDL_Log("%s: the leader now character %d", field.info().name.c_str(), script->leader());
            refreshLeader();
            changeStage = 2, changeFrames = 0;
            for (int c = 0; c < 3; ++c) fig.tint[c] = 0;
            return;
        }
        const float k = std::min(changeFrames / 32, 1.0f);
        for (int c = 0; c < 3; ++c) fig.tint[c] = k;
        if (changeFrames >= 32) changeStage = 0;
    }
    void drawChangeButton(const Mat4& view) {
        if (!changeButtonShown()) return;
        UiFig& b = uiFig("chrtrade");
        b.fig.place = cr2::Mat34();
        poseUi(b, "chrtrade", 0);
        drawFigure(b.fig, view.data());
        float sx, sy, ex, ey;
        for (const std::string& j : b.joints)
            if (j.compare(0, 8, "tapAreaS") == 0 && uiJoint(b, j, sx, sy) && uiJoint(b, "tapAreaE" + j.substr(8), ex, ey))
                tapRects.push_back({6100, sx, sy, ex, ey});
    }

    // The world select (FldWorldSelect - InterfaceMain::SetMapSelectParameter: MapSelectMenu, a
    // scroll list, with MapSelectTitleMenu's `menu` on menuWld_io and MapSelectPicMenu's `mapPic`
    // wearing the focused dungeon's mapPic_dNN). Its rows (SetMapList): the dungeons whose flags
    // are set (0x534510: 1041 ..), in the table's order (0x534544: 1, 9, 6, 7, 8, 4, 3, 5, 2, 10 ..),
    // named by announcemenuname (the dungeon's number); picked, the levels (SetLevelList: Lv. 1 ..
    // 11, those whose flags 0x5345a8 say - announcemenuname 0x14 + the level); picked, the
    // question (the System window: the dungeon, the level, announce 0x80 - yes / no). Yes: the
    // dungeon chosen (GetSelectDungeonNum), the difficulty the level less one (Cr3UtilSetDifficulty),
    // the town's WarpTask called; no: the levels again; back: the dungeons, then the field.
    //
    // The chapter select (the town's book: ReserveChapter - FldMain step 0xe, FldChapterSelect;
    // InterfaceMain's ChapterSelectMenu / ChapterSelectTitleMenu - `menu` on menuCpt_io - /
    // ChapterSelectPicMenu - `mapPic` wearing mapPic_cNN, the focused row's place + 1) works the
    // same way: its rows (SetChapterList) the chapters whose flags are set (0x5355a8: 11, 18, 19,
    // 20), their ids 0x5355b4 (1, 5, 7, 8), named by announcemenuname 0x5355c4 (14 .. 17); picked,
    // the question (the System window: the chapter, announce 0x8b). Yes: the chapter chosen
    // (InterfaceWindow.GetChapterID), the town's chapter_select called; no: the chapters again;
    // back: the field.
    struct WorldSelect {
        bool chapters = false; // (the chapter select)
        int stage = 0; // 0 shut, 1 the dungeons, 2 the levels, 3 the question, 4 the pair's question
        float at = 0;  // (since: uiClock)
        int focus = 0, dungeonFocus = 0;
        int dungeon = 0, level = 0;
    } world;
    std::vector<ListRow> worldRows() {
        static const int kFlags[13] = {1041, 1049, 1046, 1047, 1048, 1044, 1043, 1045, 1042, 1050, 1051, 1051, 1052};
        static const int kDungeons[13] = {1, 9, 6, 7, 8, 4, 3, 5, 2, 10, 11, 12, 13};
        static const int kLevelFlags[11] = {0, 1081, 1082, 1083, 1084, 1085, 1086, 1086, 1087, 1087, 1088};
        std::vector<ListRow> rows;
        if (world.chapters) {
            static const int kChapterFlags[4] = {11, 18, 19, 20}, kChapters[4] = {1, 5, 7, 8};
            for (int i = 0; i < 4; ++i)
                if (game.flag(kChapterFlags[i])) {
                    ListRow r;
                    r.id = kChapters[i], r.name = menuName(14 + i), r.number = -1, r.icon = 1; // (SetChapterList: SetIcon 0, 1)
                    rows.push_back(r);
                }
        } else if (world.stage == 1) {
            // (SetMapList: SetIcon 0, 1; a dungeon's stars - cost_id at 1 + how many of its three
            // flags are set, the table at 0x534480, its thirteenth row running on into the next -;
            // its seal_id 3 once its flag at 0x534578 is)
            static const int kStars[13][3] = {{0, 0, 0}, {1533, 1777, 1137}, {1540, 1613, 1140}, {1547, 1628, 1138}, {1553, 1647, 1139},
                                              {1696, 1153, 1131}, {1572, 1152, 1134}, {1579, 1154, 1133}, {1701, 1151, 1132}, {0, 0, 0},
                                              {0, 0, 0}, {0, 0, 0}, {1041, 1049, 1046}};
            static const int kSeals[13] = {0, 1079, 1076, 1077, 1078, 1074, 1073, 1075, 1072, 1080, 0, 0, 0};
            for (int i = 0; i < 13; ++i)
                if (game.flag(kFlags[i])) {
                    ListRow r;
                    r.id = kDungeons[i], r.name = menuName(kDungeons[i]), r.number = -1, r.icon = 1;
                    if (kStars[i][0]) r.cost = 1 + game.flag(kStars[i][0]) + game.flag(kStars[i][1]) + game.flag(kStars[i][2]);
                    if (kSeals[i] && game.flag(kSeals[i])) r.seal = 3;
                    rows.push_back(r);
                }
        } else {
            for (int k = 1; k <= 11; ++k)
                if (kLevelFlags[k - 1] == 0 || game.flag(kLevelFlags[k - 1])) {
                    ListRow r;
                    r.id = k, r.name = menuName(0x14 + k), r.number = -1, r.icon = 1; // (SetLevelList: SetIcon 0)
                    rows.push_back(r);
                }
        }
        return rows;
    }
    void openWorldSelect() {
        world = WorldSelect();
        world.stage = 1, world.at = uiClock;
        SDL_Log("world select: %zu dungeons", worldRows().size());
        if (worldRows().empty()) world.stage = 0, pendingBranch = true;
    }
    void openChapterSelect() {
        world = WorldSelect();
        world.chapters = true, world.stage = 1, world.at = uiClock;
        SDL_Log("chapter select: %zu chapters", worldRows().size());
        if (worldRows().empty()) world.stage = 0, pendingBranch = true;
    }
    // MapSelectMenu::CheckCoupling: a dungeon whose flag (0x534578, by its number) is set, and of
    // its rows (0x5345d8: the dungeon, a flag, the two characters from 0) one whose flag is set,
    // wants that pair as the party (cr3_common_data +3, +4); the pair it wants, or none
    static bool worldPair(const cr2::GameState& g, int dungeon, int pair[2]) {
        static const int kFlag[10] = {0, 1079, 1076, 1077, 1078, 1074, 1073, 1075, 1072, 1080};
        static const int kRows[13][4] = {{4, 1061, 0, 2}, {3, 1062, 0, 5}, {5, 1063, 0, 3}, {6, 1066, 0, 5}, {7, 1067, 0, 3},
                                         {4, 1064, 0, 4}, {2, 1065, 2, 3}, {6, 1066, 0, 5}, {7, 1067, 0, 3}, {8, 1068, 0, 4},
                                         {6, 1066, 0, 5}, {7, 1067, 0, 3}, {8, 1068, 0, 4}};
        if (dungeon < 1 || dungeon > 9 || !g.flag(kFlag[dungeon])) return false;
        for (const auto& r : kRows)
            if (r[0] == dungeon && g.flag(r[1]) && (g.party[0] != r[2] || g.party[1] != r[3])) {
                pair[0] = r[2], pair[1] = r[3];
                return true;
            }
        return false;
    }
    void worldPick(const ListRow& r) {
        audio.playSe(0xb4, false);
        int pair[2];
        if (world.stage == 1 && world.chapters) world.dungeon = r.id, world.dungeonFocus = world.focus, world.stage = 3;
        else if (world.stage == 1 && worldPair(game, r.id, pair)) world.dungeon = r.id, world.dungeonFocus = world.focus, world.stage = 4; // (the System window: announce 0x81, yes / no)
        else if (world.stage == 1) world.dungeon = r.id, world.dungeonFocus = world.focus, world.stage = 2, world.focus = 0;
        else if (world.stage == 2) world.level = r.id, world.stage = 3;
        world.at = uiClock;
    }
    void worldBack() {
        audio.playSe(0xb5, false);
        if (world.stage == 2 || (world.stage == 3 && world.chapters)) world.stage = 1, world.focus = world.dungeonFocus, world.at = uiClock;
        else if (world.stage == 3) world.stage = 2, world.at = uiClock;
        else world.stage = 0, pendingBranch = true; // (back to the field: what else was reserved)
    }
    void worldAnswer(bool yes) {
        if (world.stage == 4) { // (yes: ChangeCoupling - the pair the party, cr3_common_data +1..+4 - then the levels; no: the dungeons again)
            audio.playSe(yes ? 0xb4 : 0xb5, false);
            int pair[2];
            if (yes && worldPair(game, world.dungeon, pair)) {
                game.party[0] = pair[0], game.party[1] = pair[1];
                SDL_Log("world select: the party %d and %d", pair[0], pair[1]);
            }
            if (yes) world.stage = 2, world.focus = 0;
            else world.stage = 1, world.focus = world.dungeonFocus;
            world.at = uiClock;
            return;
        }
        if (world.stage != 3) return;
        if (!yes) return worldBack();
        audio.playSe(0xb4, false);
        if (world.chapters) { // (FldMain step 0xe: CallEvent("chapter_select"))
            game.selectChapter = world.dungeon;
            SDL_Log("chapter select: chapter %d", world.dungeon);
            world.stage = 0;
            return callGimmick("chapter_select");
        }
        game.selectDungeon = world.dungeon, game.difficulty = world.level - 1;
        SDL_Log("world select: dungeon %d, level %d", world.dungeon, world.level);
        world.stage = 0;
        callGimmick("WarpTask"); // (FldMain step 10: CallEvent("WarpTask"))
    }
    void worldTap(int id) {
        if (world.stage >= 3) {
            if (id == 7601 || id == 7602) worldAnswer(id == 7601);
            return;
        }
        const std::vector<ListRow> rows = worldRows();
        if (id >= 1000 && id < 1000 + int(rows.size())) world.focus = id - 1000;
        else
            for (const ListRow& r : rows)
                if (r.id == id) return worldPick(r);
    }
    void worldKey(SDL_Keycode k) {
        if (world.stage >= 3) {
            if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_Y) worldAnswer(true);
            else if (k == SDLK_BACKSPACE || k == SDLK_N) worldAnswer(false);
            return;
        }
        const std::vector<ListRow> rows = worldRows();
        const int n = std::max(int(rows.size()), 1);
        if (k == SDLK_UP) world.focus = (world.focus + n - 1) % n;
        else if (k == SDLK_DOWN) world.focus = (world.focus + 1) % n;
        else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && world.focus < int(rows.size())) worldPick(rows[size_t(world.focus)]);
        else if (k == SDLK_BACKSPACE) worldBack();
    }
    void drawWorldSelect(const Mat4& view, int w, int h) {
        const float t = uiClock - world.at;
        UiFig& title = uiFig("menu"); // (MapSelectTitleMenu)
        title.fig.place = cr2::Mat34();
        poseUi(title, world.chapters ? "menucpt_io" : "menuwld_io", uiClock - world.at + (world.stage > 1 ? 10.0f : 0.0f));
        drawFigure(title.fig, view.data());
        if (world.stage == 3 && world.chapters) {
            const std::string q = menuName(14 + std::clamp(world.dungeonFocus, 0, 3)) + "\\n" + announce(0x8b);
            return drawAsk(q, t, view, w, h, 7601, 7602);
        }
        if (world.stage == 4) return drawAsk(announce(0x81), t, view, w, h, 7601, 7602);
        if (world.stage == 3) {
            const std::string q = menuName(world.dungeon) + "\\n" + menuName(0x14 + world.level) + "\\n" + announce(0x80);
            return drawAsk(q, t, view, w, h, 7601, 7602);
        }
        const std::vector<ListRow> rows = worldRows();
        if (rows.empty()) return;
        world.focus = std::clamp(world.focus, 0, int(rows.size()) - 1);
        const int shown = world.chapters ? world.focus + 1 : world.stage == 1 ? rows[size_t(world.focus)].id : world.dungeon;
        UiFig& pic = uiFig("mappic"); // (MapSelectPicMenu: the dungeon's picture; ChapterSelectPicMenu the chapter's)
        char tex[24];
        std::snprintf(tex, sizeof tex, world.chapters ? "mapPic_c%02d" : "mapPic_d%02d", shown);
        if (cr2::Texture* tx = texture(tex))
            for (ViewMesh& vm : pic.fig.meshes) vm.texture = tx;
        pic.fig.place = cr2::Mat34();
        poseUi(pic, "mappic_in", t);
        drawFigure(pic.fig, view.data());
        drawSkillList(rows, rows[size_t(world.focus)].id, t, view, w, h, -1, -1);
    }

    // The library (a shelf: ReserveLibrary - FldMain step 0xf, FldLibrary; InterfaceMain::
    // SetLibraryParameter - ArchiveWindowMenu, its ArchiveListMenu): the shelf's entries
    // (SetArchiveList: those of its category - InterfaceWindow.SetArchiveID - from
    // ArchiveCategoryStartTable[c - 1] to [c], each shown if its ArchiveOpenFlagTable flag is none
    // or set; entry k's id k + 1, its title LibraryTxt's), each with main icon 69 and the seal
    // "new" (2) while flag 1350 + id is set (SetListItemParam). Picked, the list shut and the
    // area's library_disp called (InterfaceEvent::CallScript: the entry - GetLibraryID - read in
    // window 6, its flag cleared), then the list again; Back: the field.
    struct Library {
        int stage = 0; // 0 shut, 1 the list, 2 an entry being read (library_disp)
        int focus = 0;
        float at = 0;
    } library;
    std::vector<ListRow> libraryRows() {
        static const short kOpenFlags[50] = {0, 1613, 1628, 1647, 1777, 0, 1573, 1573, 1573, 1573, 0, 0, 0, 0, 0, 1171, 1172,
                                             1173, 1174, 1175, 1176, 1177, 1178, 1179, 1180, 1181}; // (ArchiveOpenFlagTable)
        static const short kStarts[13] = {0, 5, 10, 14, 26, 30, 32, 34, 39, 42, 45, 47, 50}; // (ArchiveCategoryStartTable)
        std::vector<ListRow> rows;
        const int c = game.libraryArchive;
        if (c < 1 || c > 12) return rows;
        for (int k = kStarts[c - 1]; k < kStarts[c]; ++k)
            if (kOpenFlags[k] == 0 || game.flag(kOpenFlags[k])) {
                ListRow r;
                r.id = k + 1, r.name = messageText("librarytxt", k + 1), r.number = -1, r.icon = 69;
                if (game.flag(1350 + r.id)) r.seal = 2;
                rows.push_back(r);
            }
        return rows;
    }
    void openLibrary() {
        library = Library();
        library.stage = 1, library.at = uiClock;
        SDL_Log("library: shelf %d, %zu entries", game.libraryArchive, libraryRows().size());
        if (libraryRows().empty()) library.stage = 0, pendingBranch = true;
    }
    void libraryPick(const ListRow& r) {
        audio.playSe(0xb4, false);
        game.libraryId = r.id;
        library.stage = 2;
        SDL_Log("library: entry %d", r.id);
        callGimmick("library_disp");
        if (!script->running()) library.stage = 1, library.at = uiClock; // (no reading: the list again)
    }
    void libraryBack() {
        audio.playSe(0xb5, false);
        library.stage = 0, pendingBranch = true; // (back to the field)
    }
    void libraryTap(int id) {
        const std::vector<ListRow> rows = libraryRows();
        if (id == 100) return libraryBack();
        if (id >= 1000 && id < 1000 + int(rows.size())) library.focus = id - 1000;
        else
            for (const ListRow& r : rows)
                if (r.id == id) return libraryPick(r);
    }
    void libraryKey(SDL_Keycode k) {
        const std::vector<ListRow> rows = libraryRows();
        const int n = std::max(int(rows.size()), 1);
        if (k == SDLK_UP) library.focus = (library.focus + n - 1) % n;
        else if (k == SDLK_DOWN) library.focus = (library.focus + 1) % n;
        else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && library.focus < int(rows.size())) libraryPick(rows[size_t(library.focus)]);
        else if (k == SDLK_BACKSPACE) libraryBack();
    }
    void drawLibrary(const Mat4& view, int w, int h) {
        const std::vector<ListRow> rows = libraryRows();
        if (rows.empty()) return;
        library.focus = std::clamp(library.focus, 0, int(rows.size()) - 1);
        drawSkillList(rows, rows[size_t(library.focus)].id, uiClock - library.at, view, w, h, -1, -1);
    }

    // The shop (Piupiu's: ReserveShop - FldMain step 0xb, FldShop; InterfaceMain::
    // SetFieldShopParameter): ShopTitleMenu's `menu` (menuShop_io), Piupiu's cut-in (cutIn on
    // cutInSgl_io, wearing cutIn102_L02, its GEO_face parts hidden), the Oz window; ShopFirstMenu
    // (shopBtn_io: circle buttons 13 talk - the town's shop_talk -, 14 buy, 15 sell, btlBack);
    // ShopSecondMenu (shopCmd_io: 16 items, 21 weapons, 22 armour, 23 accessories); the lists
    // (ShopItem / ShopWepon Buy / SellListMenu: the shop's rank the flags 1081 .. 1085 set - those
    // items of the kind (+0x10) whose rank (+0x2c) is no higher, an equipment's for the character
    // shown (its wearers, +0x12), at their price (+0x24) - greyed past the Oz, or held 99 -; to sell,
    // those held, at +0x28 - an equipment's greyed when none is spare of what is worn); then how many
    // (CampChooseQuantityMenu: shopNum_io, 1 .. GetItemBuyUpLimit - the Oz's worth, 99 less those held -
    // / the spare ones; pliceNum_io the whole price) and the System window's question (announce 0x70 /
    // 0x71: the item, those held): yes - the items and the Oz (SE 200).
    struct Shop {
        int stage = 0; // 0 shut, 1 buy / sell / talk, 2 the kinds, 3 the list, 4 how many and the question,
                       // 5 equipment bought: "Equip %s?" (ShopCutInPiupiuMenu 0x12 -> 0x14, announce 0x72)
        bool buy = true;
        int kind = 0;     // (0 items, 1 weapons, 2 armour, 3 accessories)
        int focus = 0, item = 0, count = 1, who = 0;
        float at = 0, askAt = 0;
    } shop;
    // The pub (ReservePUB - FldMain step 0xd, FldPUB; InterfaceMain::SetPubParameter: PubTitleMenu's
    // `menu` on menuPub_io, PubTopMenu, PubMainMenu's pubGirls_io): the board (posterBtn) - a
    // pubPoster_io at each call_pubPoster_ioNN for the requests posted (flags 1201 .., nine at
    // most), the one taken (Cr3UtilGlobalWorkGet(7)) its checkMark_io, a new one (flag 1250 + id)
    // its seal_id; a tap focuses one (SE 0xb3: MTR_posterL at 1.5, faceSlct on it, its title -
    // PUBTxt id - in campHelp_io), another on it picks it (SE 0xb4, its "new" flag cleared); Back
    // (btlBack) ends it (3: the field again). Picked, PubDetailMenu: pubWin_io with its text (PUBTxt
    // id + 60, a line each "\n"), its title, accept_io (or giveUp_io for the one taken: its circle
    // button), Back to the board. Accepted: the request taken (work 7), 1 - the town's pub_checkA;
    // given up: the System window's question (announce 0x83), yes - none taken, 2 - pub_checkG.
    struct Pub {
        int stage = 0; // 0 shut, 1 the board, 2 a request, 3 giving it up?, 4 the list
        int focus = 0, request = 0, listFocus = 0; // (listFocus: the list's request, 0 none)
        float at = 0;
    } pub;
    std::vector<int> pubPosters() const {
        std::vector<int> v;
        for (int i = 1; i <= 50 && v.size() < 9; ++i)
            if (game.flag(1200 + i)) v.push_back(i);
        return v;
    }
    int pubListCount() const { return !game.flag(201) ? 28 : !game.flag(202) ? 42 : 50; } // (PubListMenu::Initialize)
    // the list's sign of a request (PosterSId::SetSign - hud_posterS's quarters): 1 posted, 2 taken,
    // 3 done (flag 1300 + id), 4 not yet
    int pubSign(int id) const {
        int sign = game.flag(1200 + id) ? 1 : 4;
        if (game.globalWork[7] == id) sign = 2;
        if (game.flag(1300 + id)) sign = 3;
        return sign;
    }
    void openPub() {
        pub = Pub();
        pub.stage = 1, pub.at = uiClock;
        SDL_Log("pub: %zu requests posted", pubPosters().size());
    }
    void pubEnd(int result) { // (PubMainMenu: ResultPubParameter - FldMain's CallEvent / CallAtMenuFinished)
        pub.stage = 0;
        SDL_Log("pub: %d", result);
        if (result == 1) callGimmick("pub_checkA");
        else if (result == 2) callGimmick("pub_checkG");
        else pendingBranch = true;
    }
    void pubTap(int id) {
        const std::vector<int> posters = pubPosters();
        if (pub.stage == 1) {
            if (id == 100) return audio.playSe(0xb5, false), pubEnd(3);
            if (id == 35) return audio.playSe(0xb4, false), void((pub.stage = 4, pub.at = uiClock, pub.listFocus = 0)); // (Check: PubTopMenu 200)
            if (id >= 1000 && id < 1000 + int(posters.size())) {
                const int k = id - 1000;
                if (k != pub.focus) return audio.playSe(0xb3, false), void(pub.focus = k);
                audio.playSe(0xb4, false);
                pub.request = posters[size_t(k)];
                game.setFlag(1250 + pub.request, false);
                pub.stage = 2, pub.at = uiClock;
                SDL_Log("pub: request %d", pub.request);
            }
        } else if (pub.stage == 2) {
            if (id == 100) return audio.playSe(0xb5, false), void((pub.stage = 1, pub.at = uiClock));
            if (id == 38 || id == 39) {
                audio.playSe(0xb4, false);
                if (game.globalWork[7] == pub.request) pub.stage = 3, pub.at = uiClock; // (taken already: give it up?)
                else game.globalWork[7] = pub.request, pubEnd(1);
            }
        } else if (pub.stage == 4) { // (PubListMenu: a tap shows a request's title; Back the board)
            if (id == 100) audio.playSe(0xb5, false), pub.stage = 1, pub.at = uiClock;
            else if (id >= 2000 && id <= 2050) audio.playSe(0xb4, false), pub.listFocus = id - 2000;
        } else if (pub.stage == 3) {
            if (id == 7601) audio.playSe(0xb4, false), game.globalWork[7] = 0, pubEnd(2);
            else if (id == 7602) audio.playSe(0xb5, false), pub.stage = 2, pub.at = uiClock;
        }
    }
    void pubKey(SDL_Keycode k) {
        const int n = std::max(int(pubPosters().size()), 1);
        if (pub.stage == 4 && (k == SDLK_LEFT || k == SDLK_RIGHT)) {
            const int count = pubListCount();
            pubTap(2000 + (pub.listFocus + (k == SDLK_LEFT ? count - 2 : 0)) % count + 1);
        } else if (pub.stage == 1 && (k == SDLK_LEFT || k == SDLK_RIGHT)) pubTap(1000 + (pub.focus + (k == SDLK_LEFT ? n - 1 : 1)) % n);
        else if (pub.stage == 1 && (k == SDLK_RETURN || k == SDLK_KP_ENTER)) pubTap(1000 + pub.focus);
        else if (pub.stage == 2 && (k == SDLK_RETURN || k == SDLK_KP_ENTER)) pubTap(game.globalWork[7] == pub.request ? 39 : 38);
        else if (pub.stage == 3 && (k == SDLK_Y || k == SDLK_RETURN)) pubTap(7601);
        else if (pub.stage == 3 && k == SDLK_N) pubTap(7602);
        else if (k == SDLK_BACKSPACE) pub.stage == 3 ? pubTap(7602) : pubTap(100);
    }
    void drawPub(const Mat4& view, int w, int h) {
        const float t = uiClock - pub.at;
        auto part = [&](const char* fig, const char* anim, float x = 0, float y = 0, float tt = -1) -> UiFig& {
            UiFig& u = uiFig(fig);
            u.fig.place = cr2::Mat34();
            u.fig.place.m[3] = x, u.fig.place.m[7] = y;
            poseUi(u, anim, tt >= 0 ? tt : t);
            drawFigure(u.fig, view.data());
            return u;
        };
        part("pubgirls_io", "pubgirls_io");
        part("menu", "menupub_io");
        float x, y, sx, sy, ex, ey;
        auto back = [&]() {
            UiFig& bk = part("btlback", "btlback_in");
            if (uiJoint(bk, "tapAreaS01", sx, sy) && uiJoint(bk, "tapAreaE01", ex, ey)) tapRects.push_back({100, sx, sy, ex, ey});
        };
        auto help = [&](int id) {
            UiFig& hp = part("camphelp_io", "camphelp_io");
            if (uiJoint(hp, "typeM_L01", x, y)) drawBorderedText(messageText("pubtxt", id), 24, x, y, w, h, true);
        };
        const std::vector<int> posters = pubPosters();
        if (pub.stage == 1) {
            UiFig& board = part("posterbtn", "posterbtn");
            pub.focus = std::clamp(pub.focus, 0, std::max(int(posters.size()) - 1, 0));
            for (size_t k = 0; k < posters.size(); ++k) {
                char jn[32];
                std::snprintf(jn, sizeof jn, "call_pubPoster_io%02zu", k + 1);
                if (!uiJoint(board, jn, x, y)) continue;
                UiFig& p = uiFig("pubposter_io");
                p.fig.place = cr2::Mat34();
                p.fig.place.m[3] = x, p.fig.place.m[7] = y;
                poseUi(p, "pubposter_io", t);
                for (size_t mi = 0; mi < p.fig.meshes.size(); ++mi) { // (SetCursorPosition: the focused one's MTR_posterL at 1.5)
                    ViewMesh& vm = p.fig.meshes[mi];
                    const uint32_t m = vm.source.material;
                    const bool lit = int(k) == pub.focus && m < p.materials.size() && p.materials[m] == "MTR_posterL";
                    vm.material.params[0x33] = lit ? std::vector<float>{1.5f, 1.5f, 1.5f, 1.0f} : vm.baseMaterial.params[0x33];
                    if (vm.material.params[0x33].empty()) vm.material.params.erase(0x33);
                }
                drawFigure(p.fig, view.data());
                if (uiJoint(p, "tapAreaS01", sx, sy) && uiJoint(p, "tapAreaE01", ex, ey)) tapRects.push_back({1000 + int(k), sx, sy, ex, ey});
                const int id = posters[k];
                float mx, my;
                if (id == game.globalWork[7] && uiJoint(p, "call_checkMark", mx, my)) part("checkmark_io", "checkmark_io", mx, my);
                if (game.flag(1250 + id) && uiJoint(p, "call_seal_id", mx, my)) part("seal_id", "seal_id", mx, my, 0.1f); // (PubTopMenu: 0.1 s - "New!")
                if (int(k) == pub.focus && uiJoint(p, "call_faceSlct", mx, my)) part("faceslct", "faceslct", mx, my);
            }
            if (!posters.empty()) help(posters[size_t(pub.focus)]);
            drawCommandMenu("checkbtn_io", t, view, {}, {}, 0, 0, 0, "checkbtn_io"); // (the list: circle 35 - PubTopMenu's 200)
            back();
        } else if (pub.stage == 4) { // (PubListMenu: posterIcon, each request's sign at call_posterS_idNN, the share done)
            UiFig& icons = part("postericon", "postericon");
            cr2::Texture* tex = texture("hud_posters");
            const Mat4 proj = uiProjection(w, h);
            static const float kUv[5][4] = {{0, 0, 0, 0}, {0, 0, 0.5f, 0.5f}, {0.5f, 0, 1, 0.5f}, {0, 0.5f, 0.5f, 1}, {0.5f, 0.5f, 1, 1}};
            for (int id = 1; id <= pubListCount(); ++id) {
                char jn[32];
                std::snprintf(jn, sizeof jn, "call_posterS_id%02d", id);
                if (!uiJoint(icons, jn, x, y) || !tex) continue;
                const float* uv = kUv[pubSign(id)];
                const float h2 = 0.032f; // (a 0.064 square)
                const float v[24] = {x - h2, y - h2, uv[0], uv[3], x + h2, y - h2, uv[2], uv[3], x + h2, y + h2, uv[2], uv[1],
                                     x - h2, y - h2, uv[0], uv[3], x + h2, y + h2, uv[2], uv[1], x - h2, y + h2, uv[0], uv[1]};
                const float k = id == pub.listFocus ? 2.0f : 1.0f; // (the focused one's diffuse 2)
                const float rgba[4] = {k, k, k, 1};
                dev->drawQuad(*tex, v, proj.data(), rgba);
                tapRects.push_back({2000 + id, x - h2, y - h2, x + h2, y + h2});
                if (id == pub.listFocus) {
                    UiFig& fs = uiFig("faceslct");
                    fs.fig.place = cr2::Mat34();
                    fs.fig.place.m[0] = fs.fig.place.m[5] = fs.fig.place.m[10] = 0.5f; // (scaled half)
                    fs.fig.place.m[3] = x, fs.fig.place.m[7] = y;
                    poseUi(fs, "faceslct", t);
                    drawFigure(fs.fig, view.data());
                }
            }
            int done = 0;
            for (int f = 1301; f <= 1350; ++f) done += game.flag(f);
            UiFig& pc = part("percent_io", "percent_io");
            if (uiJoint(pc, "call_dotNum_id1", x, y)) drawDigits(done * 100 / 50, 80, 3, x, y, proj); // (GetPercentInteger(done, 50))
            if (pub.listFocus > 0) help(pub.listFocus);
            back();
        } else if (pub.stage == 3) {
            drawAsk(announce(0x83), t, view, w, h, 7601, 7602); // (PubMainMenu 9 / 10: the request shut, then give it up?)
        } else {
            UiFig& win = part("pubwin_io", "pubwin_io");
            const std::string text = messageText("pubtxt", pub.request + 60);
            size_t from = 0;
            for (int line = 1; line <= 8 && from <= text.size(); ++line) {
                const size_t at = text.find("\\n", from);
                if (uiJoint(win, "typeS_L0" + std::to_string(line), x, y))
                    drawBorderedText(text.substr(from, at == std::string::npos ? std::string::npos : at - from), 22, x, y, w, h, true);
                if (at == std::string::npos) break;
                from = at + 2;
            }
            help(pub.request);
            const bool taken = game.globalWork[7] == pub.request;
            drawCommandMenu(taken ? "giveup_io" : "accept_io", t, view, {}, {}, 0, 0, 0, taken ? "giveup_io" : "accept_io");
            back();
        }
    }

    void openShop() {
        shop = Shop();
        shop.stage = 1, shop.at = uiClock;
        SDL_Log("shop: open");
    }
    int shopRank() const {
        int r = 0;
        for (int f = 1081; f <= 1085; ++f) r += game.flag(f);
        return r;
    }
    std::vector<int> shopChars() const { // (the characters an equipment list is for: ShopWepon..ListMenu::CharaChange - every one joined, Cr3UtilValidCharacter)
        std::vector<int> c = validChars();
        if (c.empty()) c.push_back(0);
        return c;
    }
    int spareCount(int id) const { // (Cr3UtilGetNotEquippedItemNumber: held, less those worn)
        int worn = 0;
        for (int c = 0; c < 9; ++c)
            for (int s = 0; s < 3; ++s) worn += game.equip[c][s] == id;
        return std::max(int(game.itemCount[id]) - worn, 0);
    }
    std::vector<ListRow> shopRows() {
        std::vector<ListRow> rows;
        const int rank = shopRank();
        const std::vector<int> chars = shopChars();
        const int ch = chars[size_t(std::clamp(shop.who, 0, int(chars.size()) - 1))];
        for (int id = 1; id < 300; ++id) {
            const cr2::BattleData::Item* it = battleData.item(id);
            if (!it || it->type != shop.kind) continue;
            if (shop.kind > 0 && !(it->wearers >> ch & 1)) continue;
            ListRow r;
            r.id = id, r.name = itemName(id), r.icon = it->icon, r.help = itemHelp(id); // (ChangeHelpText: Cr3UtilGetItemHelpText)
            r.worn = shop.kind > 0 && game.equip[ch][shop.kind - 1] == id; // (Cr3UtilGetEquipItem)
            r.price = true;
            if (shop.buy) {
                if (it->shopRank > rank) continue;
                r.number = it->price, r.selectable = game.oz >= it->price && game.itemCount[id] < 99;
            } else {
                if (game.itemCount[id] <= 0) continue;
                r.number = it->sellPrice, r.selectable = shop.kind == 0 || spareCount(id) > 0;
            }
            rows.push_back(r);
        }
        return rows;
    }
    int shopLimit() const {
        const cr2::BattleData::Item* it = battleData.item(shop.item);
        if (!it) return 0;
        if (!shop.buy) return shop.kind == 0 ? game.itemCount[shop.item] : spareCount(shop.item);
        const int room = 99 - game.itemCount[shop.item];
        return it->price <= 0 ? room : std::min(room, game.oz / it->price);
    }
    void shopClose(const char* call = nullptr) {
        shop.stage = 0;
        if (call) callGimmick(call); // (FldMain: CallEvent)
        else pendingBranch = true;   // (back to the field)
    }
    void shopTap(int id) {
        auto stage = [&](int s) { shop.stage = s, shop.at = uiClock, shop.focus = 0; };
        if (shop.stage == 1) {
            if (id == 14 || id == 15) audio.playSe(0xb4, false), shop.buy = id == 14, stage(2); // (ShopFirstMenu: 4 buy, 5 sell)
            else if (id == 13) audio.playSe(0xb4, false), shopClose("shop_talk"); // (3: FldShop's 3 - CallEvent shop_talk)
            else if (id == 100) audio.playSe(0xb5, false), shopClose();
        } else if (shop.stage == 2) {
            static const std::pair<int, int> kKinds[4] = {{16, 0}, {21, 1}, {22, 2}, {23, 3}};
            for (const auto& [b, k] : kKinds)
                if (id == b) audio.playSe(0xb4, false), shop.kind = k, shop.who = 0, stage(3);
            if (id == 100) audio.playSe(0xb5, false), stage(1);
        } else if (shop.stage == 3 && (id == 6511 || id == 6512) && shop.kind > 0) { // (the arrows: CharaChange -1 / +1, SE 0xb7)
            const int c = int(shopChars().size());
            shop.who = (shop.who + (id == 6512 ? 1 : c - 1)) % std::max(c, 1), shop.focus = 0;
            audio.playSe(0xb7, false);
        } else if (shop.stage == 3 && (id == 6501 || id == 6502) && shop.kind > 0) { // (a small tab: that kind's list - its frame / 2)
            static const int kTabs[4][3] = {{7, -1, -1}, {1, 4, 6}, {3, 6, 2}, {5, 2, 4}};
            audio.playSe(0xb4, false), shop.kind = kTabs[shop.kind][id - 6500] / 2, shop.focus = 0;
        } else if (shop.stage == 3) {
            const std::vector<ListRow> rows = shopRows();
            if (id >= 1000 && id < 1000 + int(rows.size())) shop.focus = id - 1000;
            else if (id == 100) audio.playSe(0xb5, false), stage(2);
            else
                for (const ListRow& r : rows)
                    if (r.id == id && r.selectable && shopLimit() >= 0) {
                        shop.item = id;
                        if (shopLimit() < 1) return (void)audio.playSe(0xb6, false);
                        audio.playSe(0xb4, false);
                        shop.count = 1, shop.stage = 4, shop.at = uiClock;
                    }
        } else if (shop.stage == 4) {
            if (id == 7101 || id == 7102) { // (CampChooseQuantityMenu::SetViewNumber: one less / more, within 1 .. its limit)
                const int n = shop.count + (id == 7102 ? 1 : -1);
                if (n < 1 || n > shopLimit()) audio.playSe(0xb6, false);
                else shop.count = n, audio.playSe(0xb4, false);
            } else if (id == 7801 || id == 7802) {
                const cr2::BattleData::Item* it = battleData.item(shop.item);
                if (id == 7801 && it) { // (ShopCutInPiupiuMenu::Update 10 / 12)
                    if (shop.buy) game.itemCount[shop.item] = static_cast<short>(std::min(game.itemCount[shop.item] + shop.count, 99)), game.oz -= it->price * shop.count;
                    else game.itemCount[shop.item] = static_cast<short>(std::max(game.itemCount[shop.item] - shop.count, 0)), game.oz += it->sellPrice * shop.count;
                    game.oz = std::clamp(game.oz, 0, 9999999);
                    audio.playSe(200, false);
                    SDL_Log("shop: %s %d of %s - %d Oz now", shop.buy ? "bought" : "sold", shop.count, it->name[1].c_str(), game.oz);
                    if (shop.buy && shop.kind > 0) return (void)(shop.stage = 5, shop.askAt = uiClock); // (0x12: the question)
                } else audio.playSe(0xb4, false);
                shop.stage = 3, shop.at = uiClock;
            }
        } else if (shop.stage == 5 && (id == 7801 || id == 7802)) { // (0x14: Cr3UtilChangeEquipItem on the list's character, Cr3UtilAdjustment - 199; no, 0xb4)
            if (id == 7801) {
                const std::vector<int> chars = shopChars();
                const int ch = chars[size_t(std::clamp(shop.who, 0, int(chars.size()) - 1))];
                game.equip[ch][shop.kind - 1] = static_cast<short>(shop.item);
                clampMp(ch);
                audio.playSe(199, false);
                SDL_Log("shop: %d wears %d", ch + 1, shop.item);
            } else audio.playSe(0xb4, false);
            shop.stage = 3;
        }
    }
    // The explorer's shopping (--explore), as a player stocks up: for each of the pair the best
    // weapon, armour and accessory sold that it can wear and that beats what it has (by what they
    // add, all told), then healing items - the strongest the Oz buys, up to 10 - and revivals, up
    // to 3, from what is left; then out
    void exploreShop() {
        const int rank = shopRank();
        auto worth = [&](const cr2::BattleData::Item* it) {
            if (!it) return -1;
            int w = it->hp / 10 + it->mp / 5;
            for (int k = 0; k < 5; ++k) w += it->param[k];
            return w;
        };
        auto buy = [&](int id, int n) {
            const cr2::BattleData::Item* it = battleData.item(id);
            if (!it || n <= 0) return;
            n = std::min(n, 99 - game.itemCount[id]);
            if (it->price > 0) n = std::min(n, game.oz / it->price);
            if (n <= 0) return;
            game.itemCount[id] = static_cast<short>(game.itemCount[id] + n), game.oz -= it->price * n;
            exploreLog("  shop: bought " + std::to_string(n) + " of item " + std::to_string(id) + " - " + std::to_string(game.oz) + " Oz left");
        };
        for (int ch : game.party)
            for (int slot = 0; ch >= 0 && ch < 9 && slot < 3; ++slot) {
                int best = 0, bestWorth = worth(game.equip[ch][slot] > 0 ? battleData.item(game.equip[ch][slot]) : nullptr);
                for (int id = 1; id < 300; ++id) {
                    const cr2::BattleData::Item* it = battleData.item(id);
                    if (!it || it->type != slot + 1 || it->shopRank > rank || it->price <= 0 || it->price > game.oz || !(it->wearers >> ch & 1)) continue;
                    if (worth(it) > bestWorth) best = id, bestWorth = worth(it);
                }
                if (best && spareCount(best) == 0) buy(best, 1);
            }
        auto kindOf = [&](const cr2::BattleData::Item* it) { const cr2::BattleData::Command* c = it ? battleData.command(it->command) : nullptr; return c ? c->kind : -1; };
        int heal = 0, revive = 0;
        for (int id = 1; id < 300; ++id) {
            const cr2::BattleData::Item* it = battleData.item(id);
            if (!it || it->type != 0 || !it->battle || it->shopRank > rank || it->price <= 0) continue;
            const int k = kindOf(it);
            if ((k == 24 || k == 25) && (!revive || it->price < battleData.item(revive)->price)) revive = id;
            else if (k >= 18 && k <= 23 && it->hp > 0 && it->price * 10 <= game.oz && (!heal || it->hp > battleData.item(heal)->hp)) heal = id;
        }
        if (heal) buy(heal, 10 - game.itemCount[heal]);
        if (revive) buy(revive, 3 - game.itemCount[revive]);
        exploreOutfit(); // (what was bought, worn)
        shopClose();
    }
    void shopKey(SDL_Keycode k) {
        if (shop.stage == 1 || shop.stage == 2) {
            static const int kFirst[3] = {14, 15, 13}, kKinds[4] = {16, 21, 22, 23}; // (1 buy, 2 sell, 3 talk)
            if (k >= SDLK_1 && k <= SDLK_4) {
                const int i = int(k - SDLK_1);
                if (shop.stage == 1 && i < 3) shopTap(kFirst[i]);
                else if (shop.stage == 2) shopTap(kKinds[i]);
            } else if (k == SDLK_BACKSPACE) shopTap(100);
            return;
        }
        if (shop.stage == 5) {
            if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_Y) shopTap(7801);
            else if (k == SDLK_BACKSPACE || k == SDLK_N) shopTap(7802);
            return;
        }
        if (shop.stage == 4) {
            if (k == SDLK_LEFT || k == SDLK_DOWN) shopTap(7101);
            else if (k == SDLK_RIGHT || k == SDLK_UP) shopTap(7102);
            else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_Y) shopTap(7801);
            else if (k == SDLK_BACKSPACE || k == SDLK_N) shopTap(7802);
            return;
        }
        const std::vector<ListRow> rows = shopRows();
        const int n = std::max(int(rows.size()), 1);
        if (k == SDLK_UP) shop.focus = (shop.focus + n - 1) % n;
        else if (k == SDLK_DOWN) shop.focus = (shop.focus + 1) % n;
        else if ((k == SDLK_LEFT || k == SDLK_RIGHT) && shop.kind > 0) { // (ShopWepon..ListMenu::CharaChange)
            const int c = int(shopChars().size());
            shop.who = (shop.who + (k == SDLK_RIGHT ? 1 : c - 1)) % c, shop.focus = 0;
            audio.playSe(0xb4, false);
        } else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && shop.focus < int(rows.size())) shopTap(rows[size_t(shop.focus)].id);
        else if (k == SDLK_BACKSPACE) shopTap(100);
    }
    void shopToken(const std::string& t) {
        static const std::map<std::string, int> kButtons = {{"talk", 13}, {"buy", 14}, {"sell", 15}, {"items", 16}, {"weapons", 21},
                                                            {"armour", 22}, {"accessories", 23}, {"back", 100}};
        if (auto b = kButtons.find(t); b != kButtons.end()) return shopTap(b->second);
        static const std::map<std::string, SDL_Keycode> kKeys = {{"up", SDLK_UP}, {"down", SDLK_DOWN}, {"pick", SDLK_RETURN}, {"more", SDLK_UP},
                                                                 {"less", SDLK_DOWN}, {"yes", SDLK_Y}, {"no", SDLK_N}, {"char", SDLK_RIGHT}};
        if (auto k = kKeys.find(t); k != kKeys.end()) shopKey(k->second);
    }
    void drawShop(const Mat4& view, const Mat4& proj, int w, int h) {
        const float t = uiClock - shop.at;
        auto keepLook = buttonLook;
        auto keepLevel = buttonLevel;
        auto keepFighter = buttonFighter;
        buttonLook = nullptr, buttonLevel = nullptr, buttonFighter = nullptr;
        UiFig& title = uiFig("menu"); // (ShopTitleMenu)
        title.fig.place = cr2::Mat34();
        poseUi(title, "menushop_io", uiClock - shop.at + (shop.stage > 1 ? 10.0f : 0.0f));
        drawFigure(title.fig, view.data());
        UiFig& cut = uiFig("cutin"); // (ShopCutInPiupiuMenu: Piupiu)
        cut.fig.place = cr2::Mat34();
        poseUi(cut, "cutinsgl_io", 10.0f);
        cr2::Texture* face = texture("cutIn102_L02");
        for (ViewMesh& vm : cut.fig.meshes) {
            const uint32_t j = vm.source.palette.empty() ? ~0u : vm.source.palette[0];
            const std::string jn = j < cut.joints.size() ? cut.joints[j] : "";
            vm.hidden = jn == "GEO_faceUp_R" || jn == "GEO_faceLow_R";
            if (face) vm.texture = face;
        }
        if (shop.stage <= 2) drawFigure(cut.fig, view.data());
        { // (OzViewWindowMenu 0x53 - SetOzWindowParameter(true), the shop's: ozWinBlk_io, hud_shpOzWin, its sign 4)
            float x, y;
            UiFig& oz = uiFig("ozwinblk_io");
            oz.fig.place = cr2::Mat34();
            poseUi(oz, "ozwinblk_io", t);
            drawFigure(oz.fig, view.data());
            if (uiJoint(oz, "call_status_fr04", x, y)) drawStatusSign(4, x, y, proj);
            if (uiJoint(oz, "call_dotNum_id01", x, y)) drawDigits(std::clamp(game.oz, 0, 9999999), 110, 7, x, y, proj); // (id01 the ones, id07 the millions)
        }
        auto back = [&] {
            UiFig& bk = uiFig("btlback");
            bk.fig.place = cr2::Mat34();
            poseUi(bk, "btlback_in", t);
            drawFigure(bk.fig, view.data());
            float sx, sy, ex, ey;
            if (uiJoint(bk, "tapAreaS01", sx, sy) && uiJoint(bk, "tapAreaE01", ex, ey)) tapRects.push_back({100, sx, sy, ex, ey});
        };
        if (shop.stage == 1) drawCommandMenu("shopbtn_io", t, view, {}, {}, w, h, 0, "shopbtn_io"), back();
        else if (shop.stage == 2) { // (ShopSecondMenu::Initialize: ChangeAnimeTime - shopCmd_io to half its length, its in; to sell, items / accessories greyed with none to sell - Cr3UtilNumberAvailableForSale)
            UiFig& cmd = uiFig("shopcmd_io");
            poseUi(cmd, "shopcmd_io", 0);
            const float half = cmd.fig.animLoaded ? cmd.fig.anim.duration() * 0.5f : t;
            auto sellable = [&](int kind) {
                for (int id = 1; id < 300; ++id)
                    if (const cr2::BattleData::Item* it = game.itemCount[id] > 0 ? battleData.item(id) : nullptr)
                        if (it->type == kind && (kind == 0 || spareCount(id) > 0)) return true;
                return false;
            };
            buttonLook = [&, this](int n) { return !shop.buy && ((n == 16 && !sellable(0)) || (n == 23 && !sellable(3))) ? 0 : 1; };
            drawCommandMenu("shopcmd_io", std::min(t, half), view, {}, {}, w, h, 0, "shopcmd_io");
            buttonLook = nullptr;
            // the buy / sell sign (shopMode_io; SetSellBuySelected: the one chosen's material at
            // 1, the other's at 0.3)
            UiFig& mode = uiFig("shopmode_io");
            mode.fig.place = cr2::Mat34();
            poseUi(mode, "shopmode_io", t);
            for (ViewMesh& vm : mode.fig.meshes) {
                const uint32_t m = vm.source.material;
                const std::string mn = m < mode.materials.size() ? mode.materials[m] : "";
                const float k = mn == "MTR_buy" ? (shop.buy ? 1.0f : 0.3f) : mn == "MTR_sell" ? (shop.buy ? 0.3f : 1.0f) : -1.0f;
                vm.material.params[0x33] = k >= 0 ? std::vector<float>{k, k, k, 1.0f} : vm.baseMaterial.params[0x33];
                if (vm.material.params[0x33].empty()) vm.material.params.erase(0x33);
            }
            drawFigure(mode.fig, view.data());
            back();
        }
        else if (shop.stage == 3 || shop.stage == 5) {
            const std::vector<ListRow> rows = shopRows();
            if (rows.empty()) back();
            else { // (its own back: scrlBack, 100)
                shop.focus = std::clamp(shop.focus, 0, int(rows.size()) - 1);
                if (shop.kind > 0) { // (SetCharaStatusParamerter / SetItemStatusParamerter: the character's windows, the change the focused one makes worn)
                    const std::vector<int> chars = shopChars();
                    const int ch = chars[size_t(std::clamp(shop.who, 0, int(chars.size()) - 1))];
                    int eq[8], eq2[8], change[5] = {0, 0, 0, 0, 0};
                    for (int e = 0; e < 8; ++e) eq[e] = eq2[e] = game.equip[ch][e];
                    eq2[shop.kind - 1] = rows[size_t(shop.focus)].id;
                    const int lv = std::max(game.level[ch], 1);
                    const auto a = cr2::BattleRules::playerStats(battleData, ch + 1, lv, eq, game.sopiaLevel, game.seed[ch], game.itemCount);
                    const auto b = cr2::BattleRules::playerStats(battleData, ch + 1, lv, eq2, game.sopiaLevel, game.seed[ch], game.itemCount);
                    for (int k = 0; k < 5; ++k) change[k] = b.param[k] - a.param[k];
                    drawStatusB(ch, b.maxHp - a.maxHp, b.maxMp - a.maxMp, t, view, proj);
                    drawCapacity(ch, change, t, view, proj, w, h);
                }
                static const int kTabs[4][3] = {{7, -1, -1}, {1, 4, 6}, {3, 6, 2}, {5, 2, 4}}; // (ShopItem*: SetListTagAnimetionOnly 7)
                const int* tabs = kTabs[std::clamp(shop.kind, 0, 3)];
                listTagExtra[0] = tabs[1], listTagExtra[1] = tabs[2];
                listSwitch = shop.kind > 0; // (ShopWepon*ListMenu: ListSwitchButtonShow - CharaChange)
                drawSkillList(rows, rows[size_t(shop.focus)].id, t, view, w, h, tabs[0], 5); // (Shop*ListMenu::SetListItemParam: cost_id 5 by the price)
                listTagExtra[0] = listTagExtra[1] = -1, listSwitch = false;
            }
            if (const cr2::BattleData::Item* it = shop.stage == 5 ? battleData.item(shop.item) : nullptr)
                drawAsk(formatAnnounce(announce(0x72), {itemName(shop.item)}), uiClock - shop.askAt, view, w, h, 7801, 7802);
        } else if (shop.stage == 4) {
            const cr2::BattleData::Item* it = battleData.item(shop.item);
            UiFig& num = uiFig("shopnum_io");
            num.fig.place = cr2::Mat34();
            poseUi(num, "shopnum_io", t);
            drawFigure(num.fig, view.data());
            float x, y, sx, sy, ex, ey;
            if (uiJoint(num, "call_dotNum_id01", x, y)) drawDigits(shop.count, 70, 2, x, y, proj); // (DotNumId2 set 0x46, the zeros shown)
            for (int k = 1; k <= 2; ++k) {
                char sa[16], ea[16];
                std::snprintf(sa, sizeof sa, "tapAreaS%02d", k), std::snprintf(ea, sizeof ea, "tapAreaE%02d", k);
                if (uiJoint(num, sa, sx, sy) && uiJoint(num, ea, ex, ey)) tapRects.push_back({7100 + k, sx, sy, ex, ey});
            }
            UiFig& plate = uiFig("plicenum_io"); // (the price: count x the item's)
            plate.fig.place = cr2::Mat34();
            poseUi(plate, "plicenum_io", t);
            drawFigure(plate.fig, view.data());
            bool sign = false, digits = false; // (the first of each: Cr3UtilGetCallAnyStringParameter / GetCallDotNumParameter 0)
            for (const std::string& j : plate.joints) {
                if (!sign && j.rfind("call_status_fr", 0) == 0 && uiJoint(plate, j, x, y)) drawStatusSign(std::atoi(j.c_str() + 14), x, y, proj), sign = true;
                if (!digits && j.rfind("call_dotNum_id", 0) == 0 && uiJoint(plate, j, x, y) && it)
                    drawDigits(shop.count * (shop.buy ? it->price : it->sellPrice), 90, 7, x, y, proj), digits = true;
            }
            if (it) { // (Cr3UtilSPrint: the name for %s; DPrint: the count held for %d)
                std::string q = announce(shop.buy ? 0x70 : 0x71);
                if (size_t at = q.find("%s"); at != std::string::npos) q.replace(at, 2, itemName(shop.item));
                if (size_t at = q.find("%d"); at != std::string::npos) q.replace(at, 2, std::to_string(game.itemCount[shop.item]));
                drawAsk(q, t, view, w, h, 7801, 7802);
            }
        }
        buttonLook = keepLook, buttonLevel = keepLevel, buttonFighter = keepFighter;
    }

    // The party (the town's c007: ReserveParty - FldMain step 0xc, FldPartyOrganize;
    // PartyOrganizationMenu): its console (`console_io`: circle buttons 25 Organize, 13 Talk - the
    // town's party_talk -, btlBack - the field); organizing (SetSelectActorScene: `partyBtn` on
    // partyBtn1_io, a circle button for each of the two - its character's frame, + 1, and level -
    // or none, `partyUnder`, the two's portraits - cutIn00N_L01, 5's cutIn006_L02 - on the cut-in
    // layouts 4 and 3, btlBack - the town's party_end); a slot picked (SetHeroSelectScene: partyBtn2_io,
    // its one's button and portrait; SetHeroSelectFaceScene: `faceWin_io` with a `faceBtn` - wearing
    // faceBtn_00N - at call_faceBtnN for each who can join (flags 1011 + the character) and isn't in
    // the two, faceSlct on the focused): a face picks that slot's character (cr3_common_data +3 / +4).
    struct PartyMenu {
        int stage = 0; // 0 shut, 1 the console, 2 the two, 3 a slot's choice
        int slot = 0, focus = 0;
        float at = 0;
    } party;
    std::vector<int> partyChoices() const {
        std::vector<int> c;
        for (int ch = 0; ch < 7; ++ch)
            if (game.flag(1011 + ch) && game.party[0] != ch && game.party[1] != ch) c.push_back(ch);
        return c;
    }
    void partyTap(int id) {
        auto stage = [&](int s) { party.stage = s, party.at = uiClock, party.focus = 0; };
        if (party.stage == 1) {
            if (id == 25) audio.playSe(0xb4, false), stage(2);
            else if (id == 13) audio.playSe(0xb4, false), party.stage = 0, callGimmick("party_talk"); // (FldPartyOrganize 3)
            else if (id == 100) audio.playSe(0xb5, false), party.stage = 0, pendingBranch = true;    // (2: the field)
        } else if (party.stage == 2) {
            if (id == 1 || id == 2) {
                if (id == 1 && game.flag(0x3fa)) return (void)audio.playSe(0xb6, false); // (the first fixed: flag 0x3fa)
                audio.playSe(0xb4, false), party.slot = id - 1, stage(3);
            } else if (id == 100) audio.playSe(0xb5, false), party.stage = 0, callGimmick("party_end"); // (4)
        } else if (party.stage == 3) {
            const std::vector<int> c = partyChoices();
            if (id >= 7000 && id < 7000 + int(c.size())) { // (a face: that one in the slot)
                game.party[party.slot] = c[size_t(id - 7000)];
                audio.playSe(0xb4, false);
                SDL_Log("party: %d, %d", game.party[0], game.party[1]);
                stage(2);
            } else if (id == 100) audio.playSe(0xb5, false), stage(2);
        }
    }
    void partyKey(SDL_Keycode k) {
        if (party.stage == 1) {
            if (k == SDLK_1) partyTap(25);
            else if (k == SDLK_2) partyTap(13);
        } else if (party.stage == 2) {
            if (k == SDLK_1 || k == SDLK_2) partyTap(int(k - SDLK_1) + 1);
        } else if (party.stage == 3) {
            const int n = std::max(int(partyChoices().size()), 1);
            if (k == SDLK_LEFT || k == SDLK_UP) party.focus = (party.focus + n - 1) % n;
            else if (k == SDLK_RIGHT || k == SDLK_DOWN) party.focus = (party.focus + 1) % n;
            else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && !partyChoices().empty()) partyTap(7000 + party.focus);
        }
        if (k == SDLK_BACKSPACE) partyTap(100);
    }
    void drawParty(const Mat4& view, const Mat4& proj, int w, int h) {
        const float t = uiClock - party.at;
        auto keepLook = buttonLook;
        buttonLook = nullptr;
        { // (PartySelectTitleMenu: `menu` on menuPty_io, in once - held after the first stage, as the shop's)
            UiFig& title = uiFig("menu");
            title.fig.place = cr2::Mat34();
            poseUi(title, "menupty_io", t + (party.stage > 1 ? 10.0f : 0.0f));
            drawFigure(title.fig, view.data());
        }
        auto back = [&] {
            UiFig& bk = uiFig("btlback");
            bk.fig.place = cr2::Mat34();
            poseUi(bk, "btlback_in", t);
            drawFigure(bk.fig, view.data());
            float sx, sy, ex, ey;
            if (uiJoint(bk, "tapAreaS01", sx, sy) && uiJoint(bk, "tapAreaE01", ex, ey)) tapRects.push_back({100, sx, sy, ex, ey});
        };
        auto portrait = [&](int layout, int ch) { // (the cut-in windows' portrait of a character)
            if (ch < 0) return;
            ViewFigure& f = cutInFigures[size_t(layout)];
            if (f.meshes.empty()) return;
            char tex[24];
            std::snprintf(tex, sizeof tex, ch == 5 ? "cutIn006_L02" : "cutIn00%d_L01", ch + 1);
            f.place = cr2::Mat34();
            pose(f, cr2::EventRuntime::kCutInSeconds, false);
            cr2::Texture* face = texture(tex);
            for (ViewMesh& vm : f.meshes) vm.texture = face ? face : texture(vm.baseMaterial.diffuse());
            drawFigure(f, view.data());
        };
        auto under = [&] { // (partyUnder: across a wide screen)
            UiFig& u = uiFig("partyunder");
            u.fig.place = cr2::Mat34();
            const float aspect = float(w) / float(std::max(h, 1));
            if (aspect >= 1.5f) u.fig.place.m[0] = aspect / 1.5f;
            else u.fig.place.m[7] = -0.04f;
            poseUi(u, "partyunder", t);
            drawFigure(u.fig, view.data());
        };
        auto slots = [&](const char* anim, bool both) { // (partyBtn: the slots' circle buttons, 1 and 2)
            UiFig& b = uiFig("partybtn");
            b.fig.place = cr2::Mat34();
            poseUi(b, anim, t);
            drawFigure(b.fig, view.data());
            int k = 0;
            for (const std::string& j : b.joints) {
                if (j.rfind("call_circleBtn_fr", 0) != 0) continue;
                if (++k > (both ? 2 : 1)) break;
                const int ch = game.party[both ? k - 1 : party.slot];
                float x, y;
                if (!uiJoint(b, j, x, y)) continue;
                UiFig& c = uiFig("circlebtn_fr");
                c.fig.place = cr2::Mat34();
                c.fig.place.m[3] = x, c.fig.place.m[7] = y;
                poseUi(c, "circlebtn_fr", float(ch >= 0 ? ch + 1 : 0) / 20.0f);
                for (ViewMesh& vm : c.fig.meshes) { // (the first fixed: not selectable)
                    vm.material.params[0x33] = both && k == 1 && game.flag(0x3fa) ? std::vector<float>{0.5f, 0.5f, 0.5f, 1.0f} : vm.baseMaterial.params[0x33];
                    if (vm.material.params[0x33].empty()) vm.material.params.erase(0x33);
                }
                focusLook(c, both ? k : -2);
                drawFigure(c.fig, view.data());
                float lx, ly;
                if (ch >= 0 && uiJoint(c, "call_dotNum_id30", lx, ly)) drawDigits(game.level[ch], 120, 3, lx, ly, proj); // (SetLevelNumbers)
                float sx, sy, ex, ey;
                if (both && uiJoint(c, "tapAreaS01", sx, sy) && uiJoint(c, "tapAreaE01", ex, ey)) tapRects.push_back({k, sx, sy, ex, ey});
            }
        };
        if (party.stage == 1) { // (console_io to half its length, as ShopSecondMenu's: Initialize's ChangeAnimeTime)
            UiFig& con = uiFig("console_io");
            poseUi(con, "console_io", 0);
            drawCommandMenu("console_io", std::min(t, con.fig.animLoaded ? con.fig.anim.duration() * 0.5f : t), view, {}, {}, w, h, 0, "console_io");
            back();
        } else if (party.stage == 2) {
            portrait(4, game.party[0]), portrait(3, game.party[1]);
            under(), slots("partybtn1_io", true), back();
        } else if (party.stage == 3) {
            portrait(4, game.party[party.slot]);
            under(), slots("partybtn2_io", false);
            const std::vector<int> c = partyChoices();
            UiFig& fw = uiFig("facewin_io");
            fw.fig.place = cr2::Mat34();
            poseUi(fw, "facewin_io", t);
            drawFigure(fw.fig, view.data());
            party.focus = std::clamp(party.focus, 0, std::max(int(c.size()) - 1, 0));
            for (size_t k = 0; k < c.size(); ++k) {
                float x, y;
                if (!uiJoint(fw, "call_faceBtn" + std::to_string(k + 1), x, y)) continue;
                UiFig& b = uiFig("facebtn");
                b.fig.place = cr2::Mat34();
                b.fig.place.m[3] = x, b.fig.place.m[7] = y;
                poseUi(b, "facebtn", 0);
                if (cr2::Texture* tex = texture("faceBtn_00" + std::to_string(c[k] + 1)))
                    for (ViewMesh& vm : b.fig.meshes) vm.texture = tex;
                drawFigure(b.fig, view.data());
                float sx, sy, ex, ey;
                if (uiJoint(b, "tapAreaS01", sx, sy) && uiJoint(b, "tapAreaE01", ex, ey)) tapRects.push_back({7000 + int(k), sx, sy, ex, ey});
                if (int(k) == party.focus) {
                    UiFig& sl = uiFig("faceslct");
                    sl.fig.place = cr2::Mat34();
                    sl.fig.place.m[3] = x, sl.fig.place.m[7] = y;
                    poseUi(sl, "faceslct", t, true);
                    drawFigure(sl.fig, view.data());
                }
            }
            UiFig& help = uiFig("camphelp_io"); // (SetHelpWindow: CampHelpText, "Please select a character to swap." - announce 0x86)
            help.fig.place = cr2::Mat34();
            poseUi(help, "camphelp_io", t);
            drawFigure(help.fig, view.data());
            float x, y;
            if (uiJoint(help, "typeM_L01", x, y)) drawBorderedText(announce(0x86), 24, x, y, w, h, true);
            back();
        }
        buttonLook = keepLook;
    }

    void drawFieldUi(int w, int h) {
        if (!fieldMode || battleMode || eventMode) return;
        const bool tabShown = fieldTabShown();
        if (!campOpen && !tabShown && !mapOpen && !saveStage && !world.stage && !shop.stage && !party.stage && library.stage != 1 &&
            !pub.stage)
            return;
        const Mat4 proj = uiProjection(w, h);
        Mat4 camera = cr2::mat4Identity();
        camera[14] = -0.881f;
        const Mat4 view = cr2::mat4Multiply(proj, camera);
        cr2::SceneLighting l;
        dev->setLighting(l);
        dev->clearDepth();
        if (campOpen) drawCamp(view, proj, w, h);
        else if (mapOpen) tapRects.clear(), drawDungeonMap(view);
        else if (saveStage) tapRects.clear(), drawSave(view, proj, w, h);
        else if (world.stage) tapRects.clear(), drawWorldSelect(view, w, h);
        else if (library.stage == 1) tapRects.clear(), drawLibrary(view, w, h);
        else if (shop.stage) tapRects.clear(), drawShop(view, proj, w, h);
        else if (pub.stage) tapRects.clear(), drawPub(view, w, h);
        else if (party.stage) tapRects.clear(), drawParty(view, proj, w, h);
        else {
            tapRects.clear();
            if (game.tabEnable) drawFieldTab(view); // (Cr3TabMenu.SetEnable)
            drawChangeButton(view);
            drawWalkStick(view);
        }
        // the battery (BatteryWindowMenu: with the camp, the shop, the pub - Set*Parameter's
        // SetBatteryWindowParameter -, and in the field once its level is under 20%, looked at
        // every 10 s - InterfaceMain::Update)
        if (campOpen || shop.stage || pub.stage) drawBattery(view);
        else if (tabShown && batteryLow) drawBattery(view);
    }
    // battery_id at a frame by the device's power (BatteryWindowMenu::Update, every 20 frames:
    // charging - 0.35 full, 0.3 under 30%, 0.25 under 60%, else 0.2; charged 0.2; discharging 0.15
    // under 30%, 0.1 under 60%, else 0.05; unknown 0) - SDL's power info; a machine without a
    // battery as charged
    bool batteryLow = false;
    float batteryCheckedAt = -100, batteryFrameAt = -100, batteryFrame = 0.2f;
    void updateBattery() {
        if (uiClock - batteryFrameAt < 1.0f) return;
        batteryFrameAt = uiClock;
        int pct = -1;
        const SDL_PowerState st = SDL_GetPowerInfo(nullptr, &pct);
        const float level = pct < 0 ? 1.0f : float(pct) / 100.0f;
        if (st == SDL_POWERSTATE_CHARGING) batteryFrame = level >= 1.0f ? 0.35f : level < 0.3f ? 0.3f : level < 0.6f ? 0.25f : 0.2f;
        else if (st == SDL_POWERSTATE_CHARGED || st == SDL_POWERSTATE_NO_BATTERY) batteryFrame = 0.2f;
        else if (st == SDL_POWERSTATE_ON_BATTERY) batteryFrame = level < 0.3f ? 0.15f : level < 0.6f ? 0.1f : 0.05f;
        else batteryFrame = 0;
        if (uiClock - batteryCheckedAt >= 10.0f) { // (InterfaceMain::Update: each 10 s)
            batteryCheckedAt = uiClock;
            batteryLow = st == SDL_POWERSTATE_ON_BATTERY && level < 0.2f;
        }
    }
    void drawBattery(const Mat4& view) {
        updateBattery();
        UiFig& b = uiFig("battery_id");
        b.fig.place = cr2::Mat34();
        poseUi(b, "battery_id", batteryFrame);
        drawFigure(b.fig, view.data());
    }

    void drawBalloons(int w, int h) {
        if (!fieldMode || eventMode || !script || !script->boxVisible()) return;
        drawFlagMarks(w, h);
        if (balloons.empty()) {
            attentionShown = false; // (EndCheckArea)
            return;
        }
        if (!balloonLoaded) {
            balloonLoaded = true;
            std::string err;
            const float keep = time;
            if (!loadFigure("balloon.geom", balloonFigure, &err)) SDL_Log("balloon: %s", err.c_str());
            time = keep;
        }
        if (balloonFigure.meshes.empty()) return;
        const float k = std::min(float(w) / 0.96f, float(h) / 0.64f);
        const Mat4 proj = uiProjection(w, h);
        Mat4 camera = cr2::mat4Identity();
        camera[14] = -0.881f;
        const Mat4 view = cr2::mat4Multiply(proj, camera);
        cr2::SceneLighting l;
        dev->setLighting(l);
        for (size_t i : balloons) {
            const cr2::Gimmick& g = field.gimmicks()[i];
            const float p[3] = {g.position[0], g.position[1] + g.balloonHeight(), g.position[2]};
            const float* m = fieldScene.data();
            const float cw = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
            if (cw <= 0) continue;
            const float nx = (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]) / cw;
            const float ny = (m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]) / cw;
            balloonFigure.place = cr2::Mat34();
            balloonFigure.place.m[3] = nx * float(w) * 0.5f / k, balloonFigure.place.m[7] = ny * float(h) * 0.5f / k;
            pose(balloonFigure, time);
            drawFigure(balloonFigure, view.data());
        }
        // the attention mark (FldMain::SetAttentionMarkVisible: while any balloon is up -
        // InterfaceMain::SetCheckAreaParameter, CheckAreaExclamationMenu's `checkArea`, looping)
        if (!attentionShown) attentionShown = true, attentionAt = uiClock;
        UiFig& mark = uiFig("checkarea");
        mark.fig.place = cr2::Mat34();
        poseUi(mark, "checkarea", uiClock - attentionAt, true);
        drawFigure(mark.fig, view.data());
    }
    bool attentionShown = false;
    float attentionAt = 0;

    // the flag marks (FldFlagMark::Update: the `flag` figure at a flagged gimmick's balloon place,
    // FldUtilGetBalloonPosOffset - hidden while it has a balloon: CheckEntryFlagName)
    void drawFlagMarks(int w, int h) {
        if (script->flagMarks().empty() || script->running()) return;
        const float k = std::min(float(w) / 0.96f, float(h) / 0.64f);
        Mat4 camera = cr2::mat4Identity();
        camera[14] = -0.881f;
        const Mat4 view = cr2::mat4Multiply(uiProjection(w, h), camera);
        cr2::SceneLighting l;
        dev->setLighting(l);
        const std::vector<cr2::Gimmick>& gs = field.gimmicks();
        for (size_t i = 0; i < gs.size(); ++i) {
            const cr2::Gimmick& g = gs[i];
            if (!script->flagMarks().count(g.id) || !script->visible(g) || std::find(balloons.begin(), balloons.end(), i) != balloons.end()) continue;
            const float p[3] = {g.position[0], g.position[1] + g.balloonHeight(), g.position[2]};
            const float* m = fieldScene.data();
            const float cw = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
            if (cw <= 0) continue;
            UiFig& f = uiFig("flag");
            f.fig.place = cr2::Mat34();
            f.fig.place.m[3] = (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]) / cw * float(w) * 0.5f / k;
            f.fig.place.m[7] = (m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]) / cw * float(h) * 0.5f / k;
            poseUi(f, "flag", time, true);
            drawFigure(f.fig, view.data());
        }
    }

    // the event's UI, over the resolved scene: the fade, then the windows over it (their depths
    // in front of the fade windows')
    // the dungeon's name as it is entered (EvtIntroduction: MapTitleMenu - the language's mapName
    // figure on its mapNameDNN animator, played once)
    void drawIntro(int w, int h) {
        if (!fieldMode || !script || script->introDungeon() <= 0) return;
        static const char* const kFigures[8] = {"mapname", "mapname", "mapnamefre", "mapnameger", "mapnamespa", "mapnamechs", "mapnamecht", "mapnamekor"};
        const std::string figure = kFigures[std::clamp(optionLanguage, 0, 7)];
        char anim[32];
        std::snprintf(anim, sizeof anim, "%sd%02d", figure.c_str(), script->introDungeon());
        UiFig& u = uiFig(figure);
        u.fig.place = cr2::Mat34();
        poseUi(u, anim, float(script->ticks() - script->introTick()) * cr2::EventRuntime::kTick + fieldClock);
        if (u.fig.animLoaded) script->introSeconds = u.fig.anim.duration();
        Mat4 camera = cr2::mat4Identity();
        camera[14] = -0.881f;
        cr2::SceneLighting l;
        dev->setLighting(l);
        drawFigure(u.fig, cr2::mat4Multiply(uiProjection(w, h), camera).data());
    }
    // The vista's slash (VistaEvent.ExecutionStart / ExecutionEndStart - EvtUtilVistaExecution*:
    // InterfaceMain::SetEnforecementEffectParameter 0 / 1, FieldGimickEffect07Menu, interface 0x45
    // / 0x7b): `kill` (`finale`) - kill_io in, kill_wait looping - and redCsr_loop (blueCsr_loop)
    // at its place. A swipe through it (TouchSimpleRelease: the place's projection on the press ..
    // release line within it, and the slide area): `execution` (`catastrophe`) once, SE 0x15c,
    // then kill_io backwards and CheckExecution true (EndCheckEnforecementEffectWindow 1); a swipe
    // past it: kill_io backwards, then the whole again (2). `--input slash` / `slashfail`
    struct VistaSlash {
        int stage = 0; // 0 none, 1 in / waiting, 2 the hit's figure, 3 out
        bool end = false, hit = false, pressed = false;
        float at = 0, x0 = 0, y0 = 0;
    } vslash;
    void updateVistaSlash() {
        cr2::EventRuntime* ev = activeEvents();
        const bool awaited = ev && ev->awaitingSlash();
        if (!awaited) {
            vslash = VistaSlash();
            return;
        }
        if (vslash.stage == 0) {
            vslash = VistaSlash(), vslash.stage = 1, vslash.end = ev->finishingSlash(), vslash.at = uiClock;
            SDL_Log("vista: the slash awaited%s (%.1f s)", vslash.end ? " (the finale)" : "", uiClock);
        }
        if (vslash.stage == 1 && uiClock - vslash.at > 0.5f && !inputScript.empty() && (inputScript.front() == "slash" || inputScript.front() == "slashfail")) {
            const bool hit = inputScript.front() == "slash";
            inputScript.pop_front();
            vslashEnd(hit);
        }
        const float t = uiClock - vslash.at;
        if (vslash.stage == 2 && t > uiDuration(vslash.end ? "catastrophe" : "execution")) vslash.stage = 3, vslash.at = uiClock;
        else if (vslash.stage == 3 && t > uiDuration(vslash.end ? "finale_io" : "kill_io", vslash.end ? "finale" : "kill")) {
            if (vslash.hit) {
                SDL_Log("vista: the slash done");
                ev->advance(); // (CheckExecution: true)
                vslash = VistaSlash();
            } else {
                vslash = VistaSlash(), vslash.stage = 1, vslash.end = ev->finishingSlash(), vslash.at = uiClock; // (SetEnforecementEffectParameter again)
            }
        }
    }
    void vslashEnd(bool hit) {
        if (vslash.stage != 1) return;
        vslash.hit = hit, vslash.at = uiClock;
        if (hit) audio.playSe(0x15c, false), vslash.stage = 2;
        else vslash.stage = 3;
        SDL_Log("vista: the slash %s (%.1f s)", hit ? "lands" : "misses", uiClock);
    }
    // the mark's place (its figure's own, the interface's units)
    bool vslashPlace(float& x, float& y) {
        UiFig& f = uiFig(vslash.end ? "finale" : "kill");
        return uiJoint(f, "tapAreaS01", x, y) ? true : (x = 0, y = 0, true);
    }
    void vslashPointer(float fx, float fy, int phase) { // (0 pressed, 2 let go)
        if (vslash.stage != 1) return;
        const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
        const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
        if (phase == 0) {
            vslash.pressed = true, vslash.x0 = x, vslash.y0 = y;
            return;
        }
        if (!vslash.pressed) return;
        vslash.pressed = false;
        float mx = 0, my = 0;
        vslashPlace(mx, my);
        const float dx = x - vslash.x0, dy = y - vslash.y0, l2 = dx * dx + dy * dy;
        if (l2 <= 0) return; // (a tap: nothing)
        const float u = ((mx - vslash.x0) * dx + (my - vslash.y0) * dy) / l2;
        vslashEnd(u >= 0 && u <= 1 && std::hypot(vslash.x0 + u * dx - mx, vslash.y0 + u * dy - my) * 2048.0f < 192.0f);
    }
    float uiDuration(const std::string& anim, const std::string& fig = "") {
        UiFig& f = uiFig(fig.empty() ? anim : fig);
        poseUi(f, anim, 0);
        return f.fig.animLoaded ? f.fig.anim.duration() : 0.5f;
    }
    void drawVistaSlash() {
        if (vslash.stage == 0) return;
        Mat4 camera = cr2::mat4Identity();
        camera[14] = -0.881f;
        const Mat4 view = cr2::mat4Multiply(uiProjection(screenW, screenH), camera);
        cr2::SceneLighting l;
        dev->setLighting(l);
        const std::string name = vslash.end ? "finale" : "kill";
        UiFig& f = uiFig(name);
        f.fig.place = cr2::Mat34();
        const float t = uiClock - vslash.at, in = uiDuration(name + "_io", name);
        if (vslash.stage == 1) {
            if (t < in) poseUi(f, name + "_io", t);
            else poseUi(f, name + "_wait", t - in, true);
        } else if (vslash.stage == 2) {
            poseUi(f, name + "_wait", t, true);
        } else {
            poseUi(f, name + "_io", std::max(in - t, 0.0f)); // (+0x5c 2: backwards)
        }
        drawFigure(f.fig, view.data());
        float mx = 0, my = 0;
        vslashPlace(mx, my);
        if (vslash.stage == 1) { // (the cursor)
            const std::string cn = vslash.end ? "bluecsr_loop" : "redcsr_loop";
            UiFig& c = uiFig(cn);
            c.fig.place = cr2::Mat34();
            poseUi(c, cn, uiClock, true);
            drawFigure(c.fig, view.data());
        } else if (vslash.stage == 2) { // (the hit's)
            const std::string hn = vslash.end ? "catastrophe" : "execution";
            UiFig& h = uiFig(hn);
            h.fig.place = cr2::Mat34();
            poseUi(h, hn, t);
            drawFigure(h.fig, view.data());
        }
    }
    // Cr3Fade's type 3 (FadeWindowMenu 0x14): not a fade but the encounter's wipe - `encount`, its
    // streaked edge (hud_encount) on a panel 2.048 wide, encount_in sliding it over the screen from
    // the right (a FadeOut: the field's encounter, Cr3FadeOut(3, 20)), encount_out on off to the
    // left (a FadeIn: every battle's start, BtlMainCtrl::StepBattleIn's FadeIn(3, 20)); `p` 0..1
    // (its animators at 10 / frames speed - FadeWindowMenu::SetParamNumber 3 -: 0.25 s at 0.5, the
    // reference's half a second)
    static constexpr float kWipeSeconds = 0.5f;
    float battleWipeAt = -1;
    void drawEncountWipe(float p, bool cover) {
        Mat4 camera = cr2::mat4Identity();
        camera[14] = -0.881f;
        const Mat4 view = cr2::mat4Multiply(uiProjection(screenW, screenH), camera);
        UiFig& u = uiFig("encount");
        const char* anim = cover ? "encount_in" : "encount_out";
        poseUi(u, anim, 0);
        const float d = u.fig.animLoaded ? u.fig.anim.duration() : 0.25f;
        u.fig.place = cr2::Mat34();
        poseUi(u, anim, std::clamp(p, 0.0f, 1.0f) * d);
        dev->clearDepth();
        drawFigure(u.fig, view.data());
    }
    void drawEventUi() {
        dev->clearDepth(); // (the scene's depths behind all of it: a near camera's ground too)
        if (credits.stage) return drawCredits();
        if (titleMenu.stage) drawTitle();
        else drawIntro(screenW, screenH);
        drawBalloons(screenW, screenH);
        drawKeyMark(screenW, screenH);
        drawFieldUi(screenW, screenH);
        drawBattleHud(screenW, screenH);
        drawVistaSlash();
        if (battleMode && white.id) { // (a special scene's: Cr3Fade over the battle)
            const float alpha = battleFade.alpha.at(battleClock);
            if (alpha > 0) {
                static const float kScreen[24] = {-1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1, -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
                const Mat4 id = cr2::mat4Identity();
                const float* c = fadeColours[std::clamp(battleFade.colour, 0, 3)];
                const float rgba[4] = {c[0], c[1], c[2], alpha};
                dev->drawQuad(white, kScreen, id.data(), rgba);
            }
        }
        if (fieldMode && !battleMode && encount.fadeAt >= 0) drawEncountWipe((uiClock - encount.fadeAt) / kWipeSeconds, true); // (an encounter's: Cr3FadeOut(3, 20))
        if (battleMode && battleWipeAt >= 0 && battleClock - battleWipeAt < kWipeSeconds) drawEncountWipe((battleClock - battleWipeAt) / kWipeSeconds, false);
        cr2::EventRuntime* ev = activeEvents();
        if (!ev || !eventDrawingReady) return;
        struct YesNoLast { // (the buttons over everything else the event shows)
            Viewer* v; cr2::EventRuntime* ev;
            ~YesNoLast() { v->dev->clearDepth(), v->drawYesNo(*ev, v->screenW, v->screenH); }
        } yesNoLast{this, ev};
        if (ev->effectLine) { // (BattleEffectTypeBMenu: radiate_loop, scaled aspect / 1.5 - or 1.5 / aspect - across)
            Mat4 camera = cr2::mat4Identity();
            camera[14] = -0.881f;
            const Mat4 view = cr2::mat4Multiply(uiProjection(screenW, screenH), camera);
            UiFig& rl = uiFig("radiate_loop");
            const float aspect = float(screenW) / float(std::max(screenH, 1));
            const float k = aspect > 1.5f ? aspect / 1.5f : 1.5f / aspect;
            rl.fig.place = cr2::Mat34();
            rl.fig.place.m[0] = rl.fig.place.m[5] = k;
            poseUi(rl, "radiate_loop", uiClock, true);
            drawFigure(rl.fig, view.data());
        }
        const cr2::EventFade& fd = ev->fade();
        const float alpha = fd.at(eventLead() / cr2::EventRuntime::kTick);
        if (alpha > 0 && white.id) {
            static const float kScreen[24] = {-1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1, -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
            const Mat4 id = cr2::mat4Identity();
            const float* c = fadeColours[std::clamp(fd.colour, 0, 3)];
            const float rgba[4] = {c[0], c[1], c[2], alpha};
            dev->drawQuad(white, kScreen, id.data(), rgba);
        }
        const bool message = ev->messageShown() && ev->page() < int(ev->message().pages.size());
        if (ev->windowOpen(cr2::EventRuntime::Vista))
            drawWindow(vistaWindow, message && ev->messageWindow() == cr2::EventRuntime::Vista, screenW, screenH);
        drawCutIns(screenW, screenH);
        if (ev->windowOpen(cr2::EventRuntime::CutIn) || ev->windowClosing(cr2::EventRuntime::CutIn))
            drawWindow(conversationWindow, message && ev->messageWindow() == cr2::EventRuntime::CutIn, screenW, screenH);
        if (ev->windowOpen(cr2::EventRuntime::Narration))
            drawWindow(narrationWindow, message && ev->messageWindow() == cr2::EventRuntime::Narration, screenW, screenH);
        // the tutorials': the picture (tutorialPic, its PRG_rep the image shown), the windows over it
        const cr2::EventRuntime::TutorialPicture& tp = ev->tutorialPicture();
        if (tp.visible && tp.shown >= 0 && tp.shown < int(tp.images.size())) {
            UiFig& pic = uiFig("tutorialpic");
            pic.fig.place = cr2::Mat34();
            poseUi(pic, "tutorialpic", 0);
            if (cr2::Texture* tex = texture(tp.images[size_t(tp.shown)]))
                for (ViewMesh& vm : pic.fig.meshes) vm.texture = tex;
            Mat4 camera = cr2::mat4Identity();
            camera[14] = -0.881f;
            const Mat4 view = cr2::mat4Multiply(uiProjection(screenW, screenH), camera);
            drawFigure(pic.fig, view.data());
            dev->clearDepth();
        }
        for (WindowLayout* wl : {&annoWindow, &announceWindow, &helpWindow, &systemWindow})
            if (ev->windowOpen(wl->window) || ev->windowClosing(wl->window))
                drawWindow(*wl, message && ev->messageWindow() == wl->window, screenW, screenH);
        // the fast-forward button (FastForward: speedUp, its loop, at its own place - over the
        // message's window while allowed); held, the pages race by (fastHeld)
        fastRect.id = 0;
        if ((message && ev->fastForwardShown() && ev->windowOpen(ev->messageWindow())) || ev->skipButtonShown()) { // (or the vista's skip button: the same figure)
            Mat4 camera = cr2::mat4Identity();
            camera[14] = -0.881f;
            const Mat4 view = cr2::mat4Multiply(uiProjection(screenW, screenH), camera);
            UiFig& ff = uiFig("speedup");
            ff.fig.place = cr2::Mat34();
            poseUi(ff, "speedup", uiClock, true);
            drawFigure(ff.fig, view.data());
            float sx, sy, ex, ey;
            if (uiJoint(ff, "tapAreaS01", sx, sy) && uiJoint(ff, "tapAreaE01", ex, ey)) fastRect.id = 1, fastRect.x0 = sx, fastRect.y0 = sy, fastRect.x1 = ex, fastRect.y1 = ey;
        }
        if (ev->windowOpen(7) || ev->windowClosing(7)) {
            drawWindow(itemWindow, message && ev->messageWindow() == 7, screenW, screenH);
            const cr2::BattleData::Item* it = ev->windowOpen(7) && ev->itemShown() > 0 ? battleData.item(ev->itemShown()) : nullptr;
            float x, y;
            if (it && it->icon > 0 && windowJoint(itemWindow, "call_mainIcon_id00", x, y)) { // (GetItemWindowMenu::SetItemIcon)
                Mat4 camera = cr2::mat4Identity();
                camera[14] = -0.881f;
                const Mat4 view = cr2::mat4Multiply(uiProjection(screenW, screenH), camera);
                UiFig& ic = uiFig("mainicon_id");
                ic.fig.place = cr2::Mat34();
                // (left of the centred text: the joint less half its width, less 0.032)
                const float textW = !textQuads.empty() && shownText.compare(0, 2, "7\n") == 0 ? textQuads[0].w : 0;
                ic.fig.place.m[3] = x - (textW / 2 + 0.032f), ic.fig.place.m[7] = y;
                poseUi(ic, "mainicon_id", it->icon == 0x1c ? 1.45f : it->icon == 0x1d ? 1.4f : float(it->icon) / 20.0f);
                drawFigure(ic.fig, view.data());
            }
        }
    }
};

bool saveBmp(const char* path, const std::vector<uint8_t>& rgba, int w, int h) {
    SDL_Surface* s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ABGR8888);
    if (!s) return false;
    for (int y = 0; y < h; ++y) std::memcpy(static_cast<uint8_t*>(s->pixels) + size_t(y) * s->pitch, &rgba[size_t(y) * w * 4], size_t(w) * 4);
    const bool ok = SDL_SaveBMP(s, path);
    SDL_DestroySurface(s);
    return ok;
}

std::string upscaleName(const cr2::UpscaleSettings& u) {
    if (u.mode == cr2::Upscaler::Off) return "off";
    std::string q = "x" + std::to_string(u.ratio);
    for (const Quality& k : kQualities)
        if (k.ratio == u.ratio) q = k.name;
    return std::string(u.mode == cr2::Upscaler::Fsr3 ? "FSR 3.1 " : u.mode == cr2::Upscaler::MetalFx ? "MetalFX " : "DLSS ") + q;
}

} // namespace

// Where a copy of the game is looked for without --data: beside the executable (Android: the app's
// folder on the shared storage, reachable over USB - Android/data/local.preservation.chaosrings2/
// files/), beside an AppImage or a macOS .app (their base paths are inside them), the working directory
std::vector<std::string> dataPlaces() {
    std::vector<std::string> out;
    auto add = [&](std::string p) {
        if (p.empty()) return;
        if (p.back() != '/' && p.back() != '\\') p += '/';
        if (std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
    };
#ifdef SDL_PLATFORM_ANDROID
    if (const char* ext = SDL_GetAndroidExternalStoragePath()) add(ext);
#else
    const char* basePath = SDL_GetBasePath();
    const std::string base = basePath ? basePath : "";
#ifdef SDL_PLATFORM_LINUX
    if (const char* image = SDL_getenv("APPIMAGE")) add(std::string(image).substr(0, std::string(image).rfind('/') + 1));
#endif
#ifdef SDL_PLATFORM_MACOS
    if (const size_t app = base.rfind(".app/"); app != std::string::npos) add(base.substr(0, base.rfind('/', app) + 1));
#endif
    add(base);
    if (char* cwd = SDL_GetCurrentDirectory()) add(cwd), SDL_free(cwd);
    if (char* pref = SDL_GetPrefPath("DuIslingr", "CR2-U")) add(pref), SDL_free(pref); // (the profile folder: an .ipa the first start unpacked there)
#endif
    return out;
}
// settings.ini's data= set (the setup's), its other lines kept
void storeData(const std::string& path) {
    char* dir = SDL_GetPrefPath("DuIslingr", "CR2-U");
    const std::string file = (dir ? std::string(dir) : std::string()) + "settings.ini";
    SDL_free(dir);
    size_t size = 0;
    char* text = static_cast<char*>(SDL_LoadFile(file.c_str(), &size));
    std::string all = text ? std::string(text, size) : std::string(), out;
    SDL_free(text);
    for (size_t at = 0; at < all.size();) {
        const size_t e = std::min(all.find('\n', at), all.size());
        if (all.compare(at, 5, "data=") != 0) out += all.substr(at, e - at) + "\n";
        at = e + 1;
    }
    out += "data=" + path + "\n";
    SDL_SaveFile(file.c_str(), out.data(), out.size());
}
// settings.ini's data= - a copy the first start's setup was given (an Android or Vita copy, read where it is)
std::string storedData() {
    std::string out;
    char* dir = SDL_GetPrefPath("DuIslingr", "CR2-U");
    const std::string path = (dir ? std::string(dir) : std::string()) + "settings.ini";
    SDL_free(dir);
    size_t size = 0;
    if (char* text = static_cast<char*>(SDL_LoadFile(path.c_str(), &size))) {
        const std::string all(text, size);
        SDL_free(text);
        for (size_t at = 0; at < all.size();) {
            const size_t e = std::min(all.find('\n', at), all.size());
            if (all.compare(at, 5, "data=") == 0) out = all.substr(at + 5, e - at - 5);
            at = e + 1;
        }
    }
    while (!out.empty() && (out.back() == '\r' || out.back() == ' ')) out.pop_back();
    return out;
}

int main(int argc, char** argv) {
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    std::string data, model, capture, animName, renderer = "vulkan", lights = "d0101c", fieldName, at = "defaultPos", binary;
    float walkX = 0, walkY = 0, walkSeconds = 0; // --walk: the field's character runs first (captures)
    float drag[5] = {0, 0, 0, 0, 0};               // --drag x0,y0,x1,y1,s: then a finger's drag that long (the screen's fractions; held for the capture)
    bool tapAfter = false, mute = false, forceAudio = false;
    int hdArg = -1;        // --hd-textures / --no-hd-textures (-1: the settings')
    int speedArg[2] = {0, 0}; // --field-speed / --battle-speed n (0: the settings')
    int smoothArg = -1;       // --smooth-models 0 | 2 | 3 (-1: the settings')
    bool hdModelsArg = false; // --hd-models (on whatever the settings say)
    std::string exploreDir, choicesArg; // --explore <folder> [--choices yes,no,...] [--hours h]: the game played by itself
    float exploreHours = 12;
    std::string exportDir; // --export-textures [dir]: every image as <hash>.crraw, for tools/hd_textures.py (textures/export/ in the game folder without one)
    bool exportTextures = false;
    bool exportModels = false; // --export-models [dir]: every figure as <figure>.glb (models/export/ in the game folder without one)
    std::string exportModelsDir;
    bool exportAnimations = true; // --no-animations: --export-models without them (they are for previewing; a character's run to hundreds of MB)
    std::string eventName, vista = "null", fontPath, playFrom, callEntry = "evt000";
    int creditsArg = -1;
    int fightMode = 0;
    std::string inputArg; // (--input: the commands chosen, as tokens)
    std::vector<int> flagsArg;
    bool continueSave = false;
    std::string gameSavePath;
    bool titleScreen = false; // (--title: the game's title screen first)
    std::vector<std::pair<int, int>> itemArg;
    std::vector<std::tuple<int, int, int>> equipArg;
    int chargeArg = -1, difficultyArg = -1, membersArg[2] = {-2, -2}, ozArg = -1, gaugeArg = 0;
    std::vector<std::string> workArgs;
    std::vector<std::pair<int, int>> stateArg;
    std::vector<std::pair<int, int>> sopiaArg;
    bool skipBattlesOpt = false;
    uint32_t fightSeed = 1;
    int battleId = 0, battleParty[2] = {1, 2}; // (--battle: Darwin and Orlando unless --party)
    float page = 3.0f;
    int width = 0, height = 0;    // the window (--size; else the resolution's, fitted to the display)
    int resW = 0, resH = 0;       // --resolution: the output frames (else the window's)
    std::string upscaler = "off", quality = "quality";
    float sharpness = 0.3f;
    float runSeconds = 0; // --run: a capture plays this long first, 60 frames a second
    int fps = 60;         // --fps: the frame rate the window aims at (0: as fast as the display shows them)
    float yaw = 30, pitch = 15, startTime = 0;
    // no arguments (a double-click, Android's launcher): the game, from its title screen
    if (argc <= 1) titleScreen = true;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const char* next = i + 1 < argc ? argv[i + 1] : nullptr;
        if (a == "--data" && next) data = next, ++i;
        else if (a == "--model" && next) model = next, ++i;
        else if (a == "--capture" && next) capture = next, ++i;
        else if (a == "--anim" && next) animName = next, ++i;
        else if (a == "--time" && next) startTime = float(std::atof(next)), ++i;
        else if (a == "--size" && next) std::sscanf(next, "%dx%d", &width, &height), ++i;
        else if (a == "--yaw" && next) yaw = float(std::atof(next)), ++i;
        else if (a == "--pitch" && next) pitch = float(std::atof(next)), ++i;
        else if (a == "--renderer" && next) renderer = next, ++i;
        else if (a == "--lights" && next) lights = next, ++i;
        else if (a == "--field" && next) fieldName = next, ++i;
        else if (a == "--at" && next) at = next, ++i;
        else if (a == "--binary" && next) binary = next, ++i;
        else if (a == "--walk" && next) std::sscanf(next, "%f,%f,%f", &walkX, &walkY, &walkSeconds), ++i;
        else if (a == "--drag" && next) std::sscanf(next, "%f,%f,%f,%f,%f", &drag[0], &drag[1], &drag[2], &drag[3], &drag[4]), ++i;
        else if (a == "--event" && next) eventName = next, ++i;
        else if (a == "--battle" && next) battleId = std::atoi(next), ++i;
        else if (a == "--skip-battles") skipBattlesOpt = true;
        else if (a == "--fight" && next) fightMode = !std::strcmp(next, "pair") ? 2 : !std::strcmp(next, "manual") ? 3 : 1, ++i;
        else if (a == "--input" && next) inputArg = next, ++i;
        else if (a == "--flag" && next) flagsArg.push_back(std::atoi(next)), ++i; // (a story flag set to begin with: tests)
        else if (a == "--sopia" && next) { // (a sopia's level: id:level, tests)
            int id = 0, level = 0;
            if (std::sscanf(next, "%d:%d", &id, &level) == 2) sopiaArg.push_back({id, level});
            ++i;
        }
        else if (a == "--seed" && next) fightSeed = uint32_t(std::atoi(next)), ++i;
        else if (a == "--state" && next) { // (a state a fighter has to begin with: fighter:state, tests)
            int f = 0, id = 0;
            if (std::sscanf(next, "%d:%d", &f, &id) == 2) stateArg.push_back({f, id});
            ++i;
        }
        else if (a == "--charge" && next) chargeArg = std::atoi(next), ++i; // (the charge gauge to begin with: tests)
        else if (a == "--gauge" && next) gaugeArg = std::atoi(next), ++i; // (the break gauge to begin with, -4..4: tests)
        else if (a == "--difficulty" && next) difficultyArg = std::atoi(next), ++i; // (the world select's level - 1: tests)
        else if (a == "--oz" && next) ozArg = std::atoi(next), ++i; // (the Oz to begin with: tests)
        else if (a == "--work" && next) workArgs.push_back(next), ++i; // (n:v - Cr3UtilGlobalWork n to begin with: tests)
        else if (a == "--members" && next) std::sscanf(next, "%d,%d", &membersArg[0], &membersArg[1]), ++i; // (the field's two, from 0: tests)
        else if (a == "--item" && next) { // (an item's count to begin with: id:count, tests)
            int id = 0, n = 0;
            if (std::sscanf(next, "%d:%d", &id, &n) == 2) itemArg.push_back({id, n});
            ++i;
        }
        else if (a == "--equip" && next) { // (a character's equipment slot: character:slot:id - 3..7 its sopia; tests)
            int c = 0, slot = 0, item = 0;
            if (std::sscanf(next, "%d:%d:%d", &c, &slot, &item) == 3) equipArg.push_back({c, slot, item});
            ++i;
        }
        else if (a == "--party" && next) std::sscanf(next, "%d,%d", &battleParty[0], &battleParty[1]), ++i;
        else if (a == "--vista" && next) vista = next, ++i;
        else if (a == "--page" && next) page = float(std::atof(next)), ++i;
        else if (a == "--font" && next) fontPath = next, ++i;
        else if (a == "--tap") tapAfter = true;
        else if (a == "--mute") mute = true;
        else if (a == "--hd-textures") hdArg = 1;
        else if (a == "--no-hd-textures") hdArg = 0;
        else if (a == "--field-speed" && next) speedArg[0] = std::clamp(std::atoi(next), 1, 5), ++i;
        else if (a == "--smooth-models" && next) smoothArg = std::atoi(next) == 2 || std::atoi(next) == 3 ? std::atoi(next) : 0, ++i;
        else if (a == "--hd-models") hdModelsArg = true;
        else if (a == "--battle-speed" && next) speedArg[1] = std::clamp(std::atoi(next), 1, 5), ++i;
        else if (a == "--export-textures") {
            exportTextures = true;
            if (next && next[0] != '-') exportDir = next, ++i;
        }
        else if (a == "--export-models") {
            exportModels = true;
            if (next && next[0] != '-') exportModelsDir = next, ++i;
        }
        else if (a == "--no-animations") exportAnimations = false;
        else if (a == "--explore" && next) exploreDir = next, ++i;
        else if (a == "--choices" && next) choicesArg = next, ++i;
        else if (a == "--hours" && next) exploreHours = float(std::atof(next)), ++i;
        else if (a == "--audio") forceAudio = true; // (the device even under --capture: tests, SDL_AUDIO_DRIVER=dummy)
        else if (a == "--call" && next) callEntry = argv[++i]; // (--play: the map's event function, not its evt000)
        else if (a == "--credits" && next) creditsArg = std::atoi(argv[++i]); // (the staff roll n at once: --play's, for tests)
        else if (a == "--play") playFrom = next && next[0] != '-' ? (++i, next) : "d1020";
        else if (a == "--continue") continueSave = true; // (--play from the save the field's Save wrote)
        else if (a == "--game-save" && next) gameSavePath = next, ++i; // (--play from the game's own game.dat / cont.dat)
        else if (a == "--title") titleScreen = true;
        else if (a == "--resolution" && next) std::sscanf(next, "%dx%d", &resW, &resH), ++i;
        else if (a == "--upscaler" && next) upscaler = next, ++i;
        else if (a == "--quality" && next) quality = next, ++i;
        else if (a == "--fps" && next) fps = std::clamp(std::atoi(next), 0, 1000), ++i;
        else if (a == "--run" && next) runSeconds = float(std::atof(next)), ++i;
        else if (a == "--sharpness" && next) sharpness = std::clamp(float(std::atof(next)), 0.0f, 1.0f), ++i;
    }
    if (resW || resH) {
        if (resW < 320 || resH < 180 || resW > kMaxResW || resH > kMaxResH)
            return std::fprintf(stderr, "--resolution: 320x180 up to %dx%d\n", kMaxResW, kMaxResH), 2;
    }
    cr2::UpscaleSettings upscale;
    upscale.sharpness = sharpness;
    if (upscaler == "fsr3" || upscaler == "fsr") upscale.mode = cr2::Upscaler::Fsr3;
    else if (upscaler == "dlss") upscale.mode = cr2::Upscaler::Dlss;
    else if (upscaler == "metalfx") upscale.mode = cr2::Upscaler::MetalFx;
    else if (upscaler != "off") return std::fprintf(stderr, "--upscaler: off, fsr3, dlss or metalfx\n"), 2;
    {
        const auto q = std::find_if(std::begin(kQualities), std::end(kQualities), [&](const Quality& k) { return quality == k.name; });
        if (q == std::end(kQualities)) return std::fprintf(stderr, "--quality: native, quality, balanced, performance or ultra\n"), 2;
        upscale.ratio = q->ratio;
    }
    std::vector<std::string> searched; // (where a copy was looked for: the message if none)
    if (data.empty()) { // (CR2_DATA, else a copy in a game folder: vita/, android/, ios/ ...)
        searched = dataPlaces();
        data = cr2::defaultArchive(searched);
        if (data.empty()) data = storedData(); // (an older setup's: an Android or Vita copy where it lay)
    }
    data = cr2::resolveArchive(data); // (a folder: the OBB or the CRDB.*.mvgl in it, a Vita copy's PCSG00498 ...)
#if !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
    if (data.empty() && (argc <= 1 || cr2::setupScripted()) && !std::getenv("CR2_DATA")) { // the first start without a copy: where is it? (viewer/setup.hpp)
        SDL_Init(SDL_INIT_VIDEO); // (its message boxes and the system's dialogs)
        cr2::SetupGame setup;
        setup.title = "Chaos Rings II", setup.port = "CR2-U", setup.vitaId = "PCSG00498", setup.trilogyDir = "CR2";
        setup.trilogyAddon = "DIESEL2000ADDCON"; // (the Prequel Trilogy keeps the game's data in its add-on)
        setup.obbHint = "main.212.com.square_enix.chaosrings2gp.obb";
        setup.package = "com.square_enix.chaosrings2gp";
        setup.oneRelease = true; // (one copy is the game)
        setup.vitaProgram = true; // (the maps' table is in it: an encrypted one - a dump's, the trilogy's CR2.self - decrypted beside it)
        auto archiveIs = [](const std::string& p, const char* tail) {
            std::error_code ec;
            const std::string a = cr2::resolveArchive(p);
            const size_t n = std::strlen(tail);
            return std::filesystem::is_regular_file(a, ec) && a.size() >= n && a.compare(a.size() - n, n, tail) == 0;
        };
        setup.iosOk = [&](const std::string& p) { return archiveIs(p, ".ios.mvgl"); };
        setup.androidOk = [&](const std::string& p) { // (the OBB, and the APK with the game's library - the maps' table)
            std::vector<uint8_t> lib;
            return archiveIs(p, ".obb") && cr2::androidFile(p, "lib/armeabi-v7a/libChaosRings2.so", lib);
        };
        setup.vitaOk = [&](const std::string& p) { return archiveIs(p, ".psp2.mvgl"); };
        std::string profile;
        if (char* pref = SDL_GetPrefPath("DuIslingr", "CR2-U")) profile = pref, SDL_free(pref);
        cr2::SetupResult got;
        if (!cr2::runSetup(setup, searched.empty() ? profile : searched.front(), profile, got)) return 0;
        // (copied into the game folder - ios/, android/, vita/ -: found as on any start)
        data = cr2::resolveArchive(cr2::defaultArchive(dataPlaces()));
        SDL_Log("setup: iOS %s, Android %s, Vita %s - the game: %s", got.ios.c_str(), got.android.c_str(), got.vita.c_str(), data.c_str());
        if (cr2::setupScripted()) return data.empty() ? 1 : 0; // (a test's: the setup alone)
    }
#endif
    if (data.empty() && argc <= 1) { // (opened with nothing: a window, not the terminal's usage)
#ifdef SDL_PLATFORM_ANDROID
        if (!searched.empty()) // (the folders made, to be seen over USB)
            for (const char* f : {"ios", "android", "vita"}) SDL_CreateDirectory((searched.front() + f).c_str());
#endif
        std::string msg = "Could not find your copy of the game.\n\nPut any one of these in the folder here:\n";
        msg += searched.empty() ? std::string("(no folder)") : searched.front();
        msg += "\n\n  ios/      the iPad's ChaosRings2enHD.app (or the unpacked .ipa)"
               "\n  android/  the Android release's main.212.com.square_enix.chaosrings2gp.obb and its APK"
               "\n  vita/     the Vita's PCSG00498 folder\n\nAlso looked in:";
        for (size_t k = 1; k < searched.size(); ++k) msg += "\n" + searched[k];
        msg += "\n\nThe README's \"Your copy\" has the details.";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Chaos Rings II", msg.c_str(), nullptr);
        return 2;
    }
    if (data.empty()) {
        std::fprintf(stderr, "cr2view --data <archive> [--model name] [--anim name] [--time s] [--size WxH] [--yaw deg] "
                             "[--pitch deg] [--lights figure|none] [--field map [--at point] [--binary file]] "
                             "[--event function [--vista map] [--page s]] [--resolution WxH] [--upscaler off|fsr3|dlss "
                             "[--quality native|quality|balanced|performance|ultra] [--sharpness 0..1]] "
                             "[--renderer vulkan|opengl] [--capture out.bmp]\n");
        return 2;
    }
    Viewer v;
    { // CR2-U's settings (settings.ini; not for captures and exports), the command line's flags over them
        auto given = [&](const char* flag) {
            for (int i = 1; i < argc; ++i)
                if (std::strcmp(argv[i], flag) == 0) return true;
            return false;
        };
        Viewer::Settings& s = v.settings;
        if (!exploreDir.empty()) { // (from the title, as a player: in frames of the reference's 20:9)
            titleScreen = true;
            if (resW == 0) resW = 1280, resH = 576;
        }
        if (capture.empty() && !exportTextures && !exportModels && exploreDir.empty()) {
            v.settingsPersist = true;
            v.loadSettings();
            if (!given("--renderer")) renderer = s.renderer ? "opengl" : "vulkan";
            if (!given("--fps")) fps = s.fps;
            if (!given("--resolution")) resW = kResolutions[s.resolution][0], resH = kResolutions[s.resolution][1];
            if (!given("--upscaler")) upscale.mode = cr2::Upscaler(s.upscaler);
            if (!given("--quality")) upscale.ratio = kQualities[s.quality].ratio;
            if (!given("--sharpness")) upscale.sharpness = float(s.sharpness) / 100.0f;
            if (hdArg < 0) hdArg = s.hdTextures ? 1 : 0;
        }
        // (what runs, as the list shows it)
        s.renderer = renderer == "opengl" ? 1 : 0;
        s.fps = fps;
        s.resolution = -1;
        for (size_t k = 0; k < std::size(kResolutions); ++k)
            if (kResolutions[k][0] == resW && kResolutions[k][1] == resH) s.resolution = int(k);
        s.upscaler = int(upscale.mode);
        for (size_t k = 0; k < std::size(kQualities); ++k)
            if (kQualities[k].ratio == upscale.ratio) s.quality = int(k);
        s.sharpness = int(std::lround(upscale.sharpness * 100.0f));
        s.hdTextures = hdArg == 1;
    }
    std::string err;
    if (!v.archive.open(data, &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    SDL_Log("data: %s", data.c_str());
    const std::string gameFolder = searched.empty() ? dataPlaces().front() : searched.front(); // (textures/ goes here)
    { // the HD textures: textures/hd/ in the game folder - on, unless settings.ini says otherwise, when it is there
        const char* env = SDL_getenv("CR2_HD_TEXTURES");
        const char* base = SDL_GetBasePath();
        auto isDir = [](const std::string& p) {
            SDL_PathInfo info;
            return !p.empty() && SDL_GetPathInfo(p.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
        };
        const std::string hd = gameFolder + "textures/hd/", beside = base ? std::string(base) + "hd-textures" : std::string();
        bool bundled = true;
        if (env && *env) v.hdDir = env, bundled = false;
        else if (isDir(hd)) v.hdDir = hd;
        else if (isDir(beside)) v.hdDir = beside;
        else if (isDir(Viewer::savePath("hd-textures"))) v.hdDir = Viewer::savePath("hd-textures"), bundled = false;
        else v.hdDir = hd, bundled = false;
        if (!v.hdDir.empty() && v.hdDir.back() != '/' && v.hdDir.back() != '\\') v.hdDir += '/';
        if (bundled && !v.settings.hdSet) v.settings.hdTextures = v.hdTextures = true;
        if (hdArg >= 0) v.settings.hdTextures = v.hdTextures = hdArg == 1;
    }
    { // the HD models: models/hd/ in the game folder (CR2_HD_MODELS elsewhere)
        const char* env = SDL_getenv("CR2_HD_MODELS");
        v.modelsDir = env && *env ? std::string(env) : gameFolder + "models/hd/";
        if (!v.modelsDir.empty() && v.modelsDir.back() != '/' && v.modelsDir.back() != '\\') v.modelsDir += '/';
        if (hdModelsArg) v.settings.hdModels = true;
        v.hdModels = v.settings.hdModels;
    }
    if (smoothArg >= 0) v.settings.smoothModels = smoothArg;
    if (speedArg[0]) v.settings.fieldSpeed = speedArg[0];
    if (speedArg[1]) v.settings.battleSpeed = speedArg[1];
    if (exportModels) { // every figure as glTF (assets/gltf_figure.hpp): its skeleton, meshes, materials, pictures, animations
        std::string dir = exportModelsDir.empty() ? gameFolder + "models/export/" : exportModelsDir;
        if (dir.back() != '/' && dir.back() != '\\') dir += '/';
        if (!SDL_CreateDirectory(dir.c_str()) && exportModelsDir.empty()) dir = Viewer::savePath("models/export/"), SDL_CreateDirectory(dir.c_str());
        std::printf("exporting to %s\n", dir.c_str());
        cr2::ShaderLibrary shaders(v.archive);
        int written = 0, failed = 0, empty = 0;
        size_t bytes = 0;
        for (const auto& e : v.archive.entries()) {
            if (e.name.size() < 6 || e.name.compare(e.name.size() - 5, 5, ".geom") != 0) continue;
            const std::string base = e.name.substr(0, e.name.size() - 5);
            std::vector<uint8_t> d;
            cr2::Geom g;
            cr2::Skeleton sk;
            std::string why;
            if (!v.archive.read(e.name, d, &why) || !cr2::parseGeom(d, g, &why)) { std::printf("  %s: %s\n", e.name.c_str(), why.c_str()); ++failed; continue; }
            if (v.archive.read(base + ".skel", d)) cr2::parseSkeleton(d, sk);
            for (cr2::GeomMesh& m : g.meshes) // (the joints as MatrixPalette's rows, as loadFigure resolves them)
                if (m.material < g.materials.size()) {
                    const std::array<uint32_t, 4>& key = g.materials[m.material].shaderKey;
                    const bool builtIn = !key[0] && !key[1] && !key[2] && !key[3];
                    m.resolveJoints(builtIn ? (g.version == 0x3c ? 3.0f : 1.0f) : shaders.jointScale(key));
                }
            cr2::FigureExport fe;
            fe.geom = &g, fe.skeleton = &sk;
            if (v.archive.read(base + ".name", d)) fe.jointNames = cr2::parseJointNames(d), fe.materialNames = cr2::parseMaterialNames(d);
            for (const cr2::GeomMaterial& m : g.materials)
                for (const auto& [id, picture] : m.textures) {
                    const std::string& img = picture; // (a lambda can't take a structured binding before C++20 - Apple's clang)
                    if (img.empty() || std::any_of(fe.images.begin(), fe.images.end(), [&](const auto& p) { return p.first == img; })) continue;
                    cr2::Image im;
                    if (v.decode(img, im)) fe.images.emplace_back(img, std::move(im));
                }
            std::vector<std::unique_ptr<cr2::Animation>> anims;
            for (const auto& a : v.archive.entries()) // (<figure>*.anim: c217.anim, c217e080.anim, ...)
                if (exportAnimations && a.name.size() > 5 && a.name.compare(0, base.size(), base) == 0 && a.name.compare(a.name.size() - 5, 5, ".anim") == 0) {
                    auto an = std::make_unique<cr2::Animation>();
                    if (v.archive.read(a.name, d) && an->parse(d) && an->joints() == int(sk.basePose.size()))
                        fe.anims.emplace_back(a.name.substr(0, a.name.size() - 5), an.get()), anims.push_back(std::move(an));
                }
            // (a character's or a monster's motions move over the base pose; a map's or an object's own replace it)
            fe.absolute = !(base.size() > 1 && (base[0] == 'c' || base[0] == 'm' || base[0] == 'v') && std::isdigit(static_cast<unsigned char>(base[1])));
            cr2::GltfStats st;
            if (cr2::exportFigureGlb(fe, dir + base + ".glb", &why, &st)) {
                ++written, bytes += st.bytes;
                if (SDL_getenv("CR2_EXPORT_VERBOSE"))
                    std::printf("  %s: %d meshes, %d triangles, %d pictures, %d animations (%d channels), %zu KB\n", base.c_str(), st.meshes,
                                st.triangles, st.images, st.animations, st.channels, st.bytes / 1024);
            } else if (why == "no meshes") {
                ++empty; // (a figure of lights, cameras or anchors only)
            } else {
                std::printf("  %s: %s\n", base.c_str(), why.c_str());
                ++failed;
            }
        }
        std::printf("%d figures written (%zu MB), %d without meshes, %d failed\n", written, bytes >> 20, empty, failed);
        return failed && !written ? 1 : 0;
    }
    if (exportTextures) { // (the archive's every image, once each by its pixels: tools/hd_textures.py's input)
        if (exportDir.empty()) exportDir = gameFolder + "textures/export/"; // (else the profile folder's, if that can't be written)
        if (exportDir.back() != '/' && exportDir.back() != '\\') exportDir += '/';
        if (!SDL_CreateDirectory(exportDir.c_str())) exportDir = Viewer::savePath("textures/export/"), SDL_CreateDirectory(exportDir.c_str());
        std::printf("exporting to %s\n", exportDir.c_str());
        std::set<uint64_t> seen;
        int written = 0, failed = 0, dupes = 0, kinds[4] = {0, 0, 0, 0};
        for (const auto& e : v.archive.entries()) {
            if (e.name.rfind("images\\", 0) != 0 || e.name.size() < 12 || e.name.compare(e.name.size() - 4, 4, ".img") != 0) continue;
            std::vector<uint8_t> file;
            cr2::Image img;
            if (!v.archive.read(e.name, file) || !cr2::decodeImg(file, img)) { ++failed; continue; }
            const uint64_t h = cr2::imageHash(img);
            if (!seen.insert(h).second) { ++dupes; continue; }
            const std::string path = exportDir + cr2::hashName(h) + ".crraw";
            if (SDL_GetPathInfo(path.c_str(), nullptr)) continue; // (already there)
            if (cr2::saveRawImage(path, img)) ++written, ++kinds[cr2::imageStorage(img)];
            else ++failed;
        }
        std::printf("exported %d pictures to %s (%d lossless, %d block-compressed, %d 16-bit, %d unknown); %d alike, %d failed\n",
                    written, exportDir.c_str(), kinds[1], kinds[2], kinds[3], kinds[0], dupes, failed);
        return failed ? 1 : 0;
    }
    // the sound: played unless captured (or --mute) - then silent, its state kept all the same
    const bool device = ((capture.empty() && exploreDir.empty()) || forceAudio) && !mute;
    if (!v.audio.open(data, device, &err)) SDL_Log("sound: %s", err.c_str());
    else SDL_Log("sound: on%s%s", device ? "" : " (silent)", err.empty() ? "" : (" - " + err).c_str());
    for (const auto& e : v.archive.entries())
        if (e.name.size() > 5 && e.name.compare(e.name.size() - 5, 5, ".geom") == 0) v.models.push_back(e.name);
    std::sort(v.models.begin(), v.models.end());
    if (v.models.empty()) return std::fprintf(stderr, "no models in %s\n", data.c_str()), 1;
    const bool leaderDefaulted = model.empty() && (!fieldName.empty() || !playFrom.empty() || continueSave);
    if (leaderDefaulted) model = "c001"; // (the leader: Darwin - then the field's, refreshLeader)
    size_t start = 0;
    if (!model.empty()) {
        const std::string want = model.size() > 5 && model.compare(model.size() - 5, 5, ".geom") == 0 ? model : model + ".geom";
        const auto it = std::find(v.models.begin(), v.models.end(), want);
        if (it == v.models.end()) return std::fprintf(stderr, "%s: not in the archive\n", want.c_str()), 1;
        start = size_t(it - v.models.begin());
    }
    v.yaw = yaw, v.pitch = pitch;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) return std::fprintf(stderr, "SDL: %s\n", SDL_GetError()), 1;
    // The window: --size, else a capture's the resolution itself, else the resolution's shape
    // fitted to 90% of the display (the frames stay the resolution, scaled into it)
    if (!width || !height) {
        width = resW ? resW : 1280, height = resW ? resH : 800;
        SDL_Rect usable{};
        if (capture.empty() && SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) && usable.w > 0 && usable.h > 0) {
            const float fit = std::min({1.0f, 0.9f * float(usable.w) / float(width), 0.9f * float(usable.h) / float(height)});
            width = std::max(int(float(width) * fit), 320), height = std::max(int(float(height) * fit), 180);
        }
    }
    // The device: Vulkan unless asked otherwise or it can't start, then OpenGL
    cr2::useBundledVulkan(); // (macOS: the .app's own loader)
    std::unique_ptr<cr2::RenderDevice> dev;
    SDL_Window* win = nullptr;
    const SDL_WindowFlags shown = capture.empty() && exploreDir.empty() ? SDL_WINDOW_RESIZABLE : SDL_WINDOW_HIDDEN;
    for (const cr2::Backend b : {cr2::Backend::Vulkan, cr2::Backend::OpenGL}) {
        if (b == cr2::Backend::Vulkan && renderer != "vulkan") continue;
        std::unique_ptr<cr2::RenderDevice> d = b == cr2::Backend::Vulkan ? cr2::makeVkDevice() : cr2::makeGlDevice();
        if (!d) continue;
        win = SDL_CreateWindow(titleScreen ? "Chaos Rings II" : "CR2-U viewer", width, height, SDL_WindowFlags(d->windowFlags()) | shown);
        if (win && d->init(win)) {
            dev = std::move(d);
            SDL_Log("renderer: %s", dev->description().c_str());
            break;
        }
        SDL_Log("%s: could not start (%s)%s", cr2::backendName(b), SDL_GetError(), b == cr2::Backend::Vulkan ? ", trying OpenGL" : "");
        if (d) d->shutdown();
        if (win) SDL_DestroyWindow(win);
        win = nullptr;
    }
    if (!dev) return std::fprintf(stderr, "no renderer could start\n"), 1;
    v.dev = dev.get();
    v.resW = resW, v.resH = resH;
    if (upscale.mode != cr2::Upscaler::Off) {
        const std::string why = dev->upscalerProblem(upscale.mode);
        if (!why.empty()) SDL_Log("upscaler %s: %s - off", upscaler.c_str(), why.c_str()), upscale.mode = cr2::Upscaler::Off;
    }
    v.upscale = upscale;
    dev->setUpscaler(upscale);
    v.window = capture.empty() ? win : nullptr; // (a capture's hidden window stays as it is)
    v.rendererInUse = dev->offscreen() ? 0 : 1;
    v.settings.upscaler = int(upscale.mode); // (one that can't run here: off)
    v.applySettings();
    v.shaders = std::make_unique<cr2::ShaderLibrary>(v.archive);
    if (titleScreen && playFrom.empty()) playFrom = "d1020"; // (the title's New Game: --play's start)
    if (!playFrom.empty()) fieldName = playFrom, v.playMode = true;
    v.scriptSaves = (!playFrom.empty() || continueSave) && capture.empty();
    if (continueSave && fieldName.empty()) fieldName = "d1020"; // (the windows and fonts set up for the field)
    v.skipBattles = skipBattlesOpt; // (a new game: every flag clear)
    if (!fieldName.empty() || !eventName.empty() || battleId > 0) {
        // the font: --font, CR2_FONT, the Vita's beside its archive, the Android release's APK beside
        // its OBB (assets/00001: the Regular face it draws English in), else the system's
        std::string dataDir = data.substr(0, data.find_last_of("/\\") == std::string::npos ? 0 : data.find_last_of("/\\") + 1);
        const char* envFont = SDL_getenv("CR2_FONT");
        std::string fp = cr2::TextRasterizer::firstFont({fontPath, envFont ? envFont : "", dataDir + "font/TBGoPro-Bold.otf"});
        std::vector<uint8_t> apkFont;
        std::string apkFontFrom;
        if (fp.empty() && !cr2::androidFile(data, "assets/00001", apkFont, &apkFontFrom)) {
            const char* windir = SDL_getenv("WINDIR");
            const std::string winFonts = std::string(windir ? windir : "C:/Windows") + "/Fonts/";
            fp = cr2::TextRasterizer::firstFont({"/usr/share/fonts/noto/NotoSans-Regular.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
                                                 "/usr/share/fonts/liberation/LiberationSans-Regular.ttf", winFonts + "segoeui.ttf",
                                                 winFonts + "arial.ttf", "/System/Library/Fonts/Supplemental/Arial.ttf",
                                                 "/System/Library/Fonts/Helvetica.ttc"});
        }
        v.loadMessageWindow(fp, std::move(apkFont), apkFontFrom);
        v.initEventDrawing();
        v.pageSeconds = page;
    }
    v.newGame();
    // (the test's story flags and sopia levels: before the field's entry or the battle runs)
    for (int f : flagsArg) v.game.setFlag(f, true);
    for (const auto& [id, level] : sopiaArg)
        if (id >= 0 && id < 32) v.game.sopiaLevel[id] = static_cast<signed char>(level), v.game.sopiaOwned |= 1u << id; // (had, at that level)
    for (const auto& [id, n] : itemArg)
        if (id > 0 && id < 300) v.game.itemCount[id] = static_cast<short>(n);
    for (const auto& [c, slot, item] : equipArg)
        if (c >= 1 && c <= 9 && slot >= 0 && slot < 8) v.game.equip[c - 1][slot] = static_cast<short>(item);
    if (difficultyArg >= 0) v.game.difficulty = difficultyArg;
    if (ozArg >= 0) v.game.oz = ozArg;
    for (const std::string& w : workArgs) {
        const size_t c = w.find(':');
        const int n = std::atoi(w.c_str()), val = c == std::string::npos ? 0 : std::atoi(w.c_str() + c + 1);
        if (n >= 0 && n < int(std::size(v.game.globalWork))) v.game.globalWork[size_t(n)] = val;
    }
    if (membersArg[0] > -2) v.game.party[0] = membersArg[0], v.game.party[1] = membersArg[1];
    if (titleScreen) {
        v.playMode = true;
        v.startTitle();
    } else if (!gameSavePath.empty()) {
        v.playMode = true;
        if (!v.loadGameSave(gameSavePath, binary, data)) return 1;
    } else if (continueSave) {
        v.playMode = true;
        if (!v.continueSave(binary, data)) return 1;
    } else if (!fieldName.empty()) {
        if (!v.loadField(fieldName, at, binary, data, callEntry)) return 1;
        if (creditsArg >= 0) v.returnMap = fieldName, v.startCredits(creditsArg);
    } else if (lights != "none") {
        if (v.loadLights(lights)) SDL_Log("lights: %s", lights.c_str());
        else SDL_Log("lights: %s has none (the engine's defaults)", lights.c_str());
    }
    v.load(start);
    if (model == "c001" && leaderDefaulted) v.leaderFollows = true, v.refreshLeader();
    v.stateArg = stateArg;
    v.chargeArg = chargeArg, v.gaugeArg = gaugeArg;
    if (battleId > 0 && !v.startBattle(battleId, battleParty)) return 1;
    // the commands: chosen on the screen in a window (--fight manual, or --play), or given (--input)
    v.manualCommands = fightMode == 3 || !inputArg.empty() || (capture.empty() && !playFrom.empty());
    for (size_t p = 0; p < inputArg.size();) {
        const size_t e = inputArg.find_first_of(" ,", p);
        if (e != p) v.inputScript.push_back(inputArg.substr(p, e == std::string::npos ? std::string::npos : e - p));
        if (e == std::string::npos) break;
        p = e + 1;
    }
    if (battleId > 0 && fightMode) v.startFight(fightMode == 3 ? 2 : fightMode, fightSeed);
    if (!eventName.empty()) {
        if (!v.startEvent(eventName, vista, page)) return 1;
        for (float t = 0; t + cr2::EventRuntime::kTick * 0.5f < startTime; t += cr2::EventRuntime::kTick) v.stepEvent();
        startTime = 0;
    }
    if (!animName.empty()) {
        const std::string want = animName.size() > 5 && animName.compare(animName.size() - 5, 5, ".anim") == 0 ? animName : animName + ".anim";
        const auto it = std::find(v.fig.anims.begin(), v.fig.anims.end(), want);
        if (it == v.fig.anims.end()) return std::fprintf(stderr, "%s: not one of the figure's animations\n", want.c_str()), 1;
        v.loadAnim(v.fig, size_t(it - v.fig.anims.begin()));
    }
    if (v.fieldMode && animName.empty()) v.playMotion(v.fig, "fn01");
    // the title's choice: a new game, the save, the continue data (false: it failed)
    auto takeTitle = [&]() {
        if (v.titleMenu.stage != 0 || !v.titleMenu.action) return true;
        const int act = v.titleMenu.action;
        v.titleMenu.action = 0;
        v.game = cr2::GameState(); // (Cr3UtilResetWork)
        v.playSeconds = 0;
        v.newGame();
        if (act == 4) v.carryOver();
        const bool ok = act == 1 || act == 4 ? v.loadField(fieldName, at, binary, data, callEntry)
                                             : v.continueSave(binary, data, act == 2 ? "save1.txt" : "cont.txt");
        SDL_Log("title: %s", act == 1 ? "a new game" : act == 4 ? "a new game with the clear data" : act == 2 ? "the save loaded" : "the continue data");
        return ok;
    };
    auto frameSize = [&](int& w, int& h) {
        SDL_GetWindowSizeInPixels(win, &w, &h);
        if (dev->offscreen() && v.resW > 0 && v.resH > 0) w = v.resW, h = v.resH;
    };
    auto catchGlass = [&]() { // (frames not drawn: drawn as the screen breaks, to catch it)
        if (!v.glassPending) return;
        int w = 0, h = 0;
        frameSize(w, h);
        v.draw(w, h);
        dev->endFrame(false);
    };
    if (v.playMode) { // --time: the game played that long first (60 frames a second), then --walk
        for (float t = 0; t + 0.5f / 60.0f < startTime; t += 1.0f / 60.0f) {
            v.advance(1.0f / 60.0f, 0, 0, false);
            catchGlass();
            if (!takeTitle()) return 1;
        }
        for (float t = 0; t < walkSeconds; t += 1.0f / 60.0f) v.advance(1.0f / 60.0f, walkX, walkY, true);
        if (tapAfter) v.tap(); // (--tap: then a tap, before --run)
        if (const char* row = SDL_getenv("CR2_CAPTURE_SETTINGS")) { // (tests: as below, before --run)
            v.optionsOpen = true, v.campOpen = !v.battleMode;
            v.openSettings();
            v.settingsCursor = std::atoi(row);
        }
        for (float t = 0; t < runSeconds; t += 1.0f / 60.0f) v.advance(1.0f / 60.0f, 0, 0, false), catchGlass(); // (--run: on after the walk)
        runSeconds = 0;
        SDL_Log("play: after %.2f s (+ %.2f s walking) %s, %d battles skipped", startTime, walkSeconds,
                v.eventMode ? ("in the vista " + v.status).c_str() : ("on " + v.field.info().name).c_str(), v.battlesSkipped);
        startTime = 0;
    } else {
        for (float t = 0; v.fieldMode && t < walkSeconds; t += 1.0f / 60.0f) v.walk(1.0f / 60.0f, walkX, walkY, true);
    }
    if (v.fieldMode && walkSeconds > 0) SDL_Log("after --walk: %s at (%.3f %.3f %.3f)", v.field.info().name.c_str(), v.player[0], v.player[1], v.player[2]);
    if (drag[4] > 0 && v.walkStart(drag[0], drag[1])) {
        v.walkMove(drag[2], drag[3]);
        for (float t = 0; t < drag[4]; t += 1.0f / 60.0f) {
            float sx = 0, sy = 0;
            bool run = false;
            v.walkStick(sx, sy, run);
            if (v.playMode) v.advance(1.0f / 60.0f, sx, sy, run);
            else v.walk(1.0f / 60.0f, sx, sy, run);
        }
        SDL_Log("after --drag: %s at (%.3f %.3f %.3f)", v.field.info().name.c_str(), v.player[0], v.player[1], v.player[2]);
    }
    v.time = startTime;
    v.pose();
    SDL_Log("%s%s", v.status.c_str(), v.animStatus().c_str());

    // a frame's size: the resolution where the device draws offscreen, else the window's
    auto describe = [&]() {
        int w = 0, h = 0;
        frameSize(w, h);
        // (the device's sceneSize follows the mode it last ran: this is the one set)
        const float r = v.upscale.mode == cr2::Upscaler::Off ? 1.0f : std::clamp(v.upscale.ratio, 1.0f, 3.0f);
        const int rw = std::max(int(float(w) / r), 1), rh = std::max(int(float(h) / r), 1);
        char buf[160];
        std::snprintf(buf, sizeof buf, "%dx%d%s, upscaler %s (scene %dx%d)", w, h, v.resW ? "" : " (the window's)", upscaleName(v.upscale).c_str(), rw, rh);
        return std::string(buf);
    };
    SDL_Log("output: %s", describe().c_str());

    int code = 0;
    if (!exploreDir.empty()) { // the game played by itself (Viewer::exploreStep), its timeline and pictures to the folder
        SDL_CreateDirectory(exploreDir.c_str());
        if (exploreDir.back() != '/') exploreDir += '/';
        v.ex.on = true, v.ex.dir = exploreDir;
        if (const char* st = SDL_getenv("CR2_EXPLORE_STUCK")) v.exploreStuckSeconds = std::atof(st);
        v.ex.log = std::fopen((exploreDir + "timeline.txt").c_str(), "wb");
        for (size_t p = 0; p < choicesArg.size();) {
            const size_t c = std::min(choicesArg.find(',', p), choicesArg.size());
            v.ex.choices.push_back(choicesArg.substr(p, c - p));
            p = c + 1;
        }
        v.manualCommands = false; // (the battles by their rules)
        bool pageGiven = false;
        for (int i = 1; i < argc; ++i) pageGiven |= std::strcmp(argv[i], "--page") == 0;
        v.pageSeconds = pageGiven ? page : 1.0f;
        int w = 0, h = 0;
        frameSize(w, h);
        for (long n = 0; !v.ex.done && v.ex.clock < double(exploreHours) * 3600.0; ++n) {
            float sx, sy;
            v.exploreStep(sx, sy);
            v.advance(1.0f / 60.0f, sx, sy, true);
            v.pose();
            catchGlass();
            if (!takeTitle()) break;
            if (v.exploreShotDue >= 0 && v.ex.clock >= v.exploreShotDue) { // (the milestone's picture)
                v.draw(w, h);
                dev->endFrame(false);
                std::vector<uint8_t> px;
                int pw = w, ph = h;
                std::string name = v.exploreShotName;
                for (char& c : name)
                    if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
                char file[64];
                std::snprintf(file, sizeof file, "%04d_", ++v.ex.shots);
                if (dev->readPixels(px, pw, ph)) saveBmp((exploreDir + file + name.substr(0, 48) + ".bmp").c_str(), px, pw, ph);
                v.exploreShotDue = -1;
            } else if (n % 6 == 0) { // (the menus' tap areas as drawn: kept up)
                v.draw(w, h);
                dev->endFrame(false);
            }
        }
        v.exploreLog(v.ex.done ? "explore: stopped" : "explore: the time's up");
        if (v.ex.log) std::fclose(v.ex.log);
        code = 0;
    } else if (!capture.empty()) { // one frame to a file
        int w = 0, h = 0;
        frameSize(w, h);
        // (a temporal upscaler first gathers a still frame's history: its jitter's phases)
        const int warmup = v.upscale.mode != cr2::Upscaler::Off ? 32 : 0;
        for (int k = 0; k < warmup; ++k) {
            v.draw(w, h);
            dev->endFrame(false);
        }
        // --run: frames as the window would show them, the event and animations going on
        if (v.battleMode && !v.playMode) { // (--time: that far into the battle, played at 60 a second)
            for (float t = 0; t + 0.5f / 60.0f < startTime; t += 1.0f / 60.0f) {
                v.advance(1.0f / 60.0f, 0, 0, false), v.pose();
                catchGlass(); // (the screen caught as it breaks)
                if (!takeTitle()) return 1; // (a game over's "try again": its save loaded)
            }
        }
        if (const char* row = SDL_getenv("CR2_CAPTURE_SETTINGS"); row && !v.settingsOpen) { // (tests: the Display & Graphics list over the camp's options, that row in focus)
            v.optionsOpen = true, v.campOpen = !v.battleMode;
            v.openSettings();
            v.settingsCursor = std::atoi(row);
        }
        for (int k = 0; k < int(runSeconds * 60.0f + 0.5f); ++k) {
            v.time += 1.0f / 60.0f;
            v.advance(1.0f / 60.0f, walkX, walkY, true); // (the field on in --walk's direction)
            v.pose();
            v.draw(w, h);
            dev->endFrame(false);
        }
        v.draw(w, h);
        dev->endFrame(false);
        std::vector<uint8_t> px;
        const bool ok = dev->readPixels(px, w, h) && saveBmp(capture.c_str(), px, w, h);
        SDL_Log("capture %s: %s", capture.c_str(), ok ? "written" : SDL_GetError());
        if (v.hdTextures) SDL_Log("hd textures: %d replaced, %d without one (%s)", v.hdLoaded, v.hdMissing, v.hdDir.c_str());
        code = ok ? 0 : 1;
    } else {
        bool running = true, dragging = false;
        // A controller, as the Vita's buttons (its own code - docs/CONTROLS.md): by position, ○ the right
        // button and × the bottom one (the settings' Swap ○ and × the other way); its presses as the
        // keys the menus take - ○ Return, × Backspace, the d-pad (or the left stick pushed all the way)
        // the arrows, repeating as InputEquipment's do (after 0.6 s, then every 0.08 s), L / R a list's
        // page, START the title's "Please Touch", □ held the fast-forward; △ (the port's own: the Vita's is
        // a touch) the field's tab. On the field the left stick walks, a long push running
        SDL_Gamepad* pad = nullptr;
        SDL_Keycode padDir = 0;
        Uint64 padDirNext = 0;
        auto pushKey = [](SDL_Keycode key) {
            SDL_Event k{};
            k.type = SDL_EVENT_KEY_DOWN;
            k.key.key = key;
            k.key.down = true;
            SDL_PushEvent(&k);
        };
        // CR2_PAD_SCRIPT (tests): a virtual controller, its buttons and stick set at their times - "1.5:east
        // 2:down 3:lx=1 4:lx=0" (a button is held 0.1 s; east / south / north / west, up / down / left /
        // right, l / r, start, back; lx / ly the left stick, -1 .. 1, ly up positive)
        struct PadStep {
            float at;
            std::string what;
        };
        std::vector<PadStep> padScript;
        SDL_Joystick* testPad = nullptr;
        if (const char* ps = SDL_getenv("CR2_PAD_SCRIPT")) {
            SDL_VirtualJoystickDesc desc;
            SDL_INIT_INTERFACE(&desc);
            desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
            desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
            desc.name = "CR2-U test controller";
            if (const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc)) testPad = SDL_OpenJoystick(id);
            std::string all = ps;
            for (size_t at = 0; at < all.size();) {
                const size_t sp = std::min(all.find(' ', at), all.size());
                const std::string t = all.substr(at, sp - at);
                if (const size_t c = t.find(':'); c != std::string::npos) padScript.push_back({float(std::atof(t.c_str())), t.substr(c + 1)});
                at = sp + 1;
            }
        }
        std::vector<std::pair<Uint64, SDL_GamepadButton>> testReleases;
        auto padStick = [&](float& x, float& y) { // (the left stick, up positive; the Vita's 0.25 dead zone)
            x = pad ? float(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX)) / 32767.0f : 0.0f;
            y = pad ? -float(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY)) / 32767.0f : 0.0f;
            if (std::hypot(x, y) < 0.25f) x = y = 0;
        };
        // (the window's point on the game's screen, letterboxed in the window at its own shape: its fractions)
        auto screenFraction = [&](float wx, float wy, float& fx, float& fy) {
            int ww = 1, wh = 1, pw = 1, ph = 1;
            SDL_GetWindowSize(win, &ww, &wh);
            SDL_GetWindowSizeInPixels(win, &pw, &ph);
            const float px = wx * float(pw) / float(std::max(ww, 1)), py = wy * float(ph) / float(std::max(wh, 1));
            const float sw = float(v.screenW), sh = float(v.screenH);
            const float k = std::min(float(pw) / sw, float(ph) / sh);
            const float ox = (float(pw) - sw * k) * 0.5f, oy = (float(ph) - sh * k) * 0.5f;
            fx = (px - ox) / (sw * k), fy = (py - oy) / (sh * k);
        };
        // CR2_MOUSE_SCRIPT (tests): the mouse's button and moves at their times, at the game screen's
        // fractions - "1:down=0.5,0.6 1.2:move=0.6,0.6 3:up"
        std::vector<PadStep> mouseScript;
        if (const char* ms = SDL_getenv("CR2_MOUSE_SCRIPT")) {
            std::string all = ms;
            for (size_t at = 0; at < all.size();) {
                const size_t sp = std::min(all.find(' ', at), all.size());
                const std::string t = all.substr(at, sp - at);
                if (const size_t c = t.find(':'); c != std::string::npos) mouseScript.push_back({float(std::atof(t.c_str())), t.substr(c + 1)});
                at = sp + 1;
            }
        }
        float mouseX = 0, mouseY = 0;
        auto pushMouse = [&](const std::string& w) { // (the inverse of screenFraction, as window coordinates)
            if (const size_t eq = w.find('='); eq != std::string::npos) {
                int ww = 1, wh = 1, pw = 1, ph = 1;
                SDL_GetWindowSize(win, &ww, &wh);
                SDL_GetWindowSizeInPixels(win, &pw, &ph);
                const float sw = float(v.screenW), sh = float(v.screenH), k = std::min(float(pw) / sw, float(ph) / sh);
                const float fx = float(std::atof(w.c_str() + eq + 1)), fy = float(std::atof(w.c_str() + w.find(',') + 1));
                mouseX = ((float(pw) - sw * k) * 0.5f + fx * sw * k) * float(ww) / float(std::max(pw, 1));
                mouseY = ((float(ph) - sh * k) * 0.5f + fy * sh * k) * float(wh) / float(std::max(ph, 1));
            }
            SDL_Event m{};
            m.type = w.rfind("down", 0) == 0 ? SDL_EVENT_MOUSE_BUTTON_DOWN : w.rfind("up", 0) == 0 ? SDL_EVENT_MOUSE_BUTTON_UP : SDL_EVENT_MOUSE_MOTION;
            if (m.type == SDL_EVENT_MOUSE_MOTION) m.motion.x = mouseX, m.motion.y = mouseY, m.motion.windowID = SDL_GetWindowID(win);
            else m.button.x = mouseX, m.button.y = mouseY, m.button.button = SDL_BUTTON_LEFT, m.button.down = m.type == SDL_EVENT_MOUSE_BUTTON_DOWN, m.button.windowID = SDL_GetWindowID(win);
            SDL_PushEvent(&m);
            SDL_Log("test mouse: %s", w.c_str());
        };
        Uint64 last = SDL_GetTicksNS();
        // --fps (the settings' Frame Rate): each frame starts no sooner than its slot (the display's
        // vsync may hold it longer)
        Uint64 nextFrame = last, fpsSince = last;
        int fpsFrames = 0, totalFrames = 0;
        float measured = 0;
        const Uint64 started = last;
        while (running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_EVENT_QUIT) running = false;
                else if (e.type == SDL_EVENT_KEY_DOWN) {
                    // (Android's Back: the menus' back, as Backspace)
                    const SDL_Keycode k = e.key.key == SDLK_AC_BACK ? SDLK_BACKSPACE : e.key.key;
                    if (k == SDLK_ESCAPE) running = false;
                    if (k == SDLK_SPACE) v.paused = !v.paused;
                    // F1: the next resolution; F2: the upscaler's next mode (off, FSR 3.1 native
                    // AA .. ultra performance)
                    if (k == SDLK_F1) {
                        size_t i = 0;
                        while (i < std::size(kResolutions) && (kResolutions[i][0] != v.resW || kResolutions[i][1] != v.resH)) ++i;
                        i = (i + 1) % std::size(kResolutions);
                        v.resW = kResolutions[i][0], v.resH = kResolutions[i][1];
                        v.settings.resolution = int(i);
                        v.saveSettings();
                        dev->resetHistory();
                        SDL_Log("output: %s", describe().c_str());
                        continue;
                    }
                    if (k == SDLK_F2) {
                        cr2::UpscaleSettings u = v.upscale;
                        size_t q = 0;
                        while (q < std::size(kQualities) && kQualities[q].ratio != u.ratio) ++q;
                        if (u.mode == cr2::Upscaler::Off) u.mode = cr2::Upscaler::Fsr3, u.ratio = kQualities[0].ratio;
                        else if (q + 1 < std::size(kQualities)) u.ratio = kQualities[q + 1].ratio;
                        else u.mode = cr2::Upscaler::Off;
                        const std::string why = dev->upscalerProblem(u.mode);
                        if (!why.empty()) SDL_Log("upscaler: %s", why.c_str()), u.mode = cr2::Upscaler::Off;
                        v.settings.upscaler = int(u.mode);
                        for (size_t k = 0; k < std::size(kQualities); ++k)
                            if (kQualities[k].ratio == u.ratio) v.settings.quality = int(k);
                        v.applySettings(); // (the upscaler, MSAA and the textures' LOD bias with it)
                        v.saveSettings();
                        SDL_Log("output: %s", describe().c_str());
                        continue;
                    }
                    if (v.yesNoShown()) { // (an event's yes / no: Y or Return yes, N or Backspace no)
                        if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_Y) v.answerYesNo(true);
                        else if (k == SDLK_BACKSPACE || k == SDLK_N) v.answerYesNo(false);
                        continue;
                    }
                    if ((k == SDLK_PAGEUP || k == SDLK_PAGEDOWN) && v.listShown()) { // (a list's page: four rows on)
                        for (int r = 0; r < 4; ++r) pushKey(k == SDLK_PAGEUP ? SDLK_UP : SDLK_DOWN);
                        continue;
                    }
                    if (v.focusKey(k)) continue; // (the title's, the shop's and the party's buttons; the field's tab)
                    if (v.battleMode && v.choosing) {
                        v.battleKey(k);
                        continue;
                    }
                    if (v.campOpen) { // (the camp: Up / Down and Return, Backspace back)
                        v.campKey(k);
                        continue;
                    }
                    if (v.party.stage) { // (the party: 1 / 2, Left / Right, Return, Backspace)
                        v.partyKey(k);
                        continue;
                    }
                    if (v.shop.stage) { // (the shop: 1 .. 4 its buttons, Up / Down, Left / Right, Return, Backspace; Y / N)
                        v.shopKey(k);
                        continue;
                    }
                    if (v.world.stage) { // (the world select: Up / Down, Return, Backspace; Y / N its question)
                        v.worldKey(k);
                        continue;
                    }
                    if (v.pub.stage) { // (the pub: Left / Right, Return, Backspace; Y / N)
                        v.pubKey(k);
                        continue;
                    }
                    if (v.library.stage == 1) { // (the library: Up / Down, Return, Backspace)
                        v.libraryKey(k);
                        continue;
                    }
                    if (v.mapOpen) { // (the map: any key closes it)
                        v.audio.playSe(0xb5, false), v.mapOpen = false;
                        continue;
                    }
                    if (v.saveStage) { // (saving: Return yes, Backspace no)
                        if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_Y) v.saveAnswer(true);
                        else if (k == SDLK_BACKSPACE || k == SDLK_N) v.saveAnswer(false);
                        continue;
                    }
                    if (k == SDLK_TAB && v.fieldTabShown()) { // (the field's tab: open or closed; C the camp)
                        v.fieldTabTap(1);
                        continue;
                    }
                    if (k == SDLK_C && v.fieldTabShown()) {
                        v.fieldToken("camp");
                        continue;
                    }
                    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) v.tap();
                    if (v.eventMode) continue;
                    ViewFigure& f = v.fig;
                    if (v.fieldMode) {
                        if ((k == SDLK_PAGEUP || k == SDLK_PAGEDOWN) && !f.anims.empty())
                            v.loadAnim(f, (f.animIndex + (k == SDLK_PAGEDOWN ? 1 : f.anims.size() - 1)) % f.anims.size());
                        continue;
                    }
                    size_t to = v.current;
                    if (k == SDLK_RIGHT || k == SDLK_PAGEDOWN) to = (v.current + (k == SDLK_PAGEDOWN ? 10 : 1)) % v.models.size();
                    else if (k == SDLK_LEFT || k == SDLK_PAGEUP)
                        to = (v.current + v.models.size() * 10 - (k == SDLK_PAGEUP ? 10 : 1)) % v.models.size();
                    if (to != v.current) v.load(to);
                    if ((k == SDLK_UP || k == SDLK_DOWN) && !f.anims.empty())
                        v.loadAnim(f, (f.animIndex + (k == SDLK_DOWN ? 1 : f.anims.size() - 1)) % f.anims.size());
                } else if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
                    if (!pad && (pad = SDL_OpenGamepad(e.gdevice.which))) SDL_Log("controller: %s", SDL_GetGamepadName(pad));
                } else if (e.type == SDL_EVENT_GAMEPAD_REMOVED) {
                    if (pad && SDL_GetGamepadID(pad) == e.gdevice.which) SDL_CloseGamepad(pad), pad = nullptr, padDir = 0;
                } else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && pad) {
                    const SDL_GamepadButton b = SDL_GamepadButton(e.gbutton.button);
                    const SDL_GamepadButton circle = v.settings.padSwap ? SDL_GAMEPAD_BUTTON_SOUTH : SDL_GAMEPAD_BUTTON_EAST;
                    const SDL_GamepadButton cross = v.settings.padSwap ? SDL_GAMEPAD_BUTTON_EAST : SDL_GAMEPAD_BUTTON_SOUTH;
                    const bool shopList = v.shop.stage >= 3 && v.shop.kind > 0; // (ShopCutInPiupiuMenu: L / R the character)
                    SDL_Keycode key = 0;
                    if (b == circle) key = SDLK_RETURN;
                    else if (b == cross) key = SDLK_BACKSPACE;
                    else if (b == SDL_GAMEPAD_BUTTON_NORTH) key = SDLK_TAB;
                    else if (b == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER && v.inMenu()) key = shopList ? SDLK_LEFT : SDLK_PAGEUP;
                    else if (b == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER && v.inMenu()) key = shopList ? SDLK_RIGHT : SDLK_PAGEDOWN;
                    else if (b == SDL_GAMEPAD_BUTTON_START && v.titleMenu.stage >= 2 && v.titleMenu.stage <= 3) key = SDLK_RETURN;
                    if (key) pushKey(key);
                } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    dragging = true;
                    float fx, fy;
                    screenFraction(e.button.x, e.button.y, fx, fy);
                    if (!v.menuPointerDown(fx, fy)) v.pointerActivate(fx, fy);
                }
                else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                    float fx, fy;
                    screenFraction(e.button.x, e.button.y, fx, fy);
                    if (v.menuPointerUp(fx, fy)) { dragging = false; continue; }
                    dragging = false;
                    v.fastHeld = false;
                    if (v.walkDrag.down) v.walkEnd();
                    if (v.fieldMode && v.keyLock.stage == 1) v.unlockPointer(0, 0, 2);
                    if (v.vslash.stage == 1) {
                        float fx, fy;
                        screenFraction(e.button.x, e.button.y, fx, fy);
                        v.vslashPointer(fx, fy, 2);
                    }
                    if (v.fieldMode && v.aim.stage == 2) {
                        float fx, fy;
                        screenFraction(e.button.x, e.button.y, fx, fy);
                        v.aimPointer(fx, fy, 2);
                    }
                } else if (e.type == SDL_EVENT_MOUSE_MOTION && v.menuSwipe.down) {
                    float fx, fy;
                    screenFraction(e.motion.x, e.motion.y, fx, fy);
                    v.menuPointerMove(fx, fy);
                } else if (e.type == SDL_EVENT_MOUSE_WHEEL && v.menuScroll(e.wheel.y > 0 ? -1 : e.wheel.y < 0 ? 1 : 0)) {
                } else if (e.type == SDL_EVENT_MOUSE_MOTION && dragging && v.walkDrag.down) {
                    float fx, fy;
                    screenFraction(e.motion.x, e.motion.y, fx, fy);
                    v.walkMove(fx, fy);
                } else if (e.type == SDL_EVENT_MOUSE_MOTION && dragging && v.fieldMode && (v.keyLock.stage == 1 || v.iai.stage == 2 || v.aim.stage == 2)) {
                    float fx, fy;
                    screenFraction(e.motion.x, e.motion.y, fx, fy);
                    if (v.iai.stage == 2) v.iaiPointer(fx, fy, 1);
                    else if (v.aim.stage == 2) v.aimPointer(fx, fy, 1);
                    else v.unlockPointer(fx, fy, 1);
                }
                else if (e.type == SDL_EVENT_MOUSE_MOTION && dragging && v.campOpen && v.campLink == 201) { // (the scenarios dragged up and down)
                    int ww = 1, wh = 1;
                    SDL_GetWindowSize(win, &ww, &wh);
                    v.scrollScenarios(-e.motion.yrel / float(std::max(wh, 1)) * 0.64f);
                } else if (e.type == SDL_EVENT_MOUSE_WHEEL && v.settingsOpen) { // (the settings' list: a row up / down)
                    if (e.wheel.y != 0) v.settingsKey(e.wheel.y > 0 ? SDLK_UP : SDLK_DOWN);
                } else if (e.type == SDL_EVENT_MOUSE_WHEEL && v.campOpen && v.campLink == 201) {
                    v.scrollScenarios(e.wheel.y > 0 ? -0.1f : 0.1f);
                } else if (e.type == SDL_EVENT_MOUSE_MOTION && dragging && !v.fieldMode) {
                    v.yaw -= e.motion.xrel * 0.4f;
                    v.pitch = std::clamp(v.pitch + e.motion.yrel * 0.4f, -89.0f, 89.0f);
                } else if (e.type == SDL_EVENT_MOUSE_WHEEL && !v.fieldMode) {
                    v.zoom = std::clamp(v.zoom * (e.wheel.y > 0 ? 0.9f : 1.1f), 0.05f, 20.0f);
                }
            }
            if (!takeTitle()) running = false;
            if (v.toTitle) { // (the options' Title Screen, yes: the title screen; --play without it from a new game again; else the end)
                v.toTitle = false;
                if (titleScreen) {
                    v.leaveBattle();
                    v.startTitle();
                } else if (v.playMode) {
                    v.leaveBattle();
                    v.newGame();
                    if (!v.loadField(fieldName, at, binary, data, callEntry)) running = false;
                } else running = false;
            }
            if (const Uint64 period = v.settings.fps > 0 ? Uint64(1000000000) / Uint64(v.settings.fps) : 0) {
                const Uint64 t = SDL_GetTicksNS();
                if (t < nextFrame) SDL_DelayPrecise(nextFrame - t);
                nextFrame = std::max(nextFrame + period, SDL_GetTicksNS() - period); // (no catching up after a stall)
            }
            const Uint64 now = SDL_GetTicksNS();
            const float dt = float(double(now - last) / 1e9);
            while (!mouseScript.empty() && mouseScript.front().at <= float(double(now - started) / 1e9)) { // (CR2_MOUSE_SCRIPT's)
                pushMouse(mouseScript.front().what);
                mouseScript.erase(mouseScript.begin());
            }
            if (testPad) { // (CR2_PAD_SCRIPT's steps, by the seconds since the window opened)
                static const std::map<std::string, SDL_GamepadButton> kButtons = {
                    {"east", SDL_GAMEPAD_BUTTON_EAST}, {"south", SDL_GAMEPAD_BUTTON_SOUTH}, {"north", SDL_GAMEPAD_BUTTON_NORTH},
                    {"west", SDL_GAMEPAD_BUTTON_WEST}, {"up", SDL_GAMEPAD_BUTTON_DPAD_UP}, {"down", SDL_GAMEPAD_BUTTON_DPAD_DOWN},
                    {"left", SDL_GAMEPAD_BUTTON_DPAD_LEFT}, {"right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT}, {"l", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
                    {"r", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER}, {"start", SDL_GAMEPAD_BUTTON_START}, {"back", SDL_GAMEPAD_BUTTON_BACK}};
                const float since = float(double(now - started) / 1e9);
                while (!padScript.empty() && padScript.front().at <= since) {
                    const std::string w = padScript.front().what;
                    padScript.erase(padScript.begin());
                    if (w.rfind("lx=", 0) == 0) SDL_SetJoystickVirtualAxis(testPad, SDL_GAMEPAD_AXIS_LEFTX, Sint16(std::clamp(std::atof(w.c_str() + 3), -1.0, 1.0) * 32767));
                    else if (w.rfind("ly=", 0) == 0) SDL_SetJoystickVirtualAxis(testPad, SDL_GAMEPAD_AXIS_LEFTY, Sint16(-std::clamp(std::atof(w.c_str() + 3), -1.0, 1.0) * 32767));
                    else if (auto b = kButtons.find(w); b != kButtons.end()) {
                        SDL_SetJoystickVirtualButton(testPad, b->second, true);
                        testReleases.push_back({now + 100000000, b->second});
                    }
                    SDL_Log("test controller: %s", w.c_str());
                }
                for (size_t r = 0; r < testReleases.size();)
                    if (testReleases[r].first <= now) SDL_SetJoystickVirtualButton(testPad, testReleases[r].second, false), testReleases.erase(testReleases.begin() + long(r));
                    else ++r;
            }
            if (pad) { // (the d-pad, or the stick pushed all the way: a direction's press, then its repeats)
                float ax, ay;
                padStick(ax, ay);
                SDL_Keycode d = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP)      ? SDLK_UP
                                : SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)  ? SDLK_DOWN
                                : SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)  ? SDLK_LEFT
                                : SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ? SDLK_RIGHT
                                                                                           : 0;
                if (!d && std::max(std::abs(ax), std::abs(ay)) >= 0.9f)
                    d = std::abs(ay) >= std::abs(ax) ? (ay > 0 ? SDLK_UP : SDLK_DOWN) : (ax > 0 ? SDLK_RIGHT : SDLK_LEFT);
                if (d != padDir) {
                    padDir = d;
                    if (d) pushKey(d), padDirNext = now + 600000000;
                } else if (d && now >= padDirNext) {
                    pushKey(d), padDirNext = now + 80000000;
                }
            }
            ++fpsFrames, ++totalFrames;
            if (now - fpsSince >= 500000000) {
                measured = float(double(fpsFrames) * 1e9 / double(now - fpsSince));
                fpsFrames = 0, fpsSince = now;
            }
            if (!v.paused) v.time += dt;
            last = now;
            if (!v.paused && v.battleMode && !v.fieldMode && !v.eventMode) v.advance(dt, 0, 0, false);
            if (!v.paused && (v.eventMode || v.fieldMode || v.credits.stage || v.titleMenu.stage)) { // the arrows / WASD run, with Shift walk (the title: its clock)
                const bool* keys = SDL_GetKeyboardState(nullptr);
                if (keys[SDL_SCANCODE_F] || (pad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST))) v.fastHeld = true; // (F or □ held: the fast-forward button's)
                else if (!(SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK)) v.fastHeld = false;
                float sx = float((keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) - (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]));
                float sy = float((keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) - (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]));
                bool run = !(keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]);
                if (pad && !v.fieldTabOpen) { // (FldMain::PlayerMove: the stick's push as the finger's drag - far, a run)
                    float ax, ay;
                    padStick(ax, ay);
                    const float dx = float(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) - float(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT));
                    const float dy = float(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP)) - float(SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN));
                    if (ax != 0 || ay != 0) sx = ax, sy = ay, run = std::hypot(ax, ay) >= 0.75f;
                    else if (dx != 0 || dy != 0) sx = dx, sy = dy, run = true;
                }
                if (sx == 0 && sy == 0) v.walkStick(sx, sy, run); // (the mouse's, a finger's drag)
                v.advance(dt, sx, sy, run);
            }
            if (v.fig.animLoaded && v.time > v.fig.anim.duration()) v.time = std::fmod(v.time, std::max(v.fig.anim.duration(), 0.05f));
            v.pose();
            SDL_SetWindowTitle(win, ("CR2-U viewer (" + std::string(cr2::backendName(dev->windowFlags() & SDL_WINDOW_VULKAN ? cr2::Backend::Vulkan : cr2::Backend::OpenGL)) + ", " +
                                     describe() + ", " + std::to_string(int(measured + 0.5f)) + " fps) - " + v.status + v.animStatus()).c_str());
            int w = 0, h = 0;
            frameSize(w, h);
            v.draw(w, h);
            dev->endFrame(true);
        }
        const double secs = double(SDL_GetTicksNS() - started) / 1e9;
        SDL_Log("%d frames in %.1f s: %.1f a second (aiming at %s)", totalFrames, secs, totalFrames / std::max(secs, 1e-3),
                fps ? std::to_string(fps).c_str() : "the display's rate");
    }
    v.unload(v.fig);
    v.unload(v.mapFigure);
    v.unload(v.vistaMap);
    v.unload(v.battleMap);
    for (auto& u : v.battleUnits) v.unload(u.fig);
    v.unload(v.vistaWindow.fig);
    v.unload(v.narrationWindow.fig);
    v.unload(v.conversationWindow.fig);
    v.unload(v.conversationCursor);
    for (ViewFigure& f : v.cutInFigures) v.unload(f);
    v.unload(v.pageCursor);
    for (auto& q : v.textQuads) dev->destroy(q.tex);
    for (ViewFigure& g : v.eventFigures) v.unload(g);
    if (v.white.id) dev->destroy(v.white);
    for (ViewFigure& g : v.gimmicks) v.unload(g);
    for (auto& [k, t] : v.textures) dev->destroy(t);
    dev->shutdown();
    SDL_DestroyWindow(win);
    SDL_Quit();
    return code;
}
