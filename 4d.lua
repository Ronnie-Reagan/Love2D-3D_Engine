local love = require "love"
local screen, fov, pitch, angleYaw, anglePitch, rotationSpeed, rotating, mode, cube, lastX, lastY


local function buildTesseract(mode)
    cube = {
        vertices = {},
        edges = {}
    }

    if mode == 1 then

        for x = -1, 1, 2 do
            for y = -1, 1, 2 do
                for z = -1, 1, 2 do
                    for w = -1, 1, 2 do
                        table.insert(cube.vertices, {x, y, z, w})
                    end
                end
            end
        end

        for i = 1, #cube.vertices do
            for j = i + 1, #cube.vertices do
                local diff = 0
                for k = 1, 4 do
                    if cube.vertices[i][k] ~= cube.vertices[j][k] then
                        diff = diff + 1
                    end
                end
                if diff == 1 then
                    table.insert(cube.edges, {i, j})
                end
            end
        end
    elseif mode == 2 then
        local base3D = {}
        for x = -1, 1, 2 do
            for y = -1, 1, 2 do
                for z = -1, 1, 2 do
                    table.insert(base3D, {x, y, z})
                end
            end
        end

        -- Offset vector in X, Y, Z (no change in W)
        local offset = {1, 1, 1}

        -- Add base cube at original position (W = 0)
        for _, v in ipairs(base3D) do
            table.insert(cube.vertices, {v[1], v[2], v[3], 0})
        end

        -- Add second cube offset in XYZ space (still W = 0)
        for _, v in ipairs(base3D) do
            table.insert(cube.vertices, {v[1] + offset[1], v[2] + offset[2], v[3] + offset[3], 0})
        end

        -- Connect edges within each cube
        local function addEdges(startIndex)
            for i = 1, #base3D do
                for j = i + 1, #base3D do
                    local a, b = base3D[i], base3D[j]
                    local diff = 0
                    for k = 1, 3 do
                        if a[k] ~= b[k] then
                            diff = diff + 1
                        end
                    end
                    if diff == 1 then
                        table.insert(cube.edges, {startIndex + i - 1, startIndex + j - 1})
                    end
                end
            end
        end

        -- First cube edges
        addEdges(1)

        -- Second cube edges
        addEdges(8 + 1)

        -- Connect corresponding vertices between cubes
        for i = 1, #base3D do
            table.insert(cube.edges, {i, i + 8})
        end
    end
end

function love.load()
    love.window.setTitle("4D Object Viewing")
    love.window.setMode(800, 600)

    screen = {
        w = 800,
        h = 600
    }
    fov = 600
    pitch = 0.75
    angleYaw, anglePitch = 0, 0
    rotationSpeed = 0.01
    rotating = false

    mode = 1 -- 1 = tesseract, 2 = hypercube
    buildTesseract(mode)
end

local function rotate4D(x, y, z, w, yaw, pitch)
    local c, s = math.cos(yaw), math.sin(yaw)
    x, y = x * c - y * s, x * s + y * c

    c, s = math.cos(pitch), math.sin(pitch)
    y, z = y * c - z * s, y * s + z * c

    return x, y, z, w
end

local function project4Dto3D(x, y, z, w)
    local d = 4
    local wFactor = 1 / (d - w)
    return x * wFactor, y * wFactor, z * wFactor
end

local function project3Dto2D(x, y, z)
    local scale = fov / (z + 5)
    return x * scale + screen.w / 2, y * scale + screen.h / 2
end

function love.keypressed(key)
    if key == "1" then
        mode = 1
        buildTesseract(mode)
    elseif key == "2" then
        mode = 2
        buildTesseract(mode)
    end
end

function love.mousepressed(x, y, button)
    if button == 1 then
        rotating = true
        lastX = x
        lastY = y
    end
end

function love.mousereleased()
    rotating = false
end

function love.mousemoved(x, y)
    if rotating then
        angleYaw = angleYaw + (x - lastX) * rotationSpeed
        anglePitch = anglePitch + (y - lastY) * rotationSpeed
        lastX = x
        lastY = y
    end
end

function love.wheelmoved(x, y)
    if y > 0 then
        fov = math.min(fov + fov * 0.1, 600 * 20)
    elseif y < 0 then
        fov = math.max(fov - fov * 0.1, 100)
    end
end

function love.draw()
    love.graphics.clear(0.08, 0.08, 0.1)

    local transformed = {}

    for i, v in ipairs(cube.vertices) do
        local x, y, z, w = v[1] * pitch, v[2] * pitch, v[3] * pitch, v[4] * pitch
        x, y, z, w = rotate4D(x, y, z, w, angleYaw, anglePitch)
        local px, py, pz = project4Dto3D(x, y, z, w)
        local sx, sy = project3Dto2D(px, py, pz)
        transformed[i] = {sx, sy}
    end

    for _, edge in ipairs(cube.edges) do
        local i1, i2 = edge[1], edge[2]
        local a, b = transformed[i1], transformed[i2]

        if mode == 2 then
            -- Determine color by edge type
            if i1 <= 8 and i2 <= 8 then
                love.graphics.setColor(1.0, 0.2, 0.2) -- Red: first cube
            elseif i1 > 8 and i2 > 8 then
                love.graphics.setColor(0.2, 0.4, 1.0) -- Blue: second cube
            else
                love.graphics.setColor(0.85, 0.85, 0.85) -- White/gray: connection
            end
        end
        love.graphics.line(a[1], a[2], b[1], b[2])
    end

    love.graphics.setColor(1, 1, 1)
    love.graphics.print("Press 1 = Tesseract | 2 = HyperCube\nClick and drag to Rotate\nScroll wheel to adjust Zoom", 10, 10)
end

