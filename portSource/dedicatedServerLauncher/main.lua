local function dirname(path)
	path = tostring(path or ""):gsub("\\", "/")
	return path:match("(.+)/[^/]+$") or "."
end

local function buildRepoRoot()
	local source = tostring(love.filesystem.getSource() or "."):gsub("\\", "/")
	if source:match("%.[^/]+$") then
		source = dirname(source)
	end
	if source:match("/dedicatedServerLauncher/?$") then
		return dirname(source)
	end
	return source
end

local repoRoot = buildRepoRoot()
package.path = table.concat({
	package.path,
		repoRoot .. "/?.lua",
		repoRoot .. "/?/init.lua"
}, ";")

netMode = "dedicated"
dofile(repoRoot .. "/main.lua")
