# Editor UI spec (mirrors reference screenshot)

Dark theme editor, four regions + top bar:

- **Top bar**: back, blue **Publish** button, save, undo/redo icons,
  center: camera / play / settings, right: **Scene Settings** tab.
- **Left — Hierarchy**: search field, "Scene Settings" header, entity list
  (Camera, Directional Light, UI Canvas, Animation Director, U Car,
  Circle Asphalt Plane) with visibility toggles.
- **Center — Viewport**: rendered scene, "Camera Preview" chip bottom-left.
- **Right — Inspector**: **Environment** section with Skybox Texture
  thumbnails, Skybox Rotation slider, Skybox Tint, Skybox Exposure (0.6),
  Ambient Intensity (0.89), Reflection Intensity (0.73), Reset button.
- **Bottom — Assets**: left tree (Assets / Built-in Assets / My Assets /
  Logic / Built-in Logic / My Logic), grid of built-in assets:
  Empty, Directional Light, Cube, Sphere, Plane, Cylinder, Capsule, Quad,
  UI Canvas, Button, Text, Input Field, Image, Toggle, Web View.

All panels are live: sliders update `EnvironmentSettings` in the
ViewModel and push to the native engine through `NativeBridge`.
