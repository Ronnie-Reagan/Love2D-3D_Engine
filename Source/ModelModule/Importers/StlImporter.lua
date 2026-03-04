local stlImporter = {}

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

local function parseAsciiSTL(data)
    local vertices, faces = {}, {}
    for line in data:gmatch("[^\r\n]+") do
        local x, y, z = line:match("^%s*vertex%s+([%+%-]?[%d%.eE]+)%s+([%+%-]?[%d%.eE]+)%s+([%+%-]?[%d%.eE]+)")
        if x and y and z then
            vertices[#vertices + 1] = { tonumber(x), tonumber(y), tonumber(z) }
        elseif line:match("^%s*endfacet") then
            local count = #vertices
            if count >= 3 then
                faces[#faces + 1] = { count - 2, count - 1, count }
            end
        end
    end
    if #faces == 0 then
        return nil, "no faces found in ASCII STL"
    end
    return vertices, faces
end

local function parseBinarySTL(data)
    if #data < 84 then
        return nil, nil, "binary STL header too short"
    end

    local triCount = readU32LE(data, 81)
    if not triCount then
        return nil, nil, "could not read STL triangle count"
    end

    local expectedBytes = 84 + triCount * 50
    if #data < expectedBytes then
        return nil, nil, string.format("binary STL truncated (%d of %d bytes)", #data, expectedBytes)
    end

    local vertices, faces = {}, {}
    local cursor = 85
    for _ = 1, triCount do
        cursor = cursor + 12 -- skip normal
        local ax = readF32LE(data, cursor)
        local ay = readF32LE(data, cursor + 4)
        local az = readF32LE(data, cursor + 8)
        local bx = readF32LE(data, cursor + 12)
        local by = readF32LE(data, cursor + 16)
        local bz = readF32LE(data, cursor + 20)
        local cx = readF32LE(data, cursor + 24)
        local cy = readF32LE(data, cursor + 28)
        local cz = readF32LE(data, cursor + 32)
        if not (ax and ay and az and bx and by and bz and cx and cy and cz) then
            return nil, nil, "invalid triangle data in binary STL"
        end
        local base = #vertices
        vertices[base + 1] = { ax, ay, az }
        vertices[base + 2] = { bx, by, bz }
        vertices[base + 3] = { cx, cy, cz }
        faces[#faces + 1] = { base + 1, base + 2, base + 3 }
        cursor = cursor + 36 + 2
    end
    return vertices, faces
end

local function detectBinaryCandidate(raw)
    local triCount = (#raw >= 84) and readU32LE(raw, 81) or nil
    if not triCount then
        return false
    end
    return (84 + triCount * 50) == #raw
end

function stlImporter.canHandle(pathOrName, raw)
    if type(pathOrName) == "string" and pathOrName:lower():match("%.stl$") then
        return true
    end
    if type(raw) == "string" and detectBinaryCandidate(raw) then
        return true
    end
    if type(raw) == "string" then
        return raw:sub(1, 5):lower() == "solid"
    end
    return false
end

function stlImporter.load(raw)
    if type(raw) ~= "string" or raw == "" then
        return nil, "invalid STL payload"
    end

    local vertices, faces, parseErr
    if detectBinaryCandidate(raw) then
        vertices, faces, parseErr = parseBinarySTL(raw)
        if not vertices then
            return nil, parseErr
        end
    else
        vertices, faces = parseAsciiSTL(raw)
        if not vertices then
            vertices, faces, parseErr = parseBinarySTL(raw)
            if not vertices then
                return nil, parseErr or "failed to parse STL"
            end
        end
    end

    local geometry = {
        vertices = vertices,
        faces = faces,
        submeshes = {
            {
                materialIndex = 1,
                faceStart = 1,
                faceCount = #faces
            }
        }
    }

    local materials = {
        {
            name = "default",
            baseColorFactor = { 1, 1, 1, 1 },
            metallicFactor = 0,
            roughnessFactor = 1,
            alphaMode = "OPAQUE",
            alphaCutoff = 0.5,
            doubleSided = false
        }
    }

    return {
        sourceFormat = "stl",
        geometry = geometry,
        materials = materials,
        images = {},
        model = {
            vertices = vertices,
            faces = faces,
            isSolid = true
        }
    }
end

return stlImporter
