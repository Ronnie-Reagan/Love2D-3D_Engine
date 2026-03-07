local terrain = require("Source.Systems.TerrainSdfSystem")
local q = require("Source.Math.Quat")

local function assertTrue(value, message)
    if not value then
        error(message or "assertion failed", 2)
    end
end

local function nowSeconds()
    if type(love) == "table" and type(love.timer) == "table" and type(love.timer.getTime) == "function" then
        return love.timer.getTime()
    end
    return os.clock()
end

local function run()
    local params = terrain.normalizeGroundParams({
        seed = 777,
        chunkSize = 32,
        lod0Radius = 1,
        lod1Radius = 3,
        lod2Radius = 6,
        terrainQuality = 5.5,
        autoQualityEnabled = true,
        targetFrameMs = 16.6,
        caveEnabled = false,
        tunnelCount = 0
    }, {})

    local context = {
        defaultGroundParams = params,
        activeGroundParams = nil,
        terrainState = nil,
        camera = { pos = { 0, 40, 0 }, flightVel = { 0, 0, 0 } },
        objects = {},
        q = q,
        dt = 1 / 30,
        frameTimeMs = 16.6,
        log = function() end
    }

    local changed, next = terrain.rebuildGroundFromParams(params, "adaptive quality test", context)
    assertTrue(changed == true, "initial terrain rebuild should succeed")
    context.activeGroundParams = next.activeGroundParams
    context.terrainState = next.terrainState

    local baseQuality = tonumber(context.terrainState.adaptiveTerrainQuality) or tonumber(params.terrainQuality) or 0
    for _ = 1, 2 do
        context.frameTimeMs = 34.0
        terrain.updateGroundStreaming(false, context)
    end
    assertTrue(
        math.abs((tonumber(context.terrainState.adaptiveTerrainQuality) or 0) - baseQuality) < 1e-6,
        "adaptive terrain quality should stay frozen while terrain coverage is still rebuilding"
    )

    for _ = 1, 90 do
        context.frameTimeMs = 16.6
        terrain.updateGroundStreaming(false, context)
        if (tonumber(context.terrainState.missingRequiredChunks) or 0) <= 0 and
            (tonumber(context.terrainState.buildQueueSize) or 0) <= 0 and
            (tonumber(context.terrainState.workerInflightCount) or 0) <= 0 then
            break
        end
    end
    assertTrue(
        (tonumber(context.terrainState.missingRequiredChunks) or 0) <= 0 and
        (tonumber(context.terrainState.buildQueueSize) or 0) <= 0 and
        (tonumber(context.terrainState.workerInflightCount) or 0) <= 0,
        "terrain coverage should settle before adaptive quality is allowed to change"
    )

    context.terrainState.coverageStableSince = nowSeconds() - 1.0
    for _ = 1, 10 do
        context.frameTimeMs = 34.0
        terrain.updateGroundStreaming(false, context)
    end
    assertTrue(
        tonumber(context.terrainState.adaptiveTerrainQuality) < tonumber(params.terrainQuality),
        "sustained slow frames should reduce adaptive terrain quality after coverage stabilizes"
    )

    local reducedQuality = tonumber(context.terrainState.adaptiveTerrainQuality) or 0
    context.terrainState.coverageStableSince = nowSeconds() - 1.0
    for _ = 1, 30 do
        context.frameTimeMs = 10.0
        terrain.updateGroundStreaming(false, context)
    end
    assertTrue(
        tonumber(context.terrainState.adaptiveTerrainQuality) >= reducedQuality,
        "faster frames should allow adaptive terrain quality to recover"
    )
    assertTrue(
        tonumber(context.terrainState.adaptiveTerrainQuality) <= 6.0,
        "adaptive terrain quality should stay within the global safety clamp"
    )

    print("Terrain adaptive quality tests passed")
end

run()
