local atmosphere = require("Source.Sim.Atmosphere")
local propulsion = require("Source.Sim.PropulsionModel")

local function assertTrue(value, message)
    if not value then
        error(message or "assertion failed", 2)
    end
end

local function run()
    local sea = atmosphere.sample(0)
    local high = atmosphere.sample(5000)

    assertTrue(math.abs(sea.densityKgM3 - 1.225) < 0.08, "sea-level density should be close to ISA")
    assertTrue(high.densityKgM3 < sea.densityKgM3, "density should drop with altitude")

    local engine = { throttle = 0 }
    local config = {
        throttleTimeConstant = 0.8,
        maxThrustSeaLevel = 3200,
        maxEffectivePropSpeed = 95
    }

    engine = propulsion.updateEngineState(engine, 1.0, config, 0.4)
    assertTrue(engine.throttle > 0 and engine.throttle < 1, "throttle lag should not jump instantly")

    local thrustSeaSlow = propulsion.computeThrust(engine, sea, 20, config)
    local thrustSeaFast = propulsion.computeThrust(engine, sea, 90, config)
    local thrustHighSlow = propulsion.computeThrust(engine, high, 20, config)

    assertTrue(thrustSeaSlow.thrustNewton > thrustSeaFast.thrustNewton, "prop thrust should roll off with speed")
    assertTrue(thrustSeaSlow.thrustNewton > thrustHighSlow.thrustNewton, "thrust should scale with air density")

    print("Atmosphere/propulsion tests passed")
end

run()