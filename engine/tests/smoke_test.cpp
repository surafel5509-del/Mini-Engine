// Headless smoke test: exercises the full engine pipeline without a GPU.
// Build (desktop, no Android SDK needed):
//   g++ -std=c++17 -I engine/include engine/src/*/*.cpp engine/tests/smoke_test.cpp -o /tmp/mini_smoke -pthread
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include "mini/assets/GltfLoader.h"
#include "mini/assets/Image.h"
#include "mini/engine/Engine.h"

using namespace mini;

struct ProbeScript final : IScript {
    ProbeScript(int& created, int& updated, int& destroyed)
        : created(created), updated(updated), destroyed(destroyed) {}
    void onCreate(ScriptContext&) override { ++created; }
    void onUpdate(ScriptContext& context) override {
        ++updated;
        if (context.input) context.input->setKey(7, true);
    }
    void onDestroy(ScriptContext&) override { ++destroyed; }
    int& created;
    int& updated;
    int& destroyed;
};

int main() {
    // --- Math sanity ---
    Vec3 a{1, 2, 3}, b{4, 5, 6};
    assert(std::abs(dot(a, b) - 32.0f) < 1e-4f);
    Mat4 t = Mat4::translation({1, 0, 0}) * Mat4::scale({2, 2, 2});
    assert(std::abs(t.m[0] - 2.0f) < 1e-4f && std::abs(t.m[12] - 1.0f) < 1e-4f);
    Mat4 ortho = Mat4::orthographic(-2, 2, -1, 1, 0.1f, 20.0f);
    assert(std::abs(ortho.m[0] - 0.5f) < 1e-4f);
    assert(std::abs(ortho.m[5] - 1.0f) < 1e-4f);

    // Shared image loader decodes the bundled equirectangular environment map.
    ImageData environmentImage;
    assert(loadPpmImage("samples/CarScene/environment/city_dusk.ppm", environmentImage));
    assert(environmentImage.valid() && environmentImage.width == 512 && environmentImage.height == 256);
    ImageData decodedPng;
    assert(loadImage("engine/tests/fixtures/texture_rgba.png", decodedPng));
    assert(decodedPng.valid() && decodedPng.width == 2 && decodedPng.height == 1);
    assert(decodedPng.rgba.size() == 8 && decodedPng.rgba[3] == 128 && decodedPng.rgba[7] == 255);

    // GLB 2.0 single-file import decodes the same sample geometry/materials.
    GltfModel binaryModel;
    assert(loadGltf("samples/CarScene/models/car.glb", binaryModel));
    assert(binaryModel.meshes.size() == 8);
    assert(binaryModel.meshes.front().mesh.indices.size() == 36);
    assert(binaryModel.meshes.front().material.baseColor[1] > 0.5f);
    {
        std::ifstream source("samples/CarScene/models/car.glb", std::ios::binary);
        std::string truncated((std::istreambuf_iterator<char>(source)),
                              std::istreambuf_iterator<char>());
        assert(!truncated.empty());
        truncated.pop_back();
        const char* malformedPath = "/tmp/mini_engine_truncated.glb";
        std::ofstream malformed(malformedPath, std::ios::binary);
        malformed.write(truncated.data(), static_cast<std::streamsize>(truncated.size()));
        malformed.close();
        GltfModel rejectedModel;
        assert(!loadGltf(malformedPath, rejectedModel));
        std::remove(malformedPath);
    }
    {
        const char* sparseBufferPath = "/tmp/mini_sparse.bin";
        const char* sparseTexturePath = "/tmp/mini_sparse.ppm";
        const char* sparsePngPath = "/tmp/mini_sparse.png";
        const char* sparseGltfPath = "/tmp/mini_sparse.gltf";
        std::string bytes;
        auto appendFloat = [&](float value) {
            bytes.append(reinterpret_cast<const char*>(&value), sizeof(value));
        };
        // Base POSITION data (three vertices).
        for (float value : {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f})
            appendFloat(value);
        // Sparse POSITION index 1, followed by its replacement value.
        bytes.push_back(static_cast<char>(1));
        bytes.append(3, '\0');
        for (float value : {1.5f, 0.5f, 0.0f}) appendFloat(value);
        // UV accessor has no base bufferView: its sparse values initialize it.
        bytes.append({static_cast<char>(0), static_cast<char>(1), static_cast<char>(2)});
        bytes.push_back('\0');
        bytes.append({static_cast<char>(0), static_cast<char>(0), static_cast<char>(255),
                      static_cast<char>(0), static_cast<char>(0), static_cast<char>(255)});
        assert(bytes.size() == 62);
        {
            std::ofstream bufferFile(sparseBufferPath, std::ios::binary);
            bufferFile.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
        {
            std::ofstream textureFile(sparseTexturePath, std::ios::binary);
            textureFile.write("P6\n1 1\n255\n", 11);
            textureFile.put(static_cast<char>(200));
            textureFile.put(static_cast<char>(100));
            textureFile.put(static_cast<char>(50));
        }
        ImageData materialTextureImage;
        assert(loadPpmImage(sparseTexturePath, materialTextureImage));
        assert(materialTextureImage.width == 1 && materialTextureImage.height == 1);
        assert(materialTextureImage.rgb[0] == 200 && materialTextureImage.rgb[2] == 50);
        {
            std::ifstream fixture("engine/tests/fixtures/texture_rgba.png", std::ios::binary);
            std::string pngBytes((std::istreambuf_iterator<char>(fixture)),
                                 std::istreambuf_iterator<char>());
            std::ofstream pngFile(sparsePngPath, std::ios::binary);
            pngFile.write(pngBytes.data(), static_cast<std::streamsize>(pngBytes.size()));
        }
        const char* sparseJson = R"({
          "asset":{"version":"2.0"},
          "images":[{"uri":"mini_sparse.ppm"},{"uri":"mini_sparse.png"}],
          "textures":[{"source":0},{"source":1}],
          "materials":[{"alphaMode":"MASK","alphaCutoff":0.3,
            "pbrMetallicRoughness":{"baseColorFactor":[0.8,0.7,0.6,0.4],
                                     "baseColorTexture":{"index":1}}}],
          "buffers":[{"uri":"mini_sparse.bin","byteLength":62}],
          "bufferViews":[
            {"buffer":0,"byteOffset":0,"byteLength":36},
            {"buffer":0,"byteOffset":36,"byteLength":1},
            {"buffer":0,"byteOffset":40,"byteLength":12},
            {"buffer":0,"byteOffset":52,"byteLength":3},
            {"buffer":0,"byteOffset":56,"byteLength":6}],
          "accessors":[
            {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3",
             "sparse":{"count":1,"indices":{"bufferView":1,"componentType":5121},
                       "values":{"bufferView":2}}},
            {"componentType":5121,"normalized":true,"count":3,"type":"VEC2",
             "sparse":{"count":3,"indices":{"bufferView":3,"componentType":5121},
                       "values":{"bufferView":4}}}],
          "meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1},"material":0}]}]
        })";
        {
            std::ofstream jsonFile(sparseGltfPath);
            jsonFile << sparseJson;
        }
        GltfModel sparseModel;
        assert(loadGltf(sparseGltfPath, sparseModel));
        assert(sparseModel.meshes.size() == 1);
        const MeshData& sparseMesh = sparseModel.meshes.front().mesh;
        assert(sparseMesh.positions.size() == 9 && sparseMesh.uvs.size() == 6);
        assert(std::abs(sparseMesh.positions[3] - 1.5f) < 1e-6f);
        assert(std::abs(sparseMesh.positions[4] - 0.5f) < 1e-6f);
        assert(std::abs(sparseMesh.uvs[2] - 1.0f) < 1e-6f);
        assert(std::abs(sparseMesh.uvs[5] - 1.0f) < 1e-6f);
        assert(sparseModel.meshes.front().material.baseColorTexture == sparsePngPath);
        assert(sparseModel.meshes.front().material.alphaMode == MaterialAlphaMode::Mask);
        assert(std::abs(sparseModel.meshes.front().material.baseColor[3] - 0.4f) < 1e-6f);
        assert(std::abs(sparseModel.meshes.front().material.alphaCutoff - 0.3f) < 1e-6f);
        {
            std::fstream corruptBuffer(sparseBufferPath, std::ios::binary | std::ios::in | std::ios::out);
            corruptBuffer.seekp(36);
            corruptBuffer.put(static_cast<char>(3)); // sparse POSITION index is outside count
        }
        GltfModel invalidSparseModel;
        assert(!loadGltf(sparseGltfPath, invalidSparseModel));
        std::remove(sparseGltfPath);
        std::remove(sparseBufferPath);
        std::remove(sparseTexturePath);
        std::remove(sparsePngPath);
    }

    // Camera and lighting systems resolve ECS components into renderer-ready frames.
    World systemsWorld;
    const Entity cameraA = systemsWorld.createEntity();
    Transform cameraTransformA;
    cameraTransformA.position = {0, 0, 5};
    systemsWorld.set<Transform>(cameraA, cameraTransformA);
    Camera cameraComponentA;
    cameraComponentA.fovYDeg = 90.0f;
    systemsWorld.set<Camera>(cameraA, cameraComponentA);
    const Entity cameraB = systemsWorld.createEntity();
    Transform cameraTransformB;
    cameraTransformB.position = {2, 1, 5};
    cameraTransformB.rotation = Quaternion::fromEulerDeg({0, 90, 0});
    systemsWorld.set<Transform>(cameraB, cameraTransformB);
    Camera cameraComponentB;
    cameraComponentB.fovYDeg = 90.0f;
    cameraComponentB.priority = 2;
    systemsWorld.set<Camera>(cameraB, cameraComponentB);
    CameraFrame cameraFrame = CameraSystem{}.build(systemsWorld, 800, 400);
    assert(cameraFrame.entity == cameraB);
    assert(std::abs(cameraFrame.position.x - 2.0f) < 1e-6f);
    assert(cameraFrame.forward.x < -0.99f);
    assert(std::abs(cameraFrame.projection.m[0] - 0.5f) < 1e-5f);
    const Entity pointLightEntity = systemsWorld.createEntity();
    Transform pointTransform;
    pointTransform.position = {1, 2, 3};
    systemsWorld.set<Transform>(pointLightEntity, pointTransform);
    Light pointLight;
    pointLight.type = LightType::Point;
    pointLight.intensity = 3.0f;
    pointLight.range = 12.0f;
    systemsWorld.set<Light>(pointLightEntity, pointLight);
    const Entity directionalEntity = systemsWorld.createEntity();
    systemsWorld.set<Transform>(directionalEntity, Transform{});
    Light directionalLight;
    directionalLight.intensity = 1.2f;
    systemsWorld.set<Light>(directionalEntity, directionalLight);
    const auto renderLights = LightSystem{}.collect(systemsWorld);
    assert(renderLights.size() == 2);
    assert(renderLights.front().type == LightType::Directional);
    assert(renderLights.front().direction.y < -0.8f);
    assert(renderLights[1].type == LightType::Point);
    assert(std::abs(renderLights[1].position.y - 2.0f) < 1e-6f);

    // Fixed-step sphere contacts exchange velocities and correct overlap.
    World physicsWorld;
    const Entity sphereA = physicsWorld.createEntity();
    const Entity sphereB = physicsWorld.createEntity();
    Transform sphereTransformA, sphereTransformB;
    sphereTransformA.position = {-0.4f, 2.0f, 0.0f};
    sphereTransformB.position = {0.4f, 2.0f, 0.0f};
    physicsWorld.set<Transform>(sphereA, sphereTransformA);
    physicsWorld.set<Transform>(sphereB, sphereTransformB);
    RigidBody bodyA, bodyB;
    bodyA.useGravity = bodyB.useGravity = false;
    bodyA.linearDamping = bodyB.linearDamping = 0.0f;
    bodyA.restitution = bodyB.restitution = 1.0f;
    bodyA.velocity = {1, 0, 0};
    bodyA.angularVelocity = {0, 1, 0};
    bodyB.velocity = {-1, 0, 0};
    physicsWorld.set<RigidBody>(sphereA, bodyA);
    physicsWorld.set<RigidBody>(sphereB, bodyB);
    physicsWorld.set<SphereCollider>(sphereA, SphereCollider{});
    physicsWorld.set<SphereCollider>(sphereB, SphereCollider{});
    PhysicsWorld physics;
    physics.groundEnabled = false;
    physics.step(physicsWorld, physics.fixedDeltaTime * 0.5f);
    assert(physicsWorld.get<RigidBody>(sphereA)->velocity.x > 0.9f);
    physics.step(physicsWorld, physics.fixedDeltaTime * 0.5f);
    assert(physicsWorld.get<RigidBody>(sphereA)->velocity.x < 0.0f);
    assert(physicsWorld.get<RigidBody>(sphereB)->velocity.x > 0.0f);
    assert(std::abs(physicsWorld.get<Transform>(sphereA)->rotation.y) > 0.001f);
    assert(physicsWorld.get<Transform>(sphereB)->position.x -
           physicsWorld.get<Transform>(sphereA)->position.x > 0.99f);

    // Script registry drives create/update/destroy callbacks and receives input.
    World scriptWorld;
    const Entity scriptedEntity = scriptWorld.createEntity();
    scriptWorld.set<ScriptComponent>(scriptedEntity, ScriptComponent{"probe", true});
    int scriptCreates = 0, scriptUpdates = 0, scriptDestroys = 0;
    ScriptBridge scriptBridge;
    assert(scriptBridge.init());
    assert(scriptBridge.registerScript("probe", [&]() {
        return std::make_unique<ProbeScript>(scriptCreates, scriptUpdates, scriptDestroys);
    }));
    Input scriptInput;
    scriptBridge.onUpdate(scriptWorld, 1.0f / 60.0f, &scriptInput);
    scriptBridge.onUpdate(scriptWorld, 1.0f / 60.0f, &scriptInput);
    assert(scriptCreates == 1 && scriptUpdates == 2 && scriptBridge.activeCount() == 1);
    assert(scriptInput.isKeyDown(7));
    scriptWorld.get<ScriptComponent>(scriptedEntity)->enabled = false;
    scriptBridge.onUpdate(scriptWorld, 1.0f / 60.0f, &scriptInput);
    assert(scriptDestroys == 1 && scriptBridge.activeCount() == 0);
    scriptBridge.shutdown();

    Scene systemsScene("SystemsRoundTrip");
    const Entity serializedEntity = systemsScene.createEntity("Runtime Entity");
    Transform serializedTransform;
    serializedTransform.rotation = Quaternion::fromEulerDeg({15.0f, 35.0f, 5.0f});
    systemsScene.world().set<Transform>(serializedEntity, serializedTransform);
    Camera serializedCamera;
    serializedCamera.priority = 4;
    serializedCamera.nearPlane = 0.25f;
    systemsScene.world().set<Camera>(serializedEntity, serializedCamera);
    MeshRenderer serializedMesh;
    serializedMesh.primitive = "cube";
    serializedMesh.visible = false;
    systemsScene.world().set<MeshRenderer>(serializedEntity, serializedMesh);
    Light serializedLight;
    serializedLight.type = LightType::Spot;
    serializedLight.range = 18.0f;
    systemsScene.world().set<Light>(serializedEntity, serializedLight);
    RigidBody serializedBody;
    serializedBody.mass = 4.0f;
    serializedBody.restitution = 0.7f;
    systemsScene.world().set<RigidBody>(serializedEntity, serializedBody);
    systemsScene.world().set<ScriptComponent>(serializedEntity, ScriptComponent{"probe", true});
    Scene systemsRoundTrip;
    assert(systemsRoundTrip.fromJson(systemsScene.toJson()));
    const Entity roundTripEntity = 1;
    assert(systemsRoundTrip.world().get<Camera>(roundTripEntity)->priority == 4);
    assert(!systemsRoundTrip.world().get<MeshRenderer>(roundTripEntity)->visible);
    assert(systemsRoundTrip.world().get<Light>(roundTripEntity)->type == LightType::Spot);
    assert(std::abs(systemsRoundTrip.world().get<RigidBody>(roundTripEntity)->mass - 4.0f) < 1e-6f);
    assert(systemsRoundTrip.world().get<ScriptComponent>(roundTripEntity)->scriptKey == "probe");
    assert(std::abs(systemsRoundTrip.world().get<Transform>(roundTripEntity)->rotation.y -
                    serializedTransform.rotation.y) < 1e-6f);

    // --- Engine boot ---
    Engine engine("null");
    assert(engine.init(1280, 720));

    // --- Load the sample scene (mirrors the reference screenshot) ---
    // The test runs from the repo root; fall back to an inline scene if
    // the sample file is not present (e.g. copied binary).
    const bool sampleLoaded = engine.loadSceneFromFile("samples/CarScene/scene.json");
    bool loaded = sampleLoaded;
    if (!loaded) {
        const char* inlineScene = R"({
          "name": "InlineTest",
          "environment": {"skyboxExposure": 0.6, "ambientIntensity": 0.89,
                          "reflectionIntensity": 0.73},
          "entities": [
            {"name": "Camera", "position": [0, 2, 6], "components": ["camera"]},
            {"name": "Cube", "position": [0, 1, 0], "components": ["mesh"],
             "primitive": "cube", "color": [0.2, 0.8, 0.9]},
            {"name": "Ball", "position": [2, 1, 0], "components": ["mesh", "rigidbody", "spherecollider"],
             "primitive": "sphere", "radius": 0.5}
          ]
        })";
        loaded = engine.loadSceneFromJson(inlineScene);
    }
    assert(loaded);
    if (sampleLoaded) {
        assert(engine.scene().environment().skyboxTexture == "environment/city_dusk.ppm");
    }

    auto hierarchy = engine.hierarchyNames();
    std::cout << "Hierarchy (" << hierarchy.size() << " roots):";
    for (auto& n : hierarchy) std::cout << " " << n;
    std::cout << "\n";

    // Asset registration assigns stable renderer-cache keys; imported
    // material values survive glTF loading and are available to backends.
    if (sampleLoaded) {
        bool checkedImportedMeshes = false;
        engine.scene().world().each<ModelRenderer>([&](Entity, const ModelRenderer& model) {
            assert(model.meshes.size() == 8);
            const Asset* body = engine.assets().get(model.meshes.front());
            assert(body && body->mesh.cacheKey == body->id);
            assert(body->material.baseColor[1] > 0.5f);
            assert(body->material.metallic > 0.5f);
            assert(body->material.roughness < 0.5f);
            checkedImportedMeshes = true;
        });
        assert(checkedImportedMeshes);
    }

    // --- Simulate + render a few frames ---
    for (int i = 0; i < 60; ++i) {
        engine.tick(1.0f / 60.0f);
        engine.render();
    }
    RenderStats stats = engine.renderer()->stats();
    std::cout << "Draw calls: " << stats.drawCalls
              << ", triangles: " << stats.triangles << "\n";
    assert(stats.drawCalls > 0);
    if (sampleLoaded) {
        // The sample imports an 8-primitive glTF car + the asphalt plane.
        assert(stats.drawCalls == 9);
        assert(stats.triangles == 370);
    } else {
        // Fallback inline scene has a cube and a sphere.
        assert(stats.drawCalls == 2);
        assert(stats.triangles > 0);
    }

    // --- Editor-style operations ---
    Entity e = engine.createEntity("New Cube", "cube");
    assert(e != kNullEntity);
    MeshRenderer* newMesh = engine.scene().world().get<MeshRenderer>(e);
    assert(newMesh);
    const Asset* newAsset = engine.assets().get(newMesh->mesh);
    assert(newAsset && newAsset->mesh.cacheKey == newAsset->id);
    assert(engine.setEntityVisible("New Cube", false));
    assert(!newMesh->visible);
    assert(engine.setEntityVisible("New Cube", true));
    assert(newMesh->visible);
    assert(engine.adjustEntityTransform("New Cube", 0, 0, 0.25f));
    assert(std::abs(engine.scene().world().get<Transform>(e)->position.x - 0.25f) < 1e-6f);
    assert(engine.adjustEntityTransform("New Cube", 1, 2, 0.5f));
    assert(std::abs(engine.scene().world().get<Transform>(e)->scale.z - 1.5f) < 1e-6f);
    engine.destroyEntity(e);

    Entity light = engine.createEntity("Editor Light", "directional_light");
    assert(light != kNullEntity);
    Light* editorLight = engine.scene().world().get<Light>(light);
    assert(editorLight && editorLight->enabled);
    assert(engine.setEntityVisible("Editor Light", false));
    assert(!editorLight->enabled);
    engine.destroyEntity(light);
    engine.setEnvironment(EnvironmentSettings{});
    std::string json = engine.sceneToJson();
    assert(!json.empty() && json.find("\"name\"") != std::string::npos);
    if (sampleLoaded) {
        assert(json.find("models/car.gltf") != std::string::npos);
        assert(json.find("\"primitive\": \"plane\"") != std::string::npos);
        Scene roundTrip;
        assert(roundTrip.fromJson(json));
        assert(roundTrip.world().entityCount() == 7);
        bool foundModel = false;
        roundTrip.world().each<ModelRenderer>([&](Entity, const ModelRenderer& model) {
            foundModel = model.path == "models/car.gltf";
        });
        assert(foundModel);
    }

    engine.shutdown();
    std::cout << "SMOKE TEST PASSED\n";
    return 0;
}
