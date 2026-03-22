#pragma once

#include "NativeGame/BlobSync.hpp"
#include "NativeGame/Hash.hpp"
#include "NativeGame/NetTransport.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace NativeGame {

struct AvatarBlobRecord {
    std::string kind;
    std::string hash;
    std::string raw;
    BlobMetaPacket meta {};
};

struct AvatarBlobCache {
    std::unordered_map<std::string, AvatarBlobRecord> entries;

    [[nodiscard]] bool has(std::string_view kind, std::string_view hash) const
    {
        return entries.find(blobTransferKey(kind, hash)) != entries.end();
    }

    [[nodiscard]] const AvatarBlobRecord* get(std::string_view kind, std::string_view hash) const
    {
        const auto it = entries.find(blobTransferKey(kind, hash));
        return it == entries.end() ? nullptr : &it->second;
    }

    void put(const AvatarBlobRecord& record)
    {
        entries[blobTransferKey(record.kind, record.hash)] = record;
    }
};

struct VoiceSessionState {
    bool captureEnabled = false;
    bool transmitting = false;
    int radioChannel = 1;
    std::vector<std::string> pendingOutboundCompressedFrames;
    std::vector<std::string> inboundCompressedFrames;
    std::vector<std::string> hostLocalReceiveFrames;
};

struct AreaOfInterestState {
    int centerChunkX = 0;
    int centerChunkZ = 0;
    int radiusChunks = 20;
    float snapshotNearMeters = 1024.0f;
    float snapshotFarMeters = 4096.0f;
    std::unordered_map<std::string, int> lastChunkRevisionByKey;
};

struct HostedPlayerState {
    int id = 0;
    NetPeerId peerId = 0;
    bool isLocalAuthority = false;
    bool connected = false;
    bool hasReceivedHello = false;
    bool hasReceivedInput = false;
    bool flightMode = true;
    float walkYaw = 0.0f;
    float walkPitch = 0.0f;
    AvatarManifest avatar = defaultAvatarManifest();
    NetPlayerInput input {};
    FlightState actor {};
    FlightRuntimeState runtime {};
    AreaOfInterestState aoi {};
    BlobSyncState blobSync = createBlobSyncState();
};

struct ClientReplicationState {
    std::shared_ptr<INetTransport> transport {};
    NetPeerId serverPeerId = 0;
    bool connected = false;
    bool joinAcknowledged = false;
    bool helloPending = true;
    bool worldInfoReceived = false;
    bool worldSyncReceived = false;
    int localPlayerId = 0;
    AvatarManifest localAvatar = defaultAvatarManifest();
    AoiSubscription lastAoiSubscription {};
    bool hasAoiSubscription = false;
    int lastAckTick = 0;
    double nextHelloAt = 0.0;
    double nextInputAt = 0.0;
    WorldInfoSnapshot lastWorldInfo {};
    TerrainParams mirroredTerrain = defaultTerrainParams();
    BlobSyncState blobSync = createBlobSyncState();
    AvatarBlobCache blobCache {};
    VoiceSessionState voice {};
    std::unordered_map<int, RemotePeerState> peers;
    std::map<int, NetPlayerInput> pendingInputs;
    std::vector<std::string> outboundReliable;
    std::vector<std::string> outboundUnreliable;
};

struct SteamOnlineState {
    bool available = false;
    bool initialized = false;
    bool joinRequested = false;
    std::string status = "Offline";
    std::string lobbyId;
    std::string pendingLobbyId;
    std::string hostSteamId;
    std::shared_ptr<INetTransport> transport;
};

class HostedWorldServer {
public:
    explicit HostedWorldServer(std::shared_ptr<INetTransport> transport = {})
        : transport_(std::move(transport))
    {
        HostedPlayerState local;
        local.id = 1;
        local.peerId = 0;
        local.isLocalAuthority = true;
        local.connected = true;
        local.hasReceivedHello = true;
        local.hasReceivedInput = true;
        local.avatar = defaultAvatarManifest();
        players_.emplace(local.id, std::move(local));
    }

    void setTransport(std::shared_ptr<INetTransport> transport)
    {
        transport_ = std::move(transport);
    }

    [[nodiscard]] std::shared_ptr<INetTransport> transport() const
    {
        return transport_;
    }

    [[nodiscard]] HostedPlayerState* localPlayer()
    {
        auto it = players_.find(1);
        return it == players_.end() ? nullptr : &it->second;
    }

    [[nodiscard]] const std::unordered_map<int, HostedPlayerState>& players() const
    {
        return players_;
    }

    void setLog(std::function<void(const std::string&)> logFn)
    {
        log_ = std::move(logFn);
    }

    void setLocalAuthoritativeState(
        const FlightState& actor,
        const FlightRuntimeState& runtime,
        bool flightMode,
        float walkYaw,
        float walkPitch,
        const AvatarManifest& avatar);

    void serviceIncoming(const TerrainParams& terrainParams, WorldStore* worldStore);
    void update(double nowSeconds, float dt, const TerrainFieldContext& terrainContext, const FlightConfig& flightConfig, WorldStore* worldStore);
    bool addCrater(const TerrainCrater& crater, WorldStore* worldStore, const TerrainParams& terrainParams);
    void queueLocalVoiceFrame(int channel, std::string_view compressedFrame);
    [[nodiscard]] std::vector<std::string> drainLocalVoiceFrames();
    [[nodiscard]] std::vector<std::string> drainHostLocalVoiceFrames();

private:
    std::shared_ptr<INetTransport> transport_;
    std::unordered_map<int, HostedPlayerState> players_;
    std::unordered_map<NetPeerId, int> peerToPlayerId_;
    AvatarBlobCache blobCache_;
    std::unordered_map<std::string, std::unordered_set<int>> pendingBlobWaiters_;
    double snapshotAccumulator_ = 0.0;
    double cloudAccumulator_ = 0.0;
    int snapshotSequence_ = 0;
    int nextPlayerId_ = 2;
    std::vector<std::string> hostLocalReceiveFrames_;
    std::function<void(const std::string&)> log_;

    HostedPlayerState& ensurePlayerForPeer(NetPeerId peerId);
    void log(const std::string& message) const;
    void sendWorldBootstrap(HostedPlayerState& player, WorldStore* worldStore, const TerrainParams& terrainParams);
    void sendChunksForPlayer(HostedPlayerState& player, WorldStore* worldStore, std::string_view reason);
    void broadcastAvatarManifest(const HostedPlayerState& player, NetPeerId exceptPeer = 0);
    void broadcastSnapshots(double nowSeconds);
    void broadcastCloudSnapshot(double nowSeconds);
    void handlePacket(const NetEvent& event, const TerrainParams& terrainParams, WorldStore* worldStore);
    void simulateRemotePlayers(float dt, const TerrainFieldContext& terrainContext, const FlightConfig& flightConfig);
    void fulfillBlobRequest(NetPeerId peerId, std::string_view kind, std::string_view hash);
};

void serviceClientReplication(
    ClientReplicationState& client,
    double nowSeconds,
    TerrainParams& terrainParams,
    WorldStore* mirrorWorldStore,
    RemotePeerState* localPeer = nullptr);

void enqueueClientHello(ClientReplicationState& client, const AvatarManifest& avatar);
void enqueueClientInput(ClientReplicationState& client, const NetPlayerInput& input);
void enqueueClientAoiSubscription(ClientReplicationState& client, const AoiSubscription& subscription);
void flushClientOutbound(ClientReplicationState& client);

namespace HostedNetworkingDetail {

inline float distanceSq2(const Vec3& a, const Vec3& b)
{
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return (dx * dx) + (dz * dz);
}

inline Quat composeWalkingRotation(float yaw, float pitch)
{
    const Quat yawQuat = quatFromAxisAngle({ 0.0f, 1.0f, 0.0f }, wrapAngle(yaw));
    const Vec3 right = rotateVector(yawQuat, { 1.0f, 0.0f, 0.0f });
    const Quat pitchQuat = quatFromAxisAngle(right, clamp(pitch, radians(-89.0f), radians(89.0f)));
    return quatNormalize(quatMultiply(pitchQuat, yawQuat));
}

inline void stepWalkingAuthoritative(
    HostedPlayerState& player,
    float dt,
    const TerrainFieldContext& terrainContext,
    float baseMoveSpeed = 10.0f)
{
    FlightState& actor = player.actor;
    player.walkYaw = wrapAngle(player.input.walkYaw);
    player.walkPitch = clamp(player.input.walkPitch, radians(-89.0f), radians(89.0f));
    actor.rot = composeWalkingRotation(player.walkYaw, player.walkPitch);

    Vec3 forward = forwardFromRotation(actor.rot);
    forward.y = 0.0f;
    if (lengthSquared(forward) > 1.0e-6f) {
        forward = normalize(forward, { 0.0f, 0.0f, 1.0f });
    }
    Vec3 right = rightFromRotation(actor.rot);
    right.y = 0.0f;
    if (lengthSquared(right) > 1.0e-6f) {
        right = normalize(right, { 1.0f, 0.0f, 0.0f });
    }

    Vec3 moveDir {};
    if (player.input.walkForward) {
        moveDir += forward;
    }
    if (player.input.walkBackward) {
        moveDir -= forward;
    }
    if (player.input.walkStrafeRight) {
        moveDir += right;
    }
    if (player.input.walkStrafeLeft) {
        moveDir -= right;
    }
    if (lengthSquared(moveDir) > 1.0e-6f) {
        moveDir = normalize(moveDir, {}) * (player.input.walkSprint ? baseMoveSpeed * 1.8f : baseMoveSpeed);
    }

    actor.pos += moveDir * dt;
    actor.vel.x = moveDir.x;
    actor.vel.z = moveDir.z;
    actor.vel.y += -9.80665f * dt;
    actor.pos.y += actor.vel.y * dt;
    const float ground = sampleSurfaceHeight(actor.pos.x, actor.pos.z, terrainContext);
    if (actor.pos.y <= ground + 1.8f) {
        actor.pos.y = ground + 1.8f;
        actor.vel.y = 0.0f;
        actor.onGround = true;
    } else {
        actor.onGround = false;
    }
    if (player.input.walkJump && actor.onGround) {
        actor.vel.y = 5.0f;
        actor.onGround = false;
    }
    actor.flightVel = actor.vel;
    actor.flightAngVel = {};
    actor.debug.tick += 1;
}

inline void resetRemoteFlightState(FlightState& plane, FlightRuntimeState& runtime)
{
    plane = {};
    runtime = {};
    plane.pos = { 0.0f, 190.0f, 0.0f };
    plane.rot = quatNormalize(quatFromAxisAngle({ 1.0f, 0.0f, 0.0f }, radians(1.5f)));
    plane.flightVel = { 0.0f, 0.0f, 72.0f };
    plane.vel = plane.flightVel;
    plane.throttle = 0.64f;
    plane.collisionRadius = 3.2f;
}

inline std::string voiceFrameChannelKey(int channel)
{
    return "voice:" + std::to_string(normalizeRadioChannel(channel));
}

inline std::string packetType(const std::string& packet)
{
    const std::vector<std::string> fields = splitPacketFields(packet);
    return fields.empty() ? std::string {} : fields.front();
}

inline AvatarManifest avatarFromLocalVisuals(const AvatarManifest& current, const AvatarManifest& update)
{
    AvatarManifest avatar = current;
    avatar.role = sanitizeRole(update.role);
    avatar.callsign = sanitizeCallsign(update.callsign);
    avatar.radioChannel = normalizeRadioChannel(update.radioChannel);
    avatar.radioTx = update.radioTx;
    avatar.plane = update.plane;
    avatar.walking = update.walking;
    return avatar;
}

}  // namespace HostedNetworkingDetail

inline void HostedWorldServer::log(const std::string& message) const
{
    if (log_) {
        log_(message);
    }
}

inline HostedPlayerState& HostedWorldServer::ensurePlayerForPeer(NetPeerId peerId)
{
    const auto mapped = peerToPlayerId_.find(peerId);
    if (mapped != peerToPlayerId_.end()) {
        return players_.at(mapped->second);
    }

    HostedPlayerState player;
    player.id = nextPlayerId_++;
    player.peerId = peerId;
    player.connected = true;
    player.avatar = defaultAvatarManifest();
    HostedNetworkingDetail::resetRemoteFlightState(player.actor, player.runtime);
    peerToPlayerId_[peerId] = player.id;
    return players_.emplace(player.id, std::move(player)).first->second;
}

inline void HostedWorldServer::setLocalAuthoritativeState(
    const FlightState& actor,
    const FlightRuntimeState& runtime,
    bool flightMode,
    float walkYaw,
    float walkPitch,
    const AvatarManifest& avatar)
{
    if (HostedPlayerState* player = localPlayer(); player != nullptr) {
        player->actor = actor;
        player->runtime = runtime;
        player->flightMode = flightMode;
        player->walkYaw = walkYaw;
        player->walkPitch = walkPitch;
        player->avatar = HostedNetworkingDetail::avatarFromLocalVisuals(player->avatar, avatar);
        player->hasReceivedHello = true;
        player->hasReceivedInput = true;
    }
}

inline void HostedWorldServer::sendWorldBootstrap(HostedPlayerState& player, WorldStore* worldStore, const TerrainParams& terrainParams)
{
    if (!transport_ || !worldStore || player.peerId == 0) {
        return;
    }
    transport_->send(player.peerId, static_cast<int>(TransportLane::Control), buildJoinOkPacket(player.id), true);
    transport_->send(
        player.peerId,
        static_cast<int>(TransportLane::Control),
        buildWorldInfoPacket(buildWorldInfoSnapshotPacketData(*worldStore, terrainParams)),
        true);
    transport_->send(player.peerId, static_cast<int>(TransportLane::Control), buildWorldSyncPacket(terrainParams), true);
    for (const auto& [otherId, other] : players_) {
        if (otherId == player.id || !other.connected) {
            continue;
        }
        transport_->send(
            player.peerId,
            static_cast<int>(TransportLane::Control),
            buildAvatarManifestPacket(other.id, other.avatar),
            true);
    }
    sendChunksForPlayer(player, worldStore, "hello");
}

inline void HostedWorldServer::sendChunksForPlayer(HostedPlayerState& player, WorldStore* worldStore, std::string_view reason)
{
    if (!transport_ || !worldStore || player.peerId == 0) {
        return;
    }
    const std::vector<WorldChunkState> chunks = worldStore->collectEditedChunks(
        player.aoi.centerChunkX,
        player.aoi.centerChunkZ,
        player.aoi.radiusChunks);
    for (const WorldChunkState& chunkState : chunks) {
        const std::string key = std::to_string(chunkState.cx) + ":" + std::to_string(chunkState.cz);
        if (player.aoi.lastChunkRevisionByKey[key] == chunkState.revision) {
            continue;
        }
        player.aoi.lastChunkRevisionByKey[key] = chunkState.revision;
        transport_->send(
            player.peerId,
            static_cast<int>(TransportLane::Control),
            buildChunkPacket("CHUNK_STATE", chunkState, reason),
            true);
    }
}

inline void HostedWorldServer::broadcastAvatarManifest(const HostedPlayerState& player, NetPeerId exceptPeer)
{
    if (!transport_) {
        return;
    }
    const std::string packet = buildAvatarManifestPacket(player.id, player.avatar);
    for (const auto& [otherId, other] : players_) {
        if (!other.connected || other.peerId == 0 || other.peerId == exceptPeer || otherId == player.id) {
            continue;
        }
        transport_->send(other.peerId, static_cast<int>(TransportLane::Control), packet, true);
    }
}

inline void HostedWorldServer::fulfillBlobRequest(NetPeerId peerId, std::string_view kind, std::string_view hash)
{
    if (!transport_) {
        return;
    }
    const AvatarBlobRecord* record = blobCache_.get(kind, hash);
    if (record == nullptr) {
        return;
    }
    BlobSyncState tempState = createBlobSyncState();
    const BlobOutgoingTransfer transfer = prepareOutgoingBlobTransfer(tempState, kind, hash, record->raw, record->meta);
    transport_->send(
        peerId,
        static_cast<int>(TransportLane::Blob),
        encodePacket("BLOB_META", {
            { "kind", transfer.meta.kind },
            { "hash", transfer.meta.hash },
            { "ownerId", transfer.meta.ownerId },
            { "role", transfer.meta.role },
            { "modelHash", transfer.meta.modelHash },
            { "rawBytes", std::to_string(transfer.meta.rawBytes) },
            { "encodedBytes", std::to_string(transfer.meta.encodedBytes) },
            { "chunkSize", std::to_string(transfer.meta.chunkSize) },
            { "chunkCount", std::to_string(transfer.meta.chunkCount) }
        }),
        true);
    for (int index = 0; index < transfer.chunkCount; ++index) {
        transport_->send(
            peerId,
            static_cast<int>(TransportLane::Blob),
            encodePacket("BLOB_CHUNK", {
                { "kind", transfer.kind },
                { "hash", transfer.hash },
                { "idx", std::to_string(index + 1) },
                { "data", transfer.chunks[static_cast<std::size_t>(index)] }
            }),
            true);
    }
}

inline void HostedWorldServer::handlePacket(const NetEvent& event, const TerrainParams& terrainParams, WorldStore* worldStore)
{
    HostedPlayerState& player = ensurePlayerForPeer(event.peerId);
    const std::string type = HostedNetworkingDetail::packetType(event.payload);
    if (type == "HELLO") {
        const auto avatar = parseAvatarManifestPacket("AVATAR_MANIFEST|" + event.payload.substr(6), nullptr);
        player.hasReceivedHello = true;
        if (avatar.has_value()) {
            player.avatar = *avatar;
        }
        sendWorldBootstrap(player, worldStore, terrainParams);
        broadcastAvatarManifest(player, player.peerId);
        return;
    }
    if (type == "INPUT") {
        const auto input = parseInputPacket(event.payload);
        if (input.has_value()) {
            player.hasReceivedInput = true;
            player.input = *input;
            player.avatar = HostedNetworkingDetail::avatarFromLocalVisuals(player.avatar, input->avatar);
            player.flightMode = sanitizeRole(input->role) != "walking";
        }
        return;
    }
    if (type == "AOI_SUBSCRIBE") {
        if (const auto subscription = parseAoiSubscribePacket(event.payload); subscription.has_value()) {
            player.aoi.centerChunkX = subscription->centerChunkX;
            player.aoi.centerChunkZ = subscription->centerChunkZ;
            player.aoi.radiusChunks = subscription->radiusChunks;
            player.aoi.snapshotNearMeters = subscription->snapshotNearMeters;
            player.aoi.snapshotFarMeters = subscription->snapshotFarMeters;
            sendChunksForPlayer(player, worldStore, "subscribe");
        }
        return;
    }
    if (type == "BLOB_REQUEST") {
        const auto kv = parseKeyValueFields(splitPacketFields(event.payload));
        fulfillBlobRequest(event.peerId, kv["kind"], kv["hash"]);
        return;
    }
    if (type == "BLOB_META") {
        if (const auto meta = parseBlobMetaPacket(event.payload); meta.has_value()) {
            acceptIncomingBlobMeta(player.blobSync, *meta);
        }
        return;
    }
    if (type == "BLOB_CHUNK") {
        if (const auto chunk = parseBlobChunkPacket(event.payload); chunk.has_value()) {
            const auto completed = acceptIncomingBlobChunk(player.blobSync, *chunk);
            if (completed.has_value()) {
                AvatarBlobRecord record;
                record.kind = completed->first.kind;
                record.hash = completed->first.hash;
                record.raw = completed->second;
                record.meta = completed->first;
                blobCache_.put(record);
            }
        }
        return;
    }
    if (type == "VOICE_STATE") {
        if (const auto state = parseVoiceStatePacket(event.payload); state.has_value()) {
            player.avatar.radioChannel = state->channel;
            player.avatar.radioTx = state->transmitting;
            broadcastAvatarManifest(player, player.peerId);
        }
        return;
    }
    if (type == "VOICE_FRAME") {
        if (const auto frame = parseVoiceFramePacket(event.payload); !frame.has_value()) {
            return;
        }
        for (const auto& [otherId, other] : players_) {
            if (otherId == player.id || !other.connected) {
                continue;
            }
            const float limit = std::max(player.aoi.snapshotFarMeters, other.aoi.snapshotFarMeters);
            if (player.avatar.radioChannel == other.avatar.radioChannel &&
                HostedNetworkingDetail::distanceSq2(player.actor.pos, other.actor.pos) <= (limit * limit)) {
                if (other.peerId == 0) {
                    hostLocalReceiveFrames_.push_back(event.payload);
                } else if (transport_) {
                    transport_->send(other.peerId, static_cast<int>(TransportLane::Voice), event.payload, false);
                }
            }
        }
        return;
    }
    if (type == "CRATER_EVENT" && worldStore != nullptr) {
        if (const auto crater = parseCraterPacket(event.payload); crater.has_value()) {
            (void)addCrater(*crater, worldStore, terrainParams);
        }
    }
}

inline void HostedWorldServer::serviceIncoming(const TerrainParams& terrainParams, WorldStore* worldStore)
{
    if (!transport_) {
        return;
    }
    for (const NetEvent& event : transport_->poll()) {
        if (event.type == NetEvent::Type::Connected) {
            HostedPlayerState& player = ensurePlayerForPeer(event.peerId);
            player.connected = true;
            log("[net] peer connected " + std::to_string(player.id));
            continue;
        }
        if (event.type == NetEvent::Type::Disconnected) {
            const auto peerIt = peerToPlayerId_.find(event.peerId);
            if (peerIt != peerToPlayerId_.end()) {
                const int playerId = peerIt->second;
                peerToPlayerId_.erase(peerIt);
                if (auto playerIt = players_.find(playerId); playerIt != players_.end()) {
                    playerIt->second.connected = false;
                    if (transport_) {
                        const std::string packet = buildPeerLeavePacket(playerId);
                        for (const auto& [otherId, other] : players_) {
                            if (otherId != playerId && other.connected && other.peerId != 0) {
                                transport_->send(other.peerId, static_cast<int>(TransportLane::Control), packet, true);
                            }
                        }
                    }
                    players_.erase(playerIt);
                    log("[net] peer disconnected " + std::to_string(playerId));
                }
            }
            continue;
        }
        if (event.type == NetEvent::Type::Message) {
            handlePacket(event, terrainParams, worldStore);
        }
    }
}

inline void HostedWorldServer::simulateRemotePlayers(float dt, const TerrainFieldContext& terrainContext, const FlightConfig& flightConfig)
{
    for (auto& [playerId, player] : players_) {
        if (player.isLocalAuthority || !player.connected || !player.hasReceivedInput) {
            continue;
        }

        if (sanitizeRole(player.input.role) == "walking") {
            player.flightMode = false;
            HostedNetworkingDetail::stepWalkingAuthoritative(player, dt, terrainContext);
        } else {
            player.flightMode = true;
            InputState inputState {};
            inputState.flightThrottleUp = player.input.flightThrottleUp;
            inputState.flightThrottleDown = player.input.flightThrottleDown;
            inputState.flightAirBrakes = player.input.flightAirBrakes;
            inputState.flightAfterburner = player.input.flightAfterburner;
            inputState.flightUseAnalogYoke = true;
            inputState.flightYawLeft = player.input.yokeYaw < -0.12f;
            inputState.flightYawRight = player.input.yokeYaw > 0.12f;
            inputState.flightHoldYaw = std::fabs(player.input.yokeYaw) > 0.12f;
            inputState.flightThrottleAnalog = clamp(player.input.throttle - player.actor.throttle, -1.0f, 1.0f);
            inputState.flightPitchAnalog = player.input.yokePitch;
            inputState.flightRollAnalog = player.input.yokeRoll;
            player.actor.throttle = player.input.throttle;
            player.actor.yoke.pitch = player.input.yokePitch;
            player.actor.yoke.yaw = player.input.yokeYaw;
            player.actor.yoke.roll = player.input.yokeRoll;

            FlightEnvironment environment {};
            environment.wind = {};
            environment.groundHeightAt = [&terrainContext](float x, float z) {
                return sampleSurfaceHeight(x, z, terrainContext);
            };
            environment.waterHeightAt = [&terrainContext](float x, float z) {
                return sampleWaterHeight(x, z, terrainContext);
            };
            environment.sampleSdf = [&terrainContext](float x, float y, float z) {
                return sampleSdf(x, y, z, terrainContext);
            };
            environment.sampleNormal = [&terrainContext](float x, float y, float z) {
                return sampleTerrainNormal(x, y, z, terrainContext);
            };
            environment.collisionRadius = player.actor.collisionRadius;
            stepFlight(player.actor, player.runtime, dt, static_cast<float>(player.runtime.tick) / 60.0f, inputState, environment, flightConfig);
        }
    }
}

inline void enqueueClientHello(ClientReplicationState& client, const AvatarManifest& avatar)
{
    client.localAvatar = avatar;
    client.helloPending = true;
    client.outboundReliable.push_back(buildHelloPacket(avatar));
}

inline void enqueueClientInput(ClientReplicationState& client, const NetPlayerInput& input)
{
    client.pendingInputs[input.tick] = input;
    client.outboundUnreliable.push_back(buildInputPacket(input));
}

inline void enqueueClientAoiSubscription(ClientReplicationState& client, const AoiSubscription& subscription)
{
    const bool changed =
        !client.hasAoiSubscription ||
        client.lastAoiSubscription.centerChunkX != subscription.centerChunkX ||
        client.lastAoiSubscription.centerChunkZ != subscription.centerChunkZ ||
        client.lastAoiSubscription.radiusChunks != subscription.radiusChunks ||
        std::fabs(client.lastAoiSubscription.snapshotNearMeters - subscription.snapshotNearMeters) > 1.0f ||
        std::fabs(client.lastAoiSubscription.snapshotFarMeters - subscription.snapshotFarMeters) > 1.0f;
    if (!changed) {
        return;
    }
    client.lastAoiSubscription = subscription;
    client.hasAoiSubscription = true;
    client.outboundReliable.push_back(buildAoiSubscribePacket(subscription));
}

inline void flushClientOutbound(ClientReplicationState& client)
{
    if (!client.transport || client.serverPeerId == 0 || !client.transport->ready()) {
        return;
    }

    for (const std::string& packet : client.outboundReliable) {
        const std::string type = HostedNetworkingDetail::packetType(packet);
        const int lane =
            (type == "BLOB_META" || type == "BLOB_CHUNK" || type == "BLOB_REQUEST")
            ? static_cast<int>(TransportLane::Blob)
            : static_cast<int>(TransportLane::Control);
        (void)client.transport->send(client.serverPeerId, lane, packet, true);
    }
    client.outboundReliable.clear();

    for (const std::string& packet : client.outboundUnreliable) {
        const std::string type = HostedNetworkingDetail::packetType(packet);
        const int lane = type == "VOICE_FRAME"
            ? static_cast<int>(TransportLane::Voice)
            : static_cast<int>(TransportLane::Snapshot);
        (void)client.transport->send(client.serverPeerId, lane, packet, false);
    }
    client.outboundUnreliable.clear();
}

inline void serviceClientReplication(
    ClientReplicationState& client,
    double nowSeconds,
    TerrainParams& terrainParams,
    WorldStore* mirrorWorldStore,
    RemotePeerState* localPeer)
{
    if (!client.transport) {
        client.connected = false;
        return;
    }

    for (const NetEvent& event : client.transport->poll()) {
        if (event.type == NetEvent::Type::Connected) {
            client.connected = true;
            client.serverPeerId = event.peerId;
            continue;
        }
        if (event.type == NetEvent::Type::Disconnected) {
            client.connected = false;
            client.joinAcknowledged = false;
            client.serverPeerId = 0;
            client.peers.clear();
            continue;
        }
        if (event.type != NetEvent::Type::Message) {
            continue;
        }

        const std::string type = HostedNetworkingDetail::packetType(event.payload);
        if (type == "JOIN_OK") {
            if (const auto id = parseJoinOkPacket(event.payload); id.has_value()) {
                client.localPlayerId = *id;
                client.joinAcknowledged = true;
                client.helloPending = false;
            }
            continue;
        }
        if (type == "WORLD_INFO") {
            if (const auto info = parseWorldInfoPacket(event.payload); info.has_value()) {
                client.lastWorldInfo = *info;
                client.worldInfoReceived = true;
                if (mirrorWorldStore != nullptr) {
                    std::string error;
                    if (mirrorWorldStore->applyWorldInfo(*info, &error)) {
                        terrainParams = mirrorWorldStore->buildGroundParams(terrainParams).terrainParams;
                    }
                }
            }
            continue;
        }
        if (type == "WORLD_SYNC") {
            client.worldSyncReceived = applyWorldSyncPacket(event.payload, terrainParams);
            continue;
        }
        if (type == "AVATAR_MANIFEST") {
            int id = 0;
            if (const auto avatar = parseAvatarManifestPacket(event.payload, &id); avatar.has_value()) {
                if (id == client.localPlayerId && localPeer != nullptr) {
                    localPeer->avatar = *avatar;
                } else {
                    RemotePeerState& peer = client.peers[id];
                    peer.id = id;
                    peer.avatar = *avatar;
                    peer.connected = true;
                }
            }
            continue;
        }
        if (type == "SNAPSHOT") {
            if (const auto snapshot = parseSnapshotPacket(event.payload); snapshot.has_value()) {
                if (snapshot->id == client.localPlayerId) {
                    client.lastAckTick = std::max(client.lastAckTick, snapshot->ack);
                    for (auto it = client.pendingInputs.begin(); it != client.pendingInputs.end();) {
                        if (it->first <= client.lastAckTick) {
                            it = client.pendingInputs.erase(it);
                        } else {
                            ++it;
                        }
                    }
                    if (localPeer != nullptr) {
                        localPeer->id = snapshot->id;
                        localPeer->avatar = snapshot->avatar;
                        localPeer->connected = true;
                        appendRemoteSnapshot(*localPeer, {
                            snapshot->timestamp,
                            snapshot->pos,
                            snapshot->rot,
                            snapshot->vel,
                            snapshot->angVel
                        });
                        (void)sampleRemotePeer(*localPeer, nowSeconds, 0.0);
                    }
                } else {
                    RemotePeerState& peer = client.peers[snapshot->id];
                    peer.id = snapshot->id;
                    peer.avatar = snapshot->avatar;
                    peer.connected = true;
                    appendRemoteSnapshot(peer, {
                        snapshot->timestamp,
                        snapshot->pos,
                        snapshot->rot,
                        snapshot->vel,
                        snapshot->angVel
                    });
                    (void)sampleRemotePeer(peer, nowSeconds);
                }
            }
            continue;
        }
        if ((type == "CHUNK_STATE" || type == "CHUNK_PATCH") && mirrorWorldStore != nullptr) {
            if (const auto chunk = parseChunkPacket(event.payload); chunk.has_value()) {
                (void)mirrorWorldStore->applyChunkState(*chunk);
            }
            continue;
        }
        if (type == "CRATER_EVENT" && mirrorWorldStore != nullptr) {
            if (const auto crater = parseCraterPacket(event.payload); crater.has_value()) {
                (void)mirrorWorldStore->applyCrater(*crater);
            }
            continue;
        }
        if (type == "BLOB_META") {
            if (const auto meta = parseBlobMetaPacket(event.payload); meta.has_value()) {
                acceptIncomingBlobMeta(client.blobSync, *meta);
            }
            continue;
        }
        if (type == "BLOB_CHUNK") {
            if (const auto chunk = parseBlobChunkPacket(event.payload); chunk.has_value()) {
                const auto completed = acceptIncomingBlobChunk(client.blobSync, *chunk);
                if (completed.has_value()) {
                    AvatarBlobRecord record;
                    record.kind = completed->first.kind;
                    record.hash = completed->first.hash;
                    record.raw = completed->second;
                    record.meta = completed->first;
                    client.blobCache.put(record);
                }
            }
            continue;
        }
        if (type == "VOICE_STATE") {
            if (const auto state = parseVoiceStatePacket(event.payload); state.has_value()) {
                if (state->id == client.localPlayerId) {
                    client.voice.radioChannel = state->channel;
                    client.voice.transmitting = state->transmitting;
                } else if (auto peerIt = client.peers.find(state->id); peerIt != client.peers.end()) {
                    peerIt->second.avatar.radioChannel = state->channel;
                    peerIt->second.avatar.radioTx = state->transmitting;
                }
            }
            continue;
        }
        if (type == "VOICE_FRAME") {
            if (const auto frame = parseVoiceFramePacket(event.payload); frame.has_value()) {
                if (frame->channel == client.voice.radioChannel && !frame->compressedData.empty()) {
                    client.voice.inboundCompressedFrames.push_back(frame->compressedData);
                }
            }
            continue;
        }
        if (type == "PEER_LEAVE") {
            if (const auto id = parsePeerLeavePacket(event.payload); id.has_value()) {
                client.peers.erase(*id);
                if (localPeer != nullptr && localPeer->id == *id) {
                    localPeer->connected = false;
                }
            }
        }
    }

    for (auto& [peerId, peer] : client.peers) {
        (void)peerId;
        (void)sampleRemotePeer(peer, nowSeconds);
    }

    flushClientOutbound(client);
}

inline void HostedWorldServer::broadcastSnapshots(double nowSeconds)
{
    if (!transport_) {
        return;
    }
    ++snapshotSequence_;
    for (const auto& [targetId, target] : players_) {
        if (!target.connected || target.peerId == 0) {
            continue;
        }
        const float nearMeters = target.aoi.snapshotNearMeters;
        const float farMeters = target.aoi.snapshotFarMeters;
        for (const auto& [playerId, player] : players_) {
            if (!player.connected || !player.hasReceivedHello) {
                continue;
            }
            bool shouldSend = player.isLocalAuthority || player.hasReceivedInput;
            if (!shouldSend) {
                continue;
            }
            if (playerId != targetId) {
                const float distSq = HostedNetworkingDetail::distanceSq2(target.actor.pos, player.actor.pos);
                if (distSq > (farMeters * farMeters)) {
                    continue;
                }
                if (distSq > (nearMeters * nearMeters) && (snapshotSequence_ % 3) != 0) {
                    continue;
                }
            }

            NetPlayerSnapshot snapshot;
            snapshot.id = player.id;
            snapshot.ack = std::max(0, player.input.tick);
            snapshot.pos = player.actor.pos;
            snapshot.rot = player.actor.rot;
            snapshot.vel = player.flightMode ? player.actor.flightVel : player.actor.vel;
            snapshot.angVel = player.actor.flightAngVel;
            snapshot.throttle = player.actor.throttle;
            snapshot.elevator = player.actor.yoke.pitch;
            snapshot.aileron = player.actor.yoke.roll;
            snapshot.rudder = player.actor.yoke.yaw;
            snapshot.simTick = player.runtime.tick;
            snapshot.timestamp = nowSeconds;
            snapshot.avatar = player.avatar;
            snapshot.avatar.role = player.flightMode ? "plane" : "walking";
            transport_->send(
                target.peerId,
                static_cast<int>(TransportLane::Snapshot),
                buildSnapshotPacket(snapshot),
                playerId == targetId);
        }
    }
}

inline void HostedWorldServer::broadcastCloudSnapshot(double nowSeconds)
{
    if (!transport_) {
        return;
    }
    const std::string packet = encodePacket("CLOUD_SNAPSHOT", {
        { "ts", formatNetFloat(nowSeconds) }
    });
    for (const auto& [playerId, player] : players_) {
        if (player.connected && player.peerId != 0) {
            transport_->send(player.peerId, static_cast<int>(TransportLane::Control), packet, false);
        }
    }
}

inline void HostedWorldServer::update(
    double nowSeconds,
    float dt,
    const TerrainFieldContext& terrainContext,
    const FlightConfig& flightConfig,
    WorldStore* worldStore)
{
    simulateRemotePlayers(dt, terrainContext, flightConfig);
    snapshotAccumulator_ += dt;
    cloudAccumulator_ += dt;
    if (snapshotAccumulator_ >= (1.0 / 30.0)) {
        snapshotAccumulator_ = std::fmod(snapshotAccumulator_, 1.0 / 30.0);
        broadcastSnapshots(nowSeconds);
    }
    if (cloudAccumulator_ >= 1.0) {
        cloudAccumulator_ = std::fmod(cloudAccumulator_, 1.0);
        broadcastCloudSnapshot(nowSeconds);
    }
    if (worldStore != nullptr) {
        for (auto& [playerId, player] : players_) {
            if (!player.connected || player.peerId == 0) {
                continue;
            }
            sendChunksForPlayer(player, worldStore, "tick");
        }
    }
}

inline void HostedWorldServer::queueLocalVoiceFrame(int channel, std::string_view compressedFrame)
{
    const HostedPlayerState* local = localPlayer();
    if (local == nullptr || compressedFrame.empty()) {
        return;
    }

    const int normalizedChannel = normalizeRadioChannel(channel);
    const std::string packet = buildVoiceFramePacket({ normalizedChannel, std::string(compressedFrame) });
    for (const auto& [playerId, player] : players_) {
        if (playerId == local->id || !player.connected || player.peerId == 0) {
            continue;
        }
        const float limit = std::max(local->aoi.snapshotFarMeters, player.aoi.snapshotFarMeters);
        if (normalizedChannel != player.avatar.radioChannel ||
            HostedNetworkingDetail::distanceSq2(local->actor.pos, player.actor.pos) > (limit * limit)) {
            continue;
        }
        if (transport_) {
            transport_->send(player.peerId, static_cast<int>(TransportLane::Voice), packet, false);
        }
    }
}

inline std::vector<std::string> HostedWorldServer::drainHostLocalVoiceFrames()
{
    std::vector<std::string> packets = drainLocalVoiceFrames();
    std::vector<std::string> drained;
    drained.reserve(packets.size());
    for (const std::string& packet : packets) {
        if (const auto frame = parseVoiceFramePacket(packet); frame.has_value()) {
            drained.push_back(frame->compressedData);
        }
    }
    return drained;
}

inline std::vector<std::string> HostedWorldServer::drainLocalVoiceFrames()
{
    std::vector<std::string> drained;
    drained.swap(hostLocalReceiveFrames_);
    return drained;
}

inline bool HostedWorldServer::addCrater(const TerrainCrater& crater, WorldStore* worldStore, const TerrainParams&)
{
    if (!worldStore) {
        return false;
    }
    const auto craterResult = worldStore->applyCrater(crater);
    if (!craterResult.first) {
        return false;
    }
    if (transport_) {
        const std::string legacyPacket = buildCraterPacket(crater);
        for (const auto& [playerId, player] : players_) {
            if (!player.connected || player.peerId == 0) {
                continue;
            }
            transport_->send(player.peerId, static_cast<int>(TransportLane::Control), legacyPacket, true);
            for (const WorldChunkState& chunkState : craterResult.second) {
                transport_->send(
                    player.peerId,
                    static_cast<int>(TransportLane::Control),
                    buildChunkPacket("CHUNK_PATCH", chunkState, "crater"),
                    true);
            }
        }
    }
    worldStore->flushDirty(nullptr);
    return true;
}



}  // namespace NativeGame
