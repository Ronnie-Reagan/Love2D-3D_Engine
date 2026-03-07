local profiler = {}
local loveLib = rawget(_G, "love")

local function clamp(value, minValue, maxValue)
	if value < minValue then
		return minValue
	end
	if value > maxValue then
		return maxValue
	end
	return value
end

local function nowSeconds()
	if type(loveLib) == "table" and
		type(loveLib.timer) == "table" and
		type(loveLib.timer.getTime) == "function" then
		return loveLib.timer.getTime()
	end
	return os.clock()
end

local function colorCopy(src, fallback)
	local color = type(src) == "table" and src or fallback or { 1, 1, 1, 1 }
	return {
		clamp(tonumber(color[1]) or 1, 0, 1),
		clamp(tonumber(color[2]) or 1, 0, 1),
		clamp(tonumber(color[3]) or 1, 0, 1),
		clamp(tonumber(color[4]) or 1, 0, 1)
	}
end

local function metricIdFromEntry(entry)
	if type(entry) == "string" then
		return entry
	end
	if type(entry) == "table" then
		return tostring(entry.id or entry.name or "")
	end
	return ""
end

local function metricLabelFromEntry(entry, fallback)
	if type(entry) == "table" and type(entry.label) == "string" and entry.label ~= "" then
		return entry.label
	end
	return fallback
end

local function metricColorFromEntry(entry)
	if type(entry) == "table" then
		return entry.color
	end
	return nil
end

local defaultPalette = {
	frame = { 1.00, 1.00, 1.00, 1.00 },
	update = { 0.98, 0.62, 0.18, 1.00 },
	draw = { 0.20, 0.84, 1.00, 1.00 },
	terrain = { 0.92, 0.52, 0.20, 1.00 },
	render = { 0.20, 0.80, 0.96, 1.00 },
	network = { 0.42, 0.90, 0.35, 1.00 },
	hud = { 1.00, 0.88, 0.26, 1.00 },
	default = { 0.82, 0.88, 0.94, 1.00 }
}

local defaultStatPalette = {
	current = { 1.00, 1.00, 1.00, 0.85 },
	avg = { 0.28, 0.84, 1.00, 0.65 },
	min = { 0.42, 0.92, 0.46, 0.55 },
	max = { 1.00, 0.42, 0.34, 0.75 }
}

local function unpackCompat(values, i, j)
	local unpackFn = table.unpack or unpack
	return unpackFn(values, i, j)
end

local function packCompat(...)
	return {
		n = select("#", ...),
		...
	}
end

function profiler.create(config)
	config = type(config) == "table" and config or {}
	local historySize = clamp(math.floor(tonumber(config.historySize) or 300), 60, 1800)
	local instance = {
		historySize = historySize,
		frames = {},
		frameHead = 0,
		frameCount = 0,
		activeFrame = nil,
		targetFrameMs = math.max(1.0, tonumber(config.targetFrameMs) or 16.6),
		minGraphMs = math.max(1.0, tonumber(config.minGraphMs) or 20.0),
		maxLegendItems = clamp(math.floor(tonumber(config.maxLegendItems) or 10), 3, 24),
		statPalette = {
			current = colorCopy(config.statPalette and config.statPalette.current, defaultStatPalette.current),
			avg = colorCopy(config.statPalette and config.statPalette.avg, defaultStatPalette.avg),
			min = colorCopy(config.statPalette and config.statPalette.min, defaultStatPalette.min),
			max = colorCopy(config.statPalette and config.statPalette.max, defaultStatPalette.max)
		},
		metricMeta = {},
		metricOrder = {},
		metricKnown = {}
	}
	return setmetatable(instance, {
		__index = profiler
	})
end

function profiler:setTargetFrameMs(value)
	local ms = tonumber(value)
	if not ms or ms <= 0 then
		return
	end
	self.targetFrameMs = ms
end

function profiler:_registerMetric(metricId, color, label)
	if type(metricId) ~= "string" or metricId == "" then
		return nil
	end
	if not self.metricKnown[metricId] then
		self.metricKnown[metricId] = true
		self.metricOrder[#self.metricOrder + 1] = metricId
	end
	local meta = self.metricMeta[metricId]
	if not meta then
		meta = {}
		self.metricMeta[metricId] = meta
	end
	if color then
		meta.color = colorCopy(color, meta.color or defaultPalette.default)
	elseif not meta.color then
		meta.color = colorCopy(defaultPalette.default)
	end
	if type(label) == "string" and label ~= "" then
		meta.label = label
	elseif not meta.label then
		meta.label = metricId
	end
	return meta
end

function profiler:beginFrame(dt)
	if self.activeFrame then
		self:endFrame()
	end
	local frame = {
		startedAt = nowSeconds(),
		samples = {},
		sampleOrder = {}
	}
	self.activeFrame = frame
	if tonumber(dt) then
		self:addSample("frame.dt", tonumber(dt) * 1000.0, defaultPalette.default, "dt")
	end
end

function profiler:beginScope(metricId, color, label)
	if type(metricId) ~= "string" or metricId == "" then
		return nil
	end
	self:_registerMetric(metricId, color, label)
	return {
		metricId = metricId,
		startedAt = nowSeconds(),
		color = color,
		label = label
	}
end

function profiler:endScope(scopeToken)
	if type(scopeToken) ~= "table" then
		return 0
	end
	local elapsedMs = (nowSeconds() - (tonumber(scopeToken.startedAt) or nowSeconds())) * 1000.0
	if elapsedMs < 0 then
		elapsedMs = 0
	end
	self:addSample(scopeToken.metricId, elapsedMs, scopeToken.color, scopeToken.label)
	return elapsedMs
end

function profiler:addSample(metricId, valueMs, color, label)
	if type(metricId) ~= "string" or metricId == "" then
		return
	end
	local frame = self.activeFrame
	if not frame then
		return
	end
	local value = tonumber(valueMs) or 0
	if value < 0 then
		value = 0
	end
	local current = frame.samples[metricId]
	if current == nil then
		frame.samples[metricId] = value
		frame.sampleOrder[#frame.sampleOrder + 1] = metricId
	else
		frame.samples[metricId] = current + value
	end
	self:_registerMetric(metricId, color, label)
end

function profiler:timeCall(metricId, color, label, fn, ...)
	local scope = self:beginScope(metricId, color, label)
	local result = packCompat(fn(...))
	self:endScope(scope)
	return unpackCompat(result, 1, result.n)
end

function profiler:endFrame()
	local frame = self.activeFrame
	if not frame then
		return nil
	end
	self.activeFrame = nil
	local elapsedMs = (nowSeconds() - (tonumber(frame.startedAt) or nowSeconds())) * 1000.0
	if elapsedMs < 0 then
		elapsedMs = 0
	end
	if frame.samples["frame.total"] == nil then
		frame.samples["frame.total"] = elapsedMs
		frame.sampleOrder[#frame.sampleOrder + 1] = "frame.total"
	end
	frame.totalMs = elapsedMs
	self:_registerMetric("frame.total", defaultPalette.frame, "frame")

	local nextIndex = (self.frameHead % self.historySize) + 1
	self.frames[nextIndex] = frame
	self.frameHead = nextIndex
	self.frameCount = math.min(self.historySize, self.frameCount + 1)
	return frame
end

function profiler:getRecentFrames(maxFrames)
	local available = math.max(0, math.floor(tonumber(self.frameCount) or 0))
	if available <= 0 then
		return {}
	end
	local take = available
	if tonumber(maxFrames) then
		take = clamp(math.floor(tonumber(maxFrames) or available), 1, available)
	end

	local out = {}
	local start = self.frameHead - take + 1
	while start <= 0 do
		start = start + self.historySize
	end
	for i = 0, take - 1 do
		local idx = ((start + i - 1) % self.historySize) + 1
		local frame = self.frames[idx]
		if frame then
			out[#out + 1] = frame
		end
	end
	return out
end

function profiler:getMetricStats(metricId, window)
	local frames = self:getRecentFrames(window)
	if #frames == 0 then
		return {
			current = 0,
			latest = 0,
			avg = 0,
			min = 0,
			max = 0,
			peak = 0,
			samples = 0
		}
	end
	local latest = 0
	local sum = 0
	local maxValue = 0
	local minValue = math.huge
	for i = 1, #frames do
		local frame = frames[i]
		local value = 0
		if frame and frame.samples then
			value = tonumber(frame.samples[metricId]) or 0
		end
		sum = sum + value
		if value > maxValue then
			maxValue = value
		end
		if value < minValue then
			minValue = value
		end
		if i == #frames then
			latest = value
		end
	end
	if minValue == math.huge then
		minValue = 0
	end
	return {
		current = latest,
		latest = latest,
		avg = sum / #frames,
		min = minValue,
		max = maxValue,
		peak = maxValue,
		samples = #frames
	}
end

function profiler:getLatestFrame()
	if (tonumber(self.frameCount) or 0) <= 0 then
		return nil
	end
	return self.frames[self.frameHead]
end

local function resolveMetricEntries(entries)
	local out = {}
	for i = 1, #(entries or {}) do
		local entry = entries[i]
		local metricId = metricIdFromEntry(entry)
		if metricId ~= "" then
			out[#out + 1] = {
				id = metricId,
				label = metricLabelFromEntry(entry, metricId),
				color = metricColorFromEntry(entry)
			}
		end
	end
	return out
end

function profiler:drawGraph(x, y, width, height, options)
	options = type(options) == "table" and options or {}
	if type(loveLib) ~= "table" or type(loveLib.graphics) ~= "table" then
		return
	end
	local lg = loveLib.graphics
	local w = math.max(40, math.floor(tonumber(width) or 0))
	local h = math.max(30, math.floor(tonumber(height) or 0))
	local metrics = resolveMetricEntries(options.metrics or {})
	if #metrics == 0 then
		for i = 1, #self.metricOrder do
			local metricId = self.metricOrder[i]
			if metricId ~= "frame.dt" then
				metrics[#metrics + 1] = {
					id = metricId,
					label = self.metricMeta[metricId] and self.metricMeta[metricId].label or metricId,
					color = self.metricMeta[metricId] and self.metricMeta[metricId].color
				}
			end
			if #metrics >= 6 then
				break
			end
		end
	end
	if #metrics == 0 then
		return
	end

	local frames = self:getRecentFrames(options.frameCount or math.min(self.historySize, w))
	if #frames <= 0 then
		return
	end

	local maxMs = math.max(1.0, tonumber(options.minMs) or self.minGraphMs)
	local targetMs = math.max(1.0, tonumber(options.targetMs) or self.targetFrameMs)
	maxMs = math.max(maxMs, targetMs)
	for i = 1, #frames do
		local frame = frames[i]
		for j = 1, #metrics do
			local metric = metrics[j]
			local value = tonumber(frame.samples and frame.samples[metric.id]) or 0
			if value > maxMs then
				maxMs = value
			end
		end
	end
	local maxMsRounded = maxMs <= 20 and math.ceil(maxMs * 2) / 2 or math.ceil(maxMs / 5) * 5
	maxMs = math.max(1.0, maxMsRounded)

	lg.setColor(0.03, 0.05, 0.08, 0.92)
	lg.rectangle("fill", x, y, w, h, 4, 4)
	lg.setColor(0.24, 0.29, 0.36, 0.9)
	lg.rectangle("line", x, y, w, h, 4, 4)

	local gridLines = 4
	for i = 1, gridLines - 1 do
		local py = y + (h * (i / gridLines))
		lg.setColor(0.20, 0.24, 0.30, 0.55)
		lg.line(x, py, x + w, py)
	end

	local function drawThreshold(ms, color)
		if ms <= 0 or ms > maxMs then
			return
		end
		local py = y + h - clamp(ms / maxMs, 0, 1) * h
		lg.setColor(color[1], color[2], color[3], color[4])
		lg.line(x, py, x + w, py)
	end

	drawThreshold(targetMs, { 0.40, 0.94, 0.50, 0.55 })
	drawThreshold(33.3, { 1.00, 0.46, 0.34, 0.45 })
	if options.showSummaryLines ~= false then
		local summaryMetric = type(options.summaryMetric) == "string" and options.summaryMetric or "frame.total"
		local summaryStats = self:getMetricStats(summaryMetric, #frames)
		local statColors = type(options.statColors) == "table" and options.statColors or self.statPalette or defaultStatPalette
		drawThreshold(tonumber(summaryStats.min) or 0, colorCopy(statColors.min, defaultStatPalette.min))
		drawThreshold(tonumber(summaryStats.avg) or 0, colorCopy(statColors.avg, defaultStatPalette.avg))
		drawThreshold(tonumber(summaryStats.max) or 0, colorCopy(statColors.max, defaultStatPalette.max))
		drawThreshold(tonumber(summaryStats.current) or tonumber(summaryStats.latest) or 0, colorCopy(
			statColors.current,
			defaultStatPalette.current
		))
	end

	for i = 1, #metrics do
		local metric = metrics[i]
		local lineColor = colorCopy(metric.color, self.metricMeta[metric.id] and self.metricMeta[metric.id].color or defaultPalette.default)
		local pointCount = #frames
		if pointCount == 1 then
			local px = x + w
			local value = tonumber(frames[1].samples and frames[1].samples[metric.id]) or 0
			local py = y + h - clamp(value / maxMs, 0, 1) * h
			lg.setColor(lineColor[1], lineColor[2], lineColor[3], lineColor[4])
			lg.points(px, py)
		else
			local points = {}
			for j = 1, pointCount do
				local frame = frames[j]
				local value = tonumber(frame.samples and frame.samples[metric.id]) or 0
				local t = (j - 1) / math.max(1, pointCount - 1)
				local px = x + (t * w)
				local py = y + h - clamp(value / maxMs, 0, 1) * h
				points[#points + 1] = px
				points[#points + 1] = py
			end
			if #points >= 4 then
				lg.setLineWidth(1.2)
				lg.setColor(lineColor[1], lineColor[2], lineColor[3], lineColor[4])
				lg.line(points)
			end
		end
	end
	lg.setLineWidth(1)

	lg.setColor(0.86, 0.9, 0.96, 0.9)
	lg.print(string.format("%.1f ms", maxMs), x + 6, y + 4)
	lg.print("0", x + 6, y + h - (lg.getFont():getHeight() or 12) - 2)
end

function profiler:drawLegend(x, y, metrics, options)
	options = type(options) == "table" and options or {}
	if type(loveLib) ~= "table" or type(loveLib.graphics) ~= "table" then
		return 0
	end
	local entries = resolveMetricEntries(metrics or {})
	if #entries == 0 then
		return 0
	end
	local lg = loveLib.graphics
	local lineHeight = (lg.getFont():getHeight() or 12) + 2
	local maxItems = clamp(math.floor(tonumber(options.maxItems) or self.maxLegendItems), 1, #entries)
	local window = math.max(1, math.floor(tonumber(options.window) or 120))
	local statColors = type(options.statColors) == "table" and options.statColors or self.statPalette or defaultStatPalette
	local function textWidth(text)
		local font = lg.getFont()
		if font and type(font.getWidth) == "function" then
			return font:getWidth(text)
		end
		return #tostring(text) * 6
	end
	local function drawStatSegment(text, xPos, yPos, color)
		local tint = colorCopy(color, defaultPalette.default)
		lg.setColor(tint[1], tint[2], tint[3], tint[4])
		lg.print(text, xPos, yPos)
		return xPos + textWidth(text) + 8
	end
	for i = 1, maxItems do
		local entry = entries[i]
		local stats = self:getMetricStats(entry.id, window)
		local meta = self.metricMeta[entry.id] or {}
		local color = colorCopy(entry.color, meta.color or defaultPalette.default)
		local rowY = y + (i - 1) * lineHeight
		lg.setColor(color[1], color[2], color[3], color[4])
		lg.rectangle("fill", x, rowY + 3, 8, 8)
		local labelText = tostring(entry.label or meta.label or entry.id)
		lg.setColor(0.90, 0.94, 0.98, 0.96)
		lg.print(labelText, x + 12, rowY)
		local cursorX = x + 12 + textWidth(labelText) + 10
		cursorX = drawStatSegment(
			string.format("C %.2f", tonumber(stats.current) or tonumber(stats.latest) or 0),
			cursorX,
			rowY,
			statColors.current
		)
		cursorX = drawStatSegment(
			string.format("A %.2f", tonumber(stats.avg) or 0),
			cursorX,
			rowY,
			statColors.avg
		)
		cursorX = drawStatSegment(
			string.format("N %.2f", tonumber(stats.min) or 0),
			cursorX,
			rowY,
			statColors.min
		)
		drawStatSegment(
			string.format("X %.2f", tonumber(stats.max) or tonumber(stats.peak) or 0),
			cursorX,
			rowY,
			statColors.max
		)
	end
	return maxItems * lineHeight
end

return profiler
