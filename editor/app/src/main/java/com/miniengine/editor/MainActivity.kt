package com.miniengine.editor

import android.os.Bundle
import android.util.Log
import java.io.File
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.SystemBarStyle
import androidx.activity.enableEdgeToEdge
import com.miniengine.editor.ui.EditorScreen
import com.miniengine.editor.ui.theme.MiniEngineTheme

/** Entry point. Bundles sample files, then mounts the Compose editor. */
class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge(
            statusBarStyle = SystemBarStyle.dark(android.graphics.Color.TRANSPARENT),
            navigationBarStyle = SystemBarStyle.dark(android.graphics.Color.TRANSPARENT),
        )

        // Copy the bundled sample project to app-private storage. The
        // native asset importer uses ordinary filesystem paths for glTF.
        val sampleDir = File(filesDir, "CarScene")
        try {
            if (!File(sampleDir, "scene.json").isFile) {
                copyAssetTree("CarScene", sampleDir)
            }
            if (assets.list("vulkan").orEmpty().isNotEmpty()) {
                copyAssetTree("vulkan", File(filesDir, "MiniEngine/shaders"))
            }
        } catch (t: Throwable) {
            Log.e("MiniEngine", "Could not unpack bundled engine assets", t)
        }

        setContent {
            MiniEngineTheme {
                EditorScreen(
                    scenePath = File(sampleDir, "scene.json").absolutePath,
                    rendererBackend = BuildConfig.ENGINE_RENDERER,
                    shaderPath = File(filesDir, "MiniEngine/shaders").absolutePath,
                )
            }
        }
    }

    private fun copyAssetTree(assetPath: String, destination: File) {
        val children = assets.list(assetPath).orEmpty()
        if (children.isNotEmpty()) {
            destination.mkdirs()
            for (child in children) copyAssetTree("$assetPath/$child", File(destination, child))
        } else {
            destination.parentFile?.mkdirs()
            assets.open(assetPath).use { input ->
                destination.outputStream().use { output -> input.copyTo(output) }
            }
        }
    }

    override fun onDestroy() {
        // GLES resources are owned by GLSurfaceView's render thread. Avoid
        // calling JNI teardown from the Activity/UI thread without a GL context.
        super.onDestroy()
    }
}
