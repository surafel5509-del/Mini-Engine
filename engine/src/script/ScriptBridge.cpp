#include "mini/script/ScriptBridge.h"
#include "mini/core/Log.h"
#include "mini/scene/Components.h"
#include <algorithm>
#include <cmath>
#include <exception>

namespace mini {

bool ScriptBridge::init() {
    mInitialized = true;
    logInfo("ScriptBridge initialized (native lifecycle and hot-reload registry ready)");
    return true;
}

void ScriptBridge::destroyInstance(World& world, Entity entity, Input* input) {
    auto it = mInstances.find(entity);
    if (it == mInstances.end()) return;
    if (it->second.script) {
        try {
            ScriptContext context{world, entity, input, 0.0f};
            it->second.script->onDestroy(context);
        } catch (const std::exception& error) {
            logError("Script onDestroy failed for '" + it->second.key + "': " + error.what());
        } catch (...) {
            logError("Script onDestroy failed for '" + it->second.key + "' (unknown exception)");
        }
    }
    mInstances.erase(it);
}

void ScriptBridge::shutdown() {
    if (mWorld) {
        while (!mInstances.empty()) destroyInstance(*mWorld, mInstances.begin()->first, mInput);
    } else {
        mInstances.clear();
    }
    mPending.clear();
    mFactories.clear();
    mWarnedMissing.clear();
    mWorld = nullptr;
    mInput = nullptr;
    mInitialized = false;
}

bool ScriptBridge::registerScript(const std::string& key, ScriptFactory factory) {
    if (key.empty() || !factory) return false;
    auto existing = mFactories.find(key);
    if (existing != mFactories.end()) {
        reloadScript(key);
        existing->second = std::move(factory);
    } else {
        mFactories.emplace(key, std::move(factory));
    }
    mWarnedMissing.erase(key);
    logInfo("Script registered: " + key);
    return true;
}

void ScriptBridge::unregisterScript(const std::string& key) {
    mFactories.erase(key);
    mWarnedMissing.erase(key);
    if (mWorld) {
        std::vector<Entity> remove;
        for (const auto& item : mInstances)
            if (item.second.key == key) remove.push_back(item.first);
        for (Entity entity : remove) destroyInstance(*mWorld, entity, mInput);
    }
}

void ScriptBridge::reloadScript(const std::string& key) {
    if (!mWorld) return;
    std::vector<Entity> reload;
    for (const auto& item : mInstances)
        if (item.second.key == key) reload.push_back(item.first);
    for (Entity entity : reload) {
        const auto it = mInstances.find(entity);
        const bool owned = it != mInstances.end() && it->second.componentOwned;
        destroyInstance(*mWorld, entity, mInput);
        if (owned) {
            if (ScriptComponent* component = mWorld->get<ScriptComponent>(entity))
                mPending.push_back({entity, component->scriptKey, true});
        }
    }
}

void ScriptBridge::attachScript(Entity entity, const std::string& scriptKey) {
    if (entity == kNullEntity || scriptKey.empty()) return;
    mPending.push_back({entity, scriptKey, false});
}

void ScriptBridge::onUpdate(World& world, float dt, Input* input) {
    if (!mInitialized || !std::isfinite(dt) || dt < 0.0f) return;
    mWorld = &world;
    mInput = input;

    world.each<ScriptComponent>([&](Entity entity, const ScriptComponent& component) {
        if (!component.enabled || component.scriptKey.empty()) return;
        const auto current = mInstances.find(entity);
        if (current != mInstances.end() && current->second.key == component.scriptKey &&
            current->second.componentOwned) return;
        const bool queued = std::any_of(mPending.begin(), mPending.end(), [&](const Pending& pending) {
            return pending.entity == entity && pending.componentOwned && pending.key == component.scriptKey;
        });
        if (!queued) mPending.push_back({entity, component.scriptKey, true});
    });

    std::vector<Entity> stale;
    for (const auto& item : mInstances) {
        const Entity entity = item.first;
        const Instance& instance = item.second;
        if (!world.isAlive(entity)) { stale.push_back(entity); continue; }
        if (instance.componentOwned) {
            const ScriptComponent* component = world.get<ScriptComponent>(entity);
            if (!component || !component->enabled || component->scriptKey != instance.key)
                stale.push_back(entity);
        }
    }
    for (Entity entity : stale) destroyInstance(world, entity, input);

    std::vector<Pending> pending;
    pending.swap(mPending);
    for (const Pending& item : pending) {
        if (!world.isAlive(item.entity)) continue;
        if (item.componentOwned) {
            const ScriptComponent* component = world.get<ScriptComponent>(item.entity);
            if (!component || !component->enabled || component->scriptKey != item.key) continue;
        }
        const auto active = mInstances.find(item.entity);
        if (active != mInstances.end()) {
            if (active->second.key == item.key) continue;
            destroyInstance(world, item.entity, input);
        }
        const auto factory = mFactories.find(item.key);
        if (factory == mFactories.end()) {
            if (mWarnedMissing.insert(item.key).second)
                logWarn("Script factory not registered yet: " + item.key);
            mPending.push_back(item);
            continue;
        }
        std::unique_ptr<IScript> script;
        try {
            script = factory->second();
        } catch (const std::exception& error) {
            logError("Script factory failed for '" + item.key + "': " + error.what());
            continue;
        } catch (...) {
            logError("Script factory failed for '" + item.key + "' (unknown exception)");
            continue;
        }
        if (!script) {
            logError("Script factory returned null: " + item.key);
            continue;
        }
        auto inserted = mInstances.emplace(item.entity,
            Instance{item.key, std::move(script), item.componentOwned, true});
        try {
            ScriptContext context{world, item.entity, input, dt};
            inserted.first->second.script->onCreate(context);
        } catch (const std::exception& error) {
            logError("Script onCreate failed for '" + item.key + "': " + error.what());
            destroyInstance(world, item.entity, input);
        } catch (...) {
            logError("Script onCreate failed for '" + item.key + "' (unknown exception)");
            destroyInstance(world, item.entity, input);
        }
    }

    std::vector<Entity> failed;
    for (auto& item : mInstances) {
        Instance& instance = item.second;
        if (!instance.enabled || !instance.script) continue;
        try {
            ScriptContext context{world, item.first, input, dt};
            instance.script->onUpdate(context);
        } catch (const std::exception& error) {
            logError("Script onUpdate disabled '" + instance.key + "': " + error.what());
            instance.enabled = false;
            failed.push_back(item.first);
        } catch (...) {
            logError("Script onUpdate disabled '" + instance.key + "' (unknown exception)");
            instance.enabled = false;
            failed.push_back(item.first);
        }
    }
    for (Entity entity : failed) {
        const auto instance = mInstances.find(entity);
        if (instance != mInstances.end() && instance->second.componentOwned) {
            if (ScriptComponent* component = world.get<ScriptComponent>(entity))
                component->enabled = false;
        }
        destroyInstance(world, entity, input);
    }
}

} // namespace mini
