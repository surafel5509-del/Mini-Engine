#include "mini/physics/Physics.h"
#include "mini/scene/Components.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace mini {
namespace {

float inverseMass(const RigidBody* body) {
    if (!body || body->isKinematic || !std::isfinite(body->mass) || body->mass <= 0.0f) return 0.0f;
    return 1.0f / body->mass;
}

void integrateRotation(Transform& transform, const Vec3& angularVelocity, float dt) {
    const Quaternion q = transform.rotation;
    const Quaternion omega{angularVelocity.x, angularVelocity.y, angularVelocity.z, 0.0f};
    Quaternion derivative;
    derivative.x = 0.5f * (omega.w*q.x + omega.x*q.w + omega.y*q.z - omega.z*q.y);
    derivative.y = 0.5f * (omega.w*q.y - omega.x*q.z + omega.y*q.w + omega.z*q.x);
    derivative.z = 0.5f * (omega.w*q.z + omega.x*q.y - omega.y*q.x + omega.z*q.w);
    derivative.w = 0.5f * (omega.w*q.w - omega.x*q.x - omega.y*q.y - omega.z*q.z);
    Quaternion result{q.x + derivative.x*dt, q.y + derivative.y*dt,
                      q.z + derivative.z*dt, q.w + derivative.w*dt};
    const float norm = std::sqrt(result.x*result.x + result.y*result.y +
                                 result.z*result.z + result.w*result.w);
    if (norm > 1e-6f && std::isfinite(norm)) {
        result.x /= norm; result.y /= norm; result.z /= norm; result.w /= norm;
        transform.rotation = result;
    }
}

float scaledRadius(const SphereCollider& collider, const Transform& transform) {
    const float scale = std::max({std::abs(transform.scale.x), std::abs(transform.scale.y),
                                  std::abs(transform.scale.z)});
    return std::max(0.0f, collider.radius) * scale;
}

} // namespace

void PhysicsWorld::step(World& world, float elapsedSeconds) {
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.0f ||
        !std::isfinite(fixedDeltaTime) || fixedDeltaTime <= 0.0f || maxSubsteps == 0) return;
    const float frameLimit = std::max(fixedDeltaTime,
        std::isfinite(maxFrameDelta) ? maxFrameDelta : fixedDeltaTime);
    mAccumulator += std::min(elapsedSeconds, frameLimit);
    unsigned steps = 0;
    while (mAccumulator >= fixedDeltaTime && steps < maxSubsteps) {
        simulate(world, fixedDeltaTime);
        mAccumulator -= fixedDeltaTime;
        ++steps;
    }
    // Avoid an unbounded catch-up spiral after a long pause or debugger stop.
    if (steps == maxSubsteps && mAccumulator >= fixedDeltaTime)
        mAccumulator = std::fmod(mAccumulator, fixedDeltaTime);
}

void PhysicsWorld::simulate(World& world, float dt) {
    world.each<RigidBody>([&](Entity entity, RigidBody& body) {
        Transform* transform = world.get<Transform>(entity);
        if (!transform || inverseMass(&body) <= 0.0f) return;
        if (body.useGravity && std::isfinite(gravity)) body.velocity.y += gravity * dt;
        const float damping = std::max(0.0f, std::isfinite(body.linearDamping) ? body.linearDamping : 0.0f);
        const float angularDamping = std::max(0.0f,
            std::isfinite(body.angularDamping) ? body.angularDamping : 0.0f);
        body.velocity = body.velocity * std::exp(-damping * dt);
        body.angularVelocity = body.angularVelocity * std::exp(-angularDamping * dt);
        transform->position += body.velocity * dt;
        integrateRotation(*transform, body.angularVelocity, dt);

        if (groundEnabled) {
            if (const SphereCollider* collider = world.get<SphereCollider>(entity)) {
                const float radius = scaledRadius(*collider, *transform);
                const float floorHeight = groundY + radius;
                if (transform->position.y < floorHeight) {
                    transform->position.y = floorHeight;
                    if (body.velocity.y < 0.0f) {
                        body.velocity.y = -body.velocity.y * clamp(body.restitution, 0.0f, 1.0f);
                        const float friction = clamp(body.friction, 0.0f, 1.0f);
                        const float tangentScale = std::max(0.0f, 1.0f - friction * dt * 8.0f);
                        body.velocity.x *= tangentScale;
                        body.velocity.z *= tangentScale;
                    }
                }
            }
        }
    });

    struct SphereProxy { Entity entity; float minX; float maxX; float radius; };
    constexpr unsigned kSolverIterations = 4;
    for (unsigned iteration = 0; iteration < kSolverIterations; ++iteration) {
        std::vector<SphereProxy> proxies;
        world.each<SphereCollider>([&](Entity entity, const SphereCollider& collider) {
            const Transform* transform = world.get<Transform>(entity);
            if (!transform) return;
            const float radius = scaledRadius(collider, *transform);
            proxies.push_back({entity, transform->position.x - radius,
                               transform->position.x + radius, radius});
        });
        std::sort(proxies.begin(), proxies.end(), [](const SphereProxy& a, const SphereProxy& b) {
            return a.minX == b.minX ? a.entity < b.entity : a.minX < b.minX;
        });
        // Sweep-and-prune on the X axis avoids testing every pair in sparse scenes.
        for (size_t i = 0; i < proxies.size(); ++i) {
            for (size_t j = i + 1; j < proxies.size() && proxies[j].minX <= proxies[i].maxX; ++j) {
                const Entity a = proxies[i].entity, b = proxies[j].entity;
                Transform* ta = world.get<Transform>(a);
                Transform* tb = world.get<Transform>(b);
                if (!ta || !tb) continue;
                RigidBody* ra = world.get<RigidBody>(a);
                RigidBody* rb = world.get<RigidBody>(b);
                const float invA = inverseMass(ra), invB = inverseMass(rb);
                const float invTotal = invA + invB;
                if (invTotal <= 0.0f) continue;

                const Vec3 delta = tb->position - ta->position;
                const float distanceSquared = dot(delta, delta);
                const float sumRadius = proxies[i].radius + proxies[j].radius;
                if (distanceSquared >= sumRadius * sumRadius || sumRadius <= 0.0f) continue;
                const float distance = std::sqrt(std::max(distanceSquared, 0.0f));
                const Vec3 normal = distance > 1e-6f ? delta * (1.0f / distance) : Vec3{0, 1, 0};
                const float penetration = sumRadius - distance;
                const float correction = std::max(penetration - 0.001f, 0.0f) / invTotal;
                ta->position -= normal * (correction * invA);
                tb->position += normal * (correction * invB);

                Vec3 velocityA = ra ? ra->velocity : Vec3{};
                Vec3 velocityB = rb ? rb->velocity : Vec3{};
                const Vec3 relativeVelocity = velocityB - velocityA;
                const float normalSpeed = dot(relativeVelocity, normal);
                if (normalSpeed >= 0.0f) continue;
                const float restitutionA = ra ? clamp(ra->restitution, 0.0f, 1.0f) : 0.0f;
                const float restitutionB = rb ? clamp(rb->restitution, 0.0f, 1.0f) : 0.0f;
                const float impulseMagnitude = -(1.0f + std::min(restitutionA, restitutionB)) *
                                               normalSpeed / invTotal;
                const Vec3 impulse = normal * impulseMagnitude;
                if (ra && invA > 0.0f) ra->velocity -= impulse * invA;
                if (rb && invB > 0.0f) rb->velocity += impulse * invB;

                velocityA = ra ? ra->velocity : Vec3{};
                velocityB = rb ? rb->velocity : Vec3{};
                const Vec3 afterNormalRelative = velocityB - velocityA;
                Vec3 tangent = afterNormalRelative - normal * dot(afterNormalRelative, normal);
                const float tangentLength = tangent.length();
                if (tangentLength > 1e-6f) {
                    tangent = tangent * (1.0f / tangentLength);
                    float frictionImpulse = -dot(afterNormalRelative, tangent) / invTotal;
                    const float frictionA = ra ? clamp(ra->friction, 0.0f, 1.0f) : 0.6f;
                    const float frictionB = rb ? clamp(rb->friction, 0.0f, 1.0f) : 0.6f;
                    const float maxFriction = impulseMagnitude * std::sqrt(frictionA * frictionB);
                    frictionImpulse = clamp(frictionImpulse, -maxFriction, maxFriction);
                    const Vec3 friction = tangent * frictionImpulse;
                    if (ra && invA > 0.0f) ra->velocity -= friction * invA;
                    if (rb && invB > 0.0f) rb->velocity += friction * invB;
                }
            }
        }
    }
}

} // namespace mini
