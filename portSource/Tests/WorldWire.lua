local worldWire = require("Source.World.WorldWire")

local function assertTrue(value, message)
	if not value then
		error(message or "assertion failed", 2)
	end
end

local function run()
	local chunk = {
		cx = 3,
		cz = -2,
		resolution = 8,
		revision = 5,
		materialRevision = 7,
		heightDeltas = { 0.0, 1.25, -2.5, 3.75 },
		volumetricOverrides = {
			{ kind = "sphere", x = 1, y = 2, z = 3, radius = 4 }
		}
	}
	local fields = worldWire.buildChunkStateFields(chunk)
	local kv = {}
	for _, entry in ipairs(fields) do
		kv[entry[1]] = entry[2]
	end
	local decodedChunk = worldWire.decodeChunkStateFields(kv)
	assertTrue(decodedChunk.cx == chunk.cx and decodedChunk.cz == chunk.cz, "chunk coords should round-trip")
	assertTrue(decodedChunk.revision == chunk.revision, "chunk revision should round-trip")
	assertTrue(#decodedChunk.heightDeltas == #chunk.heightDeltas, "height delta count should round-trip")
	assertTrue(math.abs((decodedChunk.heightDeltas[2] or 0) - 1.25) < 1e-4, "height delta values should round-trip")
	assertTrue(#decodedChunk.volumetricOverrides == 1, "volumetric overrides should round-trip")
	assertTrue(math.abs((decodedChunk.volumetricOverrides[1].radius or 0) - 4) < 1e-4, "override radius should round-trip")

	local seeds = {
		{
			radius = 6,
			hillAttached = true,
			points = {
				{ 0, 10, 0 },
				{ 12, 11, -3 }
			}
		}
	}
	local encodedSeeds = worldWire.encodeTunnelSeeds(seeds)
	local decodedSeeds = worldWire.decodeTunnelSeeds(encodedSeeds)
	assertTrue(#decodedSeeds == 1, "tunnel seed count should round-trip")
	assertTrue(decodedSeeds[1].hillAttached == true, "tunnel hillAttached flag should round-trip")
	assertTrue(#decodedSeeds[1].points == 2, "tunnel points should round-trip")

	print("World wire tests passed")
end

run()
