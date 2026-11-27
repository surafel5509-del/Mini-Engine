#pragma once
#include <string>
#include <vector>
#include "mini/core/Math.h"
#include "mini/core/Types.h"

namespace mini {

struct Transform {
    Vec3 position{0, 0, 0};
    Quaternion rotation = Quaternion::identity();
    Vec3 scale{1, 1, 1};
    Entity parent = kNullEntity;

    Mat4 localMatrix() const {
        return Mat4::translation(position) * rotation.toMat4() * Mat4::scale(scale);
    }
};

struct Name {
    std::string value;
};

struct ScriptComponent {
    std::string scriptKey;
    bool enabled = true;
};

// An imported model (glTF). The engine resolves `path` to mesh assets
// on scene load and renders every mesh with the entity's transform.
struct ModelRenderer {
    std::string path;                 // e.g. "models/car.gltf"
    std::vector<AssetId> meshes;      // resolved by the engine
};

struct MeshRenderer {
    AssetId mesh = kNullAsset;
    AssetId material = kNullAsset;
    std::string primitive;      // built-in primitive name ("cube", ...) or empty
    float tint[3] = {1, 1, 1};  // base color multiplier
    bool castShadows = true;
};

struct Camera {
    f32 fovYDeg = 60.0f;
    f32 nearPlane = 0.1f;
    f32 farPlane = 1000.0f;
    bool isMain = true;
    int priority = 0;
};

enum class LightType : u8 { Directional, Point, Spot };

struct Light {
    LightType type = LightType::Directional;
    Vec3 color{1, 1, 1};
    Vec3 localDirection{-0.4f, -1.0f, -0.3f};
    f32 intensity = 1.0f;
    f32 range = 10.0f;
    f32 innerConeDegrees = 20.0f;
    f32 outerConeDegrees = 30.0f;
    bool enabled = true;
};

struct RigidBody {
    Vec3 velocity{0, 0, 0};
    Vec3 angularVelocity{0, 0, 0};
    f32 mass = 1.0f;
    f32 linearDamping = 0.05f;
    f32 angularDamping = 0.05f;
    f32 restitution = 0.0f;
    f32 friction = 0.6f;
    bool useGravity = true;
    bool isKinematic = false;
};

struct SphereCollider {
    f32 radius = 0.5f;
};

// Environment / scene settings (mirrors the Inspector > Environment panel).
struct EnvironmentSettings {
    std::string skyboxTexture;      // asset path or built-in name
    f32 skyboxRotation = 0.0f;      // degrees
    Vec3 skyboxTint{1, 1, 1};
    f32 skyboxExposure = 0.6f;
    f32 ambientIntensity = 0.89f;
    f32 reflectionIntensity = 0.73f;
};

} // namespace mini
