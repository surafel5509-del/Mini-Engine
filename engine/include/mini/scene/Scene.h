#pragma once
#include <string>
#include <vector>
#include "mini/ecs/ECS.h"
#include "mini/scene/Components.h"

namespace mini {

// A scene owns a World plus scene-level settings. Serialized as JSON.
class Scene {
public:
    explicit Scene(std::string name = "Untitled Scene");

    const std::string& name() const { return mName; }
    void setName(std::string n) { mName = std::move(n); }

    World& world() { return mWorld; }
    const World& world() const { return mWorld; }

    EnvironmentSettings& environment() { return mEnvironment; }
    const EnvironmentSettings& environment() const { return mEnvironment; }

    Entity createEntity(const std::string& name, const Vec3& position = {0, 0, 0});
    void destroyEntity(Entity e);

    // Direct children of the scene root (entities with no parent), in
    // creation order — this is what the editor Hierarchy panel shows.
    std::vector<Entity> rootEntities() const;

    // Serialize to / parse from JSON text. Returns false on parse error.
    std::string toJson() const;
    bool fromJson(const std::string& json);

private:
    std::string mName;
    World mWorld;
    EnvironmentSettings mEnvironment;
};

} // namespace mini
