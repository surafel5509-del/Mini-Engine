#version 450
layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vColor;
layout(location = 2) in vec3 vWorldPosition;
layout(location = 3) in vec2 vUv;
layout(location = 0) out vec4 outColor;
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
layout(set = 0, binding = 1) uniform sampler2D environmentMap;
layout(set = 0, binding = 2) uniform sampler2D baseColorMap;
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
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}
vec2 environmentUv(vec3 direction) {
    vec3 d = normalize(direction);
    float longitude = atan(d.z, d.x) / (2.0 * PI) + 0.5 + ubo.environmentRotation;
    float latitude = 0.5 - asin(clamp(d.y, -1.0, 1.0)) / PI;
    return vec2(fract(longitude), clamp(latitude, 0.0, 1.0));
}
void main() {
    vec3 N = normalize(vNormal);
    vec3 cameraPosition = vec3(ubo.cameraPositionX, ubo.cameraPositionY, ubo.cameraPositionZ);
    vec3 V = normalize(cameraPosition - vWorldPosition);
    vec3 L = normalize(-ubo.lightDir);
    vec3 H = normalize(V + L);
    vec4 baseSample = ubo.hasBaseColorTexture > 0.5
        ? texture(baseColorMap, vUv) : vec4(1.0);
    float alpha = baseSample.a * ubo.baseColorAlpha;
    if (ubo.alphaMode > 0.5 && ubo.alphaMode < 1.5 && alpha < ubo.alphaCutoff) discard;
    vec3 albedo = max(vColor * baseSample.rgb, vec3(0.0));
    float metallic = clamp(ubo.metallic, 0.0, 1.0);
    float roughness = clamp(ubo.roughness, 0.045, 1.0);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    float NDF = distributionGGX(N, H, roughness);
    float G = geometrySchlickGGX(max(dot(N, V), 0.0), roughness) *
               geometrySchlickGGX(max(dot(N, L), 0.0), roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    vec3 specular = (NDF * G * F) /
        max(4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0), 0.001);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    float nDotL = max(dot(N, L), 0.0);
    vec3 direct = (kD * albedo / PI + specular) * ubo.lightColorIntensity.rgb *
                  (ubo.lightColorIntensity.a * nDotL);
    vec3 ambientRadiance = vec3(0.08, 0.11, 0.16);
    vec3 environmentSpecular = vec3(0.0);
    if (ubo.environmentAvailable > 0.5) {
        float maxMip = float(max(textureQueryLevels(environmentMap) - 1, 0));
        ambientRadiance = textureLod(environmentMap, environmentUv(N), maxMip).rgb;
        vec3 reflected = reflect(-V, N);
        environmentSpecular = textureLod(environmentMap,
            environmentUv(reflected), roughness * maxMip).rgb * F;
    }
    vec3 ambient = ambientRadiance * albedo * ubo.ambient * (1.0 - metallic * 0.7);
    ambient += environmentSpecular * ubo.reflectionIntensity;
    vec3 color = (ambient + direct) * ubo.exposure;
    color = color / (color + vec3(1.0));
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    outColor = vec4(color, 1.0);
}
