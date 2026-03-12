local engine = {}
local love = require "love"
-- === GPU Shader Setup ===
engine.defaultShaderCode = [[
extern mat4 modelViewProjection;

vec4 position(mat4 transform_projection, vec4 vertex_position)
{
    return modelViewProjection * vertex_position;
}

vec4 effect(vec4 color, Image texture, vec2 texCoords, vec2 screenCoords)
{
    return color;
}
]]
engine.defaultShader = nil

function engine.ensureDefaultShader()
    if engine.defaultShader then
        return true
    end

    local ok, shaderOrErr = pcall(love.graphics.newShader, engine.defaultShaderCode)
    if not ok then
        return false, shaderOrErr
    end

    engine.defaultShader = shaderOrErr
    return true
end

--[[
Loads an STL obj file and creates vertices/faces for it to be added to the world for rendering and simulation
Essentially non-functional unless you manage to attain/create a 'unicorn' file that is compatible

to be revised after implementing globes/spheres/balls; allowing for more complexe files like guns and player models?

"file_path" → {vertices:table, faces:table, isSolid:True}
]]
local function readU32LE(data, offset)
    local b1, b2, b3, b4 = data:byte(offset, offset + 3)
    if not b4 then
        return nil
    end
    return b1 + b2 * 256 + b3 * 65536 + b4 * 16777216
end

local function readF32LE(data, offset)
    local b1, b2, b3, b4 = data:byte(offset, offset + 3)
    if not b4 then
        return nil
    end

    local sign = (b4 >= 128) and -1 or 1
    local exponent = (b4 % 128) * 2 + math.floor(b3 / 128)
    local mantissa = (b3 % 128) * 65536 + b2 * 256 + b1

    if exponent == 255 then
        if mantissa == 0 then
            return sign * math.huge
        end
        return 0 / 0
    end

    if exponent == 0 then
        if mantissa == 0 then
            return sign * 0
        end
        return sign * (mantissa * 2^-149)
    end

    return sign * ((1 + (mantissa / 8388608)) * 2^(exponent - 127))
end

local function shouldFlipFaceByFacetNormal(a, b, c, nx, ny, nz)
    if nx == nil or ny == nil or nz == nil then
        return false
    end
    local nLenSq = nx * nx + ny * ny + nz * nz
    if nLenSq <= 1e-16 then
        return false
    end

    local e1x = b[1] - a[1]
    local e1y = b[2] - a[2]
    local e1z = b[3] - a[3]
    local e2x = c[1] - a[1]
    local e2y = c[2] - a[2]
    local e2z = c[3] - a[3]

    local cx = e1y * e2z - e1z * e2y
    local cy = e1z * e2x - e1x * e2z
    local cz = e1x * e2y - e1y * e2x
    local cLenSq = cx * cx + cy * cy + cz * cz
    if cLenSq <= 1e-16 then
        return false
    end

    return (cx * nx + cy * ny + cz * nz) < 0
end

local function parseAsciiSTL(data)
    local vertices, faces = {}, {}
    local facetNx, facetNy, facetNz = nil, nil, nil

    for line in data:gmatch("[^\r\n]+") do
        local nx, ny, nz = line:match(
            "^%s*facet%s+normal%s+([%+%-]?[%d%.eE]+)%s+([%+%-]?[%d%.eE]+)%s+([%+%-]?[%d%.eE]+)"
        )
        if nx and ny and nz then
            facetNx, facetNy, facetNz = tonumber(nx), tonumber(ny), tonumber(nz)
        end

        local x, y, z = line:match("^%s*vertex%s+([%+%-]?[%d%.eE]+)%s+([%+%-]?[%d%.eE]+)%s+([%+%-]?[%d%.eE]+)")
        if x and y and z then
            vertices[#vertices + 1] = { tonumber(x), tonumber(y), tonumber(z) }
        elseif line:match("^%s*endfacet") then
            local count = #vertices
            if count >= 3 then
                local i1, i2, i3 = count - 2, count - 1, count
                if shouldFlipFaceByFacetNormal(
                        vertices[i1],
                        vertices[i2],
                        vertices[i3],
                        facetNx,
                        facetNy,
                        facetNz
                    ) then
                    i2, i3 = i3, i2
                end
                faces[#faces + 1] = { i1, i2, i3 }
            end
            facetNx, facetNy, facetNz = nil, nil, nil
        end
    end

    if #faces == 0 then
        return nil, "no faces found in ASCII STL"
    end

    return {
        vertices = vertices,
        faces = faces,
        isSolid = true
    }
end

local function parseBinarySTL(data)
    if #data < 84 then
        return nil, "binary STL header too short"
    end

    local triCount = readU32LE(data, 81)
    if not triCount then
        return nil, "could not read STL triangle count"
    end

    local expectedBytes = 84 + triCount * 50
    if #data < expectedBytes then
        return nil, string.format("binary STL truncated (%d of %d bytes)", #data, expectedBytes)
    end

    local vertices, faces = {}, {}
    local cursor = 85

    for _ = 1, triCount do
        local nx = readF32LE(data, cursor)
        local ny = readF32LE(data, cursor + 4)
        local nz = readF32LE(data, cursor + 8)

        local ax = readF32LE(data, cursor + 12)
        local ay = readF32LE(data, cursor + 16)
        local az = readF32LE(data, cursor + 20)
        local bx = readF32LE(data, cursor + 24)
        local by = readF32LE(data, cursor + 28)
        local bz = readF32LE(data, cursor + 32)
        local cx = readF32LE(data, cursor + 36)
        local cy = readF32LE(data, cursor + 40)
        local cz = readF32LE(data, cursor + 44)

        if not (nx and ny and nz and ax and ay and az and bx and by and bz and cx and cy and cz) then
            return nil, "invalid triangle data in binary STL"
        end

        local base = #vertices
        vertices[base + 1] = { ax, ay, az }
        vertices[base + 2] = { bx, by, bz }
        vertices[base + 3] = { cx, cy, cz }
        local i1, i2, i3 = base + 1, base + 2, base + 3
        if shouldFlipFaceByFacetNormal(vertices[i1], vertices[i2], vertices[i3], nx, ny, nz) then
            i2, i3 = i3, i2
        end
        faces[#faces + 1] = { i1, i2, i3 }

        cursor = cursor + 50
    end

    return {
        vertices = vertices,
        faces = faces,
        isSolid = true
    }
end

local function parseStlRaw(raw)
    local triCount = (#raw >= 84) and readU32LE(raw, 81) or nil
    local expectedBytes = triCount and (84 + triCount * 50) or nil

    if expectedBytes and expectedBytes == #raw then
        local model, parseErr = parseBinarySTL(raw)
        if model then
            return model
        end
        return nil, parseErr
    end

    local model, parseErr = parseAsciiSTL(raw)
    if model then
        return model
    end

    model, parseErr = parseBinarySTL(raw)
    if model then
        return model
    end

    return nil, parseErr
end

-- Loads either ASCII or binary STL from raw bytes.
-- Returns model on success, nil + error message on failure.
function engine.loadSTLData(raw)
    if type(raw) ~= "string" or raw == "" then
        return nil, "invalid STL payload"
    end
    return parseStlRaw(raw)
end

-- Loads either ASCII or binary STL from a virtual or absolute file path.
-- Returns model on success, nil + error message on failure.
function engine.loadSTL(path)
    if type(path) ~= "string" or path == "" then
        return nil, "missing STL path"
    end

    local raw, readErr = love.filesystem.read(path)
    if not raw then
        local handle = io.open(path, "rb")
        if handle then
            raw = handle:read("*a")
            handle:close()
        end
    end
    if not raw then
        return nil, "failed to read STL: " .. tostring(readErr)
    end

    return parseStlRaw(raw)
end

function engine.normalizeModel(model, targetExtent)
    if not model or not model.vertices or #model.vertices == 0 then
        return model
    end

    local minX, minY, minZ = math.huge, math.huge, math.huge
    local maxX, maxY, maxZ = -math.huge, -math.huge, -math.huge

    for _, v in ipairs(model.vertices) do
        minX = math.min(minX, v[1])
        minY = math.min(minY, v[2])
        minZ = math.min(minZ, v[3])
        maxX = math.max(maxX, v[1])
        maxY = math.max(maxY, v[2])
        maxZ = math.max(maxZ, v[3])
    end

    local spanX = maxX - minX
    local spanY = maxY - minY
    local spanZ = maxZ - minZ
    local largestSpan = math.max(spanX, spanY, spanZ)
    if largestSpan <= 0 then
        largestSpan = 1
    end

    local halfTarget = targetExtent and (targetExtent * 0.5) or 1.0
    local scale = halfTarget / (largestSpan * 0.5)
    local centerX = (minX + maxX) * 0.5
    local centerY = (minY + maxY) * 0.5
    local centerZ = (minZ + maxZ) * 0.5

    local out = {
        vertices = {},
        faces = {},
        isSolid = model.isSolid ~= false
    }

    for i, v in ipairs(model.vertices) do
        out.vertices[i] = {
            (v[1] - centerX) * scale,
            (v[2] - centerY) * scale,
            (v[3] - centerZ) * scale
        }
    end

    for i, face in ipairs(model.faces or {}) do
        local copy = {}
        for j, idx in ipairs(face) do
            copy[j] = idx
        end
        out.faces[i] = copy
    end

    return out
end

--[[
This function does not do dt detection for proper rebound calculations!
Only use this if you need to know if theyre colliding right now

objectA, objectB → true on hit, false on miss
]]
function engine.checkCollision(objA, objB)
    if type(objA) ~= "table" or type(objB) ~= "table" then
        return false
    end
    if not objA.isSolid or not objB.isSolid then
        return false
    end

    local posA = objA.pos
    local posB = objB.pos
    if type(posA) ~= "table" or type(posB) ~= "table" then
        return false
    end

    local function readHalfSize(obj)
        if type(obj.halfSize) == "table" then
            local x = math.abs(tonumber(obj.halfSize.x or obj.halfSize[1]) or 0)
            local y = math.abs(tonumber(obj.halfSize.y or obj.halfSize[2]) or 0)
            local z = math.abs(tonumber(obj.halfSize.z or obj.halfSize[3]) or 0)
            if x > 0 or y > 0 or z > 0 then
                return { x = x, y = y, z = z }
            end
        end

        local legacyRadius = tonumber(obj.radius)
        if legacyRadius and legacyRadius > 0 then
            local r = math.abs(legacyRadius)
            return { x = r, y = r, z = r }
        end
        return nil
    end

    local hsA = readHalfSize(objA)
    local hsB = readHalfSize(objB)
    if not hsA or not hsB then
        return false
    end

    local dx = math.abs((tonumber(posA[1]) or 0) - (tonumber(posB[1]) or 0))
    local dy = math.abs((tonumber(posA[2]) or 0) - (tonumber(posB[2]) or 0))
    local dz = math.abs((tonumber(posA[3]) or 0) - (tonumber(posB[3]) or 0))

    return dx <= (hsA.x + hsB.x) and
        dy <= (hsA.y + hsB.y) and
        dz <= (hsA.z + hsB.z)
end

function engine.getCameraBasis(camera, q, vector3)
    local forward = q.rotateVector(camera.rot, { 0, 0, 1 })
    local right = q.rotateVector(camera.rot, { 1, 0, 0 })
    local up = q.rotateVector(camera.rot, { 0, 1, 0 })
    return vector3.normalizeVec(forward), vector3.normalizeVec(right), vector3.normalizeVec(up)
end

function engine.transformVertex(v, obj, camera, q)
    local scale = obj.scale
    local sx = (scale and scale[1]) or 1
    local sy = (scale and scale[2]) or 1
    local sz = (scale and scale[3]) or 1
    local scaled = { v[1] * sx, v[2] * sy, v[3] * sz }

    local objRot = obj.rot or q.identity()
    local rotated = q.rotateVector(objRot, scaled)
    local objPos = obj.pos or { 0, 0, 0 }
    local world = { rotated[1] + objPos[1], rotated[2] + objPos[2], rotated[3] + objPos[3] }

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
    local aspect = math.max(1e-6, (screen.w or 1) / math.max(1, (screen.h or 1)))
    local px = x * (f / aspect) / z
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
function engine.processMovement(camera, dt, flightSimMode, vector3, q, objects, inputState, flightEnvironment)
    inputState = inputState or {}

    local function axis(positive, negative)
        local value = 0
        if positive then
            value = value + 1
        end
        if negative then
            value = value - 1
        end
        return value
    end

    local speed = camera.speed * dt
    local forward, right, up = engine.getCameraBasis(camera, q, vector3)

    if flightSimMode then
        -- Flight simulation is owned by Source.Systems.FlightDynamicsSystem.
        return camera
    end

    -- === Ground movement ===
    local moveDir = { 0, 0, 0 }
    local groundForward = vector3.normalizeVec({ forward[1], 0, forward[3] })
    local groundRight = vector3.normalizeVec({ right[1], 0, right[3] })
    if inputState.walkForward then
        moveDir = vector3.add(moveDir, groundForward)
    end
    if inputState.walkBackward then
        moveDir = vector3.sub(moveDir, groundForward)
    end
    if inputState.walkStrafeRight then
        moveDir = vector3.add(moveDir, groundRight)
    end
    if inputState.walkStrafeLeft then
        moveDir = vector3.sub(moveDir, groundRight)
    end
    moveDir = vector3.normalizeVec(moveDir)

    local sprintMultiplier = inputState.walkSprint and (camera.sprintMultiplier or 1.6) or 1
    local walkSpeed = speed * sprintMultiplier
    camera.pos[1] = camera.pos[1] + moveDir[1] * walkSpeed
    camera.pos[3] = camera.pos[3] + moveDir[3] * walkSpeed

    local tiltAxis = axis(inputState.walkTiltLeft, inputState.walkTiltRight)
    if tiltAxis ~= 0 and camera.allowWalkRoll then
        local tiltRate = math.rad(camera.walkTiltRate or 35)
        local tiltQuat = q.fromAxisAngle(forward, tiltAxis * tiltRate * dt)
        camera.rot = q.normalize(q.multiply(tiltQuat, camera.rot))
    end

    -- Gravity
    camera.vel[2] = camera.vel[2] + camera.gravity * dt
    camera.pos[2] = camera.pos[2] + camera.vel[2] * dt

    -- Update camera box
    camera.box.pos = { camera.pos[1], camera.pos[2], camera.pos[3] }

    -- Collision with ground tiles or procedural ground callback.
    local highestY = -math.huge
    if flightEnvironment and type(flightEnvironment.groundHeightAt) == "function" then
        highestY = flightEnvironment.groundHeightAt(camera.box.pos[1], camera.box.pos[3]) or highestY
    end
    if highestY == -math.huge then
        for _, obj in ipairs(objects) do
            if obj.isSolid and obj.halfSize then
                local dx = math.abs(camera.box.pos[1] - obj.pos[1])
                local dz = math.abs(camera.box.pos[3] - obj.pos[3])
                if dx <= obj.halfSize.x + camera.box.halfSize.x and dz <= obj.halfSize.z + camera.box.halfSize.z then
                    local topY = obj.pos[2] + obj.halfSize.y
                    if topY > highestY then
                        highestY = topY
                    end
                end
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

    if inputState.walkJump and camera.onGround then
        camera.vel[2] = camera.jumpSpeed
        camera.onGround = false
    end

    return camera
end

function engine.drawTriangle(v1, v2, v3, color, zBuffer, screen, imageData, writeDepth)
    color = color or { 0.5, 0.5, 0.5 }
    local alpha = color[4] or 1
    local minX = math.max(1, math.floor(math.min(v1[1], v2[1], v3[1])))
    local maxX = math.min(screen.w, math.ceil(math.max(v1[1], v2[1], v3[1])))
    local minY = math.max(1, math.floor(math.min(v1[2], v2[2], v3[2])))
    local maxY = math.min(screen.h, math.ceil(math.max(v1[2], v2[2], v3[2])))
    if minX > maxX or minY > maxY then return imageData end

    local function edge(ax, ay, bx, by, cx, cy)
        return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax)
    end

    local area = edge(v1[1], v1[2], v2[1], v2[2], v3[1], v3[2])
    if area == 0 then return imageData end

    for x = minX, maxX do
        for y = minY, maxY do
            local w0 = edge(v2[1], v2[2], v3[1], v3[2], x, y)
            local w1 = edge(v3[1], v3[2], v1[1], v1[2], x, y)
            local w2 = edge(v1[1], v1[2], v2[1], v2[2], x, y)

            if (area > 0 and w0 >= 0 and w1 >= 0 and w2 >= 0) or
                (area < 0 and w0 <= 0 and w1 <= 0 and w2 <= 0) then
                w0 = w0 / area
                w1 = w1 / area
                w2 = w2 / area

                local depth =
                    v1[3] * w0 +
                    v2[3] * w1 +
                    v3[3] * w2

                local index = (y - 1) * screen.w + x

                if depth < (zBuffer[index] or math.huge) then
                    if writeDepth ~= false then
                        zBuffer[index] = depth
                    end

                    if alpha >= 0.999 then
                        imageData:setPixel(x - 1, y - 1, color[1], color[2], color[3], 1)
                    else
                        local r, g, b, a = imageData:getPixel(x - 1, y - 1)
                        local invAlpha = 1 - alpha
                        local outA = alpha + a * invAlpha
                        imageData:setPixel(
                            x - 1,
                            y - 1,
                            color[1] * alpha + r * invAlpha,
                            color[2] * alpha + g * invAlpha,
                            color[3] * alpha + b * invAlpha,
                            outA
                        )
                    end
                end
            end
        end
    end
    return imageData
end

local function clamp01(v)
    if v < 0 then
        return 0
    end
    if v > 1 then
        return 1
    end
    return v
end

local function sampleTextureAtUv(images, texRef, uv)
    if type(images) ~= "table" or type(texRef) ~= "table" then
        return 1, 1, 1, 1
    end
    local imageIndex = tonumber(texRef.imageIndex)
    if not imageIndex then
        return 1, 1, 1, 1
    end
    local imageRecord = images[math.max(1, math.floor(imageIndex))]
    local imageData = imageRecord and imageRecord.imageData
    if not imageData then
        return 1, 1, 1, 1
    end

    local w, h = imageData:getWidth(), imageData:getHeight()
    if w <= 0 or h <= 0 then
        return 1, 1, 1, 1
    end

    local u = tonumber(uv and uv[1]) or 0.5
    local v = tonumber(uv and uv[2]) or 0.5
    u = u - math.floor(u)
    v = v - math.floor(v)

    local x = math.max(0, math.min(w - 1, math.floor(u * (w - 1) + 0.5)))
    local y = math.max(0, math.min(h - 1, math.floor((1 - v) * (h - 1) + 0.5)))
    return imageData:getPixel(x, y)
end

local function sampleMaterialColorForFace(obj, face, faceIndex)
    local baseColor = obj.color or { 0.5, 0.5, 0.5, 1.0 }
    local materials = obj.materials
    if type(materials) ~= "table" or #materials == 0 then
        return {
            baseColor[1] or 0.5,
            baseColor[2] or 0.5,
            baseColor[3] or 0.5,
            baseColor[4] or 1.0
        }
    end

    local faceMaterials = obj.faceMaterials or (obj.model and obj.model.faceMaterials)
    local materialIndex = tonumber(faceMaterials and faceMaterials[faceIndex]) or 1
    materialIndex = math.max(1, math.floor(materialIndex))
    local material = materials[materialIndex] or materials[1]
    if type(material) ~= "table" then
        return {
            baseColor[1] or 0.5,
            baseColor[2] or 0.5,
            baseColor[3] or 0.5,
            baseColor[4] or 1.0
        }
    end

    local factor = material.baseColorFactor or { 1, 1, 1, 1 }
    local uv = nil
    if obj.model and type(obj.model.vertexUVs) == "table" and type(face) == "table" and #face >= 3 then
        local uvA = obj.model.vertexUVs[face[1]]
        local uvB = obj.model.vertexUVs[face[2]]
        local uvC = obj.model.vertexUVs[face[3]]
        if uvA and uvB and uvC then
            uv = {
                ((uvA[1] or 0) + (uvB[1] or 0) + (uvC[1] or 0)) / 3,
                ((uvA[2] or 0) + (uvB[2] or 0) + (uvC[2] or 0)) / 3
            }
        end
    end

    local tr, tg, tb, ta = sampleTextureAtUv(obj.images, material.baseColorTexture, uv)
    local r = clamp01((factor[1] or 1) * tr * (baseColor[1] or 1))
    local g = clamp01((factor[2] or 1) * tg * (baseColor[2] or 1))
    local b = clamp01((factor[3] or 1) * tb * (baseColor[3] or 1))
    local a = clamp01((factor[4] or 1) * ta * (baseColor[4] or 1))

    local emissive = material.emissiveFactor
    if type(emissive) == "table" then
        r = clamp01(r + (emissive[1] or 0))
        g = clamp01(g + (emissive[2] or 0))
        b = clamp01(b + (emissive[3] or 0))
    end

    if material.alphaMode == "MASK" then
        if a < (tonumber(material.alphaCutoff) or 0.5) then
            return nil
        end
        a = 1
    elseif material.alphaMode ~= "BLEND" then
        a = 1
    end

    return { r, g, b, a }
end

local function pointInsidePortal(worldPos, portal)
    if type(worldPos) ~= "table" or type(portal) ~= "table" or not portal.enabled then
        return false
    end
    local origin = portal.origin
    local dir = portal.dir
    if type(origin) ~= "table" or type(dir) ~= "table" then
        return false
    end

    local dx = (worldPos[1] or 0) - (origin[1] or 0)
    local dy = (worldPos[2] or 0) - (origin[2] or 0)
    local dz = (worldPos[3] or 0) - (origin[3] or 0)
    local dirX, dirY, dirZ = dir[1] or 0, dir[2] or 0, dir[3] or 1
    local dirLen = math.sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ)
    if dirLen <= 1e-6 then
        dirX, dirY, dirZ, dirLen = 0, 0, 1, 1
    end
    dirX, dirY, dirZ = dirX / dirLen, dirY / dirLen, dirZ / dirLen

    local t = dx * dirX + dy * dirY + dz * dirZ
    local startDist = math.max(0, tonumber(portal.startDist) or 0)
    local endDist = math.max(startDist + 1e-4, tonumber(portal.endDist) or (startDist + 1))
    if t < startDist or t > endDist then
        return false
    end

    local cx = (origin[1] or 0) + dirX * t
    local cy = (origin[2] or 0) + dirY * t
    local cz = (origin[3] or 0) + dirZ * t
    local rx = (worldPos[1] or 0) - cx
    local ry = (worldPos[2] or 0) - cy
    local rz = (worldPos[3] or 0) - cz
    local radial = math.sqrt(rx * rx + ry * ry + rz * rz)

    local k = (t - startDist) / math.max(1e-4, endDist - startDist)
    if k < 0 then
        k = 0
    elseif k > 1 then
        k = 1
    end
    local radiusNear = math.max(0, tonumber(portal.radiusNear) or 0)
    local radiusFar = math.max(0, tonumber(portal.radiusFar) or radiusNear)
    local radius = radiusNear + (radiusFar - radiusNear) * k
    return radial <= radius
end

function engine.drawObject(obj, cullBackfaces, camera, vector3, q, screen, zBuffer, imageData, writeDepth)
    if not obj or not obj.model or not obj.model.vertices or not obj.model.faces then
        return imageData
    end
    local useBackfaceCulling = (cullBackfaces ~= false)

    local transformedVerts = {}
    local worldVerts = {}

    -- Transform vertices into camera space
    for i, v in ipairs(obj.model.vertices) do
        local scale = obj.scale
        local sx = (scale and scale[1]) or 1
        local sy = (scale and scale[2]) or 1
        local sz = (scale and scale[3]) or 1
        local scaled = { v[1] * sx, v[2] * sy, v[3] * sz }
        local objRot = obj.rot or q.identity()
        local rotated = q.rotateVector(objRot, scaled)
        local objPos = obj.pos or { 0, 0, 0 }
        local world = { rotated[1] + objPos[1], rotated[2] + objPos[2], rotated[3] + objPos[3] }
        worldVerts[i] = world

        local rel = { world[1] - camera.pos[1], world[2] - camera.pos[2], world[3] - camera.pos[3] }
        local camConj = (q.conjugate and q.conjugate(camera.rot)) or {
            w = camera.rot.w,
            x = -camera.rot.x,
            y = -camera.rot.y,
            z = -camera.rot.z
        }
        local camSpace = q.rotateVector(camConj, rel)
        local x, y, z = camSpace[1], camSpace[2], camSpace[3]
        transformedVerts[i] = { x, y, z }
    end

    -- Triangulate faces and raster using Z-buffer
    for faceIndex, face in ipairs(obj.model.faces) do
        local poly = {}
        local faceValid = true

        for _, vi in ipairs(face) do
            local vertex = transformedVerts[vi]
            if not vertex then
                faceValid = false
                break
            end
            table.insert(poly, vertex)
        end
        if not faceValid then goto continue end

        if obj.firstPersonPortal and obj.firstPersonPortal.enabled and #face >= 3 then
            local wcx, wcy, wcz = 0, 0, 0
            local count = 0
            for _, vi in ipairs(face) do
                local wv = worldVerts[vi]
                if wv then
                    wcx = wcx + (wv[1] or 0)
                    wcy = wcy + (wv[2] or 0)
                    wcz = wcz + (wv[3] or 0)
                    count = count + 1
                end
            end
            if count > 0 then
                local center = { wcx / count, wcy / count, wcz / count }
                if pointInsidePortal(center, obj.firstPersonPortal) then
                    goto continue
                end
            end
        end

        poly = engine.clipPolygonToNearPlane(poly, 0.0001)

        if #poly < 3 then
            goto continue
        end

        if useBackfaceCulling then
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

        local triColor = sampleMaterialColorForFace(obj, face, faceIndex)
        if not triColor then
            goto continue
        end
        if obj.model.faceColors and obj.model.faceColors[faceIndex] then
            local c = obj.model.faceColors[faceIndex]
            triColor = {
                c[1] or triColor[1],
                c[2] or triColor[2],
                c[3] or triColor[3],
                c[4] or triColor[4] or 1.0
            }
        end

        for i = 2, #projected - 1 do
            imageData = engine.drawTriangle(
                projected[1],
                projected[i],
                projected[i + 1],
                triColor,
                zBuffer,
                screen,
                imageData,
                writeDepth
            )
        end

        ::continue::
    end
    return imageData
end

function engine.perspectiveMatrix(fov, aspect, near, far)
    local f = 1 / math.tan(fov / 2)
    return {
        { f / aspect, 0, 0,                               0 },
        { 0,          f, 0,                               0 },
        { 0,          0, (far + near) / (near - far),     -1 },
        { 0,          0, (2 * far * near) / (near - far), 0 }
    }
end

function engine.mat4Multiply(a, b)
    local r = {}
    for i = 1, 4 do
        r[i] = {}
        for j = 1, 4 do
            r[i][j] = 0
            for k = 1, 4 do
                r[i][j] = r[i][j] + a[i][k] * b[k][j]
            end
        end
    end
    return r
end

function engine.drawObjectGPU(obj, camera, q, vector3, screen)
    if not obj.mesh then
        return
    end
    local shaderOk, shaderErr = engine.ensureDefaultShader()
    if not shaderOk then
        return nil, shaderErr
    end
    local function flattenMat4(m)
        return {
            m[1][1], m[1][2], m[1][3], m[1][4],
            m[2][1], m[2][2], m[2][3], m[2][4],
            m[3][1], m[3][2], m[3][3], m[3][4],
            m[4][1], m[4][2], m[4][3], m[4][4]
        }
    end

    local aspect = screen.w / screen.h
    local proj = engine.perspectiveMatrix(camera.fov, aspect, 20, 1000)

    local function mat4LookAt(pos, rot)
        local f = q.rotateVector(rot, { 0, 0, -1 })
        local r = q.rotateVector(rot, { 1, 0, 0 })
        local u = q.rotateVector(rot, { 0, 1, 0 })
        return {
            { r[1], u[1], -f[1], 0 },
            { r[2], u[2], -f[2], 0 },
            { r[3], u[3], -f[3], 0 },
            { -(r[1] * pos[1] + r[2] * pos[2] + r[3] * pos[3]),
                -(u[1] * pos[1] + u[2] * pos[2] + u[3] * pos[3]),
                f[1] * pos[1] + f[2] * pos[2] + f[3] * pos[3], 1 },
        }
    end

    local view = mat4LookAt(camera.pos, camera.rot)
    local mvp = flattenMat4(engine.mat4Multiply(proj, view))

    love.graphics.setShader(engine.defaultShader)
    engine.defaultShader:send("modelViewProjection", mvp)
    love.graphics.draw(obj.mesh)
    love.graphics.setShader()
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
