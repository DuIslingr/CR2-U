#include "viewer/viewer.hpp"

#include "assets/release.hpp"
#include "platform/atomic_file.hpp"
#include "script/gamesave.hpp"

namespace cr2_viewer {

// The events' runtime the screen shows: the vista's, else the field's
cr2::EventRuntime* Viewer::activeEvents() {
    if (titleMenu.stage && titleEvents) return titleEvents.get(); // (title.nut's)
    if ((battleMode || campOpen) && manualStage == 2 && manualEvents) return manualEvents.get(); // (the manual's tutorial)
    if (battleMode && battleScript) return &battleScript->events(); // (the battle's talks)
    return eventMode ? event.get() : script ? &script->events() : nullptr;
}
void Viewer::branchAfterVista() {
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
void Viewer::startCredits(int n) {
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
std::string Viewer::creditFigure() const {
    static const char* const kDefault[4] = {"end2_BG", "end1_BG", "end3_BG", "end4_BG"};
    std::string f = staffFigures[credits.number].empty() ? kDefault[credits.number] : staffFigures[credits.number];
    std::transform(f.begin(), f.end(), f.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return f;
}
float Viewer::creditScale() const {
    const float aspect = float(screenW) / float(std::max(screenH, 1));
    return aspect < 1.5f ? 1.0f : aspect / 1.5f;
}
bool Viewer::creditsLineShown(size_t i) const {
    return i < staffLines.size() && !staffLines[i].empty() && (i < 44 || i > 49 || game.flag(201));
}
void Viewer::updateCredits(float dt) {
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
void Viewer::drawCredits() {
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
        fullFrameQuad(white, kScreen, rgba);
    }
}
// the options' window, its question and the manual (in a battle or the camp) taking a key:
// false - none of them open
bool Viewer::optionsKey(SDL_Keycode k) {
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
        if (manualCloseAt >= 0 || manualInTicks < 5) return true; // (+0x1c 30)
        const int n = std::max(int(manualEntries.size()), 1);
        if (k == SDLK_UP) manualCursor = (manualCursor + n - 1) % n;
        else if (k == SDLK_DOWN) manualCursor = (manualCursor + 1) % n;
        else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) playManual(manualCursor);
        else if (k == SDLK_BACKSPACE) closeManual();
        return true;
    }
    if (optionsAsk) { // (the question: Return yes, Backspace no)
        if (optionsCloseAt >= 0) return true; // (its No closing)
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_Y) optionTap(3201);
        else if (k == SDLK_BACKSPACE || k == SDLK_N) optionTap(3202);
        return true;
    }
    if (optionsOpen) { // (the options: Backspace / Return back, Left / Right the focused bar; G / Tab
                       // CR2-U's Display & Graphics)
        if (optionsCloseAt >= 0) return true; // (closing: +0x1c 30)
        if (k == SDLK_I) optionTap(3111);
        else if (k == SDLK_G || k == SDLK_TAB) openSettings();
        else if (k == SDLK_BACKSPACE || k == SDLK_RETURN || k == SDLK_KP_ENTER) optionsBack();
        else if (k == SDLK_UP) optionFocus = (optionFocus + 2) % 3;
        else if (k == SDLK_DOWN) optionFocus = (optionFocus + 1) % 3;
        else if (k == SDLK_LEFT && optionFocus < 3) optionVolume[optionFocus] = std::max(optionVolume[optionFocus] - 5, 0), volumeSet(optionFocus);
        else if (k == SDLK_RIGHT && optionFocus < 3) optionVolume[optionFocus] = std::min(optionVolume[optionFocus] + 5, 100), volumeSet(optionFocus);
        return true;
    }
    return false;
}
// a volume set (OptionMenu::TouchSimpleRelease: the effects' bar let go - Cr3OptionData::SetSE -
// plays SE 0xc6 at it; the voices' - SetVoice - voice 0x8d4)
void Viewer::volumeSet(int bar) {
    audio.setVolumes(float(optionVolume[0]) / 100.0f, float(optionVolume[1]) / 100.0f, float(optionVolume[2]) / 100.0f);
    if (bar == 1) audio.playSe(0xc6, false);
    else if (bar == 2) audio.playVoice(0x8d4);
}
// a tap on the options' window, its question or the manual (`x`: across, UI units): false -
// none of them open
bool Viewer::optionsTapped(const TapRect& r, float x) {
    if (saveImport.active()) {
        if (r.id == 3112) saveImportAnswer(true);
        else if (r.id == 3113 || r.id == 3114) saveImportAnswer(false);
        return true;
    }
    if (settingsTapped(r.id)) return true;
    if (manualStage == 1) {
        if (manualCloseAt >= 0 || manualInTicks < 5) return true;
        if (r.id == 100) closeManual();
        else if (r.id >= 5000) playManual(r.id - 5000);
        else if (r.id >= 1000) manualCursor = r.id - 1000; // (another row: the focus)
        return true;
    }
    if (optionsAsk) {
        if (optionsCloseAt >= 0) return true;
        if (r.id == 3201 || r.id == 3202) optionTap(r.id);
        return true;
    }
    if (optionsOpen) { // (Back; a bar's slide area: its volume by the point across it)
        if (optionsCloseAt >= 0) return true; // (closing)
        if (r.id == 100) optionsBack();
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

bool Viewer::startEvent(const std::string& function, const std::string& map, float page) {
    initEventDrawing();
    pageSeconds = page;
    return startVista(function, map);
}
void Viewer::initEventDrawing() {
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
bool Viewer::startVista(const std::string& function, const std::string& map) {
    std::string err;
    unload(vistaMap);
    vistaMap = ViewFigure();
    for (ViewFigure& f : eventFigures) unload(f);
    eventFigures.clear(), eventFigureNames.clear();
    eventEnded = false, eventEndHold = 0, eventClock = 0, messageLogged = false;
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

// one of the game's updates, the figures following the scene's models. The script's end:
// VistaMain::EventExecute sees its state 4 the update after and says so the one after that
// (VistaMain state 200), which waits for Cr3Fade's count, then two updates more (its +0x24 = 2)
// and sets the next main; GameMain::Update makes and updates it the update after - the scene,
// the windows and the fade updated meanwhile. The battle's (the field's, the credits') first
// update is the sixth after the script's last; another vista's script is loaded in that one
// and its function called in the seventh
void Viewer::stepEvent() {
    if (eventEnded && eventEndHold <= 0) return;
    if (!event->tick()) {
        SDL_Log("event: %s", event->error().c_str());
        eventEnded = true, eventEndHold = 0;
    }
    if (!event->running() && !eventEnded) {
        eventEnded = true;
        SDL_Log("event ended at frame %d (%.2f s)%s", event->frame(), event->frame() * cr2::EventRuntime::kTick,
                event->battle() >= 0 ? (": battle " + std::to_string(event->battle())).c_str() : "");
        if (playMode) eventEndHold = eventEndTotal = game.next.battle < 0 && game.next.vista ? 7 : 6;
    } else if (eventEnded && eventEndHold > 0) {
        if (eventEndHold == eventEndTotal - 2 && event->fade().moving()) {
            // (state 200: a fade still going is waited for)
        } else if (--eventEndHold == 0) {
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
void Viewer::syncEventFigures(cr2::EventRuntime& ev) {
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
float Viewer::eventLead() const { return battleMode && battleScript ? battleScriptClock : eventMode ? (eventEnded && eventEndHold <= 0 ? 0.0f : eventClock) : fieldClock; }

void Viewer::updateEvent(float dt) {
    if (event && !event->mapAnimPaused) vistaMapTime += dt;
    eventClock = std::min(eventClock + dt, 0.25f);
    for (; eventClock >= cr2::EventRuntime::kTick && eventMode; eventClock -= cr2::EventRuntime::kTick) stepEvent();
}

// the models where the scene has them, playing what the script chose (ChangeAnimator: their
// <figure><suffix>; else the figure's own), their faces as ChangeModelFace left them
void Viewer::poseEvent() {
    pose(vistaMap, vistaMapTime);
    poseEventModels(*event);
}

void Viewer::poseEventModels(cr2::EventRuntime& ev) {
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
        // (EvtMotionManager::Update: the update after SetMotion puts the weight at 0, each after
        // counts one of the blend's updates - its value / 0.05 - towards 1)
        const float blendAt = std::max(m.animTime + eventLead() - cr2::EventRuntime::kTick, 0.0f);
        if (m.blendSeconds > 0 && blendAt < m.blendSeconds && !m.blendFrom.empty()) {
            const std::string from = m.blendFrom + ".anim";
            if (from != f.blendName) {
                std::vector<uint8_t> d;
                f.blendName = from;
                f.blendLoaded = archive.read(from, d) && f.blendAnim.parse(d) && f.blendAnim.joints() == int(f.skeleton.basePose.size());
            }
            f.blendAbsolute = m.blendFrom == m.figure;
            f.blendTime = m.blendFromTime + m.animTime + eventLead();
            f.blendWeight = blendAt / m.blendSeconds;
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

void Viewer::drawEvent(int w, int h) {
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

void Viewer::loadWindow(WindowLayout& w, const char* figure) {
    std::string err;
    if (!loadFigure(std::string(figure) + ".geom", w.fig, &err)) SDL_Log("window %s: %s", figure, err.c_str());
    std::vector<uint8_t> d;
    if (archive.read(std::string(figure) + ".name", d)) w.joints = cr2::parseJointNames(d);
    for (const std::string& j : w.joints)
        if (j.compare(0, 17, "call_sendCsr_loop") == 0 || j.compare(0, 16, "call_msgCsr_loop") == 0) w.cursorJoint = j;
}

void Viewer::loadMessageWindow(const std::string& fontPath, std::vector<uint8_t> fontBytes, const std::string& fontName) {
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

// a joint of a window's figure, posed, in UI units
bool Viewer::windowJoint(const WindowLayout& wl, const std::string& name, float& x, float& y) {
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
std::string Viewer::slotJoint(const WindowLayout& wl, size_t slot) {
    if (slot >= wl.slots.size()) return {};
    for (const std::string& j : wl.joints)
        if (j.size() == 9 && j.compare(0, 4, "type") == 0 && j.compare(7, 2, wl.slots[slot]) == 0) return j;
    return {};
}

void Viewer::drawWindow(WindowLayout& wl, bool showsMessage, int w, int h) {
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
        // (EvtWindow::ShowMessage: each line is its own call with its `\c` - the pack's +0x31 -
        // choosing the centred slots: a tutorial's "\n\c Moving" puts its title in the second
        // centred slot under an empty first line)
        if (wl.nameTag) {
            add(speaker, 0, 0, true);
            for (size_t i = 0; i < lines.size() && i < size_t(wl.linesPerPage); ++i)
                add(lines[i].text, (lines[i].centred ? 1 + wl.linesPerPage : 1) + i, lines[i].colour, false);
        } else if (wl.centredSlots) {
            for (size_t i = 0; i < lines.size() && i < size_t(wl.linesPerPage); ++i)
                add(lines[i].text, (lines[i].centred ? size_t(wl.linesPerPage) : 0) + i, lines[i].colour, false);
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

void Viewer::loadCutIns() {
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

void Viewer::drawCutIns(int w, int h) {
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
        const float t = c.fresh ? c.time : c.closing ? std::max(c.time - eventLead(), 0.0f) : std::min(c.time + eventLead(), cr2::EventRuntime::kCutInSeconds);
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
bool Viewer::saveExists(const char* file) { // (an empty one: DeleteCont's - none)
    std::FILE* f = std::fopen(savePath(file).c_str(), "rb");
    if (!f) return false;
    const bool some = std::fgetc(f) != EOF;
    std::fclose(f);
    return some;
}
auto Viewer::saveInfo(const char* file) -> SaveInfo {
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
// A title transition abandons menus and their callbacks; it never confirms them.
// Keep game progress and preferences until the player chooses a save or new game.
void Viewer::resetSessionUi() {
    campOpen = mapOpen = settingsOpen = optionsOpen = optionsAsk = false;
    optionsCloseAt = -1;
    saveStage = manualStage = 0;
    saveFailed = false;
    world.stage = shop.stage = party.stage = library.stage = pub.stage = credits.stage = 0;
    worldSwitchAt = worldOutAt = -1, worldInTicks = 5;
    partyTitleOutAt = -1;
    shopSwitchAt = -1, shopInTicks = 5;
    pubSwitchAt = -1, pubInTicks = 5;
    paused = fieldTabOpen = false;
    script.reset();
    event.reset();
    manualEvents.reset();
    cancelPointerInput();
    keyLock.stage = iai.stage = aim.stage = vslash.stage = 0;
    tapRects.clear();
    keyFocus = -1;
    pendingBranch = false;
    changeStage = 0;
    toTitle = false;
    fieldMode = eventMode = battleMode = false;
    saveImport.cancel();
}
void Viewer::startTitle() {
    resetSessionUi();
    cancelSceneAudio();
    pendingBgm = 0, pendingBgmAt = 0;
    titleMenu = TitleMenu();
    titleMenu.stage = 1, titleMenu.start = uiClock;
    titleEvents.reset();
    fieldMode = eventMode = false;
    audio.stopBgm(10);
    SDL_Log("title: the logo");
}
bool Viewer::titleRunning() const { return titleEvents && titleEvents->running(); }
void Viewer::titleScript(const char* function) {
    titleEvents = std::make_unique<cr2::EventRuntime>(archive, game);
    titleEvents->language = optionLanguage, titleEvents->autoPage = pageSeconds;
    if (!titleEvents->start("title", function)) {
        SDL_Log("title: %s - %s", function, titleEvents->error().c_str());
        titleEvents.reset();
    }
}
float Viewer::titleAnimLength(const std::string& fig, const std::string& anim) {
    UiFig& u = uiFig(fig);
    poseUi(u, anim, 0);
    return u.fig.animLoaded ? u.fig.anim.duration() : 0.0f;
}
// Cr3UtilResetWork(true) / Cr3InitWorkFromBattleData(.., true) - a new game with the clear data:
// the common data kept (the items, the Oz, the sopia's levels and EXP), each character's weapon
// and armour (cr3_player_data +0x3c, +0x3e) and seeds, the flags past 3000 (and the system's,
// 0 .. 1000 - cr3_flag_system_work, untouched); the levels, the rest of the equipment, the story's
// flags (1001 .. 3000) and the global work a new game's - ResetWork zeroes cr3_treasureBox + 0x24
// .. cr3_global_work + 0x7c either way, the global work whole (Cr3Work: the scripts' GetVar - 0 the
// scenario, which d1020's evt000 must find 0 or 1 to begin the story, SkipPrologue's question with it)
bool Viewer::carryOver() {
    std::FILE* f = std::fopen(savePath("save1.txt").c_str(), "rb");
    if (!f) return false;
    std::string text;
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) text.append(buf, n);
    std::fclose(f);
    cr2::GameState old;
    if (!old.load(text)) return false;
    std::memcpy(game.itemCount, old.itemCount, sizeof game.itemCount);
    game.oz = old.oz;
    std::memcpy(game.sopiaLevel, old.sopiaLevel, sizeof game.sopiaLevel);
    std::memcpy(game.sopiaExp, old.sopiaExp, sizeof game.sopiaExp);
    for (int c = 0; c < 9; ++c) {
        game.equip[c][0] = old.equip[c][0], game.equip[c][1] = old.equip[c][1];
        std::memcpy(game.seed[c], old.seed[c], sizeof game.seed[c]);
    }
    for (size_t k = 3001; k < old.flags.size(); ++k) game.setFlag(int(k), old.flags[k]);
    SDL_Log("title: the clear data carried over");
    return true;
}
// A save that can't be read: the title again and a word, the file left as it is - never the app
// closed, nor a new game that a later save would write over it
void Viewer::titleLoadFailed() {
    startTitle();
    titleMenu.notice = "Could not read this save.\\nThe file has been kept.";
}
void Viewer::titleAsk(int id) {
    titleMenu.ask = id, titleMenu.stage = 7, titleMenu.start = uiClock;
    SDL_Log("title: ask %d", id);
}
void Viewer::titleButton(int id) {
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
        if (t.stepAt >= 0) return; // (the panel going: +0x1c 30)
        auto step = [&](int ask) { t.stepAsk = ask, t.stepTo = 7, t.stepAt = uiClock, t.stepFrom = uiClock - t.start, t.stepTicks = 0; };
        if (id == 9) { // (New Game: NewGame's question - with clear data 102)
            audio.playSe(0xb4, false);
            step(saveInfo("save1.txt").flags.count(5) ? 102 : 101);
        } else if (id == 10 && saveExists("save1.txt")) {
            audio.playSe(0xb4, false);
            step(107);
        } else if (id == 100) { // (CreateTitleMenu(true): the buttons back)
            audio.playSe(0xb5, false);
            t.stage = 4, t.start = uiClock;
        }
    }
}
void Viewer::titleAnswer(bool yes) {
    TitleMenu& t = titleMenu;
    if (t.stage != 7 || t.stepAt >= 0) return;
    if (!t.notice.empty()) { // (told: the buttons again)
        audio.playSe(0xb4, false);
        t.notice.clear();
        t.stage = 5, t.start = uiClock;
        return;
    }
    const int ask = t.ask;
    // (101 and 107 turn the choice's sound off - SEPermitBattleYNChoicePushBottom(false) - and play 0xbe
    // for Yes themselves; 102 / 103 leave it on: the choice's 0xb4 either way)
    audio.playSe(yes && ask != 102 && ask != 103 ? 0xbe : 0xb4, false);
    // (No: EndBattleYNChoice, state 0x15 - the panel (0xb) once the choice is gone, the fifth update. Yes: the
    // same wait (0x16 / 0x11), then Cr3FadeOut(0, 20) and the 1.0 s count (0x23): the fade from the fifth)
    auto toPanel = [&] { t.stepTo = 6, t.stepAt = uiClock, t.stepFrom = uiClock - t.start, t.stepTicks = 0; };
    if (ask == 104) { // (the autosave: resumed, else the panel)
        if (yes) t.action = 3, t.stage = 8, t.ask = 0, t.doneAt = uiClock + 1.25f;
        else toPanel();
    } else if (ask == 101 || ask == 103) {
        if (yes) t.action = 1, t.stage = 8, t.ask = 0, t.doneAt = uiClock + 1.25f;
        else toPanel();
    } else if (ask == 102) { // (with the clear data: Cr3UtilResetWork(true) - what it carries over)
        if (yes) t.action = 4, t.stage = 8, t.ask = 0, t.doneAt = uiClock + 1.25f;
        else titleAsk(103);
    } else if (ask == 107) {
        if (yes) t.action = 2, t.stage = 8, t.ask = 108, t.doneAt = uiClock + 2.0f; // ("Load complete." a second, then the fade)
        else toPanel();
    }
}
void Viewer::titleToken(const std::string& tok) {
    if (saveImport.active()) {
        if (tok == "yes") saveImportAnswer(true);
        else if (tok == "no" || tok == "back") saveImportAnswer(false);
        return;
    }

    SDL_Log("title: input %s (stage %d)", tok.c_str(), titleMenu.stage);
    static const std::pair<const char*, int> kButtons[] = {{"start", 8}, {"options", 20}, {"scenarios", 24}, {"new", 9}, {"load", 10}, {"back", 100}};
    if (titleMenu.stage == 3) titleMenu.tapped = true;
    else if (titleMenu.stage == 7 && (tok == "yes" || tok == "no")) titleAnswer(tok == "yes");
    else if (titleMenu.stage == 9 && settingsOpen) { // (the Graphics list: as the camp's)
        if (tok == "up" || tok == "down" || tok == "left" || tok == "right" || tok == "enter")
            settingsKey(tok == "up" ? SDLK_UP : tok == "down" ? SDLK_DOWN : tok == "left" ? SDLK_LEFT : tok == "right" ? SDLK_RIGHT : SDLK_RETURN);
        else if (tok == "back") closeSettings();
    } else if (titleMenu.stage == 9) {
        static const std::pair<const char*, int> kOptionTaps[] = {{"import", 3111}, {"reset", 3101}, {"manual", 3102}, {"yes", 3201}, {"no", 3202},
                                                                  {"graphics", 3110}};
        for (const auto& [name, id] : kOptionTaps)
            if (tok == name && (optionsOpen || optionsAsk)) return optionTap(id); // (the options' Reset Data and its questions)
        if (tok == "back" && optionsOpen) optionsBack(); // (TitleOptionMenu's Back: its close)
        else if (tok == "back" && campLink == 201) campLink = 0, titleScenariosGone = 1;
    } else
        for (const auto& [name, id] : kButtons)
            if (tok == name) titleButton(id);
}
void Viewer::titleTap() {
    if (titleRunning()) return titleEvents->advance();
    if (titleMenu.stage == 3) titleMenu.tapped = true;
}
bool Viewer::titleClick(float fx, float fy) {
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
void Viewer::updateTitle(float dt) {
    TitleMenu& t = titleMenu;
    if (t.stage == 5 && !t.notice.empty()) t.stage = 7, t.start = uiClock; // (the buttons back: told now)
    if (titleEvents) {
        static float clock = 0;
        clock = std::min(clock + dt, 0.25f);
        for (; clock >= cr2::EventRuntime::kTick; clock -= cr2::EventRuntime::kTick)
            if (!titleEvents->tick()) SDL_Log("title: %s", titleEvents->error().c_str());
        if (!titleEvents->running()) titleEvents.reset();
    }
    if (!inputScript.empty() && uiClock >= inputNext && !titleRunning() && t.stepAt < 0 && (t.stage == 3 || (t.stage >= 5 && t.stage <= 9)) && t.stage != 8 &&
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
            if (!t.contScripted) titleScript("Tutorial_ContData"), t.contScripted = true; // (state 9: once, +0x8c)
            titleAsk(104);
        } else {
            t.stage = 6;
        }
    } else if (t.stage == 8 && uiClock >= t.doneAt) {
        t.stage = 0; // (the main loop takes it from here: action)
        audio.stopBgm(10);
    } else if (t.stage == 9 && titleScenariosGone > 0) { // (the buttons the update after the scenarios' 0x13)
        titleScenariosClock = std::min(titleScenariosClock + dt, 0.25f);
        if (titleScenariosClock >= cr2::EventRuntime::kTick) titleScenariosClock = 0, --titleScenariosGone;
    } else if (t.stage == 9 && !optionsOpen && !optionsAsk && campLink != 201 && manualStage == 0) {
        campOpen = false; // (the window closed: the title's buttons again)
        t.stage = 5, t.start = uiClock;
    }
    if (t.stage == 9) updateManual(dt);
    if (t.stepAt >= 0) { // (a step: the next made the fifth update)
        titleStepClock = std::min(titleStepClock + dt, 0.25f);
        for (; titleStepClock >= cr2::EventRuntime::kTick; titleStepClock -= cr2::EventRuntime::kTick)
            if (++t.stepTicks >= 5) {
                t.stepAt = -1;
                if (t.stepTo == 7) titleAsk(t.stepAsk);
                else t.stage = t.stepTo, t.start = uiClock;
                break;
            }
    } else titleStepClock = 0;
    if (t.stage == 9 && optionsCloseAt >= 0) { // (the options closing: gone the fourth update, the buttons the fifth)
        optionsCloseClock = std::min(optionsCloseClock + dt, 0.25f);
        for (; optionsCloseClock >= cr2::EventRuntime::kTick; optionsCloseClock -= cr2::EventRuntime::kTick)
            if (++optionsCloseTicks >= (optionsCloseTarget ? 4 : 5)) {
                if (!optionsStepDone()) {
                    optionsCloseAt = -1, optionsOpen = false;
                    SDL_Log("title: the options closed");
                }
                break;
            }
    } else optionsCloseClock = 0;
}
void Viewer::drawCalendar(const SaveInfo& s, const Mat4& view, const Mat4& proj, float t) {
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
void Viewer::showJoint(UiFig& u, const std::string& joint, bool visible) {
    for (ViewMesh& vm : u.fig.meshes) {
        const uint32_t j = vm.source.palette.empty() ? ~0u : vm.source.palette[0];
        if (j < u.joints.size() && u.joints[j] == joint) vm.hidden = !visible;
    }
}
void Viewer::hideJoint(UiFig& u, const std::string& joint) { showJoint(u, joint, false); }
// a circle button drawn at `id`: lit if it has the focus (else as its material is)
void Viewer::focusLook(UiFig& c, int id) { litLook(c, buttonLook ? buttonLook(id) == 2 : focusScreen() && id == keyFocus); }
void Viewer::litLook(UiFig& c, bool on) {
    for (ViewMesh& vm : c.fig.meshes) {
        const uint32_t mi = vm.source.material;
        if (mi >= c.materials.size() || c.materials[mi] != "PRG_shiftR") continue;
        vm.material.params[0x5e] = on ? std::vector<float>{0.5f, 0.0f} : vm.baseMaterial.params[0x5e];
        if (vm.material.params[0x5e].empty()) vm.material.params.erase(0x5e);
    }
}
void Viewer::drawTitleButtons(UiFig& parts, const Mat4& view, int skip) {
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
void Viewer::drawTitle() {
    TitleMenu& tm = titleMenu;
    tapRects.clear();
    Mat4 camera = cr2::mat4Identity();
    camera[14] = -0.881f;
    const Mat4 proj = uiProjection(screenW, screenH), view = cr2::mat4Multiply(proj, camera);
    cr2::SceneLighting l;
    dev->setLighting(l);
    float age = uiClock - tm.start;
    static const float kScreen[24] = {-1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1, -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1};
    const Mat4 id = cr2::mat4Identity();
    if (tm.stage == 1) { // (logoInc, from black over 20 frames)
        UiFig& logo = uiFig("logoinc");
        logo.fig.place = cr2::Mat34();
        poseUi(logo, "logoinc", age);
        drawFigure(logo.fig, view.data());
        const float a = std::max(0.0f, 1.0f - age / (20 * cr2::EventRuntime::kTick));
        const float rgba[4] = {0, 0, 0, a};
        if (a > 0 && white.id) fullFrameQuad(white, kScreen, rgba);
        return;
    }
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
    else if (tm.stage == 9) poseUi(ti, "title_out", 1e6f); // (the options over it: the title out, its painting left)
    else poseUi(ti, "title_in", std::max(titleAnimLength("title", "title_in") - (tm.stage == 10 ? age : 1e6f), 0.0f)); // (backwards: gone)
    if (!bg.fig.meshes.empty()) hideJoint(ti, "GEO_titleBG"); // (title_bg in its place)
    if (tm.stage == 9) // (under the options: the painting alone - the credit line and "Please Touch" sat on their buttons)
        for (const char* j : {"GEO_fontMaruC", "GEO_press"}) hideJoint(ti, j);
    drawFigure(ti.fig, view.data());
    if (tm.stage == 9) { // (the options: GameTitleMenu::Draw's last part, TitleOptionMenu, over the title and its buttons)
        drawTitleButtons(ti, view, 0);
        tapRects.clear(); // (the options' taps alone)
        if (titleScenariosGone > 0) return; // (the scenarios gone, the buttons the next update)
        return drawCamp(view, proj, screenW, screenH);
    }
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
        if (a > 0 && white.id) fullFrameQuad(white, kScreen, rgba);
    }
    if (titleRunning()) return;
    if (tm.stage == 5 || tm.stage == 4) drawTitleButtons(ti, view, 0); // (CircleButtonPose: following their joints)
    const bool panel = tm.stage == 6; // (a question: the panel gone - DeleteCircleButton, its parts, the calendar)
    if (tm.stepAt >= 0) age = std::max(std::min(tm.stepFrom, 0.2f) - (uiClock - tm.stepAt), 0.0f); // (a step: the panel or the question played back)
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
        backLook(bk);
        drawFigure(bk.fig, view.data());
        float sx, sy, ex, ey;
        if (tm.stage == 6 && uiJoint(bk, "tapAreaS01", sx, sy) && uiJoint(bk, "tapAreaE01", ex, ey)) tapRects.push_back({100, sx, sy, ex, ey});
    } else if (tm.stage == 7 && tm.ask == 104) {
        drawCalendar(saveInfo("cont.txt"), view, proj, age);
    }
    if (tm.stage == 7 && !tm.notice.empty()) drawAsk(tm.notice, age, view, screenW, screenH, 7401, 7402, true, true);
    else if (tm.stage == 7) drawAsk(announce(tm.ask), age, view, screenW, screenH, 7401, 7402, true);
    else if (tm.stage == 8 && tm.ask == 108 && uiClock < tm.doneAt - 1.0f) drawAsk(announce(108), age, view, screenW, screenH, 0, 0, true);
    if (tm.stage == 8) { // (Cr3FadeOut(0, 20): to black over the last second)
        const float a = std::clamp(1.0f - (tm.doneAt - uiClock), 0.0f, 1.0f);
        const float rgba[4] = {0, 0, 0, a};
        if (a > 0 && white.id) fullFrameQuad(white, kScreen, rgba);
    }
}
bool Viewer::titleOptions() const { return titleMenu.stage == 9; }
void Viewer::resetData() {
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
void Viewer::optionTap(int id) {
    switch (id) {
    case 3101:
        if (optionsCloseAt >= 0) break;
        audio.playSe(0xb4, false); // (OptionMenu::Update state 3 / 4: SE 0xb4)
        if (titleOptions()) optionsReset = 1;
        optionsCloseAt = uiClock, optionsCloseFrom = uiClock - optionsStart, optionsCloseTicks = 0, optionsCloseTarget = 1;
        break;
    case 3102: // (BattleOptionMenu 4 -> 9 -> 0x22: InstructionsWindowMenu, the tutorials' list)
        if (optionsCloseAt >= 0) break;
        audio.playSe(0xb4, false);
        optionsCloseAt = uiClock, optionsCloseFrom = uiClock - optionsStart, optionsCloseTicks = 0, optionsCloseTarget = 2;
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
    case 3105: // (only the languages this copy has: the Vita's no Korean)
        do optionLanguage = (optionLanguage + 7) % 8; while (optionLanguage >= copyLanguages());
        break;
    case 3106: case 4099:
        do optionLanguage = (optionLanguage + 1) % 8; while (optionLanguage >= copyLanguages());
        break;
    case 3201: // (8: the music stopped, InterfaceMain to the title, 0x1d)
        audio.playSe(0xb4, false); // (the choice's own sound, left on)
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
    case 3202: // (7: the options again, once the choice is gone)
        if (optionsCloseAt >= 0) break;
        audio.playSe(0xb4, false);
        optionsCloseAt = uiClock, optionsCloseFrom = uiClock - optionsAskStart, optionsCloseTicks = 0, optionsCloseTarget = 3;
        break;
    default: break;
    }
    if (id >= 3103 && id <= 3106) SDL_Log("options: language %d, quality %d", optionLanguage, optionQuality);
}
void Viewer::openManual() {
    manualEntries.clear();
    std::vector<uint8_t> d;
    if (archive.read("message\\instructions.dat", d)) {
        auto u32 = [&](size_t o) { return o + 4 <= d.size() ? uint32_t(d[o] | d[o + 1] << 8 | d[o + 2] << 16 | uint32_t(d[o + 3]) << 24) : 0u; };
        const size_t records = u32(0), strings = u32(4);
        // (a record: a title per language - the Vita's 7, the others' 8 -, its flag, its id)
        const size_t langs = messageLanguages(d, 8), stride = 4 * langs + 8;
        const size_t lang = size_t(std::clamp(optionLanguage, 0, 7)) < langs ? size_t(std::clamp(optionLanguage, 0, 7)) : 1;
        for (uint32_t k = 0, n = u32(8); k < n; ++k) {
            const size_t r = records + size_t(k) * stride;
            const int flag = int(u32(r + 4 * langs));
            if (flag != 0 && !game.flag(flag)) continue; // (Cr3UtilFlagCheck)
            ManualEntry e;
            for (size_t c = strings + u32(r + 4u * lang); c < d.size() && d[c]; ++c) e.title += char(d[c]);
            e.id = int(u32(r + 4 * langs + 4));
            manualEntries.push_back(e);
        }
    }
    manualStage = 1, manualStart = uiClock, manualCursor = 0;
    manualCloseAt = -1, manualInTicks = 0, manualReopen = 0;
    SDL_Log("manual: %zu entries", manualEntries.size());
}
void Viewer::closeManual() {
    if (manualStage == 1 && (manualCloseAt >= 0 || manualInTicks < 5)) return; // (closing, opening: +0x1c 30)
    if (manualStage == 1) { // (the list's frame back first; ScrollListMenu's Back: SE 0xb5)
        audio.playSe(0xb5, false);
        manualCloseAt = uiClock, manualCloseFrom = uiClock - manualStart, manualCloseTicks = 0, manualCloseTo = 2;
        return;
    }
    manualStage = 0, manualEvents.reset();
    optionsOpen = true, optionsStart = uiClock;
}
void Viewer::playManual(int k) {
    if (k < 0 || k >= int(manualEntries.size())) return;
    if (manualStage == 1 && manualCloseAt < 0) { // (SE 0xb4, the frame back, then manual_disp)
        if (manualInTicks < 5) return;
        audio.playSe(0xb4, false);
        manualCursor = k, manualPick = k;
        manualCloseAt = uiClock, manualCloseFrom = uiClock - manualStart, manualCloseTicks = 0, manualCloseTo = 1;
        return;
    }
    if (manualCloseAt >= 0) return;
    manualCursor = k;
    if (playTutorial("manual_disp", manualEntries[size_t(k)].id, true))
        SDL_Log("manual: tutorial %d (%s)", manualEntries[size_t(k)].id, manualEntries[size_t(k)].title.c_str());
}
// title.nut's `function` played over the battle (the manual's manual_disp, the options' own)
bool Viewer::playTutorial(const char* function, int id, bool toList) {
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
void Viewer::updateManual(float dt) {
    if (manualStage == 1 || manualReopen > 0) { // (the list's updates: its close, its open, the reading's end)
        manualTickClock = std::min(manualTickClock + dt, 0.25f);
        for (; manualTickClock >= cr2::EventRuntime::kTick; manualTickClock -= cr2::EventRuntime::kTick) {
            if (manualReopen > 0 && --manualReopen == 0) {
                manualStage = 1, manualStart = uiClock, manualInTicks = 0;
                continue;
            }
            if (manualStage != 1) continue;
            if (manualCloseAt < 0) {
                if (manualInTicks < 5) ++manualInTicks;
                continue;
            }
            ++manualCloseTicks;
            if (manualCloseTo == 1 && manualCloseTicks >= 4) { // (0x21: the window's manual_disp)
                manualCloseAt = -1;
                manualCursor = manualPick;
                if (playTutorial("manual_disp", manualEntries[size_t(manualPick)].id, true))
                    SDL_Log("manual: tutorial %d (%s)", manualEntries[size_t(manualPick)].id, manualEntries[size_t(manualPick)].title.c_str());
                else manualStart = uiClock, manualInTicks = 0; // (no reading: the list again)
                break;
            }
            if (manualCloseTo == 2 && manualCloseTicks >= 5) { // (0x23: the options made again)
                manualCloseAt = -1;
                manualStage = 0, manualEvents.reset();
                optionsOpen = true, optionsStart = uiClock;
                break;
            }
        }
        if (manualStage != 2) return;
    } else manualTickClock = 0;
    if (manualStage != 2 || !manualEvents) return;
    manualClock = std::min(manualClock + dt, 0.25f);
    for (; manualClock >= cr2::EventRuntime::kTick; manualClock -= cr2::EventRuntime::kTick)
        if (!manualEvents->tick()) SDL_Log("manual: %s", manualEvents->error().c_str());
    if (!manualEvents->running()) { // (InstructionsWindowMenu 3 -> 0: the list again)
        manualEvents.reset();
        if (manualToList) manualReopen = 1; // (the list the update after: 3 -> 0 -> 0x20; nothing drawn meanwhile)
        else manualStage = 0, optionsOpen = true, optionsStart = uiClock;
        SDL_Log("tutorial over");
    }
}
void Viewer::drawManualList(const Mat4& view, int w, int h) {
    tapRects.clear();
    std::vector<ListRow> rows;
    for (size_t k = 0; k < manualEntries.size(); ++k) {
        ListRow r;
        r.id = 5000 + int(k), r.name = manualEntries[k].title, r.number = -1;
        rows.push_back(r);
    }
    listRows = rows;
    const float t = manualCloseAt < 0 ? uiClock - manualStart : std::max(std::min(manualCloseFrom, 0.2f) - (uiClock - manualCloseAt), 0.0f);
    drawSkillList(rows, 5000 + manualCursor, t, view, w, h, -1, -1);
}
bool Viewer::optionsStepDone() { // (a close that leads elsewhere than Back: done here)
    const int target = optionsCloseTarget;
    if (!target) return false;
    optionsCloseAt = -1, optionsCloseTarget = 0;
    if (target == 1) optionsOpen = false, optionsAsk = true, optionsAskStart = uiClock;
    else if (target == 2) optionsOpen = false, openManual();
    else optionsAsk = false, optionsReset = 0, optionsOpen = true, optionsStart = uiClock;
    return true;
}
void Viewer::optionsBack() {
    // (the camp's; the title's - TitleOptionMenu state 2: its frame back, 0x13 at its end: the title's 0x1f,
    // CreateTitleMenu the update after; a battle's - BattleOptionMenu state 2: 3 at the frame's end, the fourth)
    if (optionsCloseAt < 0) optionsCloseAt = uiClock, optionsCloseFrom = uiClock - optionsStart, optionsCloseTicks = 0, optionsCloseTarget = 0;
}
void Viewer::drawOptionsAsk(const Mat4& view, int w, int h) { // (Battle / CampOptionMenu: "return to the title?", the title's window)
    tapRects.clear();
    const float t = optionsCloseAt >= 0 && optionsCloseTarget == 3 ? std::max(std::min(optionsCloseFrom, 0.2f) - (uiClock - optionsCloseAt), 0.0f)
                                                                  : uiClock - optionsAskStart; // (the No: the choice played back)
    drawAsk(announce(optionsReset == 1 ? 0x6e : optionsReset == 2 ? 0x6f : 0x6d), t, view, w, h, 3201, 3202, true);
}
void Viewer::saveImportAnswer(bool yes) {
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
void Viewer::drawOptions(const Mat4& view, const Mat4& proj, int w, int h) {
    tapRects.clear();
    const float t = uiClock - optionsStart;
    UiFig& title = uiFig("menu");
    title.fig.place = cr2::Mat34();
    poseUi(title, "menuopt_io", optionsPartTime(0.45f));
    drawFigure(title.fig, view.data());
    UiFig& seat = uiFig("seatopt_io");
    seat.fig.place = cr2::Mat34();
    poseUi(seat, "seatopt_io", optionsPartTime(0.2f));
    drawFigure(seat.fig, view.data());
    static const int kLabels[7] = {103, 104, 105, 102, 106, 108, 101};
    float x, y;
    for (int k = 0; k < 7; ++k) {
        char jn[16];
        std::snprintf(jn, sizeof jn, "typeM_L%02d", k + 1);
        const int label = k == 5 && titleOptions() ? 107 : kLabels[k]; // (the title's: Reset Data)
        if (uiJoint(seat, jn, x, y)) drawBorderedText(messageText("announcemenuname", label), 26, x, y, w, h, true); // (white on the parchment: an outline to read by)
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
    if (uiJoint(seat, "typeM_C01", x, y)) drawBorderedText(messageText("announcemenuname", 143 + optionQuality), 26, x, y, w, h); // (the quality: over its button)

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
            drawBorderedText("Graphics", 26, cx, cy, w, h, false, 1.0f, bw * 0.9f);
            tapRects.push_back({3110, sx + ox, sy + oy, ex + ox, ey + oy});
            // (Import Vita Saves: a row of the Graphics list at the title - the band below the volume
            // bars is Lock Orientation's)

            break;
        }
    UiFig& bk = uiFig("btlback");
    bk.fig.place = cr2::Mat34();
    poseUi(bk, "btlback_in", optionsPartTime(0.15f));
    backLook(bk);
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
std::string Viewer::savePath(const char* file) {
    char* dir = SDL_GetPrefPath("DuIslingr", "CR2-U");
    std::string p = dir ? std::string(dir) + file : std::string("cr2u-") + file;
    SDL_free(dir);
    return p;
}
void Viewer::scriptSave(int kind) {
    if (!scriptSaves) return;
    if (kind == 0) writeSave();
    else if (kind == 1) writeSave("cont.txt");
    else if (kind == 3) writeSystem(); // (SaveSystem: the system data at once)
    else if (!crstorage::replaceFile(savePath("cont.txt"), "")) SDL_Log("could not clear continue data"); // (DeleteCont: none to continue)
}
bool Viewer::writeSave(const char* file) {
    std::map<std::string, std::string> extra;
    char pos[96];
    std::snprintf(pos, sizeof pos, "%.6f %.6f %.6f %.6f", player[0], player[1], player[2], heading);
    extra["map"] = field.info().name, extra["place"] = pos;
    extra["saved"] = std::to_string(static_cast<long long>(std::time(nullptr))); // (the calendar's: cr3_savedata's time)
    extra["playtime"] = std::to_string(static_cast<long>(playSeconds * 20.0)); // (+0x10: frames of 0.05 s)
    extra["options"] = std::to_string(optionLanguage) + " " + std::to_string(optionQuality) + " " + std::to_string(optionVolume[0]) + " " +
                       std::to_string(optionVolume[1]) + " " + std::to_string(optionVolume[2]);
    const std::string text = game.save(extra), path = savePath(file);
    std::string error;
    const bool ok = crstorage::replaceFile(path, text, &error);
    if (!ok) SDL_Log("save failed: %s", error.c_str());
    SDL_Log("save: %s %s", path.c_str(), ok ? "written" : "not written");
    if (ok && std::strcmp(file, "save1.txt") == 0) writeSystem(); // (SaveConfirmationMenu: Cr3Util_Save_SystemData too)
    return ok;
}
void Viewer::restoreFieldPosition(const float* position, float yaw) {
    player[0] = position[0], player[1] = position[1], player[2] = position[2], heading = yaw;
    // The save stores the rendered feet position, 0.02 below the controller.
    // Restore both physics and interpolation before the next displayed frame.
    if (controller) {
        controller->setPosition({player[0], player[1] + 0.02f, player[2]});
        controller->setVelocity({0, 0, 0});
    }
    std::memcpy(playerPrev, player, sizeof player);
    std::memcpy(playerCur, player, sizeof player);
    stepClock = 0, substep = 0;
    exitsArmed = false;
    insideEvents.clear();
    const float centre[3] = {player[0], player[1] + 0.02f, player[2]};
    for (const cr2::Gimmick& g : field.gimmicks())
        if (g.kind == cr2::Gimmick::Event && !g.check && g.touches(centre, controller ? controller->radius : 0.02f))
            insideEvents.insert(g.name);
    checkBalloons();
    encountSet(player);
    fig.place = cr2::Mat34();
    fig.place.m[3] = player[0], fig.place.m[7] = player[1], fig.place.m[11] = player[2];
}
// --continue: the save read back, its map entered there
bool Viewer::continueSave(const std::string& binary, const std::string& dataPath, const char* only) {
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
        restoreFieldPosition(p, p[3]);
    }
    SDL_Log("continue: %s at %.3f %.3f %.3f", map.c_str(), player[0], player[1], player[2]);
    return true;
}
// --game-save: the game's own game.dat / cont.dat (script/gamesave.hpp) - and its system.dat
// beside it, the flags 0..991 - played from its map and place
bool Viewer::loadGameSave(const std::string& path, const std::string& binary, const std::string& dataPath) {
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
    restoreFieldPosition(place.position, place.heading);
    return true;
}
void Viewer::saveAnswer(bool yes) {
    if (saveStage != 1 || saveOpening()) return;
    audio.playSe(yes ? 0xbd : 0xb4, false);
    saveStage = yes ? 2 : 5, saveTicks = 0;
    saveCloseAt = uiClock, saveCloseFrom = uiClock - saveStart; // (the choice's window - and on No the menu - played back)
}
void Viewer::drawSave(const Mat4& view, const Mat4& proj, int w, int h) {
    if (uiClock - saveStart < cr2::EventRuntime::kTick) return; // (made the update after the press)
    const float open = uiClock - saveStart;
    auto partT = [&](float length) { // (played back from where it was as it closes)
        return saveCloseAt < 0 ? open : std::max(std::min(saveCloseFrom, length) - (uiClock - saveCloseAt), 0.0f);
    };
    const float t = saveStage >= 4 ? partT(0.45f) : open; // (the title: back on No or at its close)
    UiFig& title = uiFig("menu");
    title.fig.place = cr2::Mat34();
    poseUi(title, "menusave_io", t);
    drawFigure(title.fig, view.data());
    StatusShown rows[2];
    int count = 0;
    for (int k = 0; k < 2; ++k)
        if (game.party[k] >= 0 && game.party[k] < 9) rows[count++] = statusOf(game.party[k]);
    drawStatusWindows(rows, count, saveStage >= 4 ? partT(0.2f) : open, view, proj);
    if (saveStage == 1 || saveStage == 2 || saveStage == 5) drawAsk(announce(0x69), saveStage == 1 ? open : partT(0.2f), view, w, h, 7401, 7402, true);
    else if (saveStage == 4) {} // (the message ended with the close)
    else {
        UiFig& win = uiFig("syswin_io");
        win.fig.place = cr2::Mat34();
        poseUi(win, "syswin_io", t);
        drawFigure(win.fig, view.data());
        float x, y;
        if (uiJoint(win, "typeM_L01", x, y)) drawUiText(saveFailed ? "Could not save. Please try again." : announce(0x6a), 26, x, y, w, h, true);
    }
}
void Viewer::drawYesNo(cr2::EventRuntime& ev, int w, int h) {
    yesNoRects.clear();
    // (HideYesNoButton - EndBattleYNChoice: InterfaceCloseAnimeSet turns choice_io back from
    // where it is, the menu gone at its start; the buttons go with it)
    const bool closing = !ev.yesNoShown() && ev.yesNoClosing() >= -cr2::EventRuntime::kTick; // (-kTick: the hide's own update, still as it was)
    if (!ev.yesNoShown() && !closing) return;
    Mat4 camera = cr2::mat4Identity();
    camera[14] = -0.881f;
    const Mat4 view = cr2::mat4Multiply(uiProjection(w, h), camera);
    cr2::SceneLighting l;
    dev->setLighting(l);
    UiFig& ch = uiFig("choice_io");
    ch.fig.place = cr2::Mat34();
    float at = ev.yesNoAge();
    if (closing) {
        poseUi(ch, "choice_io", 0); // (loads it: its length)
        const float dur = ch.fig.animLoaded ? ch.fig.anim.duration() : 0.2f;
        at = std::min(ev.yesNoAge(), dur) - std::max(ev.yesNoClosing(), 0.0f);
        if (at < 0) return;
    }
    poseUi(ch, "choice_io", at);
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
        litLook(c, !closing && askLit(n == 12));
        drawFigure(c.fig, view.data());
        float sx, sy, ex, ey;
        if (!closing && uiJoint(c, "tapAreaS01", sx, sy) && uiJoint(c, "tapAreaE01", ex, ey)) yesNoRects.push_back({n == 11, sx, sy, ex, ey});
    }
}
void Viewer::answerYesNo(bool yes) {
    cr2::EventRuntime* ev = activeEvents();
    if (!ev || !ev->yesNoShown()) return;
    audio.playSe(0xb4, false);
    ev->answerYesNo(yes);
    SDL_Log("yes / no: %s", yes ? "yes" : "no");
}
bool Viewer::yesNoShown() {
    cr2::EventRuntime* ev = activeEvents();
    return ev && ev->yesNoShown();
}
int Viewer::askShown() { // (which question is up: 0 none)
    if (yesNoShown()) return 1;
    if (battleMode && gameOver.stage == 8) return 2;
    if (campOpen && (campUse == 2 || campUse == 3)) return 10 + campUse;
    if (campOpen && campTakeSopia) return 14;
    if (world.stage >= 3) return 20 + world.stage;
    if (pub.stage == 3) return 30;
    if (shop.stage == 4 || shop.stage == 5) return 40 + shop.stage;
    return 0;
}
void Viewer::noteAsk() { // (a question newly shown: Yes in focus - each update, and before a key)
    const int shown = askShown();
    if (shown != askWas) askFocus = 0;
    askWas = shown;
}
bool Viewer::askLit(bool no) const { return keysUsed && askFocus == (no ? 1 : 0); }
// the key as the question takes it: Up / Down taken here; Return with No in focus, and Backspace,
// turned to N (each screen's No); false - no question, or the key for the screen
bool Viewer::askKey(SDL_Keycode& k) {
    noteAsk();
    if (!askWas || optionsAsk || settingsOpen || saveImport.active()) return false;
    if (k == SDLK_UP || k == SDLK_DOWN) {
        askFocus ^= 1, keysUsed = true;
        audio.playSe(0xb3, false);
        SDL_Log("question: %s in focus", askFocus ? "no" : "yes");
        return true;
    }
    if (((k == SDLK_RETURN || k == SDLK_KP_ENTER) && askFocus == 1) || k == SDLK_BACKSPACE) k = SDLK_N;
    return false;
}
bool Viewer::yesNoClick(float fx, float fy) {
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
void Viewer::drawAsk(const std::string& message, float t, const Mat4& view, int w, int h, int yes, int no, bool titleStyle, bool notice) {
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
        if (!titleStyle && !notice && !buttonLook) litLook(c, askLit(n == 12)); // (BattleYNChoice's focus)
        else focusLook(c, n == 11 ? yes : no);
        drawFigure(c.fig, view.data());
        if (notice) drawUiText("OK", 26, x, y, w, h, false);
        float sx, sy, ex, ey;
        if (uiJoint(c, "tapAreaS01", sx, sy) && uiJoint(c, "tapAreaE01", ex, ey)) tapRects.push_back({n == 11 ? yes : no, sx, sy, ex, ey});
    }
}

// the event's UI, over the resolved scene: the fade, then the windows over it (their depths
// in front of the fade windows')
// the dungeon's name as it is entered (EvtIntroduction: MapTitleMenu - the language's mapName
// figure on its mapNameDNN animator, played once)
void Viewer::drawIntro(int w, int h) {
    if (!fieldMode || !script || script->introDungeon() <= 0) return;
    if (!script->introShown()) return; // (deleted at its end, whether or not the script asks GetVisible)
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
void Viewer::updateVistaSlash() {
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
void Viewer::vslashEnd(bool hit) {
    if (vslash.stage != 1) return;
    vslash.hit = hit, vslash.at = uiClock;
    if (hit) audio.playSe(0x15c, false), vslash.stage = 2;
    else vslash.stage = 3;
    SDL_Log("vista: the slash %s (%.1f s)", hit ? "lands" : "misses", uiClock);
}
// the mark's place (its figure's own, the interface's units)
bool Viewer::vslashPlace(float& x, float& y) {
    UiFig& f = uiFig(vslash.end ? "finale" : "kill");
    return uiJoint(f, "tapAreaS01", x, y) ? true : (x = 0, y = 0, true);
}
void Viewer::vslashPointer(float fx, float fy, int phase) { // (0 pressed, 2 let go)
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
void Viewer::drawVistaSlash() {
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
void Viewer::drawEncountWipe(float p, bool cover) {
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
void Viewer::drawEventUi() {
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
            fullFrameQuad(white, kScreen, rgba);
        }
    }
    if (fieldMode && !battleMode && encount.fadeAt >= 0 && uiClock >= encount.fadeAt) drawEncountWipe((uiClock - encount.fadeAt) / kWipeSeconds, true); // (an encounter's: Cr3FadeOut(3, 20))
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
        fullFrameQuad(white, kScreen, rgba);
    }
    const bool message = ev->messageShown() && ev->page() < int(ev->message().pages.size()) && ev->messageWindowUp(); // (none while its window's in animation plays)
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

} // namespace cr2_viewer
