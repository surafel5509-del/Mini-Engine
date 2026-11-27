package com.miniengine.editor.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
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
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.miniengine.editor.model.BuiltInAsset

/**
 * Bottom panel: asset tree (left) + built-in assets grid (right).
 * Tapping a primitive spawns it into the scene via the ViewModel.
 */
@Composable
fun AssetsPanel(
    assets: List<BuiltInAsset>,
    onAssetClick: (BuiltInAsset) -> Unit,
    modifier: Modifier = Modifier,
) {
    var selectedCategory by remember { mutableStateOf("Built-in Assets") }
    Row(
        modifier = modifier
            .fillMaxWidth()
            .background(Color(0xFF1E1E1E))
            .padding(8.dp),
    ) {
        // Left tree.
        Column(
            modifier = Modifier.width(150.dp),
            verticalArrangement = Arrangement.spacedBy(2.dp),
        ) {
            Text("💎 Add Assets", color = Color(0xFF2D9CDB), fontSize = 12.sp)
            listOf("Assets", "Built-in Assets", "My Assets", "Logic", "Built-in Logic", "My Logic")
                .forEach { cat ->
                    Text(
                        (if (cat == "Assets" || cat == "Logic") "📁 " else "📂 ") + cat,
                        color = if (cat == selectedCategory) Color.White else Color(0xFFAAAAAA),
                        fontSize = 12.sp,
                        modifier = Modifier
                            .fillMaxWidth()
                            .background(
                                if (cat == selectedCategory) Color(0xFF2D9CDB) else Color.Transparent,
                                RoundedCornerShape(4.dp),
                            )
                            .clickable { selectedCategory = cat }
                            .padding(horizontal = 6.dp, vertical = 3.dp),
                    )
                }
        }
        // Right grid.
        Column(modifier = Modifier.fillMaxSize()) {
            Text(
                "Assets / $selectedCategory",
                color = Color(0xFFAAAAAA),
                fontSize = 12.sp,
                modifier = Modifier.padding(bottom = 6.dp),
            )
            LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                items(assets) { asset ->
                    Column(
                        horizontalAlignment = Alignment.CenterHorizontally,
                        modifier = Modifier
                            .clip(RoundedCornerShape(8.dp))
                            .background(Color(0xFF2A2A2A))
                            .clickable { onAssetClick(asset) }
                            .padding(horizontal = 10.dp, vertical = 8.dp),
                    ) {
                        Text(asset.icon, fontSize = 22.sp)
                        Text(
                            asset.name,
                            color = Color(0xFFCCCCCC),
                            fontSize = 10.sp,
                            maxLines = 1,
                        )
                    }
                }
            }
        }
    }
}
