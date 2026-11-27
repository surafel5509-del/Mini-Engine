#pragma once
#include <memory>
#include <string>
#include <vector>
#include "mini/assets/AssetManager.h"
#include "mini/core/Math.h"
#include "mini/core/Types.h"
#include "mini/scene/Components.h"

namespace mini {

struct RenderStats {
    u32 drawCalls = 0;
    u32 triangles = 0;
};

struct RenderLight {
    LightType type = LightType::Directional;
    Vec3 position{0, 0, 0};
    Vec3 direction{0, -1, 0}; // direction rays travel for directional lights
    Vec3 color{1, 1, 1};
    f32 intensity = 1.0f;
    f32 range = 10.0f;
    f32 innerConeDegrees = 20.0f;
    f32 outerConeDegrees = 30.0f;
};

// Graphics-API-agnostic renderer interface. The engine talks only to
// this; concrete backends (GLES3, Vulkan) implement it. NullRenderer is
// used for headless runs and unit tests.
class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual bool init(int width, int height) = 0;
    virtual void shutdown() = 0;
    virtual void resize(int width, int height) = 0;

    virtual void setEnvironment(const EnvironmentSettings& env) = 0;
    virtual void setLights(const std::vector<RenderLight>& lights) = 0;
    virtual void setCameraPosition(const Vec3& position) = 0;
    virtual void beginFrame(const Mat4& view, const Mat4& projection) = 0;
    virtual void drawMesh(const MeshData& mesh, const Mat4& model, const MaterialData& material) = 0;
    virtual void endFrame() = 0;

    virtual const char* name() const = 0;
    virtual RenderStats stats() const = 0;
};

// Factory: picks a backend by name ("null", "gles3", "vulkan").
// Unknown names fall back to NullRenderer.
std::unique_ptr<IRenderer> createRenderer(const std::string& backendName);

} // namespace mini
