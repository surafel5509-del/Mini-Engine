package com.miniengine.editor.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.weight
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.miniengine.editor.model.BuiltInAsset

private val assetCategories = listOf(
    "Assets", "Built-in Assets", "My Assets", "Logic", "Built-in Logic", "My Logic",
)

/** Responsive asset browser. Phone layout uses scrollable category chips and a grid. */
@Composable
fun AssetsPanel(
    assets: List<BuiltInAsset>,
    onAssetClick: (BuiltInAsset) -> Unit,
    modifier: Modifier = Modifier,
    compact: Boolean = false,
) {
    var selectedCategory by rememberSaveable { mutableStateOf("Built-in Assets") }
    val visibleAssets = if (selectedCategory == "Built-in Assets") assets else emptyList()

    if (compact) {
        Column(
            modifier = modifier
                .fillMaxSize()
                .background(Color(0xFF1E1E1E))
                .padding(horizontal = 10.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("💎 Add Assets", color = Color(0xFF2D9CDB), fontSize = 13.sp)
                Text(
                    "  /  $selectedCategory",
                    color = Color(0xFFAAAAAA),
                    fontSize = 12.sp,
                    maxLines = 1,
                )
            }
            LazyRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                items(assetCategories) { category ->
                    CategoryChip(
                        label = category,
                        selected = category == selectedCategory,
                        onClick = { selectedCategory = category },
                    )
                }
            }
            if (visibleAssets.isEmpty()) {
                EmptyCategoryMessage(Modifier.weight(1f).fillMaxWidth())
            } else {
                LazyVerticalGrid(
                    columns = GridCells.Adaptive(minSize = 76.dp),
                    modifier = Modifier.weight(1f).fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(7.dp),
                    verticalArrangement = Arrangement.spacedBy(7.dp),
                ) {
                    items(visibleAssets) { asset ->
                        AssetTile(asset, onClick = { onAssetClick(asset) }, compact = true)
                    }
                }
            }
        }
        return
    }

    Row(
        modifier = modifier
            .fillMaxWidth()
            .background(Color(0xFF1E1E1E))
            .padding(8.dp),
    ) {
        Column(
            modifier = Modifier
                .width(160.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(3.dp),
        ) {
            Text("💎 Add Assets", color = Color(0xFF2D9CDB), fontSize = 12.sp)
            assetCategories.forEach { category ->
                Text(
                    (if (category == "Assets" || category == "Logic") "📁 " else "📂 ") + category,
                    color = if (category == selectedCategory) Color.White else Color(0xFFAAAAAA),
                    fontSize = 12.sp,
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(
                            if (category == selectedCategory) Color(0xFF2D9CDB) else Color.Transparent,
                            RoundedCornerShape(4.dp),
                        )
                        .clickable { selectedCategory = category }
                        .padding(horizontal = 6.dp, vertical = 5.dp),
                )
            }
        }
        Column(
            modifier = Modifier
                .weight(1f)
                .fillMaxSize()
                .padding(start = 12.dp),
        ) {
            Text(
                "Assets / $selectedCategory",
                color = Color(0xFFAAAAAA),
                fontSize = 12.sp,
                modifier = Modifier.padding(bottom = 6.dp),
            )
            if (visibleAssets.isEmpty()) {
                EmptyCategoryMessage(Modifier.weight(1f).fillMaxWidth())
            } else {
                LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    items(visibleAssets) { asset ->
                        AssetTile(asset, onClick = { onAssetClick(asset) }, compact = false)
                    }
                }
            }
        }
    }
}

@Composable
private fun CategoryChip(label: String, selected: Boolean, onClick: () -> Unit) {
    Text(
        label,
        color = if (selected) Color.White else Color(0xFFB8B8B8),
        fontSize = 11.sp,
        maxLines = 1,
        modifier = Modifier
            .clip(RoundedCornerShape(8.dp))
            .background(if (selected) Color(0xFF2D9CDB) else Color(0xFF2A2A2A))
            .clickable(onClick = onClick)
            .padding(horizontal = 10.dp, vertical = 7.dp),
    )
}

@Composable
private fun AssetTile(asset: BuiltInAsset, onClick: () -> Unit, compact: Boolean) {
    Column(
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
        modifier = Modifier
            .then(if (compact) Modifier.fillMaxWidth() else Modifier.height(94.dp))
            .clip(RoundedCornerShape(8.dp))
            .background(Color(0xFF2A2A2A))
            .clickable(onClick = onClick)
            .padding(horizontal = 8.dp, vertical = 7.dp),
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

@Composable
private fun EmptyCategoryMessage(modifier: Modifier = Modifier) {
    Box(modifier = modifier, contentAlignment = Alignment.Center) {
        Text(
            "No assets in this category yet. Choose Built-in Assets to add objects to the scene.",
            color = Color(0xFF999999),
            fontSize = 12.sp,
        )
    }
}
