#pragma once

#include <optional>
#include <string_view>
#include <vector>

namespace NativeGame {
class TerrainChunkBakeCache;
struct TerrainFieldContext;
}  // namespace NativeGame

namespace TrueFlightApp {

struct TerrainVisualStreamState;
struct TerrainTileRequest;
struct TerrainStreamStats;

void synchronizeTerrainStreamWorkers(TerrainVisualStreamState& state, int requestedWorkerCount);
void trimTerrainStreamBacklogLocked(TerrainVisualStreamState& state);
void resetTerrainVisualStreamState(TerrainVisualStreamState* streamState);
void enqueueTerrainTileRequests(
    TerrainVisualStreamState& streamState,
    const std::vector<TerrainTileRequest>& missingRequests,
    const std::vector<TerrainTileRequest>& upgradeRequests,
    int missingBudget,
    int upgradeBudget,
    int maxPendingRequests,
    int maxCompletedResults,
    const NativeGame::TerrainFieldContext& terrainContext,
    const std::optional<NativeGame::TerrainChunkBakeCache>& bakeCache,
    std::string_view terrainWorldId);
TerrainStreamStats snapshotTerrainStreamStats(TerrainVisualStreamState* streamState);

}  // namespace TrueFlightApp
