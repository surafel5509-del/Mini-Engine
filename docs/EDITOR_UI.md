# Editor UI

The editor uses a dark, responsive layout with a shared scene viewport and three working docks:

- **Wide screens (840 dp and up):** toolbar; Hierarchy / Viewport / Inspector columns; bottom Assets browser.
- **Compact landscape:** viewport beside a tabbed Hierarchy / Inspector / Assets dock.
- **Phones and narrow windows:** compact toolbar and viewport above a tabbed dock. Android safe-drawing insets keep controls clear of the status and navigation bars.

The Assets dock uses a scrollable category selector on phones and shows only content the engine can currently create. Built-in primitives create mesh entities; Directional Light creates a real ECS light; Empty Entity creates a transform/name entity. Visibility toggles affect mesh, model and light rendering. Other asset and logic categories remain visibly empty until import and runtime systems are implemented.

The Inspector exposes the bundled City Dusk environment and a procedural sky, along with rotation, tint, exposure, ambient, reflection, and reset controls. Skybox selection only offers sources that are actually available. GLES startup/scene-load failures are surfaced in the viewport rather than leaving an unexplained black panel.
