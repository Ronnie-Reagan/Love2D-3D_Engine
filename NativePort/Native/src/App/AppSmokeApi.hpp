#pragma once

#include "App/AppRunner.hpp"
#include "App/AppState.hpp"
#include "App/InputBindings.hpp"
#include "App/MenuController.hpp"
#include "App/Preferences.hpp"
#include "App/VisualAssets.hpp"
#include "App/WorldCatalog.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace TrueFlightApp {
std::uint64_t parseConnectLobbyLaunchArgument(int argc, char** argv);

bool sphereWithinView(const Camera& camera, const Vec3& center, float radius, float maxDistance);
Quat composeWalkingRotation(float yaw, float pitch);
void syncWalkingLookFromRotation(const Quat& rotation, float& walkYaw, float& walkPitch);
float sampleGroundHeight(float x, float z, const TerrainParams& terrainParams);
float sampleGroundHeight(float x, float z, const TerrainFieldContext& terrainContext);
float computeHeightAboveGround(const FlightState& plane, const TerrainFieldContext& terrainContext);
float computeWaterProximity(const FlightState& plane, const TerrainFieldContext& terrainContext);
void stepWalking(
    FlightState& actor,
    float dt,
    const WalkingInputState& input,
    const TerrainFieldContext& terrainContext,
    float baseMoveSpeed,
    const TerrainVisualCache* terrainCache,
    float* brushAmountOut);
TerrainCrater buildCrashCrater(float impactX, float impactY, float impactZ, float impactSpeed);
ProceduralAudioFrame buildProceduralAudioFrame(
    const FlightState& plane,
    const FlightRuntimeState& runtime,
    const FlightEnvironment& environment,
    const TerrainFieldContext& terrainContext,
    const FlightConfig& flightConfig,
    const UiState& uiState,
    const PropAudioConfig& propAudioConfig,
    float foliageBrush,
    float foliageImpact,
    bool active,
    bool paused,
    float dt,
    bool afterburnerActive = false);
WalkingRigPoseSample sampleWalkingRigPose(const FlightState& actor, float worldTimeSeconds);
Model buildProceduralWalkingRigModel(const WalkingRigPoseSample& pose);
float computeLod0TerrainTileSize(const TerrainParams& params);
float computeLod1TerrainTileSize(const TerrainParams& params);
float computeLod2TerrainTileSize(const TerrainParams& params);
float computeNearHalfExtent(const TerrainParams& params, float lod0TileSize);
std::uint64_t terrainParamsSignature(const TerrainParams& params);
float sampleTerrainSlope01(float x, float z, const TerrainFieldContext& terrainContext);
float computeBrushContactAmount(
    const TerrainVisualCache& terrainCache,
    const Vec3& position,
    float actorRadius,
    float actorMinY,
    float actorMaxY);
bool detectFlightPropCollision(
    const TerrainVisualCache& terrainCache,
    const TerrainDecorationSettings& decoration,
    const FlightState& plane,
    int tick,
    FlightCrashEvent& outCrash);
void resolveWalkingPropCollisions(
    const TerrainVisualCache& terrainCache,
    const TerrainDecorationSettings& decoration,
    FlightState& actor);

GameplayObjectState makeGameplayObjectState(
    GameplayObjectKind kind,
    int id,
    int ownerId,
    const Vec3& pos,
    const Vec3& vel,
    float radius,
    float ttl,
    float damage,
    float gravityScale,
    float blastRadius,
    float craterRadius,
    float craterDepth);
CombatAudioTelemetry sampleCombatAudioTelemetry(const GameSession& session);

bool terrainPatchNeedsWorldVolumetrics(const TerrainPatchBounds& bounds, const TerrainFieldContext& terrainContext);
TerrainTileDecorationResult buildTerrainTileDecoration(
    const TerrainChunkKey& key,
    const TerrainPatchBounds& bounds,
    TerrainFarTileBand band,
    const TerrainFieldContext& terrainContext,
    const TerrainChunkData* sourceData = nullptr);
TerrainFarTileDetail initialTerrainTileDetailForBand(TerrainFarTileBand band);
std::optional<TerrainFarTileDetail> nextTerrainTileDetail(TerrainFarTileBand band, TerrainFarTileDetail detail);
std::string terrainTileIdentityKey(TerrainFarTileBand band, int tileX, int tileZ);
std::string terrainTileRequestKey(const TerrainTileRequest& request);
void trimTerrainStreamBacklogLocked(TerrainVisualStreamState& state);
bool shouldApplyTerrainChunkResult(const TerrainFarTile* existingTile, const TerrainTileRequest& request);

RuntimePressureState computeRuntimePressureState(const SystemPressureSnapshot& snapshot, RuntimePressureState previousState);
void updatePerformanceGovernor(
    PerformanceGovernor& governor,
    float nowSeconds,
    float dt,
    const SystemPressureSnapshot& snapshot);
void bindTerrainContextWorldStore(
    TerrainFieldContext& terrainContext,
    WorldStore* worldStore,
    std::shared_ptr<std::shared_mutex> worldStoreMutex = {});

VisualRigCutout defaultVisualRigCutout(int slotIndex);

RendererLightingState evaluateRendererLightingState(const LightingSettings& lightingSettings, bool horizonFogEnabled);
Camera buildRenderCamera(
    const FlightState& plane,
    const TerrainFieldContext& terrainContext,
    const UiState& uiState,
    bool flightMode,
    float flightLookYaw,
    float flightLookPitch,
    float farClipMeters);
float computeWorldFarClip(const GraphicsSettings& graphicsSettings);

}  // namespace TrueFlightApp
