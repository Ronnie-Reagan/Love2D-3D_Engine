local q = require "Source.Math.Quat"

local integrator = {}

local function cross(a, b)
	return {
		(a[2] or 0) * (b[3] or 0) - (a[3] or 0) * (b[2] or 0),
		(a[3] or 0) * (b[1] or 0) - (a[1] or 0) * (b[3] or 0),
		(a[1] or 0) * (b[2] or 0) - (a[2] or 0) * (b[1] or 0)
	}
end

local function add(a, b)
	return {
		(a[1] or 0) + (b[1] or 0),
		(a[2] or 0) + (b[2] or 0),
		(a[3] or 0) + (b[3] or 0)
	}
end

local function scale(v, s)
	return {
		(v[1] or 0) * s,
		(v[2] or 0) * s,
		(v[3] or 0) * s
	}
end

local function inertiaMatrix(config)
	local ixx = tonumber(config and config.Ixx) or 1200
	local iyy = tonumber(config and config.Iyy) or 1700
	local izz = tonumber(config and config.Izz) or 2500
	local ixz = tonumber(config and config.Ixz) or 0
	return ixx, iyy, izz, ixz
end

local function mulInertia(ixx, iyy, izz, ixz, w)
	return {
		ixx * w[1] - ixz * w[3],
		iyy * w[2],
		-ixz * w[1] + izz * w[3]
	}
end

local function solveInertia(ixx, iyy, izz, ixz, rhs)
	local detXZ = (ixx * izz) - (ixz * ixz)
	if math.abs(detXZ) <= 1e-8 then
		detXZ = 1e-8
	end
	local invXX = izz / detXZ
	local invXZ = ixz / detXZ
	local invZZ = ixx / detXZ
	local invYY = (math.abs(iyy) > 1e-8) and (1 / iyy) or 0
	return {
		invXX * rhs[1] + invXZ * rhs[3],
		invYY * rhs[2],
		invXZ * rhs[1] + invZZ * rhs[3]
	}
end

local function integrateQuaternion(rot, angVelBody, dt)
	local omega = {
		w = 0,
		x = angVelBody[1] or 0,
		y = angVelBody[2] or 0,
		z = angVelBody[3] or 0
	}
	local qDot = q.multiply(rot, omega)
	local next = {
		w = rot.w + 0.5 * qDot.w * dt,
		x = rot.x + 0.5 * qDot.x * dt,
		y = rot.y + 0.5 * qDot.y * dt,
		z = rot.z + 0.5 * qDot.z * dt
	}
	return q.normalize(next)
end

function integrator.step(state, config, forceWorld, momentBody, gravityAccel, dt)
	local mass = math.max(0.1, tonumber(config and config.massKg) or 1150)
	state.vel = state.vel or { 0, 0, 0 }
	state.pos = state.pos or { 0, 0, 0 }
	state.angVel = state.angVel or { 0, 0, 0 }
	state.rot = state.rot or q.identity()

	local accelWorld = {
		(forceWorld[1] or 0) / mass,
		((forceWorld[2] or 0) / mass) - math.abs(tonumber(gravityAccel) or 9.80665),
		(forceWorld[3] or 0) / mass
	}
	state.vel = add(state.vel, scale(accelWorld, dt))
	state.pos = add(state.pos, scale(state.vel, dt))

	local ixx, iyy, izz, ixz = inertiaMatrix(config)
	local omega = state.angVel
	local iOmega = mulInertia(ixx, iyy, izz, ixz, omega)
	local gyro = cross(omega, iOmega)
	local rhs = {
		(momentBody[1] or 0) - gyro[1],
		(momentBody[2] or 0) - gyro[2],
		(momentBody[3] or 0) - gyro[3]
	}
	local angAccel = solveInertia(ixx, iyy, izz, ixz, rhs)
	state.angVel = add(state.angVel, scale(angAccel, dt))
	state.rot = integrateQuaternion(state.rot, state.angVel, dt)

	state.accelWorld = accelWorld
	state.angAccel = angAccel
	return state
end

return integrator
