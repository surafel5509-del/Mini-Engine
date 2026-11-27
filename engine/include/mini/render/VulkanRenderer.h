#pragma once
#include <string>
#include "mini/render/Renderer.h"

namespace mini {

// Vulkan renderer — the long-term primary backend. The full
// implementation (instance, device, swapchain, pipeline, command
// buffers) is compiled when the Vulkan SDK is present (CMake defines
// MINI_VULKAN); otherwise this falls back to a stub that reports
// failure so the engine can pick another backend.
class VulkanRenderer : public IRenderer {
public:
    VulkanRenderer();
    ~VulkanRenderer() override;

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    bool init(int width, int height) override;
    void shutdown() override;
    void resize(int width, int height) override;
    void setEnvironment(const EnvironmentSettings& env) override;
    void setLights(const std::vector<RenderLight>& lights) override;
    void setCameraPosition(const Vec3& position) override;
    void beginFrame(const Mat4& view, const Mat4& projection) override;
    void drawMesh(const MeshData& mesh, const Mat4& model, const MaterialData& material) override;
    void endFrame() override;

    // Directory containing vert.spv / frag.spv (default: "shaders").
    void setShaderDir(const std::string& dir);

    // Attach a platform window (ANativeWindow* on Android) and create
    // the swapchain. Until this is called the renderer runs headless
    // (device + pipeline only, no presentation).
    bool attachSurface(void* nativeWindow);
    bool detachSurface();

    const char* name() const override { return "VulkanRenderer"; }
    RenderStats stats() const override { return mStats; }

private:
    void updateEnvironmentTexture();
    bool ensureMaterialTexture(const std::string& path);
    struct Impl;
    Impl* mImpl = nullptr;
    int mWidth = 0, mHeight = 0;
    RenderStats mStats;
    EnvironmentSettings mEnv;
    std::vector<RenderLight> mLights;
    Vec3 mCameraPosition{0, 2, 6};
    std::string mShaderDir = "shaders";
};

} // namespace mini
