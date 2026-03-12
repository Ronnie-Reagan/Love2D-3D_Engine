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

local function countKeys(tbl)
    local n = 0
    for _ in pairs(tbl or {}) do
        n = n + 1
    end
    return n
end

local function countRequiredLods(state)
    local counts = {}
    for _, info in pairs(state.requiredSet or {}) do
        local lod = math.floor(tonumber(info and info.lod) or 0)
        counts[lod] = (counts[lod] or 0) + 1
    end
    return counts
end

local function chunkWorldSize(params, lod)
    lod = math.max(0, math.floor(tonumber(lod) or 0))
    if lod <= 0 then
        return math.max(8, tonumber(params.chunkSize) or 128) * math.max(1, math.floor(tonumber(params.lod0ChunkScale) or 1))
    end
    if lod == 1 then
        return math.max(8, tonumber(params.chunkSize) or 128) * math.max(1, math.floor(tonumber(params.lod1ChunkScale) or 4))
    end
    return math.max(8, tonumber(params.chunkSize) or 128) * math.max(1, math.floor(tonumber(params.lod2ChunkScale) or 16))
end

local function pointCovered(state, params, x, z)
    for _, obj in pairs(state.chunkMap or {}) do
        if type(obj) == "table" then
            local lod = math.floor(tonumber(obj.chunkLod) or 0)
            local size = chunkWorldSize(params, lod)
            local x0 = (tonumber(obj.chunkX) or 0) * size
            local z0 = (tonumber(obj.chunkZ) or 0) * size
            if x >= x0 and x <= (x0 + size) and z >= z0 and z <= (z0 + size) then
                return true
            end
        end
    end
    return false
end

local function run()
    local baseParams = terrain.normalizeGroundParams({
        seed = 5050,
        chunkSize = 24,
        lod0Radius = 1,
        lod1Radius = 2,
        lod2Radius = 4,
        lod0ChunkScale = 1,
        lod1ChunkScale = 4,
        lod2ChunkScale = 16,
        gameplayRadiusMeters = 48,
        midFieldRadiusMeters = 192,
        horizonRadiusMeters = 768,
        splitLodEnabled = false,
        highResSplitRatio = 0.5,
        lod0CellSize = 12,
        lod1CellSize = 12,
        lod2CellSize = 24,
        meshBuildBudget = 1,
        minY = -20,
        maxY = 40,
        autoQualityEnabled = false,
        caveEnabled = false,
        tunnelCount = 0
    }, {})

    local context = {
        defaultGroundParams = baseParams,
        activeGroundParams = nil,
        terrainState = nil,
        camera = { pos = { 0, 40, 0 } },
        drawDistance = 3200,
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
    local initialRequiredCount = countKeys(context.terrainState.requiredSet)
    local requiredLods = countRequiredLods(context.terrainState)
    assertTrue(initialChunkCount > 0, "streaming should populate chunk map")
    assertTrue(initialRequiredCount > 9, "hybrid streaming should request more than the immediate gameplay ring")
    assertTrue((requiredLods[0] or 0) > 0, "required set should include near gameplay chunks")
    assertTrue((requiredLods[1] or 0) > 0, "required set should include mid-field chunks")
    assertTrue((requiredLods[2] or 0) > 0, "required set should include horizon chunks")
    assertTrue(pointCovered(context.terrainState, baseParams, 0, 0), "origin should be covered by a displayed chunk")
    assertTrue(pointCovered(context.terrainState, baseParams, 72, 0), "near-to-mid ring transition should stay covered")
    assertTrue(pointCovered(context.terrainState, baseParams, 216, 0), "mid ring should stay covered")
    assertTrue(pointCovered(context.terrainState, baseParams, 720, 0), "horizon ring should stay covered")

    local initialCenterX = context.terrainState.centerChunkX
    context.camera.pos[1] = context.camera.pos[1] + (baseParams.chunkSize * 2)
    terrain.updateGroundStreaming(false, context)
    for _ = 1, 8 do
        terrain.updateGroundStreaming(false, context)
    end

    assertTrue(context.terrainState.centerChunkX ~= initialCenterX, "camera move should shift streaming center")
    assertTrue(countChunks(context.terrainState) > 0, "streaming should keep active chunks after movement")
    assertTrue(
        countChunks(context.terrainState) >= initialChunkCount,
        "streaming should retain old coverage while new movement chunks are still pending"
    )
    assertTrue(
        countChunks(context.terrainState) <= (countKeys(context.terrainState.requiredSet) * 2),
        "stale chunk retention should stay bounded instead of growing without limit"
    )

    local craterChanged, craterResult = terrain.addCrater({
        x = context.camera.pos[1],
        z = context.camera.pos[3],
        radius = 10
    }, context)
    assertTrue(craterChanged == true, "crater rebuild should succeed")
    context.activeGroundParams = craterResult.activeGroundParams
    context.terrainState = craterResult.terrainState

    assertTrue(
        countChunks(context.terrainState) >= initialChunkCount,
        "crater rebuild should retain displayed chunks until replacements are ready"
    )
    assertTrue(
        countKeys(context.terrainState.staleChunkKeys) > 0,
        "crater rebuild should mark existing chunks stale instead of deleting them immediately"
    )
    assertTrue(
        countChunks(context.terrainState) <= (countKeys(context.terrainState.requiredSet) * 2),
        "force rebuild retention should remain bounded"
    )
    assertTrue(
        pointCovered(context.terrainState, baseParams, context.camera.pos[1], context.camera.pos[3]),
        "camera position should remain covered during crater rebuild"
    )

    print("Terrain chunk streaming tests passed")
end

run()
