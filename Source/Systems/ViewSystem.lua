local M = {}

local function resolve(ref)
	if type(ref) == "function" then
		return ref()
	end
	return ref
end

function M.create(bindings)
	bindings = bindings or {}
	local q = bindings.q
	local engine = bindings.engine
	local vector3 = bindings.vector3
	local viewMath = bindings.viewMath
	local clamp = bindings.clamp
	local getSunDirection = bindings.getSunDirection

	local getCamera = bindings.camera
	local getViewState = bindings.viewState
	local getAltLookState = bindings.altLookState
	local getThirdPersonState = bindings.thirdPersonState
	local getObjects = bindings.objects
	local getLocalPlayerObject = bindings.localPlayerObject
	local getSunSettings = bindings.sunSettings
	local getSunDebugObject = bindings.sunDebugObject
	local getGraphicsSettings = bindings.graphicsSettings
	local getMouseSensitivity = bindings.mouseSensitivity
	local getInvertLookY = bindings.invertLookY
	local getWalkingPitchLimit = bindings.walkingPitchLimit

	local function composeWalkingRotation(yaw, pitch)
		local yawQuat = q.fromAxisAngle({ 0, 1, 0 }, yaw)
		local right = q.rotateVector(yawQuat, { 1, 0, 0 })
		local pitchQuat = q.fromAxisAngle(right, pitch)
		return q.normalize(q.multiply(pitchQuat, yawQuat))
	end

	local function syncWalkingLookFromRotation()
		local camera = resolve(getCamera)
		if not camera then
			return
		end
		local walkingPitchLimit = tonumber(resolve(getWalkingPitchLimit)) or math.rad(89)

		local forward = q.rotateVector(camera.rot, { 0, 0, 1 })
		local flatMag = math.sqrt(forward[1] * forward[1] + forward[3] * forward[3])
		local pitch = math.atan(forward[2], math.max(flatMag, 1e-6))
		local yaw = math.atan(forward[1], forward[3])

		camera.walkYaw = viewMath.wrapAngle(yaw)
		camera.walkPitch = clamp(pitch, -walkingPitchLimit, walkingPitchLimit)
		camera.rot = composeWalkingRotation(camera.walkYaw, camera.walkPitch)
	end

	local function resetAltLookState()
		local altLookState = resolve(getAltLookState)
		altLookState.held = false
		altLookState.yaw = 0
		altLookState.pitch = 0
	end

	local function applyAltLookMouse(dx, dy)
		local camera = resolve(getCamera)
		local altLookState = resolve(getAltLookState)
		if not camera then
			return
		end

		local mouseSensitivity = tonumber(resolve(getMouseSensitivity)) or 0.001
		local invertLookY = resolve(getInvertLookY) and true or false
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
		local altLookState = resolve(getAltLookState)
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

	local function buildThirdPersonCamera(baseCamera, objectList)
		local thirdPersonState = resolve(getThirdPersonState)
		local localPlayerObject = resolve(getLocalPlayerObject)
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
					local hitT = viewMath.segmentAabbIntersectionT(target, desiredPos, obj)
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
		local camera = resolve(getCamera)
		local viewState = resolve(getViewState)
		local altLookState = resolve(getAltLookState)
		local objects = resolve(getObjects)
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
		local viewState = resolve(getViewState)
		if not obj then
			return false
		end
		if obj.isLocalPlayer and viewState.mode == "first_person" then
			return false
		end
		return true
	end

	local function appendSunDebugRenderObject(renderObjects, activeCam, maxDist)
		local sunSettings = resolve(getSunSettings)
		local sunDebugObject = resolve(getSunDebugObject)
		if not (sunSettings.showMarker and activeCam and activeCam.pos and renderObjects) then
			return
		end

		local dir = getSunDirection()
		local distance = tonumber(sunSettings.markerDistance) or (maxDist * 0.8)
		distance = math.max(80, distance)
		local size = tonumber(sunSettings.markerSize) or 180
		size = math.max(1, size)

		local marker = sunDebugObject
		marker.pos[1] = activeCam.pos[1] + dir[1] * distance
		marker.pos[2] = activeCam.pos[2] + dir[2] * distance
		marker.pos[3] = activeCam.pos[3] + dir[3] * distance
		marker.scale[1], marker.scale[2], marker.scale[3] = size, size, size
		marker.halfSize.x, marker.halfSize.y, marker.halfSize.z = size, size, size

		local tintR = math.max(0, tonumber(sunSettings.colorR) or 1.0)
		local tintG = math.max(0, tonumber(sunSettings.colorG) or 1.0)
		local tintB = math.max(0, tonumber(sunSettings.colorB) or 1.0)
		marker.color[1], marker.color[2], marker.color[3], marker.color[4] = tintR, tintG, tintB, 1.0

		local emissive = marker.materials[1].emissiveFactor
		local emissiveScale = math.max(2.0, (tonumber(sunSettings.intensity) or 1.0) * 6.0)
		emissive[1] = tintR * emissiveScale
		emissive[2] = tintG * emissiveScale
		emissive[3] = tintB * emissiveScale

		renderObjects[#renderObjects + 1] = marker
	end

	local function buildRenderObjectList()
		local viewState = resolve(getViewState)
		local camera = resolve(getCamera)
		local graphicsSettings = resolve(getGraphicsSettings)
		local objects = resolve(getObjects)

		local renderObjects = {}
		local activeCam = viewState.activeCamera or camera
		local maxDist = math.max(300, tonumber(graphicsSettings.drawDistance) or 1800)
		for _, obj in ipairs(objects) do
			if shouldRenderObjectForView(obj) then
				if activeCam and activeCam.pos and obj.pos then
					local dx = obj.pos[1] - activeCam.pos[1]
					local dy = obj.pos[2] - activeCam.pos[2]
					local dz = obj.pos[3] - activeCam.pos[3]
					local distSq = dx * dx + dy * dy + dz * dz
					local radius = 0
					if obj.halfSize then
						radius = math.max(obj.halfSize.x or 0, obj.halfSize.y or 0, obj.halfSize.z or 0)
					end
					local limit = maxDist + radius
					if distSq <= (limit * limit) then
						renderObjects[#renderObjects + 1] = obj
					end
				else
					renderObjects[#renderObjects + 1] = obj
				end
			end
		end

		appendSunDebugRenderObject(renderObjects, activeCam, maxDist)

		return renderObjects
	end

	return {
		composeWalkingRotation = composeWalkingRotation,
		syncWalkingLookFromRotation = syncWalkingLookFromRotation,
		resetAltLookState = resetAltLookState,
		applyAltLookMouse = applyAltLookMouse,
		buildAltLookCamera = buildAltLookCamera,
		buildThirdPersonCamera = buildThirdPersonCamera,
		resolveActiveRenderCamera = resolveActiveRenderCamera,
		shouldRenderObjectForView = shouldRenderObjectForView,
		appendSunDebugRenderObject = appendSunDebugRenderObject,
		buildRenderObjectList = buildRenderObjectList
	}
end

return M
