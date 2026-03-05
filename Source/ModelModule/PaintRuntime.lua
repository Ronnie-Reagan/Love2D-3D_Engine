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

local DEFAULT_WIDTH = 1024
local DEFAULT_HEIGHT = 1024
local DEFAULT_MAX_HISTORY = 8
local DEFAULT_HISTORY_BUDGET_BYTES = 128 * 1024 * 1024

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

local function notifySession(session, message)
    if type(session) ~= "table" or type(message) ~= "string" or message == "" then
        return
    end
    session.lastError = message
    if type(session.onWarning) == "function" then
        pcall(session.onWarning, message)
    else
        print("[paint] " .. message)
    end
end

local function snapshotBytesForSession(session)
    if not session or not session.overlay then
        return 0
    end
    local w = math.max(1, math.floor(tonumber(session.overlay.width) or 1))
    local h = math.max(1, math.floor(tonumber(session.overlay.height) or 1))
    return w * h * 4
end

local function removeOldestEntry(stack, bytesField, session)
    local entry = table.remove(stack, 1)
    if not entry then
        return
    end
    session[bytesField] = math.max(0, (session[bytesField] or 0) - (tonumber(entry.bytes) or 0))
end

local function pushEntryWithBudget(session, stackName, bytesField, imageData, entryBytes)
    if type(session) ~= "table" then
        return false, "paint session not found"
    end
    if not imageData then
        return false, "snapshot unavailable"
    end

    local stack = session[stackName]
    if type(stack) ~= "table" then
        return false, "invalid paint history stack"
    end

    local bytes = math.max(0, math.floor(tonumber(entryBytes) or 0))
    if bytes <= 0 then
        bytes = snapshotBytesForSession(session)
    end

    local budget = math.max(1024 * 1024, math.floor(tonumber(session.historyBudgetBytes) or DEFAULT_HISTORY_BUDGET_BYTES))
    if bytes > budget then
        return false, string.format("paint snapshot (%d bytes) exceeds budget (%d bytes)", bytes, budget)
    end

    local maxHistory = math.max(1, math.floor(tonumber(session.maxHistory) or DEFAULT_MAX_HISTORY))
    while #stack >= maxHistory do
        removeOldestEntry(stack, bytesField, session)
    end

    local function historyBytes()
        return (session.undoBytes or 0) + (session.redoBytes or 0)
    end

    while (historyBytes() + bytes) > budget do
        if #session.undo > 0 then
            removeOldestEntry(session.undo, "undoBytes", session)
        elseif #session.redo > 0 then
            removeOldestEntry(session.redo, "redoBytes", session)
        else
            break
        end
    end

    if (historyBytes() + bytes) > budget then
        return false, string.format("paint history budget exceeded (%d / %d bytes)", historyBytes() + bytes, budget)
    end

    stack[#stack + 1] = {
        imageData = imageData,
        bytes = bytes
    }
    session[bytesField] = (session[bytesField] or 0) + bytes
    return true
end

local function clearRedoHistory(session)
    session.redo = {}
    session.redoBytes = 0
end

local function snapshotPush(session)
    local bytes = snapshotBytesForSession(session)
    local budget = math.max(1024 * 1024, math.floor(tonumber(session.historyBudgetBytes) or DEFAULT_HISTORY_BUDGET_BYTES))
    if bytes > budget then
        local reason = string.format("paint snapshot (%d bytes) exceeds session budget (%d bytes)", bytes, budget)
        notifySession(session, reason)
        return false, reason
    end

    local clone = cloneImageData(session.overlay.imageData)
    if not clone then
        local reason = "failed to create paint snapshot clone"
        notifySession(session, reason)
        return false, reason
    end

    local ok, err = pushEntryWithBudget(session, "undo", "undoBytes", clone, bytes)
    if not ok then
        notifySession(session, err)
        return false, err
    end

    clearRedoHistory(session)
    return true
end

function paintRuntime.beginSession(assetId, role, ownerId, modelHash, opts, existingOverlay)
    opts = opts or {}
    local width = math.floor(tonumber(opts.width) or DEFAULT_WIDTH)
    local height = math.floor(tonumber(opts.height) or DEFAULT_HEIGHT)
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
        undoBytes = 0,
        redoBytes = 0,
        maxHistory = math.max(1, math.floor(tonumber(opts.maxHistory) or DEFAULT_MAX_HISTORY)),
        historyBudgetBytes = math.max(
            1024 * 1024,
            math.floor(tonumber(opts.maxUndoBytes or opts.historyBudgetBytes) or DEFAULT_HISTORY_BUDGET_BYTES)
        ),
        onWarning = opts.onWarning,
        lastError = nil
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

    local okSnapshot, snapshotErr = snapshotPush(session)
    if not okSnapshot then
        return false, snapshotErr
    end

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
                local falloff = t ^ (1 / hardness)
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

    local okSnapshot, snapshotErr = snapshotPush(session)
    if not okSnapshot then
        return false, snapshotErr
    end

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
    if not current then
        return false, "failed to clone current paint state"
    end

    local prev = table.remove(session.undo)
    session.undoBytes = math.max(0, (session.undoBytes or 0) - (tonumber(prev.bytes) or 0))

    local okPush, pushErr = pushEntryWithBudget(
        session,
        "redo",
        "redoBytes",
        current,
        snapshotBytesForSession(session)
    )
    if not okPush then
        session.undo[#session.undo + 1] = prev
        session.undoBytes = (session.undoBytes or 0) + (tonumber(prev.bytes) or 0)
        notifySession(session, pushErr)
        return false, pushErr
    end

    session.overlay.imageData = prev.imageData
    if session.overlay.image and session.overlay.image.replacePixels then
        session.overlay.image:replacePixels(prev.imageData)
    end
    return true
end

function paintRuntime.paintRedo(sessionId)
    local session = sessions[sessionId]
    if not session or #session.redo == 0 then
        return false, "nothing to redo"
    end

    local current = cloneImageData(session.overlay.imageData)
    if not current then
        return false, "failed to clone current paint state"
    end

    local okPush, pushErr = pushEntryWithBudget(
        session,
        "undo",
        "undoBytes",
        current,
        snapshotBytesForSession(session)
    )
    if not okPush then
        notifySession(session, pushErr)
        return false, pushErr
    end

    local nextState = table.remove(session.redo)
    session.redoBytes = math.max(0, (session.redoBytes or 0) - (tonumber(nextState.bytes) or 0))
    session.overlay.imageData = nextState.imageData
    if session.overlay.image and session.overlay.image.replacePixels then
        session.overlay.image:replacePixels(nextState.imageData)
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
