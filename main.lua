
local hostAddy = "love.donreagan.ca:1988"
local q = require("quat")
local vector3 = require("vector3")
local love = require "love" -- avoids nil report from intellisense, safe to remove if causes issues (it should be OK)
local enet = require "enet"
local engine = require "engine"
local modelModule = require "model_module"
local networking = require "networking"
local renderer = require "gpu_renderer"
local controls = require "controls"
local groundSystem = require "ground_system"
local viewMath = require "view_math"
local cloudSim = require "cloud_sim"
local logger = require "logger"
local objectDefs = require "object"
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
defaultPlayerModelPath = "objects/Dualengine2.glb"
normalizedPlayerModelExtent = 2.2
defaultPlayerModelScale = 1.35
defaultWalkingModelScale = 1.0
playerPlaneModelScale = defaultPlayerModelScale
playerPlaneModelHash = "builtin-cube"
playerWalkingModelScale = defaultWalkingModelScale
playerWalkingModelHash = "builtin-cube"
playerModelScale = playerWalkingModelScale
playerModelHash = playerWalkingModelHash
local screen, camera, groundObject, triangleCount, frameImage, renderMode, gpuErrorLogged --, zBuffer
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
local walkingPitchLimit = math.rad(89)
local viewState = {
	mode = "first_person",
	activeCamera = nil
}
local autoSmoke = {
	enabled = (os.getenv("L2D3D_AUTOSMOKE") == "1"),
	elapsed = 0,
	screenshotRequested = false,
	quitAt = nil,
	screenshotName = "autosmoke_plane.png"
}
local altLookState = {
	held = false,
	yaw = 0,
	pitch = 0,
	yawLimit = math.rad(110),
	pitchUpLimit = math.rad(55),
	pitchDownLimit = math.rad(40),
	camera = nil
}
local thirdPersonState = {
	offset = { 0, 2.0, -7.0 },
	minDistance = 1.25,
	collisionBuffer = 0.35,
	camera = nil
}
local mapState = {
	visible = true,
	mHeld = false,
	mUsedForZoom = false,
	zoomIndex = 2,
	zoomExtents = { 140, 400, 1000 },
	panel = { margin = 14, widthRatio = 0.24, maxSize = 280, minSize = 150 },
	logicalCamera = nil,
	mapImage = nil,
	mapImageData = nil,
	mapRes = 0,
	lastCenterX = nil,
	lastCenterZ = nil,
	lastHeading = nil,
	lastExtent = nil,
	lastGroundSignature = nil,
	lastRefreshAt = -math.huge
}
local localPlayerObject = nil
local localCallsign = ""
local geoConfig = {
	originLat = 0.0,
	originLon = 0.0,
	metersPerUnit = 1.0
}
local hudTheme = {
	plateBg = { 0.03, 0.05, 0.08, 0.82 },
	plateHi = { 0.65, 0.82, 1.0, 0.24 },
	plateBorder = { 0.74, 0.9, 1.0, 0.88 },
	text = { 0.9, 0.96, 1.0, 0.95 },
	textDim = { 0.72, 0.84, 0.95, 0.92 },
	accent = { 0.26, 0.9, 0.55, 0.98 },
	needle = { 1.0, 0.36, 0.2, 0.98 },
	overspeed = { 1.0, 0.2, 0.16, 0.95 },
	shadow = { 0, 0, 0, 0.34 }
}
local hudCache = {
	dialCanvas = nil,
	dialSize = 0,
	dialFontHeight = 0,
	dialProfileKey = "",
	controlCanvas = nil,
	controlW = 0,
	controlH = 0
}

local graphicsSettings = {
	windowMode = "windowed",
	resolutionOptions = {},
	resolutionIndex = 1,
	renderScale = 1.0,
	drawDistance = 1800,
	vsync = false,
	graphicsApiPreference = "auto"
}
currentGraphicsApiPreference = "auto"
local graphicsBackend = {
	name = "Unknown",
	version = "",
	vendor = "",
	device = ""
}
bootRestartPayload, bootRestartSnapshotState = nil, nil
RESTART_STATE_VERSION = 1
RESTART_MODEL_ENCODED_LIMIT = 4 * 1024 * 1024

local hudSettings = {
	showSpeedometer = true,
	showThrottle = true,
	showFlightControls = true,
	showMap = true,
	showGeoInfo = true,
	showPeerIndicators = true,
	speedometerMaxKph = 1000,
	speedometerMinorStepKph = 20,
	speedometerMajorStepKph = 100,
	speedometerLabelStepKph = 200,
	speedometerRedlineKph = 850
}

local characterOrientation = {
	plane = {
		yaw = 0,
		pitch = 0,
		roll = 0
	},
	walking = {
		yaw = 0,
		pitch = 0,
		roll = 0
	}
}

sunSettings = {
	yaw = 20,
	pitch = 50,
	intensity = 1.3,
	ambient = 0.16,
	colorR = 1.0,
	colorG = 0.95,
	colorB = 0.86,
	skyR = 0.34,
	skyG = 0.46,
	skyB = 0.68,
	groundR = 0.75,
	groundG = 0.75,
	groundB = 0.75,
	giSpecular = 0.18,
	giBounce = 0.10,
	showMarker = false,
	markerDistance = 1400,
	markerSize = 180
}

sunDebugObject = {
	model = cubeModel,
	pos = { 0, 0, 0 },
	rot = q.identity(),
	scale = { 1, 1, 1 },
	halfSize = { x = 1, y = 1, z = 1 },
	color = { 1.0, 0.95, 0.86, 1.0 },
	isSolid = false,
	materials = {
		{
			baseColorFactor = { 1.0, 0.95, 0.86, 1.0 },
			metallicFactor = 0.0,
			roughnessFactor = 1.0,
			emissiveFactor = { 8.0, 7.0, 5.5 },
			alphaMode = "OPAQUE",
			doubleSided = true
		}
	}
}

local characterPreview = {
	plane = {
		zoom = 1.0,
		autoSpin = true,
		spinRate = 28
	},
	walking = {
		zoom = 1.0,
		autoSpin = true,
		spinRate = 28
	}
}

localGroundClearance = 1.8
modelLoadPrompt = {
	active = false,
	mode = "model_path",
	text = "",
	cursor = 0
}
modelLoadTargetRole = "plane"
playerModelCache = {}
modelTransferState = {
	requestCooldown = 1.4,
	chunkSize = 720,
	maxChunksPerTick = 1,
	transferTimeout = 15.0,
	maxRawBytes = 100 * 1024 * 1024,
	maxEncodedBytes = 200 * 1024 * 1024,
	requestedAt = {},
	outgoing = {},
	incoming = {}
}
local stateNet = {
	sendInterval = 1 / 20,
	lastSentAt = -math.huge,
	forceSend = true
}
local forceStateSync
local worldHalfExtent = 2000
local defaultGroundParams = {
	seed = math.random(0, 999999),
	tileSize = 20,
	gridCount = 128,
	baseHeight = 0,
	tileThickness = 0.005,
	curvature = 0.00000003,
	recenterStep = 48,
	roadCount = 6,
	waterRatio = 0.26,
	roadDensity = 0.10,
	fieldCount = 10,
	fieldMinSize = 40,
	fieldMaxSize = 120,
	grassColor = { 0.20, 0.62, 0.22 },
	waterColor = { 0.10, 0.10, 0.50 },
	roadColor = { 0.10, 0.10, 0.10 },
	fieldColor = { 0.35, 0.45, 0.20 },
	grassVar = { 0.05, 0.10, 0.05 },
	roadVar = { 0.02, 0.02, 0.02 },
	waterVar = { 0.02, 0.02, 0.02 },
	fieldVar = { 0.04, 0.06, 0.04 }
}
local activeGroundParams = nil
local windState = {
	angle = 0,
	speed = 9,
	targetAngle = 0,
	targetSpeed = 9,
	retargetIn = 0
}
local cloudState = {
	groups = {},
	groupCount = 200,
	minAltitude = 1200,
	maxAltitude = 3750,
	spawnRadius = 1400,
	despawnRadius = 1900,
	minGroupSize = 30,
	maxGroupSize = 100,
	minPuffs = 8,
	maxPuffs = 30
}
local drawDistanceChunkingTuning = {
	cloudSpawnRatio = 0.78,
	cloudDespawnRatio = 1.06,
	cloudSpawnMin = 260,
	cloudDespawnPadding = 120
}
local cloudNetState = {
	role = "standalone", -- standalone, pending, authority, follower
	connectedAt = 0,
	decisionDelay = 1.2,
	sawPeerOnJoin = false,
	authorityPeerId = nil,
	lastSnapshotSentAt = -math.huge,
	lastSnapshotReceivedAt = -math.huge,
	lastSnapshotRequestAt = -math.huge,
	snapshotInterval = 10.0,
	requestCooldown = 2.0,
	staleTimeout = 13.5
}
local groundNetState = {
	authorityPeerId = nil,
	lastSnapshotReceivedAt = -math.huge,
	lastSnapshotRequestAt = -math.huge,
	requestCooldown = 2.0
}

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
		renderer.setLighting({
			direction = getSunDirection(),
			color = getSunLightColor(),
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
			bounce = math.max(0, tonumber(sunSettings.giBounce) or 0.24)
		})
	end
end

local pauseMenu = {
	active = false,
	tab = "main",
	subTab = {
		settings = "graphics",
		characters = "plane",
		hud = "speedometer"
	},
	selected = 1,
	itemBounds = {},
	tabBounds = {},
	subTabBounds = {},
	helpScroll = 0,
	controlsScroll = 0,
	controlsSelection = 1,
	controlsSlot = 1,
	controlsRowBounds = {},
	controlsSlotBounds = {},
	listeningBinding = nil,
	dragRange = {
		active = false,
		itemIndex = nil,
		startX = 0,
		lastX = 0,
		totalDx = 0,
		moved = false
	},
	statusText = "",
	statusUntil = 0,
	confirmItemId = nil,
	confirmUntil = 0,
	mainItems = {
		{ id = "resume",    label = "Resume",          kind = "action", help = "Close pause menu and continue playing." },
		{ id = "flight",    label = "Flight Mode",     kind = "toggle", help = "Toggle no-gravity flight movement mode." },
		{ id = "callsign",  label = "Callsign",        kind = "action", help = "Set your multiplayer callsign label shown over your player." },
		{ id = "reset",     label = "Reset Camera",    kind = "action", help = "Reset camera position and orientation." },
		{ id = "reconnect", label = "Reconnect Relay", kind = "action", help = "Reconnect to multiplayer relay server." },
		{ id = "quit",      label = "Quit Game",       kind = "action", confirm = true,                                                      help = "Exit the game client." }
	},
	settingsItems = {
		graphics = {
			{ id = "window_mode",   label = "Window Mode",       kind = "cycle",  help = "Switch between windowed, borderless fullscreen, and exclusive fullscreen." },
			{ id = "resolution",    label = "Resolution",        kind = "cycle",  help = "Target display resolution. In borderless mode this follows desktop resolution." },
			{ id = "apply_video",   label = "Apply Display",     kind = "action", help = "Apply the selected window mode and resolution now." },
			{ id = "render_scale",  label = "Render Scale",      kind = "range",  min = 0.5,                                                                              max = 1.5,   step = 0.05,   help = "Internal 3D render scale separate from display resolution." },
			{ id = "draw_distance", label = "Draw Distance",     kind = "range",  min = 300,                                                                              max = 5000,  step = 100,    help = "How far objects remain rendered from the active camera." },
			{ id = "vsync",         label = "VSync",             kind = "toggle", help = "Toggle vertical sync for display presentation." },
			{ id = "renderer",      label = "Renderer",          kind = "toggle", help = "Switch between GPU and CPU renderer paths." },
			{ id = "graphics_api",  label = "Graphics API",      kind = "cycle",  help = "Choose startup graphics API preference. Applying this restarts the game." },
			{ id = "apply_graphics_api", label = "Apply API", kind = "action", confirm = true, help = "Restart now to apply graphics API changes and restore world/game state." },
			{ id = "fov",           label = "Field of View",     kind = "range",  min = 60,                                                                               max = 120,   step = 5,      help = "Adjust camera FOV in degrees." },
			{ id = "speed",         label = "Move Speed",        kind = "range",  min = 2,                                                                                max = 30,    step = 1,      help = "Adjust player movement speed." },
			{ id = "sensitivity",   label = "Mouse Sensitivity", kind = "range",  min = 0.0004,                                                                           max = 0.004, step = 0.0002, help = "Adjust mouse look sensitivity." },
			{ id = "invertY",       label = "Invert Look Y",     kind = "toggle", help = "Invert vertical mouse look direction." },
			{ id = "crosshair",     label = "Crosshair",         kind = "toggle", help = "Show or hide center crosshair dot." },
			{ id = "overlay",       label = "Debug Overlay",     kind = "toggle", help = "Toggle top-left help/perf text." },
			{ id = "sun_marker",          label = "Sun Marker",      kind = "toggle", help = "Show a large debug cube at the sun direction." },
			{ id = "sun_yaw",             label = "Sun Yaw",         kind = "range",  min = -180, max = 180, step = 2, help = "Rotate sun around world up axis." },
			{ id = "sun_pitch",           label = "Sun Pitch",       kind = "range",  min = -85,  max = 85,  step = 2, help = "Raise/lower sun elevation angle." },
			{ id = "sun_intensity",       label = "Sun Intensity",   kind = "range",  min = 0.0,  max = 8.0, step = 0.1, help = "Direct light intensity for PBR highlights." },
			{ id = "sun_ambient",         label = "Sun Ambient",     kind = "range",  min = 0.0,  max = 1.5, step = 0.02, help = "Ambient fill light to lift dark surfaces." },
			{ id = "sun_marker_distance", label = "Sun Box Dist",    kind = "range",  min = 200,  max = 6000, step = 50, help = "Distance from camera to the sun debug cube." },
			{ id = "sun_marker_size",     label = "Sun Box Size",    kind = "range",  min = 10,   max = 600,  step = 10, help = "Scale of the debug sun cube." }
		}
	},
	characterItems = {
		plane = {
			{ id = "load_custom_stl",         label = "Load Plane Model", kind = "action", help = "Open a path prompt and load STL, GLB, or GLTF from disk (or drop STL/GLB on the window)." },
			{ id = "model_scale",             label = "Plane Scale",    kind = "range",  min = -5.0,                                                                             max = 5.0, step = 0.05, help = "Scale normalized plane models for all clients." },
			{ id = "plane_preview_yaw",       label = "Model Yaw",      kind = "range",  min = -360,                                                                            max = 360, step = 5,    help = "Rotate the real plane model (world + preview) around the vertical axis." },
			{ id = "plane_preview_pitch",     label = "Model Pitch",    kind = "range",  min = -360,                                                                            max = 360, step = 5,    help = "Rotate the real plane model (world + preview) around the lateral axis." },
			{ id = "plane_preview_roll",      label = "Model Roll",     kind = "range",  min = -360,                                                                            max = 360, step = 5,    help = "Rotate the real plane model (world + preview) around the forward axis." },
			{ id = "plane_preview_zoom",      label = "Preview Zoom",   kind = "range",  min = 0.5,                                                                             max = 4.0, step = 0.05, help = "Scale only the preview display model." },
			{ id = "plane_preview_auto_spin", label = "Auto Spin",      kind = "toggle", help = "When enabled, preview yaw rotates automatically." }
		},
		walking = {
			{ id = "load_walking_stl",          label = "Load Walking Model", kind = "action", help = "Load a separate STL, GLB, or GLTF used only when not in flight mode." },
			{ id = "walking_model_scale",       label = "Walking Scale",    kind = "range",  min = 0.3,                                                      max = 3.0, step = 0.05, help = "Scale used for walking-mode player model." },
			{ id = "walking_preview_yaw",       label = "Model Yaw",        kind = "range",  min = -180,                                                     max = 180, step = 5,    help = "Rotate the real walking model (world + preview) around the vertical axis." },
			{ id = "walking_preview_pitch",     label = "Model Pitch",      kind = "range",  min = -180,                                                     max = 180, step = 5,    help = "Rotate the real walking model (world + preview) around the lateral axis." },
			{ id = "walking_preview_roll",      label = "Model Roll",       kind = "range",  min = -180,                                                     max = 180, step = 5,    help = "Rotate the real walking model (world + preview) around the forward axis." },
			{ id = "walking_preview_zoom",      label = "Preview Zoom",     kind = "range",  min = 0.5,                                                      max = 2.0, step = 0.05, help = "Scale only the preview display model." },
			{ id = "walking_preview_auto_spin", label = "Auto Spin",        kind = "toggle", help = "When enabled, preview yaw rotates automatically." }
		}
	},
	hudItems = {
		speedometer = {
			{ id = "hud_speedometer_enabled",    label = "Speedometer",     kind = "toggle", help = "Show/hide the speedometer gauge in flight mode." },
			{ id = "hud_speedometer_max_kph",    label = "Max KPH",         kind = "range",  min = 50,                                                max = 2500, step = 50, help = "Maximum speed represented by the speedometer arc." },
			{ id = "hud_speedometer_minor_step", label = "Minor Tick Step", kind = "range",  min = 1,                                                 max = 200,  step = 5,  help = "KPH step between minor ticks." },
			{ id = "hud_speedometer_major_step", label = "Major Tick Step", kind = "range",  min = 5,                                                 max = 500,  step = 10, help = "KPH step between major ticks." },
			{ id = "hud_speedometer_label_step", label = "Label Step",      kind = "range",  min = 5,                                                 max = 600,  step = 10, help = "KPH step used for numeric labels on major ticks." },
			{ id = "hud_speedometer_redline",    label = "Redline KPH",     kind = "range",  min = 50,                                                max = 2500, step = 50, help = "KPH threshold where overspeed highlighting begins." }
		},
		modules = {
			{ id = "hud_show_throttle",        label = "Throttle Panel",  kind = "toggle", help = "Show/hide the throttle HUD module in flight mode." },
			{ id = "hud_show_controls",        label = "Control Surface", kind = "toggle", help = "Show/hide the flight control/yoke HUD module." },
			{ id = "hud_show_map",             label = "Mini Map",        kind = "toggle", help = "Show/hide the top-right map module." },
			{ id = "hud_show_geo",             label = "Geo Info",        kind = "toggle", help = "Show/hide the LAT/LON/ALT info module." },
			{ id = "hud_show_peer_indicators", label = "Peer Indicators", kind = "toggle", help = "Show/hide world-space labels for network peers." }
		}
	}
}

local controlActions = controls.getActions()

local function resetControlBindingsToDefaults()
	controlActions = controls.resetToDefaults()
	pauseMenu.controlsSelection = math.max(1, math.min(pauseMenu.controlsSelection, #controlActions))
	pauseMenu.controlsSlot = math.max(1, math.min(pauseMenu.controlsSlot, 2))
	pauseMenu.controlsScroll = 0
	pauseMenu.controlsRowBounds = {}
	pauseMenu.controlsSlotBounds = {}
	pauseMenu.listeningBinding = nil
end

local function getPauseHelpSections()
	return {
		{
			title = "Flight Mode",
			items = {
				{ keys = "W / S",           text = "Pitch nose down or up." },
				{ keys = "A / D",           text = "Bank left or right." },
				{ keys = "Mouse X / Q / E", text = "Yaw left or right for quick heading changes." },
				{ keys = "Mouse Y",         text = "Pitch nose up/down." },
				{ keys = "Up / Down",       text = "Throttle up or down." },
				{ keys = "Space / Shift",   text = "Air brakes and afterburner." }
			}
		},
		{
			title = "Walking Mode",
			items = {
				{ keys = "W A S D",     text = "Move and strafe in ground mode." },
				{ keys = "Mouse X / Y", text = "FPS look (yaw + clamped pitch)." },
				{ keys = "Space",       text = "Jump while grounded." },
				{ keys = "Shift",       text = "Sprint while held." }
			}
		},
		{
			title = "Cameras and Map",
			items = {
				{ keys = "C",                text = "Cycle first-person and third-person camera modes." },
				{ keys = "Hold Left Alt",    text = "Temporary alt-look in first-person with head limits." },
				{ keys = "Tap M",            text = "Toggle the top-right mini map overlay." },
				{ keys = "Hold M + Up/Down", text = "Change mini map zoom level without toggling map visibility." }
			}
		},
		{
			title = "Pause Menu",
			items = {
				{ keys = "Tab / H",           text = "Cycle top tabs: Main, Settings, Characters, HUD, Controls, Help." },
				{ keys = "[ / ]",             text = "Cycle sub-pages when available (Characters and HUD)." },
				{ keys = "W/S or Up/Down",    text = "Move selection in list pages or Controls tab." },
				{ keys = "A/D or Left/Right", text = "Adjust selected values or select controls binding slot." },
				{ keys = "Enter",             text = "Activate item or start binding capture in Controls." },
				{ keys = "Mouse Wheel",       text = "Adjust ranges (list pages) or scroll (Help/Controls)." },
				{ keys = "Esc",               text = "Resume game, or cancel bind capture first." }
			}
		}
	}
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
		string.format("%.5f", tonumber(params.tileSize) or 0),
		tostring(params.gridCount or 0),
		tostring(params.roadCount or 0),
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
	local pitch = math.atan2(forward[2], math.max(flatMag, 1e-6))
	local yaw = math.atan2(forward[1], forward[3])

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

local function ensureMapGroundImage(panelSize, mapCam)
	if not mapCam then
		return nil
	end

	local now = love.timer.getTime()
	local targetRes = clamp(math.floor(panelSize), 96, 320)
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
	elseif (now - (mapState.lastRefreshAt or -math.huge)) >= 0.05 then
		local dx = math.abs((mapState.lastCenterX or mapCam.pos[1]) - mapCam.pos[1])
		local dz = math.abs((mapState.lastCenterZ or mapCam.pos[3]) - mapCam.pos[3])
		local headingDelta = math.abs(viewMath.shortestAngleDelta(mapState.lastHeading or mapCam.heading, mapCam.heading))
		local moveThreshold = math.max(2.0, mapCam.extent * 0.02)
		if dx > moveThreshold or dz > moveThreshold or headingDelta > math.rad(1.4) then
			needRefresh = true
		end
	end

	if not needRefresh then
		return mapState.mapImage
	end

	if (not mapState.mapImageData) or mapState.mapRes ~= targetRes then
		local okData, dataOrErr = pcall(function()
			return love.image.newImageData(targetRes, targetRes)
		end)
		if not okData or not dataOrErr then
			mapState.mapImage = nil
			mapState.mapImageData = nil
			mapState.mapRes = 0
			return nil
		end
		mapState.mapImageData = dataOrErr
		mapState.mapRes = targetRes
		mapState.mapImage = nil
	end

	local data = mapState.mapImageData
	local worldPerPixel = (mapCam.extent * 2) / targetRes
	local cosH = math.cos(mapCam.heading)
	local sinH = math.sin(mapCam.heading)
	local colorParams = activeGroundParams or defaultGroundParams
	local halfRes = targetRes * 0.5

	for y = 0, targetRes - 1 do
		for x = 0, targetRes - 1 do
			local mapX = (x + 0.5 - halfRes) * worldPerPixel
			local mapZ = (halfRes - (y + 0.5)) * worldPerPixel
			local worldDX = cosH * mapX + sinH * mapZ
			local worldDZ = -sinH * mapX + cosH * mapZ
			local worldX = mapCam.pos[1] + worldDX
			local worldZ = mapCam.pos[3] + worldDZ
			local c = groundSystem.sampleGroundColorAtWorld(worldX, worldZ, colorParams)
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
			return nil
		end
		mapState.mapImage = imageOrErr
		mapState.mapImage:setFilter("linear", "linear")
	end

	mapState.lastCenterX = mapCam.pos[1]
	mapState.lastCenterZ = mapCam.pos[3]
	mapState.lastHeading = mapCam.heading
	mapState.lastExtent = mapCam.extent
	mapState.lastGroundSignature = groundSig
	mapState.lastRefreshAt = now
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
	local s = scale and tonumber(scale) or 1
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
		return playerWalkingModelHash or "builtin-cube", playerWalkingModelScale or defaultWalkingModelScale
	end
	return playerPlaneModelHash or "builtin-cube", playerPlaneModelScale or defaultPlayerModelScale
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

local function setFlightMode(enabled)
	flightSimMode = enabled and true or false
	if camera then
		camera.yoke = camera.yoke or { pitch = 0, yaw = 0, roll = 0 }
		camera.flightRotVel = camera.flightRotVel or { pitch = 0, yaw = 0, roll = 0 }
		if flightSimMode then
			camera.walkYaw = nil
			camera.walkPitch = nil
			camera.flightVel = camera.flightVel or { 0, 0, 0 }
		else
			camera.throttle = 0
			camera.flightVel = { 0, 0, 0 }
			camera.vel = { 0, 0, 0 }
			camera.yoke.pitch = 0
			camera.yoke.yaw = 0
			camera.yoke.roll = 0
			camera.flightRotVel.pitch = 0
			camera.flightRotVel.yaw = 0
			camera.flightRotVel.roll = 0
			syncWalkingLookFromRotation()
		end
	end
	syncActivePlayerModelState()
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
end

function splitPacketFields(data)
	local parts = {}
	if type(data) ~= "string" then
		return parts
	end

	for part in string.gmatch(data, "([^|]+)") do
		parts[#parts + 1] = part
	end
	return parts
end

local function parsePacketKeyValues(parts, startIndex)
	local kv = {}
	if type(parts) ~= "table" then
		return kv
	end
	for i = startIndex or 2, #parts do
		local key, value = tostring(parts[i] or ""):match("^([^=]+)=(.*)$")
		if key and value then
			kv[key] = value
		end
	end
	return kv
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

local function relayIsConnected()
	if not relayServer then
		return false
	end

	local ok, state = pcall(function()
		return relayServer:state()
	end)
	return ok and state == "connected"
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
	local planeScale = math.max(0.05,
		tonumber(peer.planeModelScale) or ((peer.scale and peer.scale[1]) or defaultPlayerModelScale))
	local walkingScale = math.max(0.05, tonumber(peer.walkingModelScale) or planeScale or defaultWalkingModelScale)
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
	local senderId = tonumber(parts[#parts])
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
	local senderId = tonumber(parts[#parts])

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
	local params = activeGroundParams or groundSystem.normalizeGroundParams(defaultGroundParams, defaultGroundParams)
	local parts = {
		"GROUND_SNAPSHOT",
		formatNetFloat(now),
		tostring(params.seed),
		formatNetFloat(params.tileSize),
		tostring(params.gridCount),
		formatNetFloat(params.baseHeight),
		formatNetFloat(params.tileThickness),
		formatNetFloat(params.curvature),
		formatNetFloat(params.recenterStep),
		tostring(params.roadCount),
		formatNetFloat(params.roadDensity),
		tostring(params.fieldCount),
		tostring(params.fieldMinSize),
		tostring(params.fieldMaxSize),
		encodeColor3Token(params.grassColor),
		encodeColor3Token(params.roadColor),
		encodeColor3Token(params.fieldColor),
		encodeColor3Token(params.grassVar),
		encodeColor3Token(params.roadVar),
		encodeColor3Token(params.fieldVar),
		formatNetFloat(params.waterRatio or defaultGroundParams.waterRatio),
		encodeColor3Token(params.waterColor or defaultGroundParams.waterColor),
		encodeColor3Token(params.waterVar or defaultGroundParams.waterVar)
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

function handleGroundSnapshotPacket(data)
	local parts = splitPacketFields(data)
	if parts[1] ~= "GROUND_SNAPSHOT" then
		return false
	end

	local senderId = tonumber(parts[#parts])
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
	return applyGroundSnapshotParts(parts, senderId)
end

function handleGroundRequestPacket(data)
	local parts = splitPacketFields(data)
	if parts[1] ~= "GROUND_REQUEST" then
		return false
	end

	local senderId = tonumber(parts[#parts])
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

	local senderId = tonumber(parts[#parts])
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

	local senderId = tonumber(parts[#parts])
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

local function refreshUiFonts()
	local baseSize = math.max(14, math.floor(screen.h * 0.023))
	pauseTitleFont = love.graphics.newFont(math.max(24, baseSize + 8))
	pauseItemFont = love.graphics.newFont(baseSize)
end

local setPauseState

local function setPauseStatus(message, duration)
	pauseMenu.statusText = message or ""
	if message and message ~= "" then
		pauseMenu.statusUntil = love.timer.getTime() + (duration or 2.0)
	else
		pauseMenu.statusUntil = 0
	end
end

local function getWrappedLines(font, text, width)
	if not text or text == "" then
		return {}
	end
	local _, lines = font:getWrap(text, width)
	return lines
end

local function trimWrappedLines(lines, maxLines)
	if #lines <= maxLines then
		return lines
	end

	local trimmed = {}
	for i = 1, maxLines do
		trimmed[i] = lines[i]
	end
	trimmed[maxLines] = trimmed[maxLines] .. "..."
	return trimmed
end

local function getPauseTopTabs()
	return {
		{ id = "main",       label = "Main" },
		{ id = "settings",   label = "Settings" },
		{ id = "characters", label = "Characters" },
		{ id = "hud",        label = "HUD" },
		{ id = "controls",   label = "Controls" },
		{ id = "help",       label = "Help" }
	}
end

local function getPauseSubTabs(tabId)
	if tabId == "settings" then
		return {
			{ id = "graphics", label = "Graphics" }
		}
	end
	if tabId == "characters" then
		return {
			{ id = "plane",   label = "Plane" },
			{ id = "walking", label = "Player" }
		}
	end
	if tabId == "hud" then
		return {
			{ id = "speedometer", label = "Speedometer" },
			{ id = "modules",     label = "Modules" }
		}
	end
	return nil
end

local function getPauseActiveSubTab(tabId)
	local tabs = getPauseSubTabs(tabId)
	if not tabs or #tabs == 0 then
		return nil
	end

	local current = pauseMenu.subTab[tabId]
	for _, sub in ipairs(tabs) do
		if sub.id == current then
			return current
		end
	end

	local fallback = tabs[1].id
	pauseMenu.subTab[tabId] = fallback
	return fallback
end

local function getPauseItemsForCurrentPage()
	if pauseMenu.tab == "main" then
		return pauseMenu.mainItems
	end
	if pauseMenu.tab == "settings" then
		local sub = getPauseActiveSubTab("settings")
		return pauseMenu.settingsItems[sub] or {}
	end
	if pauseMenu.tab == "characters" then
		local sub = getPauseActiveSubTab("characters")
		return pauseMenu.characterItems[sub] or {}
	end
	if pauseMenu.tab == "hud" then
		local sub = getPauseActiveSubTab("hud")
		return pauseMenu.hudItems[sub] or {}
	end
	return {}
end

local function clampPauseSelection()
	local items = getPauseItemsForCurrentPage()
	local count = #items
	if count <= 0 then
		pauseMenu.selected = 1
		return
	end
	pauseMenu.selected = clamp(pauseMenu.selected, 1, count)
end

local function getGraphicsWindowModeLabel(modeId)
	if modeId == "borderless" then
		return "Borderless"
	end
	if modeId == "fullscreen" then
		return "Fullscreen"
	end
	return "Windowed"
end

function normalizeGraphicsApiPreference(value)
	if type(value) ~= "string" then
		return "auto"
	end
	local lowered = value:lower()
	if lowered == "vulkan" then
		return "vulkan"
	end
	if lowered == "opengl" or lowered == "gl" then
		return "opengl"
	end
	return "auto"
end

function getGraphicsApiPreferenceLabel(value)
	local preference = normalizeGraphicsApiPreference(value)
	if preference == "vulkan" then
		return "Vulkan"
	end
	if preference == "opengl" then
		return "OpenGL"
	end
	return "Auto"
end

function getEngineRestartPayload()
	if type(love.restart) ~= "table" then
		return nil
	end
	if tonumber(love.restart._l2d3d_restart_version) ~= RESTART_STATE_VERSION then
		return nil
	end
	return love.restart
end

local function refreshGraphicsBackendInfo()
	if not (love.graphics and love.graphics.getRendererInfo) then
		return
	end
	local ok, name, version, vendor, device = pcall(love.graphics.getRendererInfo)
	if not ok then
		return
	end
	graphicsBackend.name = tostring(name or "Unknown")
	graphicsBackend.version = tostring(version or "")
	graphicsBackend.vendor = tostring(vendor or "")
	graphicsBackend.device = tostring(device or "")
end

local function getGraphicsBackendLabel()
	local name = graphicsBackend.name or "Unknown"
	local device = graphicsBackend.device or ""
	if device ~= "" then
		return string.format("%s (%s)", name, device)
	end
	return name
end

local function collectResolutionOptions()
	local options = {}
	local seen = {}

	local function addOption(w, h)
		w = math.floor(tonumber(w) or 0)
		h = math.floor(tonumber(h) or 0)
		if w < 640 or h < 360 then
			return
		end
		local key = tostring(w) .. "x" .. tostring(h)
		if seen[key] then
			return
		end
		seen[key] = true
		options[#options + 1] = {
			w = w,
			h = h,
			label = string.format("%dx%d", w, h)
		}
	end

	local desktopW, desktopH = love.window.getDesktopDimensions()
	addOption(desktopW, desktopH)

	local ok, modes = pcall(function()
		return love.window.getFullscreenModes()
	end)
	if ok and type(modes) == "table" then
		for _, mode in ipairs(modes) do
			addOption(mode.width, mode.height)
		end
	end

	addOption(1920, 1080)
	addOption(1600, 900)
	addOption(1366, 768)
	addOption(1280, 720)
	addOption(1024, 768)
	addOption(800, 600)

	table.sort(options, function(a, b)
		local areaA = a.w * a.h
		local areaB = b.w * b.h
		if areaA == areaB then
			return a.w > b.w
		end
		return areaA > areaB
	end)

	return options
end

local function findClosestResolutionIndex(options, targetW, targetH)
	if type(options) ~= "table" or #options == 0 then
		return 1
	end

	targetW = tonumber(targetW) or options[1].w
	targetH = tonumber(targetH) or options[1].h
	local bestIndex = 1
	local bestScore = math.huge

	for i, opt in ipairs(options) do
		local dw = math.abs(opt.w - targetW)
		local dh = math.abs(opt.h - targetH)
		local score = (dw * dw) + (dh * dh)
		if score < bestScore then
			bestScore = score
			bestIndex = i
		end
	end

	return bestIndex
end

local function syncGraphicsSettingsFromWindow()
	local w, h, flags = love.window.getMode()
	flags = flags or {}

	if flags.fullscreen then
		if flags.fullscreentype == "exclusive" then
			graphicsSettings.windowMode = "fullscreen"
		else
			graphicsSettings.windowMode = "borderless"
		end
	else
		graphicsSettings.windowMode = "windowed"
	end
	graphicsSettings.vsync = (flags.vsync and flags.vsync ~= 0) and true or false
	graphicsSettings.resolutionOptions = collectResolutionOptions()
	graphicsSettings.resolutionIndex = findClosestResolutionIndex(graphicsSettings.resolutionOptions, w, h)
	graphicsSettings.renderScale = clamp(graphicsSettings.renderScale or 1.0, 0.5, 1.5)
	graphicsSettings.drawDistance = clamp(graphicsSettings.drawDistance or 1800, 300, 5000)
	graphicsSettings.graphicsApiPreference = normalizeGraphicsApiPreference(graphicsSettings.graphicsApiPreference)
end

local function getSelectedResolutionOption()
	local options = graphicsSettings.resolutionOptions
	if type(options) ~= "table" or #options == 0 then
		graphicsSettings.resolutionOptions = collectResolutionOptions()
		options = graphicsSettings.resolutionOptions
	end
	graphicsSettings.resolutionIndex = clamp(
		graphicsSettings.resolutionIndex or 1,
		1,
		math.max(1, #options)
	)
	return options[graphicsSettings.resolutionIndex]
end

local function rebuildCpuBuffersToWindow()
	screen.w = love.graphics.getWidth()
	screen.h = love.graphics.getHeight()
	renderer.resize(screen)
	refreshUiFonts()
	resetHudCaches()
	invalidateMapCache()

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
end

local function applyGraphicsVideoMode()
	local mode = graphicsSettings.windowMode
	local selected = getSelectedResolutionOption()
	local targetW = (selected and selected.w) or screen.w
	local targetH = (selected and selected.h) or screen.h
	local desktopW, desktopH = love.window.getDesktopDimensions()
	local flags = {
		vsync = graphicsSettings.vsync and true or false,
		depth = true,
		resizable = true
	}

	if mode == "borderless" then
		flags.fullscreen = true
		flags.fullscreentype = "desktop"
		flags.borderless = true
		targetW, targetH = desktopW, desktopH
	elseif mode == "fullscreen" then
		flags.fullscreen = true
		flags.fullscreentype = "exclusive"
		flags.borderless = false
	else
		flags.fullscreen = false
		flags.fullscreentype = "desktop"
		flags.borderless = false
	end

	local ok, err = love.window.setMode(targetW, targetH, flags)
	if not ok then
		flags.depth = nil
		ok, err = love.window.setMode(targetW, targetH, flags)
	end
	if not ok then
		return false, err
	end

	syncGraphicsSettingsFromWindow()
	refreshGraphicsBackendInfo()
	rebuildCpuBuffersToWindow()
	if renderer.setClipPlanes then
		renderer.setClipPlanes(0.1, graphicsSettings.drawDistance)
	end
	return true
end

function deepCopyPrimitiveTable(value, depth, seen)
	if type(value) ~= "table" then
		return value
	end
	if (depth or 0) <= 0 then
		return nil
	end

	seen = seen or {}
	if seen[value] then
		return nil
	end
	seen[value] = true

	local out = {}
	for key, entry in pairs(value) do
		local keyType = type(key)
		if keyType == "string" or keyType == "number" then
			local entryType = type(entry)
			if entryType == "number" or entryType == "string" or entryType == "boolean" then
				out[key] = entry
			elseif entryType == "table" then
				local nested = deepCopyPrimitiveTable(entry, depth - 1, seen)
				if nested ~= nil then
					out[key] = nested
				end
			end
		end
	end
	return out
end

function buildRestartModelSnapshot(role, hash, scale)
	local snapshot = {
		role = role,
		hash = tostring(hash or "builtin-cube"),
		scale = tonumber(scale) or 1
	}
	local entry = playerModelCache[snapshot.hash]
	if not entry then
		return snapshot
	end

	if type(entry.source) == "string" and entry.source ~= "" then
		snapshot.source = entry.source
	end
	if type(entry.encoded) == "string" and entry.encoded ~= "" and #entry.encoded <= RESTART_MODEL_ENCODED_LIMIT then
		snapshot.encoded = entry.encoded
	end
	return snapshot
end

function buildRestartSnapshot(targetGraphicsApiPreference)
	local selectedResolution = getSelectedResolutionOption()
	local cloudGroups = {}
	for i, group in ipairs(cloudState.groups or {}) do
		local center = group.center or { 0, 0, 0 }
		cloudGroups[i] = {
			center = {
				tonumber(center[1]) or 0,
				tonumber(center[2]) or 0,
				tonumber(center[3]) or 0
			},
			radius = tonumber(group.radius) or 1,
			windDrift = tonumber(group.windDrift) or 1
		}
	end

	local graphicsApiPreference = normalizeGraphicsApiPreference(targetGraphicsApiPreference or
		graphicsSettings.graphicsApiPreference)

	return {
		_l2d3d_restart_version = RESTART_STATE_VERSION,
		graphicsApiPreference = graphicsApiPreference,
		state = {
			flightSimMode = flightSimMode and true or false,
			viewMode = (viewState.mode == "third_person") and "third_person" or "first_person",
			useGpuRenderer = useGpuRenderer and true or false,
			localCallsign = localCallsign or "",
			mouseSensitivity = tonumber(mouseSensitivity) or 0.001,
			invertLookY = invertLookY and true or false,
			showCrosshair = showCrosshair and true or false,
			showDebugOverlay = showDebugOverlay and true or false,
			colorCorruptionMode = colorCorruptionMode and true or false,
			mapState = {
				visible = mapState.visible and true or false,
				zoomIndex = tonumber(mapState.zoomIndex) or 2
			},
			graphicsSettings = {
				windowMode = graphicsSettings.windowMode,
				resolution = selectedResolution and {
					w = selectedResolution.w,
					h = selectedResolution.h
				} or nil,
				renderScale = tonumber(graphicsSettings.renderScale) or 1.0,
				drawDistance = tonumber(graphicsSettings.drawDistance) or 1800,
				vsync = graphicsSettings.vsync and true or false,
				graphicsApiPreference = graphicsApiPreference
			},
			hudSettings = deepCopyPrimitiveTable(hudSettings, 3),
			characterOrientation = deepCopyPrimitiveTable(characterOrientation, 3),
			camera = deepCopyPrimitiveTable({
				pos = camera and camera.pos,
				rot = camera and camera.rot,
				vel = camera and camera.vel,
				flightVel = camera and camera.flightVel,
				flightRotVel = camera and camera.flightRotVel,
				yoke = camera and camera.yoke,
				throttle = camera and camera.throttle,
				speed = camera and camera.speed,
				baseFov = camera and camera.baseFov,
				fov = camera and camera.fov,
				walkYaw = camera and camera.walkYaw,
				walkPitch = camera and camera.walkPitch
			}, 4),
			groundParams = deepCopyPrimitiveTable(activeGroundParams or defaultGroundParams, 3),
			windState = deepCopyPrimitiveTable({
				angle = windState.angle,
				speed = windState.speed,
				targetAngle = windState.targetAngle,
				targetSpeed = windState.targetSpeed,
				retargetIn = windState.retargetIn
			}, 2),
			cloudState = {
				groupCount = tonumber(cloudState.groupCount) or #cloudState.groups,
				minAltitude = tonumber(cloudState.minAltitude) or 120,
				maxAltitude = tonumber(cloudState.maxAltitude) or 375,
				spawnRadius = tonumber(cloudState.spawnRadius) or 1400,
				despawnRadius = tonumber(cloudState.despawnRadius) or 1900,
				minGroupSize = tonumber(cloudState.minGroupSize) or 18,
				maxGroupSize = tonumber(cloudState.maxGroupSize) or 52,
				minPuffs = tonumber(cloudState.minPuffs) or 5,
				maxPuffs = tonumber(cloudState.maxPuffs) or 10,
				groups = cloudGroups
			},
			playerModels = {
				plane = buildRestartModelSnapshot("plane", playerPlaneModelHash, playerPlaneModelScale),
				walking = buildRestartModelSnapshot("walking", playerWalkingModelHash, playerWalkingModelScale)
			}
		}
	}
end

function restoreModelFromRestartSnapshot(role, snapshot)
	if type(snapshot) ~= "table" then
		return
	end

	local targetRole = (role == "walking") and "walking" or "plane"
	local minScale = (targetRole == "walking") and 0.3 or 0.5
	local maxScale = 3.0
	local defaultScale = (targetRole == "walking") and defaultWalkingModelScale or defaultPlayerModelScale
	local restoredScale = clamp(tonumber(snapshot.scale) or defaultScale, minScale, maxScale)
	if targetRole == "walking" then
		playerWalkingModelScale = restoredScale
	else
		playerPlaneModelScale = restoredScale
	end

	local loaded = false
	if type(snapshot.encoded) == "string" and snapshot.encoded ~= "" then
		local raw = decodeModelBytes(snapshot.encoded)
		if type(raw) == "string" and raw ~= "" then
			local ok = loadPlayerModelFromRaw(raw, "restart " .. targetRole, targetRole)
			loaded = ok and true or false
		end
	end

	if (not loaded) and type(snapshot.source) == "string" and snapshot.source ~= "" and snapshot.source ~= "builtin cube" then
		local ok = loadPlayerModelFromPath(snapshot.source, targetRole)
		loaded = ok and true or false
	end

	if (not loaded) and type(snapshot.hash) == "string" then
		local entry = playerModelCache[snapshot.hash]
		if entry then
			setActivePlayerModel(entry, entry.source or ("cache " .. snapshot.hash), targetRole)
			loaded = true
		end
	end

	if not loaded then
		local fallback = playerModelCache["builtin-cube"]
		if fallback then
			setActivePlayerModel(fallback, "builtin cube", targetRole)
		end
	end
end

function applyRestartSnapshot(snapshot)
	if type(snapshot) ~= "table" then
		return false
	end

	if type(snapshot.localCallsign) == "string" then
		localCallsign = snapshot.localCallsign
	end

	mouseSensitivity = tonumber(snapshot.mouseSensitivity) or mouseSensitivity
	invertLookY = snapshot.invertLookY and true or false
	showCrosshair = snapshot.showCrosshair ~= false
	showDebugOverlay = snapshot.showDebugOverlay ~= false
	colorCorruptionMode = snapshot.colorCorruptionMode and true or false

	if type(snapshot.hudSettings) == "table" then
		for key, value in pairs(snapshot.hudSettings) do
			if hudSettings[key] ~= nil then
				hudSettings[key] = value
			end
		end
		resetHudCaches()
	end

	if type(snapshot.characterOrientation) == "table" then
		for _, role in ipairs({ "plane", "walking" }) do
			local source = snapshot.characterOrientation[role]
			if type(source) == "table" then
				local orient = getRoleOrientation(role)
				orient.yaw = tonumber(source.yaw) or orient.yaw
				orient.pitch = tonumber(source.pitch) or orient.pitch
				orient.roll = tonumber(source.roll) or orient.roll
			end
		end
	end

	local desiredGraphics = type(snapshot.graphicsSettings) == "table" and snapshot.graphicsSettings or nil
	if desiredGraphics then
		graphicsSettings.windowMode = (desiredGraphics.windowMode == "fullscreen" or desiredGraphics.windowMode == "borderless")
			and desiredGraphics.windowMode or "windowed"
		graphicsSettings.renderScale = clamp(tonumber(desiredGraphics.renderScale) or graphicsSettings.renderScale, 0.5, 1.5)
		graphicsSettings.drawDistance = clamp(tonumber(desiredGraphics.drawDistance) or graphicsSettings.drawDistance, 300, 5000)
		graphicsSettings.vsync = desiredGraphics.vsync and true or false
		graphicsSettings.graphicsApiPreference = normalizeGraphicsApiPreference(
			desiredGraphics.graphicsApiPreference or graphicsSettings.graphicsApiPreference
		)

		if type(desiredGraphics.resolution) == "table" then
			graphicsSettings.resolutionOptions = collectResolutionOptions()
			graphicsSettings.resolutionIndex = findClosestResolutionIndex(
				graphicsSettings.resolutionOptions,
				desiredGraphics.resolution.w,
				desiredGraphics.resolution.h
			)
		end

		local ok, err = applyGraphicsVideoMode()
		if not ok then
			logger.log("Restart state display restore failed: " .. tostring(err))
		end
	end

	local playerModels = type(snapshot.playerModels) == "table" and snapshot.playerModels or {}
	restoreModelFromRestartSnapshot("plane", playerModels.plane)
	restoreModelFromRestartSnapshot("walking", playerModels.walking)

	local desiredFlightMode = snapshot.flightSimMode and true or false
	setFlightMode(desiredFlightMode)

	local cam = type(snapshot.camera) == "table" and snapshot.camera or nil
	if camera and cam then
		if type(cam.pos) == "table" then
			camera.pos[1] = tonumber(cam.pos[1]) or camera.pos[1]
			camera.pos[2] = tonumber(cam.pos[2]) or camera.pos[2]
			camera.pos[3] = tonumber(cam.pos[3]) or camera.pos[3]
		end
		if type(cam.rot) == "table" then
			camera.rot = {
				w = tonumber(cam.rot.w) or 1,
				x = tonumber(cam.rot.x) or 0,
				y = tonumber(cam.rot.y) or 0,
				z = tonumber(cam.rot.z) or 0
			}
		end
		if type(cam.vel) == "table" then
			camera.vel = {
				tonumber(cam.vel[1]) or 0,
				tonumber(cam.vel[2]) or 0,
				tonumber(cam.vel[3]) or 0
			}
		end
		if type(cam.flightVel) == "table" then
			camera.flightVel = {
				tonumber(cam.flightVel[1]) or 0,
				tonumber(cam.flightVel[2]) or 0,
				tonumber(cam.flightVel[3]) or 0
			}
		end
		if type(cam.yoke) == "table" then
			camera.yoke = {
				pitch = tonumber(cam.yoke.pitch) or 0,
				yaw = tonumber(cam.yoke.yaw) or 0,
				roll = tonumber(cam.yoke.roll) or 0
			}
		end
		if type(cam.flightRotVel) == "table" then
			camera.flightRotVel = {
				pitch = tonumber(cam.flightRotVel.pitch) or 0,
				yaw = tonumber(cam.flightRotVel.yaw) or 0,
				roll = tonumber(cam.flightRotVel.roll) or 0
			}
		end
		camera.throttle = clamp(tonumber(cam.throttle) or camera.throttle, 0, 1)
		camera.speed = tonumber(cam.speed) or camera.speed
		camera.baseFov = tonumber(cam.baseFov) or camera.baseFov
		camera.fov = tonumber(cam.fov) or camera.baseFov
		if not desiredFlightMode then
			if cam.walkYaw ~= nil then
				camera.walkYaw = tonumber(cam.walkYaw) or camera.walkYaw
			end
			if cam.walkPitch ~= nil then
				camera.walkPitch = tonumber(cam.walkPitch) or camera.walkPitch
			end
		end
		if camera.box and camera.box.pos then
			camera.box.pos[1] = camera.pos[1]
			camera.box.pos[2] = camera.pos[2]
			camera.box.pos[3] = camera.pos[3]
		end
	end

	local storedMap = type(snapshot.mapState) == "table" and snapshot.mapState or nil
	if storedMap then
		mapState.visible = storedMap.visible ~= false
		mapState.zoomIndex = clamp(math.floor(tonumber(storedMap.zoomIndex) or mapState.zoomIndex), 1, #mapState.zoomExtents)
		mapState.logicalCamera = nil
		invalidateMapCache()
	end

	local desiredViewMode = snapshot.viewMode
	if desiredViewMode == "third_person" then
		viewState.mode = "third_person"
	else
		viewState.mode = "first_person"
	end
	resetAltLookState()

	if type(snapshot.groundParams) == "table" then
		rebuildGroundFromParams(snapshot.groundParams, "restart restore")
	end

	local storedWind = type(snapshot.windState) == "table" and snapshot.windState or nil
	if storedWind then
		windState.angle = viewMath.wrapAngle(tonumber(storedWind.angle) or windState.angle)
		windState.speed = math.max(0, tonumber(storedWind.speed) or windState.speed)
		windState.targetAngle = viewMath.wrapAngle(tonumber(storedWind.targetAngle) or windState.angle)
		windState.targetSpeed = math.max(0, tonumber(storedWind.targetSpeed) or windState.speed)
		windState.retargetIn = math.max(0, tonumber(storedWind.retargetIn) or 0)
	end

	local storedCloud = type(snapshot.cloudState) == "table" and snapshot.cloudState or nil
	if storedCloud then
		cloudState.groupCount = math.max(1, math.floor(tonumber(storedCloud.groupCount) or cloudState.groupCount))
		cloudState.minAltitude = math.min(
			tonumber(storedCloud.minAltitude) or cloudState.minAltitude,
			tonumber(storedCloud.maxAltitude) or cloudState.maxAltitude
		)
		cloudState.maxAltitude = math.max(
			tonumber(storedCloud.minAltitude) or cloudState.minAltitude,
			tonumber(storedCloud.maxAltitude) or cloudState.maxAltitude
		)
		cloudState.spawnRadius = math.max(10, tonumber(storedCloud.spawnRadius) or cloudState.spawnRadius)
		cloudState.despawnRadius = math.max(cloudState.spawnRadius + 10,
			tonumber(storedCloud.despawnRadius) or cloudState.despawnRadius)
		cloudState.minGroupSize = math.max(1, tonumber(storedCloud.minGroupSize) or cloudState.minGroupSize)
		cloudState.maxGroupSize = math.max(cloudState.minGroupSize,
			tonumber(storedCloud.maxGroupSize) or cloudState.maxGroupSize)
		cloudState.minPuffs = math.max(1, math.floor(tonumber(storedCloud.minPuffs) or cloudState.minPuffs))
		cloudState.maxPuffs = math.max(cloudState.minPuffs, math.floor(tonumber(storedCloud.maxPuffs) or cloudState.maxPuffs))
		syncChunkingWithDrawDistance(false)

		local now = love.timer.getTime()
		cloudSim.spawnCloudField(cloudState, objects, camera, windState, cloudPuffModel, q, cloudState.groupCount, now)

		if type(storedCloud.groups) == "table" then
			for i, groupSnapshot in ipairs(storedCloud.groups) do
				local group = cloudState.groups[i]
				if group and type(groupSnapshot) == "table" then
					local center = groupSnapshot.center or {}
					group.center[1] = tonumber(center[1]) or group.center[1]
					group.center[2] = tonumber(center[2]) or group.center[2]
					group.center[3] = tonumber(center[3]) or group.center[3]
					group.radius = math.max(1, tonumber(groupSnapshot.radius) or group.radius)
					group.windDrift = tonumber(groupSnapshot.windDrift) or group.windDrift
					cloudSim.updateCloudGroupVisuals(group, now)
				end
			end
		end
	end
	syncChunkingWithDrawDistance(false)

	if snapshot.useGpuRenderer ~= nil then
		pendingRestartRendererPreference = snapshot.useGpuRenderer and true or false
	end

	syncLocalPlayerObject()
	resolveActiveRenderCamera()
	forceStateSync()
	return true
end

function requestGraphicsApiRestart(targetGraphicsApiPreference)
	local targetPreference = normalizeGraphicsApiPreference(targetGraphicsApiPreference)
	local snapshot = buildRestartSnapshot(targetPreference)
	logger.log("Applying graphics API preference '" .. targetPreference .. "' via in-process restart.")
	local ok, err = pcall(function()
		love.event.restart(snapshot)
	end)
	if not ok then
		logger.log("Restart failed: " .. tostring(err))
		return false, err
	end
	return true
end

local function cyclePauseSubTab(direction)
	local tabs = getPauseSubTabs(pauseMenu.tab)
	if not tabs or #tabs == 0 or direction == 0 then
		return
	end
	local current = getPauseActiveSubTab(pauseMenu.tab)
	local index = 1
	for i, sub in ipairs(tabs) do
		if sub.id == current then
			index = i
			break
		end
	end
	local step = direction > 0 and 1 or -1
	local nextIndex = ((index - 1 + step) % #tabs) + 1
	pauseMenu.subTab[pauseMenu.tab] = tabs[nextIndex].id
	pauseMenu.selected = 1
	pauseMenu.dragRange.active = false
	pauseMenu.dragRange.itemIndex = nil
	pauseMenu.dragRange.startX = 0
	pauseMenu.dragRange.lastX = 0
	pauseMenu.dragRange.totalDx = 0
	pauseMenu.dragRange.moved = false
	pauseMenu.confirmItemId = nil
	pauseMenu.confirmUntil = 0
end

local function getSelectedControlAction()
	local index = clamp(pauseMenu.controlsSelection, 1, #controlActions)
	return controlActions[index], index
end

local function clearControlBindingCapture()
	pauseMenu.listeningBinding = nil
end

local function setControlBinding(actionIndex, slotIndex, binding)
	local action = controlActions[actionIndex]
	if not action then
		return false
	end

	slotIndex = clamp(slotIndex or 1, 1, 2)
	action.bindings = action.bindings or {}
	action.bindings[slotIndex] = controls.cloneBinding(binding)
	return true
end

local function clearSelectedControlBinding()
	local action, actionIndex = getSelectedControlAction()
	if not action then
		return
	end

	local slotIndex = clamp(pauseMenu.controlsSlot, 1, 2)
	action.bindings = action.bindings or {}
	action.bindings[slotIndex] = nil
	clearControlBindingCapture()
	setPauseStatus(action.label .. " slot " .. tostring(slotIndex) .. " cleared.", 1.6)
end

local function beginSelectedControlBindingCapture(actionIndex, slotIndex)
	local action = controlActions[actionIndex]
	if not action then
		return
	end

	pauseMenu.controlsSelection = actionIndex
	pauseMenu.controlsSlot = clamp(slotIndex or pauseMenu.controlsSlot, 1, 2)
	pauseMenu.listeningBinding = {
		actionIndex = actionIndex,
		slot = pauseMenu.controlsSlot
	}
	setPauseStatus("Listening: " .. action.label .. " slot " .. tostring(pauseMenu.controlsSlot) .. ".", 6.0)
end

local function commitControlBindingCapture(binding)
	local listen = pauseMenu.listeningBinding
	if not listen then
		return false
	end

	if setControlBinding(listen.actionIndex, listen.slot, binding) then
		local action = controlActions[listen.actionIndex]
		clearControlBindingCapture()
		if action then
			local label = controls.formatBinding(binding)
			setPauseStatus(action.label .. " -> " .. label, 2.0)
		end
		return true
	end

	clearControlBindingCapture()
	return false
end

local function cancelControlBindingCapture()
	if not pauseMenu.listeningBinding then
		return false
	end
	clearControlBindingCapture()
	setPauseStatus("Binding capture cancelled.", 1.3)
	return true
end

local function moveControlsSelection(delta)
	local count = #controlActions
	if count <= 0 then
		return
	end
	pauseMenu.controlsSelection = ((pauseMenu.controlsSelection - 1 + delta) % count) + 1
end

local function moveControlsSlot(delta)
	if delta == 0 then
		return
	end
	local nextSlot = pauseMenu.controlsSlot + (delta > 0 and 1 or -1)
	if nextSlot < 1 then
		nextSlot = 2
	elseif nextSlot > 2 then
		nextSlot = 1
	end
	pauseMenu.controlsSlot = nextSlot
end

local function resetControlsSelectionState()
	pauseMenu.controlsSelection = clamp(pauseMenu.controlsSelection, 1, #controlActions)
	pauseMenu.controlsSlot = clamp(pauseMenu.controlsSlot, 1, 2)
	pauseMenu.controlsScroll = 0
	pauseMenu.controlsRowBounds = {}
	pauseMenu.controlsSlotBounds = {}
	clearControlBindingCapture()
end

local function captureBindingFromKey(key)
	if key == "escape" then
		return false
	end
	local binding = controls.bindKey(key, controls.extractCaptureModifiers(key))
	return commitControlBindingCapture(binding)
end

local function captureBindingFromMouseButton(button)
	local binding = controls.bindMouseButton(button, controls.extractCaptureModifiers(nil))
	return commitControlBindingCapture(binding)
end

local function captureBindingFromWheel(y)
	local direction = y > 0 and 1 or -1
	local binding = controls.bindMouseWheel(direction, controls.extractCaptureModifiers(nil))
	return commitControlBindingCapture(binding)
end

local function captureBindingFromMouseMotion(dx, dy)
	local absX = math.abs(dx)
	local absY = math.abs(dy)
	local threshold = 5
	if absX < threshold and absY < threshold then
		return false
	end

	local axis, direction
	if absX >= absY then
		axis = "x"
		direction = dx >= 0 and 1 or -1
	else
		axis = "y"
		direction = dy >= 0 and 1 or -1
	end

	local binding = controls.bindMouseAxis(axis, direction, controls.extractCaptureModifiers(nil))
	return commitControlBindingCapture(binding)
end

local function buildPauseMenuLayout()
	local titleFont = pauseTitleFont or love.graphics.getFont()
	local itemFont = pauseItemFont or love.graphics.getFont()
	local currentItems = getPauseItemsForCurrentPage()
	clampPauseSelection()
	local isListTab = pauseMenu.tab ~= "help" and pauseMenu.tab ~= "controls"
	local isHelpTab = pauseMenu.tab == "help"
	local isControlsTab = pauseMenu.tab == "controls"
	local itemCount = #currentItems
	local subTabs = getPauseSubTabs(pauseMenu.tab)
	local hasSubTabs = subTabs and #subTabs > 0

	local panelW = math.min(880, screen.w * 0.9)
	local panelX = (screen.w - panelW) / 2
	local contentX = panelX + 18
	local contentW = panelW - 36
	local footerTextW = contentW
	local controlsText
	if modelLoadPrompt.active then
		controlsText = "Model path entry active: type full path and press Enter, or Esc to cancel."
	elseif isListTab then
		if hasSubTabs then
			controlsText = "Tab/H top tabs | [ ] sub-pages | Up/Down select | Left/Right adjust | Esc resume"
		else
			controlsText = "Tab/H switch tabs | Up/Down select | Left/Right adjust | Enter apply | Esc resume"
		end
	elseif isControlsTab then
		if pauseMenu.listeningBinding then
			controlsText = "Listening: press a key/button, move mouse, or use wheel | Esc cancel"
		else
			controlsText = "Enter listen | Left/Right slot | Backspace clear | R reset defaults | Esc resume"
		end
	else
		controlsText = "Tab/H switch tabs | Mouse wheel scroll | [ ] sub-pages | Esc resume"
	end

	local selectedItem = currentItems[pauseMenu.selected]
	local selectedControl = controlActions[pauseMenu.controlsSelection]
	local detailText = ""
	if isListTab then
		detailText = selectedItem and selectedItem.help or ""
	elseif isControlsTab then
		detailText = selectedControl and selectedControl.description or ""
	end
	local statusText = pauseMenu.statusText

	local lineH = itemFont:getHeight()
	local titleTopPad = 14
	local titleH = titleFont:getHeight()
	local tabH = lineH + 8
	local subTabH = hasSubTabs and (lineH + 6) or 0
	local topPad = titleTopPad + titleH + 8 + tabH + (hasSubTabs and (subTabH + 8) or 0) + 10
	local footerTopPad = 10
	local footerGap = 6
	local footerBottomPad = 10
	local panelY = math.floor(math.max(10, screen.h * 0.07))
	local maxPanelH = math.max(220, screen.h - panelY - 8)

	local controlsLines = trimWrappedLines(getWrappedLines(itemFont, controlsText, footerTextW), 2)
	local detailLines = trimWrappedLines(getWrappedLines(itemFont, detailText, footerTextW), 2)
	local statusLines = trimWrappedLines(getWrappedLines(itemFont, statusText, footerTextW), 2)
	local reservedFooterLines = 6
	local reservedFooterContentH = (reservedFooterLines * lineH) + (footerGap * 2)
	local bottomPad = footerTopPad + reservedFooterContentH + footerBottomPad

	local availableContentH = math.max(1, maxPanelH - topPad - bottomPad)
	local rowH = 0
	local contentH = availableContentH
	if isListTab then
		local minRowH = math.max(lineH + 8, 24)
		local maxRowH = 40
		if itemCount <= 0 then
			rowH = minRowH
			contentH = minRowH * 2
		elseif availableContentH >= minRowH * itemCount then
			rowH = clamp(math.floor(availableContentH / itemCount), minRowH, maxRowH)
			contentH = math.max(1, rowH * itemCount)
		else
			rowH = math.max(lineH + 4, math.floor(availableContentH / itemCount))
			contentH = math.max(1, rowH * itemCount)
		end
	else
		contentH = math.max(lineH * 8, availableContentH)
	end

	local panelH = topPad + contentH + bottomPad
	if panelH > maxPanelH then
		panelH = maxPanelH
	end

	local finalContentH = math.max(1, panelH - topPad - bottomPad)
	if isListTab and itemCount > 0 then
		rowH = math.max(1, math.floor(finalContentH / itemCount))
		finalContentH = math.max(1, rowH * itemCount)
		panelH = topPad + finalContentH + bottomPad
		if panelH > maxPanelH then
			panelH = maxPanelH
			finalContentH = math.max(1, panelH - topPad - bottomPad)
			rowH = math.max(1, math.floor(finalContentH / itemCount))
			finalContentH = rowH * itemCount
			panelH = topPad + finalContentH + bottomPad
		end
	end

	local titleY = panelY + titleTopPad
	local tabY = titleY + titleH + 8
	local subTabY = tabY + tabH + 8
	local contentY = panelY + topPad
	local rowX = contentX
	local rowW = contentW
	local previewX, previewY, previewW, previewH = nil, nil, nil, nil
	if pauseMenu.tab == "characters" and isListTab then
		rowW = math.max(220, math.floor(contentW * 0.56))
		previewX = rowX + rowW + 8
		previewY = contentY + 4
		previewW = math.max(120, contentW - rowW - 8)
		previewH = math.max(80, finalContentH - 8)
	end

	return {
		isListTab = isListTab,
		isHelpTab = isHelpTab,
		isControlsTab = isControlsTab,
		hasSubTabs = hasSubTabs,
		subTabY = subTabY,
		subTabH = subTabH,
		panelX = panelX,
		panelY = panelY,
		panelW = panelW,
		panelH = panelH,
		topPad = topPad,
		bottomPad = bottomPad,
		titleY = titleY,
		tabY = tabY,
		tabH = tabH,
		contentX = contentX,
		contentY = contentY,
		contentW = contentW,
		contentH = finalContentH,
		rowX = rowX,
		rowW = rowW,
		rowH = rowH,
		previewX = previewX,
		previewY = previewY,
		previewW = previewW,
		previewH = previewH,
		footerTopPad = footerTopPad,
		footerGap = footerGap,
		controlsLines = controlsLines,
		detailLines = detailLines,
		statusLines = statusLines,
		lineH = lineH
	}
end

local function clearPauseConfirm()
	pauseMenu.confirmItemId = nil
	pauseMenu.confirmUntil = 0
end

local function clearPauseRangeDrag()
	pauseMenu.dragRange.active = false
	pauseMenu.dragRange.itemIndex = nil
	pauseMenu.dragRange.startX = 0
	pauseMenu.dragRange.lastX = 0
	pauseMenu.dragRange.totalDx = 0
	pauseMenu.dragRange.moved = false
end

local function setPauseTab(tab)
	local valid = false
	for _, entry in ipairs(getPauseTopTabs()) do
		if entry.id == tab then
			valid = true
			break
		end
	end
	if not valid then
		return
	end
	if pauseMenu.tab == tab then
		return
	end

	pauseMenu.tab = tab
	getPauseActiveSubTab(tab)
	pauseMenu.helpScroll = 0
	pauseMenu.controlsScroll = 0
	pauseMenu.selected = 1
	pauseMenu.itemBounds = {}
	pauseMenu.controlsRowBounds = {}
	pauseMenu.controlsSlotBounds = {}
	pauseMenu.tabBounds = {}
	pauseMenu.subTabBounds = {}
	clearPauseRangeDrag()
	clearPauseConfirm()
	if tab == "controls" then
		resetControlsSelectionState()
	else
		clearControlBindingCapture()
	end
	clampPauseSelection()
end

local function cyclePauseTab(direction)
	if direction == 0 then
		return
	end
	local tabs = getPauseTopTabs()
	local currentIndex = 1
	for i, entry in ipairs(tabs) do
		if pauseMenu.tab == entry.id then
			currentIndex = i
			break
		end
	end

	local step = direction > 0 and 1 or -1
	local nextIndex = ((currentIndex - 1 + step) % #tabs) + 1
	setPauseTab(tabs[nextIndex].id)
end

local function isPauseConfirmActive(itemId)
	return pauseMenu.confirmItemId == itemId and love.timer.getTime() <= pauseMenu.confirmUntil
end

local function getPauseItemValue(item)
	if not item then
		return ""
	end
	if item.id == "flight" then
		return flightSimMode and "On" or "Off"
	end
	if item.id == "renderer" then
		if renderer.isReady() then
			return useGpuRenderer and "GPU" or "CPU"
		end
		return "CPU (GPU unavailable)"
	end
	if item.id == "graphics_api" then
		local prefLabel = getGraphicsApiPreferenceLabel(graphicsSettings.graphicsApiPreference)
		if normalizeGraphicsApiPreference(graphicsSettings.graphicsApiPreference) ~=
			normalizeGraphicsApiPreference(currentGraphicsApiPreference) then
			return prefLabel .. " (pending restart)"
		end
		return prefLabel .. " (active)"
	end
	if item.id == "apply_graphics_api" then
		if normalizeGraphicsApiPreference(graphicsSettings.graphicsApiPreference) ~=
			normalizeGraphicsApiPreference(currentGraphicsApiPreference) then
			return "Restart Now"
		end
		return "No Changes"
	end
	if item.id == "crosshair" then
		return showCrosshair and "On" or "Off"
	end
	if item.id == "overlay" then
		return showDebugOverlay and "On" or "Off"
	end
	if item.id == "window_mode" then
		return getGraphicsWindowModeLabel(graphicsSettings.windowMode)
	end
	if item.id == "resolution" then
		if graphicsSettings.windowMode == "borderless" then
			local dw, dh = love.window.getDesktopDimensions()
			return string.format("%dx%d (Desktop)", dw, dh)
		end
		local option = getSelectedResolutionOption()
		return option and option.label or "Auto"
	end
	if item.id == "apply_video" then
		return "Apply"
	end
	if item.id == "render_scale" then
		return string.format("%.2fx", graphicsSettings.renderScale or 1.0)
	end
	if item.id == "draw_distance" then
		return string.format("%dm", math.floor((graphicsSettings.drawDistance or 1800) + 0.5))
	end
	if item.id == "vsync" then
		return graphicsSettings.vsync and "On" or "Off"
	end
	if item.id == "callsign" then
		return (localCallsign and localCallsign ~= "") and localCallsign or "Unset"
	end
	if item.id == "load_custom_stl" then
		return "Open..."
	end
	if item.id == "load_walking_stl" then
		return "Open..."
	end
	if item.id == "model_scale" then
		return string.format("%.2fx", playerPlaneModelScale)
	end
	if item.id == "walking_model_scale" then
		return string.format("%.2fx", playerWalkingModelScale)
	end
	if item.id == "plane_preview_yaw" then
		return string.format("%d deg", math.floor((getRoleOrientation("plane").yaw or 0) + 0.5))
	end
	if item.id == "plane_preview_pitch" then
		return string.format("%d deg", math.floor((getRoleOrientation("plane").pitch or 0) + 0.5))
	end
	if item.id == "plane_preview_roll" then
		return string.format("%d deg", math.floor((getRoleOrientation("plane").roll or 0) + 0.5))
	end
	if item.id == "plane_preview_zoom" then
		return string.format("%.2fx", characterPreview.plane.zoom or 1.0)
	end
	if item.id == "plane_preview_auto_spin" then
		return characterPreview.plane.autoSpin and "On" or "Off"
	end
	if item.id == "walking_preview_yaw" then
		return string.format("%d deg", math.floor((getRoleOrientation("walking").yaw or 0) + 0.5))
	end
	if item.id == "walking_preview_pitch" then
		return string.format("%d deg", math.floor((getRoleOrientation("walking").pitch or 0) + 0.5))
	end
	if item.id == "walking_preview_roll" then
		return string.format("%d deg", math.floor((getRoleOrientation("walking").roll or 0) + 0.5))
	end
	if item.id == "walking_preview_zoom" then
		return string.format("%.2fx", characterPreview.walking.zoom or 1.0)
	end
	if item.id == "walking_preview_auto_spin" then
		return characterPreview.walking.autoSpin and "On" or "Off"
	end
	if item.id == "speed" and camera then
		return string.format("%.1f", camera.speed)
	end
	if item.id == "fov" and camera then
		local fovDeg = math.deg(camera.baseFov or camera.fov)
		return string.format("%d deg", math.floor(fovDeg + 0.5))
	end
	if item.id == "sensitivity" then
		return string.format("%.4f", mouseSensitivity)
	end
	if item.id == "sun_marker" then
		return sunSettings.showMarker and "On" or "Off"
	end
	if item.id == "sun_yaw" then
		return string.format("%.0f deg", tonumber(sunSettings.yaw) or 0)
	end
	if item.id == "sun_pitch" then
		return string.format("%.0f deg", tonumber(sunSettings.pitch) or 0)
	end
	if item.id == "sun_intensity" then
		return string.format("%.2f", tonumber(sunSettings.intensity) or 0)
	end
	if item.id == "sun_ambient" then
		return string.format("%.2f", tonumber(sunSettings.ambient) or 0)
	end
	if item.id == "sun_marker_distance" then
		return string.format("%dm", math.floor((tonumber(sunSettings.markerDistance) or 0) + 0.5))
	end
	if item.id == "sun_marker_size" then
		return string.format("%du", math.floor((tonumber(sunSettings.markerSize) or 0) + 0.5))
	end
	if item.id == "invertY" then
		return invertLookY and "On" or "Off"
	end
	if item.id == "hud_speedometer_enabled" then
		return hudSettings.showSpeedometer and "On" or "Off"
	end
	if item.id == "hud_speedometer_max_kph" then
		return string.format("%d kph", math.floor(hudSettings.speedometerMaxKph or 1000))
	end
	if item.id == "hud_speedometer_minor_step" then
		return string.format("%d", math.floor(hudSettings.speedometerMinorStepKph or 20))
	end
	if item.id == "hud_speedometer_major_step" then
		return string.format("%d", math.floor(hudSettings.speedometerMajorStepKph or 100))
	end
	if item.id == "hud_speedometer_label_step" then
		return string.format("%d", math.floor(hudSettings.speedometerLabelStepKph or 200))
	end
	if item.id == "hud_speedometer_redline" then
		return string.format("%d kph", math.floor(hudSettings.speedometerRedlineKph or 850))
	end
	if item.id == "hud_show_throttle" then
		return hudSettings.showThrottle and "On" or "Off"
	end
	if item.id == "hud_show_controls" then
		return hudSettings.showFlightControls and "On" or "Off"
	end
	if item.id == "hud_show_map" then
		return hudSettings.showMap and "On" or "Off"
	end
	if item.id == "hud_show_geo" then
		return hudSettings.showGeoInfo and "On" or "Off"
	end
	if item.id == "hud_show_peer_indicators" then
		return hudSettings.showPeerIndicators and "On" or "Off"
	end
	if item.id == "reconnect" then
		return getRelayStatus()
	end
	if item.id == "apply_graphics_api" and isPauseConfirmActive("apply_graphics_api") then
		return "Are you Sure?"
	end
	if item.id == "quit" and isPauseConfirmActive("quit") then
		return "Are you Sure?"
	end
	return ""
end

local function adjustPauseItem(item, direction, multiplier)
	if direction == 0 then
		return
	end

	local scale = multiplier or 1
	local stepDirection = direction > 0 and 1 or -1

	if item.id == "window_mode" then
		local modes = { "windowed", "borderless", "fullscreen" }
		local index = 1
		for i, modeId in ipairs(modes) do
			if graphicsSettings.windowMode == modeId then
				index = i
				break
			end
		end
		local nextIndex = ((index - 1 + stepDirection) % #modes) + 1
		graphicsSettings.windowMode = modes[nextIndex]
		setPauseStatus("Window Mode: " .. getPauseItemValue(item) .. " (select Apply Display).", 1.8)
		return
	end

	if item.id == "resolution" then
		local options = graphicsSettings.resolutionOptions
		if type(options) ~= "table" or #options == 0 then
			syncGraphicsSettingsFromWindow()
			options = graphicsSettings.resolutionOptions
		end
		if #options > 0 then
			graphicsSettings.resolutionIndex = ((graphicsSettings.resolutionIndex - 1 + stepDirection) % #options) + 1
			setPauseStatus("Resolution: " .. getPauseItemValue(item) .. " (select Apply Display).", 1.8)
		end
		return
	end

	if item.id == "graphics_api" then
		local options = { "auto", "vulkan", "opengl" }
		local current = normalizeGraphicsApiPreference(graphicsSettings.graphicsApiPreference)
		local index = 1
		for i, option in ipairs(options) do
			if current == option then
				index = i
				break
			end
		end
		local nextIndex = ((index - 1 + stepDirection) % #options) + 1
		graphicsSettings.graphicsApiPreference = options[nextIndex]
		setPauseStatus(
			"Graphics API: " .. getPauseItemValue(item) ..
			" (select Apply API to restart and switch backend).",
			2.4
		)
		return
	end

	if item.id == "render_scale" then
		local nextScale = clamp((graphicsSettings.renderScale or 1.0) + item.step * direction * scale, item.min, item
			.max)
		graphicsSettings.renderScale = math.floor(nextScale * 100 + 0.5) / 100
		setPauseStatus("Render Scale: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "draw_distance" then
		graphicsSettings.drawDistance = clamp(
			(graphicsSettings.drawDistance or 1800) + item.step * direction * scale,
			item.min,
			item.max
		)
		if renderer.setClipPlanes then
			renderer.setClipPlanes(0.1, graphicsSettings.drawDistance)
		end
		syncChunkingWithDrawDistance(true)
		setPauseStatus("Draw Distance: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "speed" and camera then
		camera.speed = clamp(camera.speed + item.step * direction * scale, item.min, item.max)
		setPauseStatus("Move Speed: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "model_scale" then
		playerPlaneModelScale = clamp(playerPlaneModelScale + item.step * direction * scale, item.min, item.max)
		playerPlaneModelScale = math.floor(playerPlaneModelScale * 100 + 0.5) / 100
		if getActiveModelRole() == "plane" then
			syncActivePlayerModelState()
		end
		setPauseStatus("Plane Scale: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "walking_model_scale" then
		playerWalkingModelScale = clamp(playerWalkingModelScale + item.step * direction * scale, item.min, item.max)
		playerWalkingModelScale = math.floor(playerWalkingModelScale * 100 + 0.5) / 100
		if getActiveModelRole() == "walking" then
			syncActivePlayerModelState()
		end
		setPauseStatus("Walking Scale: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "plane_preview_yaw" then
		local orient = getRoleOrientation("plane")
		orient.yaw = clamp((orient.yaw or 0) + item.step * direction * scale, item.min, item.max)
		onRoleOrientationChanged("plane")
		setPauseStatus("Plane Model Yaw: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "plane_preview_pitch" then
		local orient = getRoleOrientation("plane")
		orient.pitch = clamp((orient.pitch or 0) + item.step * direction * scale, item.min, item.max)
		onRoleOrientationChanged("plane")
		setPauseStatus("Plane Model Pitch: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "plane_preview_roll" then
		local orient = getRoleOrientation("plane")
		orient.roll = clamp((orient.roll or 0) + item.step * direction * scale, item.min, item.max)
		onRoleOrientationChanged("plane")
		setPauseStatus("Plane Model Roll: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "plane_preview_zoom" then
		local value = clamp(characterPreview.plane.zoom + item.step * direction * scale, item.min, item.max)
		characterPreview.plane.zoom = math.floor(value * 100 + 0.5) / 100
		setPauseStatus("Plane Preview Zoom: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "walking_preview_yaw" then
		local orient = getRoleOrientation("walking")
		orient.yaw = clamp((orient.yaw or 0) + item.step * direction * scale, item.min, item.max)
		onRoleOrientationChanged("walking")
		setPauseStatus("Walking Model Yaw: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "walking_preview_pitch" then
		local orient = getRoleOrientation("walking")
		orient.pitch = clamp((orient.pitch or 0) + item.step * direction * scale, item.min, item.max)
		onRoleOrientationChanged("walking")
		setPauseStatus("Walking Model Pitch: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "walking_preview_roll" then
		local orient = getRoleOrientation("walking")
		orient.roll = clamp((orient.roll or 0) + item.step * direction * scale, item.min, item.max)
		onRoleOrientationChanged("walking")
		setPauseStatus("Walking Model Roll: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "walking_preview_zoom" then
		local value = clamp(characterPreview.walking.zoom + item.step * direction * scale, item.min, item.max)
		characterPreview.walking.zoom = math.floor(value * 100 + 0.5) / 100
		setPauseStatus("Player Preview Zoom: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "fov" and camera then
		local fovDeg = math.deg(camera.baseFov or camera.fov)
		fovDeg = clamp(fovDeg + item.step * direction * scale, item.min, item.max)
		camera.baseFov = math.rad(fovDeg)
		camera.fov = camera.baseFov
		setPauseStatus("Field of View: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "sensitivity" then
		local nextValue = clamp(mouseSensitivity + item.step * direction * scale, item.min, item.max)
		mouseSensitivity = math.floor(nextValue * 10000 + 0.5) / 10000
		setPauseStatus("Mouse Sensitivity: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "sun_yaw" then
		sunSettings.yaw = clamp((tonumber(sunSettings.yaw) or 0) + item.step * direction * scale, item.min, item.max)
		applySunSettingsToRenderer()
		setPauseStatus("Sun Yaw: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "sun_pitch" then
		sunSettings.pitch = clamp((tonumber(sunSettings.pitch) or 0) + item.step * direction * scale, item.min, item.max)
		applySunSettingsToRenderer()
		setPauseStatus("Sun Pitch: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "sun_intensity" then
		sunSettings.intensity = clamp((tonumber(sunSettings.intensity) or 0) + item.step * direction * scale, item.min,
			item.max)
		sunSettings.intensity = math.floor(sunSettings.intensity * 100 + 0.5) / 100
		applySunSettingsToRenderer()
		setPauseStatus("Sun Intensity: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "sun_ambient" then
		sunSettings.ambient = clamp((tonumber(sunSettings.ambient) or 0) + item.step * direction * scale, item.min, item
			.max)
		sunSettings.ambient = math.floor(sunSettings.ambient * 100 + 0.5) / 100
		applySunSettingsToRenderer()
		setPauseStatus("Sun Ambient: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "sun_marker_distance" then
		sunSettings.markerDistance = clamp(
			(tonumber(sunSettings.markerDistance) or 0) + item.step * direction * scale,
			item.min,
			item.max
		)
		sunSettings.markerDistance = math.floor(sunSettings.markerDistance + 0.5)
		setPauseStatus("Sun Box Dist: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "sun_marker_size" then
		sunSettings.markerSize = clamp(
			(tonumber(sunSettings.markerSize) or 0) + item.step * direction * scale,
			item.min,
			item.max
		)
		sunSettings.markerSize = math.floor(sunSettings.markerSize + 0.5)
		setPauseStatus("Sun Box Size: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "hud_speedometer_max_kph" then
		hudSettings.speedometerMaxKph = clamp(
			hudSettings.speedometerMaxKph + item.step * direction * scale,
			item.min,
			item.max
		)
		hudSettings.speedometerRedlineKph = clamp(
			hudSettings.speedometerRedlineKph,
			item.min,
			hudSettings.speedometerMaxKph
		)
		hudSettings.speedometerMinorStepKph = clamp(
			hudSettings.speedometerMinorStepKph,
			5,
			hudSettings.speedometerMaxKph
		)
		hudSettings.speedometerMajorStepKph = clamp(
			hudSettings.speedometerMajorStepKph,
			hudSettings.speedometerMinorStepKph,
			hudSettings.speedometerMaxKph
		)
		hudSettings.speedometerLabelStepKph = clamp(
			hudSettings.speedometerLabelStepKph,
			hudSettings.speedometerMajorStepKph,
			hudSettings.speedometerMaxKph
		)
		resetHudCaches()
		setPauseStatus("Speedometer Max: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "hud_speedometer_minor_step" then
		hudSettings.speedometerMinorStepKph = clamp(
			hudSettings.speedometerMinorStepKph + item.step * direction * scale,
			item.min,
			item.max
		)
		hudSettings.speedometerMajorStepKph = math.max(hudSettings.speedometerMajorStepKph,
			hudSettings.speedometerMinorStepKph)
		hudSettings.speedometerLabelStepKph = math.max(hudSettings.speedometerLabelStepKph,
			hudSettings.speedometerMajorStepKph)
		resetHudCaches()
		setPauseStatus("Minor Tick Step: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "hud_speedometer_major_step" then
		hudSettings.speedometerMajorStepKph = clamp(
			hudSettings.speedometerMajorStepKph + item.step * direction * scale,
			item.min,
			item.max
		)
		hudSettings.speedometerMajorStepKph = math.max(hudSettings.speedometerMajorStepKph,
			hudSettings.speedometerMinorStepKph)
		hudSettings.speedometerLabelStepKph = math.max(hudSettings.speedometerLabelStepKph,
			hudSettings.speedometerMajorStepKph)
		resetHudCaches()
		setPauseStatus("Major Tick Step: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "hud_speedometer_label_step" then
		hudSettings.speedometerLabelStepKph = clamp(
			hudSettings.speedometerLabelStepKph + item.step * direction * scale,
			item.min,
			item.max
		)
		hudSettings.speedometerLabelStepKph = math.max(hudSettings.speedometerLabelStepKph,
			hudSettings.speedometerMajorStepKph)
		resetHudCaches()
		setPauseStatus("Label Step: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "hud_speedometer_redline" then
		hudSettings.speedometerRedlineKph = clamp(
			hudSettings.speedometerRedlineKph + item.step * direction * scale,
			item.min,
			item.max
		)
		hudSettings.speedometerRedlineKph = clamp(
			hudSettings.speedometerRedlineKph,
			100,
			hudSettings.speedometerMaxKph
		)
		resetHudCaches()
		setPauseStatus("Redline: " .. getPauseItemValue(item), 1.2)
	end
end

local function activatePauseItem(item)
	if item.kind == "range" then
		adjustPauseItem(item, 1)
		return
	end
	if item.kind == "cycle" then
		adjustPauseItem(item, 1)
		return
	end

	if item.confirm and not isPauseConfirmActive(item.id) then
		pauseMenu.confirmItemId = item.id
		pauseMenu.confirmUntil = love.timer.getTime() + 2.5
		if item.id == "apply_graphics_api" then
			setPauseStatus("Warning: this restarts the game, reconnects relay, and reapplies world state. Confirm to continue.",
				3.2)
		else
			setPauseStatus("Confirm to \"" .. item.label .. "\".", 2.5)
		end
		return
	end
	clearPauseConfirm()

	if item.id == "resume" then
		setPauseState(false)
	elseif item.id == "flight" then
		setFlightMode(not flightSimMode)
		setPauseStatus("Flight Mode: " .. getPauseItemValue(item), 1.2)
	elseif item.id == "callsign" then
		modelLoadPrompt.active = true
		modelLoadPrompt.mode = "callsign"
		modelLoadPrompt.text = localCallsign or ""
		modelLoadPrompt.cursor = #modelLoadPrompt.text
		setPauseStatus("Callsign: Enter to apply, Esc to cancel.", 4.5)
	elseif item.id == "load_custom_stl" then
		modelLoadTargetRole = "plane"
		modelLoadPrompt.active = true
		modelLoadPrompt.mode = "model_path"
		modelLoadPrompt.text = ""
		modelLoadPrompt.cursor = 0
		setPauseStatus("Plane model path: Enter to load, Esc to cancel.", 4.5)
	elseif item.id == "load_walking_stl" then
		modelLoadTargetRole = "walking"
		modelLoadPrompt.active = true
		modelLoadPrompt.mode = "model_path"
		modelLoadPrompt.text = ""
		modelLoadPrompt.cursor = 0
		setPauseStatus("Walking model path: Enter to load, Esc to cancel.", 4.5)
	elseif item.id == "apply_video" then
		local ok, err = applyGraphicsVideoMode()
		if ok then
			setPauseStatus("Display mode applied: " .. getGraphicsWindowModeLabel(graphicsSettings.windowMode) .. ".",
				1.8)
		else
			setPauseStatus("Display apply failed: " .. tostring(err), 2.6)
		end
	elseif item.id == "vsync" then
		graphicsSettings.vsync = not graphicsSettings.vsync
		setPauseStatus("VSync: " .. getPauseItemValue(item) .. " (select Apply Display).", 1.6)
	elseif item.id == "renderer" then
		setRendererPreference(not useGpuRenderer)
		setPauseStatus("Renderer: " .. getPauseItemValue(item), 1.2)
	elseif item.id == "apply_graphics_api" then
		local targetPreference = normalizeGraphicsApiPreference(graphicsSettings.graphicsApiPreference)
		local currentPreference = normalizeGraphicsApiPreference(currentGraphicsApiPreference)
		if targetPreference == currentPreference then
			setPauseStatus("Graphics API already active: " .. getGraphicsApiPreferenceLabel(targetPreference) .. ".", 1.8)
			return
		end
		local ok, err = requestGraphicsApiRestart(targetPreference)
		if not ok then
			setPauseStatus("Restart failed: " .. tostring(err), 2.6)
		end
	elseif item.id == "crosshair" then
		showCrosshair = not showCrosshair
		setPauseStatus("Crosshair: " .. getPauseItemValue(item), 1.2)
	elseif item.id == "overlay" then
		showDebugOverlay = not showDebugOverlay
		setPauseStatus("Debug Overlay: " .. getPauseItemValue(item), 1.2)
	elseif item.id == "sun_marker" then
		sunSettings.showMarker = not sunSettings.showMarker
		setPauseStatus("Sun Marker: " .. getPauseItemValue(item), 1.2)
	elseif item.id == "invertY" then
		invertLookY = not invertLookY
		setPauseStatus("Invert Look Y: " .. getPauseItemValue(item), 1.2)
	elseif item.id == "plane_preview_auto_spin" then
		characterPreview.plane.autoSpin = not characterPreview.plane.autoSpin
		setPauseStatus("Plane Auto Spin: " .. getPauseItemValue(item), 1.2)
	elseif item.id == "walking_preview_auto_spin" then
		characterPreview.walking.autoSpin = not characterPreview.walking.autoSpin
		setPauseStatus("Player Auto Spin: " .. getPauseItemValue(item), 1.2)
	elseif item.id == "hud_speedometer_enabled" then
		hudSettings.showSpeedometer = not hudSettings.showSpeedometer
		setPauseStatus("Speedometer: " .. getPauseItemValue(item), 1.2)
	elseif item.id == "hud_show_throttle" then
		hudSettings.showThrottle = not hudSettings.showThrottle
		setPauseStatus("Throttle Panel: " .. getPauseItemValue(item), 1.2)
	elseif item.id == "hud_show_controls" then
		hudSettings.showFlightControls = not hudSettings.showFlightControls
		setPauseStatus("Control Surface: " .. getPauseItemValue(item), 1.2)
	elseif item.id == "hud_show_map" then
		hudSettings.showMap = not hudSettings.showMap
		setPauseStatus("Mini Map: " .. getPauseItemValue(item), 1.2)
	elseif item.id == "hud_show_geo" then
		hudSettings.showGeoInfo = not hudSettings.showGeoInfo
		setPauseStatus("Geo Info: " .. getPauseItemValue(item), 1.2)
	elseif item.id == "hud_show_peer_indicators" then
		hudSettings.showPeerIndicators = not hudSettings.showPeerIndicators
		setPauseStatus("Peer Indicators: " .. getPauseItemValue(item), 1.2)
	elseif item.id == "reset" then
		resetCameraTransform()
		setPauseStatus("Camera reset.", 1.2)
	elseif item.id == "reconnect" then
		local ok = reconnectRelay()
		if ok then
			setPauseStatus("Relay reconnect requested.", 1.6)
		else
			setPauseStatus("Relay reconnect failed. Check logs.", 1.6)
		end
	elseif item.id == "quit" then
		love.event.quit()
	end
end

local function movePauseSelection(delta)
	local itemCount = #getPauseItemsForCurrentPage()
	if itemCount <= 0 then
		pauseMenu.selected = 1
		return
	end
	pauseMenu.selected = ((pauseMenu.selected - 1 + delta) % itemCount) + 1
	clearPauseRangeDrag()
	clearPauseConfirm()
end

local function adjustSelectedPauseItem(direction)
	local items = getPauseItemsForCurrentPage()
	local item = items[pauseMenu.selected]
	if not item then
		return
	end
	if item.kind == "range" or item.kind == "cycle" then
		adjustPauseItem(item, direction)
	elseif item.kind == "toggle" and direction ~= 0 then
		activatePauseItem(item)
	end
end

local function adjustSelectedPauseItemCoarse(direction)
	local items = getPauseItemsForCurrentPage()
	local item = items[pauseMenu.selected]
	if item and item.kind == "range" then
		adjustPauseItem(item, direction, 5)
	end
end

local function activateSelectedPauseItem()
	local items = getPauseItemsForCurrentPage()
	local item = items[pauseMenu.selected]
	if item then
		activatePauseItem(item)
	end
end

local function updatePauseMenuHover(x, y)
	if pauseMenu.tab == "help" or pauseMenu.tab == "controls" then
		return
	end
	if pauseMenu.dragRange.active then
		return
	end

	for i, bounds in ipairs(pauseMenu.itemBounds) do
		if x >= bounds.x and x <= bounds.x + bounds.w and y >= bounds.y and y <= bounds.y + bounds.h then
			if pauseMenu.selected ~= i then
				pauseMenu.selected = i
				clearPauseRangeDrag()
				clearPauseConfirm()
			end
			return
		end
	end
end

local function updatePauseControlsHover(x, y)
	if pauseMenu.tab ~= "controls" or pauseMenu.listeningBinding then
		return
	end

	for i, slotBounds in ipairs(pauseMenu.controlsSlotBounds) do
		for slot, bounds in ipairs(slotBounds) do
			if bounds and
				x >= bounds.x and x <= bounds.x + bounds.w and
				y >= bounds.y and y <= bounds.y + bounds.h then
				pauseMenu.controlsSelection = i
				pauseMenu.controlsSlot = slot
				return
			end
		end
	end

	for i, bounds in ipairs(pauseMenu.controlsRowBounds) do
		if x >= bounds.x and x <= bounds.x + bounds.w and y >= bounds.y and y <= bounds.y + bounds.h then
			pauseMenu.controlsSelection = i
			return
		end
	end
end

local function beginPauseRangeDrag(itemIndex, x)
	pauseMenu.dragRange.active = true
	pauseMenu.dragRange.itemIndex = itemIndex
	pauseMenu.dragRange.startX = x
	pauseMenu.dragRange.lastX = x
	pauseMenu.dragRange.totalDx = 0
	pauseMenu.dragRange.moved = false
end

local function updatePauseRangeDrag(x)
	if not pauseMenu.dragRange.active then
		return
	end

	local itemIndex = pauseMenu.dragRange.itemIndex
	local items = getPauseItemsForCurrentPage()
	local item = itemIndex and items[itemIndex] or nil
	if not item or item.kind ~= "range" then
		clearPauseRangeDrag()
		return
	end

	local dx = x - pauseMenu.dragRange.lastX
	pauseMenu.dragRange.lastX = x
	pauseMenu.dragRange.totalDx = pauseMenu.dragRange.totalDx + dx

	local stepPixels = 12
	while math.abs(pauseMenu.dragRange.totalDx) >= stepPixels do
		local direction = pauseMenu.dragRange.totalDx > 0 and 1 or -1
		adjustPauseItem(item, direction)
		pauseMenu.dragRange.totalDx = pauseMenu.dragRange.totalDx - (direction * stepPixels)
		pauseMenu.dragRange.moved = true
	end
end

local function endPauseRangeDrag(x)
	if not pauseMenu.dragRange.active then
		return false
	end

	local itemIndex = pauseMenu.dragRange.itemIndex
	local moved = pauseMenu.dragRange.moved
	local items = getPauseItemsForCurrentPage()
	local item = itemIndex and items[itemIndex] or nil
	local bounds = itemIndex and pauseMenu.itemBounds[itemIndex] or nil

	clearPauseRangeDrag()

	if moved or not item or item.kind ~= "range" or not bounds then
		return moved
	end

	if x >= bounds.x and x <= (bounds.x + bounds.w) then
		local direction = (x >= (bounds.x + bounds.w * 0.5)) and 1 or -1
		adjustPauseItem(item, direction)
		return true
	end

	return false
end

local function handlePauseControlsMouseClick(x, y, button)
	if pauseMenu.listeningBinding then
		return false
	end

	if button ~= 1 then
		return false
	end

	for i, slotBounds in ipairs(pauseMenu.controlsSlotBounds) do
		for slot, bounds in ipairs(slotBounds) do
			if bounds and
				x >= bounds.x and x <= bounds.x + bounds.w and
				y >= bounds.y and y <= bounds.y + bounds.h then
				beginSelectedControlBindingCapture(i, slot)
				return true
			end
		end
	end

	for i, bounds in ipairs(pauseMenu.controlsRowBounds) do
		if x >= bounds.x and x <= bounds.x + bounds.w and y >= bounds.y and y <= bounds.y + bounds.h then
			pauseMenu.controlsSelection = i
			return true
		end
	end

	return false
end

local function handlePauseMenuMouseClick(x, y)
	for _, tabBounds in ipairs(pauseMenu.tabBounds) do
		if x >= tabBounds.x and x <= tabBounds.x + tabBounds.w and y >= tabBounds.y and y <= tabBounds.y + tabBounds.h then
			setPauseTab(tabBounds.id)
			return
		end
	end

	for _, subTabBounds in ipairs(pauseMenu.subTabBounds) do
		if x >= subTabBounds.x and x <= subTabBounds.x + subTabBounds.w and
			y >= subTabBounds.y and y <= subTabBounds.y + subTabBounds.h then
			pauseMenu.subTab[pauseMenu.tab] = subTabBounds.id
			pauseMenu.selected = 1
			clearPauseRangeDrag()
			clearPauseConfirm()
			return
		end
	end

	if pauseMenu.tab == "controls" then
		handlePauseControlsMouseClick(x, y, 1)
		return
	end

	if pauseMenu.tab == "help" then
		return
	end

	local items = getPauseItemsForCurrentPage()

	for i, bounds in ipairs(pauseMenu.itemBounds) do
		if x >= bounds.x and x <= bounds.x + bounds.w and y >= bounds.y and y <= bounds.y + bounds.h then
			pauseMenu.selected = i
			local item = items[i]
			clearPauseRangeDrag()
			if item and item.id ~= "quit" then
				clearPauseConfirm()
			end
			if item and item.kind == "range" then
				beginPauseRangeDrag(i, x)
			elseif item then
				activatePauseItem(item)
			end
			return
		end
	end
end

setPauseState = function(paused)
	if pauseMenu.active == paused then
		return
	end

	pauseMenu.active = paused and true or false
	if pauseMenu.active then
		resetAltLookState()
		mapState.mHeld = false
		mapState.mUsedForZoom = false
		modelLoadPrompt.active = false
		modelLoadPrompt.mode = "model_path"
		modelLoadTargetRole = "plane"
		pauseMenu.tab = "main"
		pauseMenu.helpScroll = 0
		pauseMenu.controlsScroll = 0
		pauseMenu.selected = 1
		getPauseActiveSubTab("settings")
		getPauseActiveSubTab("characters")
		getPauseActiveSubTab("hud")
		clampPauseSelection()
		pauseMenu.controlsSelection = clamp(pauseMenu.controlsSelection, 1, #controlActions)
		pauseMenu.controlsSlot = clamp(pauseMenu.controlsSlot, 1, 2)
		pauseMenu.itemBounds = {}
		pauseMenu.controlsRowBounds = {}
		pauseMenu.controlsSlotBounds = {}
		pauseMenu.tabBounds = {}
		pauseMenu.subTabBounds = {}
		clearPauseRangeDrag()
		clearControlBindingCapture()
		setPauseStatus("", 0)
		clearPauseConfirm()
		setMouseCapture(false)
		logger.log("Pause menu opened.")
	else
		modelLoadPrompt.active = false
		modelLoadPrompt.mode = "model_path"
		modelLoadTargetRole = "plane"
		pauseMenu.tabBounds = {}
		pauseMenu.subTabBounds = {}
		pauseMenu.itemBounds = {}
		pauseMenu.controlsRowBounds = {}
		pauseMenu.controlsSlotBounds = {}
		clearPauseRangeDrag()
		clearControlBindingCapture()
		setPauseStatus("", 0)
		clearPauseConfirm()
		if love.window.hasFocus and love.window.hasFocus() then
			setMouseCapture(true)
		end
		logger.log("Pause menu closed.")
	end
end

local function drawPauseMenuRows(layout, rowTextHeight)
	local font = love.graphics.getFont()
	local arrowText = "<"
	local rightArrowText = ">"
	local arrowPad = 12
	local items = getPauseItemsForCurrentPage()
	clampPauseSelection()

	pauseMenu.itemBounds = {}
	for i, item in ipairs(items) do
		local rowY = layout.contentY + (i - 1) * layout.rowH
		local rowHeight = math.max(1, layout.rowH - 4)
		local rowTextY = rowY + math.floor((rowHeight - rowTextHeight) / 2)
		local selected = i == pauseMenu.selected
		local value = getPauseItemValue(item)
		local centerText = item.label

		if selected then
			love.graphics.setColor(0.22, 0.28, 0.42, 0.95)
		else
			love.graphics.setColor(0.15, 0.16, 0.2, 0.9)
		end
		love.graphics.rectangle("fill", layout.rowX, rowY, layout.rowW, rowHeight, 6, 6)

		if value ~= "" then
			centerText = item.label .. "  " .. value
		end
		local centerTextW = font:getWidth(centerText)
		local centerTextX = layout.rowX + math.floor((layout.rowW - centerTextW) / 2)

		love.graphics.setColor(1, 1, 1, 1)
		love.graphics.print(centerText, centerTextX, rowTextY)

		if item.kind == "range" or item.kind == "cycle" then
			local leftX = layout.rowX + arrowPad
			local rightX = layout.rowX + layout.rowW - arrowPad - font:getWidth(rightArrowText)
			love.graphics.print(arrowText, leftX, rowTextY)
			love.graphics.print(rightArrowText, rightX, rowTextY)
		end

		pauseMenu.itemBounds[i] = { x = layout.rowX, y = rowY, w = layout.rowW, h = rowHeight }
	end
end

local function drawPauseMenuTabs(layout)
	local font = love.graphics.getFont()
	pauseMenu.tabBounds = {}
	local labels = getPauseTopTabs()
	local tabGap = 8
	local totalW = 0
	for _, tab in ipairs(labels) do
		totalW = totalW + font:getWidth(tab.label) + 22
	end
	totalW = totalW + (tabGap * (#labels - 1))

	local x = layout.panelX + (layout.panelW - totalW) / 2
	for i, tab in ipairs(labels) do
		local tabW = font:getWidth(tab.label) + 22
		local active = pauseMenu.tab == tab.id

		if active then
			love.graphics.setColor(0.2, 0.3, 0.46, 0.95)
		else
			love.graphics.setColor(0.14, 0.16, 0.2, 0.95)
		end
		love.graphics.rectangle("fill", x, layout.tabY, tabW, layout.tabH, 6, 6)

		love.graphics.setColor(1, 1, 1, active and 1 or 0.85)
		love.graphics.printf(tab.label, x, layout.tabY + math.floor((layout.tabH - font:getHeight()) / 2), tabW, "center")

		pauseMenu.tabBounds[i] = { id = tab.id, x = x, y = layout.tabY, w = tabW, h = layout.tabH }
		x = x + tabW + tabGap
	end
end

local function drawPauseMenuSubTabs(layout)
	local subTabs = getPauseSubTabs(pauseMenu.tab)
	pauseMenu.subTabBounds = {}
	if not subTabs or #subTabs == 0 then
		return
	end

	local font = love.graphics.getFont()
	local tabGap = 8
	local totalW = 0
	for _, sub in ipairs(subTabs) do
		totalW = totalW + font:getWidth(sub.label) + 20
	end
	totalW = totalW + (tabGap * (#subTabs - 1))

	local x = layout.panelX + (layout.panelW - totalW) / 2
	local activeSub = getPauseActiveSubTab(pauseMenu.tab)
	for i, sub in ipairs(subTabs) do
		local subW = font:getWidth(sub.label) + 20
		local active = activeSub == sub.id
		if active then
			love.graphics.setColor(0.22, 0.31, 0.48, 0.95)
		else
			love.graphics.setColor(0.13, 0.14, 0.17, 0.94)
		end
		love.graphics.rectangle("fill", x, layout.subTabY, subW, layout.subTabH, 5, 5)
		love.graphics.setColor(1, 1, 1, active and 1 or 0.85)
		love.graphics.printf(sub.label, x, layout.subTabY + math.floor((layout.subTabH - font:getHeight()) / 2), subW,
			"center")
		pauseMenu.subTabBounds[i] = { id = sub.id, x = x, y = layout.subTabY, w = subW, h = layout.subTabH }
		x = x + subW + tabGap
	end
end

local function drawPauseHelpContent(layout)
	local font = love.graphics.getFont()
	local sections = getPauseHelpSections()
	local cardGap = 10
	local cardPad = 10
	local cardX = layout.contentX + 4
	local cardW = layout.contentW - 8
	local lineH = font:getHeight()

	local function measureItemHeight(item)
		local badgeText = "[" .. item.keys .. "]"
		local badgeW = font:getWidth(badgeText) + 12
		local descW = math.max(40, cardW - cardPad * 2 - badgeW - 10)
		local lines = getWrappedLines(font, item.text, descW)
		local textH = math.max(lineH, #lines * lineH)
		return textH + 4
	end

	local function measureSectionHeight(section)
		local h = cardPad + lineH + 8
		for _, item in ipairs(section.items) do
			h = h + measureItemHeight(item) + 6
		end
		h = h + cardPad - 6
		return h
	end

	local totalH = 0
	for _, section in ipairs(sections) do
		totalH = totalH + measureSectionHeight(section) + cardGap
	end
	totalH = math.max(0, totalH - cardGap)

	local maxScroll = math.max(0, totalH - layout.contentH)
	pauseMenu.helpScroll = clamp(pauseMenu.helpScroll, 0, maxScroll)
	local y = layout.contentY + 4 - pauseMenu.helpScroll

	love.graphics.setColor(0.12, 0.13, 0.16, 0.9)
	love.graphics.rectangle("fill", layout.contentX, layout.contentY, layout.contentW, layout.contentH, 6, 6)
	love.graphics.setScissor(layout.contentX, layout.contentY, layout.contentW, layout.contentH)

	for sectionIndex, section in ipairs(sections) do
		local sectionH = measureSectionHeight(section)
		local cardY = y

		if cardY + sectionH >= layout.contentY and cardY <= (layout.contentY + layout.contentH) then
			local tint = 0.14 + ((sectionIndex - 1) * 0.03)
			love.graphics.setColor(tint, tint + 0.02, tint + 0.05, 0.95)
			love.graphics.rectangle("fill", cardX, cardY, cardW, sectionH, 8, 8)
			love.graphics.setColor(1, 1, 1, 0.22)
			love.graphics.rectangle("line", cardX, cardY, cardW, sectionH, 8, 8)

			love.graphics.setColor(0.85, 0.92, 1.0, 1.0)
			love.graphics.print(section.title, cardX + cardPad, cardY + cardPad)

			local itemY = cardY + cardPad + lineH + 8
			for _, item in ipairs(section.items) do
				local badgeText = "[" .. item.keys .. "]"
				local badgeW = font:getWidth(badgeText) + 12
				local badgeH = lineH + 2
				local descX = cardX + cardPad + badgeW + 10
				local descW = math.max(40, cardW - cardPad * 2 - badgeW - 10)
				local lines = getWrappedLines(font, item.text, descW)
				local textH = math.max(lineH, #lines * lineH)
				local rowH = math.max(badgeH, textH) + 4
				local badgeY = itemY + math.floor((rowH - badgeH) / 2)

				love.graphics.setColor(0.24, 0.29, 0.38, 0.95)
				love.graphics.rectangle("fill", cardX + cardPad, badgeY, badgeW, badgeH, 4, 4)
				love.graphics.setColor(1, 1, 1, 1)
				love.graphics.printf(badgeText, cardX + cardPad, badgeY + 1, badgeW, "center")

				love.graphics.setColor(0.95, 0.95, 0.95, 0.98)
				love.graphics.printf(table.concat(lines, "\n"), descX, itemY + 2, descW, "left")

				itemY = itemY + rowH + 6
			end
		end

		y = y + sectionH + cardGap
	end

	love.graphics.setScissor()

	if maxScroll > 0 then
		local barX = layout.contentX + layout.contentW - 6
		local barY = layout.contentY + 4
		local barH = layout.contentH - 8
		local visibleRatio = math.max(0.05, math.min(1, layout.contentH / math.max(layout.contentH, totalH)))
		local thumbH = math.max(16, barH * visibleRatio)
		local thumbY = barY + (barH - thumbH) * (pauseMenu.helpScroll / maxScroll)

		love.graphics.setColor(1, 1, 1, 0.18)
		love.graphics.rectangle("fill", barX, barY, 2, barH)
		love.graphics.setColor(0.8, 0.9, 1.0, 0.7)
		love.graphics.rectangle("fill", barX - 1, thumbY, 4, thumbH, 2, 2)
	end
end

local function drawPauseControlsContent(layout)
	local font = love.graphics.getFont()
	local lineH = font:getHeight()
	local cardX = layout.contentX + 4
	local cardY = layout.contentY
	local cardW = layout.contentW - 8
	local cardH = layout.contentH
	local sectionH = lineH + 6
	local sectionGap = 4
	local rowGap = 6
	local rowInset = 8
	local rowPadY = 6
	local slotInsetY = 2
	local slotGap = 8
	local slotW = clamp(math.floor(cardW * 0.23), 120, 170)
	local modeTitle = {
		flight = "Flight Mode",
		walking = "Walking Mode"
	}
	pauseMenu.controlsSelection = clamp(pauseMenu.controlsSelection, 1, #controlActions)

	local function getWrappedLinesOrFallback(text, width)
		local lines = getWrappedLines(font, text, width)
		if #lines == 0 then
			return { "" }
		end
		return lines
	end

	local function buildControlsLayoutRows()
		local rows = {}
		local headers = {}
		local y = 4
		local lastMode = nil

		for i, action in ipairs(controlActions) do
			if action.mode ~= lastMode then
				table.insert(headers, {
					y = y,
					text = modeTitle[action.mode] or action.mode
				})
				y = y + sectionH + sectionGap
				lastMode = action.mode
			end

			local slot1X = cardX + cardW - rowInset - (slotW * 2) - slotGap
			local slot2X = slot1X + slotW + slotGap
			local labelX = cardX + rowInset
			local labelW = math.max(40, slot1X - labelX - 10)
			local slotTextW = math.max(40, slotW - 12)
			local selected = i == pauseMenu.controlsSelection

			local slotLines = {}
			local slotHeights = {}
			for slot = 1, 2 do
				local isListeningSlot = pauseMenu.listeningBinding and
					pauseMenu.listeningBinding.actionIndex == i and
					pauseMenu.listeningBinding.slot == slot
				local binding = action.bindings and action.bindings[slot] or nil
				local text = controls.formatBinding(binding)
				if isListeningSlot then
					text = "Listening..."
				elseif text == "Unbound" then
					text = "-"
				end
				slotLines[slot] = getWrappedLinesOrFallback(text, slotTextW)
				slotHeights[slot] = #slotLines[slot] * lineH
			end

			local labelLines = getWrappedLinesOrFallback(action.label, labelW)
			local labelTextH = #labelLines * lineH
			local rowTextH = math.max(labelTextH, slotHeights[1], slotHeights[2])
			local rowH = math.max(lineH + 10, rowTextH + rowPadY * 2)

			rows[i] = {
				index = i,
				action = action,
				y = y,
				h = rowH,
				selected = selected,
				labelX = labelX,
				labelW = labelW,
				labelLines = labelLines,
				labelTextH = labelTextH,
				slot1X = slot1X,
				slot2X = slot2X,
				slotW = slotW,
				slotTextW = slotTextW,
				slotLines = slotLines,
				slotHeights = slotHeights
			}

			y = y + rowH + rowGap
		end

		local totalH = math.max(0, y - rowGap)
		return rows, headers, totalH
	end

	local rows, headers, totalH = buildControlsLayoutRows()

	local selectedRow = rows[pauseMenu.controlsSelection]
	local selectedTop, selectedBottom = nil, nil
	if selectedRow then
		selectedTop = selectedRow.y
		selectedBottom = selectedRow.y + selectedRow.h
	end

	local maxScroll = math.max(0, totalH - cardH)
	pauseMenu.controlsScroll = clamp(pauseMenu.controlsScroll, 0, maxScroll)

	if selectedTop and selectedBottom then
		if selectedTop < pauseMenu.controlsScroll then
			pauseMenu.controlsScroll = selectedTop
		elseif selectedBottom > (pauseMenu.controlsScroll + cardH) then
			pauseMenu.controlsScroll = selectedBottom - cardH
		end
		pauseMenu.controlsScroll = clamp(pauseMenu.controlsScroll, 0, maxScroll)
	end

	pauseMenu.controlsRowBounds = {}
	pauseMenu.controlsSlotBounds = {}

	love.graphics.setColor(0.12, 0.13, 0.16, 0.92)
	love.graphics.rectangle("fill", layout.contentX, layout.contentY, layout.contentW, layout.contentH, 6, 6)
	love.graphics.setScissor(layout.contentX, layout.contentY, layout.contentW, layout.contentH)

	for _, header in ipairs(headers) do
		local headerY = cardY + header.y - pauseMenu.controlsScroll
		if (headerY + sectionH) >= layout.contentY and headerY <= (layout.contentY + layout.contentH) then
			love.graphics.setColor(0.78, 0.86, 1.0, 0.92)
			love.graphics.print(header.text, cardX + rowInset, headerY)
		end
	end

	for i, row in ipairs(rows) do
		local rowY = cardY + row.y - pauseMenu.controlsScroll
		local rowVisible = (rowY + row.h) >= layout.contentY and rowY <= (layout.contentY + layout.contentH)

		if rowVisible then
			if row.selected then
				love.graphics.setColor(0.2, 0.28, 0.42, 0.96)
			else
				love.graphics.setColor(0.15, 0.16, 0.2, 0.9)
			end
			love.graphics.rectangle("fill", cardX, rowY, cardW, row.h, 6, 6)

			love.graphics.setColor(1, 1, 1, 0.98)
			local labelTextY = rowY + math.floor((row.h - row.labelTextH) / 2)
			love.graphics.printf(table.concat(row.labelLines, "\n"), row.labelX, labelTextY, row.labelW, "left")
		end

		pauseMenu.controlsRowBounds[i] = { x = cardX, y = rowY, w = cardW, h = row.h }
		pauseMenu.controlsSlotBounds[i] = {}

		for slot = 1, 2 do
			local slotX = slot == 1 and row.slot1X or row.slot2X
			local isActiveSlot = row.selected and pauseMenu.controlsSlot == slot
			local isListeningSlot = pauseMenu.listeningBinding and
				pauseMenu.listeningBinding.actionIndex == i and
				pauseMenu.listeningBinding.slot == slot
			local text = table.concat(row.slotLines[slot], "\n")
			local slotTextY = rowY + math.floor((row.h - row.slotHeights[slot]) / 2)

			pauseMenu.controlsSlotBounds[i][slot] = {
				x = slotX,
				y = rowY + slotInsetY,
				w = row.slotW,
				h = row.h - (slotInsetY * 2)
			}

			if rowVisible then
				if isListeningSlot then
					love.graphics.setColor(0.86, 0.6, 0.18, 0.98)
				elseif isActiveSlot then
					love.graphics.setColor(0.32, 0.4, 0.58, 0.98)
				else
					love.graphics.setColor(0.22, 0.24, 0.3, 0.95)
				end
				love.graphics.rectangle("fill", slotX, rowY + slotInsetY, row.slotW, row.h - (slotInsetY * 2), 5, 5)
				love.graphics.setColor(1, 1, 1, isListeningSlot and 1 or 0.93)
				love.graphics.printf(text, slotX + 6, slotTextY, row.slotTextW, "center")
			end
		end
	end

	love.graphics.setScissor()

	if maxScroll > 0 then
		local barX = layout.contentX + layout.contentW - 6
		local barY = layout.contentY + 4
		local barH = layout.contentH - 8
		local visibleRatio = math.max(0.05, math.min(1, layout.contentH / math.max(layout.contentH, totalH)))
		local thumbH = math.max(16, barH * visibleRatio)
		local thumbY = barY + (barH - thumbH) * (pauseMenu.controlsScroll / maxScroll)

		love.graphics.setColor(1, 1, 1, 0.18)
		love.graphics.rectangle("fill", barX, barY, 2, barH)
		love.graphics.setColor(0.8, 0.9, 1.0, 0.72)
		love.graphics.rectangle("fill", barX - 1, thumbY, 4, thumbH, 2, 2)
	end
end

local function drawPauseCharacterPreview(layout)
	if pauseMenu.tab ~= "characters" or not layout.previewX then
		return
	end

	local sub = getPauseActiveSubTab("characters")
	local role = (sub == "walking") and "walking" or "plane"
	local previewState = characterPreview[role]
	local modelHash, modelScale = getConfiguredModelForRole(role)
	local entry = playerModelCache[modelHash] or playerModelCache["builtin-cube"]
	local model = (entry and entry.model) or cubeModel

	local x = layout.previewX
	local y = layout.previewY
	local w = layout.previewW
	local h = layout.previewH
	local cx = x + w * 0.5
	local cy = y + h * 0.54
	local zoom = (previewState and previewState.zoom) or 1.0
	local displayScale = math.min(w, h) * 0.28 * zoom * math.max(0.2, tonumber(modelScale) or 1.0)
	local previewRot = composeRoleOrientationOffset(role, modelHash)
	if previewState and previewState.autoSpin then
		local spin = q.fromAxisAngle({ 0, 1, 0 }, math.rad(love.timer.getTime() * (previewState.spinRate or 22)))
		previewRot = q.normalize(q.multiply(spin, previewRot))
	end
	local camZ = 5.4

	love.graphics.setColor(0.1, 0.11, 0.14, 0.93)
	love.graphics.rectangle("fill", x, y, w, h, 6, 6)
	love.graphics.setColor(1, 1, 1, 0.18)
	love.graphics.rectangle("line", x, y, w, h, 6, 6)

	local projected = {}
	for i, v in ipairs(model.vertices or {}) do
		local rv = q.rotateVector(previewRot, {
			v[1] or 0,
			v[2] or 0,
			v[3] or 0
		})
		local rx = rv[1] or 0
		local py = rv[2] or 0
		local pz = rv[3] or 0
		local denom = pz + camZ
		if denom <= 0.05 then
			projected[i] = nil
		else
			projected[i] = {
				x = cx + (rx * displayScale) / denom,
				y = cy - (py * displayScale) / denom
			}
		end
	end

	love.graphics.setScissor(x + 1, y + 1, w - 2, h - 2)
	love.graphics.setColor(0.76, 0.9, 1.0, 0.88)
	for _, face in ipairs(model.faces or {}) do
		if #face >= 2 then
			for i = 1, #face do
				local ia = face[i]
				local ib = face[(i % #face) + 1]
				local a = projected[ia]
				local b = projected[ib]
				if a and b then
					love.graphics.line(a.x, a.y, b.x, b.y)
				end
			end
		end
	end
	love.graphics.setScissor()

	love.graphics.setColor(0.82, 0.9, 1.0, 0.95)
	love.graphics.printf(string.upper(role) .. " PREVIEW", x + 6, y + 6, w - 12, "center")
	love.graphics.setColor(0.72, 0.84, 0.95, 0.9)
	local modelHashString = tostring(modelHash)

	if #modelHashString > 10 then
		modelHashString = string.sub(modelHashString, 1, 10) .. "..."
	end

	love.graphics.printf(
		string.format("Model: %s", modelHashString),
		x + 8,
		y + h - (layout.lineH * 2) - 8,
		w - 16,
		"center"
	)
	love.graphics.printf(
		string.format("Scale %.2fx", tonumber(modelScale) or 1.0),
		x + 8,
		y + h - layout.lineH - 6,
		w - 16,
		"center"
	)
end

local function drawPauseMenuFooter(layout)
	local footerX = layout.panelX + 18
	local footerW = layout.panelW - 36
	local y = layout.panelY + layout.panelH - layout.bottomPad + layout.footerTopPad

	love.graphics.setColor(1, 1, 1, 0.85)
	if #layout.controlsLines > 0 then
		love.graphics.printf(table.concat(layout.controlsLines, "\n"), footerX, y, footerW, "center")
		y = y + (#layout.controlsLines * layout.lineH) + layout.footerGap
	end

	if #layout.detailLines > 0 then
		love.graphics.setColor(0.9, 0.95, 1.0, 0.9)
		love.graphics.printf(table.concat(layout.detailLines, "\n"), footerX, y, footerW, "center")
		y = y + (#layout.detailLines * layout.lineH) + layout.footerGap
	end

	if #layout.statusLines > 0 then
		love.graphics.setColor(0.8, 1.0, 0.8, 1.0)
		love.graphics.printf(table.concat(layout.statusLines, "\n"), footerX, y, footerW, "center")
	end
end

function drawModelLoadPrompt(layout)
	if not modelLoadPrompt.active then
		return
	end

	local promptX = layout.panelX + 24
	local promptW = layout.panelW - 48
	local promptH = math.max(54, layout.lineH * 2 + 16)
	local promptY = layout.panelY + layout.panelH - promptH - 12
	local isCallsignPrompt = modelLoadPrompt.mode == "callsign"
	local targetLabel = isCallsignPrompt and "Callsign" or
		((modelLoadTargetRole == "walking") and "Walking Model" or "Plane Model")
	local prefix = targetLabel .. ": "
	local left = modelLoadPrompt.text:sub(1, modelLoadPrompt.cursor)
	local cursorX = promptX + 10 + love.graphics.getFont():getWidth(prefix .. left)
	cursorX = clamp(cursorX, promptX + 10, promptX + promptW - 10)
	local hint = isCallsignPrompt and "Enter=Apply  Esc=Cancel" or
		"Enter=Load  Esc=Cancel  Drag/drop STL/GLB (GLTF path load)"

	love.graphics.setColor(0.03, 0.04, 0.05, 0.96)
	love.graphics.rectangle("fill", promptX, promptY, promptW, promptH, 6, 6)
	love.graphics.setColor(0.78, 0.87, 1.0, 0.9)
	love.graphics.rectangle("line", promptX, promptY, promptW, promptH, 6, 6)
	love.graphics.setColor(1, 1, 1, 0.95)
	love.graphics.print(prefix .. modelLoadPrompt.text, promptX + 10, promptY + 8)
	love.graphics.setColor(0.7, 0.9, 1.0, 0.95)
	love.graphics.line(cursorX, promptY + 8, cursorX, promptY + 8 + love.graphics.getFont():getHeight())
	love.graphics.setColor(0.85, 0.9, 0.95, 0.9)
	love.graphics.print(hint, promptX + 10, promptY + promptH - (layout.lineH + 6))
end

local function drawPauseMenu()
	local oldFont = love.graphics.getFont()
	local titleFont = pauseTitleFont or oldFont
	local itemFont = pauseItemFont or oldFont
	local layout = buildPauseMenuLayout()

	love.graphics.setColor(0, 0, 0, 0.68)
	love.graphics.rectangle("fill", 0, 0, screen.w, screen.h)

	love.graphics.setColor(0.08, 0.08, 0.1, 0.95)
	love.graphics.rectangle("fill", layout.panelX, layout.panelY, layout.panelW, layout.panelH, 8, 8)
	love.graphics.setColor(1, 1, 1, 0.2)
	love.graphics.rectangle("line", layout.panelX, layout.panelY, layout.panelW, layout.panelH, 8, 8)

	love.graphics.setFont(titleFont)
	love.graphics.setColor(1, 1, 1, 1)
	love.graphics.printf("PAUSED", layout.panelX, layout.titleY, layout.panelW, "center")

	love.graphics.setFont(itemFont)
	drawPauseMenuTabs(layout)
	drawPauseMenuSubTabs(layout)
	if layout.isListTab then
		drawPauseMenuRows(layout, itemFont:getHeight())
		drawPauseCharacterPreview(layout)
	elseif layout.isControlsTab then
		pauseMenu.itemBounds = {}
		drawPauseControlsContent(layout)
	else
		pauseMenu.itemBounds = {}
		drawPauseHelpContent(layout)
	end
	drawPauseMenuFooter(layout)
	drawModelLoadPrompt(layout)

	love.graphics.setFont(oldFont)
end

makeRng = groundSystem.makeRng
generateCellGrid = groundSystem.generateCellGrid
CELL_GRASS = groundSystem.CELL_GRASS
CELL_ROAD = groundSystem.CELL_ROAD
CELL_FIELD = groundSystem.CELL_FIELD

local function sampleGroundHeightAtWorld(worldX, worldZ, params)
	local groundParams = params or activeGroundParams or defaultGroundParams
	return groundSystem.sampleGroundHeightAtWorld(worldX, worldZ, groundParams)
end

function updateGroundStreaming(forceRebuild)
	local changed, updatedObject = groundSystem.updateGroundStreaming(forceRebuild, {
		groundObject = groundObject,
		activeGroundParams = activeGroundParams,
		camera = camera,
		drawDistance = graphicsSettings and graphicsSettings.drawDistance
	})
	if changed and updatedObject then
		groundObject = updatedObject
		local halfExtent = math.max(
			tonumber(updatedObject.halfSize and updatedObject.halfSize.x) or 0,
			tonumber(updatedObject.halfSize and updatedObject.halfSize.z) or 0
		)
		if halfExtent > 0 and math.abs((worldHalfExtent or 0) - halfExtent) > 1e-6 then
			worldHalfExtent = halfExtent
			mapState.zoomExtents = { 160, 420, math.max(worldHalfExtent, 420) }
			mapState.logicalCamera = nil
			invalidateMapCache()
		end
	end
	return changed
end

rebuildGroundFromParams = function(params, reason)
	local changed, nextState = groundSystem.rebuildGroundFromParams(params, reason, {
		defaultGroundParams = defaultGroundParams,
		activeGroundParams = activeGroundParams,
		groundObject = groundObject,
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
		groundObject = nextState.groundObject
		worldHalfExtent = nextState.worldHalfExtent or worldHalfExtent
		invalidateMapCache()
	end
	return changed
end

-- === Initial Configuration ===
function love.load()
	-- 80% screen size
	local width, height = love.window.getDesktopDimensions()
	width, height = math.floor(width * 0.8), math.floor(height * 0.8)

	love.window.setTitle("Don's 3D Engine")
	local modeOk, modeErr = love.window.setMode(width, height, { vsync = false, depth = true })
	if not modeOk then
		love.window.setMode(width, height, { vsync = false })
	end
	love.keyboard.setKeyRepeat(true)
	setMouseCapture(true)

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
		logger.log("Restart payload detected; applying preserved world/game state.")
	else
		currentGraphicsApiPreference = normalizeGraphicsApiPreference(graphicsSettings.graphicsApiPreference)
	end
	if not modeOk then
		logger.log("Depth buffer mode unavailable, fallback mode set. Detail: " .. tostring(modeErr))
	end

	screen = {
		w = width,
		h = height
	}
	refreshUiFonts()
	resetHudCaches()
	invalidateMapCache()
	localCallsign = detectDefaultCallsign()

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
		gravity = -9.81, -- units/sec^2
		jumpSpeed = 5, -- initial jump
		throttle = 0,
		throttleAccel = 0.55,
		flightMass = 1.0,
		flightThrustForce = 36,
		flightThrustAccel = 36,
		flightDragCoefficient = 0.018,
		flightAirBrakeDrag = 0.11,
		flightLiftCoefficient = 0.11,
		flightZeroLiftAngle = math.rad(2),
		flightMaxLiftAngle = math.rad(20),
		flightCamberLiftCoefficient = 0.014,
		flightStallSpeed = 8,
		flightFullLiftSpeed = 24,
		flightInducedDragCoefficient = 0.0075,
		flightGravity = -9.81,
		flightGroundFriction = 0.94,
		flightGroundPitchHoldEnabled = true,
		flightGroundPitchHoldAngle = math.rad(-20),
		flightVel = { 0, 0, 0 },
		flightRotVel = { pitch = 0, yaw = 0, roll = 0 },
		flightRotResponse = 6.0,
		yoke = { pitch = 0, yaw = 0, roll = 0 },
		yokeKeyboardRate = 2.8,
		yokeAutoCenterRate = 1.9,
		yokeMousePitchGain = 16,
		yokeMouseYawGain = 14,
		yokeMouseRollGain = 12,
		afterburnerMultiplier = 1.45,
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
	syncGraphicsSettingsFromWindow()
	graphicsSettings.graphicsApiPreference = normalizeGraphicsApiPreference(graphicsSettings.graphicsApiPreference)
	if renderer.setClipPlanes then
		renderer.setClipPlanes(0.1, graphicsSettings.drawDistance)
	end
	syncChunkingWithDrawDistance(false)

	local loadedDefaultModel = loadPlayerModelFromPath(defaultPlayerModelPath, "plane")
	if not loadedDefaultModel then
		local fallback = playerModelCache["builtin-cube"]
		setActivePlayerModel(fallback, "builtin cube", "plane")
	end
	syncActivePlayerModelState()

	-- Local avatar is synced to player camera each update; hidden in first-person, shown in third-person.
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

	rebuildGroundFromParams(defaultGroundParams, "startup")

	windState.angle = randomRange(0, math.pi * 2)
	windState.speed = randomRange(5, 10)
	cloudSim.pickNextWindTarget(windState)
	cloudSim.spawnCloudField(cloudState, objects, camera, windState, cloudPuffModel, q, nil, love.timer.getTime())
	logger.log(string.format("Cloud field initialized (%d groups).", #cloudState.groups))
	if bootRestartSnapshotState then
		if applyRestartSnapshot(bootRestartSnapshotState) then
			logger.log("Restart state restored.")
		else
			logger.log("Restart state restore failed; using default startup state.")
		end
	end
	if autoSmoke.enabled then
		flightSimMode = true
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

	zBuffer = {}
	for i = 1, screen.w * screen.h do
		zBuffer[i] = math.huge
	end

	if not relay then
		logger.log("ENet host creation failed; multiplayer disabled for this session.")
	elseif not relayServer then
		logger.log("Could not connect to relay server at " .. hostAddy)
	else
		logger.log("Connected to relay server: " .. hostAddy)
	end

	local gpuOk, gpuErr = renderer.init(screen, camera, logger.log)
	if gpuOk then
		renderMode = "GPU"
		logger.log("GPU renderer enabled.")
	else
		useGpuRenderer = false
		renderMode = "CPU"
		logger.log("GPU renderer failed, using CPU fallback: " .. tostring(gpuErr))
	end
	applySunSettingsToRenderer()
	if renderer.setClipPlanes then
		renderer.setClipPlanes(0.1, graphicsSettings.drawDistance)
	end
	if pendingRestartRendererPreference ~= nil then
		setRendererPreference(pendingRestartRendererPreference)
		pendingRestartRendererPreference = nil
	end

	resolveActiveRenderCamera()
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

function closeModelLoadPrompt(statusMessage, duration)
	modelLoadPrompt.active = false
	modelLoadPrompt.cursor = math.max(0, #modelLoadPrompt.text)
	modelLoadPrompt.mode = "model_path"
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

local lastProjectileTriggerAt = -999

local function triggerProjectileAction()
	local now = love.timer.getTime()
	if (now - lastProjectileTriggerAt) >= 0.2 then
		lastProjectileTriggerAt = now
		logger.log("Projectile fire input received (projectile spawning not implemented yet).")
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

local function updateNet()
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
		local packet = table.concat({
			"STATE2",
			"px=" .. string.format("%f", camera.pos[1]),
			"py=" .. string.format("%f", camera.pos[2]),
			"pz=" .. string.format("%f", camera.pos[3]),
			"rw=" .. string.format("%f", camera.rot.w),
			"rx=" .. string.format("%f", camera.rot.x),
			"ry=" .. string.format("%f", camera.rot.y),
			"rz=" .. string.format("%f", camera.rot.z),
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

local function serviceNetworkEvents()
	if not relay then
		return
	end

	event = relay:service()
	while event do
		if event.type == "connect" and relayServer and event.peer == relayServer then
			beginCloudRoleElection("relay handshake complete")
			forceStateSync()
		elseif event.type == "receive" then
			local packetType = type(event.data) == "string" and event.data:match("^([^|]+)")
			if packetType == "STATE" or packetType == "STATE2" then
				local peerId = networking.handlePacket(
					event.data,
					peers,
					objects,
					q,
					cubeModel,
					getModelRotationOffsetForHash,
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
					}
				)
				updatePeerVisualModel(peerId)
				notePeerStateReceived(peerId)
			elseif packetType == "CLOUD_REQUEST" then
				handleCloudRequestPacket(event.data)
			elseif packetType == "CLOUD_SNAPSHOT" then
				handleCloudSnapshotPacket(event.data)
			elseif packetType == "GROUND_REQUEST" then
				handleGroundRequestPacket(event.data)
			elseif packetType == "GROUND_SNAPSHOT" then
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
		camera = engine.processMovement(camera, dt, flightSimMode, vector3, q, objects, movementInput, {
			wind = cloudSim.getWindVector3(windState),
			groundHeightAt = function(x, z)
				return sampleGroundHeightAtWorld(x, z, activeGroundParams)
			end,
			groundClearance = (camera.box and camera.box.halfSize and camera.box.halfSize.y) or 1.0
		})
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

	updateNet()
	resolveActiveRenderCamera()
	serviceNetworkEvents()
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

-- === Input Management ===
function love.keypressed(key, scancode, isrepeat)
	if pauseMenu.active then
		if modelLoadPrompt.active then
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
				pauseMenu.controlsSelection = #controlActions
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
		triggerProjectileAction()
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
		triggerProjectileAction()
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

local function projectWorldToScreen(worldPos, viewCamera, w, h)
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
		local thrustNow = throttleRatio * (camera.flightThrustForce or camera.flightThrustAccel or 0)
		if controls.isActionDown("flight_afterburner") then
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
	triangleCount = 0
	local centerX, centerY = screen.w / 2, screen.h / 2
	local renderCamera = viewState.activeCamera or resolveActiveRenderCamera() or camera
	local renderObjects = buildRenderObjectList()
	local worldTintR, worldTintG, worldTintB = 1, 1, 1
	if colorCorruptionMode then
		local t = love.timer.getTime()
		worldTintR = 0.52 + 0.48 * math.sin(t * 2.13 + 0.4)
		worldTintG = 0.52 + 0.48 * math.sin(t * 2.71 + 2.0)
		worldTintB = 0.52 + 0.48 * math.sin(t * 3.19 + 4.1)
	end
	local renderScale = 1  -- clamp(graphicsSettings.renderScale or 1.0, 0.5, 1.5)
	local scaledWorld = false -- math.abs(renderScale - 1.0) > 0.001
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
			local alpha = (obj.color and obj.color[4]) or 1
			if alpha < 0.999 then
				transparentObjects[#transparentObjects + 1] = obj
			else
				opaqueObjects[#opaqueObjects + 1] = obj
			end
		end

		for _, obj in ipairs(opaqueObjects) do
			imageData = engine.drawObject(obj, false, renderCamera, vector3, q, drawScreen, zBuffer, imageData, true)
		end

		table.sort(transparentObjects, function(a, b)
			return viewMath.cameraSpaceDepthForObject(a, renderCamera, q) >
				viewMath.cameraSpaceDepthForObject(b, renderCamera, q)
		end)
		for _, obj in ipairs(transparentObjects) do
			imageData = engine.drawObject(obj, false, renderCamera, vector3, q, drawScreen, zBuffer, imageData, false)
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

		love.graphics.setColor(1, 1, 1)
		love.graphics.print(
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
			),
			10,
			10
		)
	end

	if showCrosshair and not pauseMenu.active then
		love.graphics.setColor(1, 0, 0, 0.5)
		love.graphics.circle("fill", centerX, centerY, 1)
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
