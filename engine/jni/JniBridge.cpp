// JNI bridge between the Kotlin editor (NativeBridge.kt) and the C++
// engine core. Compiled only for Android (see engine/CMakeLists.txt).
#include <jni.h>
#if defined(__ANDROID__)
#include <android/native_window_jni.h>
#endif
#include <memory>
#include <mutex>
#include <string>
#include "mini/engine/Engine.h"

namespace {

std::unique_ptr<mini::Engine> gEngine;
std::mutex gEngineMutex;

std::string jstringToStd(JNIEnv* env, jstring s) {
    if (!s) return {};
    const char* chars = env->GetStringUTFChars(s, nullptr);
    std::string out(chars ? chars : "");
    if (chars) env->ReleaseStringUTFChars(s, chars);
    return out;
}

} // namespace

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_miniengine_editor_bridge_NativeBridge_nativeInit(
        JNIEnv* env, jobject thiz, jint width, jint height, jstring backend,
        jstring shaderDir) {
    std::lock_guard<std::mutex> lock(gEngineMutex);
    (void)thiz;
    // init is always called from the GLSurfaceView GL thread. Recreate
    // engine state when Android replaces the EGL context.
    if (gEngine) { gEngine->shutdown(); gEngine.reset(); }
    gEngine = std::make_unique<mini::Engine>(jstringToStd(env, backend));
    return gEngine->init(width, height, jstringToStd(env, shaderDir)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_miniengine_editor_bridge_NativeBridge_nativeShutdown(
        JNIEnv*, jobject) {
    std::lock_guard<std::mutex> lock(gEngineMutex);
    if (gEngine) { gEngine->shutdown(); gEngine.reset(); }
}

JNIEXPORT void JNICALL
Java_com_miniengine_editor_bridge_NativeBridge_nativeTick(
        JNIEnv*, jobject, jfloat dt) {
    std::lock_guard<std::mutex> lock(gEngineMutex);
    if (gEngine) gEngine->tick(dt);
}

JNIEXPORT void JNICALL
Java_com_miniengine_editor_bridge_NativeBridge_nativeRender(
        JNIEnv*, jobject) {
    std::lock_guard<std::mutex> lock(gEngineMutex);
    if (gEngine) gEngine->render();
}

JNIEXPORT void JNICALL
Java_com_miniengine_editor_bridge_NativeBridge_nativeResize(
        JNIEnv*, jobject, jint width, jint height) {
    std::lock_guard<std::mutex> lock(gEngineMutex);
    if (gEngine) gEngine->resize(width, height);
}

JNIEXPORT jboolean JNICALL
Java_com_miniengine_editor_bridge_NativeBridge_nativeSetVulkanSurface(
        JNIEnv* env, jobject, jobject surface) {
    std::lock_guard<std::mutex> lock(gEngineMutex);
    if (!gEngine) return JNI_FALSE;
#if defined(__ANDROID__)
    if (!surface) return gEngine->setNativeSurface(nullptr) ? JNI_TRUE : JNI_FALSE;
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) return JNI_FALSE;
    const bool attached = gEngine->setNativeSurface(window);
    ANativeWindow_release(window); // VulkanRenderer keeps its own reference.
    return attached ? JNI_TRUE : JNI_FALSE;
#else
    (void)env;
    (void)surface;
    return JNI_FALSE;
#endif
}

JNIEXPORT jboolean JNICALL
Java_com_miniengine_editor_bridge_NativeBridge_nativeLoadSceneFile(
        JNIEnv* env, jobject, jstring path) {
    std::lock_guard<std::mutex> lock(gEngineMutex);
    if (!gEngine) return JNI_FALSE;
    return gEngine->loadSceneFromFile(jstringToStd(env, path)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_miniengine_editor_bridge_NativeBridge_nativeLoadSceneJson(
        JNIEnv* env, jobject, jstring json) {
    std::lock_guard<std::mutex> lock(gEngineMutex);
    if (!gEngine) return JNI_FALSE;
    return gEngine->loadSceneFromJson(jstringToStd(env, json)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_miniengine_editor_bridge_NativeBridge_nativeSceneToJson(
        JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> lock(gEngineMutex);
    if (!gEngine) return env->NewStringUTF("{}");
    return env->NewStringUTF(gEngine->sceneToJson().c_str());
}

JNIEXPORT jint JNICALL
Java_com_miniengine_editor_bridge_NativeBridge_nativeCreateEntity(
        JNIEnv* env, jobject, jstring name, jstring primitive) {
    std::lock_guard<std::mutex> lock(gEngineMutex);
    if (!gEngine) return 0;
    return static_cast<jint>(gEngine->createEntity(
        jstringToStd(env, name), jstringToStd(env, primitive)));
}

JNIEXPORT void JNICALL
Java_com_miniengine_editor_bridge_NativeBridge_nativeDestroyEntity(
        JNIEnv*, jobject, jint entity) {
    std::lock_guard<std::mutex> lock(gEngineMutex);
    if (gEngine) gEngine->destroyEntity(static_cast<mini::Entity>(entity));
}

JNIEXPORT void JNICALL
Java_com_miniengine_editor_bridge_NativeBridge_nativeSetEnvironment(
        JNIEnv* env, jobject, jstring skybox, jfloat rotation,
        jfloat tintR, jfloat tintG, jfloat tintB, jfloat exposure,
        jfloat ambient, jfloat reflection) {
    std::lock_guard<std::mutex> lock(gEngineMutex);
    if (!gEngine) return;
    mini::EnvironmentSettings e;
    e.skyboxTexture = jstringToStd(env, skybox);
    e.skyboxRotation = rotation;
    e.skyboxTint = {tintR, tintG, tintB};
    e.skyboxExposure = exposure;
    e.ambientIntensity = ambient;
    e.reflectionIntensity = reflection;
    gEngine->setEnvironment(e);
}

JNIEXPORT jstring JNICALL
Java_com_miniengine_editor_bridge_NativeBridge_nativeHierarchy(
        JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> lock(gEngineMutex);
    if (!gEngine) return env->NewStringUTF("[]");
    std::string out = "[";
    bool first = true;
    for (const auto& n : gEngine->hierarchyNames()) {
        if (!first) out += ",";
        out += "\"" + n + "\"";
        first = false;
    }
    out += "]";
    return env->NewStringUTF(out.c_str());
}

} // extern "C"
