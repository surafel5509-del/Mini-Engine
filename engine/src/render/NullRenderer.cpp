#include "mini/render/NullRenderer.h"
#include "mini/core/Log.h"

namespace mini {

bool NullRenderer::init(int width, int height) {
    mWidth = width; mHeight = height;
    logInfo("NullRenderer initialized (" + std::to_string(width) + "x" +
            std::to_string(height) + ")");
    return true;
}

void NullRenderer::shutdown() { logInfo("NullRenderer shutdown"); }
void NullRenderer::resize(int width, int height) { mWidth = width; mHeight = height; }
void NullRenderer::setEnvironment(const EnvironmentSettings& env) { mEnv = env; }

void NullRenderer::beginFrame(const Mat4&, const Mat4&) {
    mStats = RenderStats{};
}

void NullRenderer::drawMesh(const MeshData& mesh, const Mat4&, const MaterialData&) {
    mStats.drawCalls += 1;
    mStats.triangles += static_cast<u32>(mesh.indices.size() / 3);
}

void NullRenderer::endFrame() {}

} // namespace mini
