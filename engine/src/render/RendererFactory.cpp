#include "mini/render/Renderer.h"
#include "mini/render/GLESRenderer.h"
#include "mini/render/NullRenderer.h"
#include "mini/render/VulkanRenderer.h"
#include "mini/core/Log.h"

namespace mini {

std::unique_ptr<IRenderer> createRenderer(const std::string& backendName) {
    if (backendName == "vulkan") {
        auto r = std::make_unique<VulkanRenderer>();
        // Vulkan is optional. If the SDK/loader or compiled SPIR-V is
        // unavailable, Engine::init falls back to NullRenderer.
        return r;
    }
    if (backendName == "gles3") {
        return std::make_unique<GLESRenderer>();
    }
    if (backendName != "null") {
        logWarn("createRenderer: unknown backend '" + backendName +
                "', using NullRenderer");
    }
    return std::make_unique<NullRenderer>();
}

} // namespace mini
