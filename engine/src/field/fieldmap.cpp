#include "field/fieldmap.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "gfx/material.hpp"

namespace cr2 {

Mat4 mat4Identity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }

Mat4 mat4Multiply(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[size_t(k * 4 + row)] * b[size_t(c * 4 + k)];
            r[size_t(c * 4 + row)] = s;
        }
    return r;
}

Mat4 mat4FromMat34(const Mat34& m) {
    Mat4 r = mat4Identity();
    for (int row = 0; row < 3; ++row)
        for (int c = 0; c < 4; ++c) r[size_t(c * 4 + row)] = m.m[row * 4 + c];
    return r;
}

Mat4 mat4RigidInverse(const Mat4& m) {
    // the 3x3 part's inverse (cofactors), then the translation through it
    const float a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
    const float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    const float det = a * A + b * B + c * C;
    const float k = det != 0 ? 1.0f / det : 0.0f;
    const float inv[9] = {A * k, -(b * i - c * h) * k, (b * f - c * e) * k,
                          B * k, (a * i - c * g) * k, -(a * f - c * d) * k,
                          C * k, -(a * h - b * g) * k, (a * e - b * d) * k};
    Mat4 r = mat4Identity();
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) r[size_t(col * 4 + row)] = inv[row * 3 + col];
        r[size_t(12 + row)] = -(inv[row * 3] * m[12] + inv[row * 3 + 1] * m[13] + inv[row * 3 + 2] * m[14]);
    }
    return r;
}

namespace {

bool fail(std::string* error, const std::string& what) {
    if (error) *error = what;
    return false;
}

bool readFigure(const MvglArchive& a, const std::string& name, Geom& g, Skeleton& s, std::vector<Mat34>& world,
                std::vector<std::string>& names) {
    std::vector<uint8_t> d;
    if (!a.read(name + ".geom", d) || !parseGeom(d, g) || !a.read(name + ".skel", d) || !parseSkeleton(d, s)) return false;
    world = s.world(s.basePose);
    // <figure>.name: u32 joints, u32 materials, then an offset per name (each runs to the next)
    if (a.read(name + ".name", d) && d.size() >= 8) {
        uint32_t n[2];
        std::memcpy(n, d.data(), 8);
        const size_t count = size_t(n[0]) + n[1];
        if (8 + 4 * count <= d.size())
            for (size_t k = 0; k < n[0]; ++k) {
                uint32_t from, to = uint32_t(d.size());
                std::memcpy(&from, &d[8 + 4 * k], 4);
                if (k + 1 < count) std::memcpy(&to, &d[8 + 4 * (k + 1)], 4);
                if (from <= to && to <= d.size()) names.emplace_back(reinterpret_cast<const char*>(&d[from]), to - from);
            }
    }
    return true;
}

// MVGL::Draw::Camera::GetProjectionMatrix, the perspective one: GL's, the aspect the virtual
// screen's (the camera's own is -1: none)
Mat4 perspective(float fovYDegrees, float aspect, float zn, float zf) {
    const float f = 1.0f / std::tan(fovYDegrees * 0.01745328f * 0.5f);
    Mat4 r{};
    r[0] = f / aspect;
    r[5] = f;
    r[10] = (zn + zf) / (zn - zf);
    r[11] = -1;
    r[14] = 2 * zn * zf / (zn - zf);
    return r;
}

// GetScreenMatrix without its roll: scale, then an offset in NDC (the camera's +0x268 / +0x26c
// pixels over the screen's size)
Mat4 screen(float scale, float offsetX, float offsetY) {
    Mat4 r = mat4Identity();
    r[0] = r[5] = scale;
    r[12] = offsetX;
    r[13] = offsetY;
    return r;
}

} // namespace

bool FieldMap::load(const MvglArchive& a, const MapInfo& info, std::string* error) {
    *this = FieldMap();
    info_ = info;
    std::vector<std::string> pNames;
    if (!readFigure(a, info.name + "c", c_, cSkel_, cWorld_, names_)) return fail(error, info.name + "c: not in the archive");
    if (readFigure(a, info.name + "p", p_, pSkel_, pWorld_, pNames)) names_.insert(names_.end(), pNames.begin(), pNames.end());
    auto image = [&](const std::string& n) { return a.contains("images\\" + n + ".img") ? n : std::string(); };
    if (!info.noBackground) bg_ = image(info.layer("bg"));
    if (info.midground) mg_ = image(info.layer("mg"));
    if (info.foreground) fg_ = image(info.layer("fg"));
    if ((info.figure || info.figure2) && a.contains(info.name + "f.geom")) figure_ = info.name + "f";
    lightsFromFigure(c_, cSkel_, lighting_);
    std::vector<uint8_t> phys;
    PhysScene scene;
    std::string err;
    if (a.read(info.name + "c.phys", phys) && parsePhys(phys, scene, &err)) collision_.addScene(scene);
    else if (!err.empty()) return fail(error, info.name + "c.phys: " + err);
    // the camera (InitCamera): near halved (at least 0.1), far five times (at most 5000)
    if (c_.cameras.empty()) return fail(error, info.name + "c: no camera");
    const GeomCamera& cam = c_.cameras[0];
    const int j = cSkel_.joint(cam.hash);
    if (j >= 0 && size_t(j) < cWorld_.size()) cameraWorld_ = mat4FromMat34(cWorld_[size_t(j)]);
    fovY_ = cam.fovY;
    near_ = std::max(cam.near * 0.5f, 0.1f);
    far_ = std::min(cam.far * 5.0f, 5000.0f);
    if (info.name == "d0302") near_ = 10.0f, far_ = 1000.0f; // (InitCamera's one exception)
    return true;
}

bool FieldMap::point(const std::string& name, float* pos) const {
    const uint32_t h = nameHash(name.c_str());
    for (int which = 0; which < 2; ++which) {
        const Skeleton& s = which ? pSkel_ : cSkel_;
        const std::vector<Mat34>& w = which ? pWorld_ : cWorld_;
        const int j = s.joint(h);
        if (j >= 0 && size_t(j) < w.size()) {
            pos[0] = w[size_t(j)].m[3], pos[1] = w[size_t(j)].m[7], pos[2] = w[size_t(j)].m[11];
            return true;
        }
    }
    return false;
}

std::vector<std::string> FieldMap::pointNames() const { return names_; }

void FieldMap::groundAxes(Vec3& right, Vec3& forward) const {
    auto flat = [](float x, float z) {
        const float l = std::sqrt(x * x + z * z);
        return l > 1e-6f ? Vec3{x / l, 0, z / l} : Vec3{0, 0, 0};
    };
    right = flat(cameraWorld_[0], cameraWorld_[2]);       // the camera's +X
    forward = flat(-cameraWorld_[8], -cameraWorld_[10]);  // its view: -Z
}

FieldFrame FieldMap::frame(int w, int h, const float* focus) const {
    FieldFrame out;
    const float W = float(std::max(w, 1)), H = float(std::max(h, 1));
    const float vh = kVirtualWidth * H / W; // the virtual screen's height
    const float s = info_.sceneScale();
    // the 3D camera: magnified 2.13 x the aspect (4096 units: the painting over the virtual width)
    const float scale3d = 2.13f * W / H * s;
    const Mat4 view = mat4RigidInverse(cameraWorld_);
    const Mat4 proj = perspective(fovY_, kVirtualWidth / vh, near_, far_);
    const Mat4 vp = mat4Multiply(proj, view);
    // CalcNewScreenPos: the offset that puts the focus at the centre
    const float fx = focus[0], fy = focus[1], fz = focus[2];
    const float cx = vp[0] * fx + vp[4] * fy + vp[8] * fz + vp[12];
    const float cy = vp[1] * fx + vp[5] * fy + vp[9] * fz + vp[13];
    const float cw = vp[3] * fx + vp[7] * fy + vp[11] * fz + vp[15];
    float sx = cw != 0 ? -(cx / cw) * W * scale3d : 0.0f;
    float sy = cw != 0 ? -(cy / cw) * H * scale3d : 0.0f;
    // SetScrollStop / CalcScrollStop: within the painting (1024 or 2048 units of a 960-wide screen)
    float bx = sx, by = sy;
    if (!info_.sharedBackground) {
        const float unit = W / 960.0f;
        const float pw = unit * ((info_.variant & 2) ? 2048.0f : 1024.0f), ph = unit * ((info_.variant & 1) ? 2048.0f : 1024.0f);
        const float lx = std::max(pw - W, 0.0f), ly = std::max(ph - H, 0.0f);
        sx = std::clamp(sx, -lx, lx);
        sy = std::clamp(sy, -ly, ly);
        bx = sx, by = sy;
    } else {
        bx = sx * 0.4f, by = sy * 0.4f;
    }
    out.scroll[0] = sx, out.scroll[1] = sy;
    // (the game keeps the offsets as ints)
    const float ox = std::trunc(sx), oy = std::trunc(sy);
    out.scene = info_.noBackground ? vp : mat4Multiply(screen(scale3d, ox / W, oy / H), vp);
    // the painted layers: orthographic in virtual pixels (+-960 x +-vh/2, depth +-5000)
    Mat4 ortho = mat4Identity();
    ortho[0] = 1.0f / (kVirtualWidth * 0.5f);
    ortho[5] = 1.0f / (vh * 0.5f);
    ortho[10] = -1.0f / 5000.0f;
    out.background = mat4Multiply(screen(s, std::trunc(bx) / W, std::trunc(by) / H), ortho);
    out.midground = mat4Multiply(screen(s, std::trunc(bx * 0.5f) / W, std::trunc(by * 0.5f) / H), ortho);
    out.foreground = info_.foregroundShift == 1 ? mat4Multiply(screen(s, std::trunc(bx * 2) / W, std::trunc(by * 0.5f) / H), ortho)
                                                : out.background;
    out.camera[0] = cameraWorld_[12], out.camera[1] = cameraWorld_[13], out.camera[2] = cameraWorld_[14];
    return out;
}

void FieldMap::layerQuad(float* v, float shiftX) {
    // two triangles of (x, y, u, v); the textures' rows are bottom-up, so v = 0 is the bottom
    const float x0 = -2048 + shiftX, x1 = 2048 + shiftX, y0 = -2048, y1 = 2048;
    const float q[24] = {x0, y0, 0, 0, x1, y0, 1, 0, x1, y1, 1, 1, x0, y0, 0, 0, x1, y1, 1, 1, x0, y1, 0, 1};
    std::memcpy(v, q, sizeof q);
}

} // namespace cr2
