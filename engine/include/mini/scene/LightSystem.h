#pragma once
#include <vector>
#include "mini/ecs/ECS.h"
#include "mini/render/Renderer.h"

namespace mini {

class LightSystem {
public:
    std::vector<RenderLight> collect(const World& world) const;
};

} // namespace mini
