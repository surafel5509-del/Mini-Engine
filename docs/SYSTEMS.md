# Runtime systems

This document records the runtime-facing contracts added to the C++ engine. These are working engine subsystems, with explicit current limits; they are not claims of a complete commercial physics, graphics, or scripting runtime.

## Camera

`CameraSystem::build(world, width, height)` selects the main camera with the greatest `Camera::priority` (lowest entity id breaks ties), computes view/projection matrices, and validates field of view and clip planes. If there is no valid main camera, it returns the engine's default view. Camera transforms use the engine's local quaternion and position; the active position is sent to both rendering backends for view-dependent shading.

## Lighting

`LightSystem::collect(world)` resolves enabled ECS lights to world position/direction and sanitized renderer values. It orders directionals first, then by intensity. GLES evaluates up to four directional, point, or spot lights in its PBR shader; point/spot lights use a finite range falloff and spot cones. The existing GLES shadow map is directional and uses the first directional light. The experimental Vulkan path currently consumes the first directional light only; point/spot support and shadows are not claimed there.

## Physics

`PhysicsWorld::step` accumulates elapsed time and advances bounded fixed-size simulation steps (default 60 Hz, at most eight substeps per call). The solver supports gravity, linear/angular damping, angular quaternion integration, sphere colliders with transform scale, a horizontal ground plane, a sweep-and-prune broad phase on X, inverse-mass separation, restitution, and friction. Kinematic and invalid/non-positive-mass bodies are immovable. It does not yet implement box/capsule colliders, continuous collision detection, joints, rigid-body angular impulses, triggers, or a general constraint solver.

## Texture/material path

glTF `baseColorTexture` references resolve through the glTF texture/image tables for external image URIs. The shared decoder accepts binary P6 PPM, PNG, and JPEG; source alpha is retained where available. GLES uploads maps as sRGB textures, generates mipmaps, and caches GPU texture objects by path. Vulkan uploads external maps through staging buffers to sRGB images and binds a per-draw material sampler descriptor; its current material images use a single mip level. UV set 0 is sampled. glTF OPAQUE/MASK/BLEND alpha modes are handled on GLES (transparent draws are depth-sorted); Vulkan supports MASK, while blended draws and non-opaque shadowing remain unsupported. KTX2, embedded GLB image bufferViews/data URIs, other UV sets, Vulkan mip generation/eviction, and full Vulkan blend support remain future work. Unsupported maps use the shader's white fallback, preserving material base-color factors.

## Scripts

`ScriptBridge` is a native keyed factory/lifecycle host. Register a C++ factory with `Engine::scripts().registerScript(key, factory)`, assign `ScriptComponent{key, true}` to an entity, and implement `IScript::onCreate`, `onUpdate`, and `onDestroy`. `ScriptContext` exposes the ECS world, entity, frame delta, and input snapshot. Missing factories can be registered later; `reloadScript` recreates matching component-owned instances. Exceptions disable failing scripts and are logged rather than escaping the engine frame. This is not yet a Kotlin/JVM bytecode runtime; Android Kotlin game-script loading and safe hot reload remain roadmap work.
