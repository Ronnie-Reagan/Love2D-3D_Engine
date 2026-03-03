# Model Module Operations

## Runtime Diagnostics
Use `model_module/diagnostics.lua` budgets to tune import safety:

1. `sourceBytes`
2. `maxTriangles`
3. `maxVertices`
4. `maxMaterials`
5. `maxTextures`
6. `maxTextureDimension`
7. `maxDecodedTextureBytes`
8. `maxPaintEncodedBytes`

## Common Failure Messages
1. `source exceeds max size`:
   - model payload larger than configured `sourceBytes`.
2. `model exceeds triangle budget`:
   - imported mesh is too dense.
3. `texture exceeds max dimension`:
   - at least one decoded image exceeds max dimension.
4. `URI escapes model directory sandbox`:
   - `.gltf` sidecar URI attempted directory escape.
5. `unsupported glTF extension`:
   - compressed extension currently out of scope.

## Drag/Drop Policy
1. Accept: `.stl`, `.glb`
2. Reject: `.gltf` drop (path-load required for sidecars)

## Memory Guidance
1. 2048x2048 RGBA overlay is ~16MB per texture before compression.
2. For peer-heavy sessions, reduce live overlay count or downgrade resolution.

## Recommended Logs to Monitor
1. Model load success line with hash + format.
2. Transfer metadata/chunk retries.
3. Any import diagnostics failures in loader pipeline.
