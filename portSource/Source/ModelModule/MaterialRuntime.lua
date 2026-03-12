local materialRuntime = {}

local function copyVec(v, size, fallback)
    local out = {}
    for i = 1, size do
        out[i] = (type(v) == "table" and tonumber(v[i])) or fallback[i]
    end
    return out
end

local defaultBaseColor = { 1, 1, 1, 1 }
local defaultEmissive = { 0, 0, 0 }

local function normalizeTextureRef(ref)
    if type(ref) ~= "table" then
        return nil
    end
    local imageIndex = tonumber(ref.imageIndex)
    if not imageIndex then
        return nil
    end
    local out = {
        imageIndex = math.max(1, math.floor(imageIndex)),
        texCoord = math.max(0, math.floor(tonumber(ref.texCoord) or 0))
    }
    if ref.scale ~= nil then
        out.scale = tonumber(ref.scale) or 1
    end
    if ref.strength ~= nil then
        out.strength = tonumber(ref.strength) or 1
    end
    if ref.textureIndex ~= nil then
        out.textureIndex = math.max(0, math.floor(tonumber(ref.textureIndex) or 0))
    end
    return out
end

function materialRuntime.normalizeMaterials(materials)
    local source = (type(materials) == "table") and materials or {}
    local out = {}
    for i = 1, #source do
        local m = source[i] or {}
        out[i] = {
            name = m.name or ("material_" .. tostring(i)),
            baseColorFactor = copyVec(m.baseColorFactor, 4, defaultBaseColor),
            metallicFactor = tonumber(m.metallicFactor) or 0,
            roughnessFactor = tonumber(m.roughnessFactor) or 1,
            workflow = (m.workflow == "specGloss") and "specGloss" or "metalRough",
            baseColorTexture = normalizeTextureRef(m.baseColorTexture),
            metallicRoughnessTexture = normalizeTextureRef(m.metallicRoughnessTexture),
            normalTexture = normalizeTextureRef(m.normalTexture),
            occlusionTexture = normalizeTextureRef(m.occlusionTexture),
            emissiveTexture = normalizeTextureRef(m.emissiveTexture),
            emissiveFactor = copyVec(m.emissiveFactor, 3, defaultEmissive),
            diffuseFactor = copyVec(m.diffuseFactor, 4, defaultBaseColor),
            specularFactor = copyVec(m.specularFactor, 3, { 1, 1, 1 }),
            glossinessFactor = tonumber(m.glossinessFactor) or 1,
            diffuseTexture = normalizeTextureRef(m.diffuseTexture),
            specularGlossinessTexture = normalizeTextureRef(m.specularGlossinessTexture),
            alphaMode = (m.alphaMode == "MASK" or m.alphaMode == "BLEND") and m.alphaMode or "OPAQUE",
            alphaCutoff = tonumber(m.alphaCutoff) or 0.5,
            doubleSided = m.doubleSided and true or false
        }
    end
    if #out == 0 then
        out[1] = {
            name = "default",
            baseColorFactor = { 1, 1, 1, 1 },
            metallicFactor = 0,
            roughnessFactor = 1,
            workflow = "metalRough",
            diffuseFactor = { 1, 1, 1, 1 },
            specularFactor = { 1, 1, 1 },
            glossinessFactor = 1,
            alphaMode = "OPAQUE",
            alphaCutoff = 0.5,
            doubleSided = false
        }
    end
    return out
end

function materialRuntime.attachPaintOverlay(asset, role, overlay)
    if type(asset) ~= "table" or type(role) ~= "string" then
        return false, "invalid paint attach target"
    end
    asset.paintByRole = asset.paintByRole or {}
    asset.paintByRole[role] = overlay
    return true
end

function materialRuntime.getPaintOverlay(asset, role)
    if type(asset) ~= "table" or type(asset.paintByRole) ~= "table" then
        return nil
    end
    return asset.paintByRole[role]
end

return materialRuntime
