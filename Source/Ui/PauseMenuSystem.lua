local M = {}
local restartSnapshotCompat = require("Source.Ui.RestartSnapshotCompat")

local STANDARD_GLOBALS = {
	_G = true,
	_VERSION = true,
	assert = true,
	collectgarbage = true,
	dofile = true,
	error = true,
	getmetatable = true,
	ipairs = true,
	load = true,
	loadfile = true,
	next = true,
	pairs = true,
	pcall = true,
	print = true,
	rawequal = true,
	rawget = true,
	rawlen = true,
	rawset = true,
	require = true,
	select = true,
	setmetatable = true,
	tonumber = true,
	tostring = true,
	type = true,
	xpcall = true,
	coroutine = true,
	debug = true,
	io = true,
	math = true,
	os = true,
	package = true,
	string = true,
	table = true,
	utf8 = true
}

local REQUIRED_READ_BINDINGS = {
	"RESTART_MODEL_ENCODED_LIMIT",
	"RESTART_STATE_VERSION",
	"activeGroundParams",
	"audioSettings",
	"applySunSettingsToRenderer",
	"camera",
	"characterOrientation",
	"characterPreview",
	"clamp",
	"cloudNetState",
	"cloudPuffModel",
	"cloudSim",
	"cloudState",
	"colorCorruptionMode",
	"composeRoleOrientationOffset",
	"controls",
	"cubeModel",
	"currentGraphicsApiPreference",
	"decodeModelBytes",
	"defaultGroundParams",
	"flightModelConfig",
	"terrainSdfDefaults",
	"lightingModel",
	"defaultPlayerModelScale",
	"defaultWalkingModelScale",
	"flightSimMode",
	"ensureFlightSpawnSafety",
	"forceStateSync",
	"frameImage",
	"frameImageH",
	"frameImageW",
	"getActiveModelRole",
	"getConfiguredModelForRole",
	"getRelayStatus",
	"getRoleOrientation",
	"graphicsBackend",
	"graphicsSettings",
	"groundNetState",
	"hudSettings",
	"invalidateMapCache",
	"invertLookY",
	"loadPlayerModelFromPath",
	"localCallsign",
	"localPlayerObject",
	"logger",
	"love",
	"mapState",
	"modelLoadPrompt",
	"modelLoadTargetRole",
	"modelTransferState",
	"mouseSensitivity",
	"normalizeRoleId",
	"objects",
	"pendingRestartRendererPreference",
	"playerModelCache",
	"playerPlaneModelHash",
	"playerPlaneModelScale",
	"playerWalkingModelHash",
	"playerWalkingModelScale",
	"q",
	"onRoleOrientationChanged",
	"rebuildGroundFromParams",
	"reconnectRelay",
	"renderer",
	"resetHudCaches",
	"resetAltLookState",
	"resetCameraTransform",
	"resolveActiveRenderCamera",
	"screen",
	"setFlightMode",
	"setActivePlayerModel",
	"setMouseCapture",
	"setRendererPreference",
	"showCrosshair",
	"showDebugOverlay",
	"sunSettings",
	"syncActivePlayerModelState",
	"syncChunkingWithDrawDistance",
	"syncLocalPlayerObject",
	"updateGroundStreaming",
	"useGpuRenderer",
	"loadPlayerModelFromRaw",
	"viewMath",
	"viewState",
	"windState",
	"worldRenderCanvas",
	"worldRenderH",
	"worldRenderW",
	"zBuffer"
}

local REQUIRED_WRITE_BINDINGS = {
	"colorCorruptionMode",
	"currentGraphicsApiPreference",
	"flightSimMode",
	"frameImage",
	"frameImageH",
	"frameImageW",
	"invertLookY",
	"localCallsign",
	"modelLoadTargetRole",
	"mouseSensitivity",
	"pendingRestartRendererPreference",
	"playerPlaneModelScale",
	"playerWalkingModelScale",
	"showCrosshair",
	"showDebugOverlay",
	"useGpuRenderer",
	"worldRenderCanvas",
	"worldRenderH",
	"worldRenderW",
	"zBuffer"
}

local function hasReadableBinding(bindings, key)
	local values = bindings.values or {}
	if values[key] ~= nil then
		return true
	end
	local getters = bindings.getters or {}
	return type(getters[key]) == "function"
end

local function assertRequiredBindings(bindings)
	local missingReads = {}
	for _, key in ipairs(REQUIRED_READ_BINDINGS) do
		if not hasReadableBinding(bindings, key) then
			missingReads[#missingReads + 1] = key
		end
	end
	if #missingReads > 0 then
		table.sort(missingReads)
		error(
			"PauseMenuSystem.create missing readable bindings: " .. table.concat(missingReads, ", "),
			2
		)
	end

	local setters = bindings.setters or {}
	local missingWrites = {}
	for _, key in ipairs(REQUIRED_WRITE_BINDINGS) do
		if type(setters[key]) ~= "function" then
			missingWrites[#missingWrites + 1] = key
		end
	end
	if #missingWrites > 0 then
		table.sort(missingWrites)
		error(
			"PauseMenuSystem.create missing writable bindings (setters): " .. table.concat(missingWrites, ", "),
			2
		)
	end
end

-- Creates an isolated environment that resolves reads/writes through caller-provided bindings.
local function buildEnvironment(bindings)
	local values = bindings.values or {}
	local getters = bindings.getters or {}
	local setters = bindings.setters or {}
	local allowGlobalReads = bindings.allowGlobalReads or {}
	local allowedValueWrites = bindings.allowedValueWrites or {}
	local hasValue = {}
	local strictWrites = false

	for key in pairs(values) do
		hasValue[key] = true
	end

	local env = {}
	setmetatable(env, {
		__index = function(_, key)
			local getter = getters[key]
			if getter then
				return getter()
			end

			if hasValue[key] then
				return values[key]
			end

			local globalValue = rawget(_G, key)
			if globalValue ~= nil then
				if STANDARD_GLOBALS[key] or allowGlobalReads[key] then
					return globalValue
				end
				error(
					"PauseMenuSystem accessed non-bound global '" .. tostring(key) ..
					"'. Pass it through bindings.values/getters.",
					2
				)
			end
			error(
				"PauseMenuSystem unresolved symbol '" .. tostring(key) ..
				"'. Add it to bindings.values/getters.",
				2
			)
		end,
		__newindex = function(_, key, value)
			local setter = setters[key]
			if setter then
				setter(value)
				return
			end

			if strictWrites and (not hasValue[key]) and (not allowedValueWrites[key]) then
				error(
					"PauseMenuSystem attempted to write undeclared key '" .. tostring(key) ..
					"'. Add a setter or whitelist via bindings.allowedValueWrites.",
					2
				)
			end

			hasValue[key] = true
			values[key] = value
		end
	})
	return env, {
		lockWrites = function()
			strictWrites = true
		end
	}
end

local function bindEnvironment(fn, env)
	local legacySetfenv = rawget(_G, "setfenv")
	if type(legacySetfenv) == "function" then
		legacySetfenv(fn, env)
		return fn
	end

	local dbg = rawget(_G, "debug")
	if not dbg then
		return fn
	end

	local getupvalue = dbg.getupvalue
	local upvaluejoin = dbg.upvaluejoin
	if type(getupvalue) ~= "function" or type(upvaluejoin) ~= "function" then
		return fn
	end

	local index = 1
	while true do
		local name = getupvalue(fn, index)
		if name == "_ENV" then
			upvaluejoin(fn, index, function()
				return env
			end, 1)
			break
		end
		if name == nil then
			break
		end
		index = index + 1
	end

	return fn
end

function M.create(bindings)
	bindings = bindings or {}
	assertRequiredBindings(bindings)
	local environment, envControl = buildEnvironment(bindings)

	local function bootstrap()
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
		{ id = "flight",    label = "Flight Mode",     kind = "toggle", help = "Toggle between rigid-body prop flight and walking mode." },
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
				{ id = "render_scale",  label = "Render Scale",      kind = "range",  min = 0.5, max = 1.5, step = 0.05, help = "Internal 3D render scale separate from display resolution." },
				{ id = "draw_distance", label = "Draw Distance",     kind = "range",  min = 300, max = 5000, step = 100, help = "Controls object culling and terrain streaming radius." },
				{ id = "vsync",         label = "VSync",             kind = "toggle", help = "Toggle vertical sync for display presentation." },
				{ id = "renderer",      label = "Renderer",          kind = "toggle", help = "Switch between GPU and CPU renderer paths." },
				{ id = "graphics_api",  label = "Graphics API",      kind = "cycle",  help = "Choose startup graphics API preference. Applying this restarts the game." },
				{ id = "apply_graphics_api", label = "Apply API", kind = "action", confirm = true, help = "Restart now to apply graphics API changes and restore world/game state." }
			},
			camera = {
				{ id = "fov",         label = "Field of View",     kind = "range",  min = 60, max = 120, step = 5, help = "Adjust camera FOV in degrees." },
				{ id = "speed",       label = "Move Speed",        kind = "range",  min = 2, max = 30, step = 1, help = "Adjust player movement speed." },
				{ id = "sensitivity", label = "Mouse Sensitivity", kind = "range",  min = 0.0004, max = 0.004, step = 0.0002, help = "Adjust mouse look sensitivity." },
				{ id = "invertY",     label = "Invert Look Y",     kind = "toggle", help = "Invert vertical mouse look direction." },
				{ id = "crosshair",   label = "Crosshair",         kind = "toggle", help = "Show or hide center crosshair dot." },
				{ id = "overlay",     label = "Debug Overlay",     kind = "toggle", help = "Toggle top-left help/perf text." }
			},
			sound = {
				{ id = "audio_enabled",        label = "Audio",             kind = "toggle", help = "Enable or disable procedural in-flight audio." },
				{ id = "audio_master_volume",  label = "Master Volume",     kind = "range",  min = 0.0, max = 1.5, step = 0.05, help = "Overall audio gain." },
				{ id = "audio_engine_volume",  label = "Engine Volume",     kind = "range",  min = 0.0, max = 1.5, step = 0.05, help = "Engine synth layer volume." },
				{ id = "audio_ambient_volume", label = "Ambient Volume",    kind = "range",  min = 0.0, max = 1.5, step = 0.05, help = "Wind/water ambience volume." },
				{ id = "audio_engine_pitch",   label = "Engine Pitch",      kind = "range",  min = 0.3, max = 2.2, step = 0.05, help = "Pitch multiplier for engine layer." },
				{ id = "audio_ambient_pitch",  label = "Ambient Pitch",     kind = "range",  min = 0.3, max = 2.2, step = 0.05, help = "Pitch multiplier for ambience layer." }
			},
			flight = {
				{ id = "fm_mass",            label = "Mass (kg)",         kind = "range", min = 450, max = 4000, step = 10, help = "Aircraft mass used by the 6-DoF rigid-body integrator." },
				{ id = "fm_thrust",          label = "Max Thrust (N)",    kind = "range", min = 800, max = 12000, step = 50, help = "Sea-level maximum prop thrust." },
				{ id = "fm_cl_alpha",        label = "CLalpha",           kind = "range", min = 2.0, max = 8.5, step = 0.05, help = "Lift slope per radian; higher values increase lift responsiveness." },
				{ id = "fm_cd0",             label = "CD0",               kind = "range", min = 0.01, max = 0.12, step = 0.002, help = "Zero-lift drag coefficient." },
				{ id = "fm_induced_drag",    label = "Induced Drag K",    kind = "range", min = 0.01, max = 0.18, step = 0.002, help = "Quadratic induced drag term." },
				{ id = "fm_cm_alpha",        label = "CmAlpha",           kind = "range", min = -2.8, max = -0.1, step = 0.02, help = "Pitch static stability derivative (negative is stable)." },
				{ id = "fm_trim_auto",       label = "Auto Trim",         kind = "toggle", help = "Enable automatic trim assist for near-level cruise." },
				{ id = "fm_ground_friction", label = "Ground Friction",    kind = "range", min = 0.20, max = 0.995, step = 0.01, help = "Tangential damping applied on SDF contact." }
			},
			terrain = {
				{ id = "terrain_seed",            label = "Seed",               kind = "range", min = 1, max = 999999, step = 1, help = "Deterministic world seed shared across peers." },
				{ id = "terrain_height_amp",      label = "Height Amp",         kind = "range", min = 5, max = 420, step = 5, help = "Macro terrain height amplitude." },
				{ id = "terrain_height_freq",     label = "Height Freq",        kind = "range", min = 0.0002, max = 0.01, step = 0.0001, help = "Macro terrain frequency." },
				{ id = "terrain_cave_enabled",    label = "Caves",              kind = "toggle", help = "Enable volumetric cave carving noise." },
				{ id = "terrain_cave_strength",   label = "Cave Strength",      kind = "range", min = 2, max = 96, step = 1, help = "Carving strength for cave density subtraction." },
				{ id = "terrain_tunnel_count",    label = "Tunnel Count",       kind = "range", min = 0, max = 64, step = 1, help = "Number of deterministic tunnel worms." },
				{ id = "terrain_tunnel_radius_min", label = "Tunnel R Min",     kind = "range", min = 2, max = 40, step = 1, help = "Minimum tunnel radius." },
				{ id = "terrain_tunnel_radius_max", label = "Tunnel R Max",     kind = "range", min = 2, max = 60, step = 1, help = "Maximum tunnel radius." },
				{ id = "terrain_lod0_radius",     label = "LOD0 Radius",        kind = "range", min = 1, max = 6, step = 1, help = "High-detail chunk radius around the player." },
				{ id = "terrain_lod1_radius",     label = "LOD1 Radius",        kind = "range", min = 2, max = 10, step = 1, help = "Coarse-detail chunk radius around the player." },
				{ id = "terrain_mesh_budget",     label = "Mesh Budget",        kind = "range", min = 1, max = 8, step = 1, help = "Maximum chunk builds finalized per frame." }
			},
			lighting = {
				{ id = "sun_marker",          label = "Sun Marker",      kind = "toggle", help = "Show a large debug cube at the sun direction." },
				{ id = "sun_yaw",             label = "Sun Yaw",         kind = "range",  min = -180, max = 180, step = 2, help = "Rotate sun around world up axis." },
				{ id = "sun_pitch",           label = "Sun Pitch",       kind = "range",  min = -85, max = 85, step = 2, help = "Raise/lower sun elevation angle." },
				{ id = "sun_intensity",       label = "Sun Intensity",   kind = "range",  min = 0.0, max = 8.0, step = 0.1, help = "Direct light intensity for PBR highlights." },
				{ id = "sun_ambient",         label = "Sun Ambient",     kind = "range",  min = 0.0, max = 1.5, step = 0.02, help = "Ambient fill light to lift dark surfaces." },
				{ id = "sun_marker_distance", label = "Sun Box Dist",    kind = "range",  min = 200, max = 6000, step = 50, help = "Distance from camera to the sun debug cube." },
				{ id = "sun_marker_size",     label = "Sun Box Size",    kind = "range",  min = 10,   max = 600,  step = 10, help = "Scale of the debug sun cube." },
				{ id = "light_shadow_enabled",  label = "Shadows",            kind = "toggle", help = "Enable directional shadow attenuation in GPU path." },
				{ id = "light_shadow_softness", label = "Shadow Softness",    kind = "range", min = 0.4, max = 4.0, step = 0.1, help = "Shadow filtering softness." },
				{ id = "light_shadow_distance", label = "Shadow Distance",    kind = "range", min = 200, max = 6000, step = 50, help = "Maximum shadowed distance from camera." },
				{ id = "light_fog_density",     label = "Fog Density",        kind = "range", min = 0.0, max = 0.01, step = 0.0001, help = "Distance fog density." },
				{ id = "light_fog_height",      label = "Fog Height Falloff", kind = "range", min = 0.0, max = 0.02, step = 0.0001, help = "Vertical fog attenuation coefficient." },
				{ id = "light_exposure_ev",     label = "Exposure EV",        kind = "range", min = -4.0, max = 4.0, step = 0.1, help = "Scene exposure offset in EV stops." },
				{ id = "light_turbidity",       label = "Turbidity",          kind = "range", min = 1.0, max = 10.0, step = 0.1, help = "Analytic sky haze factor." }
			}
		},
	characterItems = {
		plane = {
			{ id = "load_custom_stl",         label = "Load Plane Model", kind = "action", help = "Open a path prompt and load STL, GLB, or GLTF from disk (or drop STL/GLB on the window)." },
			{ id = "model_scale",             label = "Plane Scale",    kind = "range",  min = 0.1,                                                                              max = 5.0, step = 0.05, help = "Scale normalized plane models for all clients (positive only)." },
			{ id = "plane_preview_yaw",       label = "Model Yaw",      kind = "range",  min = -360,                                                                            max = 360, step = 5,    help = "Rotate the real plane model (world + preview) around the vertical axis." },
			{ id = "plane_preview_pitch",     label = "Model Pitch",    kind = "range",  min = -360,                                                                            max = 360, step = 5,    help = "Rotate the real plane model (world + preview) around the lateral axis." },
			{ id = "plane_preview_roll",      label = "Model Roll",     kind = "range",  min = -360,                                                                            max = 360, step = 5,    help = "Rotate the real plane model (world + preview) around the forward axis." },
			{ id = "plane_preview_zoom",      label = "Preview Zoom",   kind = "range",  min = 0.5,                                                                             max = 4.0, step = 0.05, help = "Scale only the preview display model." },
			{ id = "plane_preview_auto_spin", label = "Auto Spin",      kind = "toggle", help = "When enabled, preview yaw rotates automatically." }
		},
		walking = {
			{ id = "load_walking_stl",          label = "Load Walking Model", kind = "action", help = "Load a separate STL, GLB, or GLTF used only when not in flight mode." },
			{ id = "walking_model_scale",       label = "Walking Scale",    kind = "range",  min = 0.1,                                                      max = 3.0, step = 0.05, help = "Scale used for walking-mode player model." },
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
				{ keys = "[ / ]",             text = "Cycle sub-pages when available (Settings, Characters, and HUD)." },
				{ keys = "W/S or Up/Down",    text = "Move selection in list pages or Controls tab." },
				{ keys = "A/D or Left/Right", text = "Adjust selected values or select controls binding slot." },
				{ keys = "Enter",             text = "Activate item or start binding capture in Controls." },
				{ keys = "Mouse Wheel",       text = "Adjust ranges (list pages) or scroll (Help/Controls)." },
				{ keys = "Esc",               text = "Resume game, or cancel bind capture first." }
			}
		}
	}
end

local function refreshUiFonts()
	local baseSize = math.max(14, math.floor(screen.h * 0.023))
	pauseTitleFont = love.graphics.newFont(math.max(24, baseSize + 8))
	pauseItemFont = love.graphics.newFont(baseSize)
end

local setPauseState

local function setPromptTextInputEnabled(enabled)
	if love.keyboard and type(love.keyboard.setTextInput) == "function" then
		love.keyboard.setTextInput(enabled and true or false)
	end
end

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
			{ id = "graphics", label = "Graphics" },
			{ id = "camera",   label = "Camera" },
			{ id = "sound",    label = "Sound" },
			{ id = "flight",   label = "Flight Tuning" },
			{ id = "terrain",  label = "Terrain" },
			{ id = "lighting", label = "Lighting" }
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

	local sourceVersion = tonumber(love.restart._l2d3d_restart_version)
	if sourceVersion == RESTART_STATE_VERSION then
		return love.restart
	end

	local migrated, migratedFromOrErr = restartSnapshotCompat.migrate(love.restart, RESTART_STATE_VERSION, {
		defaultPlaneScale = defaultPlayerModelScale,
		defaultWalkingScale = defaultWalkingModelScale
	})
	if migrated then
		if logger and logger.log then
			logger.log(string.format(
				"Migrated restart snapshot version %s -> %d.",
				tostring(sourceVersion),
				RESTART_STATE_VERSION
			))
		end
		return migrated
	end

	if logger and logger.log then
		logger.log(string.format(
			"Dropped restart snapshot: unsupported version '%s' (expected %d).",
			tostring(sourceVersion),
			RESTART_STATE_VERSION
		))
		if migratedFromOrErr and migratedFromOrErr ~= "" then
			logger.log("Restart payload migration detail: " .. tostring(migratedFromOrErr))
		end
	end
	return nil
end

function refreshGraphicsBackendInfo()
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
		scale = math.max(0.1, math.abs(tonumber(scale) or 1))
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
			audioSettings = deepCopyPrimitiveTable(audioSettings, 3),
			sunSettings = deepCopyPrimitiveTable(sunSettings, 3),
			flightModelConfig = deepCopyPrimitiveTable(flightModelConfig, 4),
			terrainSdfDefaults = deepCopyPrimitiveTable(terrainSdfDefaults, 4),
			lightingModel = deepCopyPrimitiveTable(lightingModel, 3),
			characterOrientation = deepCopyPrimitiveTable(characterOrientation, 3),
			camera = deepCopyPrimitiveTable({
				pos = camera and camera.pos,
				rot = camera and camera.rot,
				vel = camera and camera.vel,
				flightVel = camera and camera.flightVel,
				flightAngVel = camera and camera.flightAngVel,
				flightRotVel = camera and camera.flightRotVel,
				flightSimState = camera and camera.flightSimState,
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
	local minScale = 0.1
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

	if type(snapshot.audioSettings) == "table" then
		for key, value in pairs(snapshot.audioSettings) do
			if audioSettings[key] ~= nil then
				audioSettings[key] = value
			end
		end
	end

	if type(snapshot.sunSettings) == "table" then
		for key, value in pairs(snapshot.sunSettings) do
			sunSettings[key] = value
		end
		applySunSettingsToRenderer()
	end

	if type(snapshot.flightModelConfig) == "table" then
		for key, value in pairs(snapshot.flightModelConfig) do
			flightModelConfig[key] = value
		end
	end

	if type(snapshot.terrainSdfDefaults) == "table" then
		for key, value in pairs(snapshot.terrainSdfDefaults) do
			terrainSdfDefaults[key] = value
		end
	end

	if type(snapshot.lightingModel) == "table" then
		for key, value in pairs(snapshot.lightingModel) do
			lightingModel[key] = value
		end
		applySunSettingsToRenderer()
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
		if type(cam.flightAngVel) == "table" then
			camera.flightAngVel = {
				tonumber(cam.flightAngVel[1]) or 0,
				tonumber(cam.flightAngVel[2]) or 0,
				tonumber(cam.flightAngVel[3]) or 0
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
		if type(cam.flightSimState) == "table" then
			camera.flightSimState = deepCopyPrimitiveTable(cam.flightSimState, 5)
		end
		camera.throttle = clamp(tonumber(cam.throttle) or camera.throttle, 0, 1)
		camera.speed = tonumber(cam.speed) or camera.speed
		camera.baseFov = tonumber(cam.baseFov) or camera.baseFov
		camera.fov = tonumber(cam.fov) or camera.baseFov
		camera.flightMass = tonumber(flightModelConfig.massKg) or camera.flightMass
		camera.flightThrustForce = tonumber(flightModelConfig.maxThrustSeaLevel) or camera.flightThrustForce
		camera.flightThrustAccel = tonumber(flightModelConfig.maxThrustSeaLevel) or camera.flightThrustAccel
		camera.flightGroundFriction = tonumber(flightModelConfig.groundFriction) or camera.flightGroundFriction
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

	if desiredFlightMode and type(ensureFlightSpawnSafety) == "function" then
		ensureFlightSpawnSafety(45, 32)
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
	if item.id == "audio_enabled" then
		return (audioSettings.enabled ~= false) and "On" or "Off"
	end
	if item.id == "audio_master_volume" then
		return string.format("%d%%", math.floor(clamp((tonumber(audioSettings.masterVolume) or 0) * 100, 0, 150) + 0.5))
	end
	if item.id == "audio_engine_volume" then
		return string.format("%d%%", math.floor(clamp((tonumber(audioSettings.engineVolume) or 0) * 100, 0, 150) + 0.5))
	end
	if item.id == "audio_ambient_volume" then
		return string.format("%d%%", math.floor(clamp((tonumber(audioSettings.ambienceVolume) or 0) * 100, 0, 150) + 0.5))
	end
	if item.id == "audio_engine_pitch" then
		return string.format("%.2fx", tonumber(audioSettings.enginePitch) or 1.0)
	end
	if item.id == "audio_ambient_pitch" then
		return string.format("%.2fx", tonumber(audioSettings.ambiencePitch) or 1.0)
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
	if item.id == "fm_mass" then
		return string.format("%.0f", tonumber(flightModelConfig.massKg) or 0)
	end
	if item.id == "fm_thrust" then
		return string.format("%.0f", tonumber(flightModelConfig.maxThrustSeaLevel) or 0)
	end
	if item.id == "fm_cl_alpha" then
		return string.format("%.2f", tonumber(flightModelConfig.CLalpha) or 0)
	end
	if item.id == "fm_cd0" then
		return string.format("%.3f", tonumber(flightModelConfig.CD0) or 0)
	end
	if item.id == "fm_induced_drag" then
		return string.format("%.3f", tonumber(flightModelConfig.inducedDragK) or 0)
	end
	if item.id == "fm_cm_alpha" then
		return string.format("%.2f", tonumber(flightModelConfig.CmAlpha) or 0)
	end
	if item.id == "fm_trim_auto" then
		return (flightModelConfig.enableAutoTrim ~= false) and "On" or "Off"
	end
	if item.id == "fm_ground_friction" then
		return string.format("%.2f", tonumber(flightModelConfig.groundFriction) or 0)
	end
	local terrainParams = activeGroundParams or defaultGroundParams
	if item.id == "terrain_seed" then
		return string.format("%d", math.floor(tonumber(terrainParams.seed) or 0))
	end
	if item.id == "terrain_height_amp" then
		return string.format("%.1f", tonumber(terrainParams.heightAmplitude) or 0)
	end
	if item.id == "terrain_height_freq" then
		return string.format("%.4f", tonumber(terrainParams.heightFrequency) or 0)
	end
	if item.id == "terrain_cave_enabled" then
		return (terrainParams.caveEnabled ~= false) and "On" or "Off"
	end
	if item.id == "terrain_cave_strength" then
		return string.format("%.1f", tonumber(terrainParams.caveStrength) or 0)
	end
	if item.id == "terrain_tunnel_count" then
		return string.format("%d", math.floor(tonumber(terrainParams.tunnelCount) or 0))
	end
	if item.id == "terrain_tunnel_radius_min" then
		return string.format("%.1f", tonumber(terrainParams.tunnelRadiusMin) or 0)
	end
	if item.id == "terrain_tunnel_radius_max" then
		return string.format("%.1f", tonumber(terrainParams.tunnelRadiusMax) or 0)
	end
	if item.id == "terrain_lod0_radius" then
		return string.format("%d", math.floor(tonumber(terrainParams.lod0Radius) or 0))
	end
	if item.id == "terrain_lod1_radius" then
		return string.format("%d", math.floor(tonumber(terrainParams.lod1Radius) or 0))
	end
	if item.id == "terrain_mesh_budget" then
		return string.format("%d", math.floor(tonumber(terrainParams.meshBuildBudget) or 0))
	end
	if item.id == "light_shadow_enabled" then
		local enabled = (sunSettings.shadowEnabled ~= nil and sunSettings.shadowEnabled) or lightingModel.shadowEnabled
		return enabled and "On" or "Off"
	end
	if item.id == "light_shadow_softness" then
		return string.format("%.2f", tonumber(sunSettings.shadowSoftness) or tonumber(lightingModel.shadowSoftness) or 0)
	end
	if item.id == "light_shadow_distance" then
		return string.format("%dm", math.floor((tonumber(sunSettings.shadowDistance) or tonumber(lightingModel.shadowDistance) or 0) + 0.5))
	end
	if item.id == "light_fog_density" then
		return string.format("%.4f", tonumber(sunSettings.fogDensity) or tonumber(lightingModel.fogDensity) or 0)
	end
	if item.id == "light_fog_height" then
		return string.format("%.4f", tonumber(sunSettings.fogHeightFalloff) or tonumber(lightingModel.fogHeightFalloff) or 0)
	end
	if item.id == "light_exposure_ev" then
		return string.format("%.1f", tonumber(sunSettings.exposureEV) or tonumber(lightingModel.exposureEV) or 0)
	end
	if item.id == "light_turbidity" then
		return string.format("%.1f", tonumber(sunSettings.turbidity) or tonumber(lightingModel.turbidity) or 0)
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

	if item.id == "audio_master_volume" then
		audioSettings.masterVolume = clamp(
			(tonumber(audioSettings.masterVolume) or 1.0) + item.step * direction * scale,
			item.min,
			item.max
		)
		audioSettings.masterVolume = math.floor(audioSettings.masterVolume * 100 + 0.5) / 100
		setPauseStatus("Master Volume: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "audio_engine_volume" then
		audioSettings.engineVolume = clamp(
			(tonumber(audioSettings.engineVolume) or 1.0) + item.step * direction * scale,
			item.min,
			item.max
		)
		audioSettings.engineVolume = math.floor(audioSettings.engineVolume * 100 + 0.5) / 100
		setPauseStatus("Engine Volume: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "audio_ambient_volume" then
		audioSettings.ambienceVolume = clamp(
			(tonumber(audioSettings.ambienceVolume) or 1.0) + item.step * direction * scale,
			item.min,
			item.max
		)
		audioSettings.ambienceVolume = math.floor(audioSettings.ambienceVolume * 100 + 0.5) / 100
		setPauseStatus("Ambient Volume: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "audio_engine_pitch" then
		audioSettings.enginePitch = clamp(
			(tonumber(audioSettings.enginePitch) or 1.0) + item.step * direction * scale,
			item.min,
			item.max
		)
		audioSettings.enginePitch = math.floor(audioSettings.enginePitch * 100 + 0.5) / 100
		setPauseStatus("Engine Pitch: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "audio_ambient_pitch" then
		audioSettings.ambiencePitch = clamp(
			(tonumber(audioSettings.ambiencePitch) or 1.0) + item.step * direction * scale,
			item.min,
			item.max
		)
		audioSettings.ambiencePitch = math.floor(audioSettings.ambiencePitch * 100 + 0.5) / 100
		setPauseStatus("Ambient Pitch: " .. getPauseItemValue(item), 1.2)
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

	if item.id == "fm_mass" then
		flightModelConfig.massKg = clamp((tonumber(flightModelConfig.massKg) or 1150) + item.step * direction * scale, item.min, item.max)
		if camera then
			camera.flightMass = flightModelConfig.massKg
		end
		setPauseStatus("Mass (kg): " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "fm_thrust" then
		flightModelConfig.maxThrustSeaLevel = clamp((tonumber(flightModelConfig.maxThrustSeaLevel) or 3200) + item.step * direction * scale, item.min, item.max)
		if camera then
			camera.flightThrustForce = flightModelConfig.maxThrustSeaLevel
			camera.flightThrustAccel = flightModelConfig.maxThrustSeaLevel
		end
		setPauseStatus("Max Thrust: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "fm_cl_alpha" then
		flightModelConfig.CLalpha = clamp((tonumber(flightModelConfig.CLalpha) or 5.5) + item.step * direction * scale, item.min, item.max)
		setPauseStatus("CLalpha: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "fm_cd0" then
		flightModelConfig.CD0 = clamp((tonumber(flightModelConfig.CD0) or 0.03) + item.step * direction * scale, item.min, item.max)
		setPauseStatus("CD0: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "fm_induced_drag" then
		flightModelConfig.inducedDragK = clamp((tonumber(flightModelConfig.inducedDragK) or 0.045) + item.step * direction * scale, item.min, item.max)
		setPauseStatus("Induced Drag K: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "fm_cm_alpha" then
		flightModelConfig.CmAlpha = clamp((tonumber(flightModelConfig.CmAlpha) or -1.2) + item.step * direction * scale, item.min, item.max)
		setPauseStatus("CmAlpha: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "fm_ground_friction" then
		flightModelConfig.groundFriction = clamp((tonumber(flightModelConfig.groundFriction) or 0.92) + item.step * direction * scale, item.min, item.max)
		if camera then
			camera.flightGroundFriction = flightModelConfig.groundFriction
		end
		setPauseStatus("Ground Friction: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "terrain_seed" or
		item.id == "terrain_height_amp" or
		item.id == "terrain_height_freq" or
		item.id == "terrain_cave_strength" or
		item.id == "terrain_tunnel_count" or
		item.id == "terrain_tunnel_radius_min" or
		item.id == "terrain_tunnel_radius_max" or
		item.id == "terrain_lod0_radius" or
		item.id == "terrain_lod1_radius" or
		item.id == "terrain_mesh_budget" then
		local params = deepCopyPrimitiveTable(activeGroundParams or defaultGroundParams, 4) or {}
		local delta = item.step * direction * scale
		if item.id == "terrain_seed" then
			params.seed = math.floor(clamp((tonumber(params.seed) or 1) + delta, item.min, item.max))
			terrainSdfDefaults.seed = params.seed
		elseif item.id == "terrain_height_amp" then
			params.heightAmplitude = clamp((tonumber(params.heightAmplitude) or 120) + delta, item.min, item.max)
			terrainSdfDefaults.heightAmplitude = params.heightAmplitude
		elseif item.id == "terrain_height_freq" then
			params.heightFrequency = clamp((tonumber(params.heightFrequency) or 0.0018) + delta, item.min, item.max)
			terrainSdfDefaults.heightFrequency = params.heightFrequency
		elseif item.id == "terrain_cave_strength" then
			params.caveStrength = clamp((tonumber(params.caveStrength) or 42) + delta, item.min, item.max)
			terrainSdfDefaults.caveStrength = params.caveStrength
		elseif item.id == "terrain_tunnel_count" then
			params.tunnelCount = math.floor(clamp((tonumber(params.tunnelCount) or 12) + delta, item.min, item.max))
			terrainSdfDefaults.tunnelCount = params.tunnelCount
		elseif item.id == "terrain_tunnel_radius_min" then
			params.tunnelRadiusMin = clamp((tonumber(params.tunnelRadiusMin) or 9) + delta, item.min, item.max)
			params.tunnelRadiusMax = math.max(params.tunnelRadiusMin, tonumber(params.tunnelRadiusMax) or params.tunnelRadiusMin)
			terrainSdfDefaults.tunnelRadiusMin = params.tunnelRadiusMin
			terrainSdfDefaults.tunnelRadiusMax = params.tunnelRadiusMax
		elseif item.id == "terrain_tunnel_radius_max" then
			params.tunnelRadiusMax = clamp((tonumber(params.tunnelRadiusMax) or 18) + delta, item.min, item.max)
			params.tunnelRadiusMin = math.min(params.tunnelRadiusMax, tonumber(params.tunnelRadiusMin) or params.tunnelRadiusMax)
			terrainSdfDefaults.tunnelRadiusMin = params.tunnelRadiusMin
			terrainSdfDefaults.tunnelRadiusMax = params.tunnelRadiusMax
		elseif item.id == "terrain_lod0_radius" then
			params.lod0Radius = math.floor(clamp((tonumber(params.lod0Radius) or 2) + delta, item.min, item.max))
			params.lod1Radius = math.max(params.lod0Radius, tonumber(params.lod1Radius) or params.lod0Radius)
			terrainSdfDefaults.lod0Radius = params.lod0Radius
			terrainSdfDefaults.lod1Radius = params.lod1Radius
		elseif item.id == "terrain_lod1_radius" then
			params.lod1Radius = math.floor(clamp((tonumber(params.lod1Radius) or 4) + delta, item.min, item.max))
			params.lod0Radius = math.min(params.lod1Radius, tonumber(params.lod0Radius) or params.lod1Radius)
			terrainSdfDefaults.lod0Radius = params.lod0Radius
			terrainSdfDefaults.lod1Radius = params.lod1Radius
		elseif item.id == "terrain_mesh_budget" then
			params.meshBuildBudget = math.floor(clamp((tonumber(params.meshBuildBudget) or 2) + delta, item.min, item.max))
			terrainSdfDefaults.meshBuildBudget = params.meshBuildBudget
		end
		rebuildGroundFromParams(params, "pause terrain tuning")
		setPauseStatus(item.label .. ": " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "light_shadow_softness" then
		local v = clamp((tonumber(sunSettings.shadowSoftness) or tonumber(lightingModel.shadowSoftness) or 1.6) + item.step * direction * scale, item.min, item.max)
		sunSettings.shadowSoftness = v
		lightingModel.shadowSoftness = v
		applySunSettingsToRenderer()
		setPauseStatus("Shadow Softness: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "light_shadow_distance" then
		local v = clamp((tonumber(sunSettings.shadowDistance) or tonumber(lightingModel.shadowDistance) or 1800) + item.step * direction * scale, item.min, item.max)
		sunSettings.shadowDistance = math.floor(v + 0.5)
		lightingModel.shadowDistance = sunSettings.shadowDistance
		applySunSettingsToRenderer()
		setPauseStatus("Shadow Distance: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "light_fog_density" then
		local v = clamp((tonumber(sunSettings.fogDensity) or tonumber(lightingModel.fogDensity) or 0.00055) + item.step * direction * scale, item.min, item.max)
		sunSettings.fogDensity = v
		lightingModel.fogDensity = v
		applySunSettingsToRenderer()
		setPauseStatus("Fog Density: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "light_fog_height" then
		local v = clamp((tonumber(sunSettings.fogHeightFalloff) or tonumber(lightingModel.fogHeightFalloff) or 0.0017) + item.step * direction * scale, item.min, item.max)
		sunSettings.fogHeightFalloff = v
		lightingModel.fogHeightFalloff = v
		applySunSettingsToRenderer()
		setPauseStatus("Fog Height: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "light_exposure_ev" then
		local v = clamp((tonumber(sunSettings.exposureEV) or tonumber(lightingModel.exposureEV) or 0) + item.step * direction * scale, item.min, item.max)
		sunSettings.exposureEV = math.floor(v * 10 + 0.5) / 10
		lightingModel.exposureEV = sunSettings.exposureEV
		applySunSettingsToRenderer()
		setPauseStatus("Exposure EV: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "light_turbidity" then
		local v = clamp((tonumber(sunSettings.turbidity) or tonumber(lightingModel.turbidity) or 2.4) + item.step * direction * scale, item.min, item.max)
		sunSettings.turbidity = math.floor(v * 10 + 0.5) / 10
		lightingModel.turbidity = sunSettings.turbidity
		applySunSettingsToRenderer()
		setPauseStatus("Turbidity: " .. getPauseItemValue(item), 1.2)
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
		setPromptTextInputEnabled(true)
		setPauseStatus("Callsign: Enter to apply, Esc to cancel.", 4.5)
	elseif item.id == "load_custom_stl" then
		modelLoadTargetRole = "plane"
		modelLoadPrompt.active = true
		modelLoadPrompt.mode = "model_path"
		modelLoadPrompt.text = ""
		modelLoadPrompt.cursor = 0
		setPromptTextInputEnabled(true)
		setPauseStatus("Plane model path: Enter to load, Esc to cancel.", 4.5)
	elseif item.id == "load_walking_stl" then
		modelLoadTargetRole = "walking"
		modelLoadPrompt.active = true
		modelLoadPrompt.mode = "model_path"
		modelLoadPrompt.text = ""
		modelLoadPrompt.cursor = 0
		setPromptTextInputEnabled(true)
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
	elseif item.id == "fm_trim_auto" then
		flightModelConfig.enableAutoTrim = not (flightModelConfig.enableAutoTrim ~= false)
		setPauseStatus("Auto Trim: " .. getPauseItemValue(item), 1.2)
	elseif item.id == "terrain_cave_enabled" then
		local params = deepCopyPrimitiveTable(activeGroundParams or defaultGroundParams, 4) or {}
		params.caveEnabled = not (params.caveEnabled ~= false)
		terrainSdfDefaults.caveEnabled = params.caveEnabled
		rebuildGroundFromParams(params, "pause terrain tuning")
		setPauseStatus("Caves: " .. getPauseItemValue(item), 1.2)
	elseif item.id == "light_shadow_enabled" then
		local enabled = (sunSettings.shadowEnabled ~= nil and sunSettings.shadowEnabled) or lightingModel.shadowEnabled
		enabled = not enabled
		sunSettings.shadowEnabled = enabled
		lightingModel.shadowEnabled = enabled
		applySunSettingsToRenderer()
		setPauseStatus("Shadows: " .. getPauseItemValue(item), 1.2)
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
	elseif item.id == "audio_enabled" then
		audioSettings.enabled = not (audioSettings.enabled ~= false)
		setPauseStatus("Audio: " .. getPauseItemValue(item), 1.2)
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
		setPromptTextInputEnabled(false)
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
		setPromptTextInputEnabled(false)
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


		return {
			pauseMenu = pauseMenu,
			refreshUiFonts = refreshUiFonts,
			setPauseStatus = setPauseStatus,
			getPauseItemsForCurrentPage = getPauseItemsForCurrentPage,
			normalizeGraphicsApiPreference = normalizeGraphicsApiPreference,
			getGraphicsApiPreferenceLabel = getGraphicsApiPreferenceLabel,
			getGraphicsBackendLabel = getGraphicsBackendLabel,
			getEngineRestartPayload = getEngineRestartPayload,
			refreshGraphicsBackendInfo = refreshGraphicsBackendInfo,
			syncGraphicsSettingsFromWindow = syncGraphicsSettingsFromWindow,
			applyGraphicsVideoMode = applyGraphicsVideoMode,
			deepCopyPrimitiveTable = deepCopyPrimitiveTable,
			buildRestartModelSnapshot = buildRestartModelSnapshot,
			buildRestartSnapshot = buildRestartSnapshot,
			restoreModelFromRestartSnapshot = restoreModelFromRestartSnapshot,
			applyRestartSnapshot = applyRestartSnapshot,
			requestGraphicsApiRestart = requestGraphicsApiRestart,
			resetControlBindingsToDefaults = resetControlBindingsToDefaults,
			cyclePauseTab = cyclePauseTab,
			cyclePauseSubTab = cyclePauseSubTab,
			moveControlsSelection = moveControlsSelection,
			moveControlsSlot = moveControlsSlot,
			clearSelectedControlBinding = clearSelectedControlBinding,
			beginSelectedControlBindingCapture = beginSelectedControlBindingCapture,
			cancelControlBindingCapture = cancelControlBindingCapture,
			captureBindingFromKey = captureBindingFromKey,
			captureBindingFromMouseButton = captureBindingFromMouseButton,
			captureBindingFromWheel = captureBindingFromWheel,
			captureBindingFromMouseMotion = captureBindingFromMouseMotion,
			clearPauseConfirm = clearPauseConfirm,
			clearPauseRangeDrag = clearPauseRangeDrag,
			movePauseSelection = movePauseSelection,
			adjustSelectedPauseItem = adjustSelectedPauseItem,
			adjustSelectedPauseItemCoarse = adjustSelectedPauseItemCoarse,
			activateSelectedPauseItem = activateSelectedPauseItem,
			updatePauseMenuHover = updatePauseMenuHover,
			updatePauseControlsHover = updatePauseControlsHover,
			beginPauseRangeDrag = beginPauseRangeDrag,
			updatePauseRangeDrag = updatePauseRangeDrag,
			endPauseRangeDrag = endPauseRangeDrag,
			handlePauseControlsMouseClick = handlePauseControlsMouseClick,
			handlePauseMenuMouseClick = handlePauseMenuMouseClick,
			setPauseState = setPauseState,
			drawModelLoadPrompt = drawModelLoadPrompt,
			drawPauseMenu = drawPauseMenu,
			getControlActionsCount = function()
				return #controlActions
			end
		}
	end

	bindEnvironment(bootstrap, environment)
	local exports = bootstrap()
	if envControl and envControl.lockWrites then
		envControl.lockWrites()
	end
	return exports
end

return M
