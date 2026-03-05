local function dirname(path)
	local normalized = tostring(path or ''):gsub('\\\\', '/')
	return normalized:match('^(.*)/[^/]+$') or '.'
end

local function buildRepoRoot()
	local source = love.filesystem.getSource()
	local runnersDir = dirname(source)
	return dirname(runnersDir)
end

function love.load()
	local repoRoot = buildRepoRoot()
	package.path = package.path .. ';' .. repoRoot .. '/?.lua;' .. repoRoot .. '/?/init.lua'
	dofile(repoRoot .. '/Tests/TerrainCollision.lua')
	if love and love.event and love.event.quit then
		love.event.quit(0)
	end
end