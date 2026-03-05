local json = require("Source.ModelModule.JsonDecode")

local gltfImporter = {}

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

local function readU16LE(data, offset)
    local b1, b2 = data:byte(offset, offset + 1)
    if not b2 then
        return nil
    end
    return b1 + b2 * 256
end

local function readS16LE(data, offset)
    local u = readU16LE(data, offset)
    if not u then
        return nil
    end
    if u >= 32768 then
        return u - 65536
    end
    return u
end

local function readU32LE(data, offset)
    local b1, b2, b3, b4 = data:byte(offset, offset + 3)
    if not b4 then
        return nil
    end
    return b1 + b2 * 256 + b3 * 65536 + b4 * 16777216
end

local function readS8(data, offset)
    local b = data:byte(offset, offset)
    if not b then
        return nil
    end
    if b >= 128 then
        return b - 256
    end
    return b
end

local function readU8(data, offset)
    return data:byte(offset, offset)
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

local function copyTable(t)
    local out = {}
    for k, v in pairs(t) do
        out[k] = v
    end
    return out
end

local function splitPath(path)
    path = (path or ""):gsub("\\", "/")
    local prefix = ""
    if path:match("^[A-Za-z]:/") then
        prefix = path:sub(1, 2)
        path = path:sub(4)
    elseif path:sub(1, 1) == "/" then
        prefix = "/"
        path = path:sub(2)
    end
    local segs = {}
    for seg in path:gmatch("[^/]+") do
        segs[#segs + 1] = seg
    end
    return prefix, segs
end

local function joinPath(prefix, segs)
    local body = table.concat(segs, "/")
    if prefix == "/" then
        return "/" .. body
    end
    if prefix ~= "" then
        if body == "" then
            return prefix .. "/"
        end
        return prefix .. "/" .. body
    end
    return body
end

local function dirname(path)
    if type(path) ~= "string" then
        return "."
    end
    local normalized = path:gsub("\\", "/")
    local d = normalized:match("^(.*)/[^/]+$")
    if not d or d == "" then
        return "."
    end
    return d
end

local function hasScheme(uri)
    if type(uri) ~= "string" then
        return false
    end
    return uri:match("^[a-zA-Z][a-zA-Z0-9+.-]*:")
end

local function resolveRelativeSandboxed(baseDir, uri)
    if type(uri) ~= "string" or uri == "" then
        return nil, "missing uri"
    end
    if uri:sub(1, 5) == "data:" then
        return uri
    end
    if uri:sub(1, 1) == "/" or uri:sub(1, 1) == "\\" or uri:match("^[A-Za-z]:[\\/]") then
        return nil, "absolute URIs are not allowed for glTF sidecars"
    end
    if hasScheme(uri) then
        return nil, "non-data URI schemes are not allowed for glTF sidecars"
    end

    local prefix, baseSegs = splitPath(baseDir or ".")
    local outSegs = copyTable(baseSegs)
    local baseDepth = #baseSegs

    for seg in uri:gsub("\\", "/"):gmatch("[^/]+") do
        if seg == "." or seg == "" then
            -- skip
        elseif seg == ".." then
            if #outSegs <= baseDepth then
                return nil, "URI escapes model directory sandbox"
            end
            outSegs[#outSegs] = nil
        else
            outSegs[#outSegs + 1] = seg
        end
    end

    return joinPath(prefix, outSegs)
end

local function readFileBytes(path)
    if type(path) ~= "string" or path == "" then
        return nil, "missing file path"
    end
    if loveLib and loveLib.filesystem and loveLib.filesystem.read then
        local bytes = loveLib.filesystem.read(path)
        if bytes then
            return bytes
        end
    end
    local handle = io.open(path, "rb")
    if not handle then
        return nil, "could not open file"
    end
    local bytes = handle:read("*a")
    handle:close()
    if not bytes then
        return nil, "failed to read file"
    end
    return bytes
end

local BASE64_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
local BASE64_INDEX = {}
for i = 1, #BASE64_CHARS do
    BASE64_INDEX[BASE64_CHARS:sub(i, i)] = i - 1
end

local function decodeBase64(raw)
    if type(raw) ~= "string" then
        return nil
    end
    raw = raw:gsub("%s+", "")
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

local function decodeDataUri(uri)
    if type(uri) ~= "string" then
        return nil, nil, "invalid data URI"
    end
    local mime, payload = uri:match("^data:([^;]+);base64,(.+)$")
    if not payload then
        return nil, nil, "unsupported data URI format"
    end
    local raw
    if loveLib and loveLib.data and loveLib.data.decode then
        local ok, decoded = pcall(function()
            return loveLib.data.decode("string", "base64", payload)
        end)
        if ok and type(decoded) == "string" then
            raw = decoded
        end
    end
    if not raw then
        raw = decodeBase64(payload)
    end
    if not raw then
        return nil, nil, "failed to decode base64 data URI"
    end
    return raw, mime
end

local function identityMatrix()
    return {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    }
end

local function mat4Multiply(a, b)
    local out = {}
    for col = 0, 3 do
        for row = 0, 3 do
            local idx = row + col * 4 + 1
            out[idx] =
                a[row + 1] * b[col * 4 + 1] +
                a[row + 5] * b[col * 4 + 2] +
                a[row + 9] * b[col * 4 + 3] +
                a[row + 13] * b[col * 4 + 4]
        end
    end
    return out
end

local function mat4FromTRS(translation, rotation, scale)
    local tx = (translation and translation[1]) or 0
    local ty = (translation and translation[2]) or 0
    local tz = (translation and translation[3]) or 0
    local x = (rotation and rotation[1]) or 0
    local y = (rotation and rotation[2]) or 0
    local z = (rotation and rotation[3]) or 0
    local w = (rotation and rotation[4]) or 1
    local sx = (scale and scale[1]) or 1
    local sy = (scale and scale[2]) or 1
    local sz = (scale and scale[3]) or 1

    local xx, yy, zz = x * x, y * y, z * z
    local xy, xz, yz = x * y, x * z, y * z
    local wx, wy, wz = w * x, w * y, w * z

    return {
        (1 - 2 * (yy + zz)) * sx,
        (2 * (xy + wz)) * sx,
        (2 * (xz - wy)) * sx,
        0,

        (2 * (xy - wz)) * sy,
        (1 - 2 * (xx + zz)) * sy,
        (2 * (yz + wx)) * sy,
        0,

        (2 * (xz + wy)) * sz,
        (2 * (yz - wx)) * sz,
        (1 - 2 * (xx + yy)) * sz,
        0,

        tx,
        ty,
        tz,
        1
    }
end

local function transformPosition(m, v)
    return {
        m[1] * v[1] + m[5] * v[2] + m[9] * v[3] + m[13],
        m[2] * v[1] + m[6] * v[2] + m[10] * v[3] + m[14],
        m[3] * v[1] + m[7] * v[2] + m[11] * v[3] + m[15]
    }
end

local function mat3DeterminantFromMat4(m)
    local a11, a12, a13 = m[1], m[5], m[9]
    local a21, a22, a23 = m[2], m[6], m[10]
    local a31, a32, a33 = m[3], m[7], m[11]
    return a11 * (a22 * a33 - a23 * a32) -
        a12 * (a21 * a33 - a23 * a31) +
        a13 * (a21 * a32 - a22 * a31)
end

local function normalize3(v)
    local len = math.sqrt(v[1] * v[1] + v[2] * v[2] + v[3] * v[3])
    if len <= 1e-8 then
        return { 0, 0, 1 }
    end
    return { v[1] / len, v[2] / len, v[3] / len }
end

local function transformDirection(m, v)
    return normalize3({
        m[1] * v[1] + m[5] * v[2] + m[9] * v[3],
        m[2] * v[1] + m[6] * v[2] + m[10] * v[3],
        m[3] * v[1] + m[7] * v[2] + m[11] * v[3]
    })
end

local componentTypeBytes = {
    [5120] = 1, -- BYTE
    [5121] = 1, -- UNSIGNED_BYTE
    [5122] = 2, -- SHORT
    [5123] = 2, -- UNSIGNED_SHORT
    [5125] = 4, -- UNSIGNED_INT
    [5126] = 4  -- FLOAT
}

local typeComponentCount = {
    SCALAR = 1,
    VEC2 = 2,
    VEC3 = 3,
    VEC4 = 4,
    MAT4 = 16
}

local function readComponent(data, offset, componentType)
    if componentType == 5120 then
        return readS8(data, offset)
    end
    if componentType == 5121 then
        return readU8(data, offset)
    end
    if componentType == 5122 then
        return readS16LE(data, offset)
    end
    if componentType == 5123 then
        return readU16LE(data, offset)
    end
    if componentType == 5125 then
        return readU32LE(data, offset)
    end
    if componentType == 5126 then
        return readF32LE(data, offset)
    end
    return nil
end

local function normalizeComponent(value, componentType)
    if componentType == 5120 then
        return math.max(-1, value / 127)
    end
    if componentType == 5121 then
        return value / 255
    end
    if componentType == 5122 then
        return math.max(-1, value / 32767)
    end
    if componentType == 5123 then
        return value / 65535
    end
    if componentType == 5125 then
        return value / 4294967295
    end
    return value
end

local function parseGLB(raw)
    if #raw < 20 then
        return nil, "GLB header too short"
    end
    local magic = readU32LE(raw, 1)
    if magic ~= 0x46546C67 then
        return nil, "invalid GLB magic"
    end
    local version = readU32LE(raw, 5)
    if version ~= 2 then
        return nil, "unsupported GLB version " .. tostring(version)
    end
    local totalLength = readU32LE(raw, 9)
    if totalLength ~= #raw then
        return nil, "GLB length mismatch"
    end

    local cursor = 13
    local jsonChunk
    local binChunk
    while cursor <= #raw do
        local chunkLen = readU32LE(raw, cursor)
        local chunkType = readU32LE(raw, cursor + 4)
        if not chunkLen or not chunkType then
            return nil, "invalid GLB chunk header"
        end
        local startPos = cursor + 8
        local endPos = startPos + chunkLen - 1
        if endPos > #raw then
            return nil, "GLB chunk exceeds payload bounds"
        end
        local chunkData = raw:sub(startPos, endPos)
        if chunkType == 0x4E4F534A then
            jsonChunk = chunkData
        elseif chunkType == 0x004E4942 then
            binChunk = chunkData
        end
        cursor = endPos + 1
    end

    if not jsonChunk then
        return nil, "GLB missing JSON chunk"
    end
    return jsonChunk, binChunk
end

local function decodeImageRecord(imageRecord, buffers, baseDir)
    if type(imageRecord) ~= "table" then
        return nil, "invalid image record"
    end

    local rawBytes
    local mime = imageRecord.mimeType
    if imageRecord.uri then
        if type(imageRecord.uri) ~= "string" then
            return nil, "invalid image URI"
        end
        if imageRecord.uri:sub(1, 5) == "data:" then
            rawBytes, mime = decodeDataUri(imageRecord.uri)
            if not rawBytes then
                return nil, mime or "failed to decode image data URI"
            end
        else
            local resolved, err = resolveRelativeSandboxed(baseDir, imageRecord.uri)
            if not resolved then
                return nil, err
            end
            rawBytes, err = readFileBytes(resolved)
            if not rawBytes then
                return nil, err
            end
        end
    elseif imageRecord.bufferView ~= nil then
        local viewIndex = tonumber(imageRecord.bufferView)
        local view = viewIndex and buffers.bufferViews[viewIndex + 1]
        if not view then
            return nil, "image bufferView missing"
        end
        local bufferData = buffers.data[view.buffer + 1]
        if type(bufferData) ~= "string" then
            return nil, "image buffer missing"
        end
        local startByte = (view.byteOffset or 0) + 1
        local byteLength = view.byteLength or 0
        rawBytes = bufferData:sub(startByte, startByte + byteLength - 1)
    else
        return nil, "image record has no uri or bufferView"
    end

    local out = {
        mimeType = mime,
        rawBytes = rawBytes
    }

    if loveLib and loveLib.image and loveLib.graphics then
        local okData, imageData = pcall(function()
            return loveLib.image.newImageData(rawBytes)
        end)
        if (not okData or not imageData) and loveLib.filesystem and loveLib.filesystem.newFileData then
            local okFileData, fileData = pcall(function()
                return loveLib.filesystem.newFileData(rawBytes, "gltf_image_" .. tostring(math.random(1, 999999)))
            end)
            if okFileData and fileData then
                okData, imageData = pcall(function()
                    return loveLib.image.newImageData(fileData)
                end)
            end
        end
        if okData and imageData then
            out.width = imageData:getWidth()
            out.height = imageData:getHeight()
            out.imageData = imageData
            local okImage, image = pcall(function()
                return loveLib.graphics.newImage(imageData, { mipmaps = true, linear = true })
            end)
            if (not okImage or not image) then
                okImage, image = pcall(function()
                    return loveLib.graphics.newImage(imageData)
                end)
            end
            if okImage then
                pcall(function()
                    image:setFilter("linear", "linear", 8)
                end)
                pcall(function()
                    image:setMipmapFilter("linear", 0.2)
                end)
                out.image = image
            end
        end
    end

    return out
end

local function resolveTextureRef(gltf, texRef)
    if type(texRef) ~= "table" then
        return nil
    end
    local textureIndex = tonumber(texRef.index)
    if not textureIndex then
        return nil
    end
    local textures = gltf.textures or {}
    local textureDef = textures[textureIndex + 1]
    local sourceIndex = textureDef and tonumber(textureDef.source)
    if sourceIndex == nil then
        return nil
    end
    local out = {
        textureIndex = textureIndex,
        imageIndex = sourceIndex + 1,
        texCoord = tonumber(texRef.texCoord) or 0
    }
    if texRef.scale ~= nil then
        out.scale = tonumber(texRef.scale) or 1
    end
    if texRef.strength ~= nil then
        out.strength = tonumber(texRef.strength) or 1
    end
    return out
end

local function buildMaterials(gltf)
    local materials = {}
    local source = gltf.materials or {}
    for i = 1, #source do
        local m = source[i] or {}
        local pbr = m.pbrMetallicRoughness or {}
        local ext = m.extensions or {}
        local specGloss = ext.KHR_materials_pbrSpecularGlossiness
        materials[#materials + 1] = {
            name = m.name or ("material_" .. tostring(i - 1)),
            baseColorFactor = pbr.baseColorFactor or { 1, 1, 1, 1 },
            metallicFactor = (pbr.metallicFactor ~= nil) and pbr.metallicFactor or 1,
            roughnessFactor = (pbr.roughnessFactor ~= nil) and pbr.roughnessFactor or 1,
            baseColorTexture = resolveTextureRef(gltf, pbr.baseColorTexture),
            metallicRoughnessTexture = resolveTextureRef(gltf, pbr.metallicRoughnessTexture),
            normalTexture = resolveTextureRef(gltf, m.normalTexture),
            occlusionTexture = resolveTextureRef(gltf, m.occlusionTexture),
            emissiveTexture = resolveTextureRef(gltf, m.emissiveTexture),
            emissiveFactor = m.emissiveFactor or { 0, 0, 0 },
            alphaMode = m.alphaMode or "OPAQUE",
            alphaCutoff = m.alphaCutoff or 0.5,
            doubleSided = m.doubleSided and true or false,
            workflow = specGloss and "specGloss" or "metalRough",
            diffuseFactor = (specGloss and specGloss.diffuseFactor) or { 1, 1, 1, 1 },
            specularFactor = (specGloss and specGloss.specularFactor) or { 1, 1, 1 },
            glossinessFactor = (specGloss and specGloss.glossinessFactor ~= nil) and specGloss.glossinessFactor or 1,
            diffuseTexture = resolveTextureRef(gltf, specGloss and specGloss.diffuseTexture or nil),
            specularGlossinessTexture = resolveTextureRef(gltf, specGloss and specGloss.specularGlossinessTexture or nil)
        }
    end
    if #materials == 0 then
        materials[1] = {
            name = "default",
            baseColorFactor = { 1, 1, 1, 1 },
            metallicFactor = 0,
            roughnessFactor = 1,
            alphaMode = "OPAQUE",
            alphaCutoff = 0.5,
            doubleSided = false,
            workflow = "metalRough",
            diffuseFactor = { 1, 1, 1, 1 },
            specularFactor = { 1, 1, 1 },
            glossinessFactor = 1
        }
    end
    return materials
end

local function decodeAccessor(gltf, buffers, accessorIndex)
    if accessorIndex == nil then
        return nil
    end
    local accessor = gltf.accessors and gltf.accessors[accessorIndex + 1]
    if not accessor then
        return nil
    end
    local view = gltf.bufferViews and gltf.bufferViews[(accessor.bufferView or -1) + 1]
    if not view then
        return nil
    end
    local bufferData = buffers[(view.buffer or -1) + 1]
    if type(bufferData) ~= "string" then
        return nil
    end

    local componentType = accessor.componentType
    local components = typeComponentCount[accessor.type or ""] or 0
    local bytesPerComponent = componentTypeBytes[componentType]
    local count = accessor.count or 0
    if components <= 0 or bytesPerComponent <= 0 or count <= 0 then
        return nil
    end
    local elementBytes = components * bytesPerComponent
    local stride = view.byteStride or elementBytes
    local startOffset = (view.byteOffset or 0) + (accessor.byteOffset or 0)

    local out = {}
    for i = 0, count - 1 do
        local base = startOffset + i * stride + 1
        local item = {}
        for c = 0, components - 1 do
            local value = readComponent(bufferData, base + c * bytesPerComponent, componentType)
            if value == nil then
                return nil
            end
            if accessor.normalized then
                value = normalizeComponent(value, componentType)
            end
            item[c + 1] = value
        end
        if components == 1 then
            out[#out + 1] = item[1]
        else
            out[#out + 1] = item
        end
    end
    return out
end

local function parseGltfDocument(gltf, opts)
    opts = opts or {}
    local baseDir = opts.baseDir or "."
    local buffers = {}

    local sourceBuffers = gltf.buffers or {}
    for i = 1, #sourceBuffers do
        local buffer = sourceBuffers[i] or {}
        local raw
        if buffer.uri and buffer.uri ~= "" then
            if buffer.uri:sub(1, 5) == "data:" then
                raw = decodeDataUri(buffer.uri)
                if not raw then
                    return nil, "failed to decode glTF buffer data URI at index " .. tostring(i - 1)
                end
            else
                local resolved, err = resolveRelativeSandboxed(baseDir, buffer.uri)
                if not resolved then
                    return nil, "buffer URI rejected: " .. tostring(err)
                end
                raw, err = readFileBytes(resolved)
                if not raw then
                    return nil, "failed to read glTF buffer: " .. tostring(err)
                end
            end
        else
            raw = opts.primaryBin
        end
        if type(raw) ~= "string" then
            return nil, "missing buffer payload at index " .. tostring(i - 1)
        end
        buffers[i] = raw
    end

    local materials = buildMaterials(gltf)
    local images = {}
    local imageSource = gltf.images or {}
    local imageBuffers = {
        data = buffers,
        bufferViews = gltf.bufferViews or {}
    }
    for i = 1, #imageSource do
        local decoded, err = decodeImageRecord(imageSource[i], imageBuffers, baseDir)
        if not decoded then
            return nil, "image decode failed at index " .. tostring(i - 1) .. ": " .. tostring(err)
        end
        images[i] = decoded
    end

    local vertices, faces = {}, {}
    local vertexNormals, vertexUVs, vertexUVs1, vertexTangents, vertexColors = {}, {}, {}, {}, {}
    local faceMaterials = {}
    local submeshes = {}

    local function emitPrimitive(primitive, worldMatrix)
        if type(primitive) ~= "table" then
            return nil, "invalid primitive"
        end
        local mode = primitive.mode or 4
        if mode ~= 4 then
            return nil, "unsupported primitive mode " .. tostring(mode) .. " (triangles only)"
        end
        local attrs = primitive.attributes or {}
        local positions = decodeAccessor(gltf, buffers, attrs.POSITION)
        if not positions or #positions == 0 then
            return nil, "primitive missing POSITION data"
        end
        local normals = decodeAccessor(gltf, buffers, attrs.NORMAL)
        local uvs = decodeAccessor(gltf, buffers, attrs.TEXCOORD_0)
        local uvs1 = decodeAccessor(gltf, buffers, attrs.TEXCOORD_1)
        local tangents = decodeAccessor(gltf, buffers, attrs.TANGENT)
        local colors = decodeAccessor(gltf, buffers, attrs.COLOR_0)
        local indices = decodeAccessor(gltf, buffers, primitive.indices)
        local mirroredTransform = mat3DeterminantFromMat4(worldMatrix) < 0

        local triIndices = {}
        if indices and #indices > 0 then
            for i = 1, #indices, 3 do
                local a, b, c = indices[i], indices[i + 1], indices[i + 2]
                if c == nil then
                    break
                end
                if mirroredTransform then
                    triIndices[#triIndices + 1] = { a + 1, c + 1, b + 1 }
                else
                    triIndices[#triIndices + 1] = { a + 1, b + 1, c + 1 }
                end
            end
        else
            for i = 1, #positions, 3 do
                if positions[i + 2] then
                    if mirroredTransform then
                        triIndices[#triIndices + 1] = { i, i + 2, i + 1 }
                    else
                        triIndices[#triIndices + 1] = { i, i + 1, i + 2 }
                    end
                end
            end
        end

        local faceStart = #faces + 1
        for _, tri in ipairs(triIndices) do
            local base = #vertices
            for v = 1, 3 do
                local srcIndex = tri[v]
                local pos = positions[srcIndex]
                if not pos then
                    return nil, "primitive index out of range"
                end
                local worldPos = transformPosition(worldMatrix, { pos[1], pos[2], pos[3] })
                vertices[base + v] = worldPos
                if normals and normals[srcIndex] then
                    vertexNormals[base + v] = transformDirection(worldMatrix, normals[srcIndex])
                end
                if uvs and uvs[srcIndex] then
                    local uv = uvs[srcIndex]
                    vertexUVs[base + v] = { uv[1] or 0, uv[2] or 0 }
                end
                if uvs1 and uvs1[srcIndex] then
                    local uv1 = uvs1[srcIndex]
                    vertexUVs1[base + v] = { uv1[1] or 0, uv1[2] or 0 }
                end
                if tangents and tangents[srcIndex] then
                    local t = tangents[srcIndex]
                    local handedness = t[4] or 1
                    if mirroredTransform then
                        handedness = -handedness
                    end
                    vertexTangents[base + v] = {
                        t[1] or 0,
                        t[2] or 0,
                        t[3] or 0,
                        handedness
                    }
                end
                if colors and colors[srcIndex] then
                    local c = colors[srcIndex]
                    vertexColors[base + v] = {
                        c[1] or 1,
                        c[2] or 1,
                        c[3] or 1,
                        c[4] or 1
                    }
                end
            end
            faces[#faces + 1] = { base + 1, base + 2, base + 3 }
            faceMaterials[#faces] = (primitive.material or 0) + 1
        end

        submeshes[#submeshes + 1] = {
            materialIndex = (primitive.material or 0) + 1,
            faceStart = faceStart,
            faceCount = (#faces - faceStart) + 1
        }
        return true
    end

    local meshes = gltf.meshes or {}
    local nodes = gltf.nodes or {}
    local scenes = gltf.scenes or {}
    local scene = scenes[(gltf.scene or 0) + 1]

    local function nodeMatrix(node)
        if type(node.matrix) == "table" and #node.matrix == 16 then
            return {
                node.matrix[1], node.matrix[2], node.matrix[3], node.matrix[4],
                node.matrix[5], node.matrix[6], node.matrix[7], node.matrix[8],
                node.matrix[9], node.matrix[10], node.matrix[11], node.matrix[12],
                node.matrix[13], node.matrix[14], node.matrix[15], node.matrix[16]
            }
        end
        return mat4FromTRS(node.translation, node.rotation, node.scale)
    end

    local function walkNode(nodeIndex, parentMatrix)
        local node = nodes[nodeIndex + 1]
        if not node then
            return nil, "invalid node index " .. tostring(nodeIndex)
        end
        local localMatrix = nodeMatrix(node)
        local worldMatrix = mat4Multiply(parentMatrix, localMatrix)

        if node.mesh ~= nil then
            local mesh = meshes[node.mesh + 1]
            if not mesh then
                return nil, "node mesh index missing"
            end
            for _, primitive in ipairs(mesh.primitives or {}) do
                local ok, err = emitPrimitive(primitive, worldMatrix)
                if not ok then
                    return nil, err
                end
            end
        end

        for _, childIndex in ipairs(node.children or {}) do
            local ok, err = walkNode(childIndex, worldMatrix)
            if not ok then
                return nil, err
            end
        end
        return true
    end

    local roots = {}
    local seenRoot = {}
    if scene and type(scene.nodes) == "table" then
        for _, rootIndex in ipairs(scene.nodes) do
            local asNum = tonumber(rootIndex)
            if asNum then
                asNum = math.floor(asNum)
                if asNum >= 0 and asNum < #nodes and not seenRoot[asNum] then
                    seenRoot[asNum] = true
                    roots[#roots + 1] = asNum
                end
            end
        end
    end
    if #roots == 0 and #nodes > 0 then
        local hasParent = {}
        for _, node in ipairs(nodes) do
            for _, childIndex in ipairs(node.children or {}) do
                local childNum = tonumber(childIndex)
                if childNum then
                    childNum = math.floor(childNum)
                    if childNum >= 0 and childNum < #nodes then
                        hasParent[childNum] = true
                    end
                end
            end
        end
        for i = 0, #nodes - 1 do
            if not hasParent[i] then
                roots[#roots + 1] = i
            end
        end
        if #roots == 0 then
            for i = 0, #nodes - 1 do
                roots[#roots + 1] = i
            end
        end
    end

    for _, root in ipairs(roots) do
        local ok, err = walkNode(root, identityMatrix())
        if not ok then
            return nil, err
        end
    end

    if #faces == 0 then
        return nil, "no drawable triangles found in glTF"
    end

    local model = {
        vertices = vertices,
        faces = faces,
        vertexColors = (#vertexColors > 0) and vertexColors or nil,
        isSolid = true,
        vertexNormals = (#vertexNormals > 0) and vertexNormals or nil,
        vertexUVs = (#vertexUVs > 0) and vertexUVs or nil,
        vertexUVs1 = (#vertexUVs1 > 0) and vertexUVs1 or nil,
        vertexTangents = (#vertexTangents > 0) and vertexTangents or nil,
        faceMaterials = faceMaterials
    }

    return {
        sourceFormat = opts.sourceFormat or "gltf",
        geometry = {
            vertices = vertices,
            faces = faces,
            vertexNormals = model.vertexNormals,
            vertexUVs = model.vertexUVs,
            vertexUVs1 = model.vertexUVs1,
            vertexTangents = model.vertexTangents,
            vertexColors = model.vertexColors,
            submeshes = submeshes,
            faceMaterials = faceMaterials
        },
        materials = materials,
        images = images,
        model = model,
        gltf = gltf
    }
end

local function detectFormat(pathOrName, raw)
    if type(pathOrName) == "string" then
        local lower = pathOrName:lower()
        if lower:match("%.glb$") then
            return "glb"
        end
        if lower:match("%.gltf$") then
            return "gltf"
        end
    end
    if type(raw) == "string" and #raw >= 4 then
        local magic = readU32LE(raw, 1)
        if magic == 0x46546C67 then
            return "glb"
        end
        local first = raw:match("^%s*([%{%[])")
        if first == "{" then
            return "gltf"
        end
    end
    return nil
end

local function rejectUnsupportedExtensions(gltf)
    local used = gltf.extensionsUsed or {}
    local required = gltf.extensionsRequired or {}
    local unsupported = {
        KHR_draco_mesh_compression = true,
        EXT_meshopt_compression = true,
        KHR_texture_basisu = true
    }
    for _, name in ipairs(used) do
        if unsupported[name] then
            return nil, "unsupported glTF extension: " .. name
        end
    end
    for _, name in ipairs(required) do
        if unsupported[name] then
            return nil, "required unsupported glTF extension: " .. name
        end
    end
    return true
end

function gltfImporter.canHandle(pathOrName, raw)
    return detectFormat(pathOrName, raw) ~= nil
end

function gltfImporter.loadFromBytes(raw, sourceName, opts)
    opts = opts or {}
    if type(raw) ~= "string" or raw == "" then
        return nil, "invalid glTF/GLB payload"
    end

    local format = detectFormat(sourceName, raw)
    if not format then
        return nil, "unknown glTF format"
    end

    local gltfJsonRaw
    local primaryBin
    if format == "glb" then
        gltfJsonRaw, primaryBin = parseGLB(raw)
        if not gltfJsonRaw then
            return nil, primaryBin
        end
    else
        gltfJsonRaw = raw
    end

    local gltf, decodeErr = json.decode(gltfJsonRaw)
    if not gltf then
        return nil, decodeErr
    end
    local okExt, extErr = rejectUnsupportedExtensions(gltf)
    if not okExt then
        return nil, extErr
    end

    return parseGltfDocument(gltf, {
        baseDir = opts.baseDir or ".",
        primaryBin = primaryBin,
        sourceFormat = format
    })
end

function gltfImporter.loadFromPath(path, opts)
    opts = opts or {}
    local raw, err = readFileBytes(path)
    if not raw then
        return nil, err
    end
    local baseDir = dirname(path)
    return gltfImporter.loadFromBytes(raw, path, {
        baseDir = baseDir,
        sourceFormat = detectFormat(path, raw)
    })
end

return gltfImporter
