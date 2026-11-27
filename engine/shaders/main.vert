#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(set = 0, binding = 0) uniform UBO {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec3 color;
    float ambient;
    float exposure;
    float metallic;
    float roughness;
    float reflectionIntensity;
    vec3 lightDir;
    float environmentAvailable;
    float environmentRotation;
    float cameraPositionX;
    float cameraPositionY;
    float cameraPositionZ;
    vec4 lightColorIntensity;
    float hasBaseColorTexture;
    float alphaMode;
    float alphaCutoff;
    float baseColorAlpha;
} ubo;
layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vColor;
layout(location = 2) out vec3 vWorldPosition;
layout(location = 3) out vec2 vUv;
void main() {
    vNormal = mat3(ubo.model) * inNormal;
    vColor = ubo.color;
    vec4 world = ubo.model * vec4(inPosition, 1.0);
    vWorldPosition = world.xyz;
    vUv = inUv;
    vec4 clip = ubo.projection * ubo.view * world;
    // Engine matrices use OpenGL clip conventions; adapt depth range and Y
    // for Vulkan's [0, 1] depth and framebuffer coordinates.
    gl_Position = vec4(clip.x, -clip.y, (clip.z + clip.w) * 0.5, clip.w);
}
