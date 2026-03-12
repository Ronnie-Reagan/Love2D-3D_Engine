local sdfField = require("Source.Sim.SdfTerrainField")

local function assertTrue(value, message)
    if not value then
        error(message or "assertion failed", 2)
    end
end

local function signature(context)
    local acc = 0
    for x = -96, 96, 24 do
        for y = -64, 128, 24 do
            for z = -96, 96, 24 do
                local d = sdfField.sampleSdf(x, y, z, context)
                acc = acc + math.floor((d * 1000) + 0.5)
            end
        end
    end
    return acc
end

local function run()
    local p1 = sdfField.normalizeParams({ seed = 4242, tunnelCount = 8, caveEnabled = true })
    local c1a = sdfField.createContext(p1)
    local c1b = sdfField.createContext(p1)
    local sig1a = signature(c1a)
    local sig1b = signature(c1b)

    assertTrue(sig1a == sig1b, "same seed/params must be deterministic")

    local p2 = sdfField.normalizeParams({ seed = 4243, tunnelCount = 8, caveEnabled = true })
    local c2 = sdfField.createContext(p2)
    local sig2 = signature(c2)

    assertTrue(sig1a ~= sig2, "different seed should alter terrain field")

    print("Terrain SDF determinism tests passed")
end

run()