local compat = {}

local function clamp(value, minValue, maxValue)
	if value < minValue then
		return minValue
	end
	if value > maxValue then
		return maxValue
	end
	return value
end

local function clonePrimitive(value, depth)
	if type(value) ~= "table" then
		return value
	end
	if (depth or 0) <= 0 then
		return nil
	end
	local out = {}
	for key, entry in pairs(value) do
		local keyType = type(key)
		if keyType == "string" or keyType == "number" then
			local entryType = type(entry)
			if entryType == "number" or entryType == "string" or entryType == "boolean" then
				out[key] = entry
			elseif entryType == "table" then
				out[key] = clonePrimitive(entry, depth - 1)
			end
		end
	end
	return out
end

local function sanitizeSnapshotScale(snapshot, fallbackScale)
	if type(snapshot) ~= "table" then
		return
	end
	local raw = tonumber(snapshot.scale) or fallbackScale
	raw = math.abs(raw)
	snapshot.scale = math.max(0.1, raw)
end

local function ensureTable(parent, key)
	if type(parent[key]) ~= "table" then
		parent[key] = {}
	end
	return parent[key]
end

local function migrateV1ToV2(migrated, opts)
	local state = ensureTable(migrated, "state")

	if type(state.graphicsSettings) == "table" then
		state.graphicsSettings.renderScale = clamp(
			tonumber(state.graphicsSettings.renderScale) or 1.0,
			0.5,
			1.5
		)
	end

	local playerModels = ensureTable(state, "playerModels")
	sanitizeSnapshotScale(playerModels.plane, tonumber(opts.defaultPlaneScale) or 1.35)
	sanitizeSnapshotScale(playerModels.walking, tonumber(opts.defaultWalkingScale) or 1.0)
end

local function migrateV2ToV3(migrated, opts)
	opts = opts or {}
	local state = ensureTable(migrated, "state")
	local camera = ensureTable(state, "camera")

	if type(camera.flightAngVel) ~= "table" then
		local rotRates = (type(camera.flightRotVel) == "table") and camera.flightRotVel or {}
		camera.flightAngVel = {
			tonumber(rotRates.pitch) or 0,
			tonumber(rotRates.yaw) or 0,
			tonumber(rotRates.roll) or 0
		}
	end

	if type(camera.flightSimState) ~= "table" then
		camera.flightSimState = {
			accumulator = 0,
			tick = 0,
			engine = { throttle = clamp(tonumber(camera.throttle) or 0, 0, 1) },
			elevatorTrim = 0
		}
	else
		camera.flightSimState.accumulator = tonumber(camera.flightSimState.accumulator) or 0
		camera.flightSimState.tick = math.max(0, math.floor(tonumber(camera.flightSimState.tick) or 0))
		camera.flightSimState.engine = type(camera.flightSimState.engine) == "table" and camera.flightSimState.engine or {
			throttle = clamp(tonumber(camera.throttle) or 0, 0, 1)
		}
		camera.flightSimState.engine.throttle = clamp(tonumber(camera.flightSimState.engine.throttle) or 0, 0, 1)
		camera.flightSimState.elevatorTrim = tonumber(camera.flightSimState.elevatorTrim) or 0
	end

	if type(state.flightModelConfig) ~= "table" then
		state.flightModelConfig = {
			massKg = tonumber(camera.flightMass) or tonumber(opts.defaultMassKg) or 1150,
			maxThrustSeaLevel = tonumber(camera.flightThrustForce) or tonumber(opts.defaultMaxThrustSeaLevel) or 3200,
			groundFriction = tonumber(camera.flightGroundFriction) or tonumber(opts.defaultGroundFriction) or 0.92,
			enableAutoTrim = true
		}
	end

	if type(state.terrainSdfDefaults) ~= "table" then
		state.terrainSdfDefaults = clonePrimitive(state.groundParams, 4) or {
			chunkSize = 64,
			lod0Radius = 2,
			lod1Radius = 4,
			lod0CellSize = 4,
			lod1CellSize = 8,
			meshBuildBudget = 2,
			generatorVersion = 1
		}
	end

	if type(state.lightingModel) ~= "table" then
		local sun = (type(state.sunSettings) == "table") and state.sunSettings or {}
		state.lightingModel = {
			fogDensity = tonumber(sun.fogDensity) or 0.00055,
			fogHeightFalloff = tonumber(sun.fogHeightFalloff) or 0.0017,
			exposureEV = tonumber(sun.exposureEV) or 0.0,
			turbidity = tonumber(sun.turbidity) or 2.4,
			shadowEnabled = (sun.shadowEnabled ~= false),
			shadowSoftness = tonumber(sun.shadowSoftness) or 1.6,
			shadowDistance = tonumber(sun.shadowDistance) or 1800
		}
	end
end

function compat.migrate(payload, targetVersion, opts)
	if type(payload) ~= "table" then
		return nil, "restart payload must be a table"
	end
	local sourceVersion = tonumber(payload._l2d3d_restart_version)
	local target = math.floor(tonumber(targetVersion) or 0)
	if target <= 0 then
		return nil, "invalid target restart version"
	end
	if sourceVersion == target then
		return payload, sourceVersion
	end

	opts = opts or {}
	local migrated = clonePrimitive(payload, 8) or {}
	local originalVersion = sourceVersion
	local current = sourceVersion

	if current == 1 and target >= 2 then
		migrateV1ToV2(migrated, opts)
		current = 2
	end

	if current == 2 and target >= 3 then
		migrateV2ToV3(migrated, opts)
		current = 3
	end

	if current ~= target then
		return nil, string.format(
			"unsupported restart payload version '%s' for target version '%s'",
			tostring(sourceVersion),
			tostring(target)
		)
	end

	migrated._l2d3d_restart_version = target
	migrated._l2d3d_restart_migrated_from = originalVersion
	return migrated, originalVersion
end

return compat