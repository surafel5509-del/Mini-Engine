#include "mini/assets/AssetManager.h"
#include <cmath>
#include <fstream>
#include <sstream>
#include "mini/assets/GltfLoader.h"
#include "mini/core/Log.h"

namespace mini {

AssetId AssetManager::registerAsset(Asset asset) {
    asset.id = mNextId++;
    AssetId id = asset.id;
    if (asset.type == AssetType::Mesh && asset.mesh.cacheKey == kNullAsset)
        asset.mesh.cacheKey = id;
    mAssets.emplace(id, std::move(asset));
    return id;
}

Asset* AssetManager::get(AssetId id) {
    auto it = mAssets.find(id);
    return it == mAssets.end() ? nullptr : &it->second;
}

const Asset* AssetManager::get(AssetId id) const {
    auto it = mAssets.find(id);
    return it == mAssets.end() ? nullptr : &it->second;
}

namespace {

void addQuad(MeshData& m, float x0, float y0, float z0,
             float x1, float y1, float z1,
             float x2, float y2, float z2,
             float x3, float y3, float z3,
             float nx, float ny, float nz) {
    u32 base = static_cast<u32>(m.positions.size() / 3);
    const float v[4][3] = {{x0,y0,z0},{x1,y1,z1},{x2,y2,z2},{x3,y3,z3}};
    const float uv[4][2] = {{0,0},{1,0},{1,1},{0,1}};
    for (int i = 0; i < 4; ++i) {
        m.positions.insert(m.positions.end(), {v[i][0], v[i][1], v[i][2]});
        m.normals.insert(m.normals.end(), {nx, ny, nz});
        m.uvs.insert(m.uvs.end(), {uv[i][0], uv[i][1]});
    }
    m.indices.insert(m.indices.end(), {base, base+1, base+2, base, base+2, base+3});
}

MeshData makeCube() {
    MeshData m; m.primitive = "cube";
    const float h = 0.5f;
    // +Z, -Z, +X, -X, +Y, -Y faces
    addQuad(m, -h,-h, h,  h,-h, h,  h, h, h,  -h, h, h,  0,0,1);
    addQuad(m,  h,-h,-h, -h,-h,-h, -h, h,-h,   h, h,-h,  0,0,-1);
    addQuad(m,  h,-h, h,  h,-h,-h,  h, h,-h,   h, h, h,  1,0,0);
    addQuad(m, -h,-h,-h, -h,-h, h, -h, h, h,  -h, h,-h, -1,0,0);
    addQuad(m, -h, h, h,  h, h, h,  h, h,-h,  -h, h,-h,  0,1,0);
    addQuad(m, -h,-h,-h,  h,-h,-h,  h,-h, h,  -h,-h, h,  0,-1,0);
    return m;
}

MeshData makePlane() {
    MeshData m; m.primitive = "plane";
    addQuad(m, -0.5f,0,-0.5f,  0.5f,0,-0.5f,  0.5f,0,0.5f,  -0.5f,0,0.5f,  0,1,0);
    return m;
}

MeshData makeQuad() {
    MeshData m; m.primitive = "quad";
    addQuad(m, -0.5f,-0.5f,0,  0.5f,-0.5f,0,  0.5f,0.5f,0,  -0.5f,0.5f,0,  0,0,1);
    return m;
}

MeshData makeSphere(int rings = 16, int sectors = 24) {
    MeshData m; m.primitive = "sphere";
    const float r = 0.5f;
    for (int i = 0; i <= rings; ++i) {
        float phi = static_cast<float>(i) / rings * 3.14159265f; // 0..pi
        for (int j = 0; j <= sectors; ++j) {
            float theta = static_cast<float>(j) / sectors * 2.0f * 3.14159265f;
            float x = r * std::sin(phi) * std::cos(theta);
            float y = r * std::cos(phi);
            float z = r * std::sin(phi) * std::sin(theta);
            m.positions.insert(m.positions.end(), {x, y, z});
            m.normals.insert(m.normals.end(), {x / r, y / r, z / r});
            m.uvs.insert(m.uvs.end(), {static_cast<float>(j) / sectors,
                                        static_cast<float>(i) / rings});
        }
    }
    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j < sectors; ++j) {
            u32 a = i * (sectors + 1) + j;
            u32 b = a + sectors + 1;
            m.indices.insert(m.indices.end(), {a, b, a+1, b, b+1, a+1});
        }
    }
    return m;
}

MeshData makeCylinder(int sectors = 24) {
    MeshData m; m.primitive = "cylinder";
    const float r = 0.5f, h = 0.5f;
    // Side
    for (int j = 0; j <= sectors; ++j) {
        float theta = static_cast<float>(j) / sectors * 2.0f * 3.14159265f;
        float x = r * std::cos(theta), z = r * std::sin(theta);
        m.positions.insert(m.positions.end(), {x, -h, z, x, h, z});
        m.normals.insert(m.normals.end(), {x / r, 0, z / r, x / r, 0, z / r});
        m.uvs.insert(m.uvs.end(), {static_cast<float>(j) / sectors, 0,
                                    static_cast<float>(j) / sectors, 1});
    }
    for (int j = 0; j < sectors; ++j) {
        u32 a = j * 2;
        m.indices.insert(m.indices.end(), {a, a+2, a+1, a+1, a+2, a+3});
    }
    // Caps (fans around center vertices)
    for (int cap = 0; cap < 2; ++cap) {
        float y = cap == 0 ? -h : h;
        float ny = cap == 0 ? -1.0f : 1.0f;
        u32 center = static_cast<u32>(m.positions.size() / 3);
        m.positions.insert(m.positions.end(), {0, y, 0});
        m.normals.insert(m.normals.end(), {0, ny, 0});
        m.uvs.insert(m.uvs.end(), {0.5f, 0.5f});
        u32 ringStart = static_cast<u32>(m.positions.size() / 3);
        for (int j = 0; j <= sectors; ++j) {
            float theta = static_cast<float>(j) / sectors * 2.0f * 3.14159265f;
            float x = r * std::cos(theta), z = r * std::sin(theta);
            m.positions.insert(m.positions.end(), {x, y, z});
            m.normals.insert(m.normals.end(), {0, ny, 0});
            m.uvs.insert(m.uvs.end(), {0.5f + 0.5f * std::cos(theta),
                                        0.5f + 0.5f * std::sin(theta)});
        }
        for (int j = 0; j < sectors; ++j) {
            if (cap == 0) m.indices.insert(m.indices.end(),
                {center, ringStart + j + 1, ringStart + j});
            else m.indices.insert(m.indices.end(),
                {center, ringStart + j, ringStart + j + 1});
        }
    }
    return m;
}

MeshData makeCapsule() {
    // Cylinder body + two hemisphere caps, merged into one mesh.
    MeshData m; m.primitive = "capsule";
    const float r = 0.5f, halfBody = 0.25f;
    const int sectors = 24;
    for (int j = 0; j <= sectors; ++j) {
        float theta = static_cast<float>(j) / sectors * 2.0f * 3.14159265f;
        float x = r * std::cos(theta), z = r * std::sin(theta);
        m.positions.insert(m.positions.end(), {x, -halfBody, z, x, halfBody, z});
        m.normals.insert(m.normals.end(), {x / r, 0, z / r, x / r, 0, z / r});
        m.uvs.insert(m.uvs.end(), {static_cast<float>(j) / sectors, 0.25f,
                                    static_cast<float>(j) / sectors, 0.75f});
    }
    for (int j = 0; j < sectors; ++j) {
        u32 a = j * 2;
        m.indices.insert(m.indices.end(), {a, a+2, a+1, a+1, a+2, a+3});
    }
    // Hemispheres (top and bottom), 4 rings each.
    for (int cap = 0; cap < 2; ++cap) {
        float cy = cap == 0 ? -halfBody : halfBody;
        float dir = cap == 0 ? -1.0f : 1.0f;
        u32 ringBase = static_cast<u32>(m.positions.size() / 3);
        const int rings = 4;
        for (int i = 1; i <= rings; ++i) {
            float phi = static_cast<float>(i) / rings * 1.5707963f; // 0..pi/2
            float rr = r * std::cos(phi);
            float yy = cy + dir * r * std::sin(phi);
            for (int j = 0; j <= sectors; ++j) {
                float theta = static_cast<float>(j) / sectors * 2.0f * 3.14159265f;
                float x = rr * std::cos(theta), z = rr * std::sin(theta);
                m.positions.insert(m.positions.end(), {x, yy, z});
                m.normals.insert(m.normals.end(), {x / r, dir * std::sin(phi), z / r});
                m.uvs.insert(m.uvs.end(), {static_cast<float>(j) / sectors, 0.5f});
            }
        }
        for (int i = 0; i < rings - 1; ++i) {
            for (int j = 0; j < sectors; ++j) {
                u32 a = ringBase + i * (sectors + 1) + j;
                u32 b = a + sectors + 1;
                if (cap == 0) m.indices.insert(m.indices.end(), {a, a+1, b, b, a+1, b+1});
                else m.indices.insert(m.indices.end(), {a, b, a+1, b, b+1, a+1});
            }
        }
    }
    return m;
}

} // namespace

AssetId AssetManager::createPrimitive(const std::string& primitiveName) {
    MeshData mesh;
    if (primitiveName == "cube") mesh = makeCube();
    else if (primitiveName == "sphere") mesh = makeSphere();
    else if (primitiveName == "plane") mesh = makePlane();
    else if (primitiveName == "cylinder") mesh = makeCylinder();
    else if (primitiveName == "capsule") mesh = makeCapsule();
    else if (primitiveName == "quad") mesh = makeQuad();
    else {
        logWarn("AssetManager: unknown primitive '" + primitiveName + "'");
        return kNullAsset;
    }
    Asset asset;
    asset.type = AssetType::Mesh;
    asset.name = primitiveName;
    asset.mesh = std::move(mesh);
    return registerAsset(std::move(asset));
}

std::vector<AssetId> AssetManager::importGltf(const std::string& path) {
    GltfModel model;
    if (!loadGltf(path, model)) return {};
    std::vector<AssetId> ids;
    for (auto& gm : model.meshes) {
        Asset asset;
        asset.type = AssetType::Mesh;
        asset.name = gm.name.empty() ? "gltf_mesh" : gm.name;
        asset.path = path;
        asset.mesh = std::move(gm.mesh);
        asset.material = gm.material;
        ids.push_back(registerAsset(std::move(asset)));
    }
    return ids;
}

bool AssetManager::loadSceneFile(const std::string& path, std::string& outJson) {
    std::ifstream in(path);
    if (!in) {
        logError("AssetManager: cannot open scene file: " + path);
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    outJson = ss.str();
    return true;
}

} // namespace mini
