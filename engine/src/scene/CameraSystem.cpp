#include "mini/scene/CameraSystem.h"
#include "mini/scene/Components.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace mini {

CameraFrame CameraSystem::build(const World& world, int viewportWidth, int viewportHeight) const {
    CameraFrame frame;
    float fovY = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    int priority = std::numeric_limits<int>::min();

    world.each<Camera>([&](Entity entity, const Camera& camera) {
        const Transform* transform = world.get<Transform>(entity);
        if (!camera.isMain || !transform) return;
        if (frame.entity != kNullEntity && camera.priority <= priority) return;
        frame.entity = entity;
        priority = camera.priority;
        frame.position = transform->position;
        const Mat4 rotation = transform->rotation.toMat4();
        frame.forward = Vec3{-rotation.m[8], -rotation.m[9], -rotation.m[10]}.normalized();
        fovY = std::isfinite(camera.fovYDeg) ? clamp(camera.fovYDeg, 1.0f, 179.0f) : 60.0f;
        nearPlane = std::isfinite(camera.nearPlane) ? std::max(camera.nearPlane, 0.001f) : 0.1f;
        farPlane = std::isfinite(camera.farPlane) ? std::max(camera.farPlane, nearPlane + 0.01f) : 1000.0f;
    });

    const float aspect = viewportWidth > 0 && viewportHeight > 0
        ? static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight) : 1.0f;
    const Mat4 rotation = frame.entity != kNullEntity
        ? world.get<Transform>(frame.entity)->rotation.toMat4() : Mat4::identity();
    Vec3 up{rotation.m[4], rotation.m[5], rotation.m[6]};
    if (std::abs(dot(frame.forward, up)) > 0.999f) up = {0, 0, 1};
    frame.view = Mat4::lookAt(frame.position, frame.position + frame.forward, up);
    frame.projection = Mat4::perspective(degToRad(fovY), aspect, nearPlane, farPlane);
    return frame;
}

} // namespace mini
