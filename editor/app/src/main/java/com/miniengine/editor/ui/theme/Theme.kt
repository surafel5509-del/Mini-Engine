package com.miniengine.editor.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

// Dark editor theme matching the reference screenshot.
private val EditorColors = darkColorScheme(
    primary = Color(0xFF2D9CDB),
    onPrimary = Color.White,
    background = Color(0xFF1E1E1E),
    surface = Color(0xFF2A2A2A),
    surfaceVariant = Color(0xFF333333),
    onBackground = Color(0xFFE0E0E0),
    onSurface = Color(0xFFE0E0E0),
)

@Composable
fun MiniEngineTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = EditorColors, content = content)
}
