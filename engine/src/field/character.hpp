#pragma once
// The field player's movement: Bullet 2.81's btKinematicCharacterController, which MVGL's
// CharacterController wraps (FldPlayer::Init: a 0.02-radius sphere, steps of 0.01; gravity 29.4,
// fall speed 55, slopes to 45 degrees - Bullet's defaults, never changed), ported: each step up
// (the step height), across (sliding along what it meets, Bullet's way) and down (onto a
// walkable floor, or falling), after pushing out of anything it overlaps. FldPlayer::Update gives
// it a velocity each frame (setVelocityForTimeInterval for 0.05 s: walking 0.09 units a second,
// running 0.48) and the world steps it at a fixed 1/60 s.

#include "field/collision.hpp"

namespace cr2 {

class CharacterController {
public:
    explicit CharacterController(const CollisionWorld& world) : world_(world) {}

    void setPosition(const Vec3& p);       // (warp: btKinematicCharacterController::warp)
    const Vec3& position() const { return current_; }
    // the move for the next 0.05 s (units a second; y ignored)
    void setVelocity(const Vec3& v);
    // one world step (btKinematicCharacterController::updateAction: preStep, playerStep)
    void step(float dt = 1.0f / 60.0f);
    bool onGround() const { return verticalVelocity_ == 0 && verticalOffset_ == 0; }

    float radius = 0.02f, stepHeight = 0.01f;
    float gravity = 29.4f, fallSpeed = 55.0f, jumpSpeed = 10.0f;
    float maxSlopeCosine = 0.70710678f; // cos 45 degrees

private:
    bool recoverFromPenetration();
    void stepUp();
    void updateTargetPositionBasedOnCollision(const Vec3& hitNormal);
    void stepForwardAndStrafe(const Vec3& walkMove);
    void stepDown(float dt);

    const CollisionWorld& world_;
    Vec3 current_{}, target_{};
    Vec3 walkDirection_{}, normalizedDirection_{};
    float velocityTimeInterval_ = 0;
    float verticalVelocity_ = 0, verticalOffset_ = 0, currentStepOffset_ = 0;
    bool wasOnGround_ = false, wasJumping_ = false;
    bool touchingContact_ = false;
    Vec3 touchingNormal_{};
};

} // namespace cr2
