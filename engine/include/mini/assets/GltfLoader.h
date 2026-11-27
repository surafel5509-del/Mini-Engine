#pragma once
#include <string>
#include <vector>
#include "mini/assets/AssetManager.h"

namespace mini {

// One mesh primitive from a glTF file, with its material.
struct GltfMesh {
    MeshData mesh;
    MaterialData material;
    std::string name;
};

struct GltfModel {
    std::string name;
    std::vector<GltfMesh> meshes;
};

// Loads glTF 2.0 JSON (.gltf with external/base64 buffers) or binary GLB
// (.glb with JSON/BIN chunks). Sparse and normalized mesh accessors are decoded.
// Node transforms are baked into the mesh vertices, so the engine renders
// each mesh with only the entity transform.
// Returns false (and logs) on any parse/IO error.
bool loadGltf(const std::string& path, GltfModel& out);

} // namespace mini
