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
#include "viewer/viewer.hpp"

#include <SDL3/SDL_main.h>

#include "assets/gltf_figure.hpp"
#include "assets/hd_texture.hpp"
#include "assets/release.hpp"
#include "gfx/cache_trim.hpp"
#include "platform/atomic_file.hpp"
#include "platform/lifecycle_clock.hpp"
#include "viewer/setup.hpp"

using namespace cr2_viewer;

namespace {

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
    return std::string(u.mode == cr2::Upscaler::Fsr3 ? "FSR 3.1 " : u.mode == cr2::Upscaler::Fsr4 ? "FSR 4 " : u.mode == cr2::Upscaler::MetalFx ? "MetalFX " : "DLSS ") + q;
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
    std::string error;
    if (!crstorage::replaceFile(file, out, &error)) SDL_Log("settings: %s", error.c_str());
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

int inputCancelViewerSelfTest() {
    if (!SDL_Init(SDL_INIT_EVENTS)) return 1;
    int problems = 0;
    {
        Viewer v;
        crinput::CancelWatch watch;
        if (!watch.start(42)) return 1;
        for (Uint32 type : {Uint32(SDL_EVENT_WINDOW_FOCUS_LOST), Uint32(SDL_EVENT_FINGER_CANCELED), Uint32(SDL_EVENT_WILL_ENTER_BACKGROUND)}) {
            v.menuSwipe.begin(0.5f, 0.5f, 1);
            v.walkDrag.down = true;
            v.fastHeld = true, v.fastUntil = 100;
            v.iai.stage = v.aim.stage = 2;
            v.keyLock.stage = 1, v.keyLock.pressing = true;
            v.vslash.stage = 1, v.vslash.pressed = true;
            SDL_Event e{};
            e.type = type;
            if (type == SDL_EVENT_FINGER_CANCELED) e.tfinger.windowID = 42;
            else e.window.windowID = 42;
            SDL_PushEvent(&e);
            const bool consumed = v.consumeInputCancel(watch);
            const bool clean = !v.menuSwipe.down && !v.walkDrag.down && !v.fastHeld && v.fastUntil == 0 &&
                               v.iai.stage == 1 && v.aim.stage == 1 && !v.keyLock.pressing && !v.vslash.pressed;
            const bool unchanged = v.keyLock.stage == 1 && v.vslash.stage == 1 && v.pointerCancelled;
            problems += !consumed + !clean + !unchanged;
            if (!clean) std::printf("viewer input cancel FAILED: event %u retained an active gesture\n", type);
            const bool heldOver = !v.padHeldOver.live(true) && v.padHeldOver.live(false);
            problems += !heldOver;
            if (!heldOver) std::printf("viewer input cancel FAILED: event %u left a held controller live\n", type);
        }
    }
    SDL_Quit();
    std::printf("viewer input cancellation: %d problems\n", problems);
    return problems ? 1 : 0;
}

// The controller on the Vita's screens (docs/CONTROLS.md): a virtual controller, as CR2_PAD_SCRIPT's,
// its buttons' events through padButtonKey and gameKey - the d-pad's first press as the window's loop
// has it, its arrow - on screens set up without the game's files; what each did and the sounds asked
int padControlsSelfTest() {
    if (!SDL_Init(SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) return 1;
    int problems = 0;
    auto check = [&](bool ok, const std::string& what) {
        if (!ok) ++problems, std::printf("pad controls FAILED: %s\n", what.c_str());
    };
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = "CR2-U test controller";
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    SDL_Joystick* stick = id ? SDL_OpenJoystick(id) : nullptr;
    SDL_Gamepad* pad = id ? SDL_OpenGamepad(id) : nullptr;
    if (!stick || !pad) {
        std::printf("pad controls: no virtual controller (%s)\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    {
        Viewer v;
        auto drain = [&] {
            SDL_UpdateJoysticks();
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type != SDL_EVENT_GAMEPAD_BUTTON_DOWN) continue;
                const SDL_GamepadButton b = SDL_GamepadButton(e.gbutton.button);
                SDL_Keycode k = v.padButtonKey(b);
                if (b == SDL_GAMEPAD_BUTTON_DPAD_UP) k = SDLK_UP;
                else if (b == SDL_GAMEPAD_BUTTON_DPAD_DOWN) k = SDLK_DOWN;
                else if (b == SDL_GAMEPAD_BUTTON_DPAD_LEFT) k = SDLK_LEFT;
                else if (b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) k = SDLK_RIGHT;
                if (k) v.gameKey(k);
            }
        };
        drain();
        auto press = [&](SDL_GamepadButton b) { // (down, then up: the sounds asked meanwhile)
            v.audio.log.clear();
            SDL_SetJoystickVirtualButton(stick, b, true);
            drain();
            SDL_SetJoystickVirtualButton(stick, b, false);
            drain();
        };
        auto se = [&](int clip) { return std::count(v.audio.log.begin(), v.audio.log.end(), "se " + std::to_string(clip)) > 0; };
        const SDL_GamepadButton circle = SDL_GAMEPAD_BUTTON_EAST, cross = SDL_GAMEPAD_BUTTON_SOUTH, L = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
                                R = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, up = SDL_GAMEPAD_BUTTON_DPAD_UP, down = SDL_GAMEPAD_BUTTON_DPAD_DOWN,
                                left = SDL_GAMEPAD_BUTTON_DPAD_LEFT, right = SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
        for (int c = 0; c < 3; ++c) v.game.setFlag(0x3fd + c, true); // (three joined: Cr3UtilValidCharacter)

        // the camp's buttons (CampTopMenu, FUN_81084c8e): Down / Up round past Items with none held, SE 0xb3
        v.campOpen = true, v.campList = 0, v.campCursor = 0;
        press(down);
        check(v.campCursor == 1 && se(0xb3), "camp: Down the next button, SE 0xb3");
        press(up);
        check(v.campCursor == 3 && se(0xb3), "camp: Up past Items (none held) round to Options");
        // the items' list: L / R its switch - the other category (CampItemListMenu::Update's 8), SE 0xb7
        v.campList = 16, v.campCategory = 0, v.campCursor = 2;
        // (the switch plays the rows out and brings the other category in four updates on, its taps after:
        // settled here at once)
        auto settle = [&] {
            if (v.campCategoryAt >= 0) v.campCategoryAt = -1, v.campCategory = 1 - v.campCategory, v.campCursor = 0;
            v.campListOpenedAt = -10;
            if (v.campEquipSwitchAt >= 0) {
                v.campEquipSwitchAt = -1, v.campEquipMode = v.campEquipSwitchMode, v.campEquipCursor = v.campEquipSwitchCursor;
                if (v.campEquipSwitchChar >= 0) v.campEquipChar = v.campEquipSwitchChar, v.campEquipSwitchChar = -1;
            }
        };
        press(R);
        const bool rSe = se(0xb7);
        settle();
        check(v.campCategory == 1 && v.campCursor == 0 && rSe, "camp items: R the other category, SE 0xb7");
        press(L);
        const bool lSe = se(0xb7);
        settle();
        check(v.campCategory == 0 && lSe, "camp items: L back");
        // the equipment: L / R the character (CampEquipmentListMenu::Update_EquipView's 8), SE 0xb7
        v.campList = 17, v.campEquipMode = 0, v.campEquipChar = 0;
        press(R);
        const bool eSe = se(0xb7);
        settle();
        check(v.campEquipChar == 1 && eSe, "camp equipment: R the next character, SE 0xb7");
        press(L), settle(), press(L), settle();
        check(v.campEquipChar == 2, "camp equipment: L the one before, round");
        // the sopia's list (no switch): L / R its page - four rows (CR2-U's)
        v.campList = 18, v.campCursor = 0;
        press(R);
        check(v.campCategory == 0 && v.campEquipChar == 2 && !se(0xb7), "camp sopia: R no switch");
        v.campOpen = false, v.campList = 0;

        // the shop's equipment lists (ShopWepon Buy / SellListMenu, FUN_810c4650 / FUN_810c54ca): L / R the
        // character, the d-pad's Left / Right the kind round, each SE 0xb7
        v.shop.stage = 3, v.shop.kind = 1, v.shop.who = 0;
        press(R);
        check(v.shop.who == 1 && v.shop.kind == 1 && se(0xb7), "shop: R the next character, SE 0xb7");
        press(right);
        check(v.shop.kind == 2 && v.shop.who == 1 && se(0xb7), "shop: Right armour, SE 0xb7");
        press(right), press(right);
        check(v.shop.kind == 1, "shop: Right round to weapons");
        press(left);
        check(v.shop.kind == 3 && se(0xb7), "shop: Left accessories");
        v.shop.kind = 0, v.shop.who = 0;
        press(right), press(R);
        check(v.shop.kind == 0 && v.shop.who == 0, "shop: the items' list has neither");
        v.shop = Viewer::Shop();

        // a question (BattleYNChoice, FUN_8107beec): Yes in focus, Down No (SE 0xb3), ○ the one in focus
        v.shop.stage = 5, v.shop.at = v.uiClock;
        v.noteAsk();
        check(v.askFocus == 0, "question: Yes in focus as it opens");
        press(down);
        check(v.askFocus == 1 && se(0xb3), "question: Down No, SE 0xb3");
        press(circle);
        check(v.shopSwitchAt >= 0 && v.shopSwitchTo == 3, "question: ○ answers No (the shop's list again)"); // (once the choice is gone)
        v.shopSwitchAt = -1;
        v.shop.stage = 5;
        v.noteAsk(), v.noteAsk();
        v.shop.stage = 4, v.shop.count = 1; // (another question: Yes again)
        v.noteAsk();
        check(v.askFocus == 0, "question: the next one opens on Yes");
        press(up), press(up);
        check(v.askFocus == 0 && v.shop.count == 1, "question: Up / Down its focus, not the count");
        press(cross);
        check(v.shopSwitchAt >= 0 && v.shopSwitchTo == 3, "question: × No");
        v.shopSwitchAt = -1;
        v.shop = Viewer::Shop();
        v.world.stage = 4;
        v.noteAsk();
        press(down), press(cross);
        check(v.worldSwitchAt >= 0 && v.worldSwitchTo == 1, "question: the world select's × No"); // (the dungeons again once the choice is gone)
        v.world = decltype(v.world)();
        v.worldSwitchAt = v.worldOutAt = -1;
        v.noteAsk();

        // the pub's board (PubTopMenu, FUN_810b7b62): the posters three a row and Check
        for (int f = 1201; f <= 1205; ++f) v.game.setFlag(f, true); // (five posted)
        v.pub.stage = 1, v.pub.focus = 0;
        const int posted = int(v.pubPosters().size());
        check(posted == 5, "pub: five posted (" + std::to_string(posted) + ")");
        press(right), press(right);
        check(v.pub.focus == 2 && se(0xb3), "pub: Right along the row, SE 0xb3");
        press(right);
        check(v.pub.focus == -1 && se(0xb3), "pub: Right off the row's end Check");
        press(right);
        check(v.pub.focus == 3, "pub: Right from Check the fourth");
        press(down);
        check(v.pub.focus == 3 && v.audio.log.empty(), "pub: Down where no poster is, nothing");
        press(up);
        check(v.pub.focus == 0, "pub: Up a row");
        press(up);
        check(v.pub.focus == 0 && v.audio.log.empty(), "pub: Up round where no poster is, nothing");
        press(left);
        check(v.pub.focus == -1, "pub: Left off the row's start Check");
        press(left);
        check(v.pub.focus == 4, "pub: Left from Check the last (fewer than six)");
        press(left), press(left), press(right);
        check(v.pub.focus == 3, "pub: Left along the second row, off its start Check, Right the fourth again");
        v.pub.focus = -1;
        press(circle);
        check(v.pub.stage == 4 && se(0xb4), "pub: ○ on Check the list");
        v.pub.stage = 1, v.pub.focus = 1;
        press(circle);
        check(v.pub.stage == 2, "pub: ○ on a poster picks it");
        v.pub = decltype(v.pub)();

        // the buttons' keys: Swap ○ and × (the options'), △ Tab (CR2-U's: the field's tab), L / R in a menu alone
        v.settings.padSwap = true;
        check(v.padButtonKey(SDL_GAMEPAD_BUTTON_SOUTH) == SDLK_RETURN && v.padButtonKey(SDL_GAMEPAD_BUTTON_EAST) == SDLK_BACKSPACE,
              "Swap ○ and ×: the bottom button confirms");
        v.settings.padSwap = false;
        check(v.padButtonKey(SDL_GAMEPAD_BUTTON_NORTH) == SDLK_TAB && v.padButtonKey(L) == 0, "△ Tab; L outside a menu nothing");
    }
    SDL_CloseGamepad(pad);
    SDL_CloseJoystick(stick);
    SDL_DetachVirtualJoystick(id);
    SDL_Quit();
    std::printf("viewer pad controls: %d problems\n", problems);
    return problems ? 1 : 0;
}

int lifecycleClockViewerSelfTest() {
    if (!SDL_Init(SDL_INIT_EVENTS)) return 1;
    int problems = 0;
    {
        Viewer v;
        crtime::LifecycleClock clock;
        const Uint64 second = SDL_NS_PER_SECOND;
        auto frame = [&](Uint64 wall) {
            const float dt = clock.step(wall);
            v.advance(dt, 0, 0, false);
        };
        frame(second);
        frame(second + second/20);
        const float before = v.uiClock;
        SDL_Event event{};
        event.type = SDL_EVENT_WILL_ENTER_BACKGROUND;
        clock.note(event, 2*second);
        event.type = SDL_EVENT_DID_ENTER_FOREGROUND;
        clock.note(event, 3602*second);
        frame(3602*second);
        problems += v.uiClock != before; // no scene/UI catch-up after an hour asleep
        frame(3602*second + second/20);
        problems += std::fabs(v.uiClock - before - 0.05f) > 1e-5f;
        std::printf("viewer lifecycle: %.3f seconds before pause, %.3f after resumed frame; %d problems\n", before, v.uiClock, problems);
    }
    SDL_Quit();
    return problems ? 1 : 0;
}

int sceneAudioViewerSelfTest() {
    if (!SDL_Init(SDL_INIT_EVENTS)) return 1;
    int problems = 0;
    {
        auto device = cr2::makeGlDevice(); // resetHistory is a no-op; no GL context needed
        Viewer v;
        v.dev = device.get();
        auto check = [&](bool ok, const char* what) {
            if (!ok) { ++problems; std::printf("scene audio FAILED: %s\n", what); }
        };
        v.voiceJobs.push_back({2, 123});
        v.seJobs.push_back({2, 456});
        v.scene.active = true, v.scene.skippable = false, v.scene.hits = true;
        v.scene.start = 0, v.battleClock = 1;
        check(!v.skipScene() && v.voiceJobs.size() == 1 && v.seJobs.size() == 1,
              "a protected scene keeps its pending sounds");
        v.scene.skippable = true;
        check(v.skipScene(), "a permitted scene can be skipped");
        check(v.voiceJobs.empty() && v.seJobs.empty(), "skip cancels pending speech and effects");
        v.audio.log.clear();
        v.battleClock = 3;
        v.updateBattleSounds();
        check(v.audio.log.empty(), "advancing past the skipped cue cannot replay its voice");
        auto seed = [&] {
            v.voiceJobs.push_back({10, 123});
            v.seJobs.push_back({10, 456});
            v.pendingBgm = 15, v.pendingBgmAt = 10;
        };
        seed();
        v.leaveBattle();
        check(v.voiceJobs.empty() && v.seJobs.empty() && v.pendingBgm == 0,
              "abandoning battle cancels pending clips and the delayed result music");
        v.audio.log.clear();
        v.battleClock = 11;
        v.updateBattleSounds();
        check(v.audio.log.empty(), "abandoned battle cannot emit audio later");
        seed();
        v.startTitle();
        check(v.voiceJobs.empty() && v.seJobs.empty() && v.pendingBgm == 0,
              "return to title cancels old scene audio requests");
        v.audio.log.clear();
        v.updateBattleSounds();
        check(v.audio.log.empty(), "old battle cannot replace title music");
    }
    SDL_Quit();
    std::printf("scene audio cancellation: %d problems\n", problems);
    return problems ? 1 : 0;
}

int fieldRestoreSelfTest() {
    Viewer v;
    v.controller = std::make_unique<cr2::CharacterController>(v.field.collision());
    v.controller->setPosition({-9,2,-7});
    std::fill(std::begin(v.playerPrev),std::end(v.playerPrev),-8.0f);
    std::fill(std::begin(v.playerCur),std::end(v.playerCur),-6.0f);
    v.stepClock = 0.01f, v.substep = 2;
    const float saved[3] = {4,5,6};
    v.restoreFieldPosition(saved,0.75f);
    int problems = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { ++problems; std::printf("field restore FAILED: %s\n",what); }
    };
    auto near = [](float a,float b) { return std::abs(a-b)<0.00001f; };
    const auto at = v.controller->position();
    check(near(at[0],4) && near(at[1],5.02f) && near(at[2],6),"collision controller starts at the saved location");
    check(v.stepClock==0 && v.substep==0,"old interpolation time is discarded");
    check(near(v.heading,0.75f) && near(v.fig.place.m[3],4) && near(v.fig.place.m[7],5) && near(v.fig.place.m[11],6),
          "saved facing and initial render placement are restored");
    v.walk(0.001f,0,0,false); // first displayed frame, before a physics substep
    check(near(v.player[0],4) && near(v.player[1],5) && near(v.player[2],6),"first interpolated frame stays at the save instead of snapping to spawn");
    v.controller->substep(); // an empty world can fall, but horizontal position must stay at the save
    check(near(v.controller->position()[0],4) && near(v.controller->position()[2],6),"physics continues at the restored horizontal location");
    std::printf("field restore: %d problems\n",problems);
    return problems ? 1 : 0;
}

std::shared_ptr<vitaimport::Session::Choice> dirtySessionUiForTest(Viewer& v) {
    v.campOpen = v.mapOpen = v.settingsOpen = v.optionsOpen = v.optionsAsk = true;
    v.saveStage = 1, v.manualStage = 2;
    v.world.stage = v.shop.stage = v.party.stage = v.library.stage = v.pub.stage = 1;
    v.credits.stage = 1;
    v.paused = true;
    v.fieldTabOpen = true;
    v.fieldMode = v.eventMode = true;
    v.script = std::make_unique<cr2::FieldScript>(v.archive, v.game);
    v.event = std::make_unique<cr2::EventRuntime>(v.archive, v.game);
    v.manualEvents = std::make_unique<cr2::EventRuntime>(v.archive, v.game);
    v.menuSwipe.begin(0.5f, 0.5f, 1);
    v.fastHeld = true, v.fastUntil = 100;
    v.keyLock.stage = v.iai.stage = v.aim.stage = v.vslash.stage = 2;
    v.tapRects.push_back({999,0,0,1,1});
    v.game.setFlag(123,true);
    v.settings.fieldSpeed = 3;
    v.saveImport.state = vitaimport::Session::Picking;
    v.saveImport.choice = std::make_shared<vitaimport::Session::Choice>();

    return v.saveImport.choice;
}
int checkSessionUiForTest(Viewer& v, const std::shared_ptr<vitaimport::Session::Choice>& picker) {
    int problems = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) { ++problems; std::printf("session UI FAILED: %s\n", what); }
    };
    check(!v.campOpen && !v.mapOpen && !v.settingsOpen && !v.optionsOpen && !v.optionsAsk &&
          v.saveStage == 0 && v.manualStage == 0 && !v.world.stage && !v.shop.stage && !v.party.stage &&
          !v.library.stage && !v.pub.stage && !v.credits.stage && !v.paused && !v.fieldTabOpen,
          "old menus and pause cannot block a new title/session");
    check(!v.script && !v.event && !v.manualEvents && !v.activeEvents(), "old field/tutorial runtimes are detached");
    check(!v.menuSwipe.down && !v.fastHeld && v.fastUntil == 0 && v.tapRects.empty() &&
          !v.keyLock.stage && !v.iai.stage && !v.aim.stage && !v.vslash.stage,
          "old input targets and ability controls are discarded");
    { std::lock_guard<std::mutex> lock(picker->mutex); picker->path = "/no-such-late-import-after-title"; picker->ready = true; }
    v.saveImport.poll();
    check(!v.saveImport.active() && !v.saveImport.choice, "late picker completion cannot reactivate the importer");
    check(v.game.flag(123) && v.settings.fieldSpeed == 3, "progress and preferences are retained until a new game/load is selected");

    v.saveImport.cancel();
    return problems;
}
int sessionUiViewerSelfTest() {
    if (!SDL_Init(SDL_INIT_EVENTS)) return 1;
    int problems = 0;
    {
        Viewer v;
        const auto picker = dirtySessionUiForTest(v);
        v.startTitle();
        problems = checkSessionUiForTest(v,picker);
    }
    SDL_Quit();
    std::printf("viewer session UI: %d problems\n",problems);
    return problems ? 1 : 0;
}

// (no figure asked for: a game - the title's too, whose field comes later - shows the field's leader)
static bool leaderByDefault(bool noModel, bool title, bool field, bool resumed) { return noModel && (title || field || resumed); }

int newSessionSelfTest() {
    if (!SDL_Init(SDL_INIT_EVENTS)) return 1;
    int problems = 0;
    {
        Viewer v;
        const auto picker = dirtySessionUiForTest(v);
        v.systemRead = v.systemFound = true;
        v.systemFlags = {123}; // global unlocks survive a fresh game
        v.game.setFlag(1500, true);
        v.game.oz = 54321;
        v.game.itemCount[1] = 7;
        v.playSeconds = 987;
        v.voiceJobs.push_back({10,123});
        v.seJobs.push_back({10,456});
        v.pendingBgm = 15, v.pendingBgmAt = 10;
        v.newGame(); // the same entry point used by direct-play restart and title/retry
        problems += checkSessionUiForTest(v,picker);
        if (v.game.flag(1500) || v.game.oz == 54321 || v.game.itemCount[1] != 0 || v.playSeconds != 0) {
            ++problems; std::puts("new session FAILED: old progress or playtime survived");
        }
        if (!v.voiceJobs.empty() || !v.seJobs.empty() || v.pendingBgm || v.pendingBgmAt) {
            ++problems; std::puts("new session FAILED: abandoned scene audio survived");
        }
    }
    if (!leaderByDefault(true,true,false,false) || leaderByDefault(true,false,false,false) || leaderByDefault(false,true,false,false)) {
        ++problems; std::puts("new session FAILED: a game from the title has no leader");
    }
    {
        struct Entry { uint64_t used = 0; };
        std::map<int, Entry> cache;
        for (int k = 0; k < 20; ++k) cache[k].used = uint64_t(k + 1);
        cache[0].used = 99; // (the oldest, drawn just now)
        int freed = 0;
        cr2::trimLeastUsed(cache, 16, [&](Entry&) { ++freed; });
        if (freed != 8 || cache.size() != 12 || !cache.count(0) || cache.count(1) || !cache.count(19)) {
            ++problems; std::puts("new session FAILED: the text cache's limit");
        }
        cr2::trimLeastUsed(cache, 16, [&](Entry&) { ++freed; });
        if (freed != 8) { ++problems; std::puts("new session FAILED: a cache within its limit was trimmed"); }
    }
    {
        Viewer v;
        cr2::Image picture;
        picture.width = picture.height = 1, picture.rgba = {1, 2, 3, 4};
        v.modelImages["p"] = picture;
        cr2::Texture& held = v.textures["p"];
        held.id = 7;
        std::vector<std::pair<std::string, cr2::Image>> again{{"p", picture}};
        v.takeModelPictures(again);
        const auto it = v.textures.find("p");
        if (it == v.textures.end() || &it->second != &held || held.id != 7) {
            ++problems; std::puts("new session FAILED: an HD model's reload took a texture still held");
        }
    }
    SDL_Quit();
    std::printf("new session: %d problems\n",problems);
    return problems ? 1 : 0;
}

// The game as the setup (viewer/setup.hpp) knows it: the first start's on a PC, a Vita dump copied
// into vita/ on any start
static cr2::SetupGame cr2SetupGame() {
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
    setup.iosOk = [archiveIs](const std::string& p) { return archiveIs(p, ".ios.mvgl"); };
    setup.androidOk = [archiveIs](const std::string& p) { // (the OBB, and the APK with the game's library - the maps' table)
        std::vector<uint8_t> lib;
        return archiveIs(p, ".obb") && cr2::androidFile(p, "lib/armeabi-v7a/libChaosRings2.so", lib);
    };
    setup.vitaOk = [archiveIs](const std::string& p) { return archiveIs(p, ".psp2.mvgl"); };
    return setup;
}

int main(int argc, char** argv) {
    if (argc>1 && std::strcmp(argv[1],"--selftest-new-session")==0) return newSessionSelfTest();
    if (argc>1 && std::strcmp(argv[1],"--selftest-field-restore")==0) return fieldRestoreSelfTest();
    if (argc>1 && std::strcmp(argv[1],"--selftest-session-ui")==0) return sessionUiViewerSelfTest();
    if (argc > 1 && std::strcmp(argv[1], "--selftest-scene-audio") == 0) return sceneAudioViewerSelfTest();
    if (argc > 1 && std::strcmp(argv[1], "--selftest-lifecycle-clock") == 0) return lifecycleClockViewerSelfTest();
    if (argc > 1 && std::strcmp(argv[1], "--selftest-input-cancel") == 0) return inputCancelViewerSelfTest();
    if (argc > 1 && std::strcmp(argv[1], "--selftest-pad-controls") == 0) return padControlsSelfTest();
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
    std::string exportDir; // --export-textures [dir]: every image as <hash>.png (upright), for tools/hd_textures.py (textures/export/ in the game folder without one)
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
    else if (upscaler == "fsr4") upscale.mode = cr2::Upscaler::Fsr4;
    else if (upscaler != "off") return std::fprintf(stderr, "--upscaler: off, fsr3, fsr4, dlss or metalfx\n"), 2;
    {
        const auto q = std::find_if(std::begin(kQualities), std::end(kQualities), [&](const Quality& k) { return quality == k.name; });
        if (q == std::end(kQualities)) return std::fprintf(stderr, "--quality: native, quality, balanced, performance or ultra\n"), 2;
        upscale.ratio = q->ratio;
    }
    std::vector<std::string> searched; // (where a copy was looked for: the message if none)
    if (data.empty()) { // (CR2_DATA, else a copy in a game folder: vita/, android/, ios/ ...)
        searched = dataPlaces();
        for (const std::string& place : searched) { // (a Vita dump copied into vita/ - a phone's way -, its files encrypted: decrypted into place)
            cr2::SetupResult got;
            if (cr2::installDroppedVita(cr2SetupGame(), place, got, !capture.empty())) break;
        }
        data = cr2::defaultArchive(searched);
        if (data.empty()) data = storedData(); // (an older setup's: an Android or Vita copy where it lay)
    }
    data = cr2::resolveArchive(data); // (a folder: the OBB or the CRDB.*.mvgl in it, a Vita copy's PCSG00498 ...)
#if !defined(SDL_PLATFORM_IOS)
    if (data.empty() && (argc <= 1 || cr2::setupScripted()) && !std::getenv("CR2_DATA")) { // the first start without a copy: where is it? (viewer/setup.hpp - a phone's too)
        SDL_Init(SDL_INIT_VIDEO); // (its message boxes and the system's dialogs)
        const cr2::SetupGame setup = cr2SetupGame();
        std::string profile;
#ifdef SDL_PLATFORM_ANDROID
        if (!searched.empty()) // (the folders made, to be seen over USB)
            for (const char* f : {"ios", "android", "vita"}) SDL_CreateDirectory((searched.front() + f).c_str());
#else
        if (char* pref = SDL_GetPrefPath("DuIslingr", "CR2-U")) profile = pref, SDL_free(pref); // (a phone's game folder is always its own: no other)
#endif
        cr2::SetupResult got;
        if (!cr2::runSetup(setup, searched.empty() ? profile : searched.front(), profile, got)) return 0;
        // (copied into the game folder - ios/, android/, vita/ -: found as on any start; a dump copied in meanwhile decrypted)
        for (const std::string& place : dataPlaces()) {
            cr2::SetupResult dropped;
            if (cr2::installDroppedVita(setup, place, dropped, !capture.empty())) break;
        }
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
               "\n  vita/     the Vita's PCSG00498 folder, or a NoNpDrm dump's folder (the Prequel Trilogy's with its"
               "\n            addcont/ beside its app/: decrypted here on the next start)\n\nAlso looked in:";
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
        if (!exploreDir.empty()) { // (from the title, as a player - or, --continue, the save: in frames of the reference's 20:9)
            titleScreen = !continueSave;
            if (resW == 0) resW = 1280, resH = 576;
        }
        if (capture.empty() && !exportTextures && !exportModels && exploreDir.empty()) {
            v.settingsPersist = true;
            v.loadSettings();
            if (!given("--page")) page = 0; // (played: a page and a yes / no wait for the player, as the game's - --page n turns them by themselves)
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
            const std::string path = exportDir + cr2::hashName(h) + ".png";
            if (SDL_GetPathInfo(path.c_str(), nullptr)) continue; // (already there)
            if (cr2::savePngImage(path, img)) ++written, ++kinds[cr2::imageStorage(img)];
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
    const bool leaderDefaulted = leaderByDefault(model.empty(), titleScreen, !fieldName.empty() || !playFrom.empty(), continueSave);
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
#ifdef CR_HAVE_VULKAN
    { // FSR 4's files (AMD's DLLs, Linux's ffx-vk-linux): fsr4/ in the game folder, beside the executable, CR2_FSR4_DIR
        const char* env = SDL_getenv("CR2_FSR4_DIR");
        const char* base = SDL_GetBasePath();
        cr2::setFsr4Dirs({env && *env ? std::string(env) : std::string(), gameFolder + "fsr4/", base ? std::string(base) + "fsr4/" : std::string()});
    }
#endif
    // (the Vulkan pipelines compiled in earlier runs: the profile's, as the settings are kept - not
    // a capture's or an export's; CR2_PIPELINE_CACHE names another file, "" none)
    if (const char* pc = SDL_getenv("CR2_PIPELINE_CACHE")) cr2::setPipelineCacheFile(pc);
    else if (v.settingsPersist) cr2::setPipelineCacheFile(Viewer::savePath("vulkan-pipelines.bin"));
    std::unique_ptr<cr2::RenderDevice> dev;
    SDL_Window* win = nullptr;
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
    const SDL_WindowFlags shown = capture.empty() && exploreDir.empty() ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_HIDDEN; // (the system's bars hidden from the start)
#else
    const SDL_WindowFlags shown = capture.empty() && exploreDir.empty() ? SDL_WINDOW_RESIZABLE : SDL_WINDOW_HIDDEN;
#endif
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
    if (const char* expected = SDL_getenv("CR_SELFTEST_LOAD_POSITION")) {
        float xyz[3], yaw;
        if (std::sscanf(expected, "%f %f %f %f", &xyz[0], &xyz[1], &xyz[2], &yaw) != 4 || !v.fieldMode || !v.controller) return 1;
        int problems = 0;
        const auto physical = v.controller->position();
        for (int k = 0; k < 3; ++k)
            problems += std::abs(physical[k] - xyz[k] - (k == 1 ? 0.02f : 0.0f)) > 0.00001f;
        problems += std::abs(v.heading - yaw) > 0.00001f;
        v.walk(0.001f, 0, 0, false);
        for (int k = 0; k < 3; ++k) problems += std::abs(v.player[k] - xyz[k]) > 0.00001f;
        SDL_Log("loaded save position: controller and first frame; %d problems", problems);
        return problems ? 1 : 0;
    }
    auto takeTitle = [&]() {
        if (v.titleMenu.stage != 0 || !v.titleMenu.action) return true;
        const int act = v.titleMenu.action;
        v.titleMenu.action = 0;
        v.newGame();
        const bool carried = act != 4 || v.carryOver();
        const bool ok = carried && (act == 1 || act == 4 ? v.loadField(fieldName, at, binary, data, callEntry)
                                                         : v.continueSave(binary, data, act == 2 ? "save1.txt" : "cont.txt"));
        SDL_Log("title: %s%s", act == 1 ? "a new game" : act == 4 ? "a new game with the clear data" : act == 2 ? "the save loaded" : "the continue data",
                ok ? "" : " - failed");
        if (!ok && act != 1) { // (a save read: the title again; a new game that can't start is the data's fault, and the end)
            v.titleLoadFailed();
            return true;
        }
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
    { // (the field camera's units before the first frame is drawn: a run that only captures at its end)
        int w = 0, h = 0;
        frameSize(w, h);
        v.camW = w, v.camH = h;
    }
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
            // CR2_CAPTURE_EVERY=<folder>:<from>:<n> (tests): from <from> s, every nth frame as <folder>/fNNNNN.bmp
            std::string everyDir;
            float everyFrom = 0;
            int everyN = 0, frame = 0;
            std::vector<float> taps; // CR2_CAPTURE_TAP=<s>,<s>... (tests): a tap at each of those times
            if (const char* e = SDL_getenv("CR2_CAPTURE_TAP"))
                for (const char* p = e; *p;) {
                    char* end = nullptr;
                    taps.push_back(std::strtof(p, &end));
                    if (end == p) break;
                    p = *end == ',' ? end + 1 : end;
                }
            if (const char* e = SDL_getenv("CR2_CAPTURE_EVERY")) {
                const std::string s = e;
                const size_t b = s.rfind(':'), a = b == std::string::npos ? b : s.rfind(':', b - 1);
                if (a != std::string::npos) everyDir = s.substr(0, a), everyFrom = float(std::atof(s.c_str() + a + 1)), everyN = std::max(1, std::atoi(s.c_str() + b + 1));
            }
            for (float t = 0; t + 0.5f / 60.0f < startTime; t += 1.0f / 60.0f) {
                v.advance(1.0f / 60.0f, 0, 0, false), v.pose();
                catchGlass(); // (the screen caught as it breaks)
                if (!takeTitle()) return 1; // (a game over's "try again": its save loaded)
                for (float& at : taps)
                    if (at >= 0 && t >= at) v.tap(), at = -1, SDL_Log("capture: a tap at %.2f s", t);
                if (everyN > 0 && t >= everyFrom && frame++ % everyN == 0) {
                    v.draw(w, h);
                    dev->endFrame(false);
                    std::vector<uint8_t> px;
                    int pw = w, ph = h;
                    char name[32];
                    std::snprintf(name, sizeof name, "/f%05d.bmp", int(t * 60.0f + 0.5f));
                    if (dev->readPixels(px, pw, ph)) saveBmp((everyDir + name).c_str(), px, pw, ph);
                }
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
        bool recoveryOk = true;
        if (SDL_getenv("CR_SELFTEST_GRAPHICS")) {
            dev->readPixels(px, w, h);
            const auto before = px;
            size_t visible = 0;
            for (size_t i = 0; i + 3 < before.size(); i += 4)
                visible += int(before[i]) + before[i + 1] + before[i + 2] > 32;
            recoveryOk = visible >= before.size() / 4 / 10;
            SDL_Log("graphics recovery: visible scene pixels %zu (%s)", visible, recoveryOk ? "enough" : "FAILED");
            for (int cycle = 0; cycle < 3; ++cycle) {
                SDL_GL_DestroyContext(SDL_GL_GetCurrentContext());
                if (!SDL_GL_CreateContext(win) || !dev->restoreAfterContextLoss()) {
                    recoveryOk = false;
                    break;
                }
                v.draw(w, h);
                dev->endFrame(false);
                dev->readPixels(px, w, h);
                const bool sameFrame = !before.empty() && before == px && dev->takeErrors() == 0;
                recoveryOk &= sameFrame;
                SDL_Log("graphics recovery: cycle %d, identical scene frame %s", cycle + 1, sameFrame ? "yes" : "NO");
            }
        }
        const bool ok = recoveryOk && dev->readPixels(px, w, h) && saveBmp(capture.c_str(), px, w, h);
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
        crinput::CancelWatch inputCancel;
        inputCancel.start(SDL_GetWindowID(win));
        crtime::LifecycleClock frameClock;
        if (!frameClock.start()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "lifecycle clock: %s", SDL_GetError());
            return 1;
        }
        auto consumeInputCancel = [&] {
            if (v.consumeInputCancel(inputCancel)) dragging = false, padDir = 0, padDirNext = 0;
        };
        auto pushKey = [](SDL_Keycode key) {
            SDL_Event k{};
            k.type = SDL_EVENT_KEY_DOWN;
            k.key.key = key;
            k.key.down = true;
            SDL_PushEvent(&k);
        };
        // CR2_PAD_SCRIPT (tests): a virtual controller, its buttons and stick set at their times - "1.5:east
        // 2:down 3:lx=1 4:lx=0" (a button is held 0.1 s; east / south / north / west, up / down / left /
        // right, l / r, start, back; lx / ly the left stick, -1 .. 1, ly up positive; blur the window's focus
        // lost; shot=<file.bmp> the next frame shown saved; quit the window closed). Each press of a held
        // direction is logged
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
        std::string padShot; // (CR2_PAD_SCRIPT's shot=: the next frame's file)
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
            // (the UI's screen centred in the frame: an offscreen device's scaled into the window to fit; GL's in its own pixels)
            const float k = v.dev->offscreen() ? std::min(float(pw) / float(v.frameW), float(ph) / float(v.frameH)) : 1.0f;
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
        const Uint64 firstFrame = SDL_GetTicksNS();
        // --fps (the settings' Frame Rate): each frame starts no sooner than its slot (the display's
        // vsync may hold it longer)
        Uint64 nextFrame = firstFrame, fpsSince = firstFrame;
        int fpsFrames = 0, totalFrames = 0;
        float measured = 0;
        const Uint64 started = firstFrame;
        while (running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                const bool cancelled = inputCancel.note(e);
                consumeInputCancel();
                if (cancelled) continue;
                if (e.type == SDL_EVENT_RENDER_DEVICE_RESET) {
                    if (e.render.windowID == SDL_GetWindowID(win) && !dev->restoreAfterContextLoss()) {
                        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Unable to restore the OpenGL context");
                        code = 1;
                        running = false;
                        break;
                    }
                    continue;
                }
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
                    if (v.gameKey(k)) continue; // (the game's screens; a controller's buttons come as these keys)
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
                    if (const SDL_Keycode key = v.padButtonKey(SDL_GamepadButton(e.gbutton.button))) pushKey(key);
                } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    v.pointerCancelled = false;
                    v.pointerDown = true;
                    dragging = true;
                    float fx, fy;
                    screenFraction(e.button.x, e.button.y, fx, fy);
                    if (!v.menuPointerDown(fx, fy)) v.pointerActivate(fx, fy);
                }
                else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                    if (v.pointerDown) v.pointerDown = false, v.pointerLifted = true;
                    if (v.pointerCancelled) continue; // an interrupted gesture cannot activate or finish anything
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
            consumeInputCancel();
            if (!running) break;
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
            const float dt = frameClock.step();
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
                    else if (w.rfind("shot=", 0) == 0) padShot = w.substr(5);
                    else if (w == "quit") running = false;
                    else if (w == "blur") {
                        SDL_Event focus{};
                        focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
                        focus.window.windowID = SDL_GetWindowID(win);
                        SDL_PushEvent(&focus);
                    } else if (auto b = kButtons.find(w); b != kButtons.end()) {
                        SDL_SetJoystickVirtualButton(testPad, b->second, true);
                        testReleases.push_back({now + 100000000, b->second});
                    }
                    SDL_Log("test controller: %s", w.c_str());
                }
                for (size_t r = 0; r < testReleases.size();)
                    if (testReleases[r].first <= now) SDL_SetJoystickVirtualButton(testPad, testReleases[r].second, false), testReleases.erase(testReleases.begin() + long(r));
                    else ++r;
            }
            SDL_Gamepad* livePad = nullptr;
            if (pad) {
                float ax, ay;
                padStick(ax, ay);
                bool held = ax != 0 || ay != 0;
                for (auto button : {SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_WEST})
                    held = held || SDL_GetGamepadButton(pad, button);
                if (v.padHeldOver.live(held)) livePad = pad;
            }
            if (livePad) { // (the d-pad, or the stick pushed all the way: a direction's press, then its repeats)
                float ax, ay;
                padStick(ax, ay);
                SDL_Keycode d = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP)      ? SDLK_UP
                                : SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)  ? SDLK_DOWN
                                : SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)  ? SDLK_LEFT
                                : SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ? SDLK_RIGHT
                                                                                           : 0;
                if (!d && std::max(std::abs(ax), std::abs(ay)) >= 0.9f)
                    d = std::abs(ay) >= std::abs(ax) ? (ay > 0 ? SDLK_UP : SDLK_DOWN) : (ax > 0 ? SDLK_RIGHT : SDLK_LEFT);
                auto padPress = [&] {
                    pushKey(d);
                    if (testPad) SDL_Log("test controller: pressed %s", SDL_GetKeyName(d));
                };
                if (d != padDir) {
                    padDir = d;
                    if (d) padPress(), padDirNext = now + 600000000;
                } else if (d && now >= padDirNext) {
                    padPress(), padDirNext = now + 80000000;
                }
            }
            ++fpsFrames, ++totalFrames;
            if (now - fpsSince >= 500000000) {
                measured = float(double(fpsFrames) * 1e9 / double(now - fpsSince));
                fpsFrames = 0, fpsSince = now;
            }
            if (!v.paused) v.time += dt;
            if (!v.paused && v.battleMode && !v.fieldMode && !v.eventMode) v.advance(dt, 0, 0, false);
            if (!v.paused && (v.eventMode || v.fieldMode || v.credits.stage || v.titleMenu.stage)) { // the arrows / WASD run, with Shift walk (the title: its clock)
                const bool* keys = SDL_GetKeyboardState(nullptr);
                if (keys[SDL_SCANCODE_F] || (livePad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST))) v.fastHeld = true; // (F or □ held: the fast-forward button's)
                else if (!(SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK)) v.fastHeld = false;
                float sx = float((keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) - (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]));
                float sy = float((keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) - (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]));
                bool run = !(keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]);
                if (livePad && !v.fieldTabOpen) { // (FldMain::PlayerMove: the stick's push as the finger's drag - far, a run)
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
            { // (tests: CR2_WINDOW_SHOTS=<folder>:<from>:<n> - from the battle's <from> s, every nth shown frame as
              // <folder>/wNNNNN_<battle s>.bmp; CR2_WINDOW_QUIT=<s> - the window closed at that battle time; a long frame logged)
                static std::string shotDir;
                static float shotFrom = -1, quitAt = -1;
                static int shotN = 0, shotFrame = 0;
                static bool shotRead = false;
                if (!shotRead) {
                    shotRead = true;
                    if (const char* e = SDL_getenv("CR2_WINDOW_SHOTS")) {
                        const std::string s = e;
                        const size_t b = s.rfind(':'), a = b == std::string::npos ? b : s.rfind(':', b - 1);
                        if (a != std::string::npos) shotDir = s.substr(0, a), shotFrom = float(std::atof(s.c_str() + a + 1)), shotN = std::max(1, std::atoi(s.c_str() + b + 1));
                    }
                    if (const char* q = SDL_getenv("CR2_WINDOW_QUIT")) quitAt = float(std::atof(q));
                }
                if (dt > 0.1f) SDL_Log("window: a long frame, %.2f s (battle %.2f s)", dt, v.battleClock);
                if (shotN > 0 && v.battleMode && v.battleClock >= shotFrom && shotFrame++ % shotN == 0) {
                    std::vector<uint8_t> px;
                    int pw = 0, ph = 0;
                    char file[64];
                    std::snprintf(file, sizeof file, "/w%05d_%06.2f.bmp", totalFrames, v.battleClock);
                    if (dev->readPixels(px, pw, ph)) { // (kept to 960 wide: every kth pixel)
                        const int k = std::max(1, pw / 960), sw = pw / k, sh = ph / k;
                        std::vector<uint8_t> small(size_t(sw) * size_t(sh) * 4);
                        for (int y = 0; y < sh; ++y)
                            for (int x = 0; x < sw; ++x)
                                std::memcpy(&small[(size_t(y) * size_t(sw) + size_t(x)) * 4], &px[(size_t(y) * size_t(k) * size_t(pw) + size_t(x) * size_t(k)) * 4], 4);
                        saveBmp((shotDir + file).c_str(), small, sw, sh);
                    }
                }
                if (!padShot.empty()) { // (CR2_PAD_SCRIPT's shot=)
                    std::vector<uint8_t> px;
                    int pw = 0, ph = 0;
                    if (dev->readPixels(px, pw, ph)) saveBmp(padShot.c_str(), px, pw, ph), SDL_Log("test controller: saved %s", padShot.c_str());
                    padShot.clear();
                }
                if (quitAt >= 0 && v.battleMode && v.battleClock >= quitAt) SDL_Log("window: closed at battle %.2f s", v.battleClock), running = false;
            }
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
    for (auto& [k, q] : v.uiTexts) dev->destroy(q.tex);
    for (auto& [k, t] : v.textures) dev->destroy(t);
    dev->shutdown();
    SDL_DestroyWindow(win);
    v.audio.close(); // (its streams and device before SDL_Quit frees the audio subsystem: ~Viewer runs after it)
    SDL_Quit();
    return code;
}
