# Model Module Review

## Purpose
This document records the current architecture review and the long-term implications of adding GLTF/GLB, PBR, paint overlays, and blob sync support to the existing Love2D 3D engine.

## Baseline Findings
1. Legacy model import was STL-only and lived in gameplay codepaths (`main.lua` + `engine.lua`).
2. GPU rendering used position + vertex color only; no native UV/material texture sampling path existed.
3. CPU rendering used triangle rasterization with face color/vertex color; no material parity path.
4. Multiplayer model transfer had robust chunking and hash validation but was semantically model-specific.
5. Restart snapshots already had a model persistence channel that can be reused.

## Implemented Structural Changes
1. Added a module-first architecture rooted at `model_module/init.lua`.
2. Added isolated STL importer and GLTF/GLB importer modules.
3. Added runtime support modules for diagnostics, material normalization, paint sessions, generic blob sync, render bridges, and persistence helpers.
4. Routed runtime model parsing in `main.lua` through `ModelModule.loadFromBytes`.
5. Updated user-facing loading paths and drop behavior to support model formats:
   - Path load: STL/GLB/GLTF.
   - Drag/drop: STL/GLB only (GLTF path-load only due sidecar policy).

## Long-Term Risks
1. Full PBR parity on CPU remains expensive and is not fully implemented in this pass.
2. 2048 paint overlays are memory-heavy and need strict eviction policy for large multiplayer sessions.
3. Transitioning network state from positional `STATE` packets to explicit `STATE2`/`BLOB_*` is still partially staged.
4. Renderer-level per-material submesh shading needs deeper integration in `gpu_renderer.lua` and CPU raster path.

## Recommended Next Milestones
1. Implement GPU material batching by submesh/material index.
2. Implement CPU material sampling parity and `CPU_SAFE` fallback toggles.
3. Migrate network transport from `MODEL_*` to generic `BLOB_*` + `STATE2`.
4. Add paint UI tab and role-specific paint state binding to pause menu.
5. Add automated parser/protocol contract checks in CI-friendly scripts.
