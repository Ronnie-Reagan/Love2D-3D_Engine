package.loaded["love"] = package.loaded["love"] or {
    graphics = {
        newShader = function()
            return {}
        end
    }
}

local engine = require("Source.Core.Engine")
local vector3 = require("Source.Math.Vector3")
local q = require("Source.Math.Quat")

local function assertTrue(value, message)
    if not value then
        error(message or "assertion failed", 2)
    end
end

local function assertNear(actual, expected, epsilon, message)
    if math.abs(actual - expected) > (epsilon or 1e-6) then
        error((message or "values differ") .. string.format(" (actual=%f expected=%f)", actual, expected), 2)
    end
end

local function testProjectionAspect()
    local camera = { fov = math.rad(90) }
    local square = { w = 200, h = 200 }
    local wide = { w = 400, h = 200 }

    local sxSquare = select(1, engine.project(1, 0, 2, camera, square))
    local sxWide = select(1, engine.project(1, 0, 2, camera, wide))

    local offsetSquare = (sxSquare - (square.w * 0.5)) / (square.w * 0.5)
    local offsetWide = (sxWide - (wide.w * 0.5)) / (wide.w * 0.5)

    assertNear(offsetSquare, 0.5, 1e-6, "square projection offset mismatch")
    assertNear(offsetWide, 0.25, 1e-6, "wide projection should include aspect correction")
end

local function testCollisionHalfSizeAndRadiusFallback()
    local aabbA = { isSolid = true, pos = { 0, 0, 0 }, halfSize = { x = 1, y = 1, z = 1 } }
    local aabbB = { isSolid = true, pos = { 1.5, 0, 0 }, halfSize = { x = 1, y = 1, z = 1 } }
    local aabbC = { isSolid = true, pos = { 3.5, 0, 0 }, halfSize = { x = 1, y = 1, z = 1 } }

    assertTrue(engine.checkCollision(aabbA, aabbB), "AABB collision should hit")
    assertTrue(not engine.checkCollision(aabbA, aabbC), "AABB collision should miss")

    local radiusA = { isSolid = true, pos = { 0, 0, 0 }, radius = 1 }
    local radiusB = { isSolid = true, pos = { 1.5, 0, 0 }, radius = 1 }
    assertTrue(engine.checkCollision(radiusA, radiusB), "legacy radius fallback should still collide")

    local mixed = { isSolid = true, pos = { 0.5, 0, 0 }, halfSize = { x = 0.75, y = 0.75, z = 0.75 } }
    assertTrue(engine.checkCollision(radiusA, mixed), "mixed halfSize/radius collision should be supported")
end

local function testProcessMovementWalkingOnly()
    local camera = {
        pos = { 10, 20, 30 },
        rot = q.identity(),
        speed = 8,
        vel = { 0, 0, 0 },
        gravity = -9.81,
        jumpSpeed = 6,
        box = {
            pos = { 10, 20, 30 },
            halfSize = { x = 0.5, y = 1.0, z = 0.5 }
        }
    }
    local beforePos = { camera.pos[1], camera.pos[2], camera.pos[3] }
    local moved = engine.processMovement(camera, 1 / 60, true, vector3, q, {}, {
        walkForward = true,
        flightThrottleUp = true
    }, nil)
    assertTrue(moved == camera, "movement should return original camera reference")
    assertNear(camera.pos[1], beforePos[1], 1e-9, "flight mode should not move camera in engine.processMovement")
    assertNear(camera.pos[2], beforePos[2], 1e-9, "flight mode should not integrate gravity in engine.processMovement")
    assertNear(camera.pos[3], beforePos[3], 1e-9, "flight mode should not move camera in engine.processMovement")
end

local function run()
    testProjectionAspect()
    testCollisionHalfSizeAndRadiusFallback()
    testProcessMovementWalkingOnly()
    print("Engine math/collision tests passed")
end

run()
