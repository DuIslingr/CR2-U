#pragma once
// A field map (FldMap::ChangeArea / InitCamera / CalcScroll / Draw*; docs/FORMATS.md): painted
// layers - the background, an optional midground and foreground, 4096 x 4096-unit quads drawn
// with an orthographic camera in MVGL's virtual pixels - and the 3D figures (the characters, the
// map's own <map>f) drawn with the camera of the map's <map>c figure, magnified so the painting's
// height is the camera's field of view. Both scroll together to keep the player centred within
// the painting. <map>c also has the lights and the walls, <map>p the entrances and event boxes.

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "assets/geom.hpp"
#include "assets/maptable.hpp"
#include "assets/mvgl.hpp"
#include "assets/skeleton.hpp"
#include "field/collision.hpp"
#include "field/gimmicks.hpp"
#include "gfx/render_device.hpp"

namespace cr2 {

using Mat4 = std::array<float, 16>; // GL style: column-major, clip = M * v

Mat4 mat4Identity();
Mat4 mat4Multiply(const Mat4& a, const Mat4& b);
Mat4 mat4FromMat34(const Mat34& m);   // (a last row 0 0 0 1)
Mat4 mat4RigidInverse(const Mat4& m); // (rotation * scale + translation: the general 3x3 inverse)

// MVGL's virtual resolution (DisplayInfo: 1920 wide, the height the screen's aspect gives)
constexpr float kVirtualWidth = 1920.0f;

// What a frame of the field draws with
struct FieldFrame {
    Mat4 scene{};      // the 3D figures' view-projection: screen (scale, scroll) * projection * view
    Mat4 background{}; // the painted layers': screen * orthographic (virtual pixels)
    Mat4 midground{}, foreground{};
    float camera[3] = {0, 0, 0}; // the 3D camera's position (CameraPosition)
    float scroll[2] = {0, 0};    // FldMap +0x30 / +0x34: the screen offset, in its units
    float zNear = 1, zFar = 5000, fovY = 45; // the 3D camera's projection (fovY in degrees)
};

class FieldMap {
public:
    // The map's layers and figures from the archive; the table row says which there are
    bool load(const MvglArchive& archive, const MapInfo& info, std::string* error = nullptr);

    const MapInfo& info() const { return info_; }
    // the layers' image names ("" where the map has none)
    const std::string& backgroundImage() const { return bg_; }
    const std::string& midgroundImage() const { return mg_; }
    const std::string& foregroundImage() const { return fg_; }
    // the 3D figure's name ("" none)
    const std::string& figureName() const { return figure_; }
    const SceneLighting& lighting() const { return lighting_; }

    // A named point (a joint of <map>c or <map>p: "defaultPos", "d0101_p1", ...): false if none
    bool point(const std::string& name, float* pos) const;
    std::vector<std::string> pointNames() const;
    // The gimmicks <map>p's joints place (NPCs, objects, boxes, event boxes, ...)
    const std::vector<Gimmick>& gimmicks() const { return gimmicks_; }
    // Which NPCs, objects and boxes are solid (the scripts' Disp / SetCollision): the collision
    // rebuilt with those (all of them after load)
    void setSolidGimmicks(const std::function<bool(const Gimmick&)>& solid);

    // The walls and floors (<map>c.phys's solid bodies, and <map>p.phys's exits)
    const CollisionWorld& collision() const { return collision_; }
    // The exits (<map>p.phys's bodies): each is named for the pop point it leads to - the map is
    // its first five characters (FldMap::GetFieldMapNumFromPopPointName), the point a joint of that
    // map's <map>p figure (FldMain::ChangeAreaExecute)
    std::vector<std::string> exitNames() const;
    // The exit a sphere at `centre` touches ("" none): touching one changes the area
    std::string exitTouched(const Vec3& centre, float radius) const;
    // The camera's right and forward directions along the ground (unit, y 0): the player's
    // input turns into a direction with them (FldPlayer::Update)
    void groundAxes(Vec3& right, Vec3& forward) const;

    // The frame for a screen of w x h pixels with the player at `focus` (world space)
    FieldFrame frame(int w, int h, const float* focus) const;

    // The painted layers' quad: corners (x, y) and texture coordinates for drawQuad, 4096 units
    // square centred on the origin (`shiftX`: the foreground's offset)
    static void layerQuad(float* vertices24, float shiftX = 0.0f);

private:
    MapInfo info_;
    std::string bg_, mg_, fg_, figure_;
    Geom c_, p_;
    Skeleton cSkel_, pSkel_;
    std::vector<Mat34> cWorld_, pWorld_;
    std::vector<std::string> names_; // the joints' names (from the .name files), for pointNames
    SceneLighting lighting_;
    CollisionWorld collision_, static_; // (static_: the walls, floors and exits alone)
    struct Exit {
        std::string pop;
        CollisionWorld volume;
    };
    std::vector<Exit> exits_;
    std::vector<Gimmick> gimmicks_;
    Mat4 cameraWorld_ = mat4Identity();
    float fovY_ = 45, near_ = 1, far_ = 5000;
};

} // namespace cr2
