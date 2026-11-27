#include "mini/scene/LightSystem.h"
#include "mini/scene/Components.h"
#include <algorithm>
#include <cmath>

namespace mini {
namespace {

Vec3 rotateDirection(const Quaternion& rotation, const Vec3& direction) {
    const Mat4 m = rotation.toMat4();
    return Vec3{m.m[0] * direction.x + m.m[4] * direction.y + m.m[8] * direction.z,
                m.m[1] * direction.x + m.m[5] * direction.y + m.m[9] * direction.z,
                m.m[2] * direction.x + m.m[6] * direction.y + m.m[10] * direction.z}.normalized();
}

} // namespace

std::vector<RenderLight> LightSystem::collect(const World& world) const {
    std::vector<RenderLight> result;
    world.each<Light>([&](Entity entity, const Light& source) {
        const Transform* transform = world.get<Transform>(entity);
        if (!transform || !source.enabled || !std::isfinite(source.intensity) || source.intensity <= 0.0f)
            return;
        RenderLight light;
        light.type = source.type;
        light.position = transform->position;
        const Vec3 localDirection = source.type == LightType::Directional
            ? source.localDirection : Vec3{0, 0, -1};
        light.direction = rotateDirection(transform->rotation, localDirection);
        light.color = {std::max(0.0f, std::isfinite(source.color.x) ? source.color.x : 1.0f),
                       std::max(0.0f, std::isfinite(source.color.y) ? source.color.y : 1.0f),
                       std::max(0.0f, std::isfinite(source.color.z) ? source.color.z : 1.0f)};
        light.intensity = std::max(0.0f, source.intensity);
        light.range = std::max(0.01f, std::isfinite(source.range) ? source.range : 10.0f);
        const float inner = std::isfinite(source.innerConeDegrees) ? source.innerConeDegrees : 20.0f;
        const float outer = std::isfinite(source.outerConeDegrees) ? source.outerConeDegrees : 30.0f;
        light.innerConeDegrees = clamp(inner, 0.0f, 89.0f);
        light.outerConeDegrees = clamp(outer, light.innerConeDegrees + 0.1f, 89.9f);
        result.push_back(light);
    });
    std::stable_sort(result.begin(), result.end(), [](const RenderLight& a, const RenderLight& b) {
        if (a.type != b.type) return a.type == LightType::Directional;
        return a.intensity > b.intensity;
    });
    return result;
}

} // namespace mini
