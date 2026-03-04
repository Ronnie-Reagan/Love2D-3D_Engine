local diagnostics = {}

local defaultBudgets = {
    sourceBytes = 100 * 1024 * 1024,
    maxTriangles = 120000,
    maxVertices = 200000,
    maxMaterials = 24,
    maxTextures = 12,
    maxTextureDimension = 4096,
    maxDecodedTextureBytes = 256 * 1024 * 1024,
    maxPaintEncodedBytes = 32 * 1024 * 1024
}

local function clampNumber(value, fallback, minValue)
    local n = tonumber(value)
    if not n then
        return fallback
    end
    if minValue and n < minValue then
        return minValue
    end
    return n
end

function diagnostics.buildBudgets(overrides)
    local out = {}
    for key, value in pairs(defaultBudgets) do
        out[key] = value
    end
    if type(overrides) == "table" then
        for key, value in pairs(overrides) do
            if out[key] ~= nil then
                out[key] = clampNumber(value, out[key], 0)
            end
        end
    end
    return out
end

local function estimateTextureBytes(imageRecord)
    if type(imageRecord) ~= "table" then
        return 0
    end
    local width = tonumber(imageRecord.width) or 0
    local height = tonumber(imageRecord.height) or 0
    if width <= 0 or height <= 0 then
        return 0
    end
    -- Conservative RGBA8 assumption for decoded texture memory.
    return width * height * 4
end

function diagnostics.validateAsset(asset, budgets)
    budgets = diagnostics.buildBudgets(budgets)
    if type(asset) ~= "table" then
        return false, "invalid asset payload"
    end

    local geometry = asset.geometry or {}
    local vertices = geometry.vertices or (asset.model and asset.model.vertices) or {}
    local faces = geometry.faces or (asset.model and asset.model.faces) or {}
    local materials = asset.materials or {}
    local images = asset.images or {}

    if #vertices > budgets.maxVertices then
        return false, string.format("model exceeds vertex budget (%d > %d)", #vertices, budgets.maxVertices)
    end
    if #faces > budgets.maxTriangles then
        return false, string.format("model exceeds triangle budget (%d > %d)", #faces, budgets.maxTriangles)
    end
    if #materials > budgets.maxMaterials then
        return false, string.format("model exceeds material budget (%d > %d)", #materials, budgets.maxMaterials)
    end
    if #images > budgets.maxTextures then
        return false, string.format("model exceeds texture count budget (%d > %d)", #images, budgets.maxTextures)
    end

    local decodedBytes = 0
    for _, image in ipairs(images) do
        local width = tonumber(image.width) or 0
        local height = tonumber(image.height) or 0
        if width > budgets.maxTextureDimension or height > budgets.maxTextureDimension then
            return false, string.format(
                "texture exceeds max dimension (%dx%d > %d)",
                width,
                height,
                budgets.maxTextureDimension
            )
        end
        decodedBytes = decodedBytes + estimateTextureBytes(image)
        if decodedBytes > budgets.maxDecodedTextureBytes then
            return false, string.format(
                "decoded texture memory exceeds budget (%d > %d)",
                decodedBytes,
                budgets.maxDecodedTextureBytes
            )
        end
    end

    return true
end

function diagnostics.validateSourceSize(raw, budgets)
    budgets = diagnostics.buildBudgets(budgets)
    if type(raw) ~= "string" then
        return false, "invalid source payload"
    end
    if #raw > budgets.sourceBytes then
        return false, string.format("source exceeds max size (%d > %d)", #raw, budgets.sourceBytes)
    end
    return true
end

return diagnostics
