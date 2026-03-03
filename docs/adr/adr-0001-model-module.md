# ADR 0001: Introduce `model_module` as a first-class architecture boundary

## Status
Accepted

## Context
Model loading, caching, transfer, and runtime rendering inputs were tightly coupled to gameplay code and STL-specific logic. Planned features (GLTF/GLB, materials, paint overlays, protocol migration) would increase risk if implemented directly inside `main.lua`.

## Decision
Create a dedicated `model_module` package with explicit submodules:

1. importers (`stl_importer`, `gltf_importer`)
2. runtime helpers (`material_runtime`, `paint_runtime`)
3. transport (`net_blob_sync`)
4. persistence (`persistence`)
5. diagnostics (`diagnostics`)
6. render adapters (`render_bridge_gpu`, `render_bridge_cpu`)

Main/gameplay code calls module APIs instead of owning parser logic directly.

## Consequences

### Positive
1. Cleaner separation of concerns and lower change risk.
2. Easier staged rollout for format and rendering upgrades.
3. Shared blob sync path can support both model and paint payloads.

### Negative
1. More module surface area to maintain.
2. Temporary dual-path behavior while render/network migration is incomplete.
3. Requires follow-up integration work for full PBR and `STATE2` protocol.
