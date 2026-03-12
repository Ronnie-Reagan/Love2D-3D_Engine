local gameDefaults = require("Source.Core.GameDefaults")
local q = require("Source.Math.Quat")

local function assertTrue(value, message)
    if not value then
        error(message or "assertion failed", 2)
    end
end

local function run()
    local defaults = gameDefaults.create({}, q)
    assertTrue(type(defaults) == "table", "game defaults should be created")

    local mapState = defaults.mapState or {}
    assertTrue(mapState.orientationMode == "heading_up", "map orientation should default to heading-up")
    assertTrue(mapState.workerEnabled == true, "map raster worker should be enabled by default")
    assertTrue(tonumber(mapState.qualityScale) == 1.0, "map quality scale default mismatch")
    assertTrue(tonumber(mapState.maxResolution) == 512, "map max resolution default mismatch")

    local terrainDefaults = defaults.defaultGroundParams or {}
    assertTrue(tonumber(terrainDefaults.lod0Radius) == 4, "terrain lod0 radius default mismatch")
    assertTrue(tonumber(terrainDefaults.lod1Radius) == 16, "terrain lod1 radius default mismatch")
    assertTrue(tonumber(terrainDefaults.lod2Radius) == 64, "terrain lod2 radius default mismatch")
    assertTrue(tonumber(terrainDefaults.lod0TextureResolution) == 512, "terrain lod0 texture resolution default mismatch")
    assertTrue(tonumber(terrainDefaults.lod1TextureResolution) == 256, "terrain lod1 texture resolution default mismatch")
    assertTrue(tonumber(terrainDefaults.lod2TextureResolution) == 64, "terrain lod2 texture resolution default mismatch")
    assertTrue(tonumber(terrainDefaults.gameplayRadiusMeters) == 512, "terrain gameplay radius default mismatch")
    assertTrue(tonumber(terrainDefaults.midFieldRadiusMeters) == 2048, "terrain mid-field radius default mismatch")
    assertTrue(tonumber(terrainDefaults.horizonRadiusMeters) == 8192, "terrain horizon radius default mismatch")
    assertTrue(tonumber(terrainDefaults.terrainQuality) == 3.5, "terrain quality default mismatch")
    assertTrue(tonumber(terrainDefaults.meshBuildBudget) == 4, "terrain mesh build budget default mismatch")
    assertTrue(tonumber(terrainDefaults.workerMaxInflight) == 6, "terrain worker inflight default mismatch")
    assertTrue(terrainDefaults.autoQualityEnabled == false, "adaptive terrain quality should ship disabled")
    assertTrue(math.abs((tonumber(terrainDefaults.targetFrameMs) or 0) - 16.6) < 1e-6,
        "terrain target frame time default mismatch")
    assertTrue(tonumber(terrainDefaults.chunkCacheLimit) == 128, "terrain chunk cache limit default mismatch")
    assertTrue(tonumber(terrainDefaults.maxPendingChunks) == 96, "terrain pending chunk budget default mismatch")
    assertTrue(tonumber(terrainDefaults.maxStaleChunks) == 32, "terrain stale chunk budget default mismatch")
    assertTrue(tonumber(terrainDefaults.maxDisplayedChunks) == 512, "terrain displayed chunk cap default mismatch")
    assertTrue(terrainDefaults.splitLodEnabled == false, "split LOD should be disabled by default")
    assertTrue(math.abs((tonumber(terrainDefaults.highResSplitRatio) or 0) - 0.5) < 1e-6,
        "split LOD ratio default mismatch")
    assertTrue(terrainDefaults.farLodConeEnabled == true, "terrain far-LOD cone should be enabled by default")
    assertTrue(tonumber(terrainDefaults.farLodConeDegrees) == 110, "terrain far-LOD cone angle default mismatch")

    print("Game defaults contract tests passed")
end

run()
