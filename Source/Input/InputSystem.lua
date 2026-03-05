local M = {}

local function resolve(ref)
	if type(ref) == "function" then
		return ref()
	end
	return ref
end

function M.create(bindings)
	bindings = bindings or {}
	local controls = bindings.controls
	local q = bindings.q
	local viewMath = bindings.viewMath
	local clamp = bindings.clamp
	local love = bindings.love
	local logger = bindings.logger
	local composeWalkingRotation = bindings.composeWalkingRotation
	local getCamera = bindings.camera
	local getMapState = bindings.mapState
	local getFlightSimMode = bindings.flightSimMode
	local getMouseSensitivity = bindings.mouseSensitivity
	local getInvertLookY = bindings.invertLookY
	local getWalkingPitchLimit = bindings.walkingPitchLimit

	local lastProjectileTriggerAt = -999

	local function buildMovementInputState()
		local mapState = resolve(getMapState) or {}
		local throttleBlocked = mapState.mHeld and true or false
		return {
			flightPitchDown = controls.isActionDown("flight_pitch_down"),
			flightPitchUp = controls.isActionDown("flight_pitch_up"),
			flightRollLeft = controls.isActionDown("flight_roll_left"),
			flightRollRight = controls.isActionDown("flight_roll_right"),
			flightYawLeft = controls.isActionDown("flight_yaw_left"),
			flightYawRight = controls.isActionDown("flight_yaw_right"),
			flightThrottleDown = (not throttleBlocked) and controls.isActionDown("flight_throttle_down"),
			flightThrottleUp = (not throttleBlocked) and controls.isActionDown("flight_throttle_up"),
			flightAfterburner = controls.isActionDown("flight_afterburner"),
			flightAirBrakes = controls.isActionDown("flight_air_brakes"),
			walkForward = controls.isActionDown("walk_forward"),
			walkBackward = controls.isActionDown("walk_backward"),
			walkStrafeLeft = controls.isActionDown("walk_strafe_left"),
			walkStrafeRight = controls.isActionDown("walk_strafe_right"),
			walkSprint = controls.isActionDown("walk_sprint"),
			walkJump = controls.isActionDown("walk_jump")
		}
	end

	local function applyCameraRotations(pitchAngle, yawAngle, rollAngle)
		local camera = resolve(getCamera)
		if not camera then
			return
		end

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
		local camera = resolve(getCamera)
		if not camera then
			return
		end
		local mouseSensitivity = tonumber(resolve(getMouseSensitivity)) or 0.001
		local invertLookY = resolve(getInvertLookY) and true or false
		local walkingPitchLimit = tonumber(resolve(getWalkingPitchLimit)) or math.rad(89)

		local lookDown = controls.getActionMouseAxisValue("walk_look_down", dx, dy, modifiers)
		local lookUp = controls.getActionMouseAxisValue("walk_look_up", dx, dy, modifiers)
		local lookLeft = controls.getActionMouseAxisValue("walk_look_left", dx, dy, modifiers)
		local lookRight = controls.getActionMouseAxisValue("walk_look_right", dx, dy, modifiers)

		local pitchAxis = lookDown - lookUp
		local yawAxis = lookRight - lookLeft
		if invertLookY then
			pitchAxis = -pitchAxis
		end

		camera.walkYaw = camera.walkYaw or 0
		camera.walkPitch = camera.walkPitch or 0

		local pitchMult = camera.walkMousePitchMultiplier or 2.2
		local yawMult = camera.walkMouseYawMultiplier or 2.2

		camera.walkPitch = clamp(
			camera.walkPitch + (pitchAxis * mouseSensitivity * pitchMult),
			-walkingPitchLimit,
			walkingPitchLimit
		)
		camera.walkYaw = viewMath.wrapAngle(camera.walkYaw + (yawAxis * mouseSensitivity * yawMult))
		camera.rot = composeWalkingRotation(camera.walkYaw, camera.walkPitch)
	end

	local function applyFlightMouseLook(dx, dy, modifiers)
		local camera = resolve(getCamera)
		if not camera then
			return
		end
		local mouseSensitivity = tonumber(resolve(getMouseSensitivity)) or 0.001
		local invertLookY = resolve(getInvertLookY) and true or false

		local clampedDx = clamp(tonumber(dx) or 0, -42, 42)
		local clampedDy = clamp(tonumber(dy) or 0, -42, 42)
		if math.abs(clampedDx) < 0.2 then
			clampedDx = 0
		end
		if math.abs(clampedDy) < 0.2 then
			clampedDy = 0
		end

		local pitchDown = controls.getActionMouseAxisValue("flight_pitch_down", clampedDx, clampedDy, modifiers)
		local pitchUp = controls.getActionMouseAxisValue("flight_pitch_up", clampedDx, clampedDy, modifiers)
		local rollLeft = controls.getActionMouseAxisValue("flight_roll_left", clampedDx, clampedDy, modifiers)
		local rollRight = controls.getActionMouseAxisValue("flight_roll_right", clampedDx, clampedDy, modifiers)
		local yawLeft = controls.getActionMouseAxisValue("flight_yaw_left", clampedDx, clampedDy, modifiers)
		local yawRight = controls.getActionMouseAxisValue("flight_yaw_right", clampedDx, clampedDy, modifiers)

		local pitchAxis = pitchDown - pitchUp
		local yawAxis = yawRight - yawLeft
		local rollAxis = rollLeft - rollRight

		if invertLookY then
			pitchAxis = -pitchAxis
		end

		camera.yoke = camera.yoke or { pitch = 0, yaw = 0, roll = 0 }
		local yoke = camera.yoke
		local pitchGain = camera.yokeMousePitchGain or 10
		local yawGain = camera.yokeMouseYawGain or 9
		local rollGain = camera.yokeMouseRollGain or 8

		if (pitchAxis ~= 0 or yawAxis ~= 0 or rollAxis ~= 0) and love and love.timer and love.timer.getTime then
			camera.yokeLastMouseInputAt = love.timer.getTime()
		end

		yoke.pitch = clamp(yoke.pitch + pitchAxis * mouseSensitivity * pitchGain, -1, 1)
		yoke.yaw = clamp(yoke.yaw + yawAxis * mouseSensitivity * yawGain, -1, 1)
		yoke.roll = clamp(yoke.roll + rollAxis * mouseSensitivity * rollGain, -1, 1)
	end

	local function updateZoomFov(dt)
		local camera = resolve(getCamera)
		if not camera then
			return
		end
		local flightSimMode = resolve(getFlightSimMode) and true or false
		local baseFov = camera.baseFov or camera.fov
		local targetFov = baseFov
		if flightSimMode and controls.isActionDown("flight_zoom_in") then
			local zoomFactor = camera.zoomFactor or 0.6
			targetFov = math.max(math.rad(20), baseFov * zoomFactor)
		end

		local lerpSpeed = camera.zoomLerpSpeed or 10
		local alpha = clamp(lerpSpeed * dt, 0, 1)
		camera.fov = camera.fov + (targetFov - camera.fov) * alpha
	end

	local function adjustFlightThrottleFromWheel(direction)
		local camera = resolve(getCamera)
		if not camera then
			return
		end
		if not resolve(getFlightSimMode) then
			return
		end

		local step = camera.wheelThrottleStep or 0.06
		camera.throttle = clamp((camera.throttle or 0) + (step * direction), 0, 1)
	end

	local function triggerProjectileAction()
		local now = love.timer.getTime()
		if (now - lastProjectileTriggerAt) >= 0.2 then
			lastProjectileTriggerAt = now
			logger.log("Projectile fire input received (projectile spawning not implemented yet).")
		end
	end

	return {
		buildMovementInputState = buildMovementInputState,
		applyCameraRotations = applyCameraRotations,
		applyWalkingMouseLook = applyWalkingMouseLook,
		applyFlightMouseLook = applyFlightMouseLook,
		updateZoomFov = updateZoomFov,
		adjustFlightThrottleFromWheel = adjustFlightThrottleFromWheel,
		triggerProjectileAction = triggerProjectileAction
	}
end

return M
