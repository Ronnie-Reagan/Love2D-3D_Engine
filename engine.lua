local engine = {}
local love = require "love"


--[[
Loads an STL obj file and creates vertices/faces for it to be added to the world for rendering and simulation
Essentially non-functional unless you manage to attain/create a 'unicorn' file that is compatible

to be revised after implementing globes/spheres/balls; allowing for more complexe files like guns and player models?

"file_path" → {vertices:table, faces:table, isSolid:True}
]]
function engine.loadSTL(path)
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

--[[
This function does not do dt detection for proper rebound calculations!
Only use this if you need to know if theyre colliding right now

objectA, objectB → true on hit, false on miss
]]
function engine.checkCollision(objA, objB)
    if not objA.isSolid or not objB.isSolid then
        return false
    end
    local ax, ay, az = table.unpack(objA.pos)
    local bx, by, bz = table.unpack(objB.pos)
    local dx = ax - bx
    local dy = ay - by
    local dz = az - bz
    local distSq = dx * dx + dy * dy + dz * dz
    return distSq < (objA.radius + objB.radius) ^ 2
end

function engine.getCameraBasis(camera, q, vector3)
    local forward = q.rotateVector(camera.rot, { 0, 0, 1 })
    local right = q.rotateVector(camera.rot, { 1, 0, 0 })
    local up = q.rotateVector(camera.rot, { 0, 1, 0 })
    return vector3.normalizeVec(forward), vector3.normalizeVec(right), vector3.normalizeVec(up)
end

function engine.transformVertex(v, obj, camera, q)
    local rotated = q.rotateVector(obj.rot, v)
    local world = { rotated[1] + obj.pos[1], rotated[2] + obj.pos[2], rotated[3] + obj.pos[3] }

    local rel = { world[1] - camera.pos[1], world[2] - camera.pos[2], world[3] - camera.pos[3] }

    local camConj = (q.conjugate and q.conjugate(camera.rot)) or {
        w = camera.rot.w,
        x = -camera.rot.x,
        y = -camera.rot.y,
        z = -camera.rot.z
    }

    local camSpace = q.rotateVector(camConj, rel)
    return camSpace[1], camSpace[2], camSpace[3]
end

function engine.project(x, y, z, camera, screen)
    z = math.max(0.01, z)
    local f = 1 / math.tan(camera.fov / 2)
    -- local aspect = screen.w / screen.h -- unsure why this is here still
    local px = x * f / z
    local py = y * f / z
    return screen.w / 2 + px * screen.w / 2, screen.h / 2 - py * screen.h / 2
end

function engine.checkAABBCollision(box, obj)
    -- box: { pos = {x,y,z}, halfSize = {x,y,z} }
    -- obj: { pos = {x,y,z}, halfSize = {x,y,z} }
    local dx = math.abs(box.pos[1] - obj.pos[1])
    local dy = math.abs(box.pos[2] - obj.pos[2])
    local dz = math.abs(box.pos[3] - obj.pos[3])

    return dx <= (box.halfSize.x + obj.halfSize.x) and
           dy <= (box.halfSize.y + obj.halfSize.y) and
           dz <= (box.halfSize.z + obj.halfSize.z)
end

-- === Camera Movement ===
function engine.processMovement(camera, dt, flightSimMode, vector3, q, objects)
    local speed = camera.speed * dt
    local forward, right, up = engine.getCameraBasis(camera, q, vector3)

    if flightSimMode then
        -- Flight mode: throttle & roll
        if love.keyboard.isDown("w") then camera.thrust = 1 end
        if love.keyboard.isDown("s") then camera.thrust = -1 end

        camera.throttle = math.max(0, math.min(camera.throttle + (camera.thrust or 0) * dt, camera.maxSpeed))
        for i = 1, 3 do
            camera.pos[i] = camera.pos[i] + forward[i] * camera.throttle * dt
        end

        if love.keyboard.isDown("q") then
            local roll = q.fromAxisAngle(forward, math.rad(45 * dt))
            camera.rot = q.normalize(q.multiply(roll, camera.rot))
        elseif love.keyboard.isDown("e") then
            local roll = q.fromAxisAngle(forward, -math.rad(45 * dt))
            camera.rot = q.normalize(q.multiply(roll, camera.rot))
        end
        return camera
    end

    -- === Ground movement ===
    -- Horizontal input
    local moveDir = { 0, 0, 0 }
    if love.keyboard.isDown("w") then moveDir = vector3.add(moveDir, forward) end
    if love.keyboard.isDown("s") then moveDir = vector3.sub(moveDir, forward) end
    if love.keyboard.isDown("d") then moveDir = vector3.add(moveDir, right) end
    if love.keyboard.isDown("a") then moveDir = vector3.sub(moveDir, right) end
    moveDir = vector3.normalizeVec(moveDir)

    camera.pos[1] = camera.pos[1] + moveDir[1] * speed
    camera.pos[3] = camera.pos[3] + moveDir[3] * speed

    -- Gravity
    camera.vel[2] = camera.vel[2] + camera.gravity * dt
    camera.pos[2] = camera.pos[2] + camera.vel[2] * dt

    -- Update camera box
    camera.box.pos = { camera.pos[1], camera.pos[2] - camera.box.halfSize.y, camera.pos[3] }

    -- Collision with ground tiles: find highest tile under camera
    local highestY = -math.huge
    for _, obj in ipairs(objects) do
        if obj.isSolid and obj.halfSize then
            local dx = math.abs(camera.box.pos[1] - obj.pos[1])
            local dz = math.abs(camera.box.pos[3] - obj.pos[3])
            if dx <= obj.halfSize.x + camera.box.halfSize.x and dz <= obj.halfSize.z + camera.box.halfSize.z then
                local topY = obj.pos[2] + obj.halfSize.y
                if topY > highestY then highestY = topY end
            end
        end
    end

    -- Snap to ground
    if camera.pos[2] - camera.box.halfSize.y <= highestY then
        camera.pos[2] = highestY + camera.box.halfSize.y
        camera.vel[2] = 0
        camera.onGround = true
    else
        camera.onGround = false
    end

    -- Jump
    if love.keyboard.isDown("space") and camera.onGround then
        camera.vel[2] = camera.jumpSpeed
        camera.onGround = false
    end

    return camera
end

function engine.drawTriangle(v1, v2, v3, color)
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

function engine.drawObject(obj, skipCulling, camera, vector3, q, screen)
    local transformedVerts = {}
    local screenVerts = {}

    -- Transform vertices into camera space and screen space
    for i, v in ipairs(obj.model.vertices) do
        local x, y, z = engine.transformVertex(v, obj, camera, q)
        transformedVerts[i] = { x, y, z }
        if z > 0.01 then
            local sx, sy = engine.project(x, y, z, camera, screen)
            screenVerts[i] = { sx, sy, z }
        end
    end

    -- Triangulate faces and raster using Z-buffer
    for _, face in ipairs(obj.model.faces) do
        local poly = {}

        for _, vi in ipairs(face) do
            table.insert(poly, transformedVerts[vi])
        end

        poly = engine.clipPolygonToNearPlane(poly, 0.01)

        if #poly < 3 then
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
        local projected = {}

        for i, v in ipairs(poly) do
            local sx, sy = engine.project(v[1], v[2], v[3], camera, screen)
            projected[i] = { sx, sy, v[3] }
        end

        for i = 2, #projected - 1 do
            engine.drawTriangle(
                projected[1],
                projected[i],
                projected[i + 1],
                obj.color or { 0.5, 0.5, 0.5 }
            )
        end

        ::continue::
    end
end

function engine.worldToCamera(worldPos, camera, q)
    local rel = {
        worldPos[1] - camera.pos[1],
        worldPos[2] - camera.pos[2],
        worldPos[3] - camera.pos[3]
    }

    local camConj = q.conjugate(camera.rot)
    return q.rotateVector(camConj, rel)
end

function engine.clipPolygonToNearPlane(verts, nearZ)
    local clipped = {}

    local function interpolate(v1, v2)
        local t = (nearZ - v1[3]) / (v2[3] - v1[3])
        return {
            v1[1] + t * (v2[1] - v1[1]),
            v1[2] + t * (v2[2] - v1[2]),
            nearZ
        }
    end

    for i = 1, #verts do
        local current = verts[i]
        local nextVert = verts[i % #verts + 1]

        local currentInside = current[3] > nearZ
        local nextInside = nextVert[3] > nearZ

        if currentInside then
            table.insert(clipped, current)
        end

        if currentInside ~= nextInside then
            table.insert(clipped, interpolate(current, nextVert))
        end
    end

    return clipped
end

return engine
