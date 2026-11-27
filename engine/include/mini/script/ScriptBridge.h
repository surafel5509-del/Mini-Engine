#pragma once
#include <cstddef>
#include <functional>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "mini/ecs/ECS.h"
#include "mini/input/Input.h"

namespace mini {

struct ScriptContext {
    World& world;
    Entity entity;
    Input* input = nullptr;
    float deltaTime = 0.0f;
};

class IScript {
public:
    virtual ~IScript() = default;
    virtual void onCreate(ScriptContext&) {}
    virtual void onUpdate(ScriptContext&) {}
    virtual void onDestroy(ScriptContext&) {}
};

using ScriptFactory = std::function<std::unique_ptr<IScript>()>;

// Extensible native gameplay-script host. Factories are registered by stable
// script key; entity script components are instantiated lazily and receive
// deterministic create/update/destroy callbacks on the engine thread.
class ScriptBridge {
public:
    bool init();
    void shutdown();
    bool registerScript(const std::string& key, ScriptFactory factory);
    void unregisterScript(const std::string& key);
    void reloadScript(const std::string& key);
    void attachScript(Entity entity, const std::string& scriptKey);
    void onUpdate(World& world, float dt, Input* input = nullptr);
    size_t activeCount() const { return mInstances.size(); }

private:
    struct Instance {
        std::string key;
        std::unique_ptr<IScript> script;
        bool componentOwned = false;
        bool enabled = true;
    };
    struct Pending { Entity entity; std::string key; bool componentOwned; };
    void destroyInstance(World& world, Entity entity, Input* input);

    std::unordered_map<std::string, ScriptFactory> mFactories;
    std::unordered_map<Entity, Instance> mInstances;
    std::unordered_set<std::string> mWarnedMissing;
    std::vector<Pending> mPending;
    World* mWorld = nullptr;
    Input* mInput = nullptr;
    bool mInitialized = false;
};

} // namespace mini
