local sdfField = require("Source.Sim.SdfTerrainField")
local terrainCollision = require("Source.Sim.TerrainCollision")

local function assertTrue(value, message)
    if not value then
        error(message or "assertion failed", 2)
    end
end

local function run()
    local params = sdfField.normalizeParams({ seed = 777, caveEnabled = false, tunnelCount = 0 })
    local context = sdfField.createContext(params)

    local x, z = 32, -28
    local expectedSurface = sdfField.sampleSurfaceHeight(x, z, context)

    local hit = terrainCollision.raycast(
        { x, expectedSurface + 180, z },
        { 0, -1, 0 },
        500,
        function(px, py, pz)
            return sdfField.sampleSdf(px, py, pz, context)
        end,
        function(px, py, pz)
            return sdfField.sampleNormal(px, py, pz, context)
        end
    )

    assertTrue(hit ~= nil, "raycast should hit terrain")
    assertTrue(math.abs(hit.point[2] - expectedSurface) < 4.0, "raycast hit should be near sampled surface")

    local groundY = terrainCollision.queryGroundHeight(
        x,
        z,
        function(px, py, pz)
            return sdfField.sampleSdf(px, py, pz, context)
        end,
        function(px, py, pz)
            return sdfField.sampleNormal(px, py, pz, context)
        end,
        { minY = params.minY, maxY = params.maxY + 300 }
    )

    assertTrue(type(groundY) == "number", "queryGroundHeight should return numeric height")
    assertTrue(math.abs(groundY - expectedSurface) < 4.0, "ground height query should be near surface")

    print("Terrain collision tests passed")
end

run()