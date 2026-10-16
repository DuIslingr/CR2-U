#pragma once
// The field player's movement: Bullet 2.81's btKinematicCharacterController, which MVGL's
// CharacterController wraps (FldPlayer::Init: a 0.02-radius sphere, steps of 0.01; gravity 29.4,
// fall speed 55, slopes to 45 degrees - Bullet's defaults, never changed), ported: each step up
// (the step height), across (sliding along what it meets, Bullet's way) and down (onto a
// walkable floor, or falling), after pushing out of anything it overlaps. The game updates 20
// times a second; each update FldPlayer::Update gives it a velocity (direction x speed x dt x 60,
// dt 0.05: setVelocityForTimeInterval for 0.05 s) and FldMain steps the world once
// (stepSimulation(dt, 1 substep, 1/60): one 1/60 s step, the rest of the 0.05 s dropped) - so it
// moves the speed's units a second (walking 0.09, running 0.48) while its gravity runs at a third
// of real time. Here that 1/60 s of the world's time is three steps of 1/180 s, one each 1/60 s
// of real time - the same speed and gravity, the walls met as finely as the speed allows (at the
// game's one big step Bullet's sphere can wedge past a wall's plane in a crease) and a position
// for each of 60 frames a second. update() is one of the game's updates, all three at once.

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
    // the move FldPlayer::Update asks for (units a second, along the ground): its velocity for
    // the update (direction x speed x dt x 60)
    void move(const Vec3& unitsPerSecond) {
        setVelocity({unitsPerSecond[0] * kUpdate * 60, 0, unitsPerSecond[2] * kUpdate * 60});
    }
    // a third of the world's 1/60 s step an update takes
    void substep() { step(1.0f / (60.0f * kSubsteps)); }
    // one of the game's updates (0.05 s): moving at `unitsPerSecond`, the world's 1/60 s
    void update(const Vec3& unitsPerSecond) {
        move(unitsPerSecond);
        for (int k = 0; k < kSubsteps; ++k) substep();
    }
    static constexpr float kUpdate = 0.05f;
    static constexpr int kSubsteps = 3;
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
