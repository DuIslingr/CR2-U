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

void EventScene::place(EventModel& m) {
    if (locators_.count(lower(m.name))) m.place = locator(m.name, m.sceneTime);
}

Mat34 EventScene::placeAt(const EventModel& m, float lead) const {
    if (!m.moving || lead <= 0 || !locators_.count(lower(m.name))) return m.place;
    return locator(m.name, std::min(m.sceneTime + lead, m.moveEnd));
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
    }
    cameraTime_ = std::min(cameraTime_ + dt, std::max(cameraEnd_, cameraTime_));
    lightTime_ = std::min(lightTime_ + dt, std::max(lightEnd_, lightTime_));
}

bool EventScene::camera(Mat34& world, float& fovY, float& zn, float& zf, float lead) const {
    if (!hasCamera_) return false;
    const GeomCamera& c = camera_.geom.cameras[0];
    const int j = camera_.skel.joint(c.hash);
    const std::vector<Mat34> w = camera_.world(std::min(cameraTime_ + lead, std::max(cameraEnd_, cameraTime_)));
    if (j < 0 || size_t(j) >= w.size()) return false;
    world = w[size_t(j)];
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
    return lightsFromFigure(light_.geom, posed, out);
}

} // namespace cr2
