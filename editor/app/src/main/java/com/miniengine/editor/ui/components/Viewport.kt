package com.miniengine.editor.ui.components

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.miniengine.editor.model.EnvironmentSettings
import kotlin.math.cos
import kotlin.math.sin


/** Scales RGB channels (used to apply exposure/tint to preview colors). */
private fun Color.exposed(s: Float): Color = Color(
    (red * s).coerceIn(0f, 1f),
    (green * s).coerceIn(0f, 1f),
    (blue * s).coerceIn(0f, 1f),
    alpha,
)

/**
 * Center viewport. Renders a stylized preview of the active scene
 * (city backdrop, asphalt, car, light gizmos) that reacts to the
 * Environment settings. When the native GLES renderer is running on a
 * device, this same area hosts the GLSurfaceView (see MainActivity).
 */
@Composable
fun Viewport(
    environment: EnvironmentSettings,
    scenePath: String = "",
    rendererBackend: String = "gles3",
    shaderPath: String = "",
    modifier: Modifier = Modifier,
) {
    if (scenePath.isNotBlank() && com.miniengine.editor.bridge.NativeBridge.isAvailable) {
        if (rendererBackend.equals("vulkan", ignoreCase = true)) {
            VulkanViewport(scenePath, shaderPath, modifier)
        } else {
            NativeViewport(scenePath, rendererBackend, shaderPath, modifier)
        }
        return
    }
    Box(modifier = modifier.fillMaxSize().background(Color.Black)) {
        Canvas(modifier = Modifier.fillMaxSize()) {
            val w = size.width
            val h = size.height
            val exposure = 0.4f + environment.skyboxExposure
            val tint = Color(
                environment.skyboxTintR,
                environment.skyboxTintG,
                environment.skyboxTintB,
            )

            // Sky / building backdrop (dusk city).
            drawRect(
                brush = Brush.verticalGradient(
                    listOf(
                        Color(0xFF3A4A5A).exposed(exposure),
                        Color(0xFF6B7A8A).exposed(exposure),
                        Color(0xFF2A2F36).exposed(exposure),
                    ),
                ),
                size = Size(w, h * 0.55f),
            )
            // Building windows strip.
            for (i in 0 until 14) {
                val x = w * (i / 14f)
                drawRect(
                    color = Color(0xFF8A94A0).exposed(exposure * 0.7f),
                    topLeft = Offset(x, h * 0.08f),
                    size = Size(w / 14f - 4f, h * 0.30f),
                )
            }
            // Orange signage line.
            drawRect(
                color = Color(0xFFFF7A1A).exposed(exposure),
                topLeft = Offset(w * 0.30f, h * 0.10f),
                size = Size(w * 0.45f, 4f),
            )
            // Asphalt ground.
            drawRect(
                brush = Brush.verticalGradient(
                    listOf(Color(0xFF2B2F36).exposed(exposure), Color(0xFF14161A).exposed(exposure)),
                ),
                topLeft = Offset(0f, h * 0.55f),
                size = Size(w, h * 0.45f),
            )

            // Car (stylized side profile, teal like the sample scene).
            val carW = w * 0.42f
            val carH = h * 0.20f
            val carX = w * 0.30f
            val carY = h * 0.52f
            val carColor = Color(0xFF0E9E96).exposed(exposure).exposed(tint.red).exposed(tint.green)
            val body = Path().apply {
                moveTo(carX, carY + carH)
                lineTo(carX + carW * 0.05f, carY + carH * 0.55f)
                lineTo(carX + carW * 0.22f, carY + carH * 0.18f)
                lineTo(carX + carW * 0.55f, carY + carH * 0.05f)
                lineTo(carX + carW * 0.82f, carY + carH * 0.22f)
                lineTo(carX + carW, carY + carH * 0.45f)
                lineTo(carX + carW, carY + carH)
                close()
            }
            drawPath(body, carColor)
            // Cabin windows.
            drawRect(
                color = Color(0xFF2A2018).exposed(exposure),
                topLeft = Offset(carX + carW * 0.28f, carY + carH * 0.14f),
                size = Size(carW * 0.48f, carH * 0.30f),
            )
            // Wheels.
            val wheelR = carH * 0.22f
            listOf(carX + carW * 0.18f, carX + carW * 0.80f).forEach { cx ->
                drawCircle(Color(0xFF111111), wheelR, Offset(cx, carY + carH))
                drawCircle(Color(0xFF9AA0A8), wheelR * 0.55f, Offset(cx, carY + carH))
            }
            // Headlight glow (reflection intensity).
            drawCircle(
                Color(0xFFFFF2C0).copy(alpha = environment.reflectionIntensity * 0.5f),
                carH * 0.16f,
                Offset(carX + carW * 0.98f, carY + carH * 0.55f),
            )

            // Light gizmos (sun icons) like the reference editor.
            val gizmos = listOf(
                Offset(w * 0.52f, h * 0.22f) to 26f,
                Offset(w * 0.56f, h * 0.30f) to 16f,
                Offset(w * 0.57f, h * 0.16f) to 14f,
                Offset(w * 0.32f, h * 0.40f) to 20f,
                Offset(w * 0.68f, h * 0.44f) to 30f,
                Offset(w * 0.57f, h * 0.50f) to 34f,
            )
            gizmos.forEach { (center, r) ->
                drawCircle(Color.White, r * 0.55f, center)
                for (k in 0 until 8) {
                    val ang = k * Math.PI / 4
                    val inner = Offset(
                        center.x + cos(ang).toFloat() * r * 0.75f,
                        center.y + sin(ang).toFloat() * r * 0.75f,
                    )
                    val outer = Offset(
                        center.x + cos(ang).toFloat() * r,
                        center.y + sin(ang).toFloat() * r,
                    )
                    drawLine(Color.White, inner, outer, strokeWidth = 3f)
                }
            }
            // Selection outline around the car.
            drawRect(
                color = Color.White,
                topLeft = Offset(carX - 8f, carY - 8f),
                size = Size(carW + 16f, carH + 16f),
                style = Stroke(width = 2f),
            )
        }
        Box(
            modifier = Modifier
                .align(Alignment.BottomStart)
                .padding(8.dp)
                .background(Color(0xFF2A2A2A).copy(alpha = 0.9f), RoundedCornerShape(4.dp))
                .padding(horizontal = 8.dp, vertical = 4.dp),
        ) {
            Text("🎥 Camera Preview", color = Color.White, fontSize = 11.sp)
        }
    }
}
