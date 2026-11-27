# Shaders

GLSL sources for the Vulkan renderer. Compile to SPIR-V with the
Vulkan SDK's `glslc`:

```bash
glslc main.vert -o vert.spv
glslc main.frag -o frag.spv
```

The Vulkan renderer loads `vert.spv` / `frag.spv` from the directory
given to `VulkanRenderer::setShaderDir()` (default: `shaders/`).
The GLES3 renderer uses equivalent GLSL ES sources embedded in
`engine/src/render/GLESRenderer.cpp`.
