package com.miniengine.editor.viewmodel

import android.util.AtomicFile
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.miniengine.editor.bridge.NativeBridge
import com.miniengine.editor.model.AssetCatalog
import com.miniengine.editor.model.BuiltInAsset
import com.miniengine.editor.model.EnvironmentSettings
import com.miniengine.editor.model.HierarchyItem
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream

/** Immutable snapshot of everything the editor UI renders. */
data class EditorUiState(
    val hierarchy: List<HierarchyItem> = defaultHierarchy(),
    val selectedEntity: String? = null,
    val environment: EnvironmentSettings = EnvironmentSettings(),
    val assets: List<BuiltInAsset> = AssetCatalog.builtIn,
    val nativeAvailable: Boolean = NativeBridge.isAvailable,
    val isPlaying: Boolean = false,
    val publishMessage: String? = null,
) {
    companion object {
        fun defaultHierarchy(): List<HierarchyItem> = listOf(
            HierarchyItem("Camera"),
            HierarchyItem("Directional Light"),
            HierarchyItem("UI Canvas"),
            HierarchyItem("Animation Director"),
            HierarchyItem("U Car"),
            HierarchyItem("Circle Asphalt Plane"),
        )
    }
}

/**
 * Single source of truth for the editor. UI events come in as method
 * calls; state flows out. Native engine calls are fire-and-forget on
 * the IO dispatcher so the UI thread never blocks on JNI.
 */
class EditorViewModel : ViewModel() {

    private val _state = MutableStateFlow(EditorUiState())
    val state: StateFlow<EditorUiState> = _state.asStateFlow()

    init {
        // Ask the native engine for the real hierarchy when available.
        viewModelScope.launch {
            val json = NativeBridge.hierarchyJson()
            val names = parseJsonStringArray(json)
            if (names.isNotEmpty()) {
                _state.update { it.copy(hierarchy = names.map { n -> HierarchyItem(n) }) }
            }
        }
    }

    fun selectEntity(name: String) {
        _state.update { s ->
            s.copy(
                selectedEntity = name,
                hierarchy = s.hierarchy.map { it.copy(isSelected = it.name == name) },
            )
        }
    }

    fun toggleVisibility(name: String) {
        var newVisibility = true
        _state.update { s ->
            s.copy(hierarchy = s.hierarchy.map {
                if (it.name == name) {
                    newVisibility = !it.visible
                    it.copy(visible = newVisibility)
                } else it
            })
        }
        NativeBridge.setEntityVisible(name, newVisibility)
    }

    fun adjustEntityTransform(name: String, property: Int, axis: Int, delta: Float) {
        NativeBridge.adjustEntityTransform(name, property, axis, delta)
    }

    fun saveScene(path: String) {
        viewModelScope.launch {
            val message = withContext(Dispatchers.IO) {
                runCatching {
                    require(path.isNotBlank()) { "Scene path is empty" }
                    val json = NativeBridge.sceneToJson()
                    require(json != "{}" && json.contains("\"entities\"")) {
                        "The native scene is not ready yet"
                    }
                    val target = AtomicFile(File(path))
                    var stream: FileOutputStream? = null
                    try {
                        val output = target.startWrite()
                        stream = output
                        output.write(json.toByteArray(Charsets.UTF_8))
                        target.finishWrite(output)
                    } catch (t: Throwable) {
                        stream?.let { output -> runCatching { target.failWrite(output) } }
                        throw t
                    }
                    "Scene saved"
                }.getOrElse { "Save failed: ${it.message ?: it.javaClass.simpleName}" }
            }
            _state.update { it.copy(publishMessage = message) }
        }
    }

    fun updateEnvironment(env: EnvironmentSettings) {
        _state.update { it.copy(environment = env) }
        NativeBridge.setEnvironment(env)
    }

    fun resetEnvironment() = updateEnvironment(EnvironmentSettings())

    /** Spawn a built-in asset into the scene (bottom panel tap). */
    fun spawnAsset(asset: BuiltInAsset) {
        viewModelScope.launch {
            // Non-mesh scene objects (empty entities, lights, UI placeholders)
            // are still real entities; the native engine accepts an empty primitive.
            val id = NativeBridge.createEntity(asset.name, asset.primitive.orEmpty())
            if (id != 0) {
                _state.update { s ->
                    s.copy(hierarchy = s.hierarchy + HierarchyItem(asset.name, isSelected = true))
                }
            }
        }
    }

    fun togglePlay() {
        _state.update { it.copy(isPlaying = !it.isPlaying) }
    }

    /** Publish button (top bar). Phase 5 wires this to APK export. */
    fun publish() {
        _state.update { it.copy(publishMessage = "Publishing… (Phase 5: APK export)") }
    }

    fun clearPublishMessage() {
        _state.update { it.copy(publishMessage = null) }
    }

    /** Minimal parser for the native hierarchy JSON: ["A","B",...]. */
    private fun parseJsonStringArray(json: String): List<String> {
        val out = mutableListOf<String>()
        var i = 0
        while (i < json.length) {
            if (json[i] == '"') {
                val end = json.indexOf('"', i + 1)
                if (end < 0) break
                out += json.substring(i + 1, end)
                i = end + 1
            } else {
                i++
            }
        }
        return out
    }
}
