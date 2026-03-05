
local hostAddy = "love.donreagan.ca:1988"

local function requireContract(modulePath, requiredKeys)
	local ok, loaded = pcall(require, modulePath)
	if not ok then
		error(string.format("Failed to require module '%s': %s", modulePath, tostring(loaded)), 2)
	end
	if type(loaded) ~= "table" then
		error(string.format("Module '%s' did not return a table.", modulePath), 2)
	end
	for _, key in ipairs(requiredKeys or {}) do
		if loaded[key] == nil then
			error(string.format("Module '%s' is missing required export '%s'.", modulePath, tostring(key)), 2)
		end
	end
	return loaded
end

function love.run()
	if love.load then love.load(love.arg.parseGameArguments(arg), arg) end

	-- We don't want the first frame's dt to include time taken by love.load.
	if love.timer then love.timer.step() end

	local dt = 0

	-- Main loop time.
	return function()
		-- Process events.
		if love.event then
			love.event.pump()
			for name, a,b,c,d,e,f in love.event.poll() do
				if name == "quit" then
					if not love.quit or not love.quit() then
						return a or 0
					end
				end
				love.handlers[name](a,b,c,d,e,f)
			end
		end

		-- Update dt, as we'll be passing it to update
		if love.timer then dt = love.timer.step() end

		-- Call update and draw
		if love.update then love.update(dt) end -- will pass 0 if love.timer is disabled

		if love.graphics and love.graphics.isActive() then
			love.graphics.origin()
			love.graphics.clear(love.graphics.getBackgroundColor())

			if love.draw then love.draw() end

			love.graphics.present()
		end

		if love.timer then love.timer.sleep(0.0000001) end -- this is why this function is delcared, default is 0.001
	end
end

local q = require("Source.Math.Quat")
local vector3 = require("Source.Math.Vector3")
local love = require "love" -- avoids nil report from intellisense, safe to remove if causes issues (it should be OK)
local enet = require "enet"
local engine = require "Source.Core.Engine"
local modelModule = require "Source.ModelModule.Init"
local networking = require "Source.Net.Networking"
local packetCodec = require "Source.Net.PacketCodec"
local renderer = require "Source.Render.GpuRenderer"
local cpuRenderClassifier = require "Source.Render.CpuRenderClassifier"
local controls = require "Source.Input.Controls"
local groundSystem = require "Source.Systems.GroundSystem"
local terrainSdfSystem = require "Source.Systems.TerrainSdfSystem"
local flightDynamics = require "Source.Systems.FlightDynamicsSystem"
local viewMath = require "Source.Math.ViewMath"
local cloudSim = require "Source.Systems.CloudSim"
local logger = require "Source.Core.Logger"
local objectDefs = require "Source.Core.ObjectDefs"
local appStateModule = requireContract("Source.Core.AppState", { "create" })
local viewSystemModule = requireContract("Source.Systems.ViewSystem", { "create" })
local inputSystemModule = requireContract("Source.Input.InputSystem", { "create" })
local relay = enet.host_create()
local relayServer = relay and relay:connect(hostAddy) or nil
local flightSimMode = false
local relative = true
local peers = {}
local event = nil
local objects = {}
local cubeModel = objectDefs.cubeModel
local cloudPuffModel = objectDefs.cloudPuffModel or cubeModel
local playerModel = cubeModel
-- too many local scope declarations? fuck it, we're global now baby!
---@diagnostic disable lowercase-global
local MIN_MODEL_SCALE = 0.1
defaultPlayerModelPath = "Assets/Models/DualEngine2.glb"
normalizedPlayerModelExtent = 2.2
defaultPlayerModelScale = 1.35
defaultWalkingModelScale = 1.0
playerPlaneModelScale = defaultPlayerModelScale
playerPlaneModelHash = "builtin-cube"
playerWalkingModelScale = defaultWalkingModelScale
playerWalkingModelHash = "builtin-cube"
playerModelScale = playerWalkingModelScale
playerModelHash = playerWalkingModelHash
local screen, camera, groundObject, terrainState, triangleCount, frameImage, renderMode, gpuErrorLogged --, zBuffer
local frameImageW, frameImageH = 0, 0
local worldRenderCanvas, worldRenderW, worldRenderH = nil, 0, 0
local useGpuRenderer = true
local pendingRestartRendererPreference = nil
local perfElapsed, perfFrames, perfLoggedLowFps = 0, 0, false
local zBuffer = {}
local mouseSensitivity = 0.001
local invertLookY = false
local showCrosshair = true
local showDebugOverlay = true
local colorCorruptionMode = false
local pauseTitleFont, pauseItemFont
local startupUi = {
	active = false,
	stage = "Booting",
	detail = "",
	progress = 0,
	startedAt = 0,
	completedAt = 0,
	currentEntry = 0,
	entries = {},
	fontKey = "",
	titleFont = nil,
	bodyFont = nil,
	smallFont = nil
}
local walkingPitchLimit = math.rad(89)
local defaults = requireContract("Source.Core.GameDefaults", { "create" }).create(cubeModel, q)
local viewState = defaults.viewState
local autoSmoke = defaults.autoSmoke
local altLookState = defaults.altLookState
local thirdPersonState = defaults.thirdPersonState
local mapState = defaults.mapState
local localPlayerObject = nil
local localCallsign = ""
local geoConfig = defaults.geoConfig
local hudTheme = defaults.hudTheme
local hudCache = defaults.hudCache

local graphicsSettings = defaults.graphicsSettings
currentGraphicsApiPreference = defaults.currentGraphicsApiPreference
local graphicsBackend = defaults.graphicsBackend
bootRestartPayload, bootRestartSnapshotState = nil, nil
RESTART_STATE_VERSION = defaults.restartStateVersion
RESTART_MODEL_ENCODED_LIMIT = defaults.restartModelEncodedLimit

local hudSettings = defaults.hudSettings
local audioSettings = defaults.audioSettings or {
	enabled = true,
	masterVolume = 0.8,
	engineVolume = 0.85,
	ambienceVolume = 0.7,
	enginePitch = 1.0,
	ambiencePitch = 1.0
}
local characterOrientation = defaults.characterOrientation
sunSettings = defaults.sunSettings
sunDebugObject = defaults.sunDebugObject
local characterPreview = defaults.characterPreview

localGroundClearance = defaults.localGroundClearance
modelLoadPrompt = defaults.modelLoadPrompt
modelLoadTargetRole = defaults.modelLoadTargetRole
playerModelCache = {}
modelTransferState = defaults.modelTransferState
local stateNet = {
	sendInterval = 1 / 20,
	lastSentAt = -math.huge,
	forceSend = true
}
local forceStateSync
local worldHalfExtent = defaults.worldHalfExtent
local flightModelConfig = defaults.flightModel or flightDynamics.defaultConfig()
local terrainSdfDefaults = defaults.terrainSdf or {}
local lightingModel = defaults.lightingModel or {}
local defaultGroundParams = terrainSdfSystem.normalizeGroundParams(defaults.defaultGroundParams, terrainSdfDefaults)
local activeGroundParams = nil
local windState = defaults.windState
local cloudState = defaults.cloudState
local drawDistanceChunkingTuning = defaults.drawDistanceChunkingTuning
local cloudNetState = defaults.cloudNetState
local groundNetState = defaults.groundNetState
local appState = appStateModule.create({
	graphicsSettings = graphicsSettings,
	viewState = viewState,
	mapState = mapState,
	modelTransferState = modelTransferState,
	cloudNetState = cloudNetState,
	groundNetState = groundNetState,
	stateNet = stateNet
})

-- Legacy STL axis correction. Keep this only for STL assets; GLB/GLTF stay in authored axes.
local identityModelRotationOffset = q.identity()
local legacyStlModelRotationOffset = q.normalize(q.multiply(
	q.fromAxisAngle({ 0, 1, 0 }, -math.pi * 0.5),
	q.fromAxisAngle({ 1, 0, 0 }, -math.pi * 0.5)
))

local function getModelRotationOffsetForHash(modelHash)
	local entry = playerModelCache and playerModelCache[modelHash or ""]
	if entry and entry.format == "stl" then
		return legacyStlModelRotationOffset
	end
	return identityModelRotationOffset
end

local function getModelRotationOffsetForRole(role, explicitModelHash)
	local modelHash = explicitModelHash
	if (type(modelHash) ~= "string" or modelHash == "") and getConfiguredModelForRole then
		local configuredHash = select(1, getConfiguredModelForRole(role))
		if type(configuredHash) == "string" and configuredHash ~= "" then
			modelHash = configuredHash
		end
	end
	return getModelRotationOffsetForHash(modelHash)
end

function normalizeRoleId(role)
	return (role == "walking") and "walking" or "plane"
end

function getRoleOrientation(role)
	local roleId = normalizeRoleId(role)
	local orient = characterOrientation[roleId]
	if type(orient) ~= "table" then
		orient = { yaw = 0, pitch = 0, roll = 0 }
		characterOrientation[roleId] = orient
	end
	orient.yaw = tonumber(orient.yaw) or 0
	orient.pitch = tonumber(orient.pitch) or 0
	orient.roll = tonumber(orient.roll) or 0
	return orient
end

function composeRoleOrientationOffset(role, modelHash)
	local orient = getRoleOrientation(role)
	local yawQuat = q.fromAxisAngle({ 0, 1, 0 }, math.rad(orient.yaw))
	local pitchQuat = q.fromAxisAngle({ 1, 0, 0 }, math.rad(orient.pitch))
	local rollQuat = q.fromAxisAngle({ 0, 0, 1 }, math.rad(orient.roll))
	local userOffset = q.normalize(q.multiply(q.multiply(yawQuat, pitchQuat), rollQuat))
	local baseOffset = getModelRotationOffsetForRole(role, modelHash)
	return q.normalize(q.multiply(baseOffset, userOffset))
end

function composePlayerModelRotation(baseRot, role, modelHash)
	return q.normalize(q.multiply(baseRot or q.identity(), composeRoleOrientationOffset(role, modelHash)))
end

function getSunDirection()
	local yaw = math.rad(tonumber(sunSettings.yaw) or 0)
	local pitch = math.rad(tonumber(sunSettings.pitch) or 45)
	local cp = math.cos(pitch)
	local dir = {
		cp * math.sin(yaw),
		math.sin(pitch),
		cp * math.cos(yaw)
	}
	local len = math.sqrt(dir[1] * dir[1] + dir[2] * dir[2] + dir[3] * dir[3])
	if len <= 1e-6 then
		return { 0, 1, 0 }
	end
	return { dir[1] / len, dir[2] / len, dir[3] / len }
end

function getSunLightColor()
	local intensity = math.max(0, tonumber(sunSettings.intensity) or 1.0)
	local r = math.max(0, tonumber(sunSettings.colorR) or 1.0)
	local g = math.max(0, tonumber(sunSettings.colorG) or 1.0)
	local b = math.max(0, tonumber(sunSettings.colorB) or 1.0)
	return { r * intensity, g * intensity, b * intensity }
end

function applySunSettingsToRenderer()
	if renderer and renderer.setLighting then
		local fogDensity = tonumber(sunSettings.fogDensity) or tonumber(lightingModel.fogDensity) or 0.00055
		local fogHeightFalloff = tonumber(sunSettings.fogHeightFalloff) or tonumber(lightingModel.fogHeightFalloff) or 0.0017
		local exposureEV = tonumber(sunSettings.exposureEV) or tonumber(lightingModel.exposureEV) or 0.0
		local turbidity = tonumber(sunSettings.turbidity) or tonumber(lightingModel.turbidity) or 2.4
		local shadowEnabled = (sunSettings.shadowEnabled ~= nil) and (sunSettings.shadowEnabled and true or false) or
			(lightingModel.shadowEnabled and true or false)
		local shadowSoftness = tonumber(sunSettings.shadowSoftness) or tonumber(lightingModel.shadowSoftness) or 1.6
		local shadowDistance = tonumber(sunSettings.shadowDistance) or tonumber(lightingModel.shadowDistance) or
			(tonumber(graphicsSettings.drawDistance) or 1800)
		renderer.setLighting({
			direction = getSunDirection(),
			color = getSunLightColor(),
			useAnalyticSky = true,
			sunIntensity = math.max(0, tonumber(sunSettings.intensity) or 1.0),
			ambient = math.max(0, tonumber(sunSettings.ambient) or 0.25),
			skyColor = {
				math.max(0, tonumber(sunSettings.skyR) or 0.42),
				math.max(0, tonumber(sunSettings.skyG) or 0.56),
				math.max(0, tonumber(sunSettings.skyB) or 0.82)
			},
			groundColor = {
				math.max(0, tonumber(sunSettings.groundR) or 0.18),
				math.max(0, tonumber(sunSettings.groundG) or 0.15),
				math.max(0, tonumber(sunSettings.groundB) or 0.11)
			},
			specularAmbient = math.max(0, tonumber(sunSettings.giSpecular) or 0.35),
			bounce = math.max(0, tonumber(sunSettings.giBounce) or 0.24),
			fogDensity = math.max(0, fogDensity),
			fogHeightFalloff = math.max(0, fogHeightFalloff),
			fogColor = {
				math.max(0, tonumber(sunSettings.fogR) or tonumber(lightingModel.fogR) or 0.64),
				math.max(0, tonumber(sunSettings.fogG) or tonumber(lightingModel.fogG) or 0.73),
				math.max(0, tonumber(sunSettings.fogB) or tonumber(lightingModel.fogB) or 0.84)
			},
			exposureEV = exposureEV,
			turbidity = turbidity,
			shadowEnabled = shadowEnabled,
			shadowSoftness = shadowSoftness,
			shadowDistance = shadowDistance
		})
	end
end


--[[
Notes:

positions are {x = left/right(width), y = up/down(height), z = in/out(depth)} IN CAMERA SPACE!!
]]

local function clamp(value, minValue, maxValue)
	if value < minValue then
		return minValue
	end
	if value > maxValue then
		return maxValue
	end
	return value
end

local function sanitizePositiveScale(value, fallback)
	local scale = tonumber(value)
	if scale then
		scale = math.abs(scale)
	end
	if not scale or scale < MIN_MODEL_SCALE then
		local fallbackScale = tonumber(fallback)
		if fallbackScale then
			fallbackScale = math.abs(fallbackScale)
		end
		scale = fallbackScale
	end
	if not scale or scale < MIN_MODEL_SCALE then
		scale = MIN_MODEL_SCALE
	end
	return scale
end

local function syncAppStateReferences()
	appState:merge({
		screen = screen,
		camera = camera,
		objects = objects,
		peers = peers,
		localPlayerObject = localPlayerObject,
		useGpuRenderer = useGpuRenderer,
		renderMode = renderMode
	})
end

local function safeNewFont(size)
	local ok, fontOrErr = pcall(function()
		return love.graphics.newFont(size)
	end)
	if ok and fontOrErr then
		return fontOrErr
	end
	return love.graphics.getFont()
end

local function ensureStartupUiFonts(width, height)
	local h = tonumber(height) or 0
	local key = string.format("%dx%d", tonumber(width) or 0, h)
	if startupUi.fontKey == key and startupUi.titleFont and startupUi.bodyFont and startupUi.smallFont then
		return
	end

	local baseSize = math.max(14, math.floor(h * 0.024))
	startupUi.titleFont = safeNewFont(math.max(28, baseSize + 12))
	startupUi.bodyFont = safeNewFont(math.max(14, baseSize))
	startupUi.smallFont = safeNewFont(math.max(12, baseSize - 2))
	startupUi.fontKey = key
end

local function drawStartupUi()
	local w, h = love.graphics.getDimensions()
	ensureStartupUiFonts(w, h)

	local now = love.timer.getTime()
	local pulse = 0.52 + 0.48 * math.sin(now * 3.3)
	local progress = clamp(tonumber(startupUi.progress) or 0, 0, 1)

	love.graphics.clear(0.03, 0.05, 0.09, 1.0)
	love.graphics.setColor(0.08, 0.14, 0.24, 0.82)
	love.graphics.circle("fill", w * 0.82, h * 0.18, math.max(w, h) * 0.58)
	love.graphics.setColor(0.05, 0.1, 0.16, 0.76)
	love.graphics.circle("fill", w * 0.12, h * 0.92, math.max(w, h) * 0.44)

	local panelW = math.floor(math.min(820, w * 0.82))
	local panelH = math.floor(math.min(360, h * 0.68))
	local panelX = math.floor((w - panelW) * 0.5)
	local panelY = math.floor((h - panelH) * 0.5)

	love.graphics.setColor(0, 0, 0, 0.36)
	love.graphics.rectangle("fill", panelX + 4, panelY + 4, panelW, panelH, 12, 12)
	love.graphics.setColor(0.03, 0.06, 0.1, 0.96)
	love.graphics.rectangle("fill", panelX, panelY, panelW, panelH, 12, 12)
	love.graphics.setColor(0.7, 0.86, 1.0, 0.24)
	love.graphics.rectangle("fill", panelX + 2, panelY + 2, panelW - 4, math.max(16, math.floor(panelH * 0.26)), 10, 10)
	love.graphics.setColor(0.74, 0.9, 1.0, 0.88)
	love.graphics.rectangle("line", panelX, panelY, panelW, panelH, 12, 12)

	local spinnerCx = panelX + panelW - 52
	local spinnerCy = panelY + 58
	local spinnerOuter = 13
	local spinnerInner = 7
	for i = 1, 12 do
		local angle = ((i - 1) / 12) * math.pi * 2 + now * 2.6
		local alpha = (i / 12) * (0.35 + 0.65 * pulse)
		local x0 = spinnerCx + math.cos(angle) * spinnerInner
		local y0 = spinnerCy + math.sin(angle) * spinnerInner
		local x1 = spinnerCx + math.cos(angle) * spinnerOuter
		local y1 = spinnerCy + math.sin(angle) * spinnerOuter
		love.graphics.setColor(0.78, 0.92, 1.0, alpha)
		love.graphics.line(x0, y0, x1, y1)
	end

	love.graphics.setColor(0.93, 0.98, 1.0, 0.98)
	love.graphics.setFont(startupUi.titleFont or love.graphics.getFont())
	love.graphics.print(startupUi.stage or "Booting", panelX + 28, panelY + 24)

	love.graphics.setColor(0.72, 0.84, 0.95, 0.96)
	love.graphics.setFont(startupUi.bodyFont or love.graphics.getFont())
	love.graphics.printf(startupUi.detail or "", panelX + 28, panelY + 74, panelW - 56, "left")

	local listY = panelY + 118
	local firstEntry = math.max(1, #startupUi.entries - 5)
	for i = firstEntry, #startupUi.entries do
		local row = i - firstEntry
		local y = listY + row * 24
		local entry = startupUi.entries[i]
		local prefix = "[ ]"
		local cr, cg, cb, ca = 0.58, 0.7, 0.82, 0.75
		if i < startupUi.currentEntry then
			prefix = "[x]"
			cr, cg, cb, ca = 0.36, 0.96, 0.62, 0.96
		elseif i == startupUi.currentEntry then
			prefix = "[>]"
			cr, cg, cb, ca = 0.9, 0.97, 1.0, 0.98
		end
		love.graphics.setColor(cr, cg, cb, ca)
		love.graphics.print(prefix .. " " .. tostring(entry and entry.label or ""), panelX + 30, y)
	end

	local barX = panelX + 28
	local barY = panelY + panelH - 72
	local barW = panelW - 56
	local barH = 17
	local fillW = math.floor((barW - 4) * progress + 0.5)
	fillW = clamp(fillW, 0, barW - 4)

	love.graphics.setColor(0.07, 0.12, 0.19, 0.98)
	love.graphics.rectangle("fill", barX, barY, barW, barH, 7, 7)
	love.graphics.setColor(0.74, 0.9, 1.0, 0.74)
	love.graphics.rectangle("line", barX, barY, barW, barH, 7, 7)
	love.graphics.setColor(0.24, 0.92, 0.58, 0.92)
	love.graphics.rectangle("fill", barX + 2, barY + 2, fillW, barH - 4, 6, 6)

	love.graphics.setFont(startupUi.smallFont or love.graphics.getFont())
	love.graphics.setColor(0.84, 0.94, 1.0, 0.92)
	love.graphics.print(string.format("%d%%", math.floor(progress * 100 + 0.5)), barX + barW - 48, barY + 1)

	local elapsed = math.max(0, now - (startupUi.startedAt or now))
	love.graphics.setColor(0.66, 0.8, 0.92, 0.9)
	love.graphics.print(string.format("Startup %.2fs", elapsed), barX, barY + 24)

	local backendName = tostring((graphicsBackend and graphicsBackend.name) or "Unknown")
	local backendVersion = tostring((graphicsBackend and graphicsBackend.version) or "")
	local backendLabel = backendName
	if backendVersion ~= "" then
		backendLabel = backendLabel .. " " .. backendVersion
	end
	love.graphics.printf("Renderer: " .. backendLabel, barX, barY + 24, barW, "right")
end

local function presentStartupUi()
	if not startupUi.active then
		return
	end

	pcall(function()
		love.graphics.push("all")
		love.graphics.origin()
		drawStartupUi()
		love.graphics.pop()
		love.graphics.present()
	end)

	if love.event and love.event.pump then
		pcall(love.event.pump)
	end
end

local function beginStartupUi()
	startupUi.active = true
	startupUi.stage = "Booting Engine"
	startupUi.detail = ""
	startupUi.progress = 0
	startupUi.startedAt = love.timer.getTime()
	startupUi.completedAt = 0
	startupUi.currentEntry = 0
	startupUi.entries = {}
	presentStartupUi()
end

local function setStartupUiStage(stage, progress, detail)
	if not startupUi.active then
		return
	end

	local stageName = tostring(stage or startupUi.stage or "Booting Engine")
	startupUi.stage = stageName

	local entryIndex = nil
	for i, entry in ipairs(startupUi.entries) do
		if entry.label == stageName then
			entryIndex = i
			break
		end
	end
	if not entryIndex then
		startupUi.entries[#startupUi.entries + 1] = { label = stageName }
		entryIndex = #startupUi.entries
	end
	startupUi.currentEntry = entryIndex

	if progress ~= nil then
		startupUi.progress = clamp(tonumber(progress) or startupUi.progress, 0, 1)
	end
	if detail ~= nil then
		startupUi.detail = tostring(detail)
	end

	presentStartupUi()
end

local function finishStartupUi()
	if not startupUi.active then
		return
	end
	startupUi.progress = 1
	startupUi.completedAt = love.timer.getTime()
	presentStartupUi()
	startupUi.active = false
end

local function randomRange(minValue, maxValue)
	return minValue + (maxValue - minValue) * math.random()
end

local function syncChunkingWithDrawDistance(forceGroundRebuild)
	local drawDistance = clamp(tonumber(graphicsSettings.drawDistance) or 1800, 300, 5000)
	local spawnRadius = math.max(
		drawDistanceChunkingTuning.cloudSpawnMin,
		drawDistance * drawDistanceChunkingTuning.cloudSpawnRatio
	)
	local despawnRadius = math.max(
		spawnRadius + drawDistanceChunkingTuning.cloudDespawnPadding,
		drawDistance * drawDistanceChunkingTuning.cloudDespawnRatio
	)
	cloudState.spawnRadius = math.floor(spawnRadius + 0.5)
	cloudState.despawnRadius = math.floor(despawnRadius + 0.5)

	if forceGroundRebuild and type(updateGroundStreaming) == "function" then
		if updateGroundStreaming(true) then
			invalidateMapCache()
		end
	end
end

local function sanitizeCallsign(value)
	if type(value) ~= "string" then
		return nil
	end
	local text = value:gsub("[^ -~]", "")
	text = text:gsub("|", "")
	text = text:gsub("^%s+", ""):gsub("%s+$", "")
	if text == "" then
		return nil
	end
	if #text > 20 then
		text = text:sub(1, 20)
	end
	return text
end

local function detectDefaultCallsign()
	local fromEnv = os.getenv("USERNAME") or os.getenv("USER") or "Pilot"
	return sanitizeCallsign(fromEnv) or "Pilot"
end

local function drawHudPlate(x, y, w, h, radius)
	local r = radius or 6
	love.graphics.setColor(hudTheme.shadow[1], hudTheme.shadow[2], hudTheme.shadow[3], hudTheme.shadow[4])
	love.graphics.rectangle("fill", x + 2, y + 2, w, h, r, r)
	love.graphics.setColor(hudTheme.plateBg[1], hudTheme.plateBg[2], hudTheme.plateBg[3], hudTheme.plateBg[4])
	love.graphics.rectangle("fill", x, y, w, h, r, r)
	love.graphics.setColor(hudTheme.plateHi[1], hudTheme.plateHi[2], hudTheme.plateHi[3], hudTheme.plateHi[4])
	love.graphics.rectangle("fill", x + 1, y + 1, w - 2, math.max(4, h * 0.36), r, r)
	love.graphics.setColor(hudTheme.plateBorder[1], hudTheme.plateBorder[2], hudTheme.plateBorder[3],
		hudTheme.plateBorder[4])
	love.graphics.rectangle("line", x, y, w, h, r, r)
end

function drawArcLine(cx, cy, radius, a0, a1, segments)
	local pts = {}
	local seg = math.max(4, math.floor(segments or 20))
	for i = 0, seg do
		local t = i / seg
		local a = a0 + (a1 - a0) * t
		pts[#pts + 1] = cx + math.cos(a) * radius
		pts[#pts + 1] = cy + math.sin(a) * radius
	end
	if #pts >= 4 then
		love.graphics.line(pts)
	end
end

function resetHudCaches()
	hudCache.dialCanvas = nil
	hudCache.dialSize = 0
	hudCache.dialFontHeight = 0
	hudCache.dialProfileKey = ""
	hudCache.controlCanvas = nil
	hudCache.controlW = 0
	hudCache.controlH = 0
end

function invalidateMapCache()
	mapState.mapImage = nil
	mapState.mapImageData = nil
	mapState.mapBuildJob = nil
	mapState.mapRes = 0
	mapState.lastCenterX = nil
	mapState.lastCenterZ = nil
	mapState.lastHeading = nil
	mapState.lastExtent = nil
	mapState.lastGroundSignature = nil
	mapState.lastRefreshAt = -math.huge
end

function groundParamsSignature(params)
	if not params then
		return "none"
	end
	local g = params.grassColor or { 0, 0, 0 }
	local wc = params.waterColor or params.waterColour or { 0, 0, 0 }
	local r = params.roadColor or { 0, 0, 0 }
	local f = params.fieldColor or { 0, 0, 0 }
	local gv = params.grassVar or { 0, 0, 0 }
	local wv = params.waterVar or { 0, 0, 0 }
	local rv = params.roadVar or { 0, 0, 0 }
	local fv = params.fieldVar or { 0, 0, 0 }
	local wr = clamp(tonumber(params.waterRatio) or 0, 0, 1)
	return table.concat({
		tostring(params.seed or 0),
		string.format("%.3f", tonumber(params.chunkSize) or 0),
		tostring(params.lod0Radius or 0),
		tostring(params.lod1Radius or 0),
		string.format("%.5f", tonumber(params.tileSize) or 0),
		tostring(params.gridCount or 0),
		tostring(params.roadCount or 0),
		string.format("%.5f", tonumber(params.heightAmplitude) or 0),
		string.format("%.6f", tonumber(params.heightFrequency) or 0),
		tostring((params.caveEnabled ~= false) and 1 or 0),
		tostring(params.tunnelCount or 0),
		tostring(params.generatorVersion or 0),
		string.format("%.5f", wr),
		string.format("%.5f", tonumber(params.roadDensity) or 0),
		tostring(params.fieldCount or 0),
		string.format("%.3f,%.3f,%.3f", g[1] or 0, g[2] or 0, g[3] or 0),
		string.format("%.3f,%.3f,%.3f", wc[1] or 0, wc[2] or 0, wc[3] or 0),
		string.format("%.3f,%.3f,%.3f", r[1] or 0, r[2] or 0, r[3] or 0),
		string.format("%.3f,%.3f,%.3f", f[1] or 0, f[2] or 0, f[3] or 0),
		string.format("%.3f,%.3f,%.3f", gv[1] or 0, gv[2] or 0, gv[3] or 0),
		string.format("%.3f,%.3f,%.3f", wv[1] or 0, wv[2] or 0, wv[3] or 0),
		string.format("%.3f,%.3f,%.3f", rv[1] or 0, rv[2] or 0, rv[3] or 0),
		string.format("%.3f,%.3f,%.3f", fv[1] or 0, fv[2] or 0, fv[3] or 0)
	}, "|")
end

local function ensureDialCanvas(size)
	local dialSize = math.max(96, math.floor(size or 160))
	local font = love.graphics.getFont()
	local fontHeight = font and font:getHeight() or 0
	local maxKph = math.max(200, tonumber(hudSettings.speedometerMaxKph) or 1000)
	local minorStep = clamp(math.floor(tonumber(hudSettings.speedometerMinorStepKph) or 20), 5, maxKph)
	local majorStep = clamp(math.floor(tonumber(hudSettings.speedometerMajorStepKph) or 100), minorStep, maxKph)
	local labelStep = clamp(math.floor(tonumber(hudSettings.speedometerLabelStepKph) or 200), majorStep, maxKph)
	local redline = clamp(math.floor(tonumber(hudSettings.speedometerRedlineKph) or 850), minorStep, maxKph)
	local dialProfileKey = table.concat({
		tostring(maxKph),
		tostring(minorStep),
		tostring(majorStep),
		tostring(labelStep),
		tostring(redline)
	}, "|")
	if hudCache.dialCanvas and
		hudCache.dialSize == dialSize and
		hudCache.dialFontHeight == fontHeight and
		hudCache.dialProfileKey == dialProfileKey then
		return hudCache.dialCanvas
	end

	local ok, canvasOrErr = pcall(function()
		return love.graphics.newCanvas(dialSize, dialSize)
	end)
	if not ok or not canvasOrErr then
		hudCache.dialCanvas = nil
		hudCache.dialSize = 0
		hudCache.dialFontHeight = 0
		hudCache.dialProfileKey = ""
		return nil
	end

	local canvas = canvasOrErr
	local prevCanvas = love.graphics.getCanvas()
	local prevShader = love.graphics.getShader()

	love.graphics.setCanvas(canvas)
	love.graphics.clear(0, 0, 0, 0)
	love.graphics.setShader()

	local cx = dialSize * 0.5
	local cy = dialSize * 0.5
	local radius = dialSize * 0.5
	local gaugeStart = math.rad(150)
	local gaugeSweep = math.rad(240)
	local maxMph = maxKph * 0.621371

	love.graphics.setColor(0.02, 0.04, 0.06, 0.95)
	love.graphics.circle("fill", cx, cy, radius * 0.95)
	love.graphics.setColor(0.12, 0.2, 0.28, 0.95)
	love.graphics.circle("fill", cx, cy, radius * 0.9)
	love.graphics.setColor(0.72, 0.86, 1.0, 0.9)
	love.graphics.circle("line", cx, cy, radius * 0.9)
	love.graphics.setColor(0.65, 0.78, 0.9, 0.32)
	love.graphics.circle("line", cx, cy, radius * 0.63)

	love.graphics.setColor(1.0, 0.22, 0.22, 0.82)
	local redStart = gaugeStart + (redline / maxKph) * gaugeSweep
	drawArcLine(cx, cy, radius * 0.88, redStart, gaugeStart + gaugeSweep, 14)

	local function isMultiple(value, step)
		if step <= 0 then
			return false
		end
		local frac = value / step
		return math.abs(frac - math.floor(frac + 0.5)) < 1e-4
	end

	for kph = 0, maxKph, minorStep do
		local ratio = kph / maxKph
		local angle = gaugeStart + ratio * gaugeSweep
		local major = isMultiple(kph, majorStep)
		local x0 = cx + math.cos(angle) * (radius * 0.88)
		local y0 = cy + math.sin(angle) * (radius * 0.88)
		local x1 = cx + math.cos(angle) * (major and (radius * 0.72) or (radius * 0.79))
		local y1 = cy + math.sin(angle) * (major and (radius * 0.72) or (radius * 0.79))
		love.graphics.setColor(major and 0.92 or 0.62, major and 0.96 or 0.74, major and 1.0 or 0.86,
			major and 0.98 or 0.68)
		love.graphics.line(x0, y0, x1, y1)
		if major and isMultiple(kph, labelStep) then
			local tx = cx + math.cos(angle) * (radius * 0.58)
			local ty = cy + math.sin(angle) * (radius * 0.58)
			local label = tostring(kph)
			love.graphics.print(label, tx - (love.graphics.getFont():getWidth(label) * 0.5), ty - (fontHeight * 0.5))
		end
	end

	local mphMinorStep = math.max(10, math.floor((minorStep * 0.621371) + 0.5))
	local mphMajorStep = math.max(mphMinorStep, math.floor((majorStep * 0.621371) + 0.5))
	local mphLabelStep = math.max(mphMajorStep, math.floor((labelStep * 0.621371) + 0.5))
	for mph = 0, math.floor(maxMph + 0.5), mphMinorStep do
		local ratio = mph / math.max(1, maxMph)
		local angle = gaugeStart + ratio * gaugeSweep
		local major = isMultiple(mph, mphMajorStep)
		local x0 = cx + math.cos(angle) * (radius * 0.62)
		local y0 = cy + math.sin(angle) * (radius * 0.62)
		local x1 = cx + math.cos(angle) * (major and (radius * 0.52) or (radius * 0.56))
		local y1 = cy + math.sin(angle) * (major and (radius * 0.52) or (radius * 0.56))
		love.graphics.setColor(0.62, 0.75, 0.9, major and 0.9 or 0.62)
		love.graphics.line(x0, y0, x1, y1)
		if major and isMultiple(mph, mphLabelStep) then
			local tx = cx + math.cos(angle) * (radius * 0.44)
			local ty = cy + math.sin(angle) * (radius * 0.44)
			local label = tostring(mph)
			love.graphics.print(label, tx - (love.graphics.getFont():getWidth(label) * 0.5), ty - (fontHeight * 0.5))
		end
	end

	love.graphics.setColor(0.84, 0.94, 1.0, 0.9)
	love.graphics.print("kph", cx - (love.graphics.getFont():getWidth("kph") * 0.5), cy + radius * 0.75)
	love.graphics.setColor(0.62, 0.78, 0.95, 0.85)
	love.graphics.print("mph", cx - (love.graphics.getFont():getWidth("mph") * 0.5), cy + radius * 0.45)

	love.graphics.setCanvas(prevCanvas)
	love.graphics.setShader(prevShader)

	hudCache.dialCanvas = canvas
	hudCache.dialSize = dialSize
	hudCache.dialFontHeight = fontHeight
	hudCache.dialProfileKey = dialProfileKey
	return canvas
end

local function ensureControlPanelCanvas(width, height)
	local w = math.max(120, math.floor(width or 200))
	local h = math.max(80, math.floor(height or 120))
	if hudCache.controlCanvas and hudCache.controlW == w and hudCache.controlH == h then
		return hudCache.controlCanvas
	end

	local ok, canvasOrErr = pcall(function()
		return love.graphics.newCanvas(w, h)
	end)
	if not ok or not canvasOrErr then
		hudCache.controlCanvas = nil
		hudCache.controlW = 0
		hudCache.controlH = 0
		return nil
	end

	local canvas = canvasOrErr
	local prevCanvas = love.graphics.getCanvas()
	local prevShader = love.graphics.getShader()
	love.graphics.setCanvas(canvas)
	love.graphics.clear(0, 0, 0, 0)
	love.graphics.setShader()

	drawHudPlate(0, 0, w, h, 9)
	love.graphics.setColor(0.7, 0.86, 1.0, 0.18)
	love.graphics.line(10, h * 0.35, w - 10, h * 0.35)
	love.graphics.line(10, h * 0.72, w - 10, h * 0.72)

	love.graphics.setCanvas(prevCanvas)
	love.graphics.setShader(prevShader)
	hudCache.controlCanvas = canvas
	hudCache.controlW = w
	hudCache.controlH = h
	return canvas
end

local function ensureWorldRenderCanvas(width, height)
	local w = math.max(16, math.floor(width or screen.w))
	local h = math.max(16, math.floor(height or screen.h))
	if worldRenderCanvas and worldRenderW == w and worldRenderH == h then
		return worldRenderCanvas
	end

	local ok, canvasOrErr = pcall(function()
		return love.graphics.newCanvas(w, h)
	end)
	if not ok or not canvasOrErr then
		worldRenderCanvas = nil
		worldRenderW = 0
		worldRenderH = 0
		return nil
	end

	worldRenderCanvas = canvasOrErr
	worldRenderW = w
	worldRenderH = h
	return worldRenderCanvas
end

local function worldToGeo(worldX, worldZ)
	local metersPerUnit = math.max(1e-6, tonumber(geoConfig.metersPerUnit) or 1.0)
	local eastMeters = worldX * metersPerUnit
	local northMeters = worldZ * metersPerUnit
	local metersPerDegLat = 111132.92
	local lat = (tonumber(geoConfig.originLat) or 0) + (northMeters / metersPerDegLat)
	local latRad = math.rad(tonumber(geoConfig.originLat) or 0)
	local cosLat = math.cos(latRad)
	if math.abs(cosLat) < 1e-6 then
		cosLat = (cosLat < 0) and -1e-6 or 1e-6
	end
	local metersPerDegLon = 111320 * cosLat
	local lon = (tonumber(geoConfig.originLon) or 0) + (eastMeters / metersPerDegLon)
	return lat, lon
end

local function composeWalkingRotation(yaw, pitch)
	local yawQuat = q.fromAxisAngle({ 0, 1, 0 }, yaw)
	local right = q.rotateVector(yawQuat, { 1, 0, 0 })
	local pitchQuat = q.fromAxisAngle(right, pitch)
	return q.normalize(q.multiply(pitchQuat, yawQuat))
end

local function syncWalkingLookFromRotation()
	if not camera then
		return
	end

	local forward = q.rotateVector(camera.rot, { 0, 0, 1 })
	local flatMag = math.sqrt(forward[1] * forward[1] + forward[3] * forward[3])
	local pitch = math.atan(forward[2], math.max(flatMag, 1e-6))
	local yaw = math.atan(forward[1], forward[3])

	camera.walkYaw = viewMath.wrapAngle(yaw)
	camera.walkPitch = clamp(pitch, -walkingPitchLimit, walkingPitchLimit)
	camera.rot = composeWalkingRotation(camera.walkYaw, camera.walkPitch)
end

local function resetAltLookState()
	altLookState.held = false
	altLookState.yaw = 0
	altLookState.pitch = 0
end

local function applyAltLookMouse(dx, dy)
	if not camera then
		return
	end

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
	if obj.isLocalPlayer and viewState.mode == "first_person" then
		return false
	end
	return true
end

function appendSunDebugRenderObject(renderObjects, activeCam, maxDist)
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
	local renderObjects = {}
	local activeCam = viewState.activeCamera or camera
	local maxDist = math.max(300, tonumber(graphicsSettings.drawDistance) or 1800)
	for _, obj in ipairs(objects) do
		if shouldRenderObjectForView(obj) then
			local cullPos = obj.terrainCenter or obj.pos
			if activeCam and activeCam.pos and cullPos then
				local dx = (cullPos[1] or 0) - activeCam.pos[1]
				local dy = (cullPos[2] or 0) - activeCam.pos[2]
				local dz = (cullPos[3] or 0) - activeCam.pos[3]
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

local viewSystem = viewSystemModule.create({
	q = q,
	engine = engine,
	vector3 = vector3,
	viewMath = viewMath,
	clamp = clamp,
	getSunDirection = getSunDirection,
	camera = function()
		return camera
	end,
	viewState = function()
		return viewState
	end,
	altLookState = function()
		return altLookState
	end,
	thirdPersonState = function()
		return thirdPersonState
	end,
	objects = function()
		return objects
	end,
	localPlayerObject = function()
		return localPlayerObject
	end,
	sunSettings = function()
		return sunSettings
	end,
	sunDebugObject = function()
		return sunDebugObject
	end,
	graphicsSettings = function()
		return graphicsSettings
	end,
	mouseSensitivity = function()
		return mouseSensitivity
	end,
	invertLookY = function()
		return invertLookY
	end,
	walkingPitchLimit = function()
		return walkingPitchLimit
	end
})

composeWalkingRotation = viewSystem.composeWalkingRotation
syncWalkingLookFromRotation = viewSystem.syncWalkingLookFromRotation
resetAltLookState = viewSystem.resetAltLookState
applyAltLookMouse = viewSystem.applyAltLookMouse
buildAltLookCamera = viewSystem.buildAltLookCamera
buildThirdPersonCamera = viewSystem.buildThirdPersonCamera
resolveActiveRenderCamera = viewSystem.resolveActiveRenderCamera
shouldRenderObjectForView = viewSystem.shouldRenderObjectForView
appendSunDebugRenderObject = viewSystem.appendSunDebugRenderObject
buildRenderObjectList = viewSystem.buildRenderObjectList

local function ensureMapGroundImage(panelSize, mapCam)
	if not mapCam then
		return nil
	end

	local function beginMapBuild(targetRes, groundSig)
		if (not mapState.mapImageData) or mapState.mapRes ~= targetRes then
			local okData, dataOrErr = pcall(function()
				return love.image.newImageData(targetRes, targetRes)
			end)
			if not okData or not dataOrErr then
				mapState.mapImage = nil
				mapState.mapImageData = nil
				mapState.mapBuildJob = nil
				mapState.mapRes = 0
				return false
			end
			mapState.mapImageData = dataOrErr
			mapState.mapRes = targetRes
		end

		local centerX = (mapCam.pos and mapCam.pos[1]) or 0
		local centerZ = (mapCam.pos and mapCam.pos[3]) or 0
		local heading = tonumber(mapCam.heading) or 0
		mapState.mapBuildJob = {
			targetRes = targetRes,
			nextRow = 0,
			worldPerPixel = (mapCam.extent * 2) / targetRes,
			cosHeading = math.cos(heading),
			sinHeading = math.sin(heading),
			centerX = centerX,
			centerZ = centerZ,
			heading = heading,
			extent = mapCam.extent,
			groundSig = groundSig
		}
		return true
	end

	local function stepMapBuild()
		local job = mapState.mapBuildJob
		local data = mapState.mapImageData
		if type(job) ~= "table" or not data then
			mapState.mapBuildJob = nil
			return false
		end

		local targetRes = job.targetRes
		local halfRes = targetRes * 0.5
		local worldPerPixel = job.worldPerPixel
		local cosH = job.cosHeading
		local sinH = job.sinHeading
		local colorParams = activeGroundParams or defaultGroundParams
		local terrainRef = terrainState or colorParams
		local rowsPerStep = clamp(math.floor(targetRes * 0.08), 6, 24)
		local startRow = math.floor(tonumber(job.nextRow) or 0)
		local endRow = math.min(targetRes - 1, startRow + rowsPerStep - 1)

		for y = startRow, endRow do
			for x = 0, targetRes - 1 do
				local mapX = (x + 0.5 - halfRes) * worldPerPixel
				local mapZ = (halfRes - (y + 0.5)) * worldPerPixel
				local worldDX = cosH * mapX + sinH * mapZ
				local worldDZ = -sinH * mapX + cosH * mapZ
				local worldX = job.centerX + worldDX
				local worldZ = job.centerZ + worldDZ
				local c = terrainSdfSystem.sampleGroundColorAtWorld(worldX, worldZ, terrainRef)
				local edgeX = math.abs((x + 0.5) / targetRes * 2 - 1)
				local edgeY = math.abs((y + 0.5) / targetRes * 2 - 1)
				local vignette = 1 - (math.max(edgeX, edgeY) * 0.09)
				vignette = clamp(vignette, 0.78, 1.0)
				data:setPixel(
					x,
					y,
					clamp(c[1] * vignette, 0, 1),
					clamp(c[2] * vignette, 0, 1),
					clamp(c[3] * vignette, 0, 1),
					0.98
				)
			end
		end

		job.nextRow = endRow + 1
		if job.nextRow < targetRes then
			return false
		end

		if mapState.mapImage then
			local okReplace = pcall(function()
				mapState.mapImage:replacePixels(data)
			end)
			if not okReplace then
				mapState.mapImage = nil
			end
		end
		if not mapState.mapImage then
			local okImage, imageOrErr = pcall(function()
				return love.graphics.newImage(data)
			end)
			if not okImage or not imageOrErr then
				mapState.mapImage = nil
				mapState.mapBuildJob = nil
				return false
			end
			mapState.mapImage = imageOrErr
			mapState.mapImage:setFilter("linear", "linear")
		end

		mapState.lastCenterX = job.centerX
		mapState.lastCenterZ = job.centerZ
		mapState.lastHeading = job.heading
		mapState.lastExtent = job.extent
		mapState.lastGroundSignature = job.groundSig
		mapState.lastRefreshAt = love.timer.getTime()
		mapState.mapBuildJob = nil
		return true
	end

	local now = love.timer.getTime()
	local targetRes = clamp(math.floor(panelSize), 96, 224)
	local groundSig = groundParamsSignature(activeGroundParams or defaultGroundParams)
	local needRefresh = false
	if not mapState.mapImage then
		needRefresh = true
	elseif mapState.mapRes ~= targetRes then
		needRefresh = true
	elseif mapState.lastGroundSignature ~= groundSig then
		needRefresh = true
	elseif math.abs((mapState.lastExtent or 0) - mapCam.extent) > 1e-6 then
		needRefresh = true
	elseif (now - (mapState.lastRefreshAt or -math.huge)) >= 0.2 then
		local dx = math.abs((mapState.lastCenterX or mapCam.pos[1]) - mapCam.pos[1])
		local dz = math.abs((mapState.lastCenterZ or mapCam.pos[3]) - mapCam.pos[3])
		local headingDelta = math.abs(viewMath.shortestAngleDelta(mapState.lastHeading or mapCam.heading, mapCam.heading))
		local moveThreshold = math.max(3.0, mapCam.extent * 0.03)
		if dx > moveThreshold or dz > moveThreshold or headingDelta > math.rad(2.0) then
			needRefresh = true
		end
	end

	local activeJob = type(mapState.mapBuildJob) == "table"
	if activeJob then
		local job = mapState.mapBuildJob
		if job.targetRes ~= targetRes or job.groundSig ~= groundSig or math.abs((job.extent or 0) - mapCam.extent) > 1e-6 then
			beginMapBuild(targetRes, groundSig)
		end
		stepMapBuild()
		return mapState.mapImage
	end

	if not needRefresh then
		return mapState.mapImage
	end

	if beginMapBuild(targetRes, groundSig) then
		stepMapBuild()
	end
	return mapState.mapImage
end

local function updateLogicalMapCamera()
	if not camera then
		mapState.logicalCamera = nil
		return nil
	end

	mapState.logicalCamera = mapState.logicalCamera or {
		pos = { 0, 0, 0 },
		heading = 0,
		extent = mapState.zoomExtents[2] or 400
	}

	local mapCam = mapState.logicalCamera
	mapCam.pos[1] = camera.pos[1]
	mapCam.pos[2] = camera.pos[2]
	mapCam.pos[3] = camera.pos[3]
	mapCam.heading = viewMath.getYawFromRotation(camera.rot, q)
	mapCam.extent = mapState.zoomExtents[mapState.zoomIndex] or (worldHalfExtent or 1000)
	return mapCam
end

local function syncLocalPlayerObject()
	if not localPlayerObject or not camera then
		return
	end
	local activeRole = getActiveModelRole and getActiveModelRole() or "plane"
	localPlayerObject.basePos = localPlayerObject.basePos or { 0, 0, 0 }
	localPlayerObject.basePos[1] = camera.pos[1]
	localPlayerObject.basePos[2] = camera.pos[2]
	localPlayerObject.basePos[3] = camera.pos[3]
	localPlayerObject.pos[1] = camera.pos[1]
	localPlayerObject.pos[2] = camera.pos[2] + (localPlayerObject.visualOffsetY or 0)
	localPlayerObject.pos[3] = camera.pos[3]
	localPlayerObject.rot = composePlayerModelRotation(camera.rot, activeRole, localPlayerObject.modelHash)
end

function onRoleOrientationChanged(role)
	if getActiveModelRole and getActiveModelRole() == normalizeRoleId(role) then
		syncLocalPlayerObject()
	end
	forceStateSync()
end

function bytesToHex(raw)
	local out = {}
	for i = 1, #raw do
		out[i] = string.format("%02x", raw:byte(i))
	end
	return table.concat(out)
end

function fallbackHash(raw)
	local bitOps = _G.bit or _G.bit32
	if not bitOps or type(raw) ~= "string" then
		return string.format("len%08x", #raw)
	end

	local hash = 2166136261
	for i = 1, #raw do
		hash = bitOps.bxor(hash, raw:byte(i))
		hash = (hash * 16777619) % 4294967296
	end
	if hash < 0 then
		hash = hash + 4294967296
	end
	return string.format("fnv%08x", hash)
end

function hashModelBytes(raw)
	if love.data and love.data.hash then
		local ok, digest = pcall(function()
			return love.data.hash("string", "sha256", raw)
		end)
		local loveMajor = (love.getVersion and select(1, love.getVersion())) or 0
		if not ok and loveMajor < 12 then
			ok, digest = pcall(function()
				return love.data.hash("sha256", raw)
			end)
		end
		if ok and type(digest) == "string" then
			return bytesToHex(digest)
		end
	end
	return fallbackHash(raw)
end

function encodeModelBytes(raw)
	if love.data and love.data.encode then
		local ok, out = pcall(function()
			return love.data.encode("string", "base64", raw)
		end)
		if ok and type(out) == "string" then
			return out
		end
	end
	return nil
end

function decodeModelBytes(encoded)
	if love.data and love.data.decode then
		local ok, out = pcall(function()
			return love.data.decode("string", "base64", encoded)
		end)
		if ok and type(out) == "string" then
			return out
		end
	end
	return nil
end

function readFileBytes(path)
	if type(path) ~= "string" or path == "" then
		return nil, "missing path"
	end

	local raw, readErr = love.filesystem.read(path)
	if raw then
		return raw
	end

	local handle = io.open(path, "rb")
	if not handle then
		return nil, tostring(readErr or "file open failed")
	end
	local bytes = handle:read("*a")
	handle:close()
	if not bytes or bytes == "" then
		return nil, "empty file"
	end
	return bytes
end

function getPathDirectory(path)
	if type(path) ~= "string" then
		return "."
	end
	local normalized = path:gsub("\\", "/")
	local dir = normalized:match("^(.*)/[^/]+$")
	if not dir or dir == "" then
		return "."
	end
	return dir
end

function computeModelBounds(model)
	if not model or not model.vertices or #model.vertices == 0 then
		return { minY = -1, maxY = 1, bottomDistance = 1, topDistance = 1 }
	end

	local minY, maxY = math.huge, -math.huge
	for _, vertex in ipairs(model.vertices) do
		local vy = tonumber(vertex[2]) or 0
		if vy < minY then
			minY = vy
		end
		if vy > maxY then
			maxY = vy
		end
	end
	if minY > maxY then
		minY, maxY = -1, 1
	end

	return {
		minY = minY,
		maxY = maxY,
		bottomDistance = math.max(0.01, -minY),
		topDistance = math.max(0.01, maxY)
	}
end

function getGroundClearance()
	if camera and camera.box and camera.box.halfSize then
		return camera.box.halfSize.y or localGroundClearance
	end
	return localGroundClearance
end

function computeModelVisualOffsetY(modelHash, scale)
	local entry = playerModelCache[modelHash]
	local bounds = entry and entry.bounds
	local bottomDistance = ((bounds and bounds.bottomDistance) or 1) * math.max(0.01, scale or 1)
	return bottomDistance - getGroundClearance()
end

function applyPlaneVisualToObject(obj, modelHash, scale)
	if not obj then
		return
	end

	local entry = playerModelCache[modelHash]
	obj.model = (entry and entry.model) or cubeModel
	obj.materials = entry and entry.materials or nil
	obj.images = entry and entry.images or nil
	obj.submeshes = entry and entry.submeshes or nil
	obj.faceMaterials = entry and entry.faceMaterials or nil
	obj.modelAssetId = entry and entry.assetId or nil
	local s = sanitizePositiveScale(scale, 1)
	obj.scale = { s, s, s }
	obj.halfSize = { x = s, y = s, z = s }
	obj.modelHash = modelHash
	obj.uvFlipV = false
	obj.modelRotationOffset = getModelRotationOffsetForHash(modelHash)
	obj.visualOffsetY = computeModelVisualOffsetY(modelHash, s)
	if obj.basePos then
		obj.pos = {
			obj.basePos[1],
			obj.basePos[2] + obj.visualOffsetY,
			obj.basePos[3]
		}
	end
end

function refreshAllPeerModels()
	for peerId in pairs(peers) do
		updatePeerVisualModel(peerId)
	end
end

function cacheModelEntry(raw, sourceLabel, loaderOptions)
	if type(raw) ~= "string" or raw == "" then
		return nil, "empty model data"
	end
	if #raw > (modelTransferState.maxRawBytes or math.huge) then
		return nil, string.format(
			"Model too large for relay transfer (%d bytes > %d).",
			#raw,
			modelTransferState.maxRawBytes
		)
	end

	local hash = hashModelBytes(raw)
	if playerModelCache[hash] then
		return playerModelCache[hash]
	end

	loaderOptions = loaderOptions or {}
	local assetId, loadErr = modelModule.loadFromBytes(raw, sourceLabel, {
		targetExtent = normalizedPlayerModelExtent,
		chunkSize = modelTransferState.chunkSize,
		forcedHash = hash,
		baseDir = loaderOptions.baseDir or "."
	})
	if not assetId then
		return nil, loadErr
	end
	local asset = modelModule.getAsset(assetId)
	if not asset or not asset.model then
		return nil, "model loader returned invalid asset"
	end

	local encoded = encodeModelBytes(raw)
	if encoded and encoded ~= "" and #encoded > (modelTransferState.maxEncodedBytes or math.huge) then
		return nil, string.format(
			"Encoded model too large for relay transfer (%d bytes > %d).",
			#encoded,
			modelTransferState.maxEncodedBytes
		)
	end
	local chunkSize = modelTransferState.chunkSize
	local chunks = {}
	if encoded and encoded ~= "" then
		for i = 1, #encoded, chunkSize do
			chunks[#chunks + 1] = encoded:sub(i, i + chunkSize - 1)
		end
	end

	local entry = {
		hash = hash,
		source = sourceLabel or "local",
		raw = raw,
		model = asset.model,
		bounds = computeModelBounds(asset.model),
		assetId = assetId,
		format = asset.sourceFormat or "unknown",
		materials = asset.materials,
		images = asset.images,
		submeshes = asset.geometry and asset.geometry.submeshes or nil,
		faceMaterials = asset.geometry and asset.geometry.faceMaterials or asset.model.faceMaterials,
		encoded = encoded,
		encodedLength = encoded and #encoded or 0,
		byteLength = #raw,
		chunkSize = chunkSize,
		chunks = chunks,
		chunkCount = #chunks
	}
	playerModelCache[hash] = entry
	return entry
end

function setActivePlayerModel(entry, sourceLabel, targetRole)
	if not entry then
		return
	end

	local role = (targetRole == "walking") and "walking" or "plane"
	if role == "walking" then
		playerWalkingModelHash = entry.hash
	else
		playerPlaneModelHash = entry.hash
	end

	if role == getActiveModelRole() then
		syncActivePlayerModelState()
	end
	refreshAllPeerModels()
	forceStateSync()

	logger.log(string.format(
		"Loaded %s model '%s' [%s] (%d verts, %d faces, format=%s).",
		role,
		sourceLabel or entry.source or "unknown",
		entry.hash,
		#(entry.model.vertices or {}),
		#(entry.model.faces or {}),
		entry.format or "unknown"
	))
	local shareMeta = modelModule.getBlobMeta("model", entry.hash)
	if shareMeta and (tonumber(shareMeta.chunkCount) or 0) > 0 then
		logger.log(string.format(
			"Model %s is shareable (%d bytes, %d chunks).",
			entry.hash,
			tonumber(shareMeta.rawBytes) or (entry.byteLength or 0),
			tonumber(shareMeta.chunkCount) or 0
		))
	else
		logger.log("Model " .. tostring(entry.hash) .. " is local-only (transfer payload unavailable).")
	end
	local materialCount = #(entry.materials or {})
	local imageCount = #(entry.images or {})
	local readyImageCount = 0
	for _, img in ipairs(entry.images or {}) do
		if img and img.image then
			readyImageCount = readyImageCount + 1
		end
	end
	logger.log(string.format(
		"Model resources: materials=%d images=%d (gpu-ready=%d) submeshes=%d",
		materialCount,
		imageCount,
		readyImageCount,
		#(entry.submeshes or {})
	))
	if materialCount > 0 then
		local m0 = entry.materials[1] or {}
		local baseTex = m0.baseColorTexture and m0.baseColorTexture.imageIndex or nil
		local mrTex = m0.metallicRoughnessTexture and m0.metallicRoughnessTexture.imageIndex or nil
		logger.log(string.format(
			"Material[1]: alpha=%s baseTexImage=%s mrTexImage=%s",
			tostring(m0.alphaMode or "OPAQUE"),
			tostring(baseTex or "nil"),
			tostring(mrTex or "nil")
		))
	end
end

function loadPlayerModelFromRaw(raw, sourceLabel, targetRole, loaderOptions)
	local entry, err = cacheModelEntry(raw, sourceLabel, loaderOptions)
	if not entry then
		return false, err
	end
	setActivePlayerModel(entry, sourceLabel, targetRole)
	return true, entry
end

function loadPlayerModelFromPath(path, targetRole)
	local raw, readErr = readFileBytes(path)
	if not raw then
		logger.log("Player model load failed, using fallback: " .. tostring(readErr))
		return false, tostring(readErr)
	end

	local ok, entryOrErr = loadPlayerModelFromRaw(raw, path, targetRole, {
		baseDir = getPathDirectory(path)
	})
	if not ok then
		logger.log("Player model parse failed, using fallback: " .. tostring(entryOrErr))
		return false, tostring(entryOrErr)
	end
	return true, entryOrErr
end

function loadPlayerModelFromStl(path, targetRole)
	return loadPlayerModelFromPath(path, targetRole)
end

do
	local fallbackEntry = {
		hash = "builtin-cube",
		source = "builtin cube",
		raw = nil,
		model = cubeModel,
		bounds = computeModelBounds(cubeModel),
		format = "builtin",
		materials = nil,
		images = nil,
		submeshes = nil,
		faceMaterials = nil,
		encoded = nil,
		encodedLength = 0,
		byteLength = 0,
		chunkSize = modelTransferState.chunkSize,
		chunks = {},
		chunkCount = 0
	}
	playerModelCache[fallbackEntry.hash] = fallbackEntry
end


local function setMouseCapture(enabled)
	love.mouse.setRelativeMode(enabled)
	pcall(love.mouse.setVisible, not enabled)
	relative = enabled
end

function getActiveModelRole()
	return flightSimMode and "plane" or "walking"
end

function getConfiguredModelForRole(role)
	if role == "walking" then
		return playerWalkingModelHash or "builtin-cube",
			sanitizePositiveScale(playerWalkingModelScale, defaultWalkingModelScale)
	end
	return playerPlaneModelHash or "builtin-cube", sanitizePositiveScale(playerPlaneModelScale, defaultPlayerModelScale)
end

function getConfiguredSkinHashForRole(role)
	local roleId = normalizeRoleId(role)
	local modelHash = (roleId == "walking") and playerWalkingModelHash or playerPlaneModelHash
	local entry = playerModelCache and playerModelCache[modelHash or "builtin-cube"]
	if not entry or not entry.assetId then
		return ""
	end
	local asset = modelModule.getAsset(entry.assetId)
	if not asset or type(asset.paintByRole) ~= "table" then
		return ""
	end
	local overlay = asset.paintByRole[roleId]
	if overlay and type(overlay.hash) == "string" then
		return overlay.hash
	end
	return ""
end

function syncActivePlayerModelState()
	playerPlaneModelScale = sanitizePositiveScale(playerPlaneModelScale, defaultPlayerModelScale)
	playerWalkingModelScale = sanitizePositiveScale(playerWalkingModelScale, defaultWalkingModelScale)
	local modelHash, modelScale = getConfiguredModelForRole(getActiveModelRole())
	playerModelHash = modelHash
	playerModelScale = modelScale
	local entry = playerModelCache and playerModelCache[playerModelHash]
	playerModel = (entry and entry.model) or cubeModel
	if localPlayerObject then
		applyPlaneVisualToObject(localPlayerObject, playerModelHash, playerModelScale)
		syncLocalPlayerObject()
	end
	forceStateSync()
end

function ensureFlightSpawnSafety(minGroundClearance, minForwardSpeed)
	if not camera then
		return
	end

	local clearance = math.max(8, tonumber(minGroundClearance) or 45)
	local launchSpeed = math.max(8, tonumber(minForwardSpeed) or 34)
	local halfY = (camera.box and camera.box.halfSize and camera.box.halfSize.y) or 1.8
	local terrainRef = terrainState or activeGroundParams or defaultGroundParams
	local groundY = terrainSdfSystem.sampleGroundHeightAtWorld(camera.pos[1], camera.pos[3], terrainRef) or 0
	local minY = groundY + halfY + clearance
	if (camera.pos[2] or 0) < minY then
		camera.pos[2] = minY
	end

	camera.flightVel = camera.flightVel or { 0, 0, 0 }
	local vx = tonumber(camera.flightVel[1]) or 0
	local vy = tonumber(camera.flightVel[2]) or 0
	local vz = tonumber(camera.flightVel[3]) or 0
	local forward = q.rotateVector(camera.rot or q.identity(), { 0, 0, 1 })
	local fx = tonumber(forward[1]) or 0
	local fz = tonumber(forward[3]) or 0
	local fLen = math.sqrt(fx * fx + fz * fz)
	if fLen <= 1e-6 then
		fx, fz = 0, 1
	else
		fx, fz = fx / fLen, fz / fLen
	end

	local planarSpeed = math.sqrt(vx * vx + vz * vz)
	if planarSpeed < launchSpeed then
		vx = fx * launchSpeed
		vz = fz * launchSpeed
	end
	vy = math.max(vy, 0)
	camera.flightVel[1] = vx
	camera.flightVel[2] = vy
	camera.flightVel[3] = vz

	camera.vel = { camera.flightVel[1], camera.flightVel[2], camera.flightVel[3] }
	camera.throttle = math.max(tonumber(camera.throttle) or 0, 0.46)
	if camera.flightSimState and type(camera.flightSimState.engine) == "table" then
		camera.flightSimState.engine.throttle = math.max(
			tonumber(camera.flightSimState.engine.throttle) or 0,
			camera.throttle
		)
	end
	camera.onGround = false
	if camera.box and camera.box.pos then
		camera.box.pos[1] = camera.pos[1]
		camera.box.pos[2] = camera.pos[2]
		camera.box.pos[3] = camera.pos[3]
	end
end

local function buildFlightEntryRotation(sourceCamera)
	local yaw = tonumber(sourceCamera and sourceCamera.walkYaw)
	if yaw == nil then
		yaw = viewMath.getYawFromRotation((sourceCamera and sourceCamera.rot) or q.identity(), q)
	end
	yaw = viewMath.wrapAngle(yaw or 0)
	local yawQuat = q.fromAxisAngle({ 0, 1, 0 }, yaw)
	local right = q.rotateVector(yawQuat, { 1, 0, 0 })
	local pitchUp = math.rad(6)
	local pitchQuat = q.fromAxisAngle(right, -pitchUp)
	return q.normalize(q.multiply(pitchQuat, yawQuat))
end

local function setFlightMode(enabled)
	flightSimMode = enabled and true or false
	if (not flightSimMode) and startupUi and startupUi.audioRuntime and type(startupUi.audioRuntime.stop) == "function" then
		startupUi.audioRuntime:stop()
	end
	if camera then
		camera.yoke = camera.yoke or { pitch = 0, yaw = 0, roll = 0 }
		camera.flightRotVel = camera.flightRotVel or { pitch = 0, yaw = 0, roll = 0 }
		if flightSimMode then
			camera.rot = buildFlightEntryRotation(camera)
			camera.walkYaw = nil
			camera.walkPitch = nil
			camera.flightVel = { 0, 0, 0 }
			camera.flightAngVel = { 0, 0, 0 }
			camera.flightRotVel.pitch = 0
			camera.flightRotVel.yaw = 0
			camera.flightRotVel.roll = 0
			camera.yoke.pitch = 0
			camera.yoke.yaw = 0
			camera.yoke.roll = 0
			camera.yokeLastMouseInputAt = -math.huge
			camera.flightSimState = nil
			ensureFlightSpawnSafety(45, 32)
		else
			camera.throttle = 0
			camera.flightVel = { 0, 0, 0 }
			camera.flightAngVel = { 0, 0, 0 }
			camera.vel = { 0, 0, 0 }
			camera.yoke.pitch = 0
			camera.yoke.yaw = 0
			camera.yoke.roll = 0
			camera.yokeLastMouseInputAt = -math.huge
			camera.flightRotVel.pitch = 0
			camera.flightRotVel.yaw = 0
			camera.flightRotVel.roll = 0
			syncWalkingLookFromRotation()
		end
	end
	syncActivePlayerModelState()
end

function addCrashCraterAt(impactX, impactY, impactZ, impactSpeed)
	local speed = math.max(0, tonumber(impactSpeed) or 0)
	local radius = clamp(6 + speed * 0.22, 6, 28)
	local depth = clamp(radius * 0.48, 1.8, 14)
	local added, craterState = terrainSdfSystem.addCrater({
		x = impactX,
		y = impactY,
		z = impactZ,
		radius = radius,
		depth = depth,
		rim = 0.14
	}, {
		defaultGroundParams = defaultGroundParams,
		activeGroundParams = activeGroundParams,
		terrainState = terrainState,
		camera = camera,
		objects = objects,
		q = q
	})
	if added and craterState then
		activeGroundParams = craterState.activeGroundParams or activeGroundParams
		terrainState = craterState.terrainState or terrainState
		groundObject = nil
		invalidateMapCache()
		return craterState.crater or {
			x = impactX,
			y = impactY,
			z = impactZ,
			radius = radius,
			depth = depth
		}
	end
	return {
		x = impactX,
		y = impactY,
		z = impactZ,
		radius = radius,
		depth = depth
	}
end

function handleFlightCrashEvent(crashEvent)
	if type(crashEvent) ~= "table" or not camera then
		return
	end

	local impactPos = crashEvent.position or {}
	local impactX = tonumber(impactPos[1]) or tonumber(camera.pos and camera.pos[1]) or 0
	local impactY = tonumber(impactPos[2]) or tonumber(camera.pos and camera.pos[2]) or 0
	local impactZ = tonumber(impactPos[3]) or tonumber(camera.pos and camera.pos[3]) or 0
	local impactSpeed = tonumber(crashEvent.totalSpeed) or 0

	local crater = addCrashCraterAt(impactX, impactY, impactZ, impactSpeed)
	local normal = crashEvent.normal or { 0, 1, 0 }
	local nx = tonumber(normal[1]) or 0
	local nz = tonumber(normal[3]) or 0
	local horizontalLen = math.sqrt(nx * nx + nz * nz)
	if horizontalLen <= 1e-4 then
		local vx = tonumber(camera.flightVel and camera.flightVel[1]) or 0
		local vz = tonumber(camera.flightVel and camera.flightVel[3]) or 0
		if math.abs(vx) + math.abs(vz) > 1e-4 then
			nx = -vx
			nz = -vz
			horizontalLen = math.sqrt(nx * nx + nz * nz)
		end
	end
	if horizontalLen <= 1e-4 then
		nx = 1
		nz = 0
		horizontalLen = 1
	end
	nx = nx / horizontalLen
	nz = nz / horizontalLen

	setFlightMode(false)

	local spawnOffset = math.max(6, tonumber(crater.radius) or 10) + 4.5
	local spawnX = impactX + nx * spawnOffset
	local spawnZ = impactZ + nz * spawnOffset
	local terrainRef = terrainState or activeGroundParams or defaultGroundParams
	local groundY = terrainSdfSystem.sampleGroundHeightAtWorld(spawnX, spawnZ, terrainRef) or impactY
	local halfY = (camera.box and camera.box.halfSize and camera.box.halfSize.y) or localGroundClearance or 1.8
	local spawnY = groundY + halfY + 0.2

	camera.pos = { spawnX, spawnY, spawnZ }
	camera.vel = { 0, 0, 0 }
	camera.flightVel = { 0, 0, 0 }
	camera.flightAngVel = { 0, 0, 0 }
	camera.onGround = true
	camera.throttle = 0
	camera.flightCrashEvent = nil

	local toCraterX = impactX - spawnX
	local toCraterZ = impactZ - spawnZ
	camera.walkYaw = viewMath.wrapAngle(math.atan(toCraterX, toCraterZ))
	camera.walkPitch = 0
	camera.rot = composeWalkingRotation(camera.walkYaw, camera.walkPitch)
	if camera.box and camera.box.pos then
		camera.box.pos[1] = spawnX
		camera.box.pos[2] = spawnY
		camera.box.pos[3] = spawnZ
	end

	syncLocalPlayerObject()
	resolveActiveRenderCamera()
	if cloudNetState and cloudNetState.role == "authority" and type(sendGroundSnapshot) == "function" then
		sendGroundSnapshot("crash crater")
	end
	logger.log(string.format(
		"Crash detected: speed=%.1f m/s, crater radius=%.1f m, switched to walking mode.",
		impactSpeed,
		tonumber(crater.radius) or 0
	))
end

function cycleViewMode()
	if viewState.mode == "third_person" then
		viewState.mode = "first_person"
	else
		viewState.mode = "third_person"
	end
	resetAltLookState()
	resolveActiveRenderCamera()
	logger.log("View mode set to " .. viewState.mode)
end

function setRendererPreference(enableGpu)
	local desiredGpu = enableGpu and true or false
	if desiredGpu and not renderer.isReady() then
		useGpuRenderer = false
		renderMode = "CPU"
		logger.log("GPU renderer unavailable; staying in CPU mode.")
		return false
	end

	local oldMode = renderMode
	useGpuRenderer = desiredGpu
	renderMode = (useGpuRenderer and renderer.isReady()) and "GPU" or "CPU"
	if oldMode ~= renderMode then
		logger.log("Render mode toggled to " .. renderMode)
	end
	return useGpuRenderer
end

function resetCameraTransform()
	if not camera then
		return
	end

	camera.pos = { 0, 0, -5 }
	camera.rot = q.identity()
	camera.vel = { 0, 0, 0 }
	camera.flightVel = { 0, 0, 0 }
	camera.flightRotVel = { pitch = 0, yaw = 0, roll = 0 }
	camera.yoke = { pitch = 0, yaw = 0, roll = 0 }
	camera.yokeLastMouseInputAt = -math.huge
	camera.throttle = 0
	camera.onGround = false
	camera.walkYaw = 0
	camera.walkPitch = 0
	resetAltLookState()
	if camera.box and camera.box.halfSize then
		camera.box.pos = { camera.pos[1], camera.pos[2] - camera.box.halfSize.y, camera.pos[3] }
	end
	syncLocalPlayerObject()
	resolveActiveRenderCamera()
	logger.log("Camera transform reset.")
end

function clearPeerObjects()
	for i = #objects, 1, -1 do
		if objects[i].id then
			table.remove(objects, i)
		end
	end
	peers = {}
	modelTransferState.incoming = {}
	modelTransferState.outgoing = {}
	modelTransferState.requestedAt = {}
	modelTransferState.lastRequestedPruneAt = love.timer.getTime()
	syncAppStateReferences()
end

function splitPacketFields(data)
	return packetCodec.splitFields(data, "|")
end

local function parsePacketKeyValues(parts, startIndex)
	return packetCodec.parseKeyValueFields(parts, startIndex or 2)
end

local function readTrailingPacketInteger(parts, startIndex)
	return packetCodec.readTrailingInteger(parts, startIndex or 2)
end

local function makeBlobTransferKey(kind, hash)
	return tostring(kind or "") .. "|" .. tostring(hash or "")
end

local function formatNetFloat(value)
	return string.format("%.4f", tonumber(value) or 0)
end

forceStateSync = function()
	stateNet.forceSend = true
	stateNet.lastSentAt = -math.huge
end

function encodeColor3Token(color)
	return table.concat({
		formatNetFloat(color[1]),
		formatNetFloat(color[2]),
		formatNetFloat(color[3])
	}, ",")
end

function decodeColor3Token(token, fallback)
	local r, g, b = type(token) == "string" and token:match("^([^,]+),([^,]+),([^,]+)$")
	if not r or not g or not b then
		return groundSystem.cloneColor3(fallback)
	end
	return groundSystem.sanitizeColor3({
		tonumber(r),
		tonumber(g),
		tonumber(b)
	}, fallback)
end

function encodeCraterListToken(craters)
	if type(craters) ~= "table" or #craters <= 0 then
		return ""
	end
	local parts = {}
	for i = 1, #craters do
		local crater = craters[i]
		if type(crater) == "table" then
			parts[#parts + 1] = table.concat({
				formatNetFloat(crater.x),
				formatNetFloat(crater.y),
				formatNetFloat(crater.z),
				formatNetFloat(crater.radius),
				formatNetFloat(crater.depth),
				formatNetFloat(crater.rim)
			}, ",")
		end
	end
	return table.concat(parts, ";")
end

function decodeCraterListToken(token)
	local out = {}
	if type(token) ~= "string" or token == "" then
		return out
	end
	for entry in token:gmatch("[^;]+") do
		local x, y, z, radius, depth, rim = entry:match("^([^,]+),([^,]+),([^,]+),([^,]+),([^,]+),([^,]+)$")
		if x and y and z and radius and depth and rim then
			out[#out + 1] = {
				x = tonumber(x) or 0,
				y = tonumber(y) or 0,
				z = tonumber(z) or 0,
				radius = tonumber(radius) or 8.0,
				depth = tonumber(depth) or 3.0,
				rim = tonumber(rim) or 0.12
			}
		end
	end
	return out
end

local function relayIsConnected()
	if not relayServer then
		return false
	end

	local ok, state = pcall(function()
		return relayServer:state()
	end)
	return ok and state == "connected"
end

local function pruneRequestedTransferMap(now)
	now = tonumber(now) or love.timer.getTime()
	local requestedAt = modelTransferState.requestedAt
	if type(requestedAt) ~= "table" then
		modelTransferState.requestedAt = {}
		modelTransferState.lastRequestedPruneAt = now
		return
	end

	local pruneInterval = math.max(1, tonumber(modelTransferState.requestedPruneInterval) or 8)
	local lastPruneAt = tonumber(modelTransferState.lastRequestedPruneAt) or -math.huge
	if (now - lastPruneAt) < pruneInterval then
		return
	end
	modelTransferState.lastRequestedPruneAt = now

	local expiry = math.max(pruneInterval, tonumber(modelTransferState.requestedExpiry) or 120)
	local survivors = {}
	local survivorCount = 0
	for key, requestedTime in pairs(requestedAt) do
		local stamp = tonumber(requestedTime)
		if stamp and (now - stamp) <= expiry then
			survivorCount = survivorCount + 1
			survivors[survivorCount] = { key = key, requestedAt = stamp }
		else
			requestedAt[key] = nil
		end
	end

	local maxEntries = math.max(32, math.floor(tonumber(modelTransferState.maxRequestedEntries) or 512))
	if survivorCount <= maxEntries then
		return
	end

	table.sort(survivors, function(a, b)
		return a.requestedAt < b.requestedAt
	end)
	for i = 1, survivorCount - maxEntries do
		requestedAt[survivors[i].key] = nil
	end
end

local function queueOutgoingBlobTransfer(kind, hash)
	if type(kind) ~= "string" or kind == "" or type(hash) ~= "string" or hash == "" then
		return false
	end
	local key = makeBlobTransferKey(kind, hash)
	if modelTransferState.outgoing[key] then
		return true
	end
	local meta = modelModule.getBlobMeta(kind, hash)
	if type(meta) ~= "table" or math.max(0, math.floor(tonumber(meta.chunkCount) or -1)) <= 0 then
		if kind == "model" and playerModelCache[hash] then
			logger.log("Model " .. tostring(hash) .. " is local-only (no transferable payload).")
		end
		return false
	end
	modelTransferState.outgoing[key] = {
		kind = kind,
		hash = hash,
		meta = meta,
		metaSent = false,
		nextChunk = 1,
		lastTouched = love.timer.getTime()
	}
	return true
end

function queueModelTransfer(hash)
	return queueOutgoingBlobTransfer("model", hash)
end

local function requestBlobFromPeers(kind, hash, reason)
	if type(kind) ~= "string" or kind == "" or type(hash) ~= "string" or hash == "" then
		return false
	end
	if kind == "model" then
		if hash == "builtin-cube" then
			return false
		end
		if playerModelCache[hash] then
			return false
		end
	end
	if not relayIsConnected() then
		return false
	end

	local key = makeBlobTransferKey(kind, hash)
	local now = love.timer.getTime()
	pruneRequestedTransferMap(now)
	local lastRequested = modelTransferState.requestedAt[key] or -math.huge
	if (now - lastRequested) < modelTransferState.requestCooldown then
		return false
	end

	local packet = table.concat({
		"BLOB_REQUEST",
		"kind=" .. kind,
		"hash=" .. hash,
		"ts=" .. formatNetFloat(now)
	}, "|")
	local ok = pcall(function()
		relayServer:send(packet)
	end)
	if ok then
		modelTransferState.requestedAt[key] = now
		if reason and reason ~= "" then
			logger.log("Requested " .. tostring(kind) .. " blob " .. hash .. " (" .. reason .. ").")
		end
	end
	return ok
end

function requestModelFromPeers(hash, reason)
	return requestBlobFromPeers("model", hash, reason)
end

function updatePeerVisualModel(peerId)
	local peer = peerId and peers[peerId]
	if not peer then
		return
	end

	local role = (peer.remoteRole == "walking") and "walking" or "plane"
	local planeHash = peer.planeModelHash or peer.modelHash or "builtin-cube"
	local walkingHash = peer.walkingModelHash or peer.modelHash or planeHash or "builtin-cube"
	local planeScale = sanitizePositiveScale(
		peer.planeModelScale,
		(peer.scale and peer.scale[1]) or defaultPlayerModelScale
	)
	local walkingScale = sanitizePositiveScale(peer.walkingModelScale, planeScale or defaultWalkingModelScale)
	local wantedHash = (role == "walking") and walkingHash or planeHash
	local peerScale = (role == "walking") and walkingScale or planeScale

	peer.remoteRole = role
	peer.planeModelHash = planeHash
	peer.walkingModelHash = walkingHash
	peer.planeModelScale = planeScale
	peer.walkingModelScale = walkingScale
	peer.modelHash = wantedHash
	peer.scale = { peerScale, peerScale, peerScale }
	peer.halfSize = { x = peerScale, y = peerScale, z = peerScale }

	if playerModelCache[wantedHash] then
		applyPlaneVisualToObject(peer, wantedHash, peerScale)
		peer.modelHash = wantedHash
	else
		applyPlaneVisualToObject(peer, "builtin-cube", peerScale)
		peer.modelHash = wantedHash
		requestModelFromPeers(wantedHash, "missing for peer " .. tostring(peerId))
	end

	if planeHash ~= "builtin-cube" and not playerModelCache[planeHash] then
		requestModelFromPeers(planeHash, "prefetch plane for peer " .. tostring(peerId))
	end
	if walkingHash ~= "builtin-cube" and not playerModelCache[walkingHash] then
		requestModelFromPeers(walkingHash, "prefetch walking for peer " .. tostring(peerId))
	end
end

function handleModelRequestPacket(data)
	local parts = splitPacketFields(data)
	local packetType = parts[1]
	local kind
	local requestedHash
	if packetType == "MODEL_REQUEST" then
		kind = "model"
		requestedHash = parts[2]
	elseif packetType == "BLOB_REQUEST" then
		local kv = parsePacketKeyValues(parts, 2)
		kind = kv.kind or "model"
		requestedHash = kv.hash
	else
		return false
	end

	if type(kind) ~= "string" or kind == "" then
		return false
	end
	if type(requestedHash) ~= "string" or requestedHash == "" then
		return false
	end

	return queueOutgoingBlobTransfer(kind, requestedHash)
end

function handleModelMetaPacket(data)
	local parts = splitPacketFields(data)
	local packetType = parts[1]
	local kind
	local hash
	local byteLength
	local encodedLength
	local chunkSize
	local chunkCount
	local ownerId
	local role
	local modelHash
	if packetType == "MODEL_META" then
		if #parts < 7 then
			return false
		end
		kind = "model"
		hash = parts[2]
		byteLength = math.max(0, math.floor(tonumber(parts[3]) or -1))
		encodedLength = math.max(0, math.floor(tonumber(parts[4]) or -1))
		chunkSize = math.max(1, math.floor(tonumber(parts[5]) or -1))
		chunkCount = math.max(0, math.floor(tonumber(parts[6]) or -1))
	elseif packetType == "BLOB_META" then
		local kv = parsePacketKeyValues(parts, 2)
		kind = kv.kind or "model"
		hash = kv.hash
		byteLength = math.max(0, math.floor(tonumber(kv.rawBytes) or -1))
		encodedLength = math.max(0, math.floor(tonumber(kv.encodedBytes) or -1))
		chunkSize = math.max(1, math.floor(tonumber(kv.chunkSize) or -1))
		chunkCount = math.max(0, math.floor(tonumber(kv.chunkCount) or -1))
		ownerId = kv.ownerId
		role = kv.role
		modelHash = kv.modelHash
	else
		return false
	end
	local senderId = readTrailingPacketInteger(parts, 2)
	if senderId == nil and packetType == "MODEL_META" then
		return false
	end
	if type(kind) ~= "string" or kind == "" then
		return false
	end
	if type(hash) ~= "string" or hash == "" or byteLength <= 0 or encodedLength <= 0 or chunkCount <= 0 then
		return false
	end
	if chunkSize > 4096 or chunkCount > 20000 then
		return false
	end
	if byteLength > (modelTransferState.maxRawBytes or math.huge) then
		return false
	end
	if encodedLength > (modelTransferState.maxEncodedBytes or math.huge) then
		return false
	end
	if kind == "model" and playerModelCache[hash] then
		return true
	end
	local okMeta, errMeta = modelModule.acceptBlobMeta({
		kind = kind,
		hash = hash,
		rawBytes = byteLength,
		encodedBytes = encodedLength,
		chunkSize = chunkSize,
		chunkCount = chunkCount,
		ownerId = ownerId,
		role = role,
		modelHash = modelHash
	})
	if not okMeta then
		logger.log("Rejected blob meta " .. tostring(kind) .. ":" .. tostring(hash) .. " - " .. tostring(errMeta))
		return false
	end

	local key = makeBlobTransferKey(kind, hash)
	modelTransferState.incoming[key] = {
		kind = kind,
		hash = hash,
		senderId = senderId,
		lastTouched = love.timer.getTime()
	}
	return true
end

function finalizeIncomingModelTransfer(hash, transfer)
	local completed = modelModule.getCompletedBlob("model", hash)
	if type(completed) ~= "table" or type(completed.raw) ~= "string" or completed.raw == "" then
		return false
	end
	local raw = completed.raw
	if #raw > (modelTransferState.maxRawBytes or math.huge) then
		return false
	end
	local expectedBytes = math.max(0, math.floor(tonumber(completed.meta and completed.meta.rawBytes) or -1))
	if expectedBytes > 0 and #raw ~= expectedBytes then
		return false
	end

	local senderLabel = tostring((transfer and transfer.senderId) or "?")
	local entry, err = cacheModelEntry(raw, "peer " .. senderLabel)
	if not entry or entry.hash ~= hash then
		logger.log("Remote model cache failed for " .. tostring(hash) .. ": " .. tostring(err))
		return false
	end

	for peerId, peer in pairs(peers) do
		if peer and peer.modelHash == hash then
			updatePeerVisualModel(peerId)
		end
	end
	logger.log("Cached remote model " .. hash .. " (" .. tostring(#raw) .. " bytes).")
	return true
end

function handleModelChunkPacket(data)
	local parts = splitPacketFields(data)
	local packetType = parts[1]
	local kind
	local hash
	local chunkIndex
	local chunkData
	if packetType == "MODEL_CHUNK" then
		if #parts < 5 then
			return false
		end
		kind = "model"
		hash = parts[2]
		chunkIndex = math.floor(tonumber(parts[3]) or -1)
		chunkData = parts[4] or ""
	elseif packetType == "BLOB_CHUNK" then
		local kv = parsePacketKeyValues(parts, 2)
		kind = kv.kind or "model"
		hash = kv.hash
		chunkIndex = math.floor(tonumber(kv.idx or kv.chunkIndex) or -1)
		chunkData = kv.data or kv.chunkData or ""
	else
		return false
	end
	if type(kind) ~= "string" or kind == "" then
		return false
	end
	if type(hash) ~= "string" or hash == "" then
		return false
	end
	local senderId = readTrailingPacketInteger(parts, 2)
	if senderId == nil and packetType == "MODEL_CHUNK" then
		return false
	end

	local key = makeBlobTransferKey(kind, hash)
	local transfer = modelTransferState.incoming[key]
	if not transfer then
		if kind == "model" then
			requestModelFromPeers(hash, "chunk without metadata")
		end
		return false
	end
	if transfer.senderId and senderId and transfer.senderId ~= senderId then
		return false
	end
	if chunkIndex < 1 then
		return false
	end
	if type(chunkData) ~= "string" or chunkData == "" then
		return false
	end

	transfer.lastTouched = love.timer.getTime()
	local completeKind, completeHash, chunkErr = modelModule.acceptBlobChunk({
		kind = kind,
		hash = hash,
		chunkIndex = chunkIndex,
		chunkData = chunkData
	})
	if chunkErr then
		modelTransferState.incoming[key] = nil
		if kind == "model" then
			requestModelFromPeers(hash, "validation retry")
		end
		return false
	end
	if completeKind and completeHash then
		local completeKey = makeBlobTransferKey(completeKind, completeHash)
		local completeTransfer = modelTransferState.incoming[completeKey] or transfer
		modelTransferState.incoming[completeKey] = nil
		if completeKind == "model" then
			if not finalizeIncomingModelTransfer(completeHash, completeTransfer) then
				requestModelFromPeers(completeHash, "validation retry")
			end
		end
	end
	return true
end

function pumpModelTransfers(now)
	pruneRequestedTransferMap(now)
	local connected = relayIsConnected()
	if connected then
		for _, transfer in pairs(modelTransferState.outgoing) do
			if transfer and (not transfer.metaSent) then
				local packet = table.concat({
					"BLOB_META",
					"kind=" .. tostring(transfer.kind),
					"hash=" .. tostring(transfer.hash),
					"rawBytes=" .. tostring(transfer.meta.rawBytes or 0),
					"encodedBytes=" .. tostring(transfer.meta.encodedBytes or 0),
					"chunkSize=" .. tostring(transfer.meta.chunkSize or 0),
					"chunkCount=" .. tostring(transfer.meta.chunkCount or 0)
				}, "|")
				pcall(function()
					relayServer:send(packet)
				end)
				transfer.metaSent = true
			end
		end

		local chunkBudget = modelTransferState.maxChunksPerTick
		for transferKey, transfer in pairs(modelTransferState.outgoing) do
			if chunkBudget <= 0 then
				break
			end
			local chunkCount = math.max(0, math.floor(tonumber(transfer and transfer.meta and transfer.meta.chunkCount) or -1))
			if not transfer or chunkCount <= 0 then
				modelTransferState.outgoing[transferKey] = nil
			else
				while chunkBudget > 0 and transfer.nextChunk <= chunkCount do
					local chunkPayload = modelModule.getBlobChunk(transfer.kind, transfer.hash, transfer.nextChunk)
					if type(chunkPayload) ~= "string" then
						modelTransferState.outgoing[transferKey] = nil
						break
					end
					local packet = table.concat({
						"BLOB_CHUNK",
						"kind=" .. tostring(transfer.kind),
						"hash=" .. tostring(transfer.hash),
						"idx=" .. tostring(transfer.nextChunk),
						"data=" .. tostring(chunkPayload)
					}, "|")
					pcall(function()
						relayServer:send(packet)
					end)
					transfer.nextChunk = transfer.nextChunk + 1
					transfer.lastTouched = now
					chunkBudget = chunkBudget - 1
				end

				if transfer.nextChunk > chunkCount then
					modelTransferState.outgoing[transferKey] = nil
				end
			end
		end
	end

	for incomingKey, transfer in pairs(modelTransferState.incoming) do
		if (now - (transfer.lastTouched or now)) > modelTransferState.transferTimeout then
			modelTransferState.incoming[incomingKey] = nil
			if transfer.kind == "model" then
				requestModelFromPeers(transfer.hash, "timed out")
			end
		end
	end
end

local function setCloudRole(role, detail)
	if cloudNetState.role == role then
		return
	end
	cloudNetState.role = role
	if detail and detail ~= "" then
		logger.log(string.format("Cloud sync role: %s (%s)", role, detail))
	else
		logger.log("Cloud sync role: " .. role)
	end
end

local function beginCloudRoleElection(reason)
	cloudNetState.connectedAt = love.timer.getTime()
	cloudNetState.sawPeerOnJoin = false
	cloudNetState.authorityPeerId = nil
	cloudNetState.lastSnapshotSentAt = -math.huge
	cloudNetState.lastSnapshotReceivedAt = -math.huge
	cloudNetState.lastSnapshotRequestAt = -math.huge
	groundNetState.authorityPeerId = nil
	groundNetState.lastSnapshotReceivedAt = -math.huge
	groundNetState.lastSnapshotRequestAt = -math.huge
	setCloudRole("pending", reason or "relay connected")
end

local function requestCloudSnapshot(reason)
	if cloudNetState.role ~= "follower" then
		return false
	end
	if not relayIsConnected() then
		return false
	end

	local now = love.timer.getTime()
	if (now - cloudNetState.lastSnapshotRequestAt) < cloudNetState.requestCooldown then
		return false
	end

	local packet = string.format("CLOUD_REQUEST|%s", formatNetFloat(now))
	local ok = pcall(function()
		relayServer:send(packet)
	end)
	if ok then
		cloudNetState.lastSnapshotRequestAt = now
		if reason and reason ~= "" then
			logger.log("Requested cloud snapshot (" .. reason .. ").")
		end
	end
	return ok
end

local function buildCloudSnapshotPacket(now)
	local parts = {
		"CLOUD_SNAPSHOT",
		formatNetFloat(now),
		formatNetFloat(windState.angle),
		formatNetFloat(windState.speed),
		tostring(#cloudState.groups),
		formatNetFloat(cloudState.minAltitude),
		formatNetFloat(cloudState.maxAltitude),
		formatNetFloat(cloudState.spawnRadius),
		formatNetFloat(cloudState.despawnRadius),
		formatNetFloat(cloudState.minGroupSize),
		formatNetFloat(cloudState.maxGroupSize),
		tostring(math.floor(cloudState.minPuffs)),
		tostring(math.floor(cloudState.maxPuffs))
	}

	for _, group in ipairs(cloudState.groups) do
		parts[#parts + 1] = table.concat({
			formatNetFloat(group.center[1]),
			formatNetFloat(group.center[2]),
			formatNetFloat(group.center[3]),
			formatNetFloat(group.radius),
			formatNetFloat(group.windDrift)
		}, ",")
	end

	return table.concat(parts, "|")
end

local function sendCloudSnapshot(forceSend, reason)
	if cloudNetState.role ~= "authority" then
		return false
	end
	if not relayIsConnected() then
		return false
	end

	local now = love.timer.getTime()
	if (not forceSend) and (now - cloudNetState.lastSnapshotSentAt) < cloudNetState.snapshotInterval then
		return false
	end

	local packet = buildCloudSnapshotPacket(now)
	local ok = pcall(function()
		relayServer:send(packet)
	end)
	if ok then
		cloudNetState.lastSnapshotSentAt = now
		if forceSend and reason and reason ~= "" then
			logger.log("Sent cloud snapshot (" .. reason .. ").")
		end
	end
	return ok
end

function requestGroundSnapshot(reason)
	if cloudNetState.role ~= "follower" then
		return false
	end
	if not relayIsConnected() then
		return false
	end

	local now = love.timer.getTime()
	if (now - groundNetState.lastSnapshotRequestAt) < groundNetState.requestCooldown then
		return false
	end

	local packet = string.format("GROUND_REQUEST|%s", formatNetFloat(now))
	local ok = pcall(function()
		relayServer:send(packet)
	end)
	if ok then
		groundNetState.lastSnapshotRequestAt = now
		if reason and reason ~= "" then
			logger.log("Requested ground snapshot (" .. reason .. ").")
		end
	end
	return ok
end

function buildGroundSnapshotPacket(now)
	local params = activeGroundParams or terrainSdfSystem.normalizeGroundParams(defaultGroundParams, terrainSdfDefaults)
	local parts = {
		"GROUND_SNAPSHOT2",
		"ts=" .. formatNetFloat(now),
		"seed=" .. tostring(params.seed),
		"chunkSize=" .. formatNetFloat(params.chunkSize or 64),
		"lod0=" .. tostring(params.lod0Radius or 2),
		"lod1=" .. tostring(params.lod1Radius or 4),
		"lod0Cell=" .. formatNetFloat(params.lod0CellSize or 4),
		"lod1Cell=" .. formatNetFloat(params.lod1CellSize or 8),
		"meshBudget=" .. tostring(params.meshBuildBudget or 2),
		"inflight=" .. tostring(params.workerMaxInflight or 2),
		"cache=" .. tostring(params.chunkCacheLimit or 768),
		"surfaceOnly=" .. tostring((params.surfaceOnlyMeshing ~= false) and 1 or 0),
		"threaded=" .. tostring((params.threadedMeshing ~= false) and 1 or 0),
		"minY=" .. formatNetFloat(params.minY or -180),
		"maxY=" .. formatNetFloat(params.maxY or 320),
		"baseHeight=" .. formatNetFloat(params.baseHeight or 0),
		"heightAmp=" .. formatNetFloat(params.heightAmplitude or 120),
		"heightFreq=" .. formatNetFloat(params.heightFrequency or 0.0018),
		"heightOct=" .. tostring(params.heightOctaves or 5),
		"heightLac=" .. formatNetFloat(params.heightLacunarity or 2.05),
		"heightGain=" .. formatNetFloat(params.heightGain or 0.52),
		"detailAmp=" .. formatNetFloat(params.surfaceDetailAmplitude or 14),
		"detailFreq=" .. formatNetFloat(params.surfaceDetailFrequency or 0.013),
		"waterLevel=" .. formatNetFloat(params.waterLevel or -12),
		"caveEnabled=" .. tostring((params.caveEnabled ~= false) and 1 or 0),
		"caveFreq=" .. formatNetFloat(params.caveFrequency or 0.018),
		"caveThr=" .. formatNetFloat(params.caveThreshold or 0.68),
		"caveStr=" .. formatNetFloat(params.caveStrength or 42),
		"caveMinY=" .. formatNetFloat(params.caveMinY or -120),
		"caveMaxY=" .. formatNetFloat(params.caveMaxY or 220),
		"tunnelCount=" .. tostring(params.tunnelCount or 12),
		"tunnelRMin=" .. formatNetFloat(params.tunnelRadiusMin or 9),
		"tunnelRMax=" .. formatNetFloat(params.tunnelRadiusMax or 18),
		"tunnelLenMin=" .. formatNetFloat(params.tunnelLengthMin or 240),
		"tunnelLenMax=" .. formatNetFloat(params.tunnelLengthMax or 520),
		"genVer=" .. tostring(params.generatorVersion or 1),
		"craterLimit=" .. tostring(params.craterHistoryLimit or 64),
		"craters=" .. encodeCraterListToken(params.dynamicCraters),
		"waterRatio=" .. formatNetFloat(params.waterRatio or defaultGroundParams.waterRatio),
		"grassColor=" .. encodeColor3Token(params.grassColor),
		"roadColor=" .. encodeColor3Token(params.roadColor),
		"fieldColor=" .. encodeColor3Token(params.fieldColor),
		"waterColor=" .. encodeColor3Token(params.waterColor),
		"grassVar=" .. encodeColor3Token(params.grassVar),
		"roadVar=" .. encodeColor3Token(params.roadVar),
		"fieldVar=" .. encodeColor3Token(params.fieldVar),
		"waterVar=" .. encodeColor3Token(params.waterVar)
	}
	return table.concat(parts, "|")
end

function sendGroundSnapshot(reason)
	if cloudNetState.role ~= "authority" then
		return false
	end
	if not relayIsConnected() then
		return false
	end

	local packet = buildGroundSnapshotPacket(love.timer.getTime())
	local ok = pcall(function()
		relayServer:send(packet)
	end)
	if ok and reason and reason ~= "" then
		logger.log("Sent ground snapshot (" .. reason .. ").")
	end
	return ok
end

function applyGroundSnapshotParts(parts, senderId)
	if #parts < 21 then
		return false
	end

	local hasWaterFields = #parts >= 24
	local waterRatio = hasWaterFields and tonumber(parts[21]) or defaultGroundParams.waterRatio
	local waterColor = hasWaterFields and decodeColor3Token(parts[22], defaultGroundParams.waterColor) or
		groundSystem.cloneColor3(defaultGroundParams.waterColor)
	local waterVar = hasWaterFields and decodeColor3Token(parts[23], defaultGroundParams.waterVar) or
		groundSystem.cloneColor3(defaultGroundParams.waterVar)

	local params = groundSystem.normalizeGroundParams({
		seed = tonumber(parts[3]),
		tileSize = tonumber(parts[4]),
		gridCount = tonumber(parts[5]),
		baseHeight = tonumber(parts[6]),
		tileThickness = tonumber(parts[7]),
		curvature = tonumber(parts[8]) or defaultGroundParams.curvature,
		recenterStep = tonumber(parts[9]) or defaultGroundParams.recenterStep,
		roadCount = tonumber(parts[10]),
		roadDensity = tonumber(parts[11]),
		fieldCount = tonumber(parts[12]),
		fieldMinSize = tonumber(parts[13]),
		fieldMaxSize = tonumber(parts[14]),
		grassColor = decodeColor3Token(parts[15], defaultGroundParams.grassColor),
		roadColor = decodeColor3Token(parts[16], defaultGroundParams.roadColor),
		fieldColor = decodeColor3Token(parts[17], defaultGroundParams.fieldColor),
		grassVar = decodeColor3Token(parts[18], defaultGroundParams.grassVar),
		roadVar = decodeColor3Token(parts[19], defaultGroundParams.roadVar),
		fieldVar = decodeColor3Token(parts[20], defaultGroundParams.fieldVar),
		waterRatio = waterRatio,
		waterColor = waterColor,
		waterVar = waterVar
	}, defaultGroundParams)

	rebuildGroundFromParams(params, "network peer " .. tostring(senderId))
	groundNetState.authorityPeerId = senderId
	groundNetState.lastSnapshotReceivedAt = love.timer.getTime()
	return true
end

local function applyGroundSnapshot2Parts(parts, senderId, idFieldIndex)
	local kv = parsePacketKeyValues(parts, 2)
	if type(kv) ~= "table" then
		return false
	end

	local params = terrainSdfSystem.normalizeGroundParams({
		seed = tonumber(kv.seed),
		chunkSize = tonumber(kv.chunkSize),
		lod0Radius = tonumber(kv.lod0),
		lod1Radius = tonumber(kv.lod1),
		lod0CellSize = tonumber(kv.lod0Cell),
		lod1CellSize = tonumber(kv.lod1Cell),
		meshBuildBudget = tonumber(kv.meshBudget),
		workerMaxInflight = tonumber(kv.inflight),
		chunkCacheLimit = tonumber(kv.cache),
		surfaceOnlyMeshing = (kv.surfaceOnly ~= nil) and (tonumber(kv.surfaceOnly) ~= 0) or nil,
		threadedMeshing = (kv.threaded ~= nil) and (tonumber(kv.threaded) ~= 0) or nil,
		minY = tonumber(kv.minY),
		maxY = tonumber(kv.maxY),
		baseHeight = tonumber(kv.baseHeight),
		heightAmplitude = tonumber(kv.heightAmp),
		heightFrequency = tonumber(kv.heightFreq),
		heightOctaves = tonumber(kv.heightOct),
		heightLacunarity = tonumber(kv.heightLac),
		heightGain = tonumber(kv.heightGain),
		surfaceDetailAmplitude = tonumber(kv.detailAmp),
		surfaceDetailFrequency = tonumber(kv.detailFreq),
		waterLevel = tonumber(kv.waterLevel),
		caveEnabled = (kv.caveEnabled ~= nil) and (tonumber(kv.caveEnabled) ~= 0) or nil,
		caveFrequency = tonumber(kv.caveFreq),
		caveThreshold = tonumber(kv.caveThr),
		caveStrength = tonumber(kv.caveStr),
		caveMinY = tonumber(kv.caveMinY),
		caveMaxY = tonumber(kv.caveMaxY),
		tunnelCount = tonumber(kv.tunnelCount),
		tunnelRadiusMin = tonumber(kv.tunnelRMin),
		tunnelRadiusMax = tonumber(kv.tunnelRMax),
		tunnelLengthMin = tonumber(kv.tunnelLenMin),
		tunnelLengthMax = tonumber(kv.tunnelLenMax),
		generatorVersion = tonumber(kv.genVer),
		craterHistoryLimit = tonumber(kv.craterLimit),
		dynamicCraters = decodeCraterListToken(kv.craters),
		waterRatio = tonumber(kv.waterRatio),
		grassColor = decodeColor3Token(kv.grassColor, defaultGroundParams.grassColor),
		roadColor = decodeColor3Token(kv.roadColor, defaultGroundParams.roadColor),
		fieldColor = decodeColor3Token(kv.fieldColor, defaultGroundParams.fieldColor),
		waterColor = decodeColor3Token(kv.waterColor, defaultGroundParams.waterColor),
		grassVar = decodeColor3Token(kv.grassVar, defaultGroundParams.grassVar),
		roadVar = decodeColor3Token(kv.roadVar, defaultGroundParams.roadVar),
		fieldVar = decodeColor3Token(kv.fieldVar, defaultGroundParams.fieldVar),
		waterVar = decodeColor3Token(kv.waterVar, defaultGroundParams.waterVar)
	}, terrainSdfDefaults)

	rebuildGroundFromParams(params, "network peer " .. tostring(senderId))
	groundNetState.authorityPeerId = senderId
	groundNetState.lastSnapshotReceivedAt = love.timer.getTime()
	return true
end

function handleGroundSnapshotPacket(data)
	local parts = splitPacketFields(data)
	if parts[1] ~= "GROUND_SNAPSHOT" and parts[1] ~= "GROUND_SNAPSHOT2" then
		return false
	end

	local senderId, idFieldIndex = readTrailingPacketInteger(parts, 2)
	if not senderId then
		return false
	end

	if cloudNetState.role == "authority" then
		return false
	end

	if cloudNetState.role == "follower" and
		groundNetState.authorityPeerId and
		groundNetState.authorityPeerId ~= senderId and
		groundNetState.lastSnapshotReceivedAt > -1e30 then
		return false
	end

	if cloudNetState.role == "pending" then
		cloudNetState.sawPeerOnJoin = true
	end
	if parts[1] == "GROUND_SNAPSHOT2" then
		return applyGroundSnapshot2Parts(parts, senderId, idFieldIndex)
	end
	return applyGroundSnapshotParts(parts, senderId)
end

function handleGroundRequestPacket(data)
	local parts = splitPacketFields(data)
	if parts[1] ~= "GROUND_REQUEST" then
		return false
	end

	local senderId = readTrailingPacketInteger(parts, 2)
	if not senderId then
		return false
	end

	if cloudNetState.role == "pending" then
		cloudNetState.sawPeerOnJoin = true
	end
	if cloudNetState.role == "authority" then
		sendGroundSnapshot("request from peer " .. tostring(senderId))
	end
	return true
end

local function applyCloudSnapshotParts(parts, senderId)
	if #parts < 15 then
		return false
	end

	local snapshotGroupCount = math.max(1, math.floor(tonumber(parts[5]) or cloudState.groupCount))
	local expectedFields = 13 + snapshotGroupCount + 1
	if #parts < expectedFields then
		return false
	end

	local snapshotMinAltitude = tonumber(parts[6]) or cloudState.minAltitude
	local snapshotMaxAltitude = tonumber(parts[7]) or cloudState.maxAltitude
	local snapshotSpawnRadius = tonumber(parts[8]) or cloudState.spawnRadius
	local snapshotDespawnRadius = tonumber(parts[9]) or cloudState.despawnRadius
	local snapshotMinGroupSize = tonumber(parts[10]) or cloudState.minGroupSize
	local snapshotMaxGroupSize = tonumber(parts[11]) or cloudState.maxGroupSize
	local snapshotMinPuffs = math.max(1, math.floor(tonumber(parts[12]) or cloudState.minPuffs))
	local snapshotMaxPuffs = math.max(snapshotMinPuffs, math.floor(tonumber(parts[13]) or cloudState.maxPuffs))
	local snapshotWindAngle = tonumber(parts[3])
	local snapshotWindSpeed = tonumber(parts[4])

	cloudState.groupCount = snapshotGroupCount
	cloudState.minAltitude = math.min(snapshotMinAltitude, snapshotMaxAltitude)
	cloudState.maxAltitude = math.max(snapshotMinAltitude, snapshotMaxAltitude)
	cloudState.spawnRadius = math.max(10, snapshotSpawnRadius)
	cloudState.despawnRadius = math.max(cloudState.spawnRadius + 10, snapshotDespawnRadius)
	cloudState.minGroupSize = math.max(1, math.min(snapshotMinGroupSize, snapshotMaxGroupSize))
	cloudState.maxGroupSize = math.max(cloudState.minGroupSize, snapshotMaxGroupSize)
	cloudState.minPuffs = snapshotMinPuffs
	cloudState.maxPuffs = snapshotMaxPuffs
	syncChunkingWithDrawDistance(false)

	if snapshotWindAngle then
		windState.angle = viewMath.wrapAngle(snapshotWindAngle)
	end
	if snapshotWindSpeed then
		windState.speed = math.max(0, snapshotWindSpeed)
	end
	windState.targetAngle = windState.angle
	windState.targetSpeed = windState.speed
	windState.retargetIn = math.huge

	local now = love.timer.getTime()
	if #cloudState.groups ~= snapshotGroupCount then
		cloudSim.spawnCloudField(cloudState, objects, camera, windState, cloudPuffModel, q, snapshotGroupCount, now)
	end

	for i = 1, snapshotGroupCount do
		local token = parts[13 + i]
		local gx, gy, gz, gr, gd = token and token:match("^([^,]+),([^,]+),([^,]+),([^,]+),([^,]+)$")
		local group = cloudState.groups[i]
		if group and gx and gy and gz and gr and gd then
			group.center[1] = tonumber(gx) or group.center[1]
			group.center[2] = tonumber(gy) or group.center[2]
			group.center[3] = tonumber(gz) or group.center[3]
			group.radius = math.max(1, tonumber(gr) or group.radius)
			group.windDrift = tonumber(gd) or group.windDrift
			cloudSim.updateCloudGroupVisuals(group, now)
		end
	end

	cloudNetState.authorityPeerId = senderId
	cloudNetState.lastSnapshotReceivedAt = now
	return true
end

local function handleCloudSnapshotPacket(data)
	local parts = splitPacketFields(data)
	if parts[1] ~= "CLOUD_SNAPSHOT" then
		return false
	end

	local senderId = readTrailingPacketInteger(parts, 2)
	if not senderId then
		return false
	end

	if cloudNetState.role == "authority" then
		return false
	end

	if cloudNetState.role == "follower" and cloudNetState.authorityPeerId and cloudNetState.authorityPeerId ~= senderId then
		return false
	end

	if cloudNetState.role ~= "follower" then
		setCloudRole("follower", "cloud snapshot received")
	end
	cloudNetState.sawPeerOnJoin = true
	return applyCloudSnapshotParts(parts, senderId)
end

local function handleCloudRequestPacket(data)
	local parts = splitPacketFields(data)
	if parts[1] ~= "CLOUD_REQUEST" then
		return false
	end

	local senderId = readTrailingPacketInteger(parts, 2)
	if not senderId then
		return false
	end

	if cloudNetState.role == "pending" then
		cloudNetState.sawPeerOnJoin = true
	end

	if cloudNetState.role == "authority" then
		sendCloudSnapshot(true, "request from peer " .. tostring(senderId))
	end
	return true
end

local function notePeerStateReceived(peerId)
	if not peerId then
		return
	end
	if cloudNetState.role == "standalone" and relayIsConnected() then
		beginCloudRoleElection("peer state observed")
	end

	if cloudNetState.role == "pending" then
		cloudNetState.sawPeerOnJoin = true
		cloudNetState.authorityPeerId = cloudNetState.authorityPeerId or peerId
		setCloudRole("follower", "peer state received during join")
		requestCloudSnapshot("joining populated session")
		requestGroundSnapshot("joining populated session")
	elseif cloudNetState.role == "follower" and not cloudNetState.authorityPeerId then
		cloudNetState.authorityPeerId = peerId
	end
end

local function updateCloudNetworkState(now)
	if not relayIsConnected() then
		if cloudNetState.role ~= "standalone" then
			setCloudRole("standalone", "relay unavailable")
		end
		cloudNetState.authorityPeerId = nil
		groundNetState.authorityPeerId = nil
		groundNetState.lastSnapshotReceivedAt = -math.huge
		groundNetState.lastSnapshotRequestAt = -math.huge
		return
	end

	if cloudNetState.role == "standalone" then
		beginCloudRoleElection("relay connected")
	end

	if cloudNetState.role == "pending" then
		if cloudNetState.sawPeerOnJoin then
			setCloudRole("follower", "peer detected during join window")
			requestCloudSnapshot("peer already present")
			requestGroundSnapshot("peer already present")
		elseif (now - cloudNetState.connectedAt) >= cloudNetState.decisionDelay then
			cloudNetState.authorityPeerId = nil
			cloudNetState.lastSnapshotSentAt = -math.huge
			setCloudRole("authority", "no peers detected at connect")
			sendGroundSnapshot("authority elected")
		end
		return
	end

	if cloudNetState.role == "follower" and (now - cloudNetState.lastSnapshotReceivedAt) >= cloudNetState.staleTimeout then
		requestCloudSnapshot("snapshot stale")
	end

	if cloudNetState.role == "follower" and groundNetState.lastSnapshotReceivedAt <= -1e30 then
		requestGroundSnapshot("awaiting world params")
	end
end

local function getRelayStatus()
	if not relay then
		return "Host down"
	end
	if not relayServer then
		return "Disconnected"
	end

	local ok, state = pcall(function()
		return relayServer:state()
	end)
	if not ok then
		return "Unknown"
	end
	if state == "connected" then
		return "Connected"
	end
	return tostring(state)
end

local function reconnectRelay()
	if not relay then
		relay = enet.host_create()
		if not relay then
			logger.log("ENet host creation failed; reconnect unavailable.")
			return false
		end
	end

	if relayServer then
		pcall(function()
			relayServer:disconnect_now()
		end)
		relayServer = nil
	end

	clearPeerObjects()
	cloudNetState.authorityPeerId = nil
	groundNetState.authorityPeerId = nil
	groundNetState.lastSnapshotReceivedAt = -math.huge
	groundNetState.lastSnapshotRequestAt = -math.huge
	setCloudRole("standalone", "relay reconnecting")

	local ok, peerOrErr = pcall(function()
		return relay:connect(hostAddy)
	end)
	if not ok then
		logger.log("Relay reconnect failed: " .. tostring(peerOrErr))
		return false
	end

	relayServer = peerOrErr
	if relayServer then
		logger.log("Reconnecting to relay server: " .. hostAddy)
		forceStateSync()
		return true
	end

	logger.log("Relay reconnect failed: no peer handle returned.")
	return false
end


-- Pause/graphics menu orchestration lives in a dedicated module so main.lua stays focused on
-- world setup and frame-loop responsibilities.
local pauseMenuSystem = requireContract("Source.Ui.PauseMenuSystem", { "create" })

-- Module wiring: bridge pause-menu code to live main.lua state without turning those locals global.
local pauseExports = pauseMenuSystem.create({
	values = {
		love = love,
		q = q,
		viewMath = viewMath,
		controls = controls,
		renderer = renderer,
		logger = logger,
		resetHudCaches = resetHudCaches,
		mapState = mapState,
		viewState = viewState,
		graphicsSettings = graphicsSettings,
		graphicsBackend = graphicsBackend,
		hudSettings = hudSettings,
		audioSettings = audioSettings,
		sunSettings = sunSettings,
		characterOrientation = characterOrientation,
		characterPreview = characterPreview,
		modelLoadPrompt = modelLoadPrompt,
		playerModelCache = playerModelCache,
		modelTransferState = modelTransferState,
		cloudNetState = cloudNetState,
		groundNetState = groundNetState,
		defaultGroundParams = defaultGroundParams,
		flightModelConfig = flightModelConfig,
		terrainSdfDefaults = terrainSdfDefaults,
		lightingModel = lightingModel,
		defaultPlayerModelScale = defaultPlayerModelScale,
		defaultWalkingModelScale = defaultWalkingModelScale,
		windState = windState,
		cloudState = cloudState,
		objects = objects,
		cubeModel = cubeModel,
		cloudPuffModel = cloudPuffModel,
		RESTART_STATE_VERSION = RESTART_STATE_VERSION,
		RESTART_MODEL_ENCODED_LIMIT = RESTART_MODEL_ENCODED_LIMIT,
		clamp = clamp,
		decodeModelBytes = decodeModelBytes,
		loadPlayerModelFromRaw = loadPlayerModelFromRaw,
		setActivePlayerModel = setActivePlayerModel,
		onRoleOrientationChanged = onRoleOrientationChanged,
		setMouseCapture = setMouseCapture,
		ensureFlightSpawnSafety = ensureFlightSpawnSafety,
		setFlightMode = setFlightMode,
		resetCameraTransform = resetCameraTransform,
		resetAltLookState = resetAltLookState,
		resolveActiveRenderCamera = resolveActiveRenderCamera,
		reconnectRelay = reconnectRelay,
		setRendererPreference = setRendererPreference,
		applySunSettingsToRenderer = applySunSettingsToRenderer,
		syncChunkingWithDrawDistance = syncChunkingWithDrawDistance,
		getConfiguredModelForRole = getConfiguredModelForRole,
		getActiveModelRole = getActiveModelRole,
		syncActivePlayerModelState = syncActivePlayerModelState,
		composeRoleOrientationOffset = composeRoleOrientationOffset,
		normalizeRoleId = normalizeRoleId,
		getRoleOrientation = getRoleOrientation,
		loadPlayerModelFromPath = loadPlayerModelFromPath,
		syncLocalPlayerObject = syncLocalPlayerObject,
		invalidateMapCache = invalidateMapCache,
		getRelayStatus = getRelayStatus,
		cloudSim = cloudSim
	},
	getters = {
		screen = function()
			return screen
		end,
		camera = function()
			return camera
		end,
		localCallsign = function()
			return localCallsign
		end,
		flightSimMode = function()
			return flightSimMode
		end,
		mouseSensitivity = function()
			return mouseSensitivity
		end,
		invertLookY = function()
			return invertLookY
		end,
		showCrosshair = function()
			return showCrosshair
		end,
		showDebugOverlay = function()
			return showDebugOverlay
		end,
		colorCorruptionMode = function()
			return colorCorruptionMode
		end,
		useGpuRenderer = function()
			return useGpuRenderer
		end,
		pendingRestartRendererPreference = function()
			return pendingRestartRendererPreference
		end,
		currentGraphicsApiPreference = function()
			return currentGraphicsApiPreference
		end,
		frameImage = function()
			return frameImage
		end,
		frameImageW = function()
			return frameImageW
		end,
		frameImageH = function()
			return frameImageH
		end,
		worldRenderCanvas = function()
			return worldRenderCanvas
		end,
		worldRenderW = function()
			return worldRenderW
		end,
		worldRenderH = function()
			return worldRenderH
		end,
		zBuffer = function()
			return zBuffer
		end,
		activeGroundParams = function()
			return activeGroundParams
		end,
		localPlayerObject = function()
			return localPlayerObject
		end,
		modelLoadTargetRole = function()
			return modelLoadTargetRole
		end,
		updateGroundStreaming = function()
			return updateGroundStreaming
		end,
		rebuildGroundFromParams = function()
			return rebuildGroundFromParams
		end,
		forceStateSync = function()
			return forceStateSync
		end,
		playerPlaneModelHash = function()
			return playerPlaneModelHash
		end,
		playerPlaneModelScale = function()
			return playerPlaneModelScale
		end,
		playerWalkingModelHash = function()
			return playerWalkingModelHash
		end,
		playerWalkingModelScale = function()
			return playerWalkingModelScale
		end,
		pauseTitleFont = function()
			return pauseTitleFont
		end,
		pauseItemFont = function()
			return pauseItemFont
		end
	},
	setters = {
		localCallsign = function(value)
			localCallsign = value
		end,
		flightSimMode = function(value)
			flightSimMode = value
		end,
		mouseSensitivity = function(value)
			mouseSensitivity = value
		end,
		invertLookY = function(value)
			invertLookY = value
		end,
		showCrosshair = function(value)
			showCrosshair = value
		end,
		showDebugOverlay = function(value)
			showDebugOverlay = value
		end,
		colorCorruptionMode = function(value)
			colorCorruptionMode = value
		end,
		useGpuRenderer = function(value)
			useGpuRenderer = value
		end,
		pendingRestartRendererPreference = function(value)
			pendingRestartRendererPreference = value
		end,
		currentGraphicsApiPreference = function(value)
			currentGraphicsApiPreference = value
		end,
		frameImage = function(value)
			frameImage = value
		end,
		frameImageW = function(value)
			frameImageW = value
		end,
		frameImageH = function(value)
			frameImageH = value
		end,
		worldRenderCanvas = function(value)
			worldRenderCanvas = value
		end,
		worldRenderW = function(value)
			worldRenderW = value
		end,
		worldRenderH = function(value)
			worldRenderH = value
		end,
		zBuffer = function(value)
			zBuffer = value
		end,
		modelLoadTargetRole = function(value)
			modelLoadTargetRole = value
		end,
		playerPlaneModelScale = function(value)
			playerPlaneModelScale = value
		end,
		playerWalkingModelScale = function(value)
			playerWalkingModelScale = value
		end,
		pauseTitleFont = function(value)
			pauseTitleFont = value
		end,
		pauseItemFont = function(value)
			pauseItemFont = value
		end
	}
})

local function assertExports(moduleName, exportTable, requiredKeys)
	for _, key in ipairs(requiredKeys or {}) do
		if exportTable[key] == nil then
			error(string.format("%s missing required export '%s'.", moduleName, tostring(key)), 2)
		end
	end
end

assertExports("PauseMenuSystem", pauseExports, {
	"pauseMenu",
	"refreshUiFonts",
	"setPauseStatus",
	"getPauseItemsForCurrentPage",
	"normalizeGraphicsApiPreference",
	"getGraphicsApiPreferenceLabel",
	"getGraphicsBackendLabel",
	"getEngineRestartPayload",
	"deepCopyPrimitiveTable",
	"buildRestartModelSnapshot",
	"buildRestartSnapshot",
	"restoreModelFromRestartSnapshot",
	"applyRestartSnapshot",
	"requestGraphicsApiRestart",
	"syncGraphicsSettingsFromWindow",
	"resetControlBindingsToDefaults",
	"cyclePauseTab",
	"cyclePauseSubTab",
	"moveControlsSelection",
	"moveControlsSlot",
	"clearSelectedControlBinding",
	"beginSelectedControlBindingCapture",
	"cancelControlBindingCapture",
	"captureBindingFromKey",
	"captureBindingFromMouseButton",
	"captureBindingFromWheel",
	"captureBindingFromMouseMotion",
	"clearPauseConfirm",
	"clearPauseRangeDrag",
	"movePauseSelection",
	"adjustSelectedPauseItem",
	"adjustSelectedPauseItemCoarse",
	"activateSelectedPauseItem",
	"updatePauseMenuHover",
	"updatePauseControlsHover",
	"updatePauseRangeDrag",
	"endPauseRangeDrag",
	"handlePauseMenuMouseClick",
	"setPauseState",
	"drawPauseMenu",
	"getControlActionsCount",
	"refreshGraphicsBackendInfo"
})

local pauseMenu = pauseExports.pauseMenu
local refreshUiFonts = pauseExports.refreshUiFonts
local setPauseStatus = pauseExports.setPauseStatus
local getPauseItemsForCurrentPage = pauseExports.getPauseItemsForCurrentPage
normalizeGraphicsApiPreference = pauseExports.normalizeGraphicsApiPreference
getGraphicsApiPreferenceLabel = pauseExports.getGraphicsApiPreferenceLabel
local getGraphicsBackendLabel = pauseExports.getGraphicsBackendLabel
getEngineRestartPayload = pauseExports.getEngineRestartPayload
deepCopyPrimitiveTable = pauseExports.deepCopyPrimitiveTable
buildRestartModelSnapshot = pauseExports.buildRestartModelSnapshot
buildRestartSnapshot = pauseExports.buildRestartSnapshot
restoreModelFromRestartSnapshot = pauseExports.restoreModelFromRestartSnapshot
applyRestartSnapshot = pauseExports.applyRestartSnapshot
requestGraphicsApiRestart = pauseExports.requestGraphicsApiRestart

local syncGraphicsSettingsFromWindow = pauseExports.syncGraphicsSettingsFromWindow
local resetControlBindingsToDefaults = pauseExports.resetControlBindingsToDefaults
local cyclePauseTab = pauseExports.cyclePauseTab
local cyclePauseSubTab = pauseExports.cyclePauseSubTab
local moveControlsSelection = pauseExports.moveControlsSelection
local moveControlsSlot = pauseExports.moveControlsSlot
local clearSelectedControlBinding = pauseExports.clearSelectedControlBinding
local beginSelectedControlBindingCapture = pauseExports.beginSelectedControlBindingCapture
local cancelControlBindingCapture = pauseExports.cancelControlBindingCapture
local captureBindingFromKey = pauseExports.captureBindingFromKey
local captureBindingFromMouseButton = pauseExports.captureBindingFromMouseButton
local captureBindingFromWheel = pauseExports.captureBindingFromWheel
local captureBindingFromMouseMotion = pauseExports.captureBindingFromMouseMotion
local clearPauseConfirm = pauseExports.clearPauseConfirm
local clearPauseRangeDrag = pauseExports.clearPauseRangeDrag
local movePauseSelection = pauseExports.movePauseSelection
local adjustSelectedPauseItem = pauseExports.adjustSelectedPauseItem
local adjustSelectedPauseItemCoarse = pauseExports.adjustSelectedPauseItemCoarse
local activateSelectedPauseItem = pauseExports.activateSelectedPauseItem
local updatePauseMenuHover = pauseExports.updatePauseMenuHover
local updatePauseControlsHover = pauseExports.updatePauseControlsHover
local updatePauseRangeDrag = pauseExports.updatePauseRangeDrag
local endPauseRangeDrag = pauseExports.endPauseRangeDrag
local handlePauseMenuMouseClick = pauseExports.handlePauseMenuMouseClick
local setPauseState = pauseExports.setPauseState
local drawPauseMenu = pauseExports.drawPauseMenu
local getPauseControlActionsCount = pauseExports.getControlActionsCount
local refreshGraphicsBackendInfo = pauseExports.refreshGraphicsBackendInfo

makeRng = groundSystem.makeRng
generateCellGrid = groundSystem.generateCellGrid
CELL_GRASS = groundSystem.CELL_GRASS
CELL_ROAD = groundSystem.CELL_ROAD
CELL_FIELD = groundSystem.CELL_FIELD

-- Helper function
-- Ensures that the projection call doesnt use a nil map param table, passes back the module's function called
local function sampleGroundHeightAtWorld(worldX, worldZ, params)
	local groundParams = params or activeGroundParams or defaultGroundParams
	if terrainState then
		return terrainSdfSystem.sampleGroundHeightAtWorld(worldX, worldZ, terrainState) or 0
	end
	return terrainSdfSystem.sampleGroundHeightAtWorld(worldX, worldZ, groundParams) or 0
end

function updateGroundStreaming(forceRebuild)
	local changed, nextTerrainState = terrainSdfSystem.updateGroundStreaming(forceRebuild, {
		terrainState = terrainState,
		activeGroundParams = activeGroundParams,
		camera = camera,
		drawDistance = graphicsSettings and graphicsSettings.drawDistance,
		objects = objects,
		q = q
	})
	if nextTerrainState then
		terrainState = nextTerrainState
	end
	local params = activeGroundParams or defaultGroundParams
	local halfExtent = (params.chunkSize or 64) * ((params.lod1Radius or 4) + 1)
	if halfExtent > 0 and math.abs((worldHalfExtent or 0) - halfExtent) > 1e-6 then
		worldHalfExtent = halfExtent
		mapState.zoomExtents = { 160, 420, math.max(worldHalfExtent, 420) }
		mapState.logicalCamera = nil
		invalidateMapCache()
	end
	return changed
end

rebuildGroundFromParams = function(params, reason)
	local changed, nextState = terrainSdfSystem.rebuildGroundFromParams(params, reason, {
		defaultGroundParams = defaultGroundParams,
		activeGroundParams = activeGroundParams,
		terrainState = terrainState,
		camera = camera,
		drawDistance = graphicsSettings and graphicsSettings.drawDistance,
		objects = objects,
		localPlayerObject = localPlayerObject,
		mapState = mapState,
		q = q,
		log = logger.log
	})
	if changed and nextState then
		activeGroundParams = nextState.activeGroundParams
		terrainState = nextState.terrainState or terrainState
		groundObject = nextState.groundObject
		worldHalfExtent = nextState.worldHalfExtent or worldHalfExtent
		invalidateMapCache()
	end
	return changed
end

function getTerrainStreamingStats()
	local state = terrainState or {}
	local loaded = 0
	for _ in pairs(state.chunkMap or {}) do
		loaded = loaded + 1
	end
	local queued = #(state.chunkOrder or {})
	local pendingBuild = 0
	for _ in pairs(state.buildQueue or {}) do
		pendingBuild = pendingBuild + 1
	end
	local inflight = 0
	for _ in pairs(state.workerInflight or {}) do
		inflight = inflight + 1
	end
	return {
		loaded = loaded,
		queued = queued,
		pendingBuild = pendingBuild,
		inflight = inflight,
		pendingTotal = queued + pendingBuild + inflight
	}
end

function preGenerateTerrainChunks(maxSeconds, minCoverage)
	local maxWait = math.max(0.1, tonumber(maxSeconds) or 2.5)
	local coverage = clamp(tonumber(minCoverage) or 0.9, 0.2, 1.0)
	local params = activeGroundParams or defaultGroundParams or {}
	local lod1 = math.max(1, math.floor(tonumber(params.lod1Radius) or 2))
	local expected = math.max(1, (lod1 * 2 + 1) * (lod1 * 2 + 1))
	local targetReady = math.max(1, math.floor(expected * coverage + 0.5))
	local startTime = love.timer.getTime()
	local lastUiUpdate = -math.huge

	while true do
		updateGroundStreaming(false)
		local stats = getTerrainStreamingStats()
		local elapsed = love.timer.getTime() - startTime
		local targetReached = stats.loaded >= targetReady
		if (targetReached and stats.pendingTotal <= 0) or elapsed >= maxWait then
			logger.log(string.format(
				"Terrain pre-generation complete: loaded=%d target=%d pending=%d elapsed=%.2fs",
				stats.loaded,
				targetReady,
				stats.pendingTotal,
				elapsed
			))
			break
		end

		if startupUi and startupUi.active and (elapsed - lastUiUpdate) >= 0.05 then
			local phase = clamp(elapsed / maxWait, 0, 1)
			local progress = 0.57 + phase * 0.06
			setStartupUiStage(
				"Generating Terrain",
				progress,
				string.format("Pre-generating chunks %d/%d", math.min(stats.loaded, targetReady), targetReady)
			)
			lastUiUpdate = elapsed
		end

		if love.timer and love.timer.sleep then
			love.timer.sleep(0)
		end
	end
end

-- === Initial Configuration ===
function love.load()
	-- 80% screen size
	local width, height = love.window.getDesktopDimensions()
	width, height = math.floor(width * 0.8), math.floor(height * 0.8)

	love.window.setTitle("Don's 3D Engine")
	love.window.setIcon(love.image.newImageData("Assets/Icons/WindowIcon.png"))
	local modeOk, modeErr = love.window.setMode(width, height, { vsync = false, depth = true})
	if not modeOk then
		love.window.setMode(width, height, { vsync = false })
	end
	love.keyboard.setKeyRepeat(true)
	setMouseCapture(true)
	screen = {
		w = width,
		h = height
	}
	beginStartupUi()
	setStartupUiStage("Preparing Window", 0.04, "Applying display mode and input defaults.")

	logger.reset()
	logger.log("Booting Don's 3D Engine")
	logger.log("Log file: " .. logger.getPath())
	refreshGraphicsBackendInfo()
	logger.log(string.format(
		"Graphics API: %s | %s | %s | %s",
		graphicsBackend.name,
		(graphicsBackend.version ~= "" and graphicsBackend.version or "unknown-version"),
		(graphicsBackend.vendor ~= "" and graphicsBackend.vendor or "unknown-vendor"),
		(graphicsBackend.device ~= "" and graphicsBackend.device or "unknown-device")
	))
	bootRestartPayload = getEngineRestartPayload()
	bootRestartSnapshotState = nil
	if bootRestartPayload then
		currentGraphicsApiPreference = normalizeGraphicsApiPreference(bootRestartPayload.graphicsApiPreference)
		graphicsSettings.graphicsApiPreference = currentGraphicsApiPreference
		if type(bootRestartPayload.state) == "table" then
			bootRestartSnapshotState = bootRestartPayload.state
		end
		if tonumber(bootRestartPayload._l2d3d_restart_migrated_from) then
			logger.log(string.format(
				"Restart payload migrated from version %d.",
				tonumber(bootRestartPayload._l2d3d_restart_migrated_from)
			))
		end
		logger.log("Restart payload detected; applying preserved world/game state.")
	else
		currentGraphicsApiPreference = normalizeGraphicsApiPreference(graphicsSettings.graphicsApiPreference)
	end
	if not modeOk then
		logger.log("Depth buffer mode unavailable, fallback mode set. Detail: " .. tostring(modeErr))
	end
	setStartupUiStage("Loading Session", 0.14, "Restoring restart payload and runtime preferences.")

	screen = {
		w = width,
		h = height
	}
	refreshUiFonts()
	resetHudCaches()
	invalidateMapCache()
	localCallsign = detectDefaultCallsign()
	setStartupUiStage("Building World", 0.29, "Creating camera state, gameplay defaults, and UI caches.")

	camera = {
		pos = { 0, 0, -5 },
		rot = q.identity(),
		speed = 10,
		fov = math.rad(90),
		baseFov = math.rad(90),
		zoomFactor = 0.58,
		zoomLerpSpeed = 12,
		vel = { 0, 0, 0 }, -- current velocity
		onGround = false, -- contacting
		gravity = -9.80665, -- units/sec^2
		jumpSpeed = 5, -- initial jump
		throttle = 0,
		throttleAccel = 0.55,
		flightMass = tonumber(flightModelConfig.massKg) or 1150,
		flightThrustForce = tonumber(flightModelConfig.maxThrustSeaLevel) or 3200,
		flightThrustAccel = tonumber(flightModelConfig.maxThrustSeaLevel) or 3200,
		flightDragCoefficient = 0.018,
		flightAirBrakeDrag = 0.11,
		flightLiftCoefficient = 0.11,
		flightZeroLiftAngle = math.rad(2),
		flightMaxLiftAngle = math.rad(20),
		flightCamberLiftCoefficient = 0.014,
		flightStallSpeed = 8,
		flightFullLiftSpeed = 24,
		flightInducedDragCoefficient = 0.0075,
		flightGravity = -9.80665,
		flightGroundFriction = tonumber(flightModelConfig.groundFriction) or 0.92,
		flightGroundPitchHoldEnabled = true,
		flightGroundPitchHoldAngle = math.rad(-20),
		flightVel = { 0, 0, 0 },
		flightAngVel = { 0, 0, 0 }, -- body rates in local axes: +X right, +Y up, +Z forward
		flightRotVel = { pitch = 0, yaw = 0, roll = 0 },
		flightRotResponse = 6.0,
		yoke = { pitch = 0, yaw = 0, roll = 0 },
		yokeKeyboardRate = 2.8,
		yokeAutoCenterRate = 0.5,
		yokeMouseHoldDurationSec = 0.75,
		yokeMousePitchGain = 8,
		yokeMouseYawGain = 7,
		yokeMouseRollGain = 6,
		afterburnerMultiplier = 1.0,
		airBrakeStrength = 1.2,
		flightThrottleDamping = 5,
		wheelThrottleStep = 0.06,
		flightPitchRate = 78,
		flightYawRate = 62,
		flightRollRate = 95,
		flightMousePitchMultiplier = 1.9,
		flightMouseYawMultiplier = 3.5,
		flightMouseRollMultiplier = 1.2,
		sprintMultiplier = 1.8,
		walkMousePitchMultiplier = 2.4,
		walkMouseYawMultiplier = 2.8,
		allowWalkRoll = false,
		walkTiltRate = 40,
		box = {
			halfSize = { x = 0.5, y = 1.8, z = 0.5 }, -- width/height/depth half extents
			pos = { 0, 0, -5 },              -- center at camera
			isSolid = true
		}
	}
	syncWalkingLookFromRotation()
	localGroundClearance = (camera.box and camera.box.halfSize and camera.box.halfSize.y) or localGroundClearance
	viewState.mode = "first_person"
	viewState.activeCamera = camera
	resetAltLookState()
	mapState.visible = true
	mapState.mHeld = false
	mapState.mUsedForZoom = false
	mapState.zoomIndex = 2
	mapState.logicalCamera = nil
	cloudNetState.role = "standalone"
	cloudNetState.connectedAt = 0
	cloudNetState.sawPeerOnJoin = false
	cloudNetState.authorityPeerId = nil
	cloudNetState.lastSnapshotSentAt = -math.huge
	cloudNetState.lastSnapshotReceivedAt = -math.huge
	cloudNetState.lastSnapshotRequestAt = -math.huge
	groundNetState.authorityPeerId = nil
	groundNetState.lastSnapshotReceivedAt = -math.huge
	groundNetState.lastSnapshotRequestAt = -math.huge

	renderMode = "CPU"
	gpuErrorLogged = false
	perfElapsed, perfFrames, perfLoggedLowFps = 0, 0, false
	pauseMenu.active = false
	pauseMenu.tab = "main"
	pauseMenu.selected = 1
	pauseMenu.itemBounds = {}
	pauseMenu.tabBounds = {}
	pauseMenu.subTabBounds = {}
	pauseMenu.helpScroll = 0
	pauseMenu.controlsScroll = 0
	pauseMenu.controlsSelection = 1
	pauseMenu.controlsSlot = 1
	pauseMenu.controlsRowBounds = {}
	pauseMenu.controlsSlotBounds = {}
	pauseMenu.listeningBinding = nil
	clearPauseRangeDrag()
	pauseMenu.statusText = ""
	pauseMenu.statusUntil = 0
	pauseMenu.confirmItemId = nil
	pauseMenu.confirmUntil = 0
	modelLoadPrompt.active = false
	modelLoadPrompt.mode = "model_path"
	modelLoadPrompt.text = ""
	modelLoadPrompt.cursor = 0
	if love.keyboard and type(love.keyboard.setTextInput) == "function" then
		love.keyboard.setTextInput(false)
	end
	syncGraphicsSettingsFromWindow()
	graphicsSettings.graphicsApiPreference = normalizeGraphicsApiPreference(graphicsSettings.graphicsApiPreference)
	if renderer.setClipPlanes then
		renderer.setClipPlanes(0.1, graphicsSettings.drawDistance)
	end
	syncChunkingWithDrawDistance(false)
	setStartupUiStage("Loading Player Model", 0.45, "Loading default player model and syncing local avatar.")

	local loadedDefaultModel = loadPlayerModelFromPath(defaultPlayerModelPath, "plane")
	if not loadedDefaultModel then
		local fallback = playerModelCache["builtin-cube"]
		setActivePlayerModel(fallback, "builtin cube", "plane")
	end
	syncActivePlayerModelState()

	-- Local avatar is synced to player camera each update.
	localPlayerObject = {
		model = playerModel,
		pos = { camera.pos[1], camera.pos[2], camera.pos[3] },
		basePos = { camera.pos[1], camera.pos[2], camera.pos[3] },
		rot = composePlayerModelRotation(camera.rot, getActiveModelRole(), playerModelHash),
		scale = { playerModelScale, playerModelScale, playerModelScale },
		color = { 0.9, 0.4, 0.1 },
		isSolid = false,
		isLocalPlayer = true,
		modelHash = playerModelHash
	}
	applyPlaneVisualToObject(localPlayerObject, playerModelHash, playerModelScale)
	syncLocalPlayerObject()

	objects = {
		[1] = localPlayerObject
	}

	setStartupUiStage("Generating Terrain", 0.57, "Building procedural ground chunks.")
	rebuildGroundFromParams(defaultGroundParams, "startup")
	preGenerateTerrainChunks(2.8, 0.92)

	setStartupUiStage("Spawning Clouds", 0.66, "Generating cloud groups and initial wind profile.")
	windState.angle = randomRange(0, math.pi * 2)
	windState.speed = randomRange(5, 10)
	cloudSim.pickNextWindTarget(windState)
	cloudSim.spawnCloudField(cloudState, objects, camera, windState, cloudPuffModel, q, nil, love.timer.getTime())
	logger.log(string.format("Cloud field initialized (%d groups).", #cloudState.groups))
	setStartupUiStage("Audio Graph", 0.7, "Synthesizing procedural engine/atmosphere audio.")
	if startupUi.audioRuntime and type(startupUi.audioRuntime.stop) == "function" then
		startupUi.audioRuntime:stop()
	end
	startupUi.audioRuntime = require("Source.Audio.ProceduralAudio").create({
		sampleRate = 22050,
		bufferSamples = 1024,
		queueDepth = 8
	})
	if startupUi.audioRuntime and startupUi.audioRuntime.available then
		logger.log("Procedural audio initialized.")
	else
		logger.log("Procedural audio unavailable on this runtime.")
	end
	if bootRestartSnapshotState then
		if applyRestartSnapshot(bootRestartSnapshotState) then
			logger.log("Restart state restored.")
			preGenerateTerrainChunks(1.8, 0.88)
		else
			logger.log("Restart state restore failed; using default startup state.")
		end
	end
	if autoSmoke.enabled then
		setFlightMode(true)
		viewState.mode = "third_person"
		thirdPersonState.offset = { 0.0, 1.4, -3.2 }
		local smokeModelPath = os.getenv("L2D3D_AUTOSMOKE_MODEL")
		if type(smokeModelPath) ~= "string" or smokeModelPath == "" then
			smokeModelPath = defaultPlayerModelPath
		end
		local smokeLoadOk, smokeErr = loadPlayerModelFromPath(smokeModelPath, "plane")
		if smokeLoadOk then
			logger.log("AutoSmoke loaded model: " .. tostring(smokeModelPath))
		else
			logger.log("AutoSmoke model load failed: " .. tostring(smokeErr))
		end
		syncActivePlayerModelState()
		if localPlayerObject then
			applyPlaneVisualToObject(localPlayerObject, playerModelHash, playerModelScale)
			syncLocalPlayerObject()
		end
		logger.log("AutoSmoke enabled: flight mode ON, third-person camera ON.")
	end
	currentGraphicsApiPreference = normalizeGraphicsApiPreference(graphicsSettings.graphicsApiPreference)

	setStartupUiStage("Allocating Buffers", 0.74, "Preparing CPU render buffers.")
	zBuffer = {}
	for i = 1, screen.w * screen.h do
		zBuffer[i] = math.huge
	end

	setStartupUiStage("Connecting Relay", 0.82, "Establishing multiplayer relay connection.")
	if not relay then
		logger.log("ENet host creation failed; multiplayer disabled for this session.")
	elseif not relayServer then
		logger.log("Could not connect to relay server at " .. hostAddy)
	else
		logger.log("Connected to relay server: " .. hostAddy)
	end

	setStartupUiStage("Compiling Shaders", 0.88, "Creating GPU shader pipelines.")
	local defaultShaderOk, defaultShaderErr = engine.ensureDefaultShader()
	if defaultShaderOk then
		setStartupUiStage("Compiling Shaders", 0.9, "Compiling compatibility shader.")
	else
		logger.log("Compatibility shader compile failed: " .. tostring(defaultShaderErr))
		setStartupUiStage("Compiling Shaders", 0.9, "Compatibility shader failed; continuing.")
	end
	local gpuOk, gpuErr = renderer.init(screen, camera, logger.log, function(statusMessage, phaseProgress)
		local phase = clamp(tonumber(phaseProgress) or 0, 0, 1)
		local stagedProgress = 0.88 + phase * 0.09
		setStartupUiStage("Compiling Shaders", stagedProgress, statusMessage or "Compiling world shader")
	end)
	if gpuOk then
		renderMode = "GPU"
		logger.log("GPU renderer enabled.")
	else
		useGpuRenderer = false
		renderMode = "CPU"
		logger.log("GPU renderer failed, using CPU fallback: " .. tostring(gpuErr))
	end
	setStartupUiStage("Finalizing Startup", 0.98, gpuOk and "GPU renderer ready." or "Falling back to CPU renderer.")
	applySunSettingsToRenderer()
	if renderer.setClipPlanes then
		renderer.setClipPlanes(0.1, graphicsSettings.drawDistance)
	end
	if pendingRestartRendererPreference ~= nil then
		setRendererPreference(pendingRestartRendererPreference)
		pendingRestartRendererPreference = nil
	end

	resolveActiveRenderCamera()
	syncAppStateReferences()
	setStartupUiStage("Ready", 1.0, "Startup complete. Have fun.")
	finishStartupUi()
end

-- === Mouse Look ===
local function buildMovementInputState()
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
	local pitchLimit = walkingPitchLimit

	camera.walkPitch = clamp(
		camera.walkPitch + (pitchAxis * mouseSensitivity * pitchMult),
		-pitchLimit,
		pitchLimit
	)
	camera.walkYaw = viewMath.wrapAngle(camera.walkYaw + (yawAxis * mouseSensitivity * yawMult))
	camera.rot = composeWalkingRotation(camera.walkYaw, camera.walkPitch)
end

local function applyFlightMouseLook(dx, dy, modifiers)
	local pitchDown = controls.getActionMouseAxisValue("flight_pitch_down", dx, dy, modifiers)
	local pitchUp = controls.getActionMouseAxisValue("flight_pitch_up", dx, dy, modifiers)
	local rollLeft = controls.getActionMouseAxisValue("flight_roll_left", dx, dy, modifiers)
	local rollRight = controls.getActionMouseAxisValue("flight_roll_right", dx, dy, modifiers)
	local yawLeft = controls.getActionMouseAxisValue("flight_yaw_left", dx, dy, modifiers)
	local yawRight = controls.getActionMouseAxisValue("flight_yaw_right", dx, dy, modifiers)

	local pitchAxis = pitchDown - pitchUp
	local yawAxis = yawRight - yawLeft
	local rollAxis = rollLeft - rollRight

	if invertLookY then
		pitchAxis = -pitchAxis
	end

	camera.yoke = camera.yoke or { pitch = 0, yaw = 0, roll = 0 }
	local yoke = camera.yoke
	local pitchGain = camera.yokeMousePitchGain or 16
	local yawGain = camera.yokeMouseYawGain or 14
	local rollGain = camera.yokeMouseRollGain or 12

	yoke.pitch = clamp(yoke.pitch + pitchAxis * mouseSensitivity * pitchGain, -1, 1)
	yoke.yaw = clamp(yoke.yaw + yawAxis * mouseSensitivity * yawGain, -1, 1)
	yoke.roll = clamp(yoke.roll + rollAxis * mouseSensitivity * rollGain, -1, 1)
end

local function updateZoomFov(dt)
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
	if not flightSimMode then
		return
	end

	local step = camera.wheelThrottleStep or 0.06
	camera.throttle = clamp((camera.throttle or 0) + (step * direction), 0, 1)
end

local inputSystem = inputSystemModule.create({
	controls = controls,
	q = q,
	viewMath = viewMath,
	clamp = clamp,
	love = love,
	logger = logger,
	composeWalkingRotation = composeWalkingRotation,
	camera = function()
		return camera
	end,
	mapState = function()
		return mapState
	end,
	flightSimMode = function()
		return flightSimMode
	end,
	mouseSensitivity = function()
		return mouseSensitivity
	end,
	invertLookY = function()
		return invertLookY
	end,
	walkingPitchLimit = function()
		return walkingPitchLimit
	end
})

buildMovementInputState = inputSystem.buildMovementInputState
applyCameraRotations = inputSystem.applyCameraRotations
applyWalkingMouseLook = inputSystem.applyWalkingMouseLook
applyFlightMouseLook = inputSystem.applyFlightMouseLook
updateZoomFov = inputSystem.updateZoomFov
adjustFlightThrottleFromWheel = inputSystem.adjustFlightThrottleFromWheel

local function setPromptTextInputEnabled(enabled)
	if love.keyboard and type(love.keyboard.setTextInput) == "function" then
		love.keyboard.setTextInput(enabled and true or false)
	end
end

local function normalizeModelLoadPromptState()
	if type(modelLoadPrompt.text) ~= "string" then
		modelLoadPrompt.text = ""
	end
	modelLoadPrompt.cursor = clamp(tonumber(modelLoadPrompt.cursor) or #modelLoadPrompt.text, 0, #modelLoadPrompt.text)
end

local function insertModelLoadPromptText(text)
	normalizeModelLoadPromptState()
	if type(text) ~= "string" or text == "" then
		return
	end

	if modelLoadPrompt.mode == "callsign" then
		text = text:gsub("[^ -~]", "")
		text = text:gsub("|", "")
		if text == "" then
			return
		end
		local room = 20 - #modelLoadPrompt.text
		if room <= 0 then
			return
		end
		if #text > room then
			text = text:sub(1, room)
		end
	end

	local left = modelLoadPrompt.text:sub(1, modelLoadPrompt.cursor)
	local right = modelLoadPrompt.text:sub(modelLoadPrompt.cursor + 1)
	modelLoadPrompt.text = left .. text .. right
	modelLoadPrompt.cursor = modelLoadPrompt.cursor + #text
end

function closeModelLoadPrompt(statusMessage, duration)
	modelLoadPrompt.active = false
	modelLoadPrompt.cursor = math.max(0, #modelLoadPrompt.text)
	modelLoadPrompt.mode = "model_path"
	setPromptTextInputEnabled(false)
	if statusMessage then
		setPauseStatus(statusMessage, duration or 1.8)
	end
end

function submitModelLoadPrompt()
	if modelLoadPrompt.mode == "callsign" then
		local nextCallsign = sanitizeCallsign(modelLoadPrompt.text or "")
		if not nextCallsign then
			closeModelLoadPrompt("Callsign must contain printable characters.", 1.8)
			return
		end
		localCallsign = nextCallsign
		if forceStateSync then
			forceStateSync()
		end
		closeModelLoadPrompt("Callsign set: " .. localCallsign, 1.8)
		return
	end

	local path = (modelLoadPrompt.text or ""):gsub("^%s+", ""):gsub("%s+$", "")
	if path == "" then
		closeModelLoadPrompt("Model load cancelled.", 1.2)
		return
	end
	local ok, entryOrErr = loadPlayerModelFromPath(path, modelLoadTargetRole)
	if ok then
		closeModelLoadPrompt("Loaded model: " .. path, 2.4)
	else
		closeModelLoadPrompt("Failed to load model: " .. tostring(entryOrErr), 2.6)
	end
end

function love.mousemoved(x, y, dx, dy)
	if pauseMenu.active then
		if modelLoadPrompt.active then
			return
		end
		if pauseMenu.tab == "controls" and pauseMenu.listeningBinding then
			captureBindingFromMouseMotion(dx, dy)
			return
		end

		updatePauseMenuHover(x, y)
		updatePauseControlsHover(x, y)
		updatePauseRangeDrag(x)
		return
	end

	if not relative then
		return
	end

	if viewState.mode == "first_person" and altLookState.held then
		applyAltLookMouse(dx, dy)
		return
	end

	local modifiers = controls.getCurrentModifiers()
	if flightSimMode then
		applyFlightMouseLook(dx, dy, modifiers)
	else
		applyWalkingMouseLook(dx, dy, modifiers)
	end
end

function updateNet()
	local canSend = relayIsConnected()
	local now = love.timer.getTime()

	if canSend and (stateNet.forceSend or (now - stateNet.lastSentAt) >= stateNet.sendInterval) then
		local activeRole = getActiveModelRole()
		local activeHash, activeScale = getConfiguredModelForRole(activeRole)
		local planeHash, planeScale = getConfiguredModelForRole("plane")
		local walkingHash, walkingScale = getConfiguredModelForRole("walking")
		local planeSkinHash = getConfiguredSkinHashForRole("plane")
		local walkingSkinHash = getConfiguredSkinHashForRole("walking")
		local planeOrientation = getRoleOrientation("plane")
		local walkingOrientation = getRoleOrientation("walking")
		local outboundCallsign = sanitizeCallsign(localCallsign) or "Pilot"
		local activeVelocity = camera.flightVel or camera.vel or { 0, 0, 0 }
		local activeAngVel = camera.flightAngVel or {
			(camera.flightRotVel and camera.flightRotVel.pitch) or 0,
			(camera.flightRotVel and camera.flightRotVel.yaw) or 0,
			(camera.flightRotVel and camera.flightRotVel.roll) or 0
		}
		local activeYoke = camera.yoke or { pitch = 0, yaw = 0, roll = 0 }
		local flightTick = (camera.flightSimState and camera.flightSimState.tick) or 0
		local packet = table.concat({
			"STATE3",
			"px=" .. string.format("%f", camera.pos[1]),
			"py=" .. string.format("%f", camera.pos[2]),
			"pz=" .. string.format("%f", camera.pos[3]),
			"rw=" .. string.format("%f", camera.rot.w),
			"rx=" .. string.format("%f", camera.rot.x),
			"ry=" .. string.format("%f", camera.rot.y),
			"rz=" .. string.format("%f", camera.rot.z),
			"vx=" .. formatNetFloat(activeVelocity[1] or 0),
			"vy=" .. formatNetFloat(activeVelocity[2] or 0),
			"vz=" .. formatNetFloat(activeVelocity[3] or 0),
			"wx=" .. formatNetFloat(activeAngVel[1] or 0),
			"wy=" .. formatNetFloat(activeAngVel[2] or 0),
			"wz=" .. formatNetFloat(activeAngVel[3] or 0),
			"thr=" .. formatNetFloat(camera.throttle or 0),
			"elev=" .. formatNetFloat(activeYoke.pitch or 0),
			"ail=" .. formatNetFloat(activeYoke.roll or 0),
			"rud=" .. formatNetFloat(-(activeYoke.yaw or 0)),
			"tick=" .. tostring(math.floor(flightTick)),
			"ts=" .. formatNetFloat(now),
			"scale=" .. formatNetFloat(activeScale),
			"modelHash=" .. tostring(activeHash or "builtin-cube"),
			"role=" .. tostring(activeRole),
			"planeScale=" .. formatNetFloat(planeScale),
			"planeModelHash=" .. tostring(planeHash or "builtin-cube"),
			"planeYaw=" .. formatNetFloat(planeOrientation.yaw),
			"planePitch=" .. formatNetFloat(planeOrientation.pitch),
			"planeRoll=" .. formatNetFloat(planeOrientation.roll),
			"walkingScale=" .. formatNetFloat(walkingScale),
			"walkingModelHash=" .. tostring(walkingHash or "builtin-cube"),
			"walkingYaw=" .. formatNetFloat(walkingOrientation.yaw),
			"walkingPitch=" .. formatNetFloat(walkingOrientation.pitch),
			"walkingRoll=" .. formatNetFloat(walkingOrientation.roll),
			"planeSkinHash=" .. tostring(planeSkinHash or ""),
			"walkingSkinHash=" .. tostring(walkingSkinHash or ""),
			"callsign=" .. outboundCallsign
		}, "|")
		pcall(function()
			relayServer:send(packet)
		end)
		stateNet.lastSentAt = now
		stateNet.forceSend = false

		sendCloudSnapshot(false)
	end
	pumpModelTransfers(now)
end

function serviceNetworkEvents()
	if not relay then
		return
	end

	event = relay:service()
	while event do
		if event.type == "connect" and relayServer and event.peer == relayServer then
			beginCloudRoleElection("relay handshake complete")
			forceStateSync()
		elseif event.type == "receive" then
			local packetType = nil
			if type(event.data) == "string" then
				packetType = splitPacketFields(event.data)[1]
			end
			if packetType == "STATE" or packetType == "STATE2" or packetType == "STATE3" then
				local receivedAt = love.timer.getTime()
				local peerId = networking.handlePacket(
					event.data,
					peers,
					objects,
					q,
					cubeModel,
					getModelRotationOffsetForRole,
					{
						scale = defaultPlayerModelScale,
						modelHash = "builtin-cube",
						planeScale = defaultPlayerModelScale,
						walkingScale = defaultWalkingModelScale,
						planeModelHash = "builtin-cube",
						walkingModelHash = "builtin-cube",
						planeSkinHash = "",
						walkingSkinHash = "",
						planeOrientation = { yaw = 0, pitch = 0, roll = 0 },
						walkingOrientation = { yaw = 0, pitch = 0, roll = 0 },
						role = "plane"
					},
					receivedAt
				)
				updatePeerVisualModel(peerId)
				notePeerStateReceived(peerId)
			elseif packetType == "CLOUD_REQUEST" then
				handleCloudRequestPacket(event.data)
			elseif packetType == "CLOUD_SNAPSHOT" then
				handleCloudSnapshotPacket(event.data)
			elseif packetType == "GROUND_REQUEST" then
				handleGroundRequestPacket(event.data)
			elseif packetType == "GROUND_SNAPSHOT" or packetType == "GROUND_SNAPSHOT2" then
				handleGroundSnapshotPacket(event.data)
			elseif packetType == "MODEL_REQUEST" or packetType == "BLOB_REQUEST" then
				handleModelRequestPacket(event.data)
			elseif packetType == "MODEL_META" or packetType == "BLOB_META" then
				handleModelMetaPacket(event.data)
			elseif packetType == "MODEL_CHUNK" or packetType == "BLOB_CHUNK" then
				handleModelChunkPacket(event.data)
			end
		elseif event.type == "disconnect" and relayServer and event.peer == relayServer then
			relayServer = nil
			clearPeerObjects()
			forceStateSync()
			cloudNetState.authorityPeerId = nil
			groundNetState.authorityPeerId = nil
			groundNetState.lastSnapshotReceivedAt = -math.huge
			groundNetState.lastSnapshotRequestAt = -math.huge
			setCloudRole("standalone", "relay disconnected")
			logger.log("Relay disconnected.")
		end

		event = relay:service()
	end
end

-- === Camera Movement ===
function love.update(dt)
	syncAppStateReferences()
	local runtimeState = appState.values
	local now = love.timer.getTime()
	updateCloudNetworkState(now)

	if pauseMenu.statusUntil > 0 and love.timer.getTime() >= pauseMenu.statusUntil then
		pauseMenu.statusText = ""
		pauseMenu.statusUntil = 0
	end
	if pauseMenu.confirmItemId and love.timer.getTime() > pauseMenu.confirmUntil then
		clearPauseConfirm()
	end

	if not pauseMenu.active then
		updateGroundStreaming(false)
		local movementInput = buildMovementInputState()
		local terrainRef = terrainState or activeGroundParams or defaultGroundParams
		local movementEnv = {
			wind = cloudSim.getWindVector3(windState),
			groundHeightAt = function(x, z)
				return sampleGroundHeightAtWorld(x, z, activeGroundParams)
			end,
			queryGroundHeight = function(x, z)
				return terrainSdfSystem.queryGroundHeight(x, z, terrainRef)
			end,
			sampleSdf = function(x, y, z)
				return terrainSdfSystem.sampleSdfAtWorld(x, y, z, terrainRef)
			end,
			sampleNormal = function(x, y, z)
				return terrainSdfSystem.sampleNormalAtWorld(x, y, z, terrainRef)
			end,
			groundClearance = (camera.box and camera.box.halfSize and camera.box.halfSize.y) or 1.0,
			collisionRadius = math.max(
				(camera.box and camera.box.halfSize and camera.box.halfSize.x) or 0,
				(camera.box and camera.box.halfSize and camera.box.halfSize.y) or 0,
				(camera.box and camera.box.halfSize and camera.box.halfSize.z) or 0,
				1.0
			)
		}
		if flightSimMode then
			local frameFlightModel = {}
			for key, value in pairs(flightModelConfig or {}) do
				frameFlightModel[key] = value
			end
			frameFlightModel.metersPerUnit = tonumber(geoConfig and geoConfig.metersPerUnit) or
				tonumber(frameFlightModel.metersPerUnit) or 1.0
			camera = flightDynamics.step(
				camera,
				dt,
				movementInput,
				movementEnv,
				frameFlightModel
			)
			if camera and camera.flightCrashEvent then
				handleFlightCrashEvent(camera.flightCrashEvent)
			end
		else
			camera = engine.processMovement(
				camera,
				dt,
				false,
				vector3,
				q,
				runtimeState.objects or objects,
				movementInput,
				movementEnv
			)
		end
		syncLocalPlayerObject()
		cloudSim.updateClouds(dt, cloudState, cloudNetState, camera, windState, love.timer.getTime(), q)
		updateZoomFov(dt)

		perfElapsed = perfElapsed + dt
		perfFrames = perfFrames + 1
		if perfElapsed >= 2.0 then
			local avgFps = perfFrames / perfElapsed
			if useGpuRenderer and renderer.isReady() and avgFps < 100 and not perfLoggedLowFps then
				logger.log(string.format("FPS below 100 target in GPU mode (avg %.1f)", avgFps))
				perfLoggedLowFps = true
			elseif useGpuRenderer and renderer.isReady() and avgFps >= 100 then
				perfLoggedLowFps = false
			end
			perfElapsed = 0
			perfFrames = 0
		end
	end

	if startupUi.audioRuntime and type(startupUi.audioRuntime.update) == "function" and camera then
		local terrainRef = activeGroundParams or defaultGroundParams or {}
		local speedVec = (flightSimMode and camera.flightVel) or camera.vel or { 0, 0, 0 }
		local airspeed = math.sqrt(
			(speedVec[1] or 0) * (speedVec[1] or 0) +
			(speedVec[2] or 0) * (speedVec[2] or 0) +
			(speedVec[3] or 0) * (speedVec[3] or 0)
		)
		local groundY = sampleGroundHeightAtWorld(camera.pos[1], camera.pos[3], terrainRef)
		local waterLevel = tonumber(terrainRef.waterLevel) or -12
		local waterDist = math.abs((camera.pos[2] or 0) - waterLevel)
		local waterProximity = clamp(1.0 - (waterDist / 95.0), 0, 1)
		if groundY > (waterLevel + 2.0) then
			waterProximity = waterProximity * 0.35
		end
		startupUi.audioRuntime:update({
			dt = dt,
			active = flightSimMode,
			audioEnabled = audioSettings.enabled ~= false,
			masterVolume = audioSettings.masterVolume,
			engineVolume = audioSettings.engineVolume,
			ambienceVolume = audioSettings.ambienceVolume,
			enginePitch = audioSettings.enginePitch,
			ambiencePitch = audioSettings.ambiencePitch,
			throttle = flightSimMode and (camera.throttle or 0) or 0,
			afterburner = flightSimMode and controls.isActionDown("flight_afterburner"),
			airspeed = airspeed,
			waterProximity = waterProximity,
			paused = pauseMenu.active and true or false
		})
	end

	updateNet()
	resolveActiveRenderCamera()
	serviceNetworkEvents()
	networking.updateRemoteInterpolation(peers, love.timer.getTime(), {
		interpolationDelay = 0.120,
		extrapolationCap = 0.200,
		qOps = q
	})
	updateCloudNetworkState(love.timer.getTime())

	if autoSmoke.enabled then
		autoSmoke.elapsed = autoSmoke.elapsed + dt
		if (not autoSmoke.screenshotRequested) and autoSmoke.elapsed >= 2.5 then
			local screenshotName = autoSmoke.screenshotName or "autosmoke_plane.png"
			local okShot, shotErr = pcall(function()
				love.graphics.captureScreenshot(screenshotName)
			end)
			if okShot then
				local saveDir = (love.filesystem and love.filesystem.getSaveDirectory and love.filesystem.getSaveDirectory()) or
					"<save-dir>"
				logger.log("AutoSmoke screenshot captured: " .. tostring(saveDir) .. "\\" .. tostring(screenshotName))
			else
				logger.log("AutoSmoke screenshot failed: " .. tostring(shotErr))
			end
			autoSmoke.screenshotRequested = true
			autoSmoke.quitAt = autoSmoke.elapsed + 1.0
		end
		if autoSmoke.quitAt and autoSmoke.elapsed >= autoSmoke.quitAt then
			logger.log("AutoSmoke complete; quitting.")
			love.event.quit()
			return
		end
	end
end

function love.quit()
	if startupUi.audioRuntime and type(startupUi.audioRuntime.stop) == "function" then
		startupUi.audioRuntime:stop()
	end
end

-- === Input Management ===
function love.keypressed(key, scancode, isrepeat)
	if pauseMenu.active then
		if modelLoadPrompt.active then
			normalizeModelLoadPromptState()
			if key == "escape" then
				closeModelLoadPrompt("Model load cancelled.", 1.2)
			elseif key == "return" or key == "kpenter" then
				submitModelLoadPrompt()
			elseif key == "backspace" then
				if modelLoadPrompt.cursor > 0 then
					local left = modelLoadPrompt.text:sub(1, modelLoadPrompt.cursor - 1)
					local right = modelLoadPrompt.text:sub(modelLoadPrompt.cursor + 1)
					modelLoadPrompt.text = left .. right
					modelLoadPrompt.cursor = modelLoadPrompt.cursor - 1
				end
			elseif key == "delete" then
				if modelLoadPrompt.cursor < #modelLoadPrompt.text then
					local left = modelLoadPrompt.text:sub(1, modelLoadPrompt.cursor)
					local right = modelLoadPrompt.text:sub(modelLoadPrompt.cursor + 2)
					modelLoadPrompt.text = left .. right
				end
			elseif key == "left" then
				modelLoadPrompt.cursor = math.max(0, modelLoadPrompt.cursor - 1)
			elseif key == "right" then
				modelLoadPrompt.cursor = math.min(#modelLoadPrompt.text, modelLoadPrompt.cursor + 1)
			elseif key == "home" then
				modelLoadPrompt.cursor = 0
			elseif key == "end" then
				modelLoadPrompt.cursor = #modelLoadPrompt.text
			elseif key == "v" and (love.keyboard.isDown("lctrl", "rctrl")) then
				local clipboard = love.system and love.system.getClipboardText and love.system.getClipboardText() or ""
				if type(clipboard) == "string" and clipboard ~= "" then
					insertModelLoadPromptText(clipboard)
				end
			end
			return
		end

		if pauseMenu.tab == "controls" and pauseMenu.listeningBinding then
			if key == "escape" then
				cancelControlBindingCapture()
			else
				captureBindingFromKey(key)
			end
			return
		end

		if key == "escape" then
			setPauseState(false)
			return
		end

		if key == "tab" or key == "h" then
			cyclePauseTab(1)
			return
		end
		if key == "leftbracket" or key == "[" then
			cyclePauseSubTab(-1)
			return
		end
		if key == "rightbracket" or key == "]" then
			cyclePauseSubTab(1)
			return
		end

		if pauseMenu.tab == "help" then
			if key == "up" or key == "w" then
				pauseMenu.helpScroll = math.max(0, pauseMenu.helpScroll - 24)
			elseif key == "down" or key == "s" then
				pauseMenu.helpScroll = pauseMenu.helpScroll + 24
			elseif key == "left" or key == "a" then
				cyclePauseTab(-1)
			elseif key == "right" or key == "d" then
				cyclePauseTab(1)
			elseif key == "home" then
				pauseMenu.helpScroll = 0
			end
			return
		end

		if pauseMenu.tab == "controls" then
			if key == "up" or key == "w" then
				moveControlsSelection(-1)
			elseif key == "down" or key == "s" then
				moveControlsSelection(1)
			elseif key == "left" or key == "a" then
				moveControlsSlot(-1)
			elseif key == "right" or key == "d" then
				moveControlsSlot(1)
			elseif key == "home" then
				pauseMenu.controlsSelection = 1
				pauseMenu.controlsScroll = 0
			elseif key == "end" then
				pauseMenu.controlsSelection = getPauseControlActionsCount()
			elseif key == "return" or key == "kpenter" or key == "space" then
				beginSelectedControlBindingCapture(pauseMenu.controlsSelection, pauseMenu.controlsSlot)
			elseif key == "backspace" or key == "delete" then
				clearSelectedControlBinding()
			elseif key == "r" then
				resetControlBindingsToDefaults()
				setPauseStatus("Controls reset to defaults.", 1.7)
			end
			return
		end

		if key == "up" or key == "w" then
			movePauseSelection(-1)
		elseif key == "down" or key == "s" then
			movePauseSelection(1)
		elseif key == "home" then
			pauseMenu.selected = 1
			clearPauseConfirm()
		elseif key == "end" then
			local items = getPauseItemsForCurrentPage()
			pauseMenu.selected = math.max(1, #items)
			clearPauseConfirm()
		elseif key == "left" or key == "a" then
			adjustSelectedPauseItem(-1)
		elseif key == "right" or key == "d" then
			adjustSelectedPauseItem(1)
		elseif key == "pageup" then
			adjustSelectedPauseItemCoarse(1)
		elseif key == "pagedown" then
			adjustSelectedPauseItemCoarse(-1)
		elseif key == "return" or key == "kpenter" or key == "space" then
			activateSelectedPauseItem()
		end
		return
	end

	if key == "escape" then
		setPauseState(true)
		return
	end
	if key == "lalt" then
		if not isrepeat and viewState.mode == "first_person" then
			altLookState.held = true
			altLookState.yaw = 0
			altLookState.pitch = 0
		end
		return
	end
	if key == "m" then
		if not isrepeat then
			mapState.mHeld = true
			mapState.mUsedForZoom = false
		end
		return
	end
	if mapState.mHeld and (key == "up" or key == "down") then
		local delta = key == "up" and -1 or 1
		mapState.zoomIndex = clamp(mapState.zoomIndex + delta, 1, 3)
		mapState.mUsedForZoom = true
		updateLogicalMapCamera()
		return
	end
	if key == "c" then
		if not isrepeat then
			cycleViewMode()
		end
		return
	end
	if key == "0" then
		if not isrepeat then
			colorCorruptionMode = not colorCorruptionMode
			logger.log("World color corruption mode: " .. (colorCorruptionMode and "ON" or "OFF"))
		end
		return
	end

	if flightSimMode and controls.actionTriggeredByKey("flight_fire_projectile", key, controls.getCurrentModifiers()) then
		inputSystem.triggerProjectileAction()
	end

	-- debugging position/rotation and relating variables
	if key == "p" then
		local pos = camera.pos
		local rot = camera.rot
		local forward, right, up = engine.getCameraBasis(camera, q, vector3)

		print("\n=== Camera Debug Info ===")
		print(string.format("Position:    x=%.3f  y=%.3f  z=%.3f", pos[1], pos[2], pos[3]))
		print(string.format("Rotation:    w=%.5f  x=%.5f  y=%.5f  z=%.5f", rot.w, rot.x, rot.y, rot.z))
		print(string.format("Forward vec: x=%.3f  y=%.3f  z=%.3f", forward[1], forward[2], forward[3]))
		print(string.format("Right vec:   x=%.3f  y=%.3f  z=%.3f", right[1], right[2], right[3]))
		print(string.format("Up vec:      x=%.3f  y=%.3f  z=%.3f", up[1], up[2], up[3]))
	end

	if key == "f" then
		setFlightMode(not flightSimMode)
	end

	if key == "g" then
		setRendererPreference(not useGpuRenderer)
	end
end

function love.keyreleased(key)
	if key == "lalt" then
		resetAltLookState()
		resolveActiveRenderCamera()
		return
	end

	if key == "m" then
		local shouldToggle = mapState.mHeld and (not mapState.mUsedForZoom) and (not pauseMenu.active)
		mapState.mHeld = false
		mapState.mUsedForZoom = false
		if shouldToggle then
			mapState.visible = not mapState.visible
		end
	end
end

function love.textinput(text)
	if not pauseMenu.active or not modelLoadPrompt.active then
		return
	end
	insertModelLoadPromptText(text)
end

function love.mousepressed(x, y, button)
	if pauseMenu.active then
		if modelLoadPrompt.active then
			return
		end
		if pauseMenu.tab == "controls" and pauseMenu.listeningBinding then
			captureBindingFromMouseButton(button)
			return
		end

		if button == 1 then
			handlePauseMenuMouseClick(x, y)
		end
		return
	end

	if flightSimMode and controls.actionTriggeredByMouseButton("flight_fire_projectile", button, controls.getCurrentModifiers()) then
		inputSystem.triggerProjectileAction()
	end

	if not love.mouse.getRelativeMode() then
		setMouseCapture(true)
	end
end

function love.mousereleased(x, y, button)
	if not pauseMenu.active or button ~= 1 then
		return
	end
	endPauseRangeDrag(x)
end

function love.wheelmoved(x, y)
	if y == 0 then
		return
	end

	if pauseMenu.active then
		if modelLoadPrompt.active then
			return
		end
		if pauseMenu.tab == "controls" and pauseMenu.listeningBinding then
			captureBindingFromWheel(y)
			return
		end

		if pauseMenu.tab == "help" then
			pauseMenu.helpScroll = math.max(0, pauseMenu.helpScroll - (y * 24))
			return
		end

		if pauseMenu.tab == "controls" then
			pauseMenu.controlsScroll = math.max(0, pauseMenu.controlsScroll - (y * 24))
			return
		end

		local items = getPauseItemsForCurrentPage()
		local item = items[pauseMenu.selected]
		if item and (item.kind == "range" or item.kind == "cycle") then
			adjustPauseItem(item, y > 0 and 1 or -1)
		end
		return
	end

	local modifiers = controls.getCurrentModifiers()
	if controls.actionTriggeredByWheel("flight_throttle_up", y, modifiers) then
		adjustFlightThrottleFromWheel(1)
	end
	if controls.actionTriggeredByWheel("flight_throttle_down", y, modifiers) then
		adjustFlightThrottleFromWheel(-1)
	end
end

function love.filedropped(file)
	if not file then
		return
	end
	if modelLoadPrompt.active and modelLoadPrompt.mode == "callsign" then
		if pauseMenu.active then
			setPauseStatus("Finish callsign edit before dropping model files.", 1.8)
		end
		return
	end

	local name = (file.getFilename and file:getFilename()) or "dropped.model"
	local lowerName = string.lower(name or "")
	local isStl = lowerName:match("%.stl$") ~= nil
	local isGlb = lowerName:match("%.glb$") ~= nil
	local isGltf = lowerName:match("%.gltf$") ~= nil
	if isGltf then
		if pauseMenu.active then
			setPauseStatus("Dropped GLTF requires path load (for sidecar files).", 2.4)
		end
		return
	end
	if not (isStl or isGlb) then
		if pauseMenu.active then
			setPauseStatus("Dropped file is not STL/GLB: " .. tostring(name), 2.2)
		end
		return
	end

	local openOk = pcall(function()
		file:open("r")
	end)
	if not openOk then
		if pauseMenu.active then
			setPauseStatus("Could not open dropped model.", 2.2)
		end
		return
	end

	local readOk, raw = pcall(function()
		return file:read()
	end)
	if (not readOk) or (type(raw) ~= "string") or raw == "" then
		local sizeOk, sizeValue = pcall(function()
			return file:getSize()
		end)
		if sizeOk and tonumber(sizeValue) and tonumber(sizeValue) > 0 then
			readOk, raw = pcall(function()
				return file:read(tonumber(sizeValue))
			end)
		end
	end
	pcall(function()
		file:close()
	end)

	if (not readOk) or (type(raw) ~= "string") or raw == "" then
		if pauseMenu.active then
			setPauseStatus("Dropped model could not be read.", 2.2)
		end
		return
	end

	local dropRole = modelLoadPrompt.active and (modelLoadTargetRole or getActiveModelRole()) or getActiveModelRole()
	local ok, entryOrErr = loadPlayerModelFromRaw(raw, name, dropRole, { baseDir = "." })
	if ok then
		modelLoadPrompt.active = false
		modelLoadPrompt.mode = "model_path"
		setPromptTextInputEnabled(false)
		if pauseMenu.active then
			setPauseStatus("Loaded dropped model: " .. tostring(name), 2.4)
		end
	else
		if pauseMenu.active then
			setPauseStatus("Dropped model failed: " .. tostring(entryOrErr), 2.6)
		end
	end
end

function love.mousefocus(focused)
	if not focused then
		setMouseCapture(false)
	end
end

function love.focus(focused)
	if not focused and not pauseMenu.active then
		setPauseState(true)
		setPauseStatus("Paused: window focus lost.", 1.8)
	end
end

function projectWorldToScreen(worldPos, viewCamera, w, h)
	if not viewCamera or not worldPos then
		return nil
	end
	local rel = {
		worldPos[1] - viewCamera.pos[1],
		worldPos[2] - viewCamera.pos[2],
		worldPos[3] - viewCamera.pos[3]
	}
	local cam = q.rotateVector(q.conjugate(viewCamera.rot), rel)
	if cam[3] <= 0.05 then
		return nil
	end
	local f = 1 / math.tan((viewCamera.fov or math.rad(90)) * 0.5)
	local nx = cam[1] * f / cam[3]
	local ny = cam[2] * f / cam[3]
	local sx = (w * 0.5) + nx * (w * 0.5)
	local sy = (h * 0.5) - ny * (h * 0.5)
	return sx, sy, cam[3]
end

function drawWorldPeerIndicators(w, h, viewCamera)
	if not viewCamera then
		return
	end
	local margin = 20
	for peerId, peerObj in pairs(peers) do
		if peerObj and peerObj.pos then
			local anchor = {
				peerObj.pos[1],
				peerObj.pos[2] + ((peerObj.halfSize and peerObj.halfSize.y) or 1.2) + 0.7,
				peerObj.pos[3]
			}
			local sx, sy, depth = projectWorldToScreen(anchor, viewCamera, w, h)
			if sx and sy and depth and depth > 0.05 then
				sx = clamp(sx, margin, w - margin)
				sy = clamp(sy, margin, h - margin)
				local dx = peerObj.pos[1] - viewCamera.pos[1]
				local dy = peerObj.pos[2] - viewCamera.pos[2]
				local dz = peerObj.pos[3] - viewCamera.pos[3]
				local distanceM = math.sqrt(dx * dx + dy * dy + dz * dz) * (geoConfig.metersPerUnit or 1.0)
				local label = sanitizeCallsign(peerObj.callsign) or ("P" .. tostring(peerId))
				local alpha = clamp(1 - (distanceM / 2400), 0.28, 0.92)
				local text = string.format("%s  %dm", label, math.floor(distanceM + 0.5))
				local textW = love.graphics.getFont():getWidth(text)
				local boxW = textW + 10
				local boxH = love.graphics.getFont():getHeight() + 6
				drawHudPlate(sx - boxW * 0.5, sy - boxH * 0.5, boxW, boxH, 5)
				love.graphics.setColor(0.32, 0.78, 1.0, alpha)
				love.graphics.polygon(
					"fill",
					sx, sy + (boxH * 0.5) + 8,
					sx - 4, sy + (boxH * 0.5) + 2,
					sx + 4, sy + (boxH * 0.5) + 2
				)
				love.graphics.setColor(hudTheme.text[1], hudTheme.text[2], hudTheme.text[3], alpha)
				love.graphics.print(text, sx - textW * 0.5, sy - (boxH * 0.5) + 3)
			end
		end
	end
end

function drawHud(w, h, cx, cy, renderCamera)
	local mapPanelX, mapPanelY, mapPanelSize

	if flightSimMode and camera then
		local wind = cloudSim.getWindVector3(windState)
		local v = camera.flightVel or camera.vel or { 0, 0, 0 }
		local airVel = {
			(v[1] or 0) - (wind[1] or 0),
			(v[2] or 0) - (wind[2] or 0),
			(v[3] or 0) - (wind[3] or 0)
		}
		local airSpeed = math.sqrt(airVel[1] * airVel[1] + airVel[2] * airVel[2] + airVel[3] * airVel[3])
		local airSpeedMph = airSpeed * 2.236936
		local airSpeedKph = airSpeed * 3.6
		local maxDialKph = math.max(200, tonumber(hudSettings.speedometerMaxKph) or 1000)
		local redlineKph = clamp(tonumber(hudSettings.speedometerRedlineKph) or 850, 100, maxDialKph)
		local clampedKph = clamp(airSpeedKph, 0, maxDialKph)
		local throttleRatio = clamp(camera.throttle or 0, 0, 1)
		local throttlePct = math.floor((throttleRatio * 100) + 0.5)
		local thrustNow = (camera.flightDebug and camera.flightDebug.thrust) or
			(throttleRatio * (camera.flightThrustForce or camera.flightThrustAccel or 0))
		if controls.isActionDown("flight_afterburner") and not (camera.flightDebug and camera.flightDebug.thrust) then
			thrustNow = thrustNow * (camera.afterburnerMultiplier or 1.45)
		end

		local dialSize = clamp(math.floor(math.min(w, h) * 0.22), 130, 220)
		local dialX = 24 + dialSize * 0.5
		local dialY = h - dialSize * 0.5 - 22
		if dialY < (dialSize * 0.5 + 14) then
			dialY = dialSize * 0.5 + 14
		end
		if hudSettings.showSpeedometer then
			local dialCanvas = ensureDialCanvas(dialSize)
			if dialCanvas then
				love.graphics.setColor(1, 1, 1, 0.98)
				love.graphics.draw(dialCanvas, dialX - dialSize * 0.5, dialY - dialSize * 0.5)
			else
				drawHudPlate(dialX - dialSize * 0.5, dialY - dialSize * 0.5, dialSize, dialSize, 9)
			end

			local gaugeStart = math.rad(150)
			local gaugeSweep = math.rad(240)
			local speedRatio = clampedKph / maxDialKph
			local needleAngle = gaugeStart + speedRatio * gaugeSweep
			local needleLen = dialSize * 0.4
			local nx = dialX + math.cos(needleAngle) * needleLen
			local ny = dialY + math.sin(needleAngle) * needleLen
			love.graphics.setLineWidth(2.5)
			if airSpeedKph > redlineKph then
				love.graphics.setColor(hudTheme.overspeed[1], hudTheme.overspeed[2], hudTheme.overspeed[3],
					hudTheme.overspeed[4])
			else
				love.graphics.setColor(hudTheme.needle[1], hudTheme.needle[2], hudTheme.needle[3], hudTheme.needle[4])
			end
			love.graphics.line(dialX, dialY, nx, ny)
			love.graphics.setLineWidth(1)
			love.graphics.setColor(0.88, 0.95, 1.0, 0.96)
			love.graphics.circle("fill", dialX, dialY, 5)
			love.graphics.setColor(0.05, 0.09, 0.12, 0.9)
			love.graphics.circle("line", dialX, dialY, 5)

			local centerText = string.format("%4d kph", math.floor(airSpeedKph + 0.5))
			local mphText = string.format("%3d mph", math.floor(airSpeedMph + 0.5))
			local overspeedText = (airSpeedKph > redlineKph) and "OVERSPEED" or "IAS"
			local centerW = math.max(love.graphics.getFont():getWidth(centerText),
				love.graphics.getFont():getWidth(mphText)) + 12
			drawHudPlate(dialX - centerW * 0.5, dialY - 20, centerW, 42, 6)
			love.graphics.setColor(hudTheme.text[1], hudTheme.text[2], hudTheme.text[3], hudTheme.text[4])
			love.graphics.print(centerText, dialX - (love.graphics.getFont():getWidth(centerText) * 0.5), dialY - 17)
			love.graphics.setColor(hudTheme.textDim[1], hudTheme.textDim[2], hudTheme.textDim[3], hudTheme.textDim[4])
			love.graphics.print(mphText, dialX - (love.graphics.getFont():getWidth(mphText) * 0.5), dialY - 2)
			if airSpeedKph > redlineKph then
				local x, y = dialX - (love.graphics.getFont():getWidth(overspeedText) * 0.5), dialY + 28
				love.graphics.rectangle("line", x - 20, y - 3, dialSize / 4, 40)
				love.graphics.setColor(hudTheme.overspeed[1], hudTheme.overspeed[2], hudTheme.overspeed[3], 0.95)
				love.graphics.print(overspeedText, x, y)
			else
				local x, y = dialX - (love.graphics.getFont():getWidth(overspeedText) * 0.5), dialY + 28
				love.graphics.rectangle("line", x - 20, y - 3, 60, 20)
				love.graphics.setColor(0.66, 0.82, 0.98, 0.9)
				love.graphics.print(overspeedText, x, y)
			end
		end

		if hudSettings.showThrottle then
			local throttleW = 118
			local throttleH = 82
			local throttleX = 18
			local throttleY = dialY - dialSize * 0.5 - throttleH - 10
			if throttleY < 14 then
				throttleY = 14
			end
			drawHudPlate(throttleX, throttleY, throttleW, throttleH, 6)
			love.graphics.setColor(hudTheme.text[1], hudTheme.text[2], hudTheme.text[3], hudTheme.text[4])
			love.graphics.print("THROTTLE", throttleX + 10, throttleY + 8)
			love.graphics.setColor(hudTheme.accent[1], hudTheme.accent[2], hudTheme.accent[3], hudTheme.accent[4])
			love.graphics.print(string.format("%03d%%", throttlePct), throttleX + 16, throttleY + 28)
			love.graphics.setColor(hudTheme.textDim[1], hudTheme.textDim[2], hudTheme.textDim[3], hudTheme.textDim[4])
			love.graphics.print(string.format("Thrust %.1f", thrustNow), throttleX + 10, throttleY + 56)
		end

		camera.yoke = camera.yoke or { pitch = 0, yaw = 0, roll = 0 }
		local yoke = camera.yoke
		if hudSettings.showFlightControls then
			local controlW = clamp(math.floor(w * 0.31), 220, 340)
			local controlH = clamp(math.floor(h * 0.2), 124, 180)
			local controlX = math.floor((w - controlW) * 0.5)
			local controlY = h - controlH - 14
			local controlCanvas = ensureControlPanelCanvas(controlW, controlH)
			if controlCanvas then
				love.graphics.setColor(1, 1, 1, 0.98)
				love.graphics.draw(controlCanvas, controlX, controlY)
			else
				drawHudPlate(controlX, controlY, controlW, controlH, 9)
			end

			local yokeSize = math.floor(math.min(controlW * 0.42, controlH * 0.56))
			local yokeX = controlX + 18
			local yokeY = controlY + 14
			local centerX = yokeX + yokeSize * 0.5
			local centerY = yokeY + yokeSize * 0.5
			local throw = yokeSize * 0.34
			local yokeHandleSize = 16
			local yokeHandleX = centerX + clamp(-yoke.roll or 0, -1, 1) * throw - (yokeHandleSize * 0.5)
			local yokeHandleY = centerY + clamp(-yoke.pitch or 0, -1, 1) * throw - (yokeHandleSize * 0.5)

			love.graphics.setColor(0.04, 0.07, 0.1, 0.84)
			love.graphics.rectangle("fill", yokeX, yokeY, yokeSize, yokeSize, 4, 4)
			love.graphics.setColor(0.74, 0.9, 1.0, 0.9)
			love.graphics.rectangle("line", yokeX, yokeY, yokeSize, yokeSize, 4, 4)
			love.graphics.line(centerX, yokeY + 8, centerX, yokeY + yokeSize - 8)
			love.graphics.line(yokeX + 8, centerY, yokeX + yokeSize - 8, centerY)

			love.graphics.setColor(0.95, 0.97, 1.0, 0.95)
			love.graphics.rectangle("fill", yokeHandleX, yokeHandleY, yokeHandleSize, yokeHandleSize, 2, 2)
			love.graphics.setColor(0.10, 0.12, 0.15, 0.95)
			love.graphics.rectangle("line", yokeHandleX, yokeHandleY, yokeHandleSize, yokeHandleSize, 2, 2)

			local rudderWidth = math.floor(controlW * 0.42)
			local rudderHeight = 10
			local rudderX = controlX + controlW - rudderWidth - 18
			local rudderY = controlY + math.floor(controlH * 0.64)
			local rudderHandleSize = rudderHeight + 8
			local rudderRatio = (clamp(yoke.yaw or 0, -1, 1) + 1) * 0.5
			local rudderHandleX = rudderX + rudderRatio * rudderWidth - (rudderHandleSize * 0.5)
			rudderHandleX = clamp(rudderHandleX, rudderX - (rudderHandleSize * 0.5),
				rudderX + rudderWidth - (rudderHandleSize * 0.5))

			love.graphics.setColor(0.05, 0.07, 0.08, 0.75)
			love.graphics.rectangle("fill", rudderX, rudderY, rudderWidth, rudderHeight, 3, 3)
			love.graphics.setColor(0.75, 0.85, 0.92, 0.9)
			love.graphics.rectangle("line", rudderX, rudderY, rudderWidth, rudderHeight, 3, 3)
			love.graphics.setColor(0.95, 0.97, 1.0, 0.95)
			love.graphics.rectangle("fill", rudderHandleX, rudderY - 4, rudderHandleSize, rudderHandleSize, 2, 2)
			love.graphics.setColor(0.10, 0.12, 0.15, 0.95)
			love.graphics.rectangle("line", rudderHandleX, rudderY - 4, rudderHandleSize, rudderHandleSize, 2, 2)
			love.graphics.setColor(0.86, 0.92, 1.0, 0.95)
			love.graphics.print("YOKE", yokeX + 20, yokeY + yokeSize + 5)
			love.graphics.print("RUDDER", rudderX + 8, rudderY + rudderHeight + 6)
			love.graphics.setColor(0.65, 0.8, 0.95, 0.9)
			love.graphics.print(
				string.format("P %.2f  R %.2f  Y %.2f", yoke.pitch or 0, yoke.roll or 0, yoke.yaw or 0),
				controlX + 14,
				controlY + controlH - love.graphics.getFont():getHeight() - 7
			)
		end
	end

	if not pauseMenu.active and hudSettings.showPeerIndicators then
		drawWorldPeerIndicators(w, h, renderCamera or resolveActiveRenderCamera() or camera)
	end

	if hudSettings.showMap and mapState.visible then
		local mapCam = updateLogicalMapCamera()
		if mapCam then
			local panelSize = clamp(
				math.floor(math.min(w, h) * mapState.panel.widthRatio),
				mapState.panel.minSize,
				mapState.panel.maxSize
			)
			local panelX = w - mapState.panel.margin - panelSize
			local panelY = mapState.panel.margin
			local panelCenterX = panelX + panelSize * 0.5
			local panelCenterY = panelY + panelSize * 0.5
			local extent = math.max(1, mapCam.extent)
			local worldToPixel = (panelSize * 0.5) / extent
			local zoomLabels = { "Near", "Mid", "Full" }
			local markerSizes = { 4, 5, 7 }
			local localMarkerSize = markerSizes[mapState.zoomIndex] or 5
			local peerMarkerSize = math.max(2, localMarkerSize - 1)
			local showPeerLabels = mapState.zoomIndex <= 2

			mapPanelX = panelX
			mapPanelY = panelY
			mapPanelSize = panelSize

			drawHudPlate(panelX, panelY, panelSize, panelSize, 6)
			local mapImage = ensureMapGroundImage(panelSize, mapCam)
			love.graphics.setScissor(panelX, panelY, panelSize, panelSize)
			if mapImage then
				local scale = panelSize / math.max(1, mapState.mapRes)
				love.graphics.setColor(1, 1, 1, 0.97)
				love.graphics.draw(mapImage, panelX, panelY, 0, scale, scale)
			else
				love.graphics.setColor(0.16, 0.22, 0.18, 0.92)
				love.graphics.rectangle("fill", panelX, panelY, panelSize, panelSize)
			end

			local function drawMarker(worldX, worldZ, color, radius)
				local dx = worldX - mapCam.pos[1]
				local dz = worldZ - mapCam.pos[3]
				local mapX, mapZ = viewMath.rotateWorldDeltaToMap(dx, dz, mapCam.heading)
				local sx = panelCenterX + mapX * worldToPixel
				local sy = panelCenterY - mapZ * worldToPixel
				if sx < panelX or sx > (panelX + panelSize) or sy < panelY or sy > (panelY + panelSize) then
					return nil
				end
				love.graphics.setColor(color[1], color[2], color[3], color[4] or 1)
				love.graphics.circle("fill", sx, sy, radius)
				return sx, sy
			end

			if localPlayerObject and localPlayerObject.pos then
				drawMarker(localPlayerObject.pos[1], localPlayerObject.pos[3], { 0.96, 0.36, 0.12, 0.95 },
					localMarkerSize)
			end
			for peerId, peerObj in pairs(peers) do
				if peerObj and peerObj.pos then
					local sx, sy = drawMarker(peerObj.pos[1], peerObj.pos[3], { 0.22, 0.72, 1.0, 0.92 }, peerMarkerSize)
					if sx and sy and showPeerLabels then
						local label = sanitizeCallsign(peerObj.callsign) or ("P" .. tostring(peerId))
						local textW = love.graphics.getFont():getWidth(label)
						local tx = clamp(sx + peerMarkerSize + 3, panelX + 2, panelX + panelSize - textW - 2)
						local ty = clamp(sy - math.floor(love.graphics.getFont():getHeight() * 0.5), panelY + 2,
							panelY + panelSize - love.graphics.getFont():getHeight() - 2)
						love.graphics.setColor(0, 0, 0, 0.5)
						love.graphics.print(label, tx + 1, ty + 1)
						love.graphics.setColor(0.9, 0.96, 1.0, 0.95)
						love.graphics.print(label, tx, ty)
					end
				end
			end

			love.graphics.setColor(1, 1, 1, 0.95)
			love.graphics.circle("line", panelCenterX, panelCenterY, localMarkerSize + 2)
			love.graphics.polygon(
				"fill",
				panelCenterX, panelCenterY - (localMarkerSize + 5),
				panelCenterX - 3, panelCenterY - (localMarkerSize + 1),
				panelCenterX + 3, panelCenterY - (localMarkerSize + 1)
			)
			love.graphics.setScissor()

			love.graphics.setColor(0.86, 0.92, 1.0, 0.95)
			love.graphics.rectangle("line", panelX, panelY, panelSize, panelSize, 6, 6)
			love.graphics.print(
				string.format("Map %s (M tap/toggle, hold+Up/Down zoom)",
					zoomLabels[mapState.zoomIndex] or tostring(mapState.zoomIndex)),
				panelX,
				panelY + panelSize + 4
			)
		end
	end

	if camera and hudSettings.showGeoInfo then
		local lat, lon = worldToGeo(camera.pos[1], camera.pos[3])
		local metersPerUnit = math.max(1e-6, tonumber(geoConfig.metersPerUnit) or 1.0)
		local groundY = sampleGroundHeightAtWorld(camera.pos[1], camera.pos[3], activeGroundParams or defaultGroundParams)
		local altMsl = camera.pos[2] * metersPerUnit
		local altAgl = (camera.pos[2] - groundY) * metersPerUnit
		local infoW = 236
		local infoH = 78
		local infoX = w - infoW - 12
		local infoY = 12
		if mapPanelX and mapPanelY and mapPanelSize then
			infoX = mapPanelX + math.max(0, mapPanelSize - infoW)
			infoY = mapPanelY + mapPanelSize + 24
		end
		if infoY + infoH > h - 8 then
			infoY = h - infoH - 8
		end
		drawHudPlate(infoX, infoY, infoW, infoH, 6)
		love.graphics.setColor(hudTheme.text[1], hudTheme.text[2], hudTheme.text[3], hudTheme.text[4])
		love.graphics.print(string.format("LAT %.6f", lat), infoX + 10, infoY + 8)
		love.graphics.print(string.format("LON %.6f", lon), infoX + 10, infoY + 24)
		love.graphics.setColor(hudTheme.textDim[1], hudTheme.textDim[2], hudTheme.textDim[3], hudTheme.textDim[4])
		love.graphics.print(string.format("ALT MSL %7.1f m", altMsl), infoX + 10, infoY + 42)
		love.graphics.print(string.format("ALT AGL %7.1f m", altAgl), infoX + 10, infoY + 58)
	end
end

function love.draw()
	if startupUi.active then
		drawStartupUi()
		return
	end

	syncAppStateReferences()
	local runtimeState = appState.values
	triangleCount = 0
	local centerX, centerY = screen.w / 2, screen.h / 2
	local renderCamera = (runtimeState.viewState and runtimeState.viewState.activeCamera) or
		resolveActiveRenderCamera() or camera
	local renderObjects = buildRenderObjectList()
	local worldTintR, worldTintG, worldTintB = 1, 1, 1
	if colorCorruptionMode then
		local t = love.timer.getTime()
		worldTintR = 0.52 + 0.48 * math.sin(t * 2.13 + 0.4)
		worldTintG = 0.52 + 0.48 * math.sin(t * 2.71 + 2.0)
		worldTintB = 0.52 + 0.48 * math.sin(t * 3.19 + 4.1)
	end
	local renderScale = clamp(tonumber(runtimeState.graphicsSettings and runtimeState.graphicsSettings.renderScale) or 1.0, 0.5, 1.5)
	graphicsSettings.renderScale = renderScale
	local scaledWorld = math.abs(renderScale - 1.0) > 0.001
	local worldW = math.max(320, math.floor(screen.w * renderScale + 0.5))
	local worldH = math.max(180, math.floor(screen.h * renderScale + 0.5))
	local worldScreen = { w = worldW, h = worldH }
	local worldScaleX = screen.w / worldW
	local worldScaleY = screen.h / worldH

	local usedGpu = false
	if useGpuRenderer and renderer.isReady() then
		local ok, renderOk, triOrErr
		if scaledWorld then
			local worldCanvas = ensureWorldRenderCanvas(worldW, worldH)
			if worldCanvas then
				local prevCanvas = love.graphics.getCanvas()
				love.graphics.setCanvas(worldCanvas)
				love.graphics.clear(0.2, 0.2, 0.75, 1.0)
				renderer.resize(worldScreen)
				ok, renderOk, triOrErr = pcall(renderer.drawWorld, renderObjects, renderCamera, { 0.2, 0.2, 0.75, 1.0 })
				love.graphics.setCanvas(prevCanvas)
				if ok and renderOk then
					love.graphics.setColor(worldTintR, worldTintG, worldTintB, 1)
					love.graphics.draw(worldCanvas, 0, 0, 0, worldScaleX, worldScaleY)
				end
			else
				renderer.resize(screen)
				love.graphics.setColor(worldTintR, worldTintG, worldTintB, 1)
				ok, renderOk, triOrErr = pcall(renderer.drawWorld, renderObjects, renderCamera, { 0.2, 0.2, 0.75, 1.0 })
			end
		else
			renderer.resize(screen)
			love.graphics.setColor(worldTintR, worldTintG, worldTintB, 1)
			ok, renderOk, triOrErr = pcall(renderer.drawWorld, renderObjects, renderCamera, { 0.2, 0.2, 0.75, 1.0 })
		end
		if ok and renderOk then
			usedGpu = true
			renderMode = "GPU"
			triangleCount = triOrErr or 0
			gpuErrorLogged = false
		else
			if not gpuErrorLogged then
				logger.log("GPU draw failed; switching to CPU fallback. Detail: " ..
					tostring(ok and triOrErr or renderOk))
				gpuErrorLogged = true
			end
			renderMode = "CPU"
		end
	end

	if not usedGpu then
		love.graphics.setColor(0.2, 0.2, 0.75, 0.8)
		love.graphics.rectangle("fill", 0, 0, screen.w, screen.h)
		local drawScreen = scaledWorld and worldScreen or screen

		-- reset z-buffer
		for i = 1, drawScreen.w * drawScreen.h do
			zBuffer[i] = math.huge
		end

		local imageData = love.image.newImageData(drawScreen.w, drawScreen.h)
		local opaqueObjects = {}
		local transparentObjects = {}
		for _, obj in ipairs(renderObjects) do
			if cpuRenderClassifier.isObjectTransparent(obj) then
				transparentObjects[#transparentObjects + 1] = obj
			else
				opaqueObjects[#opaqueObjects + 1] = obj
			end
		end

		for _, obj in ipairs(opaqueObjects) do
			imageData = engine.drawObject(
				obj,
				cpuRenderClassifier.shouldCullBackfaces(obj),
				renderCamera,
				vector3,
				q,
				drawScreen,
				zBuffer,
				imageData,
				true
			)
		end

		table.sort(transparentObjects, function(a, b)
			return viewMath.cameraSpaceDepthForObject(a, renderCamera, q) >
				viewMath.cameraSpaceDepthForObject(b, renderCamera, q)
		end)
		for _, obj in ipairs(transparentObjects) do
			imageData = engine.drawObject(
				obj,
				cpuRenderClassifier.shouldCullBackfaces(obj),
				renderCamera,
				vector3,
				q,
				drawScreen,
				zBuffer,
				imageData,
				false
			)
		end
		if frameImage and frameImageW == drawScreen.w and frameImageH == drawScreen.h then
			frameImage:replacePixels(imageData)
		else
			frameImage = love.graphics.newImage(imageData)
			frameImageW = drawScreen.w
			frameImageH = drawScreen.h
		end
		love.graphics.setColor(worldTintR, worldTintG, worldTintB, 1)
		if scaledWorld then
			love.graphics.draw(frameImage, 0, 0, 0, worldScaleX, worldScaleY)
		else
			love.graphics.draw(frameImage)
		end
	end

	if showDebugOverlay then
		local velocity = (flightSimMode and camera.flightVel) or camera.vel or { 0, 0, 0 }
		local speedUps = math.sqrt(
			(velocity[1] or 0) * (velocity[1] or 0) +
			(velocity[2] or 0) * (velocity[2] or 0) +
			(velocity[3] or 0) * (velocity[3] or 0)
		)
		local rotVel = camera.flightRotVel or { pitch = 0, yaw = 0, roll = 0 }
		local rotPitchDps = math.deg(rotVel.pitch or 0)
		local rotYawDps = math.deg(rotVel.yaw or 0)
		local rotRollDps = math.deg(rotVel.roll or 0)
		local rotTotalDps = math.sqrt(
			rotPitchDps * rotPitchDps +
			rotYawDps * rotYawDps +
			rotRollDps * rotRollDps
		)

		local debugText =
			"Esc = pause menu (see Help/Controls tabs for full controls)\n" ..
			"Render: " ..
			tostring(renderMode) .. " | Triangles: " .. tostring(math.floor(triangleCount)) .. " | FPS: " ..
			tostring(love.timer.getFPS()) .. "\n" ..
			"Graphics API: " .. getGraphicsBackendLabel() .. "\n" ..
			string.format(
				"View: %s | AltLook: %s | Map: %s (z%d)\n",
				viewState.mode,
				(viewState.mode == "first_person" and altLookState.held) and "on" or "off",
				mapState.visible and "on" or "off",
				mapState.zoomIndex
			) ..
			string.format(
				"Wind: %.1f u/s @ %d deg | Clouds: %d | CloudSync: %s | CorruptMode:%s\n",
				windState.speed,
				math.floor(math.deg(windState.angle) + 0.5),
				#cloudState.groups,
				cloudNetState.role,
				colorCorruptionMode and "on" or "off"
			) ..
			string.format(
				"Player: %.1f u/s | Rot: %.1f deg/s (P %.1f Y %.1f R %.1f)",
				speedUps,
				rotTotalDps,
				rotPitchDps,
				rotYawDps,
				rotRollDps
			)

		local debugFont = love.graphics.getFont()
		local lineHeight = debugFont and debugFont:getHeight() or 14
		local panelW = 160
		local lineCount = 0
		for line in debugText:gmatch("[^\n]+") do
			lineCount = lineCount + 1
			panelW = math.max(panelW, (debugFont and debugFont:getWidth(line) or 0) + 20)
		end
		local panelH = math.max(44, lineCount * lineHeight + 16)
		local panelX, panelY = 10, 10
		drawHudPlate(panelX, panelY, panelW, panelH, 6)
		love.graphics.setColor(hudTheme.text[1], hudTheme.text[2], hudTheme.text[3], hudTheme.text[4])
		love.graphics.printf(debugText, panelX + 10, panelY + 8, panelW - 20, "left")

		local chipText = string.format("%s | %d FPS", tostring(renderMode), love.timer.getFPS())
		local chipW = (debugFont and debugFont:getWidth(chipText) or 0) + 16
		local chipH = lineHeight + 10
		local chipY = panelY + panelH + 6
		drawHudPlate(panelX, chipY, chipW, chipH, 6)
		love.graphics.setColor(hudTheme.accent[1], hudTheme.accent[2], hudTheme.accent[3], 0.95)
		love.graphics.print(chipText, panelX + 8, chipY + 5)
	end

	if showCrosshair and not pauseMenu.active then
		local innerGap = 2
		local armLength = 7
		love.graphics.setColor(1.0, 0.3, 0.2, 0.86)
		love.graphics.setLineWidth(1)
		love.graphics.line(centerX - armLength, centerY, centerX - innerGap, centerY)
		love.graphics.line(centerX + innerGap, centerY, centerX + armLength, centerY)
		love.graphics.line(centerX, centerY - armLength, centerX, centerY - innerGap)
		love.graphics.line(centerX, centerY + innerGap, centerX, centerY + armLength)
		love.graphics.rectangle("fill", centerX - 1, centerY - 1, 2, 2)
	end

	drawHud(screen.w, screen.h, centerX, centerY, renderCamera)

	if pauseMenu.active then
		drawPauseMenu()
	end
end

function love.resize(w, h)
	screen.w = w
	screen.h = h
	renderer.resize(screen)
	refreshUiFonts()
	resetHudCaches()
	invalidateMapCache()
	syncGraphicsSettingsFromWindow()
	if renderer.setClipPlanes then
		renderer.setClipPlanes(0.1, graphicsSettings.drawDistance)
	end

	zBuffer = {}
	for i = 1, screen.w * screen.h do
		zBuffer[i] = math.huge
	end

	frameImage = nil
	frameImageW = 0
	frameImageH = 0
	worldRenderCanvas = nil
	worldRenderW = 0
	worldRenderH = 0
	logger.log(string.format("Window resized to %dx%d", w, h))
end
