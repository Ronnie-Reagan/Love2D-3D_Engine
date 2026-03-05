local audio = {}
local loveLib = require "love"

local function clamp(v, lo, hi)
	if v < lo then
		return lo
	end
	if v > hi then
		return hi
	end
	return v
end

local function lerp(a, b, t)
	return a + (b - a) * t
end

local function nextNoise01(state)
	state.noiseSeed = (1664525 * state.noiseSeed + 1013904223) % 4294967296
	return state.noiseSeed / 4294967295
end

local function nextNoiseSigned(state)
	return nextNoise01(state) * 2.0 - 1.0
end

local function buildSilent()
	return {
		available = false,
		update = function()
		end,
		stop = function()
		end
	}
end

function audio.create(opts)
	opts = opts or {}
	if not (loveLib and loveLib.audio and loveLib.sound and loveLib.sound.newSoundData and loveLib.audio.newQueueableSource) then
		return buildSilent()
	end

	local sampleRate = math.max(8000, math.floor(tonumber(opts.sampleRate) or 22050))
	local bitDepth = 16
	local channels = 1
	local queueDepth = math.max(2, math.floor(tonumber(opts.queueDepth) or 8))
	local bufferSamples = math.max(256, math.floor(tonumber(opts.bufferSamples) or 1024))

	local okEngine, engineSource = pcall(function()
		return loveLib.audio.newQueueableSource(sampleRate, bitDepth, channels, queueDepth)
	end)
	local okAmb, ambienceSource = pcall(function()
		return loveLib.audio.newQueueableSource(sampleRate, bitDepth, channels, queueDepth)
	end)
	if not okEngine or not okAmb or not engineSource or not ambienceSource then
		return buildSilent()
	end

	engineSource:setVolume(0.78)
	ambienceSource:setVolume(0.54)

	local state = {
		available = true,
		sampleRate = sampleRate,
		bufferSamples = bufferSamples,
		noiseSeed = 19771337,
		enginePhase1 = 0,
		enginePhase2 = 0,
		enginePhase3 = 0,
		propPhase = 0,
		waterPhase = 0,
		engineNoise = 0,
		windNoise = 0,
		throttle = 0,
		afterburner = 0,
		speedNorm = 0,
		waterProximity = 0,
		pausedBlend = 0,
		running = false,
		engineSource = engineSource,
		ambienceSource = ambienceSource
	}

	local function queueGeneratedBuffer(source, generator)
		if not source or type(source.getFreeBufferCount) ~= "function" or type(source.queue) ~= "function" then
			return
		end
		local freeCount = source:getFreeBufferCount()
		local queuedAny = false
		while freeCount and freeCount > 0 do
			local soundData = loveLib.sound.newSoundData(bufferSamples, sampleRate, bitDepth, channels)
			for i = 0, bufferSamples - 1 do
				local s = clamp(generator(i), -1.0, 1.0)
				soundData:setSample(i, s)
			end
			source:queue(soundData)
			queuedAny = true
			freeCount = source:getFreeBufferCount()
		end
		if queuedAny and type(source.isPlaying) == "function" and (not source:isPlaying()) and type(source.play) == "function" then
			source:play()
		end
	end

	local function stepEngineSample()
		local throttle = state.throttle
		local afterburner = state.afterburner
		local speedNorm = state.speedNorm
		local rpm = 38 + throttle * 188 + speedNorm * 86 + afterburner * 96
		local d = (2.0 * math.pi * rpm) / sampleRate
		state.enginePhase1 = (state.enginePhase1 + d) % (2.0 * math.pi)
		state.enginePhase2 = (state.enginePhase2 + d * 1.97) % (2.0 * math.pi)
		state.enginePhase3 = (state.enginePhase3 + d * 0.49) % (2.0 * math.pi)
		state.propPhase = (state.propPhase + d * 0.32 + speedNorm * 0.01) % (2.0 * math.pi)

		local tonal = math.sin(state.enginePhase1)
			+ math.sin(state.enginePhase2 + 0.42) * 0.43
			+ math.sin(state.enginePhase3 + 1.31) * 0.24
		local prop = math.sin(state.propPhase) * (0.08 + throttle * 0.09)
		local white = nextNoiseSigned(state)
		state.engineNoise = state.engineNoise + (white - state.engineNoise) * 0.19
		local turbulence = state.engineNoise * (0.04 + speedNorm * 0.1 + afterburner * 0.15)
		local amp = 0.09 + throttle * 0.30 + afterburner * 0.21
		return (tonal * 0.26 + prop + turbulence) * amp
	end

	local function stepAmbienceSample()
		local speedNorm = state.speedNorm
		local waterProximity = state.waterProximity
		local windWhite = nextNoiseSigned(state)
		state.windNoise = state.windNoise + (windWhite - state.windNoise) * (0.03 + speedNorm * 0.06)
		local wind = state.windNoise * (0.04 + speedNorm * 0.24)
		state.waterPhase = (state.waterPhase + (0.03 + waterProximity * 0.09)) % (2.0 * math.pi)
		local surf = (math.sin(state.waterPhase) * 0.6 + math.sin(state.waterPhase * 2.3 + 0.7) * 0.4) *
			(0.03 + waterProximity * 0.18)
		return wind + surf
	end

	function state:update(frame)
		frame = frame or {}
		local enabled = frame.audioEnabled ~= false
		local active = (frame.active and true or false) and enabled
		if not active then
			state.running = false
			if state.engineSource then
				state.engineSource:setVolume(0)
				if type(state.engineSource.stop) == "function" then
					state.engineSource:stop()
				end
			end
			if state.ambienceSource then
				state.ambienceSource:setVolume(0)
				if type(state.ambienceSource.stop) == "function" then
					state.ambienceSource:stop()
				end
			end
			return
		end

		local dt = clamp(tonumber(frame.dt) or 1 / 60, 0, 0.25)
		local masterVolume = clamp(tonumber(frame.masterVolume) or 1.0, 0.0, 1.5)
		local engineVolume = clamp(tonumber(frame.engineVolume) or 1.0, 0.0, 1.5)
		local ambienceVolume = clamp(tonumber(frame.ambienceVolume) or 1.0, 0.0, 1.5)
		local enginePitch = clamp(tonumber(frame.enginePitch) or 1.0, 0.3, 2.5)
		local ambiencePitch = clamp(tonumber(frame.ambiencePitch) or 1.0, 0.3, 2.5)
		local blend = clamp(dt * 4.5, 0, 1)
		state.throttle = lerp(state.throttle, clamp(tonumber(frame.throttle) or 0, 0, 1), blend)
		state.afterburner = lerp(state.afterburner, frame.afterburner and 1 or 0, blend)
		state.speedNorm = lerp(state.speedNorm, clamp((tonumber(frame.airspeed) or 0) / 230, 0, 1), blend)
		state.waterProximity = lerp(state.waterProximity, clamp(tonumber(frame.waterProximity) or 0, 0, 1), blend)
		state.pausedBlend = lerp(state.pausedBlend, frame.paused and 1 or 0, clamp(dt * 7.5, 0, 1))

		local master = clamp(1.0 - state.pausedBlend * 0.84, 0.03, 1.0)
		state.engineSource:setVolume((0.62 + state.afterburner * 0.26) * master * masterVolume * engineVolume)
		state.ambienceSource:setVolume((0.44 + state.speedNorm * 0.22 + state.waterProximity * 0.16) * master * masterVolume * ambienceVolume)
		if type(state.engineSource.setPitch) == "function" then
			state.engineSource:setPitch(enginePitch)
		end
		if type(state.ambienceSource.setPitch) == "function" then
			state.ambienceSource:setPitch(ambiencePitch)
		end

		queueGeneratedBuffer(state.engineSource, stepEngineSample)
		queueGeneratedBuffer(state.ambienceSource, stepAmbienceSample)
		state.running = true
	end

	function state:stop()
		self.running = false
		if self.engineSource then
			self.engineSource:setVolume(0)
			self.engineSource:stop()
		end
		if self.ambienceSource then
			self.ambienceSource:setVolume(0)
			self.ambienceSource:stop()
		end
	end

	return state
end

return audio
