#include "viewer/viewer.hpp"

#include "assets/release.hpp"

namespace cr2_viewer {

bool Viewer::fieldControl() const { return script && !script->running() && pendingCall.empty() && controlHold == 0; }
void Viewer::queueEvent(const std::string& function) {
    SDL_Log("%s: %s at update %d", field.info().name.c_str(), function.c_str(), script ? script->ticks() : 0);
    pendingCall = function, pendingCallWait = 1;
    balloons.clear(), nearestBalloon = -1;
}
void Viewer::touchEvents(const cr2::Vec3& p) {
    if (!script) return;
    std::set<std::string> now;
    for (const cr2::Gimmick& g : field.gimmicks()) {
        if (g.kind != cr2::Gimmick::Event || g.check) continue;
        const float at[3] = {p[0], p[1], p[2]};
        if (!g.touches(at, controller->radius)) continue;
        now.insert(g.name);
        const auto st = script->gimmicks().find(g.id);
        const bool enabled = st == script->gimmicks().end() || st->second.event;
        if (insideEvents.count(g.name) || !enabled || !fieldControl()) continue;
        SDL_Log("%s: touched %s - %s", field.info().name.c_str(), g.name.c_str(), g.function.c_str());
        queueEvent(g.function); // (CollisionEventExec the update after, EventExecute the one after that)
    }
    insideEvents.swap(now);
}
// an event's scene putting the leader at its p000 (FldUtilSetFieldMapPlayerPos / Rot)
void Viewer::placePlayer(const cr2::FieldScript::Placement& pl) {
    if (pl.heading) heading = pl.yaw, idleTurn.on = false; // (FldUtilSetFieldMapPlayerRot)
    if (!pl.position || !controller) return;
    controller->setPosition({pl.at[0], pl.at[1] + 0.17f, pl.at[2]}); // (dropped onto the floor, as on arriving)
    for (int k = 0; k < 10; ++k) controller->update({0, 0, 0});
    const cr2::Vec3& p = controller->position();
    player[0] = p[0], player[1] = p[1] - 0.02f, player[2] = p[2];
    std::memcpy(playerPrev, player, sizeof player);
    std::memcpy(playerCur, player, sizeof player);
    stepClock = 0, substep = 0;
}
uint32_t Viewer::boxRandom() { // (lrand48's part: a simple generator of the viewer's own)
    static uint32_t x = 0x2545f491u;
    x ^= x << 13, x ^= x >> 17, x ^= x << 5;
    return x;
}
void Viewer::checkBalloons() {
    balloons.clear(), nearestBalloon = -1, nearestBalloonDist = 30000;
    if (!fieldControl()) return; // (CheckBalloon: FldMain +0x28 == 1, the leader's control)
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
        if (d < best) best = d, nearestBalloon = int(i), nearestBalloonDist = d;
    }
}
// the nearer of the nearest balloon and the touch action's mark (0 none, 1 the balloon, 2 the mark)
int Viewer::nearestTarget(float* at, int& kind) const {
    float best = 30000;
    int which = 0;
    if (nearestBalloon >= 0) {
        const cr2::Gimmick& g = field.gimmicks()[size_t(nearestBalloon)];
        at[0] = g.position[0], at[1] = g.position[1], at[2] = g.position[2], best = nearestBalloonDist, which = 1, kind = 7;
    }
    if (actionMark.gimmick >= 0 && script) {
        if (const cr2::FieldScript::TouchAction* a = script->touchAction(field.gimmicks()[size_t(actionMark.gimmick)].name); a && !a->spots.empty()) {
            const std::array<float, 3>& m = a->spots[a->markSpot()];
            const float d = std::hypot(m[0] - playerCur[0], m[1] - playerCur[1], m[2] - playerCur[2]);
            if (d < best) at[0] = m[0], at[1] = m[1], at[2] = m[2], which = 2, kind = actionMark.kind;
        }
    }
    return which;
}
void Viewer::updateFieldCamera(float dt) {
    if (!fieldMode || camW <= 0) return;
    constexpr float kTick = cr2::EventRuntime::kTick;
    const float unit = float(camW) / 960.0f;
    fieldCam.clock = std::min(fieldCam.clock + dt, 10 * kTick);
    for (; fieldCam.clock >= kTick; fieldCam.clock -= kTick) {
        std::memcpy(fieldCam.prev, fieldCam.cur, sizeof fieldCam.cur);
        if (script && script->camera().scripted()) { // (mode 1: ExecuteCameraMove's SetDirectScroll)
            fieldCam.cur[0] = -script->camera().x, fieldCam.cur[1] = -script->camera().y, fieldCam.set = true;
            continue;
        }
        if (fieldControl()) { // (+0x80 kept as it was while the leader is not in control)
            float at[3];
            int kind = 0;
            const int which = nearestTarget(at, kind);
            fieldCam.attention = which == 2 && (kind == 1 || kind == 3 || kind == 5);
            if (fieldCam.attention) std::memcpy(fieldCam.at, at, sizeof at);
        }
        float focus[3] = {player[0], player[1] + 0.045f, player[2]};
        if (fieldCam.attention)
            for (int k = 0; k < 3; ++k) focus[k] = player[k] + (fieldCam.at[k] - player[k]) * 0.5f;
        float sx, sy;
        field.focusScroll(camW, camH, focus, sx, sy);
        sx /= unit, sy /= unit;
        if (!fieldCam.set) {
            fieldCam.cur[0] = fieldCam.prev[0] = sx, fieldCam.cur[1] = fieldCam.prev[1] = sy, fieldCam.set = true;
            continue;
        }
        const float dx = sx - fieldCam.cur[0], dy = sy - fieldCam.cur[1];
        if (std::hypot(dx, dy) * unit > 1.0f) { // (within a pixel: held)
            const float k = fieldCam.attention ? 0.1f : 0.2f;
            fieldCam.cur[0] += dx * k, fieldCam.cur[1] += dy * k;
        }
    }
}

// (SetPlayerRotationToBalloon: the nearer of the balloon and the mark - none, no turn)
void Viewer::startIdleTurn() {
    if (!(idleTrig || changeTurn) || !script) return;
    changeTurn = false;
    float tx = 0, tz = 0, best = 30000;
    bool any = false;
    if (nearestBalloon >= 0) {
        const cr2::Gimmick& g = field.gimmicks()[size_t(nearestBalloon)];
        tx = g.position[0], tz = g.position[2], best = nearestBalloonDist, any = true;
    }
    if (actionMark.gimmick >= 0) {
        if (const cr2::FieldScript::TouchAction* a = script->touchAction(field.gimmicks()[size_t(actionMark.gimmick)].name); a && !a->spots.empty()) {
            const std::array<float, 3>& m = a->spots[a->markSpot()];
            const float d = std::hypot(m[0] - playerCur[0], m[1] - playerCur[1], m[2] - playerCur[2]);
            if (d < best) tx = m[0], tz = m[2], any = true;
        }
    }
    if (!any) return;
    idleTurn.on = true, idleTurn.count = 0, idleTurn.from = heading, idleTurn.to = std::atan2(tx - player[0], tz - player[2]);
    SDL_Log("%s: the leader turns to the nearest %s over 10 updates (%.0f degrees)", field.info().name.c_str(),
            nearestBalloon >= 0 && tx == field.gimmicks()[size_t(nearestBalloon)].position[0] ? "balloon" : "mark",
            std::remainder(idleTurn.to - idleTurn.from, 6.28318531f) * 57.29578f);
}
bool Viewer::walkStart(float fx, float fy) {
    if (!fieldTabShown() || fieldTabOpen || !fieldControl()) return false;
    walkDrag = WalkDrag();
    walkDrag.down = true, walkDrag.fx0 = walkDrag.fx = fx, walkDrag.fy0 = walkDrag.fy = fy, walkDrag.at = uiClock;
    return true;
}
void Viewer::walkMove(float fx, float fy) {
    walkDrag.fx = fx, walkDrag.fy = fy;
    if (std::hypot(fx - walkDrag.fx0, fy - walkDrag.fy0) > 0.01f) walkDrag.moved = true;
}
void Viewer::walkEnd() {
    if (walkDrag.down && !walkDrag.moved) tap();
    walkDrag = WalkDrag();
}
bool Viewer::walkStick(float& sx, float& sy, bool& run) const {
    if (!walkDrag.down || !fieldTabShown()) return false;
    const float dx = walkDrag.fx - walkDrag.fx0, dy = walkDrag.fy0 - walkDrag.fy, len = std::hypot(dx, dy);
    if (len < 0.02f) return false;
    sx = dx / len, sy = dy / len, run = len >= 0.05f;
    return true;
}
void Viewer::drawWalkStick(const Mat4& view) {
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
auto Viewer::menuList() -> MenuList {
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
bool Viewer::menuScroll(int rows) {
    const MenuList list = menuList();
    if (!list.cursor || list.count <= 0) return false;
    const int old = *list.cursor;
    *list.cursor = mobile::scrollCursor(old, list.count, rows);
    if (*list.cursor != old && audio.opened()) audio.playSe(0xb3, false);
    return true;
}
void Viewer::cancelPointerInput() {
    menuSwipe = mobile::MenuSwipe();
    walkDrag = WalkDrag();
    fastHeld = false, fastUntil = 0;
    keyLock.pressing = vslash.pressed = false;
    if (iai.stage == 2) iai.stage = 1;
    if (aim.stage == 2) aim.stage = 1;
    pointerCancelled = true;
    pointerDown = false; // (a gesture cut off - the app paused, the focus lost: no finger left to wait for)
    padHeldOver.cancel();
}
bool Viewer::consumeInputCancel(crinput::CancelWatch& cancel) {
    if (!cancel.consume()) return false;
    cancelPointerInput();
    return true;
}
bool Viewer::menuPointerDown(float x, float y) {
    const MenuList list = menuList();
    if (!list.cursor || list.count <= 0 || x < 0.1f || x > 0.98f || y < 0.12f || y > 0.88f) return false;
    menuSwipe.begin(x, y, list.context);
    return true;
}
void Viewer::menuPointerMove(float x, float y) {
    if (!menuSwipe.down) return;
    const auto list = menuList();
    if (!list.cursor || list.context != menuSwipe.context) { menuSwipe = {}; return; }
    menuScroll(menuSwipe.move(x, y));
}
bool Viewer::menuPointerUp(float x, float y) {
    if (!menuSwipe.down) return false;
    menuPointerMove(x, y);
    const auto gesture = menuSwipe;
    menuSwipe = {};
    if (gesture.down && !gesture.moved) pointerActivate(x, y);
    return true;
}
void Viewer::pointerActivate(float fx, float fy) {
    if (overFast(fx, fy)) fastHeld = true; // (the fast-forward button: held, not a tap)
    else if (titleMenu.stage) titleClick(fx, fy);
    else if (yesNoClick(fx, fy)) {}
    else if (battleMode && (choosing || gameOver.stage == 8)) battleClick(fx, fy); // (the commands; the game over's question)
    else if (vslash.stage == 1) vslashPointer(fx, fy, 0);
    else if (fieldMode && !eventMode && keyLock.stage != 0) unlockPointer(fx, fy, 0);
    else if (fieldMode && !eventMode && iai.stage) iaiPointer(fx, fy, 0);
    else if (fieldMode && !eventMode && aim.stage) aimPointer(fx, fy, 0);
    else if (fieldMode && !eventMode && hakkei.stage) hakkeiPointer(fx, fy);
    else if (fieldMode && !eventMode && mystic.stage) mysticPointer(fx, fy);
    else if (fieldMode && !eventMode && (sprintTap(fx, fy) || actionTap(fx, fy))) {}
    else if (fieldMode && !eventMode && fieldClick(fx, fy)) {
    } else if (fieldMode && !eventMode && walkStart(fx, fy)) { // (a walk, or the tap on its release)
    } else if (eventMode || fieldMode || battleMode) tap(fx, fy);
}
// the cut-ins' message window's own tap area (msgWin's tapAreaS01 .. E01, as the window stands
// now): a tap there skips its typing (ConversationWindowMenu::TouchGestureTap), one elsewhere
// only turns a typed page; a tap without a place (a key) counts as on it
bool Viewer::onConversationWindow(float fx, float fy) {
    if (fx < 0 || fy < 0) return true;
    float sx, sy, ex, ey;
    if (!windowJoint(conversationWindow, "tapAreaS01", sx, sy) || !windowJoint(conversationWindow, "tapAreaE01", ex, ey)) return true;
    const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
    const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
    return x >= std::min(sx, ex) && x <= std::max(sx, ex) && y >= std::min(sy, ey) && y <= std::max(sy, ey);
}
void Viewer::tap(float fx, float fy) {
    if (titleMenu.stage) return titleTap();
    if (vslash.stage) return; // (the slash: a swipe, not a tap)
    if (battleMode) {
        if (skipScene()) return;
        if (battleScript && battleScript->running()) battleScript->events().touch(onConversationWindow(fx, fy));
        else if (analyze.stage == 1 || analyze.stage == 2) analyze.tapped = true;
        else if (result.stage > 0) result.tapped = true;
        else if (gameOver.stage > 0) gameOver.tapped = true;
        return;
    }
    if (eventMode) {
        if (event) event->touch(onConversationWindow(fx, fy));
        return;
    }
    if (!fieldMode || !script) return;
    if (script->running()) {
        script->events().touch(onConversationWindow(fx, fy));
        return;
    }
    if (!fieldControl()) return; // (steps 0x1b and 1: no tap read)
    if (const int s = sprintAt(); s >= 0) return startSprint(s); // (FldMain: a sprint's before the balloons)
    if (actionMark.gimmick >= 0 && actionMark.startedAt < 0) return actionTapped(); // (then a touch action's)
    if (nearestBalloon < 0 || keyLock.stage != 0) return;
    const cr2::Gimmick& g = field.gimmicks()[size_t(nearestBalloon)];
    // (no turn at the tap: the leader turned to the nearest balloon or mark when it stopped -
    // SetPlayerRotationToBalloon, startIdleTurn -; a talk's script turns it, ModelDir_Field)
    SDL_Log("%s: tapped %s - %s", field.info().name.c_str(), g.name.c_str(), g.function.c_str());
    std::string function = g.function;
    if (g.kind == cr2::Gimmick::Box) { // (FldMain's balloon of a box: opened, then its function - box_msg)
        if (g.boxKind == 1 && script->leader() != 1) function = "Message_obox"; // (a key box: only Orlando opens it)
        else if (g.boxKind == 1) return startUnlock(nearestBalloon); // (FldUnlock first)
        else openBox(g);
    }
    queueEvent(function); // (step 0x1b; EventExecute the update after)
}
void Viewer::callGimmick(const std::string& function) {
    if (!script->call(function)) SDL_Log("%s", script->error().c_str());
    else if (!script->running() && playMode) pendingBranch = true;
    checkBalloons();
}
// FldBox::Open: the item one more (Cr3UtilGetItem), the box opening (fa01, then fn02), SE 0x153
void Viewer::openBox(const cr2::Gimmick& g) {
    const int item = game.boxes.item(g.boxKind, g.boxNumber, game.party);
    game.boxes.markOpen(g.boxKind, g.boxNumber);
    if (item > 0 && item < 300) game.itemCount[item] = static_cast<short>(std::min(game.itemCount[item] + 1, 99));
    audio.playSe(0x153, false);
    boxOpening[g.name] = uiClock;
    script->boxItem = item;
    SDL_Log("%s: box %s - item %d (%d now)", field.info().name.c_str(), g.id.c_str(), item, item > 0 && item < 300 ? game.itemCount[item] : 0);
}
void Viewer::startUnlock(int gimmick) {
    keyLock.gimmick = gimmick, keyLock.orlando = true;
    keyLock.stage = 1, keyLock.stageAt = uiClock, keyLock.spread = 0, keyLock.pressing = false;
    audio.playSe(0x157, false); // (CheckTouchEventExec_Tap: FldUnlock's start)
    SDL_Log("%s: the lock of %s", field.info().name.c_str(), field.gimmicks()[size_t(gimmick)].id.c_str());
}
void Viewer::unlockEnd(bool success) {
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
void Viewer::updateKeyLock() {
    KeyLock& k = keyLock;
    if (k.stage == 2) {
        if (--k.countdown > 0) return;
        k.stage = 0;
        if (k.gimmick < 0 || size_t(k.gimmick) >= field.gimmicks().size()) return;
        const cr2::Gimmick& g = field.gimmicks()[size_t(k.gimmick)];
        k.gimmick = -1;
        openBox(g);
        audio.playSe(0x154, false);
        queueEvent(g.function); // (CallEvent: step 0x1b, EventExecute the update after)
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
bool Viewer::keyMarkPlace(float& x, float& y) {
    if (keyLock.gimmick < 0 || size_t(keyLock.gimmick) >= field.gimmicks().size()) return false;
    const cr2::Gimmick& g = field.gimmicks()[size_t(keyLock.gimmick)];
    const float p[3] = {g.position[0], g.position[1] + 0.125f, g.position[2]};
    return uiPlace(p, x, y);
}
// a field point's place in the interface's units (Cr3UtilSetBalloonPos, / 2048)
bool Viewer::uiPlace(const float p[3], float& x, float& y) {
    const float* m = fieldScene.data();
    const float cw = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
    if (cw <= 0) return false;
    const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
    x = (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]) / cw * float(frameW) * 0.5f / k;
    y = (m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]) / cw * float(frameH) * 0.5f / k;
    return true;
}
// the mouse on the lock (fx, fy: the screen's fractions): pressed, dragged, let go
void Viewer::unlockPointer(float fx, float fy, int phase) {
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
int Viewer::actionKind(const std::string& n) {
    return n.find("hjump") != std::string::npos ? 1 : n.find("mystic") != std::string::npos ? 3 : n.find("iai") != std::string::npos ? 4
         : n.find("aim") != std::string::npos ? 5 : n.find("marts") != std::string::npos ? 6 : 0;
}
int Viewer::actionCharacter(int kind) { return kind == 1 ? 0 : kind >= 3 && kind <= 6 ? kind - 1 : -1; }
bool Viewer::actionRunning() const { return sprint.stage != 0 || iai.stage != 0 || aim.stage != 0 || hakkei.stage != 0 || mystic.stage != 0; }
void Viewer::updateActionMark() {
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
bool Viewer::actionMarkPlace(float& x, float& y) {
    if (actionMark.gimmick < 0) return false;
    const cr2::FieldScript::TouchAction* a = script->touchAction(field.gimmicks()[size_t(actionMark.gimmick)].name);
    if (!a || a->spots.empty()) return false;
    return uiPlace(a->spots[a->markSpot()].data(), x, y);
}
void Viewer::actionTapped() {
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
bool Viewer::actionTap(float fx, float fy) { // (a click: on the mark)
    float mx, my;
    if (actionMark.gimmick < 0 || actionMark.startedAt >= 0 || !fieldTabShown() || !actionMarkPlace(mx, my)) return false;
    const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
    const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
    if (std::hypot(x - mx, y - my) * 2048.0f > 128.0f) return false;
    actionTapped();
    return true;
}
void Viewer::iaiEnd(bool success) {
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
    queueEvent(function); // (FldMain steps 0x16 .. 0x1a: CallEvent - EventExecute the update after)
}
// the mouse in an iai (fx, fy: the screen's fractions; phase 0 pressed, 1 dragged)
void Viewer::iaiPointer(float fx, float fy, int phase) {
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
bool Viewer::leaderPlace(float& x, float& y) { return uiPlace(player, x, y); }
// the mouse in an aim (phase 0 pressed, 1 dragged, 2 let go)
void Viewer::aimPointer(float fx, float fy, int phase) {
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
void Viewer::hakkeiTap(bool onMark) {
    if (!hakkei.stage) return;
    if (!onMark) return iaiEnd(false);
    const int n = int(hakkei.tappedAt.size());
    hakkei.tappedAt.push_back(uiClock);
    playMotion(fig, ("fp0" + std::to_string(2 + std::min(n, 4))).c_str()), time = 0;
    audio.playSe(uiClock * 997.0f - std::floor(uiClock * 997.0f) < 0.5f ? 0x160 : 0x161, false); // (lrand48 % 2)
    if (n + 1 >= hakkei.wanted) iaiEnd(true);
}
void Viewer::hakkeiPointer(float fx, float fy) {
    const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
    const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
    float mx, my;
    if (actionMarkPlace(mx, my)) hakkeiTap(std::hypot(x - mx, y - my) * 2048.0f <= 128.0f);
}
// a mystic's tap: on one of its other spots (its number), or not (-1)
void Viewer::mysticTap(int spot) {
    if (!mystic.stage) return;
    const cr2::FieldScript::TouchAction* a = script->touchAction(field.gimmicks()[size_t(actionMark.gimmick)].name);
    if (spot < 0 || !a || size_t(spot) >= a->spotNames.size()) return iaiEnd(false);
    script->mysticTapSpot = a->spotNames[size_t(spot)];
    mystic.chose = spot, mystic.choseAt = uiClock;
    SDL_Log("%s: the mystic's tap - %s", field.info().name.c_str(), script->mysticTapSpot.c_str());
    iaiEnd(true);
}
void Viewer::mysticPointer(float fx, float fy) {
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
void Viewer::updateIai() {
    if (hakkei.stage && !hakkei.tappedAt.empty() && uiClock - hakkei.tappedAt[0] > 60.0f / 60.0f) iaiEnd(false); // (60 frames)
    if (iai.stage == 2 && uiClock - iai.pressedAt > 10.0f / 60.0f) iaiEnd(false); // (10 frames)
}
void Viewer::drawActionMark(const Mat4& view) {
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
int Viewer::sprintAt() const {
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
bool Viewer::sprintTap(float fx, float fy) { // (a click: on the marker)
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
void Viewer::startSprint(int i) {
    const cr2::Gimmick& g = field.gimmicks()[size_t(i)];
    sprint = Sprint();
    sprint.to = script->touchAction(g.name)->spots;
    sprint.stage = 1;
    heading = std::atan2(g.position[0] - player[0], g.position[2] - player[2]); // (FldPlayer::SetDirection to it)
    playMotion(fig, "fj01"), time = 0;
    SDL_Log("%s: sprint %s - %zu spots", field.info().name.c_str(), g.name.c_str(), sprint.to.size());
}
void Viewer::sprintHop() {
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
void Viewer::updateSprint(float dt) {
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

void Viewer::drawKeyMark(int w, int h) {
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
void Viewer::encountSet(const float* p) {
    encount = Encount();
    if (const cr2::EncountCell* e = encountTable.at(field.info().name)) encount.on = true, encount.area = e->area, encount.steps = e->steps, encount.chance = e->chance;
    encount.x = p[0], encount.z = p[2];
}
void Viewer::encountUpdate(const cr2::Vec3& p, bool paused) {
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
    encount.coupling = a->couplings[pick], encount.clock = 0, encount.updates = 0;
    SDL_Log("%s: an encounter (area %d, roll %d of %d%%): coupling %d", field.info().name.c_str(), encount.area, roll, encount.chance, encount.coupling);
}
// the hit, on FldMain's updates from it (H, the hit's: FldEncount::Update - Pickout's hit sets
// its state 10000): in H+1 FldEncount raises +0x1c (SetBattle(-1)); in H+2 step 2 - the leader
// still moved first (PlayerMove) - sees it: step 0x1c, SE 0xc9; in H+3 0x1c and 0x1d:
// SaveWorkBeforeBattle, the leader's animation stopped, Cr3FadeOut(3, 20) - the wipe, its first
// step the update after, covered in 0.5 s - and StopBGM(20) unless flag 0x7f7; 0x1e waits for
// Cr3Fade's own twenty updates (its value + 1/20 an update: done in H+22), 0x1f counts three,
// 0x20 sets BtlMain in H+27 and GameMain makes and updates it in H+28, the battle's first.
// True while the leader still walks (H+1, H+2)
bool Viewer::encountStep(float dt) {
    constexpr float kTick = cr2::EventRuntime::kTick;
    encount.clock += dt;
    for (; encount.clock >= kTick; encount.clock -= kTick) {
        ++encount.updates;
        if (encount.updates == 2) audio.playSe(0xc9, false);
        if (encount.updates == 3) {
            leaderHeldAt = time, idleTurn.on = false; // (step 0x1c: m_bAnimationStop - FldPlayer::Update no more)
            encount.fadeAt = uiClock + kTick; // (its first step the update after)
            if (!game.flag(0x7f7)) audio.stopBgm(20);
        }
        if (encount.updates >= 27) {
            encount.clock = 0;
            break;
        }
    }
    if (encount.updates < 27) return encount.updates < 2;
    int party[2];
    for (int k = 0; k < 2; ++k) party[k] = game.party[k] >= 0 ? game.party[k] + 1 : 0;
    const int id = encount.coupling, area = encount.area;
    encount.coupling = 0, encount.fadeAt = -1, leaderHeldAt = -1;
    returnMap = field.info().name, std::memcpy(returnPlayer, player, sizeof player), returnHeading = heading; // (SaveWorkBeforeBattle)
    SDL_Log("play: battle %d - an encounter (characters %d, %d), %d updates after the hit", id, party[0], party[1], encount.updates);
    if (ex.on) exploreLog("  an encounter: coupling " + std::to_string(id) + (ex.grind > 0 ? " (" + std::to_string(ex.grind - 1) + " more to fight first)" : "")),
                ex.grind = std::max(ex.grind - 1, 0);
    if (startBattle(id, party, area)) {
        startFight(2, 1u + uint32_t(battlesSkipped));
        fightingInFlow = true, randomBattle = true;
    }
    return false;
}
void Viewer::stepUpdate(int moving) { // (0 standing, 1 walking, 2 running)
    idleTrig = stepState != 0 && moving == 0; // (SetIdleAnim; FldPlayer::Update clears it the update after)
    if (moving != 0) idleTurn.on = false;     // (StopDirection)
    if (idleTurn.on && ++idleTurn.count >= 10) idleTurn.on = false, heading = idleTurn.to; // (FldDirectionManager::Update)
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
void Viewer::drawGroundFx(const float* mvp) {
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

void Viewer::walk(float dt, float sx, float sy, bool run) {
    if (!controller) return;
    groundClock += dt;
    cr2::Vec3 right, forward;
    field.groundAxes(right, forward);
    cr2::Vec3 dir{right[0] * sx + forward[0] * sy, 0, right[2] * sx + forward[2] * sy};
    const float l = std::sqrt(dir[0] * dir[0] + dir[2] * dir[2]);
    const float speed = l > 0 ? (run ? 0.48f : 0.09f) : 0.0f;
    if (l > 0) dir = {dir[0] / l * speed, 0, dir[2] / l * speed}, heading = std::atan2(dir[0], dir[2]), idleTurn.on = false;
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
        else if (exitsArmed && exit.size() >= 5 && fieldControl() && fieldExitHold < 0) exitTo = exit; // (CollisionAdded: the leader's control)
        touchEvents(p);
        checkBalloons();
        startIdleTurn();
        encountUpdate(p, fieldScriptRunning() || actionRunning() || changeStage != 0 || keyLock.stage != 0);
    }
    if (idleTurn.on) // (between updates: the share at the update's fraction)
        heading = turnBetween(idleTurn.from, idleTurn.to,
                              directionShare(float(idleTurn.count) + (float(substep) * kSub + stepClock) / cr2::CharacterController::kUpdate, 10));
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
        // (CollisionEventExec, case 0: control off, step 0x10 with Cr3FadeOut(0, 5) -
        // SetEventStep(true, 5); 0x10 waits for the fade, 0x11 counts three updates, 0x12 changes
        // the area and calls its entry, run the update after: the twelfth after the contact's.
        // The fade begun here, a step before the update CollisionEventExec runs in, keeps its
        // hold to that update; the exit hold counts the updates from the fade's end)
        game.next.popPoint = exitTo;
        if (script) script->events().fadeOut(0, 5);
        fieldExitHold = 6, pendingBranch = true;
        return;
    }
    const float a = std::clamp(stepClock / kSub, 0.0f, 1.0f);
    for (int c = 0; c < 3; ++c) player[c] = playerPrev[c] + (playerCur[c] - playerPrev[c]) * a;
}
bool Viewer::loadTable(const std::string& binary, const std::string& dataPath) {
    dataArg = dataPath, binaryArg = binary;
    if (cr2::releaseMapTable(dataPath, binary, table, &tableFrom)) {
        if (!cr2::releaseEncountTable(dataPath, binary, encountTable)) SDL_Log("the encounters' table: not found in %s", tableFrom.c_str());
        return true;
    }
    SDL_Log("the maps' table: not found beside %s (the iPad's executable, the Vita's eboot.bin, the Android "
            "release's APK) - give the game's executable with --binary", dataPath.c_str());
    return false;
}
void Viewer::drawResidents(const float* mvp) {
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
float Viewer::fidgetWait() { return float(encountRandom() % 1000 + 400) / 30.0f; }
// where a turn's target is ("player", else a gimmick's id or name)
bool Viewer::gimmickTarget(const std::string& target, float& x, float& z) const {
    if (target == "player") return x = player[0], z = player[2], true;
    for (const cr2::Gimmick& g : field.gimmicks())
        if (g.id == target || g.name == target) return x = g.position[0], z = g.position[2], true;
    return false;
}
float Viewer::turnBetween(float from, float to, float a) { // (the shorter way round)
    float d = std::remainder(to - from, 6.28318531f);
    return from + d * a;
}
float Viewer::directionShare(float u, int n) { return cr2::directionShare(u, n); } // (event/scene.hpp)
void Viewer::refreshGimmicks() {
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
bool Viewer::enterMap(const std::string& map, const std::string& at, const std::string& entry) {
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
    fieldClock = 0, fieldExitHold = -1;
    pendingCall.clear(), pendingCallWait = 0, controlHold = 0;
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
    idleTurn = IdleTurn(), idleTrig = changeTurn = false, leaderHeldAt = -1; // (a new area: no turn, no hold)
    fieldCam = FieldCamera(); // (SetMapCameraAtNewArea: the scroll put on the leader)
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

bool Viewer::loadField(const std::string& map, const std::string& at, const std::string& binary, const std::string& dataPath, const std::string& entry) {
    return loadTable(binary, dataPath) && enterMap(map, at, entry);
}
bool Viewer::fieldScriptRunning() const { return script && (script->running() || !pendingCall.empty() || controlHold > 0 || fieldExitHold >= 0); }
// --input's tokens the field takes (fieldToken: the tab, the camp), one a 0.4 s while it is idle
void Viewer::fieldInput() {
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
void Viewer::fieldUpdate(float dt) {
    if (!script) return;
    fieldClock = std::min(fieldClock + dt, 0.25f);
    for (; fieldClock >= cr2::EventRuntime::kTick; fieldClock -= cr2::EventRuntime::kTick) {
        // (step 1: control back once no finger is down nor lifted this update, CheckBalloon after it)
        if (controlHold > 0 && !(controlHold == 1 && (pointerDown || pointerLifted)) && --controlHold == 0) checkBalloons();
        if (emergencyExit) { // (FldMain step 5, result 2: the way out to the town's start)
            emergencyExit = false;
            SDL_Log("camp: Emergency Exit");
            game.next.popPoint = "t0101_p1";
            if (script) script->events().fadeOut(1, 20);
            audio.stopBgm(20), audio.playSe(0x14f, false);
            game.setFlag(0x3ed, true);
            fieldExitHold = 6, pendingBranch = true;
        }                        // (step 5 -> 1 the next, control the one after)
        pointerLifted = false;
        if (pendingBranch) {
            pendingBranch = false;
            if (fieldExitHold > 0) { // (FldMain's way out, steps 0x1d .. 0x20: the fade and the windows go on)
                script->tick();
                if (fieldExitHold <= 3 || !script->events().fade().moving()) --fieldExitHold; // (0x1d / 0x1e / 0x20: WaitFadeFinish)
                pendingBranch = true;
                continue;
            }
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
        if (!pendingCall.empty() && --pendingCallWait <= 0) { // (EventExecute's call: the script runs from the next update)
            const std::string f = std::move(pendingCall);
            pendingCall.clear();
            SDL_Log("%s: %s called after update %d", field.info().name.c_str(), f.c_str(), script->ticks());
            if (!script->call(f)) SDL_Log("%s", script->error().c_str());
            else if (!script->running()) { // (over at once: still EventExecute's three updates, then step 1)
                controlHold = 3;
                if (playMode) pendingBranch = true;
            }
            continue;
        }
        if (!wasRunning) continue;
        refreshGimmicks();
        if (script->events().scene().loaded()) syncEventFigures(script->events());
        placePlayer(script->takePlacement());
        checkBalloons();
        if (!script->running()) {
            SDL_Log("%s: the script ended", field.info().name.c_str());
            controlHold = 3; // (EventExecute: state 4 seen, 1000, BranchAfterEvent - step 1 in the third update)
            SDL_Log("%s: control back after update %d", field.info().name.c_str(), script->ticks() + 3);
            if (library.stage == 2) library.stage = 1, library.at = uiClock; // (an entry read: the shelf again)
            else if (playMode) branchAfterField();
            if (!fieldMode) return; // (gone to a vista)
        }
    }
}
bool Viewer::overFast(float fx, float fy) const {
    if (fastRect.id == 0) return false;
    const float k = std::min(float(screenW) / 0.96f, float(screenH) / 0.64f);
    const float x = (fx - 0.5f) * float(screenW) / k, y = (0.5f - fy) * float(screenH) / k;
    return x >= std::min(fastRect.x0, fastRect.x1) && x <= std::max(fastRect.x0, fastRect.x1) && y >= std::min(fastRect.y0, fastRect.y1) &&
           y <= std::max(fastRect.y0, fastRect.y1);
}
void Viewer::advance(float dt, float sx, float sy, bool run) {
    uiClock += dt;
    noteAsk(); // (a question's focus: Yes as it opens)
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
        const bool exitNow = battleRules && battleRules->exited(); // (Cr3BattleEvent_Exit: the way out at once)
        // (no pause of its own: a win's StepResult, a loss's StepGameover, an escape's step 0x1f go
        // to StepBattleOut the update after)
        if (fightingInFlow && battleStep == BattleStep::Over && (exitNow || (!battleScriptRunning() && battleClock >= showEnd)) &&
            (result.stage == 0 || result.stage >= 4) && gameOver.stage == 0) {
            fightingInFlow = false;
            beginBattleOut();
            SDL_Log("play: battle %s", battleRules->exited() ? "ended by its script" : battleRules->escaped() ? "escaped" : battleRules->outcome() == 1 ? "won" : "lost (back to the field: no game over yet in the story)");
            if (ex.on && battleRules->outcome() == 1 && !randomBattle && ex.grind > 0) ex.grind = 0, exploreLog("  (won: no more fighting first)");
            ++battlesSkipped;
        } else if (!fightingInFlow && !standaloneOut && battleFight != 0 && battleRules && battleStep == BattleStep::Over &&
                   (exitNow || (!battleScriptRunning() && battleClock >= showEnd)) && (result.stage == 0 || result.stage >= 4) && gameOver.stage == 0 &&
                   (battleRules->exited() || battleRules->escaped() || battleRules->outcome() == 2 || result.stage >= 4)) {
            // (a battle on its own - --battle -: no field to go back to; its way out, then the end,
            // as the game over's "no" - the title, which this mode has none of)
            standaloneOut = true;
            beginBattleOut();
            SDL_Log("battle: the way out (on its own: then the end)");
        }
        if (battleOutAt >= 0 && battleClock - battleOutAt >= kBattleOutSeconds) { // (BtlMain::Exit: the next main)
            battleOutAt = -1;
            SDL_Log("play: the battle's way out over at %.2f s", battleClock);
            if (standaloneOut) toTitle = true;
            else afterBattle();
        }
    } else if (eventMode) {
        updateEvent(event && event->skipHeld() ? dt * 5 : dt); // (the skip button held: five updates a frame)
    } else if (fieldMode && (campOpen || mapOpen || saveStage || world.stage || shop.stage || party.stage || library.stage == 1 || pub.stage)) { // (FldMain steps 5 .. 7, 10: the camp, the map, saving, the world select - the field held)
        updateManual(dt);
        if (saveStage == 2 && uiClock - saveStart > (saveFailed ? 3.0f : 1.0f)) saveStage = 0; // (a second, then closed)
        if (script) { // (the fade, the windows go on under a menu: GameMain's update - the library's fade back in)
            fieldClock = std::min(fieldClock + dt, 0.25f);
            for (; fieldClock >= cr2::EventRuntime::kTick; fieldClock -= cr2::EventRuntime::kTick) {
                if (!script->running()) script->tick();
                // (the camp closing: InterfaceMain's +0xbc in the fourth update after the release's;
                // FldMain's step 5 -> 1 the next, step 1's control the one after)
                if (campOpen && campExitTicks >= 0 && ++campExitTicks >= 5) { // (+0xbc = 2)
                    closeCamp(), emergencyExit = true;
                    break;
                }
                if (campOpen && campListCloseAt >= 0 && ++campListCloseTicks >= 4) {
                    campListCloseAt = -1;
                    if (campListCloseTarget > 0) { // (the use menus)
                        campItem = campListCloseTarget, campFace = 0, campUseStart = uiClock - cr2::EventRuntime::kTick;
                        campUse = campItem == 36 ? 3 : 1;
                    } else { // (the camp made again)
                        campList = 0, campCursor = 0, campSopiaDetail = 0;
                        campStart = campOpenedAt = uiClock - cr2::EventRuntime::kTick;
                    }
                }
                if (campOpen && campCloseAt >= 0 && ++campCloseTicks >= 5) {
                    if (campCloseTarget != 100) {
                        campEnter(campCloseTarget);
                        continue;
                    }
                    closeCamp(), controlHold = 2;
                    break;
                }
            }
        }
        fieldInput();
    } else if (fieldMode) {
        // (Field Speed: the field faster while the player walks it - no script, change, lock or encounter running)
        if (!fieldScriptRunning() && !eventMode && keyLock.stage == 0 && changeStage == 0 && !encount.coupling && !optionsOpen && !settingsOpen)
            dt *= float(settings.fieldSpeed);
        fieldUpdate(script && script->events().skipHeld() ? dt * 5 : dt);
        updateFieldCamera(dt);
        updateChange(dt);
        updateSprint(dt);
        updateIai();
        fieldInput();
        const bool hit = fieldMode && !eventMode && encount.coupling && !fieldScriptRunning() && !actionRunning();
        if (hit && !encountStep(dt)) {
            // (held: the wipe, then the battle)
        } else if (fieldMode && !eventMode) {
            const bool held = fieldScriptRunning() || keyLock.stage != 0 || changeStage != 0 || actionRunning();
            walk(dt, held ? 0 : sx, held ? 0 : sy, run);
        }
    }
}
// FldMain::BranchAfterEvent's order: an area change, a battle, a vista, then the world select,
// the camp, the shop, the party, the pub, the chapter select, the library, the title
void Viewer::branchAfterField() {
    cr2::GameState::Next& n = game.next;
    // An event's way out to another main (FldMain::BranchAfterEvent; EventExecute reports the
    // script's end the update after seeing its state 4): step 0x1d waits for Cr3Fade's count, 0x1e
    // again, 0x1f counts three updates, 0x20 waits again and sets the next main, GameMain::Update
    // makes and updates it the update after - a battle's first update (BattleIN from an event:
    // step 0x1c first, no wipe) is the eighth after the script's last, the title's the ninth; a
    // vista's script is loaded in the ninth and its function called in the tenth. A pop point
    // (FldUtilSetPopPoint reserves a dungeon change too) or an area change goes to step 0x10:
    // the fade waited for, 0x11 counts three updates, 0x12 changes the area in one
    // (ChangeAreaExecute) and calls its entry event, run in the next - the eighth.
    const bool area = !n.popPoint.empty() || (n.changeDungeon >= 0 && n.changeArea >= 0);
    if ((area || n.battle >= 0 || n.vista) && fieldExitHold < 0) {
        fieldExitHold = area || n.battle >= 0 ? 7 : 9;
        pendingBranch = true;
        return;
    }
    fieldExitHold = -1;
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
void Viewer::returnToField(int mode) {
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
// a list whose L / R is its switch (ScrollListMenu::PartsUpdate: 0x200 / 0x400 its
// ScrollListSwitchButton's left / right, +0x48 = 8 - CampItemListMenu::Update the other category,
// CampEquipmentListMenu::Update_EquipView the character); a list without one pages (CR2-U's)
bool Viewer::switchList() const {
    return campOpen && !campUse && !settingsOpen && !optionsOpen && !optionsAsk && !manualStage && !saveImport.active() &&
           (campList == 16 || (campList == 17 && !campTakeSopia));
}
// A controller's button as the key the screens take: ○ Return, × Backspace (the options' Swap ○
// and ×: the other way), △ Tab (CR2-U's: the field's tab - the Vita's is a touch), L / R
// PageUp / PageDown in a menu (a list's switch, else its page), START the title's "Please Touch"
// (GameTitleMenu::Update: ○ or START, 0x900); 0 - none
SDL_Keycode Viewer::padButtonKey(SDL_GamepadButton b) const {
    const SDL_GamepadButton circle = settings.padSwap ? SDL_GAMEPAD_BUTTON_SOUTH : SDL_GAMEPAD_BUTTON_EAST;
    const SDL_GamepadButton cross = settings.padSwap ? SDL_GAMEPAD_BUTTON_EAST : SDL_GAMEPAD_BUTTON_SOUTH;
    if (b == circle) return SDLK_RETURN;
    if (b == cross) return SDLK_BACKSPACE;
    if (b == SDL_GAMEPAD_BUTTON_NORTH) return SDLK_TAB;
    if (b == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER && inMenu()) return SDLK_PAGEUP;
    if (b == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER && inMenu()) return SDLK_PAGEDOWN;
    if (b == SDL_GAMEPAD_BUTTON_START && titleMenu.stage >= 2 && titleMenu.stage <= 3) return SDLK_RETURN;
    return 0;
}
// A key the game's screens take (the keyboard's, and the controller's buttons as padButtonKey has
// them); false - none took it (the model viewer's own keys then)
bool Viewer::gameKey(SDL_Keycode k) {
    if (askKey(k)) return true; // (a question's focus; Return on No, Backspace - its No)
    if (yesNoShown()) { // (an event's yes / no: Y or Return yes, N or Backspace no)
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_Y) answerYesNo(true);
        else if (k == SDLK_BACKSPACE || k == SDLK_N) answerYesNo(false);
        return true;
    }
    if ((k == SDLK_PAGEUP || k == SDLK_PAGEDOWN) && listShown() && !switchList()) { // (a list's page: four rows on)
        for (int r = 0; r < 4; ++r) gameKey(k == SDLK_PAGEUP ? SDLK_UP : SDLK_DOWN);
        return true;
    }
    if (focusKey(k)) return true; // (the title's, the shop's and the party's buttons; the field's tab)
    if (battleMode && (choosing || gameOver.stage == 8)) return battleKey(k), true; // (the commands; the game over's question)
    if (campOpen) return campKey(k), true;       // (the camp: Up / Down and Return, Backspace back)
    if (party.stage) return partyKey(k), true;   // (the party: 1 / 2, Left / Right, Return, Backspace)
    if (shop.stage) return shopKey(k), true;     // (the shop: 1 .. 4 its buttons, Up / Down, Left / Right, Return, Backspace; Y / N)
    if (world.stage) return worldKey(k), true;   // (the world select: Up / Down, Return, Backspace; Y / N its question)
    if (pub.stage) return pubKey(k), true;       // (the pub: the arrows, Return, Backspace; Y / N)
    if (library.stage == 1) return libraryKey(k), true; // (the library: Up / Down, Return, Backspace)
    if (mapOpen) { // (the map: any key closes it)
        audio.playSe(0xb5, false), mapOpen = false;
        return true;
    }
    if (saveStage) { // (saving: Return yes, Backspace no)
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_Y) saveAnswer(true);
        else if (k == SDLK_BACKSPACE || k == SDLK_N) saveAnswer(false);
        return true;
    }
    if (k == SDLK_TAB && fieldTabShown()) return fieldTabTap(1), true; // (the field's tab: open or closed; C the camp)
    if (k == SDLK_C && fieldTabShown()) return fieldToken("camp"), true;
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) tap();
    return eventMode;
}

// the dungeon and area the map is in (cr3_field_data +0 / +4: FldMap's table maps them to the
// maps - t01MM dungeon 0, dNNMM dungeon NN, the area MM - 1)
int Viewer::dungeonNum() const {
    const std::string& n = field.info().name;
    return n.size() >= 5 && n[0] == 'd' ? std::atoi(n.substr(1, 2).c_str()) : 0;
}
int Viewer::areaNum() const {
    const std::string& n = field.info().name;
    return n.size() >= 5 ? std::max(std::atoi(n.substr(3, 2).c_str()) - 1, 0) : 0;
}
void Viewer::drawDungeonMap(const Mat4& view) {
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
bool Viewer::fieldTabShown() const {
    return fieldMode && !eventMode && !battleMode && script && !script->running() && !campOpen && !mapOpen && !saveStage &&
           keyLock.stage == 0 && changeStage == 0 && !actionRunning() && world.stage == 0 && shop.stage == 0 && party.stage == 0 &&
           library.stage == 0 && pub.stage == 0;
}
float Viewer::fieldTabPose(UiFig& tab) {
    const float d = tab.fig.animLoaded ? tab.fig.anim.duration() : 0.0f;
    const float e = std::clamp(uiClock - fieldTabAt, 0.0f, d);
    return fieldTabOpen ? e : d - e;
}
void Viewer::drawFieldTab(const Mat4& view) {
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
void Viewer::fieldTabTap(int area) {
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
void Viewer::openCamp() {
    campOpen = true, campStart = campOpenedAt = uiClock, campList = 0, campCursor = 0, campCloseAt = -1, campCloseTicks = 0, campListCloseAt = -1;
    fieldTabOpen = false, fieldTabAt = -10;
    optionsOpen = optionsAsk = false, manualStage = 0;
    SDL_Log("camp: open");
}
float Viewer::campPartTime(float length) const {
    if (campCloseAt < 0) return uiClock - campStart;
    return std::max(std::min(campCloseFrom, length) - (uiClock - campCloseAt), 0.0f); // (played back from where it was)
}
void Viewer::closeCamp() {
    campCloseAt = -1, campCloseTicks = 0, campListCloseAt = -1, campExitTicks = -1;
    campOpen = false, campList = 0, campUse = 0, campItem = 0, campLink = 0;
    optionsOpen = optionsAsk = false, manualStage = 0, manualEvents.reset();
    SDL_Log("camp: closed");
}
// a sopia's commands and skills by its levels (SetSopiaListItem - CampSopiaListMenu's detail,
// CampEquipmentListMenu's mode 8): a command its icon and cost (cost_id 12 "MP" and its MP, an
// awakening's 13 and its level), an auto skill its icon, a level not reached "???" greyed
auto Viewer::sopiaSkillRows(int s) -> std::vector<ListRow> {
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
void Viewer::campSopiaPick(int s) {
    if (campList != 18 || campSopiaDetail || s <= 0 || s >= 32) return;
    audio.playSe(0xb4, false); // (Update_Sopia step 5)
    campSopiaDetail = s, campSopiaPrevCursor = campCursor, campCursor = 0, campStart = uiClock;
}
auto Viewer::campRows() -> std::vector<ListRow> {
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
auto Viewer::statusOf(int ch) const -> StatusShown {
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
    r.percent = r.level >= maxLevel() || hi <= lo ? 0 : int(std::clamp((int64_t(game.exp[ch]) - lo) * 100 / (hi - lo), int64_t(0), int64_t(100)));
    return r;
}
void Viewer::drawCamp(const Mat4& view, const Mat4& proj, int w, int h) {
    tapRects.clear();
    if (uiClock - campOpenedAt < cr2::EventRuntime::kTick) return; // (made the update after the press)
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
        drawSkillList(rows, focused, campListTime(), view, w, h, campList == 16 ? (campCategory ? 15 : 7) : 9, -1);
        listTagExtra[0] = -1, listSwitch = false;
        return;
    }
    UiFig& title = uiFig("menu");
    title.fig.place = cr2::Mat34();
    poseUi(title, "menucamp_io", campCloseTarget == 16 || campCloseTarget == 17 || campCloseTarget == 18 ? uiClock - campStart : campPartTime(0.45f)); // (the lists keep the title: 0x2c not closed)
    drawFigure(title.fig, view.data());
    StatusShown rows[2];
    int count = 0;
    for (int k = 0; k < 2; ++k)
        if (game.party[k] >= 0 && game.party[k] < 9) rows[count++] = statusOf(game.party[k]);
    drawStatusWindows(rows, count, campPartTime(0.2f), view, proj);
    bool anyItem = false;
    for (int id = 1; id < 300; ++id) anyItem = anyItem || game.itemCount[id] > 0;
    buttonLook = [&](int id) { return id == 16 && !anyItem ? 0 : id == campCursorButton() && keysUsed ? 2 : 1; };
    drawCommandMenu("campbtn_io", campPartTime(0.15f), view, {}, {}, w, h, 0, "campbtn_io");
    buttonLook = nullptr;
    for (TapRect& r : tapRects)
        if (r.id == 16 && !anyItem) r.id = -1;
    // the links (CampPubLink: campLnk1_io, the pub hidden, before flag 0x4b0; campLnk2_io after)
    UiFig& lnk = uiFig("camplnk");
    lnk.fig.place = cr2::Mat34();
    const bool pub = game.flag(0x4b0);
    poseUi(lnk, pub ? "camplnk2_io" : "camplnk1_io", campPartTime(0.2f));
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
    poseUi(oz, "ozwin_io", campPartTime(0.2f));
    drawFigure(oz.fig, view.data());
    if (uiJoint(oz, "call_status_fr13", x, y)) drawStatusSign(13, x, y, proj);
    if (uiJoint(oz, "call_dotNum_id01", x, y)) drawDigits(std::clamp(game.oz, 0, 9999999), 110, 7, x, y, proj); // (id01 the ones, id07 the millions)
    UiFig& tw = uiFig("timewin_io");
    tw.fig.place = cr2::Mat34();
    poseUi(tw, "timewin_io", campPartTime(0.2f));
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
    poseUi(bk, "btlback_in", campPartTime(0.15f));
    drawFigure(bk.fig, view.data());
    float sx, sy, ex, ey;
    if (uiJoint(bk, "tapAreaS01", sx, sy) && uiJoint(bk, "tapAreaE01", ex, ey)) tapRects.push_back({100, sx, sy, ex, ey});
}
int Viewer::campCursorButton() const {
    static const int kButtons[4] = {16, 17, 18, 20};
    return kButtons[std::clamp(campCursor, 0, 3)];
}
// The characters the camp offers (Cr3UtilValidCharacter: 0..6 by flags 0x3fd..0x403 - those
// who have joined), else the party's two
std::vector<int> Viewer::validChars() const {
    std::vector<int> out;
    for (int c = 0; c < 7; ++c)
        if (game.flag(0x3fd + c)) out.push_back(c);
    if (out.empty())
        for (int c : game.party)
            if (c >= 0 && c < 9) out.push_back(c);
    return out;
}
cr2::BattleRules::PlayerStats Viewer::statsOf(int ch) const {
    int eq[8];
    for (int e = 0; e < 8; ++e) eq[e] = game.equip[ch][e];
    return cr2::BattleRules::playerStats(battleData, ch + 1, std::max(game.level[ch], 1), eq, game.sopiaLevel, game.seed[ch], game.itemCount);
}
int Viewer::mpOf(int ch) const {
    const int most = statsOf(ch).maxMp;
    return game.mp[ch] < 0 ? most : std::min(game.mp[ch], most);
}
// CharaStatusWindowBMenu: one character's window (campWin with campWin3_io, the third's joints
// id90..): its face, name, level (3 of set 130), HP (5 of 110) and MP (4), their bars (btlHpBar_per
// / btlMpBar_per at the percent / 20 s), its labels, and an arrow by HP / MP (sign 5 up, 6 down)
// when what is chosen changes them
void Viewer::drawStatusB(int ch, int hpChange, int mpChange, float t, const Mat4& view, const Mat4& proj) {
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
void Viewer::drawCapacity(int ch, const int* change, float t, const Mat4& view, const Mat4& proj, int w, int h) {
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
void Viewer::campUseUpdate() { // (Update 8, 9: each 3 s; after the last, a second, then back to the list)
    if (campUse == 4 && uiClock - campUpAt >= 3.0f) campUpShown++, campUpAt = uiClock;
    if (campUse == 4 && campUpShown >= campUpText.size()) campUse = 5, campUseStart = uiClock;
    if (campUse == 5 && uiClock - campUseStart >= 1.0f) campUse = 0, campItem = 0;
}
void Viewer::showRaised(int ch, const cr2::BattleData::Item& it, int n) {
    const std::string who = menuName(0x28 + ch + 1);
    campUpText.clear();
    if (it.mp * n >= 1) campUpText.push_back(formatAnnounce(announce(0x85), {who}));
    for (int k = 0; k < 5; ++k)
        if (it.param[k] * n >= 1) campUpText.push_back(formatAnnounce(announce(0x8d + k), {who, std::to_string(it.param[k] * n)}));
    campUpShown = 0, campUpAt = campUseStart = uiClock;
    campUse = campUpText.empty() ? 5 : 4;
}
void Viewer::useItemOn(int ch, int item, int n) { // (Cr3UtilParameterUpByItem, Cr3UpdatePlayerData; PlaySoundMPRecovery)
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
void Viewer::campUseTap(int id) {
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
        if (id == 7201) { // (Yes: Cr3UtilGetItem(-1), EndCampTitle, SE 0xb4 - the rest after the window's close)
            if (campExitTicks >= 0) return;
            game.itemCount[36] = static_cast<short>(std::max(game.itemCount[36] - 1, 0));
            audio.playSe(0xb4, false);
            campExitTicks = 0;
        } else if (id == 7202) campUse = 0, campItem = 0, audio.playSe(0xb5, false);
    }
}
void Viewer::drawItemUse(const Mat4& view, const Mat4& proj, int w, int h) {
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
void Viewer::campPickItem(int item) {
    const std::vector<ListRow> rows = campRows();
    const auto r = std::find_if(rows.begin(), rows.end(), [&](const ListRow& x) { return x.id == item; });
    if (r == rows.end() || !r->selectable) return (void)SDL_Log("camp: item %d not usable here", item);
    audio.playSe(0xb4, false);
    SDL_Log("camp: using %s", r->name.c_str());
    // (state 3: the list's frame played back, then 9 - or 10 for item 36 - and InterfaceMain makes
    // the use menus, stepped once there)
    if (campListCloseAt < 0) campListCloseAt = uiClock, campListCloseFrom = uiClock - campStart, campListCloseTicks = 0, campListCloseTarget = item;
}

// The sopia's windows (CampSopiaStatusMenu, sopiaWin_io: its picture - sopiaPic with
// sopiaPic_NNN, its number, 26..29 as 301 / 303 / 305 / 307, past 7 + 193 - at call_sopiaPic,
// its name at typeM_C2, its level (cost_id at the level + 6 / 20 s), its kind's sign (kinds 1 2
// 3 4 -> 2 1 4 3), the next level's bar and EXP (cmpNxBar_per; 5 at the most); CampSopiaSkillMenu,
// skillWin_io: its five levels' command or auto skill - "???" (menu name 0x3d) past its level)
// `detail`: a sopia picked, its skills listed (Update_SopiaDetail: SetParamNumber 3 shows its
// picture, 4 on leaving hides it - none over the list)
void Viewer::drawSopiaWindows(int sopia, float t, const Mat4& view, const Mat4& proj, int w, int h, bool detail) {
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
        poseUi(bar, "cmpnxbar_per", level >= 5 || hi <= lo ? 5.0f : float(std::clamp((int64_t(exp) - lo) * 100 / (hi - lo), int64_t(0), int64_t(100))) / 20.0f);
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
std::string Viewer::menuName(int id) { return messageText("announcemenuname", id); }
auto Viewer::equipRows() -> std::vector<EquipRow> {
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
std::vector<int> Viewer::humanSopia() const {
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
void Viewer::campEquipPick(int id) {
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
void Viewer::exploreOutfit() {
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
void Viewer::clampMp(int ch) { // (Cr3UtilAdjustment: the MP within the most)
    if (game.mp[ch] >= 0) game.mp[ch] = std::min(game.mp[ch], statsOf(ch).maxMp);
}
void Viewer::campTakeAnswer(bool yes) {
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
void Viewer::campEquipBack() {
    audio.playSe(0xb5, false);
    if (campTakeSopia) return campTakeAnswer(false);
    if (campEquipMode == 8) campEquipMode = campEquipPrevMode, campEquipCursor = campEquipPrevCursor, campStart = uiClock;
    else if (campEquipMode) campEquipMode = 0, campEquipCursor = 0, campStart = uiClock;
    else campListBack();
}
void Viewer::campEquipView() { // (the change button tapped: Update_*Sopia's step 0xe)
    const std::vector<EquipRow> rows = equipRows();
    if (campEquipMode < 4 || campEquipMode > 7 || campEquipCursor >= int(rows.size()) || rows[size_t(campEquipCursor)].sopia <= 0) return;
    audio.playSe(0xb4, false);
    campEquipViewSopia = rows[size_t(campEquipCursor)].sopia;
    campEquipPrevMode = campEquipMode, campEquipPrevCursor = campEquipCursor;
    campEquipMode = 8, campEquipCursor = 0, campStart = uiClock;
}
void Viewer::campEquipChange(int dir) { // (another character: the eye buttons, Left / Right)
    const std::vector<int> chars = validChars();
    if (chars.empty() || campEquipMode) return;
    const auto it = std::find(chars.begin(), chars.end(), campEquipChar);
    const int k = it == chars.end() ? 0 : int(it - chars.begin());
    campEquipChar = chars[size_t((k + dir + int(chars.size())) % int(chars.size()))];
    audio.playSe(0xb7, false);
}
void Viewer::drawEquipment(const Mat4& view, const Mat4& proj, int w, int h) {
    const float t = campListTime();
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
void Viewer::scrollScenarios(float dy) { scenarioScroll = std::clamp(scenarioScroll + dy, 0.0f, game.flag(1) ? 1.472f : 0.704f); }
void Viewer::drawScenarios(const Mat4& view, const Mat4& proj, int w, int h) {
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
void Viewer::drawPubRequest(const Mat4& view, int w, int h) {
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
void Viewer::campPress(int id) {
    if (campCloseAt >= 0 || campListCloseAt >= 0 || campExitTicks >= 0 || campOpening() || campListOpening()) return; // (state 1 on: the menu closing; or still opening, +0x1c)
    if (campUse) return campUseTap(id);
    if (campLink) {
        if (id == 100) audio.playSe(0xb5, false), campLink = 0, campStart = uiClock;
        return;
    }
    if (id == 201) scenarioScroll = 0;
    if ((id == 201 || (id == 202 && game.globalWork[7])) && !campList) {
        audio.playSe(0xb4, false);
        game.setFlag(id == 201 ? 0x41d : 0x41e, false);
        campCloseAt = uiClock, campCloseFrom = uiClock - campStart, campCloseTicks = 0, campCloseTarget = id; // (results 8 / 7, after the close)
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
        else if (campList) campListBack();
        else if (campCloseAt < 0) campCloseAt = uiClock, campCloseFrom = uiClock - campStart, campCloseTicks = 0, campCloseTarget = 100;
        return;
    }
    if (campList) return;
    if (id != 16 && id != 17 && id != 18 && id != 20) {
        SDL_Log("camp: button %d (not here yet)", id);
        return;
    }
    // (CampMenu::TouchSimpleRelease: 0xb4, its result - 1 items, 2 equipment, 3 sopia, 4 options -
    // and the close as Back's; InterfaceMain makes the list when the menu returns 7)
    audio.playSe(0xb4, false);
    campCloseAt = uiClock, campCloseFrom = uiClock - campStart, campCloseTicks = 0, campCloseTarget = id;
}
// the top's button acted on once its close is over (InterfaceMain::Update's camp results: 0x2d the
// items, 0x2f / 0x30 / 0x3a / 0x33 the equipment, 0x36 the sopia, 0x2a the options, 0x73 / 0x6d
// the links), made and stepped once in that update
void Viewer::campListBack() {
    if (campListCloseAt < 0) campListCloseAt = uiClock, campListCloseFrom = uiClock - campStart, campListCloseTicks = 0, campListCloseTarget = 0;
}
void Viewer::campEnter(int id) {
    campCloseAt = -1, campCloseTicks = 0, campCloseTarget = 100;
    campStart = campListOpenedAt = uiClock - cr2::EventRuntime::kTick;
    if (id == 201 || id == 202) {
        campLink = id;
        SDL_Log("camp: %s", id == 201 ? "scenarios" : "the pub's request");
        return;
    }
    if (id == 17) { // (InterfaceMain's camp step 2: the equipment, the first character's)
        const std::vector<int> chars = validChars();
        campList = 17, campEquipMode = 0, campEquipCursor = 0;
        campEquipChar = !chars.empty() ? (std::find(chars.begin(), chars.end(), game.party[0]) != chars.end() ? game.party[0] : chars[0]) : 0;
        SDL_Log("camp: equipment of %d", campEquipChar + 1);
        return;
    }
    if (id == 16 || id == 18) {
        campList = id, campCategory = 0, campCursor = 0, campSopiaDetail = 0;
        SDL_Log("camp: %s - %zu", id == 16 ? "items" : "sopia", campRows().size());
    } else if (id == 20) {
        optionsOpen = true, optionsStart = uiClock; // (CampOptionMenu)
        if (!game.flag(84)) optionsOpen = false, playTutorial("Tutorial_Option", 0, false);
    }
}
void Viewer::campKey(SDL_Keycode k) {
    if (campCloseAt >= 0 || campListCloseAt >= 0 || campExitTicks >= 0 || campOpening() || campListOpening()) return;
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
        else if (k == SDLK_PAGEUP || k == SDLK_PAGEDOWN) campEquipChange(k == SDLK_PAGEUP ? -1 : 1); // (L / R: Update_EquipView's switch, 8)
        else if (k == SDLK_TAB) campEquipView(); // (the change button - a controller's triangle)
        else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && !rows.empty()) campEquipPick(rows[size_t(std::clamp(campEquipCursor, 0, n - 1))].id);
        return;
    }
    if (campList) {
        const int n = std::max(int(campRows().size()), 1);
        if (k == SDLK_UP) campCursor = (campCursor + n - 1) % n;
        else if (k == SDLK_DOWN) campCursor = (campCursor + 1) % n;
        else if (k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_PAGEUP || k == SDLK_PAGEDOWN) { // (the items' two
            // categories: the switch's arrows - L / R on the Vita, CampItemListMenu::Update's 8, SE 0xb7)
            if (campList == 16) campCategory = 1 - campCategory, campCursor = 0, audio.playSe(0xb7, false);
        } else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && (campList == 16 || campList == 18)) {
            const std::vector<ListRow> rows = campRows();
            if (!rows.empty()) {
                const int id = rows[size_t(std::clamp(campCursor, 0, int(rows.size()) - 1))].id;
                campList == 16 ? campPickItem(id) : campSopiaPick(id);
            }
        }
        return;
    }
    // (CampTopMenu's update, FUN_81084c8e: FUN_810452fe - Up / Down round past the greyed Items
    // with none held, SE 0xb3)
    if (k == SDLK_UP || k == SDLK_DOWN) {
        bool anyItem = false;
        for (int id = 1; id < 300; ++id) anyItem = anyItem || game.itemCount[id] > 0;
        do campCursor = (campCursor + (k == SDLK_UP ? 3 : 1)) % 4;
        while (campCursor == 0 && !anyItem);
        audio.playSe(0xb3, false);
    } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) campPress(campCursorButton());
}
// a click on the field's UI (the tab, the camp): false - none of it there (the field's own tap)
bool Viewer::fieldClick(float fx, float fy) {
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
bool Viewer::fieldToken(const std::string& t) {
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
uint64_t Viewer::storyKey() const {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    for (size_t i = 0; i < game.flags.size(); ++i) { // (the flags alone: the work values count taps and the like)
        const auto f = ex.flips.find(i);
        if (game.flags[i] && (f == ex.flips.end() || f->second < 3)) mix(i + 1);
    }
    return h;
}
void Viewer::exploreLog(const std::string& line) {
    if (!ex.log) return;
    const int s = int(ex.clock);
    std::fprintf(ex.log, "%02d:%02d:%02d  %s\n", s / 3600, s / 60 % 60, s % 60, line.c_str());
    std::fflush(ex.log);
    SDL_Log("explore %s", line.c_str());
}
std::string Viewer::explorePhase() const {
    if (titleMenu.stage) return "title";
    if (credits.stage) return "credits";
    if (battleMode) return "battle " + std::to_string(battle.coupling);
    if (eventMode) return "vista " + status;
    if (fieldMode) return "field " + field.info().name;
    return "";
}
// the explorer's grid of this map (built on arriving, grown where the leader lands off it - a
// sprint's or a jump's landing), its exits marked as zones
void Viewer::exploreGrid() {
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
bool Viewer::explorePath(const std::string& target, const cr2::Vec3& goal, float reach, std::vector<cr2::Vec3>& out) {
    const std::vector<std::string> exits = field.exitNames();
    int allow = -1;
    if (target.rfind("exit:", 0) == 0)
        for (size_t k = 0; k < exits.size(); ++k)
            if ("exit:" + exits[k] == target) allow = int(k);
    if (!ex.grid.path(controller->position(), goal, out, allow) || out.empty()) return false;
    const cr2::Vec3& e = out.back();
    return std::hypot(e[0] - goal[0], e[2] - goal[2]) <= reach + 0.06f;
}
bool Viewer::explorePlan() {
    if (!controller) return false;
    exploreGrid();
    ex.at = 0;
    return explorePath(ex.target, ex.goal, ex.reachPlan, ex.path);
}
// the next thing to do on the field (false: nothing left in this state of the story)
bool Viewer::explorePick() {
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
void Viewer::exploreStep(float& sx, float& sy) {
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
    if (battleMode || eventMode || credits.stage || (script && !fieldControl())) { // (pages, results: tapped now and then)
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
void Viewer::refreshLeader() {
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
bool Viewer::changeButtonShown() const {
    return fieldTabShown() && game.changeEnable && game.flag(0x3fb) && script && script->fieldCharacter(1) >= 0;
}
void Viewer::startChange() {
    if (changeStage || !changeButtonShown()) return;
    audio.playSe(0xb4, false);
    changeStage = 1, changeFrames = 0;
}
void Viewer::updateChange(float dt) {
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
        changeTurn = true; // (+0xa2: the new leader turned to the nearest - set as the lighting begins)
        for (int c = 0; c < 3; ++c) fig.tint[c] = 0;
        return;
    }
    const float k = std::min(changeFrames / 32, 1.0f);
    for (int c = 0; c < 3; ++c) fig.tint[c] = k;
    if (changeFrames >= 32) changeStage = 0;
}
void Viewer::drawChangeButton(const Mat4& view) {
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
auto Viewer::worldRows() -> std::vector<ListRow> {
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
void Viewer::openWorldSelect() {
    world = WorldSelect();
    world.stage = 1, world.at = uiClock;
    SDL_Log("world select: %zu dungeons", worldRows().size());
    if (worldRows().empty()) world.stage = 0, pendingBranch = true;
}
void Viewer::openChapterSelect() {
    world = WorldSelect();
    world.chapters = true, world.stage = 1, world.at = uiClock;
    SDL_Log("chapter select: %zu chapters", worldRows().size());
    if (worldRows().empty()) world.stage = 0, pendingBranch = true;
}
// MapSelectMenu::CheckCoupling: a dungeon whose flag (0x534578, by its number) is set, and of
// its rows (0x5345d8: the dungeon, a flag, the two characters from 0) one whose flag is set,
// wants that pair as the party (cr3_common_data +3, +4); the pair it wants, or none
bool Viewer::worldPair(const cr2::GameState& g, int dungeon, int pair[2]) {
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
void Viewer::worldPick(const ListRow& r) {
    audio.playSe(0xb4, false);
    int pair[2];
    if (world.stage == 1 && world.chapters) world.dungeon = r.id, world.dungeonFocus = world.focus, world.stage = 3;
    else if (world.stage == 1 && worldPair(game, r.id, pair)) world.dungeon = r.id, world.dungeonFocus = world.focus, world.stage = 4; // (the System window: announce 0x81, yes / no)
    else if (world.stage == 1) world.dungeon = r.id, world.dungeonFocus = world.focus, world.stage = 2, world.focus = 0;
    else if (world.stage == 2) world.level = r.id, world.stage = 3;
    world.at = uiClock;
}
void Viewer::worldBack() {
    audio.playSe(0xb5, false);
    if (world.stage == 2 || (world.stage == 3 && world.chapters)) world.stage = 1, world.focus = world.dungeonFocus, world.at = uiClock;
    else if (world.stage == 3) world.stage = 2, world.at = uiClock;
    else world.stage = 0, pendingBranch = true; // (back to the field: what else was reserved)
}
void Viewer::worldAnswer(bool yes) {
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
        return queueEvent("chapter_select");
    }
    game.selectDungeon = world.dungeon, game.difficulty = world.level - 1;
    SDL_Log("world select: dungeon %d, level %d", world.dungeon, world.level);
    world.stage = 0;
    queueEvent("WarpTask"); // (FldMain step 10: CallEvent("WarpTask") - EventExecute the update after)
}
void Viewer::worldTap(int id) {
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
void Viewer::worldKey(SDL_Keycode k) {
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
void Viewer::drawWorldSelect(const Mat4& view, int w, int h) {
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
auto Viewer::libraryRows() -> std::vector<ListRow> {
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
void Viewer::openLibrary() {
    library = Library();
    library.stage = 1, library.at = uiClock;
    SDL_Log("library: shelf %d, %zu entries", game.libraryArchive, libraryRows().size());
    if (libraryRows().empty()) library.stage = 0, pendingBranch = true;
}
void Viewer::libraryPick(const ListRow& r) {
    audio.playSe(0xb4, false);
    game.libraryId = r.id;
    library.stage = 2;
    SDL_Log("library: entry %d", r.id);
    callGimmick("library_disp"); // (InterfaceEvent::CallScript from the menu: at once, not CallEvent)
    if (!script->running()) library.stage = 1, library.at = uiClock; // (no reading: the list again)
}
void Viewer::libraryBack() {
    audio.playSe(0xb5, false);
    library.stage = 0, pendingBranch = true; // (back to the field)
}
void Viewer::libraryTap(int id) {
    const std::vector<ListRow> rows = libraryRows();
    if (id == 100) return libraryBack();
    if (id >= 1000 && id < 1000 + int(rows.size())) library.focus = id - 1000;
    else
        for (const ListRow& r : rows)
            if (r.id == id) return libraryPick(r);
}
void Viewer::libraryKey(SDL_Keycode k) {
    const std::vector<ListRow> rows = libraryRows();
    const int n = std::max(int(rows.size()), 1);
    if (k == SDLK_UP) library.focus = (library.focus + n - 1) % n;
    else if (k == SDLK_DOWN) library.focus = (library.focus + 1) % n;
    else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && library.focus < int(rows.size())) libraryPick(rows[size_t(library.focus)]);
    else if (k == SDLK_BACKSPACE) libraryBack();
}
void Viewer::drawLibrary(const Mat4& view, int w, int h) {
    const std::vector<ListRow> rows = libraryRows();
    if (rows.empty()) return;
    library.focus = std::clamp(library.focus, 0, int(rows.size()) - 1);
    drawSkillList(rows, rows[size_t(library.focus)].id, uiClock - library.at, view, w, h, -1, -1);
}
std::vector<int> Viewer::pubPosters() const {
    std::vector<int> v;
    for (int i = 1; i <= 50 && v.size() < 9; ++i)
        if (game.flag(1200 + i)) v.push_back(i);
    return v;
}
int Viewer::pubListCount() const { return !game.flag(201) ? 28 : !game.flag(202) ? 42 : 50; } // (PubListMenu::Initialize)
// the list's sign of a request (PosterSId::SetSign - hud_posterS's quarters): 1 posted, 2 taken,
// 3 done (flag 1300 + id), 4 not yet
int Viewer::pubSign(int id) const {
    int sign = game.flag(1200 + id) ? 1 : 4;
    if (game.globalWork[7] == id) sign = 2;
    if (game.flag(1300 + id)) sign = 3;
    return sign;
}
void Viewer::openPub() {
    pub = Pub();
    pub.stage = 1, pub.at = uiClock;
    SDL_Log("pub: %zu requests posted", pubPosters().size());
}
void Viewer::pubEnd(int result) { // (PubMainMenu: ResultPubParameter - FldMain's CallEvent / CallAtMenuFinished)
    pub.stage = 0;
    SDL_Log("pub: %d", result);
    if (result == 1) queueEvent("pub_checkA"); // (CallEvent: the update after)
    else if (result == 2) queueEvent("pub_checkG");
    else pendingBranch = true;
}
void Viewer::pubTap(int id) {
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
// the board's focus moved by a direction (PubTopMenu's update, FUN_810b7b62): the posters three a
// row (1 .. 9 there, pub.focus + 1 here) and Check (0 there, -1 here). Left / Right along a row,
// past its end (or the last poster) Check; from Check Left the sixth (or the last), Right the
// fourth (or the first); Up / Down a row on, round, where there is a poster; each move SE 0xb3
void Viewer::pubMove(SDL_Keycode k) {
    const int count = int(pubPosters().size());
    pub.focus = std::clamp(pub.focus, -1, count - 1);
    int f = pub.focus + 1, to = f;
    if (f == 0) {
        if (k == SDLK_LEFT) to = std::min(count, 6);
        else if (k == SDLK_RIGHT) to = count < 4 ? std::min(count, 1) : 4;
    } else if (k == SDLK_RIGHT) to = f == count || f % 3 == 0 ? 0 : f + 1;
    else if (k == SDLK_LEFT) to = (f - 1) % 3 != 0 ? f - 1 : 0;
    else if (k == SDLK_UP || k == SDLK_DOWN) {
        const int t = f + (k == SDLK_UP ? (f - 1 < 3 ? 6 : -3) : (f - 1 < 6 ? 3 : -6));
        if (t >= 1 && t <= count) to = t;
    }
    if (to == f) return;
    keysUsed = true;
    pub.focus = to - 1;
    audio.playSe(0xb3, false);
    SDL_Log("pub: %s in focus", to ? ("poster " + std::to_string(to)).c_str() : "check");
}
void Viewer::pubKey(SDL_Keycode k) {
    if (pub.stage == 4 && (k == SDLK_LEFT || k == SDLK_RIGHT)) {
        const int count = pubListCount();
        pubTap(2000 + (pub.listFocus + (k == SDLK_LEFT ? count - 2 : 0)) % count + 1);
    } else if (pub.stage == 1 && (k == SDLK_LEFT || k == SDLK_RIGHT || k == SDLK_UP || k == SDLK_DOWN)) pubMove(k);
    else if (pub.stage == 1 && (k == SDLK_RETURN || k == SDLK_KP_ENTER)) pubTap(pub.focus < 0 ? 35 : 1000 + pub.focus); // (○: Check's
        // list - PubTopMenu's 200 -, else the poster in focus picked)
    else if (pub.stage == 2 && (k == SDLK_RETURN || k == SDLK_KP_ENTER)) pubTap(game.globalWork[7] == pub.request ? 39 : 38);
    else if (pub.stage == 3 && (k == SDLK_Y || k == SDLK_RETURN)) pubTap(7601);
    else if (pub.stage == 3 && k == SDLK_N) pubTap(7602);
    else if (k == SDLK_BACKSPACE) pub.stage == 3 ? pubTap(7602) : pubTap(100);
}
void Viewer::drawPub(const Mat4& view, int w, int h) {
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
        pub.focus = std::clamp(pub.focus, -1, int(posters.size()) - 1); // (-1: Check's)
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
        if (pub.focus >= 0) help(posters[size_t(pub.focus)]);
        auto keepLook = buttonLook;
        buttonLook = [&](int id) { return id == 35 && pub.focus < 0 && keysUsed ? 2 : 1; }; // (Check in focus: lit)
        drawCommandMenu("checkbtn_io", t, view, {}, {}, 0, 0, 0, "checkbtn_io"); // (the list: circle 35 - PubTopMenu's 200)
        buttonLook = keepLook;
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

void Viewer::openShop() {
    shop = Shop();
    shop.stage = 1, shop.at = uiClock;
    SDL_Log("shop: open");
}
int Viewer::shopRank() const {
    int r = 0;
    for (int f = 1081; f <= 1085; ++f) r += game.flag(f);
    return r;
}
std::vector<int> Viewer::shopChars() const { // (the characters an equipment list is for: ShopWepon..ListMenu::CharaChange - every one joined, Cr3UtilValidCharacter)
    std::vector<int> c = validChars();
    if (c.empty()) c.push_back(0);
    return c;
}
int Viewer::spareCount(int id) const { // (Cr3UtilGetNotEquippedItemNumber: held, less those worn)
    int worn = 0;
    for (int c = 0; c < 9; ++c)
        for (int s = 0; s < 3; ++s) worn += game.equip[c][s] == id;
    return std::max(int(game.itemCount[id]) - worn, 0);
}
auto Viewer::shopRows() -> std::vector<ListRow> {
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
int Viewer::shopLimit() const {
    const cr2::BattleData::Item* it = battleData.item(shop.item);
    if (!it) return 0;
    if (!shop.buy) return shop.kind == 0 ? game.itemCount[shop.item] : spareCount(shop.item);
    const int room = 99 - game.itemCount[shop.item];
    return it->price <= 0 ? room : std::min(room, game.oz / it->price);
}
void Viewer::shopClose(const char* call) {
    shop.stage = 0;
    if (call) queueEvent(call); // (FldMain: CallEvent - EventExecute the update after)
    else pendingBranch = true;   // (back to the field)
}
void Viewer::shopTap(int id) {
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
void Viewer::exploreShop() {
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
void Viewer::shopKey(SDL_Keycode k) {
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
    else if ((k == SDLK_PAGEUP || k == SDLK_PAGEDOWN) && shop.kind > 0) shopTap(k == SDLK_PAGEUP ? 6511 : 6512); // (L / R: the
        // list's switch, ScrollListMenu's 8 - ShopWepon Buy / SellListMenu (FUN_810c4650 / FUN_810c54ca) the character, SE 0xb7)
    else if ((k == SDLK_LEFT || k == SDLK_RIGHT) && shop.kind > 0) { // (the d-pad's Left / Right, the same lists: the
        // kind - weapons, armour, accessories round -, SE 0xb7)
        shop.kind = k == SDLK_RIGHT ? shop.kind % 3 + 1 : (shop.kind + 1) % 3 + 1, shop.focus = 0;
        audio.playSe(0xb7, false);
    } else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER) && shop.focus < int(rows.size())) shopTap(rows[size_t(shop.focus)].id);
    else if (k == SDLK_BACKSPACE) shopTap(100);
}
void Viewer::shopToken(const std::string& t) {
    static const std::map<std::string, int> kButtons = {{"talk", 13}, {"buy", 14}, {"sell", 15}, {"items", 16}, {"weapons", 21},
                                                        {"armour", 22}, {"accessories", 23}, {"back", 100}};
    if (auto b = kButtons.find(t); b != kButtons.end()) return shopTap(b->second);
    static const std::map<std::string, SDL_Keycode> kKeys = {{"up", SDLK_UP}, {"down", SDLK_DOWN}, {"pick", SDLK_RETURN}, {"more", SDLK_UP},
                                                             {"less", SDLK_DOWN}, {"yes", SDLK_Y}, {"no", SDLK_N}, {"char", SDLK_PAGEDOWN},
                                                             {"kind", SDLK_RIGHT}};
    if (auto k = kKeys.find(t); k != kKeys.end()) shopKey(k->second);
}
void Viewer::drawShop(const Mat4& view, const Mat4& proj, int w, int h) {
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
std::vector<int> Viewer::partyChoices() const {
    std::vector<int> c;
    for (int ch = 0; ch < 7; ++ch)
        if (game.flag(1011 + ch) && game.party[0] != ch && game.party[1] != ch) c.push_back(ch);
    return c;
}
void Viewer::partyTap(int id) {
    auto stage = [&](int s) { party.stage = s, party.at = uiClock, party.focus = 0; };
    if (party.stage == 1) {
        if (id == 25) audio.playSe(0xb4, false), stage(2);
        else if (id == 13) audio.playSe(0xb4, false), party.stage = 0, queueEvent("party_talk"); // (FldPartyOrganize 3: CallEvent)
        else if (id == 100) audio.playSe(0xb5, false), party.stage = 0, pendingBranch = true;    // (2: the field)
    } else if (party.stage == 2) {
        if (id == 1 || id == 2) {
            if (id == 1 && game.flag(0x3fa)) return (void)audio.playSe(0xb6, false); // (the first fixed: flag 0x3fa)
            audio.playSe(0xb4, false), party.slot = id - 1, stage(3);
        } else if (id == 100) audio.playSe(0xb5, false), party.stage = 0, queueEvent("party_end"); // (4: CallEvent)
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
void Viewer::partyKey(SDL_Keycode k) {
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
void Viewer::drawParty(const Mat4& view, const Mat4& proj, int w, int h) {
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

void Viewer::drawFieldUi(int w, int h) {
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
void Viewer::updateBattery() {
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
void Viewer::drawBattery(const Mat4& view) {
    updateBattery();
    UiFig& b = uiFig("battery_id");
    b.fig.place = cr2::Mat34();
    poseUi(b, "battery_id", batteryFrame);
    drawFigure(b.fig, view.data());
}

void Viewer::drawBalloons(int w, int h) {
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
        balloonFigure.place.m[3] = nx * float(frameW) * 0.5f / k, balloonFigure.place.m[7] = ny * float(frameH) * 0.5f / k; // (the field's camera across the frame)
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

// the flag marks (FldFlagMark::Update: the `flag` figure at a flagged gimmick's balloon place,
// FldUtilGetBalloonPosOffset - hidden while it has a balloon: CheckEntryFlagName)
void Viewer::drawFlagMarks(int w, int h) {
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
        f.fig.place.m[3] = (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]) / cw * float(frameW) * 0.5f / k;
        f.fig.place.m[7] = (m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]) / cw * float(frameH) * 0.5f / k;
        poseUi(f, "flag", time, true);
        drawFigure(f.fig, view.data());
    }
}

} // namespace cr2_viewer
