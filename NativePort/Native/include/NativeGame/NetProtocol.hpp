#pragma once

#include "NativeGame/Flight.hpp"
#include "NativeGame/WorldStore.hpp"
#include "NativeGame/WorldWire.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace NativeGame {

constexpr float kRemoteInterpolationDelaySeconds = 0.12f;
constexpr float kRemoteExtrapolationCapSeconds = 0.60f;
constexpr std::size_t kRemoteSnapshotBufferLimit = 48u;
constexpr int kVoiceChannelCount = 8;

using ReplicatedEntityId = std::uint64_t;
using VehicleId = std::uint64_t;
using ConstructId = std::uint64_t;
using ConstructRevisionId = std::uint64_t;
using TerrainEditOpId = std::uint64_t;

enum class PlayerMode : std::uint8_t {
    Flight = 0,
    Walking = 1,
    Driving = 2
};

enum class BlobTransferPhase : std::uint8_t {
    Idle = 0,
    Requested = 1,
    UploadQueued = 2,
    Uploading = 3,
    Receiving = 4,
    Received = 5,
    Decoding = 6,
    Ready = 7,
    Failed = 8
};

struct OnlineSettings {
    bool steamEnabled = true;
    bool multiplayerEnabled = false;
    bool voiceEnabled = true;
    bool voiceLoopback = false;
    bool pushToTalk = true;
    int radioChannel = 1;
    std::string callsign = "Pilot";
    std::string sessionMode = "offline";
    std::string lastLobbyId;
    std::string lastJoinHostId;
};

enum class OnlineSessionRole : std::uint8_t {
    Offline = 0,
    Host = 1,
    Client = 2
};

enum class TransportLane : int {
    Control = 0,
    Snapshot = 1,
    Blob = 2,
    Voice = 3
};

struct AvatarRoleConfig {
    std::string modelHash = "builtin-procedural-plane";
    float scale = 1.0f;
    std::string skinHash;
    std::string assetKey = "builtin:procedural_plane";
    std::string modelFormat = "builtin";
    float forwardAxisYawDegrees = 0.0f;
    Quat importRotation = quatIdentity();
    bool builtinWalkingRig = false;
    float yawDegrees = 0.0f;
    float pitchDegrees = 0.0f;
    float rollDegrees = 0.0f;
    Vec3 offset {};
};

struct AvatarManifest {
    std::string role = "plane";
    std::string callsign = "Pilot";
    int radioChannel = 1;
    bool radioTx = false;
    AvatarRoleConfig plane {};
    AvatarRoleConfig walking {};
};

struct NetPlayerInput {
    int tick = 0;
    std::string role = "plane";
    float frameDt = 1.0f / 180.0f;
    Vec3 reportedPos {};
    Quat reportedRot = quatIdentity();
    Vec3 reportedVel {};
    Vec3 reportedAngVel {};
    int reportedSimTick = 0;
    float walkYaw = 0.0f;
    float walkPitch = 0.0f;
    float walkMoveSpeed = 10.0f;
    float throttle = 0.0f;
    float yokePitch = 0.0f;
    float yokeYaw = 0.0f;
    float yokeRoll = 0.0f;
    float manualElevatorTrim = 0.0f;
    float manualRudderTrim = 0.0f;
    bool walkForward = false;
    bool walkBackward = false;
    bool walkStrafeLeft = false;
    bool walkStrafeRight = false;
    bool walkSprint = false;
    bool walkJump = false;
    bool flightThrottleUp = false;
    bool flightThrottleDown = false;
    bool flightAirBrakes = false;
    bool flightAfterburner = false;
    bool firePrimary = false;
    bool dropBomb = false;
    bool terrainGunAdd = false;
    bool terrainGunRemove = false;
    TerrainEditOpId terrainEditOpId = 0;
    VehicleId vehicleId = 0;
    float driveThrottle = 0.0f;
    float driveSteer = 0.0f;
    bool driveBrake = false;
    bool terraformMode = false;
    AvatarManifest avatar {};
};

struct NetPlayerSnapshot {
    int id = 0;
    int ack = 0;
    Vec3 pos {};
    Quat rot = quatIdentity();
    Vec3 vel {};
    Vec3 angVel {};
    float throttle = 0.0f;
    float elevator = 0.0f;
    float aileron = 0.0f;
    float rudder = 0.0f;
    int simTick = 0;
    VehicleId vehicleId = 0;
    double timestamp = 0.0;
    AvatarManifest avatar {};
};

struct VoiceStatePacket {
    int id = 0;
    int channel = 1;
    bool transmitting = false;
};

struct VoiceFramePacket {
    int senderId = 0;
    int channel = 1;
    std::string data;
    std::string compressedData;
};

struct TerrainEditRequest {
    TerrainEditOpId opId = 0;
    int playerId = 0;
    std::string kind = "terrain_add";
    Vec3 center {};
    Vec3 surfaceNormal { 0.0f, 1.0f, 0.0f };
    float radius = 0.0f;
    float magnitude = 0.0f;
    double requestedAt = 0.0;
};

struct TerrainEditAck {
    TerrainEditOpId opId = 0;
    int playerId = 0;
    bool accepted = false;
    std::string kind = "terrain_add";
    Vec3 center {};
    float radius = 0.0f;
    float magnitude = 0.0f;
    int touchedChunks = 0;
    std::string reason;
};

struct SoftBodyCageState {
    float heave = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    std::array<float, 4> wheelCompression {};
};

struct ConstructBlueprint {
    ConstructId constructId = 0;
    ConstructRevisionId revisionId = 0;
    std::string label = "road_buggy";
    Vec3 bodyHalfExtents { 1.25f, 0.55f, 2.35f };
    std::vector<Vec3> seatOffsets {
        { 0.0f, 1.05f, 0.35f },
        { 0.0f, 1.05f, -0.35f }
    };
    float maxSpeedMps = 38.0f;
    float accelerationMps2 = 18.0f;
    float brakingMps2 = 26.0f;
    float steeringRate = 1.8f;
    float steeringAssist = 0.72f;
    float roadGrip = 1.0f;
    float offroadGrip = 0.45f;
    float maxHealth = 200.0f;
    float suspensionTravel = 0.28f;
};

struct ConstructState {
    ConstructId constructId = 0;
    ConstructRevisionId revisionId = 0;
    int authorPlayerId = 0;
    bool published = false;
    std::string label = "road_buggy";
};

struct SharedVehicleState {
    VehicleId vehicleId = 0;
    ReplicatedEntityId entityId = 0;
    ConstructId constructId = 0;
    ConstructRevisionId constructRevisionId = 0;
    int authoritativeOwnerPlayerId = 0;
    int driverPlayerId = 0;
    std::vector<int> seatOccupants;
    Vec3 pos {};
    Vec3 vel {};
    float yawRadians = 0.0f;
    float steerNormalized = 0.0f;
    float throttleNormalized = 0.0f;
    float roadAdhesion = 0.0f;
    float surfaceFriction = 0.0f;
    float health = 200.0f;
    float maxHealth = 200.0f;
    SoftBodyCageState cage {};
    bool active = true;
};

struct VehicleSpawnPacket {
    SharedVehicleState state {};
    std::string reason = "spawn";
};

struct VehicleSeatPacket {
    VehicleId vehicleId = 0;
    int playerId = 0;
    int seatIndex = -1;
    bool entering = true;
};

struct ConstructPublishPacket {
    ConstructBlueprint blueprint {};
    ConstructState state {};
};

struct BlobTransferStatus {
    std::string kind;
    std::string hash;
    std::string ownerId;
    std::string role;
    BlobTransferPhase phase = BlobTransferPhase::Idle;
    int chunkCount = 0;
    int completedChunks = 0;
    int rawBytes = 0;
    double updatedAtSeconds = 0.0;
    std::string detail;
};

struct VoiceTalkerState {
    int playerId = 0;
    int channel = 1;
    bool transmitting = false;
    double lastStateAtSeconds = 0.0;
    double lastFrameAtSeconds = 0.0;
};

struct BlobMetaPacket {
    std::string kind;
    std::string hash;
    std::string modelHash;
    std::string role;
    std::string ownerId;
    int rawBytes = 0;
    int encodedBytes = 0;
    int chunkSize = 0;
    int chunkCount = 0;
};

struct BlobChunkPacket {
    std::string kind;
    std::string hash;
    int index = 0;
    std::string data;
};

struct AoiSubscription {
    int centerChunkX = 0;
    int centerChunkZ = 0;
    int radiusChunks = 20;
    float snapshotNearMeters = 1024.0f;
    float snapshotFarMeters = 4096.0f;
};

struct ModeSwitchPacket {
    int tick = 0;
    std::string role = "plane";
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    VehicleId vehicleId = 0;
    float walkYaw = 0.0f;
    float walkPitch = 0.0f;
};

struct ResetFlightPacket {
    int tick = 0;
    float x = 0.0f;
    float z = 0.0f;
};

struct RemoteSnapshotSample {
    double timestamp = 0.0;
    Vec3 pos {};
    Quat rot = quatIdentity();
    Vec3 vel {};
    Vec3 angVel {};
};

struct RemotePeerState {
    int id = 0;
    AvatarManifest avatar {};
    VehicleId vehicleId = 0;
    Vec3 basePos {};
    Quat baseRot = quatIdentity();
    Vec3 displayPos {};
    Quat displayRot = quatIdentity();
    Vec3 vel {};
    Vec3 angVel {};
    float hullStrength = 100.0f;
    float fuselageStrength = 100.0f;
    float wear = 0.0f;
    bool terraformMode = false;
    bool connected = false;
    std::deque<RemoteSnapshotSample> snapshots;
};

struct NetGameplayPlayerState {
    int id = 0;
    float hullStrength = 100.0f;
    float fuselageStrength = 100.0f;
    float wear = 0.0f;
    bool terraformMode = false;
    int targetsDestroyed = 0;
};

struct NetGameplayObjectState {
    std::string kind = "projectile";
    int id = 0;
    int ownerId = 0;
    bool active = true;
    Vec3 pos {};
    Vec3 vel {};
    float radius = 0.5f;
    float ttl = 0.0f;
    float health = 0.0f;
    float maxHealth = 0.0f;
};

struct NetGameplayStatePacket {
    double timestamp = 0.0;
    std::vector<NetGameplayPlayerState> players;
    std::vector<NetGameplayObjectState> objects;
};

inline float clampNetFloat(float value, float minValue, float maxValue)
{
    return std::clamp(value, minValue, maxValue);
}

inline int normalizeRadioChannel(int value)
{
    return std::clamp(value, 1, kVoiceChannelCount);
}

inline std::string sanitizeCallsign(const std::string& value)
{
    std::string out;
    out.reserve(std::min<std::size_t>(value.size(), 20u));
    for (const unsigned char ch : value) {
        if (ch < 32 || ch > 126 || ch == '|') {
            continue;
        }
        out.push_back(static_cast<char>(ch));
        if (out.size() >= 20u) {
            break;
        }
    }
    while (!out.empty() && std::isspace(static_cast<unsigned char>(out.front())) != 0) {
        out.erase(out.begin());
    }
    while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back())) != 0) {
        out.pop_back();
    }
    return out.empty() ? std::string("Pilot") : out;
}

inline std::string sanitizeRole(std::string_view role)
{
    if (role == "walking" || role == "walk" || role == "w") {
        return "walking";
    }
    if (role == "driving" || role == "drive" || role == "vehicle" || role == "car" || role == "d") {
        return "driving";
    }
    return "plane";
}

inline PlayerMode playerModeFromRole(std::string_view role)
{
    const std::string sanitized = sanitizeRole(role);
    if (sanitized == "walking") {
        return PlayerMode::Walking;
    }
    if (sanitized == "driving") {
        return PlayerMode::Driving;
    }
    return PlayerMode::Flight;
}

inline const char* playerModeToken(PlayerMode mode)
{
    switch (mode) {
    case PlayerMode::Walking:
        return "walking";
    case PlayerMode::Driving:
        return "driving";
    case PlayerMode::Flight:
    default:
        return "plane";
    }
}

inline std::string formatNetFloat(double value, int precision = 4)
{
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(precision);
    stream << value;
    return stream.str();
}

inline std::vector<std::string> splitPacketFields(std::string_view packet, char delimiter);
inline std::string encodeNetVec3(const Vec3& value);
inline Vec3 decodeNetVec3(const std::string& value, const Vec3& fallback);

inline std::string encodeVehicleSeatOccupants(const std::vector<int>& seatOccupants)
{
    std::ostringstream stream;
    for (std::size_t index = 0; index < seatOccupants.size(); ++index) {
        if (index > 0u) {
            stream << ',';
        }
        stream << std::max(0, seatOccupants[index]);
    }
    return stream.str();
}

inline std::vector<int> decodeVehicleSeatOccupants(const std::string& value)
{
    std::vector<int> seatOccupants;
    for (const std::string& token : splitPacketFields(value, ',')) {
        if (token.empty()) {
            continue;
        }
        seatOccupants.push_back(std::max(0, std::atoi(token.c_str())));
    }
    return seatOccupants;
}

inline std::string encodeVec3List(const std::vector<Vec3>& values)
{
    std::ostringstream stream;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0u) {
            stream << ';';
        }
        stream
            << formatNetFloat(values[index].x) << ','
            << formatNetFloat(values[index].y) << ','
            << formatNetFloat(values[index].z);
    }
    return stream.str();
}

inline std::vector<Vec3> decodeVec3List(const std::string& value)
{
    std::vector<Vec3> decoded;
    for (const std::string& entry : splitPacketFields(value, ';')) {
        if (entry.empty()) {
            continue;
        }
        const std::vector<std::string> parts = splitPacketFields(entry, ',');
        if (parts.size() != 3u) {
            continue;
        }
        decoded.push_back({
            std::strtof(parts[0].c_str(), nullptr),
            std::strtof(parts[1].c_str(), nullptr),
            std::strtof(parts[2].c_str(), nullptr)
        });
    }
    return decoded;
}

inline std::string encodeWheelCompression(const std::array<float, 4>& values)
{
    std::ostringstream stream;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0u) {
            stream << ',';
        }
        stream << formatNetFloat(values[index]);
    }
    return stream.str();
}

inline std::array<float, 4> decodeWheelCompression(const std::string& value)
{
    std::array<float, 4> decoded {};
    const std::vector<std::string> parts = splitPacketFields(value, ',');
    for (std::size_t index = 0; index < decoded.size() && index < parts.size(); ++index) {
        decoded[index] = std::strtof(parts[index].c_str(), nullptr);
    }
    return decoded;
}

inline std::vector<std::string> splitPacketFields(std::string_view packet, char delimiter = '|')
{
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (start <= packet.size()) {
        const std::size_t split = packet.find(delimiter, start);
        if (split == std::string_view::npos) {
            fields.emplace_back(packet.substr(start));
            break;
        }
        fields.emplace_back(packet.substr(start, split - start));
        start = split + 1u;
        if (start > packet.size()) {
            fields.emplace_back();
            break;
        }
    }
    return fields;
}


struct PacketFieldMap : std::unordered_map<std::string, std::string> {
    using std::unordered_map<std::string, std::string>::unordered_map;
    using std::unordered_map<std::string, std::string>::operator[];

    const std::string& operator[](const std::string& key) const
    {
        const auto it = this->find(key);
        static const std::string kEmpty;
        return it == this->end() ? kEmpty : it->second;
    }
};

inline PacketFieldMap parseKeyValueFields(const std::vector<std::string>& fields, std::size_t startIndex = 1u)
{
    PacketFieldMap kv;
    if (startIndex >= fields.size()) {
        return kv;
    }
    kv.reserve(fields.size() - startIndex);
    for (std::size_t index = startIndex; index < fields.size(); ++index) {
        const std::size_t separator = fields[index].find('=');
        if (separator == std::string::npos) {
            continue;
        }
        kv.insert_or_assign(fields[index].substr(0, separator), fields[index].substr(separator + 1u));
    }
    return kv;
}

inline std::pair<std::optional<int>, std::optional<std::size_t>> readTrailingInteger(
    const std::vector<std::string>& fields,
    std::size_t startIndex = 1u)
{
    for (std::size_t index = fields.size(); index-- > startIndex;) {
        try {
            return { std::stoi(fields[index]), index };
        } catch (...) {
        }
    }
    return { std::nullopt, std::nullopt };
}

inline std::string encodePacket(std::string_view packetType, const std::vector<std::pair<std::string, std::string>>& fields)
{
    std::string packet(packetType);
    for (const auto& [key, value] : fields) {
        packet.push_back('|');
        packet.append(key);
        packet.push_back('=');
        packet.append(value);
    }
    return packet;
}

namespace NetProtocolDetail {

constexpr std::string_view kBase64Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

inline int decodeBase64Char(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A';
    }
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a' + 26;
    }
    if (ch >= '0' && ch <= '9') {
        return ch - '0' + 52;
    }
    if (ch == '+') {
        return 62;
    }
    if (ch == '/') {
        return 63;
    }
    return -1;
}

inline std::string encodeBase64(const std::string& raw)
{
    std::string encoded;
    encoded.reserve(((raw.size() + 2u) / 3u) * 4u);
    for (std::size_t index = 0; index < raw.size(); index += 3u) {
        const unsigned char b1 = static_cast<unsigned char>(raw[index]);
        const unsigned char b2 = (index + 1u < raw.size()) ? static_cast<unsigned char>(raw[index + 1u]) : 0u;
        const unsigned char b3 = (index + 2u < raw.size()) ? static_cast<unsigned char>(raw[index + 2u]) : 0u;
        const std::uint32_t block =
            (static_cast<std::uint32_t>(b1) << 16u) |
            (static_cast<std::uint32_t>(b2) << 8u) |
            static_cast<std::uint32_t>(b3);
        encoded.push_back(kBase64Chars[(block >> 18u) & 63u]);
        encoded.push_back(kBase64Chars[(block >> 12u) & 63u]);
        encoded.push_back(index + 1u < raw.size() ? kBase64Chars[(block >> 6u) & 63u] : '=');
        encoded.push_back(index + 2u < raw.size() ? kBase64Chars[block & 63u] : '=');
    }
    return encoded;
}

inline std::optional<std::string> decodeBase64(const std::string& encoded)
{
    if (encoded.empty()) {
        return std::string {};
    }

    std::string compact;
    compact.reserve(encoded.size());
    for (const unsigned char ch : encoded) {
        if (!std::isspace(ch)) {
            compact.push_back(static_cast<char>(ch));
        }
    }
    if ((compact.size() % 4u) != 0u) {
        return std::nullopt;
    }

    std::string raw;
    raw.reserve((compact.size() / 4u) * 3u);
    for (std::size_t index = 0; index < compact.size(); index += 4u) {
        const int c1 = decodeBase64Char(compact[index]);
        const int c2 = decodeBase64Char(compact[index + 1u]);
        const int c3 = compact[index + 2u] == '=' ? -2 : decodeBase64Char(compact[index + 2u]);
        const int c4 = compact[index + 3u] == '=' ? -2 : decodeBase64Char(compact[index + 3u]);
        if (c1 < 0 || c2 < 0 || c3 == -1 || c4 == -1) {
            return std::nullopt;
        }

        const std::uint32_t block =
            (static_cast<std::uint32_t>(c1) << 18u) |
            (static_cast<std::uint32_t>(c2) << 12u) |
            (static_cast<std::uint32_t>(std::max(c3, 0)) << 6u) |
            static_cast<std::uint32_t>(std::max(c4, 0));
        raw.push_back(static_cast<char>((block >> 16u) & 0xFFu));
        if (c3 != -2) {
            raw.push_back(static_cast<char>((block >> 8u) & 0xFFu));
        }
        if (c4 != -2) {
            raw.push_back(static_cast<char>(block & 0xFFu));
        }
    }
    return raw;
}

}  // namespace NetProtocolDetail

inline AvatarManifest defaultAvatarManifest()
{
    AvatarManifest manifest;
    manifest.plane.modelHash = "builtin-procedural-plane";
    manifest.plane.assetKey = "builtin:procedural_plane";
    manifest.plane.modelFormat = "builtin";
    manifest.plane.forwardAxisYawDegrees = 0.0f;
    manifest.plane.importRotation = quatIdentity();
    manifest.walking.modelHash = "builtin-walking-biped";
    manifest.walking.assetKey = "builtin:walking_biped";
    manifest.walking.modelFormat = "builtin";
    manifest.walking.forwardAxisYawDegrees = 0.0f;
    manifest.walking.importRotation = quatIdentity();
    manifest.walking.builtinWalkingRig = true;
    manifest.callsign = "Pilot";
    return manifest;
}

inline void appendAvatarRoleConfigFields(
    WorldKeyValueFields& fields,
    std::string_view prefix,
    const AvatarRoleConfig& config)
{
    const std::string keyPrefix(prefix);
    fields.emplace_back(keyPrefix + "ModelHash", config.modelHash);
    fields.emplace_back(keyPrefix + "Scale", formatNetFloat(config.scale));
    fields.emplace_back(keyPrefix + "SkinHash", config.skinHash);
    fields.emplace_back(keyPrefix + "Asset", config.assetKey);
    fields.emplace_back(keyPrefix + "Fmt", config.modelFormat);
    fields.emplace_back(keyPrefix + "FwdYaw", formatNetFloat(config.forwardAxisYawDegrees));
    fields.emplace_back(keyPrefix + "ImpW", formatNetFloat(config.importRotation.w));
    fields.emplace_back(keyPrefix + "ImpX", formatNetFloat(config.importRotation.x));
    fields.emplace_back(keyPrefix + "ImpY", formatNetFloat(config.importRotation.y));
    fields.emplace_back(keyPrefix + "ImpZ", formatNetFloat(config.importRotation.z));
    fields.emplace_back(keyPrefix + "Rig", config.builtinWalkingRig ? "1" : "0");
    fields.emplace_back(keyPrefix + "Yaw", formatNetFloat(config.yawDegrees));
    fields.emplace_back(keyPrefix + "Pitch", formatNetFloat(config.pitchDegrees));
    fields.emplace_back(keyPrefix + "Roll", formatNetFloat(config.rollDegrees));
    fields.emplace_back(keyPrefix + "OffX", formatNetFloat(config.offset.x));
    fields.emplace_back(keyPrefix + "OffY", formatNetFloat(config.offset.y));
    fields.emplace_back(keyPrefix + "OffZ", formatNetFloat(config.offset.z));
}

inline AvatarRoleConfig parseAvatarRoleConfigFields(
    const std::unordered_map<std::string, std::string>& kv,
    std::string_view prefix,
    const AvatarRoleConfig& fallback)
{
    const std::string keyPrefix(prefix);
    const auto readString = [&](std::string_view suffix, const std::string& current) -> std::string {
        const auto it = kv.find(keyPrefix + std::string(suffix));
        return (it == kv.end() || it->second.empty()) ? current : it->second;
    };
    const auto readFloat = [&](std::string_view suffix, float current) -> float {
        const auto it = kv.find(keyPrefix + std::string(suffix));
        return it == kv.end() ? current : std::strtof(it->second.c_str(), nullptr);
    };

    AvatarRoleConfig config = fallback;
    config.modelHash = readString("ModelHash", fallback.modelHash);
    config.scale = std::max(0.1f, readFloat("Scale", fallback.scale));
    config.skinHash = readString("SkinHash", fallback.skinHash);
    config.assetKey = readString("Asset", fallback.assetKey);
    config.modelFormat = readString("Fmt", fallback.modelFormat);
    config.forwardAxisYawDegrees = readFloat("FwdYaw", fallback.forwardAxisYawDegrees);
    config.importRotation = quatNormalize({
        readFloat("ImpW", fallback.importRotation.w),
        readFloat("ImpX", fallback.importRotation.x),
        readFloat("ImpY", fallback.importRotation.y),
        readFloat("ImpZ", fallback.importRotation.z)
    });
    config.builtinWalkingRig = readString("Rig", fallback.builtinWalkingRig ? "1" : "0") == "1";
    config.yawDegrees = readFloat("Yaw", fallback.yawDegrees);
    config.pitchDegrees = readFloat("Pitch", fallback.pitchDegrees);
    config.rollDegrees = readFloat("Roll", fallback.rollDegrees);
    config.offset = {
        readFloat("OffX", fallback.offset.x),
        readFloat("OffY", fallback.offset.y),
        readFloat("OffZ", fallback.offset.z)
    };
    return config;
}

inline void appendRemoteSnapshot(RemotePeerState& peer, const RemoteSnapshotSample& sample)
{
    if (!peer.snapshots.empty()) {
        const double lastTimestamp = peer.snapshots.back().timestamp;
        if (std::fabs(sample.timestamp - lastTimestamp) <= 1.0e-6) {
            peer.snapshots.back() = sample;
            return;
        }
        if (sample.timestamp < lastTimestamp) {
            auto it = peer.snapshots.begin();
            for (; it != peer.snapshots.end(); ++it) {
                if (sample.timestamp < it->timestamp) {
                    break;
                }
            }
            peer.snapshots.insert(it, sample);
        } else {
            peer.snapshots.push_back(sample);
        }
    } else {
        peer.snapshots.push_back(sample);
    }

    while (peer.snapshots.size() > kRemoteSnapshotBufferLimit) {
        peer.snapshots.pop_front();
    }
}

inline Quat nlerpQuat(const Quat& from, Quat to, float alpha)
{
    const float quatDot = (from.w * to.w) + (from.x * to.x) + (from.y * to.y) + (from.z * to.z);
    if (quatDot < 0.0f) {
        to = { -to.w, -to.x, -to.y, -to.z };
    }
    return quatNormalize({
        mix(from.w, to.w, alpha),
        mix(from.x, to.x, alpha),
        mix(from.y, to.y, alpha),
        mix(from.z, to.z, alpha)
    });
}

inline Vec3 hermiteVec3(const Vec3& p0, const Vec3& v0, const Vec3& p1, const Vec3& v1, float dt, float alpha)
{
    const float t2 = alpha * alpha;
    const float t3 = t2 * alpha;
    const float h00 = (2.0f * t3) - (3.0f * t2) + 1.0f;
    const float h10 = t3 - (2.0f * t2) + alpha;
    const float h01 = (-2.0f * t3) + (3.0f * t2);
    const float h11 = t3 - t2;
    return {
        (h00 * p0.x) + (h10 * dt * v0.x) + (h01 * p1.x) + (h11 * dt * v1.x),
        (h00 * p0.y) + (h10 * dt * v0.y) + (h01 * p1.y) + (h11 * dt * v1.y),
        (h00 * p0.z) + (h10 * dt * v0.z) + (h01 * p1.z) + (h11 * dt * v1.z)
    };
}

inline Quat integrateQuaternion(const Quat& rotation, const Vec3& angularVelocity, float dt)
{
    const Quat omega { 0.0f, angularVelocity.x, angularVelocity.y, angularVelocity.z };
    const Quat derivative = quatMultiply(rotation, omega);
    return quatNormalize({
        rotation.w + (0.5f * derivative.w * dt),
        rotation.x + (0.5f * derivative.x * dt),
        rotation.y + (0.5f * derivative.y * dt),
        rotation.z + (0.5f * derivative.z * dt)
    });
}

inline bool sampleRemotePeer(RemotePeerState& peer, double nowSeconds, double interpolationDelaySeconds = kRemoteInterpolationDelaySeconds)
{
    if (peer.snapshots.empty()) {
        return false;
    }

    const double sampleTime = nowSeconds - interpolationDelaySeconds;
    while (peer.snapshots.size() >= 3u && sampleTime >= peer.snapshots[1].timestamp) {
        peer.snapshots.pop_front();
    }

    if (peer.snapshots.size() == 1u || sampleTime <= peer.snapshots.front().timestamp) {
        const RemoteSnapshotSample& sample = peer.snapshots.front();
        peer.basePos = sample.pos;
        peer.baseRot = sample.rot;
        peer.displayPos = sample.pos;
        peer.displayRot = sample.rot;
        peer.vel = sample.vel;
        peer.angVel = sample.angVel;
        return true;
    }

    for (std::size_t index = 0; index + 1u < peer.snapshots.size(); ++index) {
        const RemoteSnapshotSample& a = peer.snapshots[index];
        const RemoteSnapshotSample& b = peer.snapshots[index + 1u];
        if (sampleTime < a.timestamp || sampleTime > b.timestamp) {
            continue;
        }
        const float dt = static_cast<float>(std::max(1.0e-6, b.timestamp - a.timestamp));
        const float alpha = clamp(
            static_cast<float>((sampleTime - a.timestamp) / std::max(1.0e-6, b.timestamp - a.timestamp)),
            0.0f,
            1.0f);
        peer.basePos = hermiteVec3(a.pos, a.vel, b.pos, b.vel, dt, alpha);
        peer.baseRot = nlerpQuat(a.rot, b.rot, alpha);
        peer.displayPos = peer.basePos;
        peer.displayRot = peer.baseRot;
        peer.vel = lerp(a.vel, b.vel, alpha);
        peer.angVel = lerp(a.angVel, b.angVel, alpha);
        return true;
    }

    const RemoteSnapshotSample& last = peer.snapshots.back();
    const float dt = clamp(static_cast<float>(sampleTime - last.timestamp), 0.0f, kRemoteExtrapolationCapSeconds);
    peer.basePos = last.pos + (last.vel * dt);
    peer.baseRot = integrateQuaternion(last.rot, last.angVel, dt);
    peer.displayPos = peer.basePos;
    peer.displayRot = peer.baseRot;
    peer.vel = last.vel;
    peer.angVel = last.angVel;
    return true;
}

inline std::string buildJoinOkPacket(int id)
{
    return encodePacket("JOIN_OK", { { "id", std::to_string(id) } });
}

inline std::string buildPeerLeavePacket(int id)
{
    return encodePacket("PEER_LEAVE", { { "id", std::to_string(id) } });
}

inline std::string buildHelloPacket(const AvatarManifest& avatar)
{
    WorldKeyValueFields fields {
        { "role", sanitizeRole(avatar.role) },
        { "callsign", sanitizeCallsign(avatar.callsign) },
        { "radio", std::to_string(normalizeRadioChannel(avatar.radioChannel)) },
        { "ptt", avatar.radioTx ? "1" : "0" }
    };
    appendAvatarRoleConfigFields(fields, "plane", avatar.plane);
    appendAvatarRoleConfigFields(fields, "walking", avatar.walking);
    return encodePacket("HELLO", fields);
}

inline std::string buildAvatarManifestPacket(int id, const AvatarManifest& avatar)
{
    WorldKeyValueFields fields {
        { "id", std::to_string(id) },
        { "role", sanitizeRole(avatar.role) },
        { "callsign", sanitizeCallsign(avatar.callsign) },
        { "radio", std::to_string(normalizeRadioChannel(avatar.radioChannel)) },
        { "ptt", avatar.radioTx ? "1" : "0" }
    };
    appendAvatarRoleConfigFields(fields, "plane", avatar.plane);
    appendAvatarRoleConfigFields(fields, "walking", avatar.walking);
    return encodePacket("AVATAR_MANIFEST", fields);
}

inline std::string buildVoiceStatePacket(const VoiceStatePacket& state)
{
    return encodePacket("VOICE_STATE", {
        { "id", std::to_string(state.id) },
        { "channel", std::to_string(normalizeRadioChannel(state.channel)) },
        { "tx", state.transmitting ? "1" : "0" }
    });
}

inline std::string buildVoiceFramePacket(const VoiceFramePacket& frame)
{
    const std::string& payload = frame.compressedData.empty() ? frame.data : frame.compressedData;
    return encodePacket("VOICE_FRAME", {
        { "id", std::to_string(std::max(0, frame.senderId)) },
        { "channel", std::to_string(normalizeRadioChannel(frame.channel)) },
        { "data", NetProtocolDetail::encodeBase64(payload) }
    });
}

inline std::string buildSnapshotPacket(const NetPlayerSnapshot& snapshot)
{
    WorldKeyValueFields fields {
        { "id", std::to_string(snapshot.id) },
        { "ack", std::to_string(snapshot.ack) },
        { "px", formatNetFloat(snapshot.pos.x) },
        { "py", formatNetFloat(snapshot.pos.y) },
        { "pz", formatNetFloat(snapshot.pos.z) },
        { "rw", formatNetFloat(snapshot.rot.w) },
        { "rx", formatNetFloat(snapshot.rot.x) },
        { "ry", formatNetFloat(snapshot.rot.y) },
        { "rz", formatNetFloat(snapshot.rot.z) },
        { "vx", formatNetFloat(snapshot.vel.x) },
        { "vy", formatNetFloat(snapshot.vel.y) },
        { "vz", formatNetFloat(snapshot.vel.z) },
        { "wx", formatNetFloat(snapshot.angVel.x) },
        { "wy", formatNetFloat(snapshot.angVel.y) },
        { "wz", formatNetFloat(snapshot.angVel.z) },
        { "thr", formatNetFloat(snapshot.throttle) },
        { "elev", formatNetFloat(snapshot.elevator) },
        { "ail", formatNetFloat(snapshot.aileron) },
        { "rud", formatNetFloat(snapshot.rudder) },
        { "tick", std::to_string(snapshot.simTick) },
        { "vehicleId", std::to_string(snapshot.vehicleId) },
        { "ts", formatNetFloat(snapshot.timestamp) },
        { "role", sanitizeRole(snapshot.avatar.role) },
        { "callsign", sanitizeCallsign(snapshot.avatar.callsign) },
        { "radio", std::to_string(normalizeRadioChannel(snapshot.avatar.radioChannel)) },
        { "ptt", snapshot.avatar.radioTx ? "1" : "0" }
    };
    appendAvatarRoleConfigFields(fields, "plane", snapshot.avatar.plane);
    appendAvatarRoleConfigFields(fields, "walking", snapshot.avatar.walking);
    return encodePacket("SNAPSHOT", fields);
}

inline std::string buildInputPacket(const NetPlayerInput& input)
{
    WorldKeyValueFields fields {
        { "tick", std::to_string(input.tick) },
        { "role", sanitizeRole(input.role) },
        { "frameDt", formatNetFloat(input.frameDt) },
        { "px", formatNetFloat(input.reportedPos.x) },
        { "py", formatNetFloat(input.reportedPos.y) },
        { "pz", formatNetFloat(input.reportedPos.z) },
        { "rw", formatNetFloat(input.reportedRot.w) },
        { "rx", formatNetFloat(input.reportedRot.x) },
        { "ry", formatNetFloat(input.reportedRot.y) },
        { "rz", formatNetFloat(input.reportedRot.z) },
        { "vx", formatNetFloat(input.reportedVel.x) },
        { "vy", formatNetFloat(input.reportedVel.y) },
        { "vz", formatNetFloat(input.reportedVel.z) },
        { "wx", formatNetFloat(input.reportedAngVel.x) },
        { "wy", formatNetFloat(input.reportedAngVel.y) },
        { "wz", formatNetFloat(input.reportedAngVel.z) },
        { "simTick", std::to_string(std::max(0, input.reportedSimTick)) },
        { "callsign", sanitizeCallsign(input.avatar.callsign) },
        { "walkYaw", formatNetFloat(input.walkYaw) },
        { "walkPitch", formatNetFloat(input.walkPitch) },
        { "walkMoveSpeed", formatNetFloat(input.walkMoveSpeed) },
        { "throttle", formatNetFloat(input.throttle) },
        { "yokePitch", formatNetFloat(input.yokePitch) },
        { "yokeYaw", formatNetFloat(input.yokeYaw) },
        { "yokeRoll", formatNetFloat(input.yokeRoll) },
        { "pitchTrim", formatNetFloat(input.manualElevatorTrim) },
        { "rudderTrim", formatNetFloat(input.manualRudderTrim) },
        { "walkForward", input.walkForward ? "1" : "0" },
        { "walkBackward", input.walkBackward ? "1" : "0" },
        { "walkStrafeLeft", input.walkStrafeLeft ? "1" : "0" },
        { "walkStrafeRight", input.walkStrafeRight ? "1" : "0" },
        { "walkSprint", input.walkSprint ? "1" : "0" },
        { "walkJump", input.walkJump ? "1" : "0" },
        { "flightThrottleUp", input.flightThrottleUp ? "1" : "0" },
        { "flightThrottleDown", input.flightThrottleDown ? "1" : "0" },
        { "flightAirBrakes", input.flightAirBrakes ? "1" : "0" },
        { "flightAfterburner", input.flightAfterburner ? "1" : "0" },
        { "firePrimary", input.firePrimary ? "1" : "0" },
        { "dropBomb", input.dropBomb ? "1" : "0" },
        { "terrainGunAdd", input.terrainGunAdd ? "1" : "0" },
        { "terrainGunRemove", input.terrainGunRemove ? "1" : "0" },
        { "terrainEditOp", std::to_string(input.terrainEditOpId) },
        { "vehicleId", std::to_string(input.vehicleId) },
        { "driveThrottle", formatNetFloat(input.driveThrottle) },
        { "driveSteer", formatNetFloat(input.driveSteer) },
        { "driveBrake", input.driveBrake ? "1" : "0" },
        { "terraformMode", input.terraformMode ? "1" : "0" },
        { "radio", std::to_string(normalizeRadioChannel(input.avatar.radioChannel)) },
        { "ptt", input.avatar.radioTx ? "1" : "0" }
    };
    appendAvatarRoleConfigFields(fields, "plane", input.avatar.plane);
    appendAvatarRoleConfigFields(fields, "walking", input.avatar.walking);
    return encodePacket("INPUT", fields);
}

inline std::string buildGameplayStatePacket(const NetGameplayStatePacket& packet)
{
    std::ostringstream playerStream;
    for (std::size_t index = 0; index < packet.players.size(); ++index) {
        const NetGameplayPlayerState& player = packet.players[index];
        if (index > 0u) {
            playerStream << ';';
        }
        playerStream
            << player.id << ','
            << formatNetFloat(player.hullStrength) << ','
            << formatNetFloat(player.fuselageStrength) << ','
            << formatNetFloat(player.wear) << ','
            << (player.terraformMode ? '1' : '0') << ','
            << player.targetsDestroyed;
    }

    std::ostringstream objectStream;
    for (std::size_t index = 0; index < packet.objects.size(); ++index) {
        const NetGameplayObjectState& object = packet.objects[index];
        if (index > 0u) {
            objectStream << ';';
        }
        objectStream
            << object.kind << ','
            << object.id << ','
            << object.ownerId << ','
            << (object.active ? '1' : '0') << ','
            << formatNetFloat(object.pos.x) << ','
            << formatNetFloat(object.pos.y) << ','
            << formatNetFloat(object.pos.z) << ','
            << formatNetFloat(object.vel.x) << ','
            << formatNetFloat(object.vel.y) << ','
            << formatNetFloat(object.vel.z) << ','
            << formatNetFloat(object.radius) << ','
            << formatNetFloat(object.ttl) << ','
            << formatNetFloat(object.health) << ','
            << formatNetFloat(object.maxHealth);
    }

    return encodePacket("GAMEPLAY_STATE", {
        { "ts", formatNetFloat(packet.timestamp) },
        { "players", playerStream.str() },
        { "objects", objectStream.str() }
    });
}

inline std::string buildModeSwitchPacket(const ModeSwitchPacket& packet)
{
    return encodePacket("MODE_SWITCH", {
        { "tick", std::to_string(std::max(0, packet.tick)) },
        { "role", sanitizeRole(packet.role) },
        { "x", formatNetFloat(packet.x) },
        { "y", formatNetFloat(packet.y) },
        { "z", formatNetFloat(packet.z) },
        { "vehicleId", std::to_string(packet.vehicleId) },
        { "walkYaw", formatNetFloat(packet.walkYaw) },
        { "walkPitch", formatNetFloat(packet.walkPitch) }
    });
}

inline std::string buildConstructPublishPacket(const ConstructPublishPacket& packet)
{
    const ConstructBlueprint& blueprint = packet.blueprint;
    const ConstructState& state = packet.state;
    return encodePacket("CONSTRUCT_PUBLISH", {
        { "construct", std::to_string(blueprint.constructId) },
        { "revision", std::to_string(blueprint.revisionId) },
        { "author", std::to_string(state.authorPlayerId) },
        { "published", state.published ? "1" : "0" },
        { "label", state.label.empty() ? blueprint.label : state.label },
        { "halfExtents", encodeNetVec3(blueprint.bodyHalfExtents) },
        { "seatOffsets", encodeVec3List(blueprint.seatOffsets) },
        { "maxSpeed", formatNetFloat(blueprint.maxSpeedMps) },
        { "accel", formatNetFloat(blueprint.accelerationMps2) },
        { "brake", formatNetFloat(blueprint.brakingMps2) },
        { "steerRate", formatNetFloat(blueprint.steeringRate) },
        { "steerAssist", formatNetFloat(blueprint.steeringAssist) },
        { "roadGrip", formatNetFloat(blueprint.roadGrip) },
        { "offroadGrip", formatNetFloat(blueprint.offroadGrip) },
        { "maxHealth", formatNetFloat(blueprint.maxHealth) },
        { "suspension", formatNetFloat(blueprint.suspensionTravel) }
    });
}

inline std::string buildVehicleStatePacket(const SharedVehicleState& state)
{
    return encodePacket("VEHICLE_STATE", {
        { "vehicle", std::to_string(state.vehicleId) },
        { "entity", std::to_string(state.entityId) },
        { "construct", std::to_string(state.constructId) },
        { "revision", std::to_string(state.constructRevisionId) },
        { "owner", std::to_string(state.authoritativeOwnerPlayerId) },
        { "driver", std::to_string(state.driverPlayerId) },
        { "seats", encodeVehicleSeatOccupants(state.seatOccupants) },
        { "px", formatNetFloat(state.pos.x) },
        { "py", formatNetFloat(state.pos.y) },
        { "pz", formatNetFloat(state.pos.z) },
        { "vx", formatNetFloat(state.vel.x) },
        { "vy", formatNetFloat(state.vel.y) },
        { "vz", formatNetFloat(state.vel.z) },
        { "yaw", formatNetFloat(state.yawRadians) },
        { "steer", formatNetFloat(state.steerNormalized) },
        { "throttle", formatNetFloat(state.throttleNormalized) },
        { "adhesion", formatNetFloat(state.roadAdhesion) },
        { "friction", formatNetFloat(state.surfaceFriction) },
        { "health", formatNetFloat(state.health) },
        { "maxHealth", formatNetFloat(state.maxHealth) },
        { "heave", formatNetFloat(state.cage.heave) },
        { "pitch", formatNetFloat(state.cage.pitch) },
        { "roll", formatNetFloat(state.cage.roll) },
        { "wheelCompression", encodeWheelCompression(state.cage.wheelCompression) },
        { "active", state.active ? "1" : "0" }
    });
}

inline std::string buildVehicleSpawnPacket(const VehicleSpawnPacket& packet)
{
    WorldKeyValueFields fields {
        { "reason", packet.reason }
    };
    const std::vector<std::string> vehicleFields = splitPacketFields(buildVehicleStatePacket(packet.state));
    for (std::size_t index = 1; index < vehicleFields.size(); ++index) {
        const std::size_t separator = vehicleFields[index].find('=');
        if (separator == std::string::npos) {
            continue;
        }
        fields.emplace_back(
            vehicleFields[index].substr(0, separator),
            vehicleFields[index].substr(separator + 1u));
    }
    return encodePacket("VEHICLE_SPAWN", fields);
}

inline std::string buildVehicleSeatPacket(const VehicleSeatPacket& packet)
{
    return encodePacket("VEHICLE_SEAT", {
        { "vehicle", std::to_string(packet.vehicleId) },
        { "player", std::to_string(packet.playerId) },
        { "seat", std::to_string(packet.seatIndex) },
        { "enter", packet.entering ? "1" : "0" }
    });
}

inline std::string buildResetFlightPacket(const ResetFlightPacket& packet)
{
    return encodePacket("RESET_FLIGHT", {
        { "tick", std::to_string(std::max(0, packet.tick)) },
        { "x", formatNetFloat(packet.x) },
        { "z", formatNetFloat(packet.z) }
    });
}

inline WorldInfoSnapshot buildWorldInfoSnapshotPacketData(const WorldStore& world, const TerrainParams& fallbackTerrain)
{
    const WorldMeta meta = world.getMeta();
    WorldInfoSnapshot info;
    info.worldId = meta.worldId;
    info.formatVersion = meta.formatVersion;
    info.seed = meta.seed;
    info.worldShape = meta.terrainProfile.worldShape;
    info.planet = meta.terrainProfile.planet;
    info.chunkSize = meta.terrainProfile.chunkSize;
    info.horizonRadiusMeters = std::max(meta.terrainProfile.worldRadius, fallbackTerrain.horizonRadiusMeters);
    info.heightAmplitude = meta.terrainProfile.heightAmplitude;
    info.heightFrequency = meta.terrainProfile.heightFrequency;
    info.waterLevel = meta.terrainProfile.waterLevel;
    info.tunnelSeeds = meta.tunnelSeeds;
    info.spawnX = meta.spawn.x;
    info.spawnY = meta.spawn.y;
    info.spawnZ = meta.spawn.z;
    info.spawnLatitudeDeg = meta.spawnGeodetic.latitudeDeg;
    info.spawnLongitudeDeg = meta.spawnGeodetic.longitudeDeg;
    info.spawnAltitudeMeters = meta.spawnGeodetic.altitudeMeters;
    return info;
}

inline std::string buildWorldInfoPacket(const WorldInfoSnapshot& info)
{
    return encodePacket("WORLD_INFO", {
        { "worldId", info.worldId },
        { "formatVersion", std::to_string(info.formatVersion) },
        { "seed", std::to_string(info.seed) },
        { "worldShape", std::to_string(static_cast<int>(info.worldShape)) },
        { "planetRadius", formatNetFloat(static_cast<float>(info.planet.radiusMeters), 3) },
        { "planetMu", formatNetFloat(static_cast<float>(info.planet.gravitationalParameter), 3) },
        { "planetRotationRate", formatNetFloat(static_cast<float>(info.planet.rotationRateRadPerSec), 8) },
        { "planetAtmosphereHeight", formatNetFloat(static_cast<float>(info.planet.atmosphereHeightMeters), 3) },
        { "planetOriginLat", formatNetFloat(static_cast<float>(info.planet.localOrigin.latitudeDeg), 6) },
        { "planetOriginLon", formatNetFloat(static_cast<float>(info.planet.localOrigin.longitudeDeg), 6) },
        { "planetOriginAlt", formatNetFloat(static_cast<float>(info.planet.localOrigin.altitudeMeters), 3) },
        { "chunkSize", formatNetFloat(info.chunkSize, 6) },
        { "horizonRadius", formatNetFloat(info.horizonRadiusMeters, 6) },
        { "heightAmp", formatNetFloat(info.heightAmplitude, 6) },
        { "heightFreq", formatNetFloat(info.heightFrequency, 6) },
        { "waterLevel", formatNetFloat(info.waterLevel, 6) },
        { "tunnelSeeds", encodeTunnelSeeds(info.tunnelSeeds) },
        { "spawnX", formatNetFloat(info.spawnX, 6) },
        { "spawnY", formatNetFloat(info.spawnY, 6) },
        { "spawnZ", formatNetFloat(info.spawnZ, 6) },
        { "spawnLat", formatNetFloat(static_cast<float>(info.spawnLatitudeDeg), 6) },
        { "spawnLon", formatNetFloat(static_cast<float>(info.spawnLongitudeDeg), 6) },
        { "spawnAlt", formatNetFloat(static_cast<float>(info.spawnAltitudeMeters), 3) }
    });
}

inline std::string encodeNetVec3(const Vec3& value)
{
    return formatNetFloat(value.x, 6) + "," + formatNetFloat(value.y, 6) + "," + formatNetFloat(value.z, 6);
}

inline Vec3 decodeNetVec3(const std::string& value, const Vec3& fallback = {})
{
    const std::vector<std::string> parts = splitWorldWireToken(value, ',');
    if (parts.size() != 3u) {
        return fallback;
    }
    return {
        parseWorldWireFloat(parts[0], fallback.x),
        parseWorldWireFloat(parts[1], fallback.y),
        parseWorldWireFloat(parts[2], fallback.z)
    };
}

inline std::string encodeRoadPaths(const std::vector<TerrainRoadPath>& paths)
{
    if (paths.empty()) {
        return {};
    }

    std::ostringstream stream;
    bool wroteAny = false;
    for (const TerrainRoadPath& path : paths) {
        if (path.nodes.empty()) {
            continue;
        }
        if (wroteAny) {
            stream << ';';
        }
        stream << (path.loop ? '1' : '0') << ':';
        for (std::size_t nodeIndex = 0; nodeIndex < path.nodes.size(); ++nodeIndex) {
            if (nodeIndex > 0u) {
                stream << '~';
            }
            const TerrainRoadNode& node = path.nodes[nodeIndex];
            stream << formatWorldWireFloat(node.position.x)
                   << ',' << formatWorldWireFloat(node.position.y)
                   << ',' << formatWorldWireFloat(node.position.z)
                   << ',' << formatWorldWireFloat(node.forward.x)
                   << ',' << formatWorldWireFloat(node.forward.y)
                   << ',' << formatWorldWireFloat(node.forward.z)
                   << ',' << formatWorldWireFloat(node.widthMeters)
                   << ',' << formatWorldWireFloat(node.cutWidthMeters)
                   << ',' << formatWorldWireFloat(node.grade)
                   << ',' << formatWorldWireFloat(node.curvature);
        }
        wroteAny = true;
    }
    return stream.str();
}

inline std::vector<TerrainRoadPath> decodeRoadPaths(const std::string& value)
{
    std::vector<TerrainRoadPath> out;
    for (const std::string& entry : splitWorldWireToken(value, ';')) {
        const std::size_t split = entry.find(':');
        if (split == std::string::npos) {
            continue;
        }

        TerrainRoadPath path;
        path.loop = parseWorldWireInt(entry.substr(0, split), 0) != 0;
        for (const std::string& nodeToken : splitWorldWireToken(entry.substr(split + 1u), '~')) {
            const std::vector<std::string> parts = splitWorldWireToken(nodeToken, ',');
            if (parts.size() != 10u) {
                continue;
            }
            TerrainRoadNode node;
            node.position = {
                parseWorldWireFloat(parts[0], 0.0f),
                parseWorldWireFloat(parts[1], 0.0f),
                parseWorldWireFloat(parts[2], 0.0f)
            };
            node.forward = {
                parseWorldWireFloat(parts[3], 1.0f),
                parseWorldWireFloat(parts[4], 0.0f),
                parseWorldWireFloat(parts[5], 0.0f)
            };
            node.widthMeters = std::max(0.1f, parseWorldWireFloat(parts[6], 9.8f));
            node.cutWidthMeters = std::max(node.widthMeters, parseWorldWireFloat(parts[7], node.widthMeters));
            node.grade = parseWorldWireFloat(parts[8], 0.0f);
            node.curvature = parseWorldWireFloat(parts[9], 0.0f);
            path.nodes.push_back(node);
        }
        if (!path.nodes.empty()) {
            out.push_back(std::move(path));
        }
    }
    return out;
}

inline std::string buildWorldSyncPacket(const TerrainParams& params)
{
    const TerrainParams normalized = normalizeTerrainParams(params);
    std::ostringstream craterStream;
    for (std::size_t index = 0; index < normalized.dynamicCraters.size(); ++index) {
        const TerrainCrater& crater = normalized.dynamicCraters[index];
        if (index > 0u) {
            craterStream << ';';
        }
        craterStream
            << formatNetFloat(crater.x) << ','
            << formatNetFloat(crater.y) << ','
            << formatNetFloat(crater.z) << ','
            << formatNetFloat(crater.radius) << ','
            << formatNetFloat(crater.depth) << ','
            << formatNetFloat(crater.rim);
    }

    WorldKeyValueFields fields;
    fields.reserve(96u);

    const auto addBool = [&fields](const char* key, bool value) {
        fields.emplace_back(key, value ? "1" : "0");
    };
    const auto addInt = [&fields](const char* key, int value) {
        fields.emplace_back(key, std::to_string(value));
    };
    const auto addFloat = [&fields](const char* key, float value) {
        fields.emplace_back(key, formatNetFloat(value, 6));
    };
    const auto addVec3 = [&fields](const char* key, const Vec3& value) {
        fields.emplace_back(key, encodeNetVec3(value));
    };

    addInt("seed", normalized.seed);
    addFloat("chunk", normalized.chunkSize);
    addFloat("worldRadius", normalized.worldRadius);
    addFloat("minY", normalized.minY);
    addFloat("maxY", normalized.maxY);
    addInt("lod0", normalized.lod0Radius);
    addInt("lod1", normalized.lod1Radius);
    addInt("lod2", normalized.lod2Radius);
    addFloat("quality", normalized.terrainQuality);
    addBool("autoQuality", normalized.autoQualityEnabled);
    addFloat("targetFrameMs", normalized.targetFrameMs);
    addInt("lod0ChunkScale", normalized.lod0ChunkScale);
    addInt("lod1ChunkScale", normalized.lod1ChunkScale);
    addInt("lod2ChunkScale", normalized.lod2ChunkScale);
    addBool("textureTiles", normalized.textureTilesEnabled);
    addInt("lod0TexRes", normalized.lod0TextureResolution);
    addInt("lod1TexRes", normalized.lod1TextureResolution);
    addInt("lod2TexRes", normalized.lod2TextureResolution);
    addFloat("gameplayRadius", normalized.gameplayRadiusMeters);
    addFloat("midRadius", normalized.midFieldRadiusMeters);
    addFloat("horizonRadius", normalized.horizonRadiusMeters);
    addFloat("lod0Cell", normalized.lod0BaseCellSize);
    addFloat("lod1Cell", normalized.lod1BaseCellSize);
    addFloat("lod2Cell", normalized.lod2BaseCellSize);
    addInt("meshBudget", normalized.meshBuildBudget);
    addInt("inflight", normalized.workerMaxInflight);
    addInt("resultBudget", normalized.workerResultBudgetPerFrame);
    addFloat("resultBudgetMs", normalized.workerResultTimeBudgetMs);
    addInt("maxAdaptiveLod1", normalized.maxAdaptiveLod1Radius);
    addInt("maxPendingChunks", normalized.maxPendingChunks);
    addInt("maxStaleChunks", normalized.maxStaleChunks);
    addInt("maxDisplayedChunks", normalized.maxDisplayedChunks);
    addInt("maxDisplayedChunksHardCap", normalized.maxDisplayedChunksHardCap);
    addBool("drawDistanceOverridesLod", normalized.drawDistanceOverridesLodRadius);
    addBool("splitLod", normalized.splitLodEnabled);
    addFloat("splitRatio", normalized.highResSplitRatio);
    addInt("cache", normalized.chunkCacheLimit);
    addBool("farLodCone", normalized.farLodConeEnabled);
    addFloat("farLodConeDegrees", normalized.farLodConeDegrees);
    addInt("rearLod2Radius", normalized.rearLod2Radius);
    addFloat("baseHeight", normalized.baseHeight);
    addFloat("heightAmp", normalized.heightAmplitude);
    addFloat("heightFreq", normalized.heightFrequency);
    addInt("heightOctaves", normalized.heightOctaves);
    addFloat("heightLacunarity", normalized.heightLacunarity);
    addFloat("heightGain", normalized.heightGain);
    addFloat("surfaceDetailAmp", normalized.surfaceDetailAmplitude);
    addFloat("surfaceDetailFreq", normalized.surfaceDetailFrequency);
    addFloat("ridgeAmp", normalized.ridgeAmplitude);
    addFloat("ridgeFreq", normalized.ridgeFrequency);
    addFloat("ridgeSharpness", normalized.ridgeSharpness);
    addFloat("macroWarpAmp", normalized.macroWarpAmplitude);
    addFloat("macroWarpFreq", normalized.macroWarpFrequency);
    addFloat("terraceStrength", normalized.terraceStrength);
    addFloat("terraceStep", normalized.terraceStep);
    addFloat("waterLevel", normalized.waterLevel);
    addFloat("shorelineBand", normalized.shorelineBand);
    addFloat("waterWaveAmp", normalized.waterWaveAmplitude);
    addFloat("waterWaveFreq", normalized.waterWaveFrequency);
    addFloat("biomeFreq", normalized.biomeFrequency);
    addFloat("snowLine", normalized.snowLine);
    addBool("caveEnabled", normalized.caveEnabled);
    addFloat("caveFreq", normalized.caveFrequency);
    addFloat("caveThreshold", normalized.caveThreshold);
    addFloat("caveStrength", normalized.caveStrength);
    addInt("caveOctaves", normalized.caveOctaves);
    addFloat("caveLacunarity", normalized.caveLacunarity);
    addFloat("caveGain", normalized.caveGain);
    addFloat("caveMinY", normalized.caveMinY);
    addFloat("caveMaxY", normalized.caveMaxY);
    addInt("tunnelCount", normalized.tunnelCount);
    addFloat("tunnelRadiusMin", normalized.tunnelRadiusMin);
    addFloat("tunnelRadiusMax", normalized.tunnelRadiusMax);
    addFloat("tunnelLengthMin", normalized.tunnelLengthMin);
    addFloat("tunnelLengthMax", normalized.tunnelLengthMax);
    addFloat("tunnelSegmentLength", normalized.tunnelSegmentLength);
    addInt("generatorVersion", normalized.generatorVersion);
    addBool("surfaceOnlyMeshing", normalized.surfaceOnlyMeshing);
    addBool("threadedMeshing", normalized.threadedMeshing);
    addBool("enableSkirts", normalized.enableSkirts);
    addFloat("skirtDepth", normalized.skirtDepth);
    addInt("maxChunkCellsPerAxis", normalized.maxChunkCellsPerAxis);
    addInt("craterHistoryLimit", normalized.craterHistoryLimit);
    addFloat("waterRatio", normalized.waterRatio);
    addBool("decorationEnabled", normalized.decoration.enabled);
    addFloat("decorationDensity", normalized.decoration.density);
    addFloat("decorationNearDensity", normalized.decoration.nearDensityScale);
    addFloat("decorationMidDensity", normalized.decoration.midDensityScale);
    addFloat("decorationFarDensity", normalized.decoration.farDensityScale);
    addFloat("decorationTreeLineOffset", normalized.decoration.treeLineOffset);
    addFloat("decorationShoreBrushDensity", normalized.decoration.shoreBrushDensity);
    addFloat("decorationRockDensity", normalized.decoration.rockDensity);
    addBool("decorationCollision", normalized.decoration.collisionEnabled);
    addInt("decorationSeedOffset", normalized.decoration.seedOffset);
    addVec3("grassColor", normalized.grassColor);
    addVec3("roadColor", normalized.roadColor);
    addVec3("fieldColor", normalized.fieldColor);
    addVec3("waterColor", normalized.waterColor);
    addVec3("grassVar", normalized.grassVar);
    addVec3("roadVar", normalized.roadVar);
    addVec3("fieldVar", normalized.fieldVar);
    addVec3("waterVar", normalized.waterVar);
    addBool("mountainFrontEnabled", normalized.mountainFront.enabled);
    addFloat("mountainFrontCoastalStart", normalized.mountainFront.coastalBandStart);
    addFloat("mountainFrontCoastalEnd", normalized.mountainFront.coastalBandEnd);
    addFloat("mountainFrontFoothillStart", normalized.mountainFront.foothillStart);
    addFloat("mountainFrontFoothillEnd", normalized.mountainFront.foothillEnd);
    addFloat("mountainFrontInlandFadeStart", normalized.mountainFront.inlandFadeStart);
    addFloat("mountainFrontInlandFadeEnd", normalized.mountainFront.inlandFadeEnd);
    addFloat("mountainFrontWallStrength", normalized.mountainFront.mountainWallStrength);
    addFloat("mountainFrontShelfStrength", normalized.mountainFront.shelfStrength);
    addFloat("mountainFrontCliffStrength", normalized.mountainFront.cliffStrength);
    addFloat("mountainFrontRidgeWarpScale", normalized.mountainFront.ridgeWarpScale);
    addBool("roadsEnabled", normalized.roads.enabled);
    addInt("roadsDesiredCount", normalized.roads.desiredRoadCount);
    addFloat("roadsLaneWidth", normalized.roads.laneWidthMeters);
    addFloat("roadsShoulderWidth", normalized.roads.shoulderWidthMeters);
    addFloat("roadsCutWidth", normalized.roads.cutWidthMeters);
    addFloat("roadsCutBankWidth", normalized.roads.cutBankWidthMeters);
    addFloat("roadsMaxGrade", normalized.roads.maxGrade);
    addFloat("roadsMaxGradeHard", normalized.roads.maxGradeHard);
    addFloat("roadsMinTurnRadius", normalized.roads.minTurnRadiusMeters);
    addFloat("roadsPreferredTurnRadius", normalized.roads.preferredTurnRadiusMeters);
    addFloat("roadsStepMeters", normalized.roads.stepMeters);
    addFloat("roadsSampleLookAhead", normalized.roads.sampleLookAheadMeters);
    addFloat("roadsSampleCone", normalized.roads.sampleConeDegrees);
    addFloat("roadsBranchChance", normalized.roads.branchChance);
    addFloat("roadsFlattenStrength", normalized.roads.flattenStrength);
    addFloat("roadsCutStrength", normalized.roads.cutStrength);
    addFloat("roadsFillStrength", normalized.roads.fillStrength);
    addFloat("roadsEdgeFeather", normalized.roads.edgeFeatherMeters);
    addFloat("roadsPotholeChance", normalized.roads.potholeChance);
    addFloat("roadsPotholeDepth", normalized.roads.potholeDepthMeters);
    addFloat("roadsPatchChance", normalized.roads.patchChance);
    addFloat("roadsPatchRaise", normalized.roads.patchRaiseMeters);
    addFloat("roadsAsphaltNoiseScale", normalized.roads.asphaltNoiseScale);
    addFloat("roadsCrackStrength", normalized.roads.crackStrength);
    fields.emplace_back("craters", craterStream.str());
    fields.emplace_back("tunnelSeeds", encodeTunnelSeeds(normalized.explicitTunnelSeeds));
    fields.emplace_back("roadPaths", encodeRoadPaths(normalized.explicitRoadPaths));

    return encodePacket("WORLD_SYNC", fields);
}

inline std::string buildChunkPacket(std::string_view packetType, const WorldChunkState& chunkState, std::string_view reason)
{
    WorldKeyValueFields fields = buildChunkStateFields(chunkState);
    fields.emplace_back("reason", std::string(reason));
    return encodePacket(packetType, fields);
}

inline std::string buildCraterPacket(const TerrainCrater& crater)
{
    return encodePacket("CRATER_EVENT", {
        { "x", formatNetFloat(crater.x) },
        { "y", formatNetFloat(crater.y) },
        { "z", formatNetFloat(crater.z) },
        { "radius", formatNetFloat(crater.radius) },
        { "depth", formatNetFloat(crater.depth) },
        { "rim", formatNetFloat(crater.rim) }
    });
}

inline std::string buildTerrainEditRequestPacket(const TerrainEditRequest& request)
{
    return encodePacket("TERRAIN_EDIT_REQUEST", {
        { "op", std::to_string(request.opId) },
        { "id", std::to_string(std::max(0, request.playerId)) },
        { "kind", request.kind },
        { "x", formatNetFloat(request.center.x) },
        { "y", formatNetFloat(request.center.y) },
        { "z", formatNetFloat(request.center.z) },
        { "nx", formatNetFloat(request.surfaceNormal.x) },
        { "ny", formatNetFloat(request.surfaceNormal.y) },
        { "nz", formatNetFloat(request.surfaceNormal.z) },
        { "radius", formatNetFloat(request.radius) },
        { "magnitude", formatNetFloat(request.magnitude) },
        { "ts", formatNetFloat(request.requestedAt) }
    });
}

inline std::string buildTerrainEditAckPacket(const TerrainEditAck& ack)
{
    return encodePacket("TERRAIN_EDIT_ACK", {
        { "op", std::to_string(ack.opId) },
        { "id", std::to_string(std::max(0, ack.playerId)) },
        { "ok", ack.accepted ? "1" : "0" },
        { "kind", ack.kind },
        { "x", formatNetFloat(ack.center.x) },
        { "y", formatNetFloat(ack.center.y) },
        { "z", formatNetFloat(ack.center.z) },
        { "radius", formatNetFloat(ack.radius) },
        { "magnitude", formatNetFloat(ack.magnitude) },
        { "chunks", std::to_string(std::max(0, ack.touchedChunks)) },
        { "reason", ack.reason }
    });
}

inline std::string buildAoiSubscribePacket(const AoiSubscription& subscription)
{
    return encodePacket("AOI_SUBSCRIBE", {
        { "cx", std::to_string(subscription.centerChunkX) },
        { "cz", std::to_string(subscription.centerChunkZ) },
        { "radius", std::to_string(std::clamp(subscription.radiusChunks, 2, 96)) },
        { "snapshotNear", formatNetFloat(std::max(128.0f, subscription.snapshotNearMeters)) },
        { "snapshotFar", formatNetFloat(std::max(subscription.snapshotNearMeters, subscription.snapshotFarMeters)) }
    });
}

inline std::string buildBlobRequestPacket(std::string_view kind, std::string_view hash)
{
    return encodePacket("BLOB_REQUEST", {
        { "kind", std::string(kind) },
        { "hash", std::string(hash) }
    });
}

inline double currentNetTimeSeconds(std::uint64_t milliseconds)
{
    return static_cast<double>(milliseconds) * 0.001;
}

inline std::optional<int> parseJoinOkPacket(const std::string& packet)
{
    const std::vector<std::string> fields = splitPacketFields(packet);
    if (fields.empty() || fields[0] != "JOIN_OK") {
        return std::nullopt;
    }
    const auto kv = parseKeyValueFields(fields);
    return std::atoi(kv["id"].c_str());
}

inline std::optional<int> parsePeerLeavePacket(const std::string& packet)
{
    const std::vector<std::string> fields = splitPacketFields(packet);
    if (fields.empty() || fields[0] != "PEER_LEAVE") {
        return std::nullopt;
    }
    const auto kv = parseKeyValueFields(fields);
    return std::atoi(kv["id"].c_str());
}

inline std::optional<WorldInfoSnapshot> parseWorldInfoPacket(const std::string& packet)
{
    const std::vector<std::string> fields = splitPacketFields(packet);
    if (fields.empty() || fields[0] != "WORLD_INFO") {
        return std::nullopt;
    }

    const auto kv = parseKeyValueFields(fields);
    WorldInfoSnapshot info;
    if (const auto it = kv.find("worldId"); it != kv.end()) {
        info.worldId = it->second;
    }
    if (const auto it = kv.find("formatVersion"); it != kv.end()) {
        info.formatVersion = std::max(1, std::atoi(it->second.c_str()));
    }
    if (const auto it = kv.find("seed"); it != kv.end()) {
        info.seed = std::max(1, std::atoi(it->second.c_str()));
    }
    if (const auto it = kv.find("worldShape"); it != kv.end()) {
        info.worldShape = std::atoi(it->second.c_str()) == static_cast<int>(WorldShape::Planet) ? WorldShape::Planet : WorldShape::Plane;
    }
    if (const auto it = kv.find("planetRadius"); it != kv.end()) {
        info.planet.radiusMeters = std::max(1000.0, static_cast<double>(std::strtof(it->second.c_str(), nullptr)));
    }
    if (const auto it = kv.find("planetMu"); it != kv.end()) {
        info.planet.gravitationalParameter = std::max(1.0, static_cast<double>(std::strtof(it->second.c_str(), nullptr)));
    }
    if (const auto it = kv.find("planetRotationRate"); it != kv.end()) {
        info.planet.rotationRateRadPerSec = static_cast<double>(std::strtof(it->second.c_str(), nullptr));
    }
    if (const auto it = kv.find("planetAtmosphereHeight"); it != kv.end()) {
        info.planet.atmosphereHeightMeters = std::max(1000.0, static_cast<double>(std::strtof(it->second.c_str(), nullptr)));
    }
    if (const auto it = kv.find("planetOriginLat"); it != kv.end()) {
        info.planet.localOrigin.latitudeDeg = static_cast<double>(std::strtof(it->second.c_str(), nullptr));
    }
    if (const auto it = kv.find("planetOriginLon"); it != kv.end()) {
        info.planet.localOrigin.longitudeDeg = static_cast<double>(std::strtof(it->second.c_str(), nullptr));
    }
    if (const auto it = kv.find("planetOriginAlt"); it != kv.end()) {
        info.planet.localOrigin.altitudeMeters = static_cast<double>(std::strtof(it->second.c_str(), nullptr));
    }
    if (const auto it = kv.find("chunkSize"); it != kv.end()) {
        info.chunkSize = std::max(8.0f, std::strtof(it->second.c_str(), nullptr));
    }
    if (const auto it = kv.find("horizonRadius"); it != kv.end()) {
        info.horizonRadiusMeters = std::max(info.chunkSize * 4.0f, std::strtof(it->second.c_str(), nullptr));
    }
    if (const auto it = kv.find("heightAmp"); it != kv.end()) {
        info.heightAmplitude = std::strtof(it->second.c_str(), nullptr);
    }
    if (const auto it = kv.find("heightFreq"); it != kv.end()) {
        info.heightFrequency = std::strtof(it->second.c_str(), nullptr);
    }
    if (const auto it = kv.find("waterLevel"); it != kv.end()) {
        info.waterLevel = std::strtof(it->second.c_str(), nullptr);
    }
    if (const auto it = kv.find("tunnelSeeds"); it != kv.end()) {
        info.tunnelSeeds = decodeTunnelSeeds(it->second);
    }
    if (const auto it = kv.find("spawnX"); it != kv.end()) {
        info.spawnX = std::strtof(it->second.c_str(), nullptr);
    }
    if (const auto it = kv.find("spawnY"); it != kv.end()) {
        info.spawnY = std::strtof(it->second.c_str(), nullptr);
    }
    if (const auto it = kv.find("spawnZ"); it != kv.end()) {
        info.spawnZ = std::strtof(it->second.c_str(), nullptr);
    }
    if (const auto it = kv.find("spawnLat"); it != kv.end()) {
        info.spawnLatitudeDeg = static_cast<double>(std::strtof(it->second.c_str(), nullptr));
    }
    if (const auto it = kv.find("spawnLon"); it != kv.end()) {
        info.spawnLongitudeDeg = static_cast<double>(std::strtof(it->second.c_str(), nullptr));
    }
    if (const auto it = kv.find("spawnAlt"); it != kv.end()) {
        info.spawnAltitudeMeters = static_cast<double>(std::strtof(it->second.c_str(), nullptr));
    }
    info.planet.localOrigin = normalizeGeodetic(info.planet.localOrigin);
    return info;
}

inline std::optional<WorldChunkState> parseChunkPacket(const std::string& packet)
{
    const std::vector<std::string> fields = splitPacketFields(packet);
    if (fields.empty() || (fields[0] != "CHUNK_STATE" && fields[0] != "CHUNK_PATCH")) {
        return std::nullopt;
    }
    const auto kv = parseKeyValueFields(fields);
    return decodeChunkStateFields(kv);
}

inline std::optional<NetPlayerSnapshot> parseSnapshotPacket(const std::string& packet)
{
    const std::vector<std::string> fields = splitPacketFields(packet);
    if (fields.empty() || fields[0] != "SNAPSHOT") {
        return std::nullopt;
    }

    const auto kv = parseKeyValueFields(fields);
    NetPlayerSnapshot snapshot;
    snapshot.id = std::atoi(kv["id"].c_str());
    snapshot.ack = std::atoi(kv["ack"].c_str());
    snapshot.pos = {
        std::strtof(kv["px"].c_str(), nullptr),
        std::strtof(kv["py"].c_str(), nullptr),
        std::strtof(kv["pz"].c_str(), nullptr)
    };
    snapshot.rot = {
        std::strtof(kv["rw"].c_str(), nullptr),
        std::strtof(kv["rx"].c_str(), nullptr),
        std::strtof(kv["ry"].c_str(), nullptr),
        std::strtof(kv["rz"].c_str(), nullptr)
    };
    snapshot.vel = {
        std::strtof(kv["vx"].c_str(), nullptr),
        std::strtof(kv["vy"].c_str(), nullptr),
        std::strtof(kv["vz"].c_str(), nullptr)
    };
    snapshot.angVel = {
        std::strtof(kv["wx"].c_str(), nullptr),
        std::strtof(kv["wy"].c_str(), nullptr),
        std::strtof(kv["wz"].c_str(), nullptr)
    };
    snapshot.throttle = std::strtof(kv["thr"].c_str(), nullptr);
    snapshot.elevator = std::strtof(kv["elev"].c_str(), nullptr);
    snapshot.aileron = std::strtof(kv["ail"].c_str(), nullptr);
    snapshot.rudder = std::strtof(kv["rud"].c_str(), nullptr);
    snapshot.simTick = std::atoi(kv["tick"].c_str());
    if (const auto it = kv.find("vehicleId"); it != kv.end()) {
        snapshot.vehicleId = static_cast<VehicleId>(std::strtoull(it->second.c_str(), nullptr, 10));
    }
    snapshot.timestamp = std::strtod(kv["ts"].c_str(), nullptr);
    snapshot.avatar.role = sanitizeRole(kv["role"]);
    snapshot.avatar.callsign = sanitizeCallsign(kv["callsign"]);
    snapshot.avatar.radioChannel = normalizeRadioChannel(std::atoi(kv["radio"].c_str()));
    snapshot.avatar.radioTx = kv["ptt"] == "1";
    snapshot.avatar.plane = parseAvatarRoleConfigFields(kv, "plane", defaultAvatarManifest().plane);
    snapshot.avatar.walking = parseAvatarRoleConfigFields(kv, "walking", defaultAvatarManifest().walking);
    return snapshot;
}

inline std::optional<NetPlayerInput> parseInputPacket(const std::string& packet)
{
    const std::vector<std::string> fields = splitPacketFields(packet);
    if (fields.empty() || fields[0] != "INPUT") {
        return std::nullopt;
    }

    const auto kv = parseKeyValueFields(fields);
    NetPlayerInput input;
    input.tick = std::atoi(kv["tick"].c_str());
    input.role = sanitizeRole(kv["role"]);
    const auto frameDtIt = kv.find("frameDt");
    const float frameDt = frameDtIt == kv.end() ? (1.0f / 60.0f) : std::strtof(frameDtIt->second.c_str(), nullptr);
    input.frameDt = std::clamp(frameDt, 1.0f / 240.0f, 0.05f);
    input.reportedPos = {
        std::strtof(kv["px"].c_str(), nullptr),
        std::strtof(kv["py"].c_str(), nullptr),
        std::strtof(kv["pz"].c_str(), nullptr)
    };
    input.reportedRot = quatNormalize({
        std::strtof(kv["rw"].c_str(), nullptr),
        std::strtof(kv["rx"].c_str(), nullptr),
        std::strtof(kv["ry"].c_str(), nullptr),
        std::strtof(kv["rz"].c_str(), nullptr)
    });
    input.reportedVel = {
        std::strtof(kv["vx"].c_str(), nullptr),
        std::strtof(kv["vy"].c_str(), nullptr),
        std::strtof(kv["vz"].c_str(), nullptr)
    };
    input.reportedAngVel = {
        std::strtof(kv["wx"].c_str(), nullptr),
        std::strtof(kv["wy"].c_str(), nullptr),
        std::strtof(kv["wz"].c_str(), nullptr)
    };
    input.reportedSimTick = std::max(0, std::atoi(kv["simTick"].c_str()));
    input.walkYaw = std::strtof(kv["walkYaw"].c_str(), nullptr);
    input.walkPitch = std::strtof(kv["walkPitch"].c_str(), nullptr);
    input.walkMoveSpeed = std::clamp(std::strtof(kv["walkMoveSpeed"].c_str(), nullptr), 2.0f, 30.0f);
    input.throttle = clampNetFloat(std::strtof(kv["throttle"].c_str(), nullptr), 0.0f, 1.0f);
    input.yokePitch = clampNetFloat(std::strtof(kv["yokePitch"].c_str(), nullptr), -1.0f, 1.0f);
    input.yokeYaw = clampNetFloat(std::strtof(kv["yokeYaw"].c_str(), nullptr), -1.0f, 1.0f);
    input.yokeRoll = clampNetFloat(std::strtof(kv["yokeRoll"].c_str(), nullptr), -1.0f, 1.0f);
    input.manualElevatorTrim = std::strtof(kv["pitchTrim"].c_str(), nullptr);
    input.manualRudderTrim = std::strtof(kv["rudderTrim"].c_str(), nullptr);
    input.walkForward = kv["walkForward"] == "1";
    input.walkBackward = kv["walkBackward"] == "1";
    input.walkStrafeLeft = kv["walkStrafeLeft"] == "1";
    input.walkStrafeRight = kv["walkStrafeRight"] == "1";
    input.walkSprint = kv["walkSprint"] == "1";
    input.walkJump = kv["walkJump"] == "1";
    input.flightThrottleUp = kv["flightThrottleUp"] == "1";
    input.flightThrottleDown = kv["flightThrottleDown"] == "1";
    input.flightAirBrakes = kv["flightAirBrakes"] == "1";
    input.flightAfterburner = kv["flightAfterburner"] == "1";
    input.firePrimary = kv["firePrimary"] == "1";
    input.dropBomb = kv["dropBomb"] == "1";
    input.terrainGunAdd = kv["terrainGunAdd"] == "1";
    input.terrainGunRemove = kv["terrainGunRemove"] == "1";
    input.terrainEditOpId = static_cast<TerrainEditOpId>(std::strtoull(kv["terrainEditOp"].c_str(), nullptr, 10));
    if (const auto it = kv.find("vehicleId"); it != kv.end()) {
        input.vehicleId = static_cast<VehicleId>(std::strtoull(it->second.c_str(), nullptr, 10));
    }
    if (const auto it = kv.find("driveThrottle"); it != kv.end()) {
        input.driveThrottle = clampNetFloat(std::strtof(it->second.c_str(), nullptr), -1.0f, 1.0f);
    }
    if (const auto it = kv.find("driveSteer"); it != kv.end()) {
        input.driveSteer = clampNetFloat(std::strtof(it->second.c_str(), nullptr), -1.0f, 1.0f);
    }
    if (const auto it = kv.find("driveBrake"); it != kv.end()) {
        input.driveBrake = it->second == "1";
    }
    input.terraformMode = kv["terraformMode"] == "1";
    input.avatar.role = input.role;
    input.avatar.callsign = sanitizeCallsign(kv["callsign"]);
    input.avatar.radioChannel = normalizeRadioChannel(std::atoi(kv["radio"].c_str()));
    input.avatar.radioTx = kv["ptt"] == "1";
    input.avatar.plane = parseAvatarRoleConfigFields(kv, "plane", defaultAvatarManifest().plane);
    input.avatar.walking = parseAvatarRoleConfigFields(kv, "walking", defaultAvatarManifest().walking);
    return input;
}

inline std::optional<NetGameplayStatePacket> parseGameplayStatePacket(const std::string& packet)
{
    const std::vector<std::string> fields = splitPacketFields(packet);
    if (fields.empty() || fields[0] != "GAMEPLAY_STATE") {
        return std::nullopt;
    }

    const auto kv = parseKeyValueFields(fields);
    NetGameplayStatePacket state;
    state.timestamp = std::strtod(kv["ts"].c_str(), nullptr);

    for (const std::string& token : splitPacketFields(kv["players"], ';')) {
        if (token.empty()) {
            continue;
        }
        const std::vector<std::string> parts = splitPacketFields(token, ',');
        if (parts.size() != 6u) {
            continue;
        }
        NetGameplayPlayerState player;
        player.id = std::atoi(parts[0].c_str());
        player.hullStrength = std::strtof(parts[1].c_str(), nullptr);
        player.fuselageStrength = std::strtof(parts[2].c_str(), nullptr);
        player.wear = std::strtof(parts[3].c_str(), nullptr);
        player.terraformMode = parts[4] == "1";
        player.targetsDestroyed = std::atoi(parts[5].c_str());
        state.players.push_back(player);
    }

    for (const std::string& token : splitPacketFields(kv["objects"], ';')) {
        if (token.empty()) {
            continue;
        }
        const std::vector<std::string> parts = splitPacketFields(token, ',');
        if (parts.size() != 14u) {
            continue;
        }
        NetGameplayObjectState object;
        object.kind = parts[0];
        object.id = std::atoi(parts[1].c_str());
        object.ownerId = std::atoi(parts[2].c_str());
        object.active = parts[3] == "1";
        object.pos = {
            std::strtof(parts[4].c_str(), nullptr),
            std::strtof(parts[5].c_str(), nullptr),
            std::strtof(parts[6].c_str(), nullptr)
        };
        object.vel = {
            std::strtof(parts[7].c_str(), nullptr),
            std::strtof(parts[8].c_str(), nullptr),
            std::strtof(parts[9].c_str(), nullptr)
        };
        object.radius = std::strtof(parts[10].c_str(), nullptr);
        object.ttl = std::strtof(parts[11].c_str(), nullptr);
        object.health = std::strtof(parts[12].c_str(), nullptr);
        object.maxHealth = std::strtof(parts[13].c_str(), nullptr);
        state.objects.push_back(object);
    }

    return state;
}

inline std::optional<ModeSwitchPacket> parseModeSwitchPacket(const std::string& packet)
{
    const std::vector<std::string> fields = splitPacketFields(packet);
    if (fields.empty() || fields[0] != "MODE_SWITCH") {
        return std::nullopt;
    }

    const auto kv = parseKeyValueFields(fields);
    ModeSwitchPacket modeSwitch;
    modeSwitch.tick = std::max(0, std::atoi(kv["tick"].c_str()));
    modeSwitch.role = sanitizeRole(kv["role"]);
    modeSwitch.x = std::strtof(kv["x"].c_str(), nullptr);
    modeSwitch.y = std::strtof(kv["y"].c_str(), nullptr);
    modeSwitch.z = std::strtof(kv["z"].c_str(), nullptr);
    if (const auto it = kv.find("vehicleId"); it != kv.end()) {
        modeSwitch.vehicleId = static_cast<VehicleId>(std::strtoull(it->second.c_str(), nullptr, 10));
    }
    modeSwitch.walkYaw = std::strtof(kv["walkYaw"].c_str(), nullptr);
    modeSwitch.walkPitch = std::strtof(kv["walkPitch"].c_str(), nullptr);
    return modeSwitch;
}

inline std::optional<SharedVehicleState> parseVehicleStatePacket(const std::string& packet)
{
    const std::vector<std::string> fields = splitPacketFields(packet);
    if (fields.empty() || fields[0] != "VEHICLE_STATE") {
        return std::nullopt;
    }

    const auto kv = parseKeyValueFields(fields);
    SharedVehicleState state;
    state.vehicleId = static_cast<VehicleId>(std::strtoull(kv["vehicle"].c_str(), nullptr, 10));
    state.entityId = static_cast<ReplicatedEntityId>(std::strtoull(kv["entity"].c_str(), nullptr, 10));
    state.constructId = static_cast<ConstructId>(std::strtoull(kv["construct"].c_str(), nullptr, 10));
    state.constructRevisionId = static_cast<ConstructRevisionId>(std::strtoull(kv["revision"].c_str(), nullptr, 10));
    state.authoritativeOwnerPlayerId = std::max(0, std::atoi(kv["owner"].c_str()));
    state.driverPlayerId = std::max(0, std::atoi(kv["driver"].c_str()));
    if (const auto it = kv.find("seats"); it != kv.end()) {
        state.seatOccupants = decodeVehicleSeatOccupants(it->second);
    }
    state.pos = {
        std::strtof(kv["px"].c_str(), nullptr),
        std::strtof(kv["py"].c_str(), nullptr),
        std::strtof(kv["pz"].c_str(), nullptr)
    };
    state.vel = {
        std::strtof(kv["vx"].c_str(), nullptr),
        std::strtof(kv["vy"].c_str(), nullptr),
        std::strtof(kv["vz"].c_str(), nullptr)
    };
    state.yawRadians = std::strtof(kv["yaw"].c_str(), nullptr);
    state.steerNormalized = clampNetFloat(std::strtof(kv["steer"].c_str(), nullptr), -1.0f, 1.0f);
    state.throttleNormalized = clampNetFloat(std::strtof(kv["throttle"].c_str(), nullptr), -1.0f, 1.0f);
    state.roadAdhesion = clampNetFloat(std::strtof(kv["adhesion"].c_str(), nullptr), 0.0f, 1.0f);
    state.surfaceFriction = clampNetFloat(std::strtof(kv["friction"].c_str(), nullptr), 0.0f, 4.0f);
    state.health = std::max(0.0f, std::strtof(kv["health"].c_str(), nullptr));
    state.maxHealth = std::max(state.health, std::strtof(kv["maxHealth"].c_str(), nullptr));
    state.cage.heave = std::strtof(kv["heave"].c_str(), nullptr);
    state.cage.pitch = std::strtof(kv["pitch"].c_str(), nullptr);
    state.cage.roll = std::strtof(kv["roll"].c_str(), nullptr);
    if (const auto it = kv.find("wheelCompression"); it != kv.end()) {
        state.cage.wheelCompression = decodeWheelCompression(it->second);
    }
    state.active = kv["active"] != "0";
    return state;
}

inline std::optional<VehicleSpawnPacket> parseVehicleSpawnPacket(const std::string& packet)
{
    const std::vector<std::string> fields = splitPacketFields(packet);
    if (fields.empty() || fields[0] != "VEHICLE_SPAWN") {
        return std::nullopt;
    }

    VehicleSpawnPacket spawn;
    const auto kv = parseKeyValueFields(fields);
    spawn.reason = kv["reason"];
    std::string vehicleStatePacket = "VEHICLE_STATE";
    for (std::size_t index = 1; index < fields.size(); ++index) {
        if (fields[index].rfind("reason=", 0) == 0) {
            continue;
        }
        vehicleStatePacket.push_back('|');
        vehicleStatePacket += fields[index];
    }
    if (const auto parsed = parseVehicleStatePacket(vehicleStatePacket); parsed.has_value()) {
        spawn.state = *parsed;
        return spawn;
    }
    return std::nullopt;
}

inline std::optional<VehicleSeatPacket> parseVehicleSeatPacket(const std::string& packet)
{
    const std::vector<std::string> fields = splitPacketFields(packet);
    if (fields.empty() || fields[0] != "VEHICLE_SEAT") {
        return std::nullopt;
    }

    const auto kv = parseKeyValueFields(fields);
    VehicleSeatPacket seat;
    seat.vehicleId = static_cast<VehicleId>(std::strtoull(kv["vehicle"].c_str(), nullptr, 10));
    seat.playerId = std::max(0, std::atoi(kv["player"].c_str()));
    seat.seatIndex = std::atoi(kv["seat"].c_str());
    seat.entering = kv["enter"] != "0";
    return seat;
}

inline std::optional<ConstructPublishPacket> parseConstructPublishPacket(const std::string& packet)
{
    const std::vector<std::string> fields = splitPacketFields(packet);
    if (fields.empty() || fields[0] != "CONSTRUCT_PUBLISH") {
        return std::nullopt;
    }

    const auto kv = parseKeyValueFields(fields);
    ConstructPublishPacket publish;
    publish.blueprint.constructId = static_cast<ConstructId>(std::strtoull(kv["construct"].c_str(), nullptr, 10));
    publish.blueprint.revisionId = static_cast<ConstructRevisionId>(std::strtoull(kv["revision"].c_str(), nullptr, 10));
    publish.blueprint.label = kv["label"];
    if (const auto it = kv.find("halfExtents"); it != kv.end()) {
        publish.blueprint.bodyHalfExtents = decodeNetVec3(it->second, publish.blueprint.bodyHalfExtents);
    }
    if (const auto it = kv.find("seatOffsets"); it != kv.end()) {
        publish.blueprint.seatOffsets = decodeVec3List(it->second);
    }
    publish.blueprint.maxSpeedMps = std::max(1.0f, std::strtof(kv["maxSpeed"].c_str(), nullptr));
    publish.blueprint.accelerationMps2 = std::max(0.0f, std::strtof(kv["accel"].c_str(), nullptr));
    publish.blueprint.brakingMps2 = std::max(0.0f, std::strtof(kv["brake"].c_str(), nullptr));
    publish.blueprint.steeringRate = std::max(0.0f, std::strtof(kv["steerRate"].c_str(), nullptr));
    publish.blueprint.steeringAssist = clampNetFloat(std::strtof(kv["steerAssist"].c_str(), nullptr), 0.0f, 1.0f);
    publish.blueprint.roadGrip = std::max(0.05f, std::strtof(kv["roadGrip"].c_str(), nullptr));
    publish.blueprint.offroadGrip = std::max(0.05f, std::strtof(kv["offroadGrip"].c_str(), nullptr));
    publish.blueprint.maxHealth = std::max(1.0f, std::strtof(kv["maxHealth"].c_str(), nullptr));
    publish.blueprint.suspensionTravel = std::max(0.0f, std::strtof(kv["suspension"].c_str(), nullptr));

    publish.state.constructId = publish.blueprint.constructId;
    publish.state.revisionId = publish.blueprint.revisionId;
    publish.state.authorPlayerId = std::max(0, std::atoi(kv["author"].c_str()));
    publish.state.published = kv["published"] != "0";
    publish.state.label = kv["label"];
    return publish;
}

inline std::optional<ResetFlightPacket> parseResetFlightPacket(const std::string& packet)
{
    const std::vector<std::string> fields = splitPacketFields(packet);
    if (fields.empty() || fields[0] != "RESET_FLIGHT") {
        return std::nullopt;
    }

    const auto kv = parseKeyValueFields(fields);
    ResetFlightPacket reset;
    reset.tick = std::max(0, std::atoi(kv["tick"].c_str()));
    reset.x = std::strtof(kv["x"].c_str(), nullptr);
    reset.z = std::strtof(kv["z"].c_str(), nullptr);
    return reset;
}

inline std::optional<AvatarManifest> parseAvatarManifestPacket(const std::string& packet, int* idOut = nullptr)
{
    const std::vector<std::string> fields = splitPacketFields(packet);
    if (fields.empty() || fields[0] != "AVATAR_MANIFEST") {
        return std::nullopt;
    }

    const auto kv = parseKeyValueFields(fields);
    if (idOut != nullptr) {
        *idOut = std::atoi(kv["id"].c_str());
    }

    AvatarManifest avatar = defaultAvatarManifest();
    avatar.role = sanitizeRole(kv["role"]);
    avatar.callsign = sanitizeCallsign(kv["callsign"]);
    avatar.radioChannel = normalizeRadioChannel(std::atoi(kv["radio"].c_str()));
    avatar.radioTx = kv["ptt"] == "1";
    avatar.plane = parseAvatarRoleConfigFields(kv, "plane", defaultAvatarManifest().plane);
    avatar.walking = parseAvatarRoleConfigFields(kv, "walking", defaultAvatarManifest().walking);
    return avatar;
}

inline std::optional<VoiceStatePacket> parseVoiceStatePacket(const std::string& packet)
{
    const std::vector<std::string> fields = splitPacketFields(packet);
    if (fields.empty() || fields[0] != "VOICE_STATE") {
        return std::nullopt;
    }

    const auto kv = parseKeyValueFields(fields);
    VoiceStatePacket state;
    state.id = std::atoi(kv["id"].c_str());
    state.channel = normalizeRadioChannel(std::atoi(kv["channel"].c_str()));
    state.transmitting = kv["tx"] == "1";
    return state;
}

inline std::optional<VoiceFramePacket> parseVoiceFramePacket(const std::string& packet)
{
    const std::vector<std::string> fields = splitPacketFields(packet);
    if (fields.empty() || fields[0] != "VOICE_FRAME") {
        return std::nullopt;
    }

    const auto kv = parseKeyValueFields(fields);
    std::optional<std::string> decoded = NetProtocolDetail::decodeBase64(kv["data"]);
    if (!decoded.has_value()) {
        return std::nullopt;
    }

    VoiceFramePacket frame;
    frame.senderId = std::max(0, std::atoi(kv["id"].c_str()));
    frame.channel = normalizeRadioChannel(std::atoi(kv["channel"].c_str()));
    frame.data = *decoded;
    frame.compressedData = std::move(*decoded);
    return frame;
}

inline std::optional<BlobMetaPacket> parseBlobMetaPacket(const std::string& packet)
{
    const std::vector<std::string> fields = splitPacketFields(packet);
    if (fields.empty() || fields[0] != "BLOB_META") {
        return std::nullopt;
    }

    const auto kv = parseKeyValueFields(fields);
    BlobMetaPacket meta;
    meta.kind = kv["kind"];
    meta.hash = kv["hash"];
    meta.modelHash = kv["modelHash"];
    meta.role = kv["role"];
    meta.ownerId = kv["ownerId"];
    meta.rawBytes = std::atoi(kv["rawBytes"].c_str());
    meta.encodedBytes = std::atoi(kv["encodedBytes"].c_str());
    meta.chunkSize = std::atoi(kv["chunkSize"].c_str());
    meta.chunkCount = std::atoi(kv["chunkCount"].c_str());
    return meta;
}

inline std::optional<BlobChunkPacket> parseBlobChunkPacket(const std::string& packet)
{
    const std::vector<std::string> fields = splitPacketFields(packet);
    if (fields.empty() || fields[0] != "BLOB_CHUNK") {
        return std::nullopt;
    }

    const auto kv = parseKeyValueFields(fields);
    BlobChunkPacket chunk;
    chunk.kind = kv["kind"];
    chunk.hash = kv["hash"];
    chunk.index = std::atoi(kv["idx"].c_str());
    chunk.data = kv["data"];
    return chunk;
}

inline std::optional<TerrainCrater> parseCraterPacket(const std::string& packet)
{
    const std::vector<std::string> fields = splitPacketFields(packet);
    if (fields.empty() || fields[0] != "CRATER_EVENT") {
        return std::nullopt;
    }

    const auto kv = parseKeyValueFields(fields);
    TerrainCrater crater;
    crater.x = std::strtof(kv["x"].c_str(), nullptr);
    crater.y = std::strtof(kv["y"].c_str(), nullptr);
    crater.z = std::strtof(kv["z"].c_str(), nullptr);
    crater.radius = std::max(1.0f, std::strtof(kv["radius"].c_str(), nullptr));
    crater.depth = std::max(0.4f, std::strtof(kv["depth"].c_str(), nullptr));
    crater.rim = clamp(std::strtof(kv["rim"].c_str(), nullptr), 0.0f, 0.75f);
    return crater;
}

inline std::optional<TerrainEditRequest> parseTerrainEditRequestPacket(const std::string& packet)
{
    const std::vector<std::string> fields = splitPacketFields(packet);
    if (fields.empty() || fields[0] != "TERRAIN_EDIT_REQUEST") {
        return std::nullopt;
    }

    const auto kv = parseKeyValueFields(fields);
    TerrainEditRequest request;
    request.opId = static_cast<TerrainEditOpId>(std::strtoull(kv["op"].c_str(), nullptr, 10));
    request.playerId = std::atoi(kv["id"].c_str());
    request.kind = kv["kind"];
    request.center = {
        std::strtof(kv["x"].c_str(), nullptr),
        std::strtof(kv["y"].c_str(), nullptr),
        std::strtof(kv["z"].c_str(), nullptr)
    };
    request.surfaceNormal = normalize({
        std::strtof(kv["nx"].c_str(), nullptr),
        std::strtof(kv["ny"].c_str(), nullptr),
        std::strtof(kv["nz"].c_str(), nullptr)
    }, { 0.0f, 1.0f, 0.0f });
    request.radius = std::max(0.0f, std::strtof(kv["radius"].c_str(), nullptr));
    request.magnitude = std::strtof(kv["magnitude"].c_str(), nullptr);
    request.requestedAt = std::strtod(kv["ts"].c_str(), nullptr);
    return request;
}

inline std::optional<TerrainEditAck> parseTerrainEditAckPacket(const std::string& packet)
{
    const std::vector<std::string> fields = splitPacketFields(packet);
    if (fields.empty() || fields[0] != "TERRAIN_EDIT_ACK") {
        return std::nullopt;
    }

    const auto kv = parseKeyValueFields(fields);
    TerrainEditAck ack;
    ack.opId = static_cast<TerrainEditOpId>(std::strtoull(kv["op"].c_str(), nullptr, 10));
    ack.playerId = std::atoi(kv["id"].c_str());
    ack.accepted = kv["ok"] == "1";
    ack.kind = kv["kind"];
    ack.center = {
        std::strtof(kv["x"].c_str(), nullptr),
        std::strtof(kv["y"].c_str(), nullptr),
        std::strtof(kv["z"].c_str(), nullptr)
    };
    ack.radius = std::max(0.0f, std::strtof(kv["radius"].c_str(), nullptr));
    ack.magnitude = std::strtof(kv["magnitude"].c_str(), nullptr);
    ack.touchedChunks = std::max(0, std::atoi(kv["chunks"].c_str()));
    ack.reason = kv["reason"];
    return ack;
}

inline bool applyWorldSyncPacket(const std::string& packet, TerrainParams& params)
{
    const std::vector<std::string> fields = splitPacketFields(packet);
    if (fields.empty() || fields[0] != "WORLD_SYNC") {
        return false;
    }

    const auto kv = parseKeyValueFields(fields);
    TerrainParams next = params;

    const auto findField = [&kv](const char* key) -> const std::string* {
        const auto it = kv.find(key);
        return it == kv.end() ? nullptr : &it->second;
    };
    const auto readBool = [&findField](const char* key, bool current) -> bool {
        if (const std::string* value = findField(key); value != nullptr) {
            return *value == "1" || *value == "true";
        }
        return current;
    };
    const auto readInt = [&findField](const char* key, int current) -> int {
        if (const std::string* value = findField(key); value != nullptr) {
            return std::atoi(value->c_str());
        }
        return current;
    };
    const auto readFloat = [&findField](const char* key, float current) -> float {
        if (const std::string* value = findField(key); value != nullptr) {
            return std::strtof(value->c_str(), nullptr);
        }
        return current;
    };
    const auto readVec3 = [&findField](const char* key, const Vec3& current) -> Vec3 {
        if (const std::string* value = findField(key); value != nullptr) {
            return decodeNetVec3(*value, current);
        }
        return current;
    };

    next.seed = std::max(1, readInt("seed", next.seed));
    next.chunkSize = std::max(8.0f, readFloat("chunk", next.chunkSize));
    next.worldRadius = std::max(next.chunkSize * 4.0f, readFloat("worldRadius", next.worldRadius));
    next.minY = readFloat("minY", next.minY);
    next.maxY = std::max(next.minY + 16.0f, readFloat("maxY", next.maxY));
    next.lod0Radius = std::max(1, readInt("lod0", next.lod0Radius));
    next.lod1Radius = std::max(next.lod0Radius, readInt("lod1", next.lod1Radius));
    next.lod2Radius = std::max(next.lod1Radius, readInt("lod2", next.lod2Radius));
    next.terrainQuality = readFloat("quality", next.terrainQuality);
    next.autoQualityEnabled = readBool("autoQuality", next.autoQualityEnabled);
    next.targetFrameMs = readFloat("targetFrameMs", next.targetFrameMs);
    next.lod0ChunkScale = std::max(1, readInt("lod0ChunkScale", next.lod0ChunkScale));
    next.lod1ChunkScale = std::max(next.lod0ChunkScale, readInt("lod1ChunkScale", next.lod1ChunkScale));
    next.lod2ChunkScale = std::max(next.lod1ChunkScale, readInt("lod2ChunkScale", next.lod2ChunkScale));
    next.textureTilesEnabled = readBool("textureTiles", next.textureTilesEnabled);
    next.lod0TextureResolution = std::max(16, readInt("lod0TexRes", next.lod0TextureResolution));
    next.lod1TextureResolution = std::max(16, readInt("lod1TexRes", next.lod1TextureResolution));
    next.lod2TextureResolution = std::max(16, readInt("lod2TexRes", next.lod2TextureResolution));
    next.gameplayRadiusMeters = readFloat("gameplayRadius", next.gameplayRadiusMeters);
    next.midFieldRadiusMeters = readFloat("midRadius", next.midFieldRadiusMeters);
    next.horizonRadiusMeters = readFloat("horizonRadius", next.horizonRadiusMeters);
    next.lod0BaseCellSize = std::max(0.25f, readFloat("lod0Cell", next.lod0BaseCellSize));
    next.lod1BaseCellSize = std::max(next.lod0BaseCellSize, readFloat("lod1Cell", next.lod1BaseCellSize));
    next.lod2BaseCellSize = std::max(next.lod1BaseCellSize, readFloat("lod2Cell", next.lod2BaseCellSize));
    next.meshBuildBudget = std::max(1, readInt("meshBudget", next.meshBuildBudget));
    next.workerMaxInflight = std::max(1, readInt("inflight", next.workerMaxInflight));
    next.workerResultBudgetPerFrame = std::max(1, readInt("resultBudget", next.workerResultBudgetPerFrame));
    next.workerResultTimeBudgetMs = readFloat("resultBudgetMs", next.workerResultTimeBudgetMs);
    next.maxAdaptiveLod1Radius = std::max(1, readInt("maxAdaptiveLod1", next.maxAdaptiveLod1Radius));
    next.maxPendingChunks = std::max(4, readInt("maxPendingChunks", next.maxPendingChunks));
    next.maxStaleChunks = std::max(0, readInt("maxStaleChunks", next.maxStaleChunks));
    next.maxDisplayedChunks = std::max(1, readInt("maxDisplayedChunks", next.maxDisplayedChunks));
    next.maxDisplayedChunksHardCap = std::max(next.maxDisplayedChunks, readInt("maxDisplayedChunksHardCap", next.maxDisplayedChunksHardCap));
    next.drawDistanceOverridesLodRadius = readBool("drawDistanceOverridesLod", next.drawDistanceOverridesLodRadius);
    next.splitLodEnabled = readBool("splitLod", next.splitLodEnabled);
    next.highResSplitRatio = readFloat("splitRatio", next.highResSplitRatio);
    next.chunkCacheLimit = std::max(8, readInt("cache", next.chunkCacheLimit));
    next.farLodConeEnabled = readBool("farLodCone", next.farLodConeEnabled);
    next.farLodConeDegrees = readFloat("farLodConeDegrees", next.farLodConeDegrees);
    next.rearLod2Radius = std::max(0, readInt("rearLod2Radius", next.rearLod2Radius));
    next.baseHeight = readFloat("baseHeight", next.baseHeight);
    next.heightAmplitude = readFloat("heightAmp", next.heightAmplitude);
    next.heightFrequency = readFloat("heightFreq", next.heightFrequency);
    next.heightOctaves = std::max(1, readInt("heightOctaves", next.heightOctaves));
    next.heightLacunarity = readFloat("heightLacunarity", next.heightLacunarity);
    next.heightGain = readFloat("heightGain", next.heightGain);
    next.surfaceDetailAmplitude = readFloat("surfaceDetailAmp", next.surfaceDetailAmplitude);
    next.surfaceDetailFrequency = readFloat("surfaceDetailFreq", next.surfaceDetailFrequency);
    next.ridgeAmplitude = readFloat("ridgeAmp", next.ridgeAmplitude);
    next.ridgeFrequency = readFloat("ridgeFreq", next.ridgeFrequency);
    next.ridgeSharpness = readFloat("ridgeSharpness", next.ridgeSharpness);
    next.macroWarpAmplitude = readFloat("macroWarpAmp", next.macroWarpAmplitude);
    next.macroWarpFrequency = readFloat("macroWarpFreq", next.macroWarpFrequency);
    next.terraceStrength = readFloat("terraceStrength", next.terraceStrength);
    next.terraceStep = readFloat("terraceStep", next.terraceStep);
    next.waterLevel = readFloat("waterLevel", next.waterLevel);
    next.shorelineBand = readFloat("shorelineBand", next.shorelineBand);
    next.waterWaveAmplitude = readFloat("waterWaveAmp", next.waterWaveAmplitude);
    next.waterWaveFrequency = readFloat("waterWaveFreq", next.waterWaveFrequency);
    next.biomeFrequency = readFloat("biomeFreq", next.biomeFrequency);
    next.snowLine = readFloat("snowLine", next.snowLine);
    next.caveEnabled = readBool("caveEnabled", next.caveEnabled);
    next.caveFrequency = readFloat("caveFreq", next.caveFrequency);
    next.caveThreshold = readFloat("caveThreshold", next.caveThreshold);
    next.caveStrength = readFloat("caveStrength", next.caveStrength);
    next.caveOctaves = std::max(1, readInt("caveOctaves", next.caveOctaves));
    next.caveLacunarity = readFloat("caveLacunarity", next.caveLacunarity);
    next.caveGain = readFloat("caveGain", next.caveGain);
    next.caveMinY = readFloat("caveMinY", next.caveMinY);
    next.caveMaxY = readFloat("caveMaxY", next.caveMaxY);
    next.tunnelCount = std::max(0, readInt("tunnelCount", next.tunnelCount));
    next.tunnelRadiusMin = readFloat("tunnelRadiusMin", next.tunnelRadiusMin);
    next.tunnelRadiusMax = readFloat("tunnelRadiusMax", next.tunnelRadiusMax);
    next.tunnelLengthMin = readFloat("tunnelLengthMin", next.tunnelLengthMin);
    next.tunnelLengthMax = readFloat("tunnelLengthMax", next.tunnelLengthMax);
    next.tunnelSegmentLength = readFloat("tunnelSegmentLength", next.tunnelSegmentLength);
    next.generatorVersion = std::max(1, readInt("generatorVersion", next.generatorVersion));
    next.surfaceOnlyMeshing = readBool("surfaceOnlyMeshing", next.surfaceOnlyMeshing);
    next.threadedMeshing = readBool("threadedMeshing", next.threadedMeshing);
    next.enableSkirts = readBool("enableSkirts", next.enableSkirts);
    next.skirtDepth = readFloat("skirtDepth", next.skirtDepth);
    next.maxChunkCellsPerAxis = std::max(8, readInt("maxChunkCellsPerAxis", next.maxChunkCellsPerAxis));
    next.craterHistoryLimit = std::max(1, readInt("craterHistoryLimit", next.craterHistoryLimit));
    next.waterRatio = readFloat("waterRatio", next.waterRatio);
    next.decoration.enabled = readBool("decorationEnabled", next.decoration.enabled);
    next.decoration.density = readFloat("decorationDensity", next.decoration.density);
    next.decoration.nearDensityScale = readFloat("decorationNearDensity", next.decoration.nearDensityScale);
    next.decoration.midDensityScale = readFloat("decorationMidDensity", next.decoration.midDensityScale);
    next.decoration.farDensityScale = readFloat("decorationFarDensity", next.decoration.farDensityScale);
    next.decoration.treeLineOffset = readFloat("decorationTreeLineOffset", next.decoration.treeLineOffset);
    next.decoration.shoreBrushDensity = readFloat("decorationShoreBrushDensity", next.decoration.shoreBrushDensity);
    next.decoration.rockDensity = readFloat("decorationRockDensity", next.decoration.rockDensity);
    next.decoration.collisionEnabled = readBool("decorationCollision", next.decoration.collisionEnabled);
    next.decoration.seedOffset = readInt("decorationSeedOffset", next.decoration.seedOffset);
    next.grassColor = readVec3("grassColor", next.grassColor);
    next.roadColor = readVec3("roadColor", next.roadColor);
    next.fieldColor = readVec3("fieldColor", next.fieldColor);
    next.waterColor = readVec3("waterColor", next.waterColor);
    next.grassVar = readVec3("grassVar", next.grassVar);
    next.roadVar = readVec3("roadVar", next.roadVar);
    next.fieldVar = readVec3("fieldVar", next.fieldVar);
    next.waterVar = readVec3("waterVar", next.waterVar);
    next.mountainFront.enabled = readBool("mountainFrontEnabled", next.mountainFront.enabled);
    next.mountainFront.coastalBandStart = readFloat("mountainFrontCoastalStart", next.mountainFront.coastalBandStart);
    next.mountainFront.coastalBandEnd = readFloat("mountainFrontCoastalEnd", next.mountainFront.coastalBandEnd);
    next.mountainFront.foothillStart = readFloat("mountainFrontFoothillStart", next.mountainFront.foothillStart);
    next.mountainFront.foothillEnd = readFloat("mountainFrontFoothillEnd", next.mountainFront.foothillEnd);
    next.mountainFront.inlandFadeStart = readFloat("mountainFrontInlandFadeStart", next.mountainFront.inlandFadeStart);
    next.mountainFront.inlandFadeEnd = readFloat("mountainFrontInlandFadeEnd", next.mountainFront.inlandFadeEnd);
    next.mountainFront.mountainWallStrength = readFloat("mountainFrontWallStrength", next.mountainFront.mountainWallStrength);
    next.mountainFront.shelfStrength = readFloat("mountainFrontShelfStrength", next.mountainFront.shelfStrength);
    next.mountainFront.cliffStrength = readFloat("mountainFrontCliffStrength", next.mountainFront.cliffStrength);
    next.mountainFront.ridgeWarpScale = readFloat("mountainFrontRidgeWarpScale", next.mountainFront.ridgeWarpScale);
    next.roads.enabled = readBool("roadsEnabled", next.roads.enabled);
    next.roads.desiredRoadCount = std::max(0, readInt("roadsDesiredCount", next.roads.desiredRoadCount));
    next.roads.laneWidthMeters = readFloat("roadsLaneWidth", next.roads.laneWidthMeters);
    next.roads.shoulderWidthMeters = readFloat("roadsShoulderWidth", next.roads.shoulderWidthMeters);
    next.roads.cutWidthMeters = readFloat("roadsCutWidth", next.roads.cutWidthMeters);
    next.roads.cutBankWidthMeters = readFloat("roadsCutBankWidth", next.roads.cutBankWidthMeters);
    next.roads.maxGrade = readFloat("roadsMaxGrade", next.roads.maxGrade);
    next.roads.maxGradeHard = readFloat("roadsMaxGradeHard", next.roads.maxGradeHard);
    next.roads.minTurnRadiusMeters = readFloat("roadsMinTurnRadius", next.roads.minTurnRadiusMeters);
    next.roads.preferredTurnRadiusMeters = readFloat("roadsPreferredTurnRadius", next.roads.preferredTurnRadiusMeters);
    next.roads.stepMeters = readFloat("roadsStepMeters", next.roads.stepMeters);
    next.roads.sampleLookAheadMeters = readFloat("roadsSampleLookAhead", next.roads.sampleLookAheadMeters);
    next.roads.sampleConeDegrees = readFloat("roadsSampleCone", next.roads.sampleConeDegrees);
    next.roads.branchChance = readFloat("roadsBranchChance", next.roads.branchChance);
    next.roads.flattenStrength = readFloat("roadsFlattenStrength", next.roads.flattenStrength);
    next.roads.cutStrength = readFloat("roadsCutStrength", next.roads.cutStrength);
    next.roads.fillStrength = readFloat("roadsFillStrength", next.roads.fillStrength);
    next.roads.edgeFeatherMeters = readFloat("roadsEdgeFeather", next.roads.edgeFeatherMeters);
    next.roads.potholeChance = readFloat("roadsPotholeChance", next.roads.potholeChance);
    next.roads.potholeDepthMeters = readFloat("roadsPotholeDepth", next.roads.potholeDepthMeters);
    next.roads.patchChance = readFloat("roadsPatchChance", next.roads.patchChance);
    next.roads.patchRaiseMeters = readFloat("roadsPatchRaise", next.roads.patchRaiseMeters);
    next.roads.asphaltNoiseScale = readFloat("roadsAsphaltNoiseScale", next.roads.asphaltNoiseScale);
    next.roads.crackStrength = readFloat("roadsCrackStrength", next.roads.crackStrength);

    if (const std::string* craterField = findField("craters"); craterField != nullptr) {
        next.dynamicCraters.clear();
        for (const std::string& craterToken : splitPacketFields(*craterField, ';')) {
            if (craterToken.empty()) {
                continue;
            }
            const std::vector<std::string> parts = splitPacketFields(craterToken, ',');
            if (parts.size() != 6u) {
                continue;
            }
            TerrainCrater crater;
            crater.x = std::strtof(parts[0].c_str(), nullptr);
            crater.y = std::strtof(parts[1].c_str(), nullptr);
            crater.z = std::strtof(parts[2].c_str(), nullptr);
            crater.radius = std::max(1.0f, std::strtof(parts[3].c_str(), nullptr));
            crater.depth = std::max(0.4f, std::strtof(parts[4].c_str(), nullptr));
            crater.rim = clamp(std::strtof(parts[5].c_str(), nullptr), 0.0f, 0.75f);
            next.dynamicCraters.push_back(crater);
        }
    }
    if (const std::string* tunnelSeeds = findField("tunnelSeeds"); tunnelSeeds != nullptr) {
        next.explicitTunnelSeeds = decodeTunnelSeeds(*tunnelSeeds);
        if (!next.explicitTunnelSeeds.empty()) {
            next.tunnelCount = static_cast<int>(next.explicitTunnelSeeds.size());
        }
    }
    if (const std::string* roadPaths = findField("roadPaths"); roadPaths != nullptr) {
        next.explicitRoadPaths = decodeRoadPaths(*roadPaths);
    }

    params = normalizeTerrainParams(next);
    return true;
}

inline std::optional<AoiSubscription> parseAoiSubscribePacket(const std::string& packet)
{
    const std::vector<std::string> fields = splitPacketFields(packet);
    if (fields.empty() || fields[0] != "AOI_SUBSCRIBE") {
        return std::nullopt;
    }

    const auto kv = parseKeyValueFields(fields);
    AoiSubscription subscription;
    subscription.centerChunkX = std::atoi(kv["cx"].c_str());
    subscription.centerChunkZ = std::atoi(kv["cz"].c_str());
    subscription.radiusChunks = std::clamp(std::atoi(kv["radius"].c_str()), 2, 96);
    subscription.snapshotNearMeters = std::max(128.0f, std::strtof(kv["snapshotNear"].c_str(), nullptr));
    subscription.snapshotFarMeters = std::max(subscription.snapshotNearMeters, std::strtof(kv["snapshotFar"].c_str(), nullptr));
    return subscription;
}

}  // namespace NativeGame
