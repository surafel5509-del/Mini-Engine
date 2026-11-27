package com.miniengine.editor.model

/** Scene environment settings — mirrors Inspector > Environment. */
data class EnvironmentSettings(
    val skyboxTexture: String = "environment/city_dusk.ppm",
    val skyboxRotation: Float = 0f,
    val skyboxTintR: Float = 1f,
    val skyboxTintG: Float = 1f,
    val skyboxTintB: Float = 1f,
    val skyboxExposure: Float = 0.6f,
    val ambientIntensity: Float = 0.89f,
    val reflectionIntensity: Float = 0.73f,
)

/** One row in the Hierarchy panel. */
data class HierarchyItem(
    val name: String,
    val visible: Boolean = true,
    val isSelected: Boolean = false,
)

/** A built-in asset shown in the Assets panel grid. */
data class BuiltInAsset(
    val name: String,
    val primitive: String? = null, // native primitive to spawn, if any
    val icon: String,              // emoji glyph used as the tile icon
)

/** The built-in asset catalog (bottom panel), matching the reference UI. */
object AssetCatalog {
    val builtIn: List<BuiltInAsset> = listOf(
        BuiltInAsset("Empty Entity", null, "⬡"),
        BuiltInAsset("Directional Light", "directional_light", "🔦"),
        BuiltInAsset("Cube", "cube", "⬛"),
        BuiltInAsset("Sphere", "sphere", "⚪"),
        BuiltInAsset("Plane", "plane", "▭"),
        BuiltInAsset("Cylinder", "cylinder", "🛢"),
        BuiltInAsset("Capsule", "capsule", "💊"),
        BuiltInAsset("Quad", "quad", "◻"),
    )

    /** Only environment sources currently bundled with the editor. */
    val skyboxes: List<String> = listOf(
        "environment/city_dusk.ppm", "",
    )
}
