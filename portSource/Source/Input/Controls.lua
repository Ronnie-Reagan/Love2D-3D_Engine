local controls = {}
local love = require "love"

function controls.bindKey(key, modifiers)
	return { kind = "key", key = key, modifiers = modifiers }
end

function controls.bindMouseButton(button, modifiers)
	return { kind = "mouse_button", button = button, modifiers = modifiers }
end

function controls.bindMouseAxis(axis, direction, modifiers)
	return { kind = "mouse_axis", axis = axis, direction = direction, modifiers = modifiers }
end

function controls.bindMouseWheel(direction, modifiers)
	return { kind = "mouse_wheel", direction = direction, modifiers = modifiers }
end

function controls.cloneBinding(binding)
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
		{ id = "flight_pitch_down", mode = "flight", label = "Pitch Forward / Nose Down", description = "Pitch the nose downward in flight mode.", bindings = { controls.bindKey("w"), controls.bindMouseAxis("y", -1) } },
		{ id = "flight_pitch_up", mode = "flight", label = "Pitch Back / Nose Up", description = "Pitch the nose upward in flight mode.", bindings = { controls.bindKey("s"), controls.bindMouseAxis("y", 1) } },
		{ id = "flight_roll_left", mode = "flight", label = "Bank / Roll Left", description = "Roll left around the forward axis.", bindings = { controls.bindKey("a"), controls.bindMouseAxis("x", -1) } },
		{ id = "flight_roll_right", mode = "flight", label = "Bank / Roll Right", description = "Roll right around the forward axis.", bindings = { controls.bindKey("d"), controls.bindMouseAxis("x", 1) } },
		{ id = "flight_yaw_left", mode = "flight", label = "Yaw / Turn Left", description = "Yaw the craft left.", bindings = { controls.bindKey("q") } },
		{ id = "flight_yaw_right", mode = "flight", label = "Yaw / Turn Right", description = "Yaw the craft right.", bindings = { controls.bindKey("e") } },
		{ id = "flight_throttle_down", mode = "flight", label = "Throttle Down", description = "Reduce throttle while flying.", bindings = { controls.bindKey("down"), controls.bindMouseWheel(-1) } },
		{ id = "flight_throttle_up", mode = "flight", label = "Throttle Up", description = "Increase throttle while flying.", bindings = { controls.bindKey("up"), controls.bindMouseWheel(1) } },
		{ id = "flight_fire_projectile", mode = "flight", label = "Fire Projectile", description = "Trigger the primary fire action.", bindings = { controls.bindMouseButton(1) } },
		{ id = "flight_zoom_in", mode = "flight", label = "Zoom In", description = "Hold to zoom in while flying.", bindings = { controls.bindMouseButton(2) } },
		{ id = "flight_afterburner", mode = "flight", label = "After Burner", description = "Hold for temporary extra top speed.", bindings = { controls.bindKey("lshift"), controls.bindMouseButton(4) } },
		{ id = "flight_air_brakes", mode = "flight", label = "Air Brakes", description = "Hold to rapidly bleed off throttle.", bindings = { controls.bindKey("space") } },
		{ id = "walk_look_down", mode = "walking", label = "Look Down", description = "Move the camera down in walking mode.", bindings = { controls.bindMouseAxis("y", 1) } },
		{ id = "walk_look_up", mode = "walking", label = "Look Up", description = "Move the camera up in walking mode.", bindings = { controls.bindMouseAxis("y", -1) } },
		{ id = "walk_look_left", mode = "walking", label = "Look Left", description = "Move the camera left in walking mode.", bindings = { controls.bindMouseAxis("x", -1) } },
		{ id = "walk_look_right", mode = "walking", label = "Look Right", description = "Move the camera right in walking mode.", bindings = { controls.bindMouseAxis("x", 1) } },
		{ id = "walk_sprint", mode = "walking", label = "Sprint", description = "Increase walking speed while held.", bindings = { controls.bindKey("lshift") } },
		{ id = "walk_jump", mode = "walking", label = "Jump", description = "Jump while grounded.", bindings = { controls.bindKey("space") } },
		{ id = "walk_forward", mode = "walking", label = "Walk Forwards", description = "Move forwards on the ground.", bindings = { controls.bindKey("w") } },
		{ id = "walk_backward", mode = "walking", label = "Walk Backwards", description = "Move backwards on the ground.", bindings = { controls.bindKey("s") } },
		{ id = "walk_strafe_left", mode = "walking", label = "Strafe Left", description = "Strafe left on the ground.", bindings = { controls.bindKey("a") } },
		{ id = "walk_strafe_right", mode = "walking", label = "Strafe Right", description = "Strafe right on the ground.", bindings = { controls.bindKey("d") } },
		{ id = "paint_mode_paint", mode = "paint", label = "Paint Mode", description = "Switch paint editor to brush mode.", bindings = { controls.bindKey("1") } },
		{ id = "paint_mode_erase", mode = "paint", label = "Erase Mode", description = "Switch paint editor to erase mode.", bindings = { controls.bindKey("2") } },
		{ id = "paint_fill", mode = "paint", label = "Fill", description = "Fill the active paint target.", bindings = { controls.bindKey("f") } },
		{ id = "paint_undo", mode = "paint", label = "Undo Paint", description = "Undo latest paint action.", bindings = { controls.bindKey("z", { ctrl = true }) } },
		{ id = "paint_redo", mode = "paint", label = "Redo Paint", description = "Redo latest paint action.", bindings = { controls.bindKey("y", { ctrl = true }) } },
		{ id = "paint_commit", mode = "paint", label = "Commit Paint", description = "Commit and sync paint overlay.", bindings = { controls.bindKey("p") } }
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

function controls.getActions()
	return controlActions
end

function controls.resetToDefaults()
	controlActions = buildDefaultControlActions()
	rebuildControlActionIndex()
	return controlActions
end

function controls.getAction(actionId)
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

function controls.getCurrentModifiers()
	return {
		alt = love.keyboard.isDown("lalt", "ralt"),
		ctrl = love.keyboard.isDown("lctrl", "rctrl"),
		shift = love.keyboard.isDown("lshift", "rshift")
	}
end

function controls.extractCaptureModifiers(excludeKey)
	local mods = controls.getCurrentModifiers()
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
	local hasRequiredModifier = required.alt or required.ctrl or required.shift

	if required.alt and not current.alt then
		return false
	end
	if required.ctrl and not current.ctrl then
		return false
	end
	if required.shift and not current.shift then
		return false
	end

	-- Strict matching is only meaningful when a binding explicitly asks for modifiers.
	-- Plain bindings (for example mouse look axes) should continue working while Shift/Ctrl/Alt
	-- are held for separate actions like sprint/afterburner.
	if strict and hasRequiredModifier then
		if current.alt and not required.alt then
			return false
		end
		if current.ctrl and not required.ctrl then
			return false
		end
		if current.shift and not required.shift then
			return false
		end
	end

	return true
end

local function keyMatchesBinding(bindingKey, inputKey)
	if bindingKey == inputKey then
		return true
	end
	if (bindingKey == "lshift" or bindingKey == "rshift") and (inputKey == "lshift" or inputKey == "rshift") then
		return true
	end
	if (bindingKey == "lctrl" or bindingKey == "rctrl") and (inputKey == "lctrl" or inputKey == "rctrl") then
		return true
	end
	if (bindingKey == "lalt" or bindingKey == "ralt") and (inputKey == "lalt" or inputKey == "ralt") then
		return true
	end
	return false
end

local function isBindingKeyDown(bindingKey)
	if not bindingKey then
		return false
	end
	if bindingKey == "lshift" or bindingKey == "rshift" then
		return love.keyboard.isDown("lshift", "rshift")
	end
	if bindingKey == "lctrl" or bindingKey == "rctrl" then
		return love.keyboard.isDown("lctrl", "rctrl")
	end
	if bindingKey == "lalt" or bindingKey == "ralt" then
		return love.keyboard.isDown("lalt", "ralt")
	end
	return love.keyboard.isDown(bindingKey)
end

function controls.formatBinding(binding)
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

function controls.isActionDown(actionId)
	local action = controls.getAction(actionId)
	if not action then
		return false
	end

	local modifiers = controls.getCurrentModifiers()
	for _, binding in ipairs(action.bindings or {}) do
		if binding.kind == "key" and binding.key and isBindingKeyDown(binding.key) then
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

function controls.getActionMouseAxisValue(actionId, dx, dy, modifiers)
	local action = controls.getAction(actionId)
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

function controls.actionTriggeredByKey(actionId, key, modifiers)
	local action = controls.getAction(actionId)
	if not action then
		return false
	end

	for _, binding in ipairs(action.bindings or {}) do
		if binding.kind == "key" and keyMatchesBinding(binding.key, key) and bindingModifiersMatch(binding.modifiers, modifiers, false) then
			return true
		end
	end
	return false
end

function controls.actionTriggeredByMouseButton(actionId, button, modifiers)
	local action = controls.getAction(actionId)
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

function controls.actionTriggeredByWheel(actionId, wheelY, modifiers)
	local action = controls.getAction(actionId)
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

return controls
