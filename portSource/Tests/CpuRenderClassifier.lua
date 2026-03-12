local classifier = require("Source.Render.CpuRenderClassifier")

local function assertTrue(value, message)
    if not value then
        error(message or "assertion failed", 2)
    end
end

local function run()
    assertTrue(classifier.isObjectTransparent({ color = { 1, 1, 1, 0.5 } }), "object alpha should mark transparent")
    assertTrue(classifier.isObjectTransparent({ materials = { { alphaMode = "BLEND" } } }), "BLEND material should mark transparent")
    assertTrue(not classifier.isObjectTransparent({ materials = { { alphaMode = "OPAQUE" } }, color = { 1, 1, 1, 1 } }), "opaque object should stay opaque")

    assertTrue(classifier.shouldCullBackfaces({}) == true, "default should cull backfaces")
    assertTrue(classifier.shouldCullBackfaces({ cullBackfaces = false }) == false, "explicit opt-out should disable culling")
    assertTrue(classifier.shouldCullBackfaces({ materials = { { doubleSided = true } } }) == false, "double-sided material should disable culling")

    print("CPU render classifier tests passed")
end

run()
