#include "event/scene.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "gfx/material.hpp"

namespace cr2 {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool readFigure(const MvglArchive& a, const std::string& name, Geom& g, Skeleton& s, Animation& anim, bool& animated) {
    std::vector<uint8_t> d;
    if (!a.read(name + ".geom", d) || !parseGeom(d, g) || !a.read(name + ".skel", d) || !parseSkeleton(d, s)) return false;
    animated = a.read(name + ".anim", d) && anim.parse(d) && anim.joints() == int(s.basePose.size());
    return true;
}

} // namespace

std::vector<Mat34> EventScene::Figure::world(float t) const {
    if (!animated) return skel.world(skel.basePose);
    const float frames = float(std::max(anim.frames() - 1, 1));
    const float fr = std::clamp(t * anim.sampleRate(), 0.0f, frames);
    return skel.world(anim.poseAbsolute(skel.basePose, fr)); // (slot 0 alone: absolute)
}

bool EventScene::load(const MvglArchive& a, const std::string& event, std::string* error) {
    *this = EventScene();
    const std::string base = lower(event); // (the archive's names are lower case)
    if (!readFigure(a, base, scene_.geom, scene_.skel, scene_.anim, scene_.animated)) {
        if (error) *error = base + ": no event scene";
        return false;
    }
    hasCamera_ = readFigure(a, base + "_camera", camera_.geom, camera_.skel, camera_.anim, camera_.animated) &&
                 !camera_.geom.cameras.empty();
    hasLight_ = readFigure(a, base + "_light", light_.geom, light_.skel, light_.anim, light_.animated);
    // the models: the joints named ev_<name> but for the camera's and the p000 reference
    // (EvtScene::Init), each showing the figure its name starts with
    std::vector<uint8_t> d;
    if (a.read(base + ".name", d)) {
        const std::vector<std::string> names = parseJointNames(d);
        for (size_t j = 0; j < names.size(); ++j) {
            const std::string& n = names[j];
            if (n == "ev_p000") p000_ = int(j);
            if (n.compare(0, 3, "ev_") != 0 || n.find("Cam") != std::string::npos || n.find("p000") != std::string::npos) continue;
            EventModel m;
            m.name = n.substr(3);
            const size_t us = m.name.rfind('_');
            m.figure = us != std::string::npos && us + 3 == m.name.size() && std::isdigit(static_cast<unsigned char>(m.name[us + 1]))
                           ? m.name.substr(0, us)
                           : m.name;
            locators_[lower(m.name)] = int(j);
            models_.push_back(m);
        }
    }
    for (EventModel& m : models_) place(m);
    name_ = base;
    loaded_ = true;
    return true;
}

EventModel* EventScene::model(const std::string& name) {
    const std::string n = lower(name);
    for (EventModel& m : models_)
        if (lower(m.name) == n) return &m;
    return nullptr;
}

Mat34 EventScene::locator(const std::string& name, float sceneTime) const {
    const auto it = locators_.find(lower(name));
    if (it == locators_.end()) return Mat34();
    const std::vector<Mat34> w = scene_.world(sceneTime);
    return size_t(it->second) < w.size() ? w[size_t(it->second)] : Mat34();
}

bool EventScene::p000(Mat34& world) const {
    if (!loaded_ || p000_ < 0) return false;
    const std::vector<Mat34> w = scene_.world(0);
    if (size_t(p000_) >= w.size()) return false;
    world = w[size_t(p000_)];
    return true;
}

namespace {
// a model held at a point or turned (SetPosDirect / ChangeDirection) over its locator's place
void hold(const EventModel& m, Mat34& p, float lead) {
    if (m.pinned) p.m[3] = m.pin[0], p.m[7] = m.pin[1], p.m[11] = m.pin[2];
    if (!m.turned) return;
    const float a = m.turnSeconds > 0 ? std::clamp((m.turnTime + lead) / m.turnSeconds, 0.0f, 1.0f) : 1.0f;
    float d = std::remainder(m.turnTo - m.turnFrom, 6.2831853f); // (the shorter way round)
    const float yaw = m.turnFrom + d * a;
    const float sc = std::hypot(p.m[0], p.m[4], p.m[8]), c = std::cos(yaw), s = std::sin(yaw);
    p.m[0] = c * sc, p.m[1] = 0, p.m[2] = s * sc;
    p.m[4] = 0, p.m[5] = sc, p.m[6] = 0;
    p.m[8] = -s * sc, p.m[9] = 0, p.m[10] = c * sc;
}
} // namespace

void EventScene::place(EventModel& m) {
    if (locators_.count(lower(m.name))) m.place = locator(m.name, m.sceneTime);
    hold(m, m.place, 0);
}

Mat34 EventScene::placeAt(const EventModel& m, float lead) const {
    if (!m.moving || lead <= 0 || !locators_.count(lower(m.name))) return m.place;
    Mat34 p = locator(m.name, std::min(m.sceneTime + lead, m.moveEnd));
    hold(m, p, lead);
    return p;
}

void EventScene::turnTo(const std::string& name, const std::string& other, int frames) {
    EventModel* m = model(name);
    const EventModel* o = model(other);
    if (!m || !o) return;
    m->turnFrom = std::atan2(m->place.m[2], m->place.m[10]);
    m->turnTo = std::atan2(o->place.m[3] - m->place.m[3], o->place.m[11] - m->place.m[11]);
    m->turned = true, m->turnSeconds = float(std::max(frames, 0)) * 0.05f, m->turnTime = 0;
}

void EventScene::pin(const std::string& name, float x, float y, float z) {
    if (EventModel* m = model(name)) {
        m->pinned = true, m->pin[0] = x, m->pin[1] = y, m->pin[2] = z;
        place(*m);
    }
}

void EventScene::pinTo(const std::string& name, const std::string& other) {
    if (const EventModel* o = model(other)) pin(name, o->place.m[3], o->place.m[7], o->place.m[11]);
}

void EventScene::setMove(const std::string& name, float from, float to) {
    if (EventModel* m = model(name)) {
        m->moving = true;
        m->sceneTime = from * 0.05f;
        m->moveEnd = to * 0.05f;
        place(*m);
    }
}

void EventScene::cameraMove(float from, float to) {
    cameraTime_ = from * 0.05f;
    cameraEnd_ = (to + 1) * 0.05f; // (EvtUtilVistaCameraMove: to + 1)
}

void EventScene::lightMove(float from, float to) {
    lightTime_ = from * 0.05f;
    lightEnd_ = (to + 1) * 0.05f;
}

void EventScene::update(float dt) {
    for (EventModel& m : models_) {
        if (m.moving) { // (EvtModel::Update: along its locator until the move's end)
            m.sceneTime += dt;
            if (m.sceneTime >= m.moveEnd) m.sceneTime = m.moveEnd, m.moving = false;
            place(m);
        }
        m.animTime += dt;
        m.alphaTime += dt;
        if (m.turned) {
            m.turnTime += dt;
            place(m);
        }
    }
    cameraTime_ = std::min(cameraTime_ + dt, std::max(cameraEnd_, cameraTime_));
    lightTime_ = std::min(lightTime_ + dt, std::max(lightEnd_, lightTime_));
    for (LightColour* l : {&directional_, &hemisphere_}) { // (VistaMain::Update: a frame more of each)
        if (!l->anim) continue;
        if (++l->frame < l->frames) {
            const float k = float(l->frame) / float(l->frames);
            for (int c = 0; c < 4; ++c) l->now[c] = l->from[c] + (l->to[c] - l->from[c]) * k;
            for (int c = 0; c < 3; ++c) l->ground[c] = l->groundFrom[c] + (l->groundTo[c] - l->groundFrom[c]) * k;
        } else {
            l->anim = false, l->frame = l->frames;
            for (int c = 0; c < 4; ++c) l->now[c] = l->to[c];
            for (int c = 0; c < 3; ++c) l->ground[c] = l->groundTo[c];
        }
    }
    if (quake_.on) { // (VistaMain::Update: the quake's time; VistaMain::Pose: the camera moved)
        if ((quake_.time += dt) >= quake_.seconds) {
            quake_.time = quake_.seconds;
            if (quake_.to <= 0) return quakeStop();
        }
        Mat34 w;
        float fov, zn, zf;
        const Quake keep = quake_;
        quake_.on = false; // (the camera's own place: without the quake's offset)
        const bool have = camera(w, fov, zn, zf);
        quake_ = keep;
        if (!have) return;
        float a = (quake_.from + (quake_.to - quake_.from) * (quake_.seconds > 0 ? quake_.time / quake_.seconds : 1)) * quake_.width;
        if (quake_.parity == 0) a = -a;
        for (int k = 0; k < 3; ++k) quake_.offset[k] += w.m[k * 4 + 1] * a; // (its up axis)
        quake_.parity = (quake_.parity + 1) % 2;
    }
}

void EventScene::quake(float width, float fromPercent, float toPercent, int frames) {
    quake_.on = true, quake_.width = width, quake_.from = fromPercent * 0.01f, quake_.to = toPercent * 0.01f;
    quake_.seconds = float(frames) * 0.05f, quake_.time = 0, quake_.parity = 0;
}

void EventScene::quakeStop() {
    quake_ = Quake();
}

bool EventScene::camera(Mat34& world, float& fovY, float& zn, float& zf, float lead) const {
    if (!hasCamera_) return false;
    const GeomCamera& c = camera_.geom.cameras[0];
    const int j = camera_.skel.joint(c.hash);
    const std::vector<Mat34> w = camera_.world(std::min(cameraTime_ + lead, std::max(cameraEnd_, cameraTime_)));
    if (j < 0 || size_t(j) >= w.size()) return false;
    world = w[size_t(j)];
    if (quake_.on) world.m[3] += quake_.offset[0], world.m[7] += quake_.offset[1], world.m[11] += quake_.offset[2];
    fovY = c.fovY;
    zn = c.near, zf = c.far;
    if (name_ != "d11_06_0102_evt" && name_ != "d12_03_0106a_evt") zn = std::max(zn, 0.05f), zf = std::min(zf * 5, 5000.0f);
    zn = std::max(zn, 0.001f), zf = std::max(zf, zn * 2);
    return true;
}

bool EventScene::lights(SceneLighting& out, float lead) const {
    if (!hasLight_) return false;
    // (the light figure posed at its time: the lights' joints carry their directions)
    Skeleton posed = light_.skel;
    if (light_.animated) {
        const float frames = float(std::max(light_.anim.frames() - 1, 1));
        const float t = std::min(lightTime_ + lead, std::max(lightEnd_, lightTime_));
        posed.basePose = light_.anim.poseAbsolute(light_.skel.basePose, std::clamp(t * light_.anim.sampleRate(), 0.0f, frames));
    }
    if (!lightsFromFigure(light_.geom, posed, out)) return false;
    // (VistaMain::Draw: the script's colours over the figure's while animated or held)
    if (directional_.anim || directional_.direct)
        for (int k = 0; k < 3; ++k) out.lightColor[k] = directional_.now[3] * directional_.now[k];
    if (hemisphere_.anim || hemisphere_.direct)
        for (int k = 0; k < 3; ++k) out.ambient[k] = hemisphere_.now[k], out.ground[k] = hemisphere_.ground[k];
    return true;
}

} // namespace cr2
