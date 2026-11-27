#pragma once
#include "mini/ecs/ECS.h"
#include "mini/core/Math.h"

namespace mini {

struct CameraFrame {
    Entity entity = kNullEntity;
    Vec3 position{0, 2, 6};
    Vec3 forward{0, 0, -1};
    Mat4 view = Mat4::identity();
    Mat4 projection = Mat4::identity();
};

// Builds the active camera matrices from ECS components. Main cameras are
// selected deterministically by highest priority, then lowest entity id.
class CameraSystem {
public:
    CameraFrame build(const World& world, int viewportWidth, int viewportHeight) const;
};

} // namespace mini
