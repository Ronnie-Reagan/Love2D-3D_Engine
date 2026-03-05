local packetCodec = {}

local function splitPreservingEmpty(payload, delimiter)
    if type(payload) ~= "string" then
        return {}
    end
    if delimiter == nil or delimiter == "" then
        return { payload }
    end

    local out = {}
    local startIndex = 1
    local delimLen = #delimiter
    while true do
        local hitStart = string.find(payload, delimiter, startIndex, true)
        if not hitStart then
            out[#out + 1] = payload:sub(startIndex)
            break
        end
        out[#out + 1] = payload:sub(startIndex, hitStart - 1)
        startIndex = hitStart + delimLen
    end
    return out
end

function packetCodec.splitFields(payload, delimiter)
    return splitPreservingEmpty(payload, delimiter or "|")
end

function packetCodec.packetType(payload, delimiter)
    local fields = packetCodec.splitFields(payload, delimiter)
    return fields[1], fields
end

function packetCodec.parseKeyValueFields(fields, startIndex)
    local kv = {}
    if type(fields) ~= "table" then
        return kv
    end

    for i = startIndex or 2, #fields do
        local token = fields[i]
        if type(token) == "string" then
            local eq = string.find(token, "=", 1, true)
            if eq and eq > 1 then
                local key = token:sub(1, eq - 1)
                kv[key] = token:sub(eq + 1)
            end
        end
    end
    return kv
end

function packetCodec.findLastNonEmptyIndex(fields, startIndex)
    if type(fields) ~= "table" then
        return nil
    end
    local first = math.max(1, math.floor(tonumber(startIndex) or 1))
    for i = #fields, first, -1 do
        if fields[i] ~= "" and fields[i] ~= nil then
            return i
        end
    end
    return nil
end

function packetCodec.readTrailingInteger(fields, startIndex)
    local index = packetCodec.findLastNonEmptyIndex(fields, startIndex)
    if not index then
        return nil, nil
    end
    local value = tonumber(fields[index])
    if not value then
        return nil, index
    end
    local asInt = math.floor(value)
    if value ~= asInt then
        return nil, index
    end
    return asInt, index
end

return packetCodec
