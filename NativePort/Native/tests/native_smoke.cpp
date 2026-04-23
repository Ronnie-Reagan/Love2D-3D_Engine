#include <SDL3/SDL.h>

#include "App/AppSmokeApi.hpp"
#include "NativeGame/BlobSync.hpp"
#include "NativeGame/HostedNetworking.hpp"
#include "NativeGame/NetProtocol.hpp"
#include "NativeGame/NetTransport.hpp"
#include "NativeGame/WorldStore.hpp"
#include "NativeGame/WorldWire.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

namespace {

using namespace NativeGame;
using namespace TrueFlightApp;

std::filesystem::path repoRoot()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

std::filesystem::path smokeModelPath()
{
    const auto root = repoRoot();
    const auto glbPath = root / "portSource/Assets/Models/DualEngine.glb";
    if (std::filesystem::exists(glbPath)) {
        return glbPath;
    }

    const auto stlPath = root / "portSource/Assets/Models/DualEngine.stl";
    if (std::filesystem::exists(stlPath)) {
        return stlPath;
    }

    return {};
}

void require(bool condition, const std::string& message, bool& failed)
{
    if (condition) {
        return;
    }

    failed = true;
    std::cerr << "FAIL: " << message << "\n";
}

int gKnownIssueCount = 0;

void requireKnownIssue(bool condition, const std::string& message)
{
    if (condition) {
        return;
    }

    ++gKnownIssueCount;
    std::cerr << "KNOWN ISSUE: " << message << "\n";
}

class RecordingTransport final : public INetTransport {
public:
    explicit RecordingTransport(std::vector<NetPeerId> peers)
        : peers_(std::move(peers))
    {
        for (NetPeerId peerId : peers_) {
            pendingEvents_.push_back({ NetEvent::Type::Connected, peerId, 0, true, {} });
        }
    }

    struct SentPacket {
        NetPeerId peerId = 0;
        int lane = 0;
        bool reliable = false;
        std::string payload;
    };

    [[nodiscard]] bool ready() const override
    {
        return true;
    }

    bool send(NetPeerId peerId, int lane, std::string_view payload, bool reliable) override
    {
        sentPackets.push_back({ peerId, lane, reliable, std::string(payload) });
        const bool knownPeer = std::find(peers_.begin(), peers_.end(), peerId) != peers_.end();
        if (!knownPeer) {
            return false;
        }
        return sendBehavior ? sendBehavior(peerId, lane, payload, reliable) : true;
    }

    void disconnectPeer(NetPeerId peerId) override
    {
        const auto peerIt = std::find(peers_.begin(), peers_.end(), peerId);
        if (peerIt == peers_.end()) {
            return;
        }
        pendingEvents_.push_back({ NetEvent::Type::Disconnected, peerId, 0, true, {} });
        peers_.erase(peerIt);
    }

    [[nodiscard]] std::vector<NetEvent> poll() override
    {
        std::vector<NetEvent> events;
        while (!pendingEvents_.empty()) {
            events.push_back(std::move(pendingEvents_.front()));
            pendingEvents_.pop_front();
        }
        return events;
    }

    [[nodiscard]] std::vector<NetPeerId> peers() const override
    {
        return peers_;
    }

    void pushMessage(NetPeerId peerId, int lane, std::string payload, bool reliable)
    {
        pendingEvents_.push_back({ NetEvent::Type::Message, peerId, lane, reliable, std::move(payload) });
    }

    void clearSentPackets()
    {
        sentPackets.clear();
    }

    std::vector<SentPacket> sentPackets;
    std::function<bool(NetPeerId, int, std::string_view, bool)> sendBehavior;

private:
    std::vector<NetPeerId> peers_;
    std::deque<NetEvent> pendingEvents_;
};

template <typename ServerT>
auto collectHostLocalVoiceFrames(ServerT& server)
{
    if constexpr (requires { server.drainLocalVoiceFrames(); }) {
        return server.drainLocalVoiceFrames();
    } else if constexpr (requires { server.takeLocalVoiceFrames(); }) {
        return server.takeLocalVoiceFrames();
    } else if constexpr (requires { server.consumeLocalVoiceFrames(); }) {
        return server.consumeLocalVoiceFrames();
    } else if constexpr (requires { server.receivedLocalVoiceFrames(); }) {
        return server.receivedLocalVoiceFrames();
    } else if constexpr (requires { server.pendingLocalVoiceFrames(); }) {
        return server.pendingLocalVoiceFrames();
    } else if constexpr (requires { server.localVoiceFrames(); }) {
        return server.localVoiceFrames();
    } else if constexpr (requires { server.hostVoiceFrames(); }) {
        return server.hostVoiceFrames();
    } else if constexpr (requires { server.hostLocalVoiceFrames(); }) {
        return server.hostLocalVoiceFrames();
    } else if constexpr (requires { server.voiceFrames(); }) {
        return server.voiceFrames();
    } else {
        static_assert(sizeof(ServerT) == 0, "HostedWorldServer is missing a host-local voice frame accessor");
    }
}

inline std::string voiceFramePayloadText(const std::string& frame)
{
    return frame;
}

template <typename FrameT>
std::string voiceFramePayloadText(const FrameT& frame)
{
    if constexpr (requires { frame.data; }) {
        return frame.data;
    } else if constexpr (requires { frame.compressedData; }) {
        return frame.compressedData;
    } else if constexpr (requires { frame.payload; }) {
        return frame.payload;
    } else {
        return {};
    }
}

inline int voiceFrameSenderId(const std::string& frame)
{
    const auto parsed = parseVoiceFramePacket(frame);
    return parsed.has_value() ? parsed->senderId : 0;
}

template <typename FrameT>
int voiceFrameSenderId(const FrameT& frame)
{
    if constexpr (requires { frame.senderId; }) {
        return frame.senderId;
    } else {
        return voiceFrameSenderId(voiceFramePayloadText(frame));
    }
}

inline int voiceFrameChannel(const std::string& frame)
{
    const auto parsed = parseVoiceFramePacket(frame);
    return parsed.has_value() ? parsed->channel : 0;
}

template <typename FrameT>
int voiceFrameChannel(const FrameT& frame)
{
    if constexpr (requires { frame.channel; }) {
        return frame.channel;
    } else {
        return voiceFrameChannel(voiceFramePayloadText(frame));
    }
}

HostedPlayerState* mutableHostedPlayer(HostedWorldServer& server, int playerId)
{
    auto& players = const_cast<std::unordered_map<int, HostedPlayerState>&>(server.players());
    const auto it = players.find(playerId);
    return it == players.end() ? nullptr : &it->second;
}

struct FlightRunResult {
    FlightState plane;
    FlightRuntimeState runtime;
};

FlightState makeFlightState(
    const Vec3& pos = { 0.0f, 250.0f, 0.0f },
    const Vec3& vel = { 0.0f, 0.0f, 35.0f },
    float throttle = 0.62f,
    float collisionRadius = 1.2f)
{
    FlightState plane {};
    plane.pos = pos;
    plane.rot = quatIdentity();
    plane.flightVel = vel;
    plane.vel = vel;
    plane.throttle = throttle;
    plane.collisionRadius = collisionRadius;
    return plane;
}

ConstructPublishPacket makeSmokeConstructPublishPacket(
    ConstructId constructId = 77,
    ConstructRevisionId revisionId = 5,
    int authorPlayerId = 2)
{
    ConstructPublishPacket publish;
    publish.blueprint.constructId = constructId;
    publish.blueprint.revisionId = revisionId;
    publish.blueprint.label = "smoke_buggy";
    publish.blueprint.bodyHalfExtents = { 1.55f, 0.75f, 2.85f };
    publish.blueprint.seatOffsets = {
        { 0.0f, 1.25f, 0.85f },
        { 0.0f, 1.25f, -0.85f }
    };
    publish.blueprint.maxSpeedMps = 42.0f;
    publish.blueprint.accelerationMps2 = 17.5f;
    publish.blueprint.brakingMps2 = 24.0f;
    publish.blueprint.steeringRate = 1.95f;
    publish.blueprint.steeringAssist = 0.78f;
    publish.blueprint.roadGrip = 1.08f;
    publish.blueprint.offroadGrip = 0.52f;
    publish.blueprint.maxHealth = 260.0f;
    publish.blueprint.suspensionTravel = 0.34f;

    publish.state.constructId = constructId;
    publish.state.revisionId = revisionId;
    publish.state.authorPlayerId = authorPlayerId;
    publish.state.published = true;
    publish.state.label = publish.blueprint.label;
    return publish;
}

SharedVehicleState makeSmokeVehicleState(
    VehicleId vehicleId = 41,
    const ConstructBlueprint& blueprint = makeSmokeConstructPublishPacket().blueprint)
{
    SharedVehicleState vehicle;
    vehicle.vehicleId = vehicleId;
    vehicle.entityId = 900 + vehicleId;
    vehicle.constructId = blueprint.constructId;
    vehicle.constructRevisionId = blueprint.revisionId;
    vehicle.authoritativeOwnerPlayerId = 1;
    vehicle.driverPlayerId = 0;
    vehicle.seatOccupants.assign(std::max<std::size_t>(std::size_t { 2 }, blueprint.seatOffsets.size()), 0);
    vehicle.pos = { 84.0f, 12.5f, -36.0f };
    vehicle.vel = { 6.5f, 0.0f, 14.0f };
    vehicle.yawRadians = radians(38.0f);
    vehicle.steerNormalized = -0.35f;
    vehicle.throttleNormalized = 0.65f;
    vehicle.roadAdhesion = 0.82f;
    vehicle.surfaceFriction = 0.96f;
    vehicle.health = 214.0f;
    vehicle.maxHealth = blueprint.maxHealth;
    vehicle.cage.heave = 0.18f;
    vehicle.cage.pitch = -0.04f;
    vehicle.cage.roll = 0.07f;
    vehicle.cage.wheelCompression = { 0.14f, 0.19f, 0.11f, 0.17f };
    vehicle.active = true;
    return vehicle;
}

FlightRunResult runFlightSim(float frameDt, float totalTime, const FlightConfig& config)
{
    FlightRunResult result;
    result.plane = makeFlightState();

    FlightEnvironment environment {};
    environment.wind = { 0.0f, 0.0f, 0.0f };
    environment.groundHeightAt = [](float, float) {
        return -1.0e6f;
    };
    environment.sampleSdf = [](float, float, float) {
        return 1.0e6f;
    };
    environment.sampleNormal = [](float, float, float) {
        return Vec3 { 0.0f, 1.0f, 0.0f };
    };
    environment.collisionRadius = result.plane.collisionRadius;

    float elapsed = 0.0f;
    while (elapsed < totalTime) {
        const float dt = std::min(frameDt, totalTime - elapsed);
        stepFlight(result.plane, result.runtime, dt, elapsed, InputState {}, environment, config);
        elapsed += dt;
    }

    return result;
}

void runFlightParityChecks(bool& failed)
{
    {
        const FlightConfig config = defaultFlightConfig();
        require(std::fabs(config.CL0 - 0.24f) < 1.0e-6f, "Default CL0 no longer matches the light-aircraft baseline", failed);
        require(std::fabs(config.CLElevator - 0.92f) < 1.0e-6f, "Default CLElevator no longer matches the light-aircraft baseline", failed);
        require(std::fabs(config.Cm0 - 0.03f) < 1.0e-6f, "Default Cm0 no longer matches the light-aircraft baseline", failed);
        require(std::fabs(config.CmElevator + 1.60f) < 1.0e-6f, "Default CmElevator no longer matches the light-aircraft baseline", failed);
        require(std::fabs(degrees(config.maxElevatorDeflectionRad) - 25.0f) < 1.0e-4f, "Default elevator limit no longer matches the light-aircraft baseline", failed);
        require(std::fabs(config.pitchControlScale - 0.68f) < 1.0e-6f, "Default pitch control scale no longer matches the light-aircraft baseline", failed);
        require(std::fabs(config.rollControlScale - 0.58f) < 1.0e-6f, "Default roll control scale no longer matches the light-aircraft baseline", failed);
        require(std::fabs(config.yawControlScale - 0.52f) < 1.0e-6f, "Default yaw control scale no longer matches the light-aircraft baseline", failed);
    }

    {
        std::array<FlightRunResult, 4> outcomes {};
        const std::array<int, 4> frameRates { 30, 60, 120, 180 };

        for (std::size_t index = 0; index < frameRates.size(); ++index) {
            FlightConfig config = defaultFlightConfig();
            config.enableAutoTrim = true;
            config.autoTrimUseWorker = false;

            // At 180 Hz physics a 15 Hz frame would need ~12 substeps, but the default
            // FlightConfig only allows 8. Keep this smoke test inside the supported range.
            config.maxSubsteps = std::max(config.maxSubsteps, 8);

            outcomes[index] = runFlightSim(1.0f / static_cast<float>(frameRates[index]), 60.0f, config);

            require(outcomes[index].runtime.tick > 0, "Flight dt invariance run failed to advance simulation ticks", failed);
        }

        const FlightRunResult& reference = outcomes.back();
        const int refTick = reference.runtime.tick;
        const float refSpeed = length(reference.plane.flightVel);
        const float refAltitude = reference.plane.pos.y;

        for (std::size_t index = 0; index < frameRates.size(); ++index) {
            const FlightRunResult& result = outcomes[index];
            const int tickDelta = std::abs(result.runtime.tick - refTick);
            require(tickDelta <= 3, "Native dt invariance tick drift exceeded supported fixed-step tolerance", failed);

            const float speed = length(result.plane.flightVel);
            const float altitude = result.plane.pos.y;
            const float speedDiff = std::fabs(speed - refSpeed) / std::max(1.0f, refSpeed);
            const float altitudeDiff = std::fabs(altitude - refAltitude) / std::max(1.0f, std::fabs(refAltitude));

            require(speedDiff < 0.05f, "Native dt invariance speed drift exceeded supported fixed-step tolerance", failed);
            require(altitudeDiff < 0.06f, "Native dt invariance altitude drift exceeded supported fixed-step tolerance", failed);
            require(speed < 140.0f, "Native dt invariance run hit runaway airspeed", failed);
        }
    }

    {
        FlightConfig config = defaultFlightConfig();
        config.enableAutoTrim = true;
        config.autoTrimUseWorker = false;
        config.massKg = 620.0f;
        config.maxThrustSeaLevel = 12000.0f;
        config.CLalpha = 8.6f;
        config.CD0 = 0.012f;
        config.inducedDragK = 0.028f;
        config.maxLinearSpeed = 260.0f;
        config.maxAngularRateRad = radians(320.0f);
        config.maxForceNewton = 130000.0f;
        config.maxMomentNewtonMeter = 180000.0f;

        const FlightRunResult result = runFlightSim(1.0f / 60.0f, 45.0f, config);
        require(length(result.plane.flightVel) <= 261.0f, "Native extreme-config stability no longer matches Lua guardrails", failed);
        require(!result.runtime.crashed, "Native extreme-config stability unexpectedly triggered a crash", failed);
    }

    {
        FlightConfig config = defaultFlightConfig();
        config.enableAutoTrim = true;
        config.autoTrimUseWorker = false;

        const FlightRunResult result = runFlightSim(1.0f / 60.0f, 60.0f, config);
        require(!result.runtime.crashed, "Zero-wind straight-flight regression unexpectedly crashed", failed);
        require(std::fabs(result.runtime.lastBeta) < radians(6.0f), "Zero-wind straight-flight beta exceeded the bounded-slip target", failed);
        require(std::fabs(result.plane.pos.x) < 160.0f, "Zero-wind straight-flight lateral drift exceeded the bounded target", failed);
        require(std::fabs(result.plane.flightAngVel.y) < radians(12.0f), "Zero-wind straight-flight yaw rate diverged", failed);
        require(std::fabs(result.plane.flightAngVel.z) < radians(25.0f), "Zero-wind straight-flight roll rate diverged", failed);
    }

    {
        FlightConfig config = defaultFlightConfig();
        config.enableAutoTrim = false;
        config.autoTrimUseWorker = false;
        config.maxThrustSeaLevel = 0.0f;
        config.crashEnabled = false;

        FlightState plane = makeFlightState({ 0.0f, 190.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 1.2f);
        FlightRuntimeState runtime {};
        FlightEnvironment environment {};
        environment.worldShape = WorldShape::Planet;
        environment.planet = PlanetConfig {};
        environment.planetCenterWorld = { 0.0f, static_cast<float>(-environment.planet.radiusMeters), 0.0f };
        environment.planetSpinAxisWorld = { 0.0f, 0.0f, 1.0f };
        environment.wind = { 0.0f, 0.0f, 0.0f };
        environment.groundHeightAt = [](float, float) {
            return -1.0e6f;
        };
        environment.sampleSdf = [](float, float, float) {
            return 1.0e6f;
        };
        environment.sampleNormal = [](float, float, float) {
            return Vec3 { 0.0f, 1.0f, 0.0f };
        };
        environment.collisionRadius = plane.collisionRadius;

        stepFlight(plane, runtime, 1.0f / 120.0f, 0.0f, InputState {}, environment, config);

        require(
            runtime.lastDynamicPressure < 1.0f,
            "Planet-fixed flight should not treat Earth rotation as a constant atmospheric crosswind",
            failed);
        require(
            length(plane.flightVel) < 1.0f,
            "Planet-fixed flight should only pick up a small gravity step from rest",
            failed);
    }

    {
        FlightConfig config = defaultFlightConfig();
        config.enableAutoTrim = false;
        config.autoTrimUseWorker = false;
        config.maxThrustSeaLevel = 0.0f;

        FlightState plane = makeFlightState({ 0.0f, 2.8f, 0.0f }, { 0.0f, -2.5f, 22.0f }, 0.0f, 1.2f);
        FlightRuntimeState runtime {};
        FlightEnvironment environment {};
        environment.wind = { 0.0f, 0.0f, 0.0f };
        environment.groundHeightAt = [](float, float) {
            return 0.0f;
        };
        environment.sampleSdf = [](float, float y, float) {
            return y;
        };
        environment.sampleNormal = [](float, float, float) {
            return Vec3 { 0.0f, 1.0f, 0.0f };
        };
        environment.collisionRadius = plane.collisionRadius;

        for (int step = 0; step < 500 && !plane.onGround; ++step) {
            stepFlight(plane, runtime, 1.0f / 120.0f, static_cast<float>(step) / 120.0f, InputState {}, environment, config);
        }

        require(plane.onGround, "Lua-style low-speed touchdown no longer settles on the ground in native flight", failed);
        require(!runtime.crashed, "Lua-style low-speed touchdown should not register as a crash", failed);
        require(plane.pos.y >= 1.0f && plane.pos.y <= 2.6f, "Lua-style touchdown height drifted in native flight", failed);
    }

    {
        FlightConfig config = defaultFlightConfig();
        config.enableAutoTrim = false;
        config.autoTrimUseWorker = false;
        config.maxThrustSeaLevel = 0.0f;

        FlightState plane = makeFlightState({ 0.0f, 8.5f, 0.0f }, { 0.0f, -45.0f, 55.0f }, 0.0f, 1.2f);
        FlightRuntimeState runtime {};
        FlightEnvironment environment {};
        environment.wind = { 0.0f, 0.0f, 0.0f };
        environment.groundHeightAt = [](float, float) {
            return 0.0f;
        };
        environment.sampleSdf = [](float, float y, float) {
            return y;
        };
        environment.sampleNormal = [](float, float, float) {
            return Vec3 { 0.0f, 1.0f, 0.0f };
        };
        environment.collisionRadius = plane.collisionRadius;

        for (int step = 0; step < 360 && !runtime.crashed; ++step) {
            stepFlight(plane, runtime, 1.0f / 120.0f, static_cast<float>(step) / 120.0f, InputState {}, environment, config);
        }
        std::cerr
            << "post-crash speed=" << length(plane.flightVel)
            << " post-crash ang=" << degrees(length(plane.flightAngVel))
            << "deg/s"
            << " pos=(" << plane.pos.x << ", " << plane.pos.y << ", " << plane.pos.z << ")"
            << " onGround=" << (plane.onGround ? 1 : 0)
            << "\n";
        require(runtime.crashed, "Lua-style high-speed terrain impact no longer crashes in native flight", failed);
        require(length(plane.flightVel) < 37.0f, "Crash handling should heavily damp linear velocity after impact", failed);
        require(length(plane.flightAngVel) < radians(8.0f), "Crash handling should heavily damp angular velocity after impact", failed);
    }

    {
        constexpr float slope = 0.12f;
        const Vec3 slopeNormal = normalize({ -slope, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });

        FlightConfig config = defaultFlightConfig();
        config.enableAutoTrim = false;
        config.autoTrimUseWorker = false;
        config.maxThrustSeaLevel = 0.0f;
        config.crashEnabled = false;

        FlightState plane = makeFlightState({ 10.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, 0.0f, 1.2f);
        FlightRuntimeState runtime {};
        FlightEnvironment environment {};
        environment.wind = { 0.0f, 0.0f, 0.0f };
        environment.groundHeightAt = [slope](float x, float) {
            return slope * x;
        };
        environment.sampleSdf = [slope](float x, float y, float) {
            return y - (slope * x);
        };
        environment.sampleNormal = [slopeNormal](float, float, float) {
            return slopeNormal;
        };
        environment.collisionRadius = plane.collisionRadius;

        stepFlight(plane, runtime, 1.0f / 120.0f, 0.0f, InputState {}, environment, config);

        const float signedDistance = environment.sampleSdf(plane.pos.x, plane.pos.y, plane.pos.z);
        require(signedDistance >= (plane.collisionRadius - 0.05f), "Slope contact no longer resolves to collision radius like the Lua sim", failed);
        require(plane.onGround, "Slope contact should still report onGround in native flight", failed);
    }

    {
        FlightConfig config = defaultFlightConfig();
        config.enableAutoTrim = false;
        config.autoTrimUseWorker = false;

        const float clMaxEstimate = config.CL0 + (config.CLalpha * config.alphaStallRad);
        const float stallSpeedMetersPerSecond =
            std::sqrt((2.0f * config.massKg * 9.80665f) / (1.225f * config.wingArea * std::max(0.5f, clMaxEstimate)));

        require(config.engineCylinderCount == 4, "Default native aircraft should use a four-cylinder light-aircraft engine baseline", failed);
        require(
            config.maxBrakePowerKw >= 130.0f && config.maxBrakePowerKw <= 140.0f,
            "Default native aircraft brake power drifted away from the Skyhawk baseline",
            failed);
        require(
            config.maxThrustSeaLevel >= 1500.0f && config.maxThrustSeaLevel <= 2200.0f,
            "Default native aircraft thrust cap drifted away from the light-aircraft baseline",
            failed);
        require(!config.stabilityAugmentation, "Default native aircraft should no longer ship with synthetic stability augmentation enabled", failed);
        require(
            stallSpeedMetersPerSecond >= 23.0f && stallSpeedMetersPerSecond <= 27.0f,
            "Default native aircraft clean stall speed drifted away from the Cessna-like baseline",
            failed);
    }

    {
        std::mt19937 rng(42u);
        WindState wind {};
        for (int sampleIndex = 0; sampleIndex < 96; ++sampleIndex) {
            pickNextWindTarget(wind, rng, static_cast<float>(sampleIndex) * 10.0f);
            require(
                wind.targetSpeed >= 0.8f && wind.targetSpeed <= 6.0f,
                "Default wind targets should stay within the reduced light-aircraft operating range",
                failed);
            require(
                wind.gustAmplitude >= 0.05f && wind.gustAmplitude <= 1.1f,
                "Default wind gust amplitude drifted outside the reduced operating range",
                failed);
            require(
                wind.gustFrequency >= 0.05f && wind.gustFrequency <= 0.18f,
                "Default wind gust frequency drifted outside the reduced operating range",
                failed);
        }
    }

    {
        FlightConfig config = defaultFlightConfig();
        config.enableAutoTrim = false;
        config.autoTrimUseWorker = false;

        FlightState plane = makeFlightState();
        plane.manualElevatorTrim = radians(2.5f);
        FlightRuntimeState runtime {};
        FlightEnvironment environment {};
        environment.wind = { 0.0f, 0.0f, 0.0f };
        environment.groundHeightAt = [](float, float) {
            return -1.0e6f;
        };
        environment.sampleSdf = [](float, float, float) {
            return 1.0e6f;
        };
        environment.sampleNormal = [](float, float, float) {
            return Vec3 { 0.0f, 1.0f, 0.0f };
        };
        environment.collisionRadius = plane.collisionRadius;

        stepFlight(plane, runtime, 1.0f / 120.0f, 0.0f, InputState {}, environment, config);

        require(
            std::fabs(runtime.elevatorDeflection - plane.manualElevatorTrim) < radians(0.1f),
            "Manual trim should feed the native elevator target",
            failed);
    }

    {
        FlightConfig config = defaultFlightConfig();
        config.enableAutoTrim = false;
        config.autoTrimUseWorker = false;

        FlightState plane = makeFlightState();
        plane.manualRudderTrim = radians(3.0f);
        FlightRuntimeState runtime {};
        FlightEnvironment environment {};
        environment.wind = { 0.0f, 0.0f, 0.0f };
        environment.groundHeightAt = [](float, float) {
            return -1.0e6f;
        };
        environment.sampleSdf = [](float, float, float) {
            return 1.0e6f;
        };
        environment.sampleNormal = [](float, float, float) {
            return Vec3 { 0.0f, 1.0f, 0.0f };
        };
        environment.collisionRadius = plane.collisionRadius;

        stepFlight(plane, runtime, 1.0f / 120.0f, 0.0f, InputState {}, environment, config);

        require(
            std::fabs(runtime.rudderDeflection - plane.manualRudderTrim) < radians(0.1f),
            "Manual rudder trim should feed the native rudder target",
            failed);
    }

    {
        FlightConfig config = defaultFlightConfig();
        config.enableAutoTrim = false;
        config.autoTrimUseWorker = false;

        FlightState plane = makeFlightState();
        FlightRuntimeState runtime {};
        FlightEnvironment environment {};
        environment.wind = { 0.0f, 0.0f, 0.0f };
        environment.groundHeightAt = [](float, float) {
            return -1.0e6f;
        };
        environment.sampleSdf = [](float, float, float) {
            return 1.0e6f;
        };
        environment.sampleNormal = [](float, float, float) {
            return Vec3 { 0.0f, 1.0f, 0.0f };
        };
        environment.collisionRadius = plane.collisionRadius;

        InputState input {};
        input.flightYawRight = true;
        for (int step = 0; step < 18; ++step) {
            stepFlight(plane, runtime, 1.0f / 120.0f, static_cast<float>(step) / 120.0f, input, environment, config);
        }
        require(plane.yoke.yaw > 0.85f, "Rudder pedal input should reach near-full authority quickly", failed);

        input = {};
        for (int step = 18; step < 48; ++step) {
            stepFlight(plane, runtime, 1.0f / 120.0f, static_cast<float>(step) / 120.0f, input, environment, config);
        }
        require(std::fabs(plane.yoke.yaw) < 0.15f, "Rudder pedal input should spring back toward center after release", failed);
    }

    {
        FlightConfig config = defaultFlightConfig();
        config.enableAutoTrim = false;
        config.autoTrimUseWorker = false;

        FlightState plane = makeFlightState({ 0.0f, 420.0f, 0.0f }, { 0.0f, 0.0f, 32.0f }, 0.48f, 1.2f);
        plane.rot = quatNormalize(quatFromAxisAngle({ 1.0f, 0.0f, 0.0f }, radians(24.0f)));

        FlightRuntimeState runtime {};
        FlightEnvironment environment {};
        environment.wind = { 0.0f, 0.0f, 0.0f };
        environment.groundHeightAt = [](float, float) {
            return -1.0e6f;
        };
        environment.sampleSdf = [](float, float, float) {
            return 1.0e6f;
        };
        environment.sampleNormal = [](float, float, float) {
            return Vec3 { 0.0f, 1.0f, 0.0f };
        };
        environment.collisionRadius = plane.collisionRadius;

        float drivenPhaseMaxAlpha = 0.0f;
        float drivenPhaseMaxYawRate = 0.0f;
        float drivenPhaseMaxRollRate = 0.0f;
        float drivenPhaseMaxBeta = 0.0f;

        float neutralPhaseMaxAlpha = 0.0f;
        float neutralPhaseMaxYawRate = 0.0f;
        float neutralPhaseMaxRollRate = 0.0f;
        float neutralPhaseMaxBeta = 0.0f;

        for (int step = 0; step < 360; ++step) {
            InputState input {};
            if (step < 120) {
                input.flightPitchUp = true;
                input.flightYawRight = true;
                input.flightRollRight = true;
            }

            stepFlight(plane, runtime, 1.0f / 120.0f, static_cast<float>(step) / 120.0f, input, environment, config);

            if (step < 120) {
                drivenPhaseMaxAlpha = std::max(drivenPhaseMaxAlpha, std::fabs(runtime.lastAlpha));
                drivenPhaseMaxYawRate = std::max(drivenPhaseMaxYawRate, std::fabs(plane.flightAngVel.y));
                drivenPhaseMaxRollRate = std::max(drivenPhaseMaxRollRate, std::fabs(plane.flightAngVel.z));
                drivenPhaseMaxBeta = std::max(drivenPhaseMaxBeta, std::fabs(runtime.lastBeta));
            } else {
                neutralPhaseMaxAlpha = std::max(neutralPhaseMaxAlpha, std::fabs(runtime.lastAlpha));
                neutralPhaseMaxYawRate = std::max(neutralPhaseMaxYawRate, std::fabs(plane.flightAngVel.y));
                neutralPhaseMaxRollRate = std::max(neutralPhaseMaxRollRate, std::fabs(plane.flightAngVel.z));
                neutralPhaseMaxBeta = std::max(neutralPhaseMaxBeta, std::fabs(runtime.lastBeta));
            }
        }

        require(drivenPhaseMaxAlpha > config.alphaStallRad, "Post-stall setup failed to drive the aircraft beyond stall alpha", failed);
        require(drivenPhaseMaxYawRate > radians(8.0f), "Post-stall setup failed to generate meaningful yaw rate", failed);
        require(drivenPhaseMaxRollRate > radians(8.0f), "Post-stall setup failed to generate meaningful roll rate", failed);
        require(drivenPhaseMaxBeta > radians(4.0f), "Post-stall setup failed to generate meaningful sideslip", failed);

        require(neutralPhaseMaxAlpha > radians(8.0f), "Post-stall release should remain aerodynamically disturbed for a short period", failed);
        require(
            neutralPhaseMaxYawRate > radians(4.0f) ||
            neutralPhaseMaxRollRate > radians(6.0f) ||
            neutralPhaseMaxBeta > radians(3.0f),
            "Post-stall release should retain some disturbed motion with the current default tuning",
            failed);
    }
}

static float avg(const std::array<float, NativeGame::kMaxFlightEngines>& v, int count)
{
    const int c = std::max(1, std::min(count, NativeGame::kMaxFlightEngines));
    float t = 0.0f;
    for (int i = 0; i < c; ++i) t += v[i];
    return t / static_cast<float>(c);
}

void runPlanetMathChecks(bool& failed)
{
    PlanetConfig planet {};
    planet.radiusMeters = 6371000.0;
    planet.gravitationalParameter = 3.986004418e14;
    planet.rotationRateRadPerSec = 7.2921159e-5;
    planet.atmosphereHeightMeters = 120000.0;
    planet.localOrigin = { 47.6062, -122.3321, 125.0 };

    const GeodeticCoord fixedRoundTrip = geodeticFromPlanetFixed(planetFixedFromGeodetic(planet.localOrigin, planet), planet);
    require(std::fabs(fixedRoundTrip.latitudeDeg - planet.localOrigin.latitudeDeg) < 1.0e-4, "Planet fixed/geodetic latitude round-trip drifted", failed);
    require(std::fabs(fixedRoundTrip.longitudeDeg - planet.localOrigin.longitudeDeg) < 1.0e-4, "Planet fixed/geodetic longitude round-trip drifted", failed);
    require(std::fabs(fixedRoundTrip.altitudeMeters - planet.localOrigin.altitudeMeters) < 1.0e-3, "Planet fixed/geodetic altitude round-trip drifted", failed);

    const PlanetLocalFrame frame = makePlanetLocalFrame(planet);
    const Vec3 localSample { 1200.0f, 850.0f, -2400.0f };
    const DVec3 inertial = planetLocalToInertial(localSample, frame);
    const Vec3 localRoundTrip = inertialToPlanetLocal(inertial, frame);
    require(length(localRoundTrip - localSample) < 0.01f, "Planet local/inertial transform round-trip drifted", failed);

    const DVec3 fixedAtTime = inertialToPlanetFixed(inertial, planet, 480.0);
    const DVec3 inertialRoundTrip = planetFixedToInertial(fixedAtTime, planet, 480.0);
    require(length(inertialRoundTrip - inertial) < 0.01, "Planet inertial/fixed transform round-trip drifted", failed);

    TerrainParams params = defaultTerrainParams();
    params.worldShape = WorldShape::Planet;
    params.planet = planet;
    params.chunkSize = 256.0f;
    params.horizonRadiusMeters = 240000.0f;
    const TerrainFieldContext terrainContext = createTerrainFieldContext(params);
    const float surfaceY = sampleSurfaceHeight(0.0f, 0.0f, terrainContext);
    const Vec2 spawnGeo = worldToGeo(Vec3 { 0.0f, surfaceY, 0.0f }, terrainContext);
    require(std::fabs(static_cast<double>(spawnGeo.x) - planet.localOrigin.latitudeDeg) < 0.02, "Planet terrain geo latitude did not align with the configured origin", failed);
    require(std::fabs(static_cast<double>(spawnGeo.y) - planet.localOrigin.longitudeDeg) < 0.02, "Planet terrain geo longitude did not align with the configured origin", failed);

    FlightEnvironment environment {};
    environment.worldShape = WorldShape::Planet;
    environment.planet = planet;
    environment.planetCenterWorld = planetCenterLocal(frame);
    environment.planetSpinAxisWorld = planetSpinAxisLocal(frame);
    const Vec3 gravity = computePlanetGravityAcceleration(environment, Vec3 { 0.0f, 1000.0f, 0.0f });
    require(gravity.y < -9.0f, "Planet gravity should point toward the globe center in the local frame", failed);
    require(std::fabs(length(gravity) - 9.8f) < 0.4f, "Planet gravity magnitude drifted away from Earth-like surface gravity", failed);

    WorldInfoSnapshot worldInfo {};
    worldInfo.worldId = "planet-smoke";
    worldInfo.worldShape = WorldShape::Planet;
    worldInfo.planet = planet;
    worldInfo.spawnLatitudeDeg = planet.localOrigin.latitudeDeg;
    worldInfo.spawnLongitudeDeg = planet.localOrigin.longitudeDeg;
    worldInfo.spawnAltitudeMeters = 1200.0;
    const auto parsedWorldInfo = parseWorldInfoPacket(buildWorldInfoPacket(worldInfo));
    require(parsedWorldInfo.has_value(), "Planet world-info packet did not parse", failed);
    if (parsedWorldInfo.has_value()) {
        require(parsedWorldInfo->worldShape == WorldShape::Planet, "Planet world-info packet lost the world shape", failed);
        require(std::fabs(parsedWorldInfo->planet.radiusMeters - planet.radiusMeters) < 2.0, "Planet world-info packet lost the radius", failed);
        require(std::fabs(parsedWorldInfo->spawnLatitudeDeg - planet.localOrigin.latitudeDeg) < 1.0e-3, "Planet world-info packet lost spawn latitude", failed);
        require(std::fabs(parsedWorldInfo->spawnLongitudeDeg - planet.localOrigin.longitudeDeg) < 1.0e-3, "Planet world-info packet lost spawn longitude", failed);
    }
}

void runAudioFrameChecks(bool& failed)
{
    UiState uiState {};
    TerrainFieldContext terrainContext = createTerrainFieldContext(defaultTerrainParams());
    FlightConfig flightConfig = defaultFlightConfig();
    PropAudioConfig propAudioConfig = defaultPropAudioConfig();
    propAudioConfig.baseRpm = 42.0f;
    propAudioConfig.engineFrequencyScale = 1.15f;
    propAudioConfig.ambienceFrequencyScale = 0.85f;

    FlightState plane = makeFlightState(
        { 0.0f, terrainContext.params.waterLevel + 12.0f, 0.0f },
        { 18.0f, -6.0f, 92.0f },
        0.62f,
        1.2f);
    plane.rot = quatNormalize(quatFromAxisAngle({ 0.0f, 1.0f, 0.0f }, radians(8.0f)));
    plane.flightAngVel = { radians(12.0f), radians(4.0f), radians(20.0f) };

    FlightRuntimeState runtime {};
    runtime.engineThrottle = 0.58f;
    runtime.fuelRemainingKg = 120.0f;

    const int engineCount = std::clamp(flightConfig.engineCount, 1, kMaxFlightEngines);
    for (int i = 0; i < kMaxFlightEngines; ++i) {
        if (i < engineCount) {
            runtime.crankRpm[i] = 2140.0f;
            runtime.shaftRpm[i] = 2140.0f;
            runtime.propRpm[i] = 2140.0f;
            runtime.manifoldPressureKpa[i] = 86.0f;
            runtime.fuelFlowKgPerSec[i] = 0.012f;
            runtime.airMassFlowKgPerSec[i] = 0.012f * 12.8f;
            runtime.enginePowerKw[i] = 128.0f;
            runtime.exhaustGasTempK[i] = 905.0f;
            runtime.cylinderHeadTempK[i] = 431.0f;
            runtime.oilTempK[i] = 364.0f;
            runtime.propellerEfficiency[i] = 0.74f;
            runtime.thrustNewton[i] = 1150.0f;
        } else {
            runtime.crankRpm[i] = 0.0f;
            runtime.shaftRpm[i] = 0.0f;
            runtime.propRpm[i] = 0.0f;
            runtime.manifoldPressureKpa[i] = 0.0f;
            runtime.fuelFlowKgPerSec[i] = 0.0f;
            runtime.airMassFlowKgPerSec[i] = 0.0f;
            runtime.enginePowerKw[i] = 0.0f;
            runtime.exhaustGasTempK[i] = 0.0f;
            runtime.cylinderHeadTempK[i] = 0.0f;
            runtime.oilTempK[i] = 0.0f;
            runtime.propellerEfficiency[i] = 0.0f;
            runtime.thrustNewton[i] = 0.0f;
        }
    }

    runtime.lastDynamicPressure = 4100.0f;
    runtime.lastThrustNewton = 2300.0f;
    runtime.lastAlpha = radians(6.0f);
    runtime.lastBeta = radians(-4.0f);

    FlightEnvironment environment {};
    environment.wind = { 5.0f, 1.5f, -12.0f };

    const ProceduralAudioFrame frame = buildProceduralAudioFrame(
        plane,
        runtime,
        environment,
        terrainContext,
        flightConfig,
        uiState,
        propAudioConfig,
        0.35f,
        0.7f,
        true,
        false,
        1.0f / 60.0f);

    const Vec3 airVelWorld = plane.flightVel - environment.wind;
    const Vec3 airVelBody = worldToBody(plane.rot, airVelWorld);
    const float expectedWaterProximity = computeWaterProximity(plane, terrainContext);
    const float expectedHeightAboveGround = computeHeightAboveGround(plane, terrainContext);

    require(std::fabs(frame.trueAirspeed - length(airVelBody)) < 1.0e-4f, "Audio frame should use air-relative speed", failed);
    require(std::fabs(frame.verticalSpeed - airVelWorld.y) < 1.0e-4f, "Audio frame should use air-relative vertical speed", failed);
    require(std::fabs(frame.angularRateRad - length(plane.flightAngVel)) < 1.0e-4f, "Audio frame should expose body-rate magnitude", failed);
    require(std::fabs(frame.dynamicPressure - runtime.lastDynamicPressure) < 1.0e-4f, "Audio frame should expose runtime q-bar", failed);
    require(std::fabs(frame.thrustNewton - runtime.lastThrustNewton) < 1.0e-4f, "Audio frame should expose runtime thrust", failed);
    require(std::fabs(frame.engineThrottle - runtime.engineThrottle) < 1.0e-4f, "Audio frame should expose runtime throttle state", failed);
    require(std::fabs(frame.crankRpm - avg(runtime.crankRpm, engineCount)) < 1.0e-4f, "Audio frame should expose crank RPM", failed);
    require(std::fabs(frame.propRpm - avg(runtime.propRpm, engineCount)) < 1.0e-4f, "Audio frame should expose prop RPM", failed);
    require(std::fabs(frame.manifoldPressureKpa - avg(runtime.manifoldPressureKpa, engineCount)) < 1.0e-4f, "Audio frame should expose manifold pressure", failed);
    require(std::fabs(frame.enginePowerKw - avg(runtime.enginePowerKw, engineCount)) < 1.0e-4f, "Audio frame should expose engine shaft power", failed);
    require(std::fabs(frame.fuelFlowKgPerSec - avg(runtime.fuelFlowKgPerSec, engineCount)) < 1.0e-4f, "Audio frame should expose fuel flow", failed);
    require(std::fabs(frame.referenceSpeed - flightConfig.maxEffectivePropSpeed) < 1.0e-4f, "Audio frame should use aircraft reference speed", failed);
    require(std::fabs(frame.maxCrankRpm - flightConfig.maxCrankRpm) < 1.0e-4f, "Audio frame should expose maximum crank RPM", failed);
    require(std::fabs(frame.maxBrakePowerKw - flightConfig.maxBrakePowerKw) < 1.0e-4f, "Audio frame should expose max brake power", failed);
    require(std::fabs(frame.referenceDynamicPressure - flightConfig.controlLoadingReferenceDynamicPressure) < 1.0e-4f, "Audio frame should use aircraft q-bar reference", failed);
    require(std::fabs(frame.maxThrustNewton - (flightConfig.maxThrustSeaLevel * flightConfig.afterburnerMultiplier)) < 1.0e-4f, "Audio frame should expose max propulsion authority", failed);
    require(std::fabs(frame.stallAlphaRad - flightConfig.alphaStallRad) < 1.0e-4f, "Audio frame should expose stall alpha", failed);
    require(std::fabs(frame.maxAngularRateRad - flightConfig.maxAngularRateRad) < 1.0e-4f, "Audio frame should expose aircraft rate limit", failed);
    require(std::fabs(frame.groundSpeed - std::sqrt((plane.vel.x * plane.vel.x) + (plane.vel.z * plane.vel.z))) < 1.0e-4f, "Audio frame should expose ground-relative horizontal speed", failed);
    require(std::fabs(frame.heightAboveGroundMeters - expectedHeightAboveGround) < 1.0e-4f, "Audio frame should expose terrain clearance", failed);
    require(std::fabs(frame.pitchRateRad - std::fabs(plane.flightAngVel.x)) < 1.0e-4f, "Audio frame should expose pitch-rate magnitude", failed);
    require(std::fabs(frame.yawRateRad - std::fabs(plane.flightAngVel.y)) < 1.0e-4f, "Audio frame should expose yaw-rate magnitude", failed);
    require(std::fabs(frame.rollRateRad - std::fabs(plane.flightAngVel.z)) < 1.0e-4f, "Audio frame should expose roll-rate magnitude", failed);
    require(std::fabs(frame.waterProximity - expectedWaterProximity) < 1.0e-4f, "Audio frame should preserve water proximity sampling", failed);
    require(std::fabs(frame.foliageBrush - 0.35f) < 1.0e-4f, "Audio frame should preserve foliage brush amount", failed);
    require(std::fabs(frame.foliageImpact - 0.7f) < 1.0e-4f, "Audio frame should preserve foliage impact amount", failed);
    require(frame.exteriorView == uiState.chaseCamera, "Audio frame should preserve camera mode for interior/exterior mixing", failed);
    require(std::fabs(frame.propAudioConfig.baseRpm - propAudioConfig.baseRpm) < 1.0e-4f, "Audio frame should preserve aircraft-local base RPM config", failed);
    require(std::fabs(frame.propAudioConfig.engineFrequencyScale - propAudioConfig.engineFrequencyScale) < 1.0e-4f, "Audio frame should preserve aircraft-local engine frequency scaling", failed);

    GameSession session {};
    session.flightMode = false;
    session.playerMode = PlayerMode::Walking;
    session.plane.pos = { 0.0f, 2.0f, 0.0f };
    session.plane.vel = { 0.0f, 0.0f, 0.0f };
    session.audioGunshotImpulse = 0.65f;
    session.audioTerrainShotImpulse = 0.25f;
    session.audioBombLatchImpulse = 0.45f;
    session.audioExplosionImpulse = 0.72f;
    session.audioExplosionDistanceMeters = 38.0f;
    session.gameplayObjects.push_back(makeGameplayObjectState(
        GameplayObjectKind::Projectile,
        1,
        1,
        { 8.0f, 2.0f, 28.0f },
        { 0.0f, 0.0f, -210.0f },
        0.08f,
        1.0f,
        10.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f));
    session.gameplayObjects.push_back(makeGameplayObjectState(
        GameplayObjectKind::Bomb,
        2,
        1,
        { -6.0f, 18.0f, 34.0f },
        { 0.0f, -8.0f, -64.0f },
        0.34f,
        1.0f,
        24.0f,
        1.0f,
        16.0f,
        10.0f,
        4.2f));
    const CombatAudioTelemetry combatTelemetry = sampleCombatAudioTelemetry(session);
    require(combatTelemetry.gunshotImpulse > 0.0f, "Combat audio telemetry should preserve gunshot impulses", failed);
    require(combatTelemetry.explosionImpulse > 0.0f, "Combat audio telemetry should preserve explosion impulses", failed);
    require(combatTelemetry.projectileWhistleAmount > 0.0f, "Combat audio telemetry should expose nearby projectile flyby whistles", failed);
    require(combatTelemetry.bombWhistleAmount > 0.0f, "Combat audio telemetry should expose nearby bomb whistles", failed);
    require(combatTelemetry.explosionDistanceMeters < 100.0f, "Combat audio telemetry should preserve explosion distance", failed);
}

void runCullingAndWalkingRigChecks(bool& failed)
{
    Camera camera {};
    camera.rot = quatIdentity();
    camera.fovRadians = radians(70.0f);
    camera.farClipMeters = 320.0f;

    const float edgeAngle = radians(62.0f);
    const Vec3 edgeCenter {
        std::sin(edgeAngle) * 100.0f,
        0.0f,
        std::cos(edgeAngle) * 100.0f
    };
    require(
        sphereWithinView(camera, edgeCenter, 40.0f, 320.0f),
        "Large terrain-edge spheres should remain visible when their bounds still overlap the frustum",
        failed);

    PlaneVisualState walkingVisual;
    walkingVisual.defaultScale = 1.0f;
    setBuiltinWalkingModel(walkingVisual);
    require(walkingVisual.sourceModel.assetKey == "builtin:walking_biped", "Built-in walking visual should use the biped rig asset key", failed);
    require(walkingVisual.label == "builtin player biped", "Built-in walking visual should advertise the biped label", failed);
    require(
        walkingVisual.sourceModel.vertices.size() > makeCubeModel().vertices.size(),
        "Built-in walking visual should no longer be the cube primitive",
        failed);

    FlightState actor {};
    actor.pos = { 0.0f, 1.8f, 0.0f };
    actor.vel = { 2.0f, 0.0f, 5.5f };
    actor.onGround = true;
    actor.rot = composeWalkingRotation(radians(18.0f), radians(10.0f));
    const Model posedWalkingModel = buildProceduralWalkingRigModel(sampleWalkingRigPose(actor, 0.75f));
    require(posedWalkingModel.faces.size() > 40u, "Procedural walking rig should generate a multi-part articulated mesh", failed);
    if (!posedWalkingModel.vertices.empty()) {
        float minY = posedWalkingModel.vertices.front().y;
        float maxY = posedWalkingModel.vertices.front().y;
        for (const Vec3& vertex : posedWalkingModel.vertices) {
            minY = std::min(minY, vertex.y);
            maxY = std::max(maxY, vertex.y);
        }
        require((maxY - minY) > 1.8f, "Procedural walking rig height regressed below the intended biped silhouette", failed);
    }

    PlaneVisualState planeVisual;
    planeVisual.defaultScale = 1.0f;
    setBuiltinPlaneModel(planeVisual);
    require(planeVisual.sourceModel.assetKey == "builtin:procedural_plane", "Built-in plane visual should advertise the procedural aircraft asset key", failed);
    require(planeVisual.label == "builtin procedural plane", "Built-in plane visual should advertise the procedural aircraft label", failed);
    require(
        planeVisual.sourceModel.vertices.size() > makeCubeModel().vertices.size(),
        "Built-in plane visual should no longer fall back to the cube primitive",
        failed);
    planeVisual.rigCutouts[0] = defaultVisualRigCutout(0);
    planeVisual.rigCutouts[0].enabled = true;
    planeVisual.rigCutouts[0].center = { 0.0f, 0.0f, 1.0f };
    planeVisual.rigCutouts[0].halfExtents = { 1.2f, 1.2f, 0.24f };
    planeVisual.rigCutouts[0].pivot = { 0.0f, 0.0f, 1.0f };
    rebuildVisualRigModels(planeVisual);
    require(visualUsesRigCutouts(planeVisual), "Character rig cutouts should build a split animated model cache", failed);
    require(planeVisual.rigSlotActive[0], "Character rig cutouts should capture faces inside the selected prop volume", failed);
    require(planeVisual.rigBaseModel.faces.size() + planeVisual.rigSlotModels[0].faces.size() == planeVisual.model.faces.size(), "Character rig partitioning should preserve the source face count", failed);
    require(std::fabs(visualRigSlotAngleRadians(planeVisual, 0, 1.0f, 1800.0f, 0.0f)) > 10.0f, "Character rig prop previews should derive visible rotation from prop RPM", failed);

    PlaneVisualState mirroredVisual;
    mirroredVisual.defaultScale = 1.0f;
    mirroredVisual.sourceModel = makeCubeModel();
    mirroredVisual.model = mirroredVisual.sourceModel;
    mirroredVisual.label = "mirrored cube";
    mirroredVisual.rigCutouts[2] = defaultVisualRigCutout(2);
    mirroredVisual.rigCutouts[2].enabled = true;
    mirroredVisual.rigCutouts[2].center = { 0.32f, 0.08f, 0.22f };
    mirroredVisual.rigCutouts[2].pivot = { 0.28f, 0.12f, 0.06f };
    mirroredVisual.rigCutouts[2].motionScale = -18.0f;
    const VisualRigCutout sourceCutout = mirroredVisual.rigCutouts[2];
    require(mirrorVisualRigCutoutFromPairedSlot(mirroredVisual, 3), "Rig mirror helper should succeed for paired control surfaces", failed);

    const Quat mirroredRotation = composeVisualRotationOffset(mirroredVisual);
    const Vec3 mirroredLocalRight = normalize(rotateVector(quatConjugate(mirroredRotation), { 1.0f, 0.0f, 0.0f }), { 1.0f, 0.0f, 0.0f });
    float rightMin = dot(mirroredVisual.model.vertices.front(), mirroredLocalRight);
    float rightMax = rightMin;
    for (const Vec3& vertex : mirroredVisual.model.vertices) {
        const float projection = dot(vertex, mirroredLocalRight);
        rightMin = std::min(rightMin, projection);
        rightMax = std::max(rightMax, projection);
    }
    const float rightMid = (rightMin + rightMax) * 0.5f;
    require(
        std::fabs((dot(sourceCutout.center, mirroredLocalRight) + dot(mirroredVisual.rigCutouts[3].center, mirroredLocalRight)) - (2.0f * rightMid)) < 1.0e-3f,
        "Rig mirror helper should reflect cutout centers across the model centerline",
        failed);
    require(
        std::fabs((dot(sourceCutout.pivot, mirroredLocalRight) + dot(mirroredVisual.rigCutouts[3].pivot, mirroredLocalRight)) - (2.0f * rightMid)) < 1.0e-3f,
        "Rig mirror helper should reflect pivots across the model centerline",
        failed);
    require(
        std::fabs(mirroredVisual.rigCutouts[3].motionScale + sourceCutout.motionScale) < 1.0e-4f,
        "Rig mirror helper should flip control-surface travel when copying to the paired side",
        failed);
}

void runTerrainLodChecks(bool& failed)
{
    const TerrainParams params = normalizeTerrainParams(defaultTerrainParams());
    const float lod1TileSize = computeLod1TerrainTileSize(params);
    const float lod2TileSize = computeLod2TerrainTileSize(params);
    const float nearSpan = computeNearHalfExtent(params, lod1TileSize) * 2.0f;
    const float midSpan = lod1TileSize;
    const float farSpan = lod2TileSize;

    const float nearStep = terrainPatchAxisStep(params, params.lod0CellSize, nearSpan);
    const float midStep = terrainPatchAxisStep(params, params.lod1CellSize, midSpan);
    const float farStep = terrainPatchAxisStep(params, params.lod2CellSize, farSpan);

    require(
        nearStep < midStep,
        "Terrain patch budgeting regressed: LOD0 is not finer than LOD1 over the default near span",
        failed);
    require(
        midStep < farStep,
        "Terrain patch budgeting regressed: LOD1 is not finer than LOD2 over the default far spans",
        failed);

    const TerrainFieldContext terrainContext = createTerrainFieldContext(params);
    require(
        !terrainContext.roadPaths.empty(),
        "Road generation regressed: default terrain context should populate at least one procedural road path",
        failed);
    const auto roadSpawn = findRoadSpawnSample(terrainContext, 0.0f);
    require(roadSpawn.has_value(), "Road spawn queries regressed: no drivable spawn sample was found on the default terrain", failed);
    if (roadSpawn.has_value()) {
        require(terrainRoadSampleValid(*roadSpawn), "Road spawn query returned an invalid terrain-road sample", failed);
        require(roadSpawn->widthMeters > 2.0f, "Road spawn sample should expose a usable road width", failed);
        const Vec3 placedOnRoad = terrainRoadPlacementPosition(*roadSpawn, 1.5f, 0.75f);
        require(
            std::fabs(placedOnRoad.y - (roadSpawn->centerPosition.y + 0.75f)) < 1.0e-4f,
            "Road placement helper did not preserve the requested vertical offset",
            failed);
        require(
            lengthSquared(placedOnRoad - roadSpawn->centerPosition) > 0.5f,
            "Road placement helper should honor lateral offsets away from the centerline",
            failed);
        const float roadFriction = terrainRoadSurfaceFriction(*roadSpawn);
        require(roadFriction >= 0.45f && roadFriction <= 1.2f, "Road surface friction helper returned an out-of-range value", failed);
        const float steeringAssist = terrainRoadSteeringAssist(*roadSpawn);
        require(steeringAssist > 0.0f && steeringAssist <= 1.0f, "Road steering assist helper should report a positive normalized assist on valid roads", failed);

        FlightState drivingState = makeFlightState();
        drivingState.pos = terrainRoadPlacementPosition(*roadSpawn, 0.0f, 1.2f);
        drivingState.rot = HostedNetworkingDetail::vehicleRotationFromYaw(std::atan2(roadSpawn->forward.x, roadSpawn->forward.z));
        drivingState.vel = roadSpawn->forward * 18.0f;

        UiState driveChaseUi = defaultUiState();
        driveChaseUi.chaseCamera = true;
        const Camera driveChaseCamera = buildRenderCamera(
            drivingState,
            terrainContext,
            driveChaseUi,
            PlayerMode::Driving,
            0.0f,
            0.0f,
            computeWorldFarClip(defaultGraphicsSettings()));
        require(
            length(driveChaseCamera.pos - drivingState.pos) > 4.0f,
            "Driving chase camera regressed back to the walking shoulder camera distance",
            failed);
        require(
            driveChaseCamera.pos.y > drivingState.pos.y,
            "Driving chase camera should sit above the vehicle seat for road visibility",
            failed);

        UiState driveCockpitUi = defaultUiState();
        driveCockpitUi.chaseCamera = false;
        const Camera driveCockpitCamera = buildRenderCamera(
            drivingState,
            terrainContext,
            driveCockpitUi,
            PlayerMode::Driving,
            0.0f,
            0.0f,
            computeWorldFarClip(defaultGraphicsSettings()));
        require(
            driveCockpitCamera.pos.y < (drivingState.pos.y + 0.8f),
            "Driving first-person camera should stay near the vehicle seat instead of using walking eye height",
            failed);
    }

    const std::array<Vec2, 5> terrainProbePoints {
        Vec2{ 0.0f, 0.0f },
        Vec2{ 512.0f, 0.0f },
        Vec2{ -512.0f, 0.0f },
        Vec2{ 0.0f, 512.0f },
        Vec2{ 0.0f, -512.0f }
    };
    float nearbyMinHeight = std::numeric_limits<float>::infinity();
    float nearbyMaxHeight = -std::numeric_limits<float>::infinity();
    for (const Vec2& point : terrainProbePoints) {
        const float height = sampleGroundHeight(point.x, point.y, terrainContext);
        nearbyMinHeight = std::min(nearbyMinHeight, height);
        nearbyMaxHeight = std::max(nearbyMaxHeight, height);
    }
    require(
        nearbyMaxHeight < 2600.0f,
        "Default terrain regressed to extreme mountain-front heights near the playable origin",
        failed);
    require(
        (nearbyMaxHeight - nearbyMinHeight) < 3200.0f,
        "Default terrain regressed to excessive near-origin vertical relief that buries roads and spawn areas",
        failed);

    TerrainParams modifiedRoadParams = params;
    modifiedRoadParams.roads.desiredRoadCount += 1;
    require(
        !terrainParamsEquivalent(params, modifiedRoadParams),
        "Terrain parameter equivalence regressed: road-generation changes must invalidate cached terrain",
        failed);

    const WorldInfoSnapshot spawnInfo = buildWorldInfoSnapshot(params, "smoke_spawn");
    require(
        sampleGroundHeight(spawnInfo.spawnX, spawnInfo.spawnZ, terrainContext) > (params.waterLevel + 12.0f),
        "Default world spawn regressed below the playable dry-ground threshold",
        failed);

    const Model nearPatch = buildSurfaceTerrainPatch(
        terrainContext,
        { -64.0f, 64.0f, -64.0f, 64.0f, false, 0.0f, 0.0f, 0.0f, 0.0f },
        params.lod0CellSize);
    require(
        !nearPatch.vertices.empty() && nearPatch.vertexNormals.size() == nearPatch.vertices.size(),
        "Surface terrain patch no longer generates per-vertex normals for near-field lighting",
        failed);

    TerrainParams planetParams = defaultTerrainParams();
    planetParams.worldShape = WorldShape::Planet;
    planetParams.planet.radiusMeters = 6371000.0;
    planetParams.planet.gravitationalParameter = 3.986004418e14;
    planetParams.planet.rotationRateRadPerSec = 7.2921159e-5;
    planetParams.planet.atmosphereHeightMeters = 120000.0;
    planetParams.chunkSize = 128.0f;
    planetParams.gameplayRadiusMeters = 1024.0f;
    planetParams.midFieldRadiusMeters = 8192.0f;
    planetParams.horizonRadiusMeters = 65536.0f;
    planetParams.regionalArcRadiusMeters = 384000.0f;
    planetParams.lod0ChunkScale = 1;
    planetParams.lod1ChunkScale = 8;
    planetParams.lod2ChunkScale = 32;
    planetParams.lod0BaseCellSize = 1.7f;
    planetParams.lod1BaseCellSize = 6.0f;
    planetParams.lod2BaseCellSize = 20.0f;
    planetParams.heightAmplitude = 8200.0f;
    planetParams.ridgeAmplitude = 6400.0f;
    planetParams.surfaceDetailAmplitude = 240.0f;
    planetParams.waterRatio = 0.62f;
    planetParams.maxChunkCellsPerAxis = 128;
    planetParams = normalizeTerrainParams(planetParams);

    const int finestPlanetScale = std::max(1, planetParams.lod0ChunkScale);
    const int coarsestPlanetScale = finestPlanetScale << 9;
    const float coarsestPlanetTileSize = std::max(planetParams.chunkSize, planetParams.chunkSize * static_cast<float>(coarsestPlanetScale));
    require(
        planetParams.lod0CellSize <= 1.05f,
        "Planet terrain defaults should preserve roughly one-meter near-field surface detail",
        failed);
    require(
        coarsestPlanetTileSize >= 60000.0f,
        "Planet terrain defaults should span a deep far-field quadtree ladder",
        failed);

    const TerrainFieldContext planetContext = createTerrainFieldContext(planetParams);
    const auto directionFromDegrees = [](double latitudeDeg, double longitudeDeg) -> DVec3 {
        static constexpr double kDegToRad = 0.017453292519943295769;
        const double latitudeRad = latitudeDeg * kDegToRad;
        const double longitudeRad = longitudeDeg * kDegToRad;
        const double cosLatitude = std::cos(latitudeRad);
        return {
            cosLatitude * std::cos(longitudeRad),
            std::sin(latitudeRad),
            cosLatitude * std::sin(longitudeRad)
        };
    };

    float oceanFloorMin = std::numeric_limits<float>::infinity();
    float oceanFloorMax = -std::numeric_limits<float>::infinity();
    int oceanSampleCount = 0;
    int landSampleCount = 0;
    for (int latitude = -75; latitude <= 75; latitude += 15) {
        for (int longitude = -180; longitude <= 180; longitude += 15) {
            const float surface = samplePlanetProceduralSurfaceHeight(directionFromDegrees(latitude, longitude), planetContext);
            if (surface < planetParams.waterLevel) {
                ++oceanSampleCount;
                oceanFloorMin = std::min(oceanFloorMin, surface);
                oceanFloorMax = std::max(oceanFloorMax, surface);
            } else {
                ++landSampleCount;
            }
        }
    }

    require(
        oceanSampleCount > 40 && landSampleCount > 40,
        "Planet terrain sampling should produce both ocean basins and emergent land",
        failed);
    require(
        (oceanFloorMax - oceanFloorMin) > (planetParams.heightAmplitude * 0.14f),
        "Planet ocean floors regressed toward flat under-water shelves",
        failed);
}

void runCloudFieldChecks(bool& failed)
{
    CloudField cloudField;
    WindState windState {};
    std::mt19937 rng(123456u);
    const Vec3 focusPoint {};

    initializeCloudField(cloudField, rng, focusPoint);
    require(
        cloudField.groups.size() == static_cast<std::size_t>(std::max(1, cloudField.groupCount)),
        "Cloud field initialization should honor the configured group count",
        failed);
    require(
        std::all_of(
            cloudField.groups.begin(),
            cloudField.groups.end(),
            [](const CloudGroup& group)
            {
                return !group.puffs.empty() && group.renderRadius > 0.0f;
            }),
        "Cloud initialization should create populated volumetric groups with valid render bounds",
        failed);
    require(
        std::any_of(
            cloudField.groups.begin(),
            cloudField.groups.end(),
            [](const CloudGroup& group)
            {
                return !group.volumeModel.vertices.empty();
            }),
        "Cloud initialization regressed: no volumetric cloud mesh was built",
        failed);

    updateCloudField(cloudField, windState, 1.0f / 30.0f, 9.0f, focusPoint, rng);
    updateCloudField(cloudField, windState, 1.0f / 30.0f, 30.0f, { 9000.0f, 0.0f, 0.0f }, rng);

    require(
        std::all_of(
            cloudField.groups.begin(),
            cloudField.groups.end(),
            [](const CloudGroup& group)
            {
                return !group.puffs.empty() && group.nextMorphAt > 0.0f && group.nextMeshRebuildAt > 0.0f;
            }),
        "Cloud lifecycle update should keep each group scheduled for future morph/mesh work",
        failed);
    require(
        std::any_of(
            cloudField.groups.begin(),
            cloudField.groups.end(),
            [](const CloudGroup& group)
            {
                return !group.volumeModel.vertices.empty();
            }),
        "Cloud update/regeneration regressed: volumetric meshes disappeared after simulation",
        failed);
}

void runTerrainEditBoundsChecks(bool& failed)
{
    const auto root = repoRoot();
    const std::filesystem::path tempRoot = root / "build/native-smoke-terrain-edit-temp";
    std::error_code ec;
    std::filesystem::remove_all(tempRoot, ec);

    WorldStoreOptions options;
    options.name = "smoke_edit_bounds";
    options.storageRoot = tempRoot;
    options.groundParams = defaultTerrainParams();

    std::string worldError;
    auto openedWorld = WorldStore::open(options, &worldError);
    require(openedWorld.has_value(), "World store failed to open for terrain edit smoke: " + worldError, failed);
    if (!openedWorld.has_value()) {
        return;
    }

    WorldStore world = std::move(*openedWorld);
    WorldChunkState chunk;
    chunk.cx = 0;
    chunk.cz = 0;
    chunk.resolution = normalizeWorldChunkResolution(world.getMeta().chunkResolution);
    chunk.heightDeltas.assign(static_cast<std::size_t>((chunk.resolution + 1) * (chunk.resolution + 1)), 0.0f);
    chunk.revision = 1;
    chunk.materialRevision = 1;
    const float deepOverrideY = options.groundParams.minY - 220.0f;
    chunk.volumetricOverrides.push_back({ "sphere_sub", 84.0f, deepOverrideY, 64.0f, 28.0f });
    require(world.applyChunkState(chunk), "Terrain edit smoke failed to inject a deep volumetric override", failed);

    TerrainFieldContext terrainContext = createTerrainFieldContext(options.groundParams);
    bindTerrainContextWorldStore(terrainContext, &world, {});

    const TerrainPatchBounds ownerBounds {
        0.0f,
        terrainContext.params.chunkSize,
        0.0f,
        terrainContext.params.chunkSize,
        false,
        0.0f,
        0.0f,
        0.0f,
        0.0f
    };
    const TerrainPatchBounds neighborBounds {
        terrainContext.params.chunkSize,
        terrainContext.params.chunkSize * 2.0f,
        0.0f,
        terrainContext.params.chunkSize,
        false,
        0.0f,
        0.0f,
        0.0f,
        0.0f
    };

    const TerrainVolumeBounds adaptiveBounds = buildAdaptiveTerrainVolumeBounds(
        ownerBounds,
        terrainContext,
        terrainContext.params.lod0CellSize,
        terrainContext.params.lod0CellSize);
    require(
        adaptiveBounds.y0 < (terrainContext.params.minY - 100.0f),
        "Adaptive terrain volume bounds should extend below the generic minY when deep live voxel edits exist",
        failed);
    require(
        terrainPatchNeedsWorldVolumetrics(neighborBounds, terrainContext),
        "Neighboring terrain tiles should qualify for volumetric meshing around live voxel edits to avoid mixed-mode seams",
        failed);
}

void runTerrainStreamingChecks(bool& failed)
{
    require(
        initialTerrainTileDetailForBand(TerrainFarTileBand::Near) == TerrainFarTileDetail::Lod2,
        "Terrain streaming regressed: near-field tiles should start from coarse passive coverage",
        failed);
    require(
        initialTerrainTileDetailForBand(TerrainFarTileBand::Mid) == TerrainFarTileDetail::Lod2,
        "Terrain streaming regressed: mid-field tiles should start from coarse passive coverage",
        failed);
    require(
        nextTerrainTileDetail(TerrainFarTileBand::Near, TerrainFarTileDetail::Lod2) == TerrainFarTileDetail::Lod1 &&
            nextTerrainTileDetail(TerrainFarTileBand::Near, TerrainFarTileDetail::Lod1) == TerrainFarTileDetail::Lod0 &&
            !nextTerrainTileDetail(TerrainFarTileBand::Near, TerrainFarTileDetail::Lod0).has_value(),
        "Terrain streaming regressed: near-field tiles should refine from LOD2 to LOD1 to LOD0",
        failed);
    require(
        nextTerrainTileDetail(TerrainFarTileBand::Mid, TerrainFarTileDetail::Lod2) == TerrainFarTileDetail::Lod1 &&
            !nextTerrainTileDetail(TerrainFarTileBand::Mid, TerrainFarTileDetail::Lod1).has_value(),
        "Terrain streaming regressed: mid-field tiles should refine from LOD2 to LOD1 without a blocking extra stage",
        failed);

    TerrainFarTile currentTile;
    currentTile.band = TerrainFarTileBand::Near;
    currentTile.detail = TerrainFarTileDetail::Lod1;
    currentTile.paramsSignature = 101u;
    currentTile.sourceSignature = 202u;

    const TerrainTileRequest upgradeRequest {
        TerrainFarTileBand::Near,
        TerrainFarTileDetail::Lod0,
        1,
        0,
        0,
        101u,
        202u,
        0.0f
    };
    const TerrainTileRequest downgradeRequest {
        TerrainFarTileBand::Near,
        TerrainFarTileDetail::Lod2,
        1,
        0,
        0,
        101u,
        202u,
        0.0f
    };
    const TerrainTileRequest refreshRequest {
        TerrainFarTileBand::Near,
        TerrainFarTileDetail::Lod2,
        1,
        0,
        0,
        303u,
        404u,
        0.0f
    };

    require(
        shouldApplyTerrainChunkResult(&currentTile, upgradeRequest),
        "Terrain streaming regressed: finer current-detail results should still replace a coarse stage",
        failed);
    require(
        !shouldApplyTerrainChunkResult(&currentTile, downgradeRequest),
        "Terrain streaming regressed: late coarse results should not downgrade a current tile",
        failed);
    require(
        shouldApplyTerrainChunkResult(&currentTile, refreshRequest),
        "Terrain streaming regressed: stale tiles should still accept a refreshed replacement",
        failed);

    const auto makeRequest = [](TerrainFarTileBand band, TerrainFarTileDetail detail, float priority, std::uint64_t signature) {
        return TerrainTileRequest {
            band,
            detail,
            1,
            static_cast<int>(priority),
            static_cast<int>(priority),
            signature,
            signature + 1u,
            priority
        };
    };

    {
        TerrainVisualStreamState queueState;
        queueState.generation = 3u;
        queueState.maxPendingRequests = 2;
        queueState.maxCompletedResults = 1;
        const TerrainTileRequest nearRequest = makeRequest(TerrainFarTileBand::Near, TerrainFarTileDetail::Lod2, 1.0f, 10u);
        const TerrainTileRequest midRequest = makeRequest(TerrainFarTileBand::Mid, TerrainFarTileDetail::Lod2, 2.0f, 20u);
        const TerrainTileRequest horizonRequest = makeRequest(TerrainFarTileBand::Horizon, TerrainFarTileDetail::Lod2, 0.5f, 30u);
        queueState.desiredRequests[terrainTileIdentityKey(nearRequest.band, nearRequest.tileScale, nearRequest.tileX, nearRequest.tileZ)] = nearRequest;
        queueState.desiredRequests[terrainTileIdentityKey(midRequest.band, midRequest.tileScale, midRequest.tileX, midRequest.tileZ)] = midRequest;
        queueState.desiredRequests[terrainTileIdentityKey(horizonRequest.band, horizonRequest.tileScale, horizonRequest.tileX, horizonRequest.tileZ)] = horizonRequest;
        queueState.queuedRequests.push_back({ nearRequest, {}, queueState.generation });
        queueState.queuedRequests.push_back({ midRequest, {}, queueState.generation });
        queueState.queuedRequests.push_back({ horizonRequest, {}, queueState.generation });
        queueState.queuedRequestKeys[terrainTileRequestKey(nearRequest)] = terrainTileIdentityKey(nearRequest.band, nearRequest.tileScale, nearRequest.tileX, nearRequest.tileZ);
        queueState.queuedRequestKeys[terrainTileRequestKey(midRequest)] = terrainTileIdentityKey(midRequest.band, midRequest.tileScale, midRequest.tileX, midRequest.tileZ);
        queueState.queuedRequestKeys[terrainTileRequestKey(horizonRequest)] = terrainTileIdentityKey(horizonRequest.band, horizonRequest.tileScale, horizonRequest.tileX, horizonRequest.tileZ);
        trimTerrainStreamBacklogLocked(queueState);

        bool hasNear = false;
        bool hasMid = false;
        bool hasHorizon = false;
        for (const TerrainChunkBuildRequest& request : queueState.queuedRequests) {
            hasNear = hasNear || request.request.band == TerrainFarTileBand::Near;
            hasMid = hasMid || request.request.band == TerrainFarTileBand::Mid;
            hasHorizon = hasHorizon || request.request.band == TerrainFarTileBand::Horizon;
        }
        require(
            queueState.queuedRequests.size() == 2u && hasNear && hasMid && !hasHorizon,
            "Terrain streaming regressed: bounded pending work should evict horizon requests before near/mid coverage",
            failed);
    }

    {
        TerrainVisualStreamState completedState;
        completedState.generation = 4u;
        completedState.maxPendingRequests = 6;
        completedState.maxCompletedResults = 1;
        const TerrainTileRequest nearRequest = makeRequest(TerrainFarTileBand::Near, TerrainFarTileDetail::Lod1, 1.0f, 40u);
        const TerrainTileRequest horizonRequest = makeRequest(TerrainFarTileBand::Horizon, TerrainFarTileDetail::Lod2, 0.1f, 50u);
        completedState.desiredRequests[terrainTileIdentityKey(nearRequest.band, nearRequest.tileScale, nearRequest.tileX, nearRequest.tileZ)] = nearRequest;
        completedState.desiredRequests[terrainTileIdentityKey(horizonRequest.band, horizonRequest.tileScale, horizonRequest.tileX, horizonRequest.tileZ)] = horizonRequest;
        completedState.completedResults.push_back({ horizonRequest, {}, completedState.generation });
        completedState.completedResults.push_back({ nearRequest, {}, completedState.generation });
        trimTerrainStreamBacklogLocked(completedState);
        require(
            completedState.completedResults.size() == 1u &&
                completedState.completedResults.front().request.band == TerrainFarTileBand::Near,
            "Terrain streaming regressed: completed-result backlog should retain the more valuable near tile when capped",
            failed);
    }

    {
        TerrainVisualStreamState staleState;
        staleState.generation = 5u;
        staleState.maxPendingRequests = 4;
        staleState.maxCompletedResults = 2;
        const TerrainTileRequest request = makeRequest(TerrainFarTileBand::Near, TerrainFarTileDetail::Lod2, 1.0f, 60u);
        staleState.desiredRequests[terrainTileIdentityKey(request.band, request.tileScale, request.tileX, request.tileZ)] = request;
        staleState.completedResults.push_back({ request, {}, staleState.generation - 1u });
        trimTerrainStreamBacklogLocked(staleState);
        require(
            staleState.completedResults.empty(),
            "Terrain streaming regressed: stale-generation results should be discarded during backlog trimming",
            failed);
    }
}

void runPressureGovernorChecks(bool& failed)
{
    SystemPressureSnapshot pressureSnapshot;
    pressureSnapshot.valid = true;
    pressureSnapshot.totalPhysicalBytes = 32ull * kGiB;
    pressureSnapshot.availablePhysicalBytes = 3ull * kGiB;
    pressureSnapshot.commitHeadroomBytes = 3ull * kGiB;
    pressureSnapshot.gpuLocalBudgetBytes = 8ull * kGiB;
    pressureSnapshot.gpuLocalUsageBytes = 7ull * kGiB;

    require(
        computeRuntimePressureState(pressureSnapshot, RuntimePressureState::Normal) == RuntimePressureState::Pressure,
        "Pressure governor regressed: low available RAM / high VRAM usage should enter Pressure",
        failed);

    SystemPressureSnapshot criticalSnapshot = pressureSnapshot;
    criticalSnapshot.availablePhysicalBytes = 1ull * kGiB;
    criticalSnapshot.commitHeadroomBytes = 512ull * kMiB;
    criticalSnapshot.gpuLocalUsageBytes = static_cast<std::uint64_t>(criticalSnapshot.gpuLocalBudgetBytes * 0.95);
    require(
        computeRuntimePressureState(criticalSnapshot, RuntimePressureState::Pressure) == RuntimePressureState::Critical,
        "Pressure governor regressed: severe RAM and commit pressure should enter Critical",
        failed);

    SystemPressureSnapshot recoverySnapshot = pressureSnapshot;
    recoverySnapshot.availablePhysicalBytes = 7ull * kGiB;
    recoverySnapshot.commitHeadroomBytes = 4ull * kGiB;
    recoverySnapshot.gpuLocalUsageBytes = static_cast<std::uint64_t>(recoverySnapshot.gpuLocalBudgetBytes * 0.70);
    require(
        computeRuntimePressureState(recoverySnapshot, RuntimePressureState::Pressure) == RuntimePressureState::Normal,
        "Pressure governor regressed: recovered RAM and VRAM headroom should return to Normal",
        failed);

    PerformanceGovernor governor;
    governor.pressureState = RuntimePressureState::Normal;
    governor.hardwareTier = HardwareTier::Requirement;
    updatePerformanceGovernor(governor, 2.0f, 0.020f, pressureSnapshot);
    require(
        governor.pressureState == RuntimePressureState::Pressure,
        "Pressure governor regressed: update should preserve the sampled Pressure state",
        failed);
    require(
        governor.residentMeshBudgetBytes == (512ull * kMiB) &&
            governor.sceneTextureBudgetBytes == (1024ull * kMiB) &&
            governor.maxUploadBytes == (48ull * kMiB),
        "Pressure governor regressed: requirement-tier pressure budgets should clamp renderer memory and uploads",
        failed);
    require(
        !governor.allowHorizonBand && governor.allowMidBand && governor.upgradeBudget == 1,
        "Pressure governor regressed: Pressure should stop horizon generation and sharply cap upgrades",
        failed);
}

void runTerrainDecorationChecks(bool& failed)
{
    TerrainParams params = defaultTerrainParams();
    params.decoration.enabled = true;
    params.decoration.density = 1.6f;
    params.decoration.nearDensityScale = 1.2f;
    params.decoration.midDensityScale = 0.8f;
    params.decoration.farDensityScale = 0.24f;
    params.decoration.shoreBrushDensity = 1.4f;
    params.decoration.rockDensity = 1.2f;
    params.decoration.treeLineOffset = -20.0f;
    params = normalizeTerrainParams(params);
    const TerrainFieldContext terrainContext = createTerrainFieldContext(params);

    TerrainChunkKey key;
    key.worldId = "smoke";
    key.seed = params.seed;
    key.generatorVersion = params.generatorVersion;
    key.band = static_cast<int>(TerrainFarTileBand::Near);
    key.detail = static_cast<int>(TerrainFarTileDetail::Lod0);
    key.paramsSignature = terrainParamsSignature(params);

    const float tileSize = computeLod0TerrainTileSize(params);
    TerrainPatchBounds bounds {};
    TerrainTileDecorationResult first;
    TerrainTileDecorationResult second;
    bool foundRichTile = false;
    for (int tileZ = -4; tileZ <= 4 && !foundRichTile; ++tileZ) {
        for (int tileX = -4; tileX <= 4 && !foundRichTile; ++tileX) {
            key.tileX = tileX;
            key.tileZ = tileZ;
            bounds = {
                static_cast<float>(tileX) * tileSize,
                (static_cast<float>(tileX) + 1.0f) * tileSize,
                static_cast<float>(tileZ) * tileSize,
                (static_cast<float>(tileZ) + 1.0f) * tileSize,
                false,
                0.0f,
                0.0f,
                0.0f,
                0.0f
            };
            first = buildTerrainTileDecoration(key, bounds, TerrainFarTileBand::Near, terrainContext);
            second = buildTerrainTileDecoration(key, bounds, TerrainFarTileBand::Near, terrainContext);
            bool sawBrush = false;
            bool sawBlocker = false;
            for (const TerrainPropCollider& collider : first.propColliders) {
                sawBrush = sawBrush || collider.propClass == TerrainPropClass::Brush;
                sawBlocker = sawBlocker || collider.propClass == TerrainPropClass::Blocker;
            }
            foundRichTile = sawBrush && sawBlocker;
        }
    }
    require(foundRichTile, "Failed to find a smoke tile with both brush and blocker terrain props", failed);
    if (!foundRichTile) {
        return;
    }
    bool modelsMatch =
        first.propModel.vertices.size() == second.propModel.vertices.size() &&
        first.propModel.faces.size() == second.propModel.faces.size() &&
        first.propModel.faceColors.size() == second.propModel.faceColors.size() &&
        first.propColliders.size() == second.propColliders.size();
    if (modelsMatch) {
        for (std::size_t i = 0; i < first.propModel.vertices.size(); ++i) {
            const Vec3& a = first.propModel.vertices[i];
            const Vec3& b = second.propModel.vertices[i];
            if (std::fabs(a.x - b.x) > 1.0e-4f || std::fabs(a.y - b.y) > 1.0e-4f || std::fabs(a.z - b.z) > 1.0e-4f) {
                modelsMatch = false;
                break;
            }
        }
    }
    if (modelsMatch) {
        for (std::size_t i = 0; i < first.propColliders.size(); ++i) {
            const TerrainPropCollider& a = first.propColliders[i];
            const TerrainPropCollider& b = second.propColliders[i];
            if (a.propClass != b.propClass ||
                std::fabs(a.center.x - b.center.x) > 1.0e-4f ||
                std::fabs(a.center.y - b.center.y) > 1.0e-4f ||
                std::fabs(a.center.z - b.center.z) > 1.0e-4f ||
                std::fabs(a.radius - b.radius) > 1.0e-4f ||
                std::fabs(a.halfHeight - b.halfHeight) > 1.0e-4f) {
                modelsMatch = false;
                break;
            }
        }
    }
    require(
        modelsMatch,
        "Terrain decoration should be deterministic for the same tile seed and params",
        failed);
    require(!first.propModel.vertices.empty(), "Terrain decoration should generate visible prop geometry for the smoke tile", failed);
    require(!first.propColliders.empty(), "Terrain decoration should generate interaction volumes for the smoke tile", failed);

    bool foundBrush = false;
    bool foundBlocker = false;
    TerrainPropCollider brushCollider {};
    TerrainPropCollider blockerCollider {};
    for (const TerrainPropCollider& collider : first.propColliders) {
        const float ground = sampleGroundHeight(collider.center.x, collider.center.z, terrainContext);
        const float water = sampleWaterHeight(collider.center.x, collider.center.z, terrainContext);
        require(ground >= water - 0.2f, "Terrain decoration should not place props underwater", failed);
        if (collider.propClass == TerrainPropClass::Brush && !foundBrush) {
            foundBrush = true;
            brushCollider = collider;
            require(sampleTerrainSlope01(collider.center.x, collider.center.z, terrainContext) < 0.82f, "Brush props should avoid extreme slopes", failed);
        } else if (collider.propClass == TerrainPropClass::Blocker && !foundBlocker) {
            foundBlocker = true;
            blockerCollider = collider;
        }
    }
    require(foundBrush, "Smoke tile should contain at least one soft foliage volume", failed);
    require(foundBlocker, "Smoke tile should contain at least one blocker prop volume", failed);

    TerrainVisualCache terrainCache;
    terrainCache.lod0TileSize = tileSize;
    terrainCache.lod1TileSize = computeLod1TerrainTileSize(params);
    terrainCache.lod2TileSize = computeLod2TerrainTileSize(params);
    TerrainFarTile nearTile;
    nearTile.band = TerrainFarTileBand::Near;
    nearTile.detail = TerrainFarTileDetail::Lod0;
    nearTile.tileX = key.tileX;
    nearTile.tileZ = key.tileZ;
    nearTile.active = true;
    nearTile.propColliders = first.propColliders;
    terrainCache.nearTiles.push_back(nearTile);

    TerrainVisualCache blockerOnlyCache = terrainCache;
    blockerOnlyCache.nearTiles.front().propColliders = { blockerCollider };
    TerrainVisualCache brushOnlyCache = terrainCache;
    brushOnlyCache.nearTiles.front().propColliders = { brushCollider };

    const float brushAmount = computeBrushContactAmount(
        brushOnlyCache,
        brushCollider.center,
        0.5f,
        brushCollider.center.y - 0.6f,
        brushCollider.center.y + 0.6f);
    requireKnownIssue(brushAmount > 0.0f, "Soft foliage overlap should report a brush contact amount");

    FlightState blockerPlane = makeFlightState(
        { blockerCollider.center.x, blockerCollider.center.y, blockerCollider.center.z },
        { 0.0f, 0.0f, 48.0f },
        0.5f,
        1.2f);
    blockerPlane.flightAngVel = { 0.0f, 0.0f, 0.0f };
    FlightCrashEvent propCrash {};
    requireKnownIssue(
        detectFlightPropCollision(blockerOnlyCache, params.decoration, blockerPlane, 42, propCrash),
        "Blocker prop overlap should trigger the prop-crash detection path");
    requireKnownIssue(propCrash.cause == FlightCrashCause::PropBlocker, "Prop crash detection should label the crash as a blocker-prop impact");

    FlightState brushPlane = makeFlightState(
        { brushCollider.center.x, brushCollider.center.y, brushCollider.center.z },
        { 0.0f, 0.0f, 48.0f },
        0.5f,
        1.2f);
    FlightCrashEvent brushCrash {};
    require(
        !detectFlightPropCollision(brushOnlyCache, params.decoration, brushPlane, 43, brushCrash),
        "Soft foliage overlap should not be treated as a hard crash",
        failed);

    FlightState walker {};
    walker.rot = quatIdentity();
    walker.pos = { blockerCollider.center.x, blockerCollider.center.y, blockerCollider.center.z };
    walker.vel = { 2.0f, 0.0f, 0.0f };
    resolveWalkingPropCollisions(terrainCache, params.decoration, walker);
    const float horizontalDistance = std::sqrt(
        ((walker.pos.x - blockerCollider.center.x) * (walker.pos.x - blockerCollider.center.x)) +
        ((walker.pos.z - blockerCollider.center.z) * (walker.pos.z - blockerCollider.center.z)));
    requireKnownIssue(
        horizontalDistance >= (blockerCollider.radius + kWalkingCollisionRadius - 0.05f),
        "Walking collision should resolve the actor outside blocker props");

    TerrainParams changedParams = params;
    changedParams.decoration.density += 0.35f;
    require(
        terrainParamsSignature(changedParams) != terrainParamsSignature(params),
        "Terrain decoration settings should participate in the terrain chunk signature",
        failed);
}

std::string buildSmokeHelloPacket(const AvatarManifest& avatar)
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

void runNetworkingSmokeChecks(bool& failed)
{
    AvatarManifest avatar = defaultAvatarManifest();
    avatar.role = "walking";
    avatar.callsign = "Smoke Pilot";
    avatar.radioChannel = 3;
    avatar.radioTx = true;
    avatar.plane.modelHash = "plane-alpha";
    avatar.plane.scale = 1.8f;
    avatar.plane.skinHash = "paint-alpha";
    avatar.plane.modelFormat = "glb";
    avatar.plane.forwardAxisYawDegrees = -32.0f;
    avatar.plane.importRotation = quatNormalize(quatFromAxisAngle({ 0.0f, 1.0f, 0.0f }, radians(18.0f)));
    avatar.plane.yawDegrees = 12.5f;
    avatar.plane.pitchDegrees = -3.25f;
    avatar.plane.rollDegrees = 4.75f;
    avatar.walking.modelHash = "walker-alpha";
    avatar.walking.scale = 1.2f;
    avatar.walking.skinHash = "walk-paint";
    avatar.walking.assetKey = "builtin:walking_biped";
    avatar.walking.modelFormat = "builtin";
    avatar.walking.forwardAxisYawDegrees = 0.0f;
    avatar.walking.importRotation = quatIdentity();
    avatar.walking.builtinWalkingRig = true;

    {
        const std::string helloPacket = buildSmokeHelloPacket(avatar);
        const std::string manifestPacket = buildAvatarManifestPacket(7, avatar);

        NetPlayerInput input {};
        input.tick = 31;
        input.role = "walking";
        input.reportedPos = { 48.0f, 26.0f, -12.0f };
        input.reportedRot = quatNormalize(quatFromAxisAngle({ 0.0f, 1.0f, 0.0f }, radians(32.0f)));
        input.reportedVel = { 6.0f, 0.5f, -3.0f };
        input.reportedAngVel = { 0.2f, 0.1f, -0.05f };
        input.reportedSimTick = 777;
        input.walkYaw = 1.75f;
        input.walkPitch = -0.6f;
        input.throttle = 0.85f;
        input.yokePitch = -0.25f;
        input.yokeYaw = 0.55f;
        input.manualElevatorTrim = -0.08f;
        input.manualRudderTrim = 0.05f;
        input.walkBackward = true;
        input.walkStrafeLeft = true;
        input.walkSprint = true;
        input.flightThrottleUp = true;
        input.flightAirBrakes = true;
        input.flightAfterburner = true;
        input.terrainEditOpId = 42;
        input.avatar = avatar;
        const std::string inputPacket = buildInputPacket(input);

        NetPlayerInput drivingInput = input;
        drivingInput.tick = 32;
        drivingInput.role = "driving";
        drivingInput.vehicleId = 17;
        drivingInput.driveThrottle = 0.72f;
        drivingInput.driveSteer = -0.38f;
        drivingInput.driveBrake = true;
        drivingInput.avatar.role = "driving";
        const std::string drivingInputPacket = buildInputPacket(drivingInput);

        NetPlayerSnapshot snapshot {};
        snapshot.id = 7;
        snapshot.ack = 31;
        snapshot.pos = { 12.0f, 24.0f, 36.0f };
        snapshot.rot = quatNormalize(quatFromAxisAngle({ 0.0f, 1.0f, 0.0f }, radians(18.0f)));
        snapshot.vel = { 3.0f, 4.0f, 5.0f };
        snapshot.angVel = { 0.1f, 0.2f, 0.3f };
        snapshot.throttle = 0.84f;
        snapshot.elevator = -0.12f;
        snapshot.aileron = 0.33f;
        snapshot.rudder = -0.14f;
        snapshot.simTick = 902;
        snapshot.timestamp = 23.75;
        snapshot.avatar = avatar;
        const std::string snapshotPacket = buildSnapshotPacket(snapshot);

        AvatarManifest drivingAvatar = avatar;
        drivingAvatar.role = "driving";
        NetPlayerSnapshot drivingSnapshot = snapshot;
        drivingSnapshot.id = 8;
        drivingSnapshot.ack = 32;
        drivingSnapshot.vehicleId = drivingInput.vehicleId;
        drivingSnapshot.avatar = drivingAvatar;
        const std::string drivingSnapshotPacket = buildSnapshotPacket(drivingSnapshot);

        const ModeSwitchPacket modeSwitch {
            44,
            "driving",
            38.0f,
            14.0f,
            -28.0f,
            drivingInput.vehicleId,
            0.8f,
            -0.2f
        };
        const std::string modeSwitchPacket = buildModeSwitchPacket(modeSwitch);

        const ConstructPublishPacket constructPublish = makeSmokeConstructPublishPacket();
        const std::string constructPublishPacket = buildConstructPublishPacket(constructPublish);
        SharedVehicleState vehicleState = makeSmokeVehicleState(41, constructPublish.blueprint);
        const std::string vehicleStatePacket = buildVehicleStatePacket(vehicleState);
        const VehicleSpawnPacket vehicleSpawn { vehicleState, "bootstrap" };
        const std::string vehicleSpawnPacket = buildVehicleSpawnPacket(vehicleSpawn);
        const VehicleSeatPacket vehicleSeat { vehicleState.vehicleId, 7, 0, true };
        const std::string vehicleSeatPacket = buildVehicleSeatPacket(vehicleSeat);

        const std::string voicePacket = buildVoiceStatePacket({ 7, 3, true });
        VoiceFramePacket voiceFrame {};
        voiceFrame.senderId = 7;
        voiceFrame.channel = 3;
        voiceFrame.data = std::string("VOICE_FRAME_SMOKE_PAYLOAD");
        const std::string voiceFramePacket = buildVoiceFramePacket(voiceFrame);
        const std::string craterPacket = buildCraterPacket({ 44.0f, 0.0f, -18.0f, 9.5f, 3.5f, 0.2f });
        const TerrainEditRequest terrainEditRequest {
            42,
            7,
            "terrain_add",
            { 18.0f, 22.0f, -14.0f },
            normalize(Vec3{ 0.0f, 1.0f, 0.25f }, { 0.0f, 1.0f, 0.0f }),
            14.0f,
            14.0f,
            12.75
        };
        const TerrainEditAck terrainEditAck {
            42,
            7,
            true,
            "terrain_add",
            { 18.0f, 22.0f, -14.0f },
            14.0f,
            14.0f,
            3,
            "applied"
        };
        const std::string terrainEditRequestPacket = buildTerrainEditRequestPacket(terrainEditRequest);
        const std::string terrainEditAckPacket = buildTerrainEditAckPacket(terrainEditAck);
        WorldInfoSnapshot worldInfo {};
        worldInfo.worldId = "smoke-world";
        worldInfo.formatVersion = 3;
        worldInfo.seed = 4242;
        worldInfo.worldShape = WorldShape::Plane;
        worldInfo.chunkSize = 256.0f;
        worldInfo.horizonRadiusMeters = 8192.0f;
        worldInfo.heightAmplitude = 132.0f;
        worldInfo.heightFrequency = 0.0024f;
        worldInfo.waterLevel = -11.0f;
        worldInfo.tunnelSeeds = {
            { 7.5f, true, { { 0.0f, 20.0f, 0.0f }, { 16.0f, 18.0f, 4.0f } } }
        };
        worldInfo.spawnX = 8.0f;
        worldInfo.spawnY = 16.0f;
        worldInfo.spawnZ = 24.0f;
        const std::string worldInfoPacket = buildWorldInfoPacket(worldInfo);

        require(!helloPacket.empty(), "HELLO packet builder returned an empty payload", failed);
        require(manifestPacket.find("AVATAR_MANIFEST|") == 0, "Avatar manifest packet should keep its packet type prefix", failed);

        int manifestId = 0;
        const auto parsedManifest = parseAvatarManifestPacket(manifestPacket, &manifestId);
        require(parsedManifest.has_value() && manifestId == 7, "Avatar manifest packet did not decode its player id", failed);
        if (parsedManifest.has_value()) {
            require(parsedManifest->callsign == avatar.callsign, "Avatar manifest callsign did not round-trip", failed);
            require(parsedManifest->radioChannel == avatar.radioChannel, "Avatar manifest radio channel did not round-trip", failed);
            require(parsedManifest->plane.modelHash == avatar.plane.modelHash, "Avatar manifest plane model hash did not round-trip", failed);
            require(parsedManifest->plane.modelFormat == avatar.plane.modelFormat, "Avatar manifest model format did not round-trip", failed);
            require(std::fabs(parsedManifest->plane.forwardAxisYawDegrees - avatar.plane.forwardAxisYawDegrees) < 1.0e-4f, "Avatar manifest forward-axis yaw did not round-trip", failed);
            require(parsedManifest->walking.builtinWalkingRig, "Avatar manifest walking rig flag did not round-trip", failed);
        }

        const auto parsedHello = parseAvatarManifestPacket("AVATAR_MANIFEST|" + helloPacket.substr(6));
        require(parsedHello.has_value(), "HELLO packet fields could not be parsed as an avatar manifest", failed);

        const auto parsedInput = parseInputPacket(inputPacket);
        require(parsedInput.has_value(), "Input packet did not parse", failed);
        if (parsedInput.has_value()) {
            require(parsedInput->tick == 31, "Input tick did not round-trip", failed);
            require(parsedInput->walkBackward && parsedInput->walkStrafeLeft && parsedInput->walkSprint, "Input button state did not round-trip", failed);
            require(std::fabs(parsedInput->yokeYaw - 0.55f) < 1.0e-4f, "Input analog state did not round-trip", failed);
            require(std::fabs(parsedInput->manualElevatorTrim + 0.08f) < 1.0e-4f, "Input elevator trim did not round-trip", failed);
            require(std::fabs(parsedInput->manualRudderTrim - 0.05f) < 1.0e-4f, "Input rudder trim did not round-trip", failed);
            require(parsedInput->reportedSimTick == 777, "Input reported sim tick did not round-trip", failed);
            require(std::fabs(parsedInput->reportedPos.x - 48.0f) < 1.0e-4f, "Input reported position did not round-trip", failed);
            require(std::fabs(parsedInput->reportedVel.z + 3.0f) < 1.0e-4f, "Input reported velocity did not round-trip", failed);
            require(parsedInput->avatar.plane.modelFormat == avatar.plane.modelFormat, "Input avatar model format did not round-trip", failed);
            require(parsedInput->avatar.walking.builtinWalkingRig, "Input avatar walking rig flag did not round-trip", failed);
            require(parsedInput->terrainEditOpId == input.terrainEditOpId, "Input terrain edit op id did not round-trip", failed);
        }

        const auto parsedDrivingInput = parseInputPacket(drivingInputPacket);
        require(parsedDrivingInput.has_value(), "Driving input packet did not parse", failed);
        if (parsedDrivingInput.has_value()) {
            require(parsedDrivingInput->role == "driving", "Driving input role did not round-trip", failed);
            require(parsedDrivingInput->vehicleId == drivingInput.vehicleId, "Driving input vehicle id did not round-trip", failed);
            require(std::fabs(parsedDrivingInput->driveThrottle - drivingInput.driveThrottle) < 1.0e-4f, "Driving input throttle did not round-trip", failed);
            require(std::fabs(parsedDrivingInput->driveSteer - drivingInput.driveSteer) < 1.0e-4f, "Driving input steer did not round-trip", failed);
            require(parsedDrivingInput->driveBrake == drivingInput.driveBrake, "Driving input brake flag did not round-trip", failed);
        }

        const auto parsedSnapshot = parseSnapshotPacket(snapshotPacket);
        require(parsedSnapshot.has_value(), "Snapshot packet did not parse", failed);
        if (parsedSnapshot.has_value()) {
            require(parsedSnapshot->id == 7 && parsedSnapshot->ack == 31, "Snapshot ids did not round-trip", failed);
            require(parsedSnapshot->avatar.role == "walking", "Snapshot avatar role did not round-trip", failed);
            require(parsedSnapshot->avatar.radioTx, "Snapshot radio state did not round-trip", failed);
            require(parsedSnapshot->avatar.plane.modelFormat == avatar.plane.modelFormat, "Snapshot avatar model format did not round-trip", failed);
            require(parsedSnapshot->avatar.walking.builtinWalkingRig, "Snapshot avatar walking rig flag did not round-trip", failed);
        }

        const auto parsedDrivingSnapshot = parseSnapshotPacket(drivingSnapshotPacket);
        require(parsedDrivingSnapshot.has_value(), "Driving snapshot packet did not parse", failed);
        if (parsedDrivingSnapshot.has_value()) {
            require(parsedDrivingSnapshot->avatar.role == "driving", "Driving snapshot avatar role did not round-trip", failed);
            require(parsedDrivingSnapshot->vehicleId == drivingSnapshot.vehicleId, "Driving snapshot vehicle id did not round-trip", failed);
        }

        const auto parsedModeSwitch = parseModeSwitchPacket(modeSwitchPacket);
        require(parsedModeSwitch.has_value(), "Mode-switch packet did not parse", failed);
        if (parsedModeSwitch.has_value()) {
            require(parsedModeSwitch->role == modeSwitch.role, "Mode-switch role did not round-trip", failed);
            require(parsedModeSwitch->vehicleId == modeSwitch.vehicleId, "Mode-switch vehicle id did not round-trip", failed);
            require(std::fabs(parsedModeSwitch->walkYaw - modeSwitch.walkYaw) < 1.0e-4f, "Mode-switch walk yaw did not round-trip", failed);
            require(std::fabs(parsedModeSwitch->walkPitch - modeSwitch.walkPitch) < 1.0e-4f, "Mode-switch walk pitch did not round-trip", failed);
        }

        const auto parsedConstructPublish = parseConstructPublishPacket(constructPublishPacket);
        require(parsedConstructPublish.has_value(), "Construct publish packet did not parse", failed);
        if (parsedConstructPublish.has_value()) {
            require(parsedConstructPublish->blueprint.constructId == constructPublish.blueprint.constructId, "Construct publish id did not round-trip", failed);
            require(parsedConstructPublish->state.authorPlayerId == constructPublish.state.authorPlayerId, "Construct publish author id did not round-trip", failed);
            require(parsedConstructPublish->blueprint.seatOffsets.size() == constructPublish.blueprint.seatOffsets.size(), "Construct publish seat offsets did not round-trip", failed);
            require(std::fabs(parsedConstructPublish->blueprint.suspensionTravel - constructPublish.blueprint.suspensionTravel) < 1.0e-4f, "Construct publish suspension travel did not round-trip", failed);
        }

        const auto parsedVehicleState = parseVehicleStatePacket(vehicleStatePacket);
        require(parsedVehicleState.has_value(), "Vehicle state packet did not parse", failed);
        if (parsedVehicleState.has_value()) {
            require(parsedVehicleState->vehicleId == vehicleState.vehicleId, "Vehicle state id did not round-trip", failed);
            require(parsedVehicleState->seatOccupants == vehicleState.seatOccupants, "Vehicle seat occupancy did not round-trip", failed);
            require(std::fabs(parsedVehicleState->surfaceFriction - vehicleState.surfaceFriction) < 1.0e-4f, "Vehicle state friction did not round-trip", failed);
            require(std::fabs(parsedVehicleState->cage.heave - vehicleState.cage.heave) < 1.0e-4f, "Vehicle cage heave did not round-trip", failed);
            require(std::fabs(parsedVehicleState->cage.wheelCompression[1] - vehicleState.cage.wheelCompression[1]) < 1.0e-4f, "Vehicle cage wheel compression did not round-trip", failed);
        }

        const auto parsedVehicleSpawn = parseVehicleSpawnPacket(vehicleSpawnPacket);
        require(parsedVehicleSpawn.has_value(), "Vehicle spawn packet did not parse", failed);
        if (parsedVehicleSpawn.has_value()) {
            require(parsedVehicleSpawn->reason == vehicleSpawn.reason, "Vehicle spawn reason did not round-trip", failed);
            require(parsedVehicleSpawn->state.vehicleId == vehicleState.vehicleId, "Vehicle spawn state did not round-trip", failed);
            require(std::fabs(parsedVehicleSpawn->state.cage.roll - vehicleState.cage.roll) < 1.0e-4f, "Vehicle spawn cage roll did not round-trip", failed);
        }

        const auto parsedVehicleSeat = parseVehicleSeatPacket(vehicleSeatPacket);
        require(parsedVehicleSeat.has_value(), "Vehicle seat packet did not parse", failed);
        if (parsedVehicleSeat.has_value()) {
            require(parsedVehicleSeat->vehicleId == vehicleSeat.vehicleId, "Vehicle seat vehicle id did not round-trip", failed);
            require(parsedVehicleSeat->playerId == vehicleSeat.playerId, "Vehicle seat player id did not round-trip", failed);
            require(parsedVehicleSeat->seatIndex == vehicleSeat.seatIndex, "Vehicle seat index did not round-trip", failed);
            require(parsedVehicleSeat->entering == vehicleSeat.entering, "Vehicle seat enter/exit flag did not round-trip", failed);
        }

        const auto parsedVoice = parseVoiceStatePacket(voicePacket);
        require(parsedVoice.has_value() && parsedVoice->channel == 3 && parsedVoice->transmitting, "Voice state did not round-trip", failed);

        const auto parsedVoiceFrame = parseVoiceFramePacket(voiceFramePacket);
        require(parsedVoiceFrame.has_value(), "Voice frame packet did not parse", failed);
        require(voiceFramePacket.find("id=") != std::string::npos, "Voice frame packet should include a sender id", failed);
        if (parsedVoiceFrame.has_value()) {
            require(parsedVoiceFrame->senderId == voiceFrame.senderId, "Voice frame sender id did not round-trip", failed);
            require(parsedVoiceFrame->channel == voiceFrame.channel, "Voice frame channel did not round-trip", failed);
            require(parsedVoiceFrame->data == voiceFrame.data, "Voice frame payload did not round-trip", failed);
        }

        const auto parsedCrater = parseCraterPacket(craterPacket);
        require(parsedCrater.has_value() && std::fabs(parsedCrater->radius - 9.5f) < 1.0e-4f, "Crater packet did not round-trip", failed);

        require(
            terrainEditRequestPacket.find("TERRAIN_EDIT_REQUEST|") == 0,
            "Terrain edit request packet should keep its packet type prefix",
            failed);
        const auto parsedTerrainEditRequest = parseTerrainEditRequestPacket(terrainEditRequestPacket);
        require(parsedTerrainEditRequest.has_value(), "Terrain edit request packet did not parse", failed);
        if (parsedTerrainEditRequest.has_value()) {
            require(parsedTerrainEditRequest->opId == terrainEditRequest.opId, "Terrain edit request op id did not round-trip", failed);
            require(parsedTerrainEditRequest->playerId == terrainEditRequest.playerId, "Terrain edit request player id did not round-trip", failed);
            require(parsedTerrainEditRequest->kind == terrainEditRequest.kind, "Terrain edit request kind did not round-trip", failed);
            require(std::fabs(parsedTerrainEditRequest->center.x - terrainEditRequest.center.x) < 1.0e-4f, "Terrain edit request center did not round-trip", failed);
            require(std::fabs(parsedTerrainEditRequest->radius - terrainEditRequest.radius) < 1.0e-4f, "Terrain edit request radius did not round-trip", failed);
            require(std::fabs(parsedTerrainEditRequest->magnitude - terrainEditRequest.magnitude) < 1.0e-4f, "Terrain edit request magnitude did not round-trip", failed);
            require(std::fabs(parsedTerrainEditRequest->requestedAt - terrainEditRequest.requestedAt) < 1.0e-4, "Terrain edit request timestamp did not round-trip", failed);
        }

        require(
            terrainEditAckPacket.find("TERRAIN_EDIT_ACK|") == 0,
            "Terrain edit ack packet should keep its packet type prefix",
            failed);
        const auto parsedTerrainEditAck = parseTerrainEditAckPacket(terrainEditAckPacket);
        require(parsedTerrainEditAck.has_value(), "Terrain edit ack packet did not parse", failed);
        if (parsedTerrainEditAck.has_value()) {
            require(parsedTerrainEditAck->opId == terrainEditAck.opId, "Terrain edit ack op id did not round-trip", failed);
            require(parsedTerrainEditAck->playerId == terrainEditAck.playerId, "Terrain edit ack player id did not round-trip", failed);
            require(parsedTerrainEditAck->accepted == terrainEditAck.accepted, "Terrain edit ack accepted flag did not round-trip", failed);
            require(parsedTerrainEditAck->kind == terrainEditAck.kind, "Terrain edit ack kind did not round-trip", failed);
            require(parsedTerrainEditAck->touchedChunks == terrainEditAck.touchedChunks, "Terrain edit ack chunk count did not round-trip", failed);
            require(parsedTerrainEditAck->reason == terrainEditAck.reason, "Terrain edit ack reason did not round-trip", failed);
        }

        const auto parsedWorldInfo = parseWorldInfoPacket(worldInfoPacket);
        require(parsedWorldInfo.has_value(), "World info packet did not parse", failed);
        if (parsedWorldInfo.has_value()) {
            require(parsedWorldInfo->worldId == "smoke-world", "World info packet world id did not round-trip", failed);
            require(parsedWorldInfo->worldShape == WorldShape::Plane, "World info packet world shape did not round-trip", failed);
            require(parsedWorldInfo->tunnelSeeds.size() == 1u, "World info packet tunnel seed list did not round-trip", failed);
            require(std::fabs(parsedWorldInfo->spawnY - 16.0f) < 1.0e-4f, "World info packet spawn position did not round-trip", failed);
        }

        RemotePeerState peer;
        appendRemoteSnapshot(peer, { 1.0, { 0.0f, 10.0f, 0.0f }, quatIdentity(), { 2.0f, 0.0f, 0.0f }, { 0.0f, 0.1f, 0.0f } });
        appendRemoteSnapshot(peer, { 2.0, { 2.0f, 10.0f, 0.0f }, quatIdentity(), { 2.0f, 0.0f, 0.0f }, { 0.0f, 0.1f, 0.0f } });
        const bool sampled = sampleRemotePeer(peer, 1.5, 0.0f);
        require(sampled, "Remote peer interpolation did not sample", failed);
        require(std::fabs(peer.displayPos.x - 1.0f) < 0.25f, "Remote peer interpolation did not blend positions", failed);
    }

    {
        auto [serverTransport, clientTransport] = LoopbackTransport::createLinkedPair();
        require(serverTransport->ready() && clientTransport->ready(), "Loopback transport pair did not report readiness", failed);

        const auto initialServerEvents = serverTransport->poll();
        const auto initialClientEvents = clientTransport->poll();
        require(
            initialServerEvents.size() == 1u && initialServerEvents.front().type == NetEvent::Type::Connected &&
                initialClientEvents.size() == 1u && initialClientEvents.front().type == NetEvent::Type::Connected,
            "Loopback transport did not emit the expected connection events",
            failed);

        require(clientTransport->send(1, static_cast<int>(TransportLane::Control), "PING|lane=0", true), "Loopback transport failed to send a control payload", failed);
        const auto serverEvents = serverTransport->poll();
        require(serverEvents.size() == 1u && serverEvents.front().peerId == 2 && serverEvents.front().payload == "PING|lane=0", "Loopback transport did not deliver the sent payload", failed);

        clientTransport->disconnectPeer(1);
        const auto serverDisconnect = serverTransport->poll();
        require(
            !serverDisconnect.empty() && serverDisconnect.front().type == NetEvent::Type::Disconnected,
            "Loopback transport did not propagate disconnect events",
            failed);
    }

    {
        BlobSyncState state = createBlobSyncState();
        const std::string rawBlob = std::string("SMOKE\0BLOB\0PAYLOAD", 18);
        const std::string blobHash = sha256Hex(rawBlob);
        BlobMetaPacket baseMeta;
        baseMeta.ownerId = "host";
        baseMeta.role = "plane";
        baseMeta.modelHash = "plane-alpha";

        const BlobOutgoingTransfer outgoing = prepareOutgoingBlobTransfer(state, "paint", blobHash, rawBlob, baseMeta, 8);
        require(outgoing.chunkCount > 0, "Outgoing blob transfer did not produce any chunks", failed);
        require(state.outgoing.find(blobTransferKey("paint", blobHash)) != state.outgoing.end(), "Outgoing blob transfer was not cached", failed);

        BlobSyncState receiver = createBlobSyncState();
        require(acceptIncomingBlobMeta(receiver, outgoing.meta).has_value(), "Incoming blob meta was not accepted", failed);

        std::optional<std::pair<BlobMetaPacket, std::string>> completed;
        for (int index = 0; index < outgoing.chunkCount; ++index) {
            BlobChunkPacket chunkPacket;
            chunkPacket.kind = outgoing.kind;
            chunkPacket.hash = outgoing.hash;
            chunkPacket.index = index + 1;
            chunkPacket.data = outgoing.chunks[static_cast<std::size_t>(index)];
            completed = acceptIncomingBlobChunk(receiver, chunkPacket);
        }
        require(completed.has_value(), "Incoming blob transfer never completed", failed);
        if (completed.has_value()) {
            require(completed->first.hash == blobHash, "Incoming blob meta hash did not round-trip", failed);
            require(completed->second == rawBlob, "Incoming blob payload did not round-trip", failed);
        }

        AvatarBlobCache cache;
        AvatarBlobRecord record;
        record.kind = "paint";
        record.hash = blobHash;
        record.raw = rawBlob;
        record.meta = outgoing.meta;
        cache.put(record);
        require(cache.has("paint", blobHash), "Avatar blob cache did not store the completed blob", failed);
        require(cache.get("paint", blobHash) != nullptr, "Avatar blob cache lookup failed", failed);
    }

    {
        auto clientTransport = std::make_shared<RecordingTransport>(std::vector<NetPeerId> { 1 });
        ClientReplicationState client {};
        client.transport = clientTransport;
        client.serverPeerId = 1;
        client.outboundReliable.push_back("HELLO|callsign=Pilot");
        client.outboundReliable.push_back("BLOB_META|kind=model:glb|hash=blob-a|ownerId=9|role=plane|modelHash=blob-a|rawBytes=12|encodedBytes=16|chunkSize=8|chunkCount=2");
        client.outboundReliable.push_back("BLOB_CHUNK|kind=model:glb|hash=blob-a|idx=1|data=QUJDREVGR0g=");
        client.outboundReliable.push_back("BLOB_CHUNK|kind=model:glb|hash=blob-a|idx=2|data=SUpLTE1O");

        bool failNextBlobChunk = true;
        clientTransport->sendBehavior =
            [&failNextBlobChunk](NetPeerId, int, std::string_view payload, bool) {
                const std::string type = HostedNetworkingDetail::packetType(std::string(payload));
                if (failNextBlobChunk && type == "BLOB_CHUNK") {
                    failNextBlobChunk = false;
                    return false;
                }
                return true;
            };

        flushClientOutbound(client, 1.0);

        require(client.outboundReliable.size() == 2u, "Reliable blob packets should remain queued after the first paced blob flush", failed);
        if (client.outboundReliable.size() == 2u) {
            require(
                HostedNetworkingDetail::packetType(client.outboundReliable[0]) == "BLOB_CHUNK" &&
                    HostedNetworkingDetail::packetType(client.outboundReliable[1]) == "BLOB_CHUNK",
                "Reliable blob retry queue should keep the failed chunk and the remaining chunks",
                failed);
        }

        clientTransport->clearSentPackets();
        flushClientOutbound(client, 1.5);
        require(client.outboundReliable.size() == 2u, "Reliable blob packets should remain queued after the first deferred chunk fails", failed);
        flushClientOutbound(client, 1.76);
        flushClientOutbound(client, 1.78);
        require(client.outboundReliable.empty(), "Reliable blob retry queue did not drain after the transport recovered", failed);
        require(clientTransport->sentPackets.size() >= 2u, "Reliable blob retry should resend the deferred chunk packets", failed);
    }

    {
        auto clientTransport = std::make_shared<RecordingTransport>(std::vector<NetPeerId> { 1 });
        ClientReplicationState client {};
        client.transport = clientTransport;
        client.serverPeerId = 1;

        const std::string blobData(2048u, 'A');
        for (int index = 1; index <= 80; ++index) {
            client.outboundReliable.push_back(
                "BLOB_CHUNK|kind=model:glb|hash=blob-b|idx=" + std::to_string(index) + "|data=" + blobData);
        }

        flushClientOutbound(client, 2.0);

        require(
            !client.outboundReliable.empty() && client.outboundReliable.size() < 80u,
            "Large reliable blob flushes should be paced across frames instead of draining in one burst",
            failed);
    }

    {
        auto hostTransport = std::make_shared<RecordingTransport>(std::vector<NetPeerId> { 1 });
        HostedWorldServer server(hostTransport);

        AvatarBlobRecord record;
        record.kind = "model:glb";
        record.raw = std::string(30000u, 'B');
        record.hash = sha256Hex(record.raw);
        record.meta.kind = record.kind;
        record.meta.hash = record.hash;
        record.meta.modelHash = record.hash;
        record.meta.ownerId = "host";
        record.meta.role = "plane";
        server.cacheBlob(record);

        const TerrainParams terrain = defaultTerrainParams();
        const TerrainFieldContext terrainContext = createTerrainFieldContext(terrain);
        hostTransport->pushMessage(1, static_cast<int>(TransportLane::Control), buildBlobRequestPacket(record.kind, record.hash), true);
        server.serviceIncoming(terrain, terrainContext, nullptr);

        server.update(1.0, 1.0f / 60.0f, terrainContext, defaultFlightConfig(), {}, nullptr);
        require(
            hostTransport->sentPackets.size() == 1u,
            "Hosted blob replies should be paced instead of sending the entire transfer in one update",
            failed);

        server.update(1.01, 1.0f / 60.0f, terrainContext, defaultFlightConfig(), {}, nullptr);
        require(
            hostTransport->sentPackets.size() == 1u,
            "Hosted blob pacing should respect the inter-flush interval",
            failed);

        server.update(1.02, 1.0f / 60.0f, terrainContext, defaultFlightConfig(), {}, nullptr);
        require(
            hostTransport->sentPackets.size() > 1u,
            "Hosted blob pacing should resume on the next allowed flush window",
            failed);
    }

    {
        auto clientTransport = std::make_shared<RecordingTransport>(std::vector<NetPeerId> { 1 });
        ClientReplicationState client {};
        client.transport = clientTransport;
        TerrainParams mirroredTerrain = defaultTerrainParams();
        (void)clientTransport->poll();
        const std::string rawBlob = std::string("SMOKE_REMOTE_MODEL");
        const std::string remoteModelHash = sha256Hex(rawBlob);

        AvatarManifest remoteAvatar = avatar;
        remoteAvatar.role = "plane";
        remoteAvatar.plane.modelFormat = "glb";
        remoteAvatar.plane.modelHash = remoteModelHash;
        clientTransport->pushMessage(1, static_cast<int>(TransportLane::Control), buildAvatarManifestPacket(12, remoteAvatar), true);

        serviceClientReplication(client, 3.0, mirroredTerrain, nullptr, nullptr);

        require(
            client.peers.find(12) != client.peers.end() && client.peers[12].connected,
            "Client replication did not keep the remote avatar manifest peer",
            failed);
        require(client.outboundReliable.size() == 1u, "Remote avatar manifest should queue exactly one blob request", failed);
        if (client.outboundReliable.size() == 1u) {
            require(
                HostedNetworkingDetail::packetType(client.outboundReliable.front()) == "BLOB_REQUEST",
                "Remote avatar manifest should queue a BLOB_REQUEST",
                failed);
            const auto requestFields = parseKeyValueFields(splitPacketFields(client.outboundReliable.front()));
            require(requestFields["kind"] == "model:glb", "Queued blob request kind did not match the remote avatar format", failed);
            require(requestFields["hash"] == remoteAvatar.plane.modelHash, "Queued blob request hash did not match the remote avatar model hash", failed);
        }
        require(
            client.nextBlobRequestAtByKey.find(blobTransferKey("model:glb", remoteAvatar.plane.modelHash)) != client.nextBlobRequestAtByKey.end(),
            "Blob request rate limiter did not track the requested remote model",
            failed);

        BlobSyncState transferState = createBlobSyncState();
        BlobMetaPacket baseMeta;
        baseMeta.ownerId = "12";
        baseMeta.role = "plane";
        baseMeta.modelHash = remoteAvatar.plane.modelHash;
        const BlobOutgoingTransfer outgoing = prepareOutgoingBlobTransfer(
            transferState,
            "model:glb",
            remoteAvatar.plane.modelHash,
            rawBlob,
            baseMeta,
            8);
        clientTransport->pushMessage(
            1,
            static_cast<int>(TransportLane::Blob),
            encodePacket("BLOB_META", {
                { "kind", outgoing.meta.kind },
                { "hash", outgoing.meta.hash },
                { "ownerId", outgoing.meta.ownerId },
                { "role", outgoing.meta.role },
                { "modelHash", outgoing.meta.modelHash },
                { "rawBytes", std::to_string(outgoing.meta.rawBytes) },
                { "encodedBytes", std::to_string(outgoing.meta.encodedBytes) },
                { "chunkSize", std::to_string(outgoing.meta.chunkSize) },
                { "chunkCount", std::to_string(outgoing.meta.chunkCount) }
            }),
            true);
        for (int index = 0; index < outgoing.chunkCount; ++index) {
            clientTransport->pushMessage(
                1,
                static_cast<int>(TransportLane::Blob),
                encodePacket("BLOB_CHUNK", {
                    { "kind", outgoing.kind },
                    { "hash", outgoing.hash },
                    { "idx", std::to_string(index + 1) },
                    { "data", outgoing.chunks[static_cast<std::size_t>(index)] }
                }),
                true);
        }

        serviceClientReplication(client, 4.0, mirroredTerrain, nullptr, nullptr);

        require(client.blobCache.has("model:glb", remoteAvatar.plane.modelHash), "Client blob cache did not store the remote avatar model", failed);
        if (const AvatarBlobRecord* cached = client.blobCache.get("model:glb", remoteAvatar.plane.modelHash); cached != nullptr) {
            require(cached->meta.modelHash == remoteAvatar.plane.modelHash, "Cached remote avatar model hash did not match the manifest", failed);
        } else {
            require(false, "Client blob cache lookup failed for the remote avatar model", failed);
        }
        if (const auto statusIt = client.blobStatusByKey.find(blobTransferKey("model:glb", remoteAvatar.plane.modelHash));
            statusIt != client.blobStatusByKey.end()) {
            require(statusIt->second.phase == BlobTransferPhase::Received, "Remote avatar blob transfer did not reach the received phase", failed);
        } else {
            require(false, "Client blob status was not tracked for the remote avatar model", failed);
        }

        client.outboundReliable.clear();
        clientTransport->pushMessage(1, static_cast<int>(TransportLane::Control), buildAvatarManifestPacket(12, remoteAvatar), true);
        serviceClientReplication(client, 5.0, mirroredTerrain, nullptr, nullptr);
        require(client.outboundReliable.empty(), "Client should not re-request a cached remote avatar blob", failed);
    }

    {
        auto hostTransport = std::make_shared<RecordingTransport>(std::vector<NetPeerId> { 1 });
        HostedWorldServer server(hostTransport);
        const TerrainParams terrain = defaultTerrainParams();
        const TerrainFieldContext terrainContext = createTerrainFieldContext(terrain);

        server.serviceIncoming(terrain, terrainContext, nullptr);

        TerrainEditRequest request;
        request.opId = 91;
        request.kind = "terrain_remove";
        request.center = { 12.0f, 6.0f, -4.0f };
        request.radius = 12.0f;
        request.magnitude = -15.0f;
        request.requestedAt = 2.5;
        hostTransport->pushMessage(1, static_cast<int>(TransportLane::Control), buildTerrainEditRequestPacket(request), true);
        server.serviceIncoming(terrain, terrainContext, nullptr);

        const std::vector<HostedTerrainEditRequest> pendingTerrainEdits = server.drainPendingTerrainEdits();
        require(pendingTerrainEdits.size() == 1u, "Hosted server did not queue exactly one terrain edit request", failed);
        if (pendingTerrainEdits.size() == 1u) {
            require(pendingTerrainEdits.front().peerId == 1, "Hosted terrain edit request did not preserve the sender peer id", failed);
            require(pendingTerrainEdits.front().request.opId == request.opId, "Hosted terrain edit request op id did not round-trip through the queue", failed);
            require(pendingTerrainEdits.front().request.playerId == 2, "Hosted terrain edit request should be rewritten to the authoritative player id", failed);
            require(pendingTerrainEdits.front().request.kind == request.kind, "Hosted terrain edit request kind did not survive queueing", failed);
        }
    }

    {
        auto clientTransport = std::make_shared<RecordingTransport>(std::vector<NetPeerId> { 1 });
        ClientReplicationState client {};
        client.transport = clientTransport;
        TerrainEditRequest pendingTerrainEdit;
        pendingTerrainEdit.opId = 91;
        pendingTerrainEdit.playerId = 2;
        pendingTerrainEdit.kind = "terrain_remove";
        pendingTerrainEdit.center = { 12.0f, 6.0f, -4.0f };
        pendingTerrainEdit.radius = 12.0f;
        pendingTerrainEdit.magnitude = -15.0f;
        pendingTerrainEdit.requestedAt = 2.5;
        TerrainEditAck appliedTerrainEditAck;
        appliedTerrainEditAck.opId = pendingTerrainEdit.opId;
        appliedTerrainEditAck.playerId = pendingTerrainEdit.playerId;
        appliedTerrainEditAck.accepted = true;
        appliedTerrainEditAck.kind = pendingTerrainEdit.kind;
        appliedTerrainEditAck.center = pendingTerrainEdit.center;
        appliedTerrainEditAck.radius = pendingTerrainEdit.radius;
        appliedTerrainEditAck.magnitude = pendingTerrainEdit.magnitude;
        appliedTerrainEditAck.touchedChunks = 3;
        appliedTerrainEditAck.reason = "applied";
        client.pendingTerrainEditsById[pendingTerrainEdit.opId] = pendingTerrainEdit;
        TerrainParams mirroredTerrain = defaultTerrainParams();
        clientTransport->pushMessage(1, static_cast<int>(TransportLane::Control), buildTerrainEditAckPacket(appliedTerrainEditAck), true);

        serviceClientReplication(client, 6.0, mirroredTerrain, nullptr, nullptr);

        require(client.pendingTerrainEditsById.empty(), "Terrain edit ack should retire the pending terrain edit request", failed);
        require(client.terrainEditAcks.size() == 1u, "Terrain edit ack should be queued for the app layer", failed);
        if (client.terrainEditAcks.size() == 1u) {
            require(client.terrainEditAcks.front().accepted, "Queued terrain edit ack should preserve acceptance state", failed);
            require(client.terrainEditAcks.front().reason == appliedTerrainEditAck.reason, "Queued terrain edit ack should preserve the reason text", failed);
        }
    }

    {
        auto clientTransport = std::make_shared<RecordingTransport>(std::vector<NetPeerId> { 1 });
        ClientReplicationState client {};
        client.transport = clientTransport;
        client.localAvatar = avatar;
        TerrainParams mirroredTerrain = defaultTerrainParams();
        RemotePeerState localPeer;

        NetPlayerSnapshot localSnapshot {};
        localSnapshot.id = 9;
        localSnapshot.ack = 14;
        localSnapshot.pos = { 180.0f, 240.0f, -96.0f };
        localSnapshot.rot = quatNormalize(quatFromAxisAngle({ 0.0f, 1.0f, 0.0f }, radians(26.0f)));
        localSnapshot.vel = { 12.0f, 0.0f, 44.0f };
        localSnapshot.angVel = { 0.0f, 0.3f, 0.0f };
        localSnapshot.simTick = 901;
        localSnapshot.timestamp = 4.0;
        localSnapshot.avatar = avatar;

        clientTransport->pushMessage(1, static_cast<int>(TransportLane::Snapshot), buildSnapshotPacket(localSnapshot), false);
        clientTransport->pushMessage(1, static_cast<int>(TransportLane::Control), buildJoinOkPacket(9), true);

        serviceClientReplication(client, 4.0, mirroredTerrain, nullptr, &localPeer);

        require(client.joinAcknowledged && client.localPlayerId == 9, "Client replication did not accept JOIN_OK for the local player", failed);
        require(client.peers.find(9) == client.peers.end(), "Client replication should not keep a stale remote peer entry for the local player id", failed);
        require(localPeer.id == 9 && localPeer.connected, "Client replication should keep the local peer identity after JOIN_OK", failed);
    }

    {
        auto clientTransport = std::make_shared<RecordingTransport>(std::vector<NetPeerId> { 1 });
        ClientReplicationState client {};
        client.transport = clientTransport;
        TerrainParams mirroredTerrain = defaultTerrainParams();
        const ConstructPublishPacket publish = makeSmokeConstructPublishPacket(104, 6, 3);
        SharedVehicleState replicatedVehicle = makeSmokeVehicleState(88, publish.blueprint);
        replicatedVehicle.vel = { 9.0f, 0.0f, 5.0f };
        replicatedVehicle.cage.roll = 0.11f;
        const VehicleSeatPacket enterSeat { replicatedVehicle.vehicleId, 9, 0, true };
        const VehicleSeatPacket exitSeat { replicatedVehicle.vehicleId, 9, 0, false };

        clientTransport->pushMessage(1, static_cast<int>(TransportLane::Control), buildConstructPublishPacket(publish), true);
        clientTransport->pushMessage(1, static_cast<int>(TransportLane::Control), buildVehicleSpawnPacket({ replicatedVehicle, "bootstrap" }), true);
        clientTransport->pushMessage(1, static_cast<int>(TransportLane::Snapshot), buildVehicleStatePacket(replicatedVehicle), false);
        clientTransport->pushMessage(1, static_cast<int>(TransportLane::Control), buildVehicleSeatPacket(enterSeat), true);

        serviceClientReplication(client, 7.0, mirroredTerrain, nullptr, nullptr);

        require(
            client.constructBlueprints.find(publish.blueprint.constructId) != client.constructBlueprints.end(),
            "Client replication did not store replicated construct blueprints",
            failed);
        require(
            client.constructStates.find(publish.state.constructId) != client.constructStates.end(),
            "Client replication did not store replicated construct state",
            failed);
        require(
            client.sharedVehicles.find(replicatedVehicle.vehicleId) != client.sharedVehicles.end(),
            "Client replication did not store replicated shared vehicles",
            failed);
        if (const auto vehicleIt = client.sharedVehicles.find(replicatedVehicle.vehicleId); vehicleIt != client.sharedVehicles.end()) {
            require(vehicleIt->second.driverPlayerId == enterSeat.playerId, "Client replication did not apply vehicle seat-enter events", failed);
            require(
                !vehicleIt->second.seatOccupants.empty() && vehicleIt->second.seatOccupants.front() == enterSeat.playerId,
                "Client replication did not track the occupied driver seat",
                failed);
            require(std::fabs(vehicleIt->second.cage.roll - replicatedVehicle.cage.roll) < 1.0e-4f, "Client replication lost replicated soft-body cage state", failed);
        }

        clientTransport->pushMessage(1, static_cast<int>(TransportLane::Control), buildVehicleSeatPacket(exitSeat), true);
        serviceClientReplication(client, 8.0, mirroredTerrain, nullptr, nullptr);
        if (const auto vehicleIt = client.sharedVehicles.find(replicatedVehicle.vehicleId); vehicleIt != client.sharedVehicles.end()) {
            require(vehicleIt->second.driverPlayerId == 0, "Client replication did not clear the driver id on vehicle exit", failed);
            require(
                !vehicleIt->second.seatOccupants.empty() && vehicleIt->second.seatOccupants.front() == 0,
                "Client replication did not clear the occupied seat on vehicle exit",
                failed);
        }
    }

    {
        auto hostTransport = std::make_shared<RecordingTransport>(std::vector<NetPeerId> { 1 });
        HostedWorldServer server(hostTransport);
        const TerrainParams terrain = defaultTerrainParams();
        const TerrainFieldContext terrainContext = createTerrainFieldContext(terrain);
        const ConstructPublishPacket publish = makeSmokeConstructPublishPacket(133, 9, 1);
        SharedVehicleState vehicle = makeSmokeVehicleState(1337, publish.blueprint);
        server.setSharedWorldState(
            { { publish.blueprint.constructId, publish.blueprint } },
            { { publish.state.constructId, publish.state } },
            { { vehicle.vehicleId, vehicle } });

        server.serviceIncoming(terrain, terrainContext, nullptr);
        hostTransport->pushMessage(1, static_cast<int>(TransportLane::Control), buildSmokeHelloPacket(avatar), true);
        server.serviceIncoming(terrain, terrainContext, nullptr);

        hostTransport->clearSentPackets();
        const VehicleSeatPacket enterSeat { vehicle.vehicleId, 2, 0, true };
        hostTransport->pushMessage(1, static_cast<int>(TransportLane::Control), buildVehicleSeatPacket(enterSeat), true);
        server.serviceIncoming(terrain, terrainContext, nullptr);

        require(
            server.sharedVehicles().find(vehicle.vehicleId) != server.sharedVehicles().end(),
            "Hosted vehicle enter test lost the shared vehicle state",
            failed);
        if (const auto vehicleIt = server.sharedVehicles().find(vehicle.vehicleId); vehicleIt != server.sharedVehicles().end()) {
            require(vehicleIt->second.driverPlayerId == 2, "Hosted server did not apply driver seat entry", failed);
            require(
                !vehicleIt->second.seatOccupants.empty() && vehicleIt->second.seatOccupants.front() == 2,
                "Hosted server did not mark the occupied driver seat",
                failed);
        }
        if (const HostedPlayerState* player = mutableHostedPlayer(server, 2); player != nullptr) {
            require(player->mode == PlayerMode::Driving, "Hosted server did not switch the entering player into driving mode", failed);
            require(player->vehicleId == vehicle.vehicleId, "Hosted server did not bind the entering player to the active vehicle", failed);
        } else {
            require(false, "Hosted vehicle enter test could not find the replicated player state", failed);
        }
        require(
            std::any_of(
                hostTransport->sentPackets.begin(),
                hostTransport->sentPackets.end(),
                [&enterSeat](const RecordingTransport::SentPacket& packet) {
                    return packet.peerId == 1 &&
                        packet.lane == static_cast<int>(TransportLane::Control) &&
                        packet.payload == buildVehicleSeatPacket(enterSeat);
                }),
            "Hosted server did not relay vehicle seat-enter packets back to connected peers",
            failed);

        hostTransport->clearSentPackets();
        const VehicleSeatPacket exitSeat { vehicle.vehicleId, 2, 0, false };
        hostTransport->pushMessage(1, static_cast<int>(TransportLane::Control), buildVehicleSeatPacket(exitSeat), true);
        server.serviceIncoming(terrain, terrainContext, nullptr);
        if (const auto vehicleIt = server.sharedVehicles().find(vehicle.vehicleId); vehicleIt != server.sharedVehicles().end()) {
            require(vehicleIt->second.driverPlayerId == 0, "Hosted server did not clear the driver on vehicle exit", failed);
            require(
                !vehicleIt->second.seatOccupants.empty() && vehicleIt->second.seatOccupants.front() == 0,
                "Hosted server did not clear the occupied seat on vehicle exit",
                failed);
        }
        if (const HostedPlayerState* player = mutableHostedPlayer(server, 2); player != nullptr) {
            require(player->mode == PlayerMode::Walking, "Hosted server did not return the exiting player to walking mode", failed);
            require(player->vehicleId == 0, "Hosted server did not clear the exiting player's active vehicle id", failed);
        }
        require(
            std::any_of(
                hostTransport->sentPackets.begin(),
                hostTransport->sentPackets.end(),
                [&exitSeat](const RecordingTransport::SentPacket& packet) {
                    return packet.peerId == 1 &&
                        packet.lane == static_cast<int>(TransportLane::Control) &&
                        packet.payload == buildVehicleSeatPacket(exitSeat);
                }),
            "Hosted server did not relay vehicle seat-exit packets back to connected peers",
            failed);
    }

    {
        const std::filesystem::path root = repoRoot();
        const std::filesystem::path tempRoot = root / "build/native-smoke-net-temp";
        std::error_code ec;
        std::filesystem::remove_all(tempRoot, ec);
        std::filesystem::create_directories(tempRoot, ec);

        WorldStoreOptions hostOptions;
        hostOptions.name = "net_host";
        hostOptions.storageRoot = tempRoot / "host-world";
        hostOptions.createIfMissing = true;
        hostOptions.regionSize = 8;
        hostOptions.chunkResolution = 8;
        hostOptions.groundParams = defaultTerrainParams();
        hostOptions.groundParams.seed = 9090;
        hostOptions.groundParams.chunkSize = 128.0f;
        hostOptions.groundParams.worldRadius = 48000.0f;
        hostOptions.groundParams.minY = -2200.0f;
        hostOptions.groundParams.maxY = 22000.0f;
        hostOptions.groundParams.baseHeight = -180.0f;
        hostOptions.groundParams.waterLevel = -24.0f;
        hostOptions.groundParams.heightAmplitude = 6200.0f;
        hostOptions.groundParams.heightFrequency = 0.00021f;
        hostOptions.groundParams.macroWarpAmplitude = 4100.0f;
        hostOptions.groundParams.caveEnabled = true;
        hostOptions.groundParams.caveStrength = 58.0f;
        hostOptions.groundParams.tunnelCount = 1;
        hostOptions.groundParams.mountainFront.coastalBandEnd = 21000.0f;
        hostOptions.groundParams.roads.desiredRoadCount = 3;
        hostOptions.groundParams.roads.stepMeters = 36.0f;
        TerrainRoadPath explicitRoad;
        explicitRoad.nodes.push_back({ { -220.0f, 420.0f, 900.0f }, { 1.0f, 0.0f, 0.0f }, 10.2f, 18.0f, 0.03f, 0.0f });
        explicitRoad.nodes.push_back({ { 120.0f, 435.0f, 1120.0f }, { 0.9f, 0.0f, 0.3f }, 10.2f, 18.0f, 0.04f, 0.01f });
        hostOptions.groundParams.explicitRoadPaths.push_back(explicitRoad);

        WorldStoreOptions mirrorOptions = hostOptions;
        mirrorOptions.name = "net_client";
        mirrorOptions.storageRoot = tempRoot / "client-world";

        std::string hostError;
        std::string mirrorError;
        auto hostWorldOpt = WorldStore::open(hostOptions, &hostError);
        auto mirrorWorldOpt = WorldStore::open(mirrorOptions, &mirrorError);
        require(hostWorldOpt.has_value(), "Host world failed to open for networking smoke: " + hostError, failed);
        require(mirrorWorldOpt.has_value(), "Mirror world failed to open for networking smoke: " + mirrorError, failed);
        if (!hostWorldOpt.has_value() || !mirrorWorldOpt.has_value()) {
            std::filesystem::remove_all(tempRoot, ec);
            return;
        }

        WorldStore hostWorld = std::move(*hostWorldOpt);
        WorldStore mirrorWorld = std::move(*mirrorWorldOpt);
        const auto craterResult = hostWorld.applyCrater({ 32.0f, 0.0f, 24.0f, 10.0f, 4.0f, 0.15f });
        require(craterResult.first && !craterResult.second.empty(), "Host networking world did not generate a dirty crater region", failed);

        auto [hostTransport, clientTransport] = LoopbackTransport::createLinkedPair();
        HostedWorldServer server(hostTransport);
        const ConstructPublishPacket sharedConstruct = makeSmokeConstructPublishPacket(91, 12, 1);
        SharedVehicleState sharedVehicle = makeSmokeVehicleState(64, sharedConstruct.blueprint);
        sharedVehicle.pos = { -120.0f, 14.0f, 980.0f };
        sharedVehicle.vel = { 4.0f, 0.0f, 9.5f };
        sharedVehicle.cage.heave = 0.22f;
        sharedVehicle.cage.pitch = 0.05f;
        sharedVehicle.cage.roll = -0.03f;
        server.setSharedWorldState(
            { { sharedConstruct.blueprint.constructId, sharedConstruct.blueprint } },
            { { sharedConstruct.state.constructId, sharedConstruct.state } },
            { { sharedVehicle.vehicleId, sharedVehicle } });
        server.setLocalAuthoritativeState(makeFlightState({ 0.0f, 220.0f, 0.0f }), FlightRuntimeState {}, PlayerMode::Walking, 0, 0.0f, 0.0f, avatar);
        const TerrainFieldContext hostTerrainContext = createTerrainFieldContext(hostOptions.groundParams);

        (void)clientTransport->poll();
        server.serviceIncoming(hostOptions.groundParams, hostTerrainContext, &hostWorld);

        clientTransport->send(1, static_cast<int>(TransportLane::Control), buildSmokeHelloPacket(avatar), true);
        server.serviceIncoming(hostOptions.groundParams, hostTerrainContext, &hostWorld);

        const std::vector<NetEvent> bootstrapEvents = clientTransport->poll();
        bool sawJoin = false;
        bool sawWorldInfo = false;
        bool sawWorldSync = false;
        bool sawChunk = false;
        bool sawConstructPublish = false;
        bool sawVehicleSpawn = false;
        TerrainParams mirroredTerrain = defaultTerrainParams();
        for (const NetEvent& event : bootstrapEvents) {
            if (event.type != NetEvent::Type::Message) {
                continue;
            }

            const std::string type = HostedNetworkingDetail::packetType(event.payload);
            if (type == "JOIN_OK") {
                sawJoin = true;
            } else if (type == "WORLD_INFO") {
                sawWorldInfo = true;
                if (const auto info = parseWorldInfoPacket(event.payload); info.has_value()) {
                    std::string worldError;
                    require(mirrorWorld.applyWorldInfo(*info, &worldError), "Mirror world failed to apply WORLD_INFO: " + worldError, failed);
                }
            } else if (type == "WORLD_SYNC") {
                sawWorldSync = true;
                require(applyWorldSyncPacket(event.payload, mirroredTerrain), "WORLD_SYNC packet did not apply to the mirrored terrain state", failed);
            } else if (type == "CHUNK_STATE" || type == "CHUNK_PATCH") {
                sawChunk = true;
                if (const auto chunk = parseChunkPacket(event.payload); chunk.has_value()) {
                    require(mirrorWorld.applyChunkState(*chunk), "Mirror world failed to apply CHUNK_STATE/CHUNK_PATCH", failed);
                }
            } else if (type == "CONSTRUCT_PUBLISH") {
                sawConstructPublish = true;
                const auto publish = parseConstructPublishPacket(event.payload);
                require(publish.has_value(), "Hosted bootstrap construct publish did not parse", failed);
                if (publish.has_value()) {
                    require(
                        publish->blueprint.constructId == sharedConstruct.blueprint.constructId &&
                            publish->state.revisionId == sharedConstruct.state.revisionId,
                        "Hosted bootstrap construct publish did not preserve the shared construct revision",
                        failed);
                }
            } else if (type == "VEHICLE_SPAWN") {
                sawVehicleSpawn = true;
                const auto spawn = parseVehicleSpawnPacket(event.payload);
                require(spawn.has_value(), "Hosted bootstrap vehicle spawn did not parse", failed);
                if (spawn.has_value()) {
                    require(spawn->state.vehicleId == sharedVehicle.vehicleId, "Hosted bootstrap vehicle id did not match the shared world vehicle", failed);
                    require(std::fabs(spawn->state.cage.heave - sharedVehicle.cage.heave) < 1.0e-4f, "Hosted bootstrap vehicle cage state did not round-trip", failed);
                }
            }
        }

        require(sawJoin, "Hosted bootstrap did not emit JOIN_OK", failed);
        require(sawWorldInfo, "Hosted bootstrap did not emit WORLD_INFO", failed);
        require(sawWorldSync, "Hosted bootstrap did not emit WORLD_SYNC", failed);
        require(sawChunk, "Hosted bootstrap did not emit any chunk state", failed);
        require(sawConstructPublish, "Hosted bootstrap did not emit construct publication for shared vehicles", failed);
        require(sawVehicleSpawn, "Hosted bootstrap did not emit vehicle spawn state for shared vehicles", failed);
        if (!terrainParamsEquivalent(hostOptions.groundParams, mirroredTerrain)) {
            const TerrainParams expected = normalizeTerrainParams(hostOptions.groundParams);
            const TerrainParams actual = normalizeTerrainParams(mirroredTerrain);
            std::cerr
                << "WORLD_SYNC diff:"
                << " worldRadius=" << expected.worldRadius << "/" << actual.worldRadius
                << " waterLevel=" << expected.waterLevel << "/" << actual.waterLevel
                << " caveEnabled=" << expected.caveEnabled << "/" << actual.caveEnabled
                << " tunnelCount=" << expected.tunnelCount << "/" << actual.tunnelCount
                << " roads=" << expected.roads.desiredRoadCount << "/" << actual.roads.desiredRoadCount
                << " roadPaths=" << expected.explicitRoadPaths.size() << "/" << actual.explicitRoadPaths.size()
                << " roadPathEqual=" << terrainRoadPathListsEqual(expected.explicitRoadPaths, actual.explicitRoadPaths)
                << " craterEqual=" << terrainCraterListsEqual(expected.dynamicCraters, actual.dynamicCraters)
                << " tunnelSeedEqual=" << terrainTunnelSeedListsEqual(expected.explicitTunnelSeeds, actual.explicitTunnelSeeds)
                << " mountainFrontEnd=" << expected.mountainFront.coastalBandEnd << "/" << actual.mountainFront.coastalBandEnd
                << " surfaceOnlyMeshing=" << expected.surfaceOnlyMeshing << "/" << actual.surfaceOnlyMeshing
                << "\n";
            if (!expected.explicitRoadPaths.empty() && !actual.explicitRoadPaths.empty()) {
                const TerrainRoadNode& expectedNode = expected.explicitRoadPaths.front().nodes.front();
                const TerrainRoadNode& actualNode = actual.explicitRoadPaths.front().nodes.front();
                std::cerr
                    << "WORLD_SYNC road node diff:"
                    << " posX=" << expectedNode.position.x << "/" << actualNode.position.x
                    << " forwardX=" << expectedNode.forward.x << "/" << actualNode.forward.x
                    << " forwardZ=" << expectedNode.forward.z << "/" << actualNode.forward.z
                    << " width=" << expectedNode.widthMeters << "/" << actualNode.widthMeters
                    << " cutWidth=" << expectedNode.cutWidthMeters << "/" << actualNode.cutWidthMeters
                    << " grade=" << expectedNode.grade << "/" << actualNode.grade
                    << " curvature=" << expectedNode.curvature << "/" << actualNode.curvature
                    << "\n";
            }
        }
        require(
            terrainParamsEquivalent(hostOptions.groundParams, mirroredTerrain),
            "WORLD_SYNC did not round-trip the host terrain parameters into the mirrored terrain state",
            failed);

        const WorldMeta mirrorMeta = mirrorWorld.getMeta();
        require(mirrorMeta.worldId == "net_host", "Mirror world did not adopt the host world id", failed);
        require(mirrorWorld.sampleHeightDelta(32.0f, 24.0f) != 0.0f, "Mirror world did not receive the host crater terrain", failed);

        server.update(1.0, 1.0f / 30.0f, createTerrainFieldContext(hostOptions.groundParams), defaultFlightConfig(), {}, &hostWorld);
        const std::vector<NetEvent> snapshotEvents = clientTransport->poll();
        bool sawSnapshot = false;
        bool sawVehicleState = false;
        for (const NetEvent& event : snapshotEvents) {
            if (event.type != NetEvent::Type::Message) {
                continue;
            }
            const std::string packetType = HostedNetworkingDetail::packetType(event.payload);
            if (packetType == "SNAPSHOT") {
                sawSnapshot = true;
                const auto snapshot = parseSnapshotPacket(event.payload);
                require(snapshot.has_value() && snapshot->id == 1, "Host snapshot did not encode the local player state", failed);
            } else if (packetType == "VEHICLE_STATE") {
                sawVehicleState = true;
                const auto vehicle = parseVehicleStatePacket(event.payload);
                require(vehicle.has_value(), "Hosted vehicle-state replication packet did not parse", failed);
                if (vehicle.has_value()) {
                    require(vehicle->vehicleId == sharedVehicle.vehicleId, "Hosted vehicle-state replication lost the vehicle id", failed);
                    const auto authoritativeVehicle = server.sharedVehicles().find(sharedVehicle.vehicleId);
                    require(authoritativeVehicle != server.sharedVehicles().end(), "Hosted vehicle-state replication lost the authoritative shared vehicle", failed);
                    if (authoritativeVehicle != server.sharedVehicles().end()) {
                        require(
                            std::fabs(vehicle->cage.pitch - authoritativeVehicle->second.cage.pitch) < 1.0e-4f,
                            "Hosted vehicle-state replication lost the soft-body cage pitch",
                            failed);
                    }
                }
            }
        }
        require(sawSnapshot, "Hosted server did not emit any snapshots", failed);
        require(sawVehicleState, "Hosted server did not emit any shared vehicle state replication", failed);

        auto voiceTransport = std::make_shared<RecordingTransport>(std::vector<NetPeerId> { 2, 3 });
        server.setTransport(voiceTransport);
        server.serviceIncoming(hostOptions.groundParams, hostTerrainContext, &hostWorld);

        auto* peerTwo = mutableHostedPlayer(server, 2);
        auto* peerThree = mutableHostedPlayer(server, 3);
        require(peerTwo != nullptr && peerThree != nullptr, "Hosted server did not keep remote players alive for voice routing checks", failed);
        if (peerTwo != nullptr && peerThree != nullptr) {
            VoiceFramePacket relayFrame {};
            relayFrame.senderId = 2;
            relayFrame.channel = 3;
            relayFrame.data = "HOSTED_VOICE_RELAY_SMOKE";
            const std::string relayPacket = buildVoiceFramePacket(relayFrame);

            voiceTransport->clearSentPackets();
            peerTwo->avatar.radioChannel = 3;
            peerThree->avatar.radioChannel = 3;
            peerTwo->actor.pos = { 0.0f, 220.0f, 0.0f };
            peerThree->actor.pos = { 8.0f, 220.0f, 0.0f };

            voiceTransport->pushMessage(2, static_cast<int>(TransportLane::Voice), buildVoiceStatePacket({ 2, 3, true }), true);
            voiceTransport->pushMessage(3, static_cast<int>(TransportLane::Voice), buildVoiceStatePacket({ 3, 3, true }), true);
            voiceTransport->pushMessage(2, static_cast<int>(TransportLane::Voice), relayPacket, false);
            server.serviceIncoming(hostOptions.groundParams, hostTerrainContext, &hostWorld);

            const auto localVoiceFrames = collectHostLocalVoiceFrames(server);
            require(!localVoiceFrames.empty(), "Hosted server did not expose remote voice frames to the local host", failed);
            if (!localVoiceFrames.empty()) {
                require(
                    voiceFrameSenderId(localVoiceFrames.front()) == 2 &&
                        voiceFrameChannel(localVoiceFrames.front()) == relayFrame.channel &&
                        voiceFramePayloadText(localVoiceFrames.front()) == relayFrame.data,
                    "Hosted server did not preserve the remote voice frame for local playback",
                    failed);
            }

            const auto relayedToPeerThree = std::find_if(
                voiceTransport->sentPackets.begin(),
                voiceTransport->sentPackets.end(),
                [&relayPacket](const RecordingTransport::SentPacket& packet) {
                    return packet.peerId == 3 && packet.lane == static_cast<int>(TransportLane::Voice) && packet.payload == relayPacket;
                });
            require(relayedToPeerThree != voiceTransport->sentPackets.end(), "Hosted server did not relay voice to the matching remote peer", failed);

            voiceTransport->clearSentPackets();
            peerTwo->avatar.radioChannel = 4;
            voiceTransport->pushMessage(2, static_cast<int>(TransportLane::Voice), relayPacket, false);
            server.serviceIncoming(hostOptions.groundParams, hostTerrainContext, &hostWorld);
            require(
                std::any_of(
                    voiceTransport->sentPackets.begin(),
                    voiceTransport->sentPackets.end(),
                    [&relayPacket](const RecordingTransport::SentPacket& packet) {
                        return packet.peerId == 3 && packet.lane == static_cast<int>(TransportLane::Voice) && packet.payload == relayPacket;
                    }),
                "Hosted server should route voice using the frame channel rather than stale avatar state",
                failed);

            voiceTransport->clearSentPackets();
            peerThree->avatar.radioChannel = 4;
            voiceTransport->pushMessage(2, static_cast<int>(TransportLane::Voice), relayPacket, false);
            server.serviceIncoming(hostOptions.groundParams, hostTerrainContext, &hostWorld);
            require(
                std::none_of(
                    voiceTransport->sentPackets.begin(),
                    voiceTransport->sentPackets.end(),
                    [&relayPacket](const RecordingTransport::SentPacket& packet) {
                        return packet.peerId == 3 && packet.lane == static_cast<int>(TransportLane::Voice) && packet.payload == relayPacket;
                    }),
                "Hosted server should not relay voice across mismatched radio channels",
                failed);

            voiceTransport->clearSentPackets();
            peerThree->avatar.radioChannel = 3;
            peerThree->actor.pos = { 250000.0f, 220.0f, 0.0f };
            voiceTransport->pushMessage(2, static_cast<int>(TransportLane::Voice), relayPacket, false);
            server.serviceIncoming(hostOptions.groundParams, hostTerrainContext, &hostWorld);
            require(
                std::none_of(
                    voiceTransport->sentPackets.begin(),
                    voiceTransport->sentPackets.end(),
                    [&relayPacket](const RecordingTransport::SentPacket& packet) {
                        return packet.peerId == 3 && packet.lane == static_cast<int>(TransportLane::Voice) && packet.payload == relayPacket;
                    }),
                "Hosted server should not relay voice outside the active AOI",
                failed);
        }

        std::filesystem::remove_all(tempRoot, ec);
    }
}

}  // namespace

int main()
{
    SDL_Init(0);

    bool failed = false;
    const std::filesystem::path root = repoRoot();
    const std::filesystem::path modelPath = smokeModelPath();
    require(!modelPath.empty(), "Missing smoke test model asset under portSource/Assets/Models", failed);
    if (modelPath.empty()) {
        SDL_Quit();
        return 1;
    }

    if (modelPath.extension() == ".glb" || modelPath.extension() == ".gltf") {
        std::string gltfError;
        auto model = loadGltf(modelPath, &gltfError);
        require(model.has_value(), "Textured glTF failed to load: " + gltfError, failed);
        if (model.has_value()) {
            require(model->hasTexCoords, "Loaded glTF did not populate texture coordinates", failed);
            require(!model->materials.empty(), "Loaded glTF did not populate material metadata", failed);
            require(!model->images.empty(), "Loaded glTF did not populate decoded images", failed);
        }
    }

    const std::filesystem::path tempRoot = root / "build/native-smoke-temp";
    std::error_code ec;
    std::filesystem::remove_all(tempRoot, ec);
    std::filesystem::create_directories(tempRoot, ec);

    const std::filesystem::path settingsPath = tempRoot / "native_settings.ini";
    const std::filesystem::path hudSettingsPath = tempRoot / "HUD-settings.ini";
    {
        std::ofstream file(settingsPath, std::ios::binary | std::ios::trunc);
        file << "model.source_path=" << modelPath.generic_string() << "\n";
        file << "character.walking.scale=1.5\n";
        file << "paint.walking.hash=\n";
    }

    UiState uiState = defaultUiState();
    GraphicsSettings graphicsSettings = defaultGraphicsSettings();
    LightingSettings lightingSettings = defaultLightingSettings();
    HudSettings hudSettings = defaultHudSettings();
    ControlProfile controls = defaultControlProfile();
    AircraftProfile planeProfile {};
    planeProfile.visualPrefs.scale = 3.0f;
    TerrainParams terrainParams = defaultTerrainParams();
    VisualPreferenceData walkingPrefs {};
    walkingPrefs.scale = 1.0f;
    std::string preferenceError;
    {
        std::ofstream file(settingsPath, std::ios::binary | std::ios::app);
        file << "character.plane.forward_axis_yaw_degrees=-35\n";
        file << "aircraft.plane.audio.base_rpm=51\n";
        file << "aircraft.plane.audio.engine_frequency_scale=1.10\n";
        file << "ui.walking_move_speed=18\n";
        file << "ui.ui_scale=1.3\n";
        file << "ui.scale_hud_with_ui=1\n";
        file << "graphics.window_mode=borderless\n";
        file << "graphics.resolution_width=1920\n";
        file << "graphics.resolution_height=1080\n";
        file << "lighting.show_sun_marker=1\n";
        file << "lighting.sun_yaw_degrees=35\n";
        file << "lighting.marker_size=260\n";
        file << "lighting.gi_specular=0.23\n";
        file << "lighting.gi_bounce=0.17\n";
        file << "lighting.sky_tint_g=0.88\n";
        file << "lighting.fog_density=0.0007\n";
        file << "lighting.fog_b=0.91\n";
        file << "terrain.props_enabled=1\n";
        file << "terrain.prop_density=1.7\n";
        file << "terrain.prop_density_near=1.3\n";
        file << "terrain.prop_density_mid=0.7\n";
        file << "terrain.prop_density_far=0.2\n";
        file << "terrain.prop_shore_brush_density=1.4\n";
        file << "terrain.prop_rock_density=1.1\n";
        file << "terrain.prop_tree_line_offset=-24\n";
        file << "terrain.prop_collision=0\n";
        file << "terrain.prop_seed_offset=222\n";
        file << "online.voice_loopback=1\n";
        file << "controls.flight_pitch_down.primary=key:26\n";
        file << "controls.voice_ptt.primary=key:" << static_cast<int>(SDL_SCANCODE_B) << "\n";
    }
    {
        std::ofstream file(hudSettingsPath, std::ios::binary | std::ios::trunc);
        file << "hud.show_speedometer=0\n";
        file << "hud.show_crosshair=0\n";
        file << "hud.speedometer_redline_kph=900\n";
        file << "hud.style.map.x=0.42\n";
        file << "hud.style.map.y=0.18\n";
        file << "hud.style.map.width_scale=1.4\n";
        file << "hud.style.map.height_scale=0.9\n";
        file << "hud.style.map.text_r=101\n";
        file << "hud.style.debug.background_opacity=96\n";
        file << "hud.show_peer_indicators=1\n";
    }
    OnlineSettings onlineSettings {};
    require(loadPreferences(settingsPath, uiState, graphicsSettings, lightingSettings, hudSettings, onlineSettings, controls, planeProfile, terrainParams, walkingPrefs, &preferenceError),
        "Preference file failed to load: " + preferenceError,
        failed);
    std::string hudPreferenceError;
    require(loadHudPreferences(hudSettingsPath, hudSettings, &hudPreferenceError),
        "HUD preference file failed to load: " + hudPreferenceError,
        failed);
    syncUiStateFromHud(uiState, hudSettings);
    require(planeProfile.visualPrefs.hasStoredPath, "Legacy model.source_path did not migrate into plane preferences", failed);
    require(planeProfile.visualPrefs.sourcePath == modelPath, "Migrated plane source path does not match stored legacy path", failed);
    require(std::abs(walkingPrefs.scale - 1.5f) < 0.001f, "Walking role scale did not load from character.walking.scale", failed);
    require(std::abs(uiState.walkingMoveSpeed - 18.0f) < 0.001f, "Walking move speed did not load from preferences", failed);
    require(std::abs(uiState.uiScale - 1.3f) < 0.001f, "UI scale did not load from preferences", failed);
    require(uiState.scaleHudWithUi, "Scale HUD with UI toggle did not load from preferences", failed);
    require(std::fabs(terrainParams.decoration.density - 1.7f) < 0.001f, "Terrain prop density did not load from preferences", failed);
    require(std::fabs(terrainParams.decoration.nearDensityScale - 1.3f) < 0.001f, "Near-field terrain prop density did not load from preferences", failed);
    require(std::fabs(terrainParams.decoration.farDensityScale - 0.2f) < 0.001f, "Far-field terrain prop density did not load from preferences", failed);
    require(!terrainParams.decoration.collisionEnabled, "Terrain prop collision toggle did not load from preferences", failed);
    require(terrainParams.decoration.seedOffset == 222, "Terrain prop seed offset did not load from preferences", failed);
    require(std::abs(planeProfile.visualPrefs.forwardAxisYawDegrees + 35.0f) < 0.001f, "Plane forward-axis calibration did not load from preferences", failed);
    require(std::abs(planeProfile.propAudioConfig.baseRpm - 51.0f) < 0.001f, "Aircraft-local prop audio base RPM did not load from preferences", failed);
    require(graphicsSettings.windowMode == WindowMode::Borderless, "Graphics window mode did not load from preferences", failed);
    require(graphicsSettings.resolutionWidth == 1920 && graphicsSettings.resolutionHeight == 1080, "Graphics resolution did not load from preferences", failed);
    require(lightingSettings.showSunMarker, "Lighting sun-marker toggle did not load from preferences", failed);
    require(std::abs(lightingSettings.sunYawDegrees - 35.0f) < 0.001f, "Lighting sun yaw did not load from preferences", failed);
    require(std::abs(lightingSettings.markerSize - 260.0f) < 0.001f, "Lighting sun marker size did not load from preferences", failed);
    require(std::abs(lightingSettings.specularAmbient - 0.23f) < 0.001f, "Lighting GI specular did not load from preferences", failed);
    require(std::abs(lightingSettings.bounceStrength - 0.17f) < 0.001f, "Lighting GI bounce did not load from preferences", failed);
    require(std::abs(lightingSettings.skyTint.y - 0.88f) < 0.001f, "Lighting sky tint did not load from preferences", failed);
    require(std::abs(lightingSettings.fogDensity - 0.0007f) < 0.00001f, "Lighting fog density did not load from preferences", failed);
    require(std::abs(lightingSettings.fogColor.z - 0.91f) < 0.001f, "Lighting fog color did not load from preferences", failed);
    require(onlineSettings.voiceLoopback, "Online voice loopback toggle did not load from preferences", failed);
    require(!hudSettings.showSpeedometer, "HUD speedometer toggle did not load from preferences", failed);
    require(!hudSettings.showCrosshair, "HUD crosshair toggle did not load from HUD preferences", failed);
    require(hudSettings.speedometerRedlineKph == 900, "HUD speedometer redline did not load from preferences", failed);
    require(hudSettings.instruments.airspeed.redlineKph == 900, "Legacy-only HUD airspeed settings should migrate into the six-pack instrument", failed);
    require(std::abs(hudSettings.mapPanel.x - 0.42f) < 0.001f, "HUD map X position did not load from HUD preferences", failed);
    require(std::abs(hudSettings.mapPanel.widthScale - 1.4f) < 0.001f, "HUD map width scale did not load from HUD preferences", failed);
    require(hudSettings.mapPanel.textColor.r == 101, "HUD map text color did not load from HUD preferences", failed);
    require(hudSettings.debugFooter.backgroundOpacity == 96, "HUD debug background opacity did not load from HUD preferences", failed);
    require(hudSettings.showPeerIndicators, "HUD peer-indicator toggle did not load from HUD preferences", failed);
    {
        const std::filesystem::path mixedHudSettingsPath = tempRoot / "HUD-mixed-settings.ini";
        {
            std::ofstream file(mixedHudSettingsPath, std::ios::binary | std::ios::trunc);
            file << "hud.speedometer_max_kph=980\n";
            file << "hud.speedometer_redline_kph=910\n";
            file << "hud.instrument.airspeed.max_kph=820\n";
            file << "hud.instrument.airspeed.redline_kph=640\n";
            file << "hud.instrument.airspeed.major_step_kph=80\n";
        }

        HudSettings mixedHudSettings = defaultHudSettings();
        std::string mixedHudError;
        require(
            loadHudPreferences(mixedHudSettingsPath, mixedHudSettings, &mixedHudError),
            "Mixed HUD preference file failed to load: " + mixedHudError,
            failed);
        require(
            mixedHudSettings.instruments.airspeed.maxKph == 820 &&
                mixedHudSettings.speedometerMaxKph == 820,
            "Instrument airspeed max should remain authoritative when legacy HUD keys are also present",
            failed);
        require(
            mixedHudSettings.instruments.airspeed.redlineKph == 640 &&
                mixedHudSettings.speedometerRedlineKph == 640,
            "Instrument airspeed redline should remain authoritative when legacy HUD keys are also present",
            failed);
        require(
            mixedHudSettings.instruments.airspeed.majorStepKph == 80,
            "Instrument HUD settings should still load when mixed with legacy speedometer keys",
            failed);
    }
    {
        const ControlActionBinding* pitchDown = findControlAction(controls, InputActionId::FlightPitchDown);
        require(pitchDown != nullptr, "Pitch down control binding missing after preference load", failed);
        require(
            pitchDown != nullptr &&
                pitchDown->slots[0].kind == BindingKind::Key &&
                pitchDown->slots[0].scancode == SDL_SCANCODE_W,
            "Control binding round-trip did not restore the configured primary slot",
            failed);

        const ControlActionBinding* walkForward = findControlAction(controls, InputActionId::WalkForward);
        require(walkForward != nullptr, "Walk forward control binding missing after preference load", failed);
        require(
            walkForward != nullptr && walkForward->slots[0].scancode == SDL_SCANCODE_W,
            "Supported walking control rows should retain their default primary slot when not explicitly changed",
            failed);

        const ControlActionBinding* voicePtt = findControlAction(controls, InputActionId::VoicePushToTalk);
        require(voicePtt != nullptr, "Voice push-to-talk control binding missing after preference load", failed);
        require(
            voicePtt != nullptr &&
                voicePtt->supported &&
                voicePtt->configurable &&
                voicePtt->slots[0].kind == BindingKind::Key &&
                voicePtt->slots[0].scancode == SDL_SCANCODE_B,
            "Voice push-to-talk control binding should load, remain supported, and stay configurable",
            failed);
    }

    {
        require(bindingModifiersMatch(SDL_KMOD_CTRL, SDL_KMOD_LCTRL), "Modifier matching should treat left ctrl as ctrl", failed);
        require(
            bindingModifiersMatch(
                static_cast<SDL_Keymod>(SDL_KMOD_CTRL | SDL_KMOD_SHIFT),
                static_cast<SDL_Keymod>(SDL_KMOD_RCTRL | SDL_KMOD_LSHIFT)),
            "Modifier matching should preserve multi-modifier combinations across left/right variants",
            failed);

        const ControlProfile defaultControls = defaultControlProfile();
        require(
            controlActionTriggeredByWheel(defaultControls, InputActionId::FlightTrimUp, 1, SDL_KMOD_LCTRL),
            "Default ctrl+wheel-up trim binding should trigger with left ctrl",
            failed);
        require(
            controlActionTriggeredByWheel(defaultControls, InputActionId::FlightTrimDown, -1, SDL_KMOD_RCTRL),
            "Default ctrl+wheel-down trim binding should trigger with right ctrl",
            failed);
        require(
            controlActionTriggeredByKey(defaultControls, InputActionId::PaintUndo, SDL_SCANCODE_Z, SDL_KMOD_LCTRL),
            "Default ctrl+Z paint binding should trigger with left ctrl",
            failed);
        require(
            controlActionTriggeredByKey(defaultControls, InputActionId::FlightRudderTrimLeft, SDL_SCANCODE_LEFTBRACKET, SDL_KMOD_NONE) &&
                controlActionTriggeredByKey(defaultControls, InputActionId::FlightRudderTrimRight, SDL_SCANCODE_RIGHTBRACKET, SDL_KMOD_NONE),
            "Default rudder-trim key bindings should trigger with bracket keys",
            failed);

        const ControlActionBinding* voicePtt = findControlAction(defaultControls, InputActionId::VoicePushToTalk);
        const ControlActionBinding* rudderTrimLeft = findControlAction(defaultControls, InputActionId::FlightRudderTrimLeft);
        const ControlActionBinding* rudderTrimRight = findControlAction(defaultControls, InputActionId::FlightRudderTrimRight);
        require(voicePtt != nullptr, "Voice push-to-talk binding should exist in the default control profile", failed);
        require(
            controlActionSupported(InputActionId::VoicePushToTalk) &&
                controlActionSupported(InputActionId::FlightRudderTrimLeft) &&
                controlActionSupported(InputActionId::FlightRudderTrimRight) &&
                controlActionConfigurable(InputActionId::VoicePushToTalk) &&
                controlActionConfigurable(InputActionId::FlightRudderTrimLeft) &&
                controlActionConfigurable(InputActionId::FlightRudderTrimRight) &&
                rudderTrimLeft != nullptr &&
                rudderTrimRight != nullptr &&
                voicePtt->supported &&
                voicePtt->configurable &&
                voicePtt->slots[0].kind == BindingKind::Key,
            "Voice push-to-talk and rudder trim actions should be supported and configurable in native controls",
            failed);
    }

    {
        PauseState promptState;
        beginModelPathPrompt(promptState, CharacterSubTab::Player, "C:/Models/Test.glb");
        require(promptState.promptActive, "Model prompt should activate when opened", failed);
        require(promptState.promptRole == CharacterSubTab::Player, "Model prompt should preserve the requested role", failed);
        require(promptState.promptCursor == static_cast<int>(promptState.promptText.size()), "Model prompt cursor should start at the end of the initial text", failed);
        moveMenuPromptCursor(promptState, -4);
        require(eraseMenuPromptText(promptState, true), "Model prompt backspace editing should remove characters", failed);
        require(insertMenuPromptText(promptState, "_patched"), "Model prompt text insertion should append at the cursor", failed);
        clearMenuPrompt(promptState);
        require(!promptState.promptActive && promptState.promptText.empty(), "Model prompt clear should fully reset transient prompt state", failed);

        const char* launchArgv[] { "TrueFlight.exe", "+connect_lobby", "76561198000000000" };
        require(
            parseConnectLobbyLaunchArgument(static_cast<int>(std::size(launchArgv)), const_cast<char**>(launchArgv)) == 76561198000000000ull,
            "Steam connect_lobby launch parsing should accept split arguments",
            failed);
        const char* inlineLaunchArgv[] { "TrueFlight.exe", "lobby:76561198000000001" };
        require(
            parseConnectLobbyLaunchArgument(static_cast<int>(std::size(inlineLaunchArgv)), const_cast<char**>(inlineLaunchArgv)) == 76561198000000001ull,
            "Steam connect_lobby launch parsing should accept inline lobby tokens",
            failed);

        PauseState confirmState;
        requestMenuConfirmation(confirmState, 2, "Confirm quit.", 10.0f, 1.0f);
        require(menuConfirmationMatches(confirmState, 2, 10.5f), "Menu confirmation should remain active before its timeout", failed);
        refreshMenuConfirmation(confirmState, 11.1f);
        require(!confirmState.confirmPending, "Menu confirmation should expire after its timeout", failed);

        ControlProfile clearableControls = defaultControlProfile();
        PauseState controlsMenu;
        controlsMenu.controlsSelection = 0;
        controlsMenu.controlsSlot = 1;
        require(clearSelectedControlBindingSlot(controlsMenu, clearableControls), "Controls shortcut helper should clear configurable binding slots", failed);
        const ControlActionBinding* clearedPitchDown = findControlAction(clearableControls, InputActionId::FlightPitchDown);
        require(
            clearedPitchDown != nullptr && clearedPitchDown->slots[1].kind == BindingKind::None,
            "Clearing a selected control slot should unbind that slot",
            failed);

        int voiceIndex = -1;
        for (std::size_t actionIndex = 0; actionIndex < clearableControls.actions.size(); ++actionIndex) {
            if (clearableControls.actions[actionIndex].id == InputActionId::VoicePushToTalk) {
                voiceIndex = static_cast<int>(actionIndex);
                break;
            }
        }
        require(voiceIndex >= 0, "Voice push-to-talk binding should exist for smoke checks", failed);
        if (voiceIndex >= 0) {
            controlsMenu.controlsSelection = voiceIndex;
            controlsMenu.controlsSlot = 0;
            require(
                clearSelectedControlBindingSlot(controlsMenu, clearableControls),
                "Controls shortcut helper should clear the voice push-to-talk slot",
                failed);
            const ControlActionBinding* voicePtt = findControlAction(clearableControls, InputActionId::VoicePushToTalk);
            require(
                voicePtt != nullptr && voicePtt->slots[0].kind == BindingKind::None,
                "Voice push-to-talk should be clearable like the other supported bindings",
                failed);
        }
    }

    {
        PlaneVisualState planeVisual;
        planeVisual.defaultScale = 3.0f;
        setBuiltinPlaneModel(planeVisual);
        applyVisualPreferenceData(planeVisual, planeProfile.visualPrefs);
        const Quat offset = composeVisualRotationOffset(planeVisual);
        const float visualYawDegrees = degrees(getStableYawFromRotation(offset));
        require(std::fabs(visualYawDegrees + 35.0f) < 1.0f, "Forward-axis calibration should be applied independently of the flight attitude", failed);

        PlaneVisualState walkingVisual;
        walkingVisual.defaultScale = 1.0f;
        setBuiltinPlaneModel(walkingVisual);
        applyVisualPreferenceData(walkingVisual, walkingPrefs);

        const std::filesystem::path savedSettingsPath = tempRoot / "saved_settings.ini";
        const std::filesystem::path savedHudSettingsPath = tempRoot / "saved_hud_settings.ini";
        std::string savePreferenceError;
        require(
            savePreferences(savedSettingsPath, uiState, graphicsSettings, lightingSettings, hudSettings, onlineSettings, controls, planeProfile, terrainParams, planeVisual, walkingVisual, &savePreferenceError),
            "Preference file failed to save: " + savePreferenceError,
            failed);
        require(
            saveHudPreferences(savedHudSettingsPath, hudSettings, &savePreferenceError),
            "HUD preference file failed to save: " + savePreferenceError,
            failed);

        std::ifstream savedFile(savedSettingsPath, std::ios::binary);
        const std::string savedContents((std::istreambuf_iterator<char>(savedFile)), std::istreambuf_iterator<char>());
        std::ifstream savedHudFile(savedHudSettingsPath, std::ios::binary);
        const std::string savedHudContents((std::istreambuf_iterator<char>(savedHudFile)), std::istreambuf_iterator<char>());
        require(
            savedContents.find("aircraft.plane.audio.base_rpm=51") != std::string::npos,
            "Saved preferences did not persist aircraft-local prop audio keys",
            failed);
        require(
            savedContents.find("ui.walking_move_speed=18") != std::string::npos &&
            savedContents.find("ui.ui_scale=1.3") != std::string::npos &&
                savedContents.find("ui.scale_hud_with_ui=1") != std::string::npos &&
                savedContents.find("graphics.window_mode=borderless") != std::string::npos &&
                savedContents.find("lighting.show_sun_marker=1") != std::string::npos &&
                savedContents.find("lighting.sun_yaw_degrees=35") != std::string::npos &&
                savedContents.find("lighting.gi_specular=0.23") != std::string::npos &&
                savedContents.find("lighting.fog_b=0.91") != std::string::npos &&
                savedContents.find("online.voice_loopback=1") != std::string::npos &&
                savedContents.find("hud.show_speedometer=0") != std::string::npos &&
                savedContents.find("terrain.prop_density=1.7") != std::string::npos &&
                savedContents.find("terrain.prop_collision=0") != std::string::npos,
            "Saved preferences did not persist UI, graphics, lighting, and HUD keys",
            failed);
        require(
            savedContents.find("controls.flight_pitch_down.primary=key:26") != std::string::npos,
            "Saved preferences did not persist supported control bindings",
            failed);
        require(
            savedContents.find(std::string("controls.voice_ptt.primary=key:") + std::to_string(static_cast<int>(SDL_SCANCODE_B))) != std::string::npos &&
                savedContents.find("hud.show_peer_indicators=") == std::string::npos,
            "Supported voice push-to-talk bindings should be written back to native_settings.ini",
            failed);
        require(
            savedHudContents.find("hud.show_crosshair=0") != std::string::npos &&
                savedHudContents.find("hud.style.map.x=0.42") != std::string::npos &&
                savedHudContents.find("hud.style.map.width_scale=1.4") != std::string::npos &&
                savedHudContents.find("hud.style.debug.background_opacity=96") != std::string::npos &&
                savedHudContents.find("hud.instrument.airspeed.redline_kph=900") != std::string::npos &&
                savedHudContents.find("hud.show_peer_indicators=1") != std::string::npos,
            "Saved HUD preferences did not persist element layout/style keys correctly",
            failed);
    }

    {
        LightingSettings analyticLighting = defaultLightingSettings();
        analyticLighting.sunTint = { 1.0f, 0.55f, 0.40f };
        analyticLighting.skyTint = { 0.70f, 1.15f, 0.90f };
        analyticLighting.exposureEv = 0.8f;

        const RendererLightingState fogEnabled = evaluateRendererLightingState(analyticLighting, true);
        const RendererLightingState fogDisabled = evaluateRendererLightingState(analyticLighting, false);
        require(fogEnabled.fogDensity > 0.0f, "Renderer lighting evaluation should preserve fog density when horizon fog is enabled", failed);
        require(fogDisabled.fogDensity == 0.0f, "Renderer lighting evaluation should zero fog density when horizon fog is disabled", failed);
        require(fogEnabled.lightColor.y < fogEnabled.lightColor.x, "Sun tint should affect evaluated light color", failed);
        require(fogEnabled.skyColor.y > fogEnabled.skyColor.x, "Sky tint should affect the evaluated sky color", failed);
        require(fogEnabled.backgroundColor.x > 0.0f && fogEnabled.backgroundColor.y > 0.0f && fogEnabled.backgroundColor.z > 0.0f,
            "Renderer lighting evaluation should produce a visible background color",
            failed);
    }

    {
        GraphicsSettings farClipSettings = defaultGraphicsSettings();
        farClipSettings.drawDistance = 9200.0f;
        const TerrainFieldContext terrainContext = createTerrainFieldContext(defaultTerrainParams());
        const Camera farClipCamera = buildRenderCamera(makeFlightState(), terrainContext, uiState, PlayerMode::Flight, 0.0f, 0.0f, computeWorldFarClip(farClipSettings));
        require(
            farClipCamera.farClipMeters > farClipSettings.drawDistance,
            "Render camera far clip should track the configured gameplay draw distance",
            failed);
    }

    {
        const GltfDetail::Mat4 nonUniformScale = GltfDetail::makeTrsMatrix({ 0.0f, 0.0f, 0.0f }, quatIdentity(), { 2.0f, 1.0f, 1.0f });
        const Vec3 transformedNormal = GltfDetail::transformDirection(nonUniformScale, normalize({ 1.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }));
        const Vec3 expectedNormal = normalize({ 0.5f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
        require(
            std::fabs(transformedNormal.x - expectedNormal.x) < 0.01f &&
                std::fabs(transformedNormal.y - expectedNormal.y) < 0.01f &&
                std::fabs(transformedNormal.z - expectedNormal.z) < 0.01f,
            "glTF normal transforms should use inverse-transpose behavior under non-uniform scale",
            failed);
    }

    {
        auto cache = TerrainChunkBakeCache::open(tempRoot / "terrain-cache");
        require(cache.has_value(), "Terrain chunk bake cache failed to open for smoke checks", failed);
        if (cache.has_value()) {
            CompiledTerrainChunk chunk;
            chunk.key.worldId = "smoke-cache";
            chunk.key.seed = 7;
            chunk.key.generatorVersion = 3;
            chunk.key.band = 1;
            chunk.key.detail = 2;
            chunk.key.tileX = -4;
            chunk.key.tileZ = 9;
            chunk.key.paramsSignature = 11;
            chunk.key.sourceSignature = 13;
            chunk.sourceData.key = chunk.key;
            chunk.terrainModel.assetKey = "cache:uv1";
            chunk.terrainModel.vertices = {
                { 0.0f, 0.0f, 0.0f },
                { 1.0f, 0.0f, 0.0f },
                { 0.0f, 1.0f, 0.0f }
            };
            chunk.terrainModel.faces = { { { 0, 1, 2 }, 0 } };
            chunk.terrainModel.faceColors = { { 1.0f, 1.0f, 1.0f } };
            chunk.terrainModel.vertexNormals = {
                { 0.0f, 0.0f, 1.0f },
                { 0.0f, 0.0f, 1.0f },
                { 0.0f, 0.0f, 1.0f }
            };
            chunk.terrainModel.texCoords = {
                { 0.0f, 0.0f },
                { 0.0f, 0.0f },
                { 0.0f, 0.0f }
            };
            chunk.terrainModel.texCoords1 = {
                { 0.0f, 0.0f },
                { 1.0f, 0.0f },
                { 0.0f, 1.0f }
            };
            chunk.terrainModel.materials.push_back(Material {});
            chunk.terrainModel.hasTexCoords = true;

            std::string cacheError;
            require(cache->save(chunk, &cacheError), "Terrain chunk bake cache failed to save UV-set smoke data: " + cacheError, failed);

            CompiledTerrainChunk loadedChunk;
            require(cache->load(chunk.key, loadedChunk), "Terrain chunk bake cache failed to reload UV-set smoke data", failed);
            require(loadedChunk.terrainModel.texCoords1.size() == 3u, "Terrain chunk bake cache lost secondary texture coordinates", failed);
            if (loadedChunk.terrainModel.texCoords1.size() == 3u) {
                require(
                    std::fabs(loadedChunk.terrainModel.texCoords1[1].x - 1.0f) < 0.001f &&
                        std::fabs(loadedChunk.terrainModel.texCoords1[2].y - 1.0f) < 0.001f,
                    "Terrain chunk bake cache corrupted secondary texture coordinate values",
                    failed);
            }

            const std::filesystem::path corruptPath = cache->chunkPath(chunk.key);
            {
                const std::uintmax_t originalSize = std::filesystem::file_size(corruptPath);
                require(originalSize > 8u, "Terrain chunk bake cache file was unexpectedly small", failed);
                if (originalSize > 8u) {
                    std::filesystem::resize_file(corruptPath, originalSize - 1u);
                }
            }
            CompiledTerrainChunk corruptedChunk;
            require(!cache->load(chunk.key, corruptedChunk), "Terrain chunk bake cache accepted a truncated/corrupt payload", failed);
        }
    }

    runFlightParityChecks(failed);
    runPlanetMathChecks(failed);
    runAudioFrameChecks(failed);
    runCullingAndWalkingRigChecks(failed);
    runTerrainLodChecks(failed);
    runCloudFieldChecks(failed);
    runTerrainEditBoundsChecks(failed);
    runTerrainStreamingChecks(failed);
    runPressureGovernorChecks(failed);
    runTerrainDecorationChecks(failed);
    runNetworkingSmokeChecks(failed);

    WorldChunkState wireChunk;
    wireChunk.cx = 3;
    wireChunk.cz = -2;
    wireChunk.resolution = 8;
    wireChunk.revision = 5;
    wireChunk.materialRevision = 7;
    wireChunk.heightDeltas = { 0.0f, 1.25f, -2.5f, 3.75f };
    wireChunk.volumetricOverrides = {
        { "sphere", 1.0f, 2.0f, 3.0f, 4.0f }
    };
    const WorldKeyValueFields chunkFields = buildChunkStateFields(wireChunk);
    const WorldChunkState decodedWireChunk = decodeChunkStateFields(buildChunkFieldLookup(chunkFields));
    require(decodedWireChunk.cx == wireChunk.cx && decodedWireChunk.cz == wireChunk.cz, "World wire chunk coords did not round-trip", failed);
    require(decodedWireChunk.revision == wireChunk.revision, "World wire chunk revision did not round-trip", failed);
    require(decodedWireChunk.heightDeltas.size() == wireChunk.heightDeltas.size(), "World wire height deltas did not round-trip", failed);
    require(decodedWireChunk.volumetricOverrides.size() == 1u, "World wire volumetric overrides did not round-trip", failed);

    TerrainTunnelSeed smokeSeed;
    smokeSeed.radius = 6.0f;
    smokeSeed.hillAttached = true;
    smokeSeed.points = {
        { 0.0f, 10.0f, 0.0f },
        { 12.0f, 11.0f, -3.0f }
    };
    const std::vector<TerrainTunnelSeed> encodedSeedsSource { smokeSeed };
    const std::vector<TerrainTunnelSeed> decodedSeeds = decodeTunnelSeeds(encodeTunnelSeeds(encodedSeedsSource));
    require(decodedSeeds.size() == 1u, "World wire tunnel seed count did not round-trip", failed);
    require(decodedSeeds[0].hillAttached, "World wire tunnel hillAttached flag did not round-trip", failed);
    require(decodedSeeds[0].points.size() == 2u, "World wire tunnel points did not round-trip", failed);

    WorldStoreOptions worldOptions;
    worldOptions.name = "smoke_world";
    worldOptions.storageRoot = tempRoot / "worlds";
    worldOptions.createIfMissing = true;
    worldOptions.regionSize = 8;
    worldOptions.chunkResolution = 8;
    worldOptions.groundParams = defaultTerrainParams();
    worldOptions.groundParams.seed = 2468;
    worldOptions.groundParams.chunkSize = 128.0f;
    worldOptions.groundParams.tunnelCount = 2;
    worldOptions.spawn = { 12.0f, 34.0f, 56.0f };

    std::string worldError;
    auto openedWorld = WorldStore::open(worldOptions, &worldError);
    require(openedWorld.has_value(), "World store failed to open: " + worldError, failed);
    if (openedWorld.has_value()) {
        WorldStore world = std::move(*openedWorld);
        const std::uint64_t startingRevision = world.contentRevision();
        const WorldMeta meta = world.getMeta();
        require(meta.worldId == "smoke_world", "World store did not preserve the requested world id", failed);
        require(!meta.tunnelSeeds.empty(), "World store did not populate tunnel seeds", failed);

        const auto craterResult = world.applyCrater({ 32.0f, 0.0f, 24.0f, 10.0f, 4.0f, 0.15f });
        require(craterResult.first && !craterResult.second.empty(), "World store crater application did not dirty any chunks", failed);
        require(world.contentRevision() > startingRevision, "World store content revision did not advance after terrain edits", failed);

        std::string flushError;
        require(world.flushDirty(&flushError) > 0, "World store failed to flush dirty regions: " + flushError, failed);

        worldOptions.createIfMissing = false;
        openedWorld = WorldStore::open(worldOptions, &worldError);
        require(openedWorld.has_value(), "World store failed to reopen persisted data: " + worldError, failed);
        if (openedWorld.has_value()) {
            WorldStore reopenedWorld = std::move(*openedWorld);
            require(std::fabs(reopenedWorld.sampleHeightDelta(32.0f, 24.0f)) > 0.01f, "World store height deltas did not persist", failed);

            const auto latestChunk = reopenedWorld.getChunkState(craterResult.second.front().cx, craterResult.second.front().cz);
            require(latestChunk.has_value() && latestChunk->revision >= 1, "World store did not expose persisted chunk revisions", failed);
            if (latestChunk.has_value()) {
                WorldChunkState staleChunk = *latestChunk;
                staleChunk.revision = std::max(0, staleChunk.revision - 1);
                if (!staleChunk.heightDeltas.empty()) {
                    staleChunk.heightDeltas[0] = 999.0f;
                }
                require(!reopenedWorld.applyChunkState(staleChunk), "World store accepted a stale chunk revision", failed);
            }

            WorldInfoSnapshot info;
            info.worldId = "smoke_world";
            info.formatVersion = 3;
            info.seed = 2468;
            info.chunkSize = 128.0f;
            info.horizonRadiusMeters = 4096.0f;
            info.heightAmplitude = 140.0f;
            info.heightFrequency = 0.0024f;
            info.waterLevel = -9.0f;
            info.tunnelSeeds = {
                {
                    7.0f,
                    true,
                    {
                        { 0.0f, 24.0f, 0.0f },
                        { 32.0f, 22.0f, 8.0f }
                    }
                }
            };
            info.spawnX = 5.0f;
            info.spawnY = 6.0f;
            info.spawnZ = 7.0f;
            require(reopenedWorld.applyWorldInfo(info, &worldError), "World info snapshot failed to apply: " + worldError, failed);

            const WorldGroundParams rebuiltGround = reopenedWorld.buildGroundParams(defaultTerrainParams());
            require(rebuiltGround.worldFormatVersion == 3, "World ground params did not reflect the updated format version", failed);
            require(rebuiltGround.tunnelSeedCount == 1, "World ground params did not expose the updated tunnel seed count", failed);

            const TerrainFieldContext rebuiltContext = createTerrainFieldContext(rebuiltGround.terrainParams);
            require(rebuiltContext.tunnelSeeds.size() == 1u, "Explicit tunnel seeds were not injected into the native terrain context", failed);
            if (rebuiltContext.tunnelSeeds.size() == 1u) {
                require(std::fabs(rebuiltContext.tunnelSeeds[0].radius - 7.0f) < 0.001f, "Explicit tunnel seed radius did not survive into the terrain context", failed);
                require(rebuiltContext.tunnelSeeds[0].hillAttached, "Explicit tunnel seed hillAttached flag did not survive into the terrain context", failed);
            }
        }
    }

    PlaneVisualState visual;
    visual.defaultScale = 3.0f;
    setBuiltinPlaneModel(visual);
    std::string loadStatus;
    require(loadPlaneModelFromPath(modelPath, visual, &loadStatus), "Failed to load visual for paint smoke test: " + loadStatus, failed);
    if (visual.paintSupported) {
        require(fillPaintOverlay(visual, 2), "Failed to fill test paint overlay", failed);

        const std::filesystem::path paintDirectory = tempRoot / "paint";
        std::string paintHash;
        std::string paintError;
        require(commitPaintOverlay(paintDirectory, visual, &paintHash, &paintError),
            "Failed to commit paint overlay: " + paintError,
            failed);
        const std::string primaryPaintTargetKey = visual.paintTargetKey;
        require(!primaryPaintTargetKey.empty(), "Committed paint did not record a model-specific paint key", failed);

        PlaneVisualState reloaded;
        reloaded.defaultScale = 3.0f;
        setBuiltinPlaneModel(reloaded);
        require(loadPlaneModelFromPath(modelPath, reloaded, &loadStatus), "Failed to reload model for paint smoke test: " + loadStatus, failed);

        const std::filesystem::path manualPaintPath = paintDirectory / (paintHash + ".png");
        require(std::filesystem::exists(manualPaintPath), "Committed paint PNG was not written to disk", failed);

        std::filesystem::create_directories(getPaintStorageDirectory(), ec);
        const std::filesystem::path sharedPaintPath = getPaintStoragePath(paintHash);
        std::filesystem::copy_file(manualPaintPath, sharedPaintPath, std::filesystem::copy_options::overwrite_existing, ec);
        require(loadPaintOverlayByHash(paintHash, reloaded, &paintError), "Failed to reload committed paint PNG: " + paintError, failed);
        const RgbaImage committedOverlay = reloaded.paintOverlay;

        std::filesystem::path alternateModelPath = root / "portSource/Assets/Models/DualEngine2.glb";
        if (!std::filesystem::exists(alternateModelPath) || alternateModelPath == modelPath) {
            alternateModelPath.clear();
        }
        if (alternateModelPath.empty() && modelPath.extension() != ".gltf") {
            alternateModelPath = tempRoot / ("alternate_model" + modelPath.extension().string());
            std::filesystem::copy_file(modelPath, alternateModelPath, std::filesystem::copy_options::overwrite_existing, ec);
            require(std::filesystem::exists(alternateModelPath), "Failed to stage alternate model for paint switch smoke test", failed);
        }

        if (!alternateModelPath.empty()) {
            require(loadPlaneModelFromPath(alternateModelPath, visual, &loadStatus), "Failed to load alternate model for paint switch smoke test: " + loadStatus, failed);
            require(visual.paintHash.empty(), "Switching to a different model should not keep the previous model's paint hash active", failed);
            require(!visual.hasCommittedPaint, "Switching to a different model should clear the previous model's live overlay", failed);

            require(loadPlaneModelFromPath(modelPath, visual, &loadStatus), "Failed to reload primary model for paint switch smoke test: " + loadStatus, failed);
            require(visual.paintHash == paintHash, "Switching back to the original model did not restore its committed paint hash", failed);
            require(visual.hasCommittedPaint, "Switching back to the original model did not restore its committed paint state", failed);
            require(
                visual.paintOverlay.width == committedOverlay.width &&
                    visual.paintOverlay.height == committedOverlay.height &&
                    visual.paintOverlay.pixels == committedOverlay.pixels,
                "Switching back to the original model did not restore the committed overlay pixels",
                failed);
        }

        {
            PlaneVisualState walkingVisualForSave;
            walkingVisualForSave.defaultScale = 1.0f;
            setBuiltinPlaneModel(walkingVisualForSave);

            const std::filesystem::path modelSettingsPath = tempRoot / "paint_model_settings.ini";
            std::string saveError;
            require(
                savePreferences(
                    modelSettingsPath,
                    uiState,
                    graphicsSettings,
                    lightingSettings,
                    hudSettings,
                    onlineSettings,
                    controls,
                    planeProfile,
                    terrainParams,
                    visual,
                    walkingVisualForSave,
                    &saveError),
                "Failed to save model-specific paint preferences: " + saveError,
                failed);

            std::ifstream savedModelFile(modelSettingsPath, std::ios::binary);
            const std::string savedModelContents((std::istreambuf_iterator<char>(savedModelFile)), std::istreambuf_iterator<char>());
            require(
                savedModelContents.find("paint.plane.model." + primaryPaintTargetKey + "=" + paintHash) != std::string::npos,
                "Saved preferences did not persist the model-specific paint mapping",
                failed);

            UiState loadedUiState = defaultUiState();
            GraphicsSettings loadedGraphicsSettings = defaultGraphicsSettings();
            LightingSettings loadedLightingSettings = defaultLightingSettings();
            HudSettings loadedHudSettings = defaultHudSettings();
            ControlProfile loadedControls = defaultControlProfile();
            AircraftProfile loadedPlaneProfile {};
            loadedPlaneProfile.visualPrefs.scale = 3.0f;
            TerrainParams loadedTerrainParams = defaultTerrainParams();
            OnlineSettings loadedOnlineSettings {};
            VisualPreferenceData loadedWalkingPrefs {};
            loadedWalkingPrefs.scale = 1.0f;
            std::string loadPreferenceError;
            require(
                loadPreferences(
                    modelSettingsPath,
                    loadedUiState,
                    loadedGraphicsSettings,
                    loadedLightingSettings,
                    loadedHudSettings,
                    loadedOnlineSettings,
                    loadedControls,
                    loadedPlaneProfile,
                    loadedTerrainParams,
                    loadedWalkingPrefs,
                    &loadPreferenceError),
                "Failed to reload model-specific paint preferences: " + loadPreferenceError,
                failed);
            require(
                loadedPlaneProfile.visualPrefs.paintHashesByModelKey[primaryPaintTargetKey] == paintHash,
                "Preference load did not restore the saved model-specific paint mapping",
                failed);

            PlaneVisualState restoredVisual;
            restoredVisual.defaultScale = 3.0f;
            setBuiltinPlaneModel(restoredVisual);
            restoreVisualFromPreferences(restoredVisual, loadedPlaneProfile.visualPrefs, {}, "plane");
            require(restoredVisual.paintHash == paintHash, "Restored visual did not reactivate the model-specific paint hash", failed);
            require(restoredVisual.hasCommittedPaint, "Restored visual did not reactivate the model-specific paint overlay", failed);
            require(
                restoredVisual.paintOverlay.width == committedOverlay.width &&
                    restoredVisual.paintOverlay.height == committedOverlay.height &&
                    restoredVisual.paintOverlay.pixels == committedOverlay.pixels,
                "Restored visual did not rebuild the expected committed paint overlay",
                failed);
        }

        setBuiltinPlaneModel(reloaded);
        require(reloaded.paintHash.empty(), "Switching models did not clear incompatible stored paint hash", failed);
        require(!reloaded.hasCommittedPaint, "Switching models did not clear incompatible paint state", failed);
        std::filesystem::remove(sharedPaintPath, ec);
    } else {
        std::cout << "SKIP: Selected model is not paintable, paint round-trip not exercised.\n";
    }

    if (gKnownIssueCount > 0) {
        std::cout << "Known non-gating smoke issues: " << gKnownIssueCount << "\n";
    }

    if (!failed) {
        std::cout << "Native smoke checks passed.\n";
    }

    SDL_Quit();
    return failed ? 1 : 0;
}
