package com.miniengine.editor.bridge

import com.miniengine.editor.model.EnvironmentSettings

/**
 * JNI bridge to the C++ engine core (libminiengine.so).
 * Every function maps 1:1 to a JNI entry point in
 * engine/jni/JniBridge.cpp. All calls are safe no-ops until [init]
 * succeeds (e.g. in IDE preview where the native lib is absent).
 */
object NativeBridge {

    private var loaded = false

    init {
        loaded = try {
            System.loadLibrary("miniengine")
            true
        } catch (t: Throwable) {
            false
        }
    }

    val isAvailable: Boolean get() = loaded

    fun init(
        width: Int,
        height: Int,
        backend: String = "gles3",
        shaderDir: String = "",
    ): Boolean = loaded && nativeInit(width, height, backend, shaderDir)

    fun setVulkanSurface(surface: android.view.Surface?): Boolean =
        loaded && nativeSetVulkanSurface(surface)

    /** Call only on the GLSurfaceView render thread. */
    fun shutdown() { if (loaded) nativeShutdown() }
    fun tick(dt: Float) { if (loaded) nativeTick(dt) }
    fun render() { if (loaded) nativeRender() }
    fun resize(width: Int, height: Int) { if (loaded) nativeResize(width, height) }

    fun loadSceneJson(json: String): Boolean =
        loaded && nativeLoadSceneJson(json)

    fun loadSceneFile(path: String): Boolean =
        loaded && nativeLoadSceneFile(path)

    fun sceneToJson(): String = if (loaded) nativeSceneToJson() else "{}"

    fun createEntity(name: String, primitive: String): Int =
        if (loaded) nativeCreateEntity(name, primitive) else 0

    fun destroyEntity(entity: Int) { if (loaded) nativeDestroyEntity(entity) }

    fun setEnvironment(env: EnvironmentSettings) {
        if (!loaded) return
        nativeSetEnvironment(
            env.skyboxTexture, env.skyboxRotation,
            env.skyboxTintR, env.skyboxTintG, env.skyboxTintB,
            env.skyboxExposure, env.ambientIntensity, env.reflectionIntensity,
        )
    }

    /** Returns the hierarchy as a JSON array of names, e.g. ["Camera", ...]. */
    fun hierarchyJson(): String = if (loaded) nativeHierarchy() else "[]"

    // --- JNI entry points (implemented in engine/jni/JniBridge.cpp) ---
    private external fun nativeInit(width: Int, height: Int, backend: String, shaderDir: String): Boolean
    private external fun nativeSetVulkanSurface(surface: android.view.Surface?): Boolean
    private external fun nativeShutdown()
    private external fun nativeTick(dt: Float)
    private external fun nativeRender()
    private external fun nativeResize(width: Int, height: Int)
    private external fun nativeLoadSceneJson(json: String): Boolean
    private external fun nativeLoadSceneFile(path: String): Boolean
    private external fun nativeSceneToJson(): String
    private external fun nativeCreateEntity(name: String, primitive: String): Int
    private external fun nativeDestroyEntity(entity: Int)
    private external fun nativeSetEnvironment(
        skybox: String, rotation: Float,
        tintR: Float, tintG: Float, tintB: Float,
        exposure: Float, ambient: Float, reflection: Float,
    )
    private external fun nativeHierarchy(): String
}
