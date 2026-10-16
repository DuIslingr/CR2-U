#include "assets/skeleton.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cr2 {

namespace {

bool fail(std::string* error, const char* what) {
    if (error) *error = what;
    return false;
}

uint16_t rd16(const std::vector<uint8_t>& d, size_t at) { uint16_t v = 0; if (at + 2 <= d.size()) std::memcpy(&v, &d[at], 2); return v; }
int32_t rd32(const std::vector<uint8_t>& d, size_t at) { int32_t v = 0; if (at + 4 <= d.size()) std::memcpy(&v, &d[at], 4); return v; }
// a self-relative offset (0: none)
size_t relative(const std::vector<uint8_t>& d, size_t at) { const int32_t o = rd32(d, at); return o ? size_t(int64_t(at) + o) : 0; }

// 6 bytes: the smallest three of a unit quaternion, 15 bits each, the dropped one's index in the low 2
void quat48(const uint8_t* b, float* q) {
    const uint32_t u = uint32_t(b[5]) | uint32_t(b[2]) << 24 | uint32_t(b[3]) << 16;
    const uint32_t v[3] = {(uint32_t(b[0]) << 8 | b[1]) & 0x7fff, u >> 17, (uint32_t(b[4]) << 8 | (u & 0x1ffff)) >> 2};
    float c[3];
    for (int k = 0; k < 3; ++k) c[k] = (float(v[k]) - 16383.5f) / 23169.768f;
    const float w = std::sqrt(std::max(0.0f, 1.0f - c[0] * c[0] - c[1] * c[1] - c[2] * c[2]));
    const int drop = b[5] & 3;
    for (int k = 0, i = 0; k < 4; ++k) q[k] = k == drop ? w : c[i++];
}

} // namespace

Mat34 Mat34::operator*(const Mat34& b) const {
    Mat34 r;
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 4; ++col) {
            float s = 0;
            for (int k = 0; k < 3; ++k) s += m[row * 4 + k] * b.m[k * 4 + col];
            r.m[row * 4 + col] = s + (col == 3 ? m[row * 4 + 3] : 0.0f);
        }
    return r;
}

Mat34 JointTransform::matrix() const {
    const float x = rotation[0], y = rotation[1], z = rotation[2], w = rotation[3];
    const float r[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
                           {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
                           {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}};
    Mat34 o;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) o.m[row * 4 + col] = r[row][col] * scale[col];
        o.m[row * 4 + 3] = translation[row];
    }
    return o;
}

JointTransform composeRelative(const JointTransform& a, const JointTransform& b) {
    JointTransform r;
    const float ax = a.rotation[0], ay = a.rotation[1], az = a.rotation[2], aw = a.rotation[3];
    const float bx = b.rotation[0], by = b.rotation[1], bz = b.rotation[2], bw = b.rotation[3];
    r.rotation[0] = aw * bx + ax * bw + ay * bz - az * by;
    r.rotation[1] = aw * by - ax * bz + ay * bw + az * bx;
    r.rotation[2] = aw * bz + ax * by - ay * bx + az * bw;
    r.rotation[3] = aw * bw - ax * bx - ay * by - az * bz;
    for (int k = 0; k < 3; ++k) r.translation[k] = a.translation[k] + b.translation[k], r.scale[k] = a.scale[k] * b.scale[k];
    return r;
}

std::vector<JointTransform> Animation::poseOver(const std::vector<JointTransform>& base, float frame,
                                                std::vector<float>* user) const {
    std::vector<JointTransform> delta(base.size());
    evaluate(frame, delta, user);
    std::vector<JointTransform> out(base.size());
    for (size_t j = 0; j < base.size(); ++j) out[j] = composeRelative(base[j], delta[j]);
    return out;
}

std::vector<JointTransform> Animation::poseAbsolute(const std::vector<JointTransform>& base, float frame,
                                                    std::vector<float>* user) const {
    std::vector<JointTransform> out = base;
    evaluate(frame, out, user);
    return out;
}

std::vector<Mat34> Skeleton::world(const std::vector<JointTransform>& pose, const Mat34& root) const {
    std::vector<Mat34> w(pose.size(), root);
    for (const auto& [joint, link] : links) {
        if (joint >= pose.size()) continue;
        const unsigned parent = link & 0x7fff;
        Mat34 local = pose[joint].matrix();
        if (parent >= 0x4000 || parent >= pose.size()) {
            w[joint] = root * local;
            continue;
        }
        if (!(link & 0x8000)) { // scale compensation: the parent's own scale undone (rows of the rotation)
            for (int row = 0; row < 3; ++row) {
                const float s = pose[parent].scale[row];
                const float inv = s != 0.0f ? 1.0f / s : 0.0f; // (hidden, zero-scaled joints)
                for (int col = 0; col < 3; ++col) local.m[row * 4 + col] *= inv;
            }
        }
        w[joint] = w[parent] * local;
    }
    return w;
}

bool parseSkeleton(const std::vector<uint8_t>& d, Skeleton& out, std::string* error) {
    out = Skeleton();
    if (d.size() < 0x40 || std::memcmp(d.data(), "20SE", 4) != 0) return fail(error, "not an ES02 skeleton");
    const unsigned joints = rd16(d, 0x10), groups = rd16(d, 0x14);
    const size_t pose = relative(d, 0x18);
    if (!pose || pose + size_t(joints) * 0x30 > d.size() || 0x40 + size_t(groups) * 16 > d.size()) return fail(error, "skeleton outside the file");
    out.basePose.resize(joints);
    for (unsigned j = 0; j < joints; ++j) {
        JointTransform& t = out.basePose[j];
        std::memcpy(t.rotation, &d[pose + 0x30 * j], 16);
        std::memcpy(t.translation, &d[pose + 0x30 * j + 0x10], 12);
        std::memcpy(t.scale, &d[pose + 0x30 * j + 0x20], 12);
    }
    for (unsigned k = 0; k < groups * 4u; ++k) out.links.emplace_back(rd16(d, 0x40 + 4 * k), rd16(d, 0x40 + 4 * k + 2));
    const size_t names = relative(d, 0x20);
    if (names && names + size_t(joints) * 4 <= d.size())
        for (unsigned j = 0; j < joints; ++j) out.nameHashes.push_back(uint32_t(rd32(d, names + 4 * j)));
    const unsigned users = rd16(d, 0x12);
    const size_t userNames = relative(d, 0x24), userInfo = relative(d, 0x28);
    if (users && userNames && userInfo && userNames + size_t(users) * 4 <= d.size() && userInfo + size_t(users) * 4 <= d.size())
        for (unsigned k = 0; k < users; ++k)
            out.userChannels.push_back({uint32_t(rd32(d, userInfo + 4 * k)), uint16_t(rd32(d, userNames + 4 * k) >> 4),
                                        uint8_t(rd32(d, userNames + 4 * k) & 15)});
    return true;
}

uint32_t nameHash(const char* name) {
    uint32_t h = 0xffffffffu;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(name); p && *p; ++p) {
        uint32_t c = (h ^ *p) & 0xff;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? (c >> 1) ^ 0xedb88320u : c >> 1;
        h = c ^ (h >> 8);
    }
    return h;
}

std::vector<std::string> parseJointNames(const std::vector<uint8_t>& d) {
    std::vector<std::string> out;
    if (d.size() < 8) return out;
    const uint32_t joints = uint32_t(rd32(d, 0)), materials = uint32_t(rd32(d, 4));
    const size_t count = size_t(joints) + materials;
    if (8 + 4 * count > d.size()) return out;
    for (size_t k = 0; k < joints; ++k) {
        const size_t from = size_t(uint32_t(rd32(d, 8 + 4 * k)));
        const size_t to = k + 1 < count ? size_t(uint32_t(rd32(d, 8 + 4 * (k + 1)))) : d.size();
        if (from <= to && to <= d.size()) out.emplace_back(reinterpret_cast<const char*>(&d[from]), to - from);
    }
    return out;
}

int Skeleton::joint(uint32_t hash) const {
    for (size_t j = 0; j < nameHashes.size(); ++j)
        if (nameHashes[j] == hash) return int(j);
    return -1;
}

bool Animation::parse(const std::vector<uint8_t>& d, std::string* error) {
    *this = Animation();
    if (d.size() < 0x60 || std::memcmp(d.data(), "40AE", 4) != 0) return fail(error, "not an EA04 animation");
    if (rd32(d, 0x4c)) return fail(error, "packed animation (not used by the game)");
    d_ = d;
    std::memcpy(&duration_, &d[4], 4);
    std::memcpy(&rate_, &d[8], 4);
    joints_ = rd16(d, 0x0e);
    frames_ = rd16(d, 0x10);
    const unsigned sets = rd16(d, 0x12);
    size_t at = 0x60;
    for (int k = 0; k < 8; ++k) {
        counts_[k] = rd16(d, 0x16 + 2 * size_t(k));
        if (at + size_t(counts_[k]) * 2 > d.size()) return fail(error, "animation tables outside the file");
        for (int i = 0; i < counts_[k]; ++i) tables_[k].push_back(rd16(d, at + 2 * size_t(i)));
        at += size_t(k == 0 ? (counts_[k] + 7) & ~7 : (counts_[k] + 3) & ~3) * 2;
    }
    constR_ = relative(d, 0x3c);
    constT_ = relative(d, 0x40);
    constS_ = relative(d, 0x44);
    constU_ = relative(d, 0x48);
    const size_t dma = relative(d, 0x34), info = relative(d, 0x38);
    if (sets && (!dma || !info || dma + 8 * size_t(sets) > d.size() || info + 4 * size_t(sets) > d.size()))
        return fail(error, "animation frame sets outside the file");
    for (unsigned k = 0; k < sets; ++k) {
        Set s;
        s.at = size_t(rd32(d, dma + 8 * k + 4));
        s.start = rd16(d, info + 4 * k);
        s.count = rd16(d, info + 4 * k + 2);
        sets_.push_back(s);
    }
    for (size_t k = 0; k < sets_.size(); ++k) {
        const Layout L = layout(k);
        if (sets_[k].at + 16 > d.size() || L.next > d.size() + 16) return fail(error, "animation frame set outside the file");
    }
    return true;
}

Animation::Layout Animation::layout(size_t k) const {
    const Set& s = sets_[k];
    uint16_t sz[8];
    for (int i = 0; i < 8; ++i) sz[i] = rd16(d_, s.at + 2 * size_t(i));
    Layout L;
    L.initR = s.at + 16;
    L.initT = L.initR + sz[0];
    L.initS = L.initT + sz[1];
    L.initU = L.initS + sz[2];
    L.bits = L.initU + sz[3];
    const int animated = counts_[4] + counts_[5] + counts_[6] + counts_[7];
    L.keyR = L.bits + (size_t(animated) * size_t(s.count) + 7) / 8;
    L.keyT = L.keyR + sz[4];
    L.keyS = L.keyT + sz[5];
    L.keyU = (L.keyS + sz[6] + 3) & ~size_t(3);
    L.next = (L.keyU + sz[7] + 15) & ~size_t(15);
    return L;
}

void Animation::evaluate(float frame, std::vector<JointTransform>& pose, std::vector<float>* user) const {
    if (pose.size() < size_t(joints_)) pose.resize(size_t(joints_));
    auto vec3 = [&](size_t at, float* o) { if (at + 12 <= d_.size()) std::memcpy(o, &d_[at], 12); };
    auto f32 = [&](size_t at, float& o) { if (at + 4 <= d_.size()) std::memcpy(&o, &d_[at], 4); };
    auto userAt = [&](uint16_t c) -> float* {
        if (!user) return nullptr;
        if (user->size() <= c) user->resize(size_t(c) + 1, 0.0f);
        return &(*user)[c];
    };
    for (size_t i = 0; i < tables_[3].size(); ++i)
        if (float* o = userAt(tables_[3][i])) f32(constU_ + 4 * i, *o);
    for (size_t i = 0; i < tables_[0].size(); ++i)
        if (constR_ + 6 * i + 6 <= d_.size()) quat48(&d_[constR_ + 6 * i], pose[tables_[0][i]].rotation);
    for (size_t i = 0; i < tables_[1].size(); ++i) vec3(constT_ + 12 * i, pose[tables_[1][i]].translation);
    for (size_t i = 0; i < tables_[2].size(); ++i) vec3(constS_ + 12 * i, pose[tables_[2][i]].scale);
    if (sets_.empty() || !(counts_[4] || counts_[5] || counts_[6] || (user && counts_[7]))) return;
    frame = std::clamp(frame, 0.0f, float(std::max(frames_ - 1, 0)));
    size_t k = 0;
    while (k + 1 < sets_.size() && float(sets_[k + 1].start) <= frame) ++k;
    const Layout L = layout(k);
    const int n = sets_[k].count;
    const float local = frame - float(sets_[k].start);
    int i = int(local);
    float f = local - float(i);
    if (i > n) i = n, f = 1.0f;
    Layout N{};
    const bool hasNext = k + 1 < sets_.size();
    if (hasNext) N = layout(k + 1);

    // channel `ch`'s keys around the frame: the data offsets of the two values and the fraction t
    auto bracket = [&](int ch, size_t initAt, size_t keyAt, size_t size, size_t nextAt, int& keysOut, size_t& a, size_t& b) {
        int before = 0, total = 0, P = 0, Q = n + 1;
        for (int bit = 0; bit < n; ++bit) {
            const size_t at = size_t(ch) * size_t(n) + size_t(bit);
            if (L.bits + (at >> 3) >= d_.size() || !(d_[L.bits + (at >> 3)] & (0x80 >> (at & 7)))) continue;
            const int key = bit + 1;
            ++total;
            if (key <= i) ++before, P = key;
            else if (Q == n + 1) Q = key;
        }
        keysOut = total;
        a = before ? keyAt + size * size_t(before - 1) : initAt;
        b = before < total ? keyAt + size * size_t(before) : (hasNext ? nextAt : a);
        return (float(i - P) + f) / float(Q - P);
    };
    int ch = 0;
    size_t kR = L.keyR, kT = L.keyT, kS = L.keyS;
    for (size_t c = 0; c < tables_[4].size(); ++c, ++ch) {
        int keys;
        size_t a, b;
        const float t = bracket(ch, L.initR + 6 * c, kR, 6, N.initR + 6 * c, keys, a, b);
        float q0[4], q1[4];
        if (a + 6 > d_.size() || b + 6 > d_.size()) continue;
        quat48(&d_[a], q0);
        quat48(&d_[b], q1);
        float dot = q0[0] * q1[0] + q0[1] * q1[1] + q0[2] * q1[2] + q0[3] * q1[3];
        if (dot < 0) {
            for (float& x : q1) x = -x;
            dot = -dot;
        }
        float w0 = 1 - t, w1 = t;
        if (dot < 0.999f) { // slerp
            const float th = std::acos(std::min(1.0f, dot)), s = std::sin(th);
            w0 = std::sin((1 - t) * th) / s;
            w1 = std::sin(t * th) / s;
        }
        float* out = pose[tables_[4][c]].rotation;
        for (int e = 0; e < 4; ++e) out[e] = w0 * q0[e] + w1 * q1[e];
        kR += 6 * size_t(keys);
    }
    auto lerpChannels = [&](int table, size_t init, size_t& key, size_t nextInit, bool scale) {
        for (size_t c = 0; c < tables_[table].size(); ++c, ++ch) {
            int keys;
            size_t a, b;
            const float t = bracket(ch, init + 12 * c, key, 12, nextInit + 12 * c, keys, a, b);
            float v0[3], v1[3];
            vec3(a, v0);
            vec3(b, v1);
            float* out = scale ? pose[tables_[table][c]].scale : pose[tables_[table][c]].translation;
            for (int e = 0; e < 3; ++e) out[e] = v0[e] + t * (v1[e] - v0[e]);
            key += 12 * size_t(keys);
        }
    };
    lerpChannels(5, L.initT, kT, N.initT, false);
    lerpChannels(6, L.initS, kS, N.initS, true);
    // user channels (floats; MVGL's: material parameters - Skeleton::userChannels)
    size_t kU = L.keyU;
    for (size_t c = 0; c < tables_[7].size(); ++c, ++ch) {
        int keys;
        size_t a, b;
        const float t = bracket(ch, L.initU + 4 * c, kU, 4, N.initU + 4 * c, keys, a, b);
        float v0 = 0, v1 = 0;
        f32(a, v0);
        f32(b, v1);
        if (float* o = userAt(tables_[7][c])) *o = v0 + t * (v1 - v0);
        kU += 4 * size_t(keys);
    }
}

} // namespace cr2
