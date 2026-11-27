package com.miniengine.editor.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.miniengine.editor.model.HierarchyItem

/**
 * Left panel: search field + scene entity list with visibility toggles.
 */
@Composable
fun HierarchyPanel(
    items: List<HierarchyItem>,
    onSelect: (String) -> Unit,
    onToggleVisibility: (String) -> Unit,
    modifier: Modifier = Modifier,
) {
    var query by remember { mutableStateOf("") }
    Column(
        modifier = modifier
            .fillMaxSize()
            .background(Color(0xFF1E1E1E))
            .padding(8.dp),
    ) {
        TextField(
            value = query,
            onValueChange = { query = it },
            placeholder = { Text("Search...", color = Color(0xFF888888), fontSize = 12.sp) },
            singleLine = true,
            shape = RoundedCornerShape(6.dp),
            colors = TextFieldDefaults.colors(
                focusedContainerColor = Color(0xFF2A2A2A),
                unfocusedContainerColor = Color(0xFF2A2A2A),
                focusedIndicatorColor = Color.Transparent,
                unfocusedIndicatorColor = Color.Transparent,
            ),
            modifier = Modifier.fillMaxWidth(),
        )
        Text(
            "Scene Settings",
            color = Color.White,
            fontSize = 13.sp,
            modifier = Modifier
                .fillMaxWidth()
                .background(Color(0xFF2D9CDB), RoundedCornerShape(4.dp))
                .padding(horizontal = 8.dp, vertical = 4.dp),
        )
        val filtered = if (query.isBlank()) items else items.filter { it.name.contains(query, true) }
        LazyColumn(verticalArrangement = Arrangement.spacedBy(2.dp)) {
            items(filtered) { item ->
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(
                            if (item.isSelected) Color(0xFF2D9CDB) else Color.Transparent,
                            RoundedCornerShape(4.dp),
                        )
                        .clickable { onSelect(item.name) }
                        .padding(horizontal = 8.dp, vertical = 6.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.SpaceBetween,
                ) {
                    Text(
                        item.name,
                        color = if (item.isSelected) Color.White else Color(0xFFCCCCCC),
                        fontSize = 13.sp,
                    )
                    Text(
                        if (item.visible) "👁" else "🚫",
                        fontSize = 12.sp,
                        modifier = Modifier.clickable { onToggleVisibility(item.name) },
                    )
                }
            }
        }
    }
}
