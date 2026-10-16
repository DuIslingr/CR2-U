#include "field/character.hpp"

#include <algorithm>
#include <cmath>

namespace cr2 {

namespace {

constexpr float kEpsilon = 1.1920929e-07f; // (SIMD_EPSILON)
const Vec3 kUp{0, 1, 0};

Vec3 sub(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3 add(const Vec3& a, const Vec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec3 mul(const Vec3& a, float s) { return {a[0] * s, a[1] * s, a[2] * s}; }
float dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 normalised(const Vec3& v) {
    const float l = std::sqrt(dot(v, v));
    return l > kEpsilon ? mul(v, 1.0f / l) : Vec3{0, 0, 0};
}
Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return add(a, mul(sub(b, a), t)); }

} // namespace

void CharacterController::setPosition(const Vec3& p) {
    current_ = target_ = p;
    verticalVelocity_ = verticalOffset_ = 0;
}

void CharacterController::setVelocity(const Vec3& v) {
    walkDirection_ = {v[0], 0, v[2]};
    normalizedDirection_ = normalised(walkDirection_);
    velocityTimeInterval_ = 0.05f;
}

// Bullet pushes out by a fifth of each contact's depth a pass, up to five passes
bool CharacterController::recoverFromPenetration() {
    bool penetration = false;
    float maxDepth = 0;
    for (const CollisionWorld::Contact& c : world_.overlaps(current_, radius)) {
        if (c.depth > maxDepth) maxDepth = c.depth, touchingNormal_ = c.normal;
        current_ = add(current_, mul(c.normal, c.depth * 0.2f));
        penetration = true;
    }
    return penetration;
}

void CharacterController::stepUp() {
    // (only hits from above count: ceilings - and, as in 2.81, they don't move it)
    target_ = add(current_, mul(kUp, stepHeight + (verticalOffset_ > 0 ? verticalOffset_ : 0)));
    CollisionWorld::Hit hit;
    if (world_.sweep(current_, target_, radius, mul(kUp, -1), 0.7071f, hit)) {
        if (dot(hit.normal, kUp) > 0) {
            currentStepOffset_ = stepHeight * hit.fraction;
            current_ = lerp(current_, target_, hit.fraction);
        }
        verticalVelocity_ = verticalOffset_ = 0;
    } else {
        currentStepOffset_ = stepHeight;
        current_ = target_;
    }
}

// slide: the reflected direction's part along the surface, from where it is (not the hit point)
void CharacterController::updateTargetPositionBasedOnCollision(const Vec3& n) {
    Vec3 dir = sub(target_, current_);
    const float len = std::sqrt(dot(dir, dir));
    if (len <= kEpsilon) return;
    dir = mul(dir, 1.0f / len);
    const Vec3 reflect = normalised(sub(dir, mul(n, 2.0f * dot(dir, n))));
    const Vec3 perpendicular = sub(reflect, mul(n, dot(reflect, n)));
    target_ = add(current_, mul(perpendicular, len));
}

void CharacterController::stepForwardAndStrafe(const Vec3& walkMove) {
    target_ = add(current_, walkMove);
    if (touchingContact_ && dot(normalizedDirection_, touchingNormal_) > 0) updateTargetPositionBasedOnCollision(touchingNormal_);
    float fraction = 1.0f;
    for (int iter = 10; fraction > 0.01f && iter-- > 0;) {
        CollisionWorld::Hit hit;
        const Vec3 sweepDirNegative = sub(current_, target_); // (hits facing the move only)
        if (world_.sweep(current_, target_, radius, sweepDirNegative, 0.0f, hit)) {
            fraction -= hit.fraction;
            updateTargetPositionBasedOnCollision(hit.normal);
            const Vec3 d = sub(target_, current_);
            if (dot(d, d) <= kEpsilon || dot(normalised(d), normalizedDirection_) <= 0) break;
        } else {
            fraction -= 1.0f;
            current_ = target_;
        }
    }
}

void CharacterController::stepDown(float dt) {
    float downVelocity = (verticalVelocity_ < 0 ? -verticalVelocity_ : 0) * dt;
    if (downVelocity > 0 && downVelocity < stepHeight && (wasOnGround_ || !wasJumping_)) downVelocity = stepHeight;
    target_ = sub(target_, mul(kUp, currentStepOffset_ + downVelocity));
    CollisionWorld::Hit hit;
    if (world_.sweep(current_, target_, radius, kUp, maxSlopeCosine, hit)) {
        current_ = lerp(current_, target_, hit.fraction);
        verticalVelocity_ = verticalOffset_ = 0;
        wasJumping_ = false;
    } else {
        current_ = target_;
    }
}

void CharacterController::step(float dt) {
    // preStep
    touchingContact_ = false;
    for (int loops = 0; recoverFromPenetration();) {
        touchingContact_ = true;
        if (++loops > 4) break;
    }
    target_ = current_;
    // playerStep
    if (velocityTimeInterval_ <= 0) return;
    wasOnGround_ = onGround();
    verticalVelocity_ -= gravity * dt;
    if (verticalVelocity_ > 0 && verticalVelocity_ > jumpSpeed) verticalVelocity_ = jumpSpeed;
    if (verticalVelocity_ < 0 && std::fabs(verticalVelocity_) > std::fabs(fallSpeed)) verticalVelocity_ = -std::fabs(fallSpeed);
    verticalOffset_ = verticalVelocity_ * dt;
    stepUp();
    const float dtMoving = std::min(dt, velocityTimeInterval_);
    velocityTimeInterval_ -= dt;
    stepForwardAndStrafe(mul(walkDirection_, dtMoving));
    stepDown(dt);
}

} // namespace cr2
