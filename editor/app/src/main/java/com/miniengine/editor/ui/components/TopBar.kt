package com.miniengine.editor.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.IconButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/**
 * Top bar: back, blue Publish button, save/undo icons on the left;
 * camera / play / settings in the center; Scene Settings tab on the right.
 */
@Composable
fun TopBar(
    isPlaying: Boolean,
    onPublish: () -> Unit,
    onTogglePlay: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .background(Color(0xFF252525))
            .padding(horizontal = 8.dp, vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            IconButton(onClick = {}) { Text("←", color = Color.White) }
            Button(
                onClick = onPublish,
                colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF2D9CDB)),
                shape = RoundedCornerShape(6.dp),
                modifier = Modifier.height(32.dp),
            ) {
                Text("🚀 Publish", fontSize = 13.sp)
            }
            IconButton(onClick = {}) { Text("💾", fontSize = 14.sp) }
            IconButton(onClick = {}) { Text("↩", color = Color.White) }
            IconButton(onClick = {}) { Text("↪", color = Color.White) }
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            IconButton(onClick = {}) { Text("📷", fontSize = 14.sp) }
            IconButton(onClick = onTogglePlay) {
                Text(if (isPlaying) "⏸" else "▶", color = Color.White, fontSize = 16.sp)
            }
            IconButton(onClick = {}) { Text("⚙", color = Color.White) }
        }
        Box(
            modifier = Modifier
                .background(Color(0xFF3A3A3A), RoundedCornerShape(6.dp))
                .padding(horizontal = 10.dp, vertical = 6.dp),
        ) {
            Text("⚙ Scene Settings", color = Color(0xFFBBBBBB), fontSize = 12.sp)
        }
    }
}
