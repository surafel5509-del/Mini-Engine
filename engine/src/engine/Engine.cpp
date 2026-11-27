#include "mini/engine/Engine.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include "mini/core/Log.h"
#include "mini/render/VulkanRenderer.h"

namespace mini {

Engine::Engine(std::string rendererBackend)
    : mRendererBackend(std::move(rendererBackend)) {}

bool Engine::init(int width, int height, const std::string& shaderDir) {
    mWidth = width;
    mHeight = height;

    mRenderer = createRenderer(mRendererBackend);
    if (auto* vulkan = dynamic_cast<VulkanRenderer*>(mRenderer.get());
        vulkan && !shaderDir.empty()) {
        vulkan->setShaderDir(shaderDir);
    }
    if (!mRenderer->init(width, height)) {
        // Backend unavailable (e.g. optional Vulkan dependencies): fall back to Null so
        // the editor and tests always have a working renderer.
        logWarn("Engine: '" + mRendererBackend +
                "' unavailable, falling back to NullRenderer");
        mRenderer = createRenderer("null");
        if (!mRenderer->init(width, height)) return false;
    }
    logInfo(std::string("Engine initialized with renderer: ") + mRenderer->name());

    mScene = std::make_unique<Scene>("Untitled Scene");
    mAudio.init();
    mScripts.init();
    mRunning = true;
    return true;
}

void Engine::resize(int width, int height) {
    mWidth = width;
    mHeight = height;
    if (mRenderer) mRenderer->resize(width, height);
}

bool Engine::setNativeSurface(void* nativeWindow) {
    auto* vulkan = dynamic_cast<VulkanRenderer*>(mRenderer.get());
    if (!vulkan) return false;
    return nativeWindow ? vulkan->attachSurface(nativeWindow) : vulkan->detachSurface();
}

void Engine::shutdown() {
    mRunning = false;
    mScripts.shutdown();
    mAudio.shutdown();
    if (mRenderer) mRenderer->shutdown();
    logInfo("Engine shutdown");
}

void Engine::tick(float dt) {
    if (!mRunning || !mScene) return;
    mScripts.onUpdate(mScene->world(), dt, &mInput);
    mPhysics.step(mScene->world(), dt);
    mInput.clearFrame();
}

void Engine::render() {
    if (!mRunning || !mScene || !mRenderer) return;
    World& world = mScene->world();

    const CameraFrame cameraFrame = mCameras.build(world, mWidth, mHeight);
    const Mat4& view = cameraFrame.view;
    const Mat4& proj = cameraFrame.projection;

    EnvironmentSettings renderEnvironment = mScene->environment();
    renderEnvironment.skyboxTexture = mResolvedEnvironmentPath;
    mRenderer->setEnvironment(renderEnvironment);
    mRenderer->setLights(mLights.collect(world));
    mRenderer->setCameraPosition(cameraFrame.position);
    mRenderer->beginFrame(view, proj);

    world.each<MeshRenderer>([&](Entity e, MeshRenderer& mr) {
        if (!mr.visible) return;
        AssetId meshId = mr.mesh;
        if (meshId == kNullAsset && !mr.primitive.empty()) {
            meshId = mAssets.createPrimitive(mr.primitive);
            mr.mesh = meshId;
        }
        const Asset* asset = mAssets.get(meshId);
        if (!asset || asset->type != AssetType::Mesh) return;
        const Transform* t = world.get<Transform>(e);
        if (!t) return;
        // Phase 1: local matrix only (parent chains resolve in Phase 2).
        Mat4 model = t->localMatrix();
        MaterialData material = asset->material;
        if (mr.material != kNullAsset) {
            const Asset* overrideMaterial = mAssets.get(mr.material);
            if (overrideMaterial && overrideMaterial->type == AssetType::Material)
                material = overrideMaterial->material;
        }
        for (int channel = 0; channel < 3; ++channel)
            material.baseColor[channel] *= mr.tint[channel];
        mRenderer->drawMesh(asset->mesh, model, material);
    });

    // Imported models: one draw per mesh primitive.
    world.each<ModelRenderer>([&](Entity e, ModelRenderer& mr) {
        if (!mr.visible) return;
        const Transform* t = world.get<Transform>(e);
        if (!t) return;
        Mat4 model = t->localMatrix();
        for (AssetId meshId : mr.meshes) {
            const Asset* asset = mAssets.get(meshId);
            if (!asset || asset->type != AssetType::Mesh) continue;
            mRenderer->drawMesh(asset->mesh, model, asset->material);
        }
    });

    mRenderer->endFrame();
}

std::string Engine::resolveModelPath(const std::string& path) const {
    if (path.empty() || path[0] == '/' || mSceneDir.empty()) return path;
    // Prefer the scene-relative path; fall back to the raw path.
    std::string candidate = mSceneDir + "/" + path;
    std::ifstream probe(candidate);
    return probe ? candidate : path;
}

bool Engine::loadSceneFromJson(const std::string& json) {
    auto scene = std::make_unique<Scene>();
    if (!scene->fromJson(json)) return false;
    mScene = std::move(scene);
    mResolvedEnvironmentPath = resolveModelPath(mScene->environment().skyboxTexture);
    // Resolve primitive meshes eagerly so the first frame is cheap.
    mScene->world().each<MeshRenderer>([this](Entity, MeshRenderer& mr) {
        if (mr.mesh == kNullAsset && !mr.primitive.empty()) {
            mr.mesh = mAssets.createPrimitive(mr.primitive);
        }
    });
    // Resolve imported models (glTF) to mesh assets.
    mScene->world().each<ModelRenderer>([this](Entity, ModelRenderer& mr) {
        if (!mr.path.empty() && mr.meshes.empty()) {
            mr.meshes = mAssets.importGltf(resolveModelPath(mr.path));
        }
    });
    logInfo("Engine: scene '" + mScene->name() + "' loaded (" +
            std::to_string(mScene->world().entityCount()) + " entities)");
    return true;
}

bool Engine::loadSceneFromFile(const std::string& path) {
    std::string json;
    if (!mAssets.loadSceneFile(path, json)) return false;
    size_t slash = path.find_last_of("/\\");
    mSceneDir = (slash == std::string::npos) ? "" : path.substr(0, slash);
    return loadSceneFromJson(json);
}

Entity Engine::createEntity(const std::string& name, const std::string& primitive) {
    Entity e = mScene->createEntity(name);
    if (primitive == "directional_light") {
        mScene->world().set<Light>(e, Light{});
    } else if (!primitive.empty()) {
        MeshRenderer mr;
        mr.primitive = primitive;
        mr.mesh = mAssets.createPrimitive(primitive);
        mScene->world().set<MeshRenderer>(e, mr);
    }
    return e;
}

void Engine::destroyEntity(Entity e) { mScene->destroyEntity(e); }

bool Engine::setEntityVisible(const std::string& name, bool visible) {
    World& world = mScene->world();
    bool matched = false;
    for (Entity entity = 1; entity < world.nextId(); ++entity) {
        if (!world.isAlive(entity)) continue;
        const Name* entityName = world.get<Name>(entity);
        if (!entityName || entityName->value != name) continue;
        matched = true;
        if (MeshRenderer* mesh = world.get<MeshRenderer>(entity)) mesh->visible = visible;
        if (ModelRenderer* model = world.get<ModelRenderer>(entity)) model->visible = visible;
        if (Light* light = world.get<Light>(entity)) light->enabled = visible;
    }
    return matched;
}

bool Engine::adjustEntityTransform(const std::string& name, int property, int axis, float delta) {
    if (property < 0 || property > 1 || axis < 0 || axis > 2 || !std::isfinite(delta))
        return false;
    World& world = mScene->world();
    for (Entity entity = 1; entity < world.nextId(); ++entity) {
        if (!world.isAlive(entity)) continue;
        const Name* entityName = world.get<Name>(entity);
        if (!entityName || entityName->value != name) continue;
        Transform* transform = world.get<Transform>(entity);
        if (!transform) return false;
        Vec3& vector = property == 0 ? transform->position : transform->scale;
        float* values[] = {&vector.x, &vector.y, &vector.z};
        *values[axis] += delta;
        if (property == 1) *values[axis] = std::max(0.01f, *values[axis]);
        return true;
    }
    return false;
}

std::string Engine::sceneToJson() const { return mScene->toJson(); }

std::vector<std::string> Engine::hierarchyNames() const {
    std::vector<std::string> names;
    for (Entity e : mScene->rootEntities()) {
        if (const Name* n = mScene->world().get<Name>(e)) names.push_back(n->value);
    }
    return names;
}

void Engine::setEnvironment(const EnvironmentSettings& env) {
    mScene->environment() = env;
    mResolvedEnvironmentPath = resolveModelPath(env.skyboxTexture);
    if (mRenderer) {
        EnvironmentSettings resolved = env;
        resolved.skyboxTexture = mResolvedEnvironmentPath;
        mRenderer->setEnvironment(resolved);
    }
}

} // namespace mini
