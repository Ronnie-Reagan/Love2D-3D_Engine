local paintRuntime = {}

local function maybeRequireLove()
    if type(love) == "table" then
        return love
    end
    local ok, mod = pcall(require, "love")
    if ok then
        return mod
    end
    return nil
end

local loveLib = maybeRequireLove()

local function clamp(v, minValue, maxValue)
    if v < minValue then
        return minValue
    end
    if v > maxValue then
        return maxValue
    end
    return v
end

local function hashBytes(raw)
    local function bytesToHex(bytes)
        local out = {}
        for i = 1, #bytes do
            out[i] = string.format("%02x", bytes:byte(i))
        end
        return table.concat(out)
    end

    if loveLib and loveLib.data and loveLib.data.hash then
        local ok, digest = pcall(function()
            return loveLib.data.hash("string", "sha256", raw)
        end)
        if ok and type(digest) == "string" and digest ~= "" then
            return bytesToHex(digest)
        end
    end
    local acc = 2166136261
    local bitOps = _G.bit or _G.bit32
    if not bitOps then
        return string.format("paint-len-%08x", #raw)
    end
    for i = 1, #raw do
        acc = bitOps.bxor(acc, raw:byte(i))
        acc = (acc * 16777619) % 4294967296
    end
    if acc < 0 then
        acc = acc + 4294967296
    end
    return string.format("paint-%08x", acc)
end

local function cloneImageData(imageData)
    if not imageData then
        return nil
    end
    local ok, cloned = pcall(function()
        return imageData:clone()
    end)
    if ok then
        return cloned
    end
    return nil
end

local function imageDataToPngBytes(imageData)
    if not imageData then
        return nil
    end
    local ok, encoded = pcall(function()
        return imageData:encode("png")
    end)
    if not ok or not encoded then
        return nil
    end
    if type(encoded) == "string" then
        return encoded
    end
    if encoded.getString then
        return encoded:getString()
    end
    return nil
end

local function createOverlay(width, height, ownerId, role, modelHash)
    if not (loveLib and loveLib.image and loveLib.graphics) then
        return nil, "paint overlay requires LOVE image API"
    end
    local imageData = loveLib.image.newImageData(width, height)
    imageData:mapPixel(function()
        return 0, 0, 0, 0
    end)
    local image = loveLib.graphics.newImage(imageData)
    image:setFilter("linear", "linear")
    return {
        width = width,
        height = height,
        ownerId = ownerId,
        role = role,
        modelHash = modelHash,
        imageData = imageData,
        image = image,
        encodedPng = nil,
        hash = nil
    }
end

local sessions = {}
local sessionCounter = 0

local function snapshotPush(session)
    local clone = cloneImageData(session.overlay.imageData)
    if not clone then
        return
    end
    session.undo[#session.undo + 1] = clone
    if #session.undo > (session.maxHistory or 32) then
        table.remove(session.undo, 1)
    end
    session.redo = {}
end

function paintRuntime.beginSession(assetId, role, ownerId, modelHash, opts, existingOverlay)
    opts = opts or {}
    local width = math.floor(tonumber(opts.width) or 2048)
    local height = math.floor(tonumber(opts.height) or 2048)
    width = clamp(width, 256, 4096)
    height = clamp(height, 256, 4096)

    local overlay = existingOverlay
    if not overlay then
        local created, err = createOverlay(width, height, ownerId, role, modelHash)
        if not created then
            return nil, err
        end
        overlay = created
    end

    sessionCounter = sessionCounter + 1
    local sessionId = "paint-session-" .. tostring(sessionCounter)
    sessions[sessionId] = {
        id = sessionId,
        assetId = assetId,
        role = role,
        ownerId = ownerId,
        modelHash = modelHash,
        overlay = overlay,
        undo = {},
        redo = {},
        maxHistory = math.max(4, math.floor(tonumber(opts.maxHistory) or 32))
    }
    return sessionId
end

local function blendPixel(imageData, x, y, color, alpha)
    local r, g, b, a = imageData:getPixel(x, y)
    local inv = 1 - alpha
    imageData:setPixel(
        x,
        y,
        (color[1] * alpha) + (r * inv),
        (color[2] * alpha) + (g * inv),
        (color[3] * alpha) + (b * inv),
        clamp(alpha + (a * inv), 0, 1)
    )
end

function paintRuntime.paintStroke(sessionId, strokeCmd)
    local session = sessions[sessionId]
    if not session then
        return false, "paint session not found"
    end
    local overlay = session.overlay
    local imageData = overlay.imageData
    if not imageData then
        return false, "overlay image unavailable"
    end

    snapshotPush(session)

    local u = clamp(tonumber(strokeCmd.u) or 0.5, 0, 1)
    local v = clamp(tonumber(strokeCmd.v) or 0.5, 0, 1)
    local cx = math.floor(u * (overlay.width - 1) + 0.5)
    local cy = math.floor((1 - v) * (overlay.height - 1) + 0.5)
    local radius = clamp(tonumber(strokeCmd.size) or 16, 1, 512)
    local opacity = clamp(tonumber(strokeCmd.opacity) or 1, 0, 1)
    local hardness = clamp(tonumber(strokeCmd.hardness) or 0.75, 0.01, 1)
    local color = strokeCmd.color or { 1, 1, 1, 1 }
    local erase = strokeCmd.mode == "erase"

    local minX = math.max(0, math.floor(cx - radius))
    local maxX = math.min(overlay.width - 1, math.ceil(cx + radius))
    local minY = math.max(0, math.floor(cy - radius))
    local maxY = math.min(overlay.height - 1, math.ceil(cy + radius))

    for y = minY, maxY do
        for x = minX, maxX do
            local dx = x - cx
            local dy = y - cy
            local dist = math.sqrt(dx * dx + dy * dy)
            if dist <= radius then
                local t = 1 - (dist / radius)
                local falloff = math.pow(t, 1 / hardness)
                local alpha = opacity * falloff * (tonumber(color[4]) or 1)
                if erase then
                    local r, g, b, a = imageData:getPixel(x, y)
                    local nextA = clamp(a * (1 - alpha), 0, 1)
                    imageData:setPixel(x, y, r, g, b, nextA)
                else
                    blendPixel(imageData, x, y, color, alpha)
                end
            end
        end
    end

    if overlay.image and overlay.image.replacePixels then
        overlay.image:replacePixels(imageData)
    end
    return true
end

function paintRuntime.paintFill(sessionId, fillCmd)
    local session = sessions[sessionId]
    if not session then
        return false, "paint session not found"
    end
    local overlay = session.overlay
    local imageData = overlay.imageData
    if not imageData then
        return false, "overlay image unavailable"
    end

    snapshotPush(session)

    local color = fillCmd.color or { 1, 1, 1, 1 }
    local r = clamp(tonumber(color[1]) or 1, 0, 1)
    local g = clamp(tonumber(color[2]) or 1, 0, 1)
    local b = clamp(tonumber(color[3]) or 1, 0, 1)
    local a = clamp(tonumber(color[4]) or 1, 0, 1)
    imageData:mapPixel(function()
        return r, g, b, a
    end)

    if overlay.image and overlay.image.replacePixels then
        overlay.image:replacePixels(imageData)
    end
    return true
end

function paintRuntime.paintUndo(sessionId)
    local session = sessions[sessionId]
    if not session or #session.undo == 0 then
        return false, "nothing to undo"
    end
    local current = cloneImageData(session.overlay.imageData)
    if current then
        session.redo[#session.redo + 1] = current
    end
    local prev = table.remove(session.undo)
    session.overlay.imageData = prev
    if session.overlay.image and session.overlay.image.replacePixels then
        session.overlay.image:replacePixels(prev)
    end
    return true
end

function paintRuntime.paintRedo(sessionId)
    local session = sessions[sessionId]
    if not session or #session.redo == 0 then
        return false, "nothing to redo"
    end
    local current = cloneImageData(session.overlay.imageData)
    if current then
        session.undo[#session.undo + 1] = current
    end
    local nextState = table.remove(session.redo)
    session.overlay.imageData = nextState
    if session.overlay.image and session.overlay.image.replacePixels then
        session.overlay.image:replacePixels(nextState)
    end
    return true
end

function paintRuntime.paintCommit(sessionId)
    local session = sessions[sessionId]
    if not session then
        return nil, "paint session not found"
    end
    local overlay = session.overlay
    local png = imageDataToPngBytes(overlay.imageData)
    if not png then
        return nil, "failed to encode paint overlay"
    end
    overlay.encodedPng = png
    overlay.hash = hashBytes(png)
    return overlay.hash, overlay
end

function paintRuntime.getSession(sessionId)
    return sessions[sessionId]
end

function paintRuntime.endSession(sessionId)
    sessions[sessionId] = nil
end

return paintRuntime
