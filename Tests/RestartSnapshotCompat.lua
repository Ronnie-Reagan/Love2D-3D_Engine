local compat = require("Source.Ui.RestartSnapshotCompat")

local function assertTrue(value, message)
    if not value then
        error(message or "assertion failed", 2)
    end
end

local function run()
    local legacy = {
        _l2d3d_restart_version = 1,
        state = {
            graphicsSettings = {
                renderScale = 3.4
            },
            playerModels = {
                plane = { scale = -2.5 },
                walking = { scale = -0.01 }
            }
        }
    }

    local migrated, sourceVersion = compat.migrate(legacy, 3, {
        defaultPlaneScale = 1.35,
        defaultWalkingScale = 1.0
    })

    assertTrue(migrated ~= nil, "legacy payload should migrate")
    assertTrue(sourceVersion == 1, "source version should be reported")
    assertTrue(migrated._l2d3d_restart_version == 3, "target restart version should be applied")
    assertTrue(migrated._l2d3d_restart_migrated_from == 1, "migrated-from marker should be set")
    assertTrue(math.abs(migrated.state.graphicsSettings.renderScale - 1.5) < 1e-6, "render scale should clamp")
    assertTrue(migrated.state.playerModels.plane.scale >= 0.1, "plane scale should be positive")
    assertTrue(migrated.state.playerModels.walking.scale >= 0.1, "walking scale should be positive")
    assertTrue(type(migrated.state.flightModelConfig) == "table", "v3 flight model config should exist")
    assertTrue(type(migrated.state.terrainSdfDefaults) == "table", "v3 terrain defaults should exist")
    assertTrue(type(migrated.state.lightingModel) == "table", "v3 lighting model should exist")

    local v2 = {
        _l2d3d_restart_version = 2,
        state = {
            camera = {
                throttle = 0.4,
                flightRotVel = { pitch = 1.1, yaw = 2.2, roll = 3.3 }
            }
        }
    }
    local migratedV2, sourceV2 = compat.migrate(v2, 3)
    assertTrue(migratedV2 ~= nil and sourceV2 == 2, "v2 payload should migrate to v3")
    assertTrue(type(migratedV2.state.camera.flightAngVel) == "table", "v2->v3 should add flight angular velocity")
    assertTrue(type(migratedV2.state.camera.flightSimState) == "table", "v2->v3 should add flight sim state")

    local unsupported, unsupportedErr = compat.migrate({ _l2d3d_restart_version = 99 }, 3)
    assertTrue(unsupported == nil, "unsupported version should fail migration")
    assertTrue(type(unsupportedErr) == "string" and unsupportedErr:find("unsupported"), "unsupported error should be explicit")

    print("Restart snapshot compatibility tests passed")
end

run()
