local logger = {}
local love = require "love"

local LOG_FILE = "runtime.log"

local function timestamp()
    return os.date("%Y-%m-%d %H:%M:%S")
end

local function appendLine(line)
    if love and love.filesystem and love.filesystem.append then
        local ok = pcall(love.filesystem.append, LOG_FILE, line .. "\n")
        if ok then
            return true
        end
    end

    local file = io.open(LOG_FILE, "a")
    if not file then
        return false
    end
    file:write(line, "\n")
    file:close()
    return true
end

function logger.reset()
    if love and love.filesystem then
        pcall(love.filesystem.write, LOG_FILE, "")
        return
    end

    local file = io.open(LOG_FILE, "w")
    if file then
        file:close()
    end
end

function logger.getPath()
    if love and love.filesystem and love.filesystem.getSaveDirectory then
        return love.filesystem.getSaveDirectory() .. "/" .. LOG_FILE
    end
    return LOG_FILE
end

function logger.log(message)
    local line = string.format("[%s] %s", timestamp(), tostring(message))
    print(line)
    appendLine(line)
end

return logger
