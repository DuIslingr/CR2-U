#include "viewer/viewer.hpp"

#include "platform/atomic_file.hpp"

namespace cr2_viewer {

void Viewer::skipBattle() {
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
void Viewer::beginBattleOut() {
    battleOutAt = battleClock;
    if (battle.coupling != 282) battleFade.colour = 1, battleFade.alpha.begin(battleClock + cr2::EventRuntime::kTick, 0, 1, 1.0f);
    audio.stopBgm(20);
}
// the battle dropped (the options' Title Screen)
void Viewer::leaveBattle() {
    battleOutAt = -1;
    cancelSceneAudio();
    pendingBgm = 0, pendingBgmAt = 0;
    battleMode = fightingInFlow = choosing = optionsOpen = optionsAsk = false;
    battleScript.reset(), battleRules.reset();
    battleFight = 0, battleStep = BattleStep::Over;
    showActions.clear(), showIndex = 0, regenPending.clear(), regenStarted = false;
    announceQueue.clear(), commandShown.clear();
}
// BtlMainCtrl::StepBattleOut / Cr3BattleEvent_Exit: with flag 1003 (the event's FlgOn(1003)
// before BattleIn and SetVistaEvent) - cleared - on to the vista reserved (mode 2), else the
// field (evt100; a vista reserved kept for the field's branch)
void Viewer::afterBattle() {
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
void Viewer::setBattleCamera(const std::string& name, const cr2::Mat34& place, bool still) {
    if (name.empty() || (name == battleCamera && std::equal(place.m, place.m + 12, battleCameraPlace.m))) return;
    battleCamera = name, battleCameraStart = battleClock, battleCameraPlace = place, battleCameraStill = still, battleCameraLoop = false;
    battleCameraMoveStart = -1, targetCameraFighter = -1;
    dev->resetHistory(); // (a cut)
}
void Viewer::setBattleCamera(int number) {
    if (const char* n = cr2::battleCameraName(number)) setBattleCamera(std::string(n));
}
void Viewer::readSystem() {
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
            for (int n; l >> n;)
                if (n >= 0 && n < kSystemFlags) systemFlags.push_back(n); // (all this file holds)
        else if (key == "options")
            for (int& o : systemOptions) l >> o;
    }
}
void Viewer::applySystem() {
    if (!systemRead) readSystem();
    if (!systemFound) return; // (none yet: the save's own as they were)
    for (int n = 0; n < kSystemFlags; ++n) game.setFlag(n, false);
    for (int n : systemFlags) game.setFlag(n, true);
    if (systemOptions[0] >= 0)
        optionLanguage = systemOptions[0], optionQuality = systemOptions[1], optionVolume[0] = systemOptions[2],
        optionVolume[1] = systemOptions[3], optionVolume[2] = systemOptions[4];
}
void Viewer::writeSystem() {
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
    std::string error;
    if (!crstorage::replaceFile(savePath("system.txt"), text, &error)) SDL_Log("system save failed: %s", error.c_str());
}
// Fresh game / retry boundary. System settings are applied by newGame;
// save loading or explicit carryOver runs afterward and supplies its own progress.
void Viewer::resetForNewSession() {
    resetSessionUi();
    cancelSceneAudio();
    pendingBgm = 0, pendingBgmAt = 0;
    titleEvents.reset();
    titleMenu = TitleMenu();
    game = cr2::GameState();
    playSeconds = 0;
}
void Viewer::newGame() {
    resetForNewSession();
    applySystem();
    std::vector<uint8_t> d;
    if (!archive.read("data\\battledata.dat", d) || !battleData.parse(d)) return;
    game.initSopia(battleData, cr2::sopiaExp), game.initItems(battleData), game.initEquip(battleData, cr2::levelExpTable());
}
bool Viewer::startBattle(int id, const int party[2], int area) {
    battleOutAt = -1;
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
    commandEffects.clear(), pendingEffects.clear();
    for (int f = 0; f < 2; ++f) dropGuardObject(f);
    for (auto& [id, fx] : scene.effects)
        if (fx.fig) unload(*fx.fig);
    scene = SceneState(), sceneCameras.clear(), battleFade = BattleFade(), battleMapHidden = false, mapColour = Ramp();
    glass = cr2::GlassFragments(), glassPending = false;
    // (the last battle's result goes with it: left at its "over" stage it kept the HUD's place -
    // drawBattleHudMain draws the result window from stage 3 on - and posed that window at
    // battleClock - its old windowStart, before its start: the next battle's HUD gone, the
    // result's panels coming in over the fight until the clock caught up)
    if (result.loopSe >= 0) audio.stopSe(result.loopSe, 0);
    result = BattleResultShow();
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
    battleClock = 0, actionDueAt = -1;
    battleMode = true, msgOpen = false, msgCloseAt = -1;
    fieldMode = eventMode = false;
    dev->resetHistory();
    status = "battle " + std::to_string(id) + " in " + battle.map;
    SDL_Log("battle %d: map %s, pattern %s,%s; camera %s", id, battle.map.c_str(), battle.patternName.c_str(), log.c_str(), battleCamera.c_str());
    return true;
}

// the battle's camera now: the named camera's joint in whichever figure has it
bool Viewer::battleView(cr2::Mat34& world, float& fovY, float& zn, float& zf) {
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
bool Viewer::useMotion(BattleUnit& u, const std::string& suffix) {
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
float Viewer::motionLength(BattleUnit& u, const std::string& suffix) { return useMotion(u, suffix) ? u.fig.anim.duration() : 0.0f; }
void Viewer::playMotion(BattleUnit& u, const std::string& suffix, float at, const std::string& then) {
    u.motion = suffix, u.then = then == "bb01" ? stanceOf(u) : then, u.motionStart = at, u.loop = false, u.hold = false;
}
// a fighter's stance: bg01 guarding (Defend), bx02 in War's Executioner's Pose (state 37 -
// ActionSopiaLaunch: bx01, then bx02), else bb01
std::string Viewer::stanceOf(const BattleUnit& u) const {
    if (u.guarding) return "bg01";
    const int f = fighterOfUnit(u);
    if (f >= 0 && battleRules && battleRules->fighter(f).hasState(37) && battleRules->fighter(f).alive) return "bx02";
    return "bb01";
}
// (BtlUtilStatus::CheckActionReady: not ready while its model blends - the port has no blends: a
// motion's end blend, 0.3 s before its end, is over at its end - or with any motion but bb01,
// bb04, bg01, bx02; one not on the field is ready)
bool Viewer::fighterReady(int f) {
    if (f < 0 || f >= 5 || fighterUnit[f] < 0 || size_t(fighterUnit[f]) >= battleUnits.size()) return true;
    if (!battleRules->fighter(f).alive) return true; // (its action not done: ActionDontAction / none)
    BattleUnit& u = battleUnits[size_t(fighterUnit[f])];
    auto stance = [](const std::string& m) { return m == "bb01" || m == "bb04" || m == "bg01" || m == "bx02"; };
    if (battleClock < u.motionStart) return false; // (its motion's change still to come)
    if (stance(u.motion)) return true;             // (a block's bg01 too, while it plays)
    if (u.loop || u.hold) return false;
    const float d = motionLength(u, u.motion);
    return (d <= 0 || battleClock - u.motionStart >= d) && stance(u.then);
}
int Viewer::fighterOfUnit(const BattleUnit& u) const {
    for (int f = 0; f < 5; ++f)
        if (fighterUnit[f] >= 0 && size_t(fighterUnit[f]) < battleUnits.size() && &battleUnits[size_t(fighterUnit[f])] == &u) return f;
    return -1;
}

void Viewer::poseBattle() {
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
        // (a looping motion with the table's loop flag - +0 bit 3 - and range, +0x04 .. +0x08:
        // played to the range's end, then the range over and over - bb03's win pose, bg01's
        // guard, bd02's last frames; one without plays whole again, as the stance bb01)
        bool ranged = false;
        if (u.loop) {
            cr2::MotionTimes mt;
            if (cr2::motionTimes(u.figure, cr2::motionIndex(u.motion), mt) && (mt.flags & 8) && mt.loopStart >= 0 && mt.loopEnd > mt.loopStart) {
                ranged = true;
                if (t >= mt.loopEnd) t = mt.loopStart + std::fmod(t - mt.loopStart, mt.loopEnd - mt.loopStart);
            }
        }
        pose(u.fig, std::max(t, 0.0f), u.loop && !ranged);
    }
}
// BtlActionCtrl::SetTargetCamera(false): the camera on the one an item or a sopia is used on -
// the foes (+0x4b 6) by the party: GetItemSopiaTargetGroup by the pattern (+2; 12, 13 the human
// ones); a party member: GetItemSopiaTarget(2 alone, 3 a pair, else which), Cam_Unit_PCrevival_00
// at its place for command 0x67 or item 0xc; a monster: GetItemSopiaTarget by the pattern - 11 a pair,
// else its size's 4 + 0..3 - in its DummyCP's frame (none for the patterns 7, 8, 10 .. 12; 8 cm
// nearer for pattern 4's by the party). The allies' (+0x4b 3: GetItemSopiaTargetGroup(+0x523))
// and a monster's on the party's side are not followed.
// (`forced`: that one instead of the first struck; `onCaster`: SetTargetCamera(true), the
// caster's own - the foes' group camera then a monster's)
bool Viewer::useTargetCamera(const ShowAction& a, const cr2::BattleData::Command* c, std::string& name, cr2::Mat34& place, int forced, bool onCaster) {
    name.clear(), place = cr2::Mat34();
    const int target = forced >= 0 ? forced : a.hits.empty() ? -1 : a.hits.front().target;
    const int p = battle.pattern;
    int n = -1;
    if (c && c->target == 3) return false;
    if (c && c->target == 6) {
        if ((a.actor >= 0 && a.actor < 2) != onCaster) n = cr2::cameraItemSopiaTargetGroup(p == 10 ? 12 : p == 11 ? 13 : p + 2);
    } else if (target >= 0 && target < 2 && !onCaster) {
        // (the revival's camera by what is used, not by what it does: the acting one's
        // command 0x67 - Resurrection - or item 0xc - Vin Brulee; not their stronger twins)
        const cr2::BattleData::Item* vin = battleData.item(0xc);
        const bool revives = a.command == 0x67 || (vin && vin->command > 0 && a.command == vin->command);
        if (revives) {
            name = "Cam_Unit_PCrevival_00";
            if (fighterUnit[target] >= 0)
                for (int r = 0; r < 3; ++r) place.m[r * 4 + 3] = battleUnits[size_t(fighterUnit[target])].home.m[r * 4 + 3];
            return true;
        }
        n = cr2::cameraItemSopiaTarget(fighterUnit[1] < 0 ? 2 : battleRules->fighter(target).partner >= 0 ? 3 : target);
    } else if (target >= 0 && target < 2) {
        n = cr2::cameraItemSopiaTarget(fighterUnit[1] < 0 ? 2 : battleRules->fighter(target).partner >= 0 ? 3 : target);
    } else if (target >= 2) {
        const cr2::BattleData::Monster* mo = battleData.monster(battleRules->fighter(target).id);
        const int k = battleRules->fighter(target).partner >= 0 ? 11
                      : p == 10                                 ? 10
                      : p == 11                                 ? (target == 2 ? 8 : 9)
                      : p == 12                                 ? 12
                      : p == 9                                  ? 13
                                                                : (mo ? mo->size : 0) + 4;
        n = cr2::cameraItemSopiaTarget(k);
        if (p != 7 && p != 8 && p != 10 && p != 11 && p != 12 && fighterUnit[target] >= 0) {
            place = targetFrame(battleUnits[size_t(fighterUnit[target])]);
            if (p == 4 && a.actor >= 0 && a.actor < 2) place.m[11] -= 0.08f;
        }
    }
    if (const char* nm = cr2::battleCameraName(n)) name = nm;
    return !name.empty();
}
bool Viewer::itemCommand(int command) {
    if (itemCommands.empty())
        for (int id = 1; id < 300; ++id)
            if (const cr2::BattleData::Item* it = battleData.item(id); it && it->command > 0) itemCommands.insert(it->command);
    return itemCommands.count(command) != 0;
}

// the battleapos joint `name`'s place (false: none)
bool Viewer::aposPlace(const std::string& name, cr2::Mat34& out) {
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
std::string Viewer::aposName(int actor, int target) {
    auto key = [&](int f) {
        const int u = fighterUnit[f];
        std::string k = u >= 0 ? battleUnits[size_t(u)].position.substr(5) : "";
        // (BtlAposTable names the two-small pattern's places ssL / ssR - battledpos's sL / sR:
        // the joints battleapos has; every other place is named alike)
        if (k == "sL" || k == "sR") k = "s" + k;
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
bool Viewer::focusScreen() const {
    return titleMenu.stage == 5 || titleMenu.stage == 6 || titleMenu.stage == 7 || shop.stage == 1 || shop.stage == 2 ||
           party.stage == 1 || party.stage == 2;
}
bool Viewer::focusKey(SDL_Keycode k) {
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
bool Viewer::tabFocusKey(SDL_Keycode k) {
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
bool Viewer::listShown() const {
    using CI = cr2::CommandInput;
    const bool battleList = battleMode && choosing &&
                            (commandInput.menu() == CI::Menu::Items || commandInput.menu() == CI::Menu::Advent ||
                             commandInput.menu() == CI::Menu::Awake || commandInput.menu() == CI::Menu::Sopia);
    return battleList || manualStage == 1 || settingsOpen || (campOpen && campList != 0) || library.stage == 1;
}
bool Viewer::inMenu() const {
    return campOpen || (battleMode && choosing) || shop.stage || world.stage || pub.stage || library.stage == 1 || party.stage;
}
// a step of the choosing begun: its camera (StepGetset: Cam_Getset; SetCommandCamera: 0x1a the
// first's, 0x1b the second's, 0x1d the pair's command, 0x1c the one alone's - the target's camera
// BtlCommand::SetCommandTargetCamera's - setTargetCamera)
void Viewer::menuChanged() {
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
void Viewer::setTargetCamera(int f) {
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
const cr2::Mat34& Viewer::targetFrame(BattleUnit& bu) {
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
void Viewer::inputToken(const std::string& t) {
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
std::vector<int> Viewer::menuChoices() const {
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
void Viewer::choose(int idOrFighter) {
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
void Viewer::battleSe(int n) {
    const int clip = cr2::SoundData::battleSe(n);
    if (clip >= 0) audio.playSe(clip, false);
}
// (`kind`: the data's - one of its three table kinds at random; `tableKind`: Cr3VoiceTable's
// own, as a command's +0x68)
void Viewer::battleVoice(int f, int kind, float delay, bool tableKind) {
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
void Viewer::partyVoice(int kind, bool tableKind, float delay) {
    const bool a = battleRules->fighter(0).alive, b = battleRules->fighter(1).present && battleRules->fighter(1).alive;
    const int who = a && b ? (int(boxRandom() % 100) > 49 ? 1 : 0) : a ? 0 : b ? 1 : -1;
    if (who >= 0) battleVoice(who, kind, delay, tableKind);
}
auto Viewer::namePlate() const -> NamePlate {
    switch (optionLanguage) {
    case 0: return {"pcnamejp_id", "call_pcNameJP_id", "22"};
    case 5: return {"pcnamechn1_id", "call_pcNameJP_id", "22"};
    case 6: return {"pcnamechn2_id", "call_pcNameJP_id", "22"};
    case 7: return {"pcnamekor_id", "call_pcNameJP_id", "22"};
    default: return {"pcnameen_id", "call_pcNameEN_id", "21"};
    }
}
// Stop both playback and delayed requests owned by a skipped/abandoned scene.
// Apply remaining combat hits after this, so their new cues keep their normal timing.
void Viewer::cancelSceneAudio() {
    voiceJobs.clear();
    seJobs.clear();
    audio.stopAllVoices(0);
    audio.stopAllSe(0);
}
void Viewer::updateBattleSounds() {
    for (size_t k = 0; k < seJobs.size();)
        if (battleClock >= seJobs[k].at) {
            audio.playSe(seJobs[k].clip, false);
            seJobs.erase(seJobs.begin() + std::ptrdiff_t(k));
        } else ++k;
    for (size_t k = 0; k < voiceJobs.size();)
        if (battleClock >= voiceJobs[k].at) {
            audio.stopAllVoices(0);
            const int slot = audio.playVoice(voiceJobs[k].clip);
            if (voiceJobs[k].command) commandVoiceSlot = slot;
            voiceJobs.erase(voiceJobs.begin() + std::ptrdiff_t(k));
        } else ++k;
    if (pendingBgm > 0 && battleClock >= pendingBgmAt) { // (BtlResult / BtlGameover: 1.1 s on)
        audio.releaseBgm();
        audio.playBgm(pendingBgm, 0);
        pendingBgm = 0;
    }
}
void Viewer::battleTutorial(const char* function, int flag) {
    if (const cr2::BattleData::Coupling* cp = battleData.coupling(battle.coupling); cp && cp->flag) return; // (+0x18)
    if (!battleScript || !battleScriptOk || battleScript->own() || battleScriptRunning() || game.flag(flag)) return;
    SDL_Log("tutorial: %s", function);
    if (!(battleScriptOk = battleScript->tutorial(function))) SDL_Log("battle script: %s", battleScript->error().c_str());
}
void Viewer::battleKey(SDL_Keycode k) {
    if (gameOver.stage == 8) { // (the question: Y / Return yes, N / Backspace no)
        if (k == SDLK_Y || k == SDLK_RETURN || k == SDLK_KP_ENTER) gameOverAnswer(true);
        else if (k == SDLK_N || k == SDLK_BACKSPACE) gameOverAnswer(false);
        return;
    }
    if (!choosing) return;
    if (battleScriptRunning()) { // (a tutorial's talk over the commands: its page first)
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) tap();
        return;
    }
    keysUsed = true;
    const std::vector<int> c = menuChoices();
    const bool target = commandInput.menu() == cr2::CommandInput::Menu::Target;
    // (the awakenings' list goes up and down)
    if (optionsKey(k)) return;
    // (the Vita's: the command buttons Up / Down round - FUN_810452fe, the selectable ones -, the
    // targets the same - BattleSelectMenu's update, FUN_810771c2 -, each step SE 0xb3; the lists -
    // ScrollListMenu - scroll without one. Left / Right the targets too, the keyboard's)
    using CI = cr2::CommandInput;
    const bool list = commandInput.menu() == CI::Menu::Items || commandInput.menu() == CI::Menu::Advent ||
                      commandInput.menu() == CI::Menu::Awake || commandInput.menu() == CI::Menu::Sopia;
    const int was = menuCursor;
    if (k == SDLK_BACKSPACE) commandInput.back(), audio.playSe(0xb5, false), menuChanged(); // (InterfaceMain: 0xb5 back)
    else if (c.empty()) return;
    else if (k == SDLK_DOWN || (target && k == SDLK_RIGHT)) menuCursor = (menuCursor + 1) % int(c.size());
    else if (k == SDLK_UP || (target && k == SDLK_LEFT)) menuCursor = (menuCursor + int(c.size()) - 1) % int(c.size());
    else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) return choose(c[size_t(std::clamp(menuCursor, 0, int(c.size()) - 1))]);
    if (k != SDLK_BACKSPACE && menuCursor != was && !list) audio.playSe(0xb3, false);
}
// a click at the screen's (fx, fy) - 0..1 across, down: a button's tap area (UI units)
void Viewer::battleClick(float fx, float fy) {
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
    if (battleScriptRunning()) return tap(); // (a tutorial's talk over the commands: its page first)
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
// The explorer's turn (--explore), through the menus as a player goes: a pair while both stand
// above half their HP, else solo; for each, one of them down revived (a sopia, else an item),
// one under 40% healed, else an awakening the charge covers, else the sopia that strikes
// hardest for its MP (keeping a third back for healing) where it beats the attack - on the
// weakest foe
bool Viewer::exploreOrders(bool& pair, std::vector<cr2::BattleRules::Order>& out) {
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
// one of the game's updates for the script (20 a second), and the exit's timer
void Viewer::updateBattleScript(float dt) {
    if (!battleRules) return;
    battleRules->runExitTimer(dt);
    if (!battleScript || !battleScriptOk) return;
    battleScript->events().language = optionLanguage; // (the options' language)
    battleScriptClock = std::min(battleScriptClock + dt, 0.25f);
    for (; battleScriptClock >= cr2::EventRuntime::kTick; battleScriptClock -= cr2::EventRuntime::kTick)
        if (!(battleScriptOk = battleScript->tick())) SDL_Log("battle script: %s", battleScript->error().c_str());
}
bool Viewer::battleScriptRunning() const { return battleScript && battleScriptOk && battleScript->running(); }
// (a battle with its own script - Cr3BattleEvent_SetScript, +0x237a: the bosses' - makes its calls
// through BtlEvent, updated after the action controller, BtlCommand and BtlTurnEnd: the call's
// state goes 1, 2, 3, 0 an update each, and the step waiting on IsRunning sees 0 the update after.
// A step machine taking one step an update - BtlCommand, BtlTurnEnd, ActionDead - waits three
// updates more than without a script; the action's table, looping its steps, four)
bool Viewer::ownBattleScript() const { return battleScript && battleScriptOk && battleScript->own(); }
void Viewer::startFight(int mode, uint32_t seed) {
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
    battleScript->events().autoPage = pageSeconds, battleScript->events().askYesNo = true;
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
    // the opening: Cam_Start_<pattern>_00 (BtlMainCtrl::StepBattleIn), then the turns. Its steps
    // from the FadeIn(3, 20) (the uncovering, now): 6 passes the update after, 7 - with a first
    // strike - waits 0.5 s and puts up its banner for 1.0 s (SetAnnounceTime 1.0), 8 waits over
    // 1.0 s more, 9 the fade's 20 updates; 10 calls Battle_Start, 11 waits for the script, 12 the
    // voice, 13, 14 the first turn the update after: Battle_Start 21 updates on (35 with the
    // banner), the turn 5 after it. The reference video: a battle's command menu 1.25 s after
    // its uncovering began, an assault's banner 0.57 s after. (It was a flat 3 s.)
    setBattleCamera(battle.startCamera);
    {
        constexpr float kTick = cr2::EventRuntime::kTick;
        const bool banner = battleRules && battleRules->advantage() != 0;
        showEnd = battleClock + (banner ? 35 : 21) * kTick;
        openingBannerAt = banner ? battleClock + 12 * kTick : -1;
        openingTurnAt = -1, openingBannerShownAt = -1;
    }
    SDL_Log("fight: %s", mode == 2 ? "pair" : "solo");
}

// a battledpos joint's place (BtlModel::GetPosition(dpos, name))
bool Viewer::dposPosition(const char* name, float* out) {
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
void Viewer::placeParty(bool pair) {
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
void Viewer::stepBattleShow() {
    if (!battleRules || battleFight == 0) return;
    // (a script's Exit - Cr3BattleEvent_Exit: BtlMain::NextMode there and then - waits for no
    // action shown and no script; a win or a loss comes once the last action is over: StepAction,
    // its controller's state 5)
    if (!battleRules->exited() && (battleScriptRunning() || battleClock < showEnd)) return;
    // (step 12 of every action table but the empty and the dead ones, ActionCommonCommandAnnounce:
    // on once the banner's queue is empty - the name's and each note's time run out; the port
    // waits for it at the action's end, after its other waits)
    if (!battleRules->exited() && battleStep == BattleStep::ActionEnd && showIndex < showActions.size() &&
        (!announceQueue.empty() || battleClock - announceAt < kAnnounceStay))
        return;
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
            const int sp = finisher();
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
        battleStep = BattleStep::Over, announceKept = false; // (the actions over: +0x131 set again)
        SDL_Log("battle over: %s (%.1f s)", battleRules->exited() ? "its script's exit" : battleRules->outcome() == 1 ? "won" : "lost", battleClock);
        showIndex = showActions.size();
        setBattleCamera(battle.camera);
        return;
    }
    switch (battleStep) {
    case BattleStep::Opening: // (the opening over: StepBattleIn's Battle_Start)
        battleStep = BattleStep::TurnBegin;
        if (battleRules->advantage()) SDL_Log("start: %s", battleRules->advantage() == 1 ? "the party first" : "the monsters' assault");
        battleRules->script("Battle_Start");
        openingTurnAt = battleClock + 5 * cr2::EventRuntime::kTick; // (steps 11 .. 14, the turn the update after)
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
        if (battleRules->turn() == 0 && (battleScriptRunning() || battleClock < openingTurnAt)) break; // (StepBattleIn's 11 .. 14)
        battleRules->beginTurn();
        battleStep = BattleStep::TurnChoose;
        // (BtlCommand::StepInit's Turn_Start, StepEvent's wait: three updates more)
        if (ownBattleScript()) showEnd = battleClock + 3 * cr2::EventRuntime::kTick;
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
            announceKept = true;
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
                // (away: StepEscapeSuccess the update after, StepEscapeSuccessLoop's 2 s, then step 0x1f -
                // StepSetCommand to StepBattleOut, NextMode the update after that: the way out 2.15 s on)
                showEnd = battleClock + (away ? 2.0f + 3 * cr2::EventRuntime::kTick : 1.0f);
                if (away) {
                    battleStep = BattleStep::Over;
                    setBattleCamera(battle.camera);
                    break;
                }
            }
            battleRules->chooseTurn(commandInput.pairTurn(), commandInput.orders());
            placeParty(battleRules->fighter(0).partner >= 0);
            SDL_Log("turn %d (%s, %.2f s)", battleRules->turn() + 1, commandInput.pairTurn() ? "pair" : "solo", battleClock);
            announceKept = true;
            battleStep = BattleStep::ActionStart;
            break;
        }
        bool pair = battleFight == 2;
        std::vector<cr2::BattleRules::Order> orders;
        if (ex.on && exploreOrders(pair, orders)) { // (the explorer's: as a careful player chooses)
            battleRules->chooseTurn(pair, orders);
            placeParty(battleRules->fighter(0).partner >= 0);
            SDL_Log("turn %d (%s, %.2f s)", battleRules->turn() + 1, pair ? "pair" : "solo", battleClock);
            announceKept = true;
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
        SDL_Log("turn %d (%.2f s)", battleRules->turn() + 1, battleClock);
        announceKept = true; // (StepSetAction: BtlAnnounce +0x131 cleared)
        battleStep = BattleStep::ActionStart;
        break;
    }
    case BattleStep::ActionStart:
        if (!battleRules->actionsLeft()) {
            announceKept = false; // (CheckAllActionDone: +0x131 set, the window hidden)
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
                if (e.damage > 0) showNumber(e.target, e.damage, 20, true), battleSe(0x13); // (BtlTurnEnd: the poison's SE)
                if (e.heal > 0) showNumber(e.target, e.heal, 50, true);
                if (e.healMp > 0) showNumber(e.target, e.healMp, 60, true);
                SDL_Log("    %s: %d poison, %d HP back, %d MP back", battleRules->fighter(e.target).name.c_str(), e.damage, e.heal, e.healMp);
            }
            // (BtlTurnEnd::Update: step 1 Turn_End, 2 its wait - three updates more with the battle's own
            // script -, 3 the poison and the pools, 4 1.5 s when any was applied, else straight to 5.
            // The port shows the numbers at once, so with a script they come those updates early)
            const float turnEndScript = ownBattleScript() ? 3 * cr2::EventRuntime::kTick : 0.0f;
            if (!battleRules->turnEndEvents().empty()) showEnd = battleClock + turnEndScript + 1.5f;
            else if (turnEndScript > 0) showEnd = battleClock + turnEndScript;
            SDL_Log("turn over; gauge %d, charge %d; MP %d %d (%.2f s)", battleRules->gauge(), battleRules->charge(), battleRules->fighter(0).mp, battleRules->fighter(1).mp, battleClock);
            battleStep = BattleStep::TurnBegin;
            break;
        }
        { // (every action table's step 2, ActionCommonStandby - before Command_Start and the
          // banner: CheckActionReady on the actor and a pair's partner, 5 s at most)
            const int actor = battleRules->nextAction().actor;
            const cr2::Fighter& x = battleRules->fighter(actor);
            const int partner = x.alive && x.partner >= 0 && battleRules->fighter(x.partner).alive ? x.partner : -1;
            if (actionDueAt < 0) {
                actionDueAt = battleClock;
                for (int f : {actor, partner})
                    if (!fighterReady(f)) {
                        const BattleUnit& u = battleUnits[size_t(fighterUnit[f])];
                        actionWaitedOn = battleRules->fighter(f).name + "'s " + u.motion + (u.loop ? "" : " (then " + u.then + ")");
                    }
            }
            if (battleClock - actionDueAt < 5.0f && (!fighterReady(actor) || (partner >= 0 && !fighterReady(partner)))) break;
            if (battleClock - actionDueAt > 0.01f) SDL_Log("  %s's action waited %.2f s for %s", x.name.c_str(), battleClock - actionDueAt, actionWaitedOn.c_str());
            actionDueAt = -1;
        }
        battleRules->startAction();
        // (a battle with its own script - Cr3BattleEvent_SetScript, +0x237a: the bosses' -
        // calls Command_Start in step 7 and waits in step 9 while BtlEvent::IsRunning: BtlEvent,
        // updated after the action controller, calls it that update, then its state goes 2, 3,
        // 0 an update each, and step 9 sees 0 the update after - four updates before the banner)
        if (ownBattleScript()) showEnd = battleClock + 4 * cr2::EventRuntime::kTick;
        battleStep = BattleStep::ActionDo;
        break;
    case BattleStep::ActionDo: {
        hudBefore.hp.clear();
        for (int k = 0; k < 5; ++k) hudBefore.hp.push_back(battleRules->fighter(k).hp);
        hudBefore.gauge = battleRules->gauge(), hudBefore.charge = battleRules->charge();
        std::vector<cr2::BattleRules::Event> events;
        battleRules->doAction(events);
        showActions.clear(), showIndex = 0, deaths.clear();
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
        // (the same for Command_End: steps 8 and 9, four updates, then 14)
        if (ownBattleScript()) showEnd = battleClock + 4 * cr2::EventRuntime::kTick;
        analyze.stage = 0, deathsQueued = false;
        battleStep = BattleStep::ActionStart;
        break;
    case BattleStep::Over: break;
    }
}
bool Viewer::hudHeld() const { return showIndex < showActions.size() && !showHitDone && hudBefore.hp.size() == 5; }
int Viewer::shownHp(int f) const {
    if (std::find(regenPending.begin(), regenPending.end(), f) != regenPending.end()) return 0; // (down until its revival shows)
    return hudHeld() ? hudBefore.hp[size_t(f)] : battleRules->fighter(f).hp;
}
void Viewer::showRegen() {
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
    // (ActionReviveWait: on once motion 8 has played out - its idle, ActionDone; the line's 3.0 s
    // runs on under what follows)
    showEnd = battleClock + motionLength(bu, "bh01") + cr2::EventRuntime::kTick;
}
int Viewer::shownGauge() const { return hudHeld() ? hudBefore.gauge : battleRules->gauge(); }
int Viewer::shownCharge() const { return hudHeld() ? hudBefore.charge : battleRules->charge(); }
std::string Viewer::announce(int id) { return messageText("announcebattle", id); }
// the names and help in the option's language (Cr3UtilGetItemName / ItemHelpText, GetCommandName /
// HelpText, GetSopiaName / HelpText, GetAutoSkillHelpText; GameMain's CharaName, MonsterName):
// message\itemname, itemhelp, commandname, commandhelp, sopianame, sopiahelp, autoskillhelp,
// charaname, monstername by number - battledata's English where one has none
std::string Viewer::localText(const char* file, int id, const std::string& fallback) {
    std::string t = messageText(file, id);
    return t.empty() ? fallback : t;
}
std::string Viewer::itemName(int id) { const cr2::BattleData::Item* it = battleData.item(id); return localText("itemname", id, it ? it->name[1] : ""); }
std::string Viewer::itemHelp(int id) { const cr2::BattleData::Item* it = battleData.item(id); return localText("itemhelp", id, it ? it->text[1] : ""); }
std::string Viewer::commandName(int id) { const cr2::BattleData::Command* c = battleData.command(id); return localText("commandname", id, c ? c->name[1] : ""); }
std::string Viewer::commandHelp(int id) { const cr2::BattleData::Command* c = battleData.command(id); return localText("commandhelp", id, c ? c->text[1] : ""); }
std::string Viewer::sopiaName(int id) { const cr2::BattleData::Sopia* so = battleData.sopia(id); return localText("sopianame", id, so ? so->name[1] : ""); }
std::string Viewer::sopiaHelp(int id) { return messageText("sopiahelp", id); }
std::string Viewer::autoSkillHelp(int id) { return messageText("autoskillhelp", id); }
std::string Viewer::fighterName(int i) {
    const cr2::Fighter& f = battleRules->fighter(i);
    return localText(f.side == 0 ? "charaname" : "monstername", f.id, f.name);
}
// a message file's languages: its records' size over 4 (the Vita's 7, the iOS / Android 8)
size_t Viewer::messageLanguages(const std::vector<uint8_t>& d, size_t extra) {
    auto u32 = [&](size_t o) { return o + 4 <= d.size() ? uint32_t(d[o] | d[o + 1] << 8 | d[o + 2] << 16 | uint32_t(d[o + 3]) << 24) : 0u; };
    const uint32_t records = u32(0), strings = u32(4), n = u32(8);
    if (n == 0 || strings <= records || (strings - records) % n) return 8;
    const size_t stride = (strings - records) / n;
    const size_t langs = stride > extra ? (stride - extra) / 4 : 8;
    return langs >= 2 && langs <= 8 ? langs : 8;
}
// the languages the copy's messages have (announcebattle's records; 8 when it can't tell)
int Viewer::copyLanguages() {
    std::vector<uint8_t>& d = messageFiles["announcebattle"];
    if (d.empty()) archive.read("message\\announcebattle.dat", d);
    return d.empty() ? 8 : int(messageLanguages(d));
}
std::string Viewer::messageText(const std::string& file, int id) {
    std::vector<uint8_t>& d = messageFiles[file];
    if (d.empty() && !archive.read("message\\" + file + ".dat", d)) return {};
    auto u32 = [&](size_t o) { return o + 4 <= d.size() ? uint32_t(d[o] | d[o + 1] << 8 | d[o + 2] << 16 | uint32_t(d[o + 3]) << 24) : 0u; };
    if (id < 1 || uint32_t(id) > u32(8)) return {};
    // (the option's language: English the second; 4 bytes a language - the Vita's 7, the others' 8)
    const size_t langs = messageLanguages(d), lang = size_t(std::clamp(optionLanguage, 0, 7)) < langs ? size_t(std::clamp(optionLanguage, 0, 7)) : 1;
    const size_t o = size_t(u32(4)) + u32(size_t(u32(0)) + size_t(id - 1) * 4 * langs + 4u * lang);
    std::string s;
    for (size_t k = o; k < d.size() && d[k]; ++k) s += char(d[k]);
    return s;
}
// BtlAnnounce::SetAnnounce's %s / %d, in turn
std::string Viewer::formatAnnounce(std::string t, const std::vector<std::string>& args) {
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
void Viewer::finishResult() {
    std::vector<std::string>& out = result.messages;
    for (const ResultMember& r : result.member) {
        if (r.ch < 0 || r.ch >= 9) continue;
        const ResultNow n = resultNow(r);
        const int up = n.level - r.level;
        const std::string name = messageText("charaname", r.id);
        const size_t k = size_t(&r - result.member);
        if (up == 1) result.levelLine[k] = formatAnnounce(announce(0x11), {name}); // (ProcessLvup1 / 2, SE 0xc3)
        else if (up > 1) result.levelLine[k] = formatAnnounce(announce(0x12), {name, std::to_string(up)});
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
    for (const std::string& s : result.levelLine)
        if (!s.empty()) SDL_Log("result: %s", s.c_str());
    for (const std::string& s : out) SDL_Log("result: %s", s.c_str());
}
void Viewer::deathEffect(const char* name, int f, BattleUnit& u) {
    commandEffect(name, f, false);
    if (commandEffects.empty()) return;
    SceneEffect& fx = commandEffects.back();
    fx.follows = -1;
    fx.place = u.home;
    unscaled(fx.place);
    fx.place.m[7] += cr2::deadOffset(u.figure);
}
// (BtlActionCtrl::CheckDead, at the blow: the one downed's action state 6 and +0x320 set -
// StepAction runs ActionDead's table for it every update alongside the action still playing,
// and ActionMain starts no next action till CheckStop finds none left in state 6. So a death
// starts with its blow - landHits -, goes on through the show, and the action's end waits for
// it; `collect`: also any not started yet, at the action's end)
bool Viewer::stepDeaths(bool collect) {
    if (collect && !deathsQueued) {
        deathsQueued = true;
        for (const ShowAction& a : showActions)
            for (const auto& e : a.hits)
                if (e.defeated && !e.regenerated && std::none_of(deaths.begin(), deaths.end(), [&](const Death& d) { return d.f == e.target; }))
                    deaths.push_back({e.target, 0, 0, battleClock});
    }
    bool wait = false;
    for (Death& d : deaths) {
        BattleUnit* u = unitOf(d.f);
        const cr2::BattleData::Monster* m = d.f >= 2 ? battleRules->fighter(d.f).monster : nullptr;
        if (!u || d.stage >= 9) continue;
        // (ActionDeadStandby: a monster with its +0x3a lets its damage motion play out first)
        if (d.stage == 0 && m && m->deathMotion && battleClock - d.hitAt < motionLength(*u, "bd01")) {
            wait = true;
            continue;
        }
        // (action_table_dead: 67 Standby, 69 WaitAwake, 68 DeadEvent - Dead, with the battle's own
        // script -, 9 its wait, 70 DeadStart: three updates more with the script)
        if (d.stage == 0 && ownBattleScript()) {
            if (d.scriptUntil < 0) d.scriptUntil = battleClock + 3 * cr2::EventRuntime::kTick;
            if (battleClock < d.scriptUntil) {
                wait = true;
                continue;
            }
        }
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
bool Viewer::stepAnalyze() {
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
void Viewer::beginResult() {
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
int Viewer::maxLevel() const { return game.flag(3039) ? 200 : game.flag(3038) ? 150 : 100; }
auto Viewer::resultNow(const ResultMember& r) const -> ResultNow {
    const float p = result.stage >= 4 ? 1.0f : float(result.count) / float(kLvupCount);
    ResultNow n;
    n.exp = r.exp0 + int(float(r.gain) * p);
    n.level = std::max(r.level, 1);
    const int most = maxLevel();
    while (n.level < most && cr2::levelExp(n.level + 1) <= n.exp) ++n.level;
    const int lo = cr2::levelExp(n.level), hi = cr2::levelExp(n.level + 1);
    n.next = n.level >= most ? 0 : hi - n.exp;
    n.percent = n.level >= most || hi <= lo ? 0 : int((int64_t(n.exp) - lo) * 100 / (hi - lo));
    n.hp = r.hp0 + int(float(r.hpMax - r.hp0) * p);
    return n;
}
void Viewer::beginGameOver() {
    gameOver = GameOverState();
    gameOver.stage = 1, gameOver.start = battleClock;
    const cr2::BattleData::Coupling* cp = battleData.coupling(battle.coupling);
    gameOver.over = (cp && cp->flag) || game.flag(0x7f9);
    announceQueue.clear();
    for (SceneEffect& fx : commandEffects) unload(*fx.fig);
    commandEffects.clear(), pendingEffects.clear();
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
void Viewer::endGameOver(bool title) {
    gameOver.stage = 0, announceHeld = false, commandShown.clear();
    battleFade = BattleFade();
    if (title) {
        SDL_Log("game over: the title");
        toTitle = true;
        return;
    }
    game.setFlag(0x7f8, true); // (StepBattleOut: lost with no game over)
}
void Viewer::updateGameOver() {
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
void Viewer::gameOverAnswer(bool yes) {
    if (gameOver.stage != 8) return;
    audio.playSe(yes ? 0xb4 : 0xb5, false);
    if (!yes) return endGameOver(true);
    gameOver = GameOverState();
    battleFade = BattleFade();
    leaveBattle();
    titleMenu.action = saveExists("cont.txt") ? 3 : 2; // (Cr3Util_Load_ContData, else Cr3Util_Load_GameData: the main loop loads it)
    SDL_Log("game over: try again (%s)", titleMenu.action == 3 ? "the continue data" : "the save");
}
void Viewer::drawGameOver(const Mat4& view, int w, int h) {
    if (gameOver.stage == 8) return drawAsk(announce(0x87), battleClock - gameOver.start, view, w, h, 7701, 7702);
    if (!gameOver.over || gameOver.hidden) return;
    UiFig& d = uiFig("dead"); // (BattleGameOverMenu: `dead` on its animation)
    d.fig.place = cr2::Mat34();
    poseUi(d, "dead", battleClock - gameOver.start);
    drawFigure(d.fig, view.data());
}
// the one who ended the battle, as BtlWork +0x16 has it after FixTodome: CheckDead's actor of
// the action that left no monster standing; alone 0; after a Pair turn kept with both
// standing, else the one standing; else that one if it stands (a monster's: the first if it
// stands), else the other
int Viewer::finisher() const {
    int last = 0;
    for (const ShowAction& sa : showActions)
        for (const auto& e : sa.hits)
            if (e.target >= 2 && e.defeated) last = sa.actor;
    const bool a0 = battleRules->fighter(0).alive, a1 = battleRules->fighter(1).present && battleRules->fighter(1).alive;
    if (fighterUnit[1] < 0) return 0;
    if (battleRules->fighter(0).partner >= 0) return a0 && a1 ? (last == 1 ? 1 : 0) : a0 ? 0 : 1;
    if (last == 1) return a1 ? 1 : 0;
    if (last == 0) return a0 ? 0 : 1;
    return a0 ? 0 : 1;
}
void Viewer::updateResult() {
    if (result.stage == 0 || result.stage >= 4) return;
    const bool tapped = result.tapped;
    result.tapped = false;
    const float t = battleClock - result.start;
    if (result.stage == 1 && t >= 1.2f) { // (SetVictoryMotion / SetVictoryCamera)
        result.stage = 2, result.start = battleClock;
        SDL_Log("result: victory at %.1f s", battleClock);
        commandShown = announce(6), commandShownStart = battleClock, announceHeld = true;
        // (BtlResult::SetVictoryMotion: SetMotion(2 - bb03 -, its table's loop flag): set looping
        // in its range, the pose held under the result window, as the reference video's)
        for (int k = 0; k < 2; ++k)
            if (battleRules->fighter(k).alive && fighterUnit[k] >= 0) {
                BattleUnit& wu = battleUnits[size_t(fighterUnit[k])];
                playMotion(wu, "bb03", battleClock, "bb03");
                cr2::MotionTimes wt;
                wu.loop = cr2::motionTimes(wu.figure, cr2::motionIndex("bb03"), wt) && (wt.flags & 8);
            }
        // (SetVictoryCamera: alone Cam_Win_PCsolo_00; after a Pair turn - BtlWork +0x523,
        // StepGetsetLoop's choice - the two standing Cam_Win_PCpair_00; else the one who struck
        // the last blow - +0x16, FixTodome's - alone: PCfighter for the first, PCpartner for
        // the other)
        const bool both = battleRules->fighter(0).alive && battleRules->fighter(1).alive;
        setBattleCamera(fighterUnit[1] < 0 ? "Cam_Win_PCsolo_00"
                        : battleRules->fighter(0).partner >= 0 && both ? "Cam_Win_PCpair_00"
                        : finisher() == 1 ? "Cam_Win_PCpartner_00" : "Cam_Win_PCfighter_00");
    } else if (result.stage == 2 && result.windowAt < 0 && (tapped || t > 5.0f)) { // (step 7: a tap, or 5 s)
        result.windowAt = battleClock + 2 * cr2::EventRuntime::kTick; // (step 8, then 9's ShowResult)
    } else if (result.stage == 2 && result.windowAt >= 0 && battleClock >= result.windowAt) {
        // (ShowResult: the HUD gone, the window, "Victory!" (0x10), the first of the 60 counts)
        result.stage = 3, result.start = result.windowStart = battleClock;
        SDL_Log("result: the window at %.1f s", battleClock);
        announceHeld = false, commandShown.clear();
        result.text = announce(16);
        result.step = 2, result.count = 1, result.sub = 0, result.timer = 0;
        result.nextTick = battleClock + cr2::EventRuntime::kTick;
        for (int k = 0; k < 2; ++k) result.levelShown[k] = result.member[k].level;
    } else if (result.stage == 3) {
        bool tap = tapped; // (a touch is the update's: lost if that update does not look)
        for (; result.stage == 3 && battleClock >= result.nextTick; result.nextTick += cr2::EventRuntime::kTick) {
            const bool tick = tap;
            tap = false;
            resultStep(tick);
        }
        if (tap) result.tapped = true; // (no update this frame: kept for the next)
    }
}
// One of BtlResult::Update's steps (the window's, from step 8): +0x20 counted first, then the step
void Viewer::resultStep(bool tapped) {
    constexpr float kTick = cr2::EventRuntime::kTick;
    constexpr int kSteps = int(sizeof(kResultSteps) / sizeof(kResultSteps[0]));
    result.timer += kTick;
    auto next = [&] { ++result.step; };
    auto skipTo = [&](int code) { // (ProcessLvup1 / 2 with no level: to the next step of that code)
        int i = result.step + 1;
        while (i < kSteps && kResultSteps[i] != code) ++i;
        result.step = i < kSteps ? i : kSteps - 1;
    };
    switch (result.step < kSteps ? kResultSteps[result.step] : 18) {
    case 7: // (a tap, or past 5 s since the last reset)
        if (tapped || result.timer > 5.0f) next(), result.timer = 0;
        break;
    case 8: next(); break; // (the finger up)
    case 10: // (0.3 s)
        if (result.timer > 0.3) next(), result.timer = 0;
        break;
    case 11: { // (SE 0xc5 looping while there is EXP or HP to go in)
        bool any = false;
        for (const ResultMember& r : result.member) any = any || (r.ch >= 0 && (r.gain > 0 || r.hp0 < r.hpMax));
        if (any) result.loopSe = audio.playSe(0xc5, true);
        next();
        break;
    }
    case 12: // (ProcessLvup: a 60th each update - a tap all at once -, then its SE stopped)
        if (result.count < kLvupCount) {
            result.count = tapped ? kLvupCount : result.count + 1;
            for (int k = 0; k < 2; ++k) // (CheckLvUp: 0xc3 each level a member passes)
                if (result.member[k].ch >= 0 && resultNow(result.member[k]).level > result.levelShown[k])
                    result.levelShown[k] = resultNow(result.member[k]).level, audio.playSe(0xc3, false);
            if (result.count == kLvupCount) finishResult();
            break;
        }
        if (result.loopSe >= 0) audio.stopSe(result.loopSe, 0);
        result.loopSe = -1;
        next();
        break;
    case 13:
    case 14: { // (ProcessLvup1 / 2: the member's levels gained, SE 0xc3 - else on to the next)
        const int k = kResultSteps[result.step] - 13;
        if (result.levelLine[k].empty()) {
            skipTo(kResultSteps[result.step] + 1);
            break;
        }
        result.text = result.levelLine[k];
        audio.playSe(0xc3, false);
        next();
        break;
    }
    case 15: // (ProcessResult: the line cleared - the SP, Oz and drops given)
        result.text.clear();
        next();
        break;
    case 16: { // (each message: the finger up, cleared, shown with its SE, then a tap - no time limit)
        const size_t i = size_t(result.sub >> 2);
        if (i >= result.messages.size()) {
            next();
            break;
        }
        const int part = result.sub & 3;
        if (part == 2) {
            result.text = result.messages[i];
            if (i < result.messageSe.size()) audio.playSe(result.messageSe[i], false);
        }
        if (part != 3 || tapped) ++result.sub;
        break;
    }
    case 17: // (HideBattleResult, the line cleared)
        result.text.clear();
        result.closeAt = battleClock;
        next();
        break;
    default: // (18: StepResult sees the end)
        result.stage = 4;
        SDL_Log("result: over at %.2f s - EXP %d, SP %d, Oz %d", battleClock, result.exp, result.sp, result.oz);
        break;
    }
}
// a number at a fighter's DummyDP01 (its bind pose's), as the hits' (DamageInfo00)
// (BtlEtcCtrl::CreateDispNumber's nudges by BtlMainCtrl's step: in StepAction (3) one of a pair
// 3 cm right and up - fighters 0 and 3 - or 3 cm left - its other -; in StepTurnEnd (4) fighter
// 0 3 cm right and up, fighter 3 3 cm left)
void Viewer::nudgeNumber(int f, bool turnEnd, float* p) const {
    const bool right = f == 0 || f == 3;
    if (turnEnd) {
        if (f == 0) p[0] += 0.03f, p[1] += 0.03f;
        else if (f == 3) p[0] -= 0.03f;
    } else if (battleRules && f >= 0 && f < 5 && battleRules->fighter(f).partner >= 0) {
        if (right) p[0] += 0.03f, p[1] += 0.03f;
        else p[0] -= 0.03f;
    }
}
void Viewer::showNumber(int f, int value, int set, bool turnEnd) {
    const int u = f >= 0 && f < 5 ? fighterUnit[f] : -1;
    if (u < 0) return;
    const BattleUnit& bu = battleUnits[size_t(u)];
    float p[3] = {bu.fig.place.m[3], bu.fig.place.m[7] + 0.15f, bu.fig.place.m[11]};
    const int dj = bu.fig.skeleton.joint(cr2::nameHash("DummyDP01"));
    if (dj >= 0) { // (GetPosition("DummyDP01"): the joint as the motion has it)
        const std::vector<cr2::Mat34> ww = bu.fig.skeleton.world(bu.fig.posed.empty() ? bu.fig.skeleton.basePose : bu.fig.posed, bu.fig.place);
        if (size_t(dj) < ww.size()) p[0] = ww[size_t(dj)].m[3], p[1] = ww[size_t(dj)].m[7], p[2] = ww[size_t(dj)].m[11];
    }
    nudgeNumber(f, turnEnd, p);
    DamageShown ds;
    ds.value = value, ds.set = set, ds.start = battleClock;
    for (int c = 0; c < 3; ++c) ds.at[c] = p[c];
    damageShown.push_back(ds);
}
void Viewer::beginShowAction(const ShowAction& a) {
    if (a.actor == 0 || a.actor == 1) lastPartyActor = a.actor;
    launchLead = 0, showItem = false, showSopia = actionSopiaTable = false, hitEffectEarly = false, timedCameras.clear();
    showStart = battleClock, showHitDone = false;
    const cr2::BattleData::Command* c = battleData.command(a.command);
    auto commandVoice = [&](float lead) { // (PlayVoiceCommand: +0x68 after +0x6a - an attack's in ActionAttackWaitAttackMotion, after its lead)
        const size_t before = voiceJobs.size();
        if (c && c->voice > 0) battleVoice(a.actor, c->voice, float(c->voiceDelay) * 0.05f + lead, true);
        if (voiceJobs.size() > before) voiceJobs.back().command = true;
    };
    commandVoiceSlot = -1, sopiaVoiceWait = false;
    if (!a.hits.empty() && a.hits.front().failed) commandVoice(0);
    if (!a.hits.empty() && a.hits.front().failed) { // (ActionDontAction*: its announcement alone, a moment)
        for (Move& m : showMoves) m = Move();
        showHitAt = 0, showCamera2 = -1;
        // (action_table_dont_action: ActionDontActionAnnounce sets its line for 2.0 s, then
        // ActionCommonCommandAnnounce - step 12 - waits till no announcement is left)
        showEnd = battleClock + 2.0f + cr2::EventRuntime::kTick;
        if (c) SDL_Log("  %s: %s - not done", battleRules->fighter(a.actor).name.c_str(), c->name[1].c_str());
        return;
    }
    if (a.actor >= 0 && a.actor < 2 && a.command == cr2::CommandInput::kDefend && !(!a.hits.empty() && a.hits.front().counter)) {
        for (Move& m : showMoves) m = Move();
        showHitAt = 0, showCamera2 = -1;
        showEnd = battleClock; // (action_table_empty: no announcement, no motion - the stance since the choice)
        SDL_Log("  %s: Defend (its stance)", battleRules->fighter(a.actor).name.c_str());
        commandVoice(0);
        return;
    }
    for (int f : {a.actor, a.partner}) // (SetAttackMotion: HideAttachObject)
        if (f >= 0 && f < 2 && guardObjects[f]) guardObjects[f]->shown = false, guardReturn[f] = true;
    SDL_Log("  action %s: %s (%.2f s)", battleRules->fighter(a.actor).name.c_str(), c ? c->name[1].c_str() : "?", battleClock);
    if (c && startScene(a, *c)) { // (an awakening's, an advent's, a monster's special: its scene)
        SDL_Log("  %s: %s", battleRules->fighter(a.actor).name.c_str(), c->name[1].c_str());
        commandVoice(0);
        return;
    }
    std::string motion = c ? c->motion : "bb01";
    if (c && motion.empty()) { // (BtlUtilData::GetMotionTypeFromCommand: none of its own - the plain attack's, the party's +0x44, a monster's +0x4c)
        const cr2::Fighter& af = battleRules->fighter(a.actor);
        int plain = af.attack;
        if (a.actor >= 2)
            if (const cr2::BattleData::Monster* mo = battleData.monster(af.id)) plain = mo->counter;
        if (const cr2::BattleData::Command* pc = battleData.command(plain); pc && !pc->motion.empty()) motion = pc->motion;
    }
    // (BtlUtilData::GetMotionTypeFromCommand by the motion's second letter: o / a / s an
    // attack's - 5 .. 7 -, p a pair's 8, m a sopia's 9 - bm02 an item's 10 -, b 1, x 11, g 2
    // - a sopia's for a monster -, i an item's 10, z a special's 12)
    const char kindLetter = motion.size() == 4 && motion[0] == 'b' ? motion[1] : 0;
    if (a.partner >= 0 && a.actor < 2 && (kindLetter == 'o' || kindLetter == 'a' || kindLetter == 's' || kindLetter == 'p'))
        motion = "bp01"; // (the pair's command: +0x46, bp01)
    // (action_table_sopia: bm01, bb01, bx01, a monster's bg01)
    const bool sopiaTable = !itemCommand(a.command) && c && c->kind != 0x1e && c->kind != 0x1f &&
                            ((kindLetter == 'm' && motion != "bm02") || kindLetter == 'b' || kindLetter == 'x' || (kindLetter == 'g' && a.actor >= 2));
    // (ActionSopiaStandby: bm01 asked for - but for commands 0x8f, 0x90, 0x92, which keep the
    // stance and wait for no motion -, bg01 for a monster's; the rest wait for none)
    const bool sopiaNoMotion = sopiaTable && kindLetter == 'm' && (a.command == 0x8f || a.command == 0x90 || a.command == 0x92);
    const bool sopiaWaitsMotion = sopiaTable && !sopiaNoMotion && (kindLetter == 'm' || kindLetter == 'g');
    if (sopiaNoMotion) motion = "bb01";
    // (ActionItemStandby: SetNextMotion(0x12) - bi01 - whatever the command's own motion; the
    // item's command has bm02, which no fighter has)
    if (itemCommand(a.command)) motion = "bi01";
    float length = 0, returned = -1;
    for (Move& m : showMoves) m = Move();
    const int target = a.hits.empty() ? -1 : a.hits.front().target;
    int slot = 0;
    // (ActionAttackPairWaitDelayTime: the pair's second after GetPairAttackDelayTime - by the two
    // characters, the first's row, in frames of 0.05 s. Its numbers are cr3_common_data[slot + 1]:
    // bytes 1 and 2 the battle's two characters, 3 and 4 the active members (game.party) - so a
    // monster pair's, slots 2 and 3, are the field party's, not the monsters'. Past 9 only 205
    // itself would be read, and nothing puts a monster's number there)
    static const unsigned char kPairDelay[9][9] = {{0, 6, 5, 4, 3, 6, 0, 0, 0},       {8, 0, 8, 6, 4, 10, 4, 0, 0},
                                                   {10, 8, 0, 7, 6, 10, 2, 0, 0},     {13, 12, 13, 0, 7, 14, 7, 0, 0},
                                                   {12, 11, 11, 11, 0, 13, 7, 0, 0},  {11, 10, 10, 8, 7, 0, 5, 0, 0},
                                                   {23, 20, 20, 20, 15, 21, 0, 0, 0}, {24, 24, 24, 24, 0, 24, 24, 0, 0},
                                                   {24, 24, 24, 24, 0, 24, 24, 0, 0}};
    auto pairDelay = [&](int first, int second) {
        int i = -1, j = -1; // (cr3_common_data's: from 0)
        if (first < 2 && second < 2) i = battleRules->fighter(first).id - 1, j = battleRules->fighter(second).id - 1;
        else if (first == 2 && second == 3) i = game.party[0], j = game.party[1]; // (bytes 3 and 4)
        return i >= 0 && i < 9 && j >= 0 && j < 9 ? float(kPairDelay[i][j]) / 20.0f : 0.0f;
    };
    // (a physical attack's lead - action_table_attack_solo: ... 6 the announcement, 15
    // ActionAttackStandby - the camera, the turn to the target, the attack motion asked for -,
    // 16 ActionAttackWaitAttackMotion till that motion is the one playing: the steps run on in
    // one update (ActionMain loops while the step changes), the motion's change takes its
    // updates. The reference video: Lockjaw's hit 1.2 s after its camera - the motion's 1.05 -,
    // Darwin's Slash's 0.67 s - 0.55: about three updates)
    float lead = 0;
    if (c && c->magic == 0 && a.actor >= 0 && fighterUnit[a.actor] >= 0) {
        cr2::MotionTimes t0;
        cr2::motionTimes(battleUnits[size_t(fighterUnit[a.actor])].figure, cr2::motionIndex(motion), t0);
        if (t0.moveStart >= 0 && t0.move2End >= 0) lead = 3 * cr2::EventRuntime::kTick;
    }
    commandVoice(lead);
    launchLead = lead;
    for (int f : {a.actor, a.partner}) {
        const int u = f >= 0 ? fighterUnit[f] : -1;
        if (u < 0) continue;
        BattleUnit& bu = battleUnits[size_t(u)];
        const float delay = lead + (f == a.partner && a.partner >= 0 ? pairDelay(a.actor, a.partner) : 0.0f);
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
        if (f == a.actor) showHitAt = t.hit[0] >= 0 ? t.hit[0] + delay : length * 0.5f;
        if (t.move2End >= 0) returned = std::max(returned, t.move2End + delay);
    }
    // (the attack's end - 21 ActionAttackReturn, 23 ActionAttackWaitFinishReturn and 5 to the
    // motion's second move's end - GetMotionTimeMoveEnd2, the actor back in place -, 22 / 26
    // the turn back and the stance, 12 the announcements gone, 8 / 9 Command_End, 24
    // ActionAttackWaitNextStart: 0.1 s - 1.6 s for command 0x130, 2.1 s for 0x145, 0x1a8 ..
    // 0x1aa - then action state 3, the next actor's turn, while 25 finishes the turn back; the
    // rest of the motion plays on under the next. The reference video: a Mantis's Lockjaw 2.6 s
    // from its camera to the next actor's - the lead and its return's end at 2.35 -, Darwin's
    // Slash 1.97 s - 1.60. A motion with no return keeps its length and 0.3 s.)
    const float nextStart = a.command == 0x130 ? 1.6f : (a.command == 0x145 || (a.command >= 0x1a8 && a.command <= 0x1aa)) ? 2.1f : 0.1f;
    showEnd = battleClock + (returned > 0 ? returned + nextStart + cr2::EventRuntime::kTick : std::max(length, 0.5f) + 0.3f);
    // (an item - action_table_item: 30 ItemStandby, 32 ItemWaitLaunch's 2.0 s, the target's
    // camera, 33 ItemWaitHit's 0.1 s and the hit, 34 ItemWaitMotion till its hit effect has
    // played out and the user stands, then on: the hit 2.1 s in, the end with its effects)
    showItem = itemCommand(a.command);
    if (showItem) showHitAt = 2.1f, showEnd = 1e9f;
    // (action_table_sopia - 35 ActionSopiaStandby the motion, its launch effect, the voice; 36
    // WaitMagicMotion till bm01 / bg01 plays; 37 WaitLaunch 2.0 s - none for bx01 -; 38 Launch
    // the hit's effect; 39 WaitHit the effect's +0x40 frames, then the numbers; 40 WaitEffect
    // till the hit's effect has played out; 41 WaitMotion 0.2 s - 0.7 for command 0x3e -; 42
    // WaitVoice. With no one to strike Launch goes to 42 at once. The reference video: Marie's
    // Banish - the caster's camera 0.30 s after the banner, the 424 2.38 s, the next banner
    // 1.93 s after that; the boss's Healing - its camera 0.28 s after the banner, the 333 2.36 s,
    // the turn's end 2.17 s after that)
    showSopia = actionSopiaTable = sopiaTable;
    sopiaStruck.clear();
    for (const auto& h : a.hits)
        if (std::find(sopiaStruck.begin(), sopiaStruck.end(), h.target) == sopiaStruck.end()) sopiaStruck.push_back(h.target);
    // (36 waits for the motion's name and its blend's end, +0x68: the change in the next update's
    // BtlMotionCtrl - SetMotionBlendStart -, then BtlModel::UpdateBlendAnimation's 0.3 s, done
    // once its time passes it - the party's and the monsters' alike, every bm01 / bg01 blending in;
    // the banner, set the same update, shows the next: six updates from it. The video: Marie's
    // Banish 0.30 s, the boss's Healing 0.28 s. A bm01 with no one to strike: the motion asked
    // for, +0x18 left 0 - no wait for it)
    sopiaCast = sopiaWaitsMotion && !(kindLetter == 'm' && sopiaStruck.empty()) ? 6 * cr2::EventRuntime::kTick : 0.0f;
    sopiaLaunch = sopiaCast + (kindLetter == 'x' ? 0.0f : 2.0f);
    // (no one struck: Launch on to 42 at once - ActionSopiaWaitVoice, while the command's voice
    // plays, 5 s at most)
    // A monster's bg01 (motion type 2) goes the same way with someone to strike: PreCommandAction,
    // PostCommandAction, the hit's SE, then 42 - no WaitHit, no WaitEffect.
    if (showSopia && (sopiaStruck.empty() || (kindLetter == 'g' && a.actor >= 2)))
        showHitAt = sopiaLaunch, showEnd = 1e9f, showSopia = false, sopiaVoiceWait = true;
    else if (showSopia) showHitAt = sopiaLaunch + float(cr2::effectSetting(c->hitEffect).hitWait) * 0.05f, showEnd = 1e9f;
    // (turned back - 44 ActionSopiaMirror the barrier's effect, 45 MirrorWaitEffect past 1.5 s,
    // 46 MirrorCamera SetTargetCamera(true) on the caster, 47 MirrorEffect the hit's effect on
    // it, 48 MirrorWaitHit the effect's +0x40 frames, the numbers, 49 MirrorWaitHitEffect)
    sopiaMirrored = showSopia && !a.hits.empty() && !a.hits.front().mirrors.empty();
    if (sopiaMirrored) showHitAt += 1.55f;
    // (an effect with a +0x40 wait - eff210, eff282, eff283, eff415 -: HitEffect at the launch,
    // the numbers that much after)
    hitEffectEarly = showSopia && !sopiaMirrored && cr2::effectSetting(c->hitEffect).hitWait > 0;
    if (hitEffectEarly) {
        const cr2::EffectSetting& set = cr2::effectSetting(c->hitEffect);
        std::vector<int> sides;
        for (int t : sopiaStruck) {
            PendingEffect pe;
            pe.name = c->hitEffect, pe.target = t, pe.pair = a.partner >= 0, pe.at = battleClock + sopiaLaunch;
            if ((a.partner >= 0 ? set.pairLocator : set.locator) == 7) { // (HitGroupEffect: once a side)
                const int side = t < 2 ? 0 : 1;
                if (std::find(sides.begin(), sides.end(), side) != sides.end()) continue;
                sides.push_back(side), pe.side = side;
            }
            pendingEffects.push_back(pe);
        }
    }
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
            if (a.partner >= 0) launchEffect("eff110", a.actor, true), launchEffect("eff110", a.actor, true);
            const cr2::Fighter& af = battleRules->fighter(a.actor);
            int plainId = a.partner >= 0 ? af.pairAttack : af.attack;
            if (a.actor >= 2)
                if (const cr2::BattleData::Monster* mo = battleData.monster(af.id)) plainId = a.partner >= 0 ? mo->pairAttack : mo->counter;
            const cr2::BattleData::Command* pc = battleData.command(plainId);
            if (a.command == plainId && pc && !pc->launchEffect.empty() && cr2::effectSetting(pc->launchEffect).pairLocator != 7) // (IsAttackAction: the attack's flow)
                launchEffect(pc->launchEffect, a.actor, a.partner >= 0);
        }
        // (ActionSopiaMirror / CreateMirrorEffect: on each whose magic barrier turns it back,
        // eff227 - at its side's middle when its setting says 7 - and SE 0x92)
        // (at the launch: ActionSopiaLaunch finds the barrier and jumps to 44)
        for (int m : a.hits.empty() ? std::vector<int>() : a.hits.front().mirrors) {
            const cr2::EffectSetting& ms = cr2::effectSetting("eff227");
            PendingEffect pe;
            pe.name = "eff227", pe.target = m, pe.at = battleClock + (showSopia ? sopiaLaunch : 0.0f), pe.se = 0x92;
            pe.side = ms.locator == 7 ? (m < 2 ? 0 : 1) : -1;
            pendingEffects.push_back(pe);
        }
        if (!c->launchEffect.empty()) {
            const cr2::EffectSetting& ls = cr2::effectSetting(c->launchEffect);
            // (ActionItemStandby: ShowCommandLaunchEffect with GetMotionTimeEffectStart(0x12, 0) -
            // bi01's effect start in the motion table, 1.1 s / Orlando's 1.15 -: the effect made
            // waiting, shown that long after - Chocolate's eff104 as the arm goes up)
            cr2::MotionTimes it;
            const BattleUnit* iu = showItem ? unitOf(a.actor) : nullptr;
            if (ls.pairLocator != 7 && iu && cr2::motionTimes(iu->figure, cr2::motionIndex("bi01"), it) && it.effectStart > 0)
                pendingEffects.push_back({c->launchEffect, a.actor, false, battleClock + it.effectStart});
            else if (ls.pairLocator != 7) launchEffect(c->launchEffect, a.actor, a.partner >= 0); // (ShowCommandLaunchEffect: its +0x32 7 left out - every caller passes true)
        }
    }
    // its cameras (BtlActionCtrl::SetAttackCamera1 / 2): by the party - its kind (0 / 1 the
    // one, 2 alone, 3 a pair), the pattern's base and the target, one of two at random, the
    // next at the hit; by a monster - the base, which it is and its target (3 a pair)
    showCamera2 = -1;
    if (showItem && a.actor < 2) {
        // (ActionItemStandby: the user's - GetUseItem(GetPlayerCamNo): 2 alone, 3 a pair, else
        // which; then ActionItemWaitLaunch's SetTargetCamera(false) on the one it is used on: a
        // party member's GetItemSopiaTarget by the same numbering, Cam_Unit_PCrevival_00 at
        // its place for a revival. The reference video, 2dy_mefNhEI 616.9 s: Marie's
        // Chocolate - on her at the banner, on the one healed 2.00 s later.)
        const bool alone = fighterUnit[1] < 0;
        setBattleCamera(cr2::cameraUseItem(alone ? 2 : a.partner >= 0 ? 3 : a.actor));
        TimedCamera tc;
        tc.at = 2.0f;
        if (useTargetCamera(a, c, tc.name, tc.place)) timedCameras.push_back(tc);
        SDL_Log("    camera %s", battleCamera.c_str());
    } else if (sopiaTable) {
        // (36 ActionSopiaWaitMagicMotion, as it moves on: GetUseSopia on the caster - the
        // party's by GetPlayerCamNo (2 alone, 3 a pair, else which); a monster's by the pattern
        // as its target's would be - 11 a pair's -, moved to its DummyCP unless its size is 4;
        // 37 ActionSopiaWaitLaunch, at its end: for one target of kind 0x10 the pattern's
        // GetSet, of kind 3 none, by motion types 2 / 11 (bg01, bx01) none, else - and for more
        // than one - SetTargetCamera(false). The reference video, 2dy_mefNhEI 638.47 s: Marie's
        // Banish - the banner, on her 0.30 s later, on the boss 2.03 s after.)
        TimedCamera caster;
        caster.at = sopiaCast;
        int k = -1;
        if (a.actor < 2) k = fighterUnit[1] < 0 ? 2 : a.partner >= 0 ? 3 : a.actor;
        else {
            const int p = battle.pattern;
            const cr2::BattleData::Monster* mo = battleData.monster(battleRules->fighter(a.actor).id);
            k = a.partner >= 0 ? 11 : p == 10 ? 10 : p == 11 ? (a.actor == 2 ? 8 : 9) : p == 12 ? 12 : p == 9 ? 13 : (mo ? mo->size : 0) + 4;
            if (mo && mo->size != 4 && fighterUnit[a.actor] >= 0) caster.place = targetFrame(battleUnits[size_t(fighterUnit[a.actor])]);
        }
        if (const char* n = cr2::battleCameraName(cr2::cameraUseSopia(k))) caster.name = n, timedCameras.push_back(caster);
        TimedCamera tc;
        tc.at = sopiaLaunch;
        if (sopiaMirrored) { // (the one it was cast on first, then the caster's, 1.55 s on)
            if (useTargetCamera(a, c, tc.name, tc.place, a.hits.front().mirrors.front())) timedCameras.push_back(tc);
            tc = TimedCamera();
            tc.at = sopiaLaunch + 1.55f;
            useTargetCamera(a, c, tc.name, tc.place, a.actor, true);
        } else if (sopiaStruck.size() == 1 && c->kind == 0x10) tc.name = battle.camera; // (GetGetSet: the pattern's)
        else if (sopiaStruck.size() >= 2 || (sopiaStruck.size() == 1 && c->kind != 3 && kindLetter != 'x' && !(kindLetter == 'g' && a.actor >= 2)))
            useTargetCamera(a, c, tc.name, tc.place);
        if (!tc.name.empty()) timedCameras.push_back(tc);
    } else if (target >= 0) {
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
                // (SetAttackCamera2, the next of the pair: 17 ActionAttackCamera, after 5's
                // WaitStep to the motion's hit less 4 frames - GetMotionTimeHitStart(9, 0, -4) -,
                // so 0.2 s before the hit, the attacker still in the shot as it strikes; the
                // reference video: Darwin's Slash cut at 521.4 s, its flash at 521.6 s)
                if (const char* n2 = cr2::battleCameraName(cam + 1)) {
                    TimedCamera tc;
                    tc.at = std::max(showHitAt - 4 * cr2::EventRuntime::kTick, 0.0f), tc.name = n2;
                    timedCameras.push_back(tc);
                }
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
    // (ActionCommonAnnounce: the name queued for 1.0 s - SetAnnounceTime -, the notes after it)
    if (c) commandShown = commandName(c->id), commandShownStart = announceAt = battleClock;
    // (ActionCommonAnnounce: a pair's attack - IsPairAction, IsAttackCommand - is named by the
    // acting character's +0x46, its pair command: "Pair Attack", as the reference video shows,
    // not the attack's own "Slash")
    if (c && a.partner >= 0 && a.actor < 2 && c->id == battleRules->fighter(a.actor).attack)
        if (const cr2::BattleData::Character* cd = battleData.character(battleRules->fighter(a.actor).id); cd && cd->pairAttack > 0)
            commandShown = commandName(cd->pairAttack);
    if (!a.hits.empty() && a.hits.front().counter) commandShown = announce(0x47), commandShownStart = announceAt = battleClock; // ("Counter")
}

void Viewer::poseBattleShow() {
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
void Viewer::updateBattleShow() {
    if (!battleRules || battleFight == 0) return;
    runPendingEffects();
    if (!deaths.empty() && showIndex < showActions.size()) stepDeaths(false); // (ActionDead alongside the action)
    // (ActionItemWaitMotion: the hit's effect played out and the user's motion of type 1 - bb.. -
    // back, then WaitStep(3, 3.0) - ActionCommonWaitTime against the time since
    // ActionCommonCommandStart)
    // (both wait on the hit's effect alone - +0x48, ShowCommandHitEffect's -, not the launch's)
    if (showItem && showHitDone && !hitEffectPlaying() && (showIndex >= showActions.size() || fighterReady(showActions[showIndex].actor)) && battleClock - showStart >= 3.0f)
        showItem = false, showEnd = battleClock + cr2::EventRuntime::kTick;
    if (sopiaVoiceWait && battleClock - showStart >= sopiaLaunch &&
        !(commandVoiceSlot >= 0 && audio.playing(commandVoiceSlot) && battleClock - showStart - sopiaLaunch < 5.0f))
        sopiaVoiceWait = false, showEnd = battleClock + cr2::EventRuntime::kTick;
    // (ActionSopiaWaitEffect, then ActionSopiaWaitMotion's 0.2 s - 0.7 for command 0x3e)
    if (showSopia && showHitDone && !hitEffectPlaying() && showIndex < showActions.size())
        showSopia = false, showEnd = battleClock + (showActions[showIndex].command == 0x3e ? 0.7f : 0.2f) + cr2::EventRuntime::kTick;
    if (openingBannerAt >= 0 && battleClock >= openingBannerAt) { // (StepBattleIn's step 7: the first strike's banner)
        openingBannerAt = -1;
        const bool solo = !battleRules->fighter(1).present;
        commandShown = battleRules->advantage() == 1 ? formatAnnounce(announce(solo ? 91 : 1), {fighterName(0)}) : announce(2);
        commandShownStart = announceAt = openingBannerShownAt = battleClock;
        SDL_Log("    announce: %s (%.2f s)", commandShown.c_str(), battleClock);
    }
    if (openingBannerShownAt >= 0 && battleClock - openingBannerShownAt >= 1.0f) { // (its 1.0 s: BtlAnnounce::Update, then Hide)
        openingBannerShownAt = -1;
        if (!announceHeld) commandShown.clear();
    }
    // (BtlAnnounce::Update: an entry shown on its first update, gone once its time - 1.0 s, set by
    // ActionCommonAnnounce and CalcCommand - is reached: 21 updates; the next the update after)
    if (!announceQueue.empty() && battleClock - announceAt >= kAnnounceStay) {
        commandShown = announceQueue.front(), commandShownStart = announceAt = battleClock;
        SDL_Log("    announce: %s (%.2f s)", commandShown.c_str(), battleClock);
        announceQueue.pop_front();
    }
    stepBattleShow();
    if (showIndex >= showActions.size()) return;
    updateScene();
    const ShowAction& a = showActions[showIndex];
    const float s = battleClock - showStart;
    for (size_t k = 0; k < timedCameras.size();)
        if (s >= timedCameras[k].at) {
            setBattleCamera(timedCameras[k].name, timedCameras[k].place);
            SDL_Log("    camera %s (%.2f s)", battleCamera.c_str(), battleClock);
            timedCameras.erase(timedCameras.begin() + std::ptrdiff_t(k));
        } else ++k;
    // the hits
    if (!scene.active && !showHitDone && s >= showHitAt) {
        showHitDone = true;
        if (showCamera2 >= 0) setBattleCamera(showCamera2);
        landHits(a, true);
    }
}
// A blow's reaction (BtlActionCtrl::CheckThrowBack): one of the party blocking it, or any one
// guarded by Auto Defend, bg01 once; one guarding (Defend) bg02 - where its table has it - back
// to bg01; in time stopped (21) none; in the Executioner's Pose (37) bx03, back to bx02; else
// bd01 (a monster blocking it: no damage, none)
void Viewer::throwBack(BattleUnit& bu, const cr2::BattleRules::Event& e) {
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
void Viewer::landHits(const ShowAction& a, bool first) {
    if (first) groupShown.clear();
    // (BtlSound's batches: the targets' cries queued by tag - SetVoiceTagFromData -, then at the
    // hit's end (BtlActionCtrl::Hit) the deaths' one at random when it wins, the game over's one
    // when it loses (the deaths' dropped), else all; the heals' one at random)
    if (first)
        for (const auto& e : a.hits)
            if (e.defeated && !e.regenerated && std::none_of(this->deaths.begin(), this->deaths.end(), [&](const Death& d) { return d.f == e.target; }))
                this->deaths.push_back({e.target, 0, 0, battleClock});
    std::vector<std::pair<int, int>> deaths, overs, heals; // (fighter, kind)
    for (const auto& e : a.hits) {
        const int u = fighterUnit[e.target];
        if (u < 0) continue;
        BattleUnit& bu = battleUnits[size_t(u)];
        const bool restore = e.heal > 0 || e.healMp > 0 || e.revived;
        const cr2::BattleData::Command* sc = battleData.command(e.command);
        const bool statusOnly = e.failed || (sc && (sc->kind == 1 || sc->kind == 3 || sc->kind == 26 || sc->kind == 28)); // (no number, no blow)
        if (first && !e.notes.empty() && actionSopiaTable) announceAt = -10; // (ActionSopiaWaitLaunch: ShowAnnounce(true) - the name skipped)
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
                // (CreateDispNumber: BtlModel::GetPosition("DummyDP01") - the joint as the motion has
                // it now; the base pose left it at the model's feet)
                const std::vector<cr2::Mat34> ww = bu.fig.skeleton.world(bu.fig.posed.empty() ? bu.fig.skeleton.basePose : bu.fig.posed, bu.fig.place);
                if (size_t(dj) < ww.size()) p[0] = ww[size_t(dj)].m[3], p[1] = ww[size_t(dj)].m[7], p[2] = ww[size_t(dj)].m[11];
            }
            // the hit's effect on it (BtlActionCtrl::HitEffect / ShowCommandHitEffect: the
            // command's +0x18 - an attack's (+0x49 0) whose element beat the target's, eff218 ..
            // eff220; a critical with the plain attack eff106 -, none here for a group's - its
            // setting's locator 7: HitGroupEffect, once)
            if (e.hit) { // (a restore's too: Chocolate's eff213, the reference video's swirl on the one healed)
                const cr2::BattleData::Command* ec = battleData.command(a.command);
                bool plain = e.actor >= 0 && e.actor < 2 && a.command == battleRules->fighter(e.actor).attack; // (a monster's: its +0x4c)
                if (e.actor >= 2)
                    if (const cr2::BattleData::Monster* en = battleData.monster(battleRules->fighter(e.actor).id)) plain = a.command == en->counter;
                std::string fx = ec ? ec->hitEffect : "";
                if (ec && ec->magic == 0 && e.burst >= 1 && e.burst <= 3) fx = "eff21" + std::to_string(7 + e.burst);
                if (e.critical && plain) fx = "eff106";
                if (!fx.empty() && !hitEffectEarly) {
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
                nudgeNumber(e.target, false, p);
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

auto Viewer::unitOf(int f) -> BattleUnit* {
    const int u = f >= 0 && f < 5 ? fighterUnit[f] : -1;
    return u >= 0 ? &battleUnits[size_t(u)] : nullptr;
}
void Viewer::unscaled(cr2::Mat34& m) {
    for (int c = 0; c < 3; ++c) {
        const float l = std::sqrt(m.m[c] * m.m[c] + m.m[4 + c] * m.m[4 + c] + m.m[8 + c] * m.m[8 + c]);
        if (l > 0) m.m[c] /= l, m.m[4 + c] /= l, m.m[8 + c] /= l;
    }
}
// a figure's joint in the world as it was last posed (false: none)
bool Viewer::jointPlace(const ViewFigure& f, const char* joint, cr2::Mat34& out) {
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
cr2::Mat34 Viewer::scenePlace(int target, bool apos, bool dpos, int tpos) {
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
cr2::Mat34 Viewer::turnY(float degrees) {
    const float r = degrees * 3.14159265f / 180.0f, c = std::cos(r), s = std::sin(r);
    cr2::Mat34 m;
    m.m[0] = c, m.m[2] = s, m.m[8] = -s, m.m[10] = c;
    return m;
}
void Viewer::setRotation(cr2::Mat34& m, const cr2::Mat34& from) {
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) m.m[r * 4 + c] = from.m[r * 4 + c];
}

// the action's scene, if its command has one (SetActionTable), begun: its steps (those of
// BuildAwakeFromMotionTable - the motion table's hits, the dash and the way back - in its
// place); false none
bool Viewer::startScene(const ShowAction& a, const cr2::BattleData::Command& c) {
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
    // (ActionAwakeStandby runs the commands at time 0 - its +0x30 0 - and asks for the motion;
    // ActionAwakeSyncMotion waits till the model's motion is it - 1 s at most - and starts +0x30
    // again: the rest, and the motion, run from there - three updates, as an attack's motion
    // change. The reference video, Darwin's Executioner: its 693 4.6 s after the banner, the
    // port's motion-table hit at 4.45 s)
    const bool awakening = !advent && a.actor >= 0 && a.actor < 2;
    const float lead = advent ? 2.25f : awakening ? 3 * cr2::EventRuntime::kTick : 0.0f;
    scene.early = awakening ? lead : 0.0f;
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
        playMotion(*bu, c.motion, battleClock + scene.early);
    }
    SDL_Log("  scene: %s %d steps", advent ? "advent" : "awakening", int(scene.steps.size()));
    return true;
}
// its effect in slot `id` (null: none)
auto Viewer::sceneEffect(int id) -> SceneEffect* {
    const auto it = scene.effects.find(id);
    return it != scene.effects.end() && it->second.fig ? &it->second : nullptr;
}
void Viewer::runStep(const cr2::SpecialStep& st) {
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
void Viewer::updateScene() {
    if (!scene.active) return;
    const float t = battleClock - scene.start;
    bool all = true;
    for (size_t k = 0; k < scene.steps.size(); ++k) {
        if (scene.done[k]) continue;
        if (t < scene.steps[k].time && !(scene.steps[k].time <= 0 && t >= -scene.early)) { // (time 0: at the standby)
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
bool Viewer::skipScene() {
    if (!scene.active || !scene.skippable || battleClock < scene.start) return false;
    const int command = showIndex < showActions.size() ? showActions[showIndex].command : 0;
    if ((battle.coupling == 0x11a && command == 0x7b) || ((battle.coupling == 0xdd || battle.coupling == 0x10f) && command == 0x160)) return false;
    SDL_Log("  scene skipped");
    cancelSceneAudio();
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
void Viewer::endScene() {
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
bool Viewer::drawEffect(SceneEffect& fx, const cr2::Mat34& camera, const Mat4& vp) {
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
void Viewer::drawSceneEffects(const cr2::Mat34& camera, const Mat4& vp) {
    for (auto it = scene.effects.begin(); it != scene.effects.end();)
        it = drawEffect(it->second, camera, vp) ? std::next(it) : scene.effects.erase(it);
    for (size_t k = 0; k < commandEffects.size();)
        if (drawEffect(commandEffects[k], camera, vp)) ++k;
        else commandEffects.erase(commandEffects.begin() + std::ptrdiff_t(k));
    for (auto& g : guardObjects)
        if (g) drawEffect(*g, camera, vp);
}
void Viewer::guardObject(int f, float delay) {
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
void Viewer::dropGuardObject(int f) {
    if (guardObjects[f]) unload(*guardObjects[f]->fig);
    guardObjects[f].reset(), guardReturn[f] = false;
}
void Viewer::setGuard(int f, bool on) {
    BattleUnit* u = unitOf(f);
    if (!u || u->guarding == on) return;
    u->guarding = on;
    u->motion = on ? "bg01" : "bb01", u->then = u->motion, u->loop = true, u->hold = false, u->motionStart = battleClock;
    if (on && battleRules->fighter(f).id == 6) guardObject(f, 0.2f);
    else if (!on) dropGuardObject(f);
}
// the stances by the orders chosen so far
void Viewer::updateGuards() {
    bool want[2] = {false, false};
    for (const cr2::BattleRules::Order& o : commandInput.orders())
        if (o.command == cr2::CommandInput::kDefend && o.actor >= 0 && o.actor < 2) {
            want[o.actor] = true;
            if (commandInput.pairTurn()) want[0] = want[1] = true;
        }
    for (int f = 0; f < 2; ++f)
        if (battleRules->fighter(f).alive) setGuard(f, want[f]);
}
// the action's hit effect still playing (BtlActionCtrl +0x48: ShowCommandHitEffect's)
bool Viewer::hitEffectPlaying() const {
    if (showIndex >= showActions.size()) return false;
    const cr2::BattleData::Command* c = battleData.command(showActions[showIndex].command);
    if (!c || c->hitEffect.empty()) return false;
    // (by its own time, not by its being drawn and erased: one never drawn, looping, kept or
    // without an animation holds nothing - the original's +0x48 waits only on an effect of
    // kind 1 / 2, one playing out)
    for (const SceneEffect& fx : commandEffects) {
        if (!fx.set || !fx.set->name || c->hitEffect != fx.set->name || fx.kept || fx.set->loop) continue;
        if (fx.end >= 0 ? battleClock < fx.end : fx.fig && fx.fig->animLoaded && battleClock < fx.start + fx.fig->anim.duration()) return true;
    }
    return false;
}
void Viewer::launchEffect(const std::string& name, int target, bool pair) {
    if (launchLead <= 0) return commandEffect(name, target, pair);
    pendingEffects.push_back({name, target, pair, battleClock + launchLead});
}
void Viewer::runPendingEffects() {
    for (size_t i = 0; i < pendingEffects.size();)
        if (battleClock >= pendingEffects[i].at) {
            const PendingEffect e = pendingEffects[i];
            pendingEffects.erase(pendingEffects.begin() + long(i));
            commandEffect(e.name, e.target, e.pair, e.side);
            if (e.se >= 0) battleSe(e.se);
        } else ++i;
}
void Viewer::commandEffect(const std::string& name, int target, bool pair, int side) {
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

void Viewer::drawBattle(int w, int h, bool mapOnly) {
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
void Viewer::glassSize(int& hw, int& hh) const {
    hh = 320;
    hw = std::max(int(320.0f * float(frameW) / float(std::max(frameH, 1)) + 0.5f), 1);
}
// BtlMain::PostEffectGlassFragments: the map alone (passes 2, 4, 8) drawn into render target
// 5, its texture the shards' (BtlPostEffectGlassFragment::Setup)
void Viewer::grabGlass(int fw, int fh) {
    glassPending = false;
    dev->beginFrame(fw, fh);
    placeScreen(fw, fh);
    drawBattle(frameW, frameH, true);
    dev->endScene();
    dev->endFrame(false);
    std::vector<uint8_t> px;
    int w = 0, h = 0;
    if (!dev->readPixels(px, w, h) || frameW <= 0 || frameH <= 0 || frameX + frameW > w || frameY + frameH > h) {
        SDL_Log("the screen breaks away: not caught (%dx%d of %dx%d)", frameW, frameH, w, h);
        return;
    }
    cr2::Image img;
    img.width = frameW, img.height = frameH;
    img.rgba.resize(size_t(frameW) * frameH * 4);
    for (int y = 0; y < frameH; ++y) { // (bottom-up: a render target's v = 0 its bottom)
        const uint8_t* row = &px[(size_t(frameY + frameH - 1 - y) * w + frameX) * 4];
        std::memcpy(&img.rgba[size_t(y) * frameW * 4], row, size_t(frameW) * 4);
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
void Viewer::drawGlass() {
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
void Viewer::spawnEffect(const std::string& name, const float at[3]) {
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
void Viewer::drawEffects(const cr2::Mat34& camera, const Mat4& vp) {
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

// BattleWindowEnchant2::SetSign: sign n (EnchantParamPackTable's u0, v0, u1, v1 of
// hud_enchant) a 0.048-unit square at (x, y) - 1 white "i" (no element), 2 red, 3 cyan, 4 green
// (the elements 1..3), 25 the clock, the others the states
void Viewer::drawEnchant(int sign, float x, float y, const Mat4& proj) {
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
void Viewer::drawStatusSign(int n, float x, float y, const Mat4& proj) {
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
int Viewer::elementSign(const cr2::Fighter& f) { return f.element >= 1 && f.element <= 3 ? f.element + 1 : 1; }
// its states' signs after the element's (GetCharaUIState: 0x536648's states as bits - poison 4,
// blind 5, seal 6, 12 7, 35 9, 30 10, 29 11, 33 12, raised Attack .. Luck 14..18, lowered 19..23,
// time stopped 24; 11 or 32 bit 8; skill 5, 9 bit 13 - the sign the bit + 1, in the bits' order)
std::vector<int> Viewer::stateSigns(const cr2::Fighter& f) {
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
// a point of the battle on the screen in UI units (false: behind the camera)
bool Viewer::battleUi(const float* p, float& x, float& y) const {
    const float* mm = battleVP.data();
    const float cw = mm[3] * p[0] + mm[7] * p[1] + mm[11] * p[2] + mm[15];
    if (cw <= 0) return false;
    const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
    x = (mm[0] * p[0] + mm[4] * p[1] + mm[8] * p[2] + mm[12]) / cw * float(frameW) * 0.5f / k;
    y = (mm[1] * p[0] + mm[5] * p[1] + mm[9] * p[2] + mm[13]) / cw * float(frameH) * 0.5f / k;
    return true;
}
// the command menu (BattleFirstMenu / BattleSoloMenu / BattleSelectMenu: btlBtn and its
// animator, a circleBtn_fr at each call_circleBtn_frNN joint - its frame NN)
// `shown`: the buttons to draw (an empty list: every one); each drawn button's tap area kept
// (tapAreaS01 / tapAreaE01) for clicks; `labels` text at a button's typeM_C01 (the targets')
void Viewer::drawCommandMenu(const std::string& anim, float t, const Mat4& view, const std::vector<int>& shown, const std::map<int, std::string>& labels, int w, int h, int frameOf, const std::string& figure) {
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
// a sopia's row icon (CampSopiaListMenu::SetSopiaList, CampEquipmentListMenu): a human's (its
// +0x10 0) sopia_id at GetHumanSopiaIconNumber's frame - 1, 4, 3, 2 by its kind (+0x11) -, a
// monster's mainIcon_id at GetMonsterSopiaIconNumber's - 65, 68, 67, 66
void Viewer::sopiaRowIcon(const cr2::BattleData::Sopia& so, ListRow& r) {
    static const int kHuman[4] = {1, 4, 3, 2}, kMonster[4] = {65, 68, 67, 66};
    const int k = so.kind - 1;
    r.sopiaIcon = so.growth == 0;
    r.icon = k >= 0 && k < 4 ? (r.sopiaIcon ? kHuman[k] : kMonster[k]) : 0;
}
void Viewer::drawSkillList(const std::vector<ListRow>& rows, int focused, float t, const Mat4& view, int w, int h, int tagFrame, int costIcon) {
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
void Viewer::drawChoosing(const Mat4& view, int w, int h) {
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
void Viewer::drawStatusWindows(const StatusShown* rows, int count, float t, const Mat4& view, const Mat4& proj, bool twoAlways) {
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
void Viewer::drawResult(const Mat4& view, const Mat4& proj, int w, int h) {
    const float t = battleClock - result.windowStart;
    // (EndResultWindow: ResultMenu, its SystemWindowMenu (4) and CharaStatusWindowMenu (1) each
    // InterfaceCloseAnimeSet - its animation backwards (+0x5c 2) from the frame it stands at -, the
    // EXP and SP numbers deleted)
    const bool closing = result.closeAt >= 0;
    auto partTime = [&](const std::string& fig, const std::string& anim) {
        if (!closing) return t;
        UiFig& u = uiFig(fig);
        poseUi(u, anim, 0);
        const float len = u.fig.animLoaded ? u.fig.anim.duration() : 0.0f;
        return std::max(std::min(result.closeAt - result.windowStart, len) - (battleClock - result.closeAt), 0.0f);
    };
    auto part = [&](const std::string& fig, const std::string& anim, const char* hide = nullptr) -> UiFig& {
        const float pt = partTime(fig, anim);
        UiFig& u = uiFig(fig);
        u.fig.place = cr2::Mat34();
        poseUi(u, anim, pt);
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
    if (!closing) { // (ShowResult: the EXP and SP in from the window's update)
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
    drawStatusWindows(rows, count, partTime("campwin", "campwin2_io"), view, proj, true);
    UiFig& sys = part("syswin_io", "syswin_io");
    if (!result.text.empty() && uiJoint(sys, "typeM_L01", x, y)) drawUiText(result.text, 24, x, y, w, h, true);
}
void Viewer::drawBattleHud(int w, int h) {
    if (!(gameOver.stage > 0 && gameOver.hidden)) drawBattleHudParts(w, h);
    if (gameOver.stage == 0 || !battleMode || !battleRules) return;
    const Mat4 proj = uiProjection(w, h);
    Mat4 camera = cr2::mat4Identity();
    camera[14] = -0.881f;
    const Mat4 view = cr2::mat4Multiply(proj, camera);
    tapRects.clear();
    drawGameOver(view, w, h);
}
void Viewer::drawBattleHudParts(int w, int h) {
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
void Viewer::drawBattleHudMain(int w, int h) {
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
        // (while the turn's actions run - BtlAnnounce +0x131 clear - the window stays with the last
        // text till another replaces it or the actions are done: the reference video's
        // "Chocolate" 4.3 s, "Banish" 4.2 s, "Healing" till its turn's end)
        const bool msgShown = !commandShown.empty() && (announceKept || battleClock - commandShownStart < 3.0f || announceHeld);
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

} // namespace cr2_viewer
