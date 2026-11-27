plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

val engineRenderer = providers.gradleProperty("miniRenderer").orElse("gles3").get()

android {
    namespace = "com.miniengine.editor"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.miniengine.editor"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "0.1.0"
        buildConfigField("String", "ENGINE_RENDERER", "\"$engineRenderer\"")

        // Engine ships as libminiengine.so (arm64 primary, armv7 fallback).
        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
    buildFeatures {
        compose = true
        buildConfig = true
    }
    ndkVersion = "26.1.10909125"

    sourceSets["main"].assets.srcDir("../../samples")
    sourceSets["main"].assets.srcDir(layout.buildDirectory.dir("generated/vulkanAssets"))

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
}

val generatedVulkanAssets = layout.buildDirectory.dir("generated/vulkanAssets")
val compileVulkanShaders = tasks.register("compileVulkanShaders") {
    val shaderSources = files("../../engine/shaders/main.vert", "../../engine/shaders/main.frag")
    inputs.files(shaderSources)
    outputs.dir(generatedVulkanAssets)
    doLast {
        val ndkRoot = System.getenv("ANDROID_NDK_HOME")
            ?: System.getenv("ANDROID_NDK_ROOT")
        val sdkRoot = System.getenv("ANDROID_SDK_ROOT") ?: System.getenv("ANDROID_HOME")
        val sdkNdkCompiler = sdkRoot?.let { root ->
            file("$root/ndk").listFiles().orEmpty()
                .sortedByDescending { it.name }
                .map { File(it, "shader-tools/glslc") }
                .firstOrNull { it.canExecute() }
                ?.absolutePath
        }
        val vulkanSdk = System.getenv("VULKAN_SDK")
        val candidates = listOfNotNull(
            ndkRoot?.let { "$it/shader-tools/glslc" },
            sdkNdkCompiler,
            vulkanSdk?.let { "$it/bin/glslc" },
            "glslc",
        )
        val compiler = candidates.firstOrNull { candidate ->
            candidate == "glslc" || file(candidate).canExecute()
        }
        if (compiler == null) {
            logger.warn("Vulkan shader compiler glslc not found; Vulkan SPIR-V assets are omitted. GLES3 remains available.")
            return@doLast
        }
        val outDir = generatedVulkanAssets.get().dir("vulkan").asFile
        outDir.mkdirs()
        for (source in shaderSources.files) {
            val output = File(outDir, if (source.name.endsWith(".vert")) "vert.spv" else "frag.spv")
            val result = exec {
                executable = compiler
                args(source.absolutePath, "-o", output.absolutePath)
                isIgnoreExitValue = true
            }
            check(result.exitValue == 0) { "glslc failed to compile ${source.name}" }
        }
        logger.lifecycle("Compiled Vulkan SPIR-V shaders into ${outDir.absolutePath}")
    }
}

tasks.named("preBuild").configure { dependsOn(compileVulkanShaders) }

dependencies {
    val composeBom = platform("androidx.compose:compose-bom:2024.06.00")
    implementation(composeBom)
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.activity:activity-compose:1.9.2")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.8.3")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.8.3")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.8.1")
}
