#include "mini/render/GLESRenderer.h"
#include "mini/assets/Image.h"
#include "mini/core/Log.h"

#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace mini {
namespace {

const char* kVertexShader = R"GLSL(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat4 uLightViewProjection;
out vec3 vWorldPosition;
out vec3 vWorldNormal;
out vec4 vShadowPosition;
out vec2 vUv;
void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorldPosition = world.xyz;
    vWorldNormal = normalize(transpose(inverse(mat3(uModel))) * aNormal);
    vShadowPosition = uLightViewProjection * world;
    vUv = aUv;
    gl_Position = uProjection * uView * world;
}
)GLSL";

const char* kFragmentShader = R"GLSL(#version 300 es
precision highp float;
in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec4 vShadowPosition;
in vec2 vUv;
uniform vec4 uColor;
uniform vec3 uCameraPosition;
uniform int uAlphaMode;
uniform float uAlphaCutoff;
uniform sampler2D uBaseColorTexture;
uniform float uHasBaseColorTexture;
const int MAX_LIGHTS = 4;
uniform int uLightCount;
uniform vec4 uLightPositionType[MAX_LIGHTS];
uniform vec4 uLightDirectionRange[MAX_LIGHTS];
uniform vec4 uLightColorIntensity[MAX_LIGHTS];
uniform vec2 uLightCone[MAX_LIGHTS];
uniform vec3 uAmbientColor;
uniform float uAmbient;
uniform float uExposure;
uniform float uMetallic;
uniform float uRoughness;
uniform sampler2D uShadowMap;
uniform float uShadowEnabled;
uniform sampler2D uEnvironmentMap;
uniform float uEnvironmentAvailable;
uniform float uEnvironmentMaxMip;
uniform float uReflectionIntensity;
uniform float uEnvironmentRotation;
out vec4 outColor;
const float PI = 3.14159265359;
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH = max(dot(N, H), 0.0);
    float nDotH2 = nDotH * nDotH;
    float denom = nDotH2 * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 0.0001);
}
float geometrySchlickGGX(float nDotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nDotV / max(nDotV * (1.0 - k) + k, 0.0001);
}
float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return geometrySchlickGGX(max(dot(N, V), 0.0), roughness) *
           geometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}
vec2 environmentUv(vec3 direction) {
    vec3 d = normalize(direction);
    float longitude = atan(d.z, d.x) / (2.0 * PI) + 0.5 + uEnvironmentRotation;
    float latitude = 0.5 - asin(clamp(d.y, -1.0, 1.0)) / PI;
    return vec2(fract(longitude), clamp(latitude, 0.0, 1.0));
}
float shadowFactor(vec3 N, vec3 L) {
    if (uShadowEnabled < 0.5 || vShadowPosition.w <= 0.0) return 0.0;
    vec3 projected = vShadowPosition.xyz / vShadowPosition.w;
    vec3 uvz = projected * 0.5 + 0.5;
    if (uvz.x < 0.0 || uvz.x > 1.0 || uvz.y < 0.0 || uvz.y > 1.0 || uvz.z > 1.0)
        return 0.0;
    float bias = max(0.0012 * (1.0 - dot(N, L)), 0.00035);
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float blocked = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float closest = texture(uShadowMap, uvz.xy + vec2(float(x), float(y)) * texel).r;
            blocked += (uvz.z - bias > closest) ? 1.0 : 0.0;
        }
    }
    return blocked / 9.0;
}
void main() {
    vec3 N = normalize(vWorldNormal);
    vec3 V = normalize(uCameraPosition - vWorldPosition);
    vec4 textureSample = uHasBaseColorTexture > 0.5
        ? texture(uBaseColorTexture, vUv) : vec4(1.0);
    vec3 albedo = max(uColor.rgb * textureSample.rgb, vec3(0.0));
    float materialAlpha = clamp(uColor.a * textureSample.a, 0.0, 1.0);
    if (uAlphaMode == 1 && materialAlpha < uAlphaCutoff) discard;
    float metallic = clamp(uMetallic, 0.0, 1.0);
    float roughness = clamp(uRoughness, 0.045, 1.0);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 direct = vec3(0.0);
    for (int i = 0; i < MAX_LIGHTS; ++i) {
        if (i >= uLightCount) break;
        int type = int(uLightPositionType[i].w + 0.5);
        vec3 L;
        float attenuation = 1.0;
        if (type == 0) {
            L = normalize(-uLightDirectionRange[i].xyz);
        } else {
            vec3 toLight = uLightPositionType[i].xyz - vWorldPosition;
            float distanceToLight = length(toLight);
            L = distanceToLight > 0.0001 ? toLight / distanceToLight : vec3(0.0, 1.0, 0.0);
            float rangeRatio = clamp(1.0 - distanceToLight / max(uLightDirectionRange[i].w, 0.01), 0.0, 1.0);
            attenuation = rangeRatio * rangeRatio / max(1.0 + distanceToLight * distanceToLight, 1.0);
            if (type == 2) {
                float cone = dot(normalize(uLightDirectionRange[i].xyz), -L);
                attenuation *= smoothstep(uLightCone[i].y, uLightCone[i].x, cone);
            }
        }
        vec3 H = normalize(V + L);
        float NDF = distributionGGX(N, H, roughness);
        float G = geometrySmith(N, V, L, roughness);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
        vec3 specular = (NDF * G * F) /
            max(4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0), 0.001);
        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
        float nDotL = max(dot(N, L), 0.0);
        float shadow = type == 0 ? shadowFactor(N, L) : 0.0;
        direct += (kD * albedo / PI + specular) * uLightColorIntensity[i].rgb *
                  (uLightColorIntensity[i].a * attenuation * nDotL) * (1.0 - shadow * 0.82);
    }
    vec3 ambientRadiance = uAmbientColor;
    vec3 environmentSpecular = vec3(0.0);
    if (uEnvironmentAvailable > 0.5) {
        ambientRadiance = textureLod(uEnvironmentMap,
            environmentUv(N), uEnvironmentMaxMip).rgb;
        vec3 reflected = reflect(-V, N);
        environmentSpecular = textureLod(uEnvironmentMap,
            environmentUv(reflected), roughness * uEnvironmentMaxMip).rgb * F;
    }
    vec3 ambient = ambientRadiance * albedo * uAmbient * (1.0 - metallic * 0.7);
    ambient += environmentSpecular * uReflectionIntensity;
    vec3 color = (ambient + direct) * uExposure;
    color = color / (color + vec3(1.0));
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    outColor = vec4(color, uAlphaMode == 2 ? materialAlpha : 1.0);
}
)GLSL";

const char* kSkyVertexShader = R"GLSL(#version 300 es
precision highp float;
out vec2 vUv;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

const char* kSkyFragmentShader = R"GLSL(#version 300 es
precision highp float;
in vec2 vUv;
uniform vec3 uSkyTint;
uniform float uSkyExposure;
uniform float uSkyRotation;
uniform sampler2D uSkyEnvironment;
uniform float uSkyEnvironmentAvailable;
out vec4 outColor;
void main() {
    float elevation = clamp(vUv.y, 0.0, 1.0);
    vec3 sky;
    if (uSkyEnvironmentAvailable > 0.5) {
        vec2 envUv = vec2(fract(vUv.x + uSkyRotation), 1.0 - vUv.y);
        sky = texture(uSkyEnvironment, envUv).rgb;
    } else {
        vec3 zenith = vec3(0.035, 0.075, 0.16);
        vec3 upperHaze = vec3(0.22, 0.31, 0.43);
        vec3 horizon = vec3(0.72, 0.30, 0.14);
        sky = mix(horizon, upperHaze, smoothstep(0.34, 0.72, elevation));
        sky = mix(sky, zenith, smoothstep(0.58, 1.0, elevation) * 0.78);
        float hazeDelta = (elevation - 0.49) * 8.0;
        float haze = exp(-hazeDelta * hazeDelta);
        sky += vec3(0.15, 0.065, 0.025) * haze;
        float sunX = 0.76 + 0.16 * sin(uSkyRotation * 6.2831853);
        float sunY = 0.47 + 0.08 * cos(uSkyRotation * 6.2831853);
        float sunDistance = distance(vUv, vec2(sunX, sunY));
        float glow = exp(-sunDistance * 18.0) * 0.48;
        float disc = 1.0 - smoothstep(0.018, 0.026, sunDistance);
        sky += vec3(1.0, 0.62, 0.29) * glow;
        sky = mix(sky, vec3(1.0, 0.78, 0.48), disc);
    }
    vec3 exposed = sky * uSkyTint * max(uSkyExposure, 0.0);
    exposed = exposed / (exposed + vec3(1.0));
    exposed = pow(max(exposed, vec3(0.0)), vec3(1.0 / 2.2));
    outColor = vec4(exposed, 1.0);
}
)GLSL";

const char* kShadowVertexShader = R"GLSL(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPosition;
uniform mat4 uModel;
uniform mat4 uLightViewProjection;
void main() {
    gl_Position = uLightViewProjection * uModel * vec4(aPosition, 1.0);
}
)GLSL";

const char* kShadowFragmentShader = R"GLSL(#version 300 es
precision highp float;
void main() { }
)GLSL";

GLuint gProgram = 0;
GLint gLocModel = -1, gLocView = -1, gLocProj = -1, gLocLightVP = -1;
GLint gLocColor = -1, gLocCameraPosition = -1;
GLint gLocAlphaMode = -1, gLocAlphaCutoff = -1;
GLint gLocBaseColorTexture = -1, gLocHasBaseColorTexture = -1;
GLint gLocLightCount = -1, gLocLightPositionType = -1;
GLint gLocLightDirectionRange = -1, gLocLightColorIntensity = -1, gLocLightCone = -1;
GLint gLocAmbientColor = -1, gLocAmbient = -1, gLocExposure = -1;
GLint gLocMetallic = -1, gLocRoughness = -1, gLocShadowMap = -1, gLocShadowEnabled = -1;
GLint gLocEnvironmentMap = -1, gLocEnvironmentAvailable = -1;
GLint gLocEnvironmentMaxMip = -1, gLocReflectionIntensity = -1, gLocEnvironmentRotation = -1;
GLint gShadowLocModel = -1, gShadowLocLightVP = -1;
GLint gSkyLocTint = -1, gSkyLocExposure = -1, gSkyLocRotation = -1;
GLint gSkyLocEnvironment = -1, gSkyLocEnvironmentAvailable = -1;

GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE) return shader;
    char message[1024] = {};
    glGetShaderInfoLog(shader, sizeof(message), nullptr, message);
    logError(std::string("GLES shader compile failed: ") + message);
    glDeleteShader(shader);
    return 0;
}

GLuint createProgram(const char* vertexSource, const char* fragmentSource) {
    GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexSource);
    GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!vertex || !fragment) {
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        return 0;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_TRUE) return program;
    char message[1024] = {};
    glGetProgramInfoLog(program, sizeof(message), nullptr, message);
    logError(std::string("GLES program link failed: ") + message);
    glDeleteProgram(program);
    return 0;
}

} // namespace

bool GLESRenderer::init(int width, int height) {
    mWidth = width;
    mHeight = height;
    clearGpuCache();
    clearGpuTextures();
    if (mShadowFramebuffer) glDeleteFramebuffers(1, &mShadowFramebuffer);
    if (mShadowTexture) glDeleteTextures(1, &mShadowTexture);
    if (mEnvironmentTexture) glDeleteTextures(1, &mEnvironmentTexture);
    mShadowFramebuffer = 0;
    mShadowTexture = 0;
    mEnvironmentTexture = 0;
    mEnvironmentTexturePath.clear();
    mEnvironmentMipLevels = 1;
    mEnvironmentTextureDirty = true;
    mShadowAvailable = false;

    if (gProgram) glDeleteProgram(gProgram);
    gProgram = createProgram(kVertexShader, kFragmentShader);
    if (!gProgram) return false;
    if (mShadowProgram) glDeleteProgram(mShadowProgram);
    mShadowProgram = createProgram(kShadowVertexShader, kShadowFragmentShader);
    if (mSkyProgram) glDeleteProgram(mSkyProgram);
    mSkyProgram = createProgram(kSkyVertexShader, kSkyFragmentShader);

    gLocModel = glGetUniformLocation(gProgram, "uModel");
    gLocView = glGetUniformLocation(gProgram, "uView");
    gLocProj = glGetUniformLocation(gProgram, "uProjection");
    gLocLightVP = glGetUniformLocation(gProgram, "uLightViewProjection");
    gLocColor = glGetUniformLocation(gProgram, "uColor");
    gLocCameraPosition = glGetUniformLocation(gProgram, "uCameraPosition");
    gLocAlphaMode = glGetUniformLocation(gProgram, "uAlphaMode");
    gLocAlphaCutoff = glGetUniformLocation(gProgram, "uAlphaCutoff");
    gLocBaseColorTexture = glGetUniformLocation(gProgram, "uBaseColorTexture");
    gLocHasBaseColorTexture = glGetUniformLocation(gProgram, "uHasBaseColorTexture");
    gLocLightCount = glGetUniformLocation(gProgram, "uLightCount");
    gLocLightPositionType = glGetUniformLocation(gProgram, "uLightPositionType[0]");
    gLocLightDirectionRange = glGetUniformLocation(gProgram, "uLightDirectionRange[0]");
    gLocLightColorIntensity = glGetUniformLocation(gProgram, "uLightColorIntensity[0]");
    gLocLightCone = glGetUniformLocation(gProgram, "uLightCone[0]");
    gLocAmbientColor = glGetUniformLocation(gProgram, "uAmbientColor");
    gLocAmbient = glGetUniformLocation(gProgram, "uAmbient");
    gLocExposure = glGetUniformLocation(gProgram, "uExposure");
    gLocMetallic = glGetUniformLocation(gProgram, "uMetallic");
    gLocRoughness = glGetUniformLocation(gProgram, "uRoughness");
    gLocShadowMap = glGetUniformLocation(gProgram, "uShadowMap");
    gLocShadowEnabled = glGetUniformLocation(gProgram, "uShadowEnabled");
    gLocEnvironmentMap = glGetUniformLocation(gProgram, "uEnvironmentMap");
    gLocEnvironmentAvailable = glGetUniformLocation(gProgram, "uEnvironmentAvailable");
    gLocEnvironmentMaxMip = glGetUniformLocation(gProgram, "uEnvironmentMaxMip");
    gLocReflectionIntensity = glGetUniformLocation(gProgram, "uReflectionIntensity");
    gLocEnvironmentRotation = glGetUniformLocation(gProgram, "uEnvironmentRotation");
    if (mShadowProgram) {
        gShadowLocModel = glGetUniformLocation(mShadowProgram, "uModel");
        gShadowLocLightVP = glGetUniformLocation(mShadowProgram, "uLightViewProjection");
    }
    if (mSkyProgram) {
        gSkyLocTint = glGetUniformLocation(mSkyProgram, "uSkyTint");
        gSkyLocExposure = glGetUniformLocation(mSkyProgram, "uSkyExposure");
        gSkyLocRotation = glGetUniformLocation(mSkyProgram, "uSkyRotation");
        gSkyLocEnvironment = glGetUniformLocation(mSkyProgram, "uSkyEnvironment");
        gSkyLocEnvironmentAvailable = glGetUniformLocation(mSkyProgram, "uSkyEnvironmentAvailable");
    }

    constexpr GLsizei shadowSize = 1024;
    glGenTextures(1, &mShadowTexture);
    glBindTexture(GL_TEXTURE_2D, mShadowTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                 shadowSize, shadowSize, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &mShadowFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, mShadowFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, mShadowTexture, 0);
    const GLenum noColor[] = {GL_NONE};
    glDrawBuffers(1, noColor);
    glReadBuffer(GL_NONE);
    mShadowAvailable = mShadowProgram != 0 &&
        glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (!mShadowAvailable)
        logWarn("GLESRenderer: shadow map unavailable; rendering without shadows");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glViewport(0, 0, width, height);
    logInfo("GLESRenderer initialized (PBR lighting, GPU cache, directional shadows)");
    return true;
}

void GLESRenderer::clearGpuCache() {
    for (const DrawCommand& command : mDrawCommands) {
        if (!command.transient) continue;
        GLuint vao = command.mesh.vao;
        GLuint buffers[2] = {command.mesh.vertexBuffer, command.mesh.indexBuffer};
        glDeleteBuffers(2, buffers);
        if (vao) glDeleteVertexArrays(1, &vao);
    }
    mDrawCommands.clear();
    for (const auto& entry : mGpuMeshes) {
        const GpuMesh& mesh = entry.second;
        GLuint vao = mesh.vao;
        GLuint buffers[2] = {mesh.vertexBuffer, mesh.indexBuffer};
        glDeleteBuffers(2, buffers);
        if (vao) glDeleteVertexArrays(1, &vao);
    }
    mGpuMeshes.clear();
}

void GLESRenderer::shutdown() {
    clearGpuCache();
    clearGpuTextures();
    if (mShadowFramebuffer) glDeleteFramebuffers(1, &mShadowFramebuffer);
    if (mShadowTexture) glDeleteTextures(1, &mShadowTexture);
    if (mEnvironmentTexture) glDeleteTextures(1, &mEnvironmentTexture);
    if (mShadowProgram) glDeleteProgram(mShadowProgram);
    if (mSkyProgram) glDeleteProgram(mSkyProgram);
    if (gProgram) glDeleteProgram(gProgram);
    mShadowFramebuffer = 0;
    mShadowTexture = 0;
    mEnvironmentTexture = 0;
    mEnvironmentTexturePath.clear();
    mEnvironmentTextureDirty = true;
    mShadowProgram = 0;
    mSkyProgram = 0;
    gProgram = 0;
    mShadowAvailable = false;
}

void GLESRenderer::resize(int width, int height) {
    mWidth = width;
    mHeight = height;
    glViewport(0, 0, width, height);
}

void GLESRenderer::clearGpuTextures() {
    for (const auto& entry : mGpuTextures) {
        const GLuint texture = entry.second;
        if (texture) glDeleteTextures(1, &texture);
    }
    mGpuTextures.clear();
}

u32 GLESRenderer::textureForPath(const std::string& path) {
    if (path.empty()) return 0;
    const auto found = mGpuTextures.find(path);
    if (found != mGpuTextures.end()) return found->second;
    ImageData image;
    if (!loadImage(path, image)) {
        logWarn("GLESRenderer: unsupported/missing base-color texture: " + path + " (white fallback)");
        mGpuTextures.emplace(path, 0);
        return 0;
    }
    GLint maxTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    if (maxTextureSize <= 0 || image.width > maxTextureSize || image.height > maxTextureSize) {
        logWarn("GLESRenderer: texture exceeds device limit: " + path);
        mGpuTextures.emplace(path, 0);
        return 0;
    }
    const size_t channels = image.rgba.empty() ? 3u : 4u;
    const std::vector<uint8_t>& sourcePixels = image.rgba.empty() ? image.rgb : image.rgba;
    const size_t rowBytes = static_cast<size_t>(image.width) * channels;
    std::vector<uint8_t> flipped(sourcePixels.size());
    for (int y = 0; y < image.height; ++y) {
        const size_t sourceRow = static_cast<size_t>(image.height - 1 - y) * rowBytes;
        const size_t targetRow = static_cast<size_t>(y) * rowBytes;
        std::copy_n(sourcePixels.data() + sourceRow, rowBytes, flipped.data() + targetRow);
    }
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const GLenum format = channels == 4 ? GL_RGBA : GL_RGB;
    const GLint internalFormat = channels == 4 ? GL_SRGB8_ALPHA8 : GL_SRGB8;
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, image.width, image.height, 0,
                 format, GL_UNSIGNED_BYTE, flipped.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
    mGpuTextures.emplace(path, texture);
    return texture;
}

void GLESRenderer::setEnvironment(const EnvironmentSettings& env) {
    if (env.skyboxTexture != mEnv.skyboxTexture) mEnvironmentTextureDirty = true;
    mEnv = env;
}

void GLESRenderer::setLights(const std::vector<RenderLight>& lights) { mLights = lights; }
void GLESRenderer::setCameraPosition(const Vec3& position) { mCameraPosition = position; }

void GLESRenderer::updateEnvironmentTexture() {
    if (!mEnvironmentTextureDirty) return;
    mEnvironmentTextureDirty = false;
    if (mEnvironmentTexture) glDeleteTextures(1, &mEnvironmentTexture);
    mEnvironmentTexture = 0;
    mEnvironmentMipLevels = 1;
    mEnvironmentTexturePath = mEnv.skyboxTexture;
    if (mEnvironmentTexturePath.empty()) return;

    ImageData image;
    if (!loadPpmImage(mEnvironmentTexturePath, image)) {
        logWarn("GLESRenderer: environment texture unavailable or unsupported: " +
                mEnvironmentTexturePath + " (using procedural sky)");
        return;
    }
    glGenTextures(1, &mEnvironmentTexture);
    glBindTexture(GL_TEXTURE_2D, mEnvironmentTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8, image.width, image.height, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, image.rgb.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    mEnvironmentMipLevels = 1 + static_cast<int>(std::floor(
        std::log2(static_cast<float>(std::max(image.width, image.height)))));
    glBindTexture(GL_TEXTURE_2D, 0);
    logInfo("GLESRenderer: loaded equirectangular environment " +
            mEnvironmentTexturePath + " (" + std::to_string(image.width) + "x" +
            std::to_string(image.height) + ", mipmapped)");
}

void GLESRenderer::beginFrame(const Mat4& view, const Mat4& projection) {
    updateEnvironmentTexture();
    mStats = RenderStats{};
    mDrawCommands.clear();
    mView = view;
    mProjection = projection;
    Vec3 lightRay{-0.4f, -1.0f, -0.3f};
    for (const RenderLight& light : mLights) {
        if (light.type == LightType::Directional) { lightRay = light.direction; break; }
    }
    const Vec3 lightPosition = lightRay * -24.0f;
    const Mat4 lightView = Mat4::lookAt(lightPosition, Vec3{0, 0, 0}, Vec3{0, 1, 0});
    const Mat4 lightProjection = Mat4::orthographic(-20.0f, 20.0f, -20.0f, 20.0f, 0.1f, 60.0f);
    mLightViewProjection = lightProjection * lightView;
}

void GLESRenderer::drawMesh(const MeshData& mesh, const Mat4& model,
                            const MaterialData& material) {
    if (mesh.positions.empty() || mesh.indices.empty()) return;

    GpuMesh transient;
    GpuMesh* gpu = nullptr;
    auto found = mesh.cacheKey == kNullAsset ? mGpuMeshes.end() : mGpuMeshes.find(mesh.cacheKey);
    if (found != mGpuMeshes.end()) {
        gpu = &found->second;
    } else {
        GpuMesh uploaded;
        glGenVertexArrays(1, &uploaded.vao);
        glGenBuffers(1, &uploaded.vertexBuffer);
        glGenBuffers(1, &uploaded.indexBuffer);
        glBindVertexArray(uploaded.vao);

        const size_t vertexCount = mesh.positions.size() / 3;
        std::vector<float> vertices(vertexCount * 8, 0.0f);
        for (size_t i = 0; i < vertexCount; ++i) {
            vertices[i*8+0] = mesh.positions[i*3+0];
            vertices[i*8+1] = mesh.positions[i*3+1];
            vertices[i*8+2] = mesh.positions[i*3+2];
            if (mesh.normals.size() >= (i+1)*3) {
                vertices[i*8+3] = mesh.normals[i*3+0];
                vertices[i*8+4] = mesh.normals[i*3+1];
                vertices[i*8+5] = mesh.normals[i*3+2];
            } else {
                vertices[i*8+4] = 1.0f;
            }
            if (mesh.uvs.size() >= (i+1)*2) {
                vertices[i*8+6] = mesh.uvs[i*2+0];
                vertices[i*8+7] = mesh.uvs[i*2+1];
            }
        }
        glBindBuffer(GL_ARRAY_BUFFER, uploaded.vertexBuffer);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                     vertices.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                              reinterpret_cast<const void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                              reinterpret_cast<const void*>(6 * sizeof(float)));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, uploaded.indexBuffer);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(u32)),
                     mesh.indices.data(), GL_STATIC_DRAW);
        uploaded.indexCount = static_cast<u32>(mesh.indices.size());
        if (mesh.cacheKey != kNullAsset) {
            auto inserted = mGpuMeshes.emplace(mesh.cacheKey, uploaded);
            gpu = &inserted.first->second;
        } else {
            transient = uploaded;
            gpu = &transient;
        }
    }
    mDrawCommands.push_back(DrawCommand{*gpu, model, material, gpu == &transient});
    ++mStats.drawCalls;
    mStats.triangles += gpu->indexCount / 3;
}

void GLESRenderer::endFrame() {
    if (mShadowAvailable && !mDrawCommands.empty()) {
        glBindFramebuffer(GL_FRAMEBUFFER, mShadowFramebuffer);
        glViewport(0, 0, 1024, 1024);
        glClear(GL_DEPTH_BUFFER_BIT);
        glUseProgram(mShadowProgram);
        glUniformMatrix4fv(gShadowLocLightVP, 1, GL_FALSE, mLightViewProjection.m);
        for (const DrawCommand& command : mDrawCommands) {
            // Shadow alpha testing will be enabled when the shadow shader also
            // samples the material texture; avoid solid shadows for cutouts.
            if (command.material.alphaMode != MaterialAlphaMode::Opaque) continue;
            glBindVertexArray(command.mesh.vao);
            glUniformMatrix4fv(gShadowLocModel, 1, GL_FALSE, command.model.m);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(command.mesh.indexCount),
                           GL_UNSIGNED_INT, nullptr);
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, mWidth, mHeight);
    glDepthMask(GL_TRUE);
    const float exposure = std::max(0.0f, mEnv.skyboxExposure);
    glClearColor(0.055f * mEnv.skyboxTint.x * exposure,
                 0.075f * mEnv.skyboxTint.y * exposure,
                 0.12f * mEnv.skyboxTint.z * exposure, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (mSkyProgram) {
        glDisable(GL_DEPTH_TEST);
        glUseProgram(mSkyProgram);
        glUniform3f(gSkyLocTint, mEnv.skyboxTint.x, mEnv.skyboxTint.y, mEnv.skyboxTint.z);
        glUniform1f(gSkyLocExposure, exposure);
        glUniform1f(gSkyLocRotation, mEnv.skyboxRotation / 360.0f);
        glUniform1i(gSkyLocEnvironment, 2);
        glUniform1f(gSkyLocEnvironmentAvailable, mEnvironmentTexture ? 1.0f : 0.0f);
        if (mEnvironmentTexture) {
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, mEnvironmentTexture);
        }
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glEnable(GL_DEPTH_TEST);
        glClear(GL_DEPTH_BUFFER_BIT);
    }

    glUseProgram(gProgram);
    glUniformMatrix4fv(gLocView, 1, GL_FALSE, mView.m);
    glUniformMatrix4fv(gLocProj, 1, GL_FALSE, mProjection.m);
    glUniformMatrix4fv(gLocLightVP, 1, GL_FALSE, mLightViewProjection.m);
    glUniform3f(gLocCameraPosition, mCameraPosition.x, mCameraPosition.y, mCameraPosition.z);
    constexpr int kMaxLights = 4;
    const int lightCount = std::min(kMaxLights, static_cast<int>(mLights.size()));
    float lightPositionType[kMaxLights * 4] = {};
    float lightDirectionRange[kMaxLights * 4] = {};
    float lightColorIntensity[kMaxLights * 4] = {};
    float lightCone[kMaxLights * 2] = {};
    for (int i = 0; i < lightCount; ++i) {
        const RenderLight& light = mLights[static_cast<size_t>(i)];
        const int offset4 = i * 4;
        lightPositionType[offset4] = light.position.x;
        lightPositionType[offset4+1] = light.position.y;
        lightPositionType[offset4+2] = light.position.z;
        lightPositionType[offset4+3] = light.type == LightType::Directional ? 0.0f
            : light.type == LightType::Point ? 1.0f : 2.0f;
        lightDirectionRange[offset4] = light.direction.x;
        lightDirectionRange[offset4+1] = light.direction.y;
        lightDirectionRange[offset4+2] = light.direction.z;
        lightDirectionRange[offset4+3] = light.range;
        lightColorIntensity[offset4] = light.color.x;
        lightColorIntensity[offset4+1] = light.color.y;
        lightColorIntensity[offset4+2] = light.color.z;
        lightColorIntensity[offset4+3] = light.intensity;
        lightCone[i*2] = std::cos(degToRad(light.innerConeDegrees));
        lightCone[i*2+1] = std::cos(degToRad(light.outerConeDegrees));
    }
    glUniform1i(gLocLightCount, lightCount);
    if (lightCount > 0) {
        glUniform4fv(gLocLightPositionType, lightCount, lightPositionType);
        glUniform4fv(gLocLightDirectionRange, lightCount, lightDirectionRange);
        glUniform4fv(gLocLightColorIntensity, lightCount, lightColorIntensity);
        glUniform2fv(gLocLightCone, lightCount, lightCone);
    }
    glUniform3f(gLocAmbientColor, mEnv.skyboxTint.x, mEnv.skyboxTint.y, mEnv.skyboxTint.z);
    glUniform1f(gLocAmbient, mEnv.ambientIntensity * 0.55f);
    glUniform1f(gLocExposure, exposure + 0.4f);
    glUniform1f(gLocShadowEnabled, mShadowAvailable ? 1.0f : 0.0f);
    glUniform1i(gLocShadowMap, 1);
    glUniform1i(gLocEnvironmentMap, 2);
    glUniform1f(gLocEnvironmentAvailable, mEnvironmentTexture ? 1.0f : 0.0f);
    glUniform1f(gLocEnvironmentMaxMip, static_cast<float>(mEnvironmentMipLevels - 1));
    glUniform1f(gLocReflectionIntensity, mEnv.reflectionIntensity);
    glUniform1f(gLocEnvironmentRotation, mEnv.skyboxRotation / 360.0f);
    if (mEnvironmentTexture) {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, mEnvironmentTexture);
    }
    if (mShadowAvailable) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, mShadowTexture);
    }

    auto drawCommand = [&](const DrawCommand& command) {
        glBindVertexArray(command.mesh.vao);
        glUniformMatrix4fv(gLocModel, 1, GL_FALSE, command.model.m);
        glUniform4f(gLocColor, command.material.baseColor[0], command.material.baseColor[1],
                    command.material.baseColor[2], command.material.baseColor[3]);
        const int alphaMode = command.material.alphaMode == MaterialAlphaMode::Mask ? 1
            : command.material.alphaMode == MaterialAlphaMode::Blend ? 2 : 0;
        glUniform1i(gLocAlphaMode, alphaMode);
        glUniform1f(gLocAlphaCutoff, command.material.alphaCutoff);
        glUniform1f(gLocMetallic, command.material.metallic);
        glUniform1f(gLocRoughness, command.material.roughness);
        const GLuint baseColorTexture = textureForPath(command.material.baseColorTexture);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, baseColorTexture);
        glUniform1i(gLocBaseColorTexture, 0);
        glUniform1f(gLocHasBaseColorTexture, baseColorTexture ? 1.0f : 0.0f);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(command.mesh.indexCount),
                       GL_UNSIGNED_INT, nullptr);
    };

    std::vector<const DrawCommand*> transparent;
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    for (const DrawCommand& command : mDrawCommands) {
        if (command.material.alphaMode == MaterialAlphaMode::Blend) {
            transparent.push_back(&command);
        } else {
            drawCommand(command);
        }
    }
    std::stable_sort(transparent.begin(), transparent.end(), [&](const DrawCommand* a, const DrawCommand* b) {
        auto distanceSquared = [&](const DrawCommand* command) {
            const float x = command->model.m[12] - mCameraPosition.x;
            const float y = command->model.m[13] - mCameraPosition.y;
            const float z = command->model.m[14] - mCameraPosition.z;
            return x*x + y*y + z*z;
        };
        return distanceSquared(a) > distanceSquared(b);
    });
    if (!transparent.empty()) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        for (const DrawCommand* command : transparent) drawCommand(*command);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
    for (const DrawCommand& command : mDrawCommands) {
        if (!command.transient) continue;
        GLuint vao = command.mesh.vao;
        GLuint buffers[2] = {command.mesh.vertexBuffer, command.mesh.indexBuffer};
        glDeleteBuffers(2, buffers);
        if (vao) glDeleteVertexArrays(1, &vao);
    }
    mDrawCommands.clear();
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
}

} // namespace mini

#else // !__ANDROID__ — desktop stub so the shared engine links everywhere.

namespace mini {

bool GLESRenderer::init(int width, int height) {
    mWidth = width;
    mHeight = height;
    logWarn("GLESRenderer: OpenGL ES is only available on Android; using NullRenderer behavior");
    return true;
}
void GLESRenderer::clearGpuCache() {
    mDrawCommands.clear();
    mGpuMeshes.clear();
}
void GLESRenderer::clearGpuTextures() { mGpuTextures.clear(); }
u32 GLESRenderer::textureForPath(const std::string&) { return 0; }
void GLESRenderer::updateEnvironmentTexture() {}
void GLESRenderer::shutdown() { clearGpuCache(); clearGpuTextures(); }
void GLESRenderer::resize(int width, int height) { mWidth = width; mHeight = height; }
void GLESRenderer::setEnvironment(const EnvironmentSettings& env) { mEnv = env; }
void GLESRenderer::setLights(const std::vector<RenderLight>& lights) { mLights = lights; }
void GLESRenderer::setCameraPosition(const Vec3& position) { mCameraPosition = position; }
void GLESRenderer::beginFrame(const Mat4&, const Mat4&) { mStats = RenderStats{}; mDrawCommands.clear(); }
void GLESRenderer::drawMesh(const MeshData& mesh, const Mat4&, const MaterialData&) {
    mStats.drawCalls += 1;
    mStats.triangles += static_cast<u32>(mesh.indices.size() / 3);
}
void GLESRenderer::endFrame() {}

} // namespace mini

#endif
