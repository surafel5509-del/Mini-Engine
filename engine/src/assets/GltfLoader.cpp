#include "mini/assets/GltfLoader.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <sstream>
#include "mini/core/Json.h"
#include "mini/core/Math.h"
#include "mini/core/Log.h"

namespace mini {
namespace {

using json::Value;

std::string readFile(const std::string& path, bool& ok) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { ok = false; return {}; }
    std::ostringstream ss;
    ss << in.rdbuf();
    ok = true;
    return ss.str();
}

std::string dirName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? "" : path.substr(0, slash);
}

// --- base64 (for data-URI buffers) ---
std::string base64Decode(const std::string& in) {
    static const char* kTable =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -8;
    for (char c : in) {
        const char* p = std::strchr(kTable, c);
        if (!p) {
            if (c == '=') break;
            continue;
        }
        val = (val << 6) | static_cast<int>(p - kTable);
        valb += 6;
        if (valb >= 0) {
            out += static_cast<char>((val >> valb) & 0xFF);
            valb -= 8;
        }
    }
    return out;
}

// --- accessor decoding ---
struct AccessorInfo {
    int bufferView = -1;
    size_t byteOffset = 0;
    int componentType = 5126; // FLOAT
    int count = 0;
    int numComponents = 1;
    bool normalized = false;
    bool sparse = false;
    int sparseCount = 0;
    int sparseIndicesView = -1;
    size_t sparseIndicesOffset = 0;
    int sparseIndicesComponentType = 0;
    int sparseValuesView = -1;
    size_t sparseValuesOffset = 0;
};

int componentSize(int componentType) {
    switch (componentType) {
        case 5120: case 5121: return 1; // BYTE, UBYTE
        case 5122: case 5123: return 2; // SHORT, USHORT
        case 5125: case 5126: return 4; // UINT, FLOAT
        default: return 0;
    }
}

int typeComponents(const std::string& type) {
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    if (type == "MAT4") return 16;
    return 0;
}

float readComponent(const std::string& buffer, size_t offset, int componentType) {
    switch (componentType) {
        case 5120: return static_cast<float>(static_cast<int8_t>(buffer[offset]));
        case 5121: return static_cast<float>(static_cast<uint8_t>(buffer[offset]));
        case 5122: {
            int16_t v; std::memcpy(&v, buffer.data() + offset, 2);
            return static_cast<float>(v);
        }
        case 5123: {
            uint16_t v; std::memcpy(&v, buffer.data() + offset, 2);
            return static_cast<float>(v);
        }
        case 5125: {
            uint32_t v; std::memcpy(&v, buffer.data() + offset, 4);
            return static_cast<float>(v);
        }
        case 5126: {
            float v; std::memcpy(&v, buffer.data() + offset, 4);
            return v;
        }
        default: return 0;
    }
}

bool readUnsignedComponent(const std::string& buffer, size_t offset, int componentType,
                           uint32_t& value) {
    switch (componentType) {
        case 5121:
            value = static_cast<uint8_t>(buffer[offset]);
            return true;
        case 5123: {
            uint16_t v; std::memcpy(&v, buffer.data() + offset, sizeof(v));
            value = v;
            return true;
        }
        case 5125:
            std::memcpy(&value, buffer.data() + offset, sizeof(value));
            return true;
        default:
            return false;
    }
}

bool resolveView(const std::vector<std::string>& buffers,
                 const std::vector<size_t>& viewBuffers,
                 const std::vector<size_t>& viewOffsets,
                 const std::vector<size_t>& viewLengths,
                 const std::vector<size_t>& viewStrides,
                 int viewIndex, size_t accessorOffset, size_t elementIndex,
                 size_t stride, size_t elementSize,
                 const std::string*& buffer, size_t& absoluteOffset) {
    if (viewIndex < 0 || static_cast<size_t>(viewIndex) >= viewOffsets.size()) return false;
    const size_t vi = static_cast<size_t>(viewIndex);
    if (vi >= viewBuffers.size() || vi >= viewLengths.size() || vi >= viewStrides.size() ||
        viewBuffers[vi] >= buffers.size()) return false;
    buffer = &buffers[viewBuffers[vi]];
    const size_t viewStart = viewOffsets[vi];
    const size_t viewLength = viewLengths[vi];
    if (viewStart > buffer->size() || viewLength > buffer->size() - viewStart ||
        accessorOffset > viewLength || stride == 0 ||
        elementIndex > (std::numeric_limits<size_t>::max() - accessorOffset) / stride)
        return false;
    const size_t relative = accessorOffset + elementIndex * stride;
    if (relative > viewLength || elementSize > viewLength - relative) return false;
    absoluteOffset = viewStart + relative;
    return true;
}

float normalizedComponent(float value, int componentType) {
    switch (componentType) {
        case 5120: return std::max(value / 127.0f, -1.0f);
        case 5121: return value / 255.0f;
        case 5122: return std::max(value / 32767.0f, -1.0f);
        case 5123: return value / 65535.0f;
        default: return value;
    }
}

bool readAccessor(const std::vector<std::string>& buffers,
                  const std::vector<size_t>& viewBuffers,
                  const std::vector<size_t>& viewOffsets,
                  const std::vector<size_t>& viewLengths,
                  const std::vector<size_t>& viewStrides,
                  const AccessorInfo& acc, std::vector<float>& out) {
    const int compSize = componentSize(acc.componentType);
    if (acc.count < 0 || acc.numComponents <= 0 || compSize == 0 ||
        (acc.normalized && (acc.componentType == 5125 || acc.componentType == 5126)) ||
        (acc.bufferView < 0 && !acc.sparse)) return false;
    if (static_cast<size_t>(acc.count) >
        std::numeric_limits<size_t>::max() / static_cast<size_t>(acc.numComponents)) return false;
    const size_t elementSize = static_cast<size_t>(compSize) *
                               static_cast<size_t>(acc.numComponents);
    const size_t valueCount = static_cast<size_t>(acc.count) *
                              static_cast<size_t>(acc.numComponents);
    out.assign(valueCount, 0.0f);

    if (acc.bufferView >= 0) {
        const size_t viewIndex = static_cast<size_t>(acc.bufferView);
        if (viewIndex >= viewStrides.size()) return false;
        const size_t stride = viewStrides[viewIndex] ? viewStrides[viewIndex] : elementSize;
        if (stride < elementSize) return false;
        for (size_t i = 0; i < static_cast<size_t>(acc.count); ++i) {
            const std::string* buffer = nullptr;
            size_t offset = 0;
            if (!resolveView(buffers, viewBuffers, viewOffsets, viewLengths, viewStrides,
                             acc.bufferView, acc.byteOffset, i, stride, elementSize,
                             buffer, offset)) return false;
            for (int c = 0; c < acc.numComponents; ++c) {
                float value = readComponent(*buffer, offset + static_cast<size_t>(c) * compSize,
                                            acc.componentType);
                if (acc.normalized) value = normalizedComponent(value, acc.componentType);
                out[i * static_cast<size_t>(acc.numComponents) + static_cast<size_t>(c)] = value;
            }
        }
    }

    if (!acc.sparse) return true;
    if (acc.sparseCount <= 0 || acc.sparseCount > acc.count ||
        (acc.sparseIndicesComponentType != 5121 &&
         acc.sparseIndicesComponentType != 5123 &&
         acc.sparseIndicesComponentType != 5125)) return false;
    const size_t sparseIndexSize = static_cast<size_t>(componentSize(acc.sparseIndicesComponentType));
    const size_t indexView = static_cast<size_t>(acc.sparseIndicesView);
    const size_t valuesView = static_cast<size_t>(acc.sparseValuesView);
    if (acc.sparseIndicesView < 0 || acc.sparseValuesView < 0 ||
        indexView >= viewStrides.size() || valuesView >= viewStrides.size() ||
        viewStrides[indexView] != 0 || viewStrides[valuesView] != 0) return false;

    uint32_t previousIndex = 0;
    for (size_t i = 0; i < static_cast<size_t>(acc.sparseCount); ++i) {
        const std::string* indexBuffer = nullptr;
        size_t indexOffset = 0;
        if (!resolveView(buffers, viewBuffers, viewOffsets, viewLengths, viewStrides,
                         acc.sparseIndicesView, acc.sparseIndicesOffset, i,
                         sparseIndexSize, sparseIndexSize, indexBuffer, indexOffset)) return false;
        uint32_t sparseIndex = 0;
        if (!readUnsignedComponent(*indexBuffer, indexOffset,
                                   acc.sparseIndicesComponentType, sparseIndex) ||
            sparseIndex >= static_cast<uint32_t>(acc.count) ||
            (i > 0 && sparseIndex <= previousIndex)) return false;
        previousIndex = sparseIndex;

        const std::string* valuesBuffer = nullptr;
        size_t valuesOffset = 0;
        if (!resolveView(buffers, viewBuffers, viewOffsets, viewLengths, viewStrides,
                         acc.sparseValuesView, acc.sparseValuesOffset, i,
                         elementSize, elementSize, valuesBuffer, valuesOffset)) return false;
        for (int c = 0; c < acc.numComponents; ++c) {
            float value = readComponent(*valuesBuffer,
                valuesOffset + static_cast<size_t>(c) * compSize, acc.componentType);
            if (acc.normalized) value = normalizedComponent(value, acc.componentType);
            out[static_cast<size_t>(sparseIndex) * static_cast<size_t>(acc.numComponents) +
                static_cast<size_t>(c)] = value;
        }
    }
    return true;
}

Mat4 nodeMatrix(const Value& node) {
    if (const Value* m = node.find("matrix"); m && m->isArray() && m->array.size() == 16) {
        Mat4 r;
        for (int i = 0; i < 16; ++i) r.m[i] = static_cast<f32>(m->array[i].number);
        return r;
    }
    Vec3 t{0, 0, 0}, s{1, 1, 1};
    Quaternion q = Quaternion::identity();
    if (const Value* v = node.find("translation"); v && v->isArray() && v->array.size() == 3)
        t = {static_cast<f32>(v->array[0].number), static_cast<f32>(v->array[1].number),
             static_cast<f32>(v->array[2].number)};
    if (const Value* v = node.find("scale"); v && v->isArray() && v->array.size() == 3)
        s = {static_cast<f32>(v->array[0].number), static_cast<f32>(v->array[1].number),
             static_cast<f32>(v->array[2].number)};
    if (const Value* v = node.find("rotation"); v && v->isArray() && v->array.size() == 4)
        q = {static_cast<f32>(v->array[0].number), static_cast<f32>(v->array[1].number),
             static_cast<f32>(v->array[2].number), static_cast<f32>(v->array[3].number)};
    return Mat4::translation(t) * q.toMat4() * Mat4::scale(s);
}

void applyMatrix(MeshData& mesh, const Mat4& m) {
    // Transform positions.
    for (size_t i = 0; i + 2 < mesh.positions.size(); i += 3) {
        float x = mesh.positions[i], y = mesh.positions[i+1], z = mesh.positions[i+2];
        mesh.positions[i]   = m.m[0]*x + m.m[4]*y + m.m[8]*z  + m.m[12];
        mesh.positions[i+1] = m.m[1]*x + m.m[5]*y + m.m[9]*z  + m.m[13];
        mesh.positions[i+2] = m.m[2]*x + m.m[6]*y + m.m[10]*z + m.m[14];
    }
    // Transform normals (upper 3x3; correct for uniform scale, which is
    // what model files use in practice).
    for (size_t i = 0; i + 2 < mesh.normals.size(); i += 3) {
        float x = mesh.normals[i], y = mesh.normals[i+1], z = mesh.normals[i+2];
        Vec3 n{m.m[0]*x + m.m[4]*y + m.m[8]*z,
               m.m[1]*x + m.m[5]*y + m.m[9]*z,
               m.m[2]*x + m.m[6]*y + m.m[10]*z};
        n = n.normalized();
        mesh.normals[i] = n.x; mesh.normals[i+1] = n.y; mesh.normals[i+2] = n.z;
    }
}

} // namespace

bool loadGltf(const std::string& path, GltfModel& out) {
    bool ok = false;
    std::string fileData = readFile(path, ok);
    if (!ok) {
        logError("glTF: cannot open " + path);
        return false;
    }

    // GLB 2.0 is a little-endian container with a JSON chunk and an optional
    // BIN chunk. Detect by magic rather than extension so renamed assets still
    // load, and validate every chunk boundary before exposing its contents.
    std::string text;
    std::string glbBinary;
    const bool isGlb = fileData.size() >= 4 &&
        std::memcmp(fileData.data(), "glTF", 4) == 0;
    if (isGlb) {
        auto readU32 = [&](size_t offset, uint32_t& value) {
            if (offset > fileData.size() || fileData.size() - offset < 4) return false;
            const auto* bytes = reinterpret_cast<const unsigned char*>(fileData.data() + offset);
            value = static_cast<uint32_t>(bytes[0]) |
                    (static_cast<uint32_t>(bytes[1]) << 8) |
                    (static_cast<uint32_t>(bytes[2]) << 16) |
                    (static_cast<uint32_t>(bytes[3]) << 24);
            return true;
        };
        uint32_t version = 0, declaredLength = 0;
        if (fileData.size() < 12 || !readU32(4, version) || !readU32(8, declaredLength) ||
            version != 2 || declaredLength != fileData.size()) {
            logError("glTF: invalid GLB 2.0 header in " + path);
            return false;
        }
        constexpr uint32_t kJsonChunk = 0x4E4F534A; // JSON
        constexpr uint32_t kBinChunk = 0x004E4942;  // BIN\0
        size_t offset = 12;
        bool jsonFound = false;
        bool binFound = false;
        while (offset < fileData.size()) {
            uint32_t chunkLength = 0, chunkType = 0;
            if (!readU32(offset, chunkLength) || !readU32(offset + 4, chunkType)) {
                logError("glTF: truncated GLB chunk header in " + path);
                return false;
            }
            offset += 8;
            if (chunkLength % 4 != 0 || chunkLength > fileData.size() - offset) {
                logError("glTF: invalid GLB chunk length in " + path);
                return false;
            }
            if (!jsonFound) {
                if (chunkType != kJsonChunk) {
                    logError("glTF: GLB JSON chunk must appear first in " + path);
                    return false;
                }
                text.assign(fileData.data() + offset, chunkLength);
                jsonFound = true;
            } else if (chunkType == kJsonChunk) {
                logError("glTF: duplicate GLB JSON chunk in " + path);
                return false;
            } else if (chunkType == kBinChunk) {
                if (binFound) {
                    logError("glTF: duplicate GLB BIN chunk in " + path);
                    return false;
                }
                glbBinary.assign(fileData.data() + offset, chunkLength);
                binFound = true;
            }
            offset += chunkLength;
        }
        if (!jsonFound) {
            logError("glTF: GLB has no JSON chunk in " + path);
            return false;
        }
    } else {
        text = std::move(fileData);
    }

    Value root;
    json::Parser parser(text);
    if (!parser.parse(root) || !root.isObject()) {
        logError("glTF: invalid JSON in " + path);
        return false;
    }

    // --- Buffers ---
    std::vector<std::string> buffers;
    if (const Value* bufs = root.find("buffers"); bufs && bufs->isArray()) {
        for (size_t bufferIndex = 0; bufferIndex < bufs->array.size(); ++bufferIndex) {
            const Value& b = bufs->array[bufferIndex];
            std::string data;
            if (const Value* uri = b.find("uri"); uri && uri->isString()) {
                if (uri->str.rfind("data:", 0) == 0) {
                    size_t comma = uri->str.find(',');
                    if (comma == std::string::npos) {
                        logError("glTF: malformed data URI in " + path);
                        return false;
                    }
                    data = base64Decode(uri->str.substr(comma + 1));
                } else {
                    std::string dir = dirName(path);
                    std::string binPath = dir.empty() ? uri->str : dir + "/" + uri->str;
                    bool bok = false;
                    data = readFile(binPath, bok);
                    if (!bok) {
                        logError("glTF: cannot open buffer " + binPath);
                        return false;
                    }
                }
            } else if (isGlb && bufferIndex == 0 && !glbBinary.empty()) {
                data = glbBinary;
            } else {
                logError("glTF: buffer has no URI or GLB BIN chunk in " + path);
                return false;
            }
            if (const Value* length = b.find("byteLength"); length && length->isNumber()) {
                const double requested = length->number;
                if (requested < 0 || std::floor(requested) != requested ||
                    requested > static_cast<double>(data.size()) ||
                    (isGlb && bufferIndex == 0 && data.size() - static_cast<size_t>(requested) > 3)) {
                    logError("glTF: buffer byteLength exceeds available data in " + path);
                    return false;
                }
            }
            buffers.push_back(std::move(data));
        }
    }

    // --- Buffer views ---
    std::vector<size_t> viewBuffers, viewOffsets, viewLengths, viewStrides;
    if (const Value* views = root.find("bufferViews"); views && views->isArray()) {
        for (const Value& v : views->array) {
            size_t bufferIndex = 0, off = 0, len = 0, stride = 0;
            if (const Value* x = v.find("buffer"); x && x->isNumber()) bufferIndex = static_cast<size_t>(x->number);
            if (const Value* x = v.find("byteOffset"); x && x->isNumber()) off = static_cast<size_t>(x->number);
            if (const Value* x = v.find("byteLength"); x && x->isNumber()) len = static_cast<size_t>(x->number);
            if (const Value* x = v.find("byteStride"); x && x->isNumber()) stride = static_cast<size_t>(x->number);
            viewBuffers.push_back(bufferIndex);
            viewOffsets.push_back(off);
            viewLengths.push_back(len);
            viewStrides.push_back(stride);
        }
    }

    // --- Accessors ---
    std::vector<AccessorInfo> accessors;
    if (const Value* accs = root.find("accessors"); accs && accs->isArray()) {
        for (const Value& a : accs->array) {
            AccessorInfo info;
            if (const Value* x = a.find("bufferView"); x && x->isNumber()) info.bufferView = static_cast<int>(x->number);
            if (const Value* x = a.find("byteOffset"); x && x->isNumber()) info.byteOffset = static_cast<size_t>(x->number);
            if (const Value* x = a.find("componentType"); x && x->isNumber()) info.componentType = static_cast<int>(x->number);
            if (const Value* x = a.find("count"); x && x->isNumber()) info.count = static_cast<int>(x->number);
            if (const Value* x = a.find("type"); x && x->isString()) info.numComponents = typeComponents(x->str);
            if (const Value* x = a.find("normalized"); x && x->type == Value::Type::Bool)
                info.normalized = x->boolean;
            if (const Value* sparse = a.find("sparse"); sparse && sparse->isObject()) {
                info.sparse = true;
                if (const Value* x = sparse->find("count"); x && x->isNumber())
                    info.sparseCount = static_cast<int>(x->number);
                if (const Value* indices = sparse->find("indices"); indices && indices->isObject()) {
                    if (const Value* x = indices->find("bufferView"); x && x->isNumber())
                        info.sparseIndicesView = static_cast<int>(x->number);
                    if (const Value* x = indices->find("byteOffset"); x && x->isNumber())
                        info.sparseIndicesOffset = static_cast<size_t>(x->number);
                    if (const Value* x = indices->find("componentType"); x && x->isNumber())
                        info.sparseIndicesComponentType = static_cast<int>(x->number);
                }
                if (const Value* values = sparse->find("values"); values && values->isObject()) {
                    if (const Value* x = values->find("bufferView"); x && x->isNumber())
                        info.sparseValuesView = static_cast<int>(x->number);
                    if (const Value* x = values->find("byteOffset"); x && x->isNumber())
                        info.sparseValuesOffset = static_cast<size_t>(x->number);
                }
            }
            accessors.push_back(info);
        }
    }

    // --- External LDR images and glTF texture-to-image references ---
    std::vector<std::string> imagePaths;
    if (const Value* images = root.find("images"); images && images->isArray()) {
        for (const Value& image : images->array) {
            std::string imagePath;
            if (const Value* uri = image.find("uri"); uri && uri->isString() &&
                uri->str.rfind("data:", 0) != 0) {
                const std::string dir = dirName(path);
                imagePath = dir.empty() ? uri->str : dir + "/" + uri->str;
            }
            imagePaths.push_back(std::move(imagePath));
        }
    }
    std::vector<int> textureSources;
    if (const Value* textures = root.find("textures"); textures && textures->isArray()) {
        for (const Value& texture : textures->array) {
            int source = -1;
            if (const Value* value = texture.find("source"); value && value->isNumber())
                source = static_cast<int>(value->number);
            textureSources.push_back(source);
        }
    }

    // --- Materials ---
    std::vector<MaterialData> materials;
    if (const Value* mats = root.find("materials"); mats && mats->isArray()) {
        for (const Value& m : mats->array) {
            MaterialData mat;
            if (const Value* n = m.find("name"); n && n->isString()) mat.name = n->str;
            if (const Value* pbr = m.find("pbrMetallicRoughness")) {
                if (const Value* c = pbr->find("baseColorFactor"); c && c->isArray() && c->array.size() >= 3) {
                    mat.baseColor[0] = static_cast<float>(c->array[0].number);
                    mat.baseColor[1] = static_cast<float>(c->array[1].number);
                    mat.baseColor[2] = static_cast<float>(c->array[2].number);
                    if (c->array.size() >= 4)
                        mat.baseColor[3] = static_cast<float>(c->array[3].number);
                }
                if (const Value* x = pbr->find("metallicFactor"); x && x->isNumber())
                    mat.metallic = static_cast<float>(x->number);
                if (const Value* x = pbr->find("roughnessFactor"); x && x->isNumber())
                    mat.roughness = static_cast<float>(x->number);
                if (const Value* textureInfo = pbr->find("baseColorTexture");
                    textureInfo && textureInfo->isObject()) {
                    int texCoord = 0;
                    if (const Value* coordinate = textureInfo->find("texCoord");
                        coordinate && coordinate->isNumber())
                        texCoord = static_cast<int>(coordinate->number);
                    if (texCoord == 0) {
                    if (const Value* index = textureInfo->find("index"); index && index->isNumber()) {
                        const int textureIndex = static_cast<int>(index->number);
                        if (textureIndex >= 0 && static_cast<size_t>(textureIndex) < textureSources.size()) {
                            const int imageIndex = textureSources[static_cast<size_t>(textureIndex)];
                            if (imageIndex >= 0 && static_cast<size_t>(imageIndex) < imagePaths.size())
                                mat.baseColorTexture = imagePaths[static_cast<size_t>(imageIndex)];
                        }
                    }
                    }
                }
            }
            if (const Value* mode = m.find("alphaMode"); mode && mode->isString()) {
                if (mode->str == "MASK") mat.alphaMode = MaterialAlphaMode::Mask;
                else if (mode->str == "BLEND") mat.alphaMode = MaterialAlphaMode::Blend;
            }
            if (const Value* cutoff = m.find("alphaCutoff"); cutoff && cutoff->isNumber())
                mat.alphaCutoff = clamp(static_cast<float>(cutoff->number), 0.0f, 1.0f);
            materials.push_back(mat);
        }
    }

    // --- Meshes ---
    struct RawMesh { std::vector<GltfMesh> prims; };
    std::vector<RawMesh> rawMeshes;
    if (const Value* meshes = root.find("meshes"); meshes && meshes->isArray()) {
        for (const Value& meshDef : meshes->array) {
            RawMesh rm;
            const Value* prims = meshDef.find("primitives");
            if (!prims || !prims->isArray()) { rawMeshes.push_back(std::move(rm)); continue; }
            for (const Value& prim : prims->array) {
                GltfMesh gm;
                if (const Value* n = meshDef.find("name"); n && n->isString()) gm.name = n->str;
                if (const Value* mi = prim.find("material"); mi && mi->isNumber()) {
                    int idx = static_cast<int>(mi->number);
                    if (idx >= 0 && static_cast<size_t>(idx) < materials.size())
                        gm.material = materials[idx];
                }
                const Value* attrs = prim.find("attributes");
                if (!attrs) continue;
                auto readAttr = [&](const char* key, std::vector<float>& dst) -> bool {
                    const Value* ai = attrs->find(key);
                    if (!ai || !ai->isNumber()) return false;
                    int idx = static_cast<int>(ai->number);
                    if (idx < 0 || static_cast<size_t>(idx) >= accessors.size()) return false;
                    return readAccessor(buffers, viewBuffers, viewOffsets, viewLengths,
                                        viewStrides, accessors[idx], dst);
                };
                if (!readAttr("POSITION", gm.mesh.positions)) continue;
                if (!readAttr("NORMAL", gm.mesh.normals)) {
                    // Generate flat up-normals if the file omits them.
                    gm.mesh.normals.assign(gm.mesh.positions.size(), 0.0f);
                    for (size_t i = 1; i < gm.mesh.normals.size(); i += 3) gm.mesh.normals[i] = 1.0f;
                }
                readAttr("TEXCOORD_0", gm.mesh.uvs);
                if (const Value* ii = prim.find("indices"); ii && ii->isNumber()) {
                    int idx = static_cast<int>(ii->number);
                    if (idx >= 0 && static_cast<size_t>(idx) < accessors.size()) {
                        std::vector<float> idxFloats;
                        if (readAccessor(buffers, viewBuffers, viewOffsets, viewLengths,
                                         viewStrides, accessors[idx], idxFloats)) {
                            for (float f : idxFloats) gm.mesh.indices.push_back(static_cast<u32>(f));
                        }
                    }
                }
                if (gm.mesh.indices.empty()) {
                    // Non-indexed primitives use implicit sequential indices.
                    for (u32 i = 0; i < gm.mesh.positions.size() / 3; ++i)
                        gm.mesh.indices.push_back(i);
                }
                int mode = 4; // TRIANGLES per glTF default
                if (const Value* m = prim.find("mode"); m && m->isNumber()) mode = static_cast<int>(m->number);
                if (mode == 5 || mode == 6) {
                    const std::vector<u32> source = std::move(gm.mesh.indices);
                    gm.mesh.indices.clear();
                    for (size_t i = 2; i < source.size(); ++i) {
                        if (mode == 5) {
                            // Triangle strip alternates winding to keep consistent faces.
                            if (i & 1) gm.mesh.indices.insert(gm.mesh.indices.end(), {source[i-1], source[i-2], source[i]});
                            else gm.mesh.indices.insert(gm.mesh.indices.end(), {source[i-2], source[i-1], source[i]});
                        } else {
                            gm.mesh.indices.insert(gm.mesh.indices.end(), {source[0], source[i-1], source[i]});
                        }
                    }
                } else if (mode != 4) {
                    // Lines/points are not mesh-renderable in this engine yet.
                    continue;
                }
                if (gm.mesh.indices.size() < 3) continue;
                rm.prims.push_back(std::move(gm));
            }
            rawMeshes.push_back(std::move(rm));
        }
    }

    // --- Nodes: walk the scene graph, baking transforms into meshes ---
    out = GltfModel{};
    if (const Value* n = root.find("asset"); n) {
        (void)n; // asset metadata (version etc.) — no action needed
    }
    out.name = path;

    const Value* nodes = root.find("nodes");
    const Value* scenes = root.find("scenes");
    int defaultScene = 0;
    if (const Value* s = root.find("scene"); s && s->isNumber())
        defaultScene = static_cast<int>(s->number);

    std::vector<int> roots;
    if (scenes && scenes->isArray() && defaultScene >= 0 &&
        static_cast<size_t>(defaultScene) < scenes->array.size()) {
        if (const Value* r = scenes->array[defaultScene].find("nodes"); r && r->isArray())
            for (const Value& n : r->array) if (n.isNumber()) roots.push_back(static_cast<int>(n.number));
    }
    if (roots.empty() && nodes && nodes->isArray()) {
        // If the file omits a scene definition, walk only graph roots;
        // starting from every node would duplicate child meshes.
        std::vector<bool> isChild(nodes->array.size(), false);
        for (const Value& node : nodes->array) {
            if (const Value* children = node.find("children"); children && children->isArray()) {
                for (const Value& child : children->array) {
                    if (child.isNumber()) {
                        int idx = static_cast<int>(child.number);
                        if (idx >= 0 && static_cast<size_t>(idx) < isChild.size())
                            isChild[static_cast<size_t>(idx)] = true;
                    }
                }
            }
        }
        for (size_t i = 0; i < isChild.size(); ++i)
            if (!isChild[i]) roots.push_back(static_cast<int>(i));
    }

    std::function<void(int, const Mat4&)> walk = [&](int nodeIdx, const Mat4& parent) {
        if (!nodes || !nodes->isArray() || nodeIdx < 0 ||
            static_cast<size_t>(nodeIdx) >= nodes->array.size()) return;
        const Value& node = nodes->array[nodeIdx];
        Mat4 world = parent * nodeMatrix(node);
        if (const Value* mi = node.find("mesh"); mi && mi->isNumber()) {
            int meshIdx = static_cast<int>(mi->number);
            if (meshIdx >= 0 && static_cast<size_t>(meshIdx) < rawMeshes.size()) {
                for (GltfMesh gm : rawMeshes[meshIdx].prims) {
                    applyMatrix(gm.mesh, world);
                    out.meshes.push_back(std::move(gm));
                }
            }
        }
        if (const Value* children = node.find("children"); children && children->isArray()) {
            for (const Value& c : children->array)
                if (c.isNumber()) walk(static_cast<int>(c.number), world);
        }
    };
    for (int r : roots) walk(r, Mat4::identity());

    // Files without a scene graph: emit all primitives untransformed.
    if (out.meshes.empty()) {
        for (auto& rm : rawMeshes)
            for (auto& gm : rm.prims) out.meshes.push_back(std::move(gm));
    }

    if (out.meshes.empty()) {
        logError("glTF: no meshes found in " + path);
        return false;
    }
    logInfo("glTF: loaded " + std::to_string(out.meshes.size()) +
            " mesh(es) from " + path);
    return true;
}

} // namespace mini
