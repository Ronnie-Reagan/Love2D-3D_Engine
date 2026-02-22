local q = require("quat")
local vector3 = require("vector3")
local love = love
local enet = require "enet"

local hostAddy = "ecosim.donreagan.ca:1988"
local relay = enet.host_create()
local relayServer = relay:connect(hostAddy)

--[[
Notes:

positions are {x = left/right(width), y = up/down(height), z = in/out(depth)} IN WORLD SPACE!!
]]

-- === Initial Configuration ===
function love.load()
    -- 80% screen size
    local width, height = love.window.getDesktopDimensions()
    width, height = width * 0.8, height * 0.8

    love.window.setTitle("Don's 3D Engine")
    love.window.setMode(width, height)
    love.mouse.setRelativeMode(true)
    zBuffer = {}

    screen = {
        w = width,
        h = height
    }

    camera = {
        pos = { 0, 0, -5 },
        rot = q.identity(),
        speed = 10,
        fov = math.rad(90)
    }

    -- defualt testing cube
    cubeModel = {
        vertices = { { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 }, { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } },
        faces = { { 4, 3, 2, 1 }, { 5, 6, 7, 8 }, { 1, 2, 6, 5 }, { 2, 3, 7, 6 }, { 3, 4, 8, 7 }, { 4, 1, 5, 8 } }
    }

    -- creating the cube's object
    objects = {
        [1] = {
            model = cubeModel,
            pos = { 0, 0, 10 },
            rot = q.identity(),
            color = { 0.9, 0.4, 0.1 },
            isSolid = true
        },

    }

    groundObject = {
        model = {
            vertices = { { -50, 0, -50 }, { 50, 0, -50 }, { 50, 0, 50 }, { -50, 0, 50 } },
            faces = { { 1, 2, 3, 4 }, -- top
                { 4, 3, 2, 1 }        -- bottom (backface)
            }
        },
        pos = { 0, -80, 0 },
        rot = q.identity(),
        isSolid = true,
        color = { 0.3, 0.3, 0.3 }
    }

    table.insert(objects, groundObject)
end

-- === Utilities ===
function loadSTL(path)
    local vertices, faces = {}, {}
    -- Parse ASCII STL (add binary support later if needed)
    for line in love.filesystem.lines(path) do
        if line:match("^vertex") then
            local x, y, z = line:match("vertex%s+([%-%d%.]+)%s+([%-%d%.]+)%s+([%-%d%.]+)")
            table.insert(vertices, { tonumber(x), tonumber(y), tonumber(z) })
        elseif line:match("^endfacet") then
            local count = #vertices
            table.insert(faces, { count - 2, count - 1, count })
        end
    end
    return {
        vertices = vertices,
        faces = faces,
        isSolid = true
    }
end

function checkCollision(objA, objB)
    if not objA.isSolid or not objB.isSolid then
        return false
    end
    local ax, ay, az = unpack(objA.pos)
    local bx, by, bz = unpack(objB.pos)
    local dx = ax - bx
    local dy = ay - by
    local dz = az - bz
    local distSq = dx * dx + dy * dy + dz * dz
    return distSq < (objA.radius + objB.radius) ^ 2
end

local function getCameraBasis()
    local forward = q.rotateVector(camera.rot, { 0, 0, 1 })
    local right = q.rotateVector(camera.rot, { 1, 0, 0 })
    local up = q.rotateVector(camera.rot, { 0, 1, 0 })
    return vector3.normalizeVec(forward), vector3.normalizeVec(right), vector3.normalizeVec(up)
end

local function transformVertex(v, obj)
    local rotated = q.rotateVector(obj.rot, v)
    local world = { rotated[1] + obj.pos[1], rotated[2] + obj.pos[2], rotated[3] + obj.pos[3] }

    local rel = { world[1] - camera.pos[1], world[2] - camera.pos[2], world[3] - camera.pos[3] }

    local camConj = q.conjugate and q.conjugate(camera.rot) or {
        w = camera.rot.w,
        x = -camera.rot.x,
        y = -camera.rot.y,
        z = -camera.rot.z
    }

    local camSpace = q.rotateVector(camConj, rel)
    return camSpace[1], camSpace[2], camSpace[3]
end

local function project(x, y, z)
    z = math.max(0.01, z)
    local f = 1 / math.tan(camera.fov / 2)
    local aspect = screen.w / screen.h
    local px = x * f / z
    local py = y * f / z
    return screen.w / 2 + px * screen.w / 2, screen.h / 2 - py * screen.h / 2
end

local flightSimMode = false
-- === Mouse Look ===
local relative = true
function love.mousemoved(x, y, dx, dy)
    if not relative then
        return
    end
    x, y, dx, dy = -x, -y, -dx * 2, -dy
    local horizontal_sensitivity = 0.001
    local vertical_sensitivity = 0.0005

    -- Rotate around camera's local Y axis (yaw) and local X axis (pitch)
    local right = q.rotateVector(camera.rot, { 1, 0, 0 })
    local up = q.rotateVector(camera.rot, { 0, 1, 0 })

    local pitchQuat = q.fromAxisAngle(right, -dy * horizontal_sensitivity)
    local yawQuat = q.fromAxisAngle(up, -dx * vertical_sensitivity)

    -- Apply pitch then yaw (relative to current orientation)
    camera.rot = q.normalize(q.multiply(yawQuat, q.multiply(pitchQuat, camera.rot)))
end

local function createObjectForPeer(peerID)
            table.insert(objects, {
            model = cubeModel,
            pos = { 50, 10, 10 },
            rot = q.identity(),
            color = { 0.9, 0.4, 0.1 },
            isSolid = true,
            id = peerID
        }
    )
end

local peers = {}
function handlePacket(data)
    local parts = {}
    for p in string.gmatch(data, "([^|]+)") do
        table.insert(parts, p)
    end

    if parts[1] == "STATE" then
        if not peers[parts[#parts]] then
            peers[parts[#parts]] = createObjectForPeer(tonumber(parts[#parts]))
        end
        objects[2].pos = {
            tonumber(parts[2]),
            tonumber(parts[3]),
            tonumber(parts[4])
        }
        objects[2].rot = {
            w = tonumber(parts[5]),
            x = tonumber(parts[6]),
            y = tonumber(parts[7]),
            z = tonumber(parts[8])
        }
    end
end

local event = relay:service()

local function updateNet()
    if relayServer then
        local packet = string.format(
            "STATE|%f|%f|%f|%f|%f|%f|%f",
            camera.pos[1],
            camera.pos[2],
            camera.pos[3],
            camera.rot.w,
            camera.rot.x,
            camera.rot.y,
            camera.rot.z
        )
        relayServer:send(packet)
    end
end

-- === Camera Movement ===
function love.update(dt)
    local speed = camera.speed * dt
    local forward, right, up = getCameraBasis()

    if love.keyboard.isDown("w") or flightSimMode == true then
        for i = 1, 3 do
            camera.pos[i] = camera.pos[i] + forward[i] * (speed * (camera.speedBoost and 1.5 or 1))
        end
    end
    if love.keyboard.isDown("s") then
        for i = 1, 3 do
            camera.pos[i] = camera.pos[i] - forward[i] * (speed * (camera.speedBoost and 1.5 or 1))
        end
    end
    if love.keyboard.isDown("d") then
        for i = 1, 3 do
            camera.pos[i] = camera.pos[i] + right[i] * (speed * (camera.speedBoost and 1.5 or 1))
        end
    end
    if love.keyboard.isDown("a") then
        for i = 1, 3 do
            camera.pos[i] = camera.pos[i] - right[i] * (speed * (camera.speedBoost and 1.5 or 1))
        end
    end
    if love.keyboard.isDown("x") or love.keyboard.isDown("space") then
        for i = 1, 3 do
            camera.pos[i] = camera.pos[i] + up[i] * (speed * (camera.speedBoost and 1.5 or 1))
        end
    end
    if love.keyboard.isDown("lshift") then
        camera.speedBoost = true
    else
        camera.speedBoost = false
    end
    if love.keyboard.isDown("z") or love.keyboard.isDown("lctrl") then
        for i = 1, 3 do
            camera.pos[i] = camera.pos[i] - up[i] * (speed * (camera.speedBoost and 1.5 or 1))
        end
    end
    if love.keyboard.isDown("e") then
        local roll = q.fromAxisAngle(q.rotateVector(camera.rot, { 0, 0, 0.5 }), -math.rad(2.5))
        camera.rot = q.normalize(q.multiply(roll, camera.rot))
    elseif love.keyboard.isDown("q") then
        local roll = q.fromAxisAngle(q.rotateVector(camera.rot, { 0, 0, 0.5 }), math.rad(2.5))
        camera.rot = q.normalize(q.multiply(roll, camera.rot))
    end
    updateNet()
    local event = relay:service()

    while event do
        if event.type == "receive" then
            handlePacket(event.data)
        end

        event = relay:service()
    end
end

-- === Input Management ===
function love.keypressed(key)
    if key == "escape" then
        love.mouse.setRelativeMode(false)
        relative = false
    end

    -- debugging position/rotation and relating variables
    if key == "p" then
        local pos = camera.pos
        local rot = camera.rot
        local forward, right, up = getCameraBasis()

        print("\n=== Camera Debug Info ===")
        print(string.format("Position:    x=%.3f  y=%.3f  z=%.3f", pos[1], pos[2], pos[3]))
        print(string.format("Rotation:    w=%.5f  x=%.5f  y=%.5f  z=%.5f", rot.w, rot.x, rot.y, rot.z))
        print(string.format("Forward vec: x=%.3f  y=%.3f  z=%.3f", forward[1], forward[2], forward[3]))
        print(string.format("Right vec:   x=%.3f  y=%.3f  z=%.3f", right[1], right[2], right[3]))
        print(string.format("Up vec:      x=%.3f  y=%.3f  z=%.3f", up[1], up[2], up[3]))
    end
end

function love.mousepressed(x, y, button)
    if not love.mouse.getRelativeMode() then
        love.mouse.setRelativeMode(true)
        relative = true
    end
end

function love.mousefocus(focused)
    if not focused then
        love.mouse.setRelativeMode(false)
        relative = false
    end
end

local function drawTriangle(v1, v2, v3, color)
    love.graphics.setColor(color or { 1, 1, 1 })
    love.graphics.polygon("fill",
        v1[1], v1[2],
        v2[1], v2[2],
        v3[1], v3[2]
    )
    love.graphics.setColor(0, 0, 0)
    love.graphics.polygon("line",
        v1[1], v1[2],
        v2[1], v2[2],
        v3[1], v3[2]
    )
end

function drawObject(obj, skipCulling)
    local transformedVerts = {}
    local screenVerts = {}

    -- Transform vertices into camera space and screen space
    for i, v in ipairs(obj.model.vertices) do
        local x, y, z = transformVertex(v, obj)
        transformedVerts[i] = { x, y, z }
        if z > 0.01 then
            local sx, sy = project(x, y, z)
            screenVerts[i] = { sx, sy, z }
        end
    end

    -- Triangulate faces and raster using Z-buffer
    for _, face in ipairs(obj.model.faces) do
        local visibleCount = 0
        for _, vi in ipairs(face) do
            if screenVerts[vi] then
                visibleCount = visibleCount + 1
            end
        end
        if visibleCount < 3 then
            goto continue
        end

        -- Optional backface culling
        if not skipCulling then
            local v1, v2, v3 = transformedVerts[face[1]], transformedVerts[face[2]], transformedVerts[face[3]]
            local edge1 = vector3.sub(v2, v1)
            local edge2 = vector3.sub(v3, v1)
            local normal = vector3.normalizeVec(vector3.cross(edge1, edge2))
            local toCam = vector3.normalizeVec({ -v1[1], -v1[2], -v1[3] })
            local dot = vector3.dot(normal, toCam)
            if dot <= 0 then
                goto continue
            end
        end

        -- Triangulate quad/poly faces using fan method
        for i = 2, #face - 1 do
            local v1 = screenVerts[face[1]]
            local v2 = screenVerts[face[i]]
            local v3 = screenVerts[face[i + 1]]

            if v1 and v2 and v3 then
                drawTriangle(v1, v2, v3, obj.color or { 0.5, 0.5, 0.5 })
            end
        end

        ::continue::
    end
end

function love.draw()
    triangleCount = 0
    local centerX, centerY = screen.w / 2, screen.h / 2
    love.graphics.setColor(0.2, 0.2, 0.75, 0.8)
    love.graphics.rectangle("fill", 0, 0, screen.w, screen.h)
    -- reset z-buffer
    --zBuffer = {}
    --for x = 1, screen.w do
    --    zBuffer[x] = {}
    --    for y = 1, screen.h do
    --        zBuffer[x][y] = math.huge
    --    end
    --end

    -- Sort objects by distance from camera (farther first)
    table.sort(objects, function(a, b)
        local ax, ay, az = unpack(a.pos)
        local bx, by, bz = unpack(b.pos)
        local cx, cy, cz = unpack(camera.pos)

        local da = (ax - cx) ^ 2 + (ay - cy) ^ 2 + (az - cz) ^ 2
        local db = (bx - cx) ^ 2 + (by - cy) ^ 2 + (bz - cz) ^ 2

        return da > db -- farthest first
    end)

    -- Draw objects in sorted order
    for _, obj in ipairs(objects) do
        drawObject(obj)
    end


    love.graphics.setColor(1, 1, 1)
    love.graphics.print("WASD + QE/ZC, Mouse to look, Esc to release mouse", 10, 10)
    love.graphics.setColor(1, 0, 0, 0.5)
    love.graphics.circle("fill", centerX, centerY, 1)
end
