local M = {}

-- Builds all mutable startup state tables in one place so main.lua can focus on behavior flow.
-- @param cubeModel table Base cube model used for fallbacks and debug objects.
-- @param q table Quaternion helper module used for identity rotation initialization.
-- @return table defaults Bag of fully initialized state tables/scalars.
function M.create(cubeModel, q)
	return {
		viewState = {
			mode = "first_person",
			activeCamera = nil
		},
		autoSmoke = {
			enabled = (os.getenv("L2D3D_AUTOSMOKE") == "1"),
			elapsed = 0,
			screenshotRequested = false,
			quitAt = nil,
			screenshotName = "autosmoke_plane.png"
		},
		altLookState = {
			held = false,
			yaw = 0,
			pitch = 0,
			yawLimit = math.rad(110),
			pitchUpLimit = math.rad(55),
			pitchDownLimit = math.rad(40),
			camera = nil
		},
		thirdPersonState = {
			offset = { 0, 2.0, -7.0 },
			minDistance = 1.25,
			collisionBuffer = 0.35,
			camera = nil
		},
		mapState = {
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
		},
		geoConfig = {
			originLat = 0.0,
			originLon = 0.0,
			metersPerUnit = 1.0
		},
		hudTheme = {
			plateBg = { 0.03, 0.05, 0.08, 0.82 },
			plateHi = { 0.65, 0.82, 1.0, 0.24 },
			plateBorder = { 0.74, 0.9, 1.0, 0.88 },
			text = { 0.9, 0.96, 1.0, 0.95 },
			textDim = { 0.72, 0.84, 0.95, 0.92 },
			accent = { 0.26, 0.9, 0.55, 0.98 },
			needle = { 1.0, 0.36, 0.2, 0.98 },
			overspeed = { 1.0, 0.2, 0.16, 0.95 },
			shadow = { 0, 0, 0, 0.34 }
		},
		hudCache = {
			dialCanvas = nil,
			dialSize = 0,
			dialFontHeight = 0,
			dialProfileKey = "",
			controlCanvas = nil,
			controlW = 0,
			controlH = 0
		},
		graphicsSettings = {
			windowMode = "windowed",
			resolutionOptions = {},
			resolutionIndex = 1,
			renderScale = 1.0,
			drawDistance = 1800,
			vsync = false,
			graphicsApiPreference = "auto"
		},
		currentGraphicsApiPreference = "auto",
		graphicsBackend = {
			name = "Unknown",
			version = "",
			vendor = "",
			device = ""
		},
		restartStateVersion = 1,
		restartModelEncodedLimit = 4 * 1024 * 1024,
		hudSettings = {
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
		},
		characterOrientation = {
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
		},
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
		},
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
		},
		characterPreview = {
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
		},
		localGroundClearance = 1.8,
		modelLoadPrompt = {
			active = false,
			mode = "model_path",
			text = "",
			cursor = 0
		},
		modelLoadTargetRole = "plane",
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
		},
		worldHalfExtent = 2000,
		defaultGroundParams = {
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
		},
		windState = {
			angle = 0,
			speed = 9,
			targetAngle = 0,
			targetSpeed = 9,
			retargetIn = 0
		},
		cloudState = {
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
		},
		drawDistanceChunkingTuning = {
			cloudSpawnRatio = 0.78,
			cloudDespawnRatio = 1.06,
			cloudSpawnMin = 260,
			cloudDespawnPadding = 120
		},
		cloudNetState = {
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
		},
		groundNetState = {
			authorityPeerId = nil,
			lastSnapshotReceivedAt = -math.huge,
			lastSnapshotRequestAt = -math.huge,
			requestCooldown = 2.0
		}
	}
end

return M
