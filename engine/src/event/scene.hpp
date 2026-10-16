#pragma once
// An event's scene (EvtScene, EvtModel, the vista camera / light; docs/SCRIPTS.md): three animated
// figures named after the event - `<event>` (its joints `ev_<model>` the models' locators),
// `<event>_camera` (an animated camera) and `<event>_light` (its lights) - and the models placed
// on the locators. The scripts drive their timelines in the scene's frames (20 a second: x 0.05
// is seconds): VistaEvent.SetMove(model, from, to) moves a model along its locator, CameraMove /
// LightMove the camera and the lights; EvtScene.ChangeAnimator(model, "e826", loop) plays the
// model's animation <figure>e826.

#include <map>
#include <string>
#include <vector>

#include "assets/geom.hpp"
#include "assets/mvgl.hpp"
#include "assets/skeleton.hpp"
#include "gfx/render_device.hpp"

namespace cr2 {

struct EventModel {
    std::string name;     // the locator's ("v206_00")
    std::string figure;   // the figure it shows ("v206": the name up to its "_NN")
    bool visible = false;
    // its animation (<figure><suffix>.anim; "" the figure's own) and where it is in it
    std::string animation;
    bool loop = true;
    float animTime = 0;
    // its place on its locator: the scene's time, and where a move ends (SetMove)
    float sceneTime = 0, moveEnd = 0;
    bool moving = false;
    Mat34 place;          // the locator's world matrix at sceneTime
    int face = -1;        // ChangeModelFace: the face texture slot (EvtScene.SetFaceTexture) on its
                          // j<NNN>face material; -1 its own
};

class EventScene {
public:
    // the event's three figures (the camera and the lights optional); false: no scene figure
    bool load(const MvglArchive& archive, const std::string& event, std::string* error = nullptr);
    bool loaded() const { return loaded_; }

    std::vector<EventModel>& models() { return models_; }
    EventModel* model(const std::string& name); // (case-insensitive, as the game's names vary)

    // the script's controls (times in the scene's frames)
    void setMove(const std::string& model, float from, float to);
    void cameraMove(float from, float to);
    void lightMove(float from, float to);

    // a frame's time passing: the moves, the camera and the lights advance
    void update(float dt);
    // `lead` seconds on from the last update (a frame drawn between the game's updates: less
    // than 0.05 s; the timelines are linear in time, so this is where they will be): a model's
    // place, the camera, the lights
    Mat34 placeAt(const EventModel& m, float lead) const;

    // the camera now: its world matrix (looking down -Z), vertical field of view (degrees), near, far
    // (VistaCamera::Init: near at least 0.05, far five times - at most 5000 - but for two events')
    bool camera(Mat34& world, float& fovY, float& zn, float& zf, float lead = 0) const;
    // the lights now (the light figure's, posed); false: none
    bool lights(SceneLighting& out, float lead = 0) const;

private:
    struct Figure {
        Geom geom;
        Skeleton skel;
        Animation anim;
        bool animated = false;
        std::vector<Mat34> world(float t) const;
    };
    void place(EventModel& m);
    Mat34 locator(const std::string& name, float sceneTime) const;
    bool loaded_ = false;
    std::string name_;
    Figure scene_, camera_, light_;
    bool hasCamera_ = false, hasLight_ = false;
    std::map<std::string, int> locators_; // ev_<name> -> joint
    std::vector<EventModel> models_;
    float cameraTime_ = 0, cameraEnd_ = 0, lightTime_ = 0, lightEnd_ = 0;
};

} // namespace cr2
