local modelModule = require("Source.ModelModule.Init")

local function assertTrue(value, message)
    if not value then
        error(message or "assertion failed", 2)
    end
end

local function assertNear(actual, expected, epsilon, message)
    local diff = math.abs((actual or 0) - (expected or 0))
    if diff > (epsilon or 1e-5) then
        error((message or "assertion failed") ..
            string.format(" (actual=%.6f expected=%.6f)", actual or 0, expected or 0), 2)
    end
end

local function assertPixelNear(imageData, x, y, expected, message)
    local r, g, b, a = imageData:getPixel(x, y)
    assertNear(r, expected[1], 0.02, (message or "pixel mismatch") .. " [r]")
    assertNear(g, expected[2], 0.02, (message or "pixel mismatch") .. " [g]")
    assertNear(b, expected[3], 0.02, (message or "pixel mismatch") .. " [b]")
    assertNear(a, expected[4], 0.02, (message or "pixel mismatch") .. " [a]")
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

    local sessionId, sessionErr = modelModule.beginPaintSession(assetId, "plane", "contract-owner")
    assertTrue(sessionId ~= nil, "beginPaintSession failed: " .. tostring(sessionErr))

    local okFill, fillErr = modelModule.paintFill(sessionId, {
        color = { 0.9, 0.2, 0.1, 1.0 }
    })
    assertTrue(okFill, "paintFill failed: " .. tostring(fillErr))

    local paintHash, paintErr = modelModule.paintCommit(sessionId)
    assertTrue(type(paintHash) == "string" and paintHash ~= "", "paintCommit failed: " .. tostring(paintErr))

    local paintedSnapshot = modelModule.buildModelSnapshot(assetId, "plane", 1.5)
    assertTrue(paintedSnapshot.paintHash == nil, "model snapshot should not include asset-bound paint ownership")

    local committedOverlay = modelModule.getPaintOverlay(paintHash)
    assertTrue(type(committedOverlay) == "table" and committedOverlay.hash == paintHash, "paint overlay lookup failed")
    assertPixelNear(
        committedOverlay.imageData,
        12,
        12,
        { 0.9, 0.2, 0.1, 1.0 },
        "committed overlay should preserve filled color"
    )

    local appliedA = {}
    local appliedB = {}
    local okApplyA, applyErrA = modelModule.applyToObject(appliedA, assetId, "plane")
    local okApplyB, applyErrB = modelModule.applyToObject(appliedB, assetId, "walking")
    assertTrue(okApplyA, "applyToObject plane failed: " .. tostring(applyErrA))
    assertTrue(okApplyB, "applyToObject walking failed: " .. tostring(applyErrB))
    assertTrue(appliedA.paintOverlay == nil and appliedB.paintOverlay == nil,
        "shared asset application should not inject paint ownership")

    local assetAfterCommit = modelModule.getAsset(assetId)
    assertTrue(type(assetAfterCommit) == "table", "asset missing after paint commit")
    assertTrue(type(assetAfterCommit.paintByRole) ~= "table" or next(assetAfterCommit.paintByRole) == nil,
        "asset should not retain committed paint overlays")

    local sessionCloneId, sessionCloneErr = modelModule.beginPaintSession(assetId, "plane", "contract-owner-2", paintHash)
    assertTrue(sessionCloneId ~= nil, "beginPaintSession from existing paint failed: " .. tostring(sessionCloneErr))
    local okCloneFill, cloneFillErr = modelModule.paintFill(sessionCloneId, {
        color = { 0.1, 0.3, 0.9, 1.0 }
    })
    assertTrue(okCloneFill, "paintFill for cloned session failed: " .. tostring(cloneFillErr))

    local originalOverlayDuringEdit = modelModule.getPaintOverlay(paintHash)
    assertPixelNear(
        originalOverlayDuringEdit.imageData,
        12,
        12,
        { 0.9, 0.2, 0.1, 1.0 },
        "uncommitted session edits must not mutate the committed overlay"
    )
    modelModule.endPaintSession(sessionCloneId)

    local originalOverlayAfterCancel = modelModule.getPaintOverlay(paintHash)
    assertPixelNear(
        originalOverlayAfterCancel.imageData,
        12,
        12,
        { 0.9, 0.2, 0.1, 1.0 },
        "ending a paint session should discard uncommitted edits"
    )

    local paintMeta = modelModule.getBlobMeta("paint", paintHash)
    assertTrue(type(paintMeta) == "table", "missing paint blob meta")
    assertTrue(paintMeta.kind == "paint", "paint meta kind mismatch")
    assertTrue(paintMeta.chunkCount >= 1, "invalid paint chunk count")

    local acceptedPaintMeta, acceptedPaintMetaErr = modelModule.acceptBlobMeta({
        kind = "paint",
        hash = paintMeta.hash,
        rawBytes = paintMeta.rawBytes,
        encodedBytes = paintMeta.encodedBytes,
        chunkSize = paintMeta.chunkSize,
        chunkCount = paintMeta.chunkCount,
        role = "plane",
        modelHash = asset.modelHash
    })
    assertTrue(acceptedPaintMeta, "accept paint meta failed: " .. tostring(acceptedPaintMetaErr))

    local completedPaintKind, completedPaintHash
    for i = 1, paintMeta.chunkCount do
        local payload = modelModule.getBlobChunk("paint", paintHash, i)
        assertTrue(type(payload) == "string" and payload ~= "", "missing paint chunk payload " .. tostring(i))
        local kind, hash, chunkErr = modelModule.acceptBlobChunk({
            kind = "paint",
            hash = paintMeta.hash,
            chunkIndex = i,
            chunkData = payload
        })
        assertTrue(chunkErr == nil, "accept paint chunk failed: " .. tostring(chunkErr))
        if kind and hash then
            completedPaintKind = kind
            completedPaintHash = hash
        end
    end

    assertTrue(completedPaintKind == "paint", "completed paint kind mismatch")
    assertTrue(completedPaintHash == paintHash, "completed paint hash mismatch")

    local completedPaint = modelModule.getCompletedBlob("paint", paintHash)
    assertTrue(type(completedPaint) == "table" and type(completedPaint.raw) == "string", "completed paint blob missing")

    local importedOverlay, importErr = modelModule.importPaintBlob(paintHash, completedPaint.raw, {
        role = "plane",
        modelHash = asset.modelHash
    })
    assertTrue(importedOverlay ~= nil, "importPaintBlob failed: " .. tostring(importErr))
    assertTrue(modelModule.hasPaintOverlay(paintHash), "paint overlay should be cached after import")
    local cachedOverlay = modelModule.getPaintOverlay(paintHash)
    assertTrue(type(cachedOverlay) == "table" and cachedOverlay.hash == paintHash, "paint overlay cache missing")
end

local function run()
    runStlContract()
    print("ModelModule contract tests passed")
end

run()
