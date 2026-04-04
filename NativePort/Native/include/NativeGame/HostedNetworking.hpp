#pragma once

#include "NativeGame/BlobSync.hpp"
#include "NativeGame/Hash.hpp"
#include "NativeGame/NetTransport.hpp"

#include <cstddef>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <string_view>

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

struct QueuedVoiceFrame {
    int senderId = 0;
    int channel = 1;
    std::string compressedData;
};

struct VoiceSessionState {
    bool captureEnabled = false;
    bool transmitting = false;
    int radioChannel = 1;
    std::vector<std::string> pendingOutboundCompressedFrames;
    std::vector<QueuedVoiceFrame> inboundCompressedFrames;
    std::vector<QueuedVoiceFrame> hostLocalReceiveFrames;
    std::unordered_map<int, VoiceTalkerState> talkersById;
};

struct AreaOfInterestState {
    int centerChunkX = 0;
    int centerChunkZ = 0;
    int radiusChunks = 20;
    float snapshotNearMeters = 1024.0f;
    float snapshotFarMeters = 4096.0f;
    std::unordered_map<std::string, int> lastChunkRevisionByKey;
};

struct HostedBlobOutboundPacket {
    std::string transferKey;
    std::string payload;
};

struct HostedTerrainEditRequest {
    NetPeerId peerId = 0;
    TerrainEditRequest request {};
};

struct HostedPlayerState {
    int id = 0;
    NetPeerId peerId = 0;
    bool isLocalAuthority = false;
    bool connected = false;
    bool hasReceivedHello = false;
    bool hasReceivedInput = false;
    PlayerMode mode = PlayerMode::Flight;
    bool flightMode = true;
    VehicleId vehicleId = 0;
    float walkYaw = 0.0f;
    float walkPitch = 0.0f;
    AvatarManifest avatar = defaultAvatarManifest();
    NetPlayerInput input {};
    double inputTimeSeconds = 0.0;
    FlightState actor {};
    FlightRuntimeState runtime {};
    AreaOfInterestState aoi {};
    BlobSyncState blobSync = createBlobSyncState();
    std::deque<HostedBlobOutboundPacket> outboundBlobReliable;
    std::unordered_map<std::string, int> queuedBlobPacketCountsByTransfer;
    double nextBlobFlushAt = 0.0;
    std::map<int, NetPlayerInput> pendingInputs;
};

struct ClientPredictedState {
    int tick = 0;
    PlayerMode mode = PlayerMode::Flight;
    bool flightMode = true;
    VehicleId vehicleId = 0;
    FlightState plane {};
    FlightRuntimeState runtime {};
    float walkYaw = 0.0f;
    float walkPitch = 0.0f;
};

struct LocalAuthoritativeState {
    bool valid = false;
    bool dirty = false;
    int ack = 0;
    int simTick = 0;
    PlayerMode mode = PlayerMode::Flight;
    bool flightMode = true;
    VehicleId vehicleId = 0;
    Vec3 pos {};
    Quat rot = quatIdentity();
    Vec3 vel {};
    Vec3 angVel {};
    float throttle = 0.0f;
    float yokePitch = 0.0f;
    float yokeYaw = 0.0f;
    float yokeRoll = 0.0f;
    AvatarManifest avatar = defaultAvatarManifest();
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
    int nextOutboundTick = 1;
    int lastAckTick = 0;
    int pendingControlTick = 0;
    double nextHelloAt = 0.0;
    double nextInputAt = 0.0;
    double nextBlobFlushAt = 0.0;
    WorldInfoSnapshot lastWorldInfo {};
    TerrainParams mirroredTerrain = defaultTerrainParams();
    bool mirrorTerrainDirty = false;
    LocalAuthoritativeState localAuthoritative {};
    BlobSyncState blobSync = createBlobSyncState();
    AvatarBlobCache blobCache {};
    std::unordered_set<std::string> uploadedBlobKeys;
    std::unordered_map<std::string, double> nextBlobRequestAtByKey;
    std::unordered_map<std::string, BlobTransferStatus> blobStatusByKey;
    std::unordered_map<TerrainEditOpId, TerrainEditRequest> pendingTerrainEditsById;
    std::deque<TerrainEditAck> terrainEditAcks;
    std::vector<WorldChunkState> recentTerrainChunks;
    TerrainEditOpId nextTerrainEditOpId = 1;
    double nextTerrainHudNotificationAt = 0.0;
    VoiceSessionState voice {};
    std::unordered_map<int, RemotePeerState> peers;
    std::unordered_map<ConstructId, ConstructBlueprint> constructBlueprints;
    std::unordered_map<ConstructId, ConstructState> constructStates;
    std::unordered_map<VehicleId, SharedVehicleState> sharedVehicles;
    NetGameplayStatePacket gameplayState {};
    bool gameplayStateDirty = false;
    std::map<int, NetPlayerInput> pendingInputs;
    std::map<int, ClientPredictedState> predictedStates;
    std::vector<std::string> outboundReliable;
    std::vector<std::string> outboundUnreliable;
};

struct SteamOnlineState {
    bool available = false;
    bool initialized = false;
    bool joinRequested = false;
    bool overlayEnabled = false;
    bool transportReady = false;
    int memberCount = 0;
    int maxPlayers = 0;
    int discoveredLobbyCount = 0;
    int selectedDiscoveredLobbyIndex = -1;
    std::string status = "Offline";
    std::string lobbyId;
    std::string pendingLobbyId;
    std::string hostSteamId;
    std::string localSteamId;
    std::string selectedDiscoveredLobbyId;
    std::string selectedDiscoveredLobbyLabel;
    std::string localPersonaName;
    std::string hostPersonaName;
    std::vector<std::string> memberNames;
    std::vector<std::string> discoveredLobbyLabels;
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

    [[nodiscard]] const AvatarBlobRecord* lookupBlob(std::string_view kind, std::string_view hash) const
    {
        return blobCache_.get(kind, hash);
    }

    [[nodiscard]] const std::unordered_map<ConstructId, ConstructBlueprint>& constructBlueprints() const
    {
        return constructBlueprints_;
    }

    [[nodiscard]] const std::unordered_map<ConstructId, ConstructState>& constructStates() const
    {
        return constructStates_;
    }

    [[nodiscard]] const std::unordered_map<VehicleId, SharedVehicleState>& sharedVehicles() const
    {
        return sharedVehicles_;
    }

    void setSharedWorldState(
        const std::unordered_map<ConstructId, ConstructBlueprint>& constructBlueprints,
        const std::unordered_map<ConstructId, ConstructState>& constructStates,
        const std::unordered_map<VehicleId, SharedVehicleState>& sharedVehicles);

    void cacheBlob(const AvatarBlobRecord& record)
    {
        blobCache_.put(record);
    }

    void setLog(std::function<void(const std::string&)> logFn)
    {
        log_ = std::move(logFn);
    }

    void setLocalAuthoritativeState(
        const FlightState& actor,
        const FlightRuntimeState& runtime,
        PlayerMode mode,
        VehicleId vehicleId,
        float walkYaw,
        float walkPitch,
        const AvatarManifest& avatar);

    void serviceIncoming(const TerrainParams& terrainParams, const TerrainFieldContext& terrainContext, WorldStore* worldStore);
    void update(double nowSeconds, float dt, const TerrainFieldContext& terrainContext, const FlightConfig& flightConfig, Vec3 wind, WorldStore* worldStore);
    bool addCrater(const TerrainCrater& crater, WorldStore* worldStore, const TerrainParams& terrainParams);
    void queueLocalVoiceFrame(int channel, std::string_view compressedFrame);
    [[nodiscard]] std::vector<QueuedVoiceFrame> drainLocalVoiceFrames();
    [[nodiscard]] std::vector<QueuedVoiceFrame> drainHostLocalVoiceFrames();
    [[nodiscard]] std::vector<HostedTerrainEditRequest> drainPendingTerrainEdits();
    void sendTerrainEditAck(NetPeerId peerId, const TerrainEditAck& ack);

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
    std::unordered_map<ConstructId, ConstructBlueprint> constructBlueprints_;
    std::unordered_map<ConstructId, ConstructState> constructStates_;
    std::unordered_map<VehicleId, SharedVehicleState> sharedVehicles_;
    std::vector<QueuedVoiceFrame> hostLocalReceiveFrames_;
    std::deque<HostedTerrainEditRequest> pendingTerrainEdits_;
    std::function<void(const std::string&)> log_;

    HostedPlayerState& ensurePlayerForPeer(NetPeerId peerId);
    void log(const std::string& message) const;
    void sendWorldBootstrap(HostedPlayerState& player, WorldStore* worldStore, const TerrainParams& terrainParams);
    void sendChunksForPlayer(HostedPlayerState& player, WorldStore* worldStore, std::string_view reason);
    void broadcastAvatarManifest(const HostedPlayerState& player, NetPeerId exceptPeer = 0);
    void broadcastVehicleStates();
    void broadcastSnapshots(double nowSeconds);
    void broadcastCloudSnapshot(double nowSeconds);
    void handlePacket(const NetEvent& event, const TerrainParams& terrainParams, const TerrainFieldContext& terrainContext, WorldStore* worldStore);
    void simulateRemotePlayers(float dt, const TerrainFieldContext& terrainContext, const FlightConfig& flightConfig, Vec3 wind);
    void fulfillBlobRequest(NetPeerId peerId, std::string_view kind, std::string_view hash);
    void flushBlobTransfers(double nowSeconds);
};

void serviceClientReplication(
    ClientReplicationState& client,
    double nowSeconds,
    TerrainParams& terrainParams,
    WorldStore* mirrorWorldStore,
    RemotePeerState* localPeer = nullptr);

void enqueueClientHello(ClientReplicationState& client, const AvatarManifest& avatar);
void enqueueClientInput(ClientReplicationState& client, const NetPlayerInput& input);
void enqueueClientModeSwitch(ClientReplicationState& client, const ModeSwitchPacket& modeSwitch);
void enqueueClientResetFlight(ClientReplicationState& client, const ResetFlightPacket& reset);
void enqueueClientAoiSubscription(ClientReplicationState& client, const AoiSubscription& subscription);
void flushClientOutbound(ClientReplicationState& client, double nowSeconds);

namespace HostedNetworkingDetail {

inline void networkStdoutLog(std::string_view message)
{
    std::cout << "[net] " << message << std::endl;
}

inline float sanitizeNetInputFrameDt(float value)
{
    if (!std::isfinite(value)) {
        return 1.0f / 60.0f;
    }
    return std::clamp(value, 1.0f / 240.0f, 0.05f);
}

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

inline void syncWalkingLookFromRotation(const Quat& rotation, float& walkYaw, float& walkPitch)
{
    const Vec3 forward = forwardFromRotation(rotation);
    const float flatMagnitude = std::sqrt((forward.x * forward.x) + (forward.z * forward.z));
    walkPitch = clamp(std::atan2(forward.y, std::max(flatMagnitude, 1.0e-6f)), radians(-89.0f), radians(89.0f));
    walkYaw = wrapAngle(std::atan2(forward.x, forward.z));
}

inline void stepWalkingAuthoritative(
    HostedPlayerState& player,
    float dt,
    const TerrainFieldContext& terrainContext,
    float baseMoveSpeed = 10.0f)
{
    constexpr float kWalkHalfHeight = 1.8f;
    constexpr float kWalkJumpSpeed = 5.4f;
    constexpr float kWalkGravity = -12.5f;
    constexpr float kWalkTerminalVelocity = -54.0f;
    constexpr float kWalkGroundSnapDistance = 0.45f;

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

    const float currentGround = sampleSurfaceHeight(actor.pos.x, actor.pos.z, terrainContext);
    const float standHeight = currentGround + kWalkHalfHeight;
    const bool nearGround = actor.pos.y <= standHeight + kWalkGroundSnapDistance;
    const Vec3 groundNormal =
        nearGround
            ? normalize(sampleTerrainNormal(actor.pos.x, actor.pos.y, actor.pos.z, terrainContext), { 0.0f, 1.0f, 0.0f })
            : Vec3 { 0.0f, 1.0f, 0.0f };

    Vec3 desiredVelocity = moveDir;
    if (nearGround && groundNormal.y > 0.18f) {
        desiredVelocity -= groundNormal * dot(desiredVelocity, groundNormal);
    } else {
        desiredVelocity.y = 0.0f;
    }

    actor.pos += desiredVelocity * dt;
    actor.vel.x = desiredVelocity.x;
    actor.vel.z = desiredVelocity.z;
    actor.vel.y = std::max(kWalkTerminalVelocity, actor.vel.y + (kWalkGravity * dt));
    actor.pos.y += actor.vel.y * dt;
    const float ground = sampleSurfaceHeight(actor.pos.x, actor.pos.z, terrainContext);
    if (actor.pos.y <= ground + kWalkHalfHeight + kWalkGroundSnapDistance) {
        actor.pos.y = ground + kWalkHalfHeight;
        actor.vel.y = 0.0f;
        actor.onGround = true;
    } else {
        actor.onGround = false;
    }
    if (player.input.walkJump && actor.onGround) {
        actor.vel.y = kWalkJumpSpeed;
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

inline void applyWalkingAuthoritativeState(
    HostedPlayerState& player,
    const TerrainFieldContext& terrainContext,
    const Vec3& requestedPos,
    float walkYaw,
    float walkPitch)
{
    player.mode = PlayerMode::Walking;
    player.flightMode = false;
    player.vehicleId = 0;
    player.walkYaw = wrapAngle(walkYaw);
    player.walkPitch = clamp(walkPitch, radians(-89.0f), radians(89.0f));
    player.actor.pos = requestedPos;
    const float ground = sampleSurfaceHeight(requestedPos.x, requestedPos.z, terrainContext);
    if (player.actor.pos.y < ground + 1.8f) {
        player.actor.pos.y = ground + 1.8f;
    }
    player.actor.rot = composeWalkingRotation(player.walkYaw, player.walkPitch);
    player.actor.vel = {};
    player.actor.flightVel = {};
    player.actor.flightAngVel = {};
    player.actor.yoke = {};
    player.actor.throttle = 0.0f;
    player.actor.onGround = player.actor.pos.y <= ground + 1.81f;
    player.runtime.crashed = false;
    player.runtime.hasPendingCrash = false;
    player.avatar.role = "walking";
    player.input.role = "walking";
}

inline void resetAuthoritativeFlightState(
    HostedPlayerState& player,
    const TerrainFieldContext& terrainContext,
    float x,
    float z)
{
    player.actor = {};
    player.runtime = {};
    const float ground = sampleSurfaceHeight(x, z, terrainContext);
    player.actor.pos = { x, ground + 190.0f, z };
    player.actor.rot = quatNormalize(quatFromAxisAngle({ 1.0f, 0.0f, 0.0f }, radians(1.5f)));
    player.actor.flightVel = { 0.0f, 0.0f, 72.0f };
    player.actor.vel = player.actor.flightVel;
    player.actor.throttle = 0.64f;
    player.actor.collisionRadius = 3.2f;
    player.actor.onGround = false;
    player.mode = PlayerMode::Flight;
    player.flightMode = true;
    player.vehicleId = 0;
    syncWalkingLookFromRotation(player.actor.rot, player.walkYaw, player.walkPitch);
    player.avatar.role = "plane";
    player.input.role = "plane";
}

inline const ConstructBlueprint* findConstructBlueprint(
    const std::unordered_map<ConstructId, ConstructBlueprint>& constructs,
    ConstructId constructId)
{
    const auto it = constructs.find(constructId);
    return it == constructs.end() ? nullptr : &it->second;
}

inline Vec3 vehicleForwardFromYaw(float yawRadians)
{
    return normalize(Vec3 { std::sin(yawRadians), 0.0f, std::cos(yawRadians) }, { 0.0f, 0.0f, 1.0f });
}

inline Quat vehicleRotationFromYaw(float yawRadians)
{
    return quatNormalize(quatFromAxisAngle({ 0.0f, 1.0f, 0.0f }, yawRadians));
}

struct VehicleHandlingPreset {
    float throttleResponseActive = 12.0f;
    float throttleResponseRelease = 5.5f;
    float steerResponseActive = 14.0f;
    float steerResponseRelease = 9.5f;
    float tractionBase = 0.36f;
    float tractionFrictionScale = 0.62f;
    float tractionRoadMin = 0.88f;
    float tractionRoadMax = 1.18f;
    float tractionClampMin = 0.18f;
    float tractionClampMax = 5.0f;
    float enginePullLowSpeed = 2.5f;
    float enginePullHighSpeed = 0.56f;
    float engineReverseScale = 0.78f;
    float rollingDragRoad = 0.35f;
    float rollingDragOffroad = 1.55f;
    float rollingDragSpeedLow = 0.80f;
    float rollingDragSpeedHigh = 1.12f;
    float aeroDrag = 0.0092f;
    float throttleNeutralEpsilon = 0.08f;
    float brakeRoadScale = 0.96f;
    float brakeRoadMaxScale = 1.32f;
    float steerRateLowSpeed = 1.15f;
    float steerRateHighSpeed = 0.32f;
    float steerRateRoadMin = 0.60f;
    float steerRateRoadMax = 1.12f;
    float roadAlignGainMin = 0.2f;
    float roadAlignGainScale = 0.62f;
    float roadAlignRate = 2.1f;
    float lateralDampingOffroad = 0.95f;
    float lateralDampingRoad = 4.2f;
    float velocityConvergeBase = 2.2f;
    float velocityConvergeFrictionScale = 1.4f;
    float slopeGravity = 9.80665f;
    float slopeResistanceScale = 1.0f;
    float slopeSlipScale = 1.6f;
    float slipTractionStart = 0.45f;
    float slipTractionScale = 0.75f;
    float wheelSpinGain = 0.28f;
    float wheelSpinLateralInfluence = 0.35f;
    float frontProbeExtra = 0.9f;
    float sideProbeExtra = 0.65f;
    float collisionMargin = 0.22f;
    float collisionSidePush = 0.65f;
    float collisionSideVelScale = 0.24f;
    float bodyClearance = 0.35f;
    float wheelbaseExtra = 0.65f;
    float trackExtra = 0.38f;
    float pitchClamp = 0.34f;
    float rollClamp = 0.30f;
    float frontProbeHeightBias = 0.55f;
    float suspensionRestScale = 0.72f;
    float suspensionScale = 1.4f;
    float cageHeaveFromCompression = 0.75f;
    float cageHeaveResponseRate = 11.0f;
    float cagePitchResponseRate = 9.0f;
    float cageRollResponseRate = 8.8f;
    float cagePitchFromThrottle = 0.25f;
    float cageRollFromSteer = 0.255f;
    float cagePitchSign = -1.0f;
    float cageRollSign = -1.0f;
    float cageSteerRollSign = -1.0f;
};

inline VehicleHandlingPreset simVehicleHandlingPreset()
{
    // Single SIM tuning block for quick handling iteration.
    // Example: invert roll direction by setting cageRollSign or cageSteerRollSign to +1.
    VehicleHandlingPreset tuning;
    return tuning;
}

inline void ensureVehicleSeatCount(SharedVehicleState& vehicle, const ConstructBlueprint& blueprint)
{
    const std::size_t desiredSeats = std::max<std::size_t>(1u, blueprint.seatOffsets.empty() ? 1u : blueprint.seatOffsets.size());
    if (vehicle.seatOccupants.size() < desiredSeats) {
        vehicle.seatOccupants.resize(desiredSeats, 0);
    }
}

inline int findVehicleSeatIndex(const SharedVehicleState& vehicle, int playerId)
{
    for (std::size_t index = 0; index < vehicle.seatOccupants.size(); ++index) {
        if (vehicle.seatOccupants[index] == playerId) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

inline void syncPlayerToVehicleSeat(
    HostedPlayerState& player,
    const SharedVehicleState& vehicle,
    const ConstructBlueprint& blueprint)
{
    const int seatIndex = std::max(0, findVehicleSeatIndex(vehicle, player.id));
    const Vec3 seatOffset =
        seatIndex >= 0 && seatIndex < static_cast<int>(blueprint.seatOffsets.size())
            ? blueprint.seatOffsets[static_cast<std::size_t>(seatIndex)]
            : Vec3 {};
    const Quat vehicleRot = vehicleRotationFromYaw(vehicle.yawRadians);
    player.mode = PlayerMode::Driving;
    player.flightMode = false;
    player.vehicleId = vehicle.vehicleId;
    player.actor.pos = vehicle.pos + rotateVector(vehicleRot, seatOffset);
    player.actor.rot = vehicleRot;
    player.actor.vel = vehicle.vel;
    player.actor.flightVel = vehicle.vel;
    player.actor.flightAngVel = {};
    player.actor.throttle = std::max(0.0f, vehicle.throttleNormalized);
    player.actor.onGround = true;
    player.runtime.crashed = false;
    player.runtime.hasPendingCrash = false;
    player.avatar.role = "driving";
    player.input.role = "driving";
}

inline void exitPlayerFromVehicle(
    HostedPlayerState& player,
    const TerrainFieldContext& terrainContext,
    const SharedVehicleState& vehicle)
{
    const TerrainRoadSample road = sampleRoadNetwork(vehicle.pos.x, vehicle.pos.z, terrainContext);
    const Vec3 right = terrainRoadRight(road);
    const Vec3 exitPos = vehicle.pos + (right * std::max(2.0f, road.widthMeters * 0.5f + 1.0f));
    applyWalkingAuthoritativeState(
        player,
        terrainContext,
        { exitPos.x, sampleSurfaceHeight(exitPos.x, exitPos.z, terrainContext) + 1.8f, exitPos.z },
        wrapAngle(vehicle.yawRadians),
        player.walkPitch);
}

inline bool applyVehicleSeatChange(
    SharedVehicleState& vehicle,
    int playerId,
    int requestedSeatIndex,
    bool entering)
{
    if (playerId <= 0) {
        return false;
    }
    const int currentSeat = findVehicleSeatIndex(vehicle, playerId);
    if (!entering) {
        if (currentSeat < 0) {
            return false;
        }
        vehicle.seatOccupants[static_cast<std::size_t>(currentSeat)] = 0;
        if (vehicle.driverPlayerId == playerId) {
            vehicle.driverPlayerId = 0;
        }
        return true;
    }

    int seatIndex = requestedSeatIndex;
    if (seatIndex < 0 || seatIndex >= static_cast<int>(vehicle.seatOccupants.size())) {
        seatIndex = -1;
        for (std::size_t index = 0; index < vehicle.seatOccupants.size(); ++index) {
            if (vehicle.seatOccupants[index] == 0 || vehicle.seatOccupants[index] == playerId) {
                seatIndex = static_cast<int>(index);
                break;
            }
        }
    }
    if (seatIndex < 0 || seatIndex >= static_cast<int>(vehicle.seatOccupants.size())) {
        return false;
    }
    if (currentSeat >= 0 && currentSeat != seatIndex) {
        vehicle.seatOccupants[static_cast<std::size_t>(currentSeat)] = 0;
    }
    if (vehicle.seatOccupants[static_cast<std::size_t>(seatIndex)] != 0 &&
        vehicle.seatOccupants[static_cast<std::size_t>(seatIndex)] != playerId) {
        return false;
    }
    vehicle.seatOccupants[static_cast<std::size_t>(seatIndex)] = playerId;
    if (seatIndex == 0) {
        vehicle.driverPlayerId = playerId;
    }
    return true;
}

inline void simulateSharedVehicle(
    SharedVehicleState& vehicle,
    const ConstructBlueprint& blueprint,
    const NetPlayerInput* driverInput,
    const TerrainFieldContext& terrainContext,
    float dt)
{
    dt = dt;
    if (dt <= 0.0f || !vehicle.active) {
        return;
    }
    const VehicleHandlingPreset tuning = simVehicleHandlingPreset();

    ensureVehicleSeatCount(vehicle, blueprint);
    const float throttleInput = driverInput != nullptr ? clamp(driverInput->driveThrottle, -1.0f, 1.0f) : 0.0f;
    const float steerInput = driverInput != nullptr ? clamp(driverInput->driveSteer, -1.0f, 1.0f) : 0.0f;
    const bool brake = driverInput != nullptr && driverInput->driveBrake;
    const TerrainRoadSample road = sampleRoadNetwork(vehicle.pos.x, vehicle.pos.z, terrainContext);
    const float roadAdhesion = clamp(road.roadMask + (road.shoulderMask * 0.2f), 0.0f, 1.0f);
    const float friction = roadAdhesion > 0.0f
        ? terrainRoadSurfaceFriction(road) * mix(blueprint.offroadGrip, blueprint.roadGrip, roadAdhesion)
        : blueprint.offroadGrip;
    const float steeringAssist = terrainRoadSteeringAssist(road) * blueprint.steeringAssist;

    const float throttleResponse = throttleInput != 0.0f ? tuning.throttleResponseActive : tuning.throttleResponseRelease;
    const float steerResponse = steerInput != 0.0f ? tuning.steerResponseActive : tuning.steerResponseRelease;
    vehicle.steerNormalized = mix(vehicle.steerNormalized, steerInput, clamp(dt * steerResponse, 0.0f, 1.0f));
    vehicle.throttleNormalized = mix(vehicle.throttleNormalized, throttleInput, clamp(dt * throttleResponse, 0.0f, 1.0f));

    Vec3 forward = vehicleForwardFromYaw(vehicle.yawRadians);
    const Vec3 right { forward.z, 0.0f, -forward.x };
    float forwardSpeed = dot(vehicle.vel, forward);
    const float speed01 = clamp(std::fabs(forwardSpeed) / std::max(1.0f, blueprint.maxSpeedMps), 0.0f, 1.0f);
    const float wheelbaseProbe = std::max(0.9f, blueprint.bodyHalfExtents.z + tuning.wheelbaseExtra);
    const float uphillHeight = sampleSurfaceHeight(vehicle.pos.x + (forward.x * wheelbaseProbe), vehicle.pos.z + (forward.z * wheelbaseProbe), terrainContext);
    const float downhillHeight = sampleSurfaceHeight(vehicle.pos.x - (forward.x * wheelbaseProbe), vehicle.pos.z - (forward.z * wheelbaseProbe), terrainContext);
    const float grade = (uphillHeight - downhillHeight) / std::max(0.5f, wheelbaseProbe * 2.0f);

    const float baseTraction = clamp((tuning.tractionBase + (friction * tuning.tractionFrictionScale)) * mix(tuning.tractionRoadMin, tuning.tractionRoadMax, roadAdhesion), tuning.tractionClampMin, tuning.tractionClampMax);
    const float desiredWheelSpeed =
        vehicle.throttleNormalized >= 0.0f
            ? vehicle.throttleNormalized * blueprint.maxSpeedMps
            : vehicle.throttleNormalized * blueprint.maxSpeedMps * 0.42f;
    const float wheelSlipSpeed = desiredWheelSpeed - forwardSpeed;
    const float wheelSlipNorm = clamp(std::fabs(wheelSlipSpeed) / std::max(3.0f, blueprint.maxSpeedMps * 0.55f), 0.0f, 1.6f);
    const float lateralSlipNorm = clamp(std::fabs(dot(vehicle.vel, right)) / std::max(2.0f, blueprint.maxSpeedMps * 0.35f), 0.0f, 1.4f);
    const float slipPenalty = clamp(
        std::max(0.0f, wheelSlipNorm - tuning.slipTractionStart) * tuning.slipTractionScale +
            (lateralSlipNorm * tuning.wheelSpinLateralInfluence),
        0.0f,
        0.72f);
    const float traction = baseTraction * (1.0f - slipPenalty);
    const float enginePull = std::max(0.0f, vehicle.throttleNormalized) * blueprint.accelerationMps2 * mix(tuning.enginePullLowSpeed, tuning.enginePullHighSpeed, speed01) * traction;
    const float engineReverse = std::min(0.0f, vehicle.throttleNormalized) * blueprint.brakingMps2 * tuning.engineReverseScale * traction;
    const float spinAssist = std::max(0.0f, vehicle.throttleNormalized) * wheelSlipNorm * tuning.wheelSpinGain * std::max(0.2f, 1.0f - roadAdhesion);
    forwardSpeed += (enginePull + engineReverse) * dt;
    forwardSpeed += spinAssist * blueprint.accelerationMps2 * dt;

    const float gravityAlongSlope = tuning.slopeGravity * grade;
    forwardSpeed -= gravityAlongSlope * tuning.slopeResistanceScale * dt;
    forwardSpeed -= gravityAlongSlope * std::max(0.0f, 1.0f - traction) * tuning.slopeSlipScale * dt;

    const float rollingDrag = mix(tuning.rollingDragRoad, tuning.rollingDragOffroad, 1.0f - roadAdhesion) * mix(tuning.rollingDragSpeedLow, tuning.rollingDragSpeedHigh, speed01);
    const float aeroDrag = tuning.aeroDrag * forwardSpeed * std::fabs(forwardSpeed);
    if (std::fabs(vehicle.throttleNormalized) < tuning.throttleNeutralEpsilon) {
        if (forwardSpeed > 0.0f) {
            forwardSpeed = std::max(0.0f, forwardSpeed - ((rollingDrag + std::fabs(aeroDrag)) * dt));
        } else if (forwardSpeed < 0.0f) {
            forwardSpeed = std::min(0.0f, forwardSpeed + ((rollingDrag + std::fabs(aeroDrag)) * dt));
        }
    } else {
        forwardSpeed -= aeroDrag * dt;
    }

    if (brake) {
        const float brakeStep = blueprint.brakingMps2 * mix(tuning.brakeRoadScale, tuning.brakeRoadMaxScale, roadAdhesion) * dt;
        if (forwardSpeed > 0.0f) {
            forwardSpeed = std::max(0.0f, forwardSpeed - brakeStep);
        } else if (forwardSpeed < 0.0f) {
            forwardSpeed = std::min(0.0f, forwardSpeed + brakeStep);
        }
    }
    forwardSpeed = clamp(forwardSpeed, -(blueprint.maxSpeedMps * 0.35f), blueprint.maxSpeedMps);

    const float yawBeforeUpdate = vehicle.yawRadians;
    const float steerRate = blueprint.steeringRate * mix(tuning.steerRateLowSpeed, tuning.steerRateHighSpeed, speed01) * mix(tuning.steerRateRoadMin, tuning.steerRateRoadMax, roadAdhesion);
    vehicle.yawRadians = wrapAngle(vehicle.yawRadians + (vehicle.steerNormalized * steerRate * dt));
    if (terrainRoadSampleValid(road)) {
        const float desiredYaw = std::atan2(road.forward.x, road.forward.z);
        const float alignmentGain = steeringAssist * clamp((speed01 * tuning.roadAlignGainScale) + tuning.roadAlignGainMin, 0.0f, 1.0f);
        vehicle.yawRadians = wrapAngle(vehicle.yawRadians + wrapAngle(desiredYaw - vehicle.yawRadians) * alignmentGain * dt * tuning.roadAlignRate);
    }
    const float yawRateActual = dt > 1.0e-4f
        ? wrapAngle(vehicle.yawRadians - yawBeforeUpdate) / dt
        : 0.0f;

    forward = vehicleForwardFromYaw(vehicle.yawRadians);
    const Vec3 rightUpdated { forward.z, 0.0f, -forward.x };
    float lateralSpeed = dot(vehicle.vel, rightUpdated);
    const float lateralDamping =
        mix(tuning.lateralDampingOffroad, tuning.lateralDampingRoad, roadAdhesion) *
        clamp(0.55f + (traction * 0.45f), 0.25f, 1.15f);
    lateralSpeed = mix(lateralSpeed, 0.0f, clamp(dt * lateralDamping, 0.0f, 1.0f));
    const Vec3 targetVel = (forward * forwardSpeed) + (rightUpdated * lateralSpeed);
    vehicle.vel += (targetVel - vehicle.vel) * clamp(dt * (tuning.velocityConvergeBase + (friction * tuning.velocityConvergeFrictionScale)), 0.0f, 1.0f);
    vehicle.vel.y = 0.0f;

    Vec3 nextPos = vehicle.pos + (vehicle.vel * dt);
    const float bodyClearance = blueprint.bodyHalfExtents.y + tuning.bodyClearance;
    nextPos.y = sampleSurfaceHeight(nextPos.x, nextPos.z, terrainContext) + bodyClearance;

    const float frontProbeDistance = blueprint.bodyHalfExtents.z + tuning.frontProbeExtra;
    const float sideProbeDistance = blueprint.bodyHalfExtents.x + tuning.sideProbeExtra;
    Vec3 frontProbe = nextPos + (forward * frontProbeDistance);
    const float frontProbeGround = sampleSurfaceHeight(frontProbe.x, frontProbe.z, terrainContext);
    frontProbe.y = std::max(frontProbe.y, frontProbeGround + (blueprint.bodyHalfExtents.y * tuning.frontProbeHeightBias));
    const Vec3 leftProbe = nextPos - (rightUpdated * sideProbeDistance);
    const Vec3 rightProbe = nextPos + (rightUpdated * sideProbeDistance);
    const float collisionMargin = tuning.collisionMargin;
    const float frontSdf = sampleSdf(frontProbe.x, frontProbe.y, frontProbe.z, terrainContext);
    if (frontSdf < collisionMargin) {
        nextPos -= forward * (collisionMargin - frontSdf);
        forwardSpeed = std::min(forwardSpeed, 0.0f) * 0.25f;
        vehicle.vel = forward * forwardSpeed;
    }
    const float leftSdf = sampleSdf(leftProbe.x, leftProbe.y, leftProbe.z, terrainContext);
    if (leftSdf < collisionMargin) {
        nextPos += rightUpdated * ((collisionMargin - leftSdf) * tuning.collisionSidePush);
        vehicle.vel += rightUpdated * std::max(0.0f, -lateralSpeed) * tuning.collisionSideVelScale;
    }
    const float rightSdf = sampleSdf(rightProbe.x, rightProbe.y, rightProbe.z, terrainContext);
    if (rightSdf < collisionMargin) {
        nextPos -= rightUpdated * ((collisionMargin - rightSdf) * tuning.collisionSidePush);
        vehicle.vel -= rightUpdated * std::max(0.0f, lateralSpeed) * tuning.collisionSideVelScale;
    }
    nextPos.y = sampleSurfaceHeight(nextPos.x, nextPos.z, terrainContext) + bodyClearance;
    vehicle.pos = nextPos;

    const float wheelbase = std::max(0.9f, blueprint.bodyHalfExtents.z + tuning.wheelbaseExtra);
    const float track = std::max(0.65f, blueprint.bodyHalfExtents.x + tuning.trackExtra);

    const Vec3 frontCenter = vehicle.pos + (forward * wheelbase);
    const Vec3 rearCenter  = vehicle.pos - (forward * wheelbase);
    const Vec3 leftCenter  = vehicle.pos - (rightUpdated * track);
    const Vec3 rightCenter = vehicle.pos + (rightUpdated * track);

    const float frontHeight = sampleSurfaceHeight(frontCenter.x, frontCenter.z, terrainContext);
    const float rearHeight = sampleSurfaceHeight(rearCenter.x, rearCenter.z, terrainContext);
    const float leftHeightGround = sampleSurfaceHeight(leftCenter.x, leftCenter.z, terrainContext);
    const float rightHeightGround = sampleSurfaceHeight(rightCenter.x, rightCenter.z, terrainContext);

    const float terrainPitch = clamp(
        std::atan2(frontHeight - rearHeight, wheelbase * 2.0f),
        -tuning.pitchClamp,
        tuning.pitchClamp);

    const float terrainRoll = clamp(
        std::atan2(rightHeightGround - leftHeightGround, track * 2.0f),
        -tuning.rollClamp,
        tuning.rollClamp);

    const float suspensionRestY = vehicle.pos.y - (blueprint.bodyHalfExtents.y * tuning.suspensionRestScale);
    const float suspensionScale = std::max(0.04f, blueprint.suspensionTravel * tuning.suspensionScale);

    const float flCompression = clamp(
        (suspensionRestY - sampleSurfaceHeight((vehicle.pos - rightUpdated * track + forward * wheelbase).x,
                                            (vehicle.pos - rightUpdated * track + forward * wheelbase).z,
                                            terrainContext)) / suspensionScale,
        0.0f, 1.0f);

    const float frCompression = clamp(
        (suspensionRestY - sampleSurfaceHeight((vehicle.pos + rightUpdated * track + forward * wheelbase).x,
                                            (vehicle.pos + rightUpdated * track + forward * wheelbase).z,
                                            terrainContext)) / suspensionScale,
        0.0f, 1.0f);

    const float rlCompression = clamp(
        (suspensionRestY - sampleSurfaceHeight((vehicle.pos - rightUpdated * track - forward * wheelbase).x,
                                            (vehicle.pos - rightUpdated * track - forward * wheelbase).z,
                                            terrainContext)) / suspensionScale,
        0.0f, 1.0f);

    const float rrCompression = clamp(
        (suspensionRestY - sampleSurfaceHeight((vehicle.pos + rightUpdated * track - forward * wheelbase).x,
                                            (vehicle.pos + rightUpdated * track - forward * wheelbase).z,
                                            terrainContext)) / suspensionScale,
        0.0f, 1.0f);

    vehicle.roadAdhesion = roadAdhesion;
    vehicle.surfaceFriction = friction * (1.0f - slipPenalty);

    const float avgCompression = (flCompression + frCompression + rlCompression + rrCompression) * 0.25f;
    vehicle.cage.heave = mix(
        vehicle.cage.heave,
        (avgCompression - 0.5f) * blueprint.suspensionTravel * tuning.cageHeaveFromCompression,
        clamp(dt * tuning.cageHeaveResponseRate, 0.0f, 1.0f));

    const float gravityAccel = 9.80665f;
    const float comHeight = std::max(0.20f, blueprint.bodyHalfExtents.y + (blueprint.suspensionTravel * 0.35f));

    // positive terrainRoll means right side is higher, so gravity pulls laterally toward the left
    const float gravityLateralAccel = -std::sin(-terrainRoll) * gravityAccel;

    // centripetal / cornering contribution from actual yaw motion, not just steer input
    const float turnLateralAccel = forwardSpeed * yawRateActual;

    // total lateral load seen by the sprung mass
    const float totalLateralAccel = turnLateralAccel + gravityLateralAccel;

    // support-plane bank
    const float bankRollTarget = -terrainRoll * tuning.cageRollSign;

    // suspension asymmetry gives a little extra visual compliance
    const float suspensionRollTarget = clamp(
        (((frCompression + rrCompression) - (flCompression + rlCompression)) * 0.5f) *
            (tuning.rollClamp * 0.85f) *
            tuning.cageRollSign,
        -tuning.rollClamp,
        tuning.rollClamp);

    // COM-height-based load roll
    const float loadRollTarget = clamp(
        std::atan2(totalLateralAccel * comHeight, std::max(0.25f, gravityAccel * track)) *
            tuning.cageRollFromSteer *
            tuning.cageSteerRollSign,
        -tuning.rollClamp,
        tuning.rollClamp);

    const float rollTarget = clamp(
        bankRollTarget +
            (loadRollTarget * 0.85f) +
            (suspensionRollTarget * 0.35f),
        -tuning.rollClamp,
        tuning.rollClamp);

    vehicle.cage.pitch = mix(
        vehicle.cage.pitch,
        (terrainPitch * tuning.cagePitchSign) - (vehicle.throttleNormalized * tuning.cagePitchFromThrottle),
        clamp(dt * tuning.cagePitchResponseRate, 0.0f, 1.0f));

    vehicle.cage.roll = mix(
        vehicle.cage.roll,
        rollTarget,
        clamp(dt * tuning.cageRollResponseRate, 0.0f, 1.0f));

    vehicle.cage.wheelCompression = {
        clamp(flCompression + (road.patchMask * 0.08f), 0.0f, 1.0f),
        clamp(frCompression + (road.potholeMask * 0.10f), 0.0f, 1.0f),
        clamp(rlCompression + (road.potholeMask * 0.10f), 0.0f, 1.0f),
        clamp(rrCompression + (road.patchMask * 0.08f), 0.0f, 1.0f)
    };
}

inline std::string voiceFrameChannelKey(int channel)
{
    return "voice:" + std::to_string(normalizeRadioChannel(channel));
}

inline float voiceRelayDistanceMeters(const AreaOfInterestState& speakerAoi, const AreaOfInterestState& listenerAoi)
{
    return std::max(6000.0f, std::max(speakerAoi.snapshotFarMeters, listenerAoi.snapshotFarMeters) * 2.0f);
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

inline std::string normalizeAvatarModelFormat(std::string format)
{
    std::transform(format.begin(), format.end(), format.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (format == "stl" || format == "glb" || format == "gltf") {
        return format;
    }
    return "builtin";
}

inline std::string avatarModelBlobKind(const AvatarRoleConfig& config)
{
    const std::string format = normalizeAvatarModelFormat(config.modelFormat);
    if (format == "builtin" || config.modelHash.empty()) {
        return {};
    }
    return "model:" + format;
}

inline BlobTransferStatus& ensureBlobTransferStatus(
    ClientReplicationState& client,
    std::string_view kind,
    std::string_view hash)
{
    BlobTransferStatus& status = client.blobStatusByKey[blobTransferKey(kind, hash)];
    status.kind = std::string(kind);
    status.hash = std::string(hash);
    return status;
}

inline void updateBlobTransferStatus(
    ClientReplicationState& client,
    double nowSeconds,
    std::string_view kind,
    std::string_view hash,
    BlobTransferPhase phase,
    std::string detail = {},
    std::string_view ownerId = {},
    std::string_view role = {},
    int chunkCount = -1,
    int completedChunks = -1,
    int rawBytes = -1)
{
    BlobTransferStatus& status = ensureBlobTransferStatus(client, kind, hash);
    status.phase = phase;
    status.updatedAtSeconds = nowSeconds;
    if (!detail.empty()) {
        status.detail = std::move(detail);
    }
    if (!ownerId.empty()) {
        status.ownerId = std::string(ownerId);
    }
    if (!role.empty()) {
        status.role = std::string(role);
    }
    if (chunkCount >= 0) {
        status.chunkCount = chunkCount;
    }
    if (completedChunks >= 0) {
        status.completedChunks = completedChunks;
    }
    if (rawBytes >= 0) {
        status.rawBytes = rawBytes;
    }
}

inline void maybeQueueClientBlobRequest(
    ClientReplicationState& client,
    double nowSeconds,
    std::string_view kind,
    std::string_view hash)
{
    if (kind.empty() || hash.empty() || client.blobCache.has(kind, hash)) {
        return;
    }
    const std::string key = blobTransferKey(kind, hash);
    const double nextAllowedAt = [&]() {
        const auto it = client.nextBlobRequestAtByKey.find(key);
        return it == client.nextBlobRequestAtByKey.end() ? 0.0 : it->second;
    }();
    if (nowSeconds < nextAllowedAt) {
        return;
    }
    client.nextBlobRequestAtByKey[key] = nowSeconds + 1.0;
    client.outboundReliable.push_back(buildBlobRequestPacket(kind, hash));
    updateBlobTransferStatus(client, nowSeconds, kind, hash, BlobTransferPhase::Requested, "requested from host");
    networkStdoutLog(
        "queued BLOB_REQUEST kind=" +
        std::string(kind) +
        " hash=" +
        std::string(hash));
}

inline void requestMissingAvatarBlobs(ClientReplicationState& client, double nowSeconds, const AvatarManifest& avatar)
{
    const bool wantsFlightRole = sanitizeRole(avatar.role) != "walking";
    const AvatarRoleConfig& activeRole = wantsFlightRole ? avatar.plane : avatar.walking;
    maybeQueueClientBlobRequest(client, nowSeconds, avatarModelBlobKind(activeRole), activeRole.modelHash);
}

inline bool isBlobPacketType(std::string_view type)
{
    return type == "BLOB_META" || type == "BLOB_CHUNK" || type == "BLOB_REQUEST";
}

inline void queueHostedBlobPacket(HostedPlayerState& player, std::string transferKey, std::string payload)
{
    if (transferKey.empty() || payload.empty()) {
        return;
    }
    player.outboundBlobReliable.push_back({ std::move(transferKey), std::move(payload) });
    const HostedBlobOutboundPacket& packet = player.outboundBlobReliable.back();
    ++player.queuedBlobPacketCountsByTransfer[packet.transferKey];
}

inline void applyReplicatedFlightControlState(FlightState& actor, const NetPlayerInput& input)
{
    actor.throttle = clamp(input.throttle, 0.0f, 1.0f);
    actor.yoke.pitch = clamp(input.yokePitch, -1.0f, 1.0f);
    actor.yoke.yaw = clamp(input.yokeYaw, -1.0f, 1.0f);
    actor.yoke.roll = clamp(input.yokeRoll, -1.0f, 1.0f);
    actor.manualElevatorTrim = input.manualElevatorTrim;
    actor.manualRudderTrim = input.manualRudderTrim;
}

inline void applyClientReportedAuthoritativeState(
    HostedPlayerState& player,
    const TerrainFieldContext& terrainContext,
    const NetPlayerInput& input)
{
    const TerrainParams params = normalizeTerrainParams(terrainContext.params);
    const PlayerMode mode = playerModeFromRole(input.role);
    const bool flightMode = mode == PlayerMode::Flight;
    const Vec3 sanitizedPos {
        clamp(sanitize(input.reportedPos.x, 0.0f), -params.worldRadius * 1.25f, params.worldRadius * 1.25f),
        clamp(sanitize(input.reportedPos.y, params.baseHeight), params.minY - 512.0f, params.maxY + 512.0f),
        clamp(sanitize(input.reportedPos.z, 0.0f), -params.worldRadius * 1.25f, params.worldRadius * 1.25f)
    };
    const Vec3 reportedVel {
        sanitize(input.reportedVel.x, 0.0f),
        sanitize(input.reportedVel.y, 0.0f),
        sanitize(input.reportedVel.z, 0.0f)
    };
    const Vec3 reportedAngVel {
        sanitize(input.reportedAngVel.x, 0.0f),
        sanitize(input.reportedAngVel.y, 0.0f),
        sanitize(input.reportedAngVel.z, 0.0f)
    };

    player.mode = mode;
    player.flightMode = flightMode;
    player.vehicleId = mode == PlayerMode::Driving ? input.vehicleId : 0;
    player.walkYaw = wrapAngle(input.walkYaw);
    player.walkPitch = clamp(input.walkPitch, radians(-89.0f), radians(89.0f));
    player.avatar.role = playerModeToken(mode);
    player.runtime.tick = std::max(player.runtime.tick, std::max(0, input.reportedSimTick));
    player.runtime.crashed = false;
    player.runtime.hasPendingCrash = false;

    if (flightMode) {
        player.actor.pos = sanitizedPos;
        player.actor.rot = quatNormalize(input.reportedRot);
        player.actor.flightVel = clampMagnitude(reportedVel, 480.0f);
        player.actor.vel = player.actor.flightVel;
        player.actor.flightAngVel = clampMagnitude(reportedAngVel, 12.0f);
        player.actor.collisionRadius = std::max(player.actor.collisionRadius, 3.2f);
        HostedNetworkingDetail::applyReplicatedFlightControlState(player.actor, input);
        const float ground = sampleSurfaceHeight(player.actor.pos.x, player.actor.pos.z, terrainContext);
        player.actor.onGround = player.actor.pos.y <= ground + 4.0f;
    } else if (mode == PlayerMode::Driving) {
        player.actor.pos = sanitizedPos;
        player.actor.rot = quatNormalize(input.reportedRot);
        player.actor.vel = clampMagnitude(reportedVel, 96.0f);
        player.actor.flightVel = player.actor.vel;
        player.actor.flightAngVel = {};
        player.actor.yoke = {};
        player.actor.throttle = std::max(0.0f, input.driveThrottle);
        player.actor.onGround = true;
    } else {
        player.actor.pos = sanitizedPos;
        const float ground = sampleSurfaceHeight(player.actor.pos.x, player.actor.pos.z, terrainContext);
        if (player.actor.pos.y < ground + 1.8f) {
            player.actor.pos.y = ground + 1.8f;
        }
        player.actor.rot = composeWalkingRotation(player.walkYaw, player.walkPitch);
        player.actor.vel = clampMagnitude(reportedVel, std::max(4.0f, input.walkMoveSpeed * 2.5f));
        player.actor.flightVel = player.actor.vel;
        player.actor.flightAngVel = {};
        player.actor.yoke = {};
        player.actor.throttle = 0.0f;
        player.actor.onGround = player.actor.pos.y <= ground + 1.81f;
    }

    player.actor.debug.tick = player.runtime.tick;
    player.actor.debug.throttle = player.actor.throttle;
    player.actor.debug.speed = length(flightMode ? player.actor.flightVel : player.actor.vel);
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
    PlayerMode mode,
    VehicleId vehicleId,
    float walkYaw,
    float walkPitch,
    const AvatarManifest& avatar)
{
    if (HostedPlayerState* player = localPlayer(); player != nullptr) {
        player->actor = actor;
        player->runtime = runtime;
        player->mode = mode;
        player->flightMode = mode == PlayerMode::Flight;
        player->vehicleId = mode == PlayerMode::Driving ? vehicleId : 0;
        player->walkYaw = walkYaw;
        player->walkPitch = walkPitch;
        player->avatar = HostedNetworkingDetail::avatarFromLocalVisuals(player->avatar, avatar);
        player->avatar.role = playerModeToken(mode);
        player->hasReceivedHello = true;
        player->hasReceivedInput = true;
    }
}

inline void HostedWorldServer::setSharedWorldState(
    const std::unordered_map<ConstructId, ConstructBlueprint>& constructBlueprints,
    const std::unordered_map<ConstructId, ConstructState>& constructStates,
    const std::unordered_map<VehicleId, SharedVehicleState>& sharedVehicles)
{
    constructBlueprints_ = constructBlueprints;
    constructStates_ = constructStates;
    sharedVehicles_ = sharedVehicles;
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
    for (const auto& [constructId, blueprint] : constructBlueprints_) {
        (void)constructId;
        ConstructPublishPacket publish;
        publish.blueprint = blueprint;
        if (const auto stateIt = constructStates_.find(blueprint.constructId); stateIt != constructStates_.end()) {
            publish.state = stateIt->second;
        } else {
            publish.state.constructId = blueprint.constructId;
            publish.state.revisionId = blueprint.revisionId;
            publish.state.label = blueprint.label;
            publish.state.published = true;
        }
        transport_->send(
            player.peerId,
            static_cast<int>(TransportLane::Control),
            buildConstructPublishPacket(publish),
            true);
    }
    for (const auto& [vehicleId, vehicle] : sharedVehicles_) {
        (void)vehicleId;
        transport_->send(
            player.peerId,
            static_cast<int>(TransportLane::Control),
            buildVehicleSpawnPacket({ vehicle, "bootstrap" }),
            true);
    }
    sendChunksForPlayer(player, worldStore, "hello");
    log(
        "[net] sent JOIN_OK/WORLD_INFO/WORLD_SYNC to player " +
        std::to_string(player.id) +
        " peer " +
        std::to_string(player.peerId));
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
    HostedPlayerState& player = ensurePlayerForPeer(peerId);
    if (!player.connected || player.peerId == 0) {
        return;
    }
    const AvatarBlobRecord* record = blobCache_.get(kind, hash);
    if (record == nullptr) {
        return;
    }
    const std::string transferKey = blobTransferKey(kind, hash);
    if (player.queuedBlobPacketCountsByTransfer.find(transferKey) != player.queuedBlobPacketCountsByTransfer.end()) {
        return;
    }
    BlobSyncState tempState = createBlobSyncState();
    const BlobOutgoingTransfer transfer = prepareOutgoingBlobTransfer(tempState, kind, hash, record->raw, record->meta);
    HostedNetworkingDetail::queueHostedBlobPacket(
        player,
        transferKey,
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
        }));
    for (int index = 0; index < transfer.chunkCount; ++index) {
        HostedNetworkingDetail::queueHostedBlobPacket(
            player,
            transferKey,
            encodePacket("BLOB_CHUNK", {
                { "kind", transfer.kind },
                { "hash", transfer.hash },
                { "idx", std::to_string(index + 1) },
                { "data", transfer.chunks[static_cast<std::size_t>(index)] }
            }));
    }
}

inline void HostedWorldServer::broadcastVehicleStates()
{
    if (!transport_) {
        return;
    }
    for (const auto& [playerId, player] : players_) {
        (void)playerId;
        if (!player.connected || player.peerId == 0 || !player.hasReceivedHello) {
            continue;
        }
        for (const auto& [vehicleId, vehicle] : sharedVehicles_) {
            (void)vehicleId;
            transport_->send(
                player.peerId,
                static_cast<int>(TransportLane::Snapshot),
                buildVehicleStatePacket(vehicle),
                false);
        }
    }
}

inline void HostedWorldServer::handlePacket(
    const NetEvent& event,
    const TerrainParams& terrainParams,
    const TerrainFieldContext& terrainContext,
    WorldStore* worldStore)
{
    HostedPlayerState& player = ensurePlayerForPeer(event.peerId);
    const std::string type = HostedNetworkingDetail::packetType(event.payload);
    if (type == "HELLO") {
        const auto avatar = parseAvatarManifestPacket("AVATAR_MANIFEST|" + event.payload.substr(6), nullptr);

        player.connected = true;
        player.hasReceivedHello = true;
        player.hasReceivedInput = false;
        player.pendingInputs.clear();
        player.input = {};
        player.inputTimeSeconds = 0.0;
        player.aoi.lastChunkRevisionByKey.clear();
        player.blobSync = createBlobSyncState();
        player.outboundBlobReliable.clear();
        player.queuedBlobPacketCountsByTransfer.clear();
        player.nextBlobFlushAt = 0.0;

        if (avatar.has_value()) {
            player.avatar = *avatar;
        } else {
            player.avatar = defaultAvatarManifest();
        }

        sendWorldBootstrap(player, worldStore, terrainParams);
        broadcastAvatarManifest(player, player.peerId);
        return;
    }
    if (type == "MODE_SWITCH") {
        if (const auto modeSwitch = parseModeSwitchPacket(event.payload); modeSwitch.has_value()) {
            player.hasReceivedInput = true;
            player.input.tick = std::max(player.input.tick, modeSwitch->tick);
            for (auto it = player.pendingInputs.begin(); it != player.pendingInputs.end();) {
                if (it->first <= player.input.tick) {
                    it = player.pendingInputs.erase(it);
                } else {
                    ++it;
                }
            }
            const PlayerMode mode = playerModeFromRole(modeSwitch->role);
            if (mode == PlayerMode::Walking) {
                HostedNetworkingDetail::applyWalkingAuthoritativeState(
                    player,
                    terrainContext,
                    { modeSwitch->x, modeSwitch->y, modeSwitch->z },
                    modeSwitch->walkYaw,
                    modeSwitch->walkPitch);
            } else if (mode == PlayerMode::Driving) {
                const auto vehicleIt = sharedVehicles_.find(modeSwitch->vehicleId);
                const ConstructBlueprint* blueprint =
                    vehicleIt == sharedVehicles_.end()
                        ? nullptr
                        : HostedNetworkingDetail::findConstructBlueprint(constructBlueprints_, vehicleIt->second.constructId);
                if (vehicleIt != sharedVehicles_.end() && blueprint != nullptr) {
                    HostedNetworkingDetail::ensureVehicleSeatCount(vehicleIt->second, *blueprint);
                    if (HostedNetworkingDetail::applyVehicleSeatChange(vehicleIt->second, player.id, 0, true)) {
                        HostedNetworkingDetail::syncPlayerToVehicleSeat(player, vehicleIt->second, *blueprint);
                    } else {
                        HostedNetworkingDetail::applyWalkingAuthoritativeState(
                            player,
                            terrainContext,
                            { modeSwitch->x, modeSwitch->y, modeSwitch->z },
                            modeSwitch->walkYaw,
                            modeSwitch->walkPitch);
                    }
                } else {
                    HostedNetworkingDetail::applyWalkingAuthoritativeState(
                        player,
                        terrainContext,
                        { modeSwitch->x, modeSwitch->y, modeSwitch->z },
                        modeSwitch->walkYaw,
                        modeSwitch->walkPitch);
                }
            } else {
                HostedNetworkingDetail::resetAuthoritativeFlightState(
                    player,
                    terrainContext,
                    modeSwitch->x,
                    modeSwitch->z);
            }
            broadcastAvatarManifest(player, player.peerId);
            log(
                "[net] applied MODE_SWITCH for player " +
                std::to_string(player.id) +
                " role " +
                sanitizeRole(modeSwitch->role) +
                " tick " +
                std::to_string(modeSwitch->tick));
        }
        return;
    }
    if (type == "RESET_FLIGHT") {
        if (const auto reset = parseResetFlightPacket(event.payload); reset.has_value()) {
            player.hasReceivedInput = true;
            player.input.tick = std::max(player.input.tick, reset->tick);
            for (auto it = player.pendingInputs.begin(); it != player.pendingInputs.end();) {
                if (it->first <= player.input.tick) {
                    it = player.pendingInputs.erase(it);
                } else {
                    ++it;
                }
            }
            HostedNetworkingDetail::resetAuthoritativeFlightState(
                player,
                terrainContext,
                reset->x,
                reset->z);
            broadcastAvatarManifest(player, player.peerId);
            log(
                "[net] applied RESET_FLIGHT for player " +
                std::to_string(player.id) +
                " tick " +
                std::to_string(reset->tick));
        }
        return;
    }
    if (type == "INPUT") {
        const auto input = parseInputPacket(event.payload);
        if (input.has_value()) {
            if (player.hasReceivedInput && input->tick <= player.input.tick) {
                return;
            }
            const PlayerMode previousMode = player.mode;
            const PlayerMode wantedMode = playerModeFromRole(input->role);
            const bool modeChanged = wantedMode != previousMode || (wantedMode == PlayerMode::Driving && player.vehicleId != input->vehicleId);
            player.hasReceivedInput = true;
            player.input = *input;
            player.pendingInputs.clear();
            player.avatar = HostedNetworkingDetail::avatarFromLocalVisuals(player.avatar, input->avatar);
            HostedNetworkingDetail::applyClientReportedAuthoritativeState(player, terrainContext, *input);
            if (modeChanged) {
                broadcastAvatarManifest(player, player.peerId);
                log(
                    "[net] applied INPUT mode switch for player " +
                    std::to_string(player.id) +
                    " role " +
                    sanitizeRole(input->role) +
                    " tick " +
                    std::to_string(input->tick));
            }
        }
        return;
    }
    if (type == "CONSTRUCT_PUBLISH") {
        if (const auto publish = parseConstructPublishPacket(event.payload); publish.has_value()) {
            constructBlueprints_[publish->blueprint.constructId] = publish->blueprint;
            constructStates_[publish->state.constructId] = publish->state;
            for (const auto& [otherId, other] : players_) {
                if (!other.connected || other.peerId == 0) {
                    continue;
                }
                transport_->send(
                    other.peerId,
                    static_cast<int>(TransportLane::Control),
                    buildConstructPublishPacket(*publish),
                    true);
            }
        }
        return;
    }
    if (type == "VEHICLE_SPAWN") {
        if (const auto spawn = parseVehicleSpawnPacket(event.payload); spawn.has_value()) {
            sharedVehicles_[spawn->state.vehicleId] = spawn->state;
            for (const auto& [otherId, other] : players_) {
                if (!other.connected || other.peerId == 0) {
                    continue;
                }
                transport_->send(
                    other.peerId,
                    static_cast<int>(TransportLane::Control),
                    buildVehicleSpawnPacket(*spawn),
                    true);
            }
        }
        return;
    }
    if (type == "VEHICLE_SEAT") {
        if (const auto seat = parseVehicleSeatPacket(event.payload); seat.has_value()) {
            auto vehicleIt = sharedVehicles_.find(seat->vehicleId);
            if (vehicleIt != sharedVehicles_.end()) {
                const ConstructBlueprint* blueprint = HostedNetworkingDetail::findConstructBlueprint(constructBlueprints_, vehicleIt->second.constructId);
                if (blueprint != nullptr) {
                    HostedNetworkingDetail::ensureVehicleSeatCount(vehicleIt->second, *blueprint);
                }
                if (HostedNetworkingDetail::applyVehicleSeatChange(vehicleIt->second, seat->playerId, seat->seatIndex, seat->entering)) {
                    auto playerIt = players_.find(seat->playerId);
                    if (playerIt != players_.end()) {
                        if (seat->entering && blueprint != nullptr) {
                            HostedNetworkingDetail::syncPlayerToVehicleSeat(playerIt->second, vehicleIt->second, *blueprint);
                        } else {
                            HostedNetworkingDetail::exitPlayerFromVehicle(playerIt->second, terrainContext, vehicleIt->second);
                        }
                    }
                    for (const auto& [otherId, other] : players_) {
                        (void)otherId;
                        if (!other.connected || other.peerId == 0) {
                            continue;
                        }
                        transport_->send(
                            other.peerId,
                            static_cast<int>(TransportLane::Control),
                            buildVehicleSeatPacket(*seat),
                            true);
                    }
                }
            }
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
            log(
                "[net] received BLOB_META from player " +
                std::to_string(player.id) +
                " kind=" +
                meta->kind +
                " hash=" +
                meta->hash +
                " chunks=" +
                std::to_string(std::max(0, meta->chunkCount)));
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
                broadcastAvatarManifest(player, player.peerId);
                log(
                    "[net] completed blob upload from player " +
                    std::to_string(player.id) +
                    " kind=" +
                    record.kind +
                    " hash=" +
                    record.hash +
                    " bytes=" +
                    std::to_string(static_cast<int>(record.raw.size())));
            }
        }
        return;
    }
    if (type == "TERRAIN_EDIT_REQUEST") {
        if (const auto request = parseTerrainEditRequestPacket(event.payload); request.has_value()) {
            HostedTerrainEditRequest queued;
            queued.peerId = event.peerId;
            queued.request = *request;
            queued.request.playerId = player.id;
            pendingTerrainEdits_.push_back(std::move(queued));
            log(
                "[net] queued TERRAIN_EDIT_REQUEST player=" +
                std::to_string(player.id) +
                " kind=" +
                request->kind +
                " op=" +
                std::to_string(request->opId));
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
        const auto frame = parseVoiceFramePacket(event.payload);
        if (!frame.has_value()) {
            return;
        }
        const int routedChannel = normalizeRadioChannel(frame->channel);
        VoiceFramePacket relayedFrame = *frame;
        relayedFrame.senderId = player.id;
        relayedFrame.channel = routedChannel;
        const std::string relayPacket = buildVoiceFramePacket(relayedFrame);
        for (const auto& [otherId, other] : players_) {
            if (otherId == player.id || !other.connected) {
                continue;
            }
            const float limit = HostedNetworkingDetail::voiceRelayDistanceMeters(player.aoi, other.aoi);
            if (routedChannel == other.avatar.radioChannel &&
                HostedNetworkingDetail::distanceSq2(player.actor.pos, other.actor.pos) <= (limit * limit)) {
                if (other.peerId == 0) {
                    hostLocalReceiveFrames_.push_back({ relayedFrame.senderId, relayedFrame.channel, relayedFrame.compressedData });
                } else if (transport_) {
                    transport_->send(other.peerId, static_cast<int>(TransportLane::Voice), relayPacket, false);
                }
            }
        }
        return;
    }
    if ((type == "CHUNK_STATE" || type == "CHUNK_PATCH") && worldStore != nullptr) {
        if (const auto chunk = parseChunkPacket(event.payload); chunk.has_value() && worldStore->applyChunkState(*chunk)) {
            worldStore->flushDirty(nullptr);
            if (transport_) {
                const std::optional<WorldChunkState> authoritativeChunk = worldStore->getChunkState(chunk->cx, chunk->cz);
                if (authoritativeChunk.has_value()) {
                    const std::string patchPacket = buildChunkPacket("CHUNK_PATCH", *authoritativeChunk, "client_edit");
                    for (const auto& [otherId, other] : players_) {
                        if (!other.connected || other.peerId == 0) {
                            continue;
                        }
                        transport_->send(other.peerId, static_cast<int>(TransportLane::Control), patchPacket, true);
                    }
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

inline void HostedWorldServer::serviceIncoming(
    const TerrainParams& terrainParams,
    const TerrainFieldContext& terrainContext,
    WorldStore* worldStore)
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
                for (auto& [vehicleId, vehicle] : sharedVehicles_) {
                    (void)vehicleId;
                    const int seatIndex = HostedNetworkingDetail::findVehicleSeatIndex(vehicle, playerId);
                    if (seatIndex >= 0) {
                        vehicle.seatOccupants[static_cast<std::size_t>(seatIndex)] = 0;
                        if (vehicle.driverPlayerId == playerId) {
                            vehicle.driverPlayerId = 0;
                        }
                    }
                }
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
            handlePacket(event, terrainParams, terrainContext, worldStore);
        }
    }
}

inline void HostedWorldServer::simulateRemotePlayers(
    float dt,
    const TerrainFieldContext& terrainContext,
    const FlightConfig& flightConfig,
    Vec3 wind)
{
    (void)flightConfig;
    (void)wind;

    for (auto& [vehicleId, vehicle] : sharedVehicles_) {
        (void)vehicleId;
        const ConstructBlueprint* blueprint = HostedNetworkingDetail::findConstructBlueprint(constructBlueprints_, vehicle.constructId);
        if (blueprint == nullptr) {
            continue;
        }
        HostedNetworkingDetail::ensureVehicleSeatCount(vehicle, *blueprint);
        const NetPlayerInput* driverInput = nullptr;
        if (vehicle.driverPlayerId > 0) {
            if (const auto playerIt = players_.find(vehicle.driverPlayerId); playerIt != players_.end()) {
                driverInput = &playerIt->second.input;
            }
        }
        HostedNetworkingDetail::simulateSharedVehicle(vehicle, *blueprint, driverInput, terrainContext, dt);
        for (std::size_t seatIndex = 0; seatIndex < vehicle.seatOccupants.size(); ++seatIndex) {
            const int occupantId = vehicle.seatOccupants[seatIndex];
            if (occupantId <= 0) {
                continue;
            }
            const auto playerIt = players_.find(occupantId);
            if (playerIt == players_.end()) {
                continue;
            }
            HostedNetworkingDetail::syncPlayerToVehicleSeat(playerIt->second, vehicle, *blueprint);
        }
    }
}

inline void enqueueClientHello(ClientReplicationState& client, const AvatarManifest& avatar)
{
    client.localAvatar = avatar;
    client.helloPending = true;
    client.outboundReliable.push_back(buildHelloPacket(avatar));
    HostedNetworkingDetail::networkStdoutLog(
        std::string("queued HELLO for callsign ") + sanitizeCallsign(avatar.callsign));
}

inline void enqueueClientInput(ClientReplicationState& client, const NetPlayerInput& input)
{
    client.pendingInputs[input.tick] = input;
    client.outboundUnreliable.push_back(buildInputPacket(input));
}

inline void enqueueClientModeSwitch(ClientReplicationState& client, const ModeSwitchPacket& modeSwitch)
{
    client.pendingControlTick = std::max(client.pendingControlTick, modeSwitch.tick);
    client.outboundReliable.push_back(buildModeSwitchPacket(modeSwitch));
    HostedNetworkingDetail::networkStdoutLog(
        std::string("queued MODE_SWITCH role=") +
        sanitizeRole(modeSwitch.role) +
        " tick=" +
        std::to_string(std::max(0, modeSwitch.tick)));
}

inline void enqueueClientResetFlight(ClientReplicationState& client, const ResetFlightPacket& reset)
{
    client.pendingControlTick = std::max(client.pendingControlTick, reset.tick);
    client.outboundReliable.push_back(buildResetFlightPacket(reset));
    HostedNetworkingDetail::networkStdoutLog(
        std::string("queued RESET_FLIGHT tick=") +
        std::to_string(std::max(0, reset.tick)));
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

inline void flushClientOutbound(ClientReplicationState& client, double nowSeconds)
{
    if (!client.transport || client.serverPeerId == 0 || !client.transport->ready()) {
        return;
    }

    std::vector<std::string> controlPackets;
    std::vector<std::string> blobPackets;
    controlPackets.reserve(client.outboundReliable.size());
    blobPackets.reserve(client.outboundReliable.size());
    for (const std::string& packet : client.outboundReliable) {
        const std::string type = HostedNetworkingDetail::packetType(packet);
        if (HostedNetworkingDetail::isBlobPacketType(type)) {
            blobPackets.push_back(packet);
        } else {
            controlPackets.push_back(packet);
        }
    }
    client.outboundReliable.clear();

    const auto sendReliablePacket = [&](const std::string& packet, int lane) {
        const std::string type = HostedNetworkingDetail::packetType(packet);
        const bool sent = client.transport->send(client.serverPeerId, lane, packet, true);
        if (type == "HELLO" || !sent) {
            HostedNetworkingDetail::networkStdoutLog(
                std::string(sent ? "sent " : "failed to send ") +
                type +
                " to peer " +
                std::to_string(client.serverPeerId));
        }
        return sent;
    };

    std::size_t controlIndex = 0;
    for (; controlIndex < controlPackets.size(); ++controlIndex) {
        if (!sendReliablePacket(controlPackets[controlIndex], static_cast<int>(TransportLane::Control))) {
            break;
        }
    }

    for (const std::string& packet : client.outboundUnreliable) {
        const std::string type = HostedNetworkingDetail::packetType(packet);
        const int lane = type == "VOICE_FRAME"
            ? static_cast<int>(TransportLane::Voice)
            : static_cast<int>(TransportLane::Snapshot);
        (void)client.transport->send(client.serverPeerId, lane, packet, false);
    }
    client.outboundUnreliable.clear();

    if (controlIndex < controlPackets.size()) {
        client.outboundReliable.insert(
            client.outboundReliable.end(),
            controlPackets.begin() + static_cast<std::ptrdiff_t>(controlIndex),
            controlPackets.end());
        client.outboundReliable.insert(client.outboundReliable.end(), blobPackets.begin(), blobPackets.end());
    } else {
        constexpr double kBlobFlushIntervalSeconds = 1.0 / 60.0;
        constexpr double kBlobBackoffSeconds = 0.25;
        constexpr std::size_t kMaxBlobReliableBytesPerFlush = 4u * 1024u;
        constexpr std::size_t kMaxBlobReliablePacketsPerFlush = 1u;

        if ((nowSeconds + 1.0e-6) < client.nextBlobFlushAt) {
            client.outboundReliable.insert(client.outboundReliable.end(), blobPackets.begin(), blobPackets.end());
        } else {
            bool blobSendFailed = false;
            std::size_t blobIndex = 0;
            std::size_t blobBytesSent = 0u;
            std::size_t blobPacketsSent = 0u;
            for (; blobIndex < blobPackets.size(); ++blobIndex) {
                const std::string& packet = blobPackets[blobIndex];
                if (blobPacketsSent > 0u &&
                    (blobPacketsSent >= kMaxBlobReliablePacketsPerFlush ||
                     blobBytesSent + packet.size() > kMaxBlobReliableBytesPerFlush)) {
                    break;
                }
                if (!sendReliablePacket(packet, static_cast<int>(TransportLane::Blob))) {
                    blobSendFailed = true;
                    break;
                }
                blobBytesSent += packet.size();
                ++blobPacketsSent;
            }
            client.outboundReliable.insert(
                client.outboundReliable.end(),
                blobPackets.begin() + static_cast<std::ptrdiff_t>(blobIndex),
                blobPackets.end());
            if (blobSendFailed || !client.outboundReliable.empty()) {
                client.nextBlobFlushAt = nowSeconds + (blobSendFailed ? kBlobBackoffSeconds : kBlobFlushIntervalSeconds);
            } else {
                client.nextBlobFlushAt = 0.0;
            }
        }
    }
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
            HostedNetworkingDetail::networkStdoutLog("client transport connected to peer " + std::to_string(event.peerId));
            continue;
        }
        if (event.type == NetEvent::Type::Disconnected) {
            client.connected = false;
            client.joinAcknowledged = false;
            client.helloPending = true;
            client.worldInfoReceived = false;
            client.worldSyncReceived = false;

            client.serverPeerId = 0;
            client.localPlayerId = 0;

            client.pendingControlTick = 0;
            client.lastAckTick = 0;
            client.nextOutboundTick = 1;
            client.nextHelloAt = 0.0;
            client.nextInputAt = 0.0;
            client.nextBlobFlushAt = 0.0;

            client.hasAoiSubscription = false;
            client.lastAoiSubscription = {};

            client.mirrorTerrainDirty = false;
            client.localAuthoritative = {};

            client.peers.clear();
            client.pendingInputs.clear();
            client.predictedStates.clear();

            client.gameplayState = {};
            client.gameplayStateDirty = false;

            client.outboundReliable.clear();
            client.outboundUnreliable.clear();

            client.blobSync = createBlobSyncState();
            client.blobCache.entries.clear();
            client.uploadedBlobKeys.clear();
            client.nextBlobRequestAtByKey.clear();
            client.blobStatusByKey.clear();
            client.pendingTerrainEditsById.clear();
            client.terrainEditAcks.clear();
            client.recentTerrainChunks.clear();
            client.voice = {};
            client.constructBlueprints.clear();
            client.constructStates.clear();
            client.sharedVehicles.clear();

            if (localPeer != nullptr) {
                localPeer->id = 0;
                localPeer->vehicleId = 0;
                localPeer->connected = false;
                localPeer->avatar = defaultAvatarManifest();
                localPeer->snapshots.clear();
                localPeer->basePos = {};
                localPeer->displayPos = {};
                localPeer->vel = {};
                localPeer->angVel = {};
                localPeer->baseRot = quatIdentity();
                localPeer->displayRot = quatIdentity();
            }

            HostedNetworkingDetail::networkStdoutLog(
                "client transport disconnected from peer " + std::to_string(event.peerId));
            continue;
        }
        if (event.type != NetEvent::Type::Message) {
            continue;
        }

        const std::string type = HostedNetworkingDetail::packetType(event.payload);
        if (type == "JOIN_OK") {
            if (const auto id = parseJoinOkPacket(event.payload); id.has_value()) {
                const bool localPeerNeedsReset = localPeer != nullptr && localPeer->id != *id;

                client.localPlayerId = *id;
                client.joinAcknowledged = true;
                client.helloPending = false;
                client.peers.erase(*id);

                client.lastAckTick = 0;
                client.pendingControlTick = 0;
                client.pendingInputs.clear();
                client.predictedStates.clear();
                client.localAuthoritative = {};

                if (localPeerNeedsReset) {
                    localPeer->id = *id;
                    localPeer->vehicleId = 0;
                    localPeer->connected = true;
                    localPeer->avatar = defaultAvatarManifest();
                    localPeer->snapshots.clear();
                    localPeer->basePos = {};
                    localPeer->displayPos = {};
                    localPeer->vel = {};
                    localPeer->angVel = {};
                    localPeer->baseRot = quatIdentity();
                    localPeer->displayRot = quatIdentity();
                } else if (localPeer != nullptr) {
                    localPeer->id = *id;
                    localPeer->vehicleId = 0;
                    localPeer->connected = true;
                }

                HostedNetworkingDetail::networkStdoutLog(
                    "received JOIN_OK as player " + std::to_string(*id));
            }
            continue;
        }
        if (type == "WORLD_INFO") {
            if (const auto info = parseWorldInfoPacket(event.payload); info.has_value()) {
                client.lastWorldInfo = *info;
                client.worldInfoReceived = true;
                HostedNetworkingDetail::networkStdoutLog("received WORLD_INFO");
                if (mirrorWorldStore != nullptr) {
                    std::string error;
                    if (mirrorWorldStore->applyWorldInfo(*info, &error)) {
                        terrainParams = mirrorWorldStore->buildGroundParams(terrainParams).terrainParams;
                        client.mirrorTerrainDirty = true;
                    }
                }
            }
            continue;
        }
        if (type == "WORLD_SYNC") {
            client.worldSyncReceived = applyWorldSyncPacket(event.payload, terrainParams);
            if (client.worldSyncReceived) {
                client.mirrorTerrainDirty = true;
            }
            HostedNetworkingDetail::networkStdoutLog(
                std::string("received WORLD_SYNC: ") + (client.worldSyncReceived ? "applied" : "rejected"));
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
                HostedNetworkingDetail::requestMissingAvatarBlobs(client, nowSeconds, *avatar);
            }
            continue;
        }
        if (type == "CONSTRUCT_PUBLISH") {
            if (const auto publish = parseConstructPublishPacket(event.payload); publish.has_value()) {
                client.constructBlueprints[publish->blueprint.constructId] = publish->blueprint;
                client.constructStates[publish->state.constructId] = publish->state;
            }
            continue;
        }
        if (type == "VEHICLE_SPAWN") {
            if (const auto spawn = parseVehicleSpawnPacket(event.payload); spawn.has_value()) {
                client.sharedVehicles[spawn->state.vehicleId] = spawn->state;
            }
            continue;
        }
        if (type == "VEHICLE_STATE") {
            if (const auto vehicle = parseVehicleStatePacket(event.payload); vehicle.has_value()) {
                client.sharedVehicles[vehicle->vehicleId] = *vehicle;
            }
            continue;
        }
        if (type == "VEHICLE_SEAT") {
            if (const auto seat = parseVehicleSeatPacket(event.payload); seat.has_value()) {
                auto vehicleIt = client.sharedVehicles.find(seat->vehicleId);
                if (vehicleIt != client.sharedVehicles.end()) {
                    if (seat->entering) {
                        const std::size_t desiredSize = std::max<std::size_t>(
                            vehicleIt->second.seatOccupants.size(),
                            static_cast<std::size_t>(std::max(0, seat->seatIndex)) + 1u);
                        vehicleIt->second.seatOccupants.resize(desiredSize, 0);
                        if (seat->seatIndex >= 0 && seat->seatIndex < static_cast<int>(vehicleIt->second.seatOccupants.size())) {
                            vehicleIt->second.seatOccupants[static_cast<std::size_t>(seat->seatIndex)] = seat->playerId;
                            if (seat->seatIndex == 0) {
                                vehicleIt->second.driverPlayerId = seat->playerId;
                            }
                        }
                    } else {
                        const auto it = std::find(
                            vehicleIt->second.seatOccupants.begin(),
                            vehicleIt->second.seatOccupants.end(),
                            seat->playerId);
                        if (it != vehicleIt->second.seatOccupants.end()) {
                            *it = 0;
                        }
                        if (vehicleIt->second.driverPlayerId == seat->playerId) {
                            vehicleIt->second.driverPlayerId = 0;
                        }
                    }
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
                    if (client.pendingControlTick > 0 && snapshot->ack >= client.pendingControlTick) {
                        client.pendingControlTick = 0;
                    }
                    if (localPeer != nullptr) {
                        localPeer->id = snapshot->id;
                        localPeer->avatar = snapshot->avatar;
                        localPeer->vehicleId = snapshot->vehicleId;
                        localPeer->connected = true;
                        if (client.pendingControlTick > 0 && snapshot->ack < client.pendingControlTick) {
                            continue;
                        }
                    }
                    client.localAuthoritative = {};
                } else {
                    RemotePeerState& peer = client.peers[snapshot->id];
                    peer.id = snapshot->id;
                    peer.avatar = snapshot->avatar;
                    peer.vehicleId = snapshot->vehicleId;
                    peer.connected = true;
                    appendRemoteSnapshot(peer, {
                        nowSeconds,
                        snapshot->pos,
                        snapshot->rot,
                        snapshot->vel,
                        snapshot->angVel
                    });
                    (void)sampleRemotePeer(peer, nowSeconds);
                    HostedNetworkingDetail::requestMissingAvatarBlobs(client, nowSeconds, snapshot->avatar);
                }
            }
            continue;
        }
        if ((type == "CHUNK_STATE" || type == "CHUNK_PATCH") && mirrorWorldStore != nullptr) {
            if (const auto chunk = parseChunkPacket(event.payload); chunk.has_value()) {
                if (mirrorWorldStore->applyChunkState(*chunk)) {
                    client.mirrorTerrainDirty = true;
                    client.recentTerrainChunks.push_back(*chunk);
                    HostedNetworkingDetail::networkStdoutLog(
                        std::string("applied ") +
                        type +
                        " cx=" +
                        std::to_string(chunk->cx) +
                        " cz=" +
                        std::to_string(chunk->cz) +
                        " rev=" +
                        std::to_string(chunk->revision));
                }
            }
            continue;
        }
        if (type == "GAMEPLAY_STATE") {
            if (const auto gameplayState = parseGameplayStatePacket(event.payload); gameplayState.has_value()) {
                client.gameplayState = *gameplayState;
                client.gameplayStateDirty = true;
            }
            continue;
        }
        if (type == "CRATER_EVENT" && mirrorWorldStore != nullptr) {
            if (const auto crater = parseCraterPacket(event.payload); crater.has_value()) {
                if (mirrorWorldStore->applyCrater(*crater).first) {
                    client.mirrorTerrainDirty = true;
                }
            }
            continue;
        }
        if (type == "BLOB_META") {
            if (const auto meta = parseBlobMetaPacket(event.payload); meta.has_value()) {
                acceptIncomingBlobMeta(client.blobSync, *meta);
                HostedNetworkingDetail::updateBlobTransferStatus(
                    client,
                    nowSeconds,
                    meta->kind,
                    meta->hash,
                    BlobTransferPhase::Receiving,
                    "metadata accepted",
                    meta->ownerId,
                    meta->role,
                    meta->chunkCount,
                    0,
                    meta->rawBytes);
                HostedNetworkingDetail::networkStdoutLog(
                    "received BLOB_META kind=" +
                    meta->kind +
                    " hash=" +
                    meta->hash +
                    " chunks=" +
                    std::to_string(std::max(0, meta->chunkCount)));
            }
            continue;
        }
        if (type == "BLOB_CHUNK") {
            if (const auto chunk = parseBlobChunkPacket(event.payload); chunk.has_value()) {
                const auto completed = acceptIncomingBlobChunk(client.blobSync, *chunk);
                if (const auto incomingIt = client.blobSync.incoming.find(blobTransferKey(chunk->kind, chunk->hash));
                    incomingIt != client.blobSync.incoming.end()) {
                    const BlobIncomingTransfer& incoming = incomingIt->second;
                    HostedNetworkingDetail::updateBlobTransferStatus(
                        client,
                        nowSeconds,
                        chunk->kind,
                        chunk->hash,
                        incoming.receivedCount >= incoming.chunkCount
                            ? BlobTransferPhase::Received
                            : BlobTransferPhase::Receiving,
                        incoming.receivedCount >= incoming.chunkCount
                            ? "all chunks received"
                            : "chunk received",
                        incoming.meta.ownerId,
                        incoming.meta.role,
                        incoming.chunkCount,
                        incoming.receivedCount,
                        incoming.meta.rawBytes);
                }
                if (completed.has_value()) {
                    AvatarBlobRecord record;
                    record.kind = completed->first.kind;
                    record.hash = completed->first.hash;
                    record.raw = completed->second;
                    record.meta = completed->first;
                    client.blobCache.put(record);
                    HostedNetworkingDetail::updateBlobTransferStatus(
                        client,
                        nowSeconds,
                        record.kind,
                        record.hash,
                        BlobTransferPhase::Received,
                        "blob transfer complete",
                        record.meta.ownerId,
                        record.meta.role,
                        record.meta.chunkCount,
                        record.meta.chunkCount,
                        record.meta.rawBytes);
                    HostedNetworkingDetail::networkStdoutLog(
                        "completed BLOB transfer kind=" +
                        record.kind +
                        " hash=" +
                        record.hash +
                        " bytes=" +
                        std::to_string(static_cast<int>(record.raw.size())));
                }
            }
            continue;
        }
        if (type == "TERRAIN_EDIT_ACK") {
            if (const auto ack = parseTerrainEditAckPacket(event.payload); ack.has_value()) {
                client.pendingTerrainEditsById.erase(ack->opId);
                client.terrainEditAcks.push_back(*ack);
                HostedNetworkingDetail::networkStdoutLog(
                    std::string("received TERRAIN_EDIT_ACK op=") +
                    std::to_string(ack->opId) +
                    " accepted=" +
                    (ack->accepted ? "1" : "0") +
                    " chunks=" +
                    std::to_string(std::max(0, ack->touchedChunks)) +
                    (ack->reason.empty() ? std::string() : std::string(" reason=") + ack->reason));
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
                VoiceTalkerState& talker = client.voice.talkersById[state->id];
                talker.playerId = state->id;
                talker.channel = state->channel;
                talker.transmitting = state->transmitting;
                talker.lastStateAtSeconds = nowSeconds;
            }
            continue;
        }
        if (type == "VOICE_FRAME") {
            if (const auto frame = parseVoiceFramePacket(event.payload); frame.has_value()) {
                if (!frame->compressedData.empty()) {
                    client.voice.inboundCompressedFrames.push_back({
                        frame->senderId,
                        normalizeRadioChannel(frame->channel),
                        frame->compressedData
                    });
                    VoiceTalkerState& talker = client.voice.talkersById[frame->senderId];
                    talker.playerId = frame->senderId;
                    talker.channel = normalizeRadioChannel(frame->channel);
                    talker.transmitting = true;
                    talker.lastFrameAtSeconds = nowSeconds;
                }
            }
            continue;
        }
        if (type == "PEER_LEAVE") {
            if (const auto id = parsePeerLeavePacket(event.payload); id.has_value()) {
                client.peers.erase(*id);
                client.voice.talkersById.erase(*id);
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

    flushClientOutbound(client, nowSeconds);
}

inline void HostedWorldServer::broadcastSnapshots(double nowSeconds)
{
    if (!transport_) {
        return;
    }
    ++snapshotSequence_;
    for (const auto& [targetId, target] : players_) {
        if (!target.connected || target.peerId == 0 || !target.hasReceivedHello) {
            continue;
        }
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
            snapshot.vehicleId = player.vehicleId;
            snapshot.timestamp = nowSeconds;
            snapshot.avatar = player.avatar;
            snapshot.avatar.role = playerModeToken(player.mode);
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
        if (player.connected && player.peerId != 0 && player.hasReceivedHello) {
            transport_->send(player.peerId, static_cast<int>(TransportLane::Control), packet, false);
        }
    }
}

inline void HostedWorldServer::flushBlobTransfers(double nowSeconds)
{
    if (!transport_) {
        return;
    }

    constexpr double kBlobFlushIntervalSeconds = 1.0 / 60.0;
    constexpr double kBlobBackoffSeconds = 0.25;
    constexpr std::size_t kMaxBlobReliableBytesPerFlush = 4u * 1024u;
    constexpr std::size_t kMaxBlobReliablePacketsPerFlush = 1u;

    for (auto& [playerId, player] : players_) {
        (void)playerId;
        if (!player.connected || player.peerId == 0 || player.outboundBlobReliable.empty()) {
            continue;
        }
        if ((nowSeconds + 1.0e-6) < player.nextBlobFlushAt) {
            continue;
        }

        bool sendFailed = false;
        std::size_t sentBytes = 0u;
        std::size_t sentPackets = 0u;
        while (!player.outboundBlobReliable.empty()) {
            const HostedBlobOutboundPacket& packet = player.outboundBlobReliable.front();
            if (sentPackets > 0u &&
                (sentPackets >= kMaxBlobReliablePacketsPerFlush ||
                 sentBytes + packet.payload.size() > kMaxBlobReliableBytesPerFlush)) {
                break;
            }
            if (!transport_->send(player.peerId, static_cast<int>(TransportLane::Blob), packet.payload, true)) {
                sendFailed = true;
                break;
            }

            sentBytes += packet.payload.size();
            ++sentPackets;

            const std::string transferKey = packet.transferKey;
            player.outboundBlobReliable.pop_front();
            if (auto countIt = player.queuedBlobPacketCountsByTransfer.find(transferKey);
                countIt != player.queuedBlobPacketCountsByTransfer.end()) {
                countIt->second -= 1;
                if (countIt->second <= 0) {
                    player.queuedBlobPacketCountsByTransfer.erase(countIt);
                }
            }
        }

        if (sendFailed || !player.outboundBlobReliable.empty()) {
            player.nextBlobFlushAt = nowSeconds + (sendFailed ? kBlobBackoffSeconds : kBlobFlushIntervalSeconds);
        } else {
            player.nextBlobFlushAt = 0.0;
        }
    }
}

inline void HostedWorldServer::update(
    double nowSeconds,
    float dt,
    const TerrainFieldContext& terrainContext,
    const FlightConfig& flightConfig,
    Vec3 wind,
    WorldStore* worldStore)
{
    simulateRemotePlayers(dt, terrainContext, flightConfig, wind);
    snapshotAccumulator_ += dt;
    cloudAccumulator_ += dt;
    if (snapshotAccumulator_ >= (1.0 / 90.0)) {
        snapshotAccumulator_ = std::fmod(snapshotAccumulator_, 1.0 / 90.0);
        broadcastSnapshots(nowSeconds);
        broadcastVehicleStates();
    }
    if (cloudAccumulator_ >= 1.0) {
        cloudAccumulator_ = std::fmod(cloudAccumulator_, 1.0);
        broadcastCloudSnapshot(nowSeconds);
    }
    flushBlobTransfers(nowSeconds);
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
    VoiceFramePacket frame;
    frame.senderId = local->id;
    frame.channel = normalizedChannel;
    frame.compressedData = std::string(compressedFrame);
    const std::string packet = buildVoiceFramePacket(frame);
    for (const auto& [playerId, player] : players_) {
        if (playerId == local->id || !player.connected || player.peerId == 0) {
            continue;
        }
        const float limit = HostedNetworkingDetail::voiceRelayDistanceMeters(local->aoi, player.aoi);
        if (normalizedChannel != player.avatar.radioChannel ||
            HostedNetworkingDetail::distanceSq2(local->actor.pos, player.actor.pos) > (limit * limit)) {
            continue;
        }
        if (transport_) {
            transport_->send(player.peerId, static_cast<int>(TransportLane::Voice), packet, false);
        }
    }
}

inline std::vector<QueuedVoiceFrame> HostedWorldServer::drainHostLocalVoiceFrames()
{
    return drainLocalVoiceFrames();
}

inline std::vector<QueuedVoiceFrame> HostedWorldServer::drainLocalVoiceFrames()
{
    std::vector<QueuedVoiceFrame> drained;
    drained.swap(hostLocalReceiveFrames_);
    return drained;
}

inline std::vector<HostedTerrainEditRequest> HostedWorldServer::drainPendingTerrainEdits()
{
    std::vector<HostedTerrainEditRequest> drained;
    drained.reserve(pendingTerrainEdits_.size());
    while (!pendingTerrainEdits_.empty()) {
        drained.push_back(std::move(pendingTerrainEdits_.front()));
        pendingTerrainEdits_.pop_front();
    }
    return drained;
}

inline void HostedWorldServer::sendTerrainEditAck(NetPeerId peerId, const TerrainEditAck& ack)
{
    if (!transport_ || peerId == 0) {
        return;
    }
    transport_->send(
        peerId,
        static_cast<int>(TransportLane::Control),
        buildTerrainEditAckPacket(ack),
        true);
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
