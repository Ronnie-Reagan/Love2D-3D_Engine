#pragma once

#include "NativeGame/World.hpp"
#include "NativeGame/WorldWire.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace NativeGame {

struct WorldStoreOptions {
    std::string name = "default";
    std::filesystem::path storageRoot {};
    int regionSize = 16;
    int chunkResolution = 16;
    bool createIfMissing = true;
    TerrainParams groundParams = defaultTerrainParams();
    Vec3 spawn {};
};

struct WorldMetaTerrainProfile {
    float chunkSize = 128.0f;
    float worldRadius = 2048.0f;
    float heightAmplitude = 120.0f;
    float heightFrequency = 0.0018f;
    float waterLevel = -12.0f;
};

struct WorldMetaTunnelProfile {
    int count = 0;
    float radiusMin = 9.0f;
    float radiusMax = 18.0f;
    float lengthMin = 240.0f;
    float lengthMax = 520.0f;
    bool hillAttached = false;
};

struct WorldMeta {
    std::string worldId = "default";
    int formatVersion = 1;
    int seed = 1;
    WorldMetaTerrainProfile terrainProfile {};
    Vec3 spawn {};
    WorldMetaTunnelProfile tunnelProfile {};
    std::vector<TerrainTunnelSeed> tunnelSeeds;
    int chunkResolution = 16;
    int regionSize = 16;
    std::string createdAt;
    std::string updatedAt;
};

struct WorldInfoSnapshot {
    std::string worldId = "default";
    int formatVersion = 1;
    int seed = 1;
    float chunkSize = 128.0f;
    float horizonRadiusMeters = 2048.0f;
    float heightAmplitude = 120.0f;
    float heightFrequency = 0.0018f;
    float waterLevel = -12.0f;
    std::vector<TerrainTunnelSeed> tunnelSeeds;
    float spawnX = 0.0f;
    float spawnY = 0.0f;
    float spawnZ = 0.0f;
};

struct WorldGroundParams {
    TerrainParams terrainParams = defaultTerrainParams();
    std::string worldId = "default";
    int worldFormatVersion = 1;
    int tunnelSeedCount = 0;
    std::vector<TerrainTunnelSeed> tunnelSeeds;
};

class WorldStore {
public:
    static constexpr int kFormatVersion = 1;

    static std::optional<WorldStore> open(const WorldStoreOptions& options, std::string* error = nullptr);

    WorldMeta getMeta() const;
    WorldChunkState* getChunk(int cx, int cz, bool create);
    std::optional<WorldChunkState> getChunkState(int cx, int cz);
    bool applyWorldInfo(const WorldInfoSnapshot& info, std::string* error = nullptr);
    bool applyChunkState(const WorldChunkState& state);
    float sampleHeightDelta(float x, float z);
    float sampleVolumetricOverrideSdf(float x, float y, float z);
    std::uint64_t revisionSignatureForBounds(float x0, float z0, float x1, float z1, int neighborRing = 1);
    std::vector<WorldChunkState> collectEditedChunks(int centerCx, int centerCz, int radiusChunks);
    std::pair<bool, std::vector<WorldChunkState>> applyCrater(const TerrainCrater& craterSpec);
    int flushDirty(std::string* error = nullptr);
    WorldGroundParams buildGroundParams(const TerrainParams& baseParams) const;

private:
    std::string name_;
    std::filesystem::path storageRoot_;
    std::filesystem::path rootPath_;
    WorldMeta meta_ {};
    int regionSize_ = 16;
    int chunkResolution_ = 16;
    float chunkSize_ = 128.0f;
    std::unordered_map<std::string, WorldChunkState> chunks_;
    std::unordered_set<std::string> loadedRegions_;
    std::unordered_set<std::string> dirtyRegions_;

    static std::string trim(const std::string& value);
    static int parseInt(const std::string& value, int fallback);
    static float parseFloat(const std::string& value, float fallback);
    static std::string formatFloat(float value, int precision = 6);
    static std::string sanitizeWorldName(const std::string& value);
    static std::string buildChunkKey(int cx, int cz);
    static std::string buildRegionKey(int rx, int rz);
    static std::pair<int, int> chunkToRegion(int cx, int cz, int regionSize);
    static std::tuple<float, float, float, float> chunkBounds(float chunkSize, int cx, int cz);
    static std::size_t deltaGridLength(int resolution);
    static WorldChunkState buildEmptyChunk(int cx, int cz, int resolution);
    static WorldChunkState normalizeChunkState(const WorldChunkState& chunk, int fallbackResolution);
    static bool chunkHasMeaningfulData(const WorldChunkState& chunk);
    static float sampleOverrideDistance(float x, float y, float z, const std::vector<WorldVolumetricOverride>& overrides);
    static std::string timestampUtc();

    std::filesystem::path metaPath() const;
    std::filesystem::path regionPath(int rx, int rz) const;

    static std::map<std::string, std::string> readKeyValueFile(const std::filesystem::path& path);
    static bool writeKeyValueFile(const std::filesystem::path& path, const std::map<std::string, std::string>& values, std::string* error);
    bool loadOrCreateMeta(const WorldStoreOptions& options, std::string* error);
    bool saveMeta(std::string* error) const;
    bool loadRegion(int rx, int rz, std::string* error);
    bool writeRegion(int rx, int rz, std::string* error) const;
    void markRegionDirty(int rx, int rz);
    void touchMeta();
    static std::string valueOr(const std::map<std::string, std::string>& values, const char* key, const std::string& fallback);
};

inline std::string WorldStore::trim(const std::string& value)
{
    const std::size_t start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return {};
    }
    const std::size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1u);
}

inline int WorldStore::parseInt(const std::string& value, int fallback)
{
    if (value.empty()) {
        return fallback;
    }
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == nullptr || *end != '\0') {
        return fallback;
    }
    return static_cast<int>(parsed);
}

inline float WorldStore::parseFloat(const std::string& value, float fallback)
{
    if (value.empty()) {
        return fallback;
    }
    char* end = nullptr;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == nullptr || *end != '\0') {
        return fallback;
    }
    return parsed;
}

inline std::string WorldStore::formatFloat(float value, int precision)
{
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream << std::setprecision(precision) << sanitize(value, 0.0f);
    return stream.str();
}

inline std::string WorldStore::sanitizeWorldName(const std::string& value)
{
    std::string out;
    out.reserve(value.size());
    for (char ch : value.empty() ? std::string("default") : value) {
        const unsigned char lowerInput = static_cast<unsigned char>(ch);
        if (std::isalnum(lowerInput) || ch == '_' || ch == '-') {
            out.push_back(static_cast<char>(std::tolower(lowerInput)));
        } else {
            out.push_back('_');
        }
    }
    while (out.find("__") != std::string::npos) {
        out.erase(out.find("__"), 1u);
    }
    while (!out.empty() && out.front() == '_') {
        out.erase(out.begin());
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    return out.empty() ? std::string("default") : out;
}

inline std::string WorldStore::buildChunkKey(int cx, int cz)
{
    return std::to_string(cx) + ":" + std::to_string(cz);
}

inline std::string WorldStore::buildRegionKey(int rx, int rz)
{
    return std::to_string(rx) + ":" + std::to_string(rz);
}

inline std::pair<int, int> WorldStore::chunkToRegion(int cx, int cz, int regionSize)
{
    const int size = std::max(1, regionSize);
    return {
        static_cast<int>(std::floor(static_cast<float>(cx) / static_cast<float>(size))),
        static_cast<int>(std::floor(static_cast<float>(cz) / static_cast<float>(size)))
    };
}

inline std::tuple<float, float, float, float> WorldStore::chunkBounds(float chunkSize, int cx, int cz)
{
    const float x0 = static_cast<float>(cx) * chunkSize;
    const float z0 = static_cast<float>(cz) * chunkSize;
    return { x0, z0, x0 + chunkSize, z0 + chunkSize };
}

inline std::size_t WorldStore::deltaGridLength(int resolution)
{
    const std::size_t axis = static_cast<std::size_t>(resolution + 1);
    return axis * axis;
}

inline WorldChunkState WorldStore::buildEmptyChunk(int cx, int cz, int resolution)
{
    WorldChunkState chunk;
    chunk.cx = cx;
    chunk.cz = cz;
    chunk.resolution = normalizeWorldChunkResolution(resolution);
    chunk.heightDeltas.assign(deltaGridLength(chunk.resolution), 0.0f);
    return chunk;
}

inline WorldChunkState WorldStore::normalizeChunkState(const WorldChunkState& chunk, int fallbackResolution)
{
    WorldChunkState normalized = buildEmptyChunk(chunk.cx, chunk.cz, chunk.resolution <= 0 ? fallbackResolution : chunk.resolution);
    normalized.revision = std::max(0, chunk.revision);
    normalized.materialRevision = std::max(0, chunk.materialRevision);
    for (std::size_t i = 0; i < std::min(normalized.heightDeltas.size(), chunk.heightDeltas.size()); ++i) {
        normalized.heightDeltas[i] = sanitize(chunk.heightDeltas[i], 0.0f);
    }
    normalized.volumetricOverrides.reserve(chunk.volumetricOverrides.size());
    for (const WorldVolumetricOverride& override : chunk.volumetricOverrides) {
        normalized.volumetricOverrides.push_back({
            override.kind.empty() ? "sphere" : override.kind,
            sanitize(override.x, 0.0f),
            sanitize(override.y, 0.0f),
            sanitize(override.z, 0.0f),
            std::max(0.1f, sanitize(override.radius, 1.0f))
        });
    }
    return normalized;
}

inline bool WorldStore::chunkHasMeaningfulData(const WorldChunkState& chunk)
{
    if (chunk.revision > 0) {
        return true;
    }
    for (float delta : chunk.heightDeltas) {
        if (std::fabs(delta) > 1.0e-6f) {
            return true;
        }
    }
    return !chunk.volumetricOverrides.empty();
}

inline float WorldStore::sampleOverrideDistance(float x, float y, float z, const std::vector<WorldVolumetricOverride>& overrides)
{
    float minDistance = std::numeric_limits<float>::infinity();
    for (const WorldVolumetricOverride& override : overrides) {
        if (override.kind != "sphere") {
            continue;
        }
        const float dx = x - override.x;
        const float dy = y - override.y;
        const float dz = z - override.z;
        const float distance = std::sqrt((dx * dx) + (dy * dy) + (dz * dz)) - std::max(0.1f, override.radius);
        minDistance = std::min(minDistance, distance);
    }
    return minDistance;
}

inline std::string WorldStore::timestampUtc()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm utcTime {};
#if defined(_WIN32)
    gmtime_s(&utcTime, &nowTime);
#else
    gmtime_r(&nowTime, &utcTime);
#endif
    char buffer[32] {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utcTime);
    return buffer;
}

inline std::filesystem::path WorldStore::metaPath() const
{
    return rootPath_ / "meta.ini";
}

inline std::filesystem::path WorldStore::regionPath(int rx, int rz) const
{
    return rootPath_ / "regions" / (std::to_string(rx) + "_" + std::to_string(rz) + ".ini");
}

inline std::optional<WorldStore> WorldStore::open(const WorldStoreOptions& options, std::string* error)
{
    WorldStore world;
    world.name_ = sanitizeWorldName(options.name);
    world.storageRoot_ = options.storageRoot.empty() ? (std::filesystem::current_path() / "worlds") : options.storageRoot;
    world.rootPath_ = world.storageRoot_ / world.name_;
    world.regionSize_ = std::clamp(options.regionSize, 4, 64);
    world.chunkResolution_ = normalizeWorldChunkResolution(options.chunkResolution);
    world.chunkSize_ = std::max(8.0f, normalizeTerrainParams(options.groundParams).chunkSize);

    std::error_code ec;
    if (options.createIfMissing) {
        std::filesystem::create_directories(world.rootPath_ / "regions", ec);
        if (ec) {
            if (error != nullptr) {
                *error = "failed to create world storage directory";
            }
            return std::nullopt;
        }
    }

    if (!world.loadOrCreateMeta(options, error)) {
        return std::nullopt;
    }
    return world;
}

inline WorldMeta WorldStore::getMeta() const
{
    return meta_;
}

inline WorldChunkState* WorldStore::getChunk(int cx, int cz, bool create)
{
    const std::string key = buildChunkKey(cx, cz);
    auto chunkIt = chunks_.find(key);
    if (chunkIt != chunks_.end()) {
        return &chunkIt->second;
    }

    const auto [rx, rz] = chunkToRegion(cx, cz, regionSize_);
    if (!loadRegion(rx, rz, nullptr)) {
        return nullptr;
    }

    chunkIt = chunks_.find(key);
    if (chunkIt != chunks_.end()) {
        return &chunkIt->second;
    }
    if (!create) {
        return nullptr;
    }

    chunks_.emplace(key, buildEmptyChunk(cx, cz, chunkResolution_));
    markRegionDirty(rx, rz);
    return &chunks_.find(key)->second;
}

inline std::optional<WorldChunkState> WorldStore::getChunkState(int cx, int cz)
{
    WorldChunkState* chunk = getChunk(cx, cz, false);
    if (chunk == nullptr) {
        return std::nullopt;
    }
    return normalizeChunkState(*chunk, chunkResolution_);
}

inline bool WorldStore::applyWorldInfo(const WorldInfoSnapshot& info, std::string* error)
{
    (void)error;
    meta_.worldId = sanitizeWorldName(info.worldId.empty() ? name_ : info.worldId);
    meta_.formatVersion = std::max(1, info.formatVersion);
    meta_.seed = std::max(1, info.seed);
    meta_.terrainProfile.chunkSize = std::max(8.0f, sanitize(info.chunkSize, meta_.terrainProfile.chunkSize));
    meta_.terrainProfile.worldRadius = std::max(meta_.terrainProfile.chunkSize * 4.0f, sanitize(info.horizonRadiusMeters, meta_.terrainProfile.worldRadius));
    meta_.terrainProfile.heightAmplitude = sanitize(info.heightAmplitude, meta_.terrainProfile.heightAmplitude);
    meta_.terrainProfile.heightFrequency = std::max(1.0e-5f, sanitize(info.heightFrequency, meta_.terrainProfile.heightFrequency));
    meta_.terrainProfile.waterLevel = sanitize(info.waterLevel, meta_.terrainProfile.waterLevel);
    meta_.spawn = {
        sanitize(info.spawnX, meta_.spawn.x),
        sanitize(info.spawnY, meta_.spawn.y),
        sanitize(info.spawnZ, meta_.spawn.z)
    };
    meta_.tunnelSeeds.clear();
    for (const TerrainTunnelSeed& seed : info.tunnelSeeds) {
        TerrainTunnelSeed normalizedSeed = sanitizeTunnelSeed(seed);
        if (!normalizedSeed.points.empty()) {
            meta_.tunnelSeeds.push_back(std::move(normalizedSeed));
        }
    }
    meta_.tunnelProfile.count = static_cast<int>(meta_.tunnelSeeds.size());
    meta_.tunnelProfile.hillAttached = false;
    for (const TerrainTunnelSeed& seed : meta_.tunnelSeeds) {
        meta_.tunnelProfile.hillAttached = meta_.tunnelProfile.hillAttached || seed.hillAttached;
    }
    chunkSize_ = std::max(8.0f, meta_.terrainProfile.chunkSize);
    touchMeta();
    return saveMeta(error);
}

inline bool WorldStore::applyChunkState(const WorldChunkState& state)
{
    const WorldChunkState incoming = normalizeChunkState(state, chunkResolution_);
    const std::string key = buildChunkKey(incoming.cx, incoming.cz);
    const auto currentIt = chunks_.find(key);
    const int currentRevision = currentIt == chunks_.end() ? 0 : std::max(0, currentIt->second.revision);
    if (currentIt != chunks_.end() && incoming.revision < currentRevision) {
        return false;
    }

    const auto [rx, rz] = chunkToRegion(incoming.cx, incoming.cz, regionSize_);
    if (!loadRegion(rx, rz, nullptr)) {
        return false;
    }

    chunks_[key] = incoming;
    markRegionDirty(rx, rz);
    touchMeta();
    return true;
}

inline float WorldStore::sampleHeightDelta(float x, float z)
{
    const float chunkSize = std::max(1.0f, chunkSize_);
    const int resolution = chunkResolution_;
    const int axis = resolution + 1;
    const int cx = static_cast<int>(std::floor(x / chunkSize));
    const int cz = static_cast<int>(std::floor(z / chunkSize));
    WorldChunkState* chunk = getChunk(cx, cz, false);
    if (chunk == nullptr) {
        return 0.0f;
    }

    const float localX = (x - (static_cast<float>(cx) * chunkSize)) / chunkSize;
    const float localZ = (z - (static_cast<float>(cz) * chunkSize)) / chunkSize;
    const float fx = clamp(localX, 0.0f, 1.0f) * static_cast<float>(resolution);
    const float fz = clamp(localZ, 0.0f, 1.0f) * static_cast<float>(resolution);
    const int ix = std::clamp(static_cast<int>(std::floor(fx)), 0, resolution);
    const int iz = std::clamp(static_cast<int>(std::floor(fz)), 0, resolution);
    const float tx = clamp(fx - static_cast<float>(ix), 0.0f, 1.0f);
    const float tz = clamp(fz - static_cast<float>(iz), 0.0f, 1.0f);
    const int ix1 = std::min(resolution, ix + 1);
    const int iz1 = std::min(resolution, iz + 1);
    const auto gridValue = [&](int gx, int gz) {
        const std::size_t index = static_cast<std::size_t>(gz * axis + gx);
        if (index >= chunk->heightDeltas.size()) {
            return 0.0f;
        }
        return chunk->heightDeltas[index];
    };
    const float v00 = gridValue(ix, iz);
    const float v10 = gridValue(ix1, iz);
    const float v01 = gridValue(ix, iz1);
    const float v11 = gridValue(ix1, iz1);
    return mix(mix(v00, v10, tx), mix(v01, v11, tx), tz);
}

inline float WorldStore::sampleVolumetricOverrideSdf(float x, float y, float z)
{
    const int cx = static_cast<int>(std::floor(x / std::max(1.0f, chunkSize_)));
    const int cz = static_cast<int>(std::floor(z / std::max(1.0f, chunkSize_)));
    float minDistance = std::numeric_limits<float>::infinity();
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            WorldChunkState* chunk = getChunk(cx + dx, cz + dz, false);
            if (chunk == nullptr || chunk->volumetricOverrides.empty()) {
                continue;
            }
            minDistance = std::min(minDistance, sampleOverrideDistance(x, y, z, chunk->volumetricOverrides));
        }
    }
    return minDistance;
}

inline std::uint64_t WorldStore::revisionSignatureForBounds(float x0, float z0, float x1, float z1, int neighborRing)
{
    const float chunkSize = std::max(1.0f, chunkSize_);
    const int ring = std::max(0, neighborRing);
    const int minCx = static_cast<int>(std::floor(std::min(x0, x1) / chunkSize)) - ring;
    const int maxCx = static_cast<int>(std::floor(std::max(x0, x1) / chunkSize)) + ring;
    const int minCz = static_cast<int>(std::floor(std::min(z0, z1) / chunkSize)) - ring;
    const int maxCz = static_cast<int>(std::floor(std::max(z0, z1) / chunkSize)) + ring;

    auto mixHash = [](std::uint64_t hash, std::uint64_t value) {
        hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
        return hash;
    };

    std::uint64_t hash = 1469598103934665603ull;
    hash = mixHash(hash, static_cast<std::uint64_t>(std::max(1, meta_.seed)));
    hash = mixHash(hash, static_cast<std::uint64_t>(std::max(1, meta_.formatVersion)));
    for (int cz = minCz; cz <= maxCz; ++cz) {
        for (int cx = minCx; cx <= maxCx; ++cx) {
            WorldChunkState* chunk = getChunk(cx, cz, false);
            hash = mixHash(hash, static_cast<std::uint64_t>(static_cast<std::uint32_t>(cx)));
            hash = mixHash(hash, static_cast<std::uint64_t>(static_cast<std::uint32_t>(cz)));
            if (chunk != nullptr) {
                hash = mixHash(hash, static_cast<std::uint64_t>(std::max(0, chunk->revision)));
                hash = mixHash(hash, static_cast<std::uint64_t>(std::max(0, chunk->materialRevision)));
                hash = mixHash(hash, static_cast<std::uint64_t>(chunk->heightDeltas.size()));
                hash = mixHash(hash, static_cast<std::uint64_t>(chunk->volumetricOverrides.size()));
            }
        }
    }
    return hash;
}

inline std::vector<WorldChunkState> WorldStore::collectEditedChunks(int centerCx, int centerCz, int radiusChunks)
{
    std::vector<WorldChunkState> out;
    const int radius = std::max(0, radiusChunks);
    for (int dz = -radius; dz <= radius; ++dz) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (auto chunk = getChunkState(centerCx + dx, centerCz + dz); chunk.has_value() && chunkHasMeaningfulData(*chunk)) {
                out.push_back(std::move(*chunk));
            }
        }
    }
    return out;
}

inline std::pair<bool, std::vector<WorldChunkState>> WorldStore::applyCrater(const TerrainCrater& craterSpec)
{
    const TerrainCrater crater = sanitizeCrater(craterSpec);
    const float radius = crater.radius;
    const float chunkSize = std::max(1.0f, chunkSize_);
    const int minCx = static_cast<int>(std::floor((crater.x - (radius * 1.4f)) / chunkSize));
    const int maxCx = static_cast<int>(std::floor((crater.x + (radius * 1.4f)) / chunkSize));
    const int minCz = static_cast<int>(std::floor((crater.z - (radius * 1.4f)) / chunkSize));
    const int maxCz = static_cast<int>(std::floor((crater.z + (radius * 1.4f)) / chunkSize));

    std::vector<WorldChunkState> changed;
    for (int cz = minCz; cz <= maxCz; ++cz) {
        for (int cx = minCx; cx <= maxCx; ++cx) {
            WorldChunkState* chunk = getChunk(cx, cz, true);
            if (chunk == nullptr) {
                continue;
            }

            const int resolution = chunk->resolution;
            const int axis = resolution + 1;
            const auto [x0, z0, x1, z1] = chunkBounds(chunkSize, cx, cz);
            (void)x1;
            (void)z1;
            bool touched = false;
            for (int gz = 0; gz <= resolution; ++gz) {
                const float worldZ = z0 + (static_cast<float>(gz) / static_cast<float>(resolution)) * chunkSize;
                for (int gx = 0; gx <= resolution; ++gx) {
                    const float worldX = x0 + (static_cast<float>(gx) / static_cast<float>(resolution)) * chunkSize;
                    const float dx = worldX - crater.x;
                    const float dz = worldZ - crater.z;
                    const float dist = std::sqrt((dx * dx) + (dz * dz));
                    const float t = dist / radius;
                    float delta = 0.0f;
                    if (t < 1.0f) {
                        const float bowl = 1.0f - (t * t);
                        delta = -crater.depth * bowl * bowl;
                    } else if (t < 1.24f) {
                        const float rimT = (t - 1.0f) / 0.24f;
                        const float rimAlpha = 1.0f - rimT;
                        delta = radius * crater.rim * rimAlpha * rimAlpha;
                    }
                    if (std::fabs(delta) > 1.0e-6f) {
                        const std::size_t index = static_cast<std::size_t>(gz * axis + gx);
                        if (index < chunk->heightDeltas.size()) {
                            chunk->heightDeltas[index] += delta;
                            touched = true;
                        }
                    }
                }
            }

            if (touched) {
                chunk->revision = std::max(0, chunk->revision) + 1;
                chunk->materialRevision = std::max(0, chunk->materialRevision) + 1;
                const auto [rx, rz] = chunkToRegion(cx, cz, regionSize_);
                markRegionDirty(rx, rz);
                changed.push_back(normalizeChunkState(*chunk, chunkResolution_));
            }
        }
    }

    if (!changed.empty()) {
        touchMeta();
        saveMeta(nullptr);
        return { true, changed };
    }
    return { false, {} };
}

inline int WorldStore::flushDirty(std::string* error)
{
    int flushed = 0;
    std::vector<std::string> regionKeys(dirtyRegions_.begin(), dirtyRegions_.end());
    std::sort(regionKeys.begin(), regionKeys.end());
    for (const std::string& key : regionKeys) {
        const std::size_t split = key.find(':');
        if (split == std::string::npos) {
            continue;
        }
        const int rx = parseInt(key.substr(0, split), 0);
        const int rz = parseInt(key.substr(split + 1u), 0);
        if (!writeRegion(rx, rz, error)) {
            return flushed;
        }
        dirtyRegions_.erase(key);
        ++flushed;
    }
    if (flushed > 0) {
        saveMeta(error);
    }
    return flushed;
}

inline WorldGroundParams WorldStore::buildGroundParams(const TerrainParams& baseParams) const
{
    WorldGroundParams out;
    out.terrainParams = normalizeTerrainParams(baseParams);
    out.terrainParams.seed = std::max(1, meta_.seed);
    out.terrainParams.chunkSize = std::max(8.0f, meta_.terrainProfile.chunkSize);
    out.terrainParams.worldRadius = std::max(out.terrainParams.chunkSize * 4.0f, meta_.terrainProfile.worldRadius);
    out.terrainParams.heightAmplitude = meta_.terrainProfile.heightAmplitude;
    out.terrainParams.heightFrequency = std::max(1.0e-5f, meta_.terrainProfile.heightFrequency);
    out.terrainParams.waterLevel = meta_.terrainProfile.waterLevel;
    out.terrainParams.explicitTunnelSeeds = meta_.tunnelSeeds;
    out.terrainParams.tunnelCount = static_cast<int>(meta_.tunnelSeeds.size());
    out.terrainParams = normalizeTerrainParams(out.terrainParams);
    out.worldId = meta_.worldId;
    out.worldFormatVersion = meta_.formatVersion;
    out.tunnelSeedCount = static_cast<int>(meta_.tunnelSeeds.size());
    out.tunnelSeeds = meta_.tunnelSeeds;
    return out;
}

inline std::map<std::string, std::string> WorldStore::readKeyValueFile(const std::filesystem::path& path)
{
    std::map<std::string, std::string> values;
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return values;
    }

    std::string line;
    while (std::getline(input, line)) {
        const std::size_t split = line.find('=');
        if (split == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, split));
        if (key.empty()) {
            continue;
        }
        values[key] = trim(line.substr(split + 1u));
    }
    return values;
}

inline bool WorldStore::writeKeyValueFile(const std::filesystem::path& path, const std::map<std::string, std::string>& values, std::string* error)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        if (error != nullptr) {
            *error = "failed to create directory for world file";
        }
        return false;
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        if (error != nullptr) {
            *error = "failed to open world file for writing";
        }
        return false;
    }

    for (const auto& [key, value] : values) {
        output << key << '=' << value << '\n';
    }
    if (!output.good()) {
        if (error != nullptr) {
            *error = "failed while writing world file";
        }
        return false;
    }
    return true;
}

inline bool WorldStore::loadOrCreateMeta(const WorldStoreOptions& options, std::string* error)
{
    const std::map<std::string, std::string> stored = readKeyValueFile(metaPath());
    if (stored.empty()) {
        if (!options.createIfMissing) {
            if (error != nullptr) {
                *error = "world not found";
            }
            return false;
        }

        const TerrainParams params = normalizeTerrainParams(options.groundParams);
        const TerrainFieldContext context = createTerrainFieldContext(params);
        meta_.worldId = name_;
        meta_.formatVersion = kFormatVersion;
        meta_.seed = params.seed;
        meta_.terrainProfile.chunkSize = params.chunkSize;
        meta_.terrainProfile.worldRadius = params.worldRadius;
        meta_.terrainProfile.heightAmplitude = params.heightAmplitude;
        meta_.terrainProfile.heightFrequency = params.heightFrequency;
        meta_.terrainProfile.waterLevel = params.waterLevel;
        meta_.spawn = options.spawn;
        meta_.tunnelProfile.count = static_cast<int>(context.tunnelSeeds.size());
        meta_.tunnelProfile.radiusMin = params.tunnelRadiusMin;
        meta_.tunnelProfile.radiusMax = params.tunnelRadiusMax;
        meta_.tunnelProfile.lengthMin = params.tunnelLengthMin;
        meta_.tunnelProfile.lengthMax = params.tunnelLengthMax;
        meta_.tunnelProfile.hillAttached = false;
        meta_.tunnelSeeds = context.tunnelSeeds;
        meta_.chunkResolution = chunkResolution_;
        meta_.regionSize = regionSize_;
        meta_.createdAt = timestampUtc();
        meta_.updatedAt = meta_.createdAt;
        return saveMeta(error);
    }

    meta_.worldId = sanitizeWorldName(valueOr(stored, "world_id", name_));
    meta_.formatVersion = std::max(1, parseInt(valueOr(stored, "format_version", "1"), kFormatVersion));
    meta_.seed = std::max(1, parseInt(valueOr(stored, "seed", "1"), 1));
    meta_.terrainProfile.chunkSize = std::max(8.0f, parseFloat(valueOr(stored, "terrain.chunk_size", "128"), 128.0f));
    meta_.terrainProfile.worldRadius = std::max(meta_.terrainProfile.chunkSize * 4.0f, parseFloat(valueOr(stored, "terrain.world_radius", "2048"), 2048.0f));
    meta_.terrainProfile.heightAmplitude = parseFloat(valueOr(stored, "terrain.height_amplitude", "120"), 120.0f);
    meta_.terrainProfile.heightFrequency = std::max(1.0e-5f, parseFloat(valueOr(stored, "terrain.height_frequency", "0.0018"), 0.0018f));
    meta_.terrainProfile.waterLevel = parseFloat(valueOr(stored, "terrain.water_level", "-12"), -12.0f);
    meta_.spawn = {
        parseFloat(valueOr(stored, "spawn.x", "0"), 0.0f),
        parseFloat(valueOr(stored, "spawn.y", "0"), 0.0f),
        parseFloat(valueOr(stored, "spawn.z", "0"), 0.0f)
    };
    meta_.tunnelProfile.count = std::max(0, parseInt(valueOr(stored, "tunnel.count", "0"), 0));
    meta_.tunnelProfile.radiusMin = parseFloat(valueOr(stored, "tunnel.radius_min", "9"), 9.0f);
    meta_.tunnelProfile.radiusMax = parseFloat(valueOr(stored, "tunnel.radius_max", "18"), 18.0f);
    meta_.tunnelProfile.lengthMin = parseFloat(valueOr(stored, "tunnel.length_min", "240"), 240.0f);
    meta_.tunnelProfile.lengthMax = parseFloat(valueOr(stored, "tunnel.length_max", "520"), 520.0f);
    meta_.tunnelProfile.hillAttached = parseInt(valueOr(stored, "tunnel.hill_attached", "0"), 0) != 0;
    meta_.tunnelSeeds = decodeTunnelSeeds(valueOr(stored, "tunnel.seeds", ""));
    meta_.chunkResolution = normalizeWorldChunkResolution(parseInt(valueOr(stored, "chunk_resolution", "16"), chunkResolution_));
    meta_.regionSize = std::clamp(parseInt(valueOr(stored, "region_size", "16"), regionSize_), 4, 64);
    meta_.createdAt = valueOr(stored, "created_at", timestampUtc());
    meta_.updatedAt = valueOr(stored, "updated_at", meta_.createdAt);
    regionSize_ = meta_.regionSize;
    chunkResolution_ = meta_.chunkResolution;
    chunkSize_ = meta_.terrainProfile.chunkSize;
    return true;
}

inline bool WorldStore::saveMeta(std::string* error) const
{
    std::map<std::string, std::string> values;
    values["chunk_resolution"] = std::to_string(meta_.chunkResolution);
    values["created_at"] = meta_.createdAt;
    values["format_version"] = std::to_string(meta_.formatVersion);
    values["region_size"] = std::to_string(meta_.regionSize);
    values["seed"] = std::to_string(meta_.seed);
    values["spawn.x"] = formatFloat(meta_.spawn.x);
    values["spawn.y"] = formatFloat(meta_.spawn.y);
    values["spawn.z"] = formatFloat(meta_.spawn.z);
    values["terrain.chunk_size"] = formatFloat(meta_.terrainProfile.chunkSize);
    values["terrain.height_amplitude"] = formatFloat(meta_.terrainProfile.heightAmplitude);
    values["terrain.height_frequency"] = formatFloat(meta_.terrainProfile.heightFrequency);
    values["terrain.water_level"] = formatFloat(meta_.terrainProfile.waterLevel);
    values["terrain.world_radius"] = formatFloat(meta_.terrainProfile.worldRadius);
    values["tunnel.count"] = std::to_string(meta_.tunnelProfile.count);
    values["tunnel.hill_attached"] = meta_.tunnelProfile.hillAttached ? "1" : "0";
    values["tunnel.length_max"] = formatFloat(meta_.tunnelProfile.lengthMax);
    values["tunnel.length_min"] = formatFloat(meta_.tunnelProfile.lengthMin);
    values["tunnel.radius_max"] = formatFloat(meta_.tunnelProfile.radiusMax);
    values["tunnel.radius_min"] = formatFloat(meta_.tunnelProfile.radiusMin);
    values["tunnel.seeds"] = encodeTunnelSeeds(meta_.tunnelSeeds);
    values["updated_at"] = meta_.updatedAt;
    values["world_id"] = meta_.worldId;
    return writeKeyValueFile(metaPath(), values, error);
}

inline bool WorldStore::loadRegion(int rx, int rz, std::string* error)
{
    const std::string key = buildRegionKey(rx, rz);
    if (loadedRegions_.find(key) != loadedRegions_.end()) {
        return true;
    }

    const std::map<std::string, std::string> values = readKeyValueFile(regionPath(rx, rz));
    const int chunkCount = std::max(0, parseInt(valueOr(values, "chunk_count", "0"), 0));
    for (int index = 0; index < chunkCount; ++index) {
        const std::string prefix = "chunk" + std::to_string(index) + ".";
        std::unordered_map<std::string, std::string> chunkFields;
        for (const auto& [fieldKey, fieldValue] : values) {
            if (fieldKey.rfind(prefix, 0u) == 0u) {
                chunkFields[fieldKey.substr(prefix.size())] = fieldValue;
            }
        }
        if (chunkFields.empty()) {
            continue;
        }
        const WorldChunkState chunk = normalizeChunkState(decodeChunkStateFields(chunkFields), chunkResolution_);
        chunks_[buildChunkKey(chunk.cx, chunk.cz)] = chunk;
    }

    loadedRegions_.insert(key);
    (void)error;
    return true;
}

inline bool WorldStore::writeRegion(int rx, int rz, std::string* error) const
{
    std::vector<WorldChunkState> regionChunks;
    for (const auto& [key, chunk] : chunks_) {
        const auto [chunkRx, chunkRz] = chunkToRegion(chunk.cx, chunk.cz, regionSize_);
        if (chunkRx == rx && chunkRz == rz && chunkHasMeaningfulData(chunk)) {
            regionChunks.push_back(normalizeChunkState(chunk, chunkResolution_));
        }
    }
    std::sort(regionChunks.begin(), regionChunks.end(), [](const WorldChunkState& lhs, const WorldChunkState& rhs) {
        if (lhs.cz == rhs.cz) {
            return lhs.cx < rhs.cx;
        }
        return lhs.cz < rhs.cz;
    });

    std::map<std::string, std::string> values;
    values["chunk_count"] = std::to_string(regionChunks.size());
    values["format_version"] = std::to_string(kFormatVersion);
    for (std::size_t index = 0; index < regionChunks.size(); ++index) {
        const std::string prefix = "chunk" + std::to_string(index) + ".";
        for (const auto& [fieldKey, fieldValue] : buildChunkStateFields(regionChunks[index])) {
            values[prefix + fieldKey] = fieldValue;
        }
    }
    return writeKeyValueFile(regionPath(rx, rz), values, error);
}

inline void WorldStore::markRegionDirty(int rx, int rz)
{
    dirtyRegions_.insert(buildRegionKey(rx, rz));
}

inline void WorldStore::touchMeta()
{
    if (meta_.createdAt.empty()) {
        meta_.createdAt = timestampUtc();
    }
    meta_.updatedAt = timestampUtc();
}

inline std::string WorldStore::valueOr(const std::map<std::string, std::string>& values, const char* key, const std::string& fallback)
{
    const auto it = values.find(key);
    return it == values.end() ? fallback : it->second;
}


}  // namespace NativeGame
