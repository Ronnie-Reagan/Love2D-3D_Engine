#pragma once

#include "NativeGame/Flight.hpp"
#include "NativeGame/Math.hpp"
#include "NativeGame/StlLoader.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_map>
#include <vector>

namespace NativeGame {

struct GeoConfig {
    float originLat = 0.0f;
    float originLon = 0.0f;
    float metersPerUnit = 1.0f;
};

inline float wrapAngle(float angle)
{
    const float twoPi = kPi * 2.0f;
    angle = std::fmod(angle, twoPi);
    if (angle > kPi) {
        angle -= twoPi;
    } else if (angle < -kPi) {
        angle += twoPi;
    }
    return angle;
}

inline float shortestAngleDelta(float currentAngle, float targetAngle)
{
    return wrapAngle(targetAngle - currentAngle);
}

inline float getStableYawFromRotation(const Quat& rotation, float fallbackYaw = 0.0f)
{
    const Vec3 forward = rotateVector(rotation, { 0.0f, 0.0f, 1.0f });
    const float flatLenSq = (forward.x * forward.x) + (forward.z * forward.z);
    if (flatLenSq <= 1.0e-6f) {
        return wrapAngle(fallbackYaw);
    }
    return std::atan2(forward.x, forward.z);
}

inline Vec2 worldToMapDelta(float dx, float dz, float yaw, bool northUp = false)
{
    if (northUp) {
        return { dx, dz };
    }

    const float cosYaw = std::cos(yaw);
    const float sinYaw = std::sin(yaw);
    return {
        (cosYaw * dx) - (sinYaw * dz),
        (sinYaw * dx) + (cosYaw * dz)
    };
}

inline Vec2 mapToWorldDelta(float mapX, float mapZ, float yaw, bool northUp = false)
{
    if (northUp) {
        return { mapX, mapZ };
    }

    const float cosYaw = std::cos(yaw);
    const float sinYaw = std::sin(yaw);
    return {
        (cosYaw * mapX) + (sinYaw * mapZ),
        (-sinYaw * mapX) + (cosYaw * mapZ)
    };
}

inline Vec2 worldToGeo(float worldX, float worldZ, const GeoConfig& config)
{
    const float metersPerUnit = std::max(1.0e-6f, config.metersPerUnit);
    const float northMeters = worldZ * metersPerUnit;
    const float eastMeters = worldX * metersPerUnit;
    constexpr float metersPerDegLat = 111132.0f;
    const float lat = config.originLat + (northMeters / metersPerDegLat);
    const float latRad = radians(config.originLat);
    const float metersPerDegLon = std::max(1.0f, std::cos(latRad) * 111320.0f);
    const float lon = config.originLon + (eastMeters / metersPerDegLon);
    return { lat, lon };
}

inline float frac(float value)
{
    return value - std::floor(value);
}

inline float hash01(int ix, int iy, int iz, int seed)
{
    const float n = std::sin((static_cast<float>(ix) * 127.1f) + (static_cast<float>(iy) * 311.7f) + (static_cast<float>(iz) * 73.13f) + (static_cast<float>(seed) * 19.97f)) * 43758.5453123f;
    return frac(n);
}

inline float smoothstep(float t)
{
    return t * t * (3.0f - (2.0f * t));
}

inline float valueNoise2(float x, float z, int seed)
{
    const int ix = static_cast<int>(std::floor(x));
    const int iz = static_cast<int>(std::floor(z));
    const float fx = x - static_cast<float>(ix);
    const float fz = z - static_cast<float>(iz);

    const float v00 = hash01(ix, 0, iz, seed);
    const float v10 = hash01(ix + 1, 0, iz, seed);
    const float v01 = hash01(ix, 0, iz + 1, seed);
    const float v11 = hash01(ix + 1, 0, iz + 1, seed);

    const float sx = smoothstep(fx);
    const float sz = smoothstep(fz);
    const float a = mix(v00, v10, sx);
    const float b = mix(v01, v11, sx);
    return mix(a, b, sz);
}

inline float valueNoise3(float x, float y, float z, int seed)
{
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const int iz = static_cast<int>(std::floor(z));
    const float fx = x - static_cast<float>(ix);
    const float fy = y - static_cast<float>(iy);
    const float fz = z - static_cast<float>(iz);

    const auto corner = [=](int dx, int dy, int dz) {
        return hash01(ix + dx, iy + dy, iz + dz, seed);
    };

    const float sx = smoothstep(fx);
    const float sy = smoothstep(fy);
    const float sz = smoothstep(fz);

    const float c000 = corner(0, 0, 0);
    const float c100 = corner(1, 0, 0);
    const float c010 = corner(0, 1, 0);
    const float c110 = corner(1, 1, 0);
    const float c001 = corner(0, 0, 1);
    const float c101 = corner(1, 0, 1);
    const float c011 = corner(0, 1, 1);
    const float c111 = corner(1, 1, 1);

    const float x00 = mix(c000, c100, sx);
    const float x10 = mix(c010, c110, sx);
    const float x01 = mix(c001, c101, sx);
    const float x11 = mix(c011, c111, sx);
    const float y0 = mix(x00, x10, sy);
    const float y1 = mix(x01, x11, sy);
    return mix(y0, y1, sz);
}

inline float fbm2(float x, float z, int octaves, float lacunarity, float gain, int seed)
{
    float amp = 1.0f;
    float freq = 1.0f;
    float sum = 0.0f;
    float weight = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += valueNoise2(x * freq, z * freq, seed + ((i + 1) * 101)) * amp;
        weight += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return weight <= 1.0e-6f ? 0.0f : (sum / weight);
}

inline float fbm3(float x, float y, float z, int octaves, float lacunarity, float gain, int seed)
{
    float amp = 1.0f;
    float freq = 1.0f;
    float sum = 0.0f;
    float weight = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += valueNoise3(x * freq, y * freq, z * freq, seed + ((i + 1) * 157)) * amp;
        weight += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return weight <= 1.0e-6f ? 0.0f : (sum / weight);
}

struct TerrainCrater {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float radius = 8.0f;
    float depth = 3.6f;
    float rim = 0.12f;
};

struct TerrainTunnelSeed {
    float radius = 10.0f;
    bool hillAttached = false;
    std::vector<Vec3> points;
};

enum class TerrainPropClass : std::uint8_t {
    Brush = 0,
    Blocker = 1
};

enum class TerrainPropVariant : std::uint8_t {
    Conifer = 0,
    Broadleaf = 1,
    Shrub = 2,
    Rock = 3
};

struct TerrainDecorationSettings {
    bool enabled = true;
    float density = 1.0f;
    float nearDensityScale = 1.0f;
    float midDensityScale = 0.58f;
    float farDensityScale = 0.18f;
    float treeLineOffset = 0.0f;
    float shoreBrushDensity = 1.0f;
    float rockDensity = 1.0f;
    bool collisionEnabled = true;
    int seedOffset = 7919;
};

struct TerrainPropPlacement {
    TerrainPropClass propClass = TerrainPropClass::Brush;
    TerrainPropVariant variant = TerrainPropVariant::Shrub;
    Vec3 position {};
    Vec3 scale { 1.0f, 1.0f, 1.0f };
    Vec3 tint { 1.0f, 1.0f, 1.0f };
    float yawRadians = 0.0f;
    float leanRadians = 0.0f;
};

struct TerrainPropCollider {
    TerrainPropClass propClass = TerrainPropClass::Blocker;
    Vec3 center {};
    float radius = 0.0f;
    float halfHeight = 0.0f;
    float softness = 0.0f;
};

struct TerrainMountainFrontSettings {
    bool enabled = true;
    float coastalBandStart = 1400.0f;
    float coastalBandEnd = 9800.0f;
    float foothillStart = 420.0f;
    float foothillEnd = 3400.0f;
    float inlandFadeStart = 14500.0f;
    float inlandFadeEnd = 24500.0f;
    float mountainWallStrength = 0.52f;
    float shelfStrength = 0.12f;
    float cliffStrength = 0.18f;
    float ridgeWarpScale = 0.000032f;
};

struct TerrainRoadSettings {
    bool enabled = true;
    int desiredRoadCount = 4;
    float laneWidthMeters = 4.0f;
    float shoulderWidthMeters = 1.4f;
    float cutWidthMeters = 13.0f;
    float cutBankWidthMeters = 22.0f;
    float maxGrade = 0.06f;
    float maxGradeHard = 0.09f;
    float minTurnRadiusMeters = 160.0f;
    float preferredTurnRadiusMeters = 280.0f;
    float stepMeters = 36.0f;
    float sampleLookAheadMeters = 260.0f;
    float sampleConeDegrees = 68.0f;
    float branchChance = 0.30f;
    float flattenStrength = 0.94f;
    float cutStrength = 0.86f;
    float fillStrength = 0.72f;
    float edgeFeatherMeters = 13.0f;
    float potholeChance = 0.008f;
    float potholeDepthMeters = 0.05f;
    float patchChance = 0.04f;
    float patchRaiseMeters = 0.018f;
    float asphaltNoiseScale = 0.07f;
    float crackStrength = 0.18f;
};

struct TerrainRoadNode {
    Vec3 position {};
    Vec3 forward { 1.0f, 0.0f, 0.0f };
    float widthMeters = 9.8f;
    float cutWidthMeters = 16.0f;
    float grade = 0.0f;
    float curvature = 0.0f;
};

struct TerrainRoadPath {
    bool loop = false;
    std::vector<TerrainRoadNode> nodes;
};

struct TerrainRoadSample {
    float distanceToCenter = 1.0e9f;
    float distanceToEdge = 1.0e9f;
    float roadMask = 0.0f;
    float shoulderMask = 0.0f;
    float cutMask = 0.0f;
    float along = 0.0f;
    float roadHeight = 0.0f;
    float patchMask = 0.0f;
    float potholeMask = 0.0f;
    Vec3 centerPosition {};
    Vec3 forward { 0.0f, 0.0f, 1.0f };
    float widthMeters = 0.0f;
    float grade = 0.0f;
    float curvature = 0.0f;
    float segmentT = 0.0f;
    int pathIndex = -1;
    int nodeIndex = -1;
};

struct TerrainRoadSegmentLocator {
    int pathIndex = 0;
    int nodeIndex = 0;
};

struct TerrainRoadNetworkIndex {
    bool valid = false;
    float cellSize = 256.0f;
    float minX = 0.0f;
    float maxX = 0.0f;
    float minZ = 0.0f;
    float maxZ = 0.0f;
    std::unordered_map<std::int64_t, std::vector<TerrainRoadSegmentLocator>> cells;
};

struct TerrainParams {
    int seed = 1337;
    float chunkSize = 128.0f;
    float worldRadius = 24576.0f;
    float minY = -1400.0f;
    float maxY = 10000.0f;
    int lod0Radius = 4;
    int lod1Radius = 12;
    int lod2Radius = 48;
    float terrainQuality = 2.8f;
    bool autoQualityEnabled = false;
    float targetFrameMs = 8.3f;
    int lod0ChunkScale = 2;
    int lod1ChunkScale = 8;
    int lod2ChunkScale = 32;
    bool textureTilesEnabled = true;
    int lod0TextureResolution = 512;
    int lod1TextureResolution = 256;
    int lod2TextureResolution = 64;
    float gameplayRadiusMeters = 1024.0f;
    float midFieldRadiusMeters = 6144.0f;
    float horizonRadiusMeters = 24576.0f;
    float lod0BaseCellSize = 3.5f;
    float lod1BaseCellSize = 12.0f;
    float lod2BaseCellSize = 36.0f;
    float lod0CellSize = 3.5f;
    float lod1CellSize = 12.0f;
    float lod2CellSize = 36.0f;
    int meshBuildBudget = 4;
    int workerMaxInflight = 6;
    int workerResultBudgetPerFrame = 4;
    float workerResultTimeBudgetMs = 3.0f;
    int maxAdaptiveLod1Radius = 8;
    int maxPendingChunks = 96;
    int maxStaleChunks = 32;
    int maxDisplayedChunks = 512;
    int maxDisplayedChunksHardCap = 8192;
    bool drawDistanceOverridesLodRadius = true;
    bool splitLodEnabled = false;
    float highResSplitRatio = 0.5f;
    int chunkCacheLimit = 128;
    bool farLodConeEnabled = true;
    float farLodConeDegrees = 110.0f;
    int rearLod2Radius = 4;
    float baseHeight = -32.0f;
    float heightAmplitude = 1850.0f;
    float heightFrequency = 0.00012f;
    int heightOctaves = 6;
    float heightLacunarity = 2.1f;
    float heightGain = 0.5f;
    float surfaceDetailAmplitude = 58.0f;
    float surfaceDetailFrequency = 0.0024f;
    float ridgeAmplitude = 1050.0f;
    float ridgeFrequency = 0.00042f;
    float ridgeSharpness = 2.8f;
    float macroWarpAmplitude = 920.0f;
    float macroWarpFrequency = 0.000038f;
    float terraceStrength = 0.03f;
    float terraceStep = 110.0f;
    float waterLevel = 0.0f;
    float shorelineBand = 22.0f;
    float waterWaveAmplitude = 1.6f;
    float waterWaveFrequency = 0.014f;
    float biomeFrequency = 0.0009f;
    float snowLine = 1750.0f;
    bool caveEnabled = false;
    float caveFrequency = 0.018f;
    float caveThreshold = 0.68f;
    float caveStrength = 42.0f;
    int caveOctaves = 3;
    float caveLacunarity = 2.1f;
    float caveGain = 0.5f;
    float caveMinY = -120.0f;
    float caveMaxY = 220.0f;
    int tunnelCount = 0;
    float tunnelRadiusMin = 9.0f;
    float tunnelRadiusMax = 18.0f;
    float tunnelLengthMin = 240.0f;
    float tunnelLengthMax = 520.0f;
    float tunnelSegmentLength = 18.0f;
    int generatorVersion = 3;
    bool surfaceOnlyMeshing = true;
    bool threadedMeshing = true;
    bool enableSkirts = true;
    float skirtDepth = 24.0f;
    int maxChunkCellsPerAxis = 56;
    int craterHistoryLimit = 64;
    float waterRatio = 0.18f;
    TerrainDecorationSettings decoration {};
    Vec3 grassColor { 0.20f, 0.62f, 0.22f };
    Vec3 roadColor { 0.10f, 0.10f, 0.10f };
    Vec3 fieldColor { 0.35f, 0.45f, 0.20f };
    Vec3 waterColor { 0.10f, 0.10f, 0.50f };
    Vec3 grassVar { 0.05f, 0.10f, 0.05f };
    Vec3 roadVar { 0.02f, 0.02f, 0.02f };
    Vec3 fieldVar { 0.04f, 0.06f, 0.04f };
    Vec3 waterVar { 0.02f, 0.02f, 0.02f };
    std::vector<TerrainCrater> dynamicCraters;
    std::vector<TerrainTunnelSeed> explicitTunnelSeeds;
    TerrainMountainFrontSettings mountainFront {};
    TerrainRoadSettings roads {};
    std::vector<TerrainRoadPath> explicitRoadPaths;
};

struct TerrainVerticalBoundsSample {
    bool valid = false;
    float minY = 0.0f;
    float maxY = 0.0f;
};

struct TerrainFieldContext {
    TerrainParams params;
    std::vector<TerrainTunnelSeed> tunnelSeeds;
    std::vector<TerrainRoadPath> roadPaths;
    std::shared_ptr<const TerrainRoadNetworkIndex> roadIndex {};
    std::function<float(float, float)> sampleHeightDeltaAt {};
    std::function<float(float, float, float)> sampleVolumetricAdditiveSdfAt {};
    std::function<float(float, float, float)> sampleVolumetricSubtractiveSdfAt {};
    std::function<bool(float, float, float, float)> hasVolumetricOverridesInBounds {};
    std::function<TerrainVerticalBoundsSample(float, float, float, float)> sampleWorldVolumetricBoundsInBounds {};
    std::function<std::uint64_t(float, float, float, float)> sampleChunkRevisionSignature {};
    std::function<TerrainRoadSample(float, float)> sampleRoadAt {};
};

struct TerrainMaterialSample {
    float surfaceHeight = 0.0f;
    float waterHeight = 0.0f;
    float wetness = 0.0f;
    float snowWeight = 0.0f;
    float rockWeight = 0.0f;
    float biomeBlend = 0.0f;
    float hardnessWeight = 0.0f;
    float resourceWeight = 0.0f;
    float erosionWeight = 0.0f;
    float flowWeight = 0.0f;
    float roadWeight = 0.0f;
    float shoulderWeight = 0.0f;
    float asphaltPatchWeight = 0.0f;
};

struct TerrainHydraulicSample {
    float moisture = 0.0f;
    float hardness = 0.0f;
    float resource = 0.0f;
    float sediment = 0.0f;
    float flow = 0.0f;
    float erosion = 0.0f;
};

struct TerrainPatchBounds {
    float x0 = -32.0f;
    float x1 = 32.0f;
    float z0 = -32.0f;
    float z1 = 32.0f;
    bool hasHole = false;
    float holeX0 = 0.0f;
    float holeX1 = 0.0f;
    float holeZ0 = 0.0f;
    float holeZ1 = 0.0f;
};

struct TerrainVolumeBounds {
    float x0 = -32.0f;
    float x1 = 32.0f;
    float y0 = -32.0f;
    float y1 = 32.0f;
    float z0 = -32.0f;
    float z1 = 32.0f;
};

struct TerrainVisualBuildResult {
    Model nearModel;
    Model farModel;
    float nearHalfExtent = 0.0f;
    float farHalfExtent = 0.0f;
    float anchorSpacing = 64.0f;
};

inline Vec3 clampColor3(const Vec3& value, const Vec3& fallback)
{
    return {
        clamp(sanitize(value.x, fallback.x), 0.0f, 1.0f),
        clamp(sanitize(value.y, fallback.y), 0.0f, 1.0f),
        clamp(sanitize(value.z, fallback.z), 0.0f, 1.0f)
    };
}

inline TerrainCrater sanitizeCrater(const TerrainCrater& crater)
{
    TerrainCrater out = crater;
    out.radius = std::max(1.0f, sanitize(out.radius, 8.0f));
    out.depth = std::max(0.4f, sanitize(out.depth, out.radius * 0.45f));
    out.rim = clamp(sanitize(out.rim, 0.12f), 0.0f, 0.75f);
    out.x = sanitize(out.x, 0.0f);
    out.y = sanitize(out.y, 0.0f);
    out.z = sanitize(out.z, 0.0f);
    return out;
}

inline TerrainTunnelSeed sanitizeTunnelSeed(const TerrainTunnelSeed& seed)
{
    TerrainTunnelSeed out;
    out.radius = std::max(0.1f, sanitize(seed.radius, 10.0f));
    out.hillAttached = seed.hillAttached;
    out.points.reserve(seed.points.size());
    for (const Vec3& point : seed.points) {
        out.points.push_back({
            sanitize(point.x, 0.0f),
            sanitize(point.y, 0.0f),
            sanitize(point.z, 0.0f)
        });
    }
    return out;
}

inline TerrainParams normalizeTerrainParams(TerrainParams params)
{
    params.seed = std::max(1, params.seed);
    params.chunkSize = std::max(8.0f, sanitize(params.chunkSize, 128.0f));
    params.worldRadius = std::max(params.chunkSize * 4.0f, sanitize(params.worldRadius, 32768.0f));
    params.minY = sanitize(params.minY, -1400.0f);
    params.maxY = std::max(params.minY + 16.0f, sanitize(params.maxY, 10000.0f));

    params.lod0Radius = std::clamp(params.lod0Radius, 1, 12);
    params.lod1Radius = std::clamp(params.lod1Radius, params.lod0Radius, 32);
    params.lod2Radius = std::clamp(params.lod2Radius, params.lod1Radius, 96);
    params.terrainQuality = clamp(sanitize(params.terrainQuality, 2.8f), 0.75f, 6.0f);
    params.targetFrameMs = clamp(sanitize(params.targetFrameMs, 8.3f), 4.0f, 50.0f);

    params.lod0ChunkScale = std::max(1, params.lod0ChunkScale);
    params.lod1ChunkScale = std::max(params.lod0ChunkScale, params.lod1ChunkScale);
    params.lod2ChunkScale = std::max(params.lod1ChunkScale, params.lod2ChunkScale);

    params.lod0TextureResolution = std::clamp(params.lod0TextureResolution, 64, 512);
    params.lod1TextureResolution = std::clamp(params.lod1TextureResolution, 32, 512);
    params.lod2TextureResolution = std::clamp(params.lod2TextureResolution, 16, 256);

    params.gameplayRadiusMeters = std::max(
        params.chunkSize,
        sanitize(params.gameplayRadiusMeters, std::max(1024.0f, params.chunkSize * static_cast<float>(std::max(params.lod0Radius, 4)))));
    params.midFieldRadiusMeters = std::max(
        params.gameplayRadiusMeters + params.chunkSize,
        sanitize(params.midFieldRadiusMeters, std::max(6144.0f, params.chunkSize * static_cast<float>(std::max(params.lod1Radius, 12)))));
    params.horizonRadiusMeters = std::max(
        params.midFieldRadiusMeters + params.chunkSize,
        sanitize(params.horizonRadiusMeters, std::max(24576.0f, params.chunkSize * static_cast<float>(std::max(params.lod2Radius, 48)))));

    params.lod0BaseCellSize = std::max(1.0f, sanitize(params.lod0BaseCellSize, params.lod0CellSize));
    params.lod1BaseCellSize = std::max(params.lod0BaseCellSize, sanitize(params.lod1BaseCellSize, params.lod1CellSize));
    params.lod2BaseCellSize = std::max(params.lod1BaseCellSize, sanitize(params.lod2BaseCellSize, params.lod2CellSize));
    const float qualityScale = std::sqrt(params.terrainQuality);
    params.lod0CellSize = clamp(params.lod0BaseCellSize / qualityScale, 1.0f, 6.0f);
    params.lod1CellSize = clamp(params.lod1BaseCellSize / qualityScale, params.lod0CellSize, 12.0f);
    params.lod2CellSize = clamp(params.lod2BaseCellSize / qualityScale, params.lod1CellSize, 24.0f);

    params.meshBuildBudget = std::clamp(params.meshBuildBudget, 1, 8);
    params.workerMaxInflight = std::clamp(params.workerMaxInflight, 1, 6);
    params.workerResultBudgetPerFrame = std::clamp(params.workerResultBudgetPerFrame, 1, 12);
    params.workerResultTimeBudgetMs = clamp(sanitize(params.workerResultTimeBudgetMs, 3.0f), 0.25f, 20.0f);
    params.maxAdaptiveLod1Radius = std::clamp(params.maxAdaptiveLod1Radius, params.lod1Radius, 32);
    params.maxPendingChunks = std::clamp(params.maxPendingChunks, 16, 192);
    params.maxStaleChunks = std::clamp(params.maxStaleChunks, 8, 128);
    params.maxDisplayedChunksHardCap = std::clamp(params.maxDisplayedChunksHardCap, 1536, 16384);
    params.maxDisplayedChunks = std::clamp(params.maxDisplayedChunks, 128, params.maxDisplayedChunksHardCap);
    params.highResSplitRatio = clamp(sanitize(params.highResSplitRatio, 0.5f), 0.2f, 0.8f);
    params.chunkCacheLimit = std::clamp(params.chunkCacheLimit, 32, 256);
    params.farLodConeDegrees = clamp(sanitize(params.farLodConeDegrees, 110.0f), 70.0f, 170.0f);
    params.rearLod2Radius = std::clamp(params.rearLod2Radius, params.lod1Radius, params.lod2Radius);

    params.baseHeight = sanitize(params.baseHeight, -32.0f);
    params.heightAmplitude = std::max(0.0f, sanitize(params.heightAmplitude, 1850.0f));
    params.heightFrequency = std::max(1.0e-5f, sanitize(params.heightFrequency, 0.00012f));
    params.heightOctaves = std::max(1, params.heightOctaves);
    params.heightLacunarity = std::max(1.1f, sanitize(params.heightLacunarity, 2.1f));
    params.heightGain = clamp(sanitize(params.heightGain, 0.5f), 0.1f, 0.95f);
    params.surfaceDetailAmplitude = std::max(0.0f, sanitize(params.surfaceDetailAmplitude, 58.0f));
    params.surfaceDetailFrequency = std::max(1.0e-4f, sanitize(params.surfaceDetailFrequency, 0.0024f));
    params.ridgeAmplitude = std::max(0.0f, sanitize(params.ridgeAmplitude, 1050.0f));
    params.ridgeFrequency = std::max(1.0e-5f, sanitize(params.ridgeFrequency, 0.00042f));
    params.ridgeSharpness = std::max(0.3f, sanitize(params.ridgeSharpness, 2.8f));
    params.macroWarpAmplitude = std::max(0.0f, sanitize(params.macroWarpAmplitude, 920.0f));
    params.macroWarpFrequency = std::max(1.0e-5f, sanitize(params.macroWarpFrequency, 0.000038f));
    params.terraceStrength = clamp(sanitize(params.terraceStrength, 0.03f), 0.0f, 1.0f);
    params.terraceStep = std::max(1.0f, sanitize(params.terraceStep, 110.0f));
    params.waterLevel = sanitize(params.waterLevel, 0.0f);
    params.shorelineBand = std::max(0.1f, sanitize(params.shorelineBand, 22.0f));
    params.waterWaveAmplitude = std::max(0.0f, sanitize(params.waterWaveAmplitude, 1.6f));
    params.waterWaveFrequency = std::max(1.0e-4f, sanitize(params.waterWaveFrequency, 0.014f));
    params.biomeFrequency = std::max(1.0e-5f, sanitize(params.biomeFrequency, 0.0009f));
    params.snowLine = sanitize(params.snowLine, 1750.0f);

    params.mountainFront.coastalBandStart = std::max(0.0f, sanitize(params.mountainFront.coastalBandStart, 1400.0f));
    params.mountainFront.coastalBandEnd = std::max(params.mountainFront.coastalBandStart + 100.0f, sanitize(params.mountainFront.coastalBandEnd, 9800.0f));
    params.mountainFront.foothillStart = std::max(0.0f, sanitize(params.mountainFront.foothillStart, 420.0f));
    params.mountainFront.foothillEnd = std::max(params.mountainFront.foothillStart + 100.0f, sanitize(params.mountainFront.foothillEnd, 3400.0f));
    params.mountainFront.inlandFadeStart = std::max(params.mountainFront.coastalBandEnd, sanitize(params.mountainFront.inlandFadeStart, 14500.0f));
    params.mountainFront.inlandFadeEnd = std::max(params.mountainFront.inlandFadeStart + 100.0f, sanitize(params.mountainFront.inlandFadeEnd, 24500.0f));
    params.mountainFront.mountainWallStrength = clamp(sanitize(params.mountainFront.mountainWallStrength, 0.52f), 0.0f, 3.0f);
    params.mountainFront.shelfStrength = clamp(sanitize(params.mountainFront.shelfStrength, 0.12f), 0.0f, 1.0f);
    params.mountainFront.cliffStrength = clamp(sanitize(params.mountainFront.cliffStrength, 0.18f), 0.0f, 1.0f);
    params.mountainFront.ridgeWarpScale = std::max(1.0e-6f, sanitize(params.mountainFront.ridgeWarpScale, 0.000032f));

    params.roads.desiredRoadCount = std::clamp(params.roads.desiredRoadCount, 0, 16);
    params.roads.laneWidthMeters = clamp(sanitize(params.roads.laneWidthMeters, 4.0f), 2.7f, 5.5f);
    params.roads.shoulderWidthMeters = clamp(sanitize(params.roads.shoulderWidthMeters, 1.4f), 0.2f, 4.0f);
    params.roads.cutWidthMeters = std::max(params.roads.laneWidthMeters * 2.0f, sanitize(params.roads.cutWidthMeters, 13.0f));
    params.roads.cutBankWidthMeters = std::max(params.roads.cutWidthMeters, sanitize(params.roads.cutBankWidthMeters, 22.0f));
    params.roads.maxGrade = clamp(sanitize(params.roads.maxGrade, 0.06f), 0.02f, 0.14f);
    params.roads.maxGradeHard = clamp(sanitize(params.roads.maxGradeHard, 0.09f), params.roads.maxGrade, 0.22f);
    params.roads.minTurnRadiusMeters = std::max(30.0f, sanitize(params.roads.minTurnRadiusMeters, 160.0f));
    params.roads.preferredTurnRadiusMeters = std::max(params.roads.minTurnRadiusMeters, sanitize(params.roads.preferredTurnRadiusMeters, 280.0f));
    params.roads.stepMeters = clamp(sanitize(params.roads.stepMeters, 36.0f), 8.0f, 96.0f);
    params.roads.sampleLookAheadMeters = clamp(sanitize(params.roads.sampleLookAheadMeters, 260.0f), 60.0f, 420.0f);
    params.roads.sampleConeDegrees = clamp(sanitize(params.roads.sampleConeDegrees, 68.0f), 20.0f, 150.0f);
    params.roads.branchChance = clamp(sanitize(params.roads.branchChance, 0.30f), 0.0f, 0.95f);
    params.roads.flattenStrength = clamp(sanitize(params.roads.flattenStrength, 0.94f), 0.0f, 1.0f);
    params.roads.cutStrength = clamp(sanitize(params.roads.cutStrength, 0.86f), 0.0f, 1.0f);
    params.roads.fillStrength = clamp(sanitize(params.roads.fillStrength, 0.72f), 0.0f, 1.0f);
    params.roads.edgeFeatherMeters = clamp(sanitize(params.roads.edgeFeatherMeters, 13.0f), 0.5f, 30.0f);
    params.roads.potholeChance = clamp(sanitize(params.roads.potholeChance, 0.008f), 0.0f, 0.25f);
    params.roads.potholeDepthMeters = clamp(sanitize(params.roads.potholeDepthMeters, 0.05f), 0.0f, 0.20f);
    params.roads.patchChance = clamp(sanitize(params.roads.patchChance, 0.04f), 0.0f, 0.25f);
    params.roads.patchRaiseMeters = clamp(sanitize(params.roads.patchRaiseMeters, 0.018f), 0.0f, 0.08f);
    params.roads.asphaltNoiseScale = std::max(1.0e-4f, sanitize(params.roads.asphaltNoiseScale, 0.07f));
    params.roads.crackStrength = clamp(sanitize(params.roads.crackStrength, 0.18f), 0.0f, 1.0f);
    params.caveFrequency = std::max(1.0e-4f, sanitize(params.caveFrequency, 0.018f));
    params.caveThreshold = clamp(sanitize(params.caveThreshold, 0.68f), 0.05f, 0.95f);
    params.caveStrength = std::max(1.0f, sanitize(params.caveStrength, 42.0f));
    params.caveOctaves = std::max(1, params.caveOctaves);
    params.caveLacunarity = std::max(1.1f, sanitize(params.caveLacunarity, 2.1f));
    params.caveGain = clamp(sanitize(params.caveGain, 0.5f), 0.1f, 0.95f);
    params.caveMinY = sanitize(params.caveMinY, -120.0f);
    params.caveMaxY = std::max(params.caveMinY + 8.0f, sanitize(params.caveMaxY, 220.0f));

    params.tunnelCount = std::max(0, params.tunnelCount);
    params.tunnelRadiusMin = std::max(1.0f, sanitize(params.tunnelRadiusMin, 9.0f));
    params.tunnelRadiusMax = std::max(params.tunnelRadiusMin, sanitize(params.tunnelRadiusMax, 18.0f));
    params.tunnelLengthMin = std::max(32.0f, sanitize(params.tunnelLengthMin, 240.0f));
    params.tunnelLengthMax = std::max(params.tunnelLengthMin, sanitize(params.tunnelLengthMax, 520.0f));
    params.tunnelSegmentLength = std::max(6.0f, sanitize(params.tunnelSegmentLength, 18.0f));

    params.generatorVersion = std::max(3, params.generatorVersion);
    params.skirtDepth = std::max(2.0f, sanitize(params.skirtDepth, 24.0f));
    params.maxChunkCellsPerAxis = std::clamp(params.maxChunkCellsPerAxis, 24, 128);
    params.craterHistoryLimit = std::max(0, params.craterHistoryLimit);
    params.waterRatio = clamp(sanitize(params.waterRatio, 0.18f), 0.0f, 1.0f);
    params.decoration.density = clamp(sanitize(params.decoration.density, 1.0f), 0.0f, 3.0f);
    params.decoration.nearDensityScale = clamp(sanitize(params.decoration.nearDensityScale, 1.0f), 0.0f, 3.0f);
    params.decoration.midDensityScale = clamp(sanitize(params.decoration.midDensityScale, 0.58f), 0.0f, 2.5f);
    params.decoration.farDensityScale = clamp(sanitize(params.decoration.farDensityScale, 0.18f), 0.0f, 1.5f);
    params.decoration.treeLineOffset = clamp(sanitize(params.decoration.treeLineOffset, 0.0f), -180.0f, 260.0f);
    params.decoration.shoreBrushDensity = clamp(sanitize(params.decoration.shoreBrushDensity, 1.0f), 0.0f, 3.0f);
    params.decoration.rockDensity = clamp(sanitize(params.decoration.rockDensity, 1.0f), 0.0f, 3.0f);
    params.decoration.seedOffset = std::clamp(params.decoration.seedOffset, -999999, 999999);

    params.grassColor = clampColor3(params.grassColor, { 0.20f, 0.62f, 0.22f });
    params.roadColor = clampColor3(params.roadColor, { 0.10f, 0.10f, 0.10f });
    params.fieldColor = clampColor3(params.fieldColor, { 0.35f, 0.45f, 0.20f });
    params.waterColor = clampColor3(params.waterColor, { 0.10f, 0.10f, 0.50f });
    params.grassVar = clampColor3(params.grassVar, { 0.05f, 0.10f, 0.05f });
    params.roadVar = clampColor3(params.roadVar, { 0.02f, 0.02f, 0.02f });
    params.fieldVar = clampColor3(params.fieldVar, { 0.04f, 0.06f, 0.04f });
    params.waterVar = clampColor3(params.waterVar, { 0.02f, 0.02f, 0.02f });

    std::vector<TerrainCrater> craterList;
    craterList.reserve(params.dynamicCraters.size());
    for (const TerrainCrater& crater : params.dynamicCraters) {
        craterList.push_back(sanitizeCrater(crater));
    }
    if (params.craterHistoryLimit > 0 && static_cast<int>(craterList.size()) > params.craterHistoryLimit) {
        craterList.erase(craterList.begin(), craterList.end() - params.craterHistoryLimit);
    }
    params.dynamicCraters = std::move(craterList);

    std::vector<TerrainTunnelSeed> tunnelSeedList;
    tunnelSeedList.reserve(params.explicitTunnelSeeds.size());
    for (const TerrainTunnelSeed& seed : params.explicitTunnelSeeds) {
        TerrainTunnelSeed normalizedSeed = sanitizeTunnelSeed(seed);
        if (!normalizedSeed.points.empty()) {
            tunnelSeedList.push_back(std::move(normalizedSeed));
        }
    }
    params.explicitTunnelSeeds = std::move(tunnelSeedList);
    if (!params.explicitTunnelSeeds.empty()) {
        params.tunnelCount = static_cast<int>(params.explicitTunnelSeeds.size());
    }

    params.surfaceOnlyMeshing = !(params.caveEnabled || params.tunnelCount > 0 || !params.explicitTunnelSeeds.empty());
    return params;
}

inline TerrainParams defaultTerrainParams()
{
    return normalizeTerrainParams(TerrainParams {});
}

inline bool terrainCraterListsEqual(const std::vector<TerrainCrater>& lhs, const std::vector<TerrainCrater>& rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        const TerrainCrater& a = lhs[i];
        const TerrainCrater& b = rhs[i];
        if (std::fabs(a.x - b.x) > 1.0e-4f ||
            std::fabs(a.y - b.y) > 1.0e-4f ||
            std::fabs(a.z - b.z) > 1.0e-4f ||
            std::fabs(a.radius - b.radius) > 1.0e-4f ||
            std::fabs(a.depth - b.depth) > 1.0e-4f ||
            std::fabs(a.rim - b.rim) > 1.0e-4f) {
            return false;
        }
    }
    return true;
}

inline bool terrainTunnelSeedListsEqual(const std::vector<TerrainTunnelSeed>& lhs, const std::vector<TerrainTunnelSeed>& rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        const TerrainTunnelSeed& a = lhs[i];
        const TerrainTunnelSeed& b = rhs[i];
        if (std::fabs(a.radius - b.radius) > 1.0e-4f || a.hillAttached != b.hillAttached || a.points.size() != b.points.size()) {
            return false;
        }
        for (std::size_t pointIndex = 0; pointIndex < a.points.size(); ++pointIndex) {
            const Vec3& ap = a.points[pointIndex];
            const Vec3& bp = b.points[pointIndex];
            if (std::fabs(ap.x - bp.x) > 1.0e-4f ||
                std::fabs(ap.y - bp.y) > 1.0e-4f ||
                std::fabs(ap.z - bp.z) > 1.0e-4f) {
                return false;
            }
        }
    }
    return true;
}

inline bool terrainRoadPathListsEqual(const std::vector<TerrainRoadPath>& lhs, const std::vector<TerrainRoadPath>& rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t pathIndex = 0; pathIndex < lhs.size(); ++pathIndex) {
        const TerrainRoadPath& a = lhs[pathIndex];
        const TerrainRoadPath& b = rhs[pathIndex];
        if (a.loop != b.loop || a.nodes.size() != b.nodes.size()) {
            return false;
        }
        for (std::size_t nodeIndex = 0; nodeIndex < a.nodes.size(); ++nodeIndex) {
            const TerrainRoadNode& an = a.nodes[nodeIndex];
            const TerrainRoadNode& bn = b.nodes[nodeIndex];
            if (std::fabs(an.position.x - bn.position.x) > 1.0e-4f ||
                std::fabs(an.position.y - bn.position.y) > 1.0e-4f ||
                std::fabs(an.position.z - bn.position.z) > 1.0e-4f ||
                std::fabs(an.forward.x - bn.forward.x) > 1.0e-4f ||
                std::fabs(an.forward.y - bn.forward.y) > 1.0e-4f ||
                std::fabs(an.forward.z - bn.forward.z) > 1.0e-4f ||
                std::fabs(an.widthMeters - bn.widthMeters) > 1.0e-4f ||
                std::fabs(an.cutWidthMeters - bn.cutWidthMeters) > 1.0e-4f ||
                std::fabs(an.grade - bn.grade) > 1.0e-4f ||
                std::fabs(an.curvature - bn.curvature) > 1.0e-4f) {
                return false;
            }
        }
    }
    return true;
}

inline bool terrainParamsEquivalent(const TerrainParams& lhsInput, const TerrainParams& rhsInput)
{
    const TerrainParams lhs = normalizeTerrainParams(lhsInput);
    const TerrainParams rhs = normalizeTerrainParams(rhsInput);
    return lhs.seed == rhs.seed &&
        lhs.chunkSize == rhs.chunkSize &&
        lhs.worldRadius == rhs.worldRadius &&
        lhs.minY == rhs.minY &&
        lhs.maxY == rhs.maxY &&
        lhs.lod0Radius == rhs.lod0Radius &&
        lhs.lod1Radius == rhs.lod1Radius &&
        lhs.lod2Radius == rhs.lod2Radius &&
        lhs.terrainQuality == rhs.terrainQuality &&
        lhs.autoQualityEnabled == rhs.autoQualityEnabled &&
        lhs.targetFrameMs == rhs.targetFrameMs &&
        lhs.lod0ChunkScale == rhs.lod0ChunkScale &&
        lhs.lod1ChunkScale == rhs.lod1ChunkScale &&
        lhs.lod2ChunkScale == rhs.lod2ChunkScale &&
        lhs.textureTilesEnabled == rhs.textureTilesEnabled &&
        lhs.gameplayRadiusMeters == rhs.gameplayRadiusMeters &&
        lhs.midFieldRadiusMeters == rhs.midFieldRadiusMeters &&
        lhs.horizonRadiusMeters == rhs.horizonRadiusMeters &&
        lhs.lod0CellSize == rhs.lod0CellSize &&
        lhs.lod1CellSize == rhs.lod1CellSize &&
        lhs.lod2CellSize == rhs.lod2CellSize &&
        lhs.meshBuildBudget == rhs.meshBuildBudget &&
        lhs.workerMaxInflight == rhs.workerMaxInflight &&
        lhs.workerResultBudgetPerFrame == rhs.workerResultBudgetPerFrame &&
        lhs.workerResultTimeBudgetMs == rhs.workerResultTimeBudgetMs &&
        lhs.maxAdaptiveLod1Radius == rhs.maxAdaptiveLod1Radius &&
        lhs.maxPendingChunks == rhs.maxPendingChunks &&
        lhs.maxStaleChunks == rhs.maxStaleChunks &&
        lhs.maxDisplayedChunks == rhs.maxDisplayedChunks &&
        lhs.maxDisplayedChunksHardCap == rhs.maxDisplayedChunksHardCap &&
        lhs.drawDistanceOverridesLodRadius == rhs.drawDistanceOverridesLodRadius &&
        lhs.splitLodEnabled == rhs.splitLodEnabled &&
        lhs.highResSplitRatio == rhs.highResSplitRatio &&
        lhs.chunkCacheLimit == rhs.chunkCacheLimit &&
        lhs.farLodConeEnabled == rhs.farLodConeEnabled &&
        lhs.farLodConeDegrees == rhs.farLodConeDegrees &&
        lhs.rearLod2Radius == rhs.rearLod2Radius &&
        lhs.baseHeight == rhs.baseHeight &&
        lhs.heightAmplitude == rhs.heightAmplitude &&
        lhs.heightFrequency == rhs.heightFrequency &&
        lhs.heightOctaves == rhs.heightOctaves &&
        lhs.heightLacunarity == rhs.heightLacunarity &&
        lhs.heightGain == rhs.heightGain &&
        lhs.surfaceDetailAmplitude == rhs.surfaceDetailAmplitude &&
        lhs.surfaceDetailFrequency == rhs.surfaceDetailFrequency &&
        lhs.ridgeAmplitude == rhs.ridgeAmplitude &&
        lhs.ridgeFrequency == rhs.ridgeFrequency &&
        lhs.ridgeSharpness == rhs.ridgeSharpness &&
        lhs.macroWarpAmplitude == rhs.macroWarpAmplitude &&
        lhs.macroWarpFrequency == rhs.macroWarpFrequency &&
        lhs.terraceStrength == rhs.terraceStrength &&
        lhs.terraceStep == rhs.terraceStep &&
        lhs.waterLevel == rhs.waterLevel &&
        lhs.shorelineBand == rhs.shorelineBand &&
        lhs.waterWaveAmplitude == rhs.waterWaveAmplitude &&
        lhs.waterWaveFrequency == rhs.waterWaveFrequency &&
        lhs.biomeFrequency == rhs.biomeFrequency &&
        lhs.snowLine == rhs.snowLine &&
        lhs.caveEnabled == rhs.caveEnabled &&
        lhs.caveFrequency == rhs.caveFrequency &&
        lhs.caveThreshold == rhs.caveThreshold &&
        lhs.caveStrength == rhs.caveStrength &&
        lhs.caveOctaves == rhs.caveOctaves &&
        lhs.caveLacunarity == rhs.caveLacunarity &&
        lhs.caveGain == rhs.caveGain &&
        lhs.caveMinY == rhs.caveMinY &&
        lhs.caveMaxY == rhs.caveMaxY &&
        lhs.tunnelCount == rhs.tunnelCount &&
        lhs.tunnelRadiusMin == rhs.tunnelRadiusMin &&
        lhs.tunnelRadiusMax == rhs.tunnelRadiusMax &&
        lhs.tunnelLengthMin == rhs.tunnelLengthMin &&
        lhs.tunnelLengthMax == rhs.tunnelLengthMax &&
        lhs.tunnelSegmentLength == rhs.tunnelSegmentLength &&
        lhs.generatorVersion == rhs.generatorVersion &&
        lhs.surfaceOnlyMeshing == rhs.surfaceOnlyMeshing &&
        lhs.threadedMeshing == rhs.threadedMeshing &&
        lhs.enableSkirts == rhs.enableSkirts &&
        lhs.skirtDepth == rhs.skirtDepth &&
        lhs.maxChunkCellsPerAxis == rhs.maxChunkCellsPerAxis &&
        lhs.craterHistoryLimit == rhs.craterHistoryLimit &&
        lhs.waterRatio == rhs.waterRatio &&
        lhs.decoration.enabled == rhs.decoration.enabled &&
        lhs.decoration.density == rhs.decoration.density &&
        lhs.decoration.nearDensityScale == rhs.decoration.nearDensityScale &&
        lhs.decoration.midDensityScale == rhs.decoration.midDensityScale &&
        lhs.decoration.farDensityScale == rhs.decoration.farDensityScale &&
        lhs.decoration.treeLineOffset == rhs.decoration.treeLineOffset &&
        lhs.decoration.shoreBrushDensity == rhs.decoration.shoreBrushDensity &&
        lhs.decoration.rockDensity == rhs.decoration.rockDensity &&
        lhs.decoration.collisionEnabled == rhs.decoration.collisionEnabled &&
        lhs.decoration.seedOffset == rhs.decoration.seedOffset &&
        lhs.grassColor.x == rhs.grassColor.x &&
        lhs.grassColor.y == rhs.grassColor.y &&
        lhs.grassColor.z == rhs.grassColor.z &&
        lhs.roadColor.x == rhs.roadColor.x &&
        lhs.roadColor.y == rhs.roadColor.y &&
        lhs.roadColor.z == rhs.roadColor.z &&
        lhs.fieldColor.x == rhs.fieldColor.x &&
        lhs.fieldColor.y == rhs.fieldColor.y &&
        lhs.fieldColor.z == rhs.fieldColor.z &&
        lhs.waterColor.x == rhs.waterColor.x &&
        lhs.waterColor.y == rhs.waterColor.y &&
        lhs.waterColor.z == rhs.waterColor.z &&
        lhs.grassVar.x == rhs.grassVar.x &&
        lhs.grassVar.y == rhs.grassVar.y &&
        lhs.grassVar.z == rhs.grassVar.z &&
        lhs.roadVar.x == rhs.roadVar.x &&
        lhs.roadVar.y == rhs.roadVar.y &&
        lhs.roadVar.z == rhs.roadVar.z &&
        lhs.fieldVar.x == rhs.fieldVar.x &&
        lhs.fieldVar.y == rhs.fieldVar.y &&
        lhs.fieldVar.z == rhs.fieldVar.z &&
        lhs.waterVar.x == rhs.waterVar.x &&
        lhs.waterVar.y == rhs.waterVar.y &&
        lhs.waterVar.z == rhs.waterVar.z &&
        lhs.mountainFront.enabled == rhs.mountainFront.enabled &&
        lhs.mountainFront.coastalBandStart == rhs.mountainFront.coastalBandStart &&
        lhs.mountainFront.coastalBandEnd == rhs.mountainFront.coastalBandEnd &&
        lhs.mountainFront.foothillStart == rhs.mountainFront.foothillStart &&
        lhs.mountainFront.foothillEnd == rhs.mountainFront.foothillEnd &&
        lhs.mountainFront.inlandFadeStart == rhs.mountainFront.inlandFadeStart &&
        lhs.mountainFront.inlandFadeEnd == rhs.mountainFront.inlandFadeEnd &&
        lhs.mountainFront.mountainWallStrength == rhs.mountainFront.mountainWallStrength &&
        lhs.mountainFront.shelfStrength == rhs.mountainFront.shelfStrength &&
        lhs.mountainFront.cliffStrength == rhs.mountainFront.cliffStrength &&
        lhs.mountainFront.ridgeWarpScale == rhs.mountainFront.ridgeWarpScale &&
        lhs.roads.enabled == rhs.roads.enabled &&
        lhs.roads.desiredRoadCount == rhs.roads.desiredRoadCount &&
        lhs.roads.laneWidthMeters == rhs.roads.laneWidthMeters &&
        lhs.roads.shoulderWidthMeters == rhs.roads.shoulderWidthMeters &&
        lhs.roads.cutWidthMeters == rhs.roads.cutWidthMeters &&
        lhs.roads.cutBankWidthMeters == rhs.roads.cutBankWidthMeters &&
        lhs.roads.maxGrade == rhs.roads.maxGrade &&
        lhs.roads.maxGradeHard == rhs.roads.maxGradeHard &&
        lhs.roads.minTurnRadiusMeters == rhs.roads.minTurnRadiusMeters &&
        lhs.roads.preferredTurnRadiusMeters == rhs.roads.preferredTurnRadiusMeters &&
        lhs.roads.stepMeters == rhs.roads.stepMeters &&
        lhs.roads.sampleLookAheadMeters == rhs.roads.sampleLookAheadMeters &&
        lhs.roads.sampleConeDegrees == rhs.roads.sampleConeDegrees &&
        lhs.roads.branchChance == rhs.roads.branchChance &&
        lhs.roads.flattenStrength == rhs.roads.flattenStrength &&
        lhs.roads.cutStrength == rhs.roads.cutStrength &&
        lhs.roads.fillStrength == rhs.roads.fillStrength &&
        lhs.roads.edgeFeatherMeters == rhs.roads.edgeFeatherMeters &&
        lhs.roads.potholeChance == rhs.roads.potholeChance &&
        lhs.roads.potholeDepthMeters == rhs.roads.potholeDepthMeters &&
        lhs.roads.patchChance == rhs.roads.patchChance &&
        lhs.roads.patchRaiseMeters == rhs.roads.patchRaiseMeters &&
        lhs.roads.asphaltNoiseScale == rhs.roads.asphaltNoiseScale &&
        lhs.roads.crackStrength == rhs.roads.crackStrength &&
        terrainCraterListsEqual(lhs.dynamicCraters, rhs.dynamicCraters) &&
        terrainTunnelSeedListsEqual(lhs.explicitTunnelSeeds, rhs.explicitTunnelSeeds) &&
        terrainRoadPathListsEqual(lhs.explicitRoadPaths, rhs.explicitRoadPaths);
}

inline float applyDynamicCratersToSurfaceHeight(float x, float z, float height, const TerrainParams& params)
{
    float surface = height;
    for (const TerrainCrater& crater : params.dynamicCraters) {
        const float dx = x - crater.x;
        const float dz = z - crater.z;
        const float dist = std::sqrt((dx * dx) + (dz * dz));
        const float t = dist / crater.radius;
        if (t < 1.0f) {
            const float bowl = 1.0f - (t * t);
            surface -= crater.depth * bowl * bowl;
        } else if (t < 1.24f) {
            const float rimT = (t - 1.0f) / 0.24f;
            const float rim = 1.0f - rimT;
            surface += crater.radius * crater.rim * rim * rim;
        }
    }
    return surface;
}

inline float distancePointToSegment(const Vec3& point, const Vec3& a, const Vec3& b)
{
    const Vec3 ab = b - a;
    const Vec3 ap = point - a;
    const float abLenSq = lengthSquared(ab);
    if (abLenSq <= 1.0e-8f) {
        return length(point - a);
    }
    const float t = clamp(dot(ap, ab) / abLenSq, 0.0f, 1.0f);
    const Vec3 closest = a + (ab * t);
    return length(point - closest);
}

inline float smootherstep01(float t)
{
    t = clamp(t, 0.0f, 1.0f);
    return t * t * t * (t * ((t * 6.0f) - 15.0f) + 10.0f);
}

inline float remap01(float value, float a, float b)
{
    if (std::fabs(b - a) <= 1.0e-6f) {
        return value >= b ? 1.0f : 0.0f;
    }
    return clamp((value - a) / (b - a), 0.0f, 1.0f);
}

inline float ridgeNoise01(float x, float z, float freq, int octaves, float lacunarity, float gain, int seed)
{
    float amp = 1.0f;
    float sum = 0.0f;
    float weight = 0.0f;
    float localFreq = freq;
    for (int i = 0; i < octaves; ++i) {
        const float n = (valueNoise2(x * localFreq, z * localFreq, seed + (i * 97)) * 2.0f) - 1.0f;
        const float r = 1.0f - std::fabs(n);
        sum += r * amp;
        weight += amp;
        amp *= gain;
        localFreq *= lacunarity;
    }
    return weight > 1.0e-6f ? (sum / weight) : 0.0f;
}

inline std::vector<TerrainTunnelSeed> buildTunnelSeeds(const TerrainParams& inputParams)
{
    const TerrainParams params = normalizeTerrainParams(inputParams);
    std::vector<TerrainTunnelSeed> out;
    out.reserve(static_cast<std::size_t>(std::max(0, params.tunnelCount)));
    for (int i = 0; i < params.tunnelCount; ++i) {
        const int idx = (i + 1) * 13;
        const auto lerpHash = [&](int a, int b, int c, float minValue, float maxValue) {
            return mix(minValue, maxValue, hash01(a, b, c, params.seed));
        };

        const float sx = lerpHash(idx, 1, 3, -params.worldRadius, params.worldRadius);
        const float sy = lerpHash(idx, 4, 8, params.minY, params.maxY);
        const float sz = lerpHash(idx, 9, 15, -params.worldRadius, params.worldRadius);
        const float heading = lerpHash(idx, 16, 22, 0.0f, kPi * 2.0f);
        const float pitch = lerpHash(idx, 23, 31, -0.2f, 0.2f);
        const float tunnelLength = lerpHash(idx, 37, 41, params.tunnelLengthMin, params.tunnelLengthMax);
        const float radius = lerpHash(idx, 43, 47, params.tunnelRadiusMin, params.tunnelRadiusMax);
        const float wobbleAmp = lerpHash(idx, 51, 59, 5.0f, 22.0f);
        const float wobbleFreq = lerpHash(idx, 61, 67, 0.01f, 0.045f);
        const float yawJitter = lerpHash(idx, 71, 73, -0.12f, 0.12f);

        TerrainTunnelSeed tunnel;
        tunnel.radius = radius;
        tunnel.hillAttached = true;

        const int steps = std::max(3, static_cast<int>(std::floor(tunnelLength / params.tunnelSegmentLength)));
        tunnel.points.reserve(static_cast<std::size_t>(steps + 1));
        for (int step = 0; step <= steps; ++step) {
            const float t = static_cast<float>(step) / static_cast<float>(steps);
            const float dist = t * tunnelLength;
            const float bendHeading = heading + (std::sin((dist * wobbleFreq) + static_cast<float>(i + 1)) * yawJitter);
            const float bendPitch = pitch + (std::cos((dist * wobbleFreq * 0.7f) + (static_cast<float>(i + 1) * 1.7f)) * 0.08f);
            const Vec3 bendDir {
                std::cos(bendPitch) * std::cos(bendHeading),
                std::sin(bendPitch),
                std::cos(bendPitch) * std::sin(bendHeading)
            };
            const float wx = std::sin((dist * wobbleFreq) + (static_cast<float>(i + 1) * 0.73f)) * wobbleAmp;
            const float wy = std::sin((dist * wobbleFreq * 0.63f) + (static_cast<float>(i + 1) * 1.19f)) * (wobbleAmp * 0.42f);
            const float wz = std::cos((dist * wobbleFreq) + (static_cast<float>(i + 1) * 0.37f)) * wobbleAmp;
            tunnel.points.push_back({
                sx + (bendDir.x * dist) + wx,
                sy + (bendDir.y * dist) + wy,
                sz + (bendDir.z * dist) + wz
            });
        }
        out.push_back(std::move(tunnel));
    }
    return out;
}

inline float sampleCoastalMountainHeight(float x, float z, const TerrainFieldContext& context)
{
    const TerrainParams& params = context.params;
    const TerrainMountainFrontSettings& mf = params.mountainFront;

    const float inland = std::max(0.0f, z);
    const float foothillT = smootherstep01(remap01(inland, mf.foothillStart, mf.foothillEnd));
    const float coastalRise = smootherstep01(remap01(inland, mf.foothillStart, mf.coastalBandEnd));
    const float inlandFade = 1.0f - smootherstep01(remap01(inland, mf.inlandFadeStart, mf.inlandFadeEnd));

    const float warpX = ((fbm2(x * mf.ridgeWarpScale, z * mf.ridgeWarpScale, 3, 2.0f, 0.5f, params.seed + 501) * 2.0f) - 1.0f) * params.macroWarpAmplitude;
    const float warpedX = x + warpX;

    const float macro = fbm2(warpedX * params.heightFrequency, z * params.heightFrequency, params.heightOctaves, params.heightLacunarity, params.heightGain, params.seed + 101);
    const float ridges = ridgeNoise01(warpedX, z, params.ridgeFrequency, 5, 2.08f, 0.52f, params.seed + 211);
    const float shelves = fbm2(warpedX * (params.heightFrequency * 2.2f), z * (params.heightFrequency * 2.2f), 3, 2.0f, 0.55f, params.seed + 313);

    const float shoreFoothills = mix(
        params.baseHeight - 28.0f,
        params.baseHeight + (params.heightAmplitude * 0.16f),
        foothillT);
    const float mountainFloor = mix(
        shoreFoothills,
        params.baseHeight + (params.heightAmplitude * 0.46f),
        coastalRise);

    const float macroRelief = mix(
        -params.heightAmplitude * 0.12f,
        params.heightAmplitude * 0.58f,
        macro);
    const float ridgeRelief =
        std::pow(ridges, params.ridgeSharpness) *
        params.ridgeAmplitude *
        mf.mountainWallStrength;
    const float shelfRelief =
        ((shelves * 2.0f) - 1.0f) *
        params.heightAmplitude *
        0.09f *
        mf.shelfStrength;
    const float cliffMask = smootherstep01(remap01(ridges, 0.66f, 0.92f));
    const float cliffLift = cliffMask * params.ridgeAmplitude * 0.24f * mf.cliffStrength;
    const float coastalEnvelope = coastalRise * inlandFade;
    float surface = mix(
        params.waterLevel - (params.shorelineBand * 1.75f),
        mountainFloor + macroRelief + ridgeRelief + shelfRelief + cliffLift,
        coastalEnvelope);

    if (inland < mf.coastalBandStart) {
        const float shoreT = smootherstep01(remap01(inland, 0.0f, mf.coastalBandStart));
        const float shoreBluffs =
            ((fbm2(x * 0.0012f, z * 0.0012f, 3, 2.0f, 0.55f, params.seed + 777) * 2.0f) - 1.0f) *
            params.heightAmplitude *
            0.028f;
        surface = mix(params.waterLevel - params.shorelineBand, surface + shoreBluffs, shoreT);
    }

    return surface;
}

inline float sampleProceduralSurfaceHeight(float x, float z, const TerrainFieldContext& context)
{
    const TerrainParams& params = context.params;
    float surface = sampleCoastalMountainHeight(x, z, context);
    const float detail = ((fbm2(x * params.surfaceDetailFrequency, z * params.surfaceDetailFrequency, 4, 2.0f, 0.55f, params.seed + 1301) * 2.0f) - 1.0f) * params.surfaceDetailAmplitude;
    surface += detail;
    if (params.terraceStrength > 0.0f) {
        const float stepped = std::floor((surface / params.terraceStep) + 0.5f) * params.terraceStep;
        surface = mix(surface, stepped, params.terraceStrength);
    }
    return applyDynamicCratersToSurfaceHeight(x, z, surface, params);
}

inline TerrainHydraulicSample sampleTerrainHydraulicMaps(float x, float z, const TerrainFieldContext& context)
{
    const TerrainParams& params = context.params;
    const float moisture = clamp(
        (fbm2(x * 0.00135f, z * 0.00135f, 4, 2.0f, 0.55f, params.seed + 1847) * 1.18f) +
            ((valueNoise2(x * 0.0047f, z * 0.0047f, params.seed + 1889) * 2.0f) - 1.0f) * 0.22f,
        0.0f,
        1.0f);
    const float hardness = clamp(
        (fbm2(x * 0.00195f, z * 0.00195f, 3, 2.12f, 0.58f, params.seed + 2011) * 1.08f) +
            ((valueNoise2(x * 0.0092f, z * 0.0092f, params.seed + 2077) * 2.0f) - 1.0f) * 0.18f,
        0.0f,
        1.0f);
    const float resource = clamp(
        (fbm2(x * 0.0011f, z * 0.0011f, 4, 2.18f, 0.53f, params.seed + 2237) * 1.14f) +
            ((valueNoise2(x * 0.0064f, z * 0.0064f, params.seed + 2293) * 2.0f) - 1.0f) * 0.20f,
        0.0f,
        1.0f);
    const float sediment = clamp(
        (fbm2(x * 0.0024f, z * 0.0024f, 3, 1.93f, 0.57f, params.seed + 2357) * 1.04f) +
            ((valueNoise2(x * 0.011f, z * 0.011f, params.seed + 2399) * 2.0f) - 1.0f) * 0.15f,
        0.0f,
        1.0f);

    const float sampleStep = clamp(params.chunkSize * 0.05f, 8.0f, 42.0f);
    const float base = sampleProceduralSurfaceHeight(x, z, context);
    const float hL = sampleProceduralSurfaceHeight(x - sampleStep, z, context);
    const float hR = sampleProceduralSurfaceHeight(x + sampleStep, z, context);
    const float hD = sampleProceduralSurfaceHeight(x, z - sampleStep, context);
    const float hU = sampleProceduralSurfaceHeight(x, z + sampleStep, context);
    const float avgNeighbor = (hL + hR + hD + hU) * 0.25f;
    const float downhill =
        std::max(0.0f, base - hL) +
        std::max(0.0f, base - hR) +
        std::max(0.0f, base - hD) +
        std::max(0.0f, base - hU);
    const float convexity = clamp((base - avgNeighbor) / std::max(1.0f, params.heightAmplitude * 0.18f), -1.0f, 1.0f);
    const float flow = clamp(
        (downhill / std::max(1.0f, params.heightAmplitude * 0.28f)) +
            (moisture * 0.42f) +
            (sediment * 0.18f),
        0.0f,
        1.0f);

    const float erosionStrength = flow * (1.0f - hardness) * mix(1.4f, 6.8f, moisture);
    const float erosion = (-std::max(0.0f, convexity) * erosionStrength) +
        (std::max(0.0f, -convexity) * sediment * moisture * 1.9f);

    return {
        moisture,
        hardness,
        resource,
        sediment,
        flow,
        erosion
    };
}

inline float sampleRoadlessHeight(float x, float z, const TerrainFieldContext& context)
{
    return sampleProceduralSurfaceHeight(x, z, context) + sampleTerrainHydraulicMaps(x, z, context).erosion;
}

inline float scoreRoadCandidate(
    const Vec3& current,
    const Vec3& forward,
    const Vec3& candidateDir,
    float stepMeters,
    const TerrainFieldContext& context)
{
    const TerrainParams& params = context.params;
    const TerrainRoadSettings& roads = params.roads;

    const Vec3 nextPos = current + (candidateDir * stepMeters);
    const float h0 = sampleRoadlessHeight(current.x, current.z, context);
    const float h1 = sampleRoadlessHeight(nextPos.x, nextPos.z, context);
    const float grade = std::fabs(h1 - h0) / std::max(1.0f, stepMeters);

    const float dotForward = clamp(dot(normalize(forward), normalize(candidateDir)), -1.0f, 1.0f);
    const float turnAngle = std::acos(dotForward);
    const float turnRadius = turnAngle > 1.0e-4f ? (stepMeters / turnAngle) : 1.0e9f;

    float score = 0.0f;
    if (grade > roads.maxGradeHard) {
        score -= 100000.0f;
    } else {
        score -= grade * 900.0f;
    }
    if (turnRadius < roads.minTurnRadiusMeters) {
        score -= 100000.0f;
    } else {
        score -= std::fabs(turnRadius - roads.preferredTurnRadiusMeters) * 0.08f;
    }

    const float lookAhead = roads.sampleLookAheadMeters;
    const int lookSteps = std::max(3, static_cast<int>(std::ceil(lookAhead / stepMeters)));
    Vec3 probePos = current;
    float accumulatedPenalty = 0.0f;
    for (int i = 0; i < lookSteps; ++i) {
        probePos = probePos + (candidateDir * stepMeters);
        const float ph0 = sampleRoadlessHeight(probePos.x, probePos.z, context);
        const float ph1 = sampleRoadlessHeight(probePos.x + (candidateDir.x * stepMeters), probePos.z + (candidateDir.z * stepMeters), context);
        accumulatedPenalty += (std::fabs(ph1 - ph0) / std::max(1.0f, stepMeters)) * 140.0f;
    }
    return score - accumulatedPenalty;
}

inline TerrainRoadPath buildMountainRoadPath(const TerrainFieldContext& context, Vec3 startPos, Vec3 startForward, int seedOffset)
{
    const TerrainParams& params = context.params;
    const TerrainRoadSettings& roads = params.roads;
    std::mt19937 rng(static_cast<std::uint32_t>(params.seed + seedOffset));

    TerrainRoadPath path;
    Vec3 current = startPos;
    current.y = sampleRoadlessHeight(current.x, current.z, context);

    Vec3 forward = normalize(Vec3 { startForward.x, 0.0f, startForward.z });
    if (lengthSquared(forward) <= 1.0e-6f) {
        forward = { 1.0f, 0.0f, 0.0f };
    }

    const float halfCone = radians(roads.sampleConeDegrees * 0.5f);
    const float stepMeters = roads.stepMeters;
    const float roadLengthBudget = std::max(
        params.chunkSize * 28.0f,
        std::min(params.worldRadius * 0.72f, params.mountainFront.inlandFadeEnd * 0.92f));
    const int maxSteps = std::max(120, static_cast<int>(std::ceil(roadLengthBudget / std::max(1.0f, stepMeters))));
    std::uniform_real_distribution<float> jitterDistribution(-3.0f, 3.0f);

    for (int step = 0; step < maxSteps; ++step) {
        TerrainRoadNode node;
        node.position = current;
        node.forward = forward;
        node.widthMeters = (roads.laneWidthMeters * 2.0f) + (roads.shoulderWidthMeters * 2.0f);
        node.cutWidthMeters = roads.cutBankWidthMeters;
        path.nodes.push_back(node);

        float bestScore = -1.0e30f;
        Vec3 bestDir = forward;
        const int candidates = 13;
        for (int i = 0; i < candidates; ++i) {
            const float t = (static_cast<float>(i) / static_cast<float>(candidates - 1)) * 2.0f - 1.0f;
            const float angle = t * halfCone;
            const float cosA = std::cos(angle);
            const float sinA = std::sin(angle);
            Vec3 candidateDir {
                (forward.x * cosA) - (forward.z * sinA),
                0.0f,
                (forward.x * sinA) + (forward.z * cosA)
            };
            candidateDir = normalize(candidateDir);
            float score = scoreRoadCandidate(current, forward, candidateDir, stepMeters, context);
            score += jitterDistribution(rng);
            if (score > bestScore) {
                bestScore = score;
                bestDir = candidateDir;
            }
        }

        const Vec3 next = current + (bestDir * stepMeters);
        const float h0 = sampleRoadlessHeight(current.x, current.z, context);
        const float h1 = sampleRoadlessHeight(next.x, next.z, context);
        const float delta = h1 - h0;
        const float maxRise = roads.maxGrade * stepMeters;
        const float clampedRise = clamp(delta, -maxRise, maxRise);
        current = { next.x, h0 + clampedRise, next.z };
        forward = bestDir;

        if (length(Vec3 { current.x, 0.0f, current.z }) > (params.worldRadius * 0.94f) ||
            current.z >= params.mountainFront.inlandFadeEnd * 0.96f ||
            current.y <= params.waterLevel + 6.0f) {
            break;
        }
    }

    return path;
}

inline std::vector<TerrainRoadPath> buildRoadPaths(const TerrainFieldContext& context)
{
    const TerrainParams& params = context.params;
    if (!params.roads.enabled) {
        return {};
    }
    if (!params.explicitRoadPaths.empty()) {
        return params.explicitRoadPaths;
    }

    std::vector<TerrainRoadPath> out;
    out.reserve(static_cast<std::size_t>(params.roads.desiredRoadCount));
    const auto appendPath = [&](Vec3 start, Vec3 forward, int seedOffset) {
        start.y = sampleRoadlessHeight(start.x, start.z, context);
        TerrainRoadPath path = buildMountainRoadPath(context, start, forward, seedOffset);
        if (path.nodes.size() > 8u) {
            out.push_back(std::move(path));
        }
    };

    if (params.roads.desiredRoadCount <= 0) {
        return out;
    }

    const float arterialZ = clamp(
        params.mountainFront.foothillStart + std::max(240.0f, params.roads.cutBankWidthMeters * 18.0f),
        params.mountainFront.foothillStart + 120.0f,
        params.mountainFront.foothillEnd * 0.55f);
    const float arterialStartX = -std::min(params.worldRadius * 0.58f, params.mountainFront.inlandFadeEnd * 0.48f);
    appendPath({ arterialStartX, 0.0f, arterialZ }, normalize(Vec3 { 1.0f, 0.0f, 0.04f }), 1000);

    const TerrainRoadPath* trunk = out.empty() ? nullptr : &out.front();
    for (int i = 1; i < params.roads.desiredRoadCount; ++i) {
        bool appendedBranch = false;
        if (trunk != nullptr && trunk->nodes.size() > 20u) {
            const float branchT =
                params.roads.desiredRoadCount <= 2
                    ? 0.58f
                    : mix(
                          0.22f,
                          0.82f,
                          static_cast<float>(i - 1) / static_cast<float>(std::max(1, params.roads.desiredRoadCount - 2)));
            const std::size_t maxNodeIndex = trunk->nodes.size() - 6u;
            const std::size_t nodeIndex = std::clamp<std::size_t>(
                static_cast<std::size_t>(std::round(static_cast<float>(trunk->nodes.size() - 1u) * branchT)),
                6u,
                maxNodeIndex);
            const TerrainRoadNode& anchor = trunk->nodes[nodeIndex];
            const float branchSign = (i % 2 == 0) ? 1.0f : -1.0f;
            const float branchAngle = radians(mix(18.0f, 36.0f, hash01(i + 31, 53, 71, params.seed)));
            const float yaw = branchSign * branchAngle;
            const float cosA = std::cos(yaw);
            const float sinA = std::sin(yaw);
            Vec3 forward {
                (anchor.forward.x * cosA) - (anchor.forward.z * sinA),
                0.0f,
                (anchor.forward.x * sinA) + (anchor.forward.z * cosA)
            };
            forward = normalize(forward, { 1.0f, 0.0f, 0.0f });
            Vec3 start = anchor.position + (forward * (params.roads.stepMeters * 3.0f));
            start.z = clamp(
                start.z,
                params.mountainFront.foothillStart + 80.0f,
                params.mountainFront.coastalBandEnd * 0.92f);
            const std::size_t beforeCount = out.size();
            appendPath(start, forward, 1000 + (i * 73));
            appendedBranch = out.size() > beforeCount;
        }

        if (appendedBranch) {
            continue;
        }

        const float z = mix(
            params.mountainFront.foothillStart + 320.0f,
            params.mountainFront.coastalBandEnd * 0.62f,
            hash01(i + 11, 7, 3, params.seed));
        const float x = mix(
            -params.worldRadius * 0.58f,
            params.worldRadius * 0.12f,
            hash01(i + 19, 17, 5, params.seed));
        const float yaw = mix(-0.28f, 0.18f, hash01(i + 23, 29, 31, params.seed));
        appendPath({ x, 0.0f, z }, normalize(Vec3 { std::cos(yaw), 0.0f, std::sin(yaw) }), 1000 + (i * 73));
    }
    return out;
}

inline std::int64_t terrainRoadCellKey(int cellX, int cellZ)
{
    return (static_cast<std::int64_t>(cellX) << 32) ^ static_cast<std::uint32_t>(cellZ);
}

inline std::shared_ptr<TerrainRoadNetworkIndex> buildRoadNetworkIndex(
    const TerrainParams& params,
    const std::vector<TerrainRoadPath>& roadPaths)
{
    auto index = std::make_shared<TerrainRoadNetworkIndex>();
    if (roadPaths.empty()) {
        return index;
    }

    index->cellSize = clamp(
        std::max(params.roads.sampleLookAheadMeters * 0.75f, params.roads.cutBankWidthMeters * 6.0f),
        128.0f,
        1024.0f);

    bool hasBounds = false;
    for (std::size_t pathIndex = 0; pathIndex < roadPaths.size(); ++pathIndex) {
        const TerrainRoadPath& path = roadPaths[pathIndex];
        for (std::size_t nodeIndex = 1; nodeIndex < path.nodes.size(); ++nodeIndex) {
            const TerrainRoadNode& a = path.nodes[nodeIndex - 1];
            const TerrainRoadNode& b = path.nodes[nodeIndex];
            const float influenceRadius =
                (std::max({ a.widthMeters, b.widthMeters, a.cutWidthMeters, b.cutWidthMeters }) * 0.5f) +
                (params.roads.shoulderWidthMeters * 1.5f) +
                params.roads.edgeFeatherMeters;
            const float minX = std::min(a.position.x, b.position.x) - influenceRadius;
            const float maxX = std::max(a.position.x, b.position.x) + influenceRadius;
            const float minZ = std::min(a.position.z, b.position.z) - influenceRadius;
            const float maxZ = std::max(a.position.z, b.position.z) + influenceRadius;

            if (!hasBounds) {
                index->minX = minX;
                index->maxX = maxX;
                index->minZ = minZ;
                index->maxZ = maxZ;
                hasBounds = true;
            } else {
                index->minX = std::min(index->minX, minX);
                index->maxX = std::max(index->maxX, maxX);
                index->minZ = std::min(index->minZ, minZ);
                index->maxZ = std::max(index->maxZ, maxZ);
            }

            const int cellX0 = static_cast<int>(std::floor(minX / index->cellSize));
            const int cellX1 = static_cast<int>(std::floor(maxX / index->cellSize));
            const int cellZ0 = static_cast<int>(std::floor(minZ / index->cellSize));
            const int cellZ1 = static_cast<int>(std::floor(maxZ / index->cellSize));
            for (int cellZ = cellZ0; cellZ <= cellZ1; ++cellZ) {
                for (int cellX = cellX0; cellX <= cellX1; ++cellX) {
                    index->cells[terrainRoadCellKey(cellX, cellZ)].push_back({
                        static_cast<int>(pathIndex),
                        static_cast<int>(nodeIndex)
                    });
                }
            }
        }
    }

    index->valid = hasBounds && !index->cells.empty();
    return index;
}

inline TerrainRoadSample sampleRoadNetwork(
    float x,
    float z,
    const TerrainParams& params,
    const std::vector<TerrainRoadPath>& roadPaths,
    const TerrainRoadNetworkIndex* roadIndex)
{
    TerrainRoadSample out;
    if (roadPaths.empty()) {
        return out;
    }
    const TerrainRoadSettings& roads = params.roads;
    const Vec3 point { x, 0.0f, z };

    const auto sampleSegment = [&](const TerrainRoadPath& path, std::size_t i) {
        if (i == 0u || i >= path.nodes.size()) {
            return;
        }
        const TerrainRoadNode& a = path.nodes[i - 1];
        const TerrainRoadNode& b = path.nodes[i];
        const Vec3 a2 { a.position.x, 0.0f, a.position.z };
        const Vec3 b2 { b.position.x, 0.0f, b.position.z };
        const Vec3 ab = b2 - a2;
        const float abLenSq = lengthSquared(ab);
        if (abLenSq <= 1.0e-6f) {
            return;
        }
        const float t = clamp(dot(point - a2, ab) / abLenSq, 0.0f, 1.0f);
        const Vec3 p = a2 + (ab * t);
        const float dist = length(point - p);
        const float width = mix(a.widthMeters, b.widthMeters, t) * 0.5f;
        const float cutWidth = mix(a.cutWidthMeters, b.cutWidthMeters, t) * 0.5f;
        const float roadY = mix(a.position.y, b.position.y, t);
        if (dist < out.distanceToCenter) {
            out.distanceToCenter = dist;
            out.distanceToEdge = dist - width;
            out.roadHeight = roadY;
            out.roadMask = 1.0f - smootherstep01(remap01(dist, width - roads.edgeFeatherMeters, width));
            out.shoulderMask = 1.0f - smootherstep01(remap01(dist, width, width + (roads.shoulderWidthMeters * 1.5f)));
            out.cutMask = 1.0f - smootherstep01(remap01(dist, width, cutWidth));
            out.along = static_cast<float>(i) + t;
            out.centerPosition = { p.x, roadY, p.z };
            out.forward = normalize(a.forward + ((b.forward - a.forward) * t), normalize(ab, { 1.0f, 0.0f, 0.0f }));
            out.widthMeters = width * 2.0f;
            out.grade = mix(a.grade, b.grade, t);
            out.curvature = mix(a.curvature, b.curvature, t);
            out.segmentT = t;
            out.pathIndex = static_cast<int>(&path - roadPaths.data());
            out.nodeIndex = static_cast<int>(i);
            const float patchNoise = valueNoise2(x * 0.18f, z * 0.18f, params.seed + 8101);
            const float potholeNoise = valueNoise2(x * 0.24f, z * 0.24f, params.seed + 9101);
            out.patchMask = patchNoise > (1.0f - roads.patchChance) ? 1.0f : 0.0f;
            out.potholeMask = potholeNoise > (1.0f - roads.potholeChance) ? 1.0f : 0.0f;
        }
    };

    if (roadIndex != nullptr && roadIndex->valid) {
        if (x < roadIndex->minX || x > roadIndex->maxX || z < roadIndex->minZ || z > roadIndex->maxZ) {
            return out;
        }

        const int cellX = static_cast<int>(std::floor(x / roadIndex->cellSize));
        const int cellZ = static_cast<int>(std::floor(z / roadIndex->cellSize));
        const auto it = roadIndex->cells.find(terrainRoadCellKey(cellX, cellZ));
        if (it == roadIndex->cells.end()) {
            return out;
        }

        for (const TerrainRoadSegmentLocator& locator : it->second) {
            if (locator.pathIndex < 0 || locator.pathIndex >= static_cast<int>(roadPaths.size())) {
                continue;
            }
            sampleSegment(roadPaths[static_cast<std::size_t>(locator.pathIndex)], static_cast<std::size_t>(locator.nodeIndex));
        }
        return out;
    }

    for (const TerrainRoadPath& path : roadPaths) {
        for (std::size_t i = 1; i < path.nodes.size(); ++i) {
            sampleSegment(path, i);
        }
    }
    return out;
}

inline TerrainRoadSample sampleRoadNetwork(float x, float z, const TerrainFieldContext& context)
{
    return sampleRoadNetwork(x, z, context.params, context.roadPaths, context.roadIndex.get());
}

inline bool terrainRoadSampleValid(const TerrainRoadSample& sample)
{
    return sample.pathIndex >= 0 && sample.nodeIndex > 0 && sample.roadMask > 0.0f;
}

inline Vec3 terrainRoadRight(const TerrainRoadSample& sample)
{
    const Vec3 flatForward = normalize(Vec3 { sample.forward.x, 0.0f, sample.forward.z }, { 0.0f, 0.0f, 1.0f });
    return normalize(Vec3 { flatForward.z, 0.0f, -flatForward.x }, { 1.0f, 0.0f, 0.0f });
}

inline Vec3 terrainRoadPlacementPosition(
    const TerrainRoadSample& sample,
    float lateralOffsetMeters = 0.0f,
    float verticalOffsetMeters = 0.0f)
{
    return sample.centerPosition + (terrainRoadRight(sample) * lateralOffsetMeters) + Vec3 { 0.0f, verticalOffsetMeters, 0.0f };
}

inline float terrainRoadSurfaceFriction(const TerrainRoadSample& sample)
{
    if (!terrainRoadSampleValid(sample)) {
        return 0.45f;
    }
    float friction = mix(0.72f, 1.08f, clamp(sample.roadMask + (sample.shoulderMask * 0.2f), 0.0f, 1.0f));
    friction -= sample.patchMask * 0.05f;
    friction -= sample.potholeMask * 0.14f;
    return clamp(friction, 0.28f, 1.2f);
}

inline float terrainRoadSteeringAssist(const TerrainRoadSample& sample)
{
    if (!terrainRoadSampleValid(sample)) {
        return 0.0f;
    }
    const float curvaturePenalty = clamp(std::fabs(sample.curvature) * 96.0f, 0.0f, 1.0f);
    const float gradePenalty = clamp(std::fabs(sample.grade) * 6.0f, 0.0f, 1.0f);
    return clamp((sample.roadMask * 0.86f) + (sample.shoulderMask * 0.18f) - (curvaturePenalty * 0.35f) - (gradePenalty * 0.15f), 0.0f, 1.0f);
}

inline std::optional<TerrainRoadSample> findRoadSpawnSample(
    const TerrainFieldContext& context,
    float preferredZ = 0.0f,
    float minDryGroundClearance = 6.0f)
{
    TerrainRoadSample best;
    bool found = false;
    float bestScore = std::numeric_limits<float>::max();
    const float dryGround = context.params.waterLevel + std::max(minDryGroundClearance, context.params.shorelineBand * 0.5f);
    for (const TerrainRoadPath& path : context.roadPaths) {
        for (std::size_t nodeIndex = 1; nodeIndex < path.nodes.size(); ++nodeIndex) {
            const TerrainRoadNode& node = path.nodes[nodeIndex];
            if (node.position.y <= dryGround) {
                continue;
            }
            const TerrainRoadSample sample = sampleRoadNetwork(node.position.x, node.position.z, context);
            if (!terrainRoadSampleValid(sample)) {
                continue;
            }
            const float score =
                std::fabs(sample.centerPosition.x) * 0.01f +
                std::fabs(sample.centerPosition.z - preferredZ) * 0.004f +
                std::fabs(sample.grade) * 80.0f +
                std::fabs(sample.curvature) * 140.0f;
            if (!found || score < bestScore) {
                best = sample;
                bestScore = score;
                found = true;
            }
        }
    }
    return found ? std::optional<TerrainRoadSample>(best) : std::nullopt;
}

inline void attachTunnelSeedToSurface(TerrainTunnelSeed& tunnel, const TerrainFieldContext& context)
{
    if (!tunnel.hillAttached || tunnel.points.size() < 2u) {
        return;
    }

    const float tunnelRadius = std::max(1.0f, tunnel.radius);
    const float mouthDepth = std::max(1.2f, tunnelRadius * 0.38f);
    const auto applyAnchor = [&](const std::size_t anchorIndex, const std::size_t adjacentIndex) {
        if (anchorIndex >= tunnel.points.size() || adjacentIndex >= tunnel.points.size()) {
            return;
        }

        Vec3& anchor = tunnel.points[anchorIndex];
        Vec3& adjacent = tunnel.points[adjacentIndex];
        const float surface = sampleProceduralSurfaceHeight(anchor.x, anchor.z, context);
        anchor.y = surface - mouthDepth;

        const Vec3 direction = normalize(adjacent - anchor, { 0.0f, -1.0f, 0.0f });
        if (lengthSquared(direction) > 1.0e-6f) {
            adjacent.y = mix(adjacent.y, anchor.y - std::max(2.0f, tunnelRadius * 0.55f), 0.55f);
        }
    };

    applyAnchor(0u, 1u);
    applyAnchor(tunnel.points.size() - 1u, tunnel.points.size() - 2u);
}

inline TerrainFieldContext createTerrainFieldContext(const TerrainParams& inputParams)
{
    TerrainFieldContext context;
    context.params = normalizeTerrainParams(inputParams);

    if (!context.params.explicitTunnelSeeds.empty()) {
        context.tunnelSeeds = context.params.explicitTunnelSeeds;
    } else if (context.params.tunnelCount > 0) {
        context.tunnelSeeds = buildTunnelSeeds(context.params);
    }

    for (TerrainTunnelSeed& seed : context.tunnelSeeds) {
        attachTunnelSeedToSurface(seed, context);
    }

    context.roadPaths = !context.params.explicitRoadPaths.empty()
        ? context.params.explicitRoadPaths
        : buildRoadPaths(context);
    context.roadIndex = buildRoadNetworkIndex(context.params, context.roadPaths);

    const TerrainParams roadParams = context.params;
    const auto roadPaths = std::make_shared<const std::vector<TerrainRoadPath>>(context.roadPaths);
    const std::shared_ptr<const TerrainRoadNetworkIndex> roadIndex = context.roadIndex;
    context.sampleRoadAt = [roadParams, roadPaths, roadIndex](float x, float z) -> TerrainRoadSample {
        return sampleRoadNetwork(x, z, roadParams, *roadPaths, roadIndex.get());
    };

    return context;
}

inline float sampleWaterHeight(float x, float z, const TerrainFieldContext& context)
{
    const TerrainParams& params = context.params;
    if (params.waterWaveAmplitude <= 0.0f) {
        return params.waterLevel;
    }

    const float n1 = (valueNoise2(x * params.waterWaveFrequency, z * params.waterWaveFrequency, params.seed + 700) * 2.0f) - 1.0f;
    const float n2 = (valueNoise2(x * params.waterWaveFrequency * 1.9f, z * params.waterWaveFrequency * 1.9f, params.seed + 937) * 2.0f) - 1.0f;
    return params.waterLevel + ((n1 * 0.7f) + (n2 * 0.3f)) * params.waterWaveAmplitude;
}

inline float sampleWaterHeight(float x, float z, const TerrainParams& params)
{
    const TerrainFieldContext context = createTerrainFieldContext(params);
    return sampleWaterHeight(x, z, context);
}

inline float sampleBaseSurfaceHeight(float x, float z, const TerrainFieldContext& context)
{
    const TerrainHydraulicSample hydraulic = sampleTerrainHydraulicMaps(x, z, context);
    return sampleProceduralSurfaceHeight(x, z, context) + hydraulic.erosion;
}

inline float sampleSurfaceHeight(float x, float z, const TerrainFieldContext& context)
{
    const TerrainParams& params = context.params;
    float surface = sampleBaseSurfaceHeight(x, z, context);
    if (context.sampleHeightDeltaAt) {
        surface += sanitize(context.sampleHeightDeltaAt(x, z), 0.0f);
    }
    if (context.sampleRoadAt) {
        const TerrainRoadSample road = context.sampleRoadAt(x, z);
        if (road.roadMask > 0.0f || road.cutMask > 0.0f || road.shoulderMask > 0.0f) {
            const float flatten = road.roadMask * params.roads.flattenStrength;
            const float cutfill = road.cutMask;
            const float delta = road.roadHeight - surface;
            if (delta < 0.0f) {
                surface = mix(surface, road.roadHeight, flatten + (cutfill * params.roads.cutStrength * (1.0f - flatten)));
            } else {
                surface = mix(surface, road.roadHeight, flatten + (cutfill * params.roads.fillStrength * (1.0f - flatten)));
            }
            if (road.roadMask > 0.0f) {
                surface += road.patchMask * params.roads.patchRaiseMeters;
                surface -= road.potholeMask * params.roads.potholeDepthMeters;
            }
        }
    }
    return clamp(surface, params.minY, params.maxY);
}

inline float sampleSurfaceHeight(float x, float z, const TerrainParams& params)
{
    const TerrainFieldContext context = createTerrainFieldContext(params);
    return sampleSurfaceHeight(x, z, context);
}

inline float sampleSdf(float x, float y, float z, const TerrainFieldContext& context)
{
    const TerrainParams& params = context.params;
    const float surface = sampleSurfaceHeight(x, z, context);
    float sdf = y - surface;

    if (params.caveEnabled && y >= params.caveMinY && y <= params.caveMaxY) {
        const float caveNoise = fbm3(
            x * params.caveFrequency,
            y * params.caveFrequency,
            z * params.caveFrequency,
            params.caveOctaves,
            params.caveLacunarity,
            params.caveGain,
            params.seed + 1701);
        const float caveDensity = caveNoise - params.caveThreshold;
        const float caveSdf = -caveDensity * params.caveStrength;
        sdf = std::max(sdf, -caveSdf);
    }

    if (!context.tunnelSeeds.empty()) {
        const Vec3 p { x, y, z };
        float minDistance = std::numeric_limits<float>::infinity();
        for (const TerrainTunnelSeed& tunnel : context.tunnelSeeds) {
            for (std::size_t i = 1; i < tunnel.points.size(); ++i) {
                const float distanceToWall = distancePointToSegment(p, tunnel.points[i - 1], tunnel.points[i]) - tunnel.radius;
                minDistance = std::min(minDistance, distanceToWall);
            }
        }
        if (std::isfinite(minDistance)) {
            sdf = std::max(sdf, -minDistance);
        }
    }

    if (context.sampleVolumetricAdditiveSdfAt) {
        const float additiveDistance = sanitize(context.sampleVolumetricAdditiveSdfAt(x, y, z), std::numeric_limits<float>::infinity());
        if (std::isfinite(additiveDistance)) {
            sdf = std::min(sdf, additiveDistance);
        }
    }

    if (context.sampleVolumetricSubtractiveSdfAt) {
        const float subtractiveDistance = sanitize(context.sampleVolumetricSubtractiveSdfAt(x, y, z), std::numeric_limits<float>::infinity());
        if (std::isfinite(subtractiveDistance)) {
            sdf = std::max(sdf, -subtractiveDistance);
        }
    }

    return sdf;
}

inline float sampleSdf(float x, float y, float z, const TerrainParams& params)
{
    const TerrainFieldContext context = createTerrainFieldContext(params);
    return sampleSdf(x, y, z, context);
}

inline Vec3 sampleTerrainNormal(float x, float y, float z, const TerrainFieldContext& context)
{
    constexpr float epsilon = 0.65f;
    const float dx = sampleSdf(x + epsilon, y, z, context) - sampleSdf(x - epsilon, y, z, context);
    const float dy = sampleSdf(x, y + epsilon, z, context) - sampleSdf(x, y - epsilon, z, context);
    const float dz = sampleSdf(x, y, z + epsilon, context) - sampleSdf(x, y, z - epsilon, context);
    return normalize({ dx, dy, dz }, { 0.0f, 1.0f, 0.0f });
}

inline Vec3 sampleTerrainNormal(float x, float y, float z, const TerrainParams& params)
{
    const TerrainFieldContext context = createTerrainFieldContext(params);
    return sampleTerrainNormal(x, y, z, context);
}

inline TerrainMaterialSample sampleTerrainMaterial(float x, float y, float z, const TerrainFieldContext& context)
{
    const TerrainParams& params = context.params;
    const float surface = sampleSurfaceHeight(x, z, context);
    const float waterHeight = sampleWaterHeight(x, z, context);
    const float hL = sampleSurfaceHeight(x - 1.5f, z, context);
    const float hR = sampleSurfaceHeight(x + 1.5f, z, context);
    const float hD = sampleSurfaceHeight(x, z - 1.5f, context);
    const float hU = sampleSurfaceHeight(x, z + 1.5f, context);
    const float slope = clamp(std::sqrt(((hR - hL) * (hR - hL)) + ((hU - hD) * (hU - hD))) * 0.18f, 0.0f, 1.0f);
    const TerrainHydraulicSample hydraulic = sampleTerrainHydraulicMaps(x, z, context);

    const float biomeNoise = (fbm2(x * params.biomeFrequency, z * params.biomeFrequency, 4, 2.0f, 0.54f, params.seed + 1201) * 2.0f) - 1.0f;
    const float elevation01 = clamp((surface - params.baseHeight + params.heightAmplitude) / std::max(1.0f, params.heightAmplitude * 2.0f), 0.0f, 1.0f);
    const float wetness = clamp(std::max((params.waterLevel + params.shorelineBand - surface) / std::max(0.1f, params.shorelineBand), (hydraulic.moisture * 0.72f) + (hydraulic.flow * 0.18f)), 0.0f, 1.0f);
    const float snowAltitude = clamp((surface - params.snowLine) / 700.0f, 0.0f, 1.0f);
    const float snowNoise = clamp(((valueNoise2(x * 0.0012f, z * 0.0012f, params.seed + 2141) * 2.0f) - 1.0f) * 0.14f, -0.14f, 0.14f);
    float rockWeight = clamp((slope - 0.1f) * 1.35f + (hydraulic.hardness * 0.24f) + (hydraulic.flow * 0.10f), 0.0f, 1.0f);
    float snow = clamp(snowAltitude + snowNoise + (rockWeight * 0.08f) - (wetness * 0.08f), 0.0f, 1.0f);
    float biomeBlend = clamp(((biomeNoise + 1.0f) * 0.5f) + (elevation01 * 0.16f) + (hydraulic.resource * 0.10f) - (hydraulic.hardness * 0.12f), 0.0f, 1.0f);

    float roadWeight = 0.0f;
    float shoulderWeight = 0.0f;
    float asphaltPatchWeight = 0.0f;
    if (context.sampleRoadAt) {
        const TerrainRoadSample road = context.sampleRoadAt(x, z);
        roadWeight = road.roadMask;
        shoulderWeight = road.shoulderMask * (1.0f - roadWeight);
        asphaltPatchWeight = road.patchMask * roadWeight;
        rockWeight *= (1.0f - roadWeight * 0.85f);
        snow *= (1.0f - roadWeight * 0.96f);
        biomeBlend *= (1.0f - roadWeight * 0.92f);
    }

    (void)y;
    return { surface, waterHeight, wetness, snow, rockWeight, biomeBlend, hydraulic.hardness, hydraulic.resource, clamp(-hydraulic.erosion * 0.18f, 0.0f, 1.0f), hydraulic.flow, roadWeight, shoulderWeight, asphaltPatchWeight };
}

inline Vec3 sampleTerrainWaterColor(float x, float y, float z, const TerrainFieldContext& context)
{
    const TerrainParams& params = context.params;
    const float surface = sampleSurfaceHeight(x, z, context);
    const float foam = clamp((params.waterLevel + 0.8f - surface) / std::max(0.1f, params.shorelineBand), 0.0f, 1.0f);
    const float waveTint = (valueNoise2(x * 0.021f, z * 0.021f, params.seed + 1431) * 2.0f) - 1.0f;
    const Vec3 waterBase = params.waterColor;
    (void)y;
    (void)z;
    return {
        clamp(waterBase.x + (waveTint * 0.02f) + (foam * 0.12f), 0.0f, 1.0f),
        clamp(waterBase.y + (waveTint * 0.04f) + (foam * 0.16f), 0.0f, 1.0f),
        clamp(waterBase.z + (waveTint * 0.06f) + (foam * 0.18f), 0.0f, 1.0f)
    };
}

inline Vec3 sampleTerrainColor(float x, float y, float z, const TerrainFieldContext& context)
{
    const TerrainParams& params = context.params;
    const TerrainMaterialSample material = sampleTerrainMaterial(x, y, z, context);
    const float depthBelowSurface = material.surfaceHeight - y;
    if (depthBelowSurface > 14.0f) {
        return { 0.30f, 0.26f, 0.23f };
    }

    const Vec3 grass { 0.24f, 0.48f, 0.25f };
    const Vec3 forest { 0.16f, 0.33f, 0.20f };
    const Vec3 sand { 0.70f, 0.64f, 0.47f };
    const Vec3 rock { 0.47f, 0.45f, 0.43f };
    const Vec3 mineral { 0.58f, 0.44f, 0.34f };
    const Vec3 snowColor { 0.88f, 0.89f, 0.92f };
    const Vec3 roadTint = clampColor3(params.roadColor, { 0.11f, 0.11f, 0.115f });
    const Vec3 asphaltDark {
        clamp(roadTint.x * 0.72f, 0.02f, 0.25f),
        clamp(roadTint.y * 0.72f, 0.02f, 0.25f),
        clamp(roadTint.z * 0.72f, 0.02f, 0.25f)
    };
    const Vec3 asphaltPatch {
        clamp(roadTint.x * 1.06f, 0.03f, 0.32f),
        clamp(roadTint.y * 1.06f, 0.03f, 0.32f),
        clamp(roadTint.z * 1.06f, 0.03f, 0.32f)
    };
    const Vec3 shoulderSoil { 0.36f, 0.34f, 0.32f };

    Vec3 base = lerp(grass, forest, material.biomeBlend);
    base = lerp(base, sand, material.wetness * 0.72f);
    base = lerp(base, rock, material.rockWeight);
    base = lerp(base, mineral, material.resourceWeight * material.hardnessWeight * 0.45f);
    const Vec3 dampened {
        clamp(base.x * (1.0f - (material.wetness * 0.18f)), 0.0f, 1.0f),
        clamp(base.y * (1.0f - (material.wetness * 0.12f)), 0.0f, 1.0f),
        clamp(base.z * (1.0f - (material.wetness * 0.08f)), 0.0f, 1.0f)
    };
    base = lerp(base, dampened, material.wetness * 0.55f);
    base = lerp(base, { 0.22f, 0.26f, 0.29f }, material.flowWeight * material.erosionWeight * 0.35f);
    base = lerp(base, snowColor, material.snowWeight);

    const float micro = (valueNoise2(x * 0.013f, z * 0.013f, params.seed + 331) * 2.0f) - 1.0f;
    base.x = clamp(base.x + (micro * 0.05f), 0.0f, 1.0f);
    base.y = clamp(base.y + (micro * 0.06f), 0.0f, 1.0f);
    base.z = clamp(base.z + (micro * 0.04f), 0.0f, 1.0f);

    if (material.shoulderWeight > 0.0f) {
        base = lerp(base, shoulderSoil, material.shoulderWeight * 0.72f);
    }
    if (material.roadWeight > 0.0f) {
        const float grain = ((fbm2(x * params.roads.asphaltNoiseScale, z * params.roads.asphaltNoiseScale, 4, 2.0f, 0.55f, params.seed + 12001) * 2.0f) - 1.0f);
        const float crack = std::pow(clamp(1.0f - std::fabs(((valueNoise2(x * 0.22f, z * 0.22f, params.seed + 12201) * 2.0f) - 1.0f)), 0.0f, 1.0f), 6.0f);
        Vec3 asphalt = asphaltDark + (Vec3 { grain, grain, grain } * 0.035f);
        asphalt = lerp(asphalt, asphaltPatch, material.asphaltPatchWeight * 0.85f);
        asphalt = asphalt - (Vec3 { 1.0f, 1.0f, 1.0f } * (crack * params.roads.crackStrength * 0.16f));
        asphalt = lerp(asphalt, asphalt * 0.58f, clamp(material.wetness * 0.55f, 0.0f, 0.55f));
        const float roadBlend = clamp((material.roadWeight * 1.18f) + (material.shoulderWeight * 0.12f), 0.0f, 1.0f);
        base = lerp(base, asphalt, roadBlend);
    }

    return base;
}

inline Vec3 sampleTerrainColor(float x, float y, float z, const TerrainParams& params)
{
    const TerrainFieldContext context = createTerrainFieldContext(params);
    return sampleTerrainColor(x, y, z, context);
}

inline void addFace(Model& model, const std::vector<int>& indices, const Vec3& color)
{
    model.faces.push_back({ indices });
    model.faceColors.push_back(color);
}

inline void addColoredTriangle(Model& model, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& color)
{
    const int base = static_cast<int>(model.vertices.size());
    const Vec3 normal = normalize(cross(b - a, c - a), { 0.0f, 1.0f, 0.0f });
    model.vertices.push_back(a);
    model.vertices.push_back(b);
    model.vertices.push_back(c);
    model.vertexNormals.push_back(normal);
    model.vertexNormals.push_back(normal);
    model.vertexNormals.push_back(normal);
    addFace(model, { base, base + 1, base + 2 }, color);
}

inline void addColoredQuad(Model& model, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, const Vec3& color)
{
    const int base = static_cast<int>(model.vertices.size());
    const Vec3 normal = normalize(cross(b - a, c - a), { 0.0f, 1.0f, 0.0f });
    model.vertices.push_back(a);
    model.vertices.push_back(b);
    model.vertices.push_back(c);
    model.vertices.push_back(d);
    model.vertexNormals.push_back(normal);
    model.vertexNormals.push_back(normal);
    model.vertexNormals.push_back(normal);
    model.vertexNormals.push_back(normal);
    addFace(model, { base, base + 1, base + 2 }, color);
    addFace(model, { base, base + 2, base + 3 }, color);
}

inline void appendModel(Model& target, const Model& source)
{
    const int baseVertex = static_cast<int>(target.vertices.size());
    target.vertices.insert(target.vertices.end(), source.vertices.begin(), source.vertices.end());
    for (std::size_t faceIndex = 0; faceIndex < source.faces.size(); ++faceIndex) {
        Face face;
        face.indices.reserve(source.faces[faceIndex].indices.size());
        for (const int index : source.faces[faceIndex].indices) {
            face.indices.push_back(baseVertex + index);
        }
        target.faces.push_back(std::move(face));
        target.faceColors.push_back(faceIndex < source.faceColors.size() ? source.faceColors[faceIndex] : Vec3 { 0.45f, 0.58f, 0.36f });
    }
}

inline bool pointInsideHole(float x, float z, const TerrainPatchBounds& bounds)
{
    if (!bounds.hasHole) {
        return false;
    }
    return x >= bounds.holeX0 && x <= bounds.holeX1 && z >= bounds.holeZ0 && z <= bounds.holeZ1;
}

inline float terrainPatchTargetSpan(const TerrainParams& params, float requestedStep)
{
    const float lod01Threshold = (params.lod0CellSize + params.lod1CellSize) * 0.5f;
    const float lod12Threshold = (params.lod1CellSize + params.lod2CellSize) * 0.5f;
    const float chunkSize = std::max(8.0f, params.chunkSize);
    if (requestedStep <= lod01Threshold) {
        return chunkSize * static_cast<float>(std::max(params.lod0ChunkScale, 1));
    }
    if (requestedStep <= lod12Threshold) {
        return chunkSize * static_cast<float>(std::max(params.lod1ChunkScale, std::max(params.lod0ChunkScale, 1)));
    }
    return chunkSize * static_cast<float>(std::max(params.lod2ChunkScale, std::max(params.lod1ChunkScale, 1)));
}

inline int terrainPatchAxisCellBudget(const TerrainParams& params, float requestedStep, float span)
{
    const float safeSpan = std::max(1.0f, span);
    const float targetSpan = std::max(1.0f, terrainPatchTargetSpan(params, requestedStep));
    const int patchCount = std::max(1, static_cast<int>(std::ceil(safeSpan / targetSpan)));
    return std::max(2, params.maxChunkCellsPerAxis) * patchCount;
}

inline float terrainPatchAxisStep(const TerrainParams& params, float requestedStep, float span)
{
    const int axisBudget = terrainPatchAxisCellBudget(params, requestedStep, span);
    return std::max(requestedStep, std::max(1.0f, span) / static_cast<float>(axisBudget));
}

inline void expandTerrainVerticalBounds(TerrainVerticalBoundsSample& bounds, float minY, float maxY)
{
    if (!std::isfinite(minY) || !std::isfinite(maxY)) {
        return;
    }
    if (maxY < minY) {
        std::swap(minY, maxY);
    }
    if (!bounds.valid) {
        bounds.valid = true;
        bounds.minY = minY;
        bounds.maxY = maxY;
        return;
    }
    bounds.minY = std::min(bounds.minY, minY);
    bounds.maxY = std::max(bounds.maxY, maxY);
}

inline bool terrainPatchOverlapsTunnelPath(const TerrainPatchBounds& bounds, const TerrainTunnelSeed& tunnel, float padding = 0.0f)
{
    const float expandedX0 = bounds.x0 - padding;
    const float expandedX1 = bounds.x1 + padding;
    const float expandedZ0 = bounds.z0 - padding;
    const float expandedZ1 = bounds.z1 + padding;
    for (std::size_t index = 1; index < tunnel.points.size(); ++index) {
        const Vec3& a = tunnel.points[index - 1];
        const Vec3& b = tunnel.points[index];
        const float radius = tunnel.radius + padding;
        const float minX = std::min(a.x, b.x) - radius;
        const float maxX = std::max(a.x, b.x) + radius;
        const float minZ = std::min(a.z, b.z) - radius;
        const float maxZ = std::max(a.z, b.z) + radius;
        if (maxX >= expandedX0 && minX <= expandedX1 && maxZ >= expandedZ0 && minZ <= expandedZ1) {
            return true;
        }
    }
    return false;
}

inline TerrainVerticalBoundsSample sampleTerrainPatchSurfaceBounds(
    const TerrainPatchBounds& bounds,
    const TerrainFieldContext& context,
    float cellSize)
{
    TerrainVerticalBoundsSample out;
    const TerrainParams& params = context.params;
    const float spanX = std::max(1.0f, bounds.x1 - bounds.x0);
    const float spanZ = std::max(1.0f, bounds.z1 - bounds.z0);
    const float requestedStep = std::max(1.0f, sanitize(cellSize, params.lod1CellSize));
    const float stepX = terrainPatchAxisStep(params, requestedStep, spanX);
    const float stepZ = terrainPatchAxisStep(params, requestedStep, spanZ);
    const int nx = std::max(2, static_cast<int>(std::floor(spanX / stepX)));
    const int nz = std::max(2, static_cast<int>(std::floor(spanZ / stepZ)));
    const float xStep = spanX / static_cast<float>(nx);
    const float zStep = spanZ / static_cast<float>(nz);

    for (int iz = 0; iz <= nz; ++iz) {
        const float z = bounds.z0 + (static_cast<float>(iz) * zStep);
        for (int ix = 0; ix <= nx; ++ix) {
            const float x = bounds.x0 + (static_cast<float>(ix) * xStep);
            if (pointInsideHole(x, z, bounds)) {
                continue;
            }
            const float y = sampleSurfaceHeight(x, z, context);
            expandTerrainVerticalBounds(out, y, y);
        }
    }
    return out;
}

inline TerrainVerticalBoundsSample estimateTerrainPatchVerticalBounds(
    const TerrainPatchBounds& bounds,
    const TerrainFieldContext& context,
    float cellSize)
{
    const TerrainParams& params = context.params;
    TerrainVerticalBoundsSample out = sampleTerrainPatchSurfaceBounds(bounds, context, cellSize);
    const float shellMargin = std::max({ 12.0f, std::max(2.0f, cellSize * 2.5f), params.skirtDepth * 0.5f });

    const float surfaceMin = out.valid ? out.minY : params.baseHeight;
    const float surfaceMax = out.valid ? out.maxY : params.baseHeight;
    expandTerrainVerticalBounds(out, surfaceMin - shellMargin, surfaceMax + shellMargin);

    if (params.caveEnabled) {
        expandTerrainVerticalBounds(out, params.caveMinY - shellMargin, params.caveMaxY + shellMargin);
    }

    for (const TerrainTunnelSeed& tunnel : context.tunnelSeeds) {
        if (!terrainPatchOverlapsTunnelPath(bounds, tunnel, shellMargin)) {
            continue;
        }
        for (const Vec3& point : tunnel.points) {
            expandTerrainVerticalBounds(
                out,
                point.y - tunnel.radius - shellMargin,
                point.y + tunnel.radius + shellMargin);
        }
    }

    if (context.sampleWorldVolumetricBoundsInBounds) {
        const TerrainVerticalBoundsSample worldBounds = context.sampleWorldVolumetricBoundsInBounds(
            bounds.x0 - shellMargin,
            bounds.z0 - shellMargin,
            bounds.x1 + shellMargin,
            bounds.z1 + shellMargin);
        if (worldBounds.valid) {
            expandTerrainVerticalBounds(
                out,
                worldBounds.minY - shellMargin,
                worldBounds.maxY + shellMargin);
        }
    }

    if (!out.valid) {
        expandTerrainVerticalBounds(out, params.baseHeight - shellMargin, params.baseHeight + shellMargin);
    }

    const float quantizationStep = std::max(0.5f, sanitize(cellSize, params.lod0CellSize));
    out.minY = std::floor(out.minY / quantizationStep) * quantizationStep;
    out.maxY = std::ceil(out.maxY / quantizationStep) * quantizationStep;
    if (out.maxY <= out.minY + quantizationStep) {
        out.maxY = out.minY + quantizationStep;
    }
    return out;
}

inline TerrainVolumeBounds buildAdaptiveTerrainVolumeBounds(
    const TerrainPatchBounds& bounds,
    const TerrainFieldContext& context,
    float cellSize,
    float overlap = 0.0f)
{
    const TerrainVerticalBoundsSample verticalBounds = estimateTerrainPatchVerticalBounds(bounds, context, cellSize);
    return {
        bounds.x0 - overlap,
        bounds.x1 + overlap,
        verticalBounds.minY,
        verticalBounds.maxY,
        bounds.z0 - overlap,
        bounds.z1 + overlap
    };
}

inline void appendTerrainWater(Model& model, const TerrainFieldContext& context, const TerrainPatchBounds& bounds, float step)
{
    const TerrainParams& params = context.params;
    const float spanX = std::max(1.0f, bounds.x1 - bounds.x0);
    const float spanZ = std::max(1.0f, bounds.z1 - bounds.z0);
    const float requestedStep = std::max(1.0f, sanitize(step, params.lod1CellSize));
    const float stepX = terrainPatchAxisStep(params, requestedStep, spanX);
    const float stepZ = terrainPatchAxisStep(params, requestedStep, spanZ);
    const int nx = std::max(2, static_cast<int>(std::floor(spanX / stepX)));
    const int nz = std::max(2, static_cast<int>(std::floor(spanZ / stepZ)));
    const float xStep = spanX / static_cast<float>(nx);
    const float zStep = spanZ / static_cast<float>(nz);

    for (int iz = 0; iz < nz; ++iz) {
        const float z0 = bounds.z0 + (static_cast<float>(iz) * zStep);
        const float z1 = z0 + zStep;
        for (int ix = 0; ix < nx; ++ix) {
            const float x0 = bounds.x0 + (static_cast<float>(ix) * xStep);
            const float x1 = x0 + xStep;
            const float cellCenterX = (x0 + x1) * 0.5f;
            const float cellCenterZ = (z0 + z1) * 0.5f;
            if (pointInsideHole(cellCenterX, cellCenterZ, bounds)) {
                continue;
            }

            const float s00 = sampleSurfaceHeight(x0, z0, context);
            const float s10 = sampleSurfaceHeight(x1, z0, context);
            const float s01 = sampleSurfaceHeight(x0, z1, context);
            const float s11 = sampleSurfaceHeight(x1, z1, context);
            const float w00 = sampleWaterHeight(x0, z0, context);
            const float w10 = sampleWaterHeight(x1, z0, context);
            const float w01 = sampleWaterHeight(x0, z1, context);
            const float w11 = sampleWaterHeight(x1, z1, context);
            if (w00 <= (s00 + 0.12f) &&
                w10 <= (s10 + 0.12f) &&
                w01 <= (s01 + 0.12f) &&
                w11 <= (s11 + 0.12f)) {
                continue;
            }

            const float centerY = (w00 + w10 + w01 + w11) * 0.25f;
            const Vec3 color = sampleTerrainWaterColor(cellCenterX, centerY, cellCenterZ, context);
            addColoredQuad(
                model,
                { x0, w00, z0 },
                { x0, w01, z1 },
                { x1, w11, z1 },
                { x1, w10, z0 },
                color);
        }
    }
}

inline void appendSkirtQuads(Model& model, const TerrainFieldContext& context, const TerrainPatchBounds& bounds, float step, float depth)
{
    const TerrainParams& params = context.params;
    const float skirtDepth = std::max(2.0f, sanitize(depth, params.skirtDepth));
    const float edgeStep = std::max(2.0f, sanitize(step, params.lod1CellSize));

    auto addEdgeQuad = [&](const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d) {
        const Vec3 color = sampleTerrainColor((a.x + b.x + c.x + d.x) * 0.25f, (a.y + b.y + c.y + d.y) * 0.25f, (a.z + b.z + c.z + d.z) * 0.25f, context);
        addColoredTriangle(model, a, b, c, color);
        addColoredTriangle(model, a, c, d, color);
    };

    for (float x = bounds.x0; x < bounds.x1; x += edgeStep) {
        const float x2 = std::min(bounds.x1, x + edgeStep);
        const float yA = sampleSurfaceHeight(x, bounds.z0, context);
        const float yB = sampleSurfaceHeight(x2, bounds.z0, context);
        addEdgeQuad(
            { x, yA, bounds.z0 },
            { x2, yB, bounds.z0 },
            { x2, yB - skirtDepth, bounds.z0 },
            { x, yA - skirtDepth, bounds.z0 });

        const float yC = sampleSurfaceHeight(x, bounds.z1, context);
        const float yD = sampleSurfaceHeight(x2, bounds.z1, context);
        addEdgeQuad(
            { x2, yD, bounds.z1 },
            { x, yC, bounds.z1 },
            { x, yC - skirtDepth, bounds.z1 },
            { x2, yD - skirtDepth, bounds.z1 });
    }

    for (float z = bounds.z0; z < bounds.z1; z += edgeStep) {
        const float z2 = std::min(bounds.z1, z + edgeStep);
        const float yA = sampleSurfaceHeight(bounds.x0, z, context);
        const float yB = sampleSurfaceHeight(bounds.x0, z2, context);
        addEdgeQuad(
            { bounds.x0, yB, z2 },
            { bounds.x0, yA, z },
            { bounds.x0, yA - skirtDepth, z },
            { bounds.x0, yB - skirtDepth, z2 });

        const float yC = sampleSurfaceHeight(bounds.x1, z, context);
        const float yD = sampleSurfaceHeight(bounds.x1, z2, context);
        addEdgeQuad(
            { bounds.x1, yC, z },
            { bounds.x1, yD, z2 },
            { bounds.x1, yD - skirtDepth, z2 },
            { bounds.x1, yC - skirtDepth, z });
    }
}

inline Model buildSurfaceTerrainPatch(const TerrainFieldContext& context, const TerrainPatchBounds& bounds, float cellSize)
{
    const TerrainParams& params = context.params;
    Model model;

    const float spanX = std::max(1.0f, bounds.x1 - bounds.x0);
    const float spanZ = std::max(1.0f, bounds.z1 - bounds.z0);
    const float requestedStep = std::max(1.0f, sanitize(cellSize, params.lod1CellSize));
    const float stepX = terrainPatchAxisStep(params, requestedStep, spanX);
    const float stepZ = terrainPatchAxisStep(params, requestedStep, spanZ);
    const int nx = std::max(2, static_cast<int>(std::floor(spanX / stepX)));
    const int nz = std::max(2, static_cast<int>(std::floor(spanZ / stepZ)));
    const float xStep = spanX / static_cast<float>(nx);
    const float zStep = spanZ / static_cast<float>(nz);

    std::vector<int> grid(static_cast<std::size_t>((nx + 1) * (nz + 1)), 0);
    std::vector<float> heights(static_cast<std::size_t>((nx + 1) * (nz + 1)), 0.0f);
    auto gridIndex = [nx](int ix, int iz) {
        return static_cast<std::size_t>(iz * (nx + 1) + ix);
    };

    for (int iz = 0; iz <= nz; ++iz) {
        const float z = bounds.z0 + (static_cast<float>(iz) * zStep);
        for (int ix = 0; ix <= nx; ++ix) {
            const float x = bounds.x0 + (static_cast<float>(ix) * xStep);
            const float y = sampleSurfaceHeight(x, z, context);
            grid[gridIndex(ix, iz)] = static_cast<int>(model.vertices.size());
            heights[gridIndex(ix, iz)] = y;
            model.vertices.push_back({ x, y, z });
        }
    }

    model.vertexNormals.resize(model.vertices.size(), { 0.0f, 1.0f, 0.0f });
    for (int iz = 0; iz <= nz; ++iz) {
        const int iz0 = std::max(0, iz - 1);
        const int iz1 = std::min(nz, iz + 1);
        for (int ix = 0; ix <= nx; ++ix) {
            const int ix0 = std::max(0, ix - 1);
            const int ix1 = std::min(nx, ix + 1);
            const float hL = heights[gridIndex(ix0, iz)];
            const float hR = heights[gridIndex(ix1, iz)];
            const float hD = heights[gridIndex(ix, iz0)];
            const float hU = heights[gridIndex(ix, iz1)];
            const float dx = xStep * static_cast<float>(std::max(1, ix1 - ix0));
            const float dz = zStep * static_cast<float>(std::max(1, iz1 - iz0));
            const Vec3 tangentZ { 0.0f, hU - hD, dz };
            const Vec3 tangentX { dx, hR - hL, 0.0f };
            model.vertexNormals[static_cast<std::size_t>(grid[gridIndex(ix, iz)])] =
                normalize(cross(tangentZ, tangentX), { 0.0f, 1.0f, 0.0f });
        }
    }

    auto addSurfaceTri = [&](int ia, int ib, int ic) {
        const Vec3& a = model.vertices[static_cast<std::size_t>(ia)];
        const Vec3& b = model.vertices[static_cast<std::size_t>(ib)];
        const Vec3& c = model.vertices[static_cast<std::size_t>(ic)];
        const float centerX = (a.x + b.x + c.x) / 3.0f;
        const float centerY = (a.y + b.y + c.y) / 3.0f;
        const float centerZ = (a.z + b.z + c.z) / 3.0f;
        if (pointInsideHole(centerX, centerZ, bounds)) {
            return;
        }
        addFace(model, { ia, ib, ic }, sampleTerrainColor(centerX, centerY, centerZ, context));
    };

    for (int iz = 0; iz < nz; ++iz) {
        for (int ix = 0; ix < nx; ++ix) {
            const int i00 = grid[gridIndex(ix, iz)];
            const int i10 = grid[gridIndex(ix + 1, iz)];
            const int i01 = grid[gridIndex(ix, iz + 1)];
            const int i11 = grid[gridIndex(ix + 1, iz + 1)];
            addSurfaceTri(i00, i11, i10);
            addSurfaceTri(i00, i01, i11);
        }
    }

    if (params.enableSkirts) {
        appendSkirtQuads(model, context, bounds, requestedStep, params.skirtDepth);
    }
    return model;
}

inline bool shouldFlipTriangle(const Vec3& a, const Vec3& b, const Vec3& c, const TerrainFieldContext& context)
{
    const Vec3 triNormal = normalize(cross(b - a, c - a), { 0.0f, 1.0f, 0.0f });
    const Vec3 center {
        (a.x + b.x + c.x) / 3.0f,
        (a.y + b.y + c.y) / 3.0f,
        (a.z + b.z + c.z) / 3.0f
    };
    const Vec3 surfaceNormal = sampleTerrainNormal(center.x, center.y, center.z, context);
    return dot(triNormal, surfaceNormal) < 0.0f;
}

inline Vec3 interpolateIso(const Vec3& p1, const Vec3& p2, float v1, float v2, float isoLevel)
{
    const float denom = v2 - v1;
    float t = 0.5f;
    if (std::fabs(denom) > 1.0e-8f) {
        t = clamp((isoLevel - v1) / denom, 0.0f, 1.0f);
    }
    return lerp(p1, p2, t);
}

inline void emitTerrainIsoTriangle(Model& model, Vec3 p1, Vec3 p2, Vec3 p3, const TerrainFieldContext& context)
{
    if (shouldFlipTriangle(p1, p2, p3, context)) {
        std::swap(p2, p3);
    }
    const Vec3 center {
        (p1.x + p2.x + p3.x) / 3.0f,
        (p1.y + p2.y + p3.y) / 3.0f,
        (p1.z + p2.z + p3.z) / 3.0f
    };
    addColoredTriangle(model, p1, p2, p3, sampleTerrainColor(center.x, center.y, center.z, context));
}

inline void polygonizeTerrainTetra(
    Model& model,
    const std::array<Vec3, 4>& positions,
    const std::array<float, 4>& values,
    float isoLevel,
    const TerrainFieldContext& context)
{
    std::array<int, 4> inside {};
    std::array<int, 4> outside {};
    int insideCount = 0;
    int outsideCount = 0;
    for (int i = 0; i < 4; ++i) {
        if (values[static_cast<std::size_t>(i)] <= isoLevel) {
            inside[static_cast<std::size_t>(insideCount++)] = i;
        } else {
            outside[static_cast<std::size_t>(outsideCount++)] = i;
        }
    }

    if (insideCount == 0 || insideCount == 4) {
        return;
    }

    if (insideCount == 1) {
        const int a = inside[0];
        const int b = outside[0];
        const int c = outside[1];
        const int d = outside[2];
        emitTerrainIsoTriangle(
            model,
            interpolateIso(positions[static_cast<std::size_t>(a)], positions[static_cast<std::size_t>(b)], values[static_cast<std::size_t>(a)], values[static_cast<std::size_t>(b)], isoLevel),
            interpolateIso(positions[static_cast<std::size_t>(a)], positions[static_cast<std::size_t>(c)], values[static_cast<std::size_t>(a)], values[static_cast<std::size_t>(c)], isoLevel),
            interpolateIso(positions[static_cast<std::size_t>(a)], positions[static_cast<std::size_t>(d)], values[static_cast<std::size_t>(a)], values[static_cast<std::size_t>(d)], isoLevel),
            context);
        return;
    }

    if (insideCount == 3) {
        const int a = outside[0];
        const int b = inside[0];
        const int c = inside[1];
        const int d = inside[2];
        emitTerrainIsoTriangle(
            model,
            interpolateIso(positions[static_cast<std::size_t>(a)], positions[static_cast<std::size_t>(b)], values[static_cast<std::size_t>(a)], values[static_cast<std::size_t>(b)], isoLevel),
            interpolateIso(positions[static_cast<std::size_t>(a)], positions[static_cast<std::size_t>(d)], values[static_cast<std::size_t>(a)], values[static_cast<std::size_t>(d)], isoLevel),
            interpolateIso(positions[static_cast<std::size_t>(a)], positions[static_cast<std::size_t>(c)], values[static_cast<std::size_t>(a)], values[static_cast<std::size_t>(c)], isoLevel),
            context);
        return;
    }

    const int a = inside[0];
    const int b = inside[1];
    const int c = outside[0];
    const int d = outside[1];
    const Vec3 p1 = interpolateIso(positions[static_cast<std::size_t>(a)], positions[static_cast<std::size_t>(c)], values[static_cast<std::size_t>(a)], values[static_cast<std::size_t>(c)], isoLevel);
    const Vec3 p2 = interpolateIso(positions[static_cast<std::size_t>(a)], positions[static_cast<std::size_t>(d)], values[static_cast<std::size_t>(a)], values[static_cast<std::size_t>(d)], isoLevel);
    const Vec3 p3 = interpolateIso(positions[static_cast<std::size_t>(b)], positions[static_cast<std::size_t>(c)], values[static_cast<std::size_t>(b)], values[static_cast<std::size_t>(c)], isoLevel);
    const Vec3 p4 = interpolateIso(positions[static_cast<std::size_t>(b)], positions[static_cast<std::size_t>(d)], values[static_cast<std::size_t>(b)], values[static_cast<std::size_t>(d)], isoLevel);
    emitTerrainIsoTriangle(model, p1, p3, p2, context);
    emitTerrainIsoTriangle(model, p2, p3, p4, context);
}

inline Model buildVolumetricTerrainPatch(const TerrainFieldContext& context, const TerrainVolumeBounds& bounds, float cellSize)
{
    static constexpr std::array<std::array<int, 4>, 6> tetrahedra {{
        { 0, 1, 3, 5 },
        { 0, 3, 4, 5 },
        { 1, 2, 3, 5 },
        { 2, 3, 5, 6 },
        { 3, 4, 5, 7 },
        { 3, 5, 6, 7 }
    }};
    static constexpr std::array<Vec3, 8> cubeOffsets {{
        { 0.0f, 0.0f, 0.0f },
        { 1.0f, 0.0f, 0.0f },
        { 1.0f, 0.0f, 1.0f },
        { 0.0f, 0.0f, 1.0f },
        { 0.0f, 1.0f, 0.0f },
        { 1.0f, 1.0f, 0.0f },
        { 1.0f, 1.0f, 1.0f },
        { 0.0f, 1.0f, 1.0f }
    }};

    const TerrainParams& params = context.params;
    const float spanX = std::max(1.0f, bounds.x1 - bounds.x0);
    const float spanY = std::max(1.0f, bounds.y1 - bounds.y0);
    const float spanZ = std::max(1.0f, bounds.z1 - bounds.z0);
    const float requestedStep = std::max(0.5f, sanitize(cellSize, params.lod0CellSize));
    const float maxSpan = std::max(spanX, std::max(spanY, spanZ));
    const float step = std::max(requestedStep, maxSpan / static_cast<float>(params.maxChunkCellsPerAxis));
    const int nx = std::max(1, static_cast<int>(std::floor(spanX / step)));
    const int ny = std::max(1, static_cast<int>(std::floor(spanY / step)));
    const int nz = std::max(1, static_cast<int>(std::floor(spanZ / step)));

    Model model;
    for (int iy = 0; iy < ny; ++iy) {
        for (int ix = 0; ix < nx; ++ix) {
            for (int iz = 0; iz < nz; ++iz) {
                std::array<Vec3, 8> cubePositions {};
                std::array<float, 8> cubeValues {};
                bool hasInside = false;
                bool hasOutside = false;
                for (int ci = 0; ci < 8; ++ci) {
                    const Vec3 offset = cubeOffsets[static_cast<std::size_t>(ci)];
                    const float px = bounds.x0 + (static_cast<float>(ix) + offset.x) * step;
                    const float py = bounds.y0 + (static_cast<float>(iy) + offset.y) * step;
                    const float pz = bounds.z0 + (static_cast<float>(iz) + offset.z) * step;
                    cubePositions[static_cast<std::size_t>(ci)] = { px, py, pz };
                    const float sdf = sampleSdf(px, py, pz, context);
                    cubeValues[static_cast<std::size_t>(ci)] = sdf;
                    hasInside = hasInside || (sdf <= 0.0f);
                    hasOutside = hasOutside || (sdf > 0.0f);
                }
                if (!(hasInside && hasOutside)) {
                    continue;
                }

                for (const auto& tetra : tetrahedra) {
                    std::array<Vec3, 4> positions {
                        cubePositions[static_cast<std::size_t>(tetra[0])],
                        cubePositions[static_cast<std::size_t>(tetra[1])],
                        cubePositions[static_cast<std::size_t>(tetra[2])],
                        cubePositions[static_cast<std::size_t>(tetra[3])]
                    };
                    std::array<float, 4> values {
                        cubeValues[static_cast<std::size_t>(tetra[0])],
                        cubeValues[static_cast<std::size_t>(tetra[1])],
                        cubeValues[static_cast<std::size_t>(tetra[2])],
                        cubeValues[static_cast<std::size_t>(tetra[3])]
                    };
                    polygonizeTerrainTetra(model, positions, values, 0.0f, context);
                }
            }
        }
    }
    return model;
}

inline TerrainVisualBuildResult buildTerrainVisualModels(const Vec3& center, const TerrainFieldContext& context)
{
    const TerrainParams& params = context.params;
    TerrainVisualBuildResult result;
    result.nearHalfExtent = std::max(params.gameplayRadiusMeters, params.chunkSize * static_cast<float>(std::max(params.lod0Radius, 4)));
    result.farHalfExtent = std::max(params.horizonRadiusMeters, result.nearHalfExtent + params.chunkSize);
    result.anchorSpacing = std::max(32.0f, params.chunkSize * 0.5f);

    const float anchorX = std::round(center.x / result.anchorSpacing) * result.anchorSpacing;
    const float anchorZ = std::round(center.z / result.anchorSpacing) * result.anchorSpacing;

    const TerrainPatchBounds nearBounds {
        anchorX - result.nearHalfExtent,
        anchorX + result.nearHalfExtent,
        anchorZ - result.nearHalfExtent,
        anchorZ + result.nearHalfExtent,
        false,
        0.0f,
        0.0f,
        0.0f,
        0.0f
    };

    if (params.surfaceOnlyMeshing) {
        result.nearModel = buildSurfaceTerrainPatch(context, nearBounds, params.lod0CellSize);
        appendTerrainWater(result.nearModel, context, nearBounds, params.lod0CellSize);
    } else {
        const float overlap = params.lod0CellSize;
        const TerrainVolumeBounds nearVolume = buildAdaptiveTerrainVolumeBounds(nearBounds, context, params.lod0CellSize, overlap);
        result.nearModel = buildVolumetricTerrainPatch(context, nearVolume, params.lod0CellSize);
        appendTerrainWater(result.nearModel, context, nearBounds, params.lod0CellSize);
        if (params.enableSkirts) {
            appendSkirtQuads(result.nearModel, context, nearBounds, params.lod0CellSize, params.skirtDepth);
        }
    }

    Model farModel;
    const float midHalfExtent = std::max(params.midFieldRadiusMeters, result.nearHalfExtent + params.chunkSize);
    if (midHalfExtent > result.nearHalfExtent + 1.0f) {
        TerrainPatchBounds midBounds {
            anchorX - midHalfExtent,
            anchorX + midHalfExtent,
            anchorZ - midHalfExtent,
            anchorZ + midHalfExtent,
            true,
            nearBounds.x0,
            nearBounds.x1,
            nearBounds.z0,
            nearBounds.z1
        };
        appendModel(farModel, buildSurfaceTerrainPatch(context, midBounds, params.lod1CellSize));
        appendTerrainWater(farModel, context, midBounds, params.lod1CellSize);
    }
    if (result.farHalfExtent > midHalfExtent + 1.0f) {
        TerrainPatchBounds horizonBounds {
            anchorX - result.farHalfExtent,
            anchorX + result.farHalfExtent,
            anchorZ - result.farHalfExtent,
            anchorZ + result.farHalfExtent,
            true,
            anchorX - midHalfExtent,
            anchorX + midHalfExtent,
            anchorZ - midHalfExtent,
            anchorZ + midHalfExtent
        };
        appendModel(farModel, buildSurfaceTerrainPatch(context, horizonBounds, params.lod2CellSize));
        appendTerrainWater(farModel, context, horizonBounds, params.lod2CellSize);
    }
    result.farModel = std::move(farModel);
    return result;
}

inline Model buildTerrainPatch(const Vec3& center, const TerrainParams& params, int rings = 20, float ringSpacing = 55.0f)
{
    const TerrainFieldContext context = createTerrainFieldContext(params);
    const float halfExtent = std::max(96.0f, std::max(static_cast<float>(rings) * ringSpacing, context.params.gameplayRadiusMeters * 0.5f));
    return buildSurfaceTerrainPatch(
        context,
        {
            center.x - halfExtent,
            center.x + halfExtent,
            center.z - halfExtent,
            center.z + halfExtent,
            false,
            0.0f,
            0.0f,
            0.0f,
            0.0f
        },
        context.params.lod0CellSize);
}

struct WindState {
    float angle = 0.0f;
    float speed = 3.0f;
    float targetAngle = 0.0f;
    float targetSpeed = 3.0f;
    float gustAmplitude = 0.45f;
    float gustFrequency = 0.12f;
    float gustPhase = 0.0f;
    float nextTargetAt = 0.0f;
};

struct CloudPuff {
    Vec3 baseOffset {};
    Vec3 offset {};
    float baseScale = 32.0f;
    float scale = 32.0f;
    float baseStretchY = 0.65f;
    float stretchY = 0.65f;
    float driftPhase = 0.0f;
    float driftAmplitude = 1.0f;
    float yaw = 0.0f;
    Vec3 color { 0.98f, 0.99f, 1.0f };
};

struct CloudGroup {
    Vec3 center {};
    float radius = 120.0f;
    float targetRadius = 120.0f;
    float verticalScale = 0.65f;
    float targetVerticalScale = 0.65f;
    float density = 1.0f;
    float targetDensity = 1.0f;
    float driftScale = 1.0f;
    float evolutionRate = 1.0f;
    float humidity = 0.62f;
    float opticalDepth = 0.52f;
    float layerBase = 560.0f;
    float layerTop = 980.0f;
    float alpha = 0.78f;
    float nextMorphAt = 0.0f;
    float nextMeshRebuildAt = 0.0f;
    float renderRadius = 160.0f;
    std::uint32_t noiseSeed = 1u;
    std::uint64_t stableId = 0u;
    std::uint64_t pendingBuildId = 0u;
    std::uint64_t lastAppliedBuildId = 0u;
    TerrainVolumeBounds localBounds {};
    Model volumeModel;
    bool meshDirty = true;
    std::vector<CloudPuff> puffs;
};

struct CloudField {
    float spawnRadius = 2600.0f;
    float baseHeight = 620.0f;
    float layerThickness = 420.0f;
    float coverage = 0.72f;
    float humidityResponse = 0.78f;
    int groupCount = 14;
    std::size_t maxMeshBuildResultsPerUpdate = 4u;
    std::vector<CloudGroup> groups;
};

inline float randomRange(std::mt19937& rng, float minValue, float maxValue)
{
    std::uniform_real_distribution<float> distribution(minValue, maxValue);
    return distribution(rng);
}

inline int randomRangeInt(std::mt19937& rng, int minValue, int maxValue)
{
    std::uniform_int_distribution<int> distribution(minValue, maxValue);
    return distribution(rng);
}

inline std::uint64_t nextCloudGroupStableId()
{
    static std::atomic<std::uint64_t> nextId { 1u };
    return nextId.fetch_add(1u, std::memory_order_relaxed);
}

inline std::uint64_t nextCloudMeshBuildId()
{
    static std::atomic<std::uint64_t> nextId { 1u };
    return nextId.fetch_add(1u, std::memory_order_relaxed);
}

inline float cloudLayerWeight(const CloudField& cloudField, float altitudeMeters)
{
    const float halfThickness = std::max(110.0f, cloudField.layerThickness * 0.5f);
    const float normalizedDistance = std::fabs(altitudeMeters - cloudField.baseHeight) / halfThickness;
    return clamp(1.0f - normalizedDistance, 0.0f, 1.0f);
}

inline float estimateCloudHumidityFromAtmosphere(const AtmosphereSample& atmosphere, float phase)
{
    const float densityFactor = clamp((atmosphere.sigma - 0.24f) / 0.90f, 0.0f, 1.0f);
    const float temperatureC = atmosphere.temperatureK - 273.15f;
    const float warmthPenalty = clamp((temperatureC - 16.0f) / 28.0f, 0.0f, 1.0f);
    const float wave = 0.5f + (0.5f * std::sin(phase));
    return clamp((densityFactor * 0.78f) + (wave * 0.18f) - (warmthPenalty * 0.10f), 0.05f, 0.99f);
}

inline Vec3 clampCloudColor(const Vec3& color)
{
    return {
        clamp(color.x, 0.18f, 1.22f),
        clamp(color.y, 0.18f, 1.22f),
        clamp(color.z, 0.20f, 1.24f)
    };
}

inline void pickNextWindTarget(WindState& windState, std::mt19937& rng, float nowSeconds = 0.0f)
{
    windState.targetAngle = wrapAngle(randomRange(rng, -kPi, kPi));
    windState.targetSpeed = randomRange(rng, 0.8f, 6.0f);
    windState.gustAmplitude = randomRange(rng, 0.05f, 1.1f);
    windState.gustFrequency = randomRange(rng, 0.05f, 0.18f);
    windState.nextTargetAt = nowSeconds + randomRange(rng, 18.0f, 45.0f);
}

inline void updateWind(WindState& windState, float dt, float nowSeconds, std::mt19937& rng)
{
    if (nowSeconds >= windState.nextTargetAt) {
        pickNextWindTarget(windState, rng, nowSeconds);
    }

    windState.angle = wrapAngle(windState.angle + shortestAngleDelta(windState.angle, windState.targetAngle) * clamp(dt * 0.12f, 0.0f, 1.0f));
    windState.speed = mix(windState.speed, windState.targetSpeed, clamp(dt * 0.08f, 0.0f, 1.0f));
    windState.gustPhase += dt * windState.gustFrequency;
}

inline Vec3 getWindVector3(const WindState& windState)
{
    const float gust = std::sin(windState.gustPhase * kPi * 2.0f) * windState.gustAmplitude;
    const float speed = std::max(0.0f, windState.speed + gust);
    return {
        std::sin(windState.angle) * speed,
        0.0f,
        std::cos(windState.angle) * speed
    };
}

inline float sampleCloudPuffSdf(const CloudGroup& group, const CloudPuff& puff, const Vec3& point)
{
    const Quat invYaw = quatConjugate(quatFromAxisAngle({ 0.0f, 1.0f, 0.0f }, puff.yaw));
    const Vec3 localPoint = rotateVector(invYaw, point - puff.offset);
    const Vec3 radii {
        std::max(6.0f, puff.scale),
        std::max(4.0f, puff.scale * puff.stretchY),
        std::max(6.0f, puff.scale)
    };
    const Vec3 normalized {
        localPoint.x / std::max(1.0f, radii.x),
        localPoint.y / std::max(1.0f, radii.y),
        localPoint.z / std::max(1.0f, radii.z)
    };
    const float ellipsoidSdf = (length(normalized) - 1.0f) * std::min(radii.x, std::min(radii.y, radii.z));
    const float erosion =
        ((fbm3(
              localPoint.x * 0.021f,
              localPoint.y * 0.027f,
              localPoint.z * 0.021f,
              3,
              2.0f,
              0.54f,
              static_cast<int>(group.noiseSeed)) * 2.0f) - 1.0f) *
        puff.scale *
        0.11f *
        clamp(group.density, 0.6f, 1.4f);
    return ellipsoidSdf - erosion;
}

inline TerrainVolumeBounds computeCloudGroupBounds(const CloudGroup& group, float* outRenderRadius = nullptr)
{
    if (group.puffs.empty()) {
        if (outRenderRadius != nullptr) {
            *outRenderRadius = std::max(80.0f, group.radius * 1.2f);
        }
        return {};
    }

    float minX = std::numeric_limits<float>::infinity();
    float minY = std::numeric_limits<float>::infinity();
    float minZ = std::numeric_limits<float>::infinity();
    float maxX = -std::numeric_limits<float>::infinity();
    float maxY = -std::numeric_limits<float>::infinity();
    float maxZ = -std::numeric_limits<float>::infinity();
    for (const CloudPuff& puff : group.puffs) {
        const float radiusX = std::max(6.0f, puff.scale * 1.08f);
        const float radiusY = std::max(4.0f, puff.scale * puff.stretchY * 1.16f);
        const float radiusZ = std::max(6.0f, puff.scale * 1.08f);
        minX = std::min(minX, puff.offset.x - radiusX);
        minY = std::min(minY, puff.offset.y - radiusY);
        minZ = std::min(minZ, puff.offset.z - radiusZ);
        maxX = std::max(maxX, puff.offset.x + radiusX);
        maxY = std::max(maxY, puff.offset.y + radiusY);
        maxZ = std::max(maxZ, puff.offset.z + radiusZ);
    }

    const float padding = std::max(10.0f, group.radius * 0.12f);
    const TerrainVolumeBounds bounds {
        minX - padding,
        maxX + padding,
        minY - padding,
        maxY + padding,
        minZ - padding,
        maxZ + padding
    };
    if (outRenderRadius != nullptr) {
        *outRenderRadius = length(Vec3 {
            std::max(std::fabs(bounds.x0), std::fabs(bounds.x1)),
            std::max(std::fabs(bounds.y0), std::fabs(bounds.y1)),
            std::max(std::fabs(bounds.z0), std::fabs(bounds.z1))
        });
    }
    return bounds;
}

inline void updateCloudGroupBounds(CloudGroup& group)
{
    group.localBounds = computeCloudGroupBounds(group, &group.renderRadius);
}

struct CloudMeshBuildRequest {
    std::size_t groupIndex = 0u;
    std::uint64_t groupStableId = 0u;
    std::uint64_t buildId = 0u;
    float centerY = 0.0f;
    float radius = 120.0f;
    float verticalScale = 0.65f;
    float density = 1.0f;
    float humidity = 0.5f;
    float opticalDepth = 0.5f;
    float renderRadius = 160.0f;
    std::uint32_t noiseSeed = 1u;
    TerrainVolumeBounds localBounds {};
    std::vector<CloudPuff> puffs;
};

struct CloudMeshBuildResult {
    std::size_t groupIndex = 0u;
    std::uint64_t groupStableId = 0u;
    std::uint64_t buildId = 0u;
    TerrainVolumeBounds localBounds {};
    float renderRadius = 0.0f;
    Model volumeModel;
    bool built = false;
};

inline Vec3 shadeCloudFaceColor(
    const CloudMeshBuildRequest& request,
    const Vec3& faceCenter,
    const Vec3& faceNormal)
{
    const float absoluteAltitude = request.centerY + faceCenter.y;
    const AtmosphereSample faceAtmosphere = sampleAtmosphere(absoluteAltitude);
    const float sigma = clamp(faceAtmosphere.sigma, 0.08f, 1.05f);
    const float verticalSpan = std::max(1.0f, request.localBounds.y1 - request.localBounds.y0);
    const float topFactor = clamp((faceCenter.y - request.localBounds.y0) / verticalSpan, 0.0f, 1.0f);
    const float upFactor = clamp((faceNormal.y * 0.5f) + 0.5f, 0.0f, 1.0f);
    const float densityShadow = clamp(request.opticalDepth * (1.0f - topFactor), 0.0f, 1.6f);
    const float silverLining = std::pow(upFactor, 1.8f) * (0.22f + (0.34f * request.humidity));
    const Vec3 coolTint { 0.63f, 0.72f, 0.84f };
    const Vec3 warmTint { 0.98f, 0.97f, 0.94f };
    Vec3 color = lerp(coolTint, warmTint, mix(topFactor, upFactor, 0.62f));
    color *= mix(0.62f, 1.14f, sigma);
    color *= 1.0f - (0.44f * densityShadow);
    color += Vec3 { silverLining, silverLining * 0.98f, silverLining * 0.94f };
    const float blueShift = clamp((1.0f - sigma) * 1.1f, 0.0f, 1.0f);
    color = lerp(color, Vec3 { 0.72f, 0.80f, 0.93f }, blueShift * 0.34f);
    return clampCloudColor(color);
}

inline CloudMeshBuildResult buildCloudMeshFromRequest(const CloudMeshBuildRequest& request)
{
    CloudMeshBuildResult result;
    result.groupIndex = request.groupIndex;
    result.groupStableId = request.groupStableId;
    result.buildId = request.buildId;
    result.localBounds = request.localBounds;
    result.renderRadius = request.renderRadius;
    if (request.puffs.empty()) {
        return result;
    }

    TerrainParams cloudParams {};
    cloudParams.seed = static_cast<int>(std::max<std::uint32_t>(1u, request.noiseSeed));
    cloudParams.chunkSize = 64.0f;
    cloudParams.worldRadius = 4096.0f;
    cloudParams.minY = -4096.0f;
    cloudParams.maxY = 4096.0f;
    cloudParams.baseHeight = -200000.0f;
    cloudParams.heightAmplitude = 0.0f;
    cloudParams.surfaceDetailAmplitude = 0.0f;
    cloudParams.ridgeAmplitude = 0.0f;
    cloudParams.macroWarpAmplitude = 0.0f;
    cloudParams.waterLevel = -200000.0f;
    cloudParams.shorelineBand = 2.0f;
    cloudParams.roads.enabled = false;
    cloudParams.caveEnabled = false;
    cloudParams.tunnelCount = 0;
    cloudParams.maxChunkCellsPerAxis = 48;

    TerrainFieldContext volumeContext;
    volumeContext.params = normalizeTerrainParams(cloudParams);
    CloudGroup sdfGroup;
    sdfGroup.radius = request.radius;
    sdfGroup.verticalScale = request.verticalScale;
    sdfGroup.density = request.density;
    sdfGroup.noiseSeed = request.noiseSeed;
    const std::vector<CloudPuff> puffs = request.puffs;
    volumeContext.sampleVolumetricAdditiveSdfAt = [sdfGroup, puffs, request](float x, float y, float z) -> float {
        const Vec3 point { x, y, z };
        float cloudSdf = std::numeric_limits<float>::infinity();
        for (const CloudPuff& puff : puffs) {
            cloudSdf = std::min(cloudSdf, sampleCloudPuffSdf(sdfGroup, puff, point));
        }

        const float cloudBaseY = -sdfGroup.radius * sdfGroup.verticalScale * mix(0.54f, 0.72f, request.humidity);
        cloudSdf = std::max(cloudSdf, cloudBaseY - point.y);
        const float verticalLimit = std::max(1.0f, sdfGroup.radius * sdfGroup.verticalScale * 1.58f);
        const float edgeFade = clamp((std::fabs(point.y) / verticalLimit) - 0.52f, 0.0f, 1.0f);
        return cloudSdf + (edgeFade * sdfGroup.radius * mix(0.035f, 0.06f, request.opticalDepth));
    };

    const float detailBias = clamp((request.humidity * 0.55f) + (request.opticalDepth * 0.45f), 0.0f, 1.0f);
    const float cellSize = clamp(request.radius * mix(0.18f, 0.10f, detailBias), 12.0f, 24.0f);
    result.volumeModel = buildVolumetricTerrainPatch(volumeContext, request.localBounds, cellSize);
    result.volumeModel.assetKey.clear();
    result.volumeModel.faceColors.resize(result.volumeModel.faces.size(), { 1.0f, 1.0f, 1.0f });
    for (std::size_t faceIndex = 0; faceIndex < result.volumeModel.faces.size(); ++faceIndex) {
        const Face& face = result.volumeModel.faces[faceIndex];
        if (face.indices.size() < 3u) {
            continue;
        }

        Vec3 center {};
        int validVertices = 0;
        for (const int index : face.indices) {
            if (index < 0 || static_cast<std::size_t>(index) >= result.volumeModel.vertices.size()) {
                continue;
            }
            center += result.volumeModel.vertices[static_cast<std::size_t>(index)];
            ++validVertices;
        }
        if (validVertices <= 0) {
            continue;
        }
        center = center / static_cast<float>(validVertices);

        const auto fetchVertex = [&](const std::size_t indexInFace) -> Vec3 {
            const int vertexIndex = face.indices[indexInFace];
            if (vertexIndex < 0 || static_cast<std::size_t>(vertexIndex) >= result.volumeModel.vertices.size()) {
                return center;
            }
            return result.volumeModel.vertices[static_cast<std::size_t>(vertexIndex)];
        };
        const Vec3 a = fetchVertex(0u);
        const Vec3 b = fetchVertex(1u);
        const Vec3 c = fetchVertex(2u);
        const Vec3 normal = normalize(cross(b - a, c - a), { 0.0f, 1.0f, 0.0f });
        result.volumeModel.faceColors[faceIndex] = shadeCloudFaceColor(request, center, normal);
    }
    result.built = !result.volumeModel.faces.empty();
    return result;
}

class CloudMeshWorkerQueue {
public:
    CloudMeshWorkerQueue() = default;

    ~CloudMeshWorkerQueue()
    {
        shutdown();
    }

    void enqueue(CloudMeshBuildRequest request)
    {
        ensureStarted();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopRequested_) {
                return;
            }
            queuedRequests_.push_back(std::move(request));
        }
        condition_.notify_one();
    }

    std::vector<CloudMeshBuildResult> drainCompleted(std::size_t maxResults)
    {
        std::vector<CloudMeshBuildResult> drained;
        std::lock_guard<std::mutex> lock(mutex_);
        const std::size_t count = maxResults == 0u
                                      ? completedResults_.size()
                                      : std::min(maxResults, completedResults_.size());
        drained.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            drained.push_back(std::move(completedResults_.front()));
            completedResults_.pop_front();
        }
        return drained;
    }

private:
    void ensureStarted()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (started_) {
            return;
        }

        const unsigned int logicalCores = std::max(1u, std::thread::hardware_concurrency());
        const unsigned int desiredWorkers = std::clamp(logicalCores > 2u ? (logicalCores / 2u) : 1u, 1u, 4u);
        workers_.reserve(desiredWorkers);
        for (unsigned int workerIndex = 0u; workerIndex < desiredWorkers; ++workerIndex) {
            workers_.emplace_back([this]() { workerLoop(); });
        }
        started_ = true;
    }

    void shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopRequested_ = true;
        }
        condition_.notify_all();
        for (std::thread& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers_.clear();
    }

    void workerLoop()
    {
        while (true) {
            CloudMeshBuildRequest request;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this]() { return stopRequested_ || !queuedRequests_.empty(); });
                if (stopRequested_ && queuedRequests_.empty()) {
                    return;
                }
                request = std::move(queuedRequests_.front());
                queuedRequests_.pop_front();
            }

            CloudMeshBuildResult result = buildCloudMeshFromRequest(request);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                completedResults_.push_back(std::move(result));
            }
        }
    }

    std::mutex mutex_;
    std::condition_variable condition_;
    bool started_ = false;
    bool stopRequested_ = false;
    std::deque<CloudMeshBuildRequest> queuedRequests_;
    std::deque<CloudMeshBuildResult> completedResults_;
    std::vector<std::thread> workers_;
};

inline CloudMeshWorkerQueue& cloudMeshWorkerQueue()
{
    static CloudMeshWorkerQueue queue;
    return queue;
}

inline void queueCloudGroupMeshBuild(CloudField& cloudField, std::size_t groupIndex)
{
    if (groupIndex >= cloudField.groups.size()) {
        return;
    }
    CloudGroup& group = cloudField.groups[groupIndex];
    if (group.pendingBuildId != 0u) {
        return;
    }

    updateCloudGroupBounds(group);
    CloudMeshBuildRequest request;
    request.groupIndex = groupIndex;
    request.groupStableId = group.stableId;
    request.buildId = nextCloudMeshBuildId();
    request.centerY = group.center.y;
    request.radius = group.radius;
    request.verticalScale = group.verticalScale;
    request.density = group.density;
    request.humidity = group.humidity;
    request.opticalDepth = group.opticalDepth;
    request.renderRadius = group.renderRadius;
    request.noiseSeed = group.noiseSeed;
    request.localBounds = group.localBounds;
    request.puffs = group.puffs;
    group.pendingBuildId = request.buildId;
    cloudMeshWorkerQueue().enqueue(std::move(request));
}

inline void applyCloudMeshBuildResult(CloudGroup& group, CloudMeshBuildResult result, float nowSeconds)
{
    if (!result.built) {
        group.pendingBuildId = 0u;
        group.meshDirty = true;
        return;
    }

    group.localBounds = result.localBounds;
    group.renderRadius = result.renderRadius;
    group.volumeModel = std::move(result.volumeModel);
    group.volumeModel.cacheRevision = result.buildId;
    group.lastAppliedBuildId = result.buildId;
    group.pendingBuildId = 0u;
    group.meshDirty = false;
    group.nextMeshRebuildAt = nowSeconds + mix(18.0f, 42.0f, clamp(group.evolutionRate / 1.8f, 0.0f, 1.0f));
}

inline void drainCompletedCloudMeshBuilds(CloudField& cloudField, float nowSeconds)
{
    const std::size_t maxResults = std::max<std::size_t>(1u, cloudField.maxMeshBuildResultsPerUpdate);
    std::vector<CloudMeshBuildResult> completed = cloudMeshWorkerQueue().drainCompleted(maxResults);
    for (CloudMeshBuildResult& result : completed) {
        if (result.groupIndex >= cloudField.groups.size()) {
            continue;
        }
        CloudGroup& group = cloudField.groups[result.groupIndex];
        if (group.stableId != result.groupStableId || group.pendingBuildId != result.buildId) {
            continue;
        }
        applyCloudMeshBuildResult(group, std::move(result), nowSeconds);
    }
}

inline void rebuildCloudGroupVolume(CloudGroup& group)
{
    updateCloudGroupBounds(group);
    CloudMeshBuildRequest request;
    request.groupStableId = group.stableId;
    request.buildId = nextCloudMeshBuildId();
    request.centerY = group.center.y;
    request.radius = group.radius;
    request.verticalScale = group.verticalScale;
    request.density = group.density;
    request.humidity = group.humidity;
    request.opticalDepth = group.opticalDepth;
    request.renderRadius = group.renderRadius;
    request.noiseSeed = group.noiseSeed;
    request.localBounds = group.localBounds;
    request.puffs = group.puffs;
    CloudMeshBuildResult result = buildCloudMeshFromRequest(request);
    applyCloudMeshBuildResult(group, std::move(result), 0.0f);
}

inline void retargetCloudGroupShape(CloudGroup& group, std::mt19937& rng, float nowSeconds)
{
    group.targetRadius = randomRange(rng, 150.0f, 310.0f);
    group.targetVerticalScale = randomRange(rng, 0.48f, 0.96f);
    group.targetDensity = randomRange(rng, 0.72f, 1.36f);
    group.nextMorphAt = nowSeconds + randomRange(rng, 220.0f, 900.0f);
    group.meshDirty = true;
}

inline CloudGroup randomCloudGroup(
    std::mt19937& rng,
    const Vec3& center,
    float baseHeight,
    float nowSeconds = 0.0f,
    bool buildMeshImmediately = false)
{
    CloudGroup group;
    group.stableId = nextCloudGroupStableId();
    group.center = {
        center.x + randomRange(rng, -1800.0f, 1800.0f),
        baseHeight + randomRange(rng, -120.0f, 140.0f),
        center.z + randomRange(rng, -1800.0f, 1800.0f)
    };
    group.radius = randomRange(rng, 150.0f, 270.0f);
    group.targetRadius = group.radius;
    group.verticalScale = randomRange(rng, 0.52f, 0.90f);
    group.targetVerticalScale = group.verticalScale;
    group.density = randomRange(rng, 0.84f, 1.20f);
    group.targetDensity = group.density;
    group.driftScale = randomRange(rng, 0.65f, 1.45f);
    group.evolutionRate = randomRange(rng, 0.55f, 1.35f);
    group.alpha = randomRange(rng, 0.46f, 0.76f);
    group.noiseSeed = static_cast<std::uint32_t>(randomRangeInt(rng, 1, 1 << 20));

    const AtmosphereSample atmosphere = sampleAtmosphere(group.center.y);
    group.humidity = estimateCloudHumidityFromAtmosphere(atmosphere, randomRange(rng, 0.0f, kPi * 2.0f));
    group.opticalDepth = clamp(group.density * mix(0.38f, 1.22f, group.humidity), 0.24f, 1.58f);
    const float layerHalf = randomRange(rng, 180.0f, 320.0f);
    group.layerBase = group.center.y - layerHalf;
    group.layerTop = group.center.y + layerHalf;

    const int puffCount = randomRangeInt(rng, 8, 16);
    group.puffs.reserve(static_cast<std::size_t>(puffCount));
    for (int i = 0; i < puffCount; ++i) {
        CloudPuff puff;
        puff.baseOffset = {
            randomRange(rng, -group.radius * 0.88f, group.radius * 0.88f),
            randomRange(rng, -group.radius * 0.22f, group.radius * 0.32f),
            randomRange(rng, -group.radius * 0.88f, group.radius * 0.88f)
        };
        puff.offset = puff.baseOffset;
        puff.baseScale = randomRange(rng, 26.0f, 88.0f);
        puff.scale = puff.baseScale;
        puff.baseStretchY = randomRange(rng, 0.52f, 1.00f);
        puff.stretchY = puff.baseStretchY;
        puff.driftPhase = randomRange(rng, 0.0f, kPi * 2.0f);
        puff.driftAmplitude = randomRange(rng, 3.2f, 11.5f);
        puff.yaw = randomRange(rng, -kPi, kPi);
        const float tint = randomRange(rng, 0.90f, 1.0f);
        puff.color = { tint, tint, std::min(1.0f, tint + 0.03f) };
        group.puffs.push_back(puff);
    }
    group.nextMorphAt = nowSeconds + randomRange(rng, 240.0f, 980.0f);
    group.nextMeshRebuildAt = nowSeconds + randomRange(rng, 16.0f, 42.0f);
    updateCloudGroupBounds(group);
    group.meshDirty = true;
    if (buildMeshImmediately) {
        rebuildCloudGroupVolume(group);
        group.nextMeshRebuildAt = nowSeconds + randomRange(rng, 22.0f, 56.0f);
    }
    return group;
}

inline void initializeCloudField(CloudField& cloudField, std::mt19937& rng, const Vec3& center)
{
    cloudField.groups.clear();
    cloudField.groups.reserve(static_cast<std::size_t>(std::max(1, cloudField.groupCount)));
    for (int i = 0; i < std::max(1, cloudField.groupCount); ++i) {
        cloudField.groups.push_back(randomCloudGroup(rng, center, cloudField.baseHeight, 0.0f, i == 0));
    }
    for (std::size_t groupIndex = 1u; groupIndex < cloudField.groups.size(); ++groupIndex) {
        queueCloudGroupMeshBuild(cloudField, groupIndex);
    }
}

inline void updateCloudField(
    CloudField& cloudField,
    WindState& windState,
    float dt,
    float nowSeconds,
    const Vec3& focusPoint,
    const AtmosphereSample& focusAtmosphere,
    std::mt19937& rng)
{
    if (cloudField.groups.empty()) {
        return;
    }

    drainCompletedCloudMeshBuilds(cloudField, nowSeconds);
    updateWind(windState, dt, nowSeconds, rng);
    const Vec3 wind = getWindVector3(windState);
    const float recycleDistance = cloudField.spawnRadius * 1.3f;

    const float weatherHumidity = estimateCloudHumidityFromAtmosphere(focusAtmosphere, nowSeconds * 0.05f);
    cloudField.coverage = mix(cloudField.coverage, clamp(0.38f + (weatherHumidity * 0.56f), 0.20f, 0.98f), clamp(dt * 0.08f, 0.0f, 1.0f));
    const float targetLayerThickness = mix(260.0f, 680.0f, weatherHumidity);
    cloudField.layerThickness = mix(cloudField.layerThickness, targetLayerThickness, clamp(dt * 0.10f, 0.0f, 1.0f));
    cloudField.baseHeight = mix(cloudField.baseHeight, mix(480.0f, 780.0f, weatherHumidity), clamp(dt * 0.04f, 0.0f, 1.0f));

    for (std::size_t groupIndex = 0u; groupIndex < cloudField.groups.size(); ++groupIndex) {
        CloudGroup& group = cloudField.groups[groupIndex];
        group.center += wind * (dt * group.driftScale);

        const float phase = (nowSeconds * 0.03f * group.evolutionRate) + static_cast<float>(group.stableId % 1048573u) * 0.000013f;
        const AtmosphereSample localAtmosphere = sampleAtmosphere(group.center.y);
        const float localHumidity = estimateCloudHumidityFromAtmosphere(localAtmosphere, phase + 1.9f);
        const float layerWeight = cloudLayerWeight(cloudField, group.center.y);
        const float humidityTarget = clamp(mix(localHumidity, cloudField.coverage, cloudField.humidityResponse), 0.05f, 0.99f);

        group.humidity = mix(group.humidity, humidityTarget, clamp(dt * 0.26f, 0.0f, 1.0f));
        group.layerBase = cloudField.baseHeight - (cloudField.layerThickness * mix(0.48f, 0.62f, group.humidity));
        group.layerTop = cloudField.baseHeight + (cloudField.layerThickness * mix(0.38f, 0.58f, group.humidity));

        const float thermalLift = std::sin(phase + group.radius * 0.01f) * mix(0.4f, 2.8f, group.humidity);
        const float yTarget = clamp(group.center.y + (thermalLift * dt), group.layerBase, group.layerTop);
        group.center.y = mix(group.center.y, yTarget, clamp(dt * 0.82f, 0.0f, 1.0f));

        if (nowSeconds >= group.nextMorphAt) {
            retargetCloudGroupShape(group, rng, nowSeconds);
        }

        group.targetDensity = mix(group.targetDensity, mix(0.62f, 1.38f, group.humidity * layerWeight), clamp(dt * 0.10f, 0.0f, 1.0f));
        group.radius = mix(group.radius, group.targetRadius, clamp(dt * 0.12f * group.evolutionRate, 0.0f, 1.0f));
        group.verticalScale = mix(group.verticalScale, group.targetVerticalScale, clamp(dt * 0.10f * group.evolutionRate, 0.0f, 1.0f));
        group.density = mix(group.density, group.targetDensity, clamp(dt * 0.11f * group.evolutionRate, 0.0f, 1.0f));
        group.opticalDepth = mix(group.opticalDepth, clamp(group.density * mix(0.34f, 1.24f, group.humidity), 0.20f, 1.65f), clamp(dt * 0.18f, 0.0f, 1.0f));
        group.alpha = clamp(mix(group.alpha, 0.20f + (group.opticalDepth * 0.50f), clamp(dt * 0.20f, 0.0f, 1.0f)), 0.20f, 0.90f);

        for (CloudPuff& puff : group.puffs) {
            const float breathing = 1.0f + std::sin((nowSeconds * 0.24f * group.evolutionRate) + puff.driftPhase) * 0.09f;
            const Vec3 drift {
                std::cos((nowSeconds * 0.11f * group.evolutionRate) + puff.driftPhase) * puff.driftAmplitude,
                std::sin((nowSeconds * 0.17f * group.evolutionRate) + puff.driftPhase) * puff.driftAmplitude * 0.36f,
                std::sin((nowSeconds * 0.13f * group.evolutionRate) + puff.driftPhase + 1.7f) * puff.driftAmplitude
            };
            puff.offset = {
                puff.baseOffset.x * (group.radius / 180.0f) + drift.x,
                puff.baseOffset.y * group.verticalScale + drift.y,
                puff.baseOffset.z * (group.radius / 180.0f) + drift.z
            };
            const float humidityScale = mix(0.76f, 1.24f, group.humidity);
            puff.scale = puff.baseScale * mix(0.82f, 1.18f, clamp(group.density, 0.0f, 1.0f)) * humidityScale * breathing;
            puff.stretchY = puff.baseStretchY * mix(0.86f, 1.14f, clamp(group.verticalScale, 0.0f, 1.0f));
        }

        const Vec3 delta = group.center - focusPoint;
        const float flatDistanceSq = (delta.x * delta.x) + (delta.z * delta.z);
        if (flatDistanceSq > (recycleDistance * recycleDistance)) {
            const float respawnAngle = randomRange(rng, -kPi, kPi);
            const float respawnDistance = cloudField.spawnRadius + randomRange(rng, 150.0f, 420.0f);
            CloudGroup replacement = randomCloudGroup(
                rng,
                {
                    focusPoint.x + (std::sin(respawnAngle) * respawnDistance),
                    focusPoint.y,
                    focusPoint.z + (std::cos(respawnAngle) * respawnDistance)
                },
                cloudField.baseHeight,
                nowSeconds,
                false);
            if (!group.volumeModel.faces.empty()) {
                replacement.volumeModel = group.volumeModel;
                replacement.localBounds = group.localBounds;
                replacement.renderRadius = group.renderRadius;
            }
            group = std::move(replacement);
            queueCloudGroupMeshBuild(cloudField, groupIndex);
            continue;
        }

        const float morphDistance =
            std::fabs(group.radius - group.targetRadius) +
            std::fabs(group.verticalScale - group.targetVerticalScale) * 120.0f;
        if (morphDistance > 8.0f || std::fabs(group.density - group.targetDensity) > 0.06f) {
            group.meshDirty = true;
        }

        if ((group.meshDirty || nowSeconds >= group.nextMeshRebuildAt) && group.pendingBuildId == 0u) {
            queueCloudGroupMeshBuild(cloudField, groupIndex);
        }
    }

    drainCompletedCloudMeshBuilds(cloudField, nowSeconds);
}

inline void updateCloudField(CloudField& cloudField, WindState& windState, float dt, float nowSeconds, const Vec3& focusPoint, std::mt19937& rng)
{
    updateCloudField(cloudField, windState, dt, nowSeconds, focusPoint, sampleAtmosphere(focusPoint.y), rng);
}

}  // namespace NativeGame
