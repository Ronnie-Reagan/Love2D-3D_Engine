#pragma once

#include "NativeGame/Flight.hpp"
#include "NativeGame/WorldStore.hpp"
#include "NativeGame/WorldWire.hpp"

#include <algorithm>
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

constexpr float kRemoteInterpolationDelaySeconds = 0.010f;
constexpr float kRemoteExtrapolationCapSeconds = 0.120f;
constexpr std::size_t kRemoteSnapshotBufferLimit = 128u;
constexpr int kVoiceChannelCount = 8;

struct OnlineSettings {
    bool steamEnabled = true;
    bool multiplayerEnabled = false;
    bool voiceEnabled = true;
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
    std::string modelHash = "builtin-cube";
    float scale = 1.0f;
    std::string skinHash;
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
    float frameDt = 1.0f / 120.0f;
    float walkYaw = 0.0f;
    float walkPitch = 0.0f;
    float walkMoveSpeed = 10.0f;
    float throttle = 0.0f;
    float yokePitch = 0.0f;
    float yokeYaw = 0.0f;
    float yokeRoll = 0.0f;
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
    double timestamp = 0.0;
    AvatarManifest avatar {};
};

struct VoiceStatePacket {
    int id = 0;
    int channel = 1;
    bool transmitting = false;
};

struct VoiceFramePacket {
    int channel = 1;
    std::string data;
    std::string compressedData;
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
    return "plane";
}

inline std::string formatNetFloat(double value, int precision = 4)
{
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(precision);
    stream << value;
    return stream.str();
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
    manifest.plane.modelHash = "builtin-cube";
    manifest.walking.modelHash = "builtin-walking-biped";
    manifest.callsign = "Pilot";
    return manifest;
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
    return encodePacket("HELLO", {
        { "role", sanitizeRole(avatar.role) },
        { "callsign", sanitizeCallsign(avatar.callsign) },
        { "radio", std::to_string(normalizeRadioChannel(avatar.radioChannel)) },
        { "ptt", avatar.radioTx ? "1" : "0" },
        { "planeModelHash", avatar.plane.modelHash },
        { "planeScale", formatNetFloat(avatar.plane.scale) },
        { "planeSkinHash", avatar.plane.skinHash },
        { "planeYaw", formatNetFloat(avatar.plane.yawDegrees) },
        { "planePitch", formatNetFloat(avatar.plane.pitchDegrees) },
        { "planeRoll", formatNetFloat(avatar.plane.rollDegrees) },
        { "planeOffX", formatNetFloat(avatar.plane.offset.x) },
        { "planeOffY", formatNetFloat(avatar.plane.offset.y) },
        { "planeOffZ", formatNetFloat(avatar.plane.offset.z) },
        { "walkingModelHash", avatar.walking.modelHash },
        { "walkingScale", formatNetFloat(avatar.walking.scale) },
        { "walkingSkinHash", avatar.walking.skinHash },
        { "walkingYaw", formatNetFloat(avatar.walking.yawDegrees) },
        { "walkingPitch", formatNetFloat(avatar.walking.pitchDegrees) },
        { "walkingRoll", formatNetFloat(avatar.walking.rollDegrees) },
        { "walkingOffX", formatNetFloat(avatar.walking.offset.x) },
        { "walkingOffY", formatNetFloat(avatar.walking.offset.y) },
        { "walkingOffZ", formatNetFloat(avatar.walking.offset.z) }
    });
}

inline std::string buildAvatarManifestPacket(int id, const AvatarManifest& avatar)
{
    return encodePacket("AVATAR_MANIFEST", {
        { "id", std::to_string(id) },
        { "role", sanitizeRole(avatar.role) },
        { "callsign", sanitizeCallsign(avatar.callsign) },
        { "radio", std::to_string(normalizeRadioChannel(avatar.radioChannel)) },
        { "ptt", avatar.radioTx ? "1" : "0" },
        { "planeModelHash", avatar.plane.modelHash },
        { "planeScale", formatNetFloat(avatar.plane.scale) },
        { "planeSkinHash", avatar.plane.skinHash },
        { "planeYaw", formatNetFloat(avatar.plane.yawDegrees) },
        { "planePitch", formatNetFloat(avatar.plane.pitchDegrees) },
        { "planeRoll", formatNetFloat(avatar.plane.rollDegrees) },
        { "planeOffX", formatNetFloat(avatar.plane.offset.x) },
        { "planeOffY", formatNetFloat(avatar.plane.offset.y) },
        { "planeOffZ", formatNetFloat(avatar.plane.offset.z) },
        { "walkingModelHash", avatar.walking.modelHash },
        { "walkingScale", formatNetFloat(avatar.walking.scale) },
        { "walkingSkinHash", avatar.walking.skinHash },
        { "walkingYaw", formatNetFloat(avatar.walking.yawDegrees) },
        { "walkingPitch", formatNetFloat(avatar.walking.pitchDegrees) },
        { "walkingRoll", formatNetFloat(avatar.walking.rollDegrees) },
        { "walkingOffX", formatNetFloat(avatar.walking.offset.x) },
        { "walkingOffY", formatNetFloat(avatar.walking.offset.y) },
        { "walkingOffZ", formatNetFloat(avatar.walking.offset.z) }
    });
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
        { "channel", std::to_string(normalizeRadioChannel(frame.channel)) },
        { "data", NetProtocolDetail::encodeBase64(payload) }
    });
}

inline std::string buildSnapshotPacket(const NetPlayerSnapshot& snapshot)
{
    return encodePacket("SNAPSHOT", {
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
        { "ts", formatNetFloat(snapshot.timestamp) },
        { "role", sanitizeRole(snapshot.avatar.role) },
        { "callsign", sanitizeCallsign(snapshot.avatar.callsign) },
        { "radio", std::to_string(normalizeRadioChannel(snapshot.avatar.radioChannel)) },
        { "ptt", snapshot.avatar.radioTx ? "1" : "0" },
        { "planeModelHash", snapshot.avatar.plane.modelHash },
        { "planeScale", formatNetFloat(snapshot.avatar.plane.scale) },
        { "planeSkinHash", snapshot.avatar.plane.skinHash },
        { "planeYaw", formatNetFloat(snapshot.avatar.plane.yawDegrees) },
        { "planePitch", formatNetFloat(snapshot.avatar.plane.pitchDegrees) },
        { "planeRoll", formatNetFloat(snapshot.avatar.plane.rollDegrees) },
        { "planeOffX", formatNetFloat(snapshot.avatar.plane.offset.x) },
        { "planeOffY", formatNetFloat(snapshot.avatar.plane.offset.y) },
        { "planeOffZ", formatNetFloat(snapshot.avatar.plane.offset.z) },
        { "walkingModelHash", snapshot.avatar.walking.modelHash },
        { "walkingScale", formatNetFloat(snapshot.avatar.walking.scale) },
        { "walkingSkinHash", snapshot.avatar.walking.skinHash },
        { "walkingYaw", formatNetFloat(snapshot.avatar.walking.yawDegrees) },
        { "walkingPitch", formatNetFloat(snapshot.avatar.walking.pitchDegrees) },
        { "walkingRoll", formatNetFloat(snapshot.avatar.walking.rollDegrees) },
        { "walkingOffX", formatNetFloat(snapshot.avatar.walking.offset.x) },
        { "walkingOffY", formatNetFloat(snapshot.avatar.walking.offset.y) },
        { "walkingOffZ", formatNetFloat(snapshot.avatar.walking.offset.z) }
    });
}

inline std::string buildInputPacket(const NetPlayerInput& input)
{
    return encodePacket("INPUT", {
        { "tick", std::to_string(input.tick) },
        { "role", sanitizeRole(input.role) },
        { "frameDt", formatNetFloat(input.frameDt) },
        { "callsign", sanitizeCallsign(input.avatar.callsign) },
        { "walkYaw", formatNetFloat(input.walkYaw) },
        { "walkPitch", formatNetFloat(input.walkPitch) },
        { "walkMoveSpeed", formatNetFloat(input.walkMoveSpeed) },
        { "throttle", formatNetFloat(input.throttle) },
        { "yokePitch", formatNetFloat(input.yokePitch) },
        { "yokeYaw", formatNetFloat(input.yokeYaw) },
        { "yokeRoll", formatNetFloat(input.yokeRoll) },
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
        { "terraformMode", input.terraformMode ? "1" : "0" },
        { "radio", std::to_string(normalizeRadioChannel(input.avatar.radioChannel)) },
        { "ptt", input.avatar.radioTx ? "1" : "0" },
        { "planeModelHash", input.avatar.plane.modelHash },
        { "planeScale", formatNetFloat(input.avatar.plane.scale) },
        { "planeSkinHash", input.avatar.plane.skinHash },
        { "planeYaw", formatNetFloat(input.avatar.plane.yawDegrees) },
        { "planePitch", formatNetFloat(input.avatar.plane.pitchDegrees) },
        { "planeRoll", formatNetFloat(input.avatar.plane.rollDegrees) },
        { "planeOffX", formatNetFloat(input.avatar.plane.offset.x) },
        { "planeOffY", formatNetFloat(input.avatar.plane.offset.y) },
        { "planeOffZ", formatNetFloat(input.avatar.plane.offset.z) },
        { "walkingModelHash", input.avatar.walking.modelHash },
        { "walkingScale", formatNetFloat(input.avatar.walking.scale) },
        { "walkingSkinHash", input.avatar.walking.skinHash },
        { "walkingYaw", formatNetFloat(input.avatar.walking.yawDegrees) },
        { "walkingPitch", formatNetFloat(input.avatar.walking.pitchDegrees) },
        { "walkingRoll", formatNetFloat(input.avatar.walking.rollDegrees) },
        { "walkingOffX", formatNetFloat(input.avatar.walking.offset.x) },
        { "walkingOffY", formatNetFloat(input.avatar.walking.offset.y) },
        { "walkingOffZ", formatNetFloat(input.avatar.walking.offset.z) }
    });
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
        { "walkYaw", formatNetFloat(packet.walkYaw) },
        { "walkPitch", formatNetFloat(packet.walkPitch) }
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
    info.chunkSize = meta.terrainProfile.chunkSize;
    info.horizonRadiusMeters = std::max(meta.terrainProfile.worldRadius, fallbackTerrain.horizonRadiusMeters);
    info.heightAmplitude = meta.terrainProfile.heightAmplitude;
    info.heightFrequency = meta.terrainProfile.heightFrequency;
    info.waterLevel = meta.terrainProfile.waterLevel;
    info.tunnelSeeds = meta.tunnelSeeds;
    info.spawnX = meta.spawn.x;
    info.spawnY = meta.spawn.y;
    info.spawnZ = meta.spawn.z;
    return info;
}

inline std::string buildWorldInfoPacket(const WorldInfoSnapshot& info)
{
    return encodePacket("WORLD_INFO", {
        { "worldId", info.worldId },
        { "formatVersion", std::to_string(info.formatVersion) },
        { "seed", std::to_string(info.seed) },
        { "chunkSize", formatNetFloat(info.chunkSize) },
        { "horizonRadius", formatNetFloat(info.horizonRadiusMeters) },
        { "heightAmp", formatNetFloat(info.heightAmplitude) },
        { "heightFreq", formatNetFloat(info.heightFrequency) },
        { "waterLevel", formatNetFloat(info.waterLevel) },
        { "tunnelSeeds", encodeTunnelSeeds(info.tunnelSeeds) },
        { "spawnX", formatNetFloat(info.spawnX) },
        { "spawnY", formatNetFloat(info.spawnY) },
        { "spawnZ", formatNetFloat(info.spawnZ) }
    });
}

inline std::string buildWorldSyncPacket(const TerrainParams& params)
{
    std::ostringstream craterStream;
    for (std::size_t index = 0; index < params.dynamicCraters.size(); ++index) {
        const TerrainCrater& crater = params.dynamicCraters[index];
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

    return encodePacket("WORLD_SYNC", {
        { "seed", std::to_string(params.seed) },
        { "chunk", formatNetFloat(params.chunkSize) },
        { "lod0", std::to_string(params.lod0Radius) },
        { "lod1", std::to_string(params.lod1Radius) },
        { "lod2", std::to_string(params.lod2Radius) },
        { "splitLod", params.splitLodEnabled ? "1" : "0" },
        { "splitRatio", formatNetFloat(params.highResSplitRatio) },
        { "lod0Cell", formatNetFloat(params.lod0BaseCellSize) },
        { "lod1Cell", formatNetFloat(params.lod1BaseCellSize) },
        { "lod2Cell", formatNetFloat(params.lod2BaseCellSize) },
        { "quality", formatNetFloat(params.terrainQuality) },
        { "meshBudget", std::to_string(params.meshBuildBudget) },
        { "inflight", std::to_string(params.workerMaxInflight) },
        { "cache", std::to_string(params.chunkCacheLimit) },
        { "autoQuality", params.autoQualityEnabled ? "1" : "0" },
        { "targetFrameMs", formatNetFloat(params.targetFrameMs) },
        { "heightAmp", formatNetFloat(params.heightAmplitude) },
        { "heightFreq", formatNetFloat(params.heightFrequency) },
        { "caveEnabled", params.caveEnabled ? "1" : "0" },
        { "caveStrength", formatNetFloat(params.caveStrength) },
        { "tunnelCount", std::to_string(params.tunnelCount) },
        { "tunnelRadiusMin", formatNetFloat(params.tunnelRadiusMin) },
        { "tunnelRadiusMax", formatNetFloat(params.tunnelRadiusMax) },
        { "craters", craterStream.str() }
    });
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
    snapshot.timestamp = std::strtod(kv["ts"].c_str(), nullptr);
    snapshot.avatar.role = sanitizeRole(kv["role"]);
    snapshot.avatar.callsign = sanitizeCallsign(kv["callsign"]);
    snapshot.avatar.radioChannel = normalizeRadioChannel(std::atoi(kv["radio"].c_str()));
    snapshot.avatar.radioTx = kv["ptt"] == "1";
    snapshot.avatar.plane.modelHash = kv["planeModelHash"].empty() ? "builtin-cube" : kv["planeModelHash"];
    snapshot.avatar.plane.scale = std::max(0.1f, std::strtof(kv["planeScale"].c_str(), nullptr));
    snapshot.avatar.plane.skinHash = kv["planeSkinHash"];
    snapshot.avatar.plane.yawDegrees = std::strtof(kv["planeYaw"].c_str(), nullptr);
    snapshot.avatar.plane.pitchDegrees = std::strtof(kv["planePitch"].c_str(), nullptr);
    snapshot.avatar.plane.rollDegrees = std::strtof(kv["planeRoll"].c_str(), nullptr);
    snapshot.avatar.plane.offset = {
        std::strtof(kv["planeOffX"].c_str(), nullptr),
        std::strtof(kv["planeOffY"].c_str(), nullptr),
        std::strtof(kv["planeOffZ"].c_str(), nullptr)
    };
    snapshot.avatar.walking.modelHash = kv["walkingModelHash"].empty() ? snapshot.avatar.plane.modelHash : kv["walkingModelHash"];
    snapshot.avatar.walking.scale = std::max(0.1f, std::strtof(kv["walkingScale"].c_str(), nullptr));
    snapshot.avatar.walking.skinHash = kv["walkingSkinHash"];
    snapshot.avatar.walking.yawDegrees = std::strtof(kv["walkingYaw"].c_str(), nullptr);
    snapshot.avatar.walking.pitchDegrees = std::strtof(kv["walkingPitch"].c_str(), nullptr);
    snapshot.avatar.walking.rollDegrees = std::strtof(kv["walkingRoll"].c_str(), nullptr);
    snapshot.avatar.walking.offset = {
        std::strtof(kv["walkingOffX"].c_str(), nullptr),
        std::strtof(kv["walkingOffY"].c_str(), nullptr),
        std::strtof(kv["walkingOffZ"].c_str(), nullptr)
    };
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
    input.walkYaw = std::strtof(kv["walkYaw"].c_str(), nullptr);
    input.walkPitch = std::strtof(kv["walkPitch"].c_str(), nullptr);
    input.walkMoveSpeed = std::clamp(std::strtof(kv["walkMoveSpeed"].c_str(), nullptr), 2.0f, 30.0f);
    input.throttle = clampNetFloat(std::strtof(kv["throttle"].c_str(), nullptr), 0.0f, 1.0f);
    input.yokePitch = clampNetFloat(std::strtof(kv["yokePitch"].c_str(), nullptr), -1.0f, 1.0f);
    input.yokeYaw = clampNetFloat(std::strtof(kv["yokeYaw"].c_str(), nullptr), -1.0f, 1.0f);
    input.yokeRoll = clampNetFloat(std::strtof(kv["yokeRoll"].c_str(), nullptr), -1.0f, 1.0f);
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
    input.terraformMode = kv["terraformMode"] == "1";
    input.avatar.role = input.role;
    input.avatar.callsign = sanitizeCallsign(kv["callsign"]);
    input.avatar.radioChannel = normalizeRadioChannel(std::atoi(kv["radio"].c_str()));
    input.avatar.radioTx = kv["ptt"] == "1";
    input.avatar.plane.modelHash = kv["planeModelHash"].empty() ? "builtin-cube" : kv["planeModelHash"];
    input.avatar.plane.scale = std::max(0.1f, std::strtof(kv["planeScale"].c_str(), nullptr));
    input.avatar.plane.skinHash = kv["planeSkinHash"];
    input.avatar.plane.yawDegrees = std::strtof(kv["planeYaw"].c_str(), nullptr);
    input.avatar.plane.pitchDegrees = std::strtof(kv["planePitch"].c_str(), nullptr);
    input.avatar.plane.rollDegrees = std::strtof(kv["planeRoll"].c_str(), nullptr);
    input.avatar.plane.offset = {
        std::strtof(kv["planeOffX"].c_str(), nullptr),
        std::strtof(kv["planeOffY"].c_str(), nullptr),
        std::strtof(kv["planeOffZ"].c_str(), nullptr)
    };
    input.avatar.walking.modelHash = kv["walkingModelHash"].empty() ? input.avatar.plane.modelHash : kv["walkingModelHash"];
    input.avatar.walking.scale = std::max(0.1f, std::strtof(kv["walkingScale"].c_str(), nullptr));
    input.avatar.walking.skinHash = kv["walkingSkinHash"];
    input.avatar.walking.yawDegrees = std::strtof(kv["walkingYaw"].c_str(), nullptr);
    input.avatar.walking.pitchDegrees = std::strtof(kv["walkingPitch"].c_str(), nullptr);
    input.avatar.walking.rollDegrees = std::strtof(kv["walkingRoll"].c_str(), nullptr);
    input.avatar.walking.offset = {
        std::strtof(kv["walkingOffX"].c_str(), nullptr),
        std::strtof(kv["walkingOffY"].c_str(), nullptr),
        std::strtof(kv["walkingOffZ"].c_str(), nullptr)
    };
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
    modeSwitch.walkYaw = std::strtof(kv["walkYaw"].c_str(), nullptr);
    modeSwitch.walkPitch = std::strtof(kv["walkPitch"].c_str(), nullptr);
    return modeSwitch;
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
    avatar.plane.modelHash = kv["planeModelHash"].empty() ? "builtin-cube" : kv["planeModelHash"];
    avatar.plane.scale = std::max(0.1f, std::strtof(kv["planeScale"].c_str(), nullptr));
    avatar.plane.skinHash = kv["planeSkinHash"];
    avatar.plane.yawDegrees = std::strtof(kv["planeYaw"].c_str(), nullptr);
    avatar.plane.pitchDegrees = std::strtof(kv["planePitch"].c_str(), nullptr);
    avatar.plane.rollDegrees = std::strtof(kv["planeRoll"].c_str(), nullptr);
    avatar.plane.offset = {
        std::strtof(kv["planeOffX"].c_str(), nullptr),
        std::strtof(kv["planeOffY"].c_str(), nullptr),
        std::strtof(kv["planeOffZ"].c_str(), nullptr)
    };
    avatar.walking.modelHash = kv["walkingModelHash"].empty() ? avatar.plane.modelHash : kv["walkingModelHash"];
    avatar.walking.scale = std::max(0.1f, std::strtof(kv["walkingScale"].c_str(), nullptr));
    avatar.walking.skinHash = kv["walkingSkinHash"];
    avatar.walking.yawDegrees = std::strtof(kv["walkingYaw"].c_str(), nullptr);
    avatar.walking.pitchDegrees = std::strtof(kv["walkingPitch"].c_str(), nullptr);
    avatar.walking.rollDegrees = std::strtof(kv["walkingRoll"].c_str(), nullptr);
    avatar.walking.offset = {
        std::strtof(kv["walkingOffX"].c_str(), nullptr),
        std::strtof(kv["walkingOffY"].c_str(), nullptr),
        std::strtof(kv["walkingOffZ"].c_str(), nullptr)
    };
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

inline bool applyWorldSyncPacket(const std::string& packet, TerrainParams& params)
{
    const std::vector<std::string> fields = splitPacketFields(packet);
    if (fields.empty() || fields[0] != "WORLD_SYNC") {
        return false;
    }

    const auto kv = parseKeyValueFields(fields);
    params.seed = std::max(1, std::atoi(kv["seed"].c_str()));
    params.chunkSize = std::max(8.0f, std::strtof(kv["chunk"].c_str(), nullptr));
    params.lod0Radius = std::max(1, std::atoi(kv["lod0"].c_str()));
    params.lod1Radius = std::max(params.lod0Radius, std::atoi(kv["lod1"].c_str()));
    params.lod2Radius = std::max(params.lod1Radius, std::atoi(kv["lod2"].c_str()));
    params.splitLodEnabled = kv["splitLod"] == "1";
    params.highResSplitRatio = std::clamp(std::strtof(kv["splitRatio"].c_str(), nullptr), 0.1f, 0.95f);
    params.lod0BaseCellSize = std::max(0.25f, std::strtof(kv["lod0Cell"].c_str(), nullptr));
    params.lod1BaseCellSize = std::max(params.lod0BaseCellSize, std::strtof(kv["lod1Cell"].c_str(), nullptr));
    params.lod2BaseCellSize = std::max(params.lod1BaseCellSize, std::strtof(kv["lod2Cell"].c_str(), nullptr));
    params.terrainQuality = std::clamp(std::strtof(kv["quality"].c_str(), nullptr), 0.25f, 8.0f);
    params.meshBuildBudget = std::max(1, std::atoi(kv["meshBudget"].c_str()));
    params.workerMaxInflight = std::max(1, std::atoi(kv["inflight"].c_str()));
    params.chunkCacheLimit = std::max(8, std::atoi(kv["cache"].c_str()));
    params.autoQualityEnabled = kv["autoQuality"] == "1";
    params.targetFrameMs = std::max(1.0f, std::strtof(kv["targetFrameMs"].c_str(), nullptr));
    params.heightAmplitude = std::strtof(kv["heightAmp"].c_str(), nullptr);
    params.heightFrequency = std::max(1.0e-5f, std::strtof(kv["heightFreq"].c_str(), nullptr));
    params.caveEnabled = kv["caveEnabled"] == "1";
    params.caveStrength = std::max(0.0f, std::strtof(kv["caveStrength"].c_str(), nullptr));
    params.tunnelCount = std::max(0, std::atoi(kv["tunnelCount"].c_str()));
    params.tunnelRadiusMin = std::max(0.1f, std::strtof(kv["tunnelRadiusMin"].c_str(), nullptr));
    params.tunnelRadiusMax = std::max(params.tunnelRadiusMin, std::strtof(kv["tunnelRadiusMax"].c_str(), nullptr));
    params.dynamicCraters.clear();

    const std::string craterField = kv["craters"];
    for (const std::string& craterToken : splitPacketFields(craterField, ';')) {
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
        params.dynamicCraters.push_back(crater);
    }

    params = normalizeTerrainParams(params);
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
