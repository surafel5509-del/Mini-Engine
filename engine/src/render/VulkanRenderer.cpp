#include "mini/render/VulkanRenderer.h"
#include "mini/assets/Image.h"
#include "mini/core/Log.h"

#ifdef MINI_VULKAN
// Full Vulkan implementation (compiled when the Vulkan SDK is found by
// CMake, which defines MINI_VULKAN). Platform surface creation is
// loaded through vkGetInstanceProcAddr so this file compiles on any
// desktop OS without platform headers; only Android presentation is
// wired in Phase 2 (attachSurface with an ANativeWindow*).
#if defined(__ANDROID__)
#include <android/native_window.h>
#endif
#include <vulkan/vulkan.h>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace mini {
namespace {

bool checkVk(VkResult r, const char* what) {
    if (r != VK_SUCCESS) {
        logError(std::string("Vulkan: ") + what + " failed (" +
                 std::to_string(static_cast<int>(r)) + ")");
        return false;
    }
    return true;
}

std::vector<char> readSpv(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return {};
    std::streamsize size = in.tellg();
    std::vector<char> data(static_cast<size_t>(size));
    in.seekg(0);
    in.read(data.data(), size);
    return data;
}

// Must match the UBO layout in engine/shaders/main.{vert,frag}.
struct UboData {
    float model[16];
    float view[16];
    float projection[16];
    float color[3];
    float ambient;
    float exposure;
    float metallic;
    float roughness;
    float reflectionIntensity;
    float lightDir[3];
    float environmentAvailable;
    float environmentRotation;
    float cameraPosition[3];
    float lightColorIntensity[4];
    float hasBaseColorTexture;
    float alphaMode;
    float alphaCutoff;
    float baseColorAlpha;
};
static_assert(sizeof(UboData) == 288, "UBO layout mismatch with GLSL");
static_assert(offsetof(UboData, color) == 192 && offsetof(UboData, lightDir) == 224 &&
              offsetof(UboData, lightColorIntensity) == 256 &&
              offsetof(UboData, hasBaseColorTexture) == 272 &&
              offsetof(UboData, baseColorAlpha) == 284,
              "UBO member offsets mismatch with GLSL std140 layout");

} // namespace

struct VulkanRenderer::Impl {
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
#if defined(__ANDROID__)
    ANativeWindow* nativeWindow = nullptr; // retained until detachSurface
#endif
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent{};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainViews;
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    struct EnvironmentTexture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        uint32_t mipLevels = 1;
    } environmentTexture;
    struct MaterialTexture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
    };
    std::unordered_map<std::string, MaterialTexture> materialTextures;
    std::string environmentPath;
    bool environmentDirty = true;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    struct GpuMesh {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory = VK_NULL_HANDLE;
        uint32_t indexCount = 0;
    };
    std::unordered_map<AssetId, GpuMesh> gpuMeshes;

    struct DrawResources {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory = VK_NULL_HANDLE;
        VkBuffer uniformBuffer = VK_NULL_HANDLE;
        VkDeviceMemory uniformMemory = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    };
    std::vector<DrawResources> frameResources;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
    VkFence inFlight = VK_NULL_HANDLE;

    uint32_t currentImage = 0;
    bool hasSwapchain = false;
    bool frameRecording = false;
    Mat4 view, projection;
};

VulkanRenderer::VulkanRenderer() : mImpl(new Impl()) {}

VulkanRenderer::~VulkanRenderer() {
    shutdown();
    delete mImpl;
}

void VulkanRenderer::setShaderDir(const std::string& dir) { mShaderDir = dir; }

bool VulkanRenderer::init(int width, int height) {
    mWidth = width;
    mHeight = height;
    Impl& im = *mImpl;

    // --- Instance ---
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Mini-Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "Mini-Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    std::vector<const char*> exts = {VK_KHR_SURFACE_EXTENSION_NAME};
#if defined(__ANDROID__)
    exts.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#endif

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(exts.size());
    createInfo.ppEnabledExtensionNames = exts.data();
    if (!checkVk(vkCreateInstance(&createInfo, nullptr, &im.instance), "vkCreateInstance"))
        return false;

    // --- Physical device (prefer discrete GPU) ---
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(im.instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        logError("Vulkan: no physical devices found");
        return false;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(im.instance, &deviceCount, devices.data());
    VkPhysicalDevice chosen = VK_NULL_HANDLE;
    for (VkPhysicalDevice d : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(d, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            chosen = d;
            break;
        }
        if (!chosen) chosen = d;
    }
    im.physicalDevice = chosen;

    // --- Queue family (graphics) ---
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(im.physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(im.physicalDevice, &familyCount, families.data());
    bool found = false;
    for (uint32_t i = 0; i < familyCount; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            im.queueFamily = i;
            found = true;
            break;
        }
    }
    if (!found) {
        logError("Vulkan: no graphics queue family");
        return false;
    }

    // --- Logical device ---
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = im.queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkPhysicalDeviceFeatures features{};
    // No optional feature is required by the Phase 2 pipeline.

    std::vector<const char*> deviceExts = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExts.size());
    deviceInfo.ppEnabledExtensionNames = deviceExts.data();
    deviceInfo.pEnabledFeatures = &features;
    if (!checkVk(vkCreateDevice(im.physicalDevice, &deviceInfo, nullptr, &im.device),
                 "vkCreateDevice"))
        return false;
    vkGetDeviceQueue(im.device, im.queueFamily, 0, &im.graphicsQueue);

    // Per-draw UBO, shared environment sampler, and material base-color map.
    VkDescriptorSetLayoutBinding descriptorBindings[3]{};
    descriptorBindings[0].binding = 0;
    descriptorBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorBindings[0].descriptorCount = 1;
    descriptorBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    descriptorBindings[1].binding = 1;
    descriptorBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorBindings[1].descriptorCount = 1;
    descriptorBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    descriptorBindings[2].binding = 2;
    descriptorBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorBindings[2].descriptorCount = 1;
    descriptorBindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = descriptorBindings;
    if (!checkVk(vkCreateDescriptorSetLayout(im.device, &layoutInfo, nullptr,
                                             &im.descriptorLayout),
                 "descriptor set layout"))
        return false;

    // --- Pipeline layout ---
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &im.descriptorLayout;
    if (!checkVk(vkCreatePipelineLayout(im.device, &pipelineLayoutInfo, nullptr,
                                        &im.pipelineLayout),
                 "pipeline layout"))
        return false;

    // --- Shaders (SPIR-V files; see engine/shaders/README.md) ---
    std::vector<char> vertSpv = readSpv(mShaderDir + "/vert.spv");
    std::vector<char> fragSpv = readSpv(mShaderDir + "/frag.spv");
    if (vertSpv.empty() || fragSpv.empty()) {
        logError("Vulkan: shader SPIR-V not found in " + mShaderDir +
                 " (compile with glslc, see engine/shaders/README.md)");
        return false;
    }
    auto makeModule = [&](const std::vector<char>& code) -> VkShaderModule {
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = code.size();
        info.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule mod = VK_NULL_HANDLE;
        vkCreateShaderModule(im.device, &info, nullptr, &mod);
        return mod;
    };
    VkShaderModule vertMod = makeModule(vertSpv);
    VkShaderModule fragMod = makeModule(fragSpv);
    if (!vertMod || !fragMod) {
        logError("Vulkan: failed to create shader modules");
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertMod;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragMod;
    stages[1].pName = "main";

    // Vertex input: position, normal, and texture coordinates.
    VkVertexInputBindingDescription bindings[1]{};
    bindings[0].binding = 0;
    bindings[0].stride = 8 * sizeof(float);
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[3]{};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[0].offset = 0;
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attrs[1].offset = 3 * sizeof(float);
    attrs[2].location = 2;
    attrs[2].binding = 0;
    attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[2].offset = 6 * sizeof(float);
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = bindings;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAttach{};
    blendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttach;

    VkDynamicState dynamicStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    // Render pass (created here so the pipeline can reference it; the
    // swapchain framebuffers are attached later in attachSurface()).
    VkAttachmentDescription colorAttach{};
    colorAttach.format = VK_FORMAT_B8G8R8A8_UNORM;
    colorAttach.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttach.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttach.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentDescription depthAttach{};
    depthAttach.format = VK_FORMAT_D32_SFLOAT;
    depthAttach.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttach.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttach.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;
    VkAttachmentDescription attachments[2] = {colorAttach, depthAttach};
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 2;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    if (!checkVk(vkCreateRenderPass(im.device, &renderPassInfo, nullptr, &im.renderPass),
                 "render pass"))
        return false;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &blend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = im.pipelineLayout;
    pipelineInfo.renderPass = im.renderPass;
    pipelineInfo.subpass = 0;
    if (!checkVk(vkCreateGraphicsPipelines(im.device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                           nullptr, &im.pipeline),
                 "graphics pipeline"))
        return false;

    vkDestroyShaderModule(im.device, vertMod, nullptr);
    vkDestroyShaderModule(im.device, fragMod, nullptr);

    // Descriptor pool is recycled after the previous frame fence signals.
    // Each draw gets its own UBO/descriptor set so writes cannot overwrite
    // data referenced by earlier draws in the same command buffer.
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 4096;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    // Each per-draw set binds both the environment and material samplers.
    poolSizes[1].descriptorCount = 8192;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 4096;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    if (!checkVk(vkCreateDescriptorPool(im.device, &poolInfo, nullptr, &im.descriptorPool),
                 "descriptor pool"))
        return false;

    // --- Command pool ---
    VkCommandPoolCreateInfo cmdPoolInfo{};
    cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolInfo.queueFamilyIndex = im.queueFamily;
    if (!checkVk(vkCreateCommandPool(im.device, &cmdPoolInfo, nullptr, &im.commandPool),
                 "command pool"))
        return false;

    // --- Sync objects ---
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    vkCreateSemaphore(im.device, &semInfo, nullptr, &im.imageAvailable);
    vkCreateSemaphore(im.device, &semInfo, nullptr, &im.renderFinished);
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(im.device, &fenceInfo, nullptr, &im.inFlight);

    logInfo("VulkanRenderer initialized (device + pipeline; attach a surface to present)");
    return true;
}

bool VulkanRenderer::attachSurface(void* nativeWindow) {
    Impl& im = *mImpl;
    if (!im.instance || !im.device || !nativeWindow) return false;
    if (im.surface) detachSurface();

#if defined(__ANDROID__)
    // Load the Android surface entry point without platform headers.
    using PFN_CreateAndroidSurfaceKHR = VkResult (*)(VkInstance, const VkAndroidSurfaceCreateInfoKHR*, const VkAllocationCallbacks*, VkSurfaceKHR*);
    auto createFn = reinterpret_cast<PFN_CreateAndroidSurfaceKHR>(
        vkGetInstanceProcAddr(im.instance, "vkCreateAndroidSurfaceKHR"));
    if (!createFn) {
        logError("Vulkan: vkCreateAndroidSurfaceKHR not available");
        return false;
    }
    VkAndroidSurfaceCreateInfoKHR surfaceInfo{};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.window = static_cast<ANativeWindow*>(nativeWindow);
    if (!checkVk(createFn(im.instance, &surfaceInfo, nullptr, &im.surface),
                 "Android surface"))
        return false;
    im.nativeWindow = static_cast<ANativeWindow*>(nativeWindow);
    ANativeWindow_acquire(im.nativeWindow);
#else
    (void)nativeWindow;
    logWarn("Vulkan: attachSurface is only implemented on Android in Phase 2");
    return false;
#endif

    // --- Swapchain ---
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(im.physicalDevice, im.surface, &caps);
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(im.physicalDevice, im.surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(im.physicalDevice, im.surface, &formatCount, formats.data());
    // The render pass is created during init with this format; select the
    // same supported format for the swapchain or fail instead of creating
    // an incompatible framebuffer/pipeline pair.
    bool compatibleFormat = false;
    VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    for (const VkSurfaceFormatKHR& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            im.swapchainFormat = f.format;
            colorSpace = f.colorSpace;
            compatibleFormat = true;
            break;
        }
    }
    if (!compatibleFormat) {
        logError("Vulkan: surface does not support the render pass format (BGRA8 UNORM)");
        return false;
    }
    VkBool32 presentSupported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(im.physicalDevice, im.queueFamily,
                                         im.surface, &presentSupported);
    if (!presentSupported) {
        logError("Vulkan: selected graphics queue cannot present to this surface");
        return false;
    }
    im.swapchainExtent = caps.currentExtent;
    if (caps.currentExtent.width == UINT32_MAX) {
        im.swapchainExtent.width = static_cast<uint32_t>(mWidth);
        im.swapchainExtent.height = static_cast<uint32_t>(mHeight);
    }
    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR swapInfo{};
    swapInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapInfo.surface = im.surface;
    swapInfo.minImageCount = imageCount;
    swapInfo.imageFormat = im.swapchainFormat;
    swapInfo.imageColorSpace = colorSpace;
    swapInfo.imageExtent = im.swapchainExtent;
    swapInfo.imageArrayLayers = 1;
    swapInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapInfo.preTransform = caps.currentTransform;
    swapInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapInfo.clipped = VK_TRUE;
    if (!checkVk(vkCreateSwapchainKHR(im.device, &swapInfo, nullptr, &im.swapchain),
                 "swapchain"))
        return false;

    uint32_t imgCount = 0;
    vkGetSwapchainImagesKHR(im.device, im.swapchain, &imgCount, nullptr);
    im.swapchainImages.resize(imgCount);
    vkGetSwapchainImagesKHR(im.device, im.swapchain, &imgCount, im.swapchainImages.data());
    im.swapchainViews.resize(imgCount);
    for (uint32_t i = 0; i < imgCount; ++i) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = im.swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = im.swapchainFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        vkCreateImageView(im.device, &viewInfo, nullptr, &im.swapchainViews[i]);
    }

    // --- Depth image ---
    VkImageCreateInfo depthInfo{};
    depthInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthInfo.imageType = VK_IMAGE_TYPE_2D;
    depthInfo.format = VK_FORMAT_D32_SFLOAT;
    depthInfo.extent = {im.swapchainExtent.width, im.swapchainExtent.height, 1};
    depthInfo.mipLevels = 1;
    depthInfo.arrayLayers = 1;
    depthInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!checkVk(vkCreateImage(im.device, &depthInfo, nullptr, &im.depthImage), "depth image"))
        return false;
    VkMemoryRequirements depthReq{};
    vkGetImageMemoryRequirements(im.device, im.depthImage, &depthReq);
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(im.physicalDevice, &memProps);
    VkMemoryAllocateInfo depthAlloc{};
    depthAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    depthAlloc.allocationSize = depthReq.size;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((depthReq.memoryTypeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            depthAlloc.memoryTypeIndex = i;
            break;
        }
    }
    vkAllocateMemory(im.device, &depthAlloc, nullptr, &im.depthMemory);
    vkBindImageMemory(im.device, im.depthImage, im.depthMemory, 0);
    VkImageViewCreateInfo depthViewInfo{};
    depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depthViewInfo.image = im.depthImage;
    depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthViewInfo.format = VK_FORMAT_D32_SFLOAT;
    depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthViewInfo.subresourceRange.levelCount = 1;
    depthViewInfo.subresourceRange.layerCount = 1;
    vkCreateImageView(im.device, &depthViewInfo, nullptr, &im.depthView);

    // --- Framebuffers + command buffers ---
    im.framebuffers.resize(imgCount);
    im.commandBuffers.resize(imgCount);
    for (uint32_t i = 0; i < imgCount; ++i) {
        VkImageView attachments[2] = {im.swapchainViews[i], im.depthView};
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = im.renderPass;
        fbInfo.attachmentCount = 2;
        fbInfo.pAttachments = attachments;
        fbInfo.width = im.swapchainExtent.width;
        fbInfo.height = im.swapchainExtent.height;
        fbInfo.layers = 1;
        vkCreateFramebuffer(im.device, &fbInfo, nullptr, &im.framebuffers[i]);

        VkCommandBufferAllocateInfo cmdInfo{};
        cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdInfo.commandPool = im.commandPool;
        cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdInfo.commandBufferCount = 1;
        vkAllocateCommandBuffers(im.device, &cmdInfo, &im.commandBuffers[i]);
    }

    im.hasSwapchain = true;
    logInfo("VulkanRenderer: swapchain attached (" +
            std::to_string(im.swapchainExtent.width) + "x" +
            std::to_string(im.swapchainExtent.height) + ")");
    return true;
}

bool VulkanRenderer::detachSurface() {
    if (!mImpl) return false;
    Impl& im = *mImpl;
    if (!im.surface && !im.hasSwapchain) return true;
    if (im.device) vkDeviceWaitIdle(im.device);
    if (im.device) {
        for (const auto& resources : im.frameResources) {
            if (resources.vertexBuffer) vkDestroyBuffer(im.device, resources.vertexBuffer, nullptr);
            if (resources.vertexMemory) vkFreeMemory(im.device, resources.vertexMemory, nullptr);
            if (resources.indexBuffer) vkDestroyBuffer(im.device, resources.indexBuffer, nullptr);
            if (resources.indexMemory) vkFreeMemory(im.device, resources.indexMemory, nullptr);
            if (resources.uniformBuffer) vkDestroyBuffer(im.device, resources.uniformBuffer, nullptr);
            if (resources.uniformMemory) vkFreeMemory(im.device, resources.uniformMemory, nullptr);
        }
        im.frameResources.clear();
        if (im.descriptorPool) vkResetDescriptorPool(im.device, im.descriptorPool, 0);
    }
    if (im.device && im.commandPool && !im.commandBuffers.empty()) {
        vkFreeCommandBuffers(im.device, im.commandPool,
            static_cast<uint32_t>(im.commandBuffers.size()), im.commandBuffers.data());
    }
    im.commandBuffers.clear();
    if (im.device) {
        for (VkFramebuffer fb : im.framebuffers)
            if (fb) vkDestroyFramebuffer(im.device, fb, nullptr);
        if (im.depthView) vkDestroyImageView(im.device, im.depthView, nullptr);
        if (im.depthImage) vkDestroyImage(im.device, im.depthImage, nullptr);
        if (im.depthMemory) vkFreeMemory(im.device, im.depthMemory, nullptr);
        for (VkImageView view : im.swapchainViews)
            if (view) vkDestroyImageView(im.device, view, nullptr);
        if (im.swapchain) vkDestroySwapchainKHR(im.device, im.swapchain, nullptr);
    }
    im.framebuffers.clear();
    im.swapchainViews.clear();
    im.swapchainImages.clear();
    im.depthView = VK_NULL_HANDLE;
    im.depthImage = VK_NULL_HANDLE;
    im.depthMemory = VK_NULL_HANDLE;
    im.swapchain = VK_NULL_HANDLE;
    im.hasSwapchain = false;
    im.frameRecording = false;
    if (im.surface && im.instance) vkDestroySurfaceKHR(im.instance, im.surface, nullptr);
    im.surface = VK_NULL_HANDLE;
#if defined(__ANDROID__)
    if (im.nativeWindow) ANativeWindow_release(im.nativeWindow);
    im.nativeWindow = nullptr;
#endif
    return true;
}

void VulkanRenderer::shutdown() {
    if (!mImpl) return;
    Impl& im = *mImpl;
    detachSurface();
    if (im.device) vkDeviceWaitIdle(im.device);
    if (im.device) {
    if (im.commandPool) vkDestroyCommandPool(im.device, im.commandPool, nullptr);
    for (VkFramebuffer fb : im.framebuffers)
        if (fb) vkDestroyFramebuffer(im.device, fb, nullptr);
    if (im.depthView) vkDestroyImageView(im.device, im.depthView, nullptr);
    if (im.depthImage) vkDestroyImage(im.device, im.depthImage, nullptr);
    if (im.depthMemory) vkFreeMemory(im.device, im.depthMemory, nullptr);
    for (VkImageView v : im.swapchainViews)
        if (v) vkDestroyImageView(im.device, v, nullptr);
    if (im.swapchain) vkDestroySwapchainKHR(im.device, im.swapchain, nullptr);
    if (im.surface) vkDestroySurfaceKHR(im.instance, im.surface, nullptr);
    if (im.pipeline) vkDestroyPipeline(im.device, im.pipeline, nullptr);
    if (im.pipelineLayout) vkDestroyPipelineLayout(im.device, im.pipelineLayout, nullptr);
    if (im.renderPass) vkDestroyRenderPass(im.device, im.renderPass, nullptr);
    if (im.descriptorPool) vkDestroyDescriptorPool(im.device, im.descriptorPool, nullptr);
    if (im.descriptorLayout) vkDestroyDescriptorSetLayout(im.device, im.descriptorLayout, nullptr);
    if (im.environmentTexture.sampler) vkDestroySampler(im.device, im.environmentTexture.sampler, nullptr);
    if (im.environmentTexture.view) vkDestroyImageView(im.device, im.environmentTexture.view, nullptr);
    if (im.environmentTexture.image) vkDestroyImage(im.device, im.environmentTexture.image, nullptr);
    if (im.environmentTexture.memory) vkFreeMemory(im.device, im.environmentTexture.memory, nullptr);
    for (const auto& entry : im.materialTextures) {
        const Impl::MaterialTexture& texture = entry.second;
        if (texture.sampler) vkDestroySampler(im.device, texture.sampler, nullptr);
        if (texture.view) vkDestroyImageView(im.device, texture.view, nullptr);
        if (texture.image) vkDestroyImage(im.device, texture.image, nullptr);
        if (texture.memory) vkFreeMemory(im.device, texture.memory, nullptr);
    }
    im.materialTextures.clear();
    for (const auto& r : im.frameResources) {
        if (r.vertexBuffer) vkDestroyBuffer(im.device, r.vertexBuffer, nullptr);
        if (r.vertexMemory) vkFreeMemory(im.device, r.vertexMemory, nullptr);
        if (r.indexBuffer) vkDestroyBuffer(im.device, r.indexBuffer, nullptr);
        if (r.indexMemory) vkFreeMemory(im.device, r.indexMemory, nullptr);
        if (r.uniformBuffer) vkDestroyBuffer(im.device, r.uniformBuffer, nullptr);
        if (r.uniformMemory) vkFreeMemory(im.device, r.uniformMemory, nullptr);
    }
    for (const auto& entry : im.gpuMeshes) {
        const Impl::GpuMesh& mesh = entry.second;
        if (mesh.vertexBuffer) vkDestroyBuffer(im.device, mesh.vertexBuffer, nullptr);
        if (mesh.vertexMemory) vkFreeMemory(im.device, mesh.vertexMemory, nullptr);
        if (mesh.indexBuffer) vkDestroyBuffer(im.device, mesh.indexBuffer, nullptr);
        if (mesh.indexMemory) vkFreeMemory(im.device, mesh.indexMemory, nullptr);
    }
    im.gpuMeshes.clear();
    if (im.imageAvailable) vkDestroySemaphore(im.device, im.imageAvailable, nullptr);
    if (im.renderFinished) vkDestroySemaphore(im.device, im.renderFinished, nullptr);
    if (im.inFlight) vkDestroyFence(im.device, im.inFlight, nullptr);
    vkDestroyDevice(im.device, nullptr);
    }
    if (im.instance) vkDestroyInstance(im.instance, nullptr);
    im = Impl{};
}

void VulkanRenderer::resize(int width, int height) {
    mWidth = width;
    mHeight = height;
#if defined(__ANDROID__)
    // Android can change the Surface size without destroying its native
    // window. Recreate swapchain-dependent resources for the new extent.
    if (mImpl && mImpl->nativeWindow && mImpl->hasSwapchain) {
        ANativeWindow* window = mImpl->nativeWindow;
        ANativeWindow_acquire(window); // keep the argument alive across detach
        attachSurface(window);
        ANativeWindow_release(window);
    }
#endif
}

void VulkanRenderer::updateEnvironmentTexture() {
    Impl& im = *mImpl;
    if (!im.environmentDirty || !im.device || !im.commandPool) return;
    im.environmentDirty = false;

    if (im.environmentTexture.sampler)
        vkDestroySampler(im.device, im.environmentTexture.sampler, nullptr);
    if (im.environmentTexture.view)
        vkDestroyImageView(im.device, im.environmentTexture.view, nullptr);
    if (im.environmentTexture.image)
        vkDestroyImage(im.device, im.environmentTexture.image, nullptr);
    if (im.environmentTexture.memory)
        vkFreeMemory(im.device, im.environmentTexture.memory, nullptr);
    im.environmentTexture = Impl::EnvironmentTexture{};

    ImageData source;
    if (!mEnv.skyboxTexture.empty() && !loadPpmImage(mEnv.skyboxTexture, source))
        logWarn("Vulkan: environment map failed to load: " + mEnv.skyboxTexture +
                " (using fallback environment)");
    if (!source.valid()) {
        source.width = 1;
        source.height = 1;
        source.rgb = {18, 28, 48};
    }

    struct MipData { uint32_t width, height; std::vector<uint8_t> rgba; };
    std::vector<MipData> mips;
    MipData base{static_cast<uint32_t>(source.width),
                 static_cast<uint32_t>(source.height), {}};
    base.rgba.resize(static_cast<size_t>(base.width) * base.height * 4);
    for (size_t i = 0; i < static_cast<size_t>(base.width) * base.height; ++i) {
        base.rgba[i*4+0] = source.rgb[i*3+0];
        base.rgba[i*4+1] = source.rgb[i*3+1];
        base.rgba[i*4+2] = source.rgb[i*3+2];
        base.rgba[i*4+3] = 255;
    }
    mips.push_back(std::move(base));
    auto srgbToLinear = [](float x) {
        x /= 255.0f;
        return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
    };
    auto linearToSrgb = [](float x) {
        x = std::max(0.0f, std::min(1.0f, x));
        const float encoded = x <= 0.0031308f ? 12.92f * x
            : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
        return static_cast<uint8_t>(std::lround(encoded * 255.0f));
    };
    while (mips.back().width > 1 || mips.back().height > 1) {
        const MipData& prev = mips.back();
        MipData next{std::max(1u, prev.width / 2), std::max(1u, prev.height / 2), {}};
        next.rgba.resize(static_cast<size_t>(next.width) * next.height * 4);
        for (uint32_t y = 0; y < next.height; ++y) {
            for (uint32_t x = 0; x < next.width; ++x) {
                float rgb[3] = {0, 0, 0};
                unsigned samples = 0;
                for (uint32_t oy = 0; oy < 2; ++oy) {
                    for (uint32_t ox = 0; ox < 2; ++ox) {
                        const uint32_t sx = std::min(prev.width - 1, x*2 + ox);
                        const uint32_t sy = std::min(prev.height - 1, y*2 + oy);
                        const size_t index = (static_cast<size_t>(sy) * prev.width + sx) * 4;
                        for (int c = 0; c < 3; ++c) rgb[c] += srgbToLinear(prev.rgba[index+c]);
                        ++samples;
                    }
                }
                const size_t dst = (static_cast<size_t>(y) * next.width + x) * 4;
                for (int c = 0; c < 3; ++c)
                    next.rgba[dst+c] = linearToSrgb(rgb[c] / samples);
                next.rgba[dst+3] = 255;
            }
        }
        mips.push_back(std::move(next));
    }
    im.environmentTexture.mipLevels = static_cast<uint32_t>(mips.size());

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.extent = {mips[0].width, mips[0].height, 1};
    imageInfo.mipLevels = im.environmentTexture.mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!checkVk(vkCreateImage(im.device, &imageInfo, nullptr,
                               &im.environmentTexture.image), "environment image")) return;

    VkMemoryRequirements imageReq{};
    vkGetImageMemoryRequirements(im.device, im.environmentTexture.image, &imageReq);
    VkPhysicalDeviceMemoryProperties memoryProps{};
    vkGetPhysicalDeviceMemoryProperties(im.physicalDevice, &memoryProps);
    uint32_t memoryType = UINT32_MAX;
    for (uint32_t i = 0; i < memoryProps.memoryTypeCount; ++i) {
        if ((imageReq.memoryTypeBits & (1u << i)) &&
            (memoryProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memoryType = i;
            break;
        }
    }
    if (memoryType == UINT32_MAX) {
        logError("Vulkan: no device-local memory for environment texture");
        vkDestroyImage(im.device, im.environmentTexture.image, nullptr);
        im.environmentTexture.image = VK_NULL_HANDLE;
        return;
    }
    VkMemoryAllocateInfo imageAlloc{};
    imageAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imageAlloc.allocationSize = imageReq.size;
    imageAlloc.memoryTypeIndex = memoryType;
    if (!checkVk(vkAllocateMemory(im.device, &imageAlloc, nullptr,
                                  &im.environmentTexture.memory), "environment image memory")) return;
    if (!checkVk(vkBindImageMemory(im.device, im.environmentTexture.image,
                                   im.environmentTexture.memory, 0), "bind environment image")) return;

    VkDeviceSize totalBytes = 0;
    std::vector<VkBufferImageCopy> regions;
    regions.reserve(mips.size());
    for (uint32_t level = 0; level < mips.size(); ++level) {
        VkBufferImageCopy region{};
        region.bufferOffset = totalBytes;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = level;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {mips[level].width, mips[level].height, 1};
        regions.push_back(region);
        totalBytes += mips[level].rgba.size(); // RGBA levels are 4-byte aligned
    }

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = totalBytes;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!checkVk(vkCreateBuffer(im.device, &stagingInfo, nullptr, &staging),
                 "environment staging buffer")) return;
    VkMemoryRequirements stagingReq{};
    vkGetBufferMemoryRequirements(im.device, staging, &stagingReq);
    uint32_t hostType = UINT32_MAX;
    for (uint32_t i = 0; i < memoryProps.memoryTypeCount; ++i) {
        const VkMemoryPropertyFlags flags = memoryProps.memoryTypes[i].propertyFlags;
        if ((stagingReq.memoryTypeBits & (1u << i)) &&
            (flags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            hostType = i;
            break;
        }
    }
    if (hostType == UINT32_MAX) {
        logError("Vulkan: no coherent host memory for environment upload");
        vkDestroyBuffer(im.device, staging, nullptr);
        return;
    }
    VkMemoryAllocateInfo stagingAlloc{};
    stagingAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingAlloc.allocationSize = stagingReq.size;
    stagingAlloc.memoryTypeIndex = hostType;
    if (!checkVk(vkAllocateMemory(im.device, &stagingAlloc, nullptr, &stagingMemory),
                 "environment staging memory")) {
        vkDestroyBuffer(im.device, staging, nullptr);
        return;
    }
    if (!checkVk(vkBindBufferMemory(im.device, staging, stagingMemory, 0),
                 "bind environment staging")) {
        vkFreeMemory(im.device, stagingMemory, nullptr);
        vkDestroyBuffer(im.device, staging, nullptr);
        return;
    }
    void* mapped = nullptr;
    if (!checkVk(vkMapMemory(im.device, stagingMemory, 0, totalBytes, 0, &mapped),
                 "map environment staging")) {
        vkFreeMemory(im.device, stagingMemory, nullptr);
        vkDestroyBuffer(im.device, staging, nullptr);
        return;
    }
    size_t cursor = 0;
    for (const MipData& mip : mips) {
        std::memcpy(static_cast<char*>(mapped) + cursor, mip.rgba.data(), mip.rgba.size());
        cursor += mip.rgba.size();
    }
    vkUnmapMemory(im.device, stagingMemory);

    VkCommandBuffer command = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo commandAlloc{};
    commandAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandAlloc.commandPool = im.commandPool;
    commandAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandAlloc.commandBufferCount = 1;
    bool ok = checkVk(vkAllocateCommandBuffers(im.device, &commandAlloc, &command),
                      "allocate environment upload command");
    if (ok) {
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ok = checkVk(vkBeginCommandBuffer(command, &begin), "begin environment upload");
    }
    if (ok) {
        VkImageMemoryBarrier toTransfer{};
        toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransfer.srcAccessMask = 0;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = im.environmentTexture.image;
        toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0,
                                       im.environmentTexture.mipLevels, 0, 1};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &toTransfer);
        vkCmdCopyBufferToImage(command, staging, im.environmentTexture.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<uint32_t>(regions.size()), regions.data());
        VkImageMemoryBarrier toSample{};
        toSample.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toSample.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toSample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toSample.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toSample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toSample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSample.image = im.environmentTexture.image;
        toSample.subresourceRange = toTransfer.subresourceRange;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &toSample);
        ok = checkVk(vkEndCommandBuffer(command), "end environment upload");
    }
    if (ok) {
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        ok = checkVk(vkQueueSubmit(im.graphicsQueue, 1, &submit, VK_NULL_HANDLE),
                     "submit environment upload");
        if (ok) ok = checkVk(vkQueueWaitIdle(im.graphicsQueue), "wait environment upload");
    }
    if (command) vkFreeCommandBuffers(im.device, im.commandPool, 1, &command);
    vkDestroyBuffer(im.device, staging, nullptr);
    vkFreeMemory(im.device, stagingMemory, nullptr);
    if (!ok) return;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = im.environmentTexture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0,
                                 im.environmentTexture.mipLevels, 0, 1};
    if (!checkVk(vkCreateImageView(im.device, &viewInfo, nullptr,
                                   &im.environmentTexture.view), "environment image view")) return;
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(im.environmentTexture.mipLevels - 1);
    if (!checkVk(vkCreateSampler(im.device, &samplerInfo, nullptr,
                                 &im.environmentTexture.sampler), "environment sampler")) return;
    im.environmentPath = mEnv.skyboxTexture;
    logInfo("Vulkan: environment texture ready (" + std::to_string(source.width) + "x" +
            std::to_string(source.height) + ", " + std::to_string(mips.size()) + " mips)");
}

bool VulkanRenderer::ensureMaterialTexture(const std::string& path) {
    Impl& im = *mImpl;
    if (path.empty() || !im.device) return false;
    if (im.materialTextures.find(path) != im.materialTextures.end()) return true;

    ImageData decoded;
    if (!loadImage(path, decoded)) {
        logWarn("Vulkan: cannot decode material texture: " + path);
        return false;
    }
    VkPhysicalDeviceProperties deviceProperties{};
    vkGetPhysicalDeviceProperties(im.physicalDevice, &deviceProperties);
    if (decoded.width > static_cast<int>(deviceProperties.limits.maxImageDimension2D) ||
        decoded.height > static_cast<int>(deviceProperties.limits.maxImageDimension2D)) {
        logWarn("Vulkan: material texture exceeds device limits: " + path);
        return false;
    }

    const size_t pixelCount = static_cast<size_t>(decoded.width) * decoded.height;
    std::vector<uint8_t> rgba(pixelCount * 4);
    for (int y = 0; y < decoded.height; ++y) {
        const size_t sourceY = static_cast<size_t>(decoded.height - 1 - y);
        for (int x = 0; x < decoded.width; ++x) {
            const size_t source = (sourceY * static_cast<size_t>(decoded.width) + x);
            const size_t target = (static_cast<size_t>(y) * decoded.width + x) * 4;
            if (!decoded.rgba.empty()) {
                std::memcpy(rgba.data() + target, decoded.rgba.data() + source * 4, 4);
            } else {
                std::memcpy(rgba.data() + target, decoded.rgb.data() + source * 3, 3);
                rgba[target + 3] = 255;
            }
        }
    }

    Impl::MaterialTexture texture;
    auto destroyTexture = [&]() {
        if (texture.sampler) vkDestroySampler(im.device, texture.sampler, nullptr);
        if (texture.view) vkDestroyImageView(im.device, texture.view, nullptr);
        if (texture.image) vkDestroyImage(im.device, texture.image, nullptr);
        if (texture.memory) vkFreeMemory(im.device, texture.memory, nullptr);
        texture = Impl::MaterialTexture{};
    };

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.extent = {static_cast<uint32_t>(decoded.width), static_cast<uint32_t>(decoded.height), 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!checkVk(vkCreateImage(im.device, &imageInfo, nullptr, &texture.image),
                 "create material image")) return false;

    VkMemoryRequirements imageRequirements{};
    vkGetImageMemoryRequirements(im.device, texture.image, &imageRequirements);
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(im.physicalDevice, &memoryProperties);
    uint32_t deviceMemoryType = UINT32_MAX;
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((imageRequirements.memoryTypeBits & (1u << i)) &&
            (memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            deviceMemoryType = i;
            break;
        }
    }
    if (deviceMemoryType == UINT32_MAX) {
        logWarn("Vulkan: no device-local memory for material image");
        destroyTexture();
        return false;
    }
    VkMemoryAllocateInfo imageAllocation{};
    imageAllocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imageAllocation.allocationSize = imageRequirements.size;
    imageAllocation.memoryTypeIndex = deviceMemoryType;
    if (!checkVk(vkAllocateMemory(im.device, &imageAllocation, nullptr, &texture.memory),
                 "allocate material image memory") ||
        !checkVk(vkBindImageMemory(im.device, texture.image, texture.memory, 0),
                 "bind material image memory")) {
        destroyTexture();
        return false;
    }

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    auto cleanupUpload = [&]() {
        if (command) vkFreeCommandBuffers(im.device, im.commandPool, 1, &command);
        if (staging) vkDestroyBuffer(im.device, staging, nullptr);
        if (stagingMemory) vkFreeMemory(im.device, stagingMemory, nullptr);
    };
    VkBufferCreateInfo stagingInfo{};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = rgba.size();
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!checkVk(vkCreateBuffer(im.device, &stagingInfo, nullptr, &staging),
                 "create material staging buffer")) {
        cleanupUpload(); destroyTexture(); return false;
    }
    VkMemoryRequirements stagingRequirements{};
    vkGetBufferMemoryRequirements(im.device, staging, &stagingRequirements);
    uint32_t hostMemoryType = UINT32_MAX;
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        const VkMemoryPropertyFlags flags = memoryProperties.memoryTypes[i].propertyFlags;
        if ((stagingRequirements.memoryTypeBits & (1u << i)) &&
            (flags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            hostMemoryType = i;
            break;
        }
    }
    if (hostMemoryType == UINT32_MAX) {
        logWarn("Vulkan: no coherent host memory for material upload");
        cleanupUpload(); destroyTexture(); return false;
    }
    VkMemoryAllocateInfo stagingAllocation{};
    stagingAllocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingAllocation.allocationSize = stagingRequirements.size;
    stagingAllocation.memoryTypeIndex = hostMemoryType;
    if (!checkVk(vkAllocateMemory(im.device, &stagingAllocation, nullptr, &stagingMemory),
                 "allocate material staging memory") ||
        !checkVk(vkBindBufferMemory(im.device, staging, stagingMemory, 0),
                 "bind material staging memory")) {
        cleanupUpload(); destroyTexture(); return false;
    }
    void* mapped = nullptr;
    if (!checkVk(vkMapMemory(im.device, stagingMemory, 0, rgba.size(), 0, &mapped),
                 "map material staging buffer")) {
        cleanupUpload(); destroyTexture(); return false;
    }
    std::memcpy(mapped, rgba.data(), rgba.size());
    vkUnmapMemory(im.device, stagingMemory);

    VkCommandBufferAllocateInfo commandAllocation{};
    commandAllocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandAllocation.commandPool = im.commandPool;
    commandAllocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandAllocation.commandBufferCount = 1;
    bool ok = checkVk(vkAllocateCommandBuffers(im.device, &commandAllocation, &command),
                      "allocate material upload command");
    if (ok) {
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ok = checkVk(vkBeginCommandBuffer(command, &begin), "begin material upload");
    }
    if (ok) {
        VkImageMemoryBarrier toTransfer{};
        toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = texture.image;
        toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &toTransfer);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = imageInfo.extent;
        vkCmdCopyBufferToImage(command, staging, texture.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        VkImageMemoryBarrier toShader{};
        toShader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShader.image = texture.image;
        toShader.subresourceRange = toTransfer.subresourceRange;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &toShader);
        ok = checkVk(vkEndCommandBuffer(command), "end material upload");
    }
    if (ok) {
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        ok = checkVk(vkQueueSubmit(im.graphicsQueue, 1, &submit, VK_NULL_HANDLE),
                     "submit material upload") &&
             checkVk(vkQueueWaitIdle(im.graphicsQueue), "wait for material upload");
    }
    cleanupUpload();
    if (!ok) { destroyTexture(); return false; }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = texture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (!checkVk(vkCreateImageView(im.device, &viewInfo, nullptr, &texture.view),
                 "create material image view")) {
        destroyTexture(); return false;
    }
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.maxLod = 0.0f;
    if (!checkVk(vkCreateSampler(im.device, &samplerInfo, nullptr, &texture.sampler),
                 "create material sampler")) {
        destroyTexture(); return false;
    }
    im.materialTextures.emplace(path, texture);
    logInfo("Vulkan: uploaded material texture " + path + " (" +
            std::to_string(decoded.width) + "x" + std::to_string(decoded.height) + ")");
    return true;
}

void VulkanRenderer::setEnvironment(const EnvironmentSettings& env) {
    if (env.skyboxTexture != mEnv.skyboxTexture) mImpl->environmentDirty = true;
    mEnv = env;
}
void VulkanRenderer::setLights(const std::vector<RenderLight>& lights) { mLights = lights; }
void VulkanRenderer::setCameraPosition(const Vec3& position) { mCameraPosition = position; }

void VulkanRenderer::beginFrame(const Mat4& view, const Mat4& projection) {
    mStats = RenderStats{};
    Impl& im = *mImpl;
    im.view = view;
    im.projection = projection;
    if (!im.hasSwapchain) return;

    vkWaitForFences(im.device, 1, &im.inFlight, VK_TRUE, UINT64_MAX);
    // Previous frame is complete now; its transient per-draw buffers are safe to release.
    for (const auto& r : im.frameResources) {
        if (r.vertexBuffer) vkDestroyBuffer(im.device, r.vertexBuffer, nullptr);
        if (r.vertexMemory) vkFreeMemory(im.device, r.vertexMemory, nullptr);
        if (r.indexBuffer) vkDestroyBuffer(im.device, r.indexBuffer, nullptr);
        if (r.indexMemory) vkFreeMemory(im.device, r.indexMemory, nullptr);
        if (r.uniformBuffer) vkDestroyBuffer(im.device, r.uniformBuffer, nullptr);
        if (r.uniformMemory) vkFreeMemory(im.device, r.uniformMemory, nullptr);
    }
    im.frameResources.clear();
    updateEnvironmentTexture();
    if (im.descriptorPool) vkDestroyDescriptorPool(im.device, im.descriptorPool, nullptr);
    VkDescriptorPoolSize framePoolSizes[2]{};
    framePoolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    framePoolSizes[0].descriptorCount = 4096;
    framePoolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    framePoolSizes[1].descriptorCount = 8192;
    VkDescriptorPoolCreateInfo framePoolInfo{};
    framePoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    framePoolInfo.maxSets = 4096;
    framePoolInfo.poolSizeCount = 2;
    framePoolInfo.pPoolSizes = framePoolSizes;
    if (!checkVk(vkCreateDescriptorPool(im.device, &framePoolInfo, nullptr,
                                        &im.descriptorPool), "frame descriptor pool")) return;

    VkResult acquire = vkAcquireNextImageKHR(
        im.device, im.swapchain, UINT64_MAX, im.imageAvailable, VK_NULL_HANDLE,
        &im.currentImage);
    if (acquire != VK_SUCCESS) return;
    vkResetFences(im.device, 1, &im.inFlight);

    VkCommandBuffer cmd = im.commandBuffers[im.currentImage];
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkResetCommandBuffer(cmd, 0);
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkClearValue clear[2]{};
    clear[0].color = {{0.05f, 0.07f, 0.10f, 1.0f}};
    clear[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = im.renderPass;
    rpInfo.framebuffer = im.framebuffers[im.currentImage];
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = im.swapchainExtent;
    rpInfo.clearValueCount = 2;
    rpInfo.pClearValues = clear;
    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    im.frameRecording = true;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, im.pipeline);
    VkViewport viewport{};
    viewport.width = static_cast<float>(im.swapchainExtent.width);
    viewport.height = static_cast<float>(im.swapchainExtent.height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{};
    scissor.extent = im.swapchainExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void VulkanRenderer::drawMesh(const MeshData& mesh, const Mat4& model,
                              const MaterialData& material) {
    Impl& im = *mImpl;
    if (!im.hasSwapchain || !im.frameRecording || mesh.positions.empty() || mesh.indices.empty()) return;

    auto findMemoryType = [&](uint32_t bits, VkMemoryPropertyFlags required,
                              uint32_t& selected) -> bool {
        VkPhysicalDeviceMemoryProperties props{};
        vkGetPhysicalDeviceMemoryProperties(im.physicalDevice, &props);
        for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
            if ((bits & (1u << i)) &&
                (props.memoryTypes[i].propertyFlags & required) == required) {
                selected = i;
                return true;
            }
        }
        return false;
    };
    auto createBuffer = [&](VkDeviceSize size, VkBufferUsageFlags usage,
                            VkMemoryPropertyFlags properties,
                            VkBuffer& buffer, VkDeviceMemory& memory) -> bool {
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (!checkVk(vkCreateBuffer(im.device, &info, nullptr, &buffer), "vkCreateBuffer"))
            return false;
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(im.device, buffer, &req);
        uint32_t type = 0;
        if (!findMemoryType(req.memoryTypeBits, properties, type)) {
            logError("Vulkan: no compatible memory type for buffer");
            vkDestroyBuffer(im.device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            return false;
        }
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = type;
        if (!checkVk(vkAllocateMemory(im.device, &alloc, nullptr, &memory), "vkAllocateMemory")) {
            vkDestroyBuffer(im.device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            return false;
        }
        if (!checkVk(vkBindBufferMemory(im.device, buffer, memory, 0), "vkBindBufferMemory")) {
            vkFreeMemory(im.device, memory, nullptr);
            vkDestroyBuffer(im.device, buffer, nullptr);
            memory = VK_NULL_HANDLE;
            buffer = VK_NULL_HANDLE;
            return false;
        }
        return true;
    };
    auto uploadMeshBuffers = [&](const float* vertexData, VkDeviceSize vertexSize,
                                 const u32* indexData, VkDeviceSize indexSize,
                                 Impl::GpuMesh& output) -> bool {
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        const VkDeviceSize totalSize = vertexSize + indexSize;
        const VkMemoryPropertyFlags hostFlags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if (!createBuffer(totalSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          hostFlags, staging, stagingMemory)) return false;
        void* mapped = nullptr;
        if (!checkVk(vkMapMemory(im.device, stagingMemory, 0, totalSize, 0, &mapped),
                     "vkMapMemory(mesh staging)")) {
            vkDestroyBuffer(im.device, staging, nullptr);
            vkFreeMemory(im.device, stagingMemory, nullptr);
            return false;
        }
        std::memcpy(mapped, vertexData, static_cast<size_t>(vertexSize));
        std::memcpy(static_cast<char*>(mapped) + vertexSize,
                    indexData, static_cast<size_t>(indexSize));
        vkUnmapMemory(im.device, stagingMemory);

        bool ready = createBuffer(vertexSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, output.vertexBuffer, output.vertexMemory);
        ready = ready && createBuffer(indexSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, output.indexBuffer, output.indexMemory);
        if (!ready) {
            vkDestroyBuffer(im.device, staging, nullptr);
            vkFreeMemory(im.device, stagingMemory, nullptr);
            if (output.vertexBuffer) vkDestroyBuffer(im.device, output.vertexBuffer, nullptr);
            if (output.vertexMemory) vkFreeMemory(im.device, output.vertexMemory, nullptr);
            if (output.indexBuffer) vkDestroyBuffer(im.device, output.indexBuffer, nullptr);
            if (output.indexMemory) vkFreeMemory(im.device, output.indexMemory, nullptr);
            output = Impl::GpuMesh{};
            return false;
        }

        VkCommandBuffer transfer = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.commandPool = im.commandPool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        if (!checkVk(vkAllocateCommandBuffers(im.device, &alloc, &transfer),
                     "vkAllocateCommandBuffers(mesh upload)")) ready = false;
        if (ready) {
            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            ready = checkVk(vkBeginCommandBuffer(transfer, &begin),
                            "vkBeginCommandBuffer(mesh upload)");
        }
        if (ready) {
            VkBufferCopy copies[2]{};
            copies[0].srcOffset = 0;
            copies[0].dstOffset = 0;
            copies[0].size = vertexSize;
            copies[1].srcOffset = vertexSize;
            copies[1].dstOffset = 0;
            copies[1].size = indexSize;
            vkCmdCopyBuffer(transfer, staging, output.vertexBuffer, 1, &copies[0]);
            vkCmdCopyBuffer(transfer, staging, output.indexBuffer, 1, &copies[1]);
            VkBufferMemoryBarrier barriers[2]{};
            for (auto& barrier : barriers) {
                barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.offset = 0;
                barrier.size = VK_WHOLE_SIZE;
            }
            barriers[0].dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            barriers[0].buffer = output.vertexBuffer;
            barriers[1].dstAccessMask = VK_ACCESS_INDEX_READ_BIT;
            barriers[1].buffer = output.indexBuffer;
            vkCmdPipelineBarrier(transfer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0,
                                 0, nullptr, 2, barriers, 0, nullptr);
            ready = checkVk(vkEndCommandBuffer(transfer), "vkEndCommandBuffer(mesh upload)");
        }
        if (ready) {
            VkSubmitInfo submit{};
            submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &transfer;
            ready = checkVk(vkQueueSubmit(im.graphicsQueue, 1, &submit, VK_NULL_HANDLE),
                            "vkQueueSubmit(mesh upload)");
            if (ready)
                ready = checkVk(vkQueueWaitIdle(im.graphicsQueue), "vkQueueWaitIdle(mesh upload)");
        }
        if (transfer) vkFreeCommandBuffers(im.device, im.commandPool, 1, &transfer);
        vkDestroyBuffer(im.device, staging, nullptr);
        vkFreeMemory(im.device, stagingMemory, nullptr);
        if (!ready) {
            if (output.vertexBuffer) vkDestroyBuffer(im.device, output.vertexBuffer, nullptr);
            if (output.vertexMemory) vkFreeMemory(im.device, output.vertexMemory, nullptr);
            if (output.indexBuffer) vkDestroyBuffer(im.device, output.indexBuffer, nullptr);
            if (output.indexMemory) vkFreeMemory(im.device, output.indexMemory, nullptr);
            output = Impl::GpuMesh{};
            return false;
        }
        return true;
    };

    // Cache is keyed by AssetManager's stable ID. Vertex/index data lives
    // in device-local memory after a one-time staging upload.
    Impl::GpuMesh transientMesh;
    Impl::GpuMesh* gpuMesh = nullptr;
    auto cached = mesh.cacheKey == kNullAsset ? im.gpuMeshes.end()
                                               : im.gpuMeshes.find(mesh.cacheKey);
    if (cached != im.gpuMeshes.end()) {
        gpuMesh = &cached->second;
    } else {
        const size_t vertexCount = mesh.positions.size() / 3;
        std::vector<float> interleaved(vertexCount * 8, 0.0f);
        for (size_t i = 0; i < vertexCount; ++i) {
            interleaved[i*8+0] = mesh.positions[i*3+0];
            interleaved[i*8+1] = mesh.positions[i*3+1];
            interleaved[i*8+2] = mesh.positions[i*3+2];
            if (mesh.normals.size() >= (i+1)*3) {
                interleaved[i*8+3] = mesh.normals[i*3+0];
                interleaved[i*8+4] = mesh.normals[i*3+1];
                interleaved[i*8+5] = mesh.normals[i*3+2];
            } else {
                interleaved[i*8+4] = 1.0f;
            }
            if (mesh.uvs.size() >= (i+1)*2) {
                interleaved[i*8+6] = mesh.uvs[i*2+0];
                interleaved[i*8+7] = mesh.uvs[i*2+1];
            }
        }
        Impl::GpuMesh uploaded;
        uploaded.indexCount = static_cast<uint32_t>(mesh.indices.size());
        const bool uploadedOk = uploadMeshBuffers(
            interleaved.data(), interleaved.size() * sizeof(float),
            mesh.indices.data(), mesh.indices.size() * sizeof(u32), uploaded);
        if (!uploadedOk) return;
        if (mesh.cacheKey != kNullAsset) {
            auto inserted = im.gpuMeshes.emplace(mesh.cacheKey, uploaded);
            gpuMesh = &inserted.first->second;
        } else {
            transientMesh = uploaded;
            gpuMesh = &transientMesh;
        }
    }

    // Per-draw uniforms are small and remain host-visible; mesh buffers are
    // immutable and are not rewritten while rendering.
    Impl::DrawResources resources;
    auto releaseTransientMesh = [&]() {
        if (gpuMesh != &transientMesh) return;
        if (transientMesh.vertexBuffer) vkDestroyBuffer(im.device, transientMesh.vertexBuffer, nullptr);
        if (transientMesh.vertexMemory) vkFreeMemory(im.device, transientMesh.vertexMemory, nullptr);
        if (transientMesh.indexBuffer) vkDestroyBuffer(im.device, transientMesh.indexBuffer, nullptr);
        if (transientMesh.indexMemory) vkFreeMemory(im.device, transientMesh.indexMemory, nullptr);
        transientMesh = Impl::GpuMesh{};
    };
    auto releaseUniform = [&]() {
        if (resources.uniformBuffer) vkDestroyBuffer(im.device, resources.uniformBuffer, nullptr);
        if (resources.uniformMemory) vkFreeMemory(im.device, resources.uniformMemory, nullptr);
        resources.uniformBuffer = VK_NULL_HANDLE;
        resources.uniformMemory = VK_NULL_HANDLE;
    };
    if (!createBuffer(sizeof(UboData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      resources.uniformBuffer, resources.uniformMemory)) {
        releaseTransientMesh();
        return;
    }
    const bool hasBaseColorTexture = ensureMaterialTexture(material.baseColorTexture);
    UboData ubo{};
    std::memcpy(ubo.model, model.m, sizeof(ubo.model));
    std::memcpy(ubo.view, im.view.m, sizeof(ubo.view));
    std::memcpy(ubo.projection, im.projection.m, sizeof(ubo.projection));
    std::memcpy(ubo.color, material.baseColor, sizeof(ubo.color));
    ubo.ambient = mEnv.ambientIntensity * 0.55f;
    ubo.exposure = mEnv.skyboxExposure + 0.4f;
    ubo.metallic = material.metallic;
    ubo.roughness = material.roughness;
    ubo.reflectionIntensity = mEnv.reflectionIntensity;
    Vec3 lightDirection{-0.4f, -1.0f, -0.3f};
    Vec3 lightColor{1.0f, 0.98f, 0.92f};
    float lightIntensity = 1.0f;
    for (const RenderLight& light : mLights) {
        if (light.type == LightType::Directional) {
            lightDirection = light.direction;
            lightColor = light.color;
            lightIntensity = light.intensity;
            break;
        }
    }
    ubo.lightDir[0] = lightDirection.x;
    ubo.lightDir[1] = lightDirection.y;
    ubo.lightDir[2] = lightDirection.z;
    ubo.lightColorIntensity[0] = lightColor.x;
    ubo.lightColorIntensity[1] = lightColor.y;
    ubo.lightColorIntensity[2] = lightColor.z;
    ubo.lightColorIntensity[3] = lightIntensity;
    ubo.environmentAvailable = im.environmentTexture.view ? 1.0f : 0.0f;
    ubo.environmentRotation = mEnv.skyboxRotation / 360.0f;
    ubo.cameraPosition[0] = mCameraPosition.x;
    ubo.cameraPosition[1] = mCameraPosition.y;
    ubo.cameraPosition[2] = mCameraPosition.z;
    ubo.hasBaseColorTexture = hasBaseColorTexture ? 1.0f : 0.0f;
    ubo.alphaMode = material.alphaMode == MaterialAlphaMode::Mask ? 1.0f
        : material.alphaMode == MaterialAlphaMode::Blend ? 2.0f : 0.0f;
    ubo.alphaCutoff = material.alphaCutoff;
    ubo.baseColorAlpha = material.baseColor[3];
    void* mapped = nullptr;
    if (!checkVk(vkMapMemory(im.device, resources.uniformMemory, 0, sizeof(UboData), 0, &mapped),
                 "vkMapMemory(ubo)")) {
        releaseUniform();
        releaseTransientMesh();
        return;
    }
    std::memcpy(mapped, &ubo, sizeof(UboData));
    vkUnmapMemory(im.device, resources.uniformMemory);

    VkDescriptorSetAllocateInfo setAlloc{};
    setAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAlloc.descriptorPool = im.descriptorPool;
    setAlloc.descriptorSetCount = 1;
    setAlloc.pSetLayouts = &im.descriptorLayout;
    if (!checkVk(vkAllocateDescriptorSets(im.device, &setAlloc, &resources.descriptorSet),
                 "draw descriptor set")) {
        releaseUniform();
        releaseTransientMesh();
        return;
    }
    VkDescriptorBufferInfo ubInfo{};
    ubInfo.buffer = resources.uniformBuffer;
    ubInfo.range = sizeof(UboData);
    VkDescriptorImageInfo envInfo{};
    envInfo.sampler = im.environmentTexture.sampler;
    envInfo.imageView = im.environmentTexture.view;
    envInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorImageInfo materialInfo = envInfo;
    if (hasBaseColorTexture) {
        const auto materialTexture = im.materialTextures.find(material.baseColorTexture);
        if (materialTexture != im.materialTextures.end()) {
            materialInfo.sampler = materialTexture->second.sampler;
            materialInfo.imageView = materialTexture->second.view;
        }
    }
    VkWriteDescriptorSet descriptorWrites[3]{};
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = resources.descriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].pBufferInfo = &ubInfo;
    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = resources.descriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[1].pImageInfo = &envInfo;
    descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[2].dstSet = resources.descriptorSet;
    descriptorWrites[2].dstBinding = 2;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[2].pImageInfo = &materialInfo;
    vkUpdateDescriptorSets(im.device, 3, descriptorWrites, 0, nullptr);

    VkCommandBuffer cmd = im.commandBuffers[im.currentImage];
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, im.pipelineLayout,
                            0, 1, &resources.descriptorSet, 0, nullptr);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &gpuMesh->vertexBuffer, &offset);
    vkCmdBindIndexBuffer(cmd, gpuMesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, gpuMesh->indexCount, 1, 0, 0, 0);

    if (gpuMesh == &transientMesh) {
        resources.vertexBuffer = transientMesh.vertexBuffer;
        resources.vertexMemory = transientMesh.vertexMemory;
        resources.indexBuffer = transientMesh.indexBuffer;
        resources.indexMemory = transientMesh.indexMemory;
    }
    im.frameResources.push_back(resources);
    mStats.drawCalls += 1;
    mStats.triangles += gpuMesh->indexCount / 3;
}

void VulkanRenderer::endFrame() {
    Impl& im = *mImpl;
    if (!im.hasSwapchain || !im.frameRecording) return;
    im.frameRecording = false;
    VkCommandBuffer cmd = im.commandBuffers[im.currentImage];
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &im.imageAvailable;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &im.renderFinished;
    vkQueueSubmit(im.graphicsQueue, 1, &submit, im.inFlight);

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &im.renderFinished;
    present.swapchainCount = 1;
    present.pSwapchains = &im.swapchain;
    present.pImageIndices = &im.currentImage;
    vkQueuePresentKHR(im.graphicsQueue, &present);
}

} // namespace mini

#else // !MINI_VULKAN — stub: init fails so the engine falls back.

namespace mini {

VulkanRenderer::VulkanRenderer() = default;
VulkanRenderer::~VulkanRenderer() = default;
void VulkanRenderer::setShaderDir(const std::string&) {}
bool VulkanRenderer::attachSurface(void*) { return false; }
bool VulkanRenderer::detachSurface() { return true; }

bool VulkanRenderer::init(int width, int height) {
    mWidth = width;
    mHeight = height;
    logWarn("VulkanRenderer: Vulkan SDK not found at build time "
            "(MINI_VULKAN undefined); falling back");
    return false;
}
void VulkanRenderer::shutdown() {}
void VulkanRenderer::resize(int width, int height) { mWidth = width; mHeight = height; }
void VulkanRenderer::updateEnvironmentTexture() {}
void VulkanRenderer::setEnvironment(const EnvironmentSettings& env) { mEnv = env; }
void VulkanRenderer::setLights(const std::vector<RenderLight>& lights) { mLights = lights; }
void VulkanRenderer::setCameraPosition(const Vec3& position) { mCameraPosition = position; }
void VulkanRenderer::beginFrame(const Mat4&, const Mat4&) { mStats = RenderStats{}; }
void VulkanRenderer::drawMesh(const MeshData& mesh, const Mat4&, const MaterialData&) {
    mStats.drawCalls += 1;
    mStats.triangles += static_cast<u32>(mesh.indices.size() / 3);
}
void VulkanRenderer::endFrame() {}

} // namespace mini

#endif
