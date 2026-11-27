#include "mini/scene/Scene.h"
#include <cmath>
#include <sstream>
#include "mini/core/Json.h"
#include "mini/core/Log.h"

namespace mini {

using json::Value;

namespace {

bool readVec3(const Value* j, Vec3& out) {
    if (!j || !j->isArray() || j->array.size() != 3) return false;
    out = {static_cast<f32>(j->array[0].number),
           static_cast<f32>(j->array[1].number),
           static_cast<f32>(j->array[2].number)};
    return true;
}

bool readQuaternion(const Value* j, Quaternion& out) {
    if (!j || !j->isArray() || j->array.size() != 4) return false;
    Quaternion q{static_cast<f32>(j->array[0].number),
                 static_cast<f32>(j->array[1].number),
                 static_cast<f32>(j->array[2].number),
                 static_cast<f32>(j->array[3].number)};
    const float norm = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (!std::isfinite(norm) || norm < 1e-6f) return false;
    out = {q.x / norm, q.y / norm, q.z / norm, q.w / norm};
    return true;
}

} // namespace

Scene::Scene(std::string name) : mName(std::move(name)) {}

Entity Scene::createEntity(const std::string& name, const Vec3& position) {
    Entity e = mWorld.createEntity();
    mWorld.set<Name>(e, Name{name});
    Transform t;
    t.position = position;
    mWorld.set<Transform>(e, t);
    return e;
}

void Scene::destroyEntity(Entity e) { mWorld.destroyEntity(e); }

std::vector<Entity> Scene::rootEntities() const {
    std::vector<Entity> roots;
    // Entities are created in increasing id order, so a linear scan
    // preserves hierarchy order.
    for (Entity e = 1; e < mWorld.nextId(); ++e) {
        if (!mWorld.isAlive(e)) continue;
        const Transform* t = mWorld.get<Transform>(e);
        if (!t || t->parent == kNullEntity) roots.push_back(e);
    }
    return roots;
}

std::string Scene::toJson() const {
    std::ostringstream os;
    os << "{\n  \"name\": \"" << json::escape(mName) << "\",\n";
    os << "  \"environment\": {\n";
    os << "    \"skyboxTexture\": \"" << json::escape(mEnvironment.skyboxTexture) << "\",\n";
    os << "    \"skyboxRotation\": " << mEnvironment.skyboxRotation << ",\n";
    os << "    \"skyboxTint\": [" << mEnvironment.skyboxTint.x << ", "
       << mEnvironment.skyboxTint.y << ", " << mEnvironment.skyboxTint.z << "],\n";
    os << "    \"skyboxExposure\": " << mEnvironment.skyboxExposure << ",\n";
    os << "    \"ambientIntensity\": " << mEnvironment.ambientIntensity << ",\n";
    os << "    \"reflectionIntensity\": " << mEnvironment.reflectionIntensity << "\n";
    os << "  },\n  \"entities\": [\n";

    bool first = true;
    for (Entity e = 1; e < mWorld.nextId(); ++e) {
        if (!mWorld.isAlive(e)) continue;
        const Name* n = mWorld.get<Name>(e);
        const Transform* t = mWorld.get<Transform>(e);
        if (!n || !t) continue;
        if (!first) os << ",\n";
        first = false;
        os << "    {\n";
        os << "      \"name\": \"" << json::escape(n->value) << "\",\n";
        os << "      \"position\": [" << t->position.x << ", " << t->position.y << ", " << t->position.z << "],\n";
        os << "      \"scale\": [" << t->scale.x << ", " << t->scale.y << ", " << t->scale.z << "],\n";
        os << "      \"rotationQuaternion\": [" << t->rotation.x << ", " << t->rotation.y
           << ", " << t->rotation.z << ", " << t->rotation.w << "],\n";
        os << "      \"components\": [";
        bool cfirst = true;
        auto addComp = [&](const char* c) {
            if (!cfirst) os << ", ";
            os << "\"" << c << "\"";
            cfirst = false;
        };
        if (mWorld.has<Camera>(e)) addComp("camera");
        if (mWorld.has<Light>(e)) addComp("light");
        if (mWorld.has<MeshRenderer>(e)) addComp("mesh");
        if (mWorld.has<ModelRenderer>(e)) addComp("model");
        if (mWorld.has<ScriptComponent>(e)) addComp("script");
        if (mWorld.has<RigidBody>(e)) addComp("rigidbody");
        if (mWorld.has<SphereCollider>(e)) addComp("spherecollider");
        os << "]";
        if (const Camera* cam = mWorld.get<Camera>(e))
            os << ",\n      \"fovYDeg\": " << cam->fovYDeg
               << ",\n      \"nearPlane\": " << cam->nearPlane
               << ",\n      \"farPlane\": " << cam->farPlane
               << ",\n      \"cameraPriority\": " << cam->priority
               << ",\n      \"mainCamera\": " << (cam->isMain ? "true" : "false");
        if (const Light* light = mWorld.get<Light>(e)) {
            const char* type = light->type == LightType::Point ? "point"
                : light->type == LightType::Spot ? "spot" : "directional";
            os << ",\n      \"lightType\": \"" << type << "\""
               << ",\n      \"color\": [" << light->color.x << ", "
               << light->color.y << ", " << light->color.z << "]"
               << ",\n      \"lightDirection\": [" << light->localDirection.x << ", "
               << light->localDirection.y << ", " << light->localDirection.z << "]"
               << ",\n      \"intensity\": " << light->intensity
               << ",\n      \"range\": " << light->range
               << ",\n      \"innerConeDegrees\": " << light->innerConeDegrees
               << ",\n      \"outerConeDegrees\": " << light->outerConeDegrees
               << ",\n      \"lightEnabled\": " << (light->enabled ? "true" : "false");
        }
        if (const MeshRenderer* mr = mWorld.get<MeshRenderer>(e)) {
            os << ",\n      \"primitive\": \"" << json::escape(mr->primitive) << "\""
               << ",\n      \"color\": [" << mr->tint[0] << ", " << mr->tint[1]
               << ", " << mr->tint[2] << "]"
               << ",\n      \"visible\": " << (mr->visible ? "true" : "false");
        }
        if (const ModelRenderer* mr = mWorld.get<ModelRenderer>(e))
            os << ",\n      \"model\": \"" << json::escape(mr->path) << "\""
               << ",\n      \"visible\": " << (mr->visible ? "true" : "false");
        if (const ScriptComponent* script = mWorld.get<ScriptComponent>(e))
            os << ",\n      \"scriptKey\": \"" << json::escape(script->scriptKey) << "\""
               << ",\n      \"scriptEnabled\": " << (script->enabled ? "true" : "false");
        if (const RigidBody* body = mWorld.get<RigidBody>(e))
            os << ",\n      \"mass\": " << body->mass
               << ",\n      \"velocity\": [" << body->velocity.x << ", " << body->velocity.y
               << ", " << body->velocity.z << "]"
               << ",\n      \"angularVelocity\": [" << body->angularVelocity.x << ", "
               << body->angularVelocity.y << ", " << body->angularVelocity.z << "]"
               << ",\n      \"linearDamping\": " << body->linearDamping
               << ",\n      \"angularDamping\": " << body->angularDamping
               << ",\n      \"restitution\": " << body->restitution
               << ",\n      \"friction\": " << body->friction
               << ",\n      \"useGravity\": " << (body->useGravity ? "true" : "false")
               << ",\n      \"isKinematic\": " << (body->isKinematic ? "true" : "false");
        if (const SphereCollider* sc = mWorld.get<SphereCollider>(e))
            os << ",\n      \"radius\": " << sc->radius;
        os << "\n    }";
    }
    os << "\n  ]\n}\n";
    return os.str();
}

bool Scene::fromJson(const std::string& json) {
    Value root;
    json::Parser parser(json);
    if (!parser.parse(root) || !root.isObject()) {
        logError("Scene::fromJson: invalid JSON");
        return false;
    }

    if (const Value* n = root.find("name"); n && n->isString()) {
        mName = n->str;
    }

    if (const Value* env = root.find("environment")) {
        if (const Value* v = env->find("skyboxTexture"); v && v->isString())
            mEnvironment.skyboxTexture = v->str;
        if (const Value* v = env->find("skyboxRotation"); v && v->isNumber())
            mEnvironment.skyboxRotation = static_cast<f32>(v->number);
        readVec3(env->find("skyboxTint"), mEnvironment.skyboxTint);
        if (const Value* v = env->find("skyboxExposure"); v && v->isNumber())
            mEnvironment.skyboxExposure = static_cast<f32>(v->number);
        if (const Value* v = env->find("ambientIntensity"); v && v->isNumber())
            mEnvironment.ambientIntensity = static_cast<f32>(v->number);
        if (const Value* v = env->find("reflectionIntensity"); v && v->isNumber())
            mEnvironment.reflectionIntensity = static_cast<f32>(v->number);
    }

    const Value* entities = root.find("entities");
    if (!entities || !entities->isArray()) return true;

    for (const Value& ej : entities->array) {
        std::string name = "Entity";
        if (const Value* n = ej.find("name"); n && n->isString()) name = n->str;

        Vec3 pos{0, 0, 0}, scl{1, 1, 1};
        readVec3(ej.find("position"), pos);
        readVec3(ej.find("scale"), scl);

        Entity e = createEntity(name, pos);
        Transform* t = mWorld.get<Transform>(e);
        t->scale = scl;
        readQuaternion(ej.find("rotationQuaternion"), t->rotation);

        const Value* comps = ej.find("components");
        if (!comps || !comps->isArray()) continue;
        for (const Value& c : comps->array) {
            if (!c.isString()) continue;
            if (c.str == "camera") {
                Camera cam;
                if (const Value* f = ej.find("fovYDeg"); f && f->isNumber())
                    cam.fovYDeg = static_cast<f32>(f->number);
                if (const Value* n = ej.find("nearPlane"); n && n->isNumber())
                    cam.nearPlane = static_cast<f32>(n->number);
                if (const Value* f = ej.find("farPlane"); f && f->isNumber())
                    cam.farPlane = static_cast<f32>(f->number);
                if (const Value* p = ej.find("cameraPriority"); p && p->isNumber())
                    cam.priority = static_cast<int>(p->number);
                if (const Value* main = ej.find("mainCamera"); main && main->type == Value::Type::Bool)
                    cam.isMain = main->boolean;
                mWorld.set<Camera>(e, cam);
            } else if (c.str == "light") {
                Light light;
                readVec3(ej.find("color"), light.color);
                readVec3(ej.find("lightDirection"), light.localDirection);
                if (const Value* type = ej.find("lightType"); type && type->isString()) {
                    if (type->str == "point") light.type = LightType::Point;
                    else if (type->str == "spot") light.type = LightType::Spot;
                }
                if (const Value* i = ej.find("intensity"); i && i->isNumber())
                    light.intensity = static_cast<f32>(i->number);
                if (const Value* r = ej.find("range"); r && r->isNumber())
                    light.range = static_cast<f32>(r->number);
                if (const Value* a = ej.find("innerConeDegrees"); a && a->isNumber())
                    light.innerConeDegrees = static_cast<f32>(a->number);
                if (const Value* a = ej.find("outerConeDegrees"); a && a->isNumber())
                    light.outerConeDegrees = static_cast<f32>(a->number);
                if (const Value* enabled = ej.find("lightEnabled"); enabled &&
                    enabled->type == Value::Type::Bool) light.enabled = enabled->boolean;
                mWorld.set<Light>(e, light);
            } else if (c.str == "mesh") {
                MeshRenderer mr;
                if (const Value* p = ej.find("primitive"); p && p->isString())
                    mr.primitive = p->str;
                Vec3 tint{1, 1, 1};
                if (readVec3(ej.find("color"), tint)) {
                    mr.tint[0] = tint.x; mr.tint[1] = tint.y; mr.tint[2] = tint.z;
                }
                if (const Value* visible = ej.find("visible"); visible &&
                    visible->type == Value::Type::Bool) mr.visible = visible->boolean;
                mWorld.set<MeshRenderer>(e, mr);
            } else if (c.str == "model") {
                ModelRenderer mr;
                if (const Value* p = ej.find("model"); p && p->isString())
                    mr.path = p->str;
                if (const Value* visible = ej.find("visible"); visible &&
                    visible->type == Value::Type::Bool) mr.visible = visible->boolean;
                mWorld.set<ModelRenderer>(e, mr);
            } else if (c.str == "script") {
                ScriptComponent script;
                if (const Value* p = ej.find("scriptKey"); p && p->isString())
                    script.scriptKey = p->str;
                if (const Value* enabled = ej.find("scriptEnabled"); enabled &&
                    enabled->type == Value::Type::Bool) script.enabled = enabled->boolean;
                mWorld.set<ScriptComponent>(e, script);
            } else if (c.str == "rigidbody") {
                RigidBody body;
                if (const Value* mass = ej.find("mass"); mass && mass->isNumber())
                    body.mass = static_cast<f32>(mass->number);
                readVec3(ej.find("velocity"), body.velocity);
                readVec3(ej.find("angularVelocity"), body.angularVelocity);
                if (const Value* damping = ej.find("linearDamping"); damping && damping->isNumber())
                    body.linearDamping = static_cast<f32>(damping->number);
                if (const Value* damping = ej.find("angularDamping"); damping && damping->isNumber())
                    body.angularDamping = static_cast<f32>(damping->number);
                if (const Value* restitution = ej.find("restitution"); restitution && restitution->isNumber())
                    body.restitution = static_cast<f32>(restitution->number);
                if (const Value* friction = ej.find("friction"); friction && friction->isNumber())
                    body.friction = static_cast<f32>(friction->number);
                if (const Value* gravity = ej.find("useGravity"); gravity && gravity->type == Value::Type::Bool)
                    body.useGravity = gravity->boolean;
                if (const Value* kinematic = ej.find("isKinematic"); kinematic && kinematic->type == Value::Type::Bool)
                    body.isKinematic = kinematic->boolean;
                mWorld.set<RigidBody>(e, body);
            } else if (c.str == "spherecollider") {
                SphereCollider sc;
                if (const Value* r = ej.find("radius"); r && r->isNumber())
                    sc.radius = static_cast<f32>(r->number);
                mWorld.set<SphereCollider>(e, sc);
            }
        }
    }
    return true;
}

} // namespace mini
