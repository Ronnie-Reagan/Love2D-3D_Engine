local json = {}

local function decodeError(pos, message)
    return nil, string.format("json decode error at byte %d: %s", pos, message)
end

local function skipWhitespace(text, i)
    local len = #text
    while i <= len do
        local c = text:sub(i, i)
        if c ~= " " and c ~= "\t" and c ~= "\r" and c ~= "\n" then
            break
        end
        i = i + 1
    end
    return i
end

local function decodeString(text, i)
    i = i + 1
    local len = #text
    local out = {}
    while i <= len do
        local c = text:sub(i, i)
        if c == "\"" then
            return table.concat(out), i + 1
        end
        if c == "\\" then
            local esc = text:sub(i + 1, i + 1)
            if esc == "" then
                return decodeError(i, "unterminated escape")
            end
            if esc == "\"" or esc == "\\" or esc == "/" then
                out[#out + 1] = esc
                i = i + 2
            elseif esc == "b" then
                out[#out + 1] = "\b"
                i = i + 2
            elseif esc == "f" then
                out[#out + 1] = "\f"
                i = i + 2
            elseif esc == "n" then
                out[#out + 1] = "\n"
                i = i + 2
            elseif esc == "r" then
                out[#out + 1] = "\r"
                i = i + 2
            elseif esc == "t" then
                out[#out + 1] = "\t"
                i = i + 2
            elseif esc == "u" then
                local hex = text:sub(i + 2, i + 5)
                if #hex < 4 or not hex:match("^[0-9a-fA-F]+$") then
                    return decodeError(i, "invalid unicode escape")
                end
                local code = tonumber(hex, 16)
                if code <= 0x7F then
                    out[#out + 1] = string.char(code)
                elseif code <= 0x7FF then
                    out[#out + 1] = string.char(
                        0xC0 + math.floor(code / 0x40),
                        0x80 + (code % 0x40)
                    )
                elseif code <= 0xFFFF then
                    out[#out + 1] = string.char(
                        0xE0 + math.floor(code / 0x1000),
                        0x80 + (math.floor(code / 0x40) % 0x40),
                        0x80 + (code % 0x40)
                    )
                else
                    out[#out + 1] = "?"
                end
                i = i + 6
            else
                return decodeError(i, "unknown escape \\" .. esc)
            end
        else
            out[#out + 1] = c
            i = i + 1
        end
    end
    return decodeError(i, "unterminated string")
end

local function decodeNumber(text, i)
    local s, e = text:find("^-?%d+%.?%d*[eE]?[+-]?%d*", i)
    if not s then
        return decodeError(i, "invalid number")
    end
    local token = text:sub(s, e)
    local value = tonumber(token)
    if value == nil then
        return decodeError(i, "invalid numeric token")
    end
    return value, e + 1
end

local decodeValue

local function decodeArray(text, i)
    i = i + 1
    local out = {}
    i = skipWhitespace(text, i)
    if text:sub(i, i) == "]" then
        return out, i + 1
    end

    while true do
        local value
        value, i = decodeValue(text, i)
        if value == nil and type(i) == "string" then
            return nil, i
        end
        out[#out + 1] = value
        i = skipWhitespace(text, i)
        local c = text:sub(i, i)
        if c == "]" then
            return out, i + 1
        end
        if c ~= "," then
            return decodeError(i, "expected ',' or ']'")
        end
        i = skipWhitespace(text, i + 1)
    end
end

local function decodeObject(text, i)
    i = i + 1
    local out = {}
    i = skipWhitespace(text, i)
    if text:sub(i, i) == "}" then
        return out, i + 1
    end

    while true do
        if text:sub(i, i) ~= "\"" then
            return decodeError(i, "expected object key string")
        end
        local key
        key, i = decodeString(text, i)
        if key == nil and type(i) == "string" then
            return nil, i
        end
        i = skipWhitespace(text, i)
        if text:sub(i, i) ~= ":" then
            return decodeError(i, "expected ':' after object key")
        end
        i = skipWhitespace(text, i + 1)
        local value
        value, i = decodeValue(text, i)
        if value == nil and type(i) == "string" then
            return nil, i
        end
        out[key] = value
        i = skipWhitespace(text, i)
        local c = text:sub(i, i)
        if c == "}" then
            return out, i + 1
        end
        if c ~= "," then
            return decodeError(i, "expected ',' or '}'")
        end
        i = skipWhitespace(text, i + 1)
    end
end

decodeValue = function(text, i)
    i = skipWhitespace(text, i)
    local c = text:sub(i, i)
    if c == "\"" then
        return decodeString(text, i)
    end
    if c == "{" then
        return decodeObject(text, i)
    end
    if c == "[" then
        return decodeArray(text, i)
    end
    if c == "-" or c:match("%d") then
        return decodeNumber(text, i)
    end
    if text:sub(i, i + 3) == "true" then
        return true, i + 4
    end
    if text:sub(i, i + 4) == "false" then
        return false, i + 5
    end
    if text:sub(i, i + 3) == "null" then
        return nil, i + 4
    end
    return decodeError(i, "unexpected token")
end

function json.decode(text)
    if type(text) ~= "string" then
        return nil, "json input must be a string"
    end
    local value, nextPos = decodeValue(text, 1)
    if value == nil and type(nextPos) == "string" then
        return nil, nextPos
    end
    nextPos = skipWhitespace(text, nextPos)
    if nextPos <= #text then
        return nil, string.format("json decode error at byte %d: trailing garbage", nextPos)
    end
    return value
end

return json
