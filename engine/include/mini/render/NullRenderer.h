#pragma once
#include "mini/render/Renderer.h"

namespace mini {

// Headless renderer: validates the frame pipeline without a GPU.
// Used by the smoke test and by desktop CI.
class NullRenderer : public IRenderer {
public:
    bool init(int width, int height) override;
    void shutdown() override;
    void resize(int width, int height) override;
    void setEnvironment(const EnvironmentSettings& env) override;
    void setLights(const std::vector<RenderLight>&) override {}
    void setCameraPosition(const Vec3&) override {}
    void beginFrame(const Mat4& view, const Mat4& projection) override;
    void drawMesh(const MeshData& mesh, const Mat4& model, const MaterialData& material) override;
    void endFrame() override;

    const char* name() const override { return "NullRenderer"; }
    RenderStats stats() const override { return mStats; }

private:
    int mWidth = 0, mHeight = 0;
    RenderStats mStats;
    EnvironmentSettings mEnv;
};

} // namespace mini
