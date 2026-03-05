local aero = {}

local function clamp(value, minValue, maxValue)
	if value < minValue then
		return minValue
	end
	if value > maxValue then
		return maxValue
	end
	return value
end

local function length3(v)
	return math.sqrt((v[1] or 0) ^ 2 + (v[2] or 0) ^ 2 + (v[3] or 0) ^ 2)
end

	-- Engine body axes are +X right, +Y up, +Z forward.
	-- Aero coefficients are kept in conventional aircraft sign convention:
	-- alpha positive nose-up, Cm positive nose-up.
	-- With +Y-up engine coordinates, +X rotation is nose-down, so pitch-rate and pitch-moment
	-- are converted when mapping between coefficient equations and engine rigid-body axes.
function aero.compute(airVelBody, angVelBody, controls, atmosphereSample, config)
	local velRight = tonumber(airVelBody and airVelBody[1]) or 0
	local velUp = tonumber(airVelBody and airVelBody[2]) or 0
	local velForward = tonumber(airVelBody and airVelBody[3]) or 0

	local u = velForward
	local v = velRight
	local w = velUp

	local rollRate = tonumber(angVelBody and angVelBody[3]) or 0
	-- Convert engine pitch rate (+X => nose-down) to conventional q (+ => nose-up).
	local pitchRate = -(tonumber(angVelBody and angVelBody[1]) or 0)
	local yawRate = tonumber(angVelBody and angVelBody[2]) or 0

	local speed = math.max(0.1, length3({ u, v, w }))
	local invSpeed = 1 / speed
	local alpha = math.atan(-w, math.max(1e-4, u))
	local beta = math.asin(clamp(v * invSpeed, -0.999, 0.999))

	local rho = math.max(0.02, tonumber(atmosphereSample and atmosphereSample.densityKgM3) or 1.225)
	local qbar = 0.5 * rho * speed * speed

	local wingArea = math.max(0.1, tonumber(config and config.wingArea) or 16.2)
	local wingSpan = math.max(0.1, tonumber(config and config.wingSpan) or 10.9)
	local meanChord = math.max(0.1, tonumber(config and config.meanChord) or 1.5)

	local elevator = tonumber(controls and controls.elevator) or 0
	local aileron = tonumber(controls and controls.aileron) or 0
	local rudder = tonumber(controls and controls.rudder) or 0

	local cl0 = tonumber(config and config.CL0) or 0.25
	local clAlpha = tonumber(config and config.CLalpha) or 5.5
	local clElevator = tonumber(config and config.CLElevator) or 0.65
	local alphaStall = tonumber(config and config.alphaStallRad) or math.rad(15)
	local stallDrop = math.max(0.05, tonumber(config and config.stallLiftDropoff) or 0.55)
	local cd0 = tonumber(config and config.CD0) or 0.03
	local inducedDragK = tonumber(config and config.inducedDragK) or 0.045

	local cyBeta = tonumber(config and config.CYbeta) or -0.95
	local cyRudder = tonumber(config and config.CYrudder) or 0.28

	local cm0 = tonumber(config and config.Cm0) or 0.04
	local cmAlpha = tonumber(config and config.CmAlpha) or -1.2
	local cmQ = tonumber(config and config.CmQ) or -12.0
	local cmElevator = tonumber(config and config.CmElevator) or -1.35

	local clBeta = tonumber(config and config.ClBeta) or -0.12
	local clP = tonumber(config and config.ClP) or -0.48
	local clR = tonumber(config and config.ClR) or 0.16
	local clAileron = tonumber(config and config.ClAileron) or 0.22
	local clRudder = tonumber(config and config.ClRudder) or 0.03

	local cnBeta = tonumber(config and config.CnBeta) or 0.16
	local cnR = tonumber(config and config.CnR) or -0.24
	local cnP = tonumber(config and config.CnP) or -0.06
	local cnRudder = tonumber(config and config.CnRudder) or -0.17
	local cnAileron = tonumber(config and config.CnAileron) or 0.02

	local pitchRateNorm = (pitchRate * meanChord) / (2 * speed)
	local rollRateNorm = (rollRate * wingSpan) / (2 * speed)
	local yawRateNorm = (yawRate * wingSpan) / (2 * speed)

	local cl = cl0 + clAlpha * alpha + clElevator * elevator
	if math.abs(alpha) > alphaStall then
		local exceed = math.abs(alpha) - alphaStall
		local drop = math.exp(-exceed * stallDrop * 8)
		cl = cl * clamp(drop, 0.15, 1.0)
	end

	local cd = math.max(0.01, cd0 + inducedDragK * cl * cl)
	local cy = cyBeta * beta + cyRudder * rudder

	local cRoll = clBeta * beta + clP * rollRateNorm + clR * yawRateNorm + clAileron * aileron + clRudder * rudder
	local cPitch = cm0 + cmAlpha * alpha + cmQ * pitchRateNorm + cmElevator * elevator
	local cYaw = cnBeta * beta + cnR * yawRateNorm + cnP * rollRateNorm + cnRudder * rudder + cnAileron * aileron

	-- Aerodynamic forces in conventional axes.
	local forceForward = qbar * wingArea * ((-cd * math.cos(alpha)) + (cl * math.sin(alpha)))
	local forceRight = qbar * wingArea * cy
	local forceUp = qbar * wingArea * ((cl * math.cos(alpha)) + (cd * math.sin(alpha)))

	-- Conventional moments: roll (forward axis), pitch (right axis, nose-up positive), yaw (up axis).
	local momentRoll = qbar * wingArea * wingSpan * cRoll
	local momentPitch = qbar * wingArea * meanChord * cPitch
	local momentYaw = qbar * wingArea * wingSpan * cYaw

	-- Map back to engine body axes: +X right, +Y up, +Z forward.
	-- Pitch sign is inverted (conventional +nose-up => engine -about +X).
	return {
		speed = speed,
		alpha = alpha,
		beta = beta,
		dynamicPressure = qbar,
		forceBody = { forceRight, forceUp, forceForward },
		momentBody = { -momentPitch, momentYaw, momentRoll }
	}
end

return aero
