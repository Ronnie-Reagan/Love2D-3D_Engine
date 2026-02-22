local enet = require("enet")

local host = enet.host_create("*:1988")
local clients = {}

print("Relay running on port 1988")

function love.update(dt)
    local event = host:service(10)

    while event do
        if event.type == "connect" then
            print("Client connected:", event.peer)
            clients[event.peer:index()] = event.peer
        elseif event.type == "receive" then
            -- Forward to all other clients
            for id, peer in pairs(clients) do
                if peer ~= event.peer then
                    peer:send(event.data .. "|" .. tostring(event.peer:index()))
                end
            end
        elseif event.type == "disconnect" then
            print("Client disconnected:", event.peer)
            clients[event.peer:index()] = nil
        end

        event = host:service()
    end
end
