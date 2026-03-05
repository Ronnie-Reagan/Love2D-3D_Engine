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

local function cross3(a, b)
	return {
		(a[2] or 0) * (b[3] or 0) - (a[3] or 0) * (b[2] or 0),
		(a[3] or 0) * (b[1] or 0) - (a[1] or 0) * (b[3] or 0),
		(a[1] or 0) * (b[2] or 0) - (a[2] or 0) * (b[1] or 0)
	}
end

local function normalize3(v, fallback)
	local len = length3(v)
	if len <= 1e-8 then
		return fallback or { 0, 1, 0 }
	end
	return {
		(v[1] or 0) / len,
		(v[2] or 0) / len,
		(v[3] or 0) / len
	}
end

local function isFiniteNumber(value)
	return type(value) == "number" and value == value and value ~= math.huge and value ~= -math.huge
end

local function sanitizeNumber(value, fallback)
	local n = tonumber(value)
	if isFiniteNumber(n) then
		return n
	end
	return fallback
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
	local rawAlpha = math.atan(-w, math.max(1e-4, u))
	local rawBeta = math.asin(clamp(v * invSpeed, -0.999, 0.999))

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
	local alphaModelClamp = math.max(
		alphaStall + math.rad(2),
		tonumber(config and config.alphaModelClampRad) or math.rad(50)
	)
	local stallDrop = math.max(0.05, tonumber(config and config.stallLiftDropoff) or 0.55)
	local betaModelClamp = math.max(0.1, tonumber(config and config.betaModelClampRad) or math.rad(65))
	local cd0 = tonumber(config and config.CD0) or 0.03
	local inducedDragK = tonumber(config and config.inducedDragK) or 0.045
	local clMin = tonumber(config and config.CLmin) or -1.4
	local clMax = tonumber(config and config.CLmax) or 1.8
	local cdMax = tonumber(config and config.CDmax) or 2.2
	local cyMax = tonumber(config and config.CYmaxAbs) or 1.4
	local cMomentMax = tonumber(config and config.CMomentMaxAbs) or 2.8
	local rateNormMax = tonumber(config and config.rateNormMaxAbs) or 4.0

	local alpha = clamp(rawAlpha, -alphaModelClamp, alphaModelClamp)
	local beta = clamp(rawBeta, -betaModelClamp, betaModelClamp)

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

	local pitchRateNorm = clamp((pitchRate * meanChord) / (2 * speed), -rateNormMax, rateNormMax)
	local rollRateNorm = clamp((rollRate * wingSpan) / (2 * speed), -rateNormMax, rateNormMax)
	local yawRateNorm = clamp((yawRate * wingSpan) / (2 * speed), -rateNormMax, rateNormMax)

	local cl = cl0 + clAlpha * alpha + clElevator * elevator
	if math.abs(alpha) > alphaStall then
		local exceed = math.abs(alpha) - alphaStall
		local drop = math.exp(-exceed * stallDrop * 8)
		cl = cl * clamp(drop, 0.15, 1.0)
	end
	cl = clamp(cl, clMin, clMax)

	local cd = clamp(cd0 + inducedDragK * cl * cl, 0.01, cdMax)
	local cy = clamp(cyBeta * beta + cyRudder * rudder, -cyMax, cyMax)

	local cRoll = clBeta * beta + clP * rollRateNorm + clR * yawRateNorm + clAileron * aileron + clRudder * rudder
	local cPitch = cm0 + cmAlpha * alpha + cmQ * pitchRateNorm + cmElevator * elevator
	local cYaw = cnBeta * beta + cnR * yawRateNorm + cnP * rollRateNorm + cnRudder * rudder + cnAileron * aileron
	cRoll = clamp(cRoll, -cMomentMax, cMomentMax)
	cPitch = clamp(cPitch, -cMomentMax, cMomentMax)
	cYaw = clamp(cYaw, -cMomentMax, cMomentMax)

	-- Force decomposition in wind axes (physically robust):
	-- drag opposes air velocity, lift is perpendicular to flow, side force is lateral.
	local vHat = normalize3({ velRight * invSpeed, velUp * invSpeed, velForward * invSpeed }, { 0, 0, 1 })
	local dragDir = { -vHat[1], -vHat[2], -vHat[3] }
	local liftSeed = cross3(vHat, { 1, 0, 0 })
	if length3(liftSeed) <= 1e-5 then
		liftSeed = cross3(vHat, { 0, 0, 1 })
	end
	local liftDir = normalize3(liftSeed, { 0, 1, 0 })
	local sideDir = normalize3(cross3(liftDir, vHat), { 1, 0, 0 })

	local dragMag = qbar * wingArea * cd
	local liftMag = qbar * wingArea * cl
	local sideMag = qbar * wingArea * cy

	local forceRight = dragDir[1] * dragMag + liftDir[1] * liftMag + sideDir[1] * sideMag
	local forceUp = dragDir[2] * dragMag + liftDir[2] * liftMag + sideDir[2] * sideMag
	local forceForward = dragDir[3] * dragMag + liftDir[3] * liftMag + sideDir[3] * sideMag

	-- Conventional moments: roll (forward axis), pitch (right axis, nose-up positive), yaw (up axis).
	local momentRoll = qbar * wingArea * wingSpan * cRoll
	local momentPitch = qbar * wingArea * meanChord * cPitch
	local momentYaw = qbar * wingArea * wingSpan * cYaw

	forceForward = sanitizeNumber(forceForward, 0)
	forceRight = sanitizeNumber(forceRight, 0)
	forceUp = sanitizeNumber(forceUp, 0)
	momentRoll = sanitizeNumber(momentRoll, 0)
	momentPitch = sanitizeNumber(momentPitch, 0)
	momentYaw = sanitizeNumber(momentYaw, 0)

	-- Map back to engine body axes: +X right, +Y up, +Z forward.
	-- Pitch sign is inverted (conventional +nose-up => engine -about +X).
	return {
		speed = speed,
		alpha = alpha,
		beta = beta,
		rawAlpha = rawAlpha,
		rawBeta = rawBeta,
		dynamicPressure = qbar,
		forceBody = { forceRight, forceUp, forceForward },
		momentBody = { -momentPitch, momentYaw, momentRoll }
	}
end

return aero
