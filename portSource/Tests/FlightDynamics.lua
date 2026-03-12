local flight = require("Source.Systems.FlightDynamicsSystem")
local q = require("Source.Math.Quat")

local function assertTrue(value, message)
    if not value then
        error(message or "assertion failed", 2)
    end
end

local function speed3(v)
    return math.sqrt((v[1] or 0) ^ 2 + (v[2] or 0) ^ 2 + (v[3] or 0) ^ 2)
end

local function isFiniteNumber(value)
    return type(value) == "number" and value == value and value ~= math.huge and value ~= -math.huge
end

local function assertFiniteVec3(v, label)
    assertTrue(isFiniteNumber(v[1]), label .. " x must be finite")
    assertTrue(isFiniteNumber(v[2]), label .. " y must be finite")
    assertTrue(isFiniteNumber(v[3]), label .. " z must be finite")
end

local function assertFiniteQuat(rot, label)
    assertTrue(isFiniteNumber(rot.w), label .. " w must be finite")
    assertTrue(isFiniteNumber(rot.x), label .. " x must be finite")
    assertTrue(isFiniteNumber(rot.y), label .. " y must be finite")
    assertTrue(isFiniteNumber(rot.z), label .. " z must be finite")
end

local function makeCamera()
    return {
        pos = { 0, 250, 0 },
        rot = q.identity(),
        flightVel = { 0, 0, 35 },
        vel = { 0, 0, 35 },
        throttle = 0.62,
        yoke = { pitch = 0, yaw = 0, roll = 0 },
        flightRotVel = { pitch = 0, yaw = 0, roll = 0 },
        flightAngVel = { 0, 0, 0 },
        box = {
            halfSize = { x = 0.8, y = 1.2, z = 1.4 },
            pos = { 0, 250, 0 }
        }
    }
end

local function runSim(frameDt, totalTime, configMutator)
    local camera = makeCamera()
    local env = {
        wind = { 0, 0, 0 },
        sampleSdf = function()
            return 1e6
        end,
        sampleNormal = function()
            return { 0, 1, 0 }
        end,
        queryGroundHeight = function()
            return -1e6
        end
    }

    local cfg = flight.defaultConfig()
    cfg.enableAutoTrim = true
    cfg.autoTrimUseWorker = false
    if type(configMutator) == "function" then
        configMutator(cfg)
    end

    local elapsed = 0
    while elapsed < totalTime do
        local dt = math.min(frameDt, totalTime - elapsed)
        camera = flight.step(camera, dt, {}, env, cfg)
        elapsed = elapsed + dt
    end

    return camera
end

local function runDtMatrixInvariance()
    local frameRates = { 15, 30, 60, 120 }
    local outcomes = {}

    for _, hz in ipairs(frameRates) do
        outcomes[hz] = runSim(1 / hz, 60)
        local result = outcomes[hz]
        assertTrue(result.flightDebug and result.flightDebug.tick > 0, string.format("%dHz sim should advance ticks", hz))
        assertFiniteVec3(result.flightVel, string.format("%dHz velocity", hz))
        assertFiniteVec3(result.pos, string.format("%dHz position", hz))
        assertFiniteQuat(result.rot, string.format("%dHz rotation", hz))
    end

    local reference = outcomes[120]
    local refTick = reference.flightDebug.tick or 0
    local refSpeed = speed3(reference.flightVel)
    local refAltitude = reference.pos[2]

    for _, hz in ipairs(frameRates) do
        local result = outcomes[hz]
        local tick = result.flightDebug.tick or 0
        local tickDelta = math.abs(tick - refTick)
        assertTrue(
            tickDelta <= 2,
            string.format("fixed-step tick drift too high at %dHz (delta=%d)", hz, tickDelta)
        )

        local speed = speed3(result.flightVel)
        local altitude = result.pos[2]
        local speedDiff = math.abs(speed - refSpeed) / math.max(1, refSpeed)
        local altitudeDiff = math.abs(altitude - refAltitude) / math.max(1, math.abs(refAltitude))

        assertTrue(
            speedDiff < 0.10,
            string.format("dt robustness speed drift too high at %dHz (ratio=%.4f)", hz, speedDiff)
        )
        assertTrue(
            altitudeDiff < 0.12,
            string.format("dt robustness altitude drift too high at %dHz (ratio=%.4f)", hz, altitudeDiff)
        )
        assertTrue(speed < 140, string.format("speed runaway detected at %dHz", hz))
    end
end

local function runExtremeButValidConfigStability()
    local result = runSim(1 / 60, 45, function(cfg)
        cfg.massKg = 620
        cfg.maxThrustSeaLevel = 12000
        cfg.CLalpha = 8.6
        cfg.CD0 = 0.012
        cfg.inducedDragK = 0.028
        cfg.maxLinearSpeed = 260
        cfg.maxAngularRateRad = math.rad(320)
        cfg.maxForceNewton = 130000
        cfg.maxMomentNewtonMeter = 180000
    end)

    assertFiniteVec3(result.pos, "extreme config position")
    assertFiniteVec3(result.flightVel, "extreme config velocity")
    assertFiniteVec3(result.flightAngVel, "extreme config angular velocity")
    assertFiniteQuat(result.rot, "extreme config rotation")
    assertTrue(speed3(result.flightVel) <= 261, "linear speed guard should prevent runaway")
    assertTrue(result.flightCrashEvent == nil, "no terrain contact expected in free-flight stability test")
end

local function run()
    runDtMatrixInvariance()
    runExtremeButValidConfigStability()

    print("Flight dynamics tests passed")
end

run()
