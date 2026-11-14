#pragma once

#include "platform/vita_import_ui.hpp"
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "assets/geom.hpp"
#include "assets/image.hpp"
#include "assets/maptable.hpp"
#include "assets/mvgl.hpp"
#include "assets/skeleton.hpp"
#include "battle/battledata.hpp"
#include "battle/camera.hpp"
#include "battle/command.hpp"
#include "battle/glass.hpp"
#include "battle/motion.hpp"
#include "battle/rules.hpp"
#include "battle/script.hpp"
#include "battle/special.hpp"
#include "event/runtime.hpp"
#include "field/character.hpp"
#include "field/fieldmap.hpp"
#include "field/fieldscript.hpp"
#include "field/navigation.hpp"
#include "gfx/material.hpp"
#include "gfx/render_device.hpp"
#include "gfx/text.hpp"
#include "platform/input_cancel.hpp"
#include "viewer/audio.hpp"
#include "viewer/mobile_input.hpp"

std::string storedData(); // (settings.ini's data=: below)

namespace cr2_viewer {

using cr2::Mat4;

// GL enums the materials' render states use
inline constexpr uint32_t kGlSrcAlpha = 0x0302, kGlOneMinusSrcAlpha = 0x0303, kGlFront = 0x0404, kGlBack = 0x0405;

void placeQuat(const cr2::Mat34& a, float q[4]);
cr2::Mat34 blendPlace(const cr2::Mat34& a, const cr2::Mat34& b, float t);
Mat4 perspective(float fovY, float aspect, float zn, float zf);
Mat4 lookAt(const float* eye, const float* at);

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
                const float* uv1);

// The output resolutions F1 steps through: the window's own size, then 16:9 up to 3840 x 2160
inline constexpr int kResolutions[][2] = {{0, 0}, {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3200, 1800}, {3840, 2160}};
inline constexpr int kMaxResW = 3840, kMaxResH = 2160;

// FSR 3.1's quality modes (the output / render ratio per axis; native AA renders at full size)
struct Quality {
    const char* name;
    float ratio;
};
inline constexpr Quality kQualities[] = {{"native", 1.0f}, {"quality", 1.5f}, {"balanced", 1.7f}, {"performance", 2.0f}, {"ultra", 3.0f}};

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
    static const char* settingsFile();
    void loadSettings();
    void saveSettings() const;
    void applySettings();
    // The HD replacement for a picture; one that doesn't fit it is left out, said once
    std::set<std::string> hdRefused;
    bool loadHd(const cr2::Image& img, cr2::Image& hd);
    void reloadTexture(const std::string& name, cr2::Texture& t);
    void reloadTextures();
    static std::string shortDir(const std::string& dir);
    struct SettingRow {
        std::string label, value, help;
        bool enabled = true;
        std::function<void(int)> change;
    };
    bool settingsOpen = false;
    int settingsCursor = 0;
    float settingsStart = 0;
    std::vector<SettingRow> settingRows();
    void settingChange(int row, int dir);
    void openSettings();
    void closeSettings();
    void drawSettings(const Mat4& view, int w, int h);
    bool settingsKey(SDL_Keycode k);
    bool settingsTapped(int id);
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
    // FldMain's own steps around a field event: a balloon's tap (CheckTouchEventExec_Tap, read in
    // step 2 at the start of an update) or an event box walked into (CollisionAdded in the
    // update's physics, CollisionEventExec in step 2 of the next) takes control at once and goes
    // to step 0x1b, where EventExecute calls the function in the following update - pendingCall,
    // called at the end of the next tick (the script then starting in the update after, as an
    // area's entry does). The script's end hands control back in step 1 three updates on
    // (EventExecute sees state 4, then 1000, BranchAfterEvent sets step 1, which gives control
    // once no finger is down; CheckBalloon after it): controlHold, its taps and walk from the fourth
    std::string pendingCall;
    int pendingCallWait = 0;
    int controlHold = 0;
    // (GetFirstSingleFingerEvent: touchUtilUpdate rebuilds its list each update - a press (1), a
    // finger held (2, 0x22) or moved (4), and the update's releases (8, 0x18, 0x48) -, so step 1
    // waits for an update with no finger down and none lifted in it)
    bool pointerDown = false, pointerLifted = false;
    bool fieldControl() const;
    void queueEvent(const std::string& function);
    int fieldExitHold = -1; // (--play: an event's way out to a vista or battle - the updates FldMain spends on it)
    void touchEvents(const cr2::Vec3& p);
    void placePlayer(const cr2::FieldScript::Placement& pl);

    // The balloons over what the leader can tap (FldMain::CheckBalloon, each update it has control:
    // Gimmick::checkLength), the nearest the one a tap talks to (CalcNearestBalloonNum)
    std::vector<size_t> balloons; // (field.gimmicks() indices)
    int nearestBalloon = -1;
    std::string boxesDungeon;               // (the dungeon the boxes were drawn for: "d01", "t01")
    std::map<std::string, float> boxOpening; // (a box's gimmick name -> when it opened: its fa01)
    static uint32_t boxRandom();
    void checkBalloons();
    // The field's camera in mode 0 (FldMap::CalcScroll, each update): the scroll's target is the
    // screen position that centres the leader 0.045 above its feet (FldMain::Update's tail) - or,
    // with the nearer of the nearest balloon and mark a high jump's, a mystic's or an aim's mark
    // (SetPlayerRotationToBalloon's +0x80 / +0x84: kinds 1, 3, 5), the point halfway from the leader
    // to it -, and the scroll moves 0.2 of the way there (0.1 for such a mark: FldMap +0x10), not at
    // all within a pixel. A script's camera (mode 1) sets the scroll outright; a new area's
    // start puts it on the leader (SetMapCameraAtNewArea). Kept in a 960-wide screen's units.
    struct FieldCamera {
        bool set = false, attention = false;
        float cur[2] = {0, 0}, prev[2] = {0, 0}, at[3] = {0, 0, 0};
        float clock = 0;
    } fieldCam;
    int camW = 0, camH = 0; // (the last frame's window: the scroll's units)
    int nearestTarget(float* at, int& kind) const;
    void updateFieldCamera(float dt);
    void startIdleTurn();

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
    bool walkStart(float fx, float fy);
    void walkMove(float fx, float fy);
    void walkEnd();
    bool walkStick(float& sx, float& sy, bool& run) const;
    void drawWalkStick(const Mat4& view);
    struct MenuList { int* cursor = nullptr; int count = 0, context = 0; };
    mobile::MenuSwipe menuSwipe;
    MenuList menuList();
    bool menuScroll(int rows);
    bool pointerCancelled = false;
    void cancelPointerInput();
    crinput::HeldOver padHeldOver; // (the controller: dead after a cancel or a session's end, until let go)
    bool consumeInputCancel(crinput::CancelWatch& cancel);
    bool menuPointerDown(float x, float y);
    void menuPointerMove(float x, float y);
    bool menuPointerUp(float x, float y);
    void pointerActivate(float fx, float fy);
    bool onConversationWindow(float fx, float fy);
    void tap(float fx = -1, float fy = -1);
    void callGimmick(const std::string& function);
    void openBox(const cr2::Gimmick& g);

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
    void startUnlock(int gimmick);
    void unlockEnd(bool success);
    void updateKeyLock();
    bool keyMarkPlace(float& x, float& y);
    bool uiPlace(const float p[3], float& x, float& y);
    void unlockPointer(float fx, float fy, int phase);
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
    static int actionKind(const std::string& n);
    static int actionCharacter(int kind);
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
    bool actionRunning() const;
    void updateActionMark();
    bool actionMarkPlace(float& x, float& y);
    void actionTapped();
    bool actionTap(float fx, float fy);
    void iaiEnd(bool success);
    void iaiPointer(float fx, float fy, int phase);
    bool leaderPlace(float& x, float& y);
    void aimPointer(float fx, float fy, int phase);
    void hakkeiTap(bool onMark);
    void hakkeiPointer(float fx, float fy);
    void mysticTap(int spot);
    void mysticPointer(float fx, float fy);
    void updateIai();
    void drawActionMark(const Mat4& view);
    int sprintAt() const;
    bool sprintTap(float fx, float fy);
    void startSprint(int i);
    void sprintHop();
    void updateSprint(float dt);
    void drawKeyMark(int w, int h);

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
        float fadeAt = -1; // (the hit's wipe begun: uiClock)
        float clock = 0;   // (the hit's updates: their time, and how many have passed)
        int updates = 0;
    } encount;
    std::mt19937 encountRandom{std::random_device{}()};
    void encountSet(const float* p);
    void encountUpdate(const cr2::Vec3& p, bool paused);
    bool encountStep(float dt);

    // FldPlayer::Update's steps: walking one each 12th update (0.6 s), running each 7th, counted from
    // the move's start (FldPlayer +0x28). SetSEAndEffect: the floor's surface under DummyEP04 (a ray
    // from 1 above to 1 below: FldUtilGetFldMapMaterialName), "seNN", plays one of its two SEs at
    // random (the table at 0x53ad0c) and starts its ground effect there (FldGroundEffect: eff001,
    // 002, 004 .. 007, five of each at once, its animator once from its start, unturned)
    int stepState = 0, stepCount = 0;
    float groundClock = 0; // (the field's time: walk()'s)
    // FldMain::SetPlayerRotationToBalloon, each update: with the leader just stopped (SetIdleAnim's
    // m_bIdleTrig, that update's) or a change of leader (ChangeCharacterExecute's +0xa2), turned
    // over 10 updates (FldPlayer::SetDirection) toward the nearer of the nearest balloon and the
    // nearest touch action's mark; a walk begun stops it (StopDirection)
    bool idleTrig = false, changeTurn = false;
    struct IdleTurn {
        bool on = false;
        float from = 0, to = 0;
        int count = 0;
    } idleTurn;
    float nearestBalloonDist = 30000;
    float leaderHeldAt = -1; // (FldPlayer::m_bAnimationStop from the encounter's step 0x1c: the leader's time held)
    struct GroundFx {
        bool on = false;
        float at = 0, p[3] = {0, 0, 0};
    };
    GroundFx groundFx[6][5];
    std::map<int, ViewFigure> groundFigs; // (kind x 5 + slot)
    void stepUpdate(int moving);
    void drawGroundFx(const float* mvp);
    void walk(float dt, float sx, float sy, bool run);
    bool decode(const std::string& name, cr2::Image& img);
    cr2::Texture* texture(const std::string& name, bool mipmaps = true);
    bool loadLights(const std::string& figure);
    void unload(ViewFigure& f);
    void takeModelPictures(std::vector<std::pair<std::string, cr2::Image>>& pictures);
    void replaceFigure(const std::string& name, cr2::Geom& g);
    bool loadFigure(const std::string& name, ViewFigure& f, std::string* err);
    bool load(size_t index);
    void loadAnim(ViewFigure& f, size_t index);

    // A field motion of the character (FldPlayer::CreatePlayer: c<id> + "fn01" idle, "ff01" walk,
    // "ff02" run, "fj01".. jumps, "fp01".. its own), if it has it and isn't playing it already
    // A character's shadow (FldShadow: obj001 on its own animation, given the character's place -
    // FldPlayer::Pose - and drawn before it in the transparent pass): the leader's, and an event's
    // c / v models' (EvtModel::Init) unless EvtScene.ShadowVisible hid it
    ViewFigure shadowFig;
    bool shadowTried = false;
    void drawShadow(const cr2::Mat34& place, const float* mvp);
    std::string leaderMotion; // (a script's: held while the leader stands)
    int leaderMotionSerial = 0;
    void playMotion(ViewFigure& f, const char* motion);
    std::string animStatus() const;
    void pose(ViewFigure& f, float t, bool loop = true);

    // The camera the figures are posed for (its place in the world): billboards turn to it
    cr2::Mat34 poseView;
    float modelEye(float* eye) const;
    void billboard(ViewMesh& vm, const cr2::Mat34& p0);
    void pose();
    void drawFigure(const ViewFigure& f, const float* mvp);
    void drawLayer(const std::string& image, const Mat4& proj, float shiftX = 0);

    // The output: a chosen resolution (0 x 0: the window's), and the temporal upscaler (Vulkan)
    int resW = 0, resH = 0;
    cr2::UpscaleSettings upscale;
    int screenX = 0, screenY = 0, screenW = 1, screenH = 1; // the game's screen in the frame: the UI's, inside the display's safe area
    int frameX = 0, frameY = 0, frameW = 1, frameH = 1;     // the frame the scene fills: under a camera hole, into the corners
    void draw(int fw, int fh);
    void fullFrameQuad(const cr2::Texture& t, const float* v, const float* rgba);
    void safeInsets(int fw, int fh, int& ix, int& iy) const;
    void placeScreen(int fw, int fh);
    void drawScene(int w, int h);

    // The maps' table from the game's executable (--binary, or beside the iPad's / Vita's archive)
    std::vector<cr2::MapInfo> table;
    std::string tableFrom;
    bool exitsArmed = false; // (an exit counts once the character has left them all since arriving)

    std::string dataArg, binaryArg; // (the release's: the staff roll's table from its executable too)
    bool loadTable(const std::string& binary, const std::string& dataPath);

    // the gimmicks shown with a figure: placed by their joints, NPCs idling (FldNPC: <figure>fn01) -
    // as the script has them now
    // the resident effects the script played (FldEffectResident: its figure on the leader - its
    // place and facing - or a gimmick's, scaled, its animator once from its start)
    std::map<int, ViewFigure> residentFigs;
    std::map<int, int> residentSerial;
    void drawResidents(const float* mvp);
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
    float fidgetWait();
    Facing leaderFace;
    bool gimmickTarget(const std::string& target, float& x, float& z) const;
    static float turnBetween(float from, float to, float a);
    static float directionShare(float u, int n);
    void refreshGimmicks();

    // A map: its table row, layers and figures, the character dropped at the point `at` (empty:
    // where it stood, the field re-entered after a vista or a battle), the area's `entry` started
    std::string fieldArrival; // (the point the leader last came onto this map at: its pop point, or the map's name)
    bool enterMap(const std::string& map, const std::string& at, const std::string& entry = "evt000");
    bool loadField(const std::string& map, const std::string& at, const std::string& binary, const std::string& dataPath,
                   const std::string& entry = "evt000");

    // The field's script, a coroutine stepped at the game's 20 updates a second (its events'
    // scene, windows and fades with it); the leader held while it runs, as FldMain holds it
    float fieldClock = 0, pageSeconds = 3.0f;
    bool fieldScriptRunning() const;
    void fieldInput();
    void fieldUpdate(float dt);

    // A frame's time: the vista's event, else the field (its script, then the leader - held while
    // the script runs)
    float uiClock = 0; // (the menus' seconds: the options', the manual's, the camp's - in a battle or out)
    // the fast-forward button's place as last drawn (id 0: not shown), whether it is held (the
    // mouse on it, or F), and --input fast's two seconds of it
    struct { int id = 0; float x0 = 0, y0 = 0, x1 = 0, y1 = 0; } fastRect;
    bool fastHeld = false;
    float fastUntil = -1;
    bool overFast(float fx, float fy) const;
    void advance(float dt, float sx, float sy, bool run);
    cr2::EventRuntime* activeEvents();

    // --play: the game's flow - the field's entry, then what it reserved (GameState::next):
    // another area (SetPopPoint), a battle, a vista event (FldMain::BranchAfterEvent); after a
    // vista, its battle, another vista, else the field again (VistaMain::BranchAfterVista: evt200
    // after SetComeFromVistaToField); after a battle the field (evt100). Battles aren't in yet:
    // one is taken as won.
    bool playMode = false;
    std::string returnMap;
    float returnPlayer[3] = {0, 0, 0}, returnHeading = 0;
    void branchAfterField();
    // a battle the flow reached: fought (its party SetBattleParty's, else the party now; the party
    // attacking as a pair each turn), then on as the game goes after it (BtlMain::Exit) - or,
    // with --skip-battles, taken as won at once
    bool skipBattles = false;
    void skipBattle();
    bool fightingInFlow = false, randomBattle = false; // (randomBattle: an encounter's)
    bool standaloneOut = false; // (a --battle fight's way out under way: then the end)
    // The battle's way out (BtlMain::NextMode - StepBattleOut's, Cr3BattleEvent_Exit's - then
    // BtlMain::UpdateStep): step 1, the update after, Cr3Fade's FadeOut over 20 updates - white
    // (colour 1), black once the game over reloaded (BtlWork +0x1a), none for coupling 282 - with
    // the music's StopBgm(20); step 2 counts past 1.0 s (20 updates: twenty 0.05f sum to 1.0000001)
    // and releases the music; 3 waits for the fade and ends the scene (Term: BtlModel::Cleanup's
    // Cr3ResourceManager::Unload queues each resource for three Procs, one at each update's end); 4
    // waits for the queue to empty - the third update after Term - and makes the next main (Exit),
    // which GameMain runs from the update after - the field's evt100 (its Battle_FadeIn: white over
    // 20) 25 updates after the battle was over. The battle's scene goes on under the fade
    // meanwhile (BtlMain +0x18 till Term)
    // (the original's waits sum its fixed 0.05f frame time: 2.0 s is reached in the 41st update
    // (forty make 1.9999998), 3.0 s in the 61st; 0.5, 1.0 and the shorter ones exactly - round 52)
    static constexpr float kWait2s = 41 * cr2::EventRuntime::kTick, kWait3s = 61 * cr2::EventRuntime::kTick;
    static constexpr float kBattleOutSeconds = 25 * cr2::EventRuntime::kTick;
    float battleOutAt = -1;
    void beginBattleOut();
    void leaveBattle();
    void afterBattle();
    int battlesSkipped = 0;
    void branchAfterVista();

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
    void startCredits(int n);
    std::string creditFigure() const;
    float creditScale() const;
    bool creditsLineShown(size_t i) const;
    void updateCredits(float dt);
    void drawCredits();
    void returnToField(int mode);

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
    void setBattleCamera(const std::string& name, const cr2::Mat34& place = cr2::Mat34(), bool still = false);
    bool battleCameraLoop = false; // (BtlCameraCtrl::SetLooping)
    void setBattleCamera(int number);

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
    void readSystem();
    void applySystem();
    void writeSystem();
    void resetForNewSession();
    void newGame();
    bool startBattle(int id, const int party[2], int area = 0);
    bool battleView(cr2::Mat34& world, float& fovY, float& zn, float& zf);
    bool useMotion(BattleUnit& u, const std::string& suffix);
    float motionLength(BattleUnit& u, const std::string& suffix);
    void playMotion(BattleUnit& u, const std::string& suffix, float at, const std::string& then = "bb01");
    std::string stanceOf(const BattleUnit& u) const;
    int fighterOfUnit(const BattleUnit& u) const;
    // BtlUtilStatus::CheckActionReady: a fighter back in its stance - its motion bb01, bb04, bg01 or
    // bx02, not one still playing out (true for one without a figure)
    bool fighterReady(int f);
    std::vector<cr2::BattleRules::Event> turnEndNumbers; // (the turn's end's poison and pools, shown at BtlTurnEnd's step 3)
    float turnEndNumbersAt = -1;
    float actionDueAt = -1; // (ActionCommonStandby's wait for it began; -1 none)
    std::string actionWaitedOn; // (for the log)
    void poseBattle();

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
    // cameras later in an action (seconds into it): an item's and a sopia's on the one it is used on
    // (ActionItemWaitLaunch / ActionSopiaWaitLaunch -> SetTargetCamera(false)), a sopia's caster's
    struct TimedCamera {
        float at = 0;
        std::string name;
        cr2::Mat34 place;
    };
    std::vector<TimedCamera> timedCameras;
    bool actionSopiaTable = false; // (the action goes by action_table_sopia)
    bool showSopia = false; // (action_table_sopia's waits: till the hit's effect has played out)
    std::vector<int> sopiaStruck; // (the ones it strikes, each once)
    float sopiaCast = 0, sopiaLaunch = 0; // (seconds into the action: the caster's camera, the launch)
    bool sopiaMirrored = false, sopiaVoiceWait = false;
    bool hitEffectEarly = false; // (the hit's effect drawn at the launch, not with the numbers)
    bool useTargetCamera(const ShowAction& a, const cr2::BattleData::Command* c, std::string& name, cr2::Mat34& place, int forced = -1, bool onCaster = false);
    bool showHitDone = false, showItem = false;
    std::set<int> itemCommands; // (the items' commands, +0x2e: an item's use goes by action_table_item)
    bool itemCommand(int command);
    struct Move {
        int unit = -1;
        cr2::Mat34 to;
        float start = -1, end = -1, back = -1, backEnd = -1;
        bool active = false;
    } showMoves[2];
    bool aposPlace(const std::string& name, cr2::Mat34& out);
    std::string aposName(int actor, int target);

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
    bool focusScreen() const;
    bool focusKey(SDL_Keycode k);
    bool tabFocusKey(SDL_Keycode k);
    bool listShown() const;
    bool inMenu() const;
    bool switchList() const;
    SDL_Keycode padButtonKey(SDL_GamepadButton b) const;
    bool gameKey(SDL_Keycode k);
    void menuChanged();
    void setTargetCamera(int f);
    const cr2::Mat34& targetFrame(BattleUnit& bu);
    void inputToken(const std::string& t);
    std::vector<int> menuChoices() const;
    void choose(int idOrFighter);
    void battleSe(int n);
    struct VoiceJob {
        float at = 0;
        int clip = -1;
        bool command = false; // (the action's own: PlayVoiceCommand - ActionSopiaWaitVoice waits on it)
    };
    int commandVoiceSlot = -1;
    std::vector<VoiceJob> voiceJobs;
    void battleVoice(int f, int kind, float delay = 0, bool tableKind = false);
    void partyVoice(int kind, bool tableKind = false, float delay = 0);
    int partyDeaths = 0, lastPartyActor = 0;
    // (CharaStatusWindowMenu / BMenu::Initialize: the name plate by the option's language - Japanese
    // pcNameJP_id, the two Chineses pcNameCHN1 / 2_id, Korean pcNameKOR_id, all at campwin's
    // call_pcNameJP_id..22; else - English, French, German, Spanish - pcNameEN_id at ..21; the
    // character's frame the same in each)
    struct NamePlate {
        std::string figure, joint; // (the joint's start: + the place's digits + its end)
        std::string end = "21"; // (campwin's: call_pcNameEN_id021, call_pcNameJP_id022 ...)
    };
    NamePlate namePlate() const;
    int faceState[2] = {0, 0};          // (the HUD's faces: 0 as they are, 1 choosing, 2 down)
    float faceStateAt[2] = {0, 0}; // (BtlWork fighters' +0x48e, the deaths so far; +0x16, the last of the party to act)
    std::vector<VoiceJob> seJobs; // (BtlSound::CreatePlaySEJob: an effect's clip later)
    void cancelSceneAudio();
    void updateBattleSounds();
    int pendingBgm = 0;
    float pendingBgmAt = 0;
    // a turn break beginning (BtlTurnEnd: the break due, +0x1f 3 -> 1 / 2): SE 2, then 1 s on 3 for
    // the party's, 4 the monsters'; turnBrk / turnBrkEnemy (Battle / EnemyBattleBreakEffectMenu)
    // played once over the HUD
    int breakShown = 0;
    float breakShownAt = 0;
    void battleTutorial(const char* function, int flag);
    bool keysUsed = false; // (a command button's focus shown once a key moves it: a touch shows none)
    bool optionsKey(SDL_Keycode k);
    void volumeSet(int bar);
    bool optionsTapped(const TapRect& r, float x);
    void battleKey(SDL_Keycode k);
    void battleClick(float fx, float fy);
    BattleStep battleStep = BattleStep::Over;
    bool exploreOrders(bool& pair, std::vector<cr2::BattleRules::Order>& out);
    std::unique_ptr<cr2::BattleScript> battleScript;
    float battleScriptClock = 0;
    bool battleScriptOk = true;
    void updateBattleScript(float dt);
    bool battleScriptRunning() const;
    bool ownBattleScript() const;

    std::vector<std::pair<int, int>> stateArg; // (--state fighter:state)
    int chargeArg = -1;                        // (--charge: the gauge to begin with)
    int gaugeArg = 0;
    void startFight(int mode, uint32_t seed);
    bool dposPosition(const char* name, float* out);
    void placeParty(bool pair);
    void stepBattleShow();

    // the HUD's as the show has it: until an action's hits land, the HP, the break gauge and the
    // charge before it (the windows change with the hit, as the reference video's)
    struct HudValues {
        std::vector<int> hp;
        int gauge = 0, charge = 0;
    } hudBefore;
    bool hudHeld() const;
    int shownHp(int f) const;
    // state 33's revivals still to show, in turn (BtlActionCtrl: ActionReviveStandby - the
    // announcement 0x5d, "Regenerate" -, ActionReviveCamera - Cam_Unit_PCrevival_00 at the one
    // revived -, ActionReviveMain - its motion 8 (bh01), its HP back in green at DummyDP01, eff216 -,
    // ActionReviveWait - the motion's end)
    std::deque<int> regenPending;
    bool regenStarted = false;
    void showRegen();
    int shownGauge() const;
    int shownCharge() const;
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
        float start = 0, windowStart = 0;
        bool tapped = false;
        int exp = 0, sp = 0, oz = 0;
        ResultMember member[2];
        std::vector<std::string> messages; // (once the EXP is in: step 16's, after the level-ups)
        std::vector<int> messageSe;        // (each one's SE as it shows: BtlResult::Update's by its kind)
        std::string levelLine[2];          // (ProcessLvup1 / 2: a member's levels gained, if any)
        std::string text;                  // (the window's line: 0x10 "Victory!" first)
        int loopSe = -1, levelShown[2] = {0, 0}; // (the transfer's SE 0xc5, looping; the levels chimed for)
        float windowAt = -1;               // (the window's update: two after the tap - steps 7, 8, then 9)
        float closeAt = -1;                // (HideBattleResult: each part backwards from where it stands)
        int step = 0, count = 0, sub = 0;  // (the step table's index; ProcessLvup's count, +0x170; step 16's, +0xc)
        float timer = 0, nextTick = 0;     // (+0x20: the time since the last reset; the next update)
    } result;
    // BtlResult's steps after the victory (its table at 0x5bf95c, from step 8): one each game
    // update (20 a second), the next taking effect the update after
    static constexpr int kResultSteps[] = {8, 9, 10, 11, 12, 7, 8, 13, 7, 8, 14, 7, 8, 15, 7, 8, 16, 17, 18};
    static constexpr int kLvupCount = 60; // (ProcessLvup: 60 updates, 3 s)
    // the battle's announcements (BtlAnnounceData, message\announcebattle.dat: u32 the records'
    // offset, the texts', the count; a record 0x20 bytes - eight languages' text offsets)
    // - and the names', the same: message\\charaname, sopianame, commandname, autoskillname,
    // itemname (by number, from 1)
    std::map<std::string, std::vector<uint8_t>> messageFiles;
    std::string announce(int id);
    std::string localText(const char* file, int id, const std::string& fallback);
    std::string itemName(int id);
    std::string itemHelp(int id);
    std::string commandName(int id);
    std::string commandHelp(int id);
    std::string sopiaName(int id);
    std::string sopiaHelp(int id);
    std::string autoSkillHelp(int id);
    std::string fighterName(int i);
    static size_t messageLanguages(const std::vector<uint8_t>& d, size_t extra = 0);
    int copyLanguages();
    std::string messageText(const std::string& file, int id);
    static std::string formatAnnounce(std::string t, const std::vector<std::string>& args);
    void finishResult();
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
        float hitAt = 0; // (the blow that downed it: CheckDead, in Hit / Damage)
        float scriptUntil = -1; // (the Dead call's wait, with the battle's own script)
    };
    std::vector<Death> deaths;
    bool deathsQueued = false;
    void deathEffect(const char* name, int f, BattleUnit& u);
    bool stepDeaths(bool collect = true);

    // Analyze (command kind 26; BtlActionCtrl::ActionSopiaWaitEffect -> ActionSopiaAnalyze): its
    // effect over, the one struck's "HP: %d / %d" on the banner until a tap, then its MonsterHelp
    // (the monster's number), until a tap - 5 s each for a fight nobody taps
    struct {
        int stage = 0; // 1 the HP, 2 the help, 3 done
        float start = 0;
        bool tapped = false;
    } analyze;
    bool stepAnalyze();
    void beginResult();
    int maxLevel() const;
    // a member's EXP, level, next and HP as the window has them now
    struct ResultNow {
        int exp, level, next, percent, hp;
    };
    ResultNow resultNow(const ResultMember& r) const;
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
    void beginGameOver();
    void endGameOver(bool title);
    void updateGameOver();
    void gameOverAnswer(bool yes);
    void drawGameOver(const Mat4& view, int w, int h);
    int finisher() const;
    void updateResult();
    void resultStep(bool tapped);
    void nudgeNumber(int f, bool turnEnd, float* p) const;
    void showNumber(int f, int value, int set, bool turnEnd = false);
    void beginShowAction(const ShowAction& a);
    void poseBattleShow();

    // the fight's time going on: the next action (or turn), the hits as they land
    std::deque<std::string> announceQueue;
    float announceAt = -10;
    static constexpr float kAnnounceStay = 21 * 0.05f - 1e-4f; // (an entry's 1.0 s and its first update)
    float openingBannerAt = -1, openingBannerShownAt = -1, openingTurnAt = -1; // (StepBattleIn's banner and first turn)
    void updateBattleShow();

    // an action's hits landing (the targets' damage motion, the sounds, the hit's effect, the
    // numbers); a special scene's later hits (`first` false) the blows alone
    std::vector<int> groupShown; // (the sides a group's effect has had this action)
    void throwBack(BattleUnit& bu, const cr2::BattleRules::Event& e);
    void landHits(const ShowAction& a, bool first);

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
        float early = 0; // (its time-0 commands that much before: ActionAwakeStandby's, before the motion's sync)
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
    BattleUnit* unitOf(int f);
    static void unscaled(cr2::Mat34& m);
    bool jointPlace(const ViewFigure& f, const char* joint, cr2::Mat34& out);
    cr2::Mat34 scenePlace(int target, bool apos, bool dpos, int tpos);
    static cr2::Mat34 turnY(float degrees);
    static void setRotation(cr2::Mat34& m, const cr2::Mat34& from);
    bool startScene(const ShowAction& a, const cr2::BattleData::Command& c);
    SceneEffect* sceneEffect(int id);
    void runStep(const cr2::SpecialStep& st);
    void updateScene();
    bool skipScene();
    void endScene();
    bool drawEffect(SceneEffect& fx, const cr2::Mat34& camera, const Mat4& vp);
    void drawSceneEffects(const cr2::Mat34& camera, const Mat4& vp);
    // Defend (BtlCommand::StepGuard, as it's chosen): the one choosing - a pair's both - to its
    // stance bg01 (motion 4), and character 6 its guard, obj303, on its model 0.2 s on
    // (BtlEffectCtrl::CreateEffect(.., 4, hidden, 0.2): ControlWait, then no control - kept); its
    // choice taken back (StepCommandLoop), the stance and obj303 gone. The guard hidden as it
    // attacks (HideAttachObject: a counter), made again 0.5 s after (ShowAttachObject); gone as it
    // falls (ActionDeadStart) and with the turn's effects (BtlTurnEnd: DeleteAll)
    std::unique_ptr<SceneEffect> guardObjects[2];
    bool guardReturn[2] = {false, false};
    void guardObject(int f, float delay);
    void dropGuardObject(int f);
    void setGuard(int f, bool on);
    void updateGuards();
    // A command's effect (BtlActionCtrl::CreateEffect - its settings' delay - and
    // SetCommandEffectPosition with 9): on the fighter `target` - following its model at the
    // setting's locator (a pair's action's +0x32) while the setting follows the position, else
    // put there once; `side` >= 0: a group's (HitGroupEffect), once at that side's middle
    std::vector<SceneEffect> commandEffects;
    bool hitEffectPlaying() const;
    // a launch effect at the end of a physical attack's lead (ActionAttackWaitAttackMotion:
    // ShowCommandLaunchEffect, CreateWarpEffect), else at once
    struct PendingEffect {
        std::string name;
        int target = -1;
        bool pair = false;
        float at = 0;
        int side = -1, se = -1; // (a side's middle; a sound with it)
    };
    std::vector<PendingEffect> pendingEffects;
    float launchLead = 0;
    void launchEffect(const std::string& name, int target, bool pair);
    void runPendingEffects();
    void commandEffect(const std::string& name, int target, bool pair, int side = -1);
    void drawBattle(int w, int h, bool mapOnly = false);
    void glassSize(int& hw, int& hh) const;
    void grabGlass(int fw, int fh);
    void drawGlass();

    // The battle's effects (BtlEffectCtrl / BtlEffect: an effNNN figure playing its animation once,
    // where it was made - turned to the camera, its quads facing it)
    struct BattleEffect {
        std::unique_ptr<ViewFigure> fig;
        float start = 0, length = 0;
        float at[3] = {0, 0, 0};
    };
    std::vector<BattleEffect> battleEffects;
    void spawnEffect(const std::string& name, const float at[3]);
    void drawEffects(const cr2::Mat34& camera, const Mat4& vp);

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
    int eventEndHold = 0, eventEndTotal = 0; // (--play: the script ended - updates till the branch after the vista)
    bool startEvent(const std::string& function, const std::string& map, float page);

    bool eventDrawingReady = false;
    void initEventDrawing();
    bool startVista(const std::string& function, const std::string& map);
    void stepEvent();
    void syncEventFigures(cr2::EventRuntime& ev);
    float eventLead() const;
    void updateEvent(float dt);
    void poseEvent();
    void poseEventModels(cr2::EventRuntime& ev);
    void drawEvent(int w, int h);

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
        uint64_t used = 0;                // (uiTexts: when last drawn)
    };
    std::vector<TextQuad> textQuads;
    std::string shownText; // (what textQuads were made for)
    int shownPx = 0;
    void loadWindow(WindowLayout& w, const char* figure);
    void loadMessageWindow(const std::string& fontPath, std::vector<uint8_t> fontBytes = {}, const std::string& fontName = {});
    static Mat4 uiProjection(int w, int h);
    bool windowJoint(const WindowLayout& wl, const std::string& name, float& x, float& y);
    std::string slotJoint(const WindowLayout& wl, size_t slot);
    void drawWindow(WindowLayout& wl, bool showsMessage, int w, int h);

    // The cut-ins' portraits (CutInWindowMenu): `cutIn`, its first material's colour map the face
    // (SetTexture), its DiffuseColor the brightness (ChangeDiffuseColor), posed by the layout's io
    // animation; the other side's joints hidden (GEO_face*_L or _R); on screens wider than 3:2
    // moved toward its side by the margin's width over the 3:2 area's (PositionCorrection: the fit
    // parameter's side - 3 / 6 right, 1 / 4 left - the two-sided layout not moved), narrower ones
    // 0.0403125 lower. Behind the conversation window (their depths: -0.064 against -0.048).
    std::array<ViewFigure, cr2::EventRuntime::kCutIns> cutInFigures;
    void loadCutIns();
    void drawCutIns(int w, int h);

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
    UiFig& uiFig(const std::string& name);
    void poseUi(UiFig& u, const std::string& anim, float t, bool loop = false);
    static float uiRight(const UiFig& u);
    bool uiJoint(const UiFig& u, const std::string& joint, float& x, float& y);
    // DotNumId2: a number's digits from hud_dotnum (DotNumParamPackTable: set `set`'s glyph d at
    // set + d, its size w x h pixels of a 1000-a-unit layout and its UVs), right to left from
    // (x, y), `digits` places (the leading ones blank, but a 0)
    struct Glyph {
        float w, h, u0, v0, u1, v1;
    };
    std::vector<Glyph> dotNumGlyphs;
    void loadDotNum();
    void drawDigits(int value, int set, int digits, float x, float y, const Mat4& proj, float alpha = 1, float scale = 1, bool zeros = false, int skip = 0);
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
        std::string notice; // (CR2-U's: told once the buttons are back - a save that couldn't be read)
    } titleMenu;
    std::unique_ptr<cr2::EventRuntime> titleEvents; // (title.nut's calls)
    double playSeconds = 0;                         // (the save's play time, cr3_savedata +0x10 / 20)
    static bool saveExists(const char* file);
    // a save's flags, its time and play time (its extras)
    struct SaveInfo {
        bool exists = false;
        std::set<int> flags;
        std::time_t saved = 0;
        long playFrames = 0;
    };
    static SaveInfo saveInfo(const char* file);
    void resetSessionUi();
    void startTitle();
    bool titleRunning() const;
    void titleScript(const char* function);
    float titleAnimLength(const std::string& fig, const std::string& anim);
    bool carryOver();
    void titleLoadFailed();
    void titleAsk(int id);
    void titleButton(int id);
    void titleAnswer(bool yes);
    void titleToken(const std::string& tok);
    void titleTap();
    bool titleClick(float fx, float fy);
    void updateTitle(float dt);
    void drawCalendar(const SaveInfo& s, const Mat4& view, const Mat4& proj, float t);
    static void showJoint(UiFig& u, const std::string& joint, bool visible);
    static void hideJoint(UiFig& u, const std::string& joint);
    void focusLook(UiFig& c, int id);
    void litLook(UiFig& c, bool on);
    void drawTitleButtons(UiFig& parts, const Mat4& view, int skip);
    void drawTitle();
    void drawEnchant(int sign, float x, float y, const Mat4& proj);
    void drawStatusSign(int n, float x, float y, const Mat4& proj);
    static int elementSign(const cr2::Fighter& f);
    static std::vector<int> stateSigns(const cr2::Fighter& f);
    // a line of text at a UI place (centred), made once
    // (every string at every size: past the limit the ones not drawn for longest go, their textures with them)
    static constexpr size_t kUiTextLimit = 1024;
    std::map<std::string, TextQuad> uiTexts;
    uint64_t uiTextUses = 0;
    float uiTextWidth(const std::string& text, float size, int w, int h);
    // (its width, UI units: 0 none drawn)
    float uiTextTint[3] = {1, 1, 1}; // (a colour over the text drawn: SetStringData's own, as the worn equipment's yellow)
    // (the text drawn bordered: MenuText::Initialize's 1 / 2 - the lists' rows, the circle buttons'
    // names, the pub's texts, the staff roll -, TextRenderer::SetBorderingEnable 2 on Android and
    // the Vita - 1 on the iPad -: the shadow 2 texture pixels, 0.002, and the line placed 0.002 left
    // and up, MenuText::SetPosition)
    bool uiTextBorder = false;
    float drawUiText(const std::string& text, float size, float x, float y, int w, int h, bool left = false, float shade = 1, float maxWidth = 0);
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
    bool battleUi(const float* p, float& x, float& y) const;
    std::vector<DamageShown> damageShown;
    std::string commandShown;
    float commandShownStart = -10;
    bool announceKept = false; // (the turn's actions running: the window not hidden as its text runs out)
    float chargeShown = 0, chargeShownAt = 0; // (the charge gauge's time shown)
    void drawCommandMenu(const std::string& anim, float t, const Mat4& view, const std::vector<int>& shown,
                         const std::map<int, std::string>& labels = {}, int w = 0, int h = 0, int frameOf = 0,
                         const std::string& figure = "btlbtn");
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
    static void sopiaRowIcon(const cr2::BattleData::Sopia& so, ListRow& r);
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
    void drawSkillList(const std::vector<ListRow>& rows, int focused, float t, const Mat4& view, int w, int h, int tagFrame, int costIcon);
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
    bool titleOptions() const;
    void resetData();
    void optionTap(int id);
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
    void openManual();
    void closeManual();
    void playManual(int k);
    bool playTutorial(const char* function, int id, bool toList);
    void updateManual(float dt);
    void drawManualList(const Mat4& view, int w, int h);
    void drawOptionsAsk(const Mat4& view, int w, int h);
    vitaimport::Session saveImport;
    void saveImportAnswer(bool yes);
    int optionVolume[3] = {100, 100, 100}, optionFocus = 0; // (0..100: GetOptionWork's time the volume / 20)
    void drawOptions(const Mat4& view, const Mat4& proj, int w, int h);
    void drawChoosing(const Mat4& view, int w, int h);
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
    void drawStatusWindows(const StatusShown* rows, int count, float t, const Mat4& view, const Mat4& proj, bool twoAlways = false);
    void drawResult(const Mat4& view, const Mat4& proj, int w, int h);
    void drawBattleHud(int w, int h);
    void drawBattleHudParts(int w, int h);
    void drawBattleHudMain(int w, int h);
    int dungeonNum() const;
    int areaNum() const;
    // The dungeon's map (FldDungeonMap -> DungeonMapMenu, interface 0x67): mapGuide_io with the
    // dungeon's guide_<t01 | dNN> on it; mapInfo with mapInfo_<..> at the area + 1 / 20 s, mapSymbol
    // (looping) at its call_mapSymbol - where the party is; the goals - each of the dungeon's
    // entries of the table at 0x535794 (from 0x535774's start) whose flag is set, mapGoal at
    // call_mapGoal with mapInfo at its place within the dungeon / 20 s. A tap closes it (0xb5).
    bool mapOpen = false;
    float mapStart = 0;
    void drawDungeonMap(const Mat4& view);

    // Saving (FldSaveLoad::ExecuteSave -> SaveConfirmationMenu, 0x4b, with the party's status
    // windows): "menu" with menuSave_io, the question (announce 0x69) - yes: 0xbd, "saved" (0x6a),
    // the game's data written (Cr3Util_Save_GameData; here CR2-U's own save, GameState::save, with
    // the map and the place, in the user's folder for the app - `--continue` resumes it), closed
    // after a second
    int saveStage = 0; // (1 asked, 2 saved)
    float saveStart = 0;
    bool saveFailed = false;
    static std::string savePath(const char* file = "save1.txt");
    // the scripts' saves (Cr3Save, FieldScript::onSave): the game's as the field's Save does, the
    // continue data (each area's entry - cont.txt, which --continue takes first), that emptied -
    // only as a game is played (--play / --continue, not a capture's)
    bool scriptSaves = false;
    void scriptSave(int kind);
    bool writeSave(const char* file = "save1.txt");
    void restoreFieldPosition(const float* position, float yaw);
    bool continueSave(const std::string& binary, const std::string& dataPath, const char* only = nullptr);
    bool loadGameSave(const std::string& path, const std::string& binary, const std::string& dataPath);
    void saveAnswer(bool yes);
    void drawSave(const Mat4& view, const Mat4& proj, int w, int h);
    bool encountersOff = false; // (cr3_field_data +0x31: the tab's 5th, FldMain step 8 / 9)

    // The field's tab (FldTabMenu -> FieldMenuTabMenu: fldMenu_tab, over the field while nothing
    // runs): its own animation played forward to open it, back to close it (Cr3UtilSoundPlaySE
    // 0xba / 0xbb); its tap areas 01 the tab, 02 Camp, 03 the map, 04 Save, 05 the encounters'
    // switch - each but the tab only once it is open (CheckOpenMenu), 0xb4 as one is pressed. Camp
    // opens the camp (FldMain step 5, FldCamp); the others are not here yet.
    bool fieldTabOpen = false;
    float fieldTabAt = -10; // (uiClock: when it last began to open or close)
    bool fieldTabShown() const;
    float fieldTabPose(UiFig& tab);
    void drawFieldTab(const Mat4& view);
    void fieldTabTap(int area);

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
    void openCamp();
    void closeCamp();
    std::vector<ListRow> sopiaSkillRows(int s);
    int campSopiaDetail = 0, campSopiaPrevCursor = 0; // (CampSopiaListMenu: a sopia picked - its detail list)
    void campSopiaPick(int s);
    std::vector<ListRow> campRows();
    StatusShown statusOf(int ch) const;
    void drawCamp(const Mat4& view, const Mat4& proj, int w, int h);
    int campCursorButton() const;
    std::vector<int> validChars() const;
    cr2::BattleRules::PlayerStats statsOf(int ch) const;
    int mpOf(int ch) const;
    // a question on the system window (syswin_io's typeM_L01) with choice_io's Yes / No (call_cBtn_fr11 /
    // 12: circleBtn_fr at those frames) - their taps `yes` / `no`
    // An event's yes / no buttons (EvtWindow.ShowYesNoButton: BattleYNChoice - choice_io, its
    // circleBtn_fr 11 yes, 12 no; a tap SE 0xb4): their tap areas kept for a click
    struct YesNoRect {
        bool yes;
        float x0, y0, x1, y1;
    };
    std::vector<YesNoRect> yesNoRects;
    void drawYesNo(cr2::EventRuntime& ev, int w, int h);
    void answerYesNo(bool yes);
    bool yesNoShown();
    // A question's Yes / No (BattleYNChoice - the System window's questions and the events': its
    // update FUN_8107beec): Yes in focus as it opens, Up / Down the other (FUN_810452ba: round, SE
    // 0xb3), ○ the one in focus, × No; the focus lit (CircleBtn::SetForcusTarget) once a key has
    // moved it - a touch shows none. (The title's and the save's - TitleYNChoice - keep their own.)
    int askFocus = 0; // (0 Yes, 1 No)
    int askWas = 0;   // (the question shown when last looked: askShown's)
    int askShown();
    void noteAsk();
    bool askLit(bool no) const;
    bool askKey(SDL_Keycode& k);
    bool yesNoClick(float fx, float fy);
    void drawAsk(const std::string& message, float t, const Mat4& view, int w, int h, int yes, int no, bool titleStyle = false, bool notice = false);
    void drawStatusB(int ch, int hpChange, int mpChange, float t, const Mat4& view, const Mat4& proj);
    void drawCapacity(int ch, const int* change, float t, const Mat4& view, const Mat4& proj, int w, int h);

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
    void campUseUpdate();
    void showRaised(int ch, const cr2::BattleData::Item& it, int n);
    void useItemOn(int ch, int item, int n);
    void campUseTap(int id);
    void drawItemUse(const Mat4& view, const Mat4& proj, int w, int h);
    void campPickItem(int item);
    void drawSopiaWindows(int sopia, float t, const Mat4& view, const Mat4& proj, int w, int h, bool detail = false);

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
    std::string menuName(int id);
    std::vector<EquipRow> equipRows();
    std::vector<int> humanSopia() const;
    void campEquipPick(int id);
    void exploreOutfit();
    void clampMp(int ch);
    void campTakeAnswer(bool yes);
    void campEquipBack();
    void campEquipView();
    void campEquipChange(int dir);
    void drawEquipment(const Mat4& view, const Mat4& proj, int w, int h);

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
    void scrollScenarios(float dy);
    void drawScenarios(const Mat4& view, const Mat4& proj, int w, int h);
    void drawPubRequest(const Mat4& view, int w, int h);
    void campPress(int id);
    void campKey(SDL_Keycode k);
    bool fieldClick(float fx, float fy);
    bool fieldToken(const std::string& t);
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
    uint64_t storyKey() const;
    void exploreLog(const std::string& line);
    std::string explorePhase() const;
    void exploreGrid();
    bool explorePath(const std::string& target, const cr2::Vec3& goal, float reach, std::vector<cr2::Vec3>& out);
    bool explorePlan();
    bool explorePick();
    void exploreStep(float& sx, float& sy);
    double exploreShotDue = -1;
    double exploreStuckSeconds = 900; // (CR2_EXPLORE_STUCK: seconds)
    std::string exploreShotName;
    // The leader's figure (FldPlayer::CreatePlayer: c + its character's 001..007), the field's
    // character the script has leading - unless --model gave one
    bool leaderFollows = false;
    void refreshLeader();
    // The character change (FldChangeCharacter: CharaTradeMenu's chrTrade button, shown in the
    // field once allowed - Cr3ChangeCharacter.SetEnable - and flag 0x3fb set, the second of the
    // two there; its tap SE 0xb4): FldMain::ChangeCharacterExecute - the leader darkened over 16
    // frames (its materials' diffuse 1 - n / 16), then the other in its place (its idle), lit over
    // 32 (n / 32)
    int changeStage = 0; // 0 none, 1 out, 2 in
    float changeFrames = 0;
    bool changeButtonShown() const;
    void startChange();
    void updateChange(float dt);
    void drawChangeButton(const Mat4& view);

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
    std::vector<ListRow> worldRows();
    void openWorldSelect();
    void openChapterSelect();
    static bool worldPair(const cr2::GameState& g, int dungeon, int pair[2]);
    void worldPick(const ListRow& r);
    void worldBack();
    void worldAnswer(bool yes);
    void worldTap(int id);
    void worldKey(SDL_Keycode k);
    void drawWorldSelect(const Mat4& view, int w, int h);

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
    std::vector<ListRow> libraryRows();
    void openLibrary();
    void libraryPick(const ListRow& r);
    void libraryBack();
    void libraryTap(int id);
    void libraryKey(SDL_Keycode k);
    void drawLibrary(const Mat4& view, int w, int h);

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
    std::vector<int> pubPosters() const;
    int pubListCount() const;
    int pubSign(int id) const;
    void openPub();
    void pubEnd(int result);
    void pubTap(int id);
    void pubMove(SDL_Keycode k);
    void pubKey(SDL_Keycode k);
    void drawPub(const Mat4& view, int w, int h);
    void openShop();
    int shopRank() const;
    std::vector<int> shopChars() const;
    int spareCount(int id) const;
    std::vector<ListRow> shopRows();
    int shopLimit() const;
    void shopClose(const char* call = nullptr);
    void shopTap(int id);
    void exploreShop();
    void shopKey(SDL_Keycode k);
    void shopToken(const std::string& t);
    void drawShop(const Mat4& view, const Mat4& proj, int w, int h);

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
    std::vector<int> partyChoices() const;
    void partyTap(int id);
    void partyKey(SDL_Keycode k);
    void drawParty(const Mat4& view, const Mat4& proj, int w, int h);
    void drawFieldUi(int w, int h);
    // battery_id at a frame by the device's power (BatteryWindowMenu::Update, every 20 frames:
    // charging - 0.35 full, 0.3 under 30%, 0.25 under 60%, else 0.2; charged 0.2; discharging 0.15
    // under 30%, 0.1 under 60%, else 0.05; unknown 0) - SDL's power info; a machine without a
    // battery as charged
    bool batteryLow = false;
    float batteryCheckedAt = -100, batteryFrameAt = -100, batteryFrame = 0.2f;
    void updateBattery();
    void drawBattery(const Mat4& view);
    void drawBalloons(int w, int h);
    bool attentionShown = false;
    float attentionAt = 0;
    void drawFlagMarks(int w, int h);
    void drawIntro(int w, int h);
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
    void updateVistaSlash();
    void vslashEnd(bool hit);
    bool vslashPlace(float& x, float& y);
    void vslashPointer(float fx, float fy, int phase);
    float uiDuration(const std::string& anim, const std::string& fig = "");
    void drawVistaSlash();
    // Cr3Fade's type 3 (FadeWindowMenu 0x14): not a fade but the encounter's wipe - `encount`, its
    // streaked edge (hud_encount) on a panel 2.048 wide, encount_in sliding it over the screen from
    // the right (a FadeOut: the field's encounter, Cr3FadeOut(3, 20)), encount_out on off to the
    // left (a FadeIn: every battle's start, BtlMainCtrl::StepBattleIn's FadeIn(3, 20)); `p` 0..1
    // (its animators at 10 / frames speed - FadeWindowMenu::SetParamNumber 3 -: 0.25 s at 0.5, the
    // reference's half a second)
    // the encounter's wipe (Cr3FadeOut / FadeIn(3, 20): FadeWindowMenu's `encount` - encount_in to
    // cover, encount_out to uncover): their own 0.25 s, as the reference video plays them (each
    // sweep, right to left, about 0.27 s at 30 frames a second) - FadeWindowMenu's speed 10 / 20
    // was read as half speed and gave 0.5 s
    static constexpr float kWipeSeconds = 0.25f;
    float battleWipeAt = -1;
    void drawEncountWipe(float p, bool cover);
    void drawEventUi();
};

} // namespace cr2_viewer
