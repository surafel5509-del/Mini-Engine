package com.miniengine.editor.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.miniengine.editor.ui.components.AssetsPanel
import com.miniengine.editor.ui.components.HierarchyPanel
import com.miniengine.editor.ui.components.InspectorPanel
import com.miniengine.editor.ui.components.TopBar
import com.miniengine.editor.ui.components.Viewport
import com.miniengine.editor.viewmodel.EditorViewModel

/**
 * Root editor layout, mirroring the reference screenshot:
 * TopBar / (Hierarchy | Viewport | Inspector) / Assets panel.
 */
@Composable
fun EditorScreen(
    scenePath: String = "",
    rendererBackend: String = "gles3",
    shaderPath: String = "",
    viewModel: EditorViewModel = viewModel(),
) {
    val state by viewModel.state.collectAsState()
    val snackbar = remember { SnackbarHostState() }

    LaunchedEffect(state.publishMessage) {
        val msg = state.publishMessage
        if (msg != null) {
            snackbar.showSnackbar(msg)
            viewModel.clearPublishMessage()
        }
    }

    Column(modifier = Modifier.fillMaxSize()) {
        TopBar(
            isPlaying = state.isPlaying,
            onPublish = viewModel::publish,
            onTogglePlay = viewModel::togglePlay,
        )
        Row(modifier = Modifier.weight(1f)) {
            HierarchyPanel(
                items = state.hierarchy,
                onSelect = viewModel::selectEntity,
                onToggleVisibility = viewModel::toggleVisibility,
                modifier = Modifier.width(200.dp),
            )
            Viewport(
                environment = state.environment,
                scenePath = scenePath,
                rendererBackend = rendererBackend,
                shaderPath = shaderPath,
                modifier = Modifier.weight(1f),
            )
            InspectorPanel(
                environment = state.environment,
                onEnvironmentChange = viewModel::updateEnvironment,
                onReset = viewModel::resetEnvironment,
                modifier = Modifier.width(240.dp),
            )
        }
        AssetsPanel(
            assets = state.assets,
            onAssetClick = viewModel::spawnAsset,
            modifier = Modifier.height(140.dp),
        )
        SnackbarHost(hostState = snackbar)
    }
}
