local viewMath = {}

function viewMath.wrapAngle(angle)
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

function viewMath.shortestAngleDelta(currentAngle, targetAngle)
	return viewMath.wrapAngle(targetAngle - currentAngle)
end

function viewMath.getYawFromRotation(rotation, q)
	local rot = rotation or q.identity()
	local forward = q.rotateVector(rot, { 0, 0, 1 })
	return math.atan(forward[1], forward[3])
end

function viewMath.getStableYawFromRotation(rotation, q, fallbackYaw)
	local rot = rotation or q.identity()
	local forward = q.rotateVector(rot, { 0, 0, 1 })
	local flatX = tonumber(forward[1]) or 0
	local flatZ = tonumber(forward[3]) or 0
	local flatLenSq = flatX * flatX + flatZ * flatZ
	if flatLenSq <= 1e-6 then
		return viewMath.wrapAngle(tonumber(fallbackYaw) or 0)
	end
	return math.atan(flatX, flatZ)
end

function viewMath.segmentAabbIntersectionT(startPos, endPos, obj)
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

function viewMath.cameraSpaceDepthForObject(obj, viewCamera, q)
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

function viewMath.positiveMod(value, modulus)
	if modulus == 0 then
		return 0
	end
	local result = value % modulus
	if result < 0 then
		result = result + modulus
	end
	return result
end

local function normalizeMapOrientationMode(mode)
	if mode == "north_up" then
		return "north_up"
	end
	return "heading_up"
end

function viewMath.worldToMapDelta(dx, dz, yaw, orientationMode)
	local mode = normalizeMapOrientationMode(orientationMode)
	if mode == "north_up" then
		return dx, dz
	end
	local cosYaw = math.cos(yaw)
	local sinYaw = math.sin(yaw)
	local mapX = cosYaw * dx - sinYaw * dz
	local mapZ = sinYaw * dx + cosYaw * dz
	return mapX, mapZ
end

function viewMath.mapToWorldDelta(mapX, mapZ, yaw, orientationMode)
	local mode = normalizeMapOrientationMode(orientationMode)
	if mode == "north_up" then
		return mapX, mapZ
	end
	local cosYaw = math.cos(yaw)
	local sinYaw = math.sin(yaw)
	local dx = cosYaw * mapX + sinYaw * mapZ
	local dz = -sinYaw * mapX + cosYaw * mapZ
	return dx, dz
end

function viewMath.rotateWorldDeltaToMap(dx, dz, yaw)
	return viewMath.worldToMapDelta(dx, dz, yaw, "heading_up")
end

return viewMath
