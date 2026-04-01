#pragma once

#include "NativeGame/Flight.hpp"
#include "NativeGame/World.hpp"

#include <array>
#include <cstdint>

namespace TrueFlightApp {

using NativeGame::FlightState;
using NativeGame::Quat;
using NativeGame::TerrainFieldContext;
using NativeGame::Vec3;

enum class BodyJointId : std::uint8_t {
    Pelvis = 0,
    Spine,
    Chest,
    Head,
    UpperArmL,
    ForearmL,
    HandL,
    UpperArmR,
    ForearmR,
    HandR,
    ThighL,
    ShinL,
    FootL,
    ThighR,
    ShinR,
    FootR,
    Count
};

struct BodySegment {
    Vec3 position {};
    Vec3 previousPosition {};
    Vec3 velocity {};
    Vec3 accumulatedForce {};
    float mass = 1.0f;
    float radius = 0.08f;
    bool pinned = false;
};

struct BodyConstraint {
    BodyJointId a = BodyJointId::Pelvis;
    BodyJointId b = BodyJointId::Spine;
    float restLength = 0.1f;
    float stiffness = 1.0f;
};

struct BodyBalanceState {
    Vec3 supportCenter {};
    Vec3 centerOfMass {};
    Vec3 desiredComOffset {};
    bool leftFootGrounded = false;
    bool rightFootGrounded = false;
};

struct BodyInput {
    float moveForward = 0.0f;
    float moveRight = 0.0f;
    bool sprint = false;
    bool jump = false;
    bool brace = false;
};

struct BodySimTuning {
Vec3 gravity { 0.0f, -18.0f, 0.0f };
float damping = 0.985f;
float groundFriction = 0.82f;
float walkSpeed = 6.0f;
float sprintMultiplier = 1.7f;
float jumpVelocity = 5.0f;
float pelvisHeight = 1.02f;
float actorPelvisOffset = 0.78f;
float pelvisHeightStrength = 240.0f;
float pelvisHeightDamping = 30.0f;
float locomotionStrength = 120.0f;
float locomotionDamping = 20.0f;
float uprightStrength = 140.0f;
float uprightDamping = 20.0f;
float facingStrength = 60.0f;
float footPlantStrength = 180.0f;
float footLiftStrength = 70.0f;
float footDamping = 18.0f;
float strideLength = 0.38f;
float stepWidth = 0.18f;
float stepHeight = 0.16f;
float gaitFrequency = 2.4f;
int solverIterations = 12;
};

struct PhysicalBody {
    static constexpr int kJointCount = static_cast<int>(BodyJointId::Count);

    std::array<BodySegment, kJointCount> joints {};
    std::array<BodyConstraint, 15> constraints {};
    BodyBalanceState balance {};
    BodySimTuning tuning {};

    Vec3 desiredFacingForward { 0.0f, 0.0f, 1.0f };
    float locomotionPhase = 0.0f;
    bool initialized = false;
};

void initializeHumanoidBody(PhysicalBody& body, const Vec3& rootPosition);
void resetHumanoidBodyFromActor(PhysicalBody& body, const FlightState& actor);
void driveBodyPoseTargets(PhysicalBody& body, const BodyInput& input, float dt);
void simulatePhysicalBody(
    PhysicalBody& body,
    float dt,
    const TerrainFieldContext& terrainContext);
void syncActorFromPhysicalBody(FlightState& actor, const PhysicalBody& body);

}  // namespace TrueFlightApp