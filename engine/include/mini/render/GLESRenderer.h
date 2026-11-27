#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "mini/render/Renderer.h"

namespace mini {

// OpenGL ES 3.0 renderer for Android (GLSurfaceView). The implementation
// is compiled only when building for Android with the NDK (GLES3 headers
// available); on desktop it falls back to a no-op stub so the shared
// engine code still compiles and links everywhere.
class GLESRenderer : public IRenderer {
public:
    bool init(int width, int height) override;
    void shutdown() override;
    void resize(int width, int height) override;
    void setEnvironment(const EnvironmentSettings& env) override;
    void setLights(const std::vector<RenderLight>& lights) override;
    void setCameraPosition(const Vec3& position) override;
    void beginFrame(const Mat4& view, const Mat4& projection) override;
    void drawMesh(const MeshData& mesh, const Mat4& model, const MaterialData& material) override;
    void endFrame() override;

    const char* name() const override { return "GLESRenderer"; }
    RenderStats stats() const override { return mStats; }

private:
    struct GpuMesh {
        u32 vao = 0;
        u32 vertexBuffer = 0;
        u32 indexBuffer = 0;
        u32 indexCount = 0;
    };
    struct DrawCommand {
        GpuMesh mesh;
        Mat4 model;
        MaterialData material;
        bool transient = false;
    };
    void clearGpuCache();
    void clearGpuTextures();
    u32 textureForPath(const std::string& path);
    void updateEnvironmentTexture();

    int mWidth = 0, mHeight = 0;
    RenderStats mStats;
    EnvironmentSettings mEnv;
    std::vector<RenderLight> mLights;
    Vec3 mCameraPosition{0, 2, 6};
    std::unordered_map<AssetId, GpuMesh> mGpuMeshes;
    std::unordered_map<std::string, u32> mGpuTextures;
    std::vector<DrawCommand> mDrawCommands;
    Mat4 mView;
    Mat4 mProjection;
    Mat4 mLightViewProjection;
    u32 mShadowFramebuffer = 0;
    u32 mShadowTexture = 0;
    u32 mShadowProgram = 0;
    u32 mSkyProgram = 0;
    bool mShadowAvailable = false;
    u32 mEnvironmentTexture = 0;
    int mEnvironmentMipLevels = 1;
    bool mEnvironmentTextureDirty = true;
    std::string mEnvironmentTexturePath;
};

} // namespace mini
