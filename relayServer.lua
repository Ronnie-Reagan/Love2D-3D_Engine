local enet = require("enet")
local love = require "love" -- avoids nil report from intellisense, safe to remove if causes issues (it should be OK)
local host = enet.host_create("*:1988") -- 1987 is used, increment upwards
local clients = {} -- {[peer:index] = peer, etc.}
local projectiles = {} -- maybe to contain projectile class for bullet handling as server after initial post of creation(validated with pos and such)

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
