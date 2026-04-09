#pragma once

#include "NativeGame/World.hpp"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace NativeGame {

struct WorldVolumetricOverride {
    std::string kind = "sphere";
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float radius = 1.0f;
};

struct WorldChunkState {
    int cx = 0;
    int cz = 0;
    int resolution = 16;
    int revision = 0;
    int materialRevision = 0;
    WorldShape worldShape = WorldShape::Plane;
    PlanetConfig planet {};
    PlanetTileId planetTile {};
    std::vector<float> heightDeltas;
    std::vector<WorldVolumetricOverride> volumetricOverrides;
};

using WorldKeyValueFields = std::vector<std::pair<std::string, std::string>>;

inline int normalizeWorldChunkResolution(int value)
{
    return std::clamp(value, 4, 64);
}

inline std::string formatWorldWireFloat(float value)
{
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream << std::setprecision(4) << sanitize(value, 0.0f);
    return stream.str();
}

inline int parseWorldWireInt(const std::string& value, int fallback)
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

inline float parseWorldWireFloat(const std::string& value, float fallback)
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

inline std::vector<std::string> splitWorldWireToken(const std::string& value, char delimiter)
{
    std::vector<std::string> out;
    if (value.empty()) {
        return out;
    }
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t split = value.find(delimiter, start);
        if (split == std::string::npos) {
            out.push_back(value.substr(start));
            break;
        }
        out.push_back(value.substr(start, split - start));
        start = split + 1;
    }
    return out;
}

inline std::string encodeHeightDeltas(const std::vector<float>& values)
{
    if (values.empty()) {
        return {};
    }
    std::ostringstream stream;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            stream << ',';
        }
        stream << formatWorldWireFloat(values[i]);
    }
    return stream.str();
}

inline std::vector<float> decodeHeightDeltas(const std::string& value)
{
    std::vector<float> out;
    for (const std::string& token : splitWorldWireToken(value, ',')) {
        if (!token.empty()) {
            out.push_back(parseWorldWireFloat(token, 0.0f));
        }
    }
    return out;
}

inline std::string encodeVolumetricOverrides(const std::vector<WorldVolumetricOverride>& overrides)
{
    if (overrides.empty()) {
        return {};
    }
    std::ostringstream stream;
    bool wroteAny = false;
    for (const WorldVolumetricOverride& override : overrides) {
        if (wroteAny) {
            stream << ';';
        }
        stream << (override.kind.empty() ? "sphere" : override.kind)
               << ',' << formatWorldWireFloat(override.x)
               << ',' << formatWorldWireFloat(override.y)
               << ',' << formatWorldWireFloat(override.z)
               << ',' << formatWorldWireFloat(std::max(0.1f, override.radius));
        wroteAny = true;
    }
    return stream.str();
}

inline std::vector<WorldVolumetricOverride> decodeVolumetricOverrides(const std::string& value)
{
    std::vector<WorldVolumetricOverride> out;
    for (const std::string& entry : splitWorldWireToken(value, ';')) {
        const std::vector<std::string> parts = splitWorldWireToken(entry, ',');
        if (parts.size() != 5u) {
            continue;
        }
        out.push_back({
            parts[0].empty() ? "sphere" : parts[0],
            parseWorldWireFloat(parts[1], 0.0f),
            parseWorldWireFloat(parts[2], 0.0f),
            parseWorldWireFloat(parts[3], 0.0f),
            std::max(0.1f, parseWorldWireFloat(parts[4], 1.0f))
        });
    }
    return out;
}

inline std::string encodeTunnelSeeds(const std::vector<TerrainTunnelSeed>& seeds)
{
    if (seeds.empty()) {
        return {};
    }
    std::ostringstream stream;
    bool wroteAny = false;
    for (const TerrainTunnelSeed& seed : seeds) {
        if (seed.points.empty()) {
            continue;
        }
        if (wroteAny) {
            stream << ';';
        }
        stream << formatWorldWireFloat(seed.radius) << ':' << (seed.hillAttached ? '1' : '0') << ':';
        for (std::size_t pointIndex = 0; pointIndex < seed.points.size(); ++pointIndex) {
            if (pointIndex > 0) {
                stream << '~';
            }
            const Vec3& point = seed.points[pointIndex];
            stream << formatWorldWireFloat(point.x)
                   << ',' << formatWorldWireFloat(point.y)
                   << ',' << formatWorldWireFloat(point.z);
        }
        wroteAny = true;
    }
    return stream.str();
}

inline std::vector<TerrainTunnelSeed> decodeTunnelSeeds(const std::string& value)
{
    std::vector<TerrainTunnelSeed> out;
    for (const std::string& entry : splitWorldWireToken(value, ';')) {
        const std::size_t firstColon = entry.find(':');
        const std::size_t secondColon = entry.find(':', firstColon == std::string::npos ? std::string::npos : firstColon + 1u);
        if (firstColon == std::string::npos || secondColon == std::string::npos) {
            continue;
        }

        TerrainTunnelSeed seed;
        seed.radius = std::max(0.1f, parseWorldWireFloat(entry.substr(0, firstColon), 1.0f));
        seed.hillAttached = parseWorldWireInt(entry.substr(firstColon + 1u, secondColon - firstColon - 1u), 0) != 0;
        for (const std::string& pointToken : splitWorldWireToken(entry.substr(secondColon + 1u), '~')) {
            const std::vector<std::string> parts = splitWorldWireToken(pointToken, ',');
            if (parts.size() != 3u) {
                continue;
            }
            seed.points.push_back({
                parseWorldWireFloat(parts[0], 0.0f),
                parseWorldWireFloat(parts[1], 0.0f),
                parseWorldWireFloat(parts[2], 0.0f)
            });
        }
        if (!seed.points.empty()) {
            out.push_back(std::move(seed));
        }
    }
    return out;
}

inline WorldKeyValueFields buildChunkStateFields(const WorldChunkState& chunk)
{
    return {
        { "cx", std::to_string(chunk.cx) },
        { "cz", std::to_string(chunk.cz) },
        { "resolution", std::to_string(normalizeWorldChunkResolution(chunk.resolution)) },
        { "revision", std::to_string(std::max(0, chunk.revision)) },
        { "materialRevision", std::to_string(std::max(0, chunk.materialRevision)) },
        { "worldShape", std::to_string(static_cast<int>(chunk.worldShape)) },
        { "planet.radius_m", formatWorldWireFloat(static_cast<float>(chunk.planet.radiusMeters)) },
        { "planet.mu", formatWorldWireFloat(static_cast<float>(chunk.planet.gravitationalParameter)) },
        { "planet.rotation_rate", formatWorldWireFloat(static_cast<float>(chunk.planet.rotationRateRadPerSec)) },
        { "planet.atmosphere_height_m", formatWorldWireFloat(static_cast<float>(chunk.planet.atmosphereHeightMeters)) },
        { "planet.origin_lat_deg", formatWorldWireFloat(static_cast<float>(chunk.planet.localOrigin.latitudeDeg)) },
        { "planet.origin_lon_deg", formatWorldWireFloat(static_cast<float>(chunk.planet.localOrigin.longitudeDeg)) },
        { "planet.origin_alt_m", formatWorldWireFloat(static_cast<float>(chunk.planet.localOrigin.altitudeMeters)) },
        { "planetTile.face", std::to_string(chunk.planetTile.face) },
        { "planetTile.lod", std::to_string(chunk.planetTile.lod) },
        { "planetTile.tx", std::to_string(chunk.planetTile.tx) },
        { "planetTile.ty", std::to_string(chunk.planetTile.ty) },
        { "heightDeltas", encodeHeightDeltas(chunk.heightDeltas) },
        { "volumetricOverrides", encodeVolumetricOverrides(chunk.volumetricOverrides) }
    };
}

inline std::unordered_map<std::string, std::string> buildChunkFieldLookup(const WorldKeyValueFields& fields)
{
    std::unordered_map<std::string, std::string> lookup;
    lookup.reserve(fields.size());
    for (const auto& [key, value] : fields) {
        lookup[key] = value;
    }
    return lookup;
}

inline WorldChunkState decodeChunkStateFields(const std::unordered_map<std::string, std::string>& kv)
{
    const auto lookupValue = [&kv](const char* key) -> std::string {
        const auto it = kv.find(key);
        return it == kv.end() ? std::string {} : it->second;
    };

    WorldChunkState chunk;
    chunk.cx = parseWorldWireInt(lookupValue("cx"), 0);
    chunk.cz = parseWorldWireInt(lookupValue("cz"), 0);
    chunk.resolution = normalizeWorldChunkResolution(parseWorldWireInt(lookupValue("resolution"), 16));
    chunk.revision = std::max(0, parseWorldWireInt(lookupValue("revision"), 0));
    chunk.materialRevision = std::max(0, parseWorldWireInt(lookupValue("materialRevision"), 0));
    chunk.worldShape =
        parseWorldWireInt(lookupValue("worldShape"), 0) == static_cast<int>(WorldShape::Planet)
            ? WorldShape::Planet
            : WorldShape::Plane;
    chunk.planet.radiusMeters = std::max(1000.0, static_cast<double>(parseWorldWireFloat(lookupValue("planet.radius_m"), 6371000.0f)));
    chunk.planet.gravitationalParameter = std::max(1.0, static_cast<double>(parseWorldWireFloat(lookupValue("planet.mu"), 3.986004418e14f)));
    chunk.planet.rotationRateRadPerSec = static_cast<double>(parseWorldWireFloat(lookupValue("planet.rotation_rate"), 0.000072921159f));
    chunk.planet.atmosphereHeightMeters = std::max(1000.0, static_cast<double>(parseWorldWireFloat(lookupValue("planet.atmosphere_height_m"), 120000.0f)));
    chunk.planet.localOrigin = normalizeGeodetic({
        static_cast<double>(parseWorldWireFloat(lookupValue("planet.origin_lat_deg"), 0.0f)),
        static_cast<double>(parseWorldWireFloat(lookupValue("planet.origin_lon_deg"), 0.0f)),
        static_cast<double>(parseWorldWireFloat(lookupValue("planet.origin_alt_m"), 0.0f))
    });
    chunk.planetTile.face = parseWorldWireInt(lookupValue("planetTile.face"), 0);
    chunk.planetTile.lod = parseWorldWireInt(lookupValue("planetTile.lod"), 0);
    chunk.planetTile.tx = parseWorldWireInt(lookupValue("planetTile.tx"), 0);
    chunk.planetTile.ty = parseWorldWireInt(lookupValue("planetTile.ty"), 0);
    chunk.heightDeltas = decodeHeightDeltas(lookupValue("heightDeltas"));
    chunk.volumetricOverrides = decodeVolumetricOverrides(lookupValue("volumetricOverrides"));
    return chunk;
}

}  // namespace NativeGame
