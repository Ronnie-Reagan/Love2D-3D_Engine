local modelModule = require("Source.ModelModule.Init")

local function assertTrue(value, message)
    if not value then
        error(message or "assertion failed", 2)
    end
end

local function runStlContract()
    local stl = table.concat({
        "solid t",
        "facet normal 0 0 1",
        "outer loop",
        "vertex 0 0 0",
        "vertex 1 0 0",
        "vertex 0 1 0",
        "endloop",
        "endfacet",
        "endsolid t"
    }, "\n")

    local assetId, err = modelModule.loadFromBytes(stl, "inline_test.stl", {
        targetExtent = 2.2
    })
    assertTrue(assetId ~= nil, "STL load failed: " .. tostring(err))

    local asset = modelModule.getAsset(assetId)
    assertTrue(type(asset) == "table", "asset missing after load")
    assertTrue(asset.sourceFormat == "stl", "source format mismatch")
    assertTrue(type(asset.model) == "table" and #asset.model.faces == 1, "triangle import failed")

    local okOrientation = modelModule.setOrientation(assetId, "plane", 5, 10, 15)
    assertTrue(okOrientation, "setOrientation failed")

    local snapshot = modelModule.buildModelSnapshot(assetId, "plane", 1.5)
    assertTrue(type(snapshot) == "table", "snapshot missing")
    assertTrue(snapshot.hash == asset.modelHash, "snapshot hash mismatch")

    local meta = modelModule.getBlobMeta("model", asset.modelHash)
    assertTrue(type(meta) == "table", "missing model blob meta")
    assertTrue(meta.chunkCount >= 1, "invalid model chunk count")

    local chunk = modelModule.getBlobChunk("model", asset.modelHash, 1)
    assertTrue(type(chunk) == "string" and chunk ~= "", "missing model chunk payload")

    local acceptedMeta, metaErr = modelModule.acceptBlobMeta({
        kind = "model",
        hash = meta.hash,
        rawBytes = meta.rawBytes,
        encodedBytes = meta.encodedBytes,
        chunkSize = meta.chunkSize,
        chunkCount = meta.chunkCount
    })
    assertTrue(acceptedMeta, "acceptBlobMeta failed: " .. tostring(metaErr))

    local completedKind, completedHash
    for i = 1, meta.chunkCount do
        local payload = modelModule.getBlobChunk("model", asset.modelHash, i)
        assertTrue(type(payload) == "string", "missing chunk payload " .. tostring(i))
        local kind, hash, chunkErr = modelModule.acceptBlobChunk({
            kind = "model",
            hash = meta.hash,
            chunkIndex = i,
            chunkData = payload
        })
        assertTrue(chunkErr == nil, "acceptBlobChunk failed: " .. tostring(chunkErr))
        if kind and hash then
            completedKind = kind
            completedHash = hash
        end
    end

    assertTrue(completedKind == "model", "completed blob kind mismatch")
    assertTrue(completedHash == asset.modelHash, "completed blob hash mismatch")

    local completed = modelModule.getCompletedBlob("model", asset.modelHash)
    assertTrue(type(completed) == "table", "completed blob missing")
    assertTrue(type(completed.raw) == "string" and completed.raw == stl, "completed blob payload mismatch")
end

local function run()
    runStlContract()
    print("ModelModule contract tests passed")
end

run()
