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
	local sampleGroundHeightAt = bindings.sampleGroundHeightAt

	local function safeClamp(value, minValue, maxValue)
		if clamp then
			return clamp(value, minValue, maxValue)
		end
		if value < minValue then
			return minValue
		end
		if value > maxValue then
			return maxValue
		end
		return value
	end

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
		if not obj then
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

	local function updateLocalPlayerPortal(obj, activeCam)
		if type(obj) ~= "table" or not obj.isLocalPlayer then
			return
		end

		obj.firstPersonPortal = obj.firstPersonPortal or {
			enabled = false,
			origin = { 0, 0, 0 },
			dir = { 0, 0, 1 },
			startDist = 0.05,
			endDist = 10.0,
			radiusNear = 0.35,
			radiusFar = 1.1
		}
		local portal = obj.firstPersonPortal
		local viewState = resolve(getViewState)
		if not viewState or viewState.mode ~= "first_person" or not activeCam or not activeCam.pos or not activeCam.rot then
			portal.enabled = false
			return
		end

		local hx, hy, hz = 1, 1, 1
		if type(obj.halfSize) == "table" then
			hx = math.max(0.01, math.abs(tonumber(obj.halfSize.x or obj.halfSize[1]) or 1))
			hy = math.max(0.01, math.abs(tonumber(obj.halfSize.y or obj.halfSize[2]) or 1))
			hz = math.max(0.01, math.abs(tonumber(obj.halfSize.z or obj.halfSize[3]) or 1))
		end

		local dir = q.rotateVector(activeCam.rot, { 0, 0, 1 })
		local dLen = math.sqrt((dir[1] or 0) ^ 2 + (dir[2] or 0) ^ 2 + (dir[3] or 0) ^ 2)
		if dLen <= 1e-6 then
			dir = { 0, 0, 1 }
			dLen = 1
		end

		local nearRadius = math.max(0.30, (hx + hy) * 0.22)
		local farRadius = math.max(nearRadius + 0.45, nearRadius * 2.6)
		local tunnelLen = math.max(8.0, hz * 14.0 + 4.0)

		portal.enabled = true
		portal.origin[1] = activeCam.pos[1]
		portal.origin[2] = activeCam.pos[2]
		portal.origin[3] = activeCam.pos[3]
		portal.dir[1] = (dir[1] or 0) / dLen
		portal.dir[2] = (dir[2] or 0) / dLen
		portal.dir[3] = (dir[3] or 1) / dLen
		portal.startDist = 0.01
		portal.endDist = tunnelLen
		portal.radiusNear = nearRadius
		portal.radiusFar = farRadius
	end

	local function estimateCullRadius(obj)
		if type(obj) ~= "table" then
			return 0
		end
		local halfSize = obj.halfSize
		if type(halfSize) ~= "table" then
			return 0
		end

		local hx = math.abs(tonumber(halfSize.x or halfSize[1]) or 0)
		local hy = math.abs(tonumber(halfSize.y or halfSize[2]) or 0)
		local hz = math.abs(tonumber(halfSize.z or halfSize[3]) or 0)
		local radius = math.sqrt((hx * hx) + (hy * hy) + (hz * hz))

		-- Terrain chunks are wide and flat; use extra slack so small corner visibility does not pop.
		if obj.isTerrainChunk or obj.isGround then
			local edgeSpan = math.max(hx, hz)
			radius = radius + math.max(8.0, math.min(56.0, edgeSpan * 0.45))
		end

		return radius
	end

	local function terrainOccludesObject(obj, activeCam, radius)
		if type(sampleGroundHeightAt) ~= "function" then
			return false
		end
		if type(obj) ~= "table" or type(activeCam) ~= "table" then
			return false
		end
		if obj.isTerrainChunk or obj.isGround or obj.isCloud or obj.noTerrainOcclusion then
			return false
		end
		local cullPos = obj.terrainCenter or obj.pos
		if not (cullPos and activeCam.pos) then
			return false
		end

		local camX = tonumber(activeCam.pos[1]) or 0
		local camY = tonumber(activeCam.pos[2]) or 0
		local camZ = tonumber(activeCam.pos[3]) or 0
		local targetX = tonumber(cullPos[1]) or 0
		local targetY = tonumber(cullPos[2]) or 0
		local targetZ = tonumber(cullPos[3]) or 0
		local dx = targetX - camX
		local dz = targetZ - camZ
		local distXZ = math.sqrt(dx * dx + dz * dz)
		if distXZ < 120 then
			return false
		end

		local projectedTargetY = targetY + radius * 0.35
		local sampleCount = safeClamp(math.floor(distXZ / 90), 2, 14)
		local blockedRun = 0
		for i = 1, sampleCount - 1 do
			local t = i / sampleCount
			local sx = camX + dx * t
			local sz = camZ + dz * t
			local terrainY = sampleGroundHeightAt(sx, sz)
			local sightY = camY + (projectedTargetY - camY) * t - radius * 0.15
			if terrainY and (terrainY > (sightY + 1.5)) then
				blockedRun = blockedRun + 1
				if blockedRun >= 2 then
					return true
				end
			else
				blockedRun = 0
			end
		end

		return false
	end

	local function buildRenderObjectList()
		local viewState = resolve(getViewState)
		local camera = resolve(getCamera)
		local graphicsSettings = resolve(getGraphicsSettings)
		local objects = resolve(getObjects)

		local renderObjects = {}
		local activeCam = viewState.activeCamera or camera
		local maxDist = math.max(300, tonumber(graphicsSettings.drawDistance) or 1800)
		local camConj = nil
		local tanHalfVertical = math.tan((activeCam and activeCam.fov or math.rad(90)) * 0.5)
		local tanHalfHorizontal = tanHalfVertical
		local screenHeight = 1080
		if activeCam and activeCam.rot then
			camConj = q.conjugate(activeCam.rot)
			if type(love) == "table" and type(love.graphics) == "table" and type(love.graphics.getDimensions) == "function" then
				local w, h = love.graphics.getDimensions()
				local aspect = (h and h > 0) and (w / h) or 1.0
				tanHalfHorizontal = tanHalfVertical * math.max(0.25, aspect)
				screenHeight = (h and h > 0) and h or screenHeight
			end
		end

		for _, obj in ipairs(objects) do
			if shouldRenderObjectForView(obj) then
				updateLocalPlayerPortal(obj, activeCam)
				local cullPos = obj.terrainCenter or obj.pos
				if activeCam and activeCam.pos and cullPos then
					local dx = (cullPos[1] or 0) - activeCam.pos[1]
					local dy = (cullPos[2] or 0) - activeCam.pos[2]
					local dz = (cullPos[3] or 0) - activeCam.pos[3]
					local distSq = dx * dx + dy * dy + dz * dz
					local radius = estimateCullRadius(obj)
					local limit = maxDist + radius
					local inRange = distSq <= (limit * limit)
					local inFrustum = true
					if inRange and camConj then
						local cam = q.rotateVector(camConj, { dx, dy, dz })
						local z = cam[3] or 0
						if (z + radius) < 0.05 then
							inFrustum = false
						else
							local zForExtent = math.max(0, z)
							local worldPerPixel = (zForExtent * tanHalfVertical * 2.0) / math.max(1.0, screenHeight)
							local pixelSlack = math.max(worldPerPixel * 3.0, 0.02)
							local xLimit = zForExtent * tanHalfHorizontal + radius + pixelSlack
							local yLimit = zForExtent * tanHalfVertical + radius + pixelSlack
							if math.abs(cam[1] or 0) > xLimit or math.abs(cam[2] or 0) > yLimit then
								inFrustum = false
							end
						end
					end
					if inRange and inFrustum and (not terrainOccludesObject(obj, activeCam, radius)) then
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
