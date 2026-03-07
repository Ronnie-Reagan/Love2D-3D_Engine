local viewMath = require("Source.Math.ViewMath")

local function assertTrue(value, message)
    if not value then
        error(message or "assertion failed", 2)
    end
end

local function assertNear(actual, expected, epsilon, message)
    local diff = math.abs((actual or 0) - (expected or 0))
    if diff > (epsilon or 1e-5) then
        error((message or "assertion failed") .. string.format(" (actual=%.6f expected=%.6f)", actual or 0, expected or 0), 2)
    end
end

local function run()
    local yaws = {
        0,
        math.pi * 0.5,
        math.pi,
        math.pi * 1.5
    }
    local deltas = {
        { 10, 0 },
        { 0, 12 },
        { -8, 4 },
        { 15, -3 }
    }

    for _, yaw in ipairs(yaws) do
        for _, delta in ipairs(deltas) do
            local mapX, mapZ = viewMath.worldToMapDelta(delta[1], delta[2], yaw, "heading_up")
            local worldX, worldZ = viewMath.mapToWorldDelta(mapX, mapZ, yaw, "heading_up")
            assertNear(worldX, delta[1], 1e-5, "heading-up world/map roundtrip should preserve X")
            assertNear(worldZ, delta[2], 1e-5, "heading-up world/map roundtrip should preserve Z")
        end
    end

    local northMapX, northMapZ = viewMath.worldToMapDelta(7, -9, math.pi * 0.5, "north_up")
    assertNear(northMapX, 7, 1e-6, "north-up should ignore yaw for X")
    assertNear(northMapZ, -9, 1e-6, "north-up should ignore yaw for Z")

    print("View math map transform tests passed")
end

run()
