# Model Module Spec

## Public API
`model_module/init.lua` exports:

1. `loadFromPath(path, opts) -> assetId|nil, err`
2. `loadFromBytes(raw, sourceName, opts) -> assetId|nil, err`
3. `getAsset(assetId) -> AssetRecord|nil`
4. `getAssetByHash(modelHash) -> AssetRecord|nil`
5. `applyToObject(obj, assetId, role) -> ok, err`
6. `setOrientation(assetId, role, yawDeg, pitchDeg, rollDeg) -> ok, err`
7. `beginPaintSession(assetId, role, ownerId) -> sessionId|nil, err`
8. `paintStroke(sessionId, strokeCmd) -> ok, err`
9. `paintFill(sessionId, fillCmd) -> ok, err`
10. `paintUndo(sessionId) -> ok, err`
11. `paintRedo(sessionId) -> ok, err`
12. `paintCommit(sessionId) -> paintHash|nil, err`
13. `getBlobMeta(kind, hash) -> BlobMeta|nil`
14. `getBlobChunk(kind, hash, idx) -> chunk|nil, err`
15. `acceptBlobMeta(metaPacket) -> ok, err`
16. `acceptBlobChunk(chunkPacket) -> kind, hash|nil, err`
17. `buildModelSnapshot(assetId, role, scale) -> table|nil`
18. `applyOrientationSnapshot(assetId, role, snapshot) -> ok`
19. `getGpuBridge() -> render bridge`
20. `getCpuBridge() -> render bridge`

## Core Types

### AssetRecord
1. `id`
2. `sourceLabel`
3. `sourceFormat` (`stl`, `glb`, `gltf`)
4. `modelHash`
5. `rawBytes`
6. `encodedRaw`
7. `model` (legacy render-compatible model table)
8. `geometry`
9. `materials`
10. `images`
11. `bounds`
12. `orientationByRole`
13. `paintByRole`

### GeometryPayload
1. `vertices`
2. `faces`
3. `vertexNormals` (optional)
4. `vertexUVs` (optional)
5. `vertexTangents` (optional)
6. `vertexColors` (optional)
7. `faceMaterials` (optional)
8. `submeshes` (optional)

### MaterialPayload
1. `name`
2. `baseColorFactor`
3. `metallicFactor`
4. `roughnessFactor`
5. `baseColorTexture` (resolved texture ref: `imageIndex`, `texCoord`, optional `scale/strength`)
6. `metallicRoughnessTexture` (resolved texture ref)
7. `normalTexture` (resolved texture ref)
8. `occlusionTexture` (resolved texture ref)
9. `emissiveTexture` (resolved texture ref)
10. `emissiveFactor`
11. `alphaMode`
12. `alphaCutoff`
13. `doubleSided`

### PaintOverlay
1. `hash`
2. `ownerId`
3. `role`
4. `modelHash`
5. `width`
6. `height`
7. `imageData`
8. `image`
9. `encodedPng`

### BlobMeta
1. `kind` (`model`/`paint`)
2. `hash`
3. `rawBytes`
4. `encodedBytes`
5. `chunkSize`
6. `chunkCount`
7. `ownerId` (optional)
8. `role` (optional)
9. `modelHash` (optional)

## Importer Policy
1. 100MB source cap enforced by diagnostics.
2. `.gltf` sidecars use relative sandbox policy.
3. Absolute sidecar paths and non-data URI schemes are rejected.
4. Unsupported extension set is explicitly rejected:
   - `KHR_draco_mesh_compression`
   - `EXT_meshopt_compression`
   - `KHR_texture_basisu`

## Budget Defaults
1. `sourceBytes`: 100MB
2. `maxTriangles`: 120,000
3. `maxVertices`: 200,000
4. `maxMaterials`: 24
5. `maxTextures`: 12
6. `maxTextureDimension`: 4096
7. `maxDecodedTextureBytes`: 256MB
8. `maxPaintEncodedBytes`: 32MB
