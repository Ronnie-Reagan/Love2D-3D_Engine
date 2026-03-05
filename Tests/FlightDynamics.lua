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

local function runSim(frameDt, totalTime)
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

    local elapsed = 0
    while elapsed < totalTime do
        local dt = math.min(frameDt, totalTime - elapsed)
        camera = flight.step(camera, dt, {}, env, cfg)
        elapsed = elapsed + dt
    end

    return camera
end

local function run()
    local cam30 = runSim(1 / 30, 60)
    local cam120 = runSim(1 / 120, 60)

    assertTrue(cam30.flightDebug and cam30.flightDebug.tick > 0, "30Hz sim should advance fixed-step ticks")
    assertTrue(
        cam120.flightDebug and math.abs((cam120.flightDebug.tick or 0) - (cam30.flightDebug.tick or 0)) <= 2,
        "fixed-step ticks should stay consistent across frame rates"
    )

    local speed30 = speed3(cam30.flightVel)
    local speed120 = speed3(cam120.flightVel)
    local altitude30 = cam30.pos[2]
    local altitude120 = cam120.pos[2]

    assertTrue(speed30 > 5 and speed120 > 5, "aircraft should maintain forward motion")

    local speedDiff = math.abs(speed30 - speed120) / math.max(1, speed120)
    local altitudeDiff = math.abs(altitude30 - altitude120) / math.max(1, math.abs(altitude120))
    assertTrue(speedDiff < 0.08, "dt robustness: speed divergence should remain bounded")
    assertTrue(altitudeDiff < 0.10, "dt robustness: altitude divergence should remain bounded")
    assertTrue(speed30 < 120 and speed120 < 120, "speed should not runaway under trimmed cruise throttle")

    print("Flight dynamics tests passed")
end

run()
