#pragma once
// Edge Animation skeletons (`.skel`, 'ES02') and animations (`.anim`, 'EA04'): MVGL's port of Sony's
// Edge Animation (docs/FORMATS.md; tools/skel.py, tools/anim.py are the reference decoders).

#include <cstdint>
#include <string>
#include <vector>

namespace cr2 {

// a 3x4 row-major matrix, the translation in the 4th column (what MatrixPalette's rows hold)
struct Mat34 {
    float m[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    Mat34 operator*(const Mat34& b) const; // as 4x4s with a last row 0 0 0 1
};

struct JointTransform {
    float rotation[4] = {0, 0, 0, 1}; // quaternion x y z w
    float translation[3] = {0, 0, 0};
    float scale[3] = {1, 1, 1};
    Mat34 matrix() const;
};

// edgeAnimBlendJointsRelative's composition, at full weight: rotation base * delta (Hamilton
// product), translation base + delta, scale base * delta
JointTransform composeRelative(const JointTransform& base, const JointTransform& delta);

struct Skeleton {
    std::vector<JointTransform> basePose;
    std::vector<std::pair<uint16_t, uint16_t>> links; // (joint, parent | flags), parents first
    // world matrices for a pose (edgeAnimLocalJointsToWorldMatrices3x4): world = parent * local,
    // with the parent's own scale undone for the child's rotation unless the link's 0x8000 is set
    std::vector<Mat34> world(const std::vector<JointTransform>& pose, const Mat34& root = Mat34()) const;
};

bool parseSkeleton(const std::vector<uint8_t>& file, Skeleton& out, std::string* error = nullptr);

class Animation {
public:
    bool parse(const std::vector<uint8_t>& file, std::string* error = nullptr);
    float duration() const { return duration_; }
    float sampleRate() const { return rate_; }
    int frames() const { return frames_; }
    int joints() const { return joints_; }
    // the pose at `frame` (in frames, from 0) over `pose` (what joints without channels keep)
    void evaluate(float frame, std::vector<JointTransform>& pose) const;
    // the game's animations are relative to the figure's base pose (their constant channels hold
    // 0 / identity where a joint stays put): evaluated over identity, composed onto `base`
    std::vector<JointTransform> poseOver(const std::vector<JointTransform>& base, float frame) const;

private:
    struct Set { int start = 0, count = 0; size_t at = 0; };
    struct Layout { size_t initR, initT, initS, bits, keyR, keyT, keyS, next; };
    Layout layout(size_t k) const;
    std::vector<uint8_t> d_;
    float duration_ = 0, rate_ = 20;
    int joints_ = 0, frames_ = 0;
    int counts_[8] = {}; // constant R T S U, animated R T S U
    std::vector<uint16_t> tables_[8];
    size_t constR_ = 0, constT_ = 0, constS_ = 0;
    std::vector<Set> sets_;
};

} // namespace cr2
