local q = require("quat")
local vector3 = require("vector3")
local love = require "love" -- avoids nil report from intellisense, safe to remove if causes issues (it should be OK)
local enet = require "enet"
local engine = require "engine"
local networking = require "networking"
local renderer = require "gpu_renderer"
local logger = require "logger"
local objectDefs = require "object"
local hostAddy = "ecosim.donreagan.ca:1988"
local relay = enet.host_create()
local relayServer = relay and relay:connect(hostAddy) or nil
local flightSimMode = false
local relative = true
local peers = {}
local event = nil
local objects = {}
local cubeModel = objectDefs.cubeModel
local cloudPuffModel = objectDefs.cloudPuffModel or cubeModel
local playerModel = cubeModel
local screen, camera, groundObject, triangleCount, frameImage, renderMode, gpuErrorLogged --, zBuffer
local useGpuRenderer = true
local perfElapsed, perfFrames, perfLoggedLowFps = 0, 0, false
local zBuffer = {}
local mouseSensitivity = 0.001
local invertLookY = false
local showCrosshair = true
local showDebugOverlay = true
local pauseTitleFont, pauseItemFont
local walkingPitchLimit = math.rad(89)
local viewState = {
	mode = "first_person",
	activeCamera = nil
}
local altLookState = {
	held = false,
	yaw = 0,
	pitch = 0,
	yawLimit = math.rad(85),
	pitchUpLimit = math.rad(55),
	pitchDownLimit = math.rad(40),
	camera = nil
}
local thirdPersonState = {
	offset = { 0, 2.0, -7.0 },
	minDistance = 1.25,
	collisionBuffer = 0.35,
	camera = nil
}
local mapState = {
	visible = false,
	mHeld = false,
	mUsedForZoom = false,
	zoomIndex = 2,
	zoomExtents = { 140, 400, 1000 },
	panel = { margin = 14, widthRatio = 0.24, maxSize = 280, minSize = 150 },
	logicalCamera = nil,
	gridImage = nil,
	gridImageSize = 0
}
local localPlayerObject = nil
local worldHalfExtent = 1000
local defaultGroundParams = {
	seed = 12345,
	tileSize = 2,
	gridCount = 128,
	baseHeight = 0,
	tileThickness = 0.005,
	roadCount = 6,
	roadDensity = 0.10,
	fieldCount = 10,
	fieldMinSize = 40,
	fieldMaxSize = 120,
	grassColor = { 0.20, 0.62, 0.22 },
	roadColor = { 0.10, 0.10, 0.10 },
	fieldColor = { 0.35, 0.45, 0.20 },
	grassVar = { 0.05, 0.10, 0.05 },
	roadVar = { 0.02, 0.02, 0.02 },
	fieldVar = { 0.04, 0.06, 0.04 }
}
local activeGroundParams = nil
local windState = {
	angle = 0,
	speed = 9,
	targetAngle = 0,
	targetSpeed = 9,
	retargetIn = 0
}
local cloudState = {
	groups = {},
	groupCount = 85,
	minAltitude = 80,
	maxAltitude = 180,
	spawnRadius = 1400,
	despawnRadius = 1900,
	minGroupSize = 14,
	maxGroupSize = 34,
	minPuffs = 5,
	maxPuffs = 10
}
local cloudNetState = {
	role = "standalone", -- standalone, pending, authority, follower
	connectedAt = 0,
	decisionDelay = 1.2,
	sawPeerOnJoin = false,
	authorityPeerId = nil,
	lastSnapshotSentAt = -math.huge,
	lastSnapshotReceivedAt = -math.huge,
	lastSnapshotRequestAt = -math.huge,
	snapshotInterval = 10.0,
	requestCooldown = 2.0,
	staleTimeout = 13.5
}
local groundNetState = {
	authorityPeerId = nil,
	lastSnapshotReceivedAt = -math.huge,
	lastSnapshotRequestAt = -math.huge,
	requestCooldown = 2.0
}

-- Correct imported player STL orientation to engine forward/up axes.
local playerModelRotationOffset = q.normalize(q.multiply(
	q.fromAxisAngle({ 0, 1, 0 }, -math.pi * 0.5), -- rotate left 90 deg
	q.fromAxisAngle({ 1, 0, 0 }, -math.pi * 0.5) -- rotate forward 90 deg
))

local function composePlayerModelRotation(baseRot)
	return q.normalize(q.multiply(baseRot or q.identity(), playerModelRotationOffset))
end

local pauseMenu = {
	active = false,
	tab = "game",
	selected = 1,
	itemBounds = {},
	tabBounds = {},
	helpScroll = 0,
	controlsScroll = 0,
	controlsSelection = 1,
	controlsSlot = 1,
	controlsRowBounds = {},
	controlsSlotBounds = {},
	listeningBinding = nil,
	dragRange = {
		active = false,
		itemIndex = nil,
		startX = 0,
		lastX = 0,
		totalDx = 0,
		moved = false
	},
	statusText = "",
	statusUntil = 0,
	confirmItemId = nil,
	confirmUntil = 0,
	items = {
		{ id = "resume", label = "Resume", kind = "action", help = "Close pause menu and continue playing." },
		{ id = "flight", label = "Flight Mode", kind = "toggle", help = "Toggle no-gravity flight movement mode." },
		{ id = "renderer", label = "Renderer", kind = "toggle", help = "Switch between GPU and CPU renderer paths." },
		{ id = "crosshair", label = "Crosshair", kind = "toggle", help = "Show or hide center crosshair dot." },
		{ id = "overlay", label = "Debug Overlay", kind = "toggle", help = "Toggle top-left help/perf text." },
		{ id = "speed", label = "Move Speed", kind = "range", min = 2, max = 30, step = 1, help = "Adjust player movement speed." },
		{ id = "fov", label = "Field of View", kind = "range", min = 60, max = 120, step = 5, help = "Adjust camera FOV in degrees." },
		{ id = "sensitivity", label = "Mouse Sensitivity", kind = "range", min = 0.0004, max = 0.004, step = 0.0002, help = "Adjust mouse look sensitivity." },
		{ id = "invertY", label = "Invert Look Y", kind = "toggle", help = "Invert vertical mouse look direction." },
		{ id = "reset", label = "Reset Camera", kind = "action", help = "Reset camera position and orientation." },
		{ id = "reconnect", label = "Reconnect Relay", kind = "action", help = "Reconnect to multiplayer relay server." },
		{ id = "quit", label = "Quit Game", kind = "action", confirm = true, help = "Exit the game client." }
	}
}

local function bindKey(key, modifiers)
	return { kind = "key", key = key, modifiers = modifiers }
end

local function bindMouseButton(button, modifiers)
	return { kind = "mouse_button", button = button, modifiers = modifiers }
end

local function bindMouseAxis(axis, direction, modifiers)
	return { kind = "mouse_axis", axis = axis, direction = direction, modifiers = modifiers }
end

local function bindMouseWheel(direction, modifiers)
	return { kind = "mouse_wheel", direction = direction, modifiers = modifiers }
end

local function cloneBinding(binding)
	if not binding then
		return nil
	end
	local clone = {}
	for key, value in pairs(binding) do
		if key == "modifiers" and value then
			clone.modifiers = {
				alt = value.alt and true or nil,
				ctrl = value.ctrl and true or nil,
				shift = value.shift and true or nil
			}
		else
			clone[key] = value
		end
	end
	return clone
end

local function buildDefaultControlActions()
	return {
		{ id = "flight_pitch_down", mode = "flight", label = "Pitch Forward / Nose Down", description = "Pitch the nose downward in flight mode.", bindings = { bindKey("w"), bindMouseAxis("y", -1) } },
		{ id = "flight_pitch_up", mode = "flight", label = "Pitch Back / Nose Up", description = "Pitch the nose upward in flight mode.", bindings = { bindKey("s"), bindMouseAxis("y", 1) } },
		{ id = "flight_roll_left", mode = "flight", label = "Bank / Roll Left", description = "Roll left around the forward axis.", bindings = { bindKey("a"), bindMouseAxis("x", -1) } },
		{ id = "flight_roll_right", mode = "flight", label = "Bank / Roll Right", description = "Roll right around the forward axis.", bindings = { bindKey("d"), bindMouseAxis("x", 1) } },
		{ id = "flight_yaw_left", mode = "flight", label = "Yaw / Turn Left", description = "Yaw the craft left.", bindings = { bindKey("q") } },
		{ id = "flight_yaw_right", mode = "flight", label = "Yaw / Turn Right", description = "Yaw the craft right.", bindings = { bindKey("e") } },
		{ id = "flight_throttle_down", mode = "flight", label = "Throttle Down", description = "Reduce throttle while flying.", bindings = { bindKey("down"), bindMouseWheel(-1) } },
		{ id = "flight_throttle_up", mode = "flight", label = "Throttle Up", description = "Increase throttle while flying.", bindings = { bindKey("up"), bindMouseWheel(1) } },
		{ id = "flight_fire_projectile", mode = "flight", label = "Fire Projectile", description = "Trigger the primary fire action.", bindings = { bindMouseButton(1) } },
		{ id = "flight_zoom_in", mode = "flight", label = "Zoom In", description = "Hold to zoom in while flying.", bindings = { bindMouseButton(2) } },
		{ id = "flight_afterburner", mode = "flight", label = "After Burner", description = "Hold for temporary extra top speed.", bindings = { bindKey("lshift"), bindMouseButton(4) } },
		{ id = "flight_air_brakes", mode = "flight", label = "Air Brakes", description = "Hold to rapidly bleed off throttle.", bindings = { bindKey("space") } },
		{ id = "walk_look_down", mode = "walking", label = "Look Down", description = "Move the camera down in walking mode.", bindings = { bindMouseAxis("y", 1) } },
		{ id = "walk_look_up", mode = "walking", label = "Look Up", description = "Move the camera up in walking mode.", bindings = { bindMouseAxis("y", -1) } },
		{ id = "walk_look_left", mode = "walking", label = "Look Left", description = "Move the camera left in walking mode.", bindings = { bindMouseAxis("x", -1) } },
		{ id = "walk_look_right", mode = "walking", label = "Look Right", description = "Move the camera right in walking mode.", bindings = { bindMouseAxis("x", 1) } },
		{ id = "walk_sprint", mode = "walking", label = "Sprint", description = "Increase walking speed while held.", bindings = { bindKey("lshift") } },
		{ id = "walk_jump", mode = "walking", label = "Jump", description = "Jump while grounded.", bindings = { bindKey("space") } },
		{ id = "walk_forward", mode = "walking", label = "Walk Forwards", description = "Move forwards on the ground.", bindings = { bindKey("w") } },
		{ id = "walk_backward", mode = "walking", label = "Walk Backwards", description = "Move backwards on the ground.", bindings = { bindKey("s") } },
		{ id = "walk_strafe_left", mode = "walking", label = "Strafe Left", description = "Strafe left on the ground.", bindings = { bindKey("a") } },
		{ id = "walk_strafe_right", mode = "walking", label = "Strafe Right", description = "Strafe right on the ground.", bindings = { bindKey("d") } }
	}
end

local controlActions = buildDefaultControlActions()
local controlActionById = {}

local function rebuildControlActionIndex()
	controlActionById = {}
	for i, action in ipairs(controlActions) do
		controlActionById[action.id] = i
	end
end

rebuildControlActionIndex()

local function resetControlBindingsToDefaults()
	controlActions = buildDefaultControlActions()
	rebuildControlActionIndex()
	pauseMenu.controlsSelection = math.max(1, math.min(pauseMenu.controlsSelection, #controlActions))
	pauseMenu.controlsSlot = math.max(1, math.min(pauseMenu.controlsSlot, 2))
	pauseMenu.controlsScroll = 0
	pauseMenu.controlsRowBounds = {}
	pauseMenu.controlsSlotBounds = {}
	pauseMenu.listeningBinding = nil
end

local function getControlAction(actionId)
	local index = controlActionById[actionId]
	if not index then
		return nil
	end
	return controlActions[index], index
end

local keyLabelMap = {
	up = "Up Arrow",
	down = "Down Arrow",
	left = "Left Arrow",
	right = "Right Arrow",
	space = "Spacebar",
	lshift = "Left Shift",
	rshift = "Right Shift",
	lalt = "Left Alt",
	ralt = "Right Alt",
	lctrl = "Left Ctrl",
	rctrl = "Right Ctrl",
	kpenter = "Numpad Enter",
	["return"] = "Enter"
}

local mouseButtonLabelMap = {
	[1] = "Left Mouse Button",
	[2] = "Right Mouse Button",
	[3] = "Middle Mouse Button",
	[4] = "Mouse X1",
	[5] = "Mouse X2"
}

local function formatKeyLabel(key)
	if not key or key == "" then
		return "Unbound"
	end
	if keyLabelMap[key] then
		return keyLabelMap[key]
	end
	if #key == 1 then
		return string.upper(key)
	end

	local pretty = key:gsub("_", " ")
	return pretty:gsub("(%a)([%w_']*)", function(first, rest)
		return string.upper(first) .. rest
	end)
end

local function getCurrentModifiers()
	return {
		alt = love.keyboard.isDown("lalt", "ralt"),
		ctrl = love.keyboard.isDown("lctrl", "rctrl"),
		shift = love.keyboard.isDown("lshift", "rshift")
	}
end

local function extractCaptureModifiers(excludeKey)
	local mods = getCurrentModifiers()
	if excludeKey == "lalt" or excludeKey == "ralt" then
		mods.alt = false
	elseif excludeKey == "lctrl" or excludeKey == "rctrl" then
		mods.ctrl = false
	elseif excludeKey == "lshift" or excludeKey == "rshift" then
		mods.shift = false
	end

	if mods.alt or mods.ctrl or mods.shift then
		return {
			alt = mods.alt or nil,
			ctrl = mods.ctrl or nil,
			shift = mods.shift or nil
		}
	end
	return nil
end

local function bindingModifiersMatch(required, current, strict)
	required = required or {}
	current = current or { alt = false, ctrl = false, shift = false }

	if required.alt and not current.alt then
		return false
	end
	if required.ctrl and not current.ctrl then
		return false
	end
	if required.shift and not current.shift then
		return false
	end

	if strict then
		if current.alt and not required.alt then
			return false
		end
	end

	return true
end

local function formatBinding(binding)
	if not binding then
		return "Unbound"
	end

	local text = "Unbound"
	if binding.kind == "key" then
		text = formatKeyLabel(binding.key)
	elseif binding.kind == "mouse_button" then
		text = mouseButtonLabelMap[binding.button] or ("Mouse Button " .. tostring(binding.button))
	elseif binding.kind == "mouse_axis" then
		if binding.axis == "x" then
			text = (binding.direction or 0) < 0 and "Mouse Left" or "Mouse Right"
		else
			text = (binding.direction or 0) < 0 and "Mouse Up" or "Mouse Down"
		end
	elseif binding.kind == "mouse_wheel" then
		text = (binding.direction or 0) < 0 and "Mouse Wheel Down" or "Mouse Wheel Up"
	end

	if binding.modifiers then
		local prefix = {}
		if binding.modifiers.ctrl then
			table.insert(prefix, "Ctrl")
		end
		if binding.modifiers.alt then
			table.insert(prefix, "Alt")
		end
		if binding.modifiers.shift then
			table.insert(prefix, "Shift")
		end
		if #prefix > 0 then
			text = table.concat(prefix, " + ") .. " + " .. text
		end
	end

	return text
end

local function isActionDown(actionId)
	local action = getControlAction(actionId)
	if not action then
		return false
	end

	local modifiers = getCurrentModifiers()
	for _, binding in ipairs(action.bindings or {}) do
		if binding.kind == "key" and binding.key and love.keyboard.isDown(binding.key) then
			if bindingModifiersMatch(binding.modifiers, modifiers, false) then
				return true
			end
		elseif binding.kind == "mouse_button" and binding.button and love.mouse.isDown(binding.button) then
			if bindingModifiersMatch(binding.modifiers, modifiers, false) then
				return true
			end
		end
	end
	return false
end

local function getActionMouseAxisValue(actionId, dx, dy, modifiers)
	local action = getControlAction(actionId)
	if not action then
		return 0
	end

	local total = 0
	for _, binding in ipairs(action.bindings or {}) do
		if binding.kind == "mouse_axis" and bindingModifiersMatch(binding.modifiers, modifiers, true) then
			local component = (binding.axis == "x") and dx or dy
			local direction = binding.direction or 0
			if direction < 0 and component < 0 then
				total = total + math.abs(component)
			elseif direction > 0 and component > 0 then
				total = total + math.abs(component)
			end
		end
	end

	return total
end

local function actionTriggeredByKey(actionId, key, modifiers)
	local action = getControlAction(actionId)
	if not action then
		return false
	end

	for _, binding in ipairs(action.bindings or {}) do
		if binding.kind == "key" and binding.key == key and bindingModifiersMatch(binding.modifiers, modifiers, false) then
			return true
		end
	end
	return false
end

local function actionTriggeredByMouseButton(actionId, button, modifiers)
	local action = getControlAction(actionId)
	if not action then
		return false
	end

	for _, binding in ipairs(action.bindings or {}) do
		if binding.kind == "mouse_button" and binding.button == button and bindingModifiersMatch(binding.modifiers, modifiers, false) then
			return true
		end
	end
	return false
end

local function actionTriggeredByWheel(actionId, wheelY, modifiers)
	local action = getControlAction(actionId)
	if not action then
		return false
	end

	for _, binding in ipairs(action.bindings or {}) do
		if binding.kind == "mouse_wheel" and bindingModifiersMatch(binding.modifiers, modifiers, false) then
			if (binding.direction or 0) < 0 and wheelY < 0 then
				return true
			end
			if (binding.direction or 0) > 0 and wheelY > 0 then
				return true
			end
		end
	end
	return false
end

local function getPauseHelpSections()
	return {
		{
			title = "Flight Mode",
			items = {
				{ keys = "W / S", text = "Pitch nose down or up." },
				{ keys = "A / D", text = "Bank left or right." },
				{ keys = "Mouse X / Q / E", text = "Yaw left or right for quick heading changes." },
				{ keys = "Mouse Y", text = "Pitch nose up/down." },
				{ keys = "Up / Down", text = "Throttle up or down." },
				{ keys = "Space / Shift", text = "Air brakes and afterburner." }
			}
		},
		{
			title = "Walking Mode",
			items = {
				{ keys = "W A S D", text = "Move and strafe in ground mode." },
				{ keys = "Mouse X / Y", text = "FPS look (yaw + clamped pitch)." },
				{ keys = "Space", text = "Jump while grounded." },
				{ keys = "Shift", text = "Sprint while held." }
			}
		},
		{
			title = "Cameras and Map",
			items = {
				{ keys = "C", text = "Cycle first-person and third-person camera modes." },
				{ keys = "Hold Left Alt", text = "Temporary alt-look in first-person with head limits." },
				{ keys = "Tap M", text = "Toggle the top-right mini map overlay." },
				{ keys = "Hold M + Up/Down", text = "Change mini map zoom level without toggling map visibility." }
			}
		},
		{
			title = "Pause Menu",
			items = {
				{ keys = "Tab / H", text = "Cycle Game, Help, and Controls tabs." },
				{ keys = "W/S or Up/Down", text = "Move selection in Game or Controls tab." },
				{ keys = "A/D or Left/Right", text = "Adjust values or select binding slot." },
				{ keys = "Enter", text = "Activate item or start binding capture in Controls." },
				{ keys = "Mouse Wheel", text = "Adjust ranges (Game) or scroll (Help/Controls)." },
				{ keys = "Esc", text = "Resume game, or cancel bind capture first." }
			}
		}
	}
end

--[[
Notes:

positions are {x = left/right(width), y = up/down(height), z = in/out(depth)} IN CAMERA SPACE!!
]]

local function clamp(value, minValue, maxValue)
	if value < minValue then
		return minValue
	end
	if value > maxValue then
		return maxValue
	end
	return value
end

local function randomRange(minValue, maxValue)
	return minValue + (maxValue - minValue) * math.random()
end

local function wrapAngle(angle)
	local twoPi = math.pi * 2
	if twoPi == 0 then
		return angle
	end
	angle = angle % twoPi
	if angle > math.pi then
		angle = angle - twoPi
	end
	return angle
end

local function shortestAngleDelta(currentAngle, targetAngle)
	return wrapAngle(targetAngle - currentAngle)
end

local function composeWalkingRotation(yaw, pitch)
	local yawQuat = q.fromAxisAngle({ 0, 1, 0 }, yaw)
	local right = q.rotateVector(yawQuat, { 1, 0, 0 })
	local pitchQuat = q.fromAxisAngle(right, pitch)
	return q.normalize(q.multiply(pitchQuat, yawQuat))
end

local function syncWalkingLookFromRotation()
	if not camera then
		return
	end

	local forward = q.rotateVector(camera.rot, { 0, 0, 1 })
	local flatMag = math.sqrt(forward[1] * forward[1] + forward[3] * forward[3])
	local pitch = math.atan2(forward[2], math.max(flatMag, 1e-6))
	local yaw = math.atan2(forward[1], forward[3])

	camera.walkYaw = wrapAngle(yaw)
	camera.walkPitch = clamp(pitch, -walkingPitchLimit, walkingPitchLimit)
	camera.rot = composeWalkingRotation(camera.walkYaw, camera.walkPitch)
end

local function resetAltLookState()
	altLookState.held = false
	altLookState.yaw = 0
	altLookState.pitch = 0
end

local function applyAltLookMouse(dx, dy)
	if not camera then
		return
	end

	local pitchMult = camera.walkMousePitchMultiplier or 2.2
	local yawMult = camera.walkMouseYawMultiplier or 2.2
	local yawDelta = dx * mouseSensitivity * yawMult
	local pitchDelta = (-dy) * mouseSensitivity * pitchMult
	if invertLookY then
		pitchDelta = -pitchDelta
	end

	altLookState.yaw = clamp(altLookState.yaw + yawDelta, -altLookState.yawLimit, altLookState.yawLimit)
	altLookState.pitch = clamp(
		altLookState.pitch + pitchDelta,
		-altLookState.pitchDownLimit,
		altLookState.pitchUpLimit
	)
end

local function buildAltLookCamera(baseCamera)
	if not baseCamera then
		return nil
	end

	local rot = baseCamera.rot or q.identity()
	if altLookState.yaw ~= 0 then
		local up = q.rotateVector(rot, { 0, 1, 0 })
		rot = q.normalize(q.multiply(q.fromAxisAngle(up, altLookState.yaw), rot))
	end
	if altLookState.pitch ~= 0 then
		local right = q.rotateVector(rot, { 1, 0, 0 })
		rot = q.normalize(q.multiply(q.fromAxisAngle(right, altLookState.pitch), rot))
	end

	altLookState.camera = altLookState.camera or {
		pos = { 0, 0, 0 },
		rot = q.identity(),
		fov = math.rad(90)
	}
	local altCam = altLookState.camera
	altCam.pos[1] = baseCamera.pos[1]
	altCam.pos[2] = baseCamera.pos[2]
	altCam.pos[3] = baseCamera.pos[3]
	altCam.rot = rot
	altCam.fov = baseCamera.fov
	return altCam
end

local function getYawFromRotation(rotation)
	local rot = rotation or q.identity()
	local forward = q.rotateVector(rot, { 0, 0, 1 })
	return math.atan2(forward[1], forward[3])
end

local function segmentAabbIntersectionT(startPos, endPos, obj)
	if not startPos or not endPos or not obj or not obj.pos or not obj.halfSize then
		return nil
	end

	local minB = {
		obj.pos[1] - obj.halfSize.x,
		obj.pos[2] - obj.halfSize.y,
		obj.pos[3] - obj.halfSize.z
	}
	local maxB = {
		obj.pos[1] + obj.halfSize.x,
		obj.pos[2] + obj.halfSize.y,
		obj.pos[3] + obj.halfSize.z
	}
	local dir = {
		endPos[1] - startPos[1],
		endPos[2] - startPos[2],
		endPos[3] - startPos[3]
	}
	local tMin = 0
	local tMax = 1

	for axis = 1, 3 do
		local origin = startPos[axis]
		local delta = dir[axis]
		local slabMin = minB[axis]
		local slabMax = maxB[axis]

		if math.abs(delta) < 1e-8 then
			if origin < slabMin or origin > slabMax then
				return nil
			end
		else
			local invDelta = 1 / delta
			local t1 = (slabMin - origin) * invDelta
			local t2 = (slabMax - origin) * invDelta
			if t1 > t2 then
				t1, t2 = t2, t1
			end

			tMin = math.max(tMin, t1)
			tMax = math.min(tMax, t2)
			if tMin > tMax then
				return nil
			end
		end
	end

	return tMin
end

local function buildThirdPersonCamera(baseCamera, objectList)
	if not baseCamera then
		return nil
	end

	local forward, right, up = engine.getCameraBasis(baseCamera, q, vector3)
	local target = baseCamera.pos
	local desiredPos = {
		target[1] + right[1] * thirdPersonState.offset[1] + up[1] * thirdPersonState.offset[2] +
		forward[1] * thirdPersonState.offset[3],
		target[2] + right[2] * thirdPersonState.offset[1] + up[2] * thirdPersonState.offset[2] +
		forward[2] * thirdPersonState.offset[3],
		target[3] + right[3] * thirdPersonState.offset[1] + up[3] * thirdPersonState.offset[2] +
		forward[3] * thirdPersonState.offset[3]
	}

	local rawDir = {
		desiredPos[1] - target[1],
		desiredPos[2] - target[2],
		desiredPos[3] - target[3]
	}
	local rawDist = math.sqrt(rawDir[1] * rawDir[1] + rawDir[2] * rawDir[2] + rawDir[3] * rawDir[3])
	local dir = { 0, 0, -1 }
	if rawDist > 1e-6 then
		dir[1] = rawDir[1] / rawDist
		dir[2] = rawDir[2] / rawDist
		dir[3] = rawDir[3] / rawDist
	end

	local nearestT = 1
	local checkRadius = rawDist + thirdPersonState.collisionBuffer + 1
	for _, obj in ipairs(objectList or {}) do
		if obj and obj ~= localPlayerObject and obj.isSolid and obj.pos and obj.halfSize then
			local nearX = math.abs(obj.pos[1] - target[1]) <= (obj.halfSize.x + checkRadius)
			local nearY = math.abs(obj.pos[2] - target[2]) <= (obj.halfSize.y + checkRadius)
			local nearZ = math.abs(obj.pos[3] - target[3]) <= (obj.halfSize.z + checkRadius)
			if nearX and nearY and nearZ then
				local hitT = segmentAabbIntersectionT(target, desiredPos, obj)
				if hitT and hitT >= 0 and hitT <= nearestT then
					nearestT = hitT
				end
			end
		end
	end

	local dist = rawDist
	if nearestT < 1 then
		dist = math.max(thirdPersonState.minDistance, rawDist * nearestT - thirdPersonState.collisionBuffer)
	else
		dist = math.max(dist, thirdPersonState.minDistance)
	end

	thirdPersonState.camera = thirdPersonState.camera or {
		pos = { 0, 0, 0 },
		rot = q.identity(),
		fov = math.rad(90)
	}
	local cam = thirdPersonState.camera
	cam.pos[1] = target[1] + dir[1] * dist
	cam.pos[2] = target[2] + dir[2] * dist
	cam.pos[3] = target[3] + dir[3] * dist
	cam.rot = baseCamera.rot
	cam.fov = baseCamera.fov
	return cam
end

local function resolveActiveRenderCamera()
	if not camera then
		viewState.activeCamera = nil
		return nil
	end

	local activeCam = camera
	if viewState.mode == "third_person" then
		activeCam = buildThirdPersonCamera(camera, objects) or camera
	elseif viewState.mode == "first_person" and altLookState.held then
		activeCam = buildAltLookCamera(camera) or camera
	end

	viewState.activeCamera = activeCam
	return activeCam
end

local function shouldRenderObjectForView(obj)
	if not obj then
		return false
	end
	if obj.isLocalPlayer and viewState.mode == "first_person" then
		return false
	end
	return true
end

local function buildRenderObjectList()
	local renderObjects = {}
	for _, obj in ipairs(objects) do
		if shouldRenderObjectForView(obj) then
			renderObjects[#renderObjects + 1] = obj
		end
	end
	return renderObjects
end

local function positiveMod(value, modulus)
	if modulus == 0 then
		return 0
	end
	local result = value % modulus
	if result < 0 then
		result = result + modulus
	end
	return result
end

local function rotateWorldDeltaToMap(dx, dz, yaw)
	local cosYaw = math.cos(yaw)
	local sinYaw = math.sin(yaw)
	local mapX = cosYaw * dx - sinYaw * dz
	local mapZ = sinYaw * dx + cosYaw * dz
	return mapX, mapZ
end

local function ensureMapGridImage()
	if mapState.gridImage then
		return mapState.gridImage
	end

	local size = 128
	local data = love.image.newImageData(size, size)
	for y = 0, size - 1 do
		for x = 0, size - 1 do
			local major = (x % 32 == 0) or (y % 32 == 0)
			local minor = (x % 16 == 0) or (y % 16 == 0)
			local tone = 0.12
			if minor then
				tone = 0.16
			end
			if major then
				tone = 0.2
			end
			data:setPixel(x, y, tone, tone + 0.04, tone, 1)
		end
	end

	mapState.gridImage = love.graphics.newImage(data)
	mapState.gridImage:setFilter("nearest", "nearest")
	mapState.gridImageSize = size
	return mapState.gridImage
end

local function updateLogicalMapCamera()
	if not camera then
		mapState.logicalCamera = nil
		return nil
	end

	mapState.logicalCamera = mapState.logicalCamera or {
		pos = { 0, 0, 0 },
		heading = 0,
		extent = mapState.zoomExtents[2] or 400
	}

	local mapCam = mapState.logicalCamera
	mapCam.pos[1] = camera.pos[1]
	mapCam.pos[2] = camera.pos[2]
	mapCam.pos[3] = camera.pos[3]
	mapCam.heading = getYawFromRotation(camera.rot)
	mapCam.extent = mapState.zoomExtents[mapState.zoomIndex] or (worldHalfExtent or 1000)
	return mapCam
end

local function syncLocalPlayerObject()
	if not localPlayerObject or not camera then
		return
	end
	localPlayerObject.pos[1] = camera.pos[1]
	localPlayerObject.pos[2] = camera.pos[2]
	localPlayerObject.pos[3] = camera.pos[3]
	localPlayerObject.rot = composePlayerModelRotation(camera.rot)
end

local function loadPlayerModelFromStl(path)
	local stlModel, loadErr = engine.loadSTL(path)
	if not stlModel then
		logger.log("Player STL load failed, using cube fallback: " .. tostring(loadErr))
		playerModel = cubeModel
		return
	end

	playerModel = engine.normalizeModel(stlModel, 2.2)
	logger.log(string.format(
		"Loaded player STL '%s' (%d verts, %d faces).",
		path,
		#(playerModel.vertices or {}),
		#(playerModel.faces or {})
	))
end

local function pickNextWindTarget()
	windState.targetAngle = randomRange(0, math.pi * 2)
	windState.targetSpeed = randomRange(4.5, 12.0)
	windState.retargetIn = randomRange(7.0, 15.0)
end

local function updateWind(dt)
	windState.retargetIn = windState.retargetIn - dt
	if windState.retargetIn <= 0 then
		pickNextWindTarget()
	end

	local blend = clamp(dt * 0.6, 0, 1)
	windState.angle = wrapAngle(windState.angle + shortestAngleDelta(windState.angle, windState.targetAngle) * blend)
	windState.speed = windState.speed + (windState.targetSpeed - windState.speed) * blend
end

local function getWindVectorXZ()
	return math.cos(windState.angle) * windState.speed, math.sin(windState.angle) * windState.speed
end

local function getWindVector3()
	local windX, windZ = getWindVectorXZ()
	return { windX, 0, windZ }
end

local function clearCloudObjects()
	for i = #objects, 1, -1 do
		if objects[i].isCloud then
			table.remove(objects, i)
		end
	end
end

local function chooseCloudCenter(spawnUpwind)
	local refX = (camera and camera.pos and camera.pos[1]) or 0
	local refZ = (camera and camera.pos and camera.pos[3]) or 0
	local angle
	if spawnUpwind then
		angle = wrapAngle(windState.angle + math.pi + randomRange(-0.8, 0.8))
	else
		angle = randomRange(0, math.pi * 2)
	end

	local distance = randomRange(cloudState.spawnRadius * 0.45, cloudState.spawnRadius)
	local x = refX + math.cos(angle) * distance
	local y = randomRange(cloudState.minAltitude, cloudState.maxAltitude)
	local z = refZ + math.sin(angle) * distance
	return x, y, z
end

local function updateCloudGroupVisuals(group, now)
	for _, puff in ipairs(group.puffs) do
		local bob = math.sin(now * 0.2 + puff.cloudBobPhase) * puff.cloudBobAmplitude
		puff.pos[1] = group.center[1] + puff.cloudOffset[1]
		puff.pos[2] = group.center[2] + puff.cloudOffset[2] + bob
		puff.pos[3] = group.center[3] + puff.cloudOffset[3]
	end
end

local function randomizeCloudGroup(group, spawnUpwind)
	local cx, cy, cz = chooseCloudCenter(spawnUpwind)
	group.center[1], group.center[2], group.center[3] = cx, cy, cz
	group.radius = randomRange(cloudState.minGroupSize, cloudState.maxGroupSize)
	group.windDrift = randomRange(0.85, 1.25)

	local maxOffset = group.radius
	for _, puff in ipairs(group.puffs) do
		local angle = randomRange(0, math.pi * 2)
		local radial = randomRange(maxOffset * 0.2, maxOffset)
		puff.cloudOffset[1] = math.cos(angle) * radial
		puff.cloudOffset[2] = randomRange(-maxOffset * 0.2, maxOffset * 0.25)
		puff.cloudOffset[3] = math.sin(angle) * radial
		puff.cloudBobPhase = randomRange(0, math.pi * 2)
		puff.cloudBobAmplitude = randomRange(0.2, 1.2)

		local puffSize = randomRange(group.radius * 0.22, group.radius * 0.48)
		puff.scale[1] = puffSize
		puff.scale[2] = puffSize * randomRange(0.55, 0.78)
		puff.scale[3] = puffSize
		puff.rot = q.fromAxisAngle({ 0, 1, 0 }, randomRange(0, math.pi * 2))

		local shade = randomRange(0.88, 0.99)
		puff.color[1] = shade
		puff.color[2] = shade
		puff.color[3] = math.min(1.0, shade + randomRange(0.01, 0.03))
		puff.color[4] = randomRange(0.17, 0.35)
	end

	updateCloudGroupVisuals(group, love.timer.getTime())
end

local function spawnCloudField(groupCountOverride)
	clearCloudObjects()
	cloudState.groups = {}
	cloudState.groupCount = math.max(1, math.floor(groupCountOverride or cloudState.groupCount or 1))
	cloudState.minPuffs = math.max(1, math.floor(cloudState.minPuffs or 1))
	cloudState.maxPuffs = math.max(cloudState.minPuffs, math.floor(cloudState.maxPuffs or cloudState.minPuffs))

	for _ = 1, cloudState.groupCount do
		local puffCount = math.random(cloudState.minPuffs, cloudState.maxPuffs)
		local group = {
			center = { 0, 0, 0 },
			radius = randomRange(cloudState.minGroupSize, cloudState.maxGroupSize),
			windDrift = randomRange(0.85, 1.25),
			puffs = {}
		}

		for _ = 1, puffCount do
			local puff = {
				model = cloudPuffModel,
				pos = { 0, 0, 0 },
				rot = q.identity(),
				scale = { 1, 1, 1 },
				color = { 0.95, 0.95, 0.97, 0.24 },
				isSolid = false,
				isCloud = true,
				cloudOffset = { 0, 0, 0 },
				cloudBobPhase = 0,
				cloudBobAmplitude = 0.5
			}
			group.puffs[#group.puffs + 1] = puff
			objects[#objects + 1] = puff
		end

		randomizeCloudGroup(group, false)
		cloudState.groups[#cloudState.groups + 1] = group
	end
end

local function updateClouds(dt)
	if #cloudState.groups == 0 or not camera then
		return
	end

	local isAuthoritySimulation = cloudNetState.role ~= "follower"
	if isAuthoritySimulation then
		updateWind(dt)
	end

	local windX, windZ = getWindVectorXZ()
	local now = love.timer.getTime()
	local maxDistSq = cloudState.despawnRadius * cloudState.despawnRadius

	for _, group in ipairs(cloudState.groups) do
		group.center[1] = group.center[1] + windX * group.windDrift * dt
		group.center[3] = group.center[3] + windZ * group.windDrift * dt

		local dx = group.center[1] - camera.pos[1]
		local dz = group.center[3] - camera.pos[3]
		if isAuthoritySimulation and (dx * dx + dz * dz) > maxDistSq then
			randomizeCloudGroup(group, true)
		else
			updateCloudGroupVisuals(group, now)
		end
	end
end

local function cameraSpaceDepthForObject(obj, viewCamera)
	if not obj or not obj.pos or not viewCamera then
		return -math.huge
	end
	local rel = {
		obj.pos[1] - viewCamera.pos[1],
		obj.pos[2] - viewCamera.pos[2],
		obj.pos[3] - viewCamera.pos[3]
	}
	local cam = q.rotateVector(q.conjugate(viewCamera.rot), rel)
	return cam[3]
end

local function setMouseCapture(enabled)
	love.mouse.setRelativeMode(enabled)
	pcall(love.mouse.setVisible, not enabled)
	relative = enabled
end

local function setFlightMode(enabled)
	flightSimMode = enabled and true or false
	if camera then
		if flightSimMode then
			camera.walkYaw = nil
			camera.walkPitch = nil
			camera.flightVel = camera.flightVel or { 0, 0, 0 }
		else
			camera.throttle = 0
			camera.flightVel = { 0, 0, 0 }
			camera.vel = { 0, 0, 0 }
			syncWalkingLookFromRotation()
		end
	end
end

local function cycleViewMode()
	if viewState.mode == "third_person" then
		viewState.mode = "first_person"
	else
		viewState.mode = "third_person"
	end
	resetAltLookState()
	resolveActiveRenderCamera()
	logger.log("View mode set to " .. viewState.mode)
end

local function setRendererPreference(enableGpu)
	local desiredGpu = enableGpu and true or false
	if desiredGpu and not renderer.isReady() then
		useGpuRenderer = false
		renderMode = "CPU"
		logger.log("GPU renderer unavailable; staying in CPU mode.")
		return false
	end

	local oldMode = renderMode
	useGpuRenderer = desiredGpu
	renderMode = (useGpuRenderer and renderer.isReady()) and "GPU" or "CPU"
	if oldMode ~= renderMode then
		logger.log("Render mode toggled to " .. renderMode)
	end
	return useGpuRenderer
end

local function resetCameraTransform()
	if not camera then
		return
	end

	camera.pos = { 0, 0, -5 }
	camera.rot = q.identity()
	camera.vel = { 0, 0, 0 }
	camera.flightVel = { 0, 0, 0 }
	camera.throttle = 0
	camera.onGround = false
	camera.walkYaw = 0
	camera.walkPitch = 0
	resetAltLookState()
	if camera.box and camera.box.halfSize then
		camera.box.pos = { camera.pos[1], camera.pos[2] - camera.box.halfSize.y, camera.pos[3] }
	end
	syncLocalPlayerObject()
	resolveActiveRenderCamera()
	logger.log("Camera transform reset.")
end

local function clearPeerObjects()
	for i = #objects, 1, -1 do
		if objects[i].id then
			table.remove(objects, i)
		end
	end
	peers = {}
end

local function splitPacketFields(data)
	local parts = {}
	if type(data) ~= "string" then
		return parts
	end

	for part in string.gmatch(data, "([^|]+)") do
		parts[#parts + 1] = part
	end
	return parts
end

local function formatNetFloat(value)
	return string.format("%.4f", tonumber(value) or 0)
end

function encodeColor3Token(color)
	return table.concat({
		formatNetFloat(color[1]),
		formatNetFloat(color[2]),
		formatNetFloat(color[3])
	}, ",")
end

function decodeColor3Token(token, fallback)
	local r, g, b = type(token) == "string" and token:match("^([^,]+),([^,]+),([^,]+)$")
	if not r or not g or not b then
		return cloneColor3(fallback)
	end
	return sanitizeColor3({
		tonumber(r),
		tonumber(g),
		tonumber(b)
	}, fallback)
end

local function relayIsConnected()
	if not relayServer then
		return false
	end

	local ok, state = pcall(function()
		return relayServer:state()
	end)
	return ok and state == "connected"
end

local function setCloudRole(role, detail)
	if cloudNetState.role == role then
		return
	end
	cloudNetState.role = role
	if detail and detail ~= "" then
		logger.log(string.format("Cloud sync role: %s (%s)", role, detail))
	else
		logger.log("Cloud sync role: " .. role)
	end
end

local function beginCloudRoleElection(reason)
	cloudNetState.connectedAt = love.timer.getTime()
	cloudNetState.sawPeerOnJoin = false
	cloudNetState.authorityPeerId = nil
	cloudNetState.lastSnapshotSentAt = -math.huge
	cloudNetState.lastSnapshotReceivedAt = -math.huge
	cloudNetState.lastSnapshotRequestAt = -math.huge
	groundNetState.authorityPeerId = nil
	groundNetState.lastSnapshotReceivedAt = -math.huge
	groundNetState.lastSnapshotRequestAt = -math.huge
	setCloudRole("pending", reason or "relay connected")
end

local function requestCloudSnapshot(reason)
	if cloudNetState.role ~= "follower" then
		return false
	end
	if not relayIsConnected() then
		return false
	end

	local now = love.timer.getTime()
	if (now - cloudNetState.lastSnapshotRequestAt) < cloudNetState.requestCooldown then
		return false
	end

	local packet = string.format("CLOUD_REQUEST|%s", formatNetFloat(now))
	local ok = pcall(function()
		relayServer:send(packet)
	end)
	if ok then
		cloudNetState.lastSnapshotRequestAt = now
		if reason and reason ~= "" then
			logger.log("Requested cloud snapshot (" .. reason .. ").")
		end
	end
	return ok
end

local function buildCloudSnapshotPacket(now)
	local parts = {
		"CLOUD_SNAPSHOT",
		formatNetFloat(now),
		formatNetFloat(windState.angle),
		formatNetFloat(windState.speed),
		tostring(#cloudState.groups),
		formatNetFloat(cloudState.minAltitude),
		formatNetFloat(cloudState.maxAltitude),
		formatNetFloat(cloudState.spawnRadius),
		formatNetFloat(cloudState.despawnRadius),
		formatNetFloat(cloudState.minGroupSize),
		formatNetFloat(cloudState.maxGroupSize),
		tostring(math.floor(cloudState.minPuffs)),
		tostring(math.floor(cloudState.maxPuffs))
	}

	for _, group in ipairs(cloudState.groups) do
		parts[#parts + 1] = table.concat({
			formatNetFloat(group.center[1]),
			formatNetFloat(group.center[2]),
			formatNetFloat(group.center[3]),
			formatNetFloat(group.radius),
			formatNetFloat(group.windDrift)
		}, ",")
	end

	return table.concat(parts, "|")
end

local function sendCloudSnapshot(forceSend, reason)
	if cloudNetState.role ~= "authority" then
		return false
	end
	if not relayIsConnected() then
		return false
	end

	local now = love.timer.getTime()
	if (not forceSend) and (now - cloudNetState.lastSnapshotSentAt) < cloudNetState.snapshotInterval then
		return false
	end

	local packet = buildCloudSnapshotPacket(now)
	local ok = pcall(function()
		relayServer:send(packet)
	end)
	if ok then
		cloudNetState.lastSnapshotSentAt = now
		if forceSend and reason and reason ~= "" then
			logger.log("Sent cloud snapshot (" .. reason .. ").")
		end
	end
	return ok
end

function requestGroundSnapshot(reason)
	if cloudNetState.role ~= "follower" then
		return false
	end
	if not relayIsConnected() then
		return false
	end

	local now = love.timer.getTime()
	if (now - groundNetState.lastSnapshotRequestAt) < groundNetState.requestCooldown then
		return false
	end

	local packet = string.format("GROUND_REQUEST|%s", formatNetFloat(now))
	local ok = pcall(function()
		relayServer:send(packet)
	end)
	if ok then
		groundNetState.lastSnapshotRequestAt = now
		if reason and reason ~= "" then
			logger.log("Requested ground snapshot (" .. reason .. ").")
		end
	end
	return ok
end

function buildGroundSnapshotPacket(now)
	local params = activeGroundParams or normalizeGroundParams(defaultGroundParams)
	local parts = {
		"GROUND_SNAPSHOT",
		formatNetFloat(now),
		tostring(params.seed),
		formatNetFloat(params.tileSize),
		tostring(params.gridCount),
		formatNetFloat(params.baseHeight),
		formatNetFloat(params.tileThickness),
		tostring(params.roadCount),
		formatNetFloat(params.roadDensity),
		tostring(params.fieldCount),
		tostring(params.fieldMinSize),
		tostring(params.fieldMaxSize),
		encodeColor3Token(params.grassColor),
		encodeColor3Token(params.roadColor),
		encodeColor3Token(params.fieldColor),
		encodeColor3Token(params.grassVar),
		encodeColor3Token(params.roadVar),
		encodeColor3Token(params.fieldVar)
	}
	return table.concat(parts, "|")
end

function sendGroundSnapshot(reason)
	if cloudNetState.role ~= "authority" then
		return false
	end
	if not relayIsConnected() then
		return false
	end

	local packet = buildGroundSnapshotPacket(love.timer.getTime())
	local ok = pcall(function()
		relayServer:send(packet)
	end)
	if ok and reason and reason ~= "" then
		logger.log("Sent ground snapshot (" .. reason .. ").")
	end
	return ok
end

function applyGroundSnapshotParts(parts, senderId)
	if #parts < 19 then
		return false
	end

	local params = normalizeGroundParams({
		seed = tonumber(parts[3]),
		tileSize = tonumber(parts[4]),
		gridCount = tonumber(parts[5]),
		baseHeight = tonumber(parts[6]),
		tileThickness = tonumber(parts[7]),
		roadCount = tonumber(parts[8]),
		roadDensity = tonumber(parts[9]),
		fieldCount = tonumber(parts[10]),
		fieldMinSize = tonumber(parts[11]),
		fieldMaxSize = tonumber(parts[12]),
		grassColor = decodeColor3Token(parts[13], defaultGroundParams.grassColor),
		roadColor = decodeColor3Token(parts[14], defaultGroundParams.roadColor),
		fieldColor = decodeColor3Token(parts[15], defaultGroundParams.fieldColor),
		grassVar = decodeColor3Token(parts[16], defaultGroundParams.grassVar),
		roadVar = decodeColor3Token(parts[17], defaultGroundParams.roadVar),
		fieldVar = decodeColor3Token(parts[18], defaultGroundParams.fieldVar)
	})

	rebuildGroundFromParams(params, "network peer " .. tostring(senderId))
	groundNetState.authorityPeerId = senderId
	groundNetState.lastSnapshotReceivedAt = love.timer.getTime()
	return true
end

function handleGroundSnapshotPacket(data)
	local parts = splitPacketFields(data)
	if parts[1] ~= "GROUND_SNAPSHOT" then
		return false
	end

	local senderId = tonumber(parts[#parts])
	if not senderId then
		return false
	end

	if cloudNetState.role == "authority" then
		return false
	end

	if cloudNetState.role == "follower" and
		groundNetState.authorityPeerId and
		groundNetState.authorityPeerId ~= senderId and
		groundNetState.lastSnapshotReceivedAt > -1e30 then
		return false
	end

	if cloudNetState.role == "pending" then
		cloudNetState.sawPeerOnJoin = true
	end
	return applyGroundSnapshotParts(parts, senderId)
end

function handleGroundRequestPacket(data)
	local parts = splitPacketFields(data)
	if parts[1] ~= "GROUND_REQUEST" then
		return false
	end

	local senderId = tonumber(parts[#parts])
	if not senderId then
		return false
	end

	if cloudNetState.role == "pending" then
		cloudNetState.sawPeerOnJoin = true
	end
	if cloudNetState.role == "authority" then
		sendGroundSnapshot("request from peer " .. tostring(senderId))
	end
	return true
end

local function applyCloudSnapshotParts(parts, senderId)
	if #parts < 15 then
		return false
	end

	local snapshotGroupCount = math.max(1, math.floor(tonumber(parts[5]) or cloudState.groupCount))
	local expectedFields = 13 + snapshotGroupCount + 1
	if #parts < expectedFields then
		return false
	end

	local snapshotMinAltitude = tonumber(parts[6]) or cloudState.minAltitude
	local snapshotMaxAltitude = tonumber(parts[7]) or cloudState.maxAltitude
	local snapshotSpawnRadius = tonumber(parts[8]) or cloudState.spawnRadius
	local snapshotDespawnRadius = tonumber(parts[9]) or cloudState.despawnRadius
	local snapshotMinGroupSize = tonumber(parts[10]) or cloudState.minGroupSize
	local snapshotMaxGroupSize = tonumber(parts[11]) or cloudState.maxGroupSize
	local snapshotMinPuffs = math.max(1, math.floor(tonumber(parts[12]) or cloudState.minPuffs))
	local snapshotMaxPuffs = math.max(snapshotMinPuffs, math.floor(tonumber(parts[13]) or cloudState.maxPuffs))
	local snapshotWindAngle = tonumber(parts[3])
	local snapshotWindSpeed = tonumber(parts[4])

	cloudState.groupCount = snapshotGroupCount
	cloudState.minAltitude = math.min(snapshotMinAltitude, snapshotMaxAltitude)
	cloudState.maxAltitude = math.max(snapshotMinAltitude, snapshotMaxAltitude)
	cloudState.spawnRadius = math.max(10, snapshotSpawnRadius)
	cloudState.despawnRadius = math.max(cloudState.spawnRadius + 10, snapshotDespawnRadius)
	cloudState.minGroupSize = math.max(1, math.min(snapshotMinGroupSize, snapshotMaxGroupSize))
	cloudState.maxGroupSize = math.max(cloudState.minGroupSize, snapshotMaxGroupSize)
	cloudState.minPuffs = snapshotMinPuffs
	cloudState.maxPuffs = snapshotMaxPuffs

	if snapshotWindAngle then
		windState.angle = wrapAngle(snapshotWindAngle)
	end
	if snapshotWindSpeed then
		windState.speed = math.max(0, snapshotWindSpeed)
	end
	windState.targetAngle = windState.angle
	windState.targetSpeed = windState.speed
	windState.retargetIn = math.huge

	if #cloudState.groups ~= snapshotGroupCount then
		spawnCloudField(snapshotGroupCount)
	end

	local now = love.timer.getTime()
	for i = 1, snapshotGroupCount do
		local token = parts[13 + i]
		local gx, gy, gz, gr, gd = token and token:match("^([^,]+),([^,]+),([^,]+),([^,]+),([^,]+)$")
		local group = cloudState.groups[i]
		if group and gx and gy and gz and gr and gd then
			group.center[1] = tonumber(gx) or group.center[1]
			group.center[2] = tonumber(gy) or group.center[2]
			group.center[3] = tonumber(gz) or group.center[3]
			group.radius = math.max(1, tonumber(gr) or group.radius)
			group.windDrift = tonumber(gd) or group.windDrift
			updateCloudGroupVisuals(group, now)
		end
	end

	cloudNetState.authorityPeerId = senderId
	cloudNetState.lastSnapshotReceivedAt = now
	return true
end

local function handleCloudSnapshotPacket(data)
	local parts = splitPacketFields(data)
	if parts[1] ~= "CLOUD_SNAPSHOT" then
		return false
	end

	local senderId = tonumber(parts[#parts])
	if not senderId then
		return false
	end

	if cloudNetState.role == "authority" then
		return false
	end

	if cloudNetState.role == "follower" and cloudNetState.authorityPeerId and cloudNetState.authorityPeerId ~= senderId then
		return false
	end

	if cloudNetState.role ~= "follower" then
		setCloudRole("follower", "cloud snapshot received")
	end
	cloudNetState.sawPeerOnJoin = true
	return applyCloudSnapshotParts(parts, senderId)
end

local function handleCloudRequestPacket(data)
	local parts = splitPacketFields(data)
	if parts[1] ~= "CLOUD_REQUEST" then
		return false
	end

	local senderId = tonumber(parts[#parts])
	if not senderId then
		return false
	end

	if cloudNetState.role == "pending" then
		cloudNetState.sawPeerOnJoin = true
	end

	if cloudNetState.role == "authority" then
		sendCloudSnapshot(true, "request from peer " .. tostring(senderId))
	end
	return true
end

local function notePeerStateReceived(peerId)
	if not peerId then
		return
	end
	if cloudNetState.role == "standalone" and relayIsConnected() then
		beginCloudRoleElection("peer state observed")
	end

	if cloudNetState.role == "pending" then
		cloudNetState.sawPeerOnJoin = true
		cloudNetState.authorityPeerId = cloudNetState.authorityPeerId or peerId
		setCloudRole("follower", "peer state received during join")
		requestCloudSnapshot("joining populated session")
		requestGroundSnapshot("joining populated session")
	elseif cloudNetState.role == "follower" and not cloudNetState.authorityPeerId then
		cloudNetState.authorityPeerId = peerId
	end
end

local function updateCloudNetworkState(now)
	if not relayIsConnected() then
		if cloudNetState.role ~= "standalone" then
			setCloudRole("standalone", "relay unavailable")
		end
		cloudNetState.authorityPeerId = nil
		groundNetState.authorityPeerId = nil
		groundNetState.lastSnapshotReceivedAt = -math.huge
		groundNetState.lastSnapshotRequestAt = -math.huge
		return
	end

	if cloudNetState.role == "standalone" then
		beginCloudRoleElection("relay connected")
	end

	if cloudNetState.role == "pending" then
		if cloudNetState.sawPeerOnJoin then
			setCloudRole("follower", "peer detected during join window")
			requestCloudSnapshot("peer already present")
			requestGroundSnapshot("peer already present")
		elseif (now - cloudNetState.connectedAt) >= cloudNetState.decisionDelay then
			cloudNetState.authorityPeerId = nil
			cloudNetState.lastSnapshotSentAt = -math.huge
			setCloudRole("authority", "no peers detected at connect")
			sendGroundSnapshot("authority elected")
		end
		return
	end

	if cloudNetState.role == "follower" and (now - cloudNetState.lastSnapshotReceivedAt) >= cloudNetState.staleTimeout then
		requestCloudSnapshot("snapshot stale")
	end

	if cloudNetState.role == "follower" and groundNetState.lastSnapshotReceivedAt <= -1e30 then
		requestGroundSnapshot("awaiting world params")
	end
end

local function getRelayStatus()
	if not relay then
		return "Host down"
	end
	if not relayServer then
		return "Disconnected"
	end

	local ok, state = pcall(function()
		return relayServer:state()
	end)
	if not ok then
		return "Unknown"
	end
	if state == "connected" then
		return "Connected"
	end
	return tostring(state)
end

local function reconnectRelay()
	if not relay then
		relay = enet.host_create()
		if not relay then
			logger.log("ENet host creation failed; reconnect unavailable.")
			return false
		end
	end

	if relayServer then
		pcall(function()
			relayServer:disconnect_now()
		end)
		relayServer = nil
	end

	clearPeerObjects()
	cloudNetState.authorityPeerId = nil
	groundNetState.authorityPeerId = nil
	groundNetState.lastSnapshotReceivedAt = -math.huge
	groundNetState.lastSnapshotRequestAt = -math.huge
	setCloudRole("standalone", "relay reconnecting")

	local ok, peerOrErr = pcall(function()
		return relay:connect(hostAddy)
	end)
	if not ok then
		logger.log("Relay reconnect failed: " .. tostring(peerOrErr))
		return false
	end

	relayServer = peerOrErr
	if relayServer then
		logger.log("Reconnecting to relay server: " .. hostAddy)
		return true
	end

	logger.log("Relay reconnect failed: no peer handle returned.")
	return false
end

local function refreshUiFonts()
	local baseSize = math.max(14, math.floor(screen.h * 0.023))
	pauseTitleFont = love.graphics.newFont(math.max(24, baseSize + 8))
	pauseItemFont = love.graphics.newFont(baseSize)
end

local setPauseState

local function setPauseStatus(message, duration)
	pauseMenu.statusText = message or ""
	if message and message ~= "" then
		pauseMenu.statusUntil = love.timer.getTime() + (duration or 2.0)
	else
		pauseMenu.statusUntil = 0
	end
end

local function getWrappedLines(font, text, width)
	if not text or text == "" then
		return {}
	end
	local _, lines = font:getWrap(text, width)
	return lines
end

local function trimWrappedLines(lines, maxLines)
	if #lines <= maxLines then
		return lines
	end

	local trimmed = {}
	for i = 1, maxLines do
		trimmed[i] = lines[i]
	end
	trimmed[maxLines] = trimmed[maxLines] .. "..."
	return trimmed
end

local function getSelectedControlAction()
	local index = clamp(pauseMenu.controlsSelection, 1, #controlActions)
	return controlActions[index], index
end

local function clearControlBindingCapture()
	pauseMenu.listeningBinding = nil
end

local function setControlBinding(actionIndex, slotIndex, binding)
	local action = controlActions[actionIndex]
	if not action then
		return false
	end

	slotIndex = clamp(slotIndex or 1, 1, 2)
	action.bindings = action.bindings or {}
	action.bindings[slotIndex] = cloneBinding(binding)
	return true
end

local function clearSelectedControlBinding()
	local action, actionIndex = getSelectedControlAction()
	if not action then
		return
	end

	local slotIndex = clamp(pauseMenu.controlsSlot, 1, 2)
	action.bindings = action.bindings or {}
	action.bindings[slotIndex] = nil
	clearControlBindingCapture()
	setPauseStatus(action.label .. " slot " .. tostring(slotIndex) .. " cleared.", 1.6)
end

local function beginSelectedControlBindingCapture(actionIndex, slotIndex)
	local action = controlActions[actionIndex]
	if not action then
		return
	end

	pauseMenu.controlsSelection = actionIndex
	pauseMenu.controlsSlot = clamp(slotIndex or pauseMenu.controlsSlot, 1, 2)
	pauseMenu.listeningBinding = {
		actionIndex = actionIndex,
		slot = pauseMenu.controlsSlot
	}
	setPauseStatus("Listening: " .. action.label .. " slot " .. tostring(pauseMenu.controlsSlot) .. ".", 6.0)
end

local function commitControlBindingCapture(binding)
	local listen = pauseMenu.listeningBinding
	if not listen then
		return false
	end

	if setControlBinding(listen.actionIndex, listen.slot, binding) then
		local action = controlActions[listen.actionIndex]
		clearControlBindingCapture()
		if action then
			local label = formatBinding(binding)
			setPauseStatus(action.label .. " -> " .. label, 2.0)
		end
		return true
	end

	clearControlBindingCapture()
	return false
end

local function cancelControlBindingCapture()
	if not pauseMenu.listeningBinding then
		return false
	end
	clearControlBindingCapture()
	setPauseStatus("Binding capture cancelled.", 1.3)
	return true
end

local function moveControlsSelection(delta)
	local count = #controlActions
	if count <= 0 then
		return
	end
	pauseMenu.controlsSelection = ((pauseMenu.controlsSelection - 1 + delta) % count) + 1
end

local function moveControlsSlot(delta)
	if delta == 0 then
		return
	end
	local nextSlot = pauseMenu.controlsSlot + (delta > 0 and 1 or -1)
	if nextSlot < 1 then
		nextSlot = 2
	elseif nextSlot > 2 then
		nextSlot = 1
	end
	pauseMenu.controlsSlot = nextSlot
end

local function resetControlsSelectionState()
	pauseMenu.controlsSelection = clamp(pauseMenu.controlsSelection, 1, #controlActions)
	pauseMenu.controlsSlot = clamp(pauseMenu.controlsSlot, 1, 2)
	pauseMenu.controlsScroll = 0
	pauseMenu.controlsRowBounds = {}
	pauseMenu.controlsSlotBounds = {}
	clearControlBindingCapture()
end

local function captureBindingFromKey(key)
	if key == "escape" then
		return false
	end
	local binding = bindKey(key, extractCaptureModifiers(key))
	return commitControlBindingCapture(binding)
end

local function captureBindingFromMouseButton(button)
	local binding = bindMouseButton(button, extractCaptureModifiers(nil))
	return commitControlBindingCapture(binding)
end

local function captureBindingFromWheel(y)
	local direction = y > 0 and 1 or -1
	local binding = bindMouseWheel(direction, extractCaptureModifiers(nil))
	return commitControlBindingCapture(binding)
end

local function captureBindingFromMouseMotion(dx, dy)
	local absX = math.abs(dx)
	local absY = math.abs(dy)
	local threshold = 5
	if absX < threshold and absY < threshold then
		return false
	end

	local axis, direction
	if absX >= absY then
		axis = "x"
		direction = dx >= 0 and 1 or -1
	else
		axis = "y"
		direction = dy >= 0 and 1 or -1
	end

	local binding = bindMouseAxis(axis, direction, extractCaptureModifiers(nil))
	return commitControlBindingCapture(binding)
end

local function buildPauseMenuLayout()
	local titleFont = pauseTitleFont or love.graphics.getFont()
	local itemFont = pauseItemFont or love.graphics.getFont()
	local isGameTab = pauseMenu.tab == "game"
	local isHelpTab = pauseMenu.tab == "help"
	local isControlsTab = pauseMenu.tab == "controls"
	local itemCount = #pauseMenu.items

	local panelW = math.min(660, screen.w * 0.9)
	local panelX = (screen.w - panelW) / 2
	local contentX = panelX + 18
	local contentW = panelW - 36
	local footerTextW = contentW
	local controlsText
	if isGameTab then
		controlsText = "Tab/H for Help/Controls | Up/Down select | Enter apply | Esc resume"
	elseif isControlsTab then
		if pauseMenu.listeningBinding then
			controlsText = "Listening: press a key/button, move mouse, or use wheel | Esc cancel"
		else
			controlsText = "Enter listen | Left/Right slot | Backspace clear | R reset defaults | Esc resume"
		end
	else
		controlsText = "Tab/H for Game/Controls | Mouse wheel scroll | Esc resume"
	end

	local selectedItem = pauseMenu.items[pauseMenu.selected]
	local selectedControl = controlActions[pauseMenu.controlsSelection]
	local detailText = ""
	if isGameTab then
		detailText = selectedItem and selectedItem.help or ""
	elseif isControlsTab then
		detailText = selectedControl and selectedControl.description or ""
	end
	local statusText = pauseMenu.statusText

	local lineH = itemFont:getHeight()
	local titleTopPad = 14
	local titleH = titleFont:getHeight()
	local tabH = lineH + 8
	local topPad = titleTopPad + titleH + 8 + tabH + 10
	local footerTopPad = 10
	local footerGap = 6
	local footerBottomPad = 10
	local maxPanelH = math.max(220, screen.h - 24)
	maxPanelH = math.min(maxPanelH, screen.h - 8)

	local function computeFooterContentHeight(controlsLines, detailLines, statusLines)
		local h = #controlsLines * lineH
		if #detailLines > 0 then
			h = h + footerGap + (#detailLines * lineH)
		end
		if #statusLines > 0 then
			h = h + footerGap + (#statusLines * lineH)
		end
		return h
	end

	local controlsLines = trimWrappedLines(getWrappedLines(itemFont, controlsText, footerTextW), 2)
	local detailLines = trimWrappedLines(getWrappedLines(itemFont, detailText, footerTextW), 2)
	local statusLines = trimWrappedLines(getWrappedLines(itemFont, statusText, footerTextW), 2)
	local footerContentH = computeFooterContentHeight(controlsLines, detailLines, statusLines)
	local bottomPad = footerTopPad + footerContentH + footerBottomPad

	if (maxPanelH - topPad - bottomPad) < itemCount * 18 then
		controlsLines = trimWrappedLines(getWrappedLines(itemFont, controlsText, footerTextW), 1)
		detailLines = trimWrappedLines(getWrappedLines(itemFont, detailText, footerTextW), 1)
		statusLines = trimWrappedLines(getWrappedLines(itemFont, statusText, footerTextW), 1)
		footerContentH = computeFooterContentHeight(controlsLines, detailLines, statusLines)
		bottomPad = footerTopPad + footerContentH + footerBottomPad
	end

	local availableContentH = math.max(1, maxPanelH - topPad - bottomPad)
	local rowH = 0
	local contentH = availableContentH
	if isGameTab then
		local minRowH = math.max(lineH + 8, 24)
		local maxRowH = 40
		if availableContentH >= minRowH * itemCount then
			rowH = clamp(math.floor(availableContentH / itemCount), minRowH, maxRowH)
		else
			rowH = math.max(lineH + 4, math.floor(availableContentH / itemCount))
		end
		contentH = math.max(1, rowH * itemCount)
	else
		contentH = math.max(lineH * 8, availableContentH)
	end

	local panelH = topPad + contentH + bottomPad
	if panelH > maxPanelH then
		panelH = maxPanelH
	end

	local finalContentH = math.max(1, panelH - topPad - bottomPad)
	if isGameTab then
		rowH = math.max(1, math.floor(finalContentH / itemCount))
		finalContentH = rowH * itemCount
		panelH = topPad + finalContentH + bottomPad
		if panelH > maxPanelH then
			panelH = maxPanelH
			finalContentH = math.max(1, panelH - topPad - bottomPad)
			rowH = math.max(1, math.floor(finalContentH / itemCount))
			finalContentH = rowH * itemCount
			panelH = topPad + finalContentH + bottomPad
		end
	end

	local panelY = (screen.h - panelH) / 2
	local titleY = panelY + titleTopPad
	local tabY = titleY + titleH + 8
	local contentY = panelY + topPad

	return {
		isGameTab = isGameTab,
		isHelpTab = isHelpTab,
		isControlsTab = isControlsTab,
		panelX = panelX,
		panelY = panelY,
		panelW = panelW,
		panelH = panelH,
		topPad = topPad,
		bottomPad = bottomPad,
		titleY = titleY,
		tabY = tabY,
		tabH = tabH,
		contentX = contentX,
		contentY = contentY,
		contentW = contentW,
		contentH = finalContentH,
		rowX = contentX,
		rowW = contentW,
		rowH = rowH,
		footerTopPad = footerTopPad,
		footerGap = footerGap,
		controlsLines = controlsLines,
		detailLines = detailLines,
		statusLines = statusLines,
		lineH = lineH
	}
end

local function clearPauseConfirm()
	pauseMenu.confirmItemId = nil
	pauseMenu.confirmUntil = 0
end

local function clearPauseRangeDrag()
	pauseMenu.dragRange.active = false
	pauseMenu.dragRange.itemIndex = nil
	pauseMenu.dragRange.startX = 0
	pauseMenu.dragRange.lastX = 0
	pauseMenu.dragRange.totalDx = 0
	pauseMenu.dragRange.moved = false
end

local function setPauseTab(tab)
	if tab ~= "game" and tab ~= "help" and tab ~= "controls" then
		return
	end
	if pauseMenu.tab == tab then
		return
	end

	pauseMenu.tab = tab
	pauseMenu.helpScroll = 0
	pauseMenu.controlsScroll = 0
	pauseMenu.itemBounds = {}
	pauseMenu.controlsRowBounds = {}
	pauseMenu.controlsSlotBounds = {}
	pauseMenu.tabBounds = {}
	clearPauseRangeDrag()
	clearPauseConfirm()
	if tab == "controls" then
		resetControlsSelectionState()
	else
		clearControlBindingCapture()
	end
end

local function cyclePauseTab(direction)
	if direction == 0 then
		return
	end
	local tabs = { "game", "help", "controls" }
	local currentIndex = 1
	for i, tab in ipairs(tabs) do
		if pauseMenu.tab == tab then
			currentIndex = i
			break
		end
	end

	local step = direction > 0 and 1 or -1
	local nextIndex = ((currentIndex - 1 + step) % #tabs) + 1
	setPauseTab(tabs[nextIndex])
end

local function isPauseConfirmActive(itemId)
	return pauseMenu.confirmItemId == itemId and love.timer.getTime() <= pauseMenu.confirmUntil
end

local function getPauseItemValue(item)
	if item.id == "flight" then
		return flightSimMode and "On" or "Off"
	end
	if item.id == "renderer" then
		if renderer.isReady() then
			return useGpuRenderer and "GPU" or "CPU"
		end
		return "CPU (GPU unavailable)"
	end
	if item.id == "crosshair" then
		return showCrosshair and "On" or "Off"
	end
	if item.id == "overlay" then
		return showDebugOverlay and "On" or "Off"
	end
	if item.id == "speed" and camera then
		return string.format("%.1f", camera.speed)
	end
	if item.id == "fov" and camera then
		local fovDeg = math.deg(camera.baseFov or camera.fov)
		return string.format("%d deg", math.floor(fovDeg + 0.5))
	end
	if item.id == "sensitivity" then
		return string.format("%.4f", mouseSensitivity)
	end
	if item.id == "invertY" then
		return invertLookY and "On" or "Off"
	end
	if item.id == "reconnect" then
		return getRelayStatus()
	end
	if item.id == "quit" and isPauseConfirmActive("quit") then
		return "Are you Sure?"
	end
	return ""
end

local function adjustPauseItem(item, direction, multiplier)
	if direction == 0 then
		return
	end

	local scale = multiplier or 1

	if item.id == "speed" and camera then
		camera.speed = clamp(camera.speed + item.step * direction * scale, item.min, item.max)
		setPauseStatus("Move Speed: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "fov" and camera then
		local fovDeg = math.deg(camera.baseFov or camera.fov)
		fovDeg = clamp(fovDeg + item.step * direction * scale, item.min, item.max)
		camera.baseFov = math.rad(fovDeg)
		camera.fov = camera.baseFov
		setPauseStatus("Field of View: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "sensitivity" then
		local nextValue = clamp(mouseSensitivity + item.step * direction * scale, item.min, item.max)
		mouseSensitivity = math.floor(nextValue * 10000 + 0.5) / 10000
		setPauseStatus("Mouse Sensitivity: " .. getPauseItemValue(item), 1.2)
	end
end

local function activatePauseItem(item)
	if item.kind == "range" then
		adjustPauseItem(item, 1)
		return
	end

	if item.confirm and not isPauseConfirmActive(item.id) then
		pauseMenu.confirmItemId = item.id
		pauseMenu.confirmUntil = love.timer.getTime() + 2.5
		setPauseStatus("Confirm to \"" .. item.label .. "\".", 2.5)
		return
	end
	clearPauseConfirm()

	if item.id == "resume" then
		setPauseState(false)
	elseif item.id == "flight" then
		setFlightMode(not flightSimMode)
		setPauseStatus("Flight Mode: " .. getPauseItemValue(item), 1.2)
	elseif item.id == "renderer" then
		setRendererPreference(not useGpuRenderer)
		setPauseStatus("Renderer: " .. getPauseItemValue(item), 1.2)
	elseif item.id == "crosshair" then
		showCrosshair = not showCrosshair
		setPauseStatus("Crosshair: " .. getPauseItemValue(item), 1.2)
	elseif item.id == "overlay" then
		showDebugOverlay = not showDebugOverlay
		setPauseStatus("Debug Overlay: " .. getPauseItemValue(item), 1.2)
	elseif item.id == "invertY" then
		invertLookY = not invertLookY
		setPauseStatus("Invert Look Y: " .. getPauseItemValue(item), 1.2)
	elseif item.id == "reset" then
		resetCameraTransform()
		setPauseStatus("Camera reset.", 1.2)
	elseif item.id == "reconnect" then
		local ok = reconnectRelay()
		if ok then
			setPauseStatus("Relay reconnect requested.", 1.6)
		else
			setPauseStatus("Relay reconnect failed. Check logs.", 1.6)
		end
	elseif item.id == "quit" then
		love.event.quit()
	end
end

local function movePauseSelection(delta)
	local itemCount = #pauseMenu.items
	pauseMenu.selected = ((pauseMenu.selected - 1 + delta) % itemCount) + 1
	clearPauseRangeDrag()
	clearPauseConfirm()
end

local function adjustSelectedPauseItem(direction)
	local item = pauseMenu.items[pauseMenu.selected]
	if not item then
		return
	end
	if item.kind == "range" then
		adjustPauseItem(item, direction)
	elseif item.kind == "toggle" and direction ~= 0 then
		activatePauseItem(item)
	end
end

local function adjustSelectedPauseItemCoarse(direction)
	local item = pauseMenu.items[pauseMenu.selected]
	if item and item.kind == "range" then
		adjustPauseItem(item, direction, 5)
	end
end

local function activateSelectedPauseItem()
	local item = pauseMenu.items[pauseMenu.selected]
	if item then
		activatePauseItem(item)
	end
end

local function updatePauseMenuHover(x, y)
	if pauseMenu.tab ~= "game" then
		return
	end
	if pauseMenu.dragRange.active then
		return
	end

	for i, bounds in ipairs(pauseMenu.itemBounds) do
		if x >= bounds.x and x <= bounds.x + bounds.w and y >= bounds.y and y <= bounds.y + bounds.h then
			if pauseMenu.selected ~= i then
				pauseMenu.selected = i
				clearPauseRangeDrag()
				clearPauseConfirm()
			end
			return
		end
	end
end

local function updatePauseControlsHover(x, y)
	if pauseMenu.tab ~= "controls" or pauseMenu.listeningBinding then
		return
	end

	for i, slotBounds in ipairs(pauseMenu.controlsSlotBounds) do
		for slot, bounds in ipairs(slotBounds) do
			if bounds and
					x >= bounds.x and x <= bounds.x + bounds.w and
					y >= bounds.y and y <= bounds.y + bounds.h then
				pauseMenu.controlsSelection = i
				pauseMenu.controlsSlot = slot
				return
			end
		end
	end

	for i, bounds in ipairs(pauseMenu.controlsRowBounds) do
		if x >= bounds.x and x <= bounds.x + bounds.w and y >= bounds.y and y <= bounds.y + bounds.h then
			pauseMenu.controlsSelection = i
			return
		end
	end
end

local function beginPauseRangeDrag(itemIndex, x)
	pauseMenu.dragRange.active = true
	pauseMenu.dragRange.itemIndex = itemIndex
	pauseMenu.dragRange.startX = x
	pauseMenu.dragRange.lastX = x
	pauseMenu.dragRange.totalDx = 0
	pauseMenu.dragRange.moved = false
end

local function updatePauseRangeDrag(x)
	if not pauseMenu.dragRange.active then
		return
	end

	local itemIndex = pauseMenu.dragRange.itemIndex
	local item = itemIndex and pauseMenu.items[itemIndex] or nil
	if not item or item.kind ~= "range" then
		clearPauseRangeDrag()
		return
	end

	local dx = x - pauseMenu.dragRange.lastX
	pauseMenu.dragRange.lastX = x
	pauseMenu.dragRange.totalDx = pauseMenu.dragRange.totalDx + dx

	local stepPixels = 12
	while math.abs(pauseMenu.dragRange.totalDx) >= stepPixels do
		local direction = pauseMenu.dragRange.totalDx > 0 and 1 or -1
		adjustPauseItem(item, direction)
		pauseMenu.dragRange.totalDx = pauseMenu.dragRange.totalDx - (direction * stepPixels)
		pauseMenu.dragRange.moved = true
	end
end

local function endPauseRangeDrag(x)
	if not pauseMenu.dragRange.active then
		return false
	end

	local itemIndex = pauseMenu.dragRange.itemIndex
	local moved = pauseMenu.dragRange.moved
	local item = itemIndex and pauseMenu.items[itemIndex] or nil
	local bounds = itemIndex and pauseMenu.itemBounds[itemIndex] or nil

	clearPauseRangeDrag()

	if moved or not item or item.kind ~= "range" or not bounds then
		return moved
	end

	if x >= bounds.x and x <= (bounds.x + bounds.w) then
		local direction = (x >= (bounds.x + bounds.w * 0.5)) and 1 or -1
		adjustPauseItem(item, direction)
		return true
	end

	return false
end

local function handlePauseControlsMouseClick(x, y, button)
	if pauseMenu.listeningBinding then
		return false
	end

	if button ~= 1 then
		return false
	end

	for i, slotBounds in ipairs(pauseMenu.controlsSlotBounds) do
		for slot, bounds in ipairs(slotBounds) do
			if bounds and
					x >= bounds.x and x <= bounds.x + bounds.w and
					y >= bounds.y and y <= bounds.y + bounds.h then
				beginSelectedControlBindingCapture(i, slot)
				return true
			end
		end
	end

	for i, bounds in ipairs(pauseMenu.controlsRowBounds) do
		if x >= bounds.x and x <= bounds.x + bounds.w and y >= bounds.y and y <= bounds.y + bounds.h then
			pauseMenu.controlsSelection = i
			return true
		end
	end

	return false
end

local function handlePauseMenuMouseClick(x, y)
	for _, tabBounds in ipairs(pauseMenu.tabBounds) do
		if x >= tabBounds.x and x <= tabBounds.x + tabBounds.w and y >= tabBounds.y and y <= tabBounds.y + tabBounds.h then
			setPauseTab(tabBounds.id)
			return
		end
	end

	if pauseMenu.tab == "controls" then
		handlePauseControlsMouseClick(x, y, 1)
		return
	end

	if pauseMenu.tab ~= "game" then
		return
	end

	for i, bounds in ipairs(pauseMenu.itemBounds) do
		if x >= bounds.x and x <= bounds.x + bounds.w and y >= bounds.y and y <= bounds.y + bounds.h then
			pauseMenu.selected = i
			local item = pauseMenu.items[i]
			clearPauseRangeDrag()
			if item and item.id ~= "quit" then
				clearPauseConfirm()
			end
			if item and item.kind == "range" then
				beginPauseRangeDrag(i, x)
			elseif item then
				activatePauseItem(item)
			end
			return
		end
	end
end

setPauseState = function(paused)
	if pauseMenu.active == paused then
		return
	end

	pauseMenu.active = paused and true or false
	if pauseMenu.active then
		resetAltLookState()
		mapState.mHeld = false
		mapState.mUsedForZoom = false
		pauseMenu.tab = "game"
		pauseMenu.helpScroll = 0
		pauseMenu.controlsScroll = 0
		pauseMenu.selected = clamp(pauseMenu.selected, 1, #pauseMenu.items)
		pauseMenu.controlsSelection = clamp(pauseMenu.controlsSelection, 1, #controlActions)
		pauseMenu.controlsSlot = clamp(pauseMenu.controlsSlot, 1, 2)
		pauseMenu.itemBounds = {}
		pauseMenu.controlsRowBounds = {}
		pauseMenu.controlsSlotBounds = {}
		pauseMenu.tabBounds = {}
		clearPauseRangeDrag()
		clearControlBindingCapture()
		setPauseStatus("", 0)
		clearPauseConfirm()
		setMouseCapture(false)
		logger.log("Pause menu opened.")
	else
		pauseMenu.tabBounds = {}
		pauseMenu.itemBounds = {}
		pauseMenu.controlsRowBounds = {}
		pauseMenu.controlsSlotBounds = {}
		clearPauseRangeDrag()
		clearControlBindingCapture()
		setPauseStatus("", 0)
		clearPauseConfirm()
		if love.window.hasFocus and love.window.hasFocus() then
			setMouseCapture(true)
		end
		logger.log("Pause menu closed.")
	end
end

local function drawPauseMenuRows(layout, rowTextHeight)
	local font = love.graphics.getFont()
	local arrowText = "<"
	local rightArrowText = ">"
	local arrowPad = 12

	pauseMenu.itemBounds = {}
	for i, item in ipairs(pauseMenu.items) do
		local rowY = layout.panelY + layout.topPad + (i - 1) * layout.rowH
		local rowHeight = math.max(1, layout.rowH - 4)
		local rowTextY = rowY + math.floor((rowHeight - rowTextHeight) / 2)
		local selected = i == pauseMenu.selected
		local value = getPauseItemValue(item)
		local centerText = item.label

		if selected then
			love.graphics.setColor(0.22, 0.28, 0.42, 0.95)
		else
			love.graphics.setColor(0.15, 0.16, 0.2, 0.9)
		end
		love.graphics.rectangle("fill", layout.rowX, rowY, layout.rowW, rowHeight, 6, 6)

		if value ~= "" then
			centerText = item.label .. "  " .. value
		end
		local centerTextW = font:getWidth(centerText)
		local centerTextX = layout.rowX + math.floor((layout.rowW - centerTextW) / 2)

		love.graphics.setColor(1, 1, 1, 1)
		love.graphics.print(centerText, centerTextX, rowTextY)

		if item.kind == "range" then
			local leftX = layout.rowX + arrowPad
			local rightX = layout.rowX + layout.rowW - arrowPad - font:getWidth(rightArrowText)
			love.graphics.print(arrowText, leftX, rowTextY)
			love.graphics.print(rightArrowText, rightX, rowTextY)
		end

		pauseMenu.itemBounds[i] = { x = layout.rowX, y = rowY, w = layout.rowW, h = rowHeight }
	end
end

local function drawPauseMenuTabs(layout)
	local font = love.graphics.getFont()
	pauseMenu.tabBounds = {}
	local labels = {
		{ id = "game", label = "Game" },
		{ id = "help", label = "Help" },
		{ id = "controls", label = "Controls" }
	}
	local tabGap = 8
	local totalW = 0
	for _, tab in ipairs(labels) do
		totalW = totalW + font:getWidth(tab.label) + 22
	end
	totalW = totalW + (tabGap * (#labels - 1))

	local x = layout.panelX + (layout.panelW - totalW) / 2
	for i, tab in ipairs(labels) do
		local tabW = font:getWidth(tab.label) + 22
		local active = pauseMenu.tab == tab.id

		if active then
			love.graphics.setColor(0.2, 0.3, 0.46, 0.95)
		else
			love.graphics.setColor(0.14, 0.16, 0.2, 0.95)
		end
		love.graphics.rectangle("fill", x, layout.tabY, tabW, layout.tabH, 6, 6)

		love.graphics.setColor(1, 1, 1, active and 1 or 0.85)
		love.graphics.printf(tab.label, x, layout.tabY + math.floor((layout.tabH - font:getHeight()) / 2), tabW, "center")

		pauseMenu.tabBounds[i] = { id = tab.id, x = x, y = layout.tabY, w = tabW, h = layout.tabH }
		x = x + tabW + tabGap
	end
end

local function drawPauseHelpContent(layout)
	local font = love.graphics.getFont()
	local sections = getPauseHelpSections()
	local cardGap = 10
	local cardPad = 10
	local cardX = layout.contentX + 4
	local cardW = layout.contentW - 8
	local lineH = font:getHeight()

	local function measureItemHeight(item)
		local badgeText = "[" .. item.keys .. "]"
		local badgeW = font:getWidth(badgeText) + 12
		local descW = math.max(40, cardW - cardPad * 2 - badgeW - 10)
		local lines = getWrappedLines(font, item.text, descW)
		local textH = math.max(lineH, #lines * lineH)
		return textH + 4
	end

	local function measureSectionHeight(section)
		local h = cardPad + lineH + 8
		for _, item in ipairs(section.items) do
			h = h + measureItemHeight(item) + 6
		end
		h = h + cardPad - 6
		return h
	end

	local totalH = 0
	for _, section in ipairs(sections) do
		totalH = totalH + measureSectionHeight(section) + cardGap
	end
	totalH = math.max(0, totalH - cardGap)

	local maxScroll = math.max(0, totalH - layout.contentH)
	pauseMenu.helpScroll = clamp(pauseMenu.helpScroll, 0, maxScroll)
	local y = layout.contentY + 4 - pauseMenu.helpScroll

	love.graphics.setColor(0.12, 0.13, 0.16, 0.9)
	love.graphics.rectangle("fill", layout.contentX, layout.contentY, layout.contentW, layout.contentH, 6, 6)
	love.graphics.setScissor(layout.contentX, layout.contentY, layout.contentW, layout.contentH)

	for sectionIndex, section in ipairs(sections) do
		local sectionH = measureSectionHeight(section)
		local cardY = y

		if cardY + sectionH >= layout.contentY and cardY <= (layout.contentY + layout.contentH) then
			local tint = 0.14 + ((sectionIndex - 1) * 0.03)
			love.graphics.setColor(tint, tint + 0.02, tint + 0.05, 0.95)
			love.graphics.rectangle("fill", cardX, cardY, cardW, sectionH, 8, 8)
			love.graphics.setColor(1, 1, 1, 0.22)
			love.graphics.rectangle("line", cardX, cardY, cardW, sectionH, 8, 8)

			love.graphics.setColor(0.85, 0.92, 1.0, 1.0)
			love.graphics.print(section.title, cardX + cardPad, cardY + cardPad)

			local itemY = cardY + cardPad + lineH + 8
			for _, item in ipairs(section.items) do
				local badgeText = "[" .. item.keys .. "]"
				local badgeW = font:getWidth(badgeText) + 12
				local badgeH = lineH + 2
				local descX = cardX + cardPad + badgeW + 10
				local descW = math.max(40, cardW - cardPad * 2 - badgeW - 10)
				local lines = getWrappedLines(font, item.text, descW)
				local textH = math.max(lineH, #lines * lineH)
				local rowH = math.max(badgeH, textH) + 4
				local badgeY = itemY + math.floor((rowH - badgeH) / 2)

				love.graphics.setColor(0.24, 0.29, 0.38, 0.95)
				love.graphics.rectangle("fill", cardX + cardPad, badgeY, badgeW, badgeH, 4, 4)
				love.graphics.setColor(1, 1, 1, 1)
				love.graphics.printf(badgeText, cardX + cardPad, badgeY + 1, badgeW, "center")

				love.graphics.setColor(0.95, 0.95, 0.95, 0.98)
				love.graphics.printf(table.concat(lines, "\n"), descX, itemY + 2, descW, "left")

				itemY = itemY + rowH + 6
			end
		end

		y = y + sectionH + cardGap
	end

	love.graphics.setScissor()

	if maxScroll > 0 then
		local barX = layout.contentX + layout.contentW - 6
		local barY = layout.contentY + 4
		local barH = layout.contentH - 8
		local visibleRatio = math.max(0.05, math.min(1, layout.contentH / math.max(layout.contentH, totalH)))
		local thumbH = math.max(16, barH * visibleRatio)
		local thumbY = barY + (barH - thumbH) * (pauseMenu.helpScroll / maxScroll)

		love.graphics.setColor(1, 1, 1, 0.18)
		love.graphics.rectangle("fill", barX, barY, 2, barH)
		love.graphics.setColor(0.8, 0.9, 1.0, 0.7)
		love.graphics.rectangle("fill", barX - 1, thumbY, 4, thumbH, 2, 2)
	end
end

local function drawPauseControlsContent(layout)
	local font = love.graphics.getFont()
	local lineH = font:getHeight()
	local cardX = layout.contentX + 4
	local cardY = layout.contentY
	local cardW = layout.contentW - 8
	local cardH = layout.contentH
	local sectionH = lineH + 6
	local sectionGap = 4
	local rowGap = 6
	local rowInset = 8
	local rowPadY = 6
	local slotInsetY = 2
	local slotGap = 8
	local slotW = clamp(math.floor(cardW * 0.23), 120, 170)
	local modeTitle = {
		flight = "Flight Mode",
		walking = "Walking Mode"
	}
	pauseMenu.controlsSelection = clamp(pauseMenu.controlsSelection, 1, #controlActions)

	local function getWrappedLinesOrFallback(text, width)
		local lines = getWrappedLines(font, text, width)
		if #lines == 0 then
			return { "" }
		end
		return lines
	end

	local function buildControlsLayoutRows()
		local rows = {}
		local headers = {}
		local y = 4
		local lastMode = nil

		for i, action in ipairs(controlActions) do
			if action.mode ~= lastMode then
				table.insert(headers, {
					y = y,
					text = modeTitle[action.mode] or action.mode
				})
				y = y + sectionH + sectionGap
				lastMode = action.mode
			end

			local slot1X = cardX + cardW - rowInset - (slotW * 2) - slotGap
			local slot2X = slot1X + slotW + slotGap
			local labelX = cardX + rowInset
			local labelW = math.max(40, slot1X - labelX - 10)
			local slotTextW = math.max(40, slotW - 12)
			local selected = i == pauseMenu.controlsSelection

			local slotLines = {}
			local slotHeights = {}
			for slot = 1, 2 do
				local isListeningSlot = pauseMenu.listeningBinding and
						pauseMenu.listeningBinding.actionIndex == i and
						pauseMenu.listeningBinding.slot == slot
				local binding = action.bindings and action.bindings[slot] or nil
				local text = formatBinding(binding)
				if isListeningSlot then
					text = "Listening..."
				elseif text == "Unbound" then
					text = "-"
				end
				slotLines[slot] = getWrappedLinesOrFallback(text, slotTextW)
				slotHeights[slot] = #slotLines[slot] * lineH
			end

			local labelLines = getWrappedLinesOrFallback(action.label, labelW)
			local labelTextH = #labelLines * lineH
			local rowTextH = math.max(labelTextH, slotHeights[1], slotHeights[2])
			local rowH = math.max(lineH + 10, rowTextH + rowPadY * 2)

			rows[i] = {
				index = i,
				action = action,
				y = y,
				h = rowH,
				selected = selected,
				labelX = labelX,
				labelW = labelW,
				labelLines = labelLines,
				labelTextH = labelTextH,
				slot1X = slot1X,
				slot2X = slot2X,
				slotW = slotW,
				slotTextW = slotTextW,
				slotLines = slotLines,
				slotHeights = slotHeights
			}

			y = y + rowH + rowGap
		end

		local totalH = math.max(0, y - rowGap)
		return rows, headers, totalH
	end

	local rows, headers, totalH = buildControlsLayoutRows()

	local selectedRow = rows[pauseMenu.controlsSelection]
	local selectedTop, selectedBottom = nil, nil
	if selectedRow then
		selectedTop = selectedRow.y
		selectedBottom = selectedRow.y + selectedRow.h
	end

	local maxScroll = math.max(0, totalH - cardH)
	pauseMenu.controlsScroll = clamp(pauseMenu.controlsScroll, 0, maxScroll)

	if selectedTop and selectedBottom then
		if selectedTop < pauseMenu.controlsScroll then
			pauseMenu.controlsScroll = selectedTop
		elseif selectedBottom > (pauseMenu.controlsScroll + cardH) then
			pauseMenu.controlsScroll = selectedBottom - cardH
		end
		pauseMenu.controlsScroll = clamp(pauseMenu.controlsScroll, 0, maxScroll)
	end

	pauseMenu.controlsRowBounds = {}
	pauseMenu.controlsSlotBounds = {}

	love.graphics.setColor(0.12, 0.13, 0.16, 0.92)
	love.graphics.rectangle("fill", layout.contentX, layout.contentY, layout.contentW, layout.contentH, 6, 6)
	love.graphics.setScissor(layout.contentX, layout.contentY, layout.contentW, layout.contentH)

	for _, header in ipairs(headers) do
		local headerY = cardY + header.y - pauseMenu.controlsScroll
		if (headerY + sectionH) >= layout.contentY and headerY <= (layout.contentY + layout.contentH) then
			love.graphics.setColor(0.78, 0.86, 1.0, 0.92)
			love.graphics.print(header.text, cardX + rowInset, headerY)
		end
	end

	for i, row in ipairs(rows) do
		local rowY = cardY + row.y - pauseMenu.controlsScroll
		local rowVisible = (rowY + row.h) >= layout.contentY and rowY <= (layout.contentY + layout.contentH)

		if rowVisible then
			if row.selected then
				love.graphics.setColor(0.2, 0.28, 0.42, 0.96)
			else
				love.graphics.setColor(0.15, 0.16, 0.2, 0.9)
			end
			love.graphics.rectangle("fill", cardX, rowY, cardW, row.h, 6, 6)

			love.graphics.setColor(1, 1, 1, 0.98)
			local labelTextY = rowY + math.floor((row.h - row.labelTextH) / 2)
			love.graphics.printf(table.concat(row.labelLines, "\n"), row.labelX, labelTextY, row.labelW, "left")
		end

		pauseMenu.controlsRowBounds[i] = { x = cardX, y = rowY, w = cardW, h = row.h }
		pauseMenu.controlsSlotBounds[i] = {}

		for slot = 1, 2 do
			local slotX = slot == 1 and row.slot1X or row.slot2X
			local isActiveSlot = row.selected and pauseMenu.controlsSlot == slot
			local isListeningSlot = pauseMenu.listeningBinding and
					pauseMenu.listeningBinding.actionIndex == i and
					pauseMenu.listeningBinding.slot == slot
			local text = table.concat(row.slotLines[slot], "\n")
			local slotTextY = rowY + math.floor((row.h - row.slotHeights[slot]) / 2)

			pauseMenu.controlsSlotBounds[i][slot] = {
				x = slotX,
				y = rowY + slotInsetY,
				w = row.slotW,
				h = row.h - (slotInsetY * 2)
			}

			if rowVisible then
				if isListeningSlot then
					love.graphics.setColor(0.86, 0.6, 0.18, 0.98)
				elseif isActiveSlot then
					love.graphics.setColor(0.32, 0.4, 0.58, 0.98)
				else
					love.graphics.setColor(0.22, 0.24, 0.3, 0.95)
				end
				love.graphics.rectangle("fill", slotX, rowY + slotInsetY, row.slotW, row.h - (slotInsetY * 2), 5, 5)
				love.graphics.setColor(1, 1, 1, isListeningSlot and 1 or 0.93)
				love.graphics.printf(text, slotX + 6, slotTextY, row.slotTextW, "center")
			end
		end
	end

	love.graphics.setScissor()

	if maxScroll > 0 then
		local barX = layout.contentX + layout.contentW - 6
		local barY = layout.contentY + 4
		local barH = layout.contentH - 8
		local visibleRatio = math.max(0.05, math.min(1, layout.contentH / math.max(layout.contentH, totalH)))
		local thumbH = math.max(16, barH * visibleRatio)
		local thumbY = barY + (barH - thumbH) * (pauseMenu.controlsScroll / maxScroll)

		love.graphics.setColor(1, 1, 1, 0.18)
		love.graphics.rectangle("fill", barX, barY, 2, barH)
		love.graphics.setColor(0.8, 0.9, 1.0, 0.72)
		love.graphics.rectangle("fill", barX - 1, thumbY, 4, thumbH, 2, 2)
	end
end

local function drawPauseMenuFooter(layout)
	local footerX = layout.panelX + 18
	local footerW = layout.panelW - 36
	local y = layout.panelY + layout.panelH - layout.bottomPad + layout.footerTopPad

	love.graphics.setColor(1, 1, 1, 0.85)
	if #layout.controlsLines > 0 then
		love.graphics.printf(table.concat(layout.controlsLines, "\n"), footerX, y, footerW, "center")
		y = y + (#layout.controlsLines * layout.lineH) + layout.footerGap
	end

	if #layout.detailLines > 0 then
		love.graphics.setColor(0.9, 0.95, 1.0, 0.9)
		love.graphics.printf(table.concat(layout.detailLines, "\n"), footerX, y, footerW, "center")
		y = y + (#layout.detailLines * layout.lineH) + layout.footerGap
	end

	if #layout.statusLines > 0 then
		love.graphics.setColor(0.8, 1.0, 0.8, 1.0)
		love.graphics.printf(table.concat(layout.statusLines, "\n"), footerX, y, footerW, "center")
	end
end

local function drawPauseMenu()
	local oldFont = love.graphics.getFont()
	local titleFont = pauseTitleFont or oldFont
	local itemFont = pauseItemFont or oldFont
	local layout = buildPauseMenuLayout()

	love.graphics.setColor(0, 0, 0, 0.68)
	love.graphics.rectangle("fill", 0, 0, screen.w, screen.h)

	love.graphics.setColor(0.08, 0.08, 0.1, 0.95)
	love.graphics.rectangle("fill", layout.panelX, layout.panelY, layout.panelW, layout.panelH, 8, 8)
	love.graphics.setColor(1, 1, 1, 0.2)
	love.graphics.rectangle("line", layout.panelX, layout.panelY, layout.panelW, layout.panelH, 8, 8)

	love.graphics.setFont(titleFont)
	love.graphics.setColor(1, 1, 1, 1)
	love.graphics.printf("PAUSED", layout.panelX, layout.titleY, layout.panelW, "center")

	love.graphics.setFont(itemFont)
	drawPauseMenuTabs(layout)
	if layout.isGameTab then
		drawPauseMenuRows(layout, itemFont:getHeight())
	elseif layout.isControlsTab then
		pauseMenu.itemBounds = {}
		drawPauseControlsContent(layout)
	else
		pauseMenu.itemBounds = {}
		drawPauseHelpContent(layout)
	end
	drawPauseMenuFooter(layout)

	love.graphics.setFont(oldFont)
end

function makeRng(seed)
    -- Park-Miller LCG using Schrage method (LuaJIT/Lua 5.1 compatible).
    local m = 2147483647
    local a = 16807
    local q = 127773
    local r = 2836
    local state = math.floor(tonumber(seed) or 1) % m
    if state <= 0 then
        state = state + (m - 1)
    end

    local function u32()
        local hi = math.floor(state / q)
        local lo = state - hi * q
        local test = (a * lo) - (r * hi)
        if test > 0 then
            state = test
        else
            state = test + m
        end
        return state
    end

    return {
        u32 = u32,
        -- [0,1)
        rand01 = function()
            return (u32() - 1) / (m - 1)
        end,
        -- inclusive integers
        randint = function(_, x, y)
            local low = math.floor(tonumber(x) or 0)
            local high = math.floor(tonumber(y) or 0)
            if high < low then
                low, high = high, low
            end
            local span = high - low + 1
            return low + math.floor(((u32() - 1) / (m - 1)) * span)
        end
    }
end

CELL_GRASS = 0
CELL_ROAD  = 1
CELL_FIELD = 2

function generateCellGrid(params)
    -- params: seed, gridCount, roadCount, roadDensity(0..1), fieldCount, fieldMinSize, fieldMaxSize
    local rng = makeRng(params.seed or 1)
    local n = params.gridCount
    local grid = {}
    for x = 1, n do
        grid[x] = {}
        for z = 1, n do
            grid[x][z] = CELL_GRASS
        end
    end

    local function inBounds(x, z)
        return x >= 1 and x <= n and z >= 1 and z <= n
    end

    -- --- Roads ---
    local roadCount = math.max(0, math.floor(params.roadCount or 3))
    local roadDensity = clamp(params.roadDensity or 0.15, 0, 1)

    -- total carves roughly proportional to density * cells
    local totalSteps = math.floor(roadDensity * n * n)
    local stepsPerRoad = (roadCount > 0) and math.max(1, math.floor(totalSteps / roadCount)) or 0

    for i = 1, roadCount do
        -- start on a random edge
        local side = rng:randint(1, 4)
        local x, z
        if side == 1 then x, z = 1, rng:randint(1, n)
        elseif side == 2 then x, z = n, rng:randint(1, n)
        elseif side == 3 then x, z = rng:randint(1, n), 1
        else x, z = rng:randint(1, n), n
        end

        local dirX, dirZ = 0, 0
        local function randomDir()
            local d = rng:randint(1, 4)
            if d == 1 then return 1, 0 end
            if d == 2 then return -1, 0 end
            if d == 3 then return 0, 1 end
            return 0, -1
        end
        dirX, dirZ = randomDir()

        for s = 1, stepsPerRoad do
            if inBounds(x, z) then
                grid[x][z] = CELL_ROAD
            end

            -- occasionally turn; higher density -> slightly straighter looks better, so keep turn chance modest
            local turnChance = 0.18
            if rng:rand01() < turnChance then
                dirX, dirZ = randomDir()
            end

            x, z = x + dirX, z + dirZ
            if not inBounds(x, z) then
                -- bounce back in bounds
                x = clamp(x, 1, n)
                z = clamp(z, 1, n)
                dirX, dirZ = randomDir()
            end
        end
    end

    -- --- Fields (patches) ---
    local fieldCount   = params.fieldCount or 6
    local fieldMinSize = params.fieldMinSize or math.floor(n * 0.8)
    local fieldMaxSize = params.fieldMaxSize or math.floor(n * 2.0)

    for i = 1, fieldCount do
        local sx, sz = rng:randint(1, n), rng:randint(1, n)
        local targetSize = rng:randint(fieldMinSize, fieldMaxSize)

        local queue = { {sx, sz} }
        local qi = 1
        local placed = 0

        while qi <= #queue and placed < targetSize do
            local x, z = queue[qi][1], queue[qi][2]
            qi = qi + 1

            if inBounds(x, z) and grid[x][z] ~= CELL_ROAD and grid[x][z] ~= CELL_FIELD then
                grid[x][z] = CELL_FIELD
                placed = placed + 1

                -- grow outward randomly
                if rng:rand01() < 0.80 then queue[#queue+1] = {x+1, z} end
                if rng:rand01() < 0.80 then queue[#queue+1] = {x-1, z} end
                if rng:rand01() < 0.80 then queue[#queue+1] = {x, z+1} end
                if rng:rand01() < 0.80 then queue[#queue+1] = {x, z-1} end
            end
        end
    end

    return grid, rng
end

cloneColor3 = function(value)
	return { value[1], value[2], value[3] }
end

sanitizeColor3 = function(value, fallback)
	local out = cloneColor3(fallback)
	if type(value) == "table" then
		out[1] = clamp(tonumber(value[1]) or out[1], 0, 1)
		out[2] = clamp(tonumber(value[2]) or out[2], 0, 1)
		out[3] = clamp(tonumber(value[3]) or out[3], 0, 1)
	end
	return out
end

normalizeGroundParams = function(params)
	local src = params or defaultGroundParams
	local normalized = {
		seed = math.floor(tonumber(src.seed) or defaultGroundParams.seed),
		tileSize = math.max(0.05, tonumber(src.tileSize) or defaultGroundParams.tileSize),
		gridCount = math.max(1, math.floor(tonumber(src.gridCount) or defaultGroundParams.gridCount)),
		baseHeight = tonumber(src.baseHeight) or defaultGroundParams.baseHeight,
		tileThickness = math.max(0.0001, tonumber(src.tileThickness) or defaultGroundParams.tileThickness),
		roadCount = math.max(0, math.floor(tonumber(src.roadCount) or defaultGroundParams.roadCount)),
		roadDensity = clamp(tonumber(src.roadDensity) or defaultGroundParams.roadDensity, 0, 1),
		fieldCount = math.max(0, math.floor(tonumber(src.fieldCount) or defaultGroundParams.fieldCount)),
		fieldMinSize = math.max(1, math.floor(tonumber(src.fieldMinSize) or defaultGroundParams.fieldMinSize)),
		fieldMaxSize = math.max(1, math.floor(tonumber(src.fieldMaxSize) or defaultGroundParams.fieldMaxSize)),
		grassColor = sanitizeColor3(src.grassColor, defaultGroundParams.grassColor),
		roadColor = sanitizeColor3(src.roadColor, defaultGroundParams.roadColor),
		fieldColor = sanitizeColor3(src.fieldColor, defaultGroundParams.fieldColor),
		grassVar = sanitizeColor3(src.grassVar, defaultGroundParams.grassVar),
		roadVar = sanitizeColor3(src.roadVar, defaultGroundParams.roadVar),
		fieldVar = sanitizeColor3(src.fieldVar, defaultGroundParams.fieldVar)
	}
	normalized.fieldMaxSize = math.max(normalized.fieldMinSize, normalized.fieldMaxSize)
	return normalized
end

function colorsEqual(a, b)
	local eps = 1e-6
	for i = 1, 3 do
		if math.abs((a[i] or 0) - (b[i] or 0)) > eps then
			return false
		end
	end
	return true
end

groundParamsEqual = function(a, b)
	if not a or not b then
		return false
	end
	return a.seed == b.seed and
		a.tileSize == b.tileSize and
		a.gridCount == b.gridCount and
		a.baseHeight == b.baseHeight and
		a.tileThickness == b.tileThickness and
		a.roadCount == b.roadCount and
		a.roadDensity == b.roadDensity and
		a.fieldCount == b.fieldCount and
		a.fieldMinSize == b.fieldMinSize and
		a.fieldMaxSize == b.fieldMaxSize and
		colorsEqual(a.grassColor, b.grassColor) and
		colorsEqual(a.roadColor, b.roadColor) and
		colorsEqual(a.fieldColor, b.fieldColor) and
		colorsEqual(a.grassVar, b.grassVar) and
		colorsEqual(a.roadVar, b.roadVar) and
		colorsEqual(a.fieldVar, b.fieldVar)
end

function generateGroundMeshModel(params)
	local tileSize = params.tileSize
	local gridCount = params.gridCount
	local half = tileSize / 2

	local grid, rng = generateCellGrid(params)

	local grass = params.grassColor
	local road = params.roadColor
	local field = params.fieldColor

	local grassVar = params.grassVar
	local roadVar = params.roadVar
	local fieldVar = params.fieldVar

	local function varied(base, var)
		return {
			clamp(base[1] + (rng:rand01() * 2 - 1) * var[1], 0, 1),
			clamp(base[2] + (rng:rand01() * 2 - 1) * var[2], 0, 1),
			clamp(base[3] + (rng:rand01() * 2 - 1) * var[3], 0, 1)
		}
	end

	local vertices, faces = {}, {}
	local vertexColors, faceColors = {}, {}

	for gx = 1, gridCount do
		for gz = 1, gridCount do
			local x = (gx - 1) - gridCount / 2
			local z = (gz - 1) - gridCount / 2
			local posX = x * tileSize + half
			local posZ = z * tileSize + half

			local cellType = grid[gx][gz]
			local c
			if cellType == CELL_ROAD then
				c = varied(road, roadVar)
			elseif cellType == CELL_FIELD then
				c = varied(field, fieldVar)
			else
				c = varied(grass, grassVar)
			end
			local rgba = { c[1], c[2], c[3], 1.0 }

			local base = #vertices
			vertices[base + 1] = { posX - half, 0, posZ - half }
			vertices[base + 2] = { posX + half, 0, posZ - half }
			vertices[base + 3] = { posX + half, 0, posZ + half }
			vertices[base + 4] = { posX - half, 0, posZ + half }

			vertexColors[base + 1] = rgba
			vertexColors[base + 2] = rgba
			vertexColors[base + 3] = rgba
			vertexColors[base + 4] = rgba

			faces[#faces + 1] = { base + 1, base + 2, base + 3 }
			faceColors[#faceColors + 1] = rgba
			faces[#faces + 1] = { base + 1, base + 3, base + 4 }
			faceColors[#faceColors + 1] = rgba
		end
	end

	return {
		vertices = vertices,
		faces = faces,
		vertexColors = vertexColors,
		faceColors = faceColors,
		isSolid = true
	}
end

function createGroundObject(params)
	local halfExtent = (params.gridCount * params.tileSize) * 0.5
	return {
		model = generateGroundMeshModel(params),
		pos = { 0, params.baseHeight, 0 },
		rot = q.identity(),
		scale = { 1, 1, 1 },
		color = { 1, 1, 1, 1 },
		isSolid = true,
		isGround = true,
		halfSize = { x = halfExtent, y = params.tileThickness, z = halfExtent }
	}
end

rebuildGroundFromParams = function(params, reason)
	local normalized = normalizeGroundParams(params)
	if activeGroundParams and groundParamsEqual(activeGroundParams, normalized) then
		return false
	end

	activeGroundParams = normalized
	if groundObject then
		for i = #objects, 1, -1 do
			if objects[i] == groundObject or objects[i].isGround then
				table.remove(objects, i)
			end
		end
	end

	groundObject = createGroundObject(normalized)
	local insertIndex = 1
	if localPlayerObject and objects[1] == localPlayerObject then
		insertIndex = 2
	end
	table.insert(objects, insertIndex, groundObject)

	worldHalfExtent = (normalized.tileSize * normalized.gridCount) * 0.5
	mapState.zoomExtents = { 160, 420, math.max(worldHalfExtent, 420) }
	mapState.logicalCamera = nil
	if reason and reason ~= "" then
		logger.log(string.format(
			"Ground rebuilt (%s): seed=%d tile=%.3f grid=%d",
			reason,
			normalized.seed,
			normalized.tileSize,
			normalized.gridCount
		))
	end

	return true
end

-- === Initial Configuration ===
function love.load()
	-- 80% screen size
	local width, height = love.window.getDesktopDimensions()
	width, height = math.floor(width * 0.8), math.floor(height * 0.8)

	love.window.setTitle("Don's 3D Engine")
	local modeOk, modeErr = love.window.setMode(width, height, { vsync = false, depth = 24 })
	if not modeOk then
		love.window.setMode(width, height, { vsync = false })
	end
	love.keyboard.setKeyRepeat(true)
	setMouseCapture(true)

	logger.reset()
	logger.log("Booting Don's 3D Engine")
	logger.log("Log file: " .. logger.getPath())
	if not modeOk then
		logger.log("Depth buffer mode unavailable, fallback mode set. Detail: " .. tostring(modeErr))
	end

	screen = {
		w = width,
		h = height
	}
	refreshUiFonts()

	camera = {
		pos = { 0, 0, -5 },
		rot = q.identity(),
		speed = 10,
		fov = math.rad(90),
		baseFov = math.rad(90),
		zoomFactor = 0.58,
		zoomLerpSpeed = 12,
		vel = { 0, 0, 0 }, -- current velocity
		onGround = false,  -- contacting
		gravity = -9.81,   -- units/sec^2
		jumpSpeed = 5,     -- initial jump
		throttle = 0,
		maxSpeed = 34,
		throttleAccel = 18,
		flightThrustAccel = 32,
		flightDragCoefficient = 0.018,
		flightAirBrakeDrag = 0.11,
		flightLiftCoefficient = 0.11,
		flightZeroLiftAngle = 0,
		flightMaxLiftAngle = math.rad(20),
		flightStallSpeed = 8,
		flightFullLiftSpeed = 24,
		flightInducedDragCoefficient = 0.0035,
		flightGravity = -9.81,
		flightMaxVelocity = 92,
		flightVel = { 0, 0, 0 },
		afterburnerMultiplier = 1.45,
		airBrakeStrength = 65,
		flightThrottleDamping = 5,
		wheelThrottleStep = 2,
		flightPitchRate = 78,
		flightYawRate = 10,
		flightRollRate = 95,
		flightMousePitchMultiplier = 1.9,
		flightMouseYawMultiplier = 3.5,
		flightMouseRollMultiplier = 1.2,
		sprintMultiplier = 1.8,
		walkMousePitchMultiplier = 2.4,
		walkMouseYawMultiplier = 2.8,
		allowWalkRoll = false,
		walkTiltRate = 40,
		box = {
			halfSize = { x = 0.5, y = 1.8, z = 0.5 }, -- width/height/depth half extents
			pos = { 0, 0, -5 },                       -- center at camera
			isSolid = true
		}
	}
	syncWalkingLookFromRotation()
	viewState.mode = "first_person"
	viewState.activeCamera = camera
	resetAltLookState()
	mapState.visible = false
	mapState.mHeld = false
	mapState.mUsedForZoom = false
	mapState.zoomIndex = 2
	mapState.logicalCamera = nil
	cloudNetState.role = "standalone"
	cloudNetState.connectedAt = 0
	cloudNetState.sawPeerOnJoin = false
	cloudNetState.authorityPeerId = nil
	cloudNetState.lastSnapshotSentAt = -math.huge
	cloudNetState.lastSnapshotReceivedAt = -math.huge
	cloudNetState.lastSnapshotRequestAt = -math.huge
	groundNetState.authorityPeerId = nil
	groundNetState.lastSnapshotReceivedAt = -math.huge
	groundNetState.lastSnapshotRequestAt = -math.huge

	renderMode = "CPU"
	gpuErrorLogged = false
	perfElapsed, perfFrames, perfLoggedLowFps = 0, 0, false
	pauseMenu.active = false
	pauseMenu.tab = "game"
	pauseMenu.selected = 1
	pauseMenu.itemBounds = {}
	pauseMenu.tabBounds = {}
	pauseMenu.helpScroll = 0
	pauseMenu.controlsScroll = 0
	pauseMenu.controlsSelection = 1
	pauseMenu.controlsSlot = 1
	pauseMenu.controlsRowBounds = {}
	pauseMenu.controlsSlotBounds = {}
	pauseMenu.listeningBinding = nil
	clearPauseRangeDrag()
	pauseMenu.statusText = ""
	pauseMenu.statusUntil = 0
	pauseMenu.confirmItemId = nil
	pauseMenu.confirmUntil = 0

	loadPlayerModelFromStl("objects/Dualengine.stl")

	-- Local avatar is synced to player camera each update; hidden in first-person, shown in third-person.
	localPlayerObject = {
		model = playerModel,
		pos = { camera.pos[1], camera.pos[2], camera.pos[3] },
		rot = composePlayerModelRotation(camera.rot),
		scale = { 0.9, 0.9, 0.9 },
		color = { 0.9, 0.4, 0.1 },
		isSolid = false,
		isLocalPlayer = true
	}

	objects = {
		[1] = localPlayerObject
	}

	rebuildGroundFromParams(defaultGroundParams, "startup")

	windState.angle = randomRange(0, math.pi * 2)
	windState.speed = randomRange(5, 10)
	pickNextWindTarget()
	spawnCloudField()
	logger.log(string.format("Cloud field initialized (%d groups).", #cloudState.groups))

	zBuffer = {}
	for i = 1, screen.w * screen.h do
		zBuffer[i] = math.huge
	end

	if not relay then
		logger.log("ENet host creation failed; multiplayer disabled for this session.")
	elseif not relayServer then
		logger.log("Could not connect to relay server at " .. hostAddy)
	else
		logger.log("Connected to relay server: " .. hostAddy)
	end

	local gpuOk, gpuErr = renderer.init(screen, camera, logger.log)
	if gpuOk then
		renderMode = "GPU"
		logger.log("GPU renderer enabled.")
	else
		useGpuRenderer = false
		renderMode = "CPU"
		logger.log("GPU renderer failed, using CPU fallback: " .. tostring(gpuErr))
	end

	resolveActiveRenderCamera()
end

-- === Mouse Look ===
local function buildMovementInputState()
	local throttleBlocked = mapState.mHeld and true or false
	return {
		flightPitchDown = isActionDown("flight_pitch_down"),
		flightPitchUp = isActionDown("flight_pitch_up"),
		flightRollLeft = isActionDown("flight_roll_left"),
		flightRollRight = isActionDown("flight_roll_right"),
		flightYawLeft = isActionDown("flight_yaw_left"),
		flightYawRight = isActionDown("flight_yaw_right"),
		flightThrottleDown = (not throttleBlocked) and isActionDown("flight_throttle_down"),
		flightThrottleUp = (not throttleBlocked) and isActionDown("flight_throttle_up"),
		flightAfterburner = isActionDown("flight_afterburner"),
		flightAirBrakes = isActionDown("flight_air_brakes"),
		walkForward = isActionDown("walk_forward"),
		walkBackward = isActionDown("walk_backward"),
		walkStrafeLeft = isActionDown("walk_strafe_left"),
		walkStrafeRight = isActionDown("walk_strafe_right"),
		walkSprint = isActionDown("walk_sprint"),
		walkJump = isActionDown("walk_jump")
	}
end

local function applyCameraRotations(pitchAngle, yawAngle, rollAngle)
	local rot = camera.rot
	if pitchAngle ~= 0 then
		local right = q.rotateVector(rot, { 1, 0, 0 })
		rot = q.normalize(q.multiply(q.fromAxisAngle(right, pitchAngle), rot))
	end
	if yawAngle ~= 0 then
		local up = q.rotateVector(rot, { 0, 1, 0 })
		rot = q.normalize(q.multiply(q.fromAxisAngle(up, yawAngle), rot))
	end
	if rollAngle ~= 0 then
		local forward = q.rotateVector(rot, { 0, 0, 1 })
		rot = q.normalize(q.multiply(q.fromAxisAngle(forward, rollAngle), rot))
	end
	camera.rot = rot
end

local function applyWalkingMouseLook(dx, dy, modifiers)
	local lookDown = getActionMouseAxisValue("walk_look_down", dx, dy, modifiers)
	local lookUp = getActionMouseAxisValue("walk_look_up", dx, dy, modifiers)
	local lookLeft = getActionMouseAxisValue("walk_look_left", dx, dy, modifiers)
	local lookRight = getActionMouseAxisValue("walk_look_right", dx, dy, modifiers)

	local pitchAxis = lookDown - lookUp
	local yawAxis = lookRight - lookLeft
	if invertLookY then
		pitchAxis = -pitchAxis
	end

	camera.walkYaw = camera.walkYaw or 0
	camera.walkPitch = camera.walkPitch or 0

	local pitchMult = camera.walkMousePitchMultiplier or 2.2
	local yawMult = camera.walkMouseYawMultiplier or 2.2
	local pitchLimit = walkingPitchLimit

	camera.walkPitch = clamp(
		camera.walkPitch + (pitchAxis * mouseSensitivity * pitchMult),
		-pitchLimit,
		pitchLimit
	)
	camera.walkYaw = wrapAngle(camera.walkYaw + (yawAxis * mouseSensitivity * yawMult))
	camera.rot = composeWalkingRotation(camera.walkYaw, camera.walkPitch)
end

local function applyFlightMouseLook(dx, dy, modifiers)
	local pitchDown = getActionMouseAxisValue("flight_pitch_down", dx, dy, modifiers)
	local pitchUp = getActionMouseAxisValue("flight_pitch_up", dx, dy, modifiers)
	local rollLeft = getActionMouseAxisValue("flight_roll_left", dx, dy, modifiers)
	local rollRight = getActionMouseAxisValue("flight_roll_right", dx, dy, modifiers)
	local yawLeft = getActionMouseAxisValue("flight_yaw_left", dx, dy, modifiers)
	local yawRight = getActionMouseAxisValue("flight_yaw_right", dx, dy, modifiers)

	local pitchAxis = pitchDown - pitchUp
	local yawAxis = yawRight - yawLeft
	local rollAxis = rollLeft - rollRight

	if invertLookY then
		pitchAxis = -pitchAxis
	end

	local pitchAngle = pitchAxis * mouseSensitivity * (camera.flightMousePitchMultiplier or 1.6)
	local yawAngle = yawAxis * mouseSensitivity * (camera.flightMouseYawMultiplier or 3.0)
	local rollAngle = rollAxis * mouseSensitivity * (camera.flightMouseRollMultiplier or 1.0)
	applyCameraRotations(pitchAngle, yawAngle, rollAngle)
end

local function updateZoomFov(dt)
	local baseFov = camera.baseFov or camera.fov
	local targetFov = baseFov
	if flightSimMode and isActionDown("flight_zoom_in") then
		local zoomFactor = camera.zoomFactor or 0.6
		targetFov = math.max(math.rad(20), baseFov * zoomFactor)
	end

	local lerpSpeed = camera.zoomLerpSpeed or 10
	local alpha = clamp(lerpSpeed * dt, 0, 1)
	camera.fov = camera.fov + (targetFov - camera.fov) * alpha
end

local function adjustFlightThrottleFromWheel(direction)
	if not flightSimMode then
		return
	end

	local maxThrottle = camera.maxSpeed
	if isActionDown("flight_afterburner") then
		maxThrottle = maxThrottle * (camera.afterburnerMultiplier or 1.6)
	end

	local step = camera.wheelThrottleStep or 2
	camera.throttle = clamp(camera.throttle + (step * direction), 0, maxThrottle)
end

local lastProjectileTriggerAt = -999

local function triggerProjectileAction()
	local now = love.timer.getTime()
	if (now - lastProjectileTriggerAt) >= 0.2 then
		lastProjectileTriggerAt = now
		logger.log("Projectile fire input received (projectile spawning not implemented yet).")
	end
end

function love.mousemoved(x, y, dx, dy)
	if pauseMenu.active then
		if pauseMenu.tab == "controls" and pauseMenu.listeningBinding then
			captureBindingFromMouseMotion(dx, dy)
			return
		end

		updatePauseMenuHover(x, y)
		updatePauseControlsHover(x, y)
		updatePauseRangeDrag(x)
		return
	end

	if not relative then
		return
	end

	if viewState.mode == "first_person" and altLookState.held then
		applyAltLookMouse(dx, dy)
		return
	end

	local modifiers = getCurrentModifiers()
	if flightSimMode then
		applyFlightMouseLook(dx, dy, modifiers)
	else
		applyWalkingMouseLook(dx, dy, modifiers)
	end
end

local function updateNet()
	local canSend = relayIsConnected()

	if canSend then
		local packet = string.format(
			"STATE|%f|%f|%f|%f|%f|%f|%f",
			camera.pos[1],
			camera.pos[2],
			camera.pos[3],
			camera.rot.w,
			camera.rot.x,
			camera.rot.y,
			camera.rot.z
		)
		pcall(function()
			relayServer:send(packet)
		end)

		sendCloudSnapshot(false)
	end
end

local function serviceNetworkEvents()
	if not relay then
		return
	end

	event = relay:service()
	while event do
		if event.type == "connect" and relayServer and event.peer == relayServer then
			beginCloudRoleElection("relay handshake complete")
		elseif event.type == "receive" then
			local packetType = type(event.data) == "string" and event.data:match("^([^|]+)")
			if packetType == "STATE" then
				local peerId = networking.handlePacket(
					event.data,
					peers,
					objects,
					q,
					playerModel,
					playerModelRotationOffset
				)
				notePeerStateReceived(peerId)
			elseif packetType == "CLOUD_REQUEST" then
				handleCloudRequestPacket(event.data)
			elseif packetType == "CLOUD_SNAPSHOT" then
				handleCloudSnapshotPacket(event.data)
			elseif packetType == "GROUND_REQUEST" then
				handleGroundRequestPacket(event.data)
			elseif packetType == "GROUND_SNAPSHOT" then
				handleGroundSnapshotPacket(event.data)
			end
		elseif event.type == "disconnect" and relayServer and event.peer == relayServer then
			relayServer = nil
			clearPeerObjects()
			cloudNetState.authorityPeerId = nil
			groundNetState.authorityPeerId = nil
			groundNetState.lastSnapshotReceivedAt = -math.huge
			groundNetState.lastSnapshotRequestAt = -math.huge
			setCloudRole("standalone", "relay disconnected")
			logger.log("Relay disconnected.")
		end

		event = relay:service()
	end
end

-- === Camera Movement ===
function love.update(dt)
	local now = love.timer.getTime()
	updateCloudNetworkState(now)

	if pauseMenu.statusUntil > 0 and love.timer.getTime() >= pauseMenu.statusUntil then
		pauseMenu.statusText = ""
		pauseMenu.statusUntil = 0
	end
	if pauseMenu.confirmItemId and love.timer.getTime() > pauseMenu.confirmUntil then
		clearPauseConfirm()
	end

	if not pauseMenu.active then
		local movementInput = buildMovementInputState()
		camera = engine.processMovement(camera, dt, flightSimMode, vector3, q, objects, movementInput, {
			wind = getWindVector3()
		})
		syncLocalPlayerObject()
		updateClouds(dt)
		updateZoomFov(dt)

		perfElapsed = perfElapsed + dt
		perfFrames = perfFrames + 1
		if perfElapsed >= 2.0 then
			local avgFps = perfFrames / perfElapsed
			if useGpuRenderer and renderer.isReady() and avgFps < 100 and not perfLoggedLowFps then
				logger.log(string.format("FPS below 100 target in GPU mode (avg %.1f)", avgFps))
				perfLoggedLowFps = true
			elseif useGpuRenderer and renderer.isReady() and avgFps >= 100 then
				perfLoggedLowFps = false
			end
			perfElapsed = 0
			perfFrames = 0
		end
	end

	updateNet()
	resolveActiveRenderCamera()
	serviceNetworkEvents()
	updateCloudNetworkState(love.timer.getTime())
end

-- === Input Management ===
function love.keypressed(key, scancode, isrepeat)
	if pauseMenu.active then
		if pauseMenu.tab == "controls" and pauseMenu.listeningBinding then
			if key == "escape" then
				cancelControlBindingCapture()
			else
				captureBindingFromKey(key)
			end
			return
		end

		if key == "escape" then
			setPauseState(false)
			return
		end

		if key == "tab" or key == "h" then
			cyclePauseTab(1)
			return
		end

		if pauseMenu.tab == "help" then
			if key == "up" or key == "w" then
				pauseMenu.helpScroll = math.max(0, pauseMenu.helpScroll - 24)
			elseif key == "down" or key == "s" then
				pauseMenu.helpScroll = pauseMenu.helpScroll + 24
			elseif key == "left" or key == "a" then
				cyclePauseTab(-1)
			elseif key == "right" or key == "d" then
				cyclePauseTab(1)
			elseif key == "home" then
				pauseMenu.helpScroll = 0
			end
			return
		end

		if pauseMenu.tab == "controls" then
			if key == "up" or key == "w" then
				moveControlsSelection(-1)
			elseif key == "down" or key == "s" then
				moveControlsSelection(1)
			elseif key == "left" or key == "a" then
				moveControlsSlot(-1)
			elseif key == "right" or key == "d" then
				moveControlsSlot(1)
			elseif key == "home" then
				pauseMenu.controlsSelection = 1
				pauseMenu.controlsScroll = 0
			elseif key == "end" then
				pauseMenu.controlsSelection = #controlActions
			elseif key == "return" or key == "kpenter" or key == "space" then
				beginSelectedControlBindingCapture(pauseMenu.controlsSelection, pauseMenu.controlsSlot)
			elseif key == "backspace" or key == "delete" then
				clearSelectedControlBinding()
			elseif key == "r" then
				resetControlBindingsToDefaults()
				setPauseStatus("Controls reset to defaults.", 1.7)
			end
			return
		end

		if key == "up" or key == "w" then
			movePauseSelection(-1)
		elseif key == "down" or key == "s" then
			movePauseSelection(1)
		elseif key == "home" then
			pauseMenu.selected = 1
			clearPauseConfirm()
		elseif key == "end" then
			pauseMenu.selected = #pauseMenu.items
			clearPauseConfirm()
		elseif key == "left" or key == "a" then
			adjustSelectedPauseItem(-1)
		elseif key == "right" or key == "d" then
			adjustSelectedPauseItem(1)
		elseif key == "pageup" then
			adjustSelectedPauseItemCoarse(1)
		elseif key == "pagedown" then
			adjustSelectedPauseItemCoarse(-1)
		elseif key == "return" or key == "kpenter" or key == "space" then
			activateSelectedPauseItem()
		end
		return
	end

	if key == "escape" then
		setPauseState(true)
		return
	end
	if key == "lalt" then
		if not isrepeat and viewState.mode == "first_person" then
			altLookState.held = true
			altLookState.yaw = 0
			altLookState.pitch = 0
		end
		return
	end
	if key == "m" then
		if not isrepeat then
			mapState.mHeld = true
			mapState.mUsedForZoom = false
		end
		return
	end
	if mapState.mHeld and (key == "up" or key == "down") then
		local delta = key == "up" and -1 or 1
		mapState.zoomIndex = clamp(mapState.zoomIndex + delta, 1, 3)
		mapState.mUsedForZoom = true
		updateLogicalMapCamera()
		return
	end
	if key == "c" then
		if not isrepeat then
			cycleViewMode()
		end
		return
	end

	if flightSimMode and actionTriggeredByKey("flight_fire_projectile", key, getCurrentModifiers()) then
		triggerProjectileAction()
	end

	-- debugging position/rotation and relating variables
	if key == "p" then
		local pos = camera.pos
		local rot = camera.rot
		local forward, right, up = engine.getCameraBasis(camera, q, vector3)

		print("\n=== Camera Debug Info ===")
		print(string.format("Position:    x=%.3f  y=%.3f  z=%.3f", pos[1], pos[2], pos[3]))
		print(string.format("Rotation:    w=%.5f  x=%.5f  y=%.5f  z=%.5f", rot.w, rot.x, rot.y, rot.z))
		print(string.format("Forward vec: x=%.3f  y=%.3f  z=%.3f", forward[1], forward[2], forward[3]))
		print(string.format("Right vec:   x=%.3f  y=%.3f  z=%.3f", right[1], right[2], right[3]))
		print(string.format("Up vec:      x=%.3f  y=%.3f  z=%.3f", up[1], up[2], up[3]))
	end

	if key == "f" then
		setFlightMode(not flightSimMode)
	end

	if key == "g" then
		setRendererPreference(not useGpuRenderer)
	end
end

function love.keyreleased(key)
	if key == "lalt" then
		resetAltLookState()
		resolveActiveRenderCamera()
		return
	end

	if key == "m" then
		local shouldToggle = mapState.mHeld and (not mapState.mUsedForZoom) and (not pauseMenu.active)
		mapState.mHeld = false
		mapState.mUsedForZoom = false
		if shouldToggle then
			mapState.visible = not mapState.visible
		end
	end
end

function love.mousepressed(x, y, button)
	if pauseMenu.active then
		if pauseMenu.tab == "controls" and pauseMenu.listeningBinding then
			captureBindingFromMouseButton(button)
			return
		end

		if button == 1 then
			handlePauseMenuMouseClick(x, y)
		end
		return
	end

	if flightSimMode and actionTriggeredByMouseButton("flight_fire_projectile", button, getCurrentModifiers()) then
		triggerProjectileAction()
	end

	if not love.mouse.getRelativeMode() then
		setMouseCapture(true)
	end
end

function love.mousereleased(x, y, button)
	if not pauseMenu.active or button ~= 1 then
		return
	end
	endPauseRangeDrag(x)
end

function love.wheelmoved(x, y)
	if y == 0 then
		return
	end

	if pauseMenu.active then
		if pauseMenu.tab == "controls" and pauseMenu.listeningBinding then
			captureBindingFromWheel(y)
			return
		end

		if pauseMenu.tab == "help" then
			pauseMenu.helpScroll = math.max(0, pauseMenu.helpScroll - (y * 24))
			return
		end

		if pauseMenu.tab == "controls" then
			pauseMenu.controlsScroll = math.max(0, pauseMenu.controlsScroll - (y * 24))
			return
		end

		local item = pauseMenu.items[pauseMenu.selected]
		if item and item.kind == "range" then
			adjustPauseItem(item, y > 0 and 1 or -1)
		end
		return
	end

	local modifiers = getCurrentModifiers()
	if actionTriggeredByWheel("flight_throttle_up", y, modifiers) then
		adjustFlightThrottleFromWheel(1)
	end
	if actionTriggeredByWheel("flight_throttle_down", y, modifiers) then
		adjustFlightThrottleFromWheel(-1)
	end
end

function love.mousefocus(focused)
	if not focused then
		setMouseCapture(false)
	end
end

function love.focus(focused)
	if not focused and not pauseMenu.active then
		setPauseState(true)
		setPauseStatus("Paused: window focus lost.", 1.8)
	end
end

local function drawHud(w, h, cx, cy)
	if flightSimMode and camera then
		local currentMaxThrottle = math.max(0.001, camera.maxSpeed or 1)
		if isActionDown("flight_afterburner") then
			currentMaxThrottle = currentMaxThrottle * (camera.afterburnerMultiplier or 1.6)
		end

		local throttleRatio = clamp((camera.throttle or 0) / currentMaxThrottle, 0, 1)
		local barHeight = math.floor(math.min(h * 0.34, 240))
		local barWidth = 18
		local barX = 24
		local barY = math.floor((h - barHeight) * 0.5)
		local handleSize = barWidth + 8
		local handleY = barY + (1 - throttleRatio) * barHeight - (handleSize * 0.5)
		handleY = clamp(handleY, barY - (handleSize * 0.5), barY + barHeight - (handleSize * 0.5))

		love.graphics.setColor(0.05, 0.07, 0.08, 0.75)
		love.graphics.rectangle("fill", barX, barY, barWidth, barHeight, 3, 3)
		love.graphics.setColor(0.75, 0.85, 0.92, 0.9)
		love.graphics.rectangle("line", barX, barY, barWidth, barHeight, 3, 3)

		local fillTop = barY + (1 - throttleRatio) * barHeight
		love.graphics.setColor(0.22, 0.82, 0.36, 0.65)
		love.graphics.rectangle("fill", barX + 1, fillTop, barWidth - 2, (barY + barHeight) - fillTop)

		love.graphics.setColor(0.95, 0.97, 1.0, 0.95)
		love.graphics.rectangle("fill", barX - 4, handleY, handleSize, handleSize, 2, 2)
		love.graphics.setColor(0.10, 0.12, 0.15, 0.95)
		love.graphics.rectangle("line", barX - 4, handleY, handleSize, handleSize, 2, 2)

		love.graphics.setColor(0.86, 0.92, 1.0, 0.95)
		love.graphics.print(string.format("THR %d%%", math.floor((throttleRatio * 100) + 0.5)), barX - 8, barY + barHeight + 6)
	end

	if not mapState.visible then
		return
	end

	local mapCam = updateLogicalMapCamera()
	if not mapCam then
		return
	end

	local panelSize = clamp(
		math.floor(math.min(w, h) * mapState.panel.widthRatio),
		mapState.panel.minSize,
		mapState.panel.maxSize
	)
	local panelX = w - mapState.panel.margin - panelSize
	local panelY = mapState.panel.margin
	local panelCenterX = panelX + panelSize * 0.5
	local panelCenterY = panelY + panelSize * 0.5
	local extent = math.max(1, mapCam.extent)
	local worldToPixel = (panelSize * 0.5) / extent
	local zoomLabels = { "Near", "Mid", "Full" }
	local markerSizes = { 4, 5, 7 }
	local localMarkerSize = markerSizes[mapState.zoomIndex] or 5
	local peerMarkerSize = math.max(2, localMarkerSize - 1)

	love.graphics.setColor(0.03, 0.05, 0.06, 0.88)
	love.graphics.rectangle("fill", panelX, panelY, panelSize, panelSize, 5, 5)

	local gridImage = ensureMapGridImage()
	local tileWorld = math.max(50, extent / 3.2)
	local tilePixel = tileWorld * worldToPixel
	local tileScale = tilePixel / math.max(1, mapState.gridImageSize)
	local tileSpanCount = math.ceil((panelSize * 1.7) / math.max(1, tilePixel))
	local worldOffsetX = -positiveMod(mapCam.pos[1], tileWorld)
	local worldOffsetZ = -positiveMod(mapCam.pos[3], tileWorld)

	love.graphics.setScissor(panelX, panelY, panelSize, panelSize)
	for ix = -tileSpanCount, tileSpanCount do
		for iz = -tileSpanCount, tileSpanCount do
			local worldDX = worldOffsetX + ix * tileWorld
			local worldDZ = worldOffsetZ + iz * tileWorld
			local mapX, mapZ = rotateWorldDeltaToMap(worldDX, worldDZ, mapCam.heading)
			local sx = panelCenterX + mapX * worldToPixel
			local sy = panelCenterY - mapZ * worldToPixel
			love.graphics.setColor(0.72, 0.8, 0.72, 0.24)
			love.graphics.draw(
				gridImage,
				sx,
				sy,
				-mapCam.heading,
				tileScale,
				tileScale,
				mapState.gridImageSize * 0.5,
				mapState.gridImageSize * 0.5
			)
		end
	end

	local function drawMarker(worldX, worldZ, color, radius)
		local dx = worldX - mapCam.pos[1]
		local dz = worldZ - mapCam.pos[3]
		local mapX, mapZ = rotateWorldDeltaToMap(dx, dz, mapCam.heading)
		local sx = panelCenterX + mapX * worldToPixel
		local sy = panelCenterY - mapZ * worldToPixel
		if sx < panelX or sx > (panelX + panelSize) or sy < panelY or sy > (panelY + panelSize) then
			return
		end

		love.graphics.setColor(color[1], color[2], color[3], color[4] or 1)
		love.graphics.circle("fill", sx, sy, radius)
	end

	if localPlayerObject and localPlayerObject.pos then
		drawMarker(localPlayerObject.pos[1], localPlayerObject.pos[3], { 0.96, 0.36, 0.12, 0.95 }, localMarkerSize)
	end
	for _, peerObj in pairs(peers) do
		if peerObj and peerObj.pos then
			drawMarker(peerObj.pos[1], peerObj.pos[3], { 0.22, 0.72, 1.0, 0.92 }, peerMarkerSize)
		end
	end

	love.graphics.setColor(1, 1, 1, 0.95)
	love.graphics.circle("line", panelCenterX, panelCenterY, localMarkerSize + 2)
	love.graphics.polygon(
		"fill",
		panelCenterX, panelCenterY - (localMarkerSize + 5),
		panelCenterX - 3, panelCenterY - (localMarkerSize + 1),
		panelCenterX + 3, panelCenterY - (localMarkerSize + 1)
	)

	love.graphics.setScissor()
	love.graphics.setColor(0.86, 0.92, 1.0, 0.95)
	love.graphics.rectangle("line", panelX, panelY, panelSize, panelSize, 5, 5)
	love.graphics.print(
		string.format("Map %s (M tap/toggle, hold+Up/Down zoom)", zoomLabels[mapState.zoomIndex] or tostring(mapState.zoomIndex)),
		panelX,
		panelY + panelSize + 4
	)
end
function love.draw()
	triangleCount = 0
	local centerX, centerY = screen.w / 2, screen.h / 2
	local renderCamera = viewState.activeCamera or resolveActiveRenderCamera() or camera
	local renderObjects = buildRenderObjectList()

	local usedGpu = false
	if useGpuRenderer and renderer.isReady() then
		local ok, renderOk, triOrErr = pcall(renderer.drawWorld, renderObjects, renderCamera, { 0.2, 0.2, 0.75, 1.0 })
		if ok and renderOk then
			usedGpu = true
			renderMode = "GPU"
			triangleCount = triOrErr or 0
			gpuErrorLogged = false
		else
			if not gpuErrorLogged then
				logger.log("GPU draw failed; switching to CPU fallback. Detail: " ..
					tostring(ok and triOrErr or renderOk))
				gpuErrorLogged = true
			end
			renderMode = "CPU"
		end
	end

	if not usedGpu then
		love.graphics.setColor(0.2, 0.2, 0.75, 0.8)
		love.graphics.rectangle("fill", 0, 0, screen.w, screen.h)

		-- reset z-buffer
		for i = 1, screen.w * screen.h do
			zBuffer[i] = math.huge
		end

		local imageData = love.image.newImageData(screen.w, screen.h)
		local opaqueObjects = {}
		local transparentObjects = {}
		for _, obj in ipairs(renderObjects) do
			local alpha = (obj.color and obj.color[4]) or 1
			if alpha < 0.999 then
				transparentObjects[#transparentObjects + 1] = obj
			else
				opaqueObjects[#opaqueObjects + 1] = obj
			end
		end

		for _, obj in ipairs(opaqueObjects) do
			imageData = engine.drawObject(obj, false, renderCamera, vector3, q, screen, zBuffer, imageData, true)
		end

		table.sort(transparentObjects, function(a, b)
			return cameraSpaceDepthForObject(a, renderCamera) > cameraSpaceDepthForObject(b, renderCamera)
		end)
		for _, obj in ipairs(transparentObjects) do
			imageData = engine.drawObject(obj, false, renderCamera, vector3, q, screen, zBuffer, imageData, false)
		end
		if frameImage then
			frameImage:replacePixels(imageData)
		else
			frameImage = love.graphics.newImage(imageData)
		end
		love.graphics.setColor(1, 1, 1, 1)
		love.graphics.draw(frameImage)
	end

	if showDebugOverlay then
		love.graphics.setColor(1, 1, 1)
		love.graphics.print(
			"Esc = pause menu (see Help/Controls tabs for full controls)\n" ..
			"Render: " .. tostring(renderMode) .. " | Triangles: " .. tostring(math.floor(triangleCount)) .. " | FPS: " ..
			tostring(love.timer.getFPS()) .. "\n" ..
			string.format(
				"View: %s | AltLook: %s | Map: %s (z%d)\n",
				viewState.mode,
				(viewState.mode == "first_person" and altLookState.held) and "on" or "off",
				mapState.visible and "on" or "off",
				mapState.zoomIndex
			) ..
			string.format(
				"Wind: %.1f u/s @ %d deg | Clouds: %d | CloudSync: %s",
				windState.speed,
				math.floor(math.deg(windState.angle) + 0.5),
				#cloudState.groups,
				cloudNetState.role
			),
			10,
			10
		)
	end

	if showCrosshair and not pauseMenu.active then
		love.graphics.setColor(1, 0, 0, 0.5)
		love.graphics.circle("fill", centerX, centerY, 1)
	end

	drawHud(screen.w, screen.h, centerX, centerY)

	if pauseMenu.active then
		drawPauseMenu()
	end
end

function love.resize(w, h)
	screen.w = w
	screen.h = h
	renderer.resize(screen)
	refreshUiFonts()

	zBuffer = {}
	for i = 1, screen.w * screen.h do
		zBuffer[i] = math.huge
	end

	frameImage = nil
	logger.log(string.format("Window resized to %dx%d", w, h))

end
