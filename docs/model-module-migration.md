# Model Module Migration

## Goal
Move model-related responsibilities out of `main.lua` and into `model_module` with minimal runtime disruption.

## Completed in This Pass
1. Added `require "model_module"` to `main.lua`.
2. Reworked `cacheModelEntry` to parse via `ModelModule.loadFromBytes` instead of direct STL parsing.
3. Added generic path loader (`loadPlayerModelFromPath`) and retained `loadPlayerModelFromStl` as compatibility alias.
4. Updated model status text and prompts from STL-only wording to model-format wording.
5. Updated drag/drop logic to:
   - accept STL/GLB
   - reject GLTF drops with explicit path-load requirement.
6. Migrated outbound state sync to `STATE2` key/value packets.
7. Migrated model transfer to `BLOB_*` packets and `ModelModule` blob APIs while retaining legacy `MODEL_*` packet parsing as compatibility fallback.

## Remaining Migration Work
1. Renderer integration:
   - Replace ad-hoc mesh build assumptions with `submeshes/materials/images` aware draw path.
2. Networking integration:
   - Extend `BLOB_*` flow for paint overlays and promote protocol version gating.
3. Persistence integration:
   - Add role paint snapshot and asset orientation round-trip into restart payloads.
4. UI integration:
   - add Paint tab and controls bound to `ModelModule.paint*`.

## Compatibility Notes
1. Existing model cache and transfer flow remains active.
2. Existing peer model synchronization is unchanged and should remain functional.
3. Loaded model hashes continue to use existing hash logic from `main.lua`.
