local renderer = {}
local love = require "love"
local q = require "quat"

local vertexFormat = {
    { "VertexPosition", "float", 3 },
    { "VertexColor", "float", 4 }
}

local shaderSource = [[
uniform vec3 uObjPos;
uniform vec3 uObjRight;
uniform vec3 uObjUp;
uniform vec3 uObjForward;
uniform vec3 uObjScale;
uniform vec3 uCamPos;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform vec3 uCamForward;
uniform vec3 uColor;
uniform float uAlpha;
uniform float uFov;
uniform float uAspect;
uniform float uNear;
uniform float uFar;

vec4 position(mat4 transform_projection, vec4 vertex_position)
{
    vec3 worldPos = uObjPos
        + (vertex_position.x * uObjScale.x) * uObjRight
        + (vertex_position.y * uObjScale.y) * uObjUp
        + (vertex_position.z * uObjScale.z) * uObjForward;

    vec3 rel = worldPos - uCamPos;
    vec3 cam = vec3(
        dot(rel, uCamRight),
        dot(rel, uCamUp),
        dot(rel, uCamForward)
    );

    float f = 1.0 / tan(uFov * 0.5);
    float a = (uFar + uNear) / (uFar - uNear);
    float b = (-2.0 * uFar * uNear) / (uFar - uNear);

    // Clip-space output with w = z for perspective division and proper depth testing.
    return vec4(
        cam.x * f / uAspect,
        cam.y * f,
        a * cam.z + b,
        cam.z
    );
}

vec4 effect(vec4 color, Image texture, vec2 texture_coords, vec2 screen_coords)
{
    return vec4(color.rgb * uColor, color.a * uAlpha);
}
]]

local meshCache = setmetatable({}, { __mode = "k" })

local state = {
    ready = false,
    depthSupported = false,
    shader = nil,
    aspect = 1,
    nearPlane = 0.1,
    farPlane = 5000.0,
    log = function(_) end,
    loggedFirstFrame = false
}

local function log(message)
    state.log("[gpu] " .. tostring(message))
end

local function cloneObjects(objects)
    local out = {}
    for i, obj in ipairs(objects) do
        out[i] = obj
    end
    return out
end

local function cameraSpaceDepth(worldPos, camera)
    if not worldPos or not camera or not camera.pos or not camera.rot then
        return -math.huge
    end

    local rel = {
        worldPos[1] - camera.pos[1],
        worldPos[2] - camera.pos[2],
        worldPos[3] - camera.pos[3]
    }
    local camConj = q.conjugate(camera.rot)
    local cam = q.rotateVector(camConj, rel)
    return cam[3]
end

local function isTransparent(obj)
    local color = obj and obj.color
    local alpha = color and color[4] or 1
    return alpha < 0.999
end

local function buildMeshForModel(model)
    if meshCache[model] ~= nil then
        return meshCache[model] or nil
    end

    if not model or not model.vertices or not model.faces then
        meshCache[model] = false
        return nil
    end

    local triangleVertices = {}

    for _, face in ipairs(model.faces) do
        if #face >= 3 then
            for i = 2, #face - 1 do
                local ia, ib, ic = face[1], face[i], face[i + 1]
                local a, b, c = model.vertices[ia], model.vertices[ib], model.vertices[ic]
                local ca = model.vertexColors and model.vertexColors[ia]
                local cb = model.vertexColors and model.vertexColors[ib]
                local cc = model.vertexColors and model.vertexColors[ic]

                if a and b and c then
                    triangleVertices[#triangleVertices + 1] = {
                        a[1], a[2], a[3],
                        (ca and ca[1]) or 1, (ca and ca[2]) or 1, (ca and ca[3]) or 1, (ca and ca[4]) or 1
                    }
                    triangleVertices[#triangleVertices + 1] = {
                        b[1], b[2], b[3],
                        (cb and cb[1]) or 1, (cb and cb[2]) or 1, (cb and cb[3]) or 1, (cb and cb[4]) or 1
                    }
                    triangleVertices[#triangleVertices + 1] = {
                        c[1], c[2], c[3],
                        (cc and cc[1]) or 1, (cc and cc[2]) or 1, (cc and cc[3]) or 1, (cc and cc[4]) or 1
                    }
                end
            end
        end
    end

    if #triangleVertices == 0 then
        meshCache[model] = false
        return nil
    end

    local ok, meshOrErr = pcall(love.graphics.newMesh, vertexFormat, triangleVertices, "triangles", "static")
    if not ok then
        log("mesh creation failed: " .. tostring(meshOrErr))
        meshCache[model] = false
        return nil
    end

    meshCache[model] = meshOrErr
    return meshOrErr
end

function renderer.isReady()
    return state.ready
end

function renderer.init(screen, camera, logFn)
    state.log = logFn or state.log
    state.aspect = (screen and screen.h and screen.h ~= 0) and (screen.w / screen.h) or 1

    if camera and camera.fov then
        state.fov = camera.fov
    end

    local ok, shaderOrErr = pcall(love.graphics.newShader, shaderSource)
    if not ok then
        state.ready = false
        log("shader compile failed: " .. tostring(shaderOrErr))
        return false, shaderOrErr
    end

    state.shader = shaderOrErr

    local depthOk = pcall(function()
        love.graphics.setDepthMode("lequal", true)
        love.graphics.setDepthMode("always", false)
    end)
    state.depthSupported = depthOk
    state.loggedFirstFrame = false

    state.ready = true
    log("initialized; depth_supported=" .. tostring(state.depthSupported))
    return true
end

function renderer.resize(screen)
    if not screen or not screen.w or not screen.h or screen.h == 0 then
        return
    end
    state.aspect = screen.w / screen.h
end

function renderer.drawWorld(objects, camera, backgroundColor)
    if not state.ready or not state.shader then
        return false, "renderer not initialized"
    end

    local bg = backgroundColor or { 0.2, 0.2, 0.75, 1.0 }
    local bgA = bg[4] or 1.0

    if state.depthSupported then
        -- clear color (LOVE clears depth for the active target as well)
        love.graphics.clear(bg[1], bg[2], bg[3], bgA)
        love.graphics.setDepthMode("lequal", true)
    else
        love.graphics.clear(bg[1], bg[2], bg[3], bgA)
    end

    pcall(love.graphics.setMeshCullMode, "none")
    love.graphics.setShader(state.shader)

    local camPos = camera.pos or { 0, 0, 0 }
    local camRot = camera.rot or { w = 1, x = 0, y = 0, z = 0 }
    local camRight = q.rotateVector(camRot, { 1, 0, 0 })
    local camUp = q.rotateVector(camRot, { 0, 1, 0 })
    local camForward = q.rotateVector(camRot, { 0, 0, 1 })

    state.shader:send("uCamPos", camPos)
    state.shader:send("uCamRight", camRight)
    state.shader:send("uCamUp", camUp)
    state.shader:send("uCamForward", camForward)
    state.shader:send("uFov", camera.fov or state.fov or math.rad(90))
    state.shader:send("uAspect", state.aspect)
    state.shader:send("uNear", state.nearPlane)
    state.shader:send("uFar", state.farPlane)

    local function drawSingleObject(obj)
        local mesh = buildMeshForModel(obj.model)
        if not mesh then
            return 0
        end

        local rot = obj.rot or { w = 1, x = 0, y = 0, z = 0 }
        local scale = obj.scale or { 1, 1, 1 }
        local color = obj.color or { 0.5, 0.5, 0.5, 1.0 }
        local alpha = color[4] or 1.0
        local objRight = q.rotateVector(rot, { 1, 0, 0 })
        local objUp = q.rotateVector(rot, { 0, 1, 0 })
        local objForward = q.rotateVector(rot, { 0, 0, 1 })

        state.shader:send("uObjPos", obj.pos or { 0, 0, 0 })
        state.shader:send("uObjRight", objRight)
        state.shader:send("uObjUp", objUp)
        state.shader:send("uObjForward", objForward)
        state.shader:send("uObjScale", { scale[1] or 1, scale[2] or 1, scale[3] or 1 })
        state.shader:send("uColor", { color[1] or 0.5, color[2] or 0.5, color[3] or 0.5 })
        state.shader:send("uAlpha", alpha)

        love.graphics.draw(mesh)
        return mesh:getVertexCount() / 3
    end

    local triangleCount = 0

    if state.depthSupported then
        local opaqueObjects = {}
        local transparentObjects = {}

        for _, obj in ipairs(objects) do
            if isTransparent(obj) then
                transparentObjects[#transparentObjects + 1] = obj
            else
                opaqueObjects[#opaqueObjects + 1] = obj
            end
        end

        love.graphics.setDepthMode("lequal", true)
        for _, obj in ipairs(opaqueObjects) do
            triangleCount = triangleCount + drawSingleObject(obj)
        end

        if #transparentObjects > 0 then
            table.sort(transparentObjects, function(a, b)
                return cameraSpaceDepth(a and a.pos, camera) > cameraSpaceDepth(b and b.pos, camera)
            end)

            love.graphics.setDepthMode("lequal", false)
            for _, obj in ipairs(transparentObjects) do
                triangleCount = triangleCount + drawSingleObject(obj)
            end
            love.graphics.setDepthMode("lequal", true)
        end
    else
        local drawList = cloneObjects(objects)
        table.sort(drawList, function(a, b)
            return cameraSpaceDepth(a and a.pos, camera) > cameraSpaceDepth(b and b.pos, camera)
        end)

        for _, obj in ipairs(drawList) do
            triangleCount = triangleCount + drawSingleObject(obj)
        end
    end

    love.graphics.setShader()
    if state.depthSupported then
        love.graphics.setDepthMode("always", false)
    end

    if not state.loggedFirstFrame then
        log(string.format("first frame: objects=%d triangles=%d", #objects, triangleCount))
        state.loggedFirstFrame = true
    end

    return true, triangleCount
end

return renderer
