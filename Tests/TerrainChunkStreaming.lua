local terrain = require("Source.Systems.TerrainSdfSystem")
local q = require("Source.Math.Quat")

local function assertTrue(value, message)
    if not value then
        error(message or "assertion failed", 2)
    end
end

local function countChunks(state)
    local n = 0
    for _ in pairs(state.chunkMap or {}) do
        n = n + 1
    end
    return n
end

local function run()
    local baseParams = terrain.normalizeGroundParams({
        seed = 5050,
        chunkSize = 24,
        lod0Radius = 1,
        lod1Radius = 1,
        lod0CellSize = 12,
        lod1CellSize = 12,
        meshBuildBudget = 3,
        minY = -20,
        maxY = 40,
        caveEnabled = false,
        tunnelCount = 0
    }, {})

    local context = {
        defaultGroundParams = baseParams,
        activeGroundParams = nil,
        terrainState = nil,
        camera = { pos = { 0, 40, 0 } },
        objects = {},
        q = q,
        log = function() end
    }

    local changed, next = terrain.rebuildGroundFromParams(baseParams, "test", context)
    assertTrue(changed == true, "initial rebuild should report changes")
    assertTrue(type(next) == "table" and type(next.terrainState) == "table", "rebuild should return terrain state")

    context.activeGroundParams = next.activeGroundParams
    context.terrainState = next.terrainState

    for _ = 1, 12 do
        terrain.updateGroundStreaming(false, context)
    end

    local initialChunkCount = countChunks(context.terrainState)
    assertTrue(initialChunkCount > 0, "streaming should populate chunk map")

    local initialCenterX = context.terrainState.centerChunkX
    context.camera.pos[1] = context.camera.pos[1] + (baseParams.chunkSize * 2)
    terrain.updateGroundStreaming(false, context)
    for _ = 1, 8 do
        terrain.updateGroundStreaming(false, context)
    end

    assertTrue(context.terrainState.centerChunkX ~= initialCenterX, "camera move should shift streaming center")
    assertTrue(countChunks(context.terrainState) > 0, "streaming should keep active chunks after movement")

    print("Terrain chunk streaming tests passed")
end

run()
