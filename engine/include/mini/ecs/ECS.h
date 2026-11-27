#pragma once
#include <memory>
#include <optional>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include "mini/core/Types.h"

namespace mini {

// Entity-Component-System world with SoA (structure-of-arrays) pools.
// Each component type gets a flat vector indexed by entity id; empty
// slots are std::nullopt. Iteration is a simple linear scan, which is
// cache-friendly and branch-light — good enough for thousands of
// entities and trivial to extend with sparse sets later.
class World {
public:
    Entity createEntity();
    void destroyEntity(Entity e);
    bool isAlive(Entity e) const;
    u32 entityCount() const { return static_cast<u32>(mAlive.size()); }
    Entity nextId() const { return mNextId; }  // one past the highest id ever issued

    template <typename T>
    void set(Entity e, const T& value) {
        pool<T>()[e] = value;
    }

    template <typename T>
    T* get(Entity e) {
        auto& p = pool<T>();
        if (e >= p.size() || !p[e].has_value()) return nullptr;
        return &(*p[e]);
    }

    template <typename T>
    const T* get(Entity e) const {
        auto& p = pool<T>();
        if (e >= p.size() || !p[e].has_value()) return nullptr;
        return &(*p[e]);
    }

    template <typename T>
    bool has(Entity e) const {
        auto& p = pool<T>();
        return e < p.size() && p[e].has_value();
    }

    template <typename T>
    void remove(Entity e) {
        auto& p = pool<T>();
        if (e < p.size()) p[e].reset();
    }

    // Iterate all alive entities that have component T.
    template <typename T, typename F>
    void each(F&& fn) {
        auto& p = pool<T>();
        for (u32 i = 1; i < p.size(); ++i) {
            if (p[i].has_value() && isAlive(i)) fn(static_cast<Entity>(i), *p[i]);
        }
    }

    template <typename T, typename F>
    void each(F&& fn) const {
        const auto& p = pool<T>();
        for (u32 i = 1; i < p.size(); ++i) {
            if (p[i].has_value() && isAlive(i)) fn(static_cast<Entity>(i), *p[i]);
        }
    }

private:
    template <typename T>
    std::vector<std::optional<T>>& pool() {
        auto key = std::type_index(typeid(T));
        auto it = mPools.find(key);
        if (it == mPools.end()) {
            using Vec = std::vector<std::optional<T>>;
            auto* raw = new Vec(mNextId + 1);
            std::unique_ptr<void, void (*)(void*)> holder(
                raw, [](void* p) { delete static_cast<Vec*>(p); });
            it = mPools.emplace(key, std::move(holder)).first;
        }
        auto& vec = *static_cast<std::vector<std::optional<T>>*>(it->second.get());
        if (vec.size() < mNextId + 1) vec.resize(mNextId + 1);
        return vec;
    }

    template <typename T>
    const std::vector<std::optional<T>>& pool() const {
        auto it = mPools.find(std::type_index(typeid(T)));
        static const std::vector<std::optional<T>> kEmpty;
        if (it == mPools.end()) return kEmpty;
        return *static_cast<const std::vector<std::optional<T>>*>(it->second.get());
    }

    std::unordered_map<std::type_index,
                       std::unique_ptr<void, void (*)(void*)>> mPools;
    std::vector<char> mAlive{0}; // index 0 unused (null entity)
    Entity mNextId = 1;
};

} // namespace mini
