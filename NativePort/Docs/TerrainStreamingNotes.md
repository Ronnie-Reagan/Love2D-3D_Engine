# Terrain Streaming Notes

## Current bottleneck

The freeze is caused by mesh generation, not by texture lookup.

If the game rebuilds a very large `LOD0` terrain mesh on the main thread, frame time collapses regardless of how cheap the terrain textures are. Pre-generating thousands of texture variants can improve appearance variety, but it does not remove the cost of:

- sampling the terrain field
- polygonizing voxel/SDF data
- uploading new mesh buffers
- replacing large render objects while the simulation is running

## What Minecraft gets right

Minecraft survives by treating terrain as a stream of small independent chunks.

- World generation is keyed by stable chunk coordinates.
- Generation, lighting, meshing, and rendering are separate stages.
- The renderer draws static chunk meshes, not a giant continuously rebuilt terrain object.
- Local edits only dirty the touched chunk and a small neighbor ring.
- Work is budgeted over time instead of rebuilding the visible world in one frame.
- Chunk priority is biased toward movement direction so fast travel does not outrun generation immediately.

That same model applies here even if the underlying terrain source is heightfield, voxel, or SDF-based.

## Recommended native-port structure

### Near field

Use small `LOD0` tiles/chunks as the editable terrain representation.

- Keep a voxel/SDF source per chunk.
- Convert each dirty chunk into a static render mesh.
- Rebuild only the changed chunk plus the neighbor ring needed for seam safety.
- Keep chunk size small enough that a crater, trench, or tunnel does not force a rebuild of a large square of land.

Practical starting point:

- `64 m` to `128 m` chunk width for editable `LOD0`
- a separate dirty queue for deformation rebuilds
- worker-thread meshing with a hard per-frame finalize budget on the main thread

### Mid and far field

Do not carry full editable voxel detail to the horizon.

- `LOD1` and `LOD2` should be cheaper surface tiles or clipmap-style terrain.
- Far terrain should prefer cached height/surface meshes over volumetric remeshing.
- Horizon rendering should be treated as its own system: large tiles, clipmaps, or baked far-field proxies.

For aircraft speeds near `1000 kph`, prefetch must be velocity-aware. Streaming around the camera position alone is too late.

## Deformation model

The robust path is:

`voxel/SDF chunk -> meshed static chunk -> render`

When deformation happens:

1. Write the edit into the chunk-local terrain data.
2. Mark the touched chunk dirty.
3. Mark immediate neighbors dirty if the edit can affect seam vertices.
4. Re-mesh only those chunks.
5. Swap finished meshes into the renderer without stalling simulation.

This is the main reason small chunks matter. Local edits stay local.

## About compile-time terrain texture variants

This is useful only in a limited role.

Good use:

- prebuilt material atlases
- biome texture sets
- macro albedo/normal variation tables
- slope/height blend lookup textures
- precomputed detail masks or noise textures

Bad use:

- trying to pre-bake unique textures for "billions" of terrain combinations
- using texture variation as a substitute for geometry streaming

The combinatorics explode long before it becomes practical. The better approach is:

- keep a shared material library
- generate per-chunk masks or procedural blend weights
- let geometry come from streamed chunk meshes

## What is not optional for 120 Hz

The flight sim, atmosphere, and audio update must never wait on terrain generation.

- Terrain generation must be asynchronous.
- Mesh uploads/finalization must be time-budgeted.
- The renderer should consume already-built chunk meshes.
- If terrain cannot finish in time, the sim still runs and the terrain system catches up later.

## Immediate project direction

1. Keep `LOD0` as small streamed tiles around the aircraft.
2. Store terrain edits in chunk-local data, not only in high-level params.
3. Re-mesh only dirty chunks and seam neighbors.
4. Move chunk meshing off the main thread.
5. Use velocity-biased prefetch for high-speed flight.
6. Use separate far-field terrain representation instead of pushing editable detail to the horizon.

Pre-generated textures can support the material system, but they are not the "magical" answer. The magic, if there is any, is strict chunking, asynchronous meshing, aggressive budgeting, and a different representation for the far field.
