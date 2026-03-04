local M = {}

local function forward(fn)
	return function(...)
		return fn(...)
	end
end

function M.create(bindings)
	bindings = bindings or {}
	return {
		drawHudPlate = forward(assert(bindings.drawHudPlate, "drawHudPlate binding required")),
		drawArcLine = forward(assert(bindings.drawArcLine, "drawArcLine binding required")),
		resetHudCaches = forward(assert(bindings.resetHudCaches, "resetHudCaches binding required")),
		invalidateMapCache = forward(assert(bindings.invalidateMapCache, "invalidateMapCache binding required")),
		groundParamsSignature = forward(assert(bindings.groundParamsSignature, "groundParamsSignature binding required")),
		updateLogicalMapCamera = forward(assert(bindings.updateLogicalMapCamera, "updateLogicalMapCamera binding required")),
		ensureMapGroundImage = forward(assert(bindings.ensureMapGroundImage, "ensureMapGroundImage binding required")),
		projectWorldToScreen = forward(assert(bindings.projectWorldToScreen, "projectWorldToScreen binding required")),
		drawWorldPeerIndicators = forward(assert(bindings.drawWorldPeerIndicators, "drawWorldPeerIndicators binding required")),
		drawHud = forward(assert(bindings.drawHud, "drawHud binding required"))
	}
end

return M
