#pragma once
#include <memory>
#include <string>
#include "mini/assets/AssetManager.h"
#include "mini/audio/Audio.h"
#include "mini/core/JobSystem.h"
#include "mini/input/Input.h"
#include "mini/physics/Physics.h"
#include "mini/render/Renderer.h"
#include "mini/scene/CameraSystem.h"
#include "mini/scene/LightSystem.h"
#include "mini/scene/Scene.h"
#include "mini/script/ScriptBridge.h"

namespace mini {

// Top-level engine facade. Owns every subsystem and drives the frame
// loop: input -> scripts -> physics -> render. The Kotlin editor talks
// to the engine exclusively through this class (via JNI).
class Engine {
public:
    explicit Engine(std::string rendererBackend = "null");

    bool init(int width, int height, const std::string& shaderDir = {});
    void shutdown();
    void resize(int width, int height);
    bool setNativeSurface(void* nativeWindow);

    // Advance simulation by dt seconds (call from the game thread).
    void tick(float dt);

    // Render the active scene from the main camera.
    void render();

    // Scene management (used by the editor).
    bool loadSceneFromJson(const std::string& json);
    bool loadSceneFromFile(const std::string& path);
    Entity createEntity(const std::string& name, const std::string& primitive = "");
    void destroyEntity(Entity e);
    std::string sceneToJson() const;
    std::vector<std::string> hierarchyNames() const;

    // Environment (Inspector > Environment panel).
    void setEnvironment(const EnvironmentSettings& env);

    Scene& scene() { return *mScene; }
    AssetManager& assets() { return mAssets; }
    Input& input() { return mInput; }
    ScriptBridge& scripts() { return mScripts; }
    IRenderer* renderer() { return mRenderer.get(); }
    bool isRunning() const { return mRunning; }

private:
    std::string resolveModelPath(const std::string& path) const;

    std::string mRendererBackend;
    std::string mSceneDir;  // directory of the loaded scene file (for relative assets)
    std::string mResolvedEnvironmentPath;
    std::unique_ptr<IRenderer> mRenderer;
    std::unique_ptr<Scene> mScene;
    AssetManager mAssets;
    PhysicsWorld mPhysics;
    CameraSystem mCameras;
    LightSystem mLights;
    AudioEngine mAudio;
    Input mInput;
    ScriptBridge mScripts;
    JobSystem mJobs;
    bool mRunning = false;
    int mWidth = 0, mHeight = 0;
};

} // namespace mini
