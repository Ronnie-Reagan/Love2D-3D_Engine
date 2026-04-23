#include "App/TerrainStreamController.hpp"

#include "App/AppTypes.hpp"

#include <algorithm>
#include <chrono>
#include <tuple>

namespace TrueFlightApp {

CompiledTerrainChunk buildTerrainTileChunk(
    TerrainFarTileBand band,
    TerrainFarTileDetail detail,
    int tileScale,
    int tileX,
    int tileZ,
    const TerrainFieldContext& terrainContext,
    const std::optional<TerrainChunkBakeCache>& bakeCache,
    std::string_view terrainWorldId,
    std::uint64_t paramsSignature,
    std::uint64_t sourceSignature,
    bool usePlanetTile,
    const PlanetTileId& planetTile);

namespace {

int terrainBandPriorityRank(TerrainFarTileBand band)
{
    switch (band) {
    case TerrainFarTileBand::Near:
        return 0;
    case TerrainFarTileBand::Mid:
        return 1;
    case TerrainFarTileBand::Horizon:
    default:
        return 2;
    }
}

int terrainTileDetailRank(TerrainFarTileDetail detail)
{
    switch (detail) {
    case TerrainFarTileDetail::Lod0:
        return 0;
    case TerrainFarTileDetail::Lod1:
        return 1;
    case TerrainFarTileDetail::Lod2:
    default:
        return 2;
    }
}

std::string terrainTileIdentityKey(TerrainFarTileBand band, int tileScale, int tileX, int tileZ)
{
    return std::to_string(static_cast<int>(band)) + "|" +
           std::to_string(tileScale) + "|" +
           std::to_string(tileX) + "|" +
           std::to_string(tileZ);
}

std::string terrainTileIdentityKey(const TerrainTileRequest& request)
{
    if (!request.usePlanetTile) {
        return terrainTileIdentityKey(request.band, request.tileScale, request.tileX, request.tileZ);
    }
    return std::to_string(static_cast<int>(request.band)) + "|" +
           std::to_string(request.tileScale) + "|p|" +
           std::to_string(request.planetTile.face) + "|" +
           std::to_string(request.planetTile.lod) + "|" +
           std::to_string(request.planetTile.tx) + "|" +
           std::to_string(request.planetTile.ty);
}

std::string terrainTileRequestKey(const TerrainTileRequest& request)
{
    return std::to_string(static_cast<int>(request.band)) + "|" +
           std::to_string(static_cast<int>(request.detail)) + "|" +
           (request.usePlanetTile
                ? ("p|" +
                   std::to_string(request.tileScale) + "|" +
                   std::to_string(request.planetTile.face) + "|" +
                   std::to_string(request.planetTile.lod) + "|" +
                   std::to_string(request.planetTile.tx) + "|" +
                   std::to_string(request.planetTile.ty))
                : (std::to_string(request.tileScale) + "|" +
                   std::to_string(request.tileX) + "|" +
                   std::to_string(request.tileZ))) +
           "|" +
           std::to_string(request.paramsSignature) + "|" +
           std::to_string(request.sourceSignature);
}

bool terrainTileRequestsEquivalent(const TerrainTileRequest& lhs, const TerrainTileRequest& rhs)
{
    return lhs.band == rhs.band &&
           lhs.detail == rhs.detail &&
           lhs.tileScale == rhs.tileScale &&
           lhs.usePlanetTile == rhs.usePlanetTile &&
           (lhs.usePlanetTile
                ? (lhs.planetTile.face == rhs.planetTile.face &&
                   lhs.planetTile.lod == rhs.planetTile.lod &&
                   lhs.planetTile.tx == rhs.planetTile.tx &&
                   lhs.planetTile.ty == rhs.planetTile.ty)
                : (lhs.tileX == rhs.tileX && lhs.tileZ == rhs.tileZ)) &&
           lhs.paramsSignature == rhs.paramsSignature &&
           lhs.sourceSignature == rhs.sourceSignature;
}

bool terrainStreamRequestMoreValuable(const TerrainTileRequest& lhs, const TerrainTileRequest& rhs)
{
    const auto lhsKey = std::tuple{
        terrainBandPriorityRank(lhs.band),
        terrainTileDetailRank(lhs.detail),
        lhs.tileScale,
        lhs.priority};
    const auto rhsKey = std::tuple{
        terrainBandPriorityRank(rhs.band),
        terrainTileDetailRank(rhs.detail),
        rhs.tileScale,
        rhs.priority};
    return lhsKey < rhsKey;
}

std::size_t terrainStreamOutstandingCountLocked(const TerrainVisualStreamState& state)
{
    return state.queuedRequests.size() + state.inflightRequestKeys.size() + state.completedResults.size();
}

void updateTerrainStreamStatsLocked(TerrainVisualStreamState& state)
{
    state.stats.queuedCount = static_cast<int>(state.queuedRequests.size());
    state.stats.inflightCount = static_cast<int>(state.inflightRequestKeys.size());
    state.stats.completedCount = static_cast<int>(state.completedResults.size());
}

bool terrainStreamRequestIsDesiredLocked(
    const TerrainVisualStreamState& state,
    const TerrainTileRequest& request,
    std::uint64_t generation)
{
    if (generation != state.generation) {
        return false;
    }
    const auto desiredIt = state.desiredRequests.find(terrainTileIdentityKey(request));
    return desiredIt != state.desiredRequests.end() && terrainTileRequestsEquivalent(desiredIt->second, request);
}

void dropTerrainQueuedRequestAtLocked(TerrainVisualStreamState& state, std::size_t index)
{
    const TerrainChunkBuildRequest& droppedRequest = state.queuedRequests[index];
    state.queuedRequestKeys.erase(terrainTileRequestKey(droppedRequest.request));
    state.queuedRequests.erase(state.queuedRequests.begin() + static_cast<std::ptrdiff_t>(index));
    state.stats.droppedRequestCount += 1u;
}

void dropTerrainCompletedResultAtLocked(TerrainVisualStreamState& state, std::size_t index)
{
    state.completedResults.erase(state.completedResults.begin() + static_cast<std::ptrdiff_t>(index));
    state.stats.droppedResultCount += 1u;
}

std::size_t findWorstTerrainCompletedResultIndexLocked(const TerrainVisualStreamState& state)
{
    std::size_t worstIndex = 0;
    for (std::size_t index = 1; index < state.completedResults.size(); ++index) {
        if (terrainStreamRequestMoreValuable(state.completedResults[worstIndex].request, state.completedResults[index].request)) {
            worstIndex = index;
        }
    }
    return worstIndex;
}

void trimTerrainStreamBacklogLockedImpl(TerrainVisualStreamState& state)
{
    state.completedResults.erase(
        std::remove_if(
            state.completedResults.begin(),
            state.completedResults.end(),
            [&](const TerrainChunkBuildResult& result)
            {
                return !terrainStreamRequestIsDesiredLocked(state, result.request, result.generation);
            }),
        state.completedResults.end());

    state.queuedRequests.erase(
        std::remove_if(
            state.queuedRequests.begin(),
            state.queuedRequests.end(),
            [&](const TerrainChunkBuildRequest& request)
            {
                if (terrainStreamRequestIsDesiredLocked(state, request.request, request.generation)) {
                    return false;
                }
                state.queuedRequestKeys.erase(terrainTileRequestKey(request.request));
                state.stats.droppedRequestCount += 1u;
                return true;
            }),
        state.queuedRequests.end());

    std::sort(
        state.queuedRequests.begin(),
        state.queuedRequests.end(),
        [](const TerrainChunkBuildRequest& lhs, const TerrainChunkBuildRequest& rhs)
        {
            return terrainStreamRequestMoreValuable(lhs.request, rhs.request);
        });

    while (state.maxCompletedResults > 0 && static_cast<int>(state.completedResults.size()) > state.maxCompletedResults) {
        dropTerrainCompletedResultAtLocked(state, findWorstTerrainCompletedResultIndexLocked(state));
    }

    while (state.maxPendingRequests > 0 && static_cast<int>(terrainStreamOutstandingCountLocked(state)) > state.maxPendingRequests) {
        if (!state.queuedRequests.empty()) {
            dropTerrainQueuedRequestAtLocked(state, state.queuedRequests.size() - 1u);
            continue;
        }
        if (!state.completedResults.empty()) {
            dropTerrainCompletedResultAtLocked(state, findWorstTerrainCompletedResultIndexLocked(state));
            continue;
        }
        break;
    }
    updateTerrainStreamStatsLocked(state);
}

void terrainStreamWorkerLoop(TerrainVisualStreamState& state)
{
    for (;;) {
        TerrainChunkBuildRequest request;
        std::string requestKey;
        {
            std::unique_lock<std::mutex> lock(state.mutex);
            state.condition.wait(lock, [&state]
                                 { return state.stopRequested || !state.queuedRequests.empty(); });
            if (state.stopRequested) {
                return;
            }

            bool foundRequest = false;
            while (!state.queuedRequests.empty()) {
                request = std::move(state.queuedRequests.front());
                state.queuedRequests.pop_front();
                requestKey = terrainTileRequestKey(request.request);
                state.queuedRequestKeys.erase(requestKey);
                if (!terrainStreamRequestIsDesiredLocked(state, request.request, request.generation) ||
                    request.generationContext == nullptr) {
                    continue;
                }

                state.inflightRequestKeys[terrainTileIdentityKey(
                    request.request)] = requestKey;
                updateTerrainStreamStatsLocked(state);
                foundRequest = true;
                break;
            }

            if (!foundRequest) {
                continue;
            }
        }

        const auto buildStart = std::chrono::steady_clock::now();
        CompiledTerrainChunk compiledChunk = buildTerrainTileChunk(
            request.request.band,
            request.request.detail,
            request.request.tileScale,
            request.request.tileX,
            request.request.tileZ,
            request.generationContext->terrainContext,
            request.generationContext->bakeCache,
            request.generationContext->terrainWorldId,
            request.request.paramsSignature,
            request.request.sourceSignature,
            request.request.usePlanetTile,
            request.request.planetTile);
        const float buildTimeMs =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - buildStart).count();

        {
            std::lock_guard<std::mutex> lock(state.mutex);
            state.stats.workerBuildCount += 1u;
            state.stats.lastWorkerBuildTimeMs = buildTimeMs;
            const std::string identityKey = terrainTileIdentityKey(request.request);
            const auto inflightIt = state.inflightRequestKeys.find(identityKey);
            if (inflightIt != state.inflightRequestKeys.end() && inflightIt->second == requestKey) {
                state.inflightRequestKeys.erase(inflightIt);
            }
            if (terrainStreamRequestIsDesiredLocked(state, request.request, request.generation)) {
                const auto existingIt = std::find_if(
                    state.completedResults.begin(),
                    state.completedResults.end(),
                    [&](const TerrainChunkBuildResult& existing)
                    {
                        return terrainTileIdentityKey(existing.request) == identityKey;
                    });
                if (existingIt != state.completedResults.end()) {
                    if (terrainStreamRequestMoreValuable(request.request, existingIt->request)) {
                        state.completedResults.erase(existingIt);
                        state.stats.droppedResultCount += 1u;
                    }
                    else {
                        state.stats.staleResultCount += 1u;
                        updateTerrainStreamStatsLocked(state);
                        continue;
                    }
                }

                if (state.maxCompletedResults > 0 && static_cast<int>(state.completedResults.size()) >= state.maxCompletedResults) {
                    const std::size_t worstIndex = findWorstTerrainCompletedResultIndexLocked(state);
                    if (terrainStreamRequestMoreValuable(request.request, state.completedResults[worstIndex].request)) {
                        dropTerrainCompletedResultAtLocked(state, worstIndex);
                    }
                    else {
                        state.stats.staleResultCount += 1u;
                        updateTerrainStreamStatsLocked(state);
                        continue;
                    }
                }

                state.completedResults.push_back({request.request, std::move(compiledChunk), request.generation});
                trimTerrainStreamBacklogLockedImpl(state);
            }
            else {
                state.stats.staleResultCount += 1u;
            }
            updateTerrainStreamStatsLocked(state);
        }
    }
}

}  // namespace

void synchronizeTerrainStreamWorkers(TerrainVisualStreamState& state, int requestedWorkerCount)
{
    const int workerCount = std::clamp(requestedWorkerCount, 1, 6);
    if (state.workerCount == workerCount && static_cast<int>(state.workers.size()) == workerCount) {
        return;
    }

    state.shutdown();
    state.workerCount = workerCount;
    for (int workerIndex = 0; workerIndex < workerCount; ++workerIndex) {
        state.workers.emplace_back([&state]
                                   { terrainStreamWorkerLoop(state); });
    }
}

void resetTerrainVisualStreamState(TerrainVisualStreamState* streamState)
{
    if (streamState == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(streamState->mutex);
    streamState->queuedRequests.clear();
    streamState->completedResults.clear();
    streamState->desiredRequests.clear();
    streamState->queuedRequestKeys.clear();
    streamState->inflightRequestKeys.clear();
    streamState->activeGenerationContext.reset();
    streamState->generation += 1u;
    streamState->stats = {};
    updateTerrainStreamStatsLocked(*streamState);
}

void enqueueTerrainTileRequests(
    TerrainVisualStreamState& streamState,
    const std::vector<TerrainTileRequest>& missingRequests,
    const std::vector<TerrainTileRequest>& upgradeRequests,
    int missingBudget,
    int upgradeBudget,
    int maxPendingRequests,
    int maxCompletedResults,
    const TerrainFieldContext& terrainContext,
    const std::optional<TerrainChunkBakeCache>& bakeCache,
    std::string_view terrainWorldId)
{
    std::lock_guard<std::mutex> lock(streamState.mutex);
    streamState.maxPendingRequests = maxPendingRequests;
    streamState.maxCompletedResults = maxCompletedResults;
    streamState.desiredRequests.clear();
    if (!streamState.activeGenerationContext || streamState.activeGenerationContext->generation != streamState.generation) {
        auto generationContext = std::make_shared<TerrainStreamGenerationSnapshot>();
        generationContext->terrainContext = terrainContext;
        generationContext->bakeCache = bakeCache;
        generationContext->terrainWorldId = terrainWorldId.empty() ? std::string("default") : std::string(terrainWorldId);
        generationContext->generation = streamState.generation;
        streamState.activeGenerationContext = std::move(generationContext);
    }

    const auto recordDesiredRequest = [&](const TerrainTileRequest& request)
    {
        streamState.desiredRequests[terrainTileIdentityKey(request)] = request;
    };
    for (const TerrainTileRequest& request : missingRequests) {
        recordDesiredRequest(request);
    }
    for (const TerrainTileRequest& request : upgradeRequests) {
        recordDesiredRequest(request);
    }

    const auto queueRequest = [&](const TerrainTileRequest& request)
    {
        const std::string requestKey = terrainTileRequestKey(request);
        if (streamState.queuedRequestKeys.find(requestKey) != streamState.queuedRequestKeys.end()) {
            return;
        }

        const std::string identityKey = terrainTileIdentityKey(request);
        const auto queuedIt = std::find_if(
            streamState.queuedRequests.begin(),
            streamState.queuedRequests.end(),
            [&](const TerrainChunkBuildRequest& queued)
            {
                return terrainTileIdentityKey(queued.request) == identityKey;
            });
        if (queuedIt != streamState.queuedRequests.end()) {
            return;
        }
        const auto inflightIt = streamState.inflightRequestKeys.find(identityKey);
        if (inflightIt != streamState.inflightRequestKeys.end()) {
            return;
        }

        TerrainChunkBuildRequest buildRequest;
        buildRequest.request = request;
        buildRequest.generationContext = streamState.activeGenerationContext;
        buildRequest.generation = streamState.generation;
        streamState.queuedRequests.push_back(std::move(buildRequest));
        streamState.queuedRequestKeys[requestKey] = identityKey;
    };

    for (int index = 0; index < std::min<int>(static_cast<int>(missingRequests.size()), missingBudget); ++index) {
        queueRequest(missingRequests[static_cast<std::size_t>(index)]);
    }
    for (int index = 0; index < std::min<int>(static_cast<int>(upgradeRequests.size()), upgradeBudget); ++index) {
        queueRequest(upgradeRequests[static_cast<std::size_t>(index)]);
    }

    trimTerrainStreamBacklogLockedImpl(streamState);
    streamState.condition.notify_all();
}

void trimTerrainStreamBacklogLocked(TerrainVisualStreamState& state)
{
    trimTerrainStreamBacklogLockedImpl(state);
}

TerrainStreamStats snapshotTerrainStreamStats(TerrainVisualStreamState* streamState)
{
    if (streamState == nullptr) {
        return {};
    }

    std::lock_guard<std::mutex> lock(streamState->mutex);
    TerrainStreamStats stats = streamState->stats;
    stats.queuedCount = static_cast<int>(streamState->queuedRequests.size());
    stats.inflightCount = static_cast<int>(streamState->inflightRequestKeys.size());
    stats.completedCount = static_cast<int>(streamState->completedResults.size());
    return stats;
}

}  // namespace TrueFlightApp
