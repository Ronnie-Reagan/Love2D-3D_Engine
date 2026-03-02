local q = require("quat")
local vector3 = require("vector3")
local love = require "love" -- avoids nil report from intellisense, safe to remove if causes issues (it should be OK)
local enet = require "enet"
local engine = require "engine"
local networking = require "networking"
local renderer = require "gpu_renderer"
local logger = require "logger"
local hostAddy = "ecosim.donreagan.ca:1988"
local relay = enet.host_create()
local relayServer = relay and relay:connect(hostAddy) or nil
local flightSimMode = false
local relative = true
local peers = {}
local event = nil
local objects = require "object"
local cubeModel = objects.cubeModel
local screen, camera, groundObject, triangleCount, frameImage, renderMode, gpuErrorLogged --, zBuffer
local useGpuRenderer = true
local perfElapsed, perfFrames, perfLoggedLowFps = 0, 0, false
local zBuffer = {}

--[[
Notes:

positions are {x = left/right(width), y = up/down(height), z = in/out(depth)} IN WORLD SPACE!!
]]
-- Procedurally generate a ground grid of cubes
local function generateGround(tileSize, gridCount, baseHeight)
	local tiles = {}
	local half = tileSize / 2

	for x = -gridCount / 2, gridCount / 2 - 1 do
		for z = -gridCount / 2, gridCount / 2 - 1 do
			local posX = x * tileSize + half
			local posZ = z * tileSize + half
			-- small color variation for realism
			local r = 0.2 + math.random() * 0.05
			local g = 0.6 + math.random() * 0.1
			local b = 0.2 + math.random() * 0.05

			table.insert(tiles, {
				model = cubeModel,
				pos = { posX, baseHeight, posZ },
				rot = q.identity(),
				color = { r, g, b },
				isSolid = true,
				halfSize = { x = tileSize / 2, y = 0.005, z = tileSize / 2 } -- thin tile
			})
		end
	end

	return tiles
end

-- === Initial Configuration ===
function love.load()
	-- 80% screen size
	local width, height = love.window.getDesktopDimensions()
	width, height = math.floor(width * 0.8), math.floor(height * 0.8)

	love.window.setTitle("Don's 3D Engine")
	local modeOk, modeErr = love.window.setMode(width, height, { vsync = false, depth = 24 })
	if not modeOk then
		love.window.setMode(width, height, { vsync = false })
	end
	love.mouse.setRelativeMode(true)

	logger.reset()
	logger.log("Booting Don's 3D Engine")
	logger.log("Log file: " .. logger.getPath())
	if not modeOk then
		logger.log("Depth buffer mode unavailable, fallback mode set. Detail: " .. tostring(modeErr))
	end

	screen = {
		w = width,
		h = height
	}

	camera = {
		pos = { 0, 0, -5 },
		rot = q.identity(),
		speed = 10,
		fov = math.rad(90),
		vel = { 0, 0, 0 }, -- current velocity
		onGround = false,  -- contacting
		gravity = -9.81,   -- units/sec^2
		jumpSpeed = 5,     -- initial jump
		throttle = 0,
		maxSpeed = 50,
		box = {
			halfSize = { x = 0.5, y = 1.8, z = 0.5 }, -- width/height/depth half extents
			pos = { 0, 0, -5 },                       -- center at camera
			isSolid = true
		}
	}

	renderMode = "CPU"
	gpuErrorLogged = false
	perfElapsed, perfFrames, perfLoggedLowFps = 0, 0, false

	-- creating the cube's object
	-- disabled now that other players can join allowing for non-static testing of latency and culling/depth ordering
	objects = {
		[1] = {
			model = cubeModel,
			pos = { 0, 0, 10 },
			rot = q.identity(),
			color = { 0.9, 0.4, 0.1 },
			isSolid = true
		}
	}

	-- generate a 1000x1000 ground made of 10x10 tiles
	local tileSize = 2
	local gridCount = 10 -- 100 tiles per side -> 1000 units total
	local baseHeight = -0.1

	local groundTiles = generateGround(tileSize, gridCount, baseHeight)

	for _, tile in ipairs(groundTiles) do
		table.insert(objects, tile)
	end

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
end

-- === Mouse Look ===
function love.mousemoved(x, y, dx, dy)
	if not relative then
		return
	end
	x, y, dx, dy = -x, -y, -dx, -dy
	local horizontal_sensitivity = 0.001
	local vertical_sensitivity = 0.001

	-- Rotate around camera's local Y axis (yaw) and local X axis (pitch)
	local right = q.rotateVector(camera.rot, { 1, 0, 0 })
	local up = q.rotateVector(camera.rot, { 0, 1, 0 })

	local pitchQuat = q.fromAxisAngle(right, -dy * vertical_sensitivity)
	local yawQuat = q.fromAxisAngle(up, -dx * horizontal_sensitivity)

	-- Apply pitch then yaw (relative to current orientation)
	camera.rot = q.normalize(q.multiply(yawQuat, q.multiply(pitchQuat, camera.rot)))
end

local function updateNet()
	local canSend = relayServer ~= nil
	if canSend then
		local ok, state = pcall(function()
			return relayServer:state()
		end)
		if ok and state == "disconnected" then
			canSend = false
		end
	end

	if canSend then
		local packet = string.format(
			"STATE|%f|%f|%f|%f|%f|%f|%f",
			camera.pos[1],
			camera.pos[2],
			camera.pos[3],
			camera.rot.w,
			camera.rot.x,
			camera.rot.y,
			camera.rot.z
		)
		pcall(function()
			relayServer:send(packet)
		end)
	end
end

-- === Camera Movement ===
function love.update(dt)
	camera = engine.processMovement(camera, dt, flightSimMode, vector3, q, objects)
	updateNet()

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

	if not relay then return end
	event = relay:service()

	while event do
		if event.type == "receive" then
			networking.handlePacket(event.data, peers, objects, q)
		elseif event.type == "disconnect" and relayServer and event.peer == relayServer then
			relayServer = nil
		end

		event = relay:service()
	end
end

-- === Input Management ===
function love.keypressed(key)
	if key == "escape" then
		love.mouse.setRelativeMode(false)
		relative = false
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
		flightSimMode = not flightSimMode
		if not flightSimMode then
			camera.throttle = 0
		end
	end

	if key == "g" then
		useGpuRenderer = not useGpuRenderer
		local mode = (useGpuRenderer and renderer.isReady()) and "GPU" or "CPU"
		renderMode = mode
		logger.log("Render mode toggled to " .. mode)
	end
end

function love.mousepressed(x, y, button)
	if not love.mouse.getRelativeMode() then
		love.mouse.setRelativeMode(true)
		relative = true
	end
end

function love.mousefocus(focused)
	if not focused then
		love.mouse.setRelativeMode(false)
		relative = false
	end
end

local function drawHud(w, h, cx, cy)
	-- to do: add bars on the bottom or left of the screen, white background rectangles, thinner coloured rectangle to indicate pos along the different axis with wrap around
end
function love.draw()
	triangleCount = 0
	local centerX, centerY = screen.w / 2, screen.h / 2

	local usedGpu = false
	if useGpuRenderer and renderer.isReady() then
		local ok, renderOk, triOrErr = pcall(renderer.drawWorld, objects, camera, { 0.2, 0.2, 0.75, 1.0 })
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

		-- reset z-buffer
		for i = 1, screen.w * screen.h do
			zBuffer[i] = math.huge
		end

		local imageData = love.image.newImageData(screen.w, screen.h)
		for _, obj in ipairs(objects) do
			imageData = engine.drawObject(obj, false, camera, vector3, q, screen, zBuffer, imageData)
		end
		if frameImage then
			frameImage:replacePixels(imageData)
		else
			frameImage = love.graphics.newImage(imageData)
		end
		love.graphics.setColor(1, 1, 1, 1)
		love.graphics.draw(frameImage)
	end


	love.graphics.setColor(1, 1, 1)
	love.graphics.print(
		"WASD + Space (ground), F = flight mode, Q/E roll (flight), G = GPU toggle, Mouse look, Esc release mouse\n" ..
		"Render: " .. tostring(renderMode) .. " | Triangles: " .. tostring(math.floor(triangleCount)) .. " | FPS: " ..
		love.timer.getFPS(),
		10,
		10
	)
	love.graphics.setColor(1, 0, 0, 0.5)
	love.graphics.circle("fill", centerX, centerY, 1)

	drawHud(screen.w, screen.h, centerX, centerY)
end

function love.resize(w, h)
	screen.w = w
	screen.h = h
	renderer.resize(screen)

	zBuffer = {}
	for i = 1, screen.w * screen.h do
		zBuffer[i] = math.huge
	end

	frameImage = nil
	logger.log(string.format("Window resized to %dx%d", w, h))
end
