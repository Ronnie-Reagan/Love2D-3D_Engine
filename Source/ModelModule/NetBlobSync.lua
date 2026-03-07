local blobSync = {}

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

local BASE64_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
local BASE64_INDEX = {}
for i = 1, #BASE64_CHARS do
    BASE64_INDEX[BASE64_CHARS:sub(i, i)] = i - 1
end

local function encodeBase64(raw)
    if loveLib and loveLib.data and loveLib.data.encode then
        local ok, encoded = pcall(function()
            return loveLib.data.encode("string", "base64", raw)
        end)
        if ok and type(encoded) == "string" then
            return encoded
        end
    end

    local out = {}
    local i = 1
    while i <= #raw do
        local b1 = raw:byte(i) or 0
        local b2 = raw:byte(i + 1)
        local b3 = raw:byte(i + 2)

        local n = b1 * 65536 + (b2 or 0) * 256 + (b3 or 0)
        local c1 = math.floor(n / 262144) % 64
        local c2 = math.floor(n / 4096) % 64
        local c3 = math.floor(n / 64) % 64
        local c4 = n % 64

        out[#out + 1] = BASE64_CHARS:sub(c1 + 1, c1 + 1)
        out[#out + 1] = BASE64_CHARS:sub(c2 + 1, c2 + 1)
        out[#out + 1] = b2 and BASE64_CHARS:sub(c3 + 1, c3 + 1) or "="
        out[#out + 1] = b3 and BASE64_CHARS:sub(c4 + 1, c4 + 1) or "="
        i = i + 3
    end
    return table.concat(out)
end

local function decodeBase64(raw)
    if loveLib and loveLib.data and loveLib.data.decode then
        local ok, decoded = pcall(function()
            return loveLib.data.decode("string", "base64", raw)
        end)
        if ok and type(decoded) == "string" then
            return decoded
        end
    end

    raw = (raw or ""):gsub("%s+", "")
    local out = {}
    local i = 1
    while i <= #raw do
        local c1 = raw:sub(i, i)
        local c2 = raw:sub(i + 1, i + 1)
        local c3 = raw:sub(i + 2, i + 2)
        local c4 = raw:sub(i + 3, i + 3)
        if c1 == "" or c2 == "" then
            break
        end
        local v1 = BASE64_INDEX[c1]
        local v2 = BASE64_INDEX[c2]
        local v3 = (c3 == "=") and nil or BASE64_INDEX[c3]
        local v4 = (c4 == "=") and nil or BASE64_INDEX[c4]
        if v1 == nil or v2 == nil or (c3 ~= "=" and v3 == nil) or (c4 ~= "=" and v4 == nil) then
            return nil
        end
        local n = v1 * 262144 + v2 * 4096 + (v3 or 0) * 64 + (v4 or 0)
        local b1 = math.floor(n / 65536) % 256
        local b2 = math.floor(n / 256) % 256
        local b3 = n % 256
        out[#out + 1] = string.char(b1)
        if c3 ~= "=" then
            out[#out + 1] = string.char(b2)
        end
        if c4 ~= "=" then
            out[#out + 1] = string.char(b3)
        end
        i = i + 4
    end
    return table.concat(out)
end

function blobSync.newState()
    return {
        outgoing = {},
        incoming = {}
    }
end

function blobSync.prepareOutgoing(state, kind, hash, rawBytes, opts)
    opts = opts or {}
    rawBytes = rawBytes or ""
    local chunkSize = math.max(64, math.floor(tonumber(opts.chunkSize) or 720))
    local encoded = encodeBase64(rawBytes)
    local chunks = {}
    for i = 1, #encoded, chunkSize do
        chunks[#chunks + 1] = encoded:sub(i, i + chunkSize - 1)
    end

    local transfer = {
        kind = kind,
        hash = hash,
        raw = rawBytes,
        rawBytes = #rawBytes,
        encodedBytes = #encoded,
        chunkSize = chunkSize,
        chunkCount = #chunks,
        chunks = chunks,
        extra = opts.extra or {}
    }
    state.outgoing[kind .. "|" .. hash] = transfer
    return transfer
end

function blobSync.buildMetaPacket(transfer, extra)
    extra = extra or transfer.extra or {}
    return {
        kind = transfer.kind,
        hash = transfer.hash,
        rawBytes = transfer.rawBytes,
        encodedBytes = transfer.encodedBytes,
        chunkSize = transfer.chunkSize,
        chunkCount = transfer.chunkCount,
        ownerId = extra.ownerId,
        role = extra.role,
        modelHash = extra.modelHash
    }
end

function blobSync.getOutgoingChunk(transfer, chunkIndex)
    if not transfer then
        return nil
    end
    return transfer.chunks[chunkIndex]
end

function blobSync.acceptMeta(state, meta)
    if type(meta) ~= "table" then
        return false, "invalid blob meta packet"
    end
    if type(meta.kind) ~= "string" or type(meta.hash) ~= "string" then
        return false, "missing kind/hash in blob meta"
    end
    local chunkCount = math.max(0, math.floor(tonumber(meta.chunkCount) or 0))
    local chunkSize = math.max(1, math.floor(tonumber(meta.chunkSize) or 0))
    if chunkCount <= 0 then
        return false, "invalid chunk count"
    end
    state.incoming[meta.kind .. "|" .. meta.hash] = {
        meta = meta,
        received = {},
        receivedCount = 0,
        chunkCount = chunkCount,
        chunkSize = chunkSize
    }
    return true
end

function blobSync.acceptChunk(state, kind, hash, chunkIndex, chunkData)
    local key = tostring(kind) .. "|" .. tostring(hash)
    local transfer = state.incoming[key]
    if not transfer then
        return nil, "blob chunk without metadata"
    end
    if chunkIndex < 1 or chunkIndex > transfer.chunkCount then
        return nil, "chunk index out of bounds"
    end
    if type(chunkData) ~= "string" or #chunkData > transfer.chunkSize then
        return nil, "invalid blob chunk payload"
    end
    if not transfer.received[chunkIndex] then
        transfer.received[chunkIndex] = chunkData
        transfer.receivedCount = transfer.receivedCount + 1
    end
    if transfer.receivedCount < transfer.chunkCount then
        return nil
    end

    local ordered = {}
    for i = 1, transfer.chunkCount do
        if type(transfer.received[i]) ~= "string" then
            return nil, "missing blob chunk " .. tostring(i)
        end
        ordered[i] = transfer.received[i]
    end
    local encoded = table.concat(ordered)
    local raw = decodeBase64(encoded)
    if not raw then
        return nil, "failed to decode blob payload"
    end
    state.incoming[key] = nil
    return {
        kind = transfer.meta.kind,
        hash = transfer.meta.hash,
        raw = raw,
        meta = transfer.meta
    }
end

return blobSync
