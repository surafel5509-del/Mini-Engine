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
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
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
    isPlaying: Boolean = false,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current
    val playingState = rememberUpdatedState(isPlaying)
    var rendererStatus by remember { mutableStateOf("Starting GLES3 renderer…") }
    val surface = remember(context, scenePath, backend, shaderPath) {
        GLSurfaceView(context).apply {
            setEGLContextClientVersion(3)
            preserveEGLContextOnPause = true
            setRenderer(
                EditorRenderer(scenePath, backend, shaderPath, { playingState.value }) {
                    rendererStatus = it
                },
            )
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
            text = rendererStatus,
            color = Color.White,
            fontSize = 11.sp,
            modifier = Modifier
                .align(if (rendererStatus.startsWith("Ready")) Alignment.BottomStart else Alignment.Center)
                .padding(8.dp)
                .background(
                    if (rendererStatus.startsWith("Ready")) Color(0xCC20242A) else Color(0xEE7A302A),
                    RoundedCornerShape(6.dp),
                )
                .padding(horizontal = 12.dp, vertical = 8.dp),
        )
    }
}

private class EditorRenderer(
    private val scenePath: String,
    private val backend: String,
    private val shaderPath: String,
    private val isPlaying: () -> Boolean,
    private val onStatus: (String) -> Unit,
) : Renderer {
    private var lastFrameNanos = 0L
    private var engineReady = false

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        lastFrameNanos = 0L
        engineReady = false
        try {
            engineReady = NativeBridge.init(1280, 720, backend, shaderPath)
            if (!engineReady) {
                onStatus("Renderer initialization failed — check the native library and GLES 3 support")
                return
            }
            val sceneFile = File(scenePath)
            if (!sceneFile.isFile) {
                onStatus("Sample scene is missing: ${sceneFile.name}")
                return
            }
            if (!NativeBridge.loadSceneFile(sceneFile.absolutePath)) {
                engineReady = false
                onStatus("Scene failed to load — check the bundled CarScene assets")
                Log.e("MiniEngine", "Native engine failed to load sample scene: $scenePath")
            } else {
                onStatus("Ready · CarScene")
            }
        } catch (t: Throwable) {
            engineReady = false
            onStatus("Native startup error: ${t.javaClass.simpleName}")
            Log.e("MiniEngine", "Native renderer startup failed", t)
        }
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        if (!engineReady || width <= 0 || height <= 0) return
        try {
            NativeBridge.resize(width, height)
        } catch (t: Throwable) {
            engineReady = false
            onStatus("Viewport resize failed: ${t.javaClass.simpleName}")
            Log.e("MiniEngine", "Native viewport resize failed", t)
        }
    }

    override fun onDrawFrame(gl: GL10?) {
        if (!engineReady) return
        val now = System.nanoTime()
        val dt = if (lastFrameNanos == 0L) 1f / 60f
        else ((now - lastFrameNanos) / 1_000_000_000f).coerceIn(0f, 0.1f)
        lastFrameNanos = now
        try {
            if (isPlaying()) NativeBridge.tick(dt)
            NativeBridge.render()
        } catch (t: Throwable) {
            engineReady = false
            onStatus("Render stopped: ${t.javaClass.simpleName}")
            Log.e("MiniEngine", "Native frame failed", t)
        }
    }
}
