package com.miniengine.editor.ui.components

import android.opengl.GLSurfaceView
import android.util.Log
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.miniengine.editor.bridge.NativeBridge
import java.io.File
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10
import android.opengl.GLSurfaceView.Renderer

/** Native OpenGL ES viewport. Engine init and all GL calls stay on the EGL render thread. */
@Composable
fun NativeViewport(
    scenePath: String,
    backend: String = "gles3",
    shaderPath: String = "",
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current
    val surface = remember(context) {
        GLSurfaceView(context).apply {
            setEGLContextClientVersion(3)
            preserveEGLContextOnPause = true
            setRenderer(EditorRenderer(scenePath, backend, shaderPath))
            renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY
        }
    }

    DisposableEffect(lifecycleOwner, surface) {
        var resumed = false
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_RESUME -> if (!resumed) {
                    surface.onResume()
                    resumed = true
                }
                Lifecycle.Event.ON_PAUSE -> if (resumed) {
                    surface.onPause()
                    resumed = false
                }
                else -> Unit
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        if (lifecycleOwner.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED) && !resumed) {
            surface.onResume()
            resumed = true
        }
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
            // The GL thread owns native renderer teardown; Android releases
            // the context/surface when the view is detached.
        }
    }

    Box(modifier = modifier.background(Color(0xFF080A0D))) {
        AndroidView(
            factory = { surface },
            modifier = Modifier.fillMaxSize(),
        )
        Text(
            text = "GLES3 · Native Scene",
            color = Color.White,
            fontSize = 10.sp,
            modifier = Modifier
                .align(Alignment.BottomStart)
                .padding(8.dp)
                .background(Color(0xCC20242A), RoundedCornerShape(4.dp))
                .padding(horizontal = 8.dp, vertical = 4.dp),
        )
    }
}

private class EditorRenderer(
    private val scenePath: String,
    private val backend: String,
    private val shaderPath: String,
) : Renderer {
    private var lastFrameNanos = 0L
    private var engineReady = false

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        lastFrameNanos = 0L
        engineReady = NativeBridge.init(1280, 720, backend, shaderPath)
        if (engineReady) {
            val sceneFile = File(scenePath)
            if (sceneFile.isFile && !NativeBridge.loadSceneFile(sceneFile.absolutePath)) {
                Log.e("MiniEngine", "Native engine failed to load sample scene: $scenePath")
            }
        } else {
            Log.e("MiniEngine", "Native engine initialization failed")
        }
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        if (engineReady && width > 0 && height > 0) NativeBridge.resize(width, height)
    }

    override fun onDrawFrame(gl: GL10?) {
        if (!engineReady) return
        val now = System.nanoTime()
        val dt = if (lastFrameNanos == 0L) 1f / 60f
        else ((now - lastFrameNanos) / 1_000_000_000f).coerceIn(0f, 0.1f)
        lastFrameNanos = now
        NativeBridge.tick(dt)
        NativeBridge.render()
    }
}
