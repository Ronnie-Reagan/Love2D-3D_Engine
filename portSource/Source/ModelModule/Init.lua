local stlImporter = require("Source.ModelModule.Importers.StlImporter")
local gltfImporter = require("Source.ModelModule.Importers.GltfImporter")
local diagnostics = require("Source.ModelModule.Diagnostics")
local materialRuntime = require("Source.ModelModule.MaterialRuntime")
local paintRuntime = require("Source.ModelModule.PaintRuntime")
local blobSync = require("Source.ModelModule.NetBlobSync")
local persistence = require("Source.ModelModule.Persistence")
local gpuBridge = require("Source.ModelModule.RenderBridgeGpu")
local cpuBridge = require("Source.ModelModule.RenderBridgeCpu")

local ModelModule = {}

local state = {
    assetsById = {},
    assetIdsByHash = {},
    nextAssetId = 0,
    budgets = diagnostics.buildBudgets(),
    blobState = blobSync.newState(),
    outgoingBlobIndex = {},
    incomingBlobResults = {},
    paintTemplatesByHash = {},
    importers = { gltfImporter, stlImporter }
}

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

local function buildPaintOverlayFromPngBytes(rawPng, ownerId, role, modelHash, forcedHash)
    if type(rawPng) ~= "string" or rawPng == "" then
        return nil, "invalid paint payload"
    end
    if not (loveLib and loveLib.image and loveLib.graphics) then
        return nil, "paint overlay requires LOVE image API"
    end

    local imageSource = rawPng
    if loveLib.filesystem and loveLib.filesystem.newFileData then
        local okFileData, fileDataOrErr = pcall(function()
            return loveLib.filesystem.newFileData(rawPng, (forcedHash or "paint") .. ".png")
        end)
        if okFileData and fileDataOrErr then
            imageSource = fileDataOrErr
        end
    elseif loveLib.data and loveLib.data.newByteData then
        local okByteData, byteDataOrErr = pcall(function()
            return loveLib.data.newByteData(rawPng)
        end)
        if okByteData and byteDataOrErr then
            imageSource = byteDataOrErr
        end
    end

    local okImageData, imageDataOrErr = pcall(function()
        return loveLib.image.newImageData(imageSource)
    end)
    if not okImageData or not imageDataOrErr then
        return nil, tostring(imageDataOrErr or "failed to decode paint image")
    end

    local imageData = imageDataOrErr
    local okImage, imageOrErr = pcall(function()
        return loveLib.graphics.newImage(imageData)
    end)
    if not okImage or not imageOrErr then
        return nil, tostring(imageOrErr or "failed to create paint texture")
    end

    local image = imageOrErr
    if image.setFilter then
        image:setFilter("linear", "linear")
    end

    return {
        width = (imageData.getWidth and imageData:getWidth()) or 0,
        height = (imageData.getHeight and imageData:getHeight()) or 0,
        ownerId = ownerId,
        role = role,
        modelHash = modelHash,
        imageData = imageData,
        image = image,
        encodedPng = rawPng,
        hash = forcedHash or hashBytes(rawPng)
    }
end

local function clonePaintOverlay(overlay)
    if type(overlay) ~= "table" or not overlay.imageData then
        return nil, "paint overlay unavailable"
    end
    if not (loveLib and loveLib.graphics) then
        return nil, "paint overlay requires LOVE graphics API"
    end

    local okClone, imageDataOrErr = pcall(function()
        return overlay.imageData:clone()
    end)
    if not okClone or not imageDataOrErr then
        return nil, tostring(imageDataOrErr or "failed to clone paint image")
    end

    local imageData = imageDataOrErr
    local okImage, imageOrErr = pcall(function()
        return loveLib.graphics.newImage(imageData)
    end)
    if not okImage or not imageOrErr then
        return nil, tostring(imageOrErr or "failed to clone paint texture")
    end

    local image = imageOrErr
    if image.setFilter then
        image:setFilter("linear", "linear")
    end

    return {
        width = tonumber(overlay.width) or (imageData.getWidth and imageData:getWidth()) or 0,
        height = tonumber(overlay.height) or (imageData.getHeight and imageData:getHeight()) or 0,
        ownerId = overlay.ownerId,
        role = overlay.role,
        modelHash = overlay.modelHash,
        imageData = imageData,
        image = image,
        encodedPng = overlay.encodedPng,
        hash = overlay.hash
    }
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
    local hash = 2166136261
    local bitOps = _G.bit or _G.bit32
    if not bitOps then
        return string.format("len%08x", #raw)
    end
    for i = 1, #raw do
        hash = bitOps.bxor(hash, raw:byte(i))
        hash = (hash * 16777619) % 4294967296
    end
    if hash < 0 then
        hash = hash + 4294967296
    end
    return string.format("fnv%08x", hash)
end

local function encodeBase64(raw)
    if loveLib and loveLib.data and loveLib.data.encode then
        local ok, out = pcall(function()
            return loveLib.data.encode("string", "base64", raw)
        end)
        if ok and type(out) == "string" then
            return out
        end
    end
    return nil
end

local function selectImporter(sourceLabel, raw)
    for _, importer in ipairs(state.importers) do
        if importer.canHandle(sourceLabel, raw) then
            return importer
        end
    end
    return nil
end

local function cloneFaceList(faceList)
    local out = {}
    for i, face in ipairs(faceList or {}) do
        out[i] = { face[1], face[2], face[3] }
    end
    return out
end

local function normalizeModel(model, targetExtent)
    if not model or not model.vertices or #model.vertices == 0 then
        return model
    end
    local minX, minY, minZ = math.huge, math.huge, math.huge
    local maxX, maxY, maxZ = -math.huge, -math.huge, -math.huge
    for _, v in ipairs(model.vertices) do
        minX = math.min(minX, v[1])
        minY = math.min(minY, v[2])
        minZ = math.min(minZ, v[3])
        maxX = math.max(maxX, v[1])
        maxY = math.max(maxY, v[2])
        maxZ = math.max(maxZ, v[3])
    end
    local spanX = maxX - minX
    local spanY = maxY - minY
    local spanZ = maxZ - minZ
    local largest = math.max(spanX, spanY, spanZ)
    if largest <= 0 then
        largest = 1
    end
    local halfTarget = ((tonumber(targetExtent) or 2) * 0.5)
    local scale = halfTarget / (largest * 0.5)
    local cx = (minX + maxX) * 0.5
    local cy = (minY + maxY) * 0.5
    local cz = (minZ + maxZ) * 0.5

    local out = {
        vertices = {},
        faces = cloneFaceList(model.faces),
        isSolid = model.isSolid ~= false
    }
    for i, v in ipairs(model.vertices) do
        out.vertices[i] = {
            (v[1] - cx) * scale,
            (v[2] - cy) * scale,
            (v[3] - cz) * scale
        }
    end
    if type(model.vertexColors) == "table" then
        out.vertexColors = {}
        for i, c in ipairs(model.vertexColors) do
            out.vertexColors[i] = { c[1] or 1, c[2] or 1, c[3] or 1, c[4] or 1 }
        end
    end
    if type(model.vertexUVs) == "table" then
        out.vertexUVs = {}
        for i, uv in ipairs(model.vertexUVs) do
            out.vertexUVs[i] = { uv[1] or 0, uv[2] or 0 }
        end
    end
    if type(model.vertexUVs1) == "table" then
        out.vertexUVs1 = {}
        for i, uv in ipairs(model.vertexUVs1) do
            out.vertexUVs1[i] = { uv[1] or 0, uv[2] or 0 }
        end
    end
    if type(model.vertexNormals) == "table" then
        out.vertexNormals = {}
        for i, n in ipairs(model.vertexNormals) do
            out.vertexNormals[i] = { n[1] or 0, n[2] or 0, n[3] or 1 }
        end
    end
    if type(model.vertexTangents) == "table" then
        out.vertexTangents = {}
        for i, t in ipairs(model.vertexTangents) do
            out.vertexTangents[i] = { t[1] or 0, t[2] or 0, t[3] or 0, t[4] or 1 }
        end
    end
    if type(model.faceMaterials) == "table" then
        out.faceMaterials = {}
        for i, matIndex in ipairs(model.faceMaterials) do
            out.faceMaterials[i] = matIndex
        end
    end
    return out
end

local function computeBounds(model)
    if not model or not model.vertices or #model.vertices == 0 then
        return { minY = -1, maxY = 1, bottomDistance = 1, topDistance = 1 }
    end
    local minY, maxY = math.huge, -math.huge
    for _, v in ipairs(model.vertices) do
        minY = math.min(minY, tonumber(v[2]) or 0)
        maxY = math.max(maxY, tonumber(v[2]) or 0)
    end
    if minY > maxY then
        minY, maxY = -1, 1
    end
    return {
        minY = minY,
        maxY = maxY,
        bottomDistance = math.max(0.01, -minY),
        topDistance = math.max(0.01, maxY)
    }
end

local function registerAsset(asset, raw, sourceLabel, opts)
    local rawHash = (opts and opts.forcedHash) or hashBytes(raw)
    local existingId = state.assetIdsByHash[rawHash]
    if existingId then
        return existingId, state.assetsById[existingId]
    end

    state.nextAssetId = state.nextAssetId + 1
    local assetId = "asset-" .. tostring(state.nextAssetId)
    asset.id = assetId
    asset.sourceLabel = sourceLabel
    asset.modelHash = rawHash
    asset.rawBytes = raw
    asset.encodedRaw = encodeBase64(raw)
    asset.materials = materialRuntime.normalizeMaterials(asset.materials)
    asset.orientationByRole = asset.orientationByRole or {
        plane = { yaw = 0, pitch = 0, roll = 0 },
        walking = { yaw = 0, pitch = 0, roll = 0 }
    }
    asset.paintByRole = asset.paintByRole or {}
    asset.bounds = computeBounds(asset.model)

    state.assetsById[assetId] = asset
    state.assetIdsByHash[rawHash] = assetId
    local transfer = blobSync.prepareOutgoing(
        state.blobState,
        "model",
        rawHash,
        raw,
        { chunkSize = (opts and opts.chunkSize) or 720 }
    )
    state.outgoingBlobIndex["model|" .. rawHash] = transfer
    return assetId, asset
end

function ModelModule.setBudgets(overrides)
    state.budgets = diagnostics.buildBudgets(overrides)
end

function ModelModule.getBudgets()
    return diagnostics.buildBudgets(state.budgets)
end

function ModelModule.loadFromBytes(raw, sourceName, opts)
    opts = opts or {}
    local okSize, errSize = diagnostics.validateSourceSize(raw, state.budgets)
    if not okSize then
        return nil, errSize
    end

    local importer = selectImporter(sourceName, raw)
    if not importer then
        return nil, "unsupported model format"
    end

    local parsed, parseErr
    if importer.loadFromBytes then
        parsed, parseErr = importer.loadFromBytes(raw, sourceName, opts)
    else
        parsed, parseErr = importer.load(raw, opts)
    end
    if not parsed then
        return nil, parseErr
    end

    local model = normalizeModel(parsed.model or parsed.geometry, opts.targetExtent or 2.2)
    parsed.model = model
    parsed.geometry = parsed.geometry or {}
    parsed.geometry.vertices = model.vertices
    parsed.geometry.faces = model.faces

    local okAsset, assetErr = diagnostics.validateAsset(parsed, state.budgets)
    if not okAsset then
        return nil, assetErr
    end

    local assetId = registerAsset(parsed, raw, sourceName, opts)
    return assetId
end

function ModelModule.loadFromPath(path, opts)
    opts = opts or {}
    local importer = selectImporter(path, nil)
    if not importer then
        return nil, "unsupported model file extension"
    end
    if importer.loadFromPath then
        local parsed, err = importer.loadFromPath(path, opts)
        if not parsed then
            return nil, err
        end
        local raw
        if parsed.sourceFormat == "glb" or parsed.sourceFormat == "gltf" then
            local handle = io.open(path, "rb")
            if handle then
                raw = handle:read("*a")
                handle:close()
            end
        else
            local handle = io.open(path, "rb")
            if handle then
                raw = handle:read("*a")
                handle:close()
            end
        end
        if type(raw) ~= "string" then
            return nil, "failed to read source payload"
        end
        local okSize, errSize = diagnostics.validateSourceSize(raw, state.budgets)
        if not okSize then
            return nil, errSize
        end
        local model = normalizeModel(parsed.model or parsed.geometry, opts.targetExtent or 2.2)
        parsed.model = model
        parsed.geometry = parsed.geometry or {}
        parsed.geometry.vertices = model.vertices
        parsed.geometry.faces = model.faces
        local okAsset, assetErr = diagnostics.validateAsset(parsed, state.budgets)
        if not okAsset then
            return nil, assetErr
        end
        local assetId = registerAsset(parsed, raw, path, opts)
        return assetId
    end

    local raw, readErr
    local handle = io.open(path, "rb")
    if handle then
        raw = handle:read("*a")
        handle:close()
    else
        readErr = "could not open file"
    end
    if not raw then
        return nil, readErr or "failed to read model file"
    end
    return ModelModule.loadFromBytes(raw, path, opts)
end

function ModelModule.getAsset(assetId)
    return state.assetsById[assetId]
end

function ModelModule.getAssetByHash(modelHash)
    local assetId = state.assetIdsByHash[modelHash]
    if not assetId then
        return nil
    end
    return state.assetsById[assetId]
end

function ModelModule.applyToObject(obj, assetId, role)
    local asset = state.assetsById[assetId]
    if not asset then
        return false, "asset not found"
    end
    obj.model = asset.model
    obj.modelAssetId = assetId
    obj.modelHash = asset.modelHash
    obj.materials = asset.materials
    obj.images = asset.images
    obj.submeshes = asset.geometry and asset.geometry.submeshes or nil
    obj.faceMaterials = asset.geometry and asset.geometry.faceMaterials or nil
    obj.uvFlipV = false
    local orient = asset.orientationByRole and asset.orientationByRole[role or "plane"]
    if orient then
        obj.modelOrientation = {
            yaw = orient.yaw,
            pitch = orient.pitch,
            roll = orient.roll
        }
    end
    obj.paintOverlay = nil
    return true
end

function ModelModule.setOrientation(assetId, role, yawDeg, pitchDeg, rollDeg)
    local asset = state.assetsById[assetId]
    if not asset then
        return false, "asset not found"
    end
    role = (role == "walking") and "walking" or "plane"
    asset.orientationByRole = asset.orientationByRole or {}
    asset.orientationByRole[role] = {
        yaw = tonumber(yawDeg) or 0,
        pitch = tonumber(pitchDeg) or 0,
        roll = tonumber(rollDeg) or 0
    }
    return true
end

function ModelModule.beginPaintSession(assetId, role, ownerId, existingPaintHash)
    local asset = state.assetsById[assetId]
    if not asset then
        return nil, "asset not found"
    end
    role = (role == "walking") and "walking" or "plane"
    local existing = nil
    existingPaintHash = tostring(existingPaintHash or "")
    if existingPaintHash ~= "" then
        existing = ModelModule.getPaintOverlay(existingPaintHash, true)
        if not existing then
            return nil, "paint overlay not found"
        end
        existing.hash = existingPaintHash
        existing.role = role
        existing.modelHash = asset.modelHash
    end
    return paintRuntime.beginSession(assetId, role, ownerId, asset.modelHash, {
        width = 1024,
        height = 1024,
        maxHistory = 8,
        maxUndoBytes = 128 * 1024 * 1024
    }, existing)
end

function ModelModule.paintStroke(sessionId, strokeCmd)
    return paintRuntime.paintStroke(sessionId, strokeCmd)
end

function ModelModule.paintFill(sessionId, fillCmd)
    return paintRuntime.paintFill(sessionId, fillCmd)
end

function ModelModule.paintUndo(sessionId)
    return paintRuntime.paintUndo(sessionId)
end

function ModelModule.paintRedo(sessionId)
    return paintRuntime.paintRedo(sessionId)
end

function ModelModule.paintCommit(sessionId)
    local paintHash, overlayOrErr = paintRuntime.paintCommit(sessionId)
    if not paintHash then
        return nil, overlayOrErr
    end
    local session = paintRuntime.getSession(sessionId)
    if not session then
        return nil, "paint session not found"
    end

    local pngRaw = overlayOrErr.encodedPng
    if type(pngRaw) == "string" and pngRaw ~= "" then
        local transfer = blobSync.prepareOutgoing(
            state.blobState,
            "paint",
            paintHash,
            pngRaw,
            {
                chunkSize = 720,
                extra = {
                    ownerId = session.ownerId,
                    role = session.role,
                    modelHash = session.modelHash
                }
            }
        )
        state.outgoingBlobIndex["paint|" .. paintHash] = transfer
    end
    local templateOverlay = clonePaintOverlay(overlayOrErr)
    state.paintTemplatesByHash[paintHash] = templateOverlay or overlayOrErr
    return paintHash
end

function ModelModule.endPaintSession(sessionId)
    paintRuntime.endSession(sessionId)
end

function ModelModule.getPaintSession(sessionId)
    return paintRuntime.getSession(sessionId)
end

function ModelModule.getBlobMeta(kind, hash)
    local key = tostring(kind) .. "|" .. tostring(hash)
    local transfer = state.outgoingBlobIndex[key]
    if not transfer then
        return nil
    end
    return blobSync.buildMetaPacket(transfer)
end

function ModelModule.getBlobChunk(kind, hash, idx)
    local key = tostring(kind) .. "|" .. tostring(hash)
    local transfer = state.outgoingBlobIndex[key]
    if not transfer then
        return nil, "unknown outgoing blob"
    end
    return blobSync.getOutgoingChunk(transfer, idx)
end

function ModelModule.getBlobRaw(kind, hash)
    local key = tostring(kind) .. "|" .. tostring(hash)
    local transfer = state.outgoingBlobIndex[key]
    if transfer and type(transfer.raw) == "string" and transfer.raw ~= "" then
        return transfer.raw
    end
    local completed = state.incomingBlobResults[key]
    if completed and type(completed.raw) == "string" and completed.raw ~= "" then
        return completed.raw
    end
    return nil
end

function ModelModule.hasPaintOverlay(hash)
    return state.paintTemplatesByHash[tostring(hash)] ~= nil
end

function ModelModule.getPaintOverlay(hash, cloneTemplate)
    local template = state.paintTemplatesByHash[tostring(hash)]
    if not template then
        return nil
    end
    if cloneTemplate then
        local overlay = clonePaintOverlay(template)
        return overlay or template
    end
    return template
end

function ModelModule.attachPaintHashToAsset(assetId, role, paintHash)
    local asset = state.assetsById[assetId]
    if not asset then
        return false, "asset not found"
    end
    role = (role == "walking") and "walking" or "plane"
    paintHash = tostring(paintHash or "")
    if paintHash == "" then
        asset.paintByRole = asset.paintByRole or {}
        asset.paintByRole[role] = nil
        return true
    end

    local template = state.paintTemplatesByHash[paintHash]
    if not template then
        return false, "paint overlay not found"
    end

    local overlay, cloneErr = clonePaintOverlay(template)
    if not overlay then
        return false, cloneErr
    end
    overlay.hash = paintHash
    overlay.role = role
    overlay.modelHash = asset.modelHash
    return materialRuntime.attachPaintOverlay(asset, role, overlay)
end

function ModelModule.importPaintBlob(hash, rawPng, meta)
    hash = tostring(hash or "")
    if hash == "" then
        return nil, "missing paint hash"
    end

    meta = meta or {}
    local role = (meta.role == "walking") and "walking" or "plane"
    local overlay, err = buildPaintOverlayFromPngBytes(
        rawPng,
        meta.ownerId,
        role,
        meta.modelHash,
        hash
    )
    if not overlay then
        return nil, err
    end

    state.paintTemplatesByHash[hash] = overlay
    local transfer = blobSync.prepareOutgoing(
        state.blobState,
        "paint",
        hash,
        rawPng,
        {
            chunkSize = math.max(64, math.floor(tonumber(meta.chunkSize) or 720)),
            extra = {
                ownerId = meta.ownerId,
                role = role,
                modelHash = meta.modelHash
            }
        }
    )
    state.outgoingBlobIndex["paint|" .. hash] = transfer
    state.incomingBlobResults["paint|" .. hash] = {
        kind = "paint",
        hash = hash,
        raw = rawPng,
        meta = meta
    }
    return overlay
end

function ModelModule.acceptBlobMeta(metaPacket)
    return blobSync.acceptMeta(state.blobState, metaPacket)
end

function ModelModule.acceptBlobChunk(chunkPacket)
    if type(chunkPacket) ~= "table" then
        return nil, nil, "invalid chunk packet"
    end
    local result, err = blobSync.acceptChunk(
        state.blobState,
        chunkPacket.kind,
        chunkPacket.hash,
        chunkPacket.chunkIndex,
        chunkPacket.chunkData
    )
    if not result then
        if err then
            return nil, nil, err
        end
        return nil
    end

    state.incomingBlobResults[result.kind .. "|" .. result.hash] = result
    return result.kind, result.hash
end

function ModelModule.getCompletedBlob(kind, hash)
    return state.incomingBlobResults[tostring(kind) .. "|" .. tostring(hash)]
end

function ModelModule.getGpuBridge()
    return gpuBridge
end

function ModelModule.getCpuBridge()
    return cpuBridge
end

function ModelModule.buildModelSnapshot(assetId, role, scale)
    local asset = state.assetsById[assetId]
    return persistence.buildModelSnapshot(asset, role, scale)
end

function ModelModule.applyOrientationSnapshot(assetId, role, snapshot)
    local asset = state.assetsById[assetId]
    if not asset then
        return false
    end
    return persistence.applyOrientationSnapshot(asset, role, snapshot)
end

return ModelModule
