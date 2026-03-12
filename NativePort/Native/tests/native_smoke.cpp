#include <SDL3/SDL.h>

#include "NativeGame/WorldStore.hpp"
#include "NativeGame/WorldWire.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <array>
#include <string>

#define main trueflight_native_main
#include "../src/main.cpp"
#undef main

namespace {

std::filesystem::path repoRoot()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

std::filesystem::path smokeModelPath()
{
    const auto root = repoRoot();
    const auto glbPath = root / "portSource/Assets/Models/DualEngine.glb";
    if (std::filesystem::exists(glbPath)) {
        return glbPath;
    }

    const auto stlPath = root / "portSource/Assets/Models/DualEngine.stl";
    if (std::filesystem::exists(stlPath)) {
        return stlPath;
    }

    return {};
}

void require(bool condition, const std::string& message, bool& failed)
{
    if (condition) {
        return;
    }

    failed = true;
    std::cerr << "FAIL: " << message << "\n";
}

struct FlightRunResult {
    FlightState plane;
    FlightRuntimeState runtime;
};

FlightState makeFlightState(
    const Vec3& pos = { 0.0f, 250.0f, 0.0f },
    const Vec3& vel = { 0.0f, 0.0f, 35.0f },
    float throttle = 0.62f,
    float collisionRadius = 1.2f)
{
    FlightState plane {};
    plane.pos = pos;
    plane.rot = quatIdentity();
    plane.flightVel = vel;
    plane.vel = vel;
    plane.throttle = throttle;
    plane.collisionRadius = collisionRadius;
    return plane;
}

FlightRunResult runFlightSim(float frameDt, float totalTime, const FlightConfig& config)
{
    FlightRunResult result;
    result.plane = makeFlightState();

    FlightEnvironment environment {};
    environment.wind = { 0.0f, 0.0f, 0.0f };
    environment.groundHeightAt = [](float, float) {
        return -1.0e6f;
    };
    environment.sampleSdf = [](float, float, float) {
        return 1.0e6f;
    };
    environment.sampleNormal = [](float, float, float) {
        return Vec3 { 0.0f, 1.0f, 0.0f };
    };
    environment.collisionRadius = result.plane.collisionRadius;

    float elapsed = 0.0f;
    while (elapsed < totalTime) {
        const float dt = std::min(frameDt, totalTime - elapsed);
        stepFlight(result.plane, result.runtime, dt, elapsed, InputState {}, environment, config);
        elapsed += dt;
    }

    return result;
}

void runFlightParityChecks(bool& failed)
{
    {
        const FlightConfig config = defaultFlightConfig();
        require(std::fabs(config.CL0 - 0.25f) < 1.0e-6f, "Default CL0 no longer matches the Lua flight model", failed);
        require(std::fabs(config.CLElevator - 0.65f) < 1.0e-6f, "Default CLElevator no longer matches the Lua flight model", failed);
        require(std::fabs(config.Cm0 - 0.04f) < 1.0e-6f, "Default Cm0 no longer matches the Lua flight model", failed);
        require(std::fabs(config.CmElevator + 1.35f) < 1.0e-6f, "Default CmElevator no longer matches the Lua flight model", failed);
        require(std::fabs(degrees(config.maxElevatorDeflectionRad) - 25.0f) < 1.0e-4f, "Default elevator limit no longer matches the Lua flight model", failed);
        require(std::fabs(config.pitchControlScale - 0.78f) < 1.0e-6f, "Default pitch control scale no longer matches the Lua flight model", failed);
        require(std::fabs(config.rollControlScale - 0.72f) < 1.0e-6f, "Default roll control scale no longer matches the Lua flight model", failed);
        require(std::fabs(config.yawControlScale - 0.65f) < 1.0e-6f, "Default yaw control scale no longer matches the Lua flight model", failed);
    }

    {
        std::array<FlightRunResult, 4> outcomes {};
        const std::array<int, 4> frameRates { 15, 30, 60, 120 };

        for (std::size_t index = 0; index < frameRates.size(); ++index) {
            FlightConfig config = defaultFlightConfig();
            config.enableAutoTrim = true;
            config.autoTrimUseWorker = false;
            outcomes[index] = runFlightSim(1.0f / static_cast<float>(frameRates[index]), 60.0f, config);
            require(outcomes[index].plane.debug.tick > 0, "Flight dt invariance run failed to advance simulation ticks", failed);
        }

        const FlightRunResult& reference = outcomes.back();
        const int refTick = reference.plane.debug.tick;
        const float refSpeed = length(reference.plane.flightVel);
        const float refAltitude = reference.plane.pos.y;

        for (std::size_t index = 0; index < frameRates.size(); ++index) {
            const FlightRunResult& result = outcomes[index];
            const int tickDelta = std::abs(result.plane.debug.tick - refTick);
            require(tickDelta <= 2, "Native dt invariance tick drift exceeded Lua tolerance", failed);

            const float speed = length(result.plane.flightVel);
            const float altitude = result.plane.pos.y;
            const float speedDiff = std::fabs(speed - refSpeed) / std::max(1.0f, refSpeed);
            const float altitudeDiff = std::fabs(altitude - refAltitude) / std::max(1.0f, std::fabs(refAltitude));
            require(speedDiff < 0.10f, "Native dt invariance speed drift exceeded Lua tolerance", failed);
            require(altitudeDiff < 0.12f, "Native dt invariance altitude drift exceeded Lua tolerance", failed);
            require(speed < 140.0f, "Native dt invariance run hit runaway airspeed", failed);
        }
    }

    {
        FlightConfig config = defaultFlightConfig();
        config.enableAutoTrim = true;
        config.autoTrimUseWorker = false;
        config.massKg = 620.0f;
        config.maxThrustSeaLevel = 12000.0f;
        config.CLalpha = 8.6f;
        config.CD0 = 0.012f;
        config.inducedDragK = 0.028f;
        config.maxLinearSpeed = 260.0f;
        config.maxAngularRateRad = radians(320.0f);
        config.maxForceNewton = 130000.0f;
        config.maxMomentNewtonMeter = 180000.0f;

        const FlightRunResult result = runFlightSim(1.0f / 60.0f, 45.0f, config);
        require(length(result.plane.flightVel) <= 261.0f, "Native extreme-config stability no longer matches Lua guardrails", failed);
        require(!result.runtime.crashed, "Native extreme-config stability unexpectedly triggered a crash", failed);
    }

    {
        FlightConfig config = defaultFlightConfig();
        config.enableAutoTrim = true;
        config.autoTrimUseWorker = false;

        const FlightRunResult result = runFlightSim(1.0f / 60.0f, 60.0f, config);
        require(!result.runtime.crashed, "Zero-wind straight-flight regression unexpectedly crashed", failed);
        require(std::fabs(result.runtime.lastBeta) < radians(6.0f), "Zero-wind straight-flight beta exceeded the bounded-slip target", failed);
        require(std::fabs(result.plane.pos.x) < 160.0f, "Zero-wind straight-flight lateral drift exceeded the bounded target", failed);
        require(std::fabs(result.plane.flightAngVel.y) < radians(12.0f), "Zero-wind straight-flight yaw rate diverged", failed);
        require(std::fabs(result.plane.flightAngVel.z) < radians(25.0f), "Zero-wind straight-flight roll rate diverged", failed);
    }

    {
        FlightConfig config = defaultFlightConfig();
        config.enableAutoTrim = false;
        config.autoTrimUseWorker = false;
        config.maxThrustSeaLevel = 0.0f;

        FlightState plane = makeFlightState({ 0.0f, 2.8f, 0.0f }, { 0.0f, -2.5f, 22.0f }, 0.0f, 1.2f);
        FlightRuntimeState runtime {};
        FlightEnvironment environment {};
        environment.wind = { 0.0f, 0.0f, 0.0f };
        environment.groundHeightAt = [](float, float) {
            return 0.0f;
        };
        environment.sampleSdf = [](float, float y, float) {
            return y;
        };
        environment.sampleNormal = [](float, float, float) {
            return Vec3 { 0.0f, 1.0f, 0.0f };
        };
        environment.collisionRadius = plane.collisionRadius;

        for (int step = 0; step < 500 && !plane.onGround; ++step) {
            stepFlight(plane, runtime, 1.0f / 120.0f, static_cast<float>(step) / 120.0f, InputState {}, environment, config);
        }

        require(plane.onGround, "Lua-style low-speed touchdown no longer settles on the ground in native flight", failed);
        require(!runtime.crashed, "Lua-style low-speed touchdown should not register as a crash", failed);
        require(plane.pos.y >= 1.0f && plane.pos.y <= 2.6f, "Lua-style touchdown height drifted in native flight", failed);
    }

    {
        FlightConfig config = defaultFlightConfig();
        config.enableAutoTrim = false;
        config.autoTrimUseWorker = false;
        config.maxThrustSeaLevel = 0.0f;

        FlightState plane = makeFlightState({ 0.0f, 8.5f, 0.0f }, { 0.0f, -45.0f, 55.0f }, 0.0f, 1.2f);
        FlightRuntimeState runtime {};
        FlightEnvironment environment {};
        environment.wind = { 0.0f, 0.0f, 0.0f };
        environment.groundHeightAt = [](float, float) {
            return 0.0f;
        };
        environment.sampleSdf = [](float, float y, float) {
            return y;
        };
        environment.sampleNormal = [](float, float, float) {
            return Vec3 { 0.0f, 1.0f, 0.0f };
        };
        environment.collisionRadius = plane.collisionRadius;

        for (int step = 0; step < 360 && !runtime.crashed; ++step) {
            stepFlight(plane, runtime, 1.0f / 120.0f, static_cast<float>(step) / 120.0f, InputState {}, environment, config);
        }

        require(runtime.crashed, "Lua-style high-speed terrain impact no longer crashes in native flight", failed);
        require(length(plane.flightVel) <= 1.0e-6f, "Crash handling should zero linear velocity like the Lua flight system", failed);
        require(length(plane.flightAngVel) <= 1.0e-6f, "Crash handling should zero angular velocity like the Lua flight system", failed);
    }

    {
        constexpr float slope = 0.12f;
        const Vec3 slopeNormal = normalize({ -slope, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });

        FlightConfig config = defaultFlightConfig();
        config.enableAutoTrim = false;
        config.autoTrimUseWorker = false;
        config.maxThrustSeaLevel = 0.0f;
        config.crashEnabled = false;

        FlightState plane = makeFlightState({ 10.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 1.2f);
        FlightRuntimeState runtime {};
        FlightEnvironment environment {};
        environment.wind = { 0.0f, 0.0f, 0.0f };
        environment.groundHeightAt = [slope](float x, float) {
            return slope * x;
        };
        environment.sampleSdf = [slope](float x, float y, float) {
            return y - (slope * x);
        };
        environment.sampleNormal = [slopeNormal](float, float, float) {
            return slopeNormal;
        };
        environment.collisionRadius = plane.collisionRadius;

        stepFlight(plane, runtime, 1.0f / 120.0f, 0.0f, InputState {}, environment, config);

        const float signedDistance = environment.sampleSdf(plane.pos.x, plane.pos.y, plane.pos.z);
        require(signedDistance >= (plane.collisionRadius - 0.05f), "Slope contact no longer resolves to collision radius like the Lua sim", failed);
        require(plane.onGround, "Slope contact should still report onGround in native flight", failed);
    }

    {
        FlightConfig config = defaultFlightConfig();
        config.enableAutoTrim = false;
        config.autoTrimUseWorker = false;

        FlightState plane = makeFlightState();
        plane.manualElevatorTrim = radians(2.5f);
        FlightRuntimeState runtime {};
        FlightEnvironment environment {};
        environment.wind = { 0.0f, 0.0f, 0.0f };
        environment.groundHeightAt = [](float, float) {
            return -1.0e6f;
        };
        environment.sampleSdf = [](float, float, float) {
            return 1.0e6f;
        };
        environment.sampleNormal = [](float, float, float) {
            return Vec3 { 0.0f, 1.0f, 0.0f };
        };
        environment.collisionRadius = plane.collisionRadius;

        stepFlight(plane, runtime, 1.0f / 120.0f, 0.0f, InputState {}, environment, config);

        require(
            std::fabs(runtime.elevatorDeflection - plane.manualElevatorTrim) < radians(0.1f),
            "Manual trim should feed the native elevator target",
            failed);
    }
}

void runAudioFrameChecks(bool& failed)
{
    UiState uiState {};
    TerrainFieldContext terrainContext = createTerrainFieldContext(defaultTerrainParams());
    PropAudioConfig propAudioConfig = defaultPropAudioConfig();
    propAudioConfig.baseRpm = 42.0f;
    propAudioConfig.engineFrequencyScale = 1.15f;
    propAudioConfig.ambienceFrequencyScale = 0.85f;

    FlightState plane = makeFlightState(
        { 0.0f, terrainContext.params.waterLevel + 12.0f, 0.0f },
        { 18.0f, -6.0f, 92.0f },
        0.62f,
        1.2f);
    plane.rot = quatNormalize(quatFromAxisAngle({ 0.0f, 1.0f, 0.0f }, radians(8.0f)));
    plane.flightAngVel = { radians(12.0f), radians(4.0f), radians(20.0f) };

    FlightRuntimeState runtime {};
    runtime.lastDynamicPressure = 4100.0f;
    runtime.lastThrustNewton = 2300.0f;
    runtime.lastAlpha = radians(6.0f);
    runtime.lastBeta = radians(-4.0f);

    FlightEnvironment environment {};
    environment.wind = { 5.0f, 1.5f, -12.0f };

    const ProceduralAudioFrame frame = buildProceduralAudioFrame(
        plane,
        runtime,
        environment,
        terrainContext,
        uiState,
        propAudioConfig,
        0.35f,
        0.7f,
        true,
        false,
        1.0f / 60.0f);

    const Vec3 airVelWorld = plane.flightVel - environment.wind;
    const Vec3 airVelBody = worldToBody(plane.rot, airVelWorld);
    const float expectedWaterProximity = computeWaterProximity(plane, terrainContext);
    require(std::fabs(frame.trueAirspeed - length(airVelBody)) < 1.0e-4f, "Audio frame should use air-relative speed", failed);
    require(std::fabs(frame.verticalSpeed - airVelWorld.y) < 1.0e-4f, "Audio frame should use air-relative vertical speed", failed);
    require(std::fabs(frame.angularRateRad - length(plane.flightAngVel)) < 1.0e-4f, "Audio frame should expose body-rate magnitude", failed);
    require(std::fabs(frame.dynamicPressure - runtime.lastDynamicPressure) < 1.0e-4f, "Audio frame should expose runtime q-bar", failed);
    require(std::fabs(frame.thrustNewton - runtime.lastThrustNewton) < 1.0e-4f, "Audio frame should expose runtime thrust", failed);
    require(std::fabs(frame.waterProximity - expectedWaterProximity) < 1.0e-4f, "Audio frame should preserve water proximity sampling", failed);
    require(std::fabs(frame.foliageBrush - 0.35f) < 1.0e-4f, "Audio frame should preserve foliage brush amount", failed);
    require(std::fabs(frame.foliageImpact - 0.7f) < 1.0e-4f, "Audio frame should preserve foliage impact amount", failed);
    require(std::fabs(frame.propAudioConfig.baseRpm - propAudioConfig.baseRpm) < 1.0e-4f, "Audio frame should preserve aircraft-local base RPM config", failed);
    require(std::fabs(frame.propAudioConfig.engineFrequencyScale - propAudioConfig.engineFrequencyScale) < 1.0e-4f, "Audio frame should preserve aircraft-local engine frequency scaling", failed);
}

void runTerrainLodChecks(bool& failed)
{
    const TerrainParams params = normalizeTerrainParams(defaultTerrainParams());
    const float lod1TileSize = computeLod1TerrainTileSize(params);
    const float lod2TileSize = computeLod2TerrainTileSize(params);
    const float nearSpan = computeNearHalfExtent(params, lod1TileSize) * 2.0f;
    const float midSpan = lod1TileSize;
    const float farSpan = lod2TileSize;

    const float nearStep = terrainPatchAxisStep(params, params.lod0CellSize, nearSpan);
    const float midStep = terrainPatchAxisStep(params, params.lod1CellSize, midSpan);
    const float farStep = terrainPatchAxisStep(params, params.lod2CellSize, farSpan);

    require(
        nearStep < midStep,
        "Terrain patch budgeting regressed: LOD0 is not finer than LOD1 over the default near span",
        failed);
    require(
        midStep < farStep,
        "Terrain patch budgeting regressed: LOD1 is not finer than LOD2 over the default far spans",
        failed);

    const TerrainFieldContext terrainContext = createTerrainFieldContext(params);
    const Model nearPatch = buildSurfaceTerrainPatch(
        terrainContext,
        { -64.0f, 64.0f, -64.0f, 64.0f, false, 0.0f, 0.0f, 0.0f, 0.0f },
        params.lod0CellSize);
    require(
        !nearPatch.vertices.empty() && nearPatch.vertexNormals.size() == nearPatch.vertices.size(),
        "Surface terrain patch no longer generates per-vertex normals for near-field lighting",
        failed);
}

void runTerrainDecorationChecks(bool& failed)
{
    TerrainParams params = defaultTerrainParams();
    params.decoration.enabled = true;
    params.decoration.density = 1.6f;
    params.decoration.nearDensityScale = 1.2f;
    params.decoration.midDensityScale = 0.8f;
    params.decoration.farDensityScale = 0.24f;
    params.decoration.shoreBrushDensity = 1.4f;
    params.decoration.rockDensity = 1.2f;
    params.decoration.treeLineOffset = -20.0f;
    params = normalizeTerrainParams(params);
    const TerrainFieldContext terrainContext = createTerrainFieldContext(params);

    TerrainChunkKey key;
    key.worldId = "smoke";
    key.seed = params.seed;
    key.generatorVersion = params.generatorVersion;
    key.band = static_cast<int>(TerrainFarTileBand::Near);
    key.detail = static_cast<int>(TerrainFarTileDetail::Lod0);
    key.paramsSignature = terrainParamsSignature(params);

    const float tileSize = computeLod0TerrainTileSize(params);
    TerrainPatchBounds bounds {};
    TerrainTileDecorationResult first;
    TerrainTileDecorationResult second;
    bool foundRichTile = false;
    for (int tileZ = -4; tileZ <= 4 && !foundRichTile; ++tileZ) {
        for (int tileX = -4; tileX <= 4 && !foundRichTile; ++tileX) {
            key.tileX = tileX;
            key.tileZ = tileZ;
            bounds = {
                static_cast<float>(tileX) * tileSize,
                (static_cast<float>(tileX) + 1.0f) * tileSize,
                static_cast<float>(tileZ) * tileSize,
                (static_cast<float>(tileZ) + 1.0f) * tileSize,
                false,
                0.0f,
                0.0f,
                0.0f,
                0.0f
            };
            first = buildTerrainTileDecoration(key, bounds, TerrainFarTileBand::Near, terrainContext);
            second = buildTerrainTileDecoration(key, bounds, TerrainFarTileBand::Near, terrainContext);
            bool sawBrush = false;
            bool sawBlocker = false;
            for (const TerrainPropCollider& collider : first.propColliders) {
                sawBrush = sawBrush || collider.propClass == TerrainPropClass::Brush;
                sawBlocker = sawBlocker || collider.propClass == TerrainPropClass::Blocker;
            }
            foundRichTile = sawBrush && sawBlocker;
        }
    }
    require(foundRichTile, "Failed to find a smoke tile with both brush and blocker terrain props", failed);
    if (!foundRichTile) {
        return;
    }
    bool modelsMatch =
        first.propModel.vertices.size() == second.propModel.vertices.size() &&
        first.propModel.faces.size() == second.propModel.faces.size() &&
        first.propModel.faceColors.size() == second.propModel.faceColors.size() &&
        first.propColliders.size() == second.propColliders.size();
    if (modelsMatch) {
        for (std::size_t i = 0; i < first.propModel.vertices.size(); ++i) {
            const Vec3& a = first.propModel.vertices[i];
            const Vec3& b = second.propModel.vertices[i];
            if (std::fabs(a.x - b.x) > 1.0e-4f || std::fabs(a.y - b.y) > 1.0e-4f || std::fabs(a.z - b.z) > 1.0e-4f) {
                modelsMatch = false;
                break;
            }
        }
    }
    if (modelsMatch) {
        for (std::size_t i = 0; i < first.propColliders.size(); ++i) {
            const TerrainPropCollider& a = first.propColliders[i];
            const TerrainPropCollider& b = second.propColliders[i];
            if (a.propClass != b.propClass ||
                std::fabs(a.center.x - b.center.x) > 1.0e-4f ||
                std::fabs(a.center.y - b.center.y) > 1.0e-4f ||
                std::fabs(a.center.z - b.center.z) > 1.0e-4f ||
                std::fabs(a.radius - b.radius) > 1.0e-4f ||
                std::fabs(a.halfHeight - b.halfHeight) > 1.0e-4f) {
                modelsMatch = false;
                break;
            }
        }
    }
    require(
        modelsMatch,
        "Terrain decoration should be deterministic for the same tile seed and params",
        failed);
    require(!first.propModel.vertices.empty(), "Terrain decoration should generate visible prop geometry for the smoke tile", failed);
    require(!first.propColliders.empty(), "Terrain decoration should generate interaction volumes for the smoke tile", failed);

    bool foundBrush = false;
    bool foundBlocker = false;
    TerrainPropCollider brushCollider {};
    TerrainPropCollider blockerCollider {};
    for (const TerrainPropCollider& collider : first.propColliders) {
        const float ground = sampleGroundHeight(collider.center.x, collider.center.z, terrainContext);
        const float water = sampleWaterHeight(collider.center.x, collider.center.z, terrainContext);
        require(ground >= water - 0.2f, "Terrain decoration should not place props underwater", failed);
        if (collider.propClass == TerrainPropClass::Brush && !foundBrush) {
            foundBrush = true;
            brushCollider = collider;
            require(sampleTerrainSlope01(collider.center.x, collider.center.z, terrainContext) < 0.82f, "Brush props should avoid extreme slopes", failed);
        } else if (collider.propClass == TerrainPropClass::Blocker && !foundBlocker) {
            foundBlocker = true;
            blockerCollider = collider;
        }
    }
    require(foundBrush, "Smoke tile should contain at least one soft foliage volume", failed);
    require(foundBlocker, "Smoke tile should contain at least one blocker prop volume", failed);

    TerrainVisualCache terrainCache;
    terrainCache.lod0TileSize = tileSize;
    terrainCache.lod1TileSize = computeLod1TerrainTileSize(params);
    terrainCache.lod2TileSize = computeLod2TerrainTileSize(params);
    TerrainFarTile nearTile;
    nearTile.band = TerrainFarTileBand::Near;
    nearTile.detail = TerrainFarTileDetail::Lod0;
    nearTile.tileX = key.tileX;
    nearTile.tileZ = key.tileZ;
    nearTile.active = true;
    nearTile.propColliders = first.propColliders;
    terrainCache.nearTiles.push_back(nearTile);

    TerrainVisualCache blockerOnlyCache = terrainCache;
    blockerOnlyCache.nearTiles.front().propColliders = { blockerCollider };
    TerrainVisualCache brushOnlyCache = terrainCache;
    brushOnlyCache.nearTiles.front().propColliders = { brushCollider };

    const float brushAmount = computeBrushContactAmount(
        brushOnlyCache,
        brushCollider.center,
        0.5f,
        brushCollider.center.y - 0.6f,
        brushCollider.center.y + 0.6f);
    require(brushAmount > 0.0f, "Soft foliage overlap should report a brush contact amount", failed);

    FlightState blockerPlane = makeFlightState(
        { blockerCollider.center.x, blockerCollider.center.y, blockerCollider.center.z },
        { 0.0f, 0.0f, 48.0f },
        0.5f,
        1.2f);
    blockerPlane.flightAngVel = { 0.0f, 0.0f, 0.0f };
    FlightCrashEvent propCrash {};
    require(
        detectFlightPropCollision(blockerOnlyCache, params.decoration, blockerPlane, 42, propCrash),
        "Blocker prop overlap should trigger the prop-crash detection path",
        failed);
    require(propCrash.cause == FlightCrashCause::PropBlocker, "Prop crash detection should label the crash as a blocker-prop impact", failed);

    FlightState brushPlane = makeFlightState(
        { brushCollider.center.x, brushCollider.center.y, brushCollider.center.z },
        { 0.0f, 0.0f, 48.0f },
        0.5f,
        1.2f);
    FlightCrashEvent brushCrash {};
    require(
        !detectFlightPropCollision(brushOnlyCache, params.decoration, brushPlane, 43, brushCrash),
        "Soft foliage overlap should not be treated as a hard crash",
        failed);

    FlightState walker {};
    walker.rot = quatIdentity();
    walker.pos = { blockerCollider.center.x, blockerCollider.center.y, blockerCollider.center.z };
    walker.vel = { 2.0f, 0.0f, 0.0f };
    resolveWalkingPropCollisions(terrainCache, params.decoration, walker);
    const float horizontalDistance = std::sqrt(
        ((walker.pos.x - blockerCollider.center.x) * (walker.pos.x - blockerCollider.center.x)) +
        ((walker.pos.z - blockerCollider.center.z) * (walker.pos.z - blockerCollider.center.z)));
    require(
        horizontalDistance >= (blockerCollider.radius + kWalkingCollisionRadius - 0.05f),
        "Walking collision should resolve the actor outside blocker props",
        failed);

    TerrainParams changedParams = params;
    changedParams.decoration.density += 0.35f;
    require(
        terrainParamsSignature(changedParams) != terrainParamsSignature(params),
        "Terrain decoration settings should participate in the terrain chunk signature",
        failed);
}

}  // namespace

int main()
{
    SDL_Init(0);

    bool failed = false;
    const std::filesystem::path root = repoRoot();
    const std::filesystem::path modelPath = smokeModelPath();
    require(!modelPath.empty(), "Missing smoke test model asset under portSource/Assets/Models", failed);
    if (modelPath.empty()) {
        SDL_Quit();
        return 1;
    }

    if (modelPath.extension() == ".glb" || modelPath.extension() == ".gltf") {
        std::string gltfError;
        auto model = loadGltf(modelPath, &gltfError);
        require(model.has_value(), "Textured glTF failed to load: " + gltfError, failed);
        if (model.has_value()) {
            require(model->hasTexCoords, "Loaded glTF did not populate texture coordinates", failed);
            require(!model->materials.empty(), "Loaded glTF did not populate material metadata", failed);
            require(!model->images.empty(), "Loaded glTF did not populate decoded images", failed);
        }
    }

    const std::filesystem::path tempRoot = root / "build/native-smoke-temp";
    std::error_code ec;
    std::filesystem::remove_all(tempRoot, ec);
    std::filesystem::create_directories(tempRoot, ec);

    const std::filesystem::path settingsPath = tempRoot / "native_settings.ini";
    {
        std::ofstream file(settingsPath, std::ios::binary | std::ios::trunc);
        file << "model.source_path=" << modelPath.generic_string() << "\n";
        file << "character.walking.scale=1.5\n";
        file << "paint.walking.hash=\n";
    }

    UiState uiState = defaultUiState();
    GraphicsSettings graphicsSettings = defaultGraphicsSettings();
    LightingSettings lightingSettings = defaultLightingSettings();
    HudSettings hudSettings = defaultHudSettings();
    ControlProfile controls = defaultControlProfile();
    AircraftProfile planeProfile {};
    planeProfile.visualPrefs.scale = 3.0f;
    TerrainParams terrainParams = defaultTerrainParams();
    VisualPreferenceData walkingPrefs {};
    walkingPrefs.scale = 1.0f;
    std::string preferenceError;
    {
        std::ofstream file(settingsPath, std::ios::binary | std::ios::app);
        file << "character.plane.forward_axis_yaw_degrees=-35\n";
        file << "aircraft.plane.audio.base_rpm=51\n";
        file << "aircraft.plane.audio.engine_frequency_scale=1.10\n";
        file << "ui.walking_move_speed=18\n";
        file << "graphics.window_mode=borderless\n";
        file << "graphics.resolution_width=1920\n";
        file << "graphics.resolution_height=1080\n";
        file << "lighting.show_sun_marker=1\n";
        file << "lighting.sun_yaw_degrees=35\n";
        file << "lighting.marker_size=260\n";
        file << "lighting.gi_specular=0.23\n";
        file << "lighting.gi_bounce=0.17\n";
        file << "lighting.sky_tint_g=0.88\n";
        file << "lighting.fog_density=0.0007\n";
        file << "lighting.fog_b=0.91\n";
        file << "hud.show_speedometer=0\n";
        file << "hud.speedometer_redline_kph=900\n";
        file << "terrain.props_enabled=1\n";
        file << "terrain.prop_density=1.7\n";
        file << "terrain.prop_density_near=1.3\n";
        file << "terrain.prop_density_mid=0.7\n";
        file << "terrain.prop_density_far=0.2\n";
        file << "terrain.prop_shore_brush_density=1.4\n";
        file << "terrain.prop_rock_density=1.1\n";
        file << "terrain.prop_tree_line_offset=-24\n";
        file << "terrain.prop_collision=0\n";
        file << "terrain.prop_seed_offset=222\n";
        file << "controls.flight_pitch_down.primary=key:26\n";
        file << "controls.voice_ptt.primary=key:999\n";
        file << "hud.show_peer_indicators=1\n";
    }
    require(loadPreferences(settingsPath, uiState, graphicsSettings, lightingSettings, hudSettings, controls, planeProfile, terrainParams, walkingPrefs, &preferenceError),
        "Preference file failed to load: " + preferenceError,
        failed);
    require(planeProfile.visualPrefs.hasStoredPath, "Legacy model.source_path did not migrate into plane preferences", failed);
    require(planeProfile.visualPrefs.sourcePath == modelPath, "Migrated plane source path does not match stored legacy path", failed);
    require(std::abs(walkingPrefs.scale - 1.5f) < 0.001f, "Walking role scale did not load from character.walking.scale", failed);
    require(std::abs(uiState.walkingMoveSpeed - 18.0f) < 0.001f, "Walking move speed did not load from preferences", failed);
    require(std::fabs(terrainParams.decoration.density - 1.7f) < 0.001f, "Terrain prop density did not load from preferences", failed);
    require(std::fabs(terrainParams.decoration.nearDensityScale - 1.3f) < 0.001f, "Near-field terrain prop density did not load from preferences", failed);
    require(std::fabs(terrainParams.decoration.farDensityScale - 0.2f) < 0.001f, "Far-field terrain prop density did not load from preferences", failed);
    require(!terrainParams.decoration.collisionEnabled, "Terrain prop collision toggle did not load from preferences", failed);
    require(terrainParams.decoration.seedOffset == 222, "Terrain prop seed offset did not load from preferences", failed);
    require(std::abs(planeProfile.visualPrefs.forwardAxisYawDegrees + 35.0f) < 0.001f, "Plane forward-axis calibration did not load from preferences", failed);
    require(std::abs(planeProfile.propAudioConfig.baseRpm - 51.0f) < 0.001f, "Aircraft-local prop audio base RPM did not load from preferences", failed);
    require(graphicsSettings.windowMode == WindowMode::Borderless, "Graphics window mode did not load from preferences", failed);
    require(graphicsSettings.resolutionWidth == 1920 && graphicsSettings.resolutionHeight == 1080, "Graphics resolution did not load from preferences", failed);
    require(lightingSettings.showSunMarker, "Lighting sun-marker toggle did not load from preferences", failed);
    require(std::abs(lightingSettings.sunYawDegrees - 35.0f) < 0.001f, "Lighting sun yaw did not load from preferences", failed);
    require(std::abs(lightingSettings.markerSize - 260.0f) < 0.001f, "Lighting sun marker size did not load from preferences", failed);
    require(std::abs(lightingSettings.specularAmbient - 0.23f) < 0.001f, "Lighting GI specular did not load from preferences", failed);
    require(std::abs(lightingSettings.bounceStrength - 0.17f) < 0.001f, "Lighting GI bounce did not load from preferences", failed);
    require(std::abs(lightingSettings.skyTint.y - 0.88f) < 0.001f, "Lighting sky tint did not load from preferences", failed);
    require(std::abs(lightingSettings.fogDensity - 0.0007f) < 0.00001f, "Lighting fog density did not load from preferences", failed);
    require(std::abs(lightingSettings.fogColor.z - 0.91f) < 0.001f, "Lighting fog color did not load from preferences", failed);
    require(!hudSettings.showSpeedometer, "HUD speedometer toggle did not load from preferences", failed);
    require(hudSettings.speedometerRedlineKph == 900, "HUD speedometer redline did not load from preferences", failed);
    require(!hudSettings.showPeerIndicators, "Unsupported HUD rows should ignore persisted values", failed);
    {
        const ControlActionBinding* pitchDown = findControlAction(controls, InputActionId::FlightPitchDown);
        require(pitchDown != nullptr, "Pitch down control binding missing after preference load", failed);
        require(
            pitchDown != nullptr &&
                pitchDown->slots[0].kind == BindingKind::Key &&
                pitchDown->slots[0].scancode == SDL_SCANCODE_W,
            "Control binding round-trip did not restore the configured primary slot",
            failed);

        const ControlActionBinding* walkForward = findControlAction(controls, InputActionId::WalkForward);
        require(walkForward != nullptr, "Walk forward control binding missing after preference load", failed);
        require(
            walkForward != nullptr && walkForward->slots[0].scancode == SDL_SCANCODE_W,
            "Supported walking control rows should retain their default primary slot when not explicitly changed",
            failed);

        const ControlActionBinding* voicePtt = findControlAction(controls, InputActionId::VoicePushToTalk);
        require(voicePtt != nullptr, "Voice push-to-talk control binding missing after preference load", failed);
        require(
            voicePtt != nullptr && voicePtt->slots[0].scancode == SDL_SCANCODE_V,
            "Unsupported control rows should ignore persisted values",
            failed);
    }

    {
        require(bindingModifiersMatch(SDL_KMOD_CTRL, SDL_KMOD_LCTRL), "Modifier matching should treat left ctrl as ctrl", failed);
        require(
            bindingModifiersMatch(
                static_cast<SDL_Keymod>(SDL_KMOD_CTRL | SDL_KMOD_SHIFT),
                static_cast<SDL_Keymod>(SDL_KMOD_RCTRL | SDL_KMOD_LSHIFT)),
            "Modifier matching should preserve multi-modifier combinations across left/right variants",
            failed);

        const ControlProfile defaultControls = defaultControlProfile();
        require(
            controlActionTriggeredByWheel(defaultControls, InputActionId::FlightTrimUp, 1, SDL_KMOD_LCTRL),
            "Default ctrl+wheel-up trim binding should trigger with left ctrl",
            failed);
        require(
            controlActionTriggeredByWheel(defaultControls, InputActionId::FlightTrimDown, -1, SDL_KMOD_RCTRL),
            "Default ctrl+wheel-down trim binding should trigger with right ctrl",
            failed);
        require(
            controlActionTriggeredByKey(defaultControls, InputActionId::PaintUndo, SDL_SCANCODE_Z, SDL_KMOD_LCTRL),
            "Default ctrl+Z paint binding should trigger with left ctrl",
            failed);
    }

    {
        PauseState promptState;
        beginModelPathPrompt(promptState, CharacterSubTab::Player, "C:/Models/Test.glb");
        require(promptState.promptActive, "Model prompt should activate when opened", failed);
        require(promptState.promptRole == CharacterSubTab::Player, "Model prompt should preserve the requested role", failed);
        require(promptState.promptCursor == static_cast<int>(promptState.promptText.size()), "Model prompt cursor should start at the end of the initial text", failed);
        moveMenuPromptCursor(promptState, -4);
        require(eraseMenuPromptText(promptState, true), "Model prompt backspace editing should remove characters", failed);
        require(insertMenuPromptText(promptState, "_patched"), "Model prompt text insertion should append at the cursor", failed);
        clearMenuPrompt(promptState);
        require(!promptState.promptActive && promptState.promptText.empty(), "Model prompt clear should fully reset transient prompt state", failed);

        PauseState confirmState;
        requestMenuConfirmation(confirmState, 2, "Confirm quit.", 10.0f, 1.0f);
        require(menuConfirmationMatches(confirmState, 2, 10.5f), "Menu confirmation should remain active before its timeout", failed);
        refreshMenuConfirmation(confirmState, 11.1f);
        require(!confirmState.confirmPending, "Menu confirmation should expire after its timeout", failed);

        ControlProfile clearableControls = defaultControlProfile();
        PauseState controlsMenu;
        controlsMenu.controlsSelection = 0;
        controlsMenu.controlsSlot = 1;
        require(clearSelectedControlBindingSlot(controlsMenu, clearableControls), "Controls shortcut helper should clear configurable binding slots", failed);
        const ControlActionBinding* clearedPitchDown = findControlAction(clearableControls, InputActionId::FlightPitchDown);
        require(
            clearedPitchDown != nullptr && clearedPitchDown->slots[1].kind == BindingKind::None,
            "Clearing a selected control slot should unbind that slot",
            failed);

        int unsupportedIndex = -1;
        for (std::size_t actionIndex = 0; actionIndex < clearableControls.actions.size(); ++actionIndex) {
            if (clearableControls.actions[actionIndex].id == InputActionId::VoicePushToTalk) {
                unsupportedIndex = static_cast<int>(actionIndex);
                break;
            }
        }
        require(unsupportedIndex >= 0, "Voice push-to-talk binding should exist for unsupported-row smoke checks", failed);
        if (unsupportedIndex >= 0) {
            controlsMenu.controlsSelection = unsupportedIndex;
            controlsMenu.controlsSlot = 0;
            require(
                !clearSelectedControlBindingSlot(controlsMenu, clearableControls),
                "Controls shortcut helper should ignore unsupported rows",
                failed);
        }
    }

    {
        PlaneVisualState planeVisual;
        planeVisual.defaultScale = 3.0f;
        setBuiltinPlaneModel(planeVisual);
        applyVisualPreferenceData(planeVisual, planeProfile.visualPrefs);
        const Quat offset = composeVisualRotationOffset(planeVisual);
        const float visualYawDegrees = degrees(getStableYawFromRotation(offset));
        require(std::fabs(visualYawDegrees + 35.0f) < 1.0f, "Forward-axis calibration should be applied independently of the flight attitude", failed);

        PlaneVisualState walkingVisual;
        walkingVisual.defaultScale = 1.0f;
        setBuiltinPlaneModel(walkingVisual);
        applyVisualPreferenceData(walkingVisual, walkingPrefs);

        const std::filesystem::path savedSettingsPath = tempRoot / "saved_settings.ini";
        std::string savePreferenceError;
        require(
            savePreferences(savedSettingsPath, uiState, graphicsSettings, lightingSettings, hudSettings, controls, planeProfile, terrainParams, planeVisual, walkingVisual, &savePreferenceError),
            "Preference file failed to save: " + savePreferenceError,
            failed);

        std::ifstream savedFile(savedSettingsPath, std::ios::binary);
        const std::string savedContents((std::istreambuf_iterator<char>(savedFile)), std::istreambuf_iterator<char>());
        require(
            savedContents.find("aircraft.plane.audio.base_rpm=51") != std::string::npos,
            "Saved preferences did not persist aircraft-local prop audio keys",
            failed);
        require(
            savedContents.find("ui.walking_move_speed=18") != std::string::npos &&
            savedContents.find("graphics.window_mode=borderless") != std::string::npos &&
                savedContents.find("lighting.show_sun_marker=1") != std::string::npos &&
                savedContents.find("lighting.sun_yaw_degrees=35") != std::string::npos &&
                savedContents.find("lighting.gi_specular=0.23") != std::string::npos &&
                savedContents.find("lighting.fog_b=0.91") != std::string::npos &&
                savedContents.find("hud.show_speedometer=0") != std::string::npos &&
                savedContents.find("terrain.prop_density=1.7") != std::string::npos &&
                savedContents.find("terrain.prop_collision=0") != std::string::npos,
            "Saved preferences did not persist UI, graphics, lighting, and HUD keys",
            failed);
        require(
            savedContents.find("controls.flight_pitch_down.primary=key:26") != std::string::npos,
            "Saved preferences did not persist supported control bindings",
            failed);
        require(
            savedContents.find("controls.voice_ptt.primary=") == std::string::npos &&
                savedContents.find("hud.show_peer_indicators=") == std::string::npos,
            "Disabled rows should not be written back to native_settings.ini",
            failed);
    }

    {
        LightingSettings analyticLighting = defaultLightingSettings();
        analyticLighting.sunTint = { 1.0f, 0.55f, 0.40f };
        analyticLighting.skyTint = { 0.70f, 1.15f, 0.90f };
        analyticLighting.exposureEv = 0.8f;

        const RendererLightingState fogEnabled = evaluateRendererLightingState(analyticLighting, true);
        const RendererLightingState fogDisabled = evaluateRendererLightingState(analyticLighting, false);
        require(fogEnabled.fogDensity > 0.0f, "Renderer lighting evaluation should preserve fog density when horizon fog is enabled", failed);
        require(fogDisabled.fogDensity == 0.0f, "Renderer lighting evaluation should zero fog density when horizon fog is disabled", failed);
        require(fogEnabled.lightColor.y < fogEnabled.lightColor.x, "Sun tint should affect evaluated light color", failed);
        require(fogEnabled.skyColor.y > fogEnabled.skyColor.x, "Sky tint should affect the evaluated sky color", failed);
        require(fogEnabled.backgroundColor.x > 0.0f && fogEnabled.backgroundColor.y > 0.0f && fogEnabled.backgroundColor.z > 0.0f,
            "Renderer lighting evaluation should produce a visible background color",
            failed);
    }

    runFlightParityChecks(failed);
    runAudioFrameChecks(failed);
    runTerrainLodChecks(failed);
    runTerrainDecorationChecks(failed);

    WorldChunkState wireChunk;
    wireChunk.cx = 3;
    wireChunk.cz = -2;
    wireChunk.resolution = 8;
    wireChunk.revision = 5;
    wireChunk.materialRevision = 7;
    wireChunk.heightDeltas = { 0.0f, 1.25f, -2.5f, 3.75f };
    wireChunk.volumetricOverrides = {
        { "sphere", 1.0f, 2.0f, 3.0f, 4.0f }
    };
    const WorldKeyValueFields chunkFields = buildChunkStateFields(wireChunk);
    const WorldChunkState decodedWireChunk = decodeChunkStateFields(buildChunkFieldLookup(chunkFields));
    require(decodedWireChunk.cx == wireChunk.cx && decodedWireChunk.cz == wireChunk.cz, "World wire chunk coords did not round-trip", failed);
    require(decodedWireChunk.revision == wireChunk.revision, "World wire chunk revision did not round-trip", failed);
    require(decodedWireChunk.heightDeltas.size() == wireChunk.heightDeltas.size(), "World wire height deltas did not round-trip", failed);
    require(decodedWireChunk.volumetricOverrides.size() == 1u, "World wire volumetric overrides did not round-trip", failed);

    TerrainTunnelSeed smokeSeed;
    smokeSeed.radius = 6.0f;
    smokeSeed.hillAttached = true;
    smokeSeed.points = {
        { 0.0f, 10.0f, 0.0f },
        { 12.0f, 11.0f, -3.0f }
    };
    const std::vector<TerrainTunnelSeed> encodedSeedsSource { smokeSeed };
    const std::vector<TerrainTunnelSeed> decodedSeeds = decodeTunnelSeeds(encodeTunnelSeeds(encodedSeedsSource));
    require(decodedSeeds.size() == 1u, "World wire tunnel seed count did not round-trip", failed);
    require(decodedSeeds[0].hillAttached, "World wire tunnel hillAttached flag did not round-trip", failed);
    require(decodedSeeds[0].points.size() == 2u, "World wire tunnel points did not round-trip", failed);

    WorldStoreOptions worldOptions;
    worldOptions.name = "smoke_world";
    worldOptions.storageRoot = tempRoot / "worlds";
    worldOptions.createIfMissing = true;
    worldOptions.regionSize = 8;
    worldOptions.chunkResolution = 8;
    worldOptions.groundParams = defaultTerrainParams();
    worldOptions.groundParams.seed = 2468;
    worldOptions.groundParams.chunkSize = 128.0f;
    worldOptions.groundParams.tunnelCount = 2;
    worldOptions.spawn = { 12.0f, 34.0f, 56.0f };

    std::string worldError;
    auto openedWorld = WorldStore::open(worldOptions, &worldError);
    require(openedWorld.has_value(), "World store failed to open: " + worldError, failed);
    if (openedWorld.has_value()) {
        WorldStore world = std::move(*openedWorld);
        const WorldMeta meta = world.getMeta();
        require(meta.worldId == "smoke_world", "World store did not preserve the requested world id", failed);
        require(!meta.tunnelSeeds.empty(), "World store did not populate tunnel seeds", failed);

        const auto craterResult = world.applyCrater({ 32.0f, 0.0f, 24.0f, 10.0f, 4.0f, 0.15f });
        require(craterResult.first && !craterResult.second.empty(), "World store crater application did not dirty any chunks", failed);

        std::string flushError;
        require(world.flushDirty(&flushError) > 0, "World store failed to flush dirty regions: " + flushError, failed);

        worldOptions.createIfMissing = false;
        openedWorld = WorldStore::open(worldOptions, &worldError);
        require(openedWorld.has_value(), "World store failed to reopen persisted data: " + worldError, failed);
        if (openedWorld.has_value()) {
            WorldStore reopenedWorld = std::move(*openedWorld);
            require(std::fabs(reopenedWorld.sampleHeightDelta(32.0f, 24.0f)) > 0.01f, "World store height deltas did not persist", failed);

            const auto latestChunk = reopenedWorld.getChunkState(craterResult.second.front().cx, craterResult.second.front().cz);
            require(latestChunk.has_value() && latestChunk->revision >= 1, "World store did not expose persisted chunk revisions", failed);
            if (latestChunk.has_value()) {
                WorldChunkState staleChunk = *latestChunk;
                staleChunk.revision = std::max(0, staleChunk.revision - 1);
                if (!staleChunk.heightDeltas.empty()) {
                    staleChunk.heightDeltas[0] = 999.0f;
                }
                require(!reopenedWorld.applyChunkState(staleChunk), "World store accepted a stale chunk revision", failed);
            }

            WorldInfoSnapshot info;
            info.worldId = "smoke_world";
            info.formatVersion = 3;
            info.seed = 2468;
            info.chunkSize = 128.0f;
            info.horizonRadiusMeters = 4096.0f;
            info.heightAmplitude = 140.0f;
            info.heightFrequency = 0.0024f;
            info.waterLevel = -9.0f;
            info.tunnelSeeds = {
                {
                    7.0f,
                    true,
                    {
                        { 0.0f, 24.0f, 0.0f },
                        { 32.0f, 22.0f, 8.0f }
                    }
                }
            };
            info.spawnX = 5.0f;
            info.spawnY = 6.0f;
            info.spawnZ = 7.0f;
            require(reopenedWorld.applyWorldInfo(info, &worldError), "World info snapshot failed to apply: " + worldError, failed);

            const WorldGroundParams rebuiltGround = reopenedWorld.buildGroundParams(defaultTerrainParams());
            require(rebuiltGround.worldFormatVersion == 3, "World ground params did not reflect the updated format version", failed);
            require(rebuiltGround.tunnelSeedCount == 1, "World ground params did not expose the updated tunnel seed count", failed);

            const TerrainFieldContext rebuiltContext = createTerrainFieldContext(rebuiltGround.terrainParams);
            require(rebuiltContext.tunnelSeeds.size() == 1u, "Explicit tunnel seeds were not injected into the native terrain context", failed);
            if (rebuiltContext.tunnelSeeds.size() == 1u) {
                require(std::fabs(rebuiltContext.tunnelSeeds[0].radius - 7.0f) < 0.001f, "Explicit tunnel seed radius did not survive into the terrain context", failed);
                require(rebuiltContext.tunnelSeeds[0].hillAttached, "Explicit tunnel seed hillAttached flag did not survive into the terrain context", failed);
            }
        }
    }

    PlaneVisualState visual;
    visual.defaultScale = 3.0f;
    setBuiltinPlaneModel(visual);
    std::string loadStatus;
    require(loadPlaneModelFromPath(modelPath, visual, &loadStatus), "Failed to load visual for paint smoke test: " + loadStatus, failed);
    if (visual.paintSupported) {
        require(fillPaintOverlay(visual, 2), "Failed to fill test paint overlay", failed);

        const std::filesystem::path paintDirectory = tempRoot / "paint";
        std::string paintHash;
        std::string paintError;
        require(commitPaintOverlay(paintDirectory, visual, &paintHash, &paintError),
            "Failed to commit paint overlay: " + paintError,
            failed);
        const std::string primaryPaintTargetKey = visual.paintTargetKey;
        require(!primaryPaintTargetKey.empty(), "Committed paint did not record a model-specific paint key", failed);

        PlaneVisualState reloaded;
        reloaded.defaultScale = 3.0f;
        setBuiltinPlaneModel(reloaded);
        require(loadPlaneModelFromPath(modelPath, reloaded, &loadStatus), "Failed to reload model for paint smoke test: " + loadStatus, failed);

        const std::filesystem::path manualPaintPath = paintDirectory / (paintHash + ".png");
        require(std::filesystem::exists(manualPaintPath), "Committed paint PNG was not written to disk", failed);

        std::filesystem::create_directories(getPaintStorageDirectory(), ec);
        const std::filesystem::path sharedPaintPath = getPaintStoragePath(paintHash);
        std::filesystem::copy_file(manualPaintPath, sharedPaintPath, std::filesystem::copy_options::overwrite_existing, ec);
        require(loadPaintOverlayByHash(paintHash, reloaded, &paintError), "Failed to reload committed paint PNG: " + paintError, failed);
        const RgbaImage committedOverlay = reloaded.paintOverlay;

        std::filesystem::path alternateModelPath = root / "portSource/Assets/Models/DualEngine2.glb";
        if (!std::filesystem::exists(alternateModelPath) || alternateModelPath == modelPath) {
            alternateModelPath.clear();
        }
        if (alternateModelPath.empty() && modelPath.extension() != ".gltf") {
            alternateModelPath = tempRoot / ("alternate_model" + modelPath.extension().string());
            std::filesystem::copy_file(modelPath, alternateModelPath, std::filesystem::copy_options::overwrite_existing, ec);
            require(std::filesystem::exists(alternateModelPath), "Failed to stage alternate model for paint switch smoke test", failed);
        }

        if (!alternateModelPath.empty()) {
            require(loadPlaneModelFromPath(alternateModelPath, visual, &loadStatus), "Failed to load alternate model for paint switch smoke test: " + loadStatus, failed);
            require(visual.paintHash.empty(), "Switching to a different model should not keep the previous model's paint hash active", failed);
            require(!visual.hasCommittedPaint, "Switching to a different model should clear the previous model's live overlay", failed);

            require(loadPlaneModelFromPath(modelPath, visual, &loadStatus), "Failed to reload primary model for paint switch smoke test: " + loadStatus, failed);
            require(visual.paintHash == paintHash, "Switching back to the original model did not restore its committed paint hash", failed);
            require(visual.hasCommittedPaint, "Switching back to the original model did not restore its committed paint state", failed);
            require(
                visual.paintOverlay.width == committedOverlay.width &&
                    visual.paintOverlay.height == committedOverlay.height &&
                    visual.paintOverlay.pixels == committedOverlay.pixels,
                "Switching back to the original model did not restore the committed overlay pixels",
                failed);
        }

        {
            PlaneVisualState walkingVisualForSave;
            walkingVisualForSave.defaultScale = 1.0f;
            setBuiltinPlaneModel(walkingVisualForSave);

            const std::filesystem::path modelSettingsPath = tempRoot / "paint_model_settings.ini";
            std::string saveError;
            require(
                savePreferences(
                    modelSettingsPath,
                    uiState,
                    graphicsSettings,
                    lightingSettings,
                    hudSettings,
                    controls,
                    planeProfile,
                    terrainParams,
                    visual,
                    walkingVisualForSave,
                    &saveError),
                "Failed to save model-specific paint preferences: " + saveError,
                failed);

            std::ifstream savedModelFile(modelSettingsPath, std::ios::binary);
            const std::string savedModelContents((std::istreambuf_iterator<char>(savedModelFile)), std::istreambuf_iterator<char>());
            require(
                savedModelContents.find("paint.plane.model." + primaryPaintTargetKey + "=" + paintHash) != std::string::npos,
                "Saved preferences did not persist the model-specific paint mapping",
                failed);

            UiState loadedUiState = defaultUiState();
            GraphicsSettings loadedGraphicsSettings = defaultGraphicsSettings();
            LightingSettings loadedLightingSettings = defaultLightingSettings();
            HudSettings loadedHudSettings = defaultHudSettings();
            ControlProfile loadedControls = defaultControlProfile();
            AircraftProfile loadedPlaneProfile {};
            loadedPlaneProfile.visualPrefs.scale = 3.0f;
            TerrainParams loadedTerrainParams = defaultTerrainParams();
            VisualPreferenceData loadedWalkingPrefs {};
            loadedWalkingPrefs.scale = 1.0f;
            std::string loadPreferenceError;
            require(
                loadPreferences(
                    modelSettingsPath,
                    loadedUiState,
                    loadedGraphicsSettings,
                    loadedLightingSettings,
                    loadedHudSettings,
                    loadedControls,
                    loadedPlaneProfile,
                    loadedTerrainParams,
                    loadedWalkingPrefs,
                    &loadPreferenceError),
                "Failed to reload model-specific paint preferences: " + loadPreferenceError,
                failed);
            require(
                loadedPlaneProfile.visualPrefs.paintHashesByModelKey[primaryPaintTargetKey] == paintHash,
                "Preference load did not restore the saved model-specific paint mapping",
                failed);

            PlaneVisualState restoredVisual;
            restoredVisual.defaultScale = 3.0f;
            setBuiltinPlaneModel(restoredVisual);
            restoreVisualFromPreferences(restoredVisual, loadedPlaneProfile.visualPrefs, {}, "plane");
            require(restoredVisual.paintHash == paintHash, "Restored visual did not reactivate the model-specific paint hash", failed);
            require(restoredVisual.hasCommittedPaint, "Restored visual did not reactivate the model-specific paint overlay", failed);
            require(
                restoredVisual.paintOverlay.width == committedOverlay.width &&
                    restoredVisual.paintOverlay.height == committedOverlay.height &&
                    restoredVisual.paintOverlay.pixels == committedOverlay.pixels,
                "Restored visual did not rebuild the expected committed paint overlay",
                failed);
        }

        setBuiltinPlaneModel(reloaded);
        require(reloaded.paintHash.empty(), "Switching models did not clear incompatible stored paint hash", failed);
        require(!reloaded.hasCommittedPaint, "Switching models did not clear incompatible paint state", failed);
        std::filesystem::remove(sharedPaintPath, ec);
    } else {
        std::cout << "SKIP: Selected model is not paintable, paint round-trip not exercised.\n";
    }

    if (!failed) {
        std::cout << "Native smoke checks passed.\n";
    }

    SDL_Quit();
    return failed ? 1 : 0;
}
