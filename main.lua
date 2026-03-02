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
local mouseSensitivity = 0.001
local invertLookY = false
local showCrosshair = true
local showDebugOverlay = true
local pauseTitleFont, pauseItemFont

local pauseMenu = {
	active = false,
	tab = "game",
	selected = 1,
	itemBounds = {},
	tabBounds = {},
	helpScroll = 0,
	statusText = "",
	statusUntil = 0,
	confirmItemId = nil,
	confirmUntil = 0,
	items = {
		{ id = "resume", label = "Resume", kind = "action", help = "Close pause menu and continue playing." },
		{ id = "flight", label = "Flight Mode", kind = "toggle", help = "Toggle no-gravity flight movement mode." },
		{ id = "renderer", label = "Renderer", kind = "toggle", help = "Switch between GPU and CPU renderer paths." },
		{ id = "crosshair", label = "Crosshair", kind = "toggle", help = "Show or hide center crosshair dot." },
		{ id = "overlay", label = "Debug Overlay", kind = "toggle", help = "Toggle top-left help/perf text." },
		{ id = "speed", label = "Move Speed", kind = "range", min = 2, max = 30, step = 1, help = "Adjust player movement speed." },
		{ id = "fov", label = "Field of View", kind = "range", min = 60, max = 120, step = 5, help = "Adjust camera FOV in degrees." },
		{ id = "sensitivity", label = "Mouse Sensitivity", kind = "range", min = 0.0004, max = 0.004, step = 0.0002, help = "Adjust mouse look sensitivity." },
		{ id = "invertY", label = "Invert Look Y", kind = "toggle", help = "Invert vertical mouse look direction." },
		{ id = "reset", label = "Reset Camera", kind = "action", help = "Reset camera position and orientation." },
		{ id = "reconnect", label = "Reconnect Relay", kind = "action", help = "Reconnect to multiplayer relay server." },
		{ id = "quit", label = "Quit Game", kind = "action", confirm = true, help = "Exit the game client." }
	}
}

local function getPauseHelpText()
	return table.concat({
		"GAME CONTROLS",
		"WASD: move",
		"Space: jump (ground mode)",
		"Mouse: look",
		"F: toggle flight mode",
		"Q/E: roll while in flight mode",
		"G: toggle GPU/CPU renderer",
		"P: print camera debug info",
		"Esc: open/close pause menu",
		"",
		"PAUSE MENU",
		"Tab or H: switch Game/Help tabs",
		"Up/Down or W/S: select options",
		"Left/Right or A/D: adjust selected value",
		"PgUp/PgDn: coarse value adjust",
		"Enter/Space: activate option",
		"Mouse wheel: adjust values (Game tab) / scroll (Help tab)",
		"Esc: resume game"
	}, "\n")
end

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

local function clamp(value, minValue, maxValue)
	if value < minValue then
		return minValue
	end
	if value > maxValue then
		return maxValue
	end
	return value
end

local function setMouseCapture(enabled)
	love.mouse.setRelativeMode(enabled)
	pcall(love.mouse.setVisible, not enabled)
	relative = enabled
end

local function setFlightMode(enabled)
	flightSimMode = enabled and true or false
	if not flightSimMode and camera then
		camera.throttle = 0
	end
end

local function setRendererPreference(enableGpu)
	local desiredGpu = enableGpu and true or false
	if desiredGpu and not renderer.isReady() then
		useGpuRenderer = false
		renderMode = "CPU"
		logger.log("GPU renderer unavailable; staying in CPU mode.")
		return false
	end

	local oldMode = renderMode
	useGpuRenderer = desiredGpu
	renderMode = (useGpuRenderer and renderer.isReady()) and "GPU" or "CPU"
	if oldMode ~= renderMode then
		logger.log("Render mode toggled to " .. renderMode)
	end
	return useGpuRenderer
end

local function resetCameraTransform()
	if not camera then
		return
	end

	camera.pos = { 0, 0, -5 }
	camera.rot = q.identity()
	camera.vel = { 0, 0, 0 }
	camera.throttle = 0
	camera.onGround = false
	if camera.box and camera.box.halfSize then
		camera.box.pos = { camera.pos[1], camera.pos[2] - camera.box.halfSize.y, camera.pos[3] }
	end
	logger.log("Camera transform reset.")
end

local function clearPeerObjects()
	for i = #objects, 1, -1 do
		if objects[i].id then
			table.remove(objects, i)
		end
	end
	peers = {}
end

local function getRelayStatus()
	if not relay then
		return "Host down"
	end
	if not relayServer then
		return "Disconnected"
	end

	local ok, state = pcall(function()
		return relayServer:state()
	end)
	if not ok then
		return "Unknown"
	end
	if state == "connected" then
		return "Connected"
	end
	return tostring(state)
end

local function reconnectRelay()
	if not relay then
		relay = enet.host_create()
		if not relay then
			logger.log("ENet host creation failed; reconnect unavailable.")
			return false
		end
	end

	if relayServer then
		pcall(function()
			relayServer:disconnect_now()
		end)
		relayServer = nil
	end

	clearPeerObjects()

	local ok, peerOrErr = pcall(function()
		return relay:connect(hostAddy)
	end)
	if not ok then
		logger.log("Relay reconnect failed: " .. tostring(peerOrErr))
		return false
	end

	relayServer = peerOrErr
	if relayServer then
		logger.log("Reconnecting to relay server: " .. hostAddy)
		return true
	end

	logger.log("Relay reconnect failed: no peer handle returned.")
	return false
end

local function refreshUiFonts()
	local baseSize = math.max(14, math.floor(screen.h * 0.023))
	pauseTitleFont = love.graphics.newFont(math.max(24, baseSize + 8))
	pauseItemFont = love.graphics.newFont(baseSize)
end

local setPauseState

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

local function buildPauseMenuLayout()
	local titleFont = pauseTitleFont or love.graphics.getFont()
	local itemFont = pauseItemFont or love.graphics.getFont()
	local isGameTab = pauseMenu.tab == "game"
	local itemCount = #pauseMenu.items

	local panelW = math.min(660, screen.w * 0.9)
	local panelX = (screen.w - panelW) / 2
	local contentX = panelX + 18
	local contentW = panelW - 36
	local footerTextW = contentW
	local controlsText = isGameTab
			and "Tab/H for Help | Up/Down select | Enter apply | Esc resume"
		or "Tab/H for Game | Mouse wheel scroll | Esc resume"

	local selectedItem = pauseMenu.items[pauseMenu.selected]
	local detailText = isGameTab and (selectedItem and selectedItem.help or "") or ""
	local statusText = pauseMenu.statusText

	local lineH = itemFont:getHeight()
	local titleTopPad = 14
	local titleH = titleFont:getHeight()
	local tabH = lineH + 8
	local topPad = titleTopPad + titleH + 8 + tabH + 10
	local footerTopPad = 10
	local footerGap = 6
	local footerBottomPad = 10
	local maxPanelH = math.max(220, screen.h - 24)
	maxPanelH = math.min(maxPanelH, screen.h - 8)

	local function computeFooterContentHeight(controlsLines, detailLines, statusLines)
		local h = #controlsLines * lineH
		if #detailLines > 0 then
			h = h + footerGap + (#detailLines * lineH)
		end
		if #statusLines > 0 then
			h = h + footerGap + (#statusLines * lineH)
		end
		return h
	end

	local controlsLines = trimWrappedLines(getWrappedLines(itemFont, controlsText, footerTextW), 2)
	local detailLines = trimWrappedLines(getWrappedLines(itemFont, detailText, footerTextW), 2)
	local statusLines = trimWrappedLines(getWrappedLines(itemFont, statusText, footerTextW), 2)
	local footerContentH = computeFooterContentHeight(controlsLines, detailLines, statusLines)
	local bottomPad = footerTopPad + footerContentH + footerBottomPad

	if (maxPanelH - topPad - bottomPad) < itemCount * 18 then
		controlsLines = trimWrappedLines(getWrappedLines(itemFont, controlsText, footerTextW), 1)
		detailLines = trimWrappedLines(getWrappedLines(itemFont, detailText, footerTextW), 1)
		statusLines = trimWrappedLines(getWrappedLines(itemFont, statusText, footerTextW), 1)
		footerContentH = computeFooterContentHeight(controlsLines, detailLines, statusLines)
		bottomPad = footerTopPad + footerContentH + footerBottomPad
	end

	local availableContentH = math.max(1, maxPanelH - topPad - bottomPad)
	local rowH = 0
	local contentH = availableContentH
	if isGameTab then
		local minRowH = math.max(lineH + 8, 24)
		local maxRowH = 40
		if availableContentH >= minRowH * itemCount then
			rowH = clamp(math.floor(availableContentH / itemCount), minRowH, maxRowH)
		else
			rowH = math.max(lineH + 4, math.floor(availableContentH / itemCount))
		end
		contentH = math.max(1, rowH * itemCount)
	else
		contentH = math.max(lineH * 8, availableContentH)
	end

	local panelH = topPad + contentH + bottomPad
	if panelH > maxPanelH then
		panelH = maxPanelH
	end

	local finalContentH = math.max(1, panelH - topPad - bottomPad)
	if isGameTab then
		rowH = math.max(1, math.floor(finalContentH / itemCount))
		finalContentH = rowH * itemCount
		panelH = topPad + finalContentH + bottomPad
		if panelH > maxPanelH then
			panelH = maxPanelH
			finalContentH = math.max(1, panelH - topPad - bottomPad)
			rowH = math.max(1, math.floor(finalContentH / itemCount))
			finalContentH = rowH * itemCount
			panelH = topPad + finalContentH + bottomPad
		end
	end

	local panelY = (screen.h - panelH) / 2
	local titleY = panelY + titleTopPad
	local tabY = titleY + titleH + 8
	local contentY = panelY + topPad

	return {
		isGameTab = isGameTab,
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
		rowX = contentX,
		rowW = contentW,
		rowH = rowH,
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

local function setPauseTab(tab)
	if tab ~= "game" and tab ~= "help" then
		return
	end
	if pauseMenu.tab == tab then
		return
	end

	pauseMenu.tab = tab
	pauseMenu.helpScroll = 0
	pauseMenu.itemBounds = {}
	pauseMenu.tabBounds = {}
	clearPauseConfirm()
end

local function cyclePauseTab(direction)
	if direction == 0 then
		return
	end
	if pauseMenu.tab == "game" then
		setPauseTab("help")
	else
		setPauseTab("game")
	end
end

local function isPauseConfirmActive(itemId)
	return pauseMenu.confirmItemId == itemId and love.timer.getTime() <= pauseMenu.confirmUntil
end

local function getPauseItemValue(item)
	if item.id == "flight" then
		return flightSimMode and "On" or "Off"
	end
	if item.id == "renderer" then
		if renderer.isReady() then
			return useGpuRenderer and "GPU" or "CPU"
		end
		return "CPU (GPU unavailable)"
	end
	if item.id == "crosshair" then
		return showCrosshair and "On" or "Off"
	end
	if item.id == "overlay" then
		return showDebugOverlay and "On" or "Off"
	end
	if item.id == "speed" and camera then
		return string.format("%.1f", camera.speed)
	end
	if item.id == "fov" and camera then
		local fovDeg = math.deg(camera.fov)
		return string.format("%d deg", math.floor(fovDeg + 0.5))
	end
	if item.id == "sensitivity" then
		return string.format("%.4f", mouseSensitivity)
	end
	if item.id == "invertY" then
		return invertLookY and "On" or "Off"
	end
	if item.id == "reconnect" then
		return getRelayStatus()
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

	if item.id == "speed" and camera then
		camera.speed = clamp(camera.speed + item.step * direction * scale, item.min, item.max)
		setPauseStatus("Move Speed: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "fov" and camera then
		local fovDeg = math.deg(camera.fov)
		fovDeg = clamp(fovDeg + item.step * direction * scale, item.min, item.max)
		camera.fov = math.rad(fovDeg)
		setPauseStatus("Field of View: " .. getPauseItemValue(item), 1.2)
		return
	end

	if item.id == "sensitivity" then
		local nextValue = clamp(mouseSensitivity + item.step * direction * scale, item.min, item.max)
		mouseSensitivity = math.floor(nextValue * 10000 + 0.5) / 10000
		setPauseStatus("Mouse Sensitivity: " .. getPauseItemValue(item), 1.2)
	end
end

local function activatePauseItem(item)
	if item.kind == "range" then
		adjustPauseItem(item, 1)
		return
	end

	if item.confirm and not isPauseConfirmActive(item.id) then
		pauseMenu.confirmItemId = item.id
		pauseMenu.confirmUntil = love.timer.getTime() + 2.5
		setPauseStatus("Confirm to \"" .. item.label .. "\".", 2.5)
		return
	end
	clearPauseConfirm()

	if item.id == "resume" then
		setPauseState(false)
	elseif item.id == "flight" then
		setFlightMode(not flightSimMode)
		setPauseStatus("Flight Mode: " .. getPauseItemValue(item), 1.2)
	elseif item.id == "renderer" then
		setRendererPreference(not useGpuRenderer)
		setPauseStatus("Renderer: " .. getPauseItemValue(item), 1.2)
	elseif item.id == "crosshair" then
		showCrosshair = not showCrosshair
		setPauseStatus("Crosshair: " .. getPauseItemValue(item), 1.2)
	elseif item.id == "overlay" then
		showDebugOverlay = not showDebugOverlay
		setPauseStatus("Debug Overlay: " .. getPauseItemValue(item), 1.2)
	elseif item.id == "invertY" then
		invertLookY = not invertLookY
		setPauseStatus("Invert Look Y: " .. getPauseItemValue(item), 1.2)
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
	local itemCount = #pauseMenu.items
	pauseMenu.selected = ((pauseMenu.selected - 1 + delta) % itemCount) + 1
	clearPauseConfirm()
end

local function adjustSelectedPauseItem(direction)
	local item = pauseMenu.items[pauseMenu.selected]
	if not item then
		return
	end
	if item.kind == "range" then
		adjustPauseItem(item, direction)
	elseif item.kind == "toggle" and direction ~= 0 then
		activatePauseItem(item)
	end
end

local function adjustSelectedPauseItemCoarse(direction)
	local item = pauseMenu.items[pauseMenu.selected]
	if item and item.kind == "range" then
		adjustPauseItem(item, direction, 5)
	end
end

local function activateSelectedPauseItem()
	local item = pauseMenu.items[pauseMenu.selected]
	if item then
		activatePauseItem(item)
	end
end

local function updatePauseMenuHover(x, y)
	for i, bounds in ipairs(pauseMenu.itemBounds) do
		if x >= bounds.x and x <= bounds.x + bounds.w and y >= bounds.y and y <= bounds.y + bounds.h then
			if pauseMenu.selected ~= i then
				pauseMenu.selected = i
				clearPauseConfirm()
			end
			return
		end
	end
end

local function handlePauseMenuMouseClick(x, y)
	for _, tabBounds in ipairs(pauseMenu.tabBounds) do
		if x >= tabBounds.x and x <= tabBounds.x + tabBounds.w and y >= tabBounds.y and y <= tabBounds.y + tabBounds.h then
			setPauseTab(tabBounds.id)
			return
		end
	end

	if pauseMenu.tab ~= "game" then
		return
	end

	for i, bounds in ipairs(pauseMenu.itemBounds) do
		if x >= bounds.x and x <= bounds.x + bounds.w and y >= bounds.y and y <= bounds.y + bounds.h then
			pauseMenu.selected = i
			local item = pauseMenu.items[i]
			if item and item.id ~= "quit" then
				clearPauseConfirm()
			end
			if item and item.kind == "range" then
				local direction = (x >= (bounds.x + bounds.w * 0.5)) and 1 or -1
				adjustPauseItem(item, direction)
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
		pauseMenu.tab = "game"
		pauseMenu.helpScroll = 0
		pauseMenu.selected = clamp(pauseMenu.selected, 1, #pauseMenu.items)
		pauseMenu.itemBounds = {}
		pauseMenu.tabBounds = {}
		setPauseStatus("", 0)
		clearPauseConfirm()
		setMouseCapture(false)
		logger.log("Pause menu opened.")
	else
		pauseMenu.tabBounds = {}
		setPauseStatus("", 0)
		clearPauseConfirm()
		if love.window.hasFocus and love.window.hasFocus() then
			setMouseCapture(true)
		end
		logger.log("Pause menu closed.")
	end
end

local function drawPauseMenuRows(layout, rowTextHeight)
	pauseMenu.itemBounds = {}
	for i, item in ipairs(pauseMenu.items) do
		local rowY = layout.panelY + layout.topPad + (i - 1) * layout.rowH
		local rowHeight = math.max(1, layout.rowH - 4)
		local rowTextY = rowY + math.floor((rowHeight - rowTextHeight) / 2)
		local selected = i == pauseMenu.selected
		local value = getPauseItemValue(item)

		if selected then
			love.graphics.setColor(0.22, 0.28, 0.42, 0.95)
		else
			love.graphics.setColor(0.15, 0.16, 0.2, 0.9)
		end
		love.graphics.rectangle("fill", layout.rowX, rowY, layout.rowW, rowHeight, 6, 6)

		love.graphics.setColor(1, 1, 1, 1)
		love.graphics.print(item.label, layout.rowX + 12, rowTextY)
		if item.kind == "range" then
			value = "< " .. value .. " >"
		end
		if value ~= "" then
			love.graphics.printf(value, layout.rowX + 12, rowTextY, layout.rowW - 24, "right")
		end

		pauseMenu.itemBounds[i] = { x = layout.rowX, y = rowY, w = layout.rowW, h = rowHeight }
	end
end

local function drawPauseMenuTabs(layout)
	local font = love.graphics.getFont()
	pauseMenu.tabBounds = {}
	local labels = {
		{ id = "game", label = "Game" },
		{ id = "help", label = "Help" }
	}
	local tabGap = 8
	local totalW = 0
	for _, tab in ipairs(labels) do
		totalW = totalW + font:getWidth(tab.label) + 22
	end
	totalW = totalW + tabGap

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

local function drawPauseHelpContent(layout)
	local font = love.graphics.getFont()
	local helpText = getPauseHelpText()
	local helpLines = getWrappedLines(font, helpText, layout.contentW)
	local visibleLines = math.max(1, math.floor(layout.contentH / layout.lineH))
	local maxScroll = math.max(0, #helpLines - visibleLines)
	pauseMenu.helpScroll = clamp(pauseMenu.helpScroll, 0, maxScroll)

	local startLine = pauseMenu.helpScroll + 1
	local endLine = math.min(#helpLines, startLine + visibleLines - 1)
	local y = layout.contentY

	love.graphics.setColor(0.12, 0.13, 0.16, 0.9)
	love.graphics.rectangle("fill", layout.contentX, layout.contentY, layout.contentW, layout.contentH, 6, 6)
	love.graphics.setColor(1, 1, 1, 0.95)

	for i = startLine, endLine do
		love.graphics.print(helpLines[i], layout.contentX + 10, y)
		y = y + layout.lineH
	end

	if maxScroll > 0 then
		local barX = layout.contentX + layout.contentW - 6
		local barY = layout.contentY + 4
		local barH = layout.contentH - 8
		local thumbH = math.max(16, barH * (visibleLines / #helpLines))
		local thumbY = barY + (barH - thumbH) * (pauseMenu.helpScroll / maxScroll)

		love.graphics.setColor(1, 1, 1, 0.18)
		love.graphics.rectangle("fill", barX, barY, 2, barH)
		love.graphics.setColor(0.8, 0.9, 1.0, 0.7)
		love.graphics.rectangle("fill", barX - 1, thumbY, 4, thumbH, 2, 2)
	end
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
	if layout.isGameTab then
		drawPauseMenuRows(layout, itemFont:getHeight())
	else
		pauseMenu.itemBounds = {}
		drawPauseHelpContent(layout)
	end
	drawPauseMenuFooter(layout)

	love.graphics.setFont(oldFont)
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
	love.keyboard.setKeyRepeat(true)
	setMouseCapture(true)

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
	refreshUiFonts()

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
	pauseMenu.active = false
	pauseMenu.tab = "game"
	pauseMenu.selected = 1
	pauseMenu.itemBounds = {}
	pauseMenu.tabBounds = {}
	pauseMenu.helpScroll = 0
	pauseMenu.statusText = ""
	pauseMenu.statusUntil = 0
	pauseMenu.confirmItemId = nil
	pauseMenu.confirmUntil = 0

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
	if pauseMenu.active then
		updatePauseMenuHover(x, y)
		return
	end

	if not relative then
		return
	end
	x, y, dx, dy = -x, -y, -dx, -dy

	-- Rotate around camera's local Y axis (yaw) and local X axis (pitch)
	local right = q.rotateVector(camera.rot, { 1, 0, 0 })
	local up = q.rotateVector(camera.rot, { 0, 1, 0 })

	local verticalSign = invertLookY and 1 or -1
	local pitchQuat = q.fromAxisAngle(right, verticalSign * dy * mouseSensitivity)
	local yawQuat = q.fromAxisAngle(up, -dx * mouseSensitivity)

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

local function serviceNetworkEvents()
	if not relay then
		return
	end

	event = relay:service()
	while event do
		if event.type == "receive" then
			networking.handlePacket(event.data, peers, objects, q)
		elseif event.type == "disconnect" and relayServer and event.peer == relayServer then
			relayServer = nil
			logger.log("Relay disconnected.")
		end

		event = relay:service()
	end
end

-- === Camera Movement ===
function love.update(dt)
	if pauseMenu.statusUntil > 0 and love.timer.getTime() >= pauseMenu.statusUntil then
		pauseMenu.statusText = ""
		pauseMenu.statusUntil = 0
	end
	if pauseMenu.confirmItemId and love.timer.getTime() > pauseMenu.confirmUntil then
		clearPauseConfirm()
	end

	if not pauseMenu.active then
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
	end

	serviceNetworkEvents()
end

-- === Input Management ===
function love.keypressed(key)
	if key == "escape" then
		setPauseState(not pauseMenu.active)
		return
	end

	if pauseMenu.active then
		if key == "tab" or key == "h" then
			cyclePauseTab(1)
			return
		end

		if pauseMenu.tab == "help" then
			if key == "up" or key == "w" then
				pauseMenu.helpScroll = math.max(0, pauseMenu.helpScroll - 1)
			elseif key == "down" or key == "s" then
				pauseMenu.helpScroll = pauseMenu.helpScroll + 1
			elseif key == "left" or key == "a" then
				setPauseTab("game")
			elseif key == "right" or key == "d" then
				setPauseTab("help")
			elseif key == "home" then
				pauseMenu.helpScroll = 0
			end
			return
		end

		if key == "up" or key == "w" then
			movePauseSelection(-1)
		elseif key == "down" or key == "s" then
			movePauseSelection(1)
		elseif key == "home" then
			pauseMenu.selected = 1
			clearPauseConfirm()
		elseif key == "end" then
			pauseMenu.selected = #pauseMenu.items
			clearPauseConfirm()
		elseif key == "left" or key == "a" then
			adjustSelectedPauseItem(-1)
		elseif key == "right" or key == "d" then
			adjustSelectedPauseItem(1)
		elseif key == "pageup" then
			adjustSelectedPauseItemCoarse(1)
		elseif key == "pagedown" then
			adjustSelectedPauseItemCoarse(-1)
		elseif key == "return" or key == "kpenter" or key == "space" then
			activateSelectedPauseItem()
		end
		return
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
		setFlightMode(not flightSimMode)
	end

	if key == "g" then
		setRendererPreference(not useGpuRenderer)
	end
end

function love.mousepressed(x, y, button)
	if pauseMenu.active then
		if button == 1 then
			handlePauseMenuMouseClick(x, y)
		end
		return
	end

	if not love.mouse.getRelativeMode() then
		setMouseCapture(true)
	end
end

function love.wheelmoved(x, y)
	if not pauseMenu.active or y == 0 then
		return
	end

	if pauseMenu.tab == "help" then
		pauseMenu.helpScroll = math.max(0, pauseMenu.helpScroll - y)
		return
	end

	local item = pauseMenu.items[pauseMenu.selected]
	if item and item.kind == "range" then
		adjustPauseItem(item, y > 0 and 1 or -1)
	end
end

function love.mousefocus(focused)
	if not focused then
		setMouseCapture(false)
	end
end

function love.focus(focused)
	if not focused and not pauseMenu.active then
		setPauseState(true)
		setPauseStatus("Paused: window focus lost.", 1.8)
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

	if showDebugOverlay then
		love.graphics.setColor(1, 1, 1)
		love.graphics.print(
			"Esc = pause menu (see Help tab for full controls)\n" ..
			"Render: " .. tostring(renderMode) .. " | Triangles: " .. tostring(math.floor(triangleCount)) .. " | FPS: " ..
			tostring(love.timer.getFPS()),
			10,
			10
		)
	end

	if showCrosshair and not pauseMenu.active then
		love.graphics.setColor(1, 0, 0, 0.5)
		love.graphics.circle("fill", centerX, centerY, 1)
	end

	drawHud(screen.w, screen.h, centerX, centerY)

	if pauseMenu.active then
		drawPauseMenu()
	end
end

function love.resize(w, h)
	screen.w = w
	screen.h = h
	renderer.resize(screen)
	refreshUiFonts()

	zBuffer = {}
	for i = 1, screen.w * screen.h do
		zBuffer[i] = math.huge
	end

	frameImage = nil
	logger.log(string.format("Window resized to %dx%d", w, h))
end
