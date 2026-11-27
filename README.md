# Mini-Engine

A from-scratch game-engine foundation for **Android**, using **Kotlin + Jetpack Compose** for the editor and **C++17** for engine systems.

The editor follows the supplied Buildbox-like layout: Publish top bar, left Hierarchy, center Viewport, right Inspector / Environment, and bottom Assets panel. It is intentionally a clean starter workspace, not a crowded tool suite.

## Repository layout

```
engine/    C++17 core: math, ECS, scenes, assets, renderer, physics,
           audio, input, jobs, scripting bridge, JNI, shaders
editor/    Kotlin + Jetpack Compose Android editor app
samples/   CarScene, including an 8-part stylized glTF car
 docs/     Architecture, roadmap, editor UI specification
tools/     Desktop/Android build scripts and sample asset generator
```

## Build

### Desktop engine and smoke test

```bash
bash tools/build_engine.sh
```

This builds the dependency-free C++ engine path and exercises the sample scene, including glTF import. Expected result: `9` draw calls, `370` triangles, `SMOKE TEST PASSED`.

### Android editor

Requires Android Studio, Android SDK, JDK 17, and NDK 26:

```bash
bash tools/build_android.sh
```

The Android CMake project links the engine into `libminiengine.so`; Kotlin calls it through JNI. GLES3 is the default viewport. To try the optional Vulkan path from the repository root, run `cd editor && ./gradlew -PminiRenderer=vulkan :app:assembleDebug`; `glslc` must be available from the NDK/Vulkan SDK or `PATH`, and the device must support Vulkan. Android build/runtime validation still requires an Android toolchain and device/emulator.

### Regenerate sample assets

```bash
python3 tools/make_car_gltf.py
python3 tools/make_environment.py
```

## Phase 2 status

- **glTF 2.0 static mesh import:** JSON `.gltf` external/base64 buffers and binary `.glb` JSON/BIN chunks; sparse/normalized mesh accessors, node transforms, triangle modes, normals/UVs and metallic-roughness factors. External P6 PPM/PNG/JPEG base-color textures resolve through material/image/texture indices and render on GLES and Vulkan; GLES handles alpha mask/blend, Vulkan handles alpha mask. KTX2, embedded image buffers, other UV sets, animation and skinning are not supported yet.
- **GLES3 viewport:** Android `GLSurfaceView` now owns EGL lifecycle; JNI init/resize/frame calls run against its current GLES context. The bundled CarScene is unpacked into app-private storage and loaded by native C++.
- **Vulkan:** optional Android `SurfaceView` → JNI → `ANativeWindow` attach/detach/resize path is wired. Gradle tries `glslc` from the NDK/Vulkan SDK/PATH and packages generated SPIR-V under app assets. Select with `-PminiRenderer=vulkan`; this experimental route still requires a Vulkan-capable device and has not been device-tested here. It falls back to a stub if Vulkan or compiled shaders are unavailable.
- **GLES3:** static meshes upload once per stable asset key; material P6 PPM/PNG/JPEG base-color maps use an sRGB GPU texture cache. A Cook–Torrance-style shader consumes the active camera and up to four directional/point/spot lights, with tone mapping. Android runtime/device rendering remains untested.
- **GLES3:** renders a directional depth shadow map (1024² with 3×3 PCF) and loads the sample's equirectangular PPM dusk-city environment for the sky background and mip-based, approximate diffuse/specular IBL. Tint, exposure, rotation, and reflection intensity feed the renderer. The sample map is LDR, not an HDR cubemap.
- **Vulkan:** loads the shared P6 PPM environment image into an sRGB sampled texture, generates mip levels, and samples it for approximate environment diffuse/specular lighting. C++ compilation is checked, but shader compilation and device rendering remain unverified.
- **Camera and lighting systems:** camera priority selects stable view/projection matrices; ECS light components resolve to renderer data, with GLES support for four dynamic lights. Vulkan currently consumes one directional light.
- **Physics:** a bounded fixed-step accumulator, semi-implicit motion, angular integration, sphere sweep-and-prune, positional correction, restitution and friction are implemented. This is a compact sphere-collider solver, not a full rigid-body/constraint engine.
- **Scripting:** entity script components use a native keyed factory registry with create/update/destroy callbacks, input access, exception isolation and reload hooks. A Kotlin/JVM game-script runtime is not yet integrated.
- **Vulkan:** static meshes use one-time staging transfers into device-local buffers and an AssetId-keyed cache; external base-color textures are uploaded as sRGB images and sampled via a third descriptor binding. It supports one directional light; material mipmaps and blended transparency remain outstanding. Shader compilation/device tests remain outstanding.
- **Remaining renderer/import work:** HDR/cubemap/KTX2 environments, Vulkan cache batching and device-side shadow/performance tuning, KTX2 material textures, animation/skin and post-processing remain.

See `docs/SYSTEMS.md` for runtime contracts and current limits, `docs/ROADMAP.md` for the remaining sequence, and `docs/ARCHITECTURE.md` for subsystem boundaries.
