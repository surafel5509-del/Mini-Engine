# Roadmap

## Phase 1 — Foundation (complete)
- [x] Math library (Vec3/4, Mat4, Quaternion)
- [x] ECS world (SoA pools, entity handles)
- [x] Scene JSON load/save foundation
- [x] AssetManager + procedural primitives
- [x] Renderer abstraction + NullRenderer
- [x] Physics step, JobSystem, engine facade and JNI bridge
- [x] Kotlin editor foundation (Hierarchy / Viewport / Inspector / Assets / TopBar)
- [x] CarScene sample

## Phase 2 — Import and rendering (in progress)
- [x] glTF 2.0 static mesh import: JSON `.gltf` external/base64 buffers and binary `.glb` JSON/BIN chunks, sparse/normalized mesh accessors, node transforms, triangle lists/strips/fans, normals/UVs, basic PBR base color/metallic/roughness
- [x] Stylized 8-part sample car generated as glTF + binary buffer
- [x] Optional Vulkan renderer implementation path (instance/device, graphics pipeline, Android surface/swapchain code, command buffers, depth, descriptors)
- [x] Wire Android GLES viewport through `GLSurfaceView`; initialize, resize and render on EGL's GL thread
- [x] Wire optional Vulkan `SurfaceView` lifecycle through JNI/Engine to retained `ANativeWindow`; detach and recreate swapchain resources on size changes
- [x] Add Gradle `glslc` discovery/compile/package task for Vulkan SPIR-V assets (optional; requires toolchain)
- [x] GLES static GPU mesh cache keyed by stable asset ID (VAO/VBO/EBO uploaded once per context)
- [x] GLES material flow, external P6 PPM/PNG/JPEG base-color textures, sRGB GPU cache, alpha mask/blend, and compact Cook–Torrance-style metallic/roughness shader with tone mapping
- [x] Vulkan stable AssetId-keyed GPU mesh cache with one-time staging copies to device-local vertex/index buffers
- [x] Priority-selected active camera system with validated view/projection generation
- [x] ECS directional/point/spot light extraction; GLES evaluates up to four dynamic lights
- [x] GLES P6 PPM base-color material texture import, sRGB upload, mipmaps and GPU cache
- [x] GLES directional shadow depth pass with PCF sampling and environment tint/exposure influence
- [x] Procedural dusk sky fallback responding to tint/exposure/rotation
- [x] GLES equirectangular P6 PPM environment map loading, mip generation, sky sampling and approximate mip-based diffuse/specular IBL
- [x] Shared P6 PPM environment image loading and Vulkan sampled environment texture with mip-based approximate lighting
- [ ] HDR/cubemap/KTX2 environment import and convolved/prefiltered IBL
- [ ] Vulkan cache batching/eviction and GPU performance tuning
- [ ] Vulkan and GLES device tests (not available in current sandbox)
- [x] Binary `.glb` containers and sparse/normalized glTF accessors
- [ ] KTX2, embedded-image and multi-UV material texture import; animations/skins
- [ ] Post-processing (tone mapping, bloom)

## Phase 3 — Editor completeness
- [x] Native GLES viewport embedded; optional Vulkan SurfaceView path
- [ ] Viewport selection, drag/drop and move/rotate/scale gizmos
- [ ] Component inspector editing bound to native scene via JNI
- [ ] Undo/redo, play mode, build settings

## Phase 4 — Gameplay systems
- [x] Bounded fixed-step sphere physics with damping, restitution, friction and rotational integration
- [x] Native keyed script registry with entity lifecycle, exception isolation and reload hooks
- [ ] Character controller, animation, particles and broader collider/constraint support
- [ ] Audio engine backend (OpenSL ES / Oboe), touch/gamepad input
- [ ] Kotlin/JVM game scripting runtime and Android hot reload

## Phase 5 — Publishing
- [ ] One-tap APK/AAB export via Publish workflow
- [ ] Asset store / optional cloud projects and analytics

## Verification note
The desktop smoke test is run with `bash tools/build_engine.sh`. This environment has no Android SDK/JDK or Vulkan runtime, so Kotlin/Android compilation, native-window presentation, SPIR-V packaging on an Android build, and device rendering are not yet verified. The Vulkan C++ path was separately compiled against Khronos headers on desktop; that does not validate the Android driver path. Phase labels describe implementation progress, not a claim that the engine is production-ready.
