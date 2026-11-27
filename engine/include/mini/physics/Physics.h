#pragma once
#include "mini/ecs/ECS.h"

namespace mini {

// Deterministic fixed-step sphere physics with gravity, a configurable ground
// plane, inverse-mass positional correction, restitution and Coulomb friction.
class PhysicsWorld {
public:
    void step(World& world, float elapsedSeconds);
    void resetClock() { mAccumulator = 0.0f; }

    float gravity = -9.81f;
    float groundY = 0.0f;
    bool groundEnabled = true;
    float fixedDeltaTime = 1.0f / 60.0f;
    unsigned maxSubsteps = 8;
    float maxFrameDelta = 0.25f;

private:
    void simulate(World& world, float dt);
    float mAccumulator = 0.0f;
};

} // namespace mini
