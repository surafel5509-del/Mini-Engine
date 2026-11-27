package com.miniengine.editor.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.miniengine.editor.model.EnvironmentSettings

/**
 * Right panel: Scene Settings > Environment.
 * Skybox thumbnails, rotation, tint, exposure, ambient, reflection, reset.
 */
@Composable
fun InspectorPanel(
    environment: EnvironmentSettings,
    onEnvironmentChange: (EnvironmentSettings) -> Unit,
    onReset: () -> Unit,
    modifier: Modifier = Modifier,
    selectedEntity: String? = null,
    onAdjustTransform: (String, Int, Int, Float) -> Unit = { _, _, _, _ -> },
) {
    Column(
        modifier = modifier
            .fillMaxSize()
            .background(Color(0xFF1E1E1E))
            .verticalScroll(rememberScrollState())
            .padding(10.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        if (selectedEntity != null) {
            Text("Selected Object", color = Color.White, fontSize = 14.sp)
            Text(selectedEntity, color = Color(0xFF2D9CDB), fontSize = 12.sp)
            TransformAdjustRow("Position", selectedEntity, property = 0, step = 0.25f, onAdjustTransform)
            TransformAdjustRow("Scale", selectedEntity, property = 1, step = 0.1f, onAdjustTransform)
        }

        Text("Scene Environment", color = Color.White, fontSize = 14.sp)
        Text("Skybox Texture", color = Color(0xFFAAAAAA), fontSize = 12.sp)
        val availableSkyboxes = listOf(
            "City dusk" to "environment/city_dusk.ppm",
            "Procedural" to "",
        )
        LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            items(availableSkyboxes) { (label, path) ->
                BoxThumb(
                    label = label,
                    selected = environment.skyboxTexture == path,
                    onClick = { onEnvironmentChange(environment.copy(skyboxTexture = path)) },
                )
            }
        }

        EnvSlider(
            label = "Skybox Rotation",
            value = environment.skyboxRotation,
            range = 0f..360f,
            onChange = { onEnvironmentChange(environment.copy(skyboxRotation = it)) },
        )
        Text("Skybox Tint", color = Color(0xFFAAAAAA), fontSize = 12.sp)
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            listOf(Color(0xFFB0C4DE), Color(0xFFFFD9B0), Color(0xFFB0FFC8), Color(0xFFFFB0B0))
                .forEach { c ->
                    Box(
                        modifier = Modifier
                            .size(28.dp)
                            .clip(RoundedCornerShape(4.dp))
                            .background(c)
                            .border(
                                2.dp,
                                if (environment.skyboxTintR == c.red) Color.White else Color.Transparent,
                                RoundedCornerShape(4.dp),
                            )
                            .clickable {
                                onEnvironmentChange(
                                    environment.copy(
                                        skyboxTintR = c.red,
                                        skyboxTintG = c.green,
                                        skyboxTintB = c.blue,
                                    ),
                                )
                            },
                    )
                }
        }
        EnvSlider(
            label = "Skybox Exposure",
            value = environment.skyboxExposure,
            range = 0f..2f,
            onChange = { onEnvironmentChange(environment.copy(skyboxExposure = it)) },
        )
        EnvSlider(
            label = "Ambient Intensity",
            value = environment.ambientIntensity,
            range = 0f..1f,
            onChange = { onEnvironmentChange(environment.copy(ambientIntensity = it)) },
        )
        EnvSlider(
            label = "Reflection Intensity",
            value = environment.reflectionIntensity,
            range = 0f..1f,
            onChange = { onEnvironmentChange(environment.copy(reflectionIntensity = it)) },
        )

        Button(
            onClick = onReset,
            colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF3A3A3A)),
            shape = RoundedCornerShape(6.dp),
            modifier = Modifier.fillMaxWidth().height(36.dp),
        ) {
            Text("Reset", color = Color.White, fontSize = 13.sp)
        }
    }
}

@Composable
private fun TransformAdjustRow(
    label: String,
    entity: String,
    property: Int,
    step: Float,
    onAdjust: (String, Int, Int, Float) -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Text(label, color = Color(0xFFAAAAAA), fontSize = 12.sp)
        Row(verticalAlignment = Alignment.CenterVertically) {
            listOf("X", "Y", "Z").forEachIndexed { axis, axisLabel ->
                Text(axisLabel, color = Color(0xFF888888), fontSize = 10.sp)
                androidx.compose.material3.IconButton(
                    onClick = { onAdjust(entity, property, axis, -step) },
                    modifier = Modifier.size(28.dp),
                ) { Text("−", color = Color.White, fontSize = 15.sp) }
                androidx.compose.material3.IconButton(
                    onClick = { onAdjust(entity, property, axis, step) },
                    modifier = Modifier.size(28.dp),
                ) { Text("+", color = Color.White, fontSize = 14.sp) }
            }
        }
    }
}

@Composable
private fun BoxThumb(label: String, selected: Boolean, onClick: () -> Unit) {
    Box(
        modifier = Modifier
            .width(78.dp)
            .height(48.dp)
            .clip(RoundedCornerShape(6.dp))
            .background(Color(0xFF4A5A6A))
            .border(
                2.dp,
                if (selected) Color(0xFF2D9CDB) else Color.Transparent,
                RoundedCornerShape(4.dp),
            )
            .clickable(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(label, color = Color.White, fontSize = 10.sp)
    }
}

@Composable
private fun EnvSlider(
    label: String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    onChange: (Float) -> Unit,
) {
    Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Text(label, color = Color(0xFFAAAAAA), fontSize = 12.sp)
            Text(
                "%.2f".format(value),
                color = Color(0xFFCCCCCC),
                fontSize = 12.sp,
            )
        }
        Slider(
            value = value,
            onValueChange = onChange,
            valueRange = range,
            colors = SliderDefaults.colors(
                activeTrackColor = Color(0xFF2D9CDB),
                thumbColor = Color(0xFF2D9CDB),
            ),
            modifier = Modifier.fillMaxWidth().height(24.dp),
        )
    }
}
