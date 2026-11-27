#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "mini/core/Types.h"

namespace mini {

enum class AssetType : u8 { Mesh, Material, Texture, Scene, Audio, Script, Unknown };

struct MeshData {
    std::vector<float> positions;  // xyz triplets
    std::vector<float> normals;    // xyz triplets
    std::vector<float> uvs;        // uv pairs
    std::vector<u32> indices;
    AssetId cacheKey = kNullAsset; // stable GPU-cache key assigned at registration
    std::string primitive;         // "cube", "sphere", ... (empty = imported)
};

enum class MaterialAlphaMode : u8 { Opaque, Mask, Blend };

struct MaterialData {
    std::string name = "Default";
    float baseColor[4] = {1, 1, 1, 1};
    float metallic = 0.0f;
    float roughness = 0.5f;
    std::string baseColorTexture;
    MaterialAlphaMode alphaMode = MaterialAlphaMode::Opaque;
    float alphaCutoff = 0.5f;
};

struct Asset {
    AssetId id = kNullAsset;
    AssetType type = AssetType::Unknown;
    std::string name;
    std::string path;              // empty for built-in / procedural assets
    MeshData mesh;
    MaterialData material;
};

// Registry + loader for all project assets. Built-in primitives are
// generated procedurally so the editor's "Built-in Assets" panel works
// with zero binary assets shipped in the APK.
class AssetManager {
public:
    AssetId registerAsset(Asset asset);
    Asset* get(AssetId id);
    const Asset* get(AssetId id) const;

    // Generate a built-in primitive mesh ("cube", "sphere", "plane",
    // "cylinder", "capsule", "quad"). Returns kNullAsset on unknown name.
    AssetId createPrimitive(const std::string& primitiveName);

    // Load a scene JSON file from disk (path relative to project root).
    bool loadSceneFile(const std::string& path, std::string& outJson);

    // Import a glTF 2.0 model file. Returns one mesh asset id per mesh
    // primitive (empty on failure). Each asset carries its material.
    std::vector<AssetId> importGltf(const std::string& path);

    u32 count() const { return static_cast<u32>(mAssets.size()); }

private:
    std::unordered_map<AssetId, Asset> mAssets;
    AssetId mNextId = 1;
};

} // namespace mini
