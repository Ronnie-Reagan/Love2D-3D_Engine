local sky = {}

local function clamp(value, minValue, maxValue)
	if value < minValue then
		return minValue
	end
	if value > maxValue then
		return maxValue
	end
	return value
end

local function mix(a, b, t)
	return a + (b - a) * t
end

local function normalize(v)
	local x = tonumber(v and v[1]) or 0
	local y = tonumber(v and v[2]) or 1
	local z = tonumber(v and v[3]) or 0
	local len = math.sqrt(x * x + y * y + z * z)
	if len <= 1e-8 then
		return { 0, 1, 0 }
	end
	return { x / len, y / len, z / len }
end

function sky.evaluate(lightDir, opts)
	opts = opts or {}
	local sunDir = normalize(lightDir)
	local sunHeight = clamp(sunDir[2] * 0.5 + 0.5, 0, 1)
	local turbidity = clamp(tonumber(opts.turbidity) or 2.4, 1.0, 10.0)
	local haze = clamp((turbidity - 1.0) / 9.0, 0, 1)

	local zenith = {
		mix(0.15, 0.23, sunHeight),
		mix(0.24, 0.42, sunHeight),
		mix(0.45, 0.82, sunHeight)
	}
	local horizon = {
		mix(0.45, 0.78, sunHeight),
		mix(0.42, 0.63, sunHeight),
		mix(0.36, 0.46, sunHeight)
	}
	local skyColor = {
		mix(zenith[1], horizon[1], haze * 0.55),
		mix(zenith[2], horizon[2], haze * 0.55),
		mix(zenith[3], horizon[3], haze * 0.55)
	}
	local groundColor = {
		mix(0.16, 0.24, sunHeight),
		mix(0.13, 0.20, sunHeight),
		mix(0.10, 0.15, sunHeight)
	}

	local sunTint = {
		mix(1.0, 0.96, haze),
		mix(0.98, 0.88, haze),
		mix(0.92, 0.72, haze)
	}
	local intensity = math.max(0, tonumber(opts.sunIntensity) or 1.2)
	local lightColor = {
		sunTint[1] * intensity,
		sunTint[2] * intensity,
		sunTint[3] * intensity
	}

	return {
		lightDir = sunDir,
		lightColor = lightColor,
		skyColor = skyColor,
		groundColor = groundColor,
		ambient = clamp(tonumber(opts.ambient) or 0.14, 0, 2),
		exposureEV = tonumber(opts.exposureEV) or 0.0
	}
end

return sky
