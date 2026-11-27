#include "mini/ecs/ECS.h"

namespace mini {

Entity World::createEntity() {
    Entity e = mNextId++;
    if (mAlive.size() <= e) mAlive.resize(e + 1, 0);
    mAlive[e] = 1;
    return e;
}

void World::destroyEntity(Entity e) {
    if (e < mAlive.size()) mAlive[e] = 0;
}

bool World::isAlive(Entity e) const {
    return e < mAlive.size() && mAlive[e] != 0;
}

} // namespace mini
