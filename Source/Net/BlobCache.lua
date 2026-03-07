local userProfileStore = require "Source.Core.UserProfileStore"

local blobCache = {}

local function makeKey(kind, hash)
	return tostring(kind or "") .. "|" .. tostring(hash or "")
end

function blobCache.create(path)
	local store = {
		path = path or "server_avatar_cache.lua",
		entries = {}
	}

	local loaded = userProfileStore.load(store.path)
	if type(loaded) == "table" and type(loaded.entries) == "table" then
		store.entries = loaded.entries
	end

	function store:save()
		return userProfileStore.save({
			entries = self.entries
		}, self.path)
	end

	function store:get(kind, hash)
		return self.entries[makeKey(kind, hash)]
	end

	function store:put(kind, hash, raw, meta)
		if type(kind) ~= "string" or kind == "" or type(hash) ~= "string" or hash == "" or type(raw) ~= "string" then
			return false, "invalid blob cache entry"
		end
		self.entries[makeKey(kind, hash)] = {
			kind = kind,
			hash = hash,
			raw = raw,
			meta = meta or {}
		}
		return self:save()
	end

	function store:has(kind, hash)
		return self:get(kind, hash) ~= nil
	end

	return store
end

return blobCache
