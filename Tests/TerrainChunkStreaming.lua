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

local function countUniqueChunkCoords(state)
    local seen = {}
    local n = 0
    for key, obj in pairs(state.chunkMap or {}) do
        local cx = tonumber(obj and obj.chunkX)
        local cz = tonumber(obj and obj.chunkZ)
        if cx == nil or cz == nil then
            local parts = {}
            for token in tostring(key):gmatch("[^:]+") do
                parts[#parts + 1] = token
            end
            if #parts >= 5 then
                cx = tonumber(parts[2]) or 0
                cz = tonumber(parts[3]) or 0
            else
                cx = tonumber(parts[1]) or 0
                cz = tonumber(parts[2]) or 0
            end
        end
        local coordKey = tostring(math.floor(cx or 0)) .. ":" .. tostring(math.floor(cz or 0))
        if not seen[coordKey] then
            seen[coordKey] = true
            n = n + 1
        end
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

local function run()
    local baseParams = terrain.normalizeGroundParams({
        seed = 5050,
        chunkSize = 24,
        lod0Radius = 1,
        lod1Radius = 1,
        lod2Radius = 3,
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
    assertTrue(initialChunkCount > 0, "streaming should populate chunk map")
    assertTrue(
        initialRequiredCount <= ((baseParams.lod2Radius * 2 + 1) * (baseParams.lod2Radius * 2 + 1)),
        "render draw distance should not expand terrain coverage beyond the configured lod2 radius"
    )
    local nonZeroLodCount = 0
    for _, info in pairs(context.terrainState.requiredSet or {}) do
        if tonumber(info.lod) ~= 0 then
            nonZeroLodCount = nonZeroLodCount + 1
        end
    end
    assertTrue(nonZeroLodCount == 0, "terrain streaming should request only fixed LOD0 chunks")

    local initialCenterX = context.terrainState.centerChunkX
    context.camera.pos[1] = context.camera.pos[1] + (baseParams.chunkSize * 2)
    terrain.updateGroundStreaming(false, context)
    for _ = 1, 8 do
        terrain.updateGroundStreaming(false, context)
    end

    assertTrue(context.terrainState.centerChunkX ~= initialCenterX, "camera move should shift streaming center")
    assertTrue(countChunks(context.terrainState) > 0, "streaming should keep active chunks after movement")
    assertTrue(
        countChunks(context.terrainState) == countUniqueChunkCoords(context.terrainState),
        "streaming should not keep multiple LOD meshes for the same chunk coordinate"
    )
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
        countChunks(context.terrainState) == countUniqueChunkCoords(context.terrainState),
        "force rebuild retention should keep a single displayed mesh per chunk coordinate"
    )

    print("Terrain chunk streaming tests passed")
end

run()
