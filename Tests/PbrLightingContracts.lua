local skyModel = require("Source.Render.SkyModel")
local shadowPass = require("Source.Render.ShadowPass")

local function assertTrue(value, message)
    if not value then
        error(message or "assertion failed", 2)
    end
end

local function len3(v)
    return math.sqrt((v[1] or 0) ^ 2 + (v[2] or 0) ^ 2 + (v[3] or 0) ^ 2)
end

local function run()
    local clearSky = skyModel.evaluate({ 0.3, 0.8, 0.2 }, {
        turbidity = 1.5,
        sunIntensity = 1.2,
        ambient = 0.2,
        exposureEV = 0.0
    })
    local hazySky = skyModel.evaluate({ 0.3, 0.8, 0.2 }, {
        turbidity = 8.0,
        sunIntensity = 1.2,
        ambient = 0.2,
        exposureEV = 1.0
    })

    assertTrue(math.abs(len3(clearSky.lightDir) - 1.0) < 1e-4, "sky model should normalize light direction")
    assertTrue(type(clearSky.skyColor) == "table" and #clearSky.skyColor == 3, "sky color output contract should be vec3")
    assertTrue(hazySky.skyColor[1] ~= clearSky.skyColor[1] or hazySky.skyColor[2] ~= clearSky.skyColor[2], "turbidity should alter sky color")
    assertTrue(hazySky.exposureEV == 1.0, "exposure EV should pass through")

    shadowPass.configure({ enabled = true, size = 64, maxDistance = 20, softness = 0.1 })
    local shadowConfig = shadowPass.getConfig()
    assertTrue(shadowConfig.enabled == true, "shadow pass should enable flag")
    assertTrue(shadowConfig.size >= 256, "shadow map size should clamp")
    assertTrue(shadowConfig.maxDistance >= 100, "shadow distance should clamp")
    assertTrue(shadowConfig.softness >= 0.4, "shadow softness should clamp")

    print("PBR lighting contract tests passed")
end

run()