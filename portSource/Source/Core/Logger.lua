local logger = {}
local love = require "love"

local activeLogFile = nil

local function buildLogFileName()
    return os.date("%Y-%m-%d_%H-%M-%S") .. ".log"
end

local function getActiveLogFile()
    if type(activeLogFile) ~= "string" or activeLogFile == "" then
        activeLogFile = buildLogFileName()
    end
    return activeLogFile
end

local function timestamp()
    return os.date("%Y-%m-%d %H:%M:%S")
end

local function appendLine(line)
    local logFile = getActiveLogFile()

    if love and love.filesystem and love.filesystem.append then
        local ok = pcall(love.filesystem.append, logFile, line .. "\n")
        if ok then
            return true
        end
    end

    local file = io.open(logFile, "a")
    if not file then
        return false
    end
    file:write(line, "\n")
    file:close()
    return true
end

function logger.reset()
    activeLogFile = buildLogFileName()
end

function logger.getPath()
    local logFile = getActiveLogFile()
    if love and love.filesystem and love.filesystem.getSaveDirectory then
        return love.filesystem.getSaveDirectory() .. "/" .. logFile
    end
    return logFile
end

function logger.log(message)
    local line = string.format("[%s] %s", timestamp(), tostring(message))
    print(line)
    appendLine(line)
end

return logger
