# Architecture

## Layers

```
┌─────────────────────────────────────────────┐
│  Editor (Kotlin / Compose)                  │
│  Hierarchy · Viewport · Inspector · Assets  │
├─────────────────────────────────────────────┤
│  JNI Bridge (engine/jni/JniBridge.cpp)      │
│  NativeBridge.kt  <->  mini::Engine         │
├─────────────────────────────────────────────┤
│  Engine Core (C++17, platform agnostic)     │
│  Engine facade                              │
│  ├─ ECS World (SoA component pools)         │
│  ├─ Scene graph + JSON serialization        │
│  ├─ AssetManager (registry + primitives)    │
│  ├─ Renderer (IRenderer abstraction)        │
│  │    ├─ NullRenderer   (headless / tests)  │
│  │    ├─ GLESRenderer  (OpenGL ES 3, Android)│
│  │    └─ VulkanRenderer (optional / experimental)│
│  ├─ Physics (custom, no external deps)      │
│  ├─ Audio / Input                           │
│  ├─ JobSystem (thread pool)                 │
│  └─ ScriptBridge (Kotlin scripting, JNI)    │
└─────────────────────────────────────────────┘
```

## Key decisions

- **ECS with SoA pools**: components stored in flat vectors per type for
  cache-friendly iteration. Entities are 32-bit handles.
- **Renderer abstraction**: the engine never talks to a graphics API
  directly. GLES3 is the current Android implementation target;
  Vulkan is an optional experimental backend; NullRenderer powers tests
  and headless builds. Device validation is still pending.
- **No external dependencies** in the core (no STL-external JSON/physics
  libs) so the NDK build stays hermetic and small.
- **Scene format**: JSON (`samples/CarScene/scene.json`) — human editable,
  diff-friendly, and trivially generated from the Kotlin editor.
- **Threading**: JobSystem thread pool for asset import / physics;
  render thread owned by the Android `GLSurfaceView` / Vulkan swapchain.

## Android specifics

- `minSdk 26`, `compileSdk 34`, NDK 26, CMake 3.22.
- `libminiengine.so` built from `engine/` via `editor/app/src/main/cpp/CMakeLists.txt`.
- Viewport: `GLSurfaceView` + GLES3 renderer; Compose `Canvas` preview
  fallback when the native library is unavailable (e.g. IDE preview).
