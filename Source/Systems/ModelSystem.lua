local M = {}

local function forward(fn)
	return function(...)
		return fn(...)
	end
end

function M.create(bindings)
	bindings = bindings or {}
	return {
		hashModelBytes = forward(assert(bindings.hashModelBytes, "hashModelBytes binding required")),
		encodeModelBytes = forward(assert(bindings.encodeModelBytes, "encodeModelBytes binding required")),
		decodeModelBytes = forward(assert(bindings.decodeModelBytes, "decodeModelBytes binding required")),
		readFileBytes = forward(assert(bindings.readFileBytes, "readFileBytes binding required")),
		getPathDirectory = forward(assert(bindings.getPathDirectory, "getPathDirectory binding required")),
		computeModelBounds = forward(assert(bindings.computeModelBounds, "computeModelBounds binding required")),
		getGroundClearance = forward(assert(bindings.getGroundClearance, "getGroundClearance binding required")),
		computeModelVisualOffsetY = forward(assert(bindings.computeModelVisualOffsetY, "computeModelVisualOffsetY binding required")),
		applyPlaneVisualToObject = forward(assert(bindings.applyPlaneVisualToObject, "applyPlaneVisualToObject binding required")),
		refreshAllPeerModels = forward(assert(bindings.refreshAllPeerModels, "refreshAllPeerModels binding required")),
		cacheModelEntry = forward(assert(bindings.cacheModelEntry, "cacheModelEntry binding required")),
		setActivePlayerModel = forward(assert(bindings.setActivePlayerModel, "setActivePlayerModel binding required")),
		loadPlayerModelFromRaw = forward(assert(bindings.loadPlayerModelFromRaw, "loadPlayerModelFromRaw binding required")),
		loadPlayerModelFromPath = forward(assert(bindings.loadPlayerModelFromPath, "loadPlayerModelFromPath binding required")),
		loadPlayerModelFromStl = forward(assert(bindings.loadPlayerModelFromStl, "loadPlayerModelFromStl binding required"))
	}
end

return M
