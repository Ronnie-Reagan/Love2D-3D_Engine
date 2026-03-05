local function buildFakeLove()
    local function makeImageData(width, height)
        local pixels = {}
        local imageData = {}

        local function indexOf(x, y)
            return y * width + x + 1
        end

        function imageData:getWidth()
            return width
        end

        function imageData:getHeight()
            return height
        end

        function imageData:setPixel(x, y, r, g, b, a)
            pixels[indexOf(x, y)] = { r or 0, g or 0, b or 0, a or 0 }
        end

        function imageData:getPixel(x, y)
            local px = pixels[indexOf(x, y)]
            if not px then
                return 0, 0, 0, 0
            end
            return px[1], px[2], px[3], px[4]
        end

        function imageData:mapPixel(fn)
            for y = 0, height - 1 do
                for x = 0, width - 1 do
                    local r, g, b, a = fn(x, y, self:getPixel(x, y))
                    self:setPixel(x, y, r, g, b, a)
                end
            end
        end

        function imageData:clone()
            local cloned = makeImageData(width, height)
            for k, px in pairs(pixels) do
                cloned._pixels[k] = { px[1], px[2], px[3], px[4] }
            end
            return cloned
        end

        function imageData:encode()
            local encoded = {}
            function encoded:getString()
                return "fakepng-" .. tostring(width) .. "x" .. tostring(height) .. "-" .. tostring(#pixels)
            end
            return encoded
        end

        imageData._pixels = pixels
        return imageData
    end

    local fakeLove = {
        image = {
            newImageData = makeImageData
        },
        graphics = {
            newImage = function(imageData)
                local image = {
                    imageData = imageData
                }
                function image:setFilter()
                end
                function image:replacePixels(nextImageData)
                    self.imageData = nextImageData
                end
                return image
            end
        },
        data = {
            hash = function(_, _, raw)
                return raw
            end
        }
    }
    return fakeLove
end

package.loaded["love"] = buildFakeLove()

local paintRuntime = require("Source.ModelModule.PaintRuntime")

local function assertTrue(value, message)
    if not value then
        error(message or "assertion failed", 2)
    end
end

local function run()
    local sessionId, err = paintRuntime.beginSession("asset-1", "plane", "owner-1", "hash-1", {
        width = 512,
        height = 512,
        maxHistory = 8,
        maxUndoBytes = 1024 * 1024
    })
    assertTrue(sessionId ~= nil, "beginSession failed: " .. tostring(err))

    local ok1, err1 = paintRuntime.paintStroke(sessionId, {
        u = 0.5,
        v = 0.5,
        size = 12,
        opacity = 1,
        hardness = 0.8,
        color = { 1, 0, 0, 1 }
    })
    assertTrue(ok1, "first stroke should succeed: " .. tostring(err1))

    local session = paintRuntime.getSession(sessionId)
    assertTrue(#session.undo == 1, "undo history should contain first snapshot")

    local ok2, err2 = paintRuntime.paintStroke(sessionId, {
        u = 0.52,
        v = 0.52,
        size = 12,
        opacity = 1,
        hardness = 0.8,
        color = { 0, 1, 0, 1 }
    })
    assertTrue(ok2, "second stroke should succeed with budget pruning: " .. tostring(err2))
    assertTrue(#session.undo == 1, "undo history should be pruned to stay within budget")

    local bigSessionId = paintRuntime.beginSession("asset-2", "plane", "owner-2", "hash-2", {
        width = 1024,
        height = 1024,
        maxHistory = 8,
        maxUndoBytes = 1024 * 1024
    })
    assertTrue(bigSessionId ~= nil, "large beginSession should still succeed")

    local okBig, errBig = paintRuntime.paintStroke(bigSessionId, {
        u = 0.5,
        v = 0.5,
        size = 16,
        opacity = 1,
        hardness = 0.75,
        color = { 1, 1, 1, 1 }
    })
    assertTrue(not okBig, "stroke should fail when single snapshot exceeds budget")
    assertTrue(type(errBig) == "string" and errBig:find("budget"), "failure reason should mention budget")

    print("Paint runtime budget tests passed")
end

run()
