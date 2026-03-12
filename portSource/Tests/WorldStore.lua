local worldStore = require("Source.World.WorldStore")

local function assertTrue(value, message)
	if not value then
		error(message or "assertion failed", 2)
	end
end

local function shallowCopy(src)
	local out = {}
	for key, value in pairs(src or {}) do
		if type(value) == "table" then
			local nested = {}
			for nestedKey, nestedValue in pairs(value) do
				nested[nestedKey] = nestedValue
			end
			out[key] = nested
		else
			out[key] = value
		end
	end
	return out
end

local function run()
	local worldName = string.format("test_world_%d_%d", os.time(), math.random(1000, 9999))
	local world, err = worldStore.open({
		name = worldName,
		createIfMissing = true,
		groundParams = {
			seed = 2468,
			chunkSize = 128,
			tunnelCount = 2,
			tunnelRadiusMin = 8,
			tunnelRadiusMax = 12
		},
		spawn = { x = 12, y = 34, z = 56 }
	})
	assertTrue(world ~= nil, "world should open: " .. tostring(err))

	local meta = world:getMeta()
	assertTrue(meta.worldId == worldName, "world meta should use the requested world id")
	assertTrue(type(meta.tunnelSeeds) == "table", "world meta should include tunnel seeds")

	local changed, changedChunks = world:applyCrater({
		x = 32,
		y = 0,
		z = 24,
		radius = 10,
		depth = 4,
		rim = 0.15
	})
	assertTrue(changed == true, "crater application should dirty at least one chunk")
	assertTrue(type(changedChunks) == "table" and #changedChunks > 0, "crater application should return changed chunks")
	assertTrue(world:flushDirty() > 0, "dirty world regions should flush to disk")

	local reopened = worldStore.open({
		name = worldName,
		createIfMissing = false,
		groundParams = {
			seed = 2468,
			chunkSize = 128
		}
	})
	assertTrue(reopened ~= nil, "reopened world should load from disk")
	assertTrue(math.abs(reopened:sampleHeightDelta(32, 24)) > 0.01, "persisted crater height deltas should reload")

	local latestChunk = reopened:getChunkState(changedChunks[1].cx, changedChunks[1].cz)
	assertTrue(type(latestChunk) == "table" and latestChunk.revision >= 1, "chunk state should expose a revision")
	local staleChunk = shallowCopy(latestChunk)
	staleChunk.revision = math.max(0, latestChunk.revision - 1)
	staleChunk.heightDeltas[1] = 999
	assertTrue(reopened:applyChunkState(staleChunk) == false, "stale chunk revisions should be ignored")

	local appliedInfo = reopened:applyWorldInfo({
		worldId = worldName,
		formatVersion = 3,
		seed = 2468,
		chunkSize = 128,
		horizonRadiusMeters = 4096,
		heightAmplitude = 140,
		heightFrequency = 0.0024,
		waterLevel = -9,
		tunnelSeeds = {
			{
				radius = 7,
				hillAttached = true,
				points = {
					{ 0, 24, 0 },
					{ 32, 22, 8 }
				}
			}
		},
		spawnX = 5,
		spawnY = 6,
		spawnZ = 7
	})
	assertTrue(appliedInfo == true, "world info snapshots should apply to the cache")
	local rebuiltParams = reopened:buildGroundParams({})
	assertTrue(rebuiltParams.worldFormatVersion == 3, "ground params should reflect updated world format version")
	assertTrue(rebuiltParams.tunnelSeedCount == 1, "ground params should expose tunnel seed count")

	print("World store tests passed")
end

run()
