local q = require("quat")
local vector3 = require("vector3")
local love = require "love" -- avoids nil report from intellisense, safe to remove if causes issues (it should be OK)
local enet = require "enet"
local engine = require "engine"
local networking = require "networking"
local renderer = require "gpu_renderer"
local logger = require "logger"
local hostAddy = "ecosim.donreagan.ca:1988"
local relay = enet.host_create()
local relayServer = relay and relay:connect(hostAddy) or nil
local flightSimMode = false
local relative = true
local peers = {}
local event = nil
local objects = require "object"
local cubeModel = objects.cubeModel
local screen, camera, groundObject, triangleCount, frameImage, renderMode, gpuErrorLogged --, zBuffer
local useGpuRenderer = true
local perfElapsed, perfFrames, perfLoggedLowFps = 0, 0, false
local zBuffer = {}
local mouseSensitivity = 0.001
local invertLookY = false
local showCrosshair = true
local showDebugOverlay = true
local pauseTitleFont, pauseItemFont

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
		{ id = "flight_yaw_left", mode = "flight", label = "Yaw / Turn Left", description = "Yaw the craft left.", bindings = { bindKey("q"), bindMouseAxis("x", -1, { alt = true }) } },
		{ id = "flight_yaw_right", mode = "flight", label = "Yaw / Turn Right", description = "Yaw the craft right.", bindings = { bindKey("e"), bindMouseAxis("x", 1, { alt = true }) } },
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
		{ id = "walk_strafe_right", mode = "walking", label = "Strafe Right", description = "Strafe right on the ground.", bindings = { bindKey("d") } },
		{ id = "walk_tilt_left", mode = "walking", label = "Tilt Head / Camera Left", description = "Roll camera left while walking.", bindings = { bindKey("q") } },
		{ id = "walk_tilt_right", mode = "walking", label = "Tilt Head / Camera Right", description = "Roll camera right while walking.", bindings = { bindKey("e") } }
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
				{ keys = "Q / E", text = "Yaw left or right." },
				{ keys = "Up / Down", text = "Throttle up or down." },
				{ keys = "Space / Shift", text = "Air brakes and afterburner." }
			}
		},
		{
			title = "Walking Mode",
			items = {
				{ keys = "W A S D", text = "Move and strafe in ground mode." },
				{ keys = "Space", text = "Jump while grounded." },
				{ keys = "Shift", text = "Sprint while held." },
				{ keys = "Q / E", text = "Tilt the camera left or right." }
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
-- Procedurally generate a ground grid of cubes
local function generateGround(tileSize, gridCount, baseHeight)
	local tiles = {}
	local half = tileSize / 2

	for x = -gridCount / 2, gridCount / 2 - 1 do
		for z = -gridCount / 2, gridCount / 2 - 1 do
			local posX = x * tileSize + half
			local posZ = z * tileSize + half
			-- small color variation for realism
			local r = 0.2 + math.random() * 0.05
			local g = 0.6 + math.random() * 0.1
			local b = 0.2 + math.random() * 0.05

			table.insert(tiles, {
				model = cubeModel,
				pos = { posX, baseHeight, posZ },
				rot = q.identity(),
				color = { r, g, b },
				isSolid = true,
				halfSize = { x = tileSize / 2, y = 0.005, z = tileSize / 2 } -- thin tile
			})
		end
	end

	return tiles
end

local function clamp(value, minValue, maxValue)
	if value < minValue then
		return minValue
	end
	if value > maxValue then
		return maxValue
	end
	return value
end

local function setMouseCapture(enabled)
	love.mouse.setRelativeMode(enabled)
	pcall(love.mouse.setVisible, not enabled)
	relative = enabled
end

local function setFlightMode(enabled)
	flightSimMode = enabled and true or false
	if not flightSimMode and camera then
		camera.throttle = 0
	end
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
	camera.throttle = 0
	camera.onGround = false
	if camera.box and camera.box.halfSize then
		camera.box.pos = { camera.pos[1], camera.pos[2] - camera.box.halfSize.y, camera.pos[3] }
	end
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
		maxSpeed = 50,
		throttleAccel = 24,
		afterburnerMultiplier = 1.7,
		airBrakeStrength = 55,
		wheelThrottleStep = 3,
		flightPitchRate = 65,
		flightYawRate = 70,
		flightRollRate = 95,
		sprintMultiplier = 1.8,
		walkTiltRate = 40,
		box = {
			halfSize = { x = 0.5, y = 1.8, z = 0.5 }, -- width/height/depth half extents
			pos = { 0, 0, -5 },                       -- center at camera
			isSolid = true
		}
	}

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

	-- creating the cube's object
	-- disabled now that other players can join allowing for non-static testing of latency and culling/depth ordering
	objects = {
		[1] = {
			model = cubeModel,
			pos = { 0, 0, 10 },
			rot = q.identity(),
			color = { 0.9, 0.4, 0.1 },
			isSolid = true
		}
	}

	-- generate a 1000x1000 ground made of 10x10 tiles
	local tileSize = 2
	local gridCount = 10 -- 100 tiles per side -> 1000 units total
	local baseHeight = -0.1

	local groundTiles = generateGround(tileSize, gridCount, baseHeight)

	for _, tile in ipairs(groundTiles) do
		table.insert(objects, tile)
	end

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
end

-- === Mouse Look ===
local function buildMovementInputState()
	return {
		flightPitchDown = isActionDown("flight_pitch_down"),
		flightPitchUp = isActionDown("flight_pitch_up"),
		flightRollLeft = isActionDown("flight_roll_left"),
		flightRollRight = isActionDown("flight_roll_right"),
		flightYawLeft = isActionDown("flight_yaw_left"),
		flightYawRight = isActionDown("flight_yaw_right"),
		flightThrottleDown = isActionDown("flight_throttle_down"),
		flightThrottleUp = isActionDown("flight_throttle_up"),
		flightAfterburner = isActionDown("flight_afterburner"),
		flightAirBrakes = isActionDown("flight_air_brakes"),
		walkForward = isActionDown("walk_forward"),
		walkBackward = isActionDown("walk_backward"),
		walkStrafeLeft = isActionDown("walk_strafe_left"),
		walkStrafeRight = isActionDown("walk_strafe_right"),
		walkSprint = isActionDown("walk_sprint"),
		walkJump = isActionDown("walk_jump"),
		walkTiltLeft = isActionDown("walk_tilt_left"),
		walkTiltRight = isActionDown("walk_tilt_right")
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

	local pitchAngle = pitchAxis * mouseSensitivity
	local yawAngle = yawAxis * mouseSensitivity
	applyCameraRotations(pitchAngle, yawAngle, 0)
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

	local pitchAngle = pitchAxis * mouseSensitivity
	local yawAngle = yawAxis * mouseSensitivity
	local rollAngle = rollAxis * mouseSensitivity
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

	local modifiers = getCurrentModifiers()
	if flightSimMode then
		applyFlightMouseLook(dx, dy, modifiers)
	else
		applyWalkingMouseLook(dx, dy, modifiers)
	end
end

local function updateNet()
	local canSend = relayServer ~= nil
	if canSend then
		local ok, state = pcall(function()
			return relayServer:state()
		end)
		if ok and state == "disconnected" then
			canSend = false
		end
	end

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
	end
end

local function serviceNetworkEvents()
	if not relay then
		return
	end

	event = relay:service()
	while event do
		if event.type == "receive" then
			networking.handlePacket(event.data, peers, objects, q)
		elseif event.type == "disconnect" and relayServer and event.peer == relayServer then
			relayServer = nil
			logger.log("Relay disconnected.")
		end

		event = relay:service()
	end
end

-- === Camera Movement ===
function love.update(dt)
	if pauseMenu.statusUntil > 0 and love.timer.getTime() >= pauseMenu.statusUntil then
		pauseMenu.statusText = ""
		pauseMenu.statusUntil = 0
	end
	if pauseMenu.confirmItemId and love.timer.getTime() > pauseMenu.confirmUntil then
		clearPauseConfirm()
	end

	if not pauseMenu.active then
		local movementInput = buildMovementInputState()
		camera = engine.processMovement(camera, dt, flightSimMode, vector3, q, objects, movementInput)
		updateZoomFov(dt)
		updateNet()

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

	serviceNetworkEvents()
end

-- === Input Management ===
function love.keypressed(key)
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

end
function love.draw()
	triangleCount = 0
	local centerX, centerY = screen.w / 2, screen.h / 2

	local usedGpu = false
	if useGpuRenderer and renderer.isReady() then
		local ok, renderOk, triOrErr = pcall(renderer.drawWorld, objects, camera, { 0.2, 0.2, 0.75, 1.0 })
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
		for _, obj in ipairs(objects) do
			imageData = engine.drawObject(obj, false, camera, vector3, q, screen, zBuffer, imageData)
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
			tostring(love.timer.getFPS()),
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
