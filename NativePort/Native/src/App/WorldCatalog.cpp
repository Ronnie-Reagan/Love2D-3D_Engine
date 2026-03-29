#include "App/WorldCatalog.hpp"

#include "App/Preferences.hpp"

namespace TrueFlightApp {

namespace {

std::uintmax_t directorySizeBytes(const std::filesystem::path& root)
{
    if (root.empty()) {
        return 0u;
    }

    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) {
        return 0u;
    }

    std::uintmax_t totalBytes = 0u;
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)) {
            continue;
        }
        totalBytes += it->file_size(ec);
    }
    return totalBytes;
}

}  // namespace

std::string sanitizeWorldInstanceName(std::string_view value)
{
    std::string out;
    out.reserve(value.size());
    const std::string fallback = value.empty() ? std::string("native_default") : std::string(value);
    for (const unsigned char ch : fallback) {
        if (std::isalnum(ch) != 0 || ch == '_' || ch == '-') {
            out.push_back(static_cast<char>(std::tolower(ch)));
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
    return out.empty() ? std::string("native_default") : out;
}

std::vector<WorldInstanceSummary> scanWorldInstances()
{
    std::vector<WorldInstanceSummary> worlds;
    const std::filesystem::path storageRoot = getWorldStorageDirectory();
    std::error_code ec;
    if (!storageRoot.empty() && std::filesystem::exists(storageRoot, ec)) {
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(storageRoot, ec)) {
            if (ec || !entry.is_directory()) {
                continue;
            }

            WorldStoreOptions options;
            options.name = entry.path().filename().string();
            options.storageRoot = storageRoot;
            options.createIfMissing = false;
            options.regionSize = 8;
            options.chunkResolution = 16;
            options.groundParams = defaultTerrainParams();

            std::string worldError;
            const std::optional<WorldStore> openedWorld = WorldStore::open(options, &worldError);
            if (!openedWorld.has_value()) {
                continue;
            }

            const WorldMeta meta = openedWorld->getMeta();
            WorldInstanceSummary summary;
            summary.worldId = meta.worldId.empty() ? options.name : meta.worldId;
            summary.seed = meta.seed;
            summary.chunkSize = meta.terrainProfile.chunkSize;
            summary.worldRadius = meta.terrainProfile.worldRadius;
            summary.waterLevel = meta.terrainProfile.waterLevel;
            summary.tunnelCount = static_cast<int>(meta.tunnelSeeds.size());
            summary.createdAt = meta.createdAt;
            summary.updatedAt = meta.updatedAt;
            summary.cacheBytes = directorySizeBytes(getTerrainChunkCacheDirectory() / summary.worldId);
            summary.persistent = true;
            worlds.push_back(std::move(summary));
        }
    }

    std::sort(
        worlds.begin(),
        worlds.end(),
        [](const WorldInstanceSummary& lhs, const WorldInstanceSummary& rhs) {
            if (lhs.updatedAt != rhs.updatedAt) {
                return lhs.updatedAt > rhs.updatedAt;
            }
            return lhs.worldId < rhs.worldId;
        });

    if (worlds.empty()) {
        worlds.push_back({});
    }
    return worlds;
}

void refreshWorldInstanceCatalog(BootResources& boot)
{
    boot.worldInstances = scanWorldInstances();
    const std::string desiredWorldId = sanitizeWorldInstanceName(boot.selectedWorldId);
    const auto selectedIt = std::find_if(
        boot.worldInstances.begin(),
        boot.worldInstances.end(),
        [&](const WorldInstanceSummary& summary) {
            return summary.worldId == desiredWorldId;
        });
    if (selectedIt != boot.worldInstances.end()) {
        boot.selectedWorldId = selectedIt->worldId;
        return;
    }
    boot.selectedWorldId = boot.worldInstances.empty() ? std::string("native_default") : boot.worldInstances.front().worldId;
}

int selectedWorldInstanceIndex(const BootResources& boot)
{
    for (std::size_t index = 0; index < boot.worldInstances.size(); ++index) {
        if (boot.worldInstances[static_cast<std::size_t>(index)].worldId == boot.selectedWorldId) {
            return static_cast<int>(index);
        }
    }
    return boot.worldInstances.empty() ? -1 : 0;
}

const WorldInstanceSummary* selectedWorldInstance(const BootResources& boot)
{
    const int index = selectedWorldInstanceIndex(boot);
    return index >= 0 && index < static_cast<int>(boot.worldInstances.size())
        ? &boot.worldInstances[static_cast<std::size_t>(index)]
        : nullptr;
}

void cycleSelectedWorldInstance(BootResources& boot, int direction)
{
    if (boot.worldInstances.empty() || direction == 0) {
        return;
    }
    const int count = static_cast<int>(boot.worldInstances.size());
    const int currentIndex = std::max(0, selectedWorldInstanceIndex(boot));
    const int nextIndex = (currentIndex + direction + count) % count;
    boot.selectedWorldId = boot.worldInstances[static_cast<std::size_t>(nextIndex)].worldId;
}

std::string makeSuggestedWorldId(const BootResources& boot)
{
    for (int attempt = 1; attempt <= 999; ++attempt) {
        const std::string candidate = std::string("world_") + (attempt < 10 ? "0" : "") + std::to_string(attempt);
        const auto it = std::find_if(
            boot.worldInstances.begin(),
            boot.worldInstances.end(),
            [&](const WorldInstanceSummary& summary) {
                return summary.worldId == candidate;
            });
        if (it == boot.worldInstances.end()) {
            return candidate;
        }
    }
    return "world_" + std::to_string(static_cast<int>(boot.worldInstances.size()) + 1);
}

WorldInfoSnapshot buildWorldInfoSnapshot(const TerrainParams& terrainParams, std::string_view worldId, const Vec3& spawn)
{
    WorldInfoSnapshot info;
    info.worldId = worldId.empty() ? std::string("default") : std::string(worldId);
    info.formatVersion = std::max(1, terrainParams.generatorVersion);
    info.seed = std::max(1, terrainParams.seed);
    info.chunkSize = terrainParams.chunkSize;
    info.horizonRadiusMeters = terrainParams.horizonRadiusMeters;
    info.heightAmplitude = terrainParams.heightAmplitude;
    info.heightFrequency = terrainParams.heightFrequency;
    info.waterLevel = terrainParams.waterLevel;
    info.tunnelSeeds = terrainParams.explicitTunnelSeeds;
    info.spawnX = spawn.x;
    info.spawnY = spawn.y;
    info.spawnZ = spawn.z;
    return info;
}

bool createWorldInstance(BootResources& boot, const std::string& requestedName, std::string* errorText)
{
    const std::string worldId = sanitizeWorldInstanceName(requestedName);
    if (worldId.empty()) {
        if (errorText != nullptr) {
            *errorText = "Enter a world name first.";
        }
        return false;
    }

    WorldStoreOptions options;
    options.name = worldId;
    options.storageRoot = getWorldStorageDirectory();
    options.createIfMissing = true;
    options.regionSize = 8;
    options.chunkResolution = 16;
    options.groundParams = boot.terrainParams;

    std::string worldError;
    std::optional<WorldStore> world = WorldStore::open(options, &worldError);
    if (!world.has_value()) {
        if (errorText != nullptr) {
            *errorText = worldError.empty() ? std::string("Failed to create world instance.") : worldError;
        }
        return false;
    }

    const WorldInfoSnapshot info = buildWorldInfoSnapshot(boot.terrainParams, worldId);
    if (!world->applyWorldInfo(info, &worldError) && !worldError.empty()) {
        if (errorText != nullptr) {
            *errorText = worldError;
        }
        return false;
    }

    boot.selectedWorldId = worldId;
    refreshWorldInstanceCatalog(boot);
    return true;
}

bool deleteWorldInstance(BootResources& boot, std::string_view worldId, std::string* errorText)
{
    const std::string sanitized = sanitizeWorldInstanceName(worldId);
    if (sanitized.empty()) {
        if (errorText != nullptr) {
            *errorText = "No world is selected.";
        }
        return false;
    }

    std::error_code ec;
    const std::uintmax_t removedWorldEntries = std::filesystem::remove_all(getWorldStorageDirectory() / sanitized, ec);
    if (ec) {
        if (errorText != nullptr) {
            *errorText = "Failed to delete world storage for " + sanitized + ".";
        }
        return false;
    }

    std::filesystem::remove_all(getTerrainChunkCacheDirectory() / sanitized, ec);
    if (ec) {
        if (errorText != nullptr) {
            *errorText = "World removed, but its terrain cache could not be fully deleted.";
        }
        return false;
    }

    if (removedWorldEntries == 0u && errorText != nullptr) {
        *errorText = "World instance was not found on disk.";
    }

    refreshWorldInstanceCatalog(boot);
    return removedWorldEntries > 0u;
}

}  // namespace TrueFlightApp
