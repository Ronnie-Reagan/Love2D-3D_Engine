#pragma once

#include "NativeGame/Math.hpp"
#include "NativeGame/StlLoader.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <random>
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

struct TerrainParams {
    int seed = 1337;
    float chunkSize = 128.0f;
    float worldRadius = 2048.0f;
    float minY = -120.0f;
    float maxY = 1200.0f;
    int lod0Radius = 4;
    int lod1Radius = 16;
    int lod2Radius = 64;
    float terrainQuality = 3.5f;
    bool autoQualityEnabled = false;
    float targetFrameMs = 16.6f;
    int lod0ChunkScale = 1;
    int lod1ChunkScale = 4;
    int lod2ChunkScale = 16;
    bool textureTilesEnabled = true;
    int lod0TextureResolution = 512;
    int lod1TextureResolution = 256;
    int lod2TextureResolution = 64;
    float gameplayRadiusMeters = 512.0f;
    float midFieldRadiusMeters = 2048.0f;
    float horizonRadiusMeters = 8192.0f;
    float lod0BaseCellSize = 3.0f;
    float lod1BaseCellSize = 6.0f;
    float lod2BaseCellSize = 12.0f;
    float lod0CellSize = 3.0f;
    float lod1CellSize = 6.0f;
    float lod2CellSize = 12.0f;
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
    float baseHeight = 0.0f;
    float heightAmplitude = 120.0f;
    float heightFrequency = 0.0018f;
    int heightOctaves = 5;
    float heightLacunarity = 2.05f;
    float heightGain = 0.52f;
    float surfaceDetailAmplitude = 14.0f;
    float surfaceDetailFrequency = 0.013f;
    float ridgeAmplitude = 38.0f;
    float ridgeFrequency = 0.0042f;
    float ridgeSharpness = 2.1f;
    float macroWarpAmplitude = 90.0f;
    float macroWarpFrequency = 0.00055f;
    float terraceStrength = 0.16f;
    float terraceStep = 18.0f;
    float waterLevel = -12.0f;
    float shorelineBand = 5.0f;
    float waterWaveAmplitude = 1.6f;
    float waterWaveFrequency = 0.014f;
    float biomeFrequency = 0.0009f;
    float snowLine = 140.0f;
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
    int generatorVersion = 1;
    bool surfaceOnlyMeshing = true;
    bool threadedMeshing = true;
    bool enableSkirts = true;
    float skirtDepth = 24.0f;
    int maxChunkCellsPerAxis = 48;
    int craterHistoryLimit = 64;
    float waterRatio = 0.26f;
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
};

struct TerrainFieldContext {
    TerrainParams params;
    std::vector<TerrainTunnelSeed> tunnelSeeds;
    std::function<float(float, float)> sampleHeightDeltaAt {};
    std::function<float(float, float, float)> sampleVolumetricOverrideSdfAt {};
    std::function<std::uint64_t(float, float, float, float)> sampleChunkRevisionSignature {};
};

struct TerrainMaterialSample {
    float surfaceHeight = 0.0f;
    float waterHeight = 0.0f;
    float wetness = 0.0f;
    float snowWeight = 0.0f;
    float rockWeight = 0.0f;
    float biomeBlend = 0.0f;
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
    params.worldRadius = std::max(params.chunkSize * 4.0f, sanitize(params.worldRadius, 2048.0f));
    params.minY = sanitize(params.minY, -120.0f);
    params.maxY = std::max(params.minY + 16.0f, sanitize(params.maxY, 1200.0f));

    params.lod0Radius = std::clamp(params.lod0Radius, 1, 12);
    params.lod1Radius = std::clamp(params.lod1Radius, params.lod0Radius, 32);
    params.lod2Radius = std::clamp(params.lod2Radius, params.lod1Radius, 96);
    params.terrainQuality = clamp(sanitize(params.terrainQuality, 3.5f), 0.75f, 6.0f);
    params.targetFrameMs = clamp(sanitize(params.targetFrameMs, 16.6f), 8.0f, 50.0f);

    params.lod0ChunkScale = std::max(1, params.lod0ChunkScale);
    params.lod1ChunkScale = std::max(params.lod0ChunkScale, params.lod1ChunkScale);
    params.lod2ChunkScale = std::max(params.lod1ChunkScale, params.lod2ChunkScale);

    params.lod0TextureResolution = std::clamp(params.lod0TextureResolution, 64, 512);
    params.lod1TextureResolution = std::clamp(params.lod1TextureResolution, 32, 512);
    params.lod2TextureResolution = std::clamp(params.lod2TextureResolution, 16, 256);

    params.gameplayRadiusMeters = std::max(
        params.chunkSize,
        sanitize(params.gameplayRadiusMeters, std::max(512.0f, params.chunkSize * static_cast<float>(std::max(params.lod0Radius, 4)))));
    params.midFieldRadiusMeters = std::max(
        params.gameplayRadiusMeters + params.chunkSize,
        sanitize(params.midFieldRadiusMeters, std::max(2048.0f, params.chunkSize * static_cast<float>(std::max(params.lod1Radius, 16)))));
    params.horizonRadiusMeters = std::max(
        params.midFieldRadiusMeters + params.chunkSize,
        sanitize(params.horizonRadiusMeters, std::max(8192.0f, params.chunkSize * static_cast<float>(std::max(params.lod2Radius, 64)))));

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

    params.baseHeight = sanitize(params.baseHeight, 0.0f);
    params.heightAmplitude = std::max(0.0f, sanitize(params.heightAmplitude, 120.0f));
    params.heightFrequency = std::max(1.0e-5f, sanitize(params.heightFrequency, 0.0018f));
    params.heightOctaves = std::max(1, params.heightOctaves);
    params.heightLacunarity = std::max(1.1f, sanitize(params.heightLacunarity, 2.05f));
    params.heightGain = clamp(sanitize(params.heightGain, 0.52f), 0.1f, 0.95f);
    params.surfaceDetailAmplitude = std::max(0.0f, sanitize(params.surfaceDetailAmplitude, 14.0f));
    params.surfaceDetailFrequency = std::max(1.0e-4f, sanitize(params.surfaceDetailFrequency, 0.013f));
    params.ridgeAmplitude = std::max(0.0f, sanitize(params.ridgeAmplitude, 38.0f));
    params.ridgeFrequency = std::max(1.0e-5f, sanitize(params.ridgeFrequency, 0.0042f));
    params.ridgeSharpness = std::max(0.3f, sanitize(params.ridgeSharpness, 2.1f));
    params.macroWarpAmplitude = std::max(0.0f, sanitize(params.macroWarpAmplitude, 90.0f));
    params.macroWarpFrequency = std::max(1.0e-5f, sanitize(params.macroWarpFrequency, 0.00055f));
    params.terraceStrength = clamp(sanitize(params.terraceStrength, 0.16f), 0.0f, 1.0f);
    params.terraceStep = std::max(1.0f, sanitize(params.terraceStep, 18.0f));
    params.waterLevel = sanitize(params.waterLevel, -12.0f);
    params.shorelineBand = std::max(0.1f, sanitize(params.shorelineBand, 5.0f));
    params.waterWaveAmplitude = std::max(0.0f, sanitize(params.waterWaveAmplitude, 1.6f));
    params.waterWaveFrequency = std::max(1.0e-4f, sanitize(params.waterWaveFrequency, 0.014f));
    params.biomeFrequency = std::max(1.0e-5f, sanitize(params.biomeFrequency, 0.0009f));
    params.snowLine = sanitize(params.snowLine, 140.0f);
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

    params.generatorVersion = std::max(1, params.generatorVersion);
    params.skirtDepth = std::max(2.0f, sanitize(params.skirtDepth, 24.0f));
    params.maxChunkCellsPerAxis = std::clamp(params.maxChunkCellsPerAxis, 24, 128);
    params.craterHistoryLimit = std::max(0, params.craterHistoryLimit);
    params.waterRatio = clamp(sanitize(params.waterRatio, 0.26f), 0.0f, 1.0f);
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

    if (!params.caveEnabled && params.tunnelCount <= 0 && params.explicitTunnelSeeds.empty()) {
        params.surfaceOnlyMeshing = true;
    }
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
        lhs.waterColor.x == rhs.waterColor.x &&
        lhs.waterColor.y == rhs.waterColor.y &&
        lhs.waterColor.z == rhs.waterColor.z &&
        terrainCraterListsEqual(lhs.dynamicCraters, rhs.dynamicCraters) &&
        terrainTunnelSeedListsEqual(lhs.explicitTunnelSeeds, rhs.explicitTunnelSeeds);
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

inline TerrainFieldContext createTerrainFieldContext(const TerrainParams& inputParams)
{
    TerrainFieldContext context;
    context.params = normalizeTerrainParams(inputParams);
    if (!context.params.explicitTunnelSeeds.empty()) {
        context.tunnelSeeds = context.params.explicitTunnelSeeds;
    } else if (context.params.tunnelCount > 0) {
        context.tunnelSeeds = buildTunnelSeeds(context.params);
    }
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
    const TerrainParams& params = context.params;

    const float warpN1 = (valueNoise2(x * params.macroWarpFrequency, z * params.macroWarpFrequency, params.seed + 211) * 2.0f) - 1.0f;
    const float warpN2 = (valueNoise2(x * params.macroWarpFrequency, z * params.macroWarpFrequency, params.seed + 347) * 2.0f) - 1.0f;
    const float wx = x + (warpN1 * params.macroWarpAmplitude);
    const float wz = z + (warpN2 * params.macroWarpAmplitude);

    const float nx = wx * params.heightFrequency;
    const float nz = wz * params.heightFrequency;
    const float heightNoise = (fbm2(nx, nz, params.heightOctaves, params.heightLacunarity, params.heightGain, params.seed) * 2.0f) - 1.0f;
    const float detailNoise = (valueNoise2(wx * params.surfaceDetailFrequency, wz * params.surfaceDetailFrequency, params.seed + 907) * 2.0f) - 1.0f;
    float ridge = (fbm2(wx * params.ridgeFrequency, wz * params.ridgeFrequency, 4, 2.03f, 0.53f, params.seed + 503) * 2.0f) - 1.0f;
    ridge = 1.0f - std::fabs(ridge);
    ridge = std::pow(ridge, params.ridgeSharpness);
    const float ridgeSigned = (ridge * 2.0f) - 1.0f;

    float surface = params.baseHeight +
        (heightNoise * params.heightAmplitude) +
        (detailNoise * params.surfaceDetailAmplitude) +
        (ridgeSigned * params.ridgeAmplitude);
    if (params.terraceStrength > 0.0f) {
        const float stepped = std::floor((surface / params.terraceStep) + 0.5f) * params.terraceStep;
        surface = mix(surface, stepped, params.terraceStrength);
    }
    surface = applyDynamicCratersToSurfaceHeight(x, z, surface, params);
    return surface;
}

inline float sampleSurfaceHeight(float x, float z, const TerrainFieldContext& context)
{
    float surface = sampleBaseSurfaceHeight(x, z, context);
    if (context.sampleHeightDeltaAt) {
        surface += sanitize(context.sampleHeightDeltaAt(x, z), 0.0f);
    }
    return surface;
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

    if (context.sampleVolumetricOverrideSdfAt) {
        const float overrideDistance = sanitize(context.sampleVolumetricOverrideSdfAt(x, y, z), std::numeric_limits<float>::infinity());
        if (std::isfinite(overrideDistance)) {
            sdf = std::max(sdf, -overrideDistance);
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

    const float biomeNoise = (fbm2(x * params.biomeFrequency, z * params.biomeFrequency, 4, 2.0f, 0.54f, params.seed + 1201) * 2.0f) - 1.0f;
    const float elevation01 = clamp((surface - params.baseHeight + params.heightAmplitude) / std::max(1.0f, params.heightAmplitude * 2.0f), 0.0f, 1.0f);
    const float wetness = clamp((params.waterLevel + params.shorelineBand - surface) / std::max(0.1f, params.shorelineBand), 0.0f, 1.0f);
    const float snowAltitude = clamp((surface - params.snowLine) / 90.0f, 0.0f, 1.0f);
    const float snowNoise = clamp(((valueNoise2(x * 0.0052f, z * 0.0052f, params.seed + 2141) * 2.0f) - 1.0f) * 0.18f, -0.18f, 0.18f);
    const float rockWeight = clamp((slope - 0.12f) * 1.5f, 0.0f, 1.0f);
    const float snow = clamp(snowAltitude + snowNoise + (rockWeight * 0.08f), 0.0f, 1.0f);
    const float biomeBlend = clamp(((biomeNoise + 1.0f) * 0.5f) + (elevation01 * 0.16f), 0.0f, 1.0f);

    (void)y;
    return {
        surface,
        waterHeight,
        wetness,
        snow,
        rockWeight,
        biomeBlend
    };
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
    const Vec3 snowColor { 0.88f, 0.89f, 0.92f };

    Vec3 base = lerp(grass, forest, material.biomeBlend);
    base = lerp(base, sand, material.wetness * 0.72f);
    base = lerp(base, rock, material.rockWeight);
    const Vec3 dampened {
        clamp(base.x * (1.0f - (material.wetness * 0.18f)), 0.0f, 1.0f),
        clamp(base.y * (1.0f - (material.wetness * 0.12f)), 0.0f, 1.0f),
        clamp(base.z * (1.0f - (material.wetness * 0.08f)), 0.0f, 1.0f)
    };
    base = lerp(base, dampened, material.wetness * 0.55f);
    base = lerp(base, snowColor, material.snowWeight);

    const float micro = (valueNoise2(x * 0.013f, z * 0.013f, params.seed + 331) * 2.0f) - 1.0f;
    base.x = clamp(base.x + (micro * 0.05f), 0.0f, 1.0f);
    base.y = clamp(base.y + (micro * 0.06f), 0.0f, 1.0f);
    base.z = clamp(base.z + (micro * 0.04f), 0.0f, 1.0f);
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
    addFace(model, { base, base + 2, base + 1 }, color);
    addFace(model, { base, base + 3, base + 2 }, color);
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
                { x1, w10, z0 },
                { x1, w11, z1 },
                { x0, w01, z1 },
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
        TerrainVolumeBounds nearVolume {
            nearBounds.x0 - overlap,
            nearBounds.x1 + overlap,
            params.minY,
            params.maxY,
            nearBounds.z0 - overlap,
            nearBounds.z1 + overlap
        };
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
    float speed = 10.0f;
    float targetAngle = 0.0f;
    float targetSpeed = 10.0f;
    float gustAmplitude = 2.0f;
    float gustFrequency = 0.35f;
    float gustPhase = 0.0f;
    float nextTargetAt = 0.0f;
};

struct CloudPuff {
    Vec3 offset {};
    float scale = 32.0f;
    float stretchY = 0.65f;
    float bobPhase = 0.0f;
    float bobAmplitude = 1.0f;
    float yaw = 0.0f;
    Vec3 color { 0.98f, 0.99f, 1.0f };
};

struct CloudGroup {
    Vec3 center {};
    float radius = 120.0f;
    float driftScale = 1.0f;
    std::vector<CloudPuff> puffs;
};

struct CloudField {
    float spawnRadius = 2200.0f;
    float baseHeight = 460.0f;
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

inline void pickNextWindTarget(WindState& windState, std::mt19937& rng, float nowSeconds = 0.0f)
{
    windState.targetAngle = wrapAngle(randomRange(rng, -kPi, kPi));
    windState.targetSpeed = randomRange(rng, 8.0f, 26.0f);
    windState.gustAmplitude = randomRange(rng, 1.0f, 4.0f);
    windState.gustFrequency = randomRange(rng, 0.2f, 0.55f);
    windState.nextTargetAt = nowSeconds + randomRange(rng, 8.0f, 20.0f);
}

inline void updateWind(WindState& windState, float dt, float nowSeconds, std::mt19937& rng)
{
    if (nowSeconds >= windState.nextTargetAt) {
        pickNextWindTarget(windState, rng, nowSeconds);
    }

    windState.angle = wrapAngle(windState.angle + shortestAngleDelta(windState.angle, windState.targetAngle) * clamp(dt * 0.28f, 0.0f, 1.0f));
    windState.speed = mix(windState.speed, windState.targetSpeed, clamp(dt * 0.16f, 0.0f, 1.0f));
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

inline CloudGroup randomCloudGroup(std::mt19937& rng, const Vec3& center, float baseHeight)
{
    CloudGroup group;
    group.center = {
        center.x + randomRange(rng, -1800.0f, 1800.0f),
        baseHeight + randomRange(rng, -80.0f, 120.0f),
        center.z + randomRange(rng, -1800.0f, 1800.0f)
    };
    group.radius = randomRange(rng, 90.0f, 220.0f);
    group.driftScale = randomRange(rng, 0.65f, 1.45f);

    const int puffCount = randomRangeInt(rng, 5, 12);
    group.puffs.reserve(static_cast<std::size_t>(puffCount));
    for (int i = 0; i < puffCount; ++i) {
        CloudPuff puff;
        puff.offset = {
            randomRange(rng, -group.radius, group.radius),
            randomRange(rng, -18.0f, 22.0f),
            randomRange(rng, -group.radius, group.radius)
        };
        puff.scale = randomRange(rng, 28.0f, 82.0f);
        puff.stretchY = randomRange(rng, 0.45f, 0.9f);
        puff.bobPhase = randomRange(rng, 0.0f, kPi * 2.0f);
        puff.bobAmplitude = randomRange(rng, 1.0f, 4.5f);
        puff.yaw = randomRange(rng, -kPi, kPi);
        const float tint = randomRange(rng, 0.93f, 1.0f);
        puff.color = { tint, tint, std::min(1.0f, tint + 0.02f) };
        group.puffs.push_back(puff);
    }
    return group;
}

inline void initializeCloudField(CloudField& cloudField, std::mt19937& rng, const Vec3& center)
{
    cloudField.groups.clear();
    cloudField.groups.reserve(18);
    for (int i = 0; i < 18; ++i) {
        cloudField.groups.push_back(randomCloudGroup(rng, center, cloudField.baseHeight));
    }
}

inline void updateCloudField(CloudField& cloudField, WindState& windState, float dt, float nowSeconds, const Vec3& focusPoint, std::mt19937& rng)
{
    updateWind(windState, dt, nowSeconds, rng);
    const Vec3 wind = getWindVector3(windState);
    const float recycleDistance = cloudField.spawnRadius * 1.3f;
    for (CloudGroup& group : cloudField.groups) {
        group.center += wind * (dt * group.driftScale);
        group.center.y += std::sin((nowSeconds * 0.07f) + group.radius * 0.01f) * dt * 2.0f;
        const Vec3 delta = group.center - focusPoint;
        const float flatDistanceSq = (delta.x * delta.x) + (delta.z * delta.z);
        if (flatDistanceSq > (recycleDistance * recycleDistance)) {
            const float respawnAngle = randomRange(rng, -kPi, kPi);
            const float respawnDistance = cloudField.spawnRadius + randomRange(rng, 150.0f, 420.0f);
            group = randomCloudGroup(
                rng,
                {
                    focusPoint.x + (std::sin(respawnAngle) * respawnDistance),
                    focusPoint.y,
                    focusPoint.z + (std::cos(respawnAngle) * respawnDistance)
                },
                cloudField.baseHeight);
        }
    }
}

}  // namespace NativeGame
