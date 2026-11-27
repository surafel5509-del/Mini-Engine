package com.miniengine.editor.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.miniengine.editor.ui.components.AssetsPanel
import com.miniengine.editor.ui.components.HierarchyPanel
import com.miniengine.editor.ui.components.InspectorPanel
import com.miniengine.editor.ui.components.TopBar
import com.miniengine.editor.ui.components.Viewport
import com.miniengine.editor.viewmodel.EditorViewModel

private enum class DockPanel(val label: String, val compactLabel: String) {
    Hierarchy("Hierarchy", "Scene"),
    Inspector("Inspector", "Inspect"),
    Assets("Assets", "Assets"),
}

/** Responsive editor shell: desktop panels, compact landscape dock, phone tabs. */
@Composable
fun EditorScreen(
    scenePath: String = "",
    rendererBackend: String = "gles3",
    shaderPath: String = "",
    viewModel: EditorViewModel = viewModel(),
) {
    val state by viewModel.state.collectAsState()
    val snackbar = remember { SnackbarHostState() }
    var activePanel by rememberSaveable { mutableStateOf(DockPanel.Assets) }

    LaunchedEffect(state.publishMessage) {
        val msg = state.publishMessage
        if (msg != null) {
            snackbar.showSnackbar(msg)
            viewModel.clearPublishMessage()
        }
    }

    BoxWithConstraints(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0xFF17191C))
            .windowInsetsPadding(WindowInsets.safeDrawing),
    ) {
        val desktopLayout = maxWidth >= 840.dp
        val compactLandscape = !desktopLayout && maxWidth >= 480.dp &&
            maxHeight < 520.dp && maxWidth > maxHeight

        if (desktopLayout) {
            val sideWidth = (maxWidth * 0.20f).coerceIn(190.dp, 270.dp)
            Column(Modifier.fillMaxSize()) {
                TopBar(
                    isPlaying = state.isPlaying,
                    onPublish = viewModel::publish,
                    onSave = { viewModel.saveScene(scenePath) },
                    onTogglePlay = viewModel::togglePlay,
                )
                Row(Modifier.weight(1f).fillMaxWidth()) {
                    HierarchyPanel(
                        items = state.hierarchy,
                        onSelect = viewModel::selectEntity,
                        onToggleVisibility = viewModel::toggleVisibility,
                        modifier = Modifier.width(sideWidth),
                    )
                    Viewport(
                        environment = state.environment,
                        scenePath = scenePath,
                        rendererBackend = rendererBackend,
                        isPlaying = state.isPlaying,
                        shaderPath = shaderPath,
                        modifier = Modifier.weight(1f),
                    )
                    InspectorPanel(
                        environment = state.environment,
                        onEnvironmentChange = viewModel::updateEnvironment,
                        onReset = viewModel::resetEnvironment,
                        modifier = Modifier.width(sideWidth),
                        selectedEntity = state.selectedEntity,
                        onAdjustTransform = viewModel::adjustEntityTransform,
                    )
                }
                AssetsPanel(
                    assets = state.assets,
                    onAssetClick = viewModel::spawnAsset,
                    modifier = Modifier.height(174.dp),
                )
            }
        } else if (compactLandscape) {
            Row(Modifier.fillMaxSize()) {
                Column(Modifier.weight(1f).fillMaxHeight()) {
                    TopBar(
                        isPlaying = state.isPlaying,
                        onPublish = viewModel::publish,
                        onSave = { viewModel.saveScene(scenePath) },
                        onTogglePlay = viewModel::togglePlay,
                        compact = true,
                    )
                    Viewport(
                        environment = state.environment,
                        scenePath = scenePath,
                        rendererBackend = rendererBackend,
                        isPlaying = state.isPlaying,
                        shaderPath = shaderPath,
                        modifier = Modifier.weight(1f),
                    )
                }
                Column(
                    Modifier
                        .width(300.dp)
                        .fillMaxHeight()
                        .background(Color(0xFF1E1E1E)),
                ) {
                    PanelSelector(activePanel, compact = true) { activePanel = it }
                    Box(Modifier.weight(1f).fillMaxWidth()) {
                        DockContent(
                            panel = activePanel,
                            hierarchy = state.hierarchy,
                            selectedEntity = state.selectedEntity,
                            environment = state.environment,
                            assets = state.assets,
                            onSelect = viewModel::selectEntity,
                            onToggleVisibility = viewModel::toggleVisibility,
                            onAdjustTransform = viewModel::adjustEntityTransform,
                            onEnvironmentChange = viewModel::updateEnvironment,
                            onReset = viewModel::resetEnvironment,
                            onAssetClick = viewModel::spawnAsset,
                        )
                    }
                }
            }
        } else {
            Column(Modifier.fillMaxSize()) {
                TopBar(
                    isPlaying = state.isPlaying,
                    onPublish = viewModel::publish,
                    onSave = { viewModel.saveScene(scenePath) },
                    onTogglePlay = viewModel::togglePlay,
                    compact = true,
                )
                Viewport(
                    environment = state.environment,
                    scenePath = scenePath,
                    rendererBackend = rendererBackend,
                    isPlaying = state.isPlaying,
                    shaderPath = shaderPath,
                    modifier = Modifier.weight(1.08f).fillMaxWidth(),
                )
                PanelSelector(activePanel, compact = false) { activePanel = it }
                Box(
                    Modifier
                        .weight(0.92f)
                        .fillMaxWidth()
                        .background(Color(0xFF1E1E1E)),
                ) {
                    DockContent(
                        panel = activePanel,
                        hierarchy = state.hierarchy,
                        selectedEntity = state.selectedEntity,
                        environment = state.environment,
                        assets = state.assets,
                        onSelect = viewModel::selectEntity,
                        onToggleVisibility = viewModel::toggleVisibility,
                        onAdjustTransform = viewModel::adjustEntityTransform,
                        onEnvironmentChange = viewModel::updateEnvironment,
                        onReset = viewModel::resetEnvironment,
                        onAssetClick = viewModel::spawnAsset,
                    )
                }
            }
        }

        SnackbarHost(
            hostState = snackbar,
            modifier = Modifier.align(Alignment.BottomCenter),
        )
    }
}

@Composable
private fun PanelSelector(
    selected: DockPanel,
    compact: Boolean,
    onSelect: (DockPanel) -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(if (compact) 40.dp else 46.dp)
            .background(Color(0xFF252525))
            .padding(horizontal = 6.dp, vertical = 4.dp),
        horizontalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        DockPanel.entries.forEach { panel ->
            Box(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxHeight()
                    .background(
                        if (panel == selected) Color(0xFF2D9CDB) else Color.Transparent,
                        androidx.compose.foundation.shape.RoundedCornerShape(6.dp),
                    )
                    .clickable { onSelect(panel) },
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    if (compact) panel.compactLabel else panel.label,
                    color = if (panel == selected) Color.White else Color(0xFFBBBBBB),
                    fontSize = if (compact) 11.sp else 12.sp,
                )
            }
        }
    }
}

@Composable
private fun DockContent(
    panel: DockPanel,
    hierarchy: List<com.miniengine.editor.model.HierarchyItem>,
    selectedEntity: String?,
    environment: com.miniengine.editor.model.EnvironmentSettings,
    assets: List<com.miniengine.editor.model.BuiltInAsset>,
    onSelect: (String) -> Unit,
    onToggleVisibility: (String) -> Unit,
    onAdjustTransform: (String, Int, Int, Float) -> Unit,
    onEnvironmentChange: (com.miniengine.editor.model.EnvironmentSettings) -> Unit,
    onReset: () -> Unit,
    onAssetClick: (com.miniengine.editor.model.BuiltInAsset) -> Unit,
) {
    when (panel) {
        DockPanel.Hierarchy -> HierarchyPanel(
            items = hierarchy,
            onSelect = onSelect,
            onToggleVisibility = onToggleVisibility,
            modifier = Modifier.fillMaxSize(),
        )
        DockPanel.Inspector -> InspectorPanel(
            environment = environment,
            onEnvironmentChange = onEnvironmentChange,
            onReset = onReset,
            modifier = Modifier.fillMaxSize(),
            selectedEntity = selectedEntity,
            onAdjustTransform = onAdjustTransform,
        )
        DockPanel.Assets -> AssetsPanel(
            assets = assets,
            onAssetClick = onAssetClick,
            modifier = Modifier.fillMaxSize(),
            compact = true,
        )
    }
}
