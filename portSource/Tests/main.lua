local function appendPackagePath()
	local sep = package.config and package.config:sub(1, 1) or "/"
	local root = love.filesystem.getSourceBaseDirectory() or (".." .. sep)
	if root:sub(-1) ~= sep then
		root = root .. sep
	end
	local patterns = {
		root .. "?.lua",
		root .. "?" .. sep .. "init.lua"
	}
	package.path = package.path .. ";" .. table.concat(patterns, ";")
end

local function parseRequestedTest(argv)
	local runAll = false
	if type(argv) ~= "table" then
		return nil, runAll
	end

	for i = 1, #argv do
		local token = tostring(argv[i] or "")
		if token == "--all" then
			runAll = true
		elseif token == "--test" then
			local value = tostring(argv[i + 1] or "")
			if value ~= "" then
				return value, runAll
			end
		else
			local inline = token:match("^%-%-test=(.+)$")
			if inline and inline ~= "" then
				return inline, runAll
			end
		end
	end

	return nil, runAll
end

local function normalizeTestName(name)
	local token = tostring(name or "")
	token = token:gsub("\\", "/")
	token = token:gsub("^.+/", "")
	token = token:gsub("%.lua$", "")
	token = token:gsub("[^%w_%-]", "")
	return token
end

local function listTests()
	local out = {}
	local items = love.filesystem.getDirectoryItems("")
	for _, item in ipairs(items) do
		if item:match("%.lua$") and item ~= "main.lua" then
			out[#out + 1] = item
		end
	end
	table.sort(out)
	return out
end

local function runTestFile(filename)
	local chunk, loadErr = love.filesystem.load(filename)
	if not chunk then
		print(string.format("[FAIL] %s", filename))
		print(tostring(loadErr))
		return false
	end

	local ok, err = pcall(chunk)
	if ok then
		print(string.format("[PASS] %s", filename))
		return true
	end
	print(string.format("[FAIL] %s", filename))
	print(tostring(err))
	return false
end

local function runSelectedOrAll()
	appendPackagePath()

	local requested, runAll = parseRequestedTest(arg or {})
	if requested and requested ~= "" then
		local normalized = normalizeTestName(requested)
		local target = normalized .. ".lua"
		if not love.filesystem.getInfo(target, "file") then
			error(string.format("Unknown test '%s'. Expected %s in portSource/Tests/.", requested, target))
		end
		local passed = runTestFile(target)
		if not passed then
			love.event.quit(1)
			return
		end
		love.event.quit(0)
		return
	end

	local tests = listTests()
	if #tests == 0 then
		error("No test files found in portSource/Tests/.")
	end

	if not runAll then
		-- Default target keeps quick iteration fast.
		local defaultTest = "FlightDynamics.lua"
		if love.filesystem.getInfo(defaultTest, "file") then
			local passed = runTestFile(defaultTest)
			love.event.quit(passed and 0 or 1)
			return
		end
	end

	local failures = 0
	for _, testFile in ipairs(tests) do
		if not runTestFile(testFile) then
			failures = failures + 1
		end
	end

	if failures > 0 then
		print(string.format("[summary] %d test file(s) failed.", failures))
		love.event.quit(1)
		return
	end
	love.event.quit(0)
end

function love.load()
	runSelectedOrAll()
end
