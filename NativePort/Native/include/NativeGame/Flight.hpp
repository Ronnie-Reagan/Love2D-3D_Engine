#pragma once

#include "NativeGame/Math.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>

namespace NativeGame {

struct AtmosphereSample {
    float altitude = 0.0f;
    float temperatureK = 288.15f;
    float pressurePa = 101325.0f;
    float densityKgM3 = 1.225f;
    float sigma = 1.0f;
    float speedOfSound = 340.0f;
    float gravity = 9.80665f;
};

enum class FlightEngineModel : std::uint8_t {
    RadialProp = 0,
    Turbojet = 1
};

struct FlightConfig {
    float physicsHz = 120.0f;
    int maxSubsteps = 8;
    float maxFrameDt = 0.05f;
    float metersPerUnit = 1.0f;
    float massKg = 1157.0f;
    float Ixx = 1280.0f;
    float Iyy = 1825.0f;
    float Izz = 2660.0f;
    float Ixz = 0.0f;
    float wingArea = 16.17f;
    float wingSpan = 11.0f;
    float meanChord = 1.47f;
    float CL0 = 0.24f;
    float CLalpha = 4.95f;
    float CLElevator = 0.92f;
    float alphaStallRad = radians(20.0f);
    float stallLiftDropoff = 0.08f;
    float postStallSpinBlendRangeRad = radians(10.0f);
    float postStallDampingScale = 0.1f;
    float postStallAileronEffectiveness = 0.07f;
    float postStallRudderEffectiveness = 0.22f;
    float postStallYawStabilityScale = 0.12f;
    float spinRollMoment = 0.78f;
    float spinYawMoment = 1.04f;
    float CD0 = 0.031f;
    float inducedDragK = 0.045f;
    float CYbeta = -0.78f;
    float CYrudder = 0.21f;
    float Cm0 = 0.03f;
    float CmAlpha = -1.22f;
    float CmQ = -14.0f;
    float CmElevator = -1.60f;
    float ClBeta = -0.08f;
    float ClP = -0.48f;
    float ClR = 0.12f;
    float ClAileron = 0.24f;
    float ClRudder = 0.03f;
    float CnBeta = 0.10f;
    float CnR = -0.12f;
    float CnP = -0.02f;
    float CnRudder = -0.15f;
    float CnAileron = 0.02f;
    FlightEngineModel engineModel = FlightEngineModel::RadialProp;
    float maxThrustSeaLevel = 1850.0f;
    float afterburnerMultiplier = 1.0f;
    float maxEffectivePropSpeed = 78.0f;
    float idleCrankRpm = 650.0f;
    float maxCrankRpm = 2700.0f;
    float propellerGearRatio = 1.0f;
    float propellerDiameterMeters = 1.93f;
    int engineCount = 2;
    float engineLateralSpacingMeters = 5.5f;
    float engineDisplacementLiters = 5.9f;
    int engineCylinderCount = 4;
    float maxBrakePowerKw = 134.0f;
    float fuelMassKg = 500.0f;
    float throttleTimeConstant = 1.15f;
    float maxElevatorDeflectionRad = radians(25.0f);
    float maxAileronDeflectionRad = radians(24.0f);
    float maxRudderDeflectionRad = radians(16.0f);
    float maxManualElevatorTrimRad = radians(6.0f);
    float maxManualRudderTrimRad = radians(4.0f);
    float pitchInputExpo = 0.72f;
    float rollInputExpo = 0.58f;
    float yawInputExpo = 0.38f;
    float yawInputResponseRate = 12.0f;
    float yawInputReturnRate = 12.0f;
    float elevatorSurfaceRateRadPerSec = radians(45.0f);
    float aileronSurfaceRateRadPerSec = radians(58.0f);
    float rudderSurfaceRateRadPerSec = radians(48.0f);
    float controlLoadingReferenceDynamicPressure = 1800.0f;
    float controlLoadingRateBlend = 0.62f;
    bool enableAutoTrim = false;
    float autoTrimUpdateHz = 6.0f;
    bool autoTrimUseWorker = true;
    float autoTrimWorkerTimeoutSec = 0.35f;
    float groundFriction = 0.78f;
    float groundAngularDamping = 0.42f;
    bool stabilityAugmentation = false;
    float pitchRateDampingMoment = 2600.0f;
    float yawRateDampingMoment = 1700.0f;
    float rollRateDampingMoment = 2100.0f;
    float controlAuthoritySpeed = 28.0f;
    float minControlAuthority = 0.08f;
    float pitchControlScale = 0.68f;
    float rollControlScale = 0.58f;
    float yawControlScale = 0.52f;
    bool crashEnabled = true;
    float maxLinearSpeed = 6000.0f;
    float maxAngularRateRad = radians(240.0f);
    float maxForceNewton = 70000.0f;
    float maxMomentNewtonMeter = 120000.0f;
    float maxPositionAbs = 50000000.0f;
    float crashNormalSpeed = 13.0f;
    float crashTotalSpeed = 32.0f;
    float crashAngularSpeed = radians(160.0f);
    float crashCooldownSec = 0.9f;
    float waterLinearDrag = 2.4f;
    float waterVerticalDrag = 3.4f;
    float waterBuoyancyAccel = 18.0f;
    float waterSkimLift = 1.8f;
    float waterAngularDamping = 2.8f;
};

inline FlightConfig defaultFlightConfig()
{
    return {};
}

struct InputState {
    bool flightThrottleUp = false;
    bool flightThrottleDown = false;
    bool flightPitchUp = false;
    bool flightPitchDown = false;
    bool flightRollLeft = false;
    bool flightRollRight = false;
    bool flightYawLeft = false;
    bool flightYawRight = false;
    bool flightAirBrakes = false;
    bool flightAfterburner = false;
    bool flightUseAnalogYoke = false;
    bool flightHoldYaw = false;
    float flightThrottleAnalog = 0.0f;
    float flightPitchAnalog = 0.0f;
    float flightRollAnalog = 0.0f;
};

struct FlightDebugState {
    int tick = 0;
    int substeps = 0;
    float airDensity = 1.225f;
    float alpha = 0.0f;
    float beta = 0.0f;
    float qbar = 0.0f;
    float thrust = 0.0f;
    float throttle = 0.0f;
    float speed = 0.0f;
    float crankRpm = 0.0f;
    float propRpm = 0.0f;
    float manifoldPressureKpa = 0.0f;
    float enginePowerKw = 0.0f;
    float fuelFlowKgPerHour = 0.0f;
    float exhaustGasTempC = 0.0f;
    float cylinderHeadTempC = 0.0f;
    float oilTempC = 0.0f;
    float fuelRemainingKg = 0.0f;
};

struct FlightState {
    Vec3 pos { 0.0f, 0.0f, 0.0f };
    Quat rot = quatIdentity();
    Vec3 vel { 0.0f, 0.0f, 0.0f };
    Vec3 flightVel { 0.0f, 0.0f, 0.0f };
    Vec3 flightAngVel { 0.0f, 0.0f, 0.0f };
    DVec3 inertialPos {};
    DVec3 inertialVel {};
    bool planetMode = false;
    struct {
        float pitch = 0.0f;
        float yaw = 0.0f;
        float roll = 0.0f;
    } yoke;
    float manualElevatorTrim = 0.0f;
    float manualRudderTrim = 0.0f;
    float throttle = 0.0f;
    float throttleAccel = 0.55f;
    float airBrakeStrength = 1.2f;
    float wheelThrottleStep = 0.06f;
    float wheelElevatorTrimStepRad = radians(0.25f);
    float rudderTrimStepRad = radians(0.35f);
    float yokeKeyboardRate = 2.8f;
    float yokeAutoCenterRate = 0.5f;
    float yokeMouseHoldDurationSec = 0.75f;
    float yokeMousePitchGain = 8.0f;
    float yokeMouseYawGain = 7.0f;
    float yokeMouseRollGain = 6.0f;
    float yokeLastMouseInputAt = -1000.0f;
    float collisionRadius = 1.0f;
    bool onGround = false;
    FlightDebugState debug {};
};

struct FlightEnvironment {
    Vec3 wind { 0.0f, 0.0f, 0.0f };
    std::function<float(float, float)> groundHeightAt;
    std::function<float(float, float)> waterHeightAt;
    std::function<float(float, float, float)> sampleSdf;
    std::function<Vec3(float, float, float)> sampleNormal;
    float collisionRadius = 0.0f;
    WorldShape worldShape = WorldShape::Plane;
    PlanetConfig planet {};
    Vec3 planetCenterWorld {};
    Vec3 planetSpinAxisWorld { 0.0f, 1.0f, 0.0f };
    double planetTimeSeconds = 0.0;
};

enum class FlightCrashCause : std::uint8_t {
    Terrain = 0,
    PropBlocker = 1
};

struct FlightCrashEvent {
    Vec3 position {};
    Vec3 normal { 0.0f, 1.0f, 0.0f };
    Vec3 velocity {};
    float radius = 1.0f;
    float impactNormalSpeed = 0.0f;
    float totalSpeed = 0.0f;
    float angularSpeed = 0.0f;
    int tick = 0;
    FlightCrashCause cause = FlightCrashCause::Terrain;
};

constexpr int kMaxFlightEngines = 69;

struct FlightRuntimeState {
    float accumulator = 0.0f;
    int tick = 0;

    float engineThrottle = 0.0f;
    float fuelRemainingKg = 0.0f;
    int engineCount = 2;
    FlightEngineModel engineModel = FlightEngineModel::RadialProp;

    std::array<float, kMaxFlightEngines> crankRpm {};
    std::array<float, kMaxFlightEngines> shaftRpm {};
    std::array<float, kMaxFlightEngines> propRpm {};
    std::array<float, kMaxFlightEngines> manifoldPressureKpa {};
    std::array<float, kMaxFlightEngines> fuelFlowKgPerSec {};
    std::array<float, kMaxFlightEngines> airMassFlowKgPerSec {};
    std::array<float, kMaxFlightEngines> enginePowerKw {};
    std::array<float, kMaxFlightEngines> exhaustGasTempK {};
    std::array<float, kMaxFlightEngines> cylinderHeadTempK {};
    std::array<float, kMaxFlightEngines> oilTempK {};
    std::array<float, kMaxFlightEngines> propellerEfficiency {};
    std::array<float, kMaxFlightEngines> thrustNewton {};

    float elevatorTrim = 0.0f;
    float trimTarget = 0.0f;
    int trimNextUpdateTick = 0;
    int lastCrashTick = -1000000;
    AtmosphereSample lastAtmosphere {};
    float lastAlpha = 0.0f;
    float lastBeta = 0.0f;
    float lastDynamicPressure = 0.0f;
    float lastThrustNewton = 0.0f;
    float elevatorDeflection = 0.0f;
    float aileronDeflection = 0.0f;
    float rudderDeflection = 0.0f;
    bool bootstrapDone = false;
    bool crashed = false;
    bool hasPendingCrash = false;
    FlightCrashEvent pendingCrash {};
};

inline AtmosphereSample sampleAtmosphere(float altitudeMeters)
{
    constexpr float seaLevelTempK = 288.15f;
    constexpr float seaLevelPressurePa = 101325.0f;
    constexpr float lapseRateKPerM = 0.0065f;
    constexpr float gasConstantAir = 287.05f;
    constexpr float gravity = 9.80665f;
    constexpr float gammaAir = 1.4f;
    constexpr float tropopauseAltitudeM = 11000.0f;

    const float altitude = clamp(altitudeMeters, -2000.0f, 120000.0f);
    float temperature = 0.0f;
    float pressure = 0.0f;

    if (altitude <= tropopauseAltitudeM) {
        temperature = seaLevelTempK - (lapseRateKPerM * altitude);
        const float ratio = temperature / seaLevelTempK;
        pressure = seaLevelPressurePa * std::pow(ratio, gravity / (gasConstantAir * lapseRateKPerM));
    } else {
        temperature = seaLevelTempK - (lapseRateKPerM * tropopauseAltitudeM);
        const float ratio = temperature / seaLevelTempK;
        const float pressureAtTropopause =
            seaLevelPressurePa * std::pow(ratio, gravity / (gasConstantAir * lapseRateKPerM));
        const float h = altitude - tropopauseAltitudeM;
        pressure = pressureAtTropopause * std::exp((-gravity * h) / (gasConstantAir * temperature));
    }

    const float density = pressure / (gasConstantAir * temperature);
    const float radiusMeters = 6371000.0f;
    const float gravityScale = (radiusMeters * radiusMeters) / std::max(1.0f, (radiusMeters + altitude) * (radiusMeters + altitude));
    return {
        altitude,
        temperature,
        pressure,
        density,
        density / 1.225f,
        std::sqrt(gammaAir * gasConstantAir * temperature),
        gravity * gravityScale
    };
}

inline float estimateLevelAlpha(const FlightConfig& config, const AtmosphereSample& atmosphere, float trueAirspeed)
{
    const float rho = std::max(0.02f, atmosphere.densityKgM3);
    const float speed = std::max(5.0f, trueAirspeed);
    const float clRequired = (config.massKg * atmosphere.gravity) / (rho * speed * speed * config.wingArea);
    return (clRequired - config.CL0) / std::max(1.0e-4f, config.CLalpha);
}

inline float estimateElevatorForLevelFlight(const FlightConfig& config, const AtmosphereSample& atmosphere, float trueAirspeed)
{
    const float alpha = estimateLevelAlpha(config, atmosphere, trueAirspeed);
    const float elevator = -((config.Cm0 + (config.CmAlpha * alpha)) / std::max(1.0e-4f, config.CmElevator));
    return clamp(elevator, -config.maxElevatorDeflectionRad, config.maxElevatorDeflectionRad);
}

inline float axis(bool positive, bool negative)
{
    float value = 0.0f;
    if (positive) {
        value += 1.0f;
    }
    if (negative) {
        value -= 1.0f;
    }
    return value;
}

inline float moveTowardsScalar(float current, float target, float maxDelta)
{
    const float delta = target - current;
    if (std::fabs(delta) <= maxDelta) {
        return target;
    }
    return current + (delta > 0.0f ? maxDelta : -maxDelta);
}

inline float averageEngineValue(const std::array<float, kMaxFlightEngines>& values, int engineCount)
{
    const int count = std::clamp(engineCount, 1, kMaxFlightEngines);
    float total = 0.0f;
    for (int i = 0; i < count; ++i) {
        total += values[i];
    }
    return total / static_cast<float>(count);
}

inline Vec3 bodyToWorld(const Quat& rot, const Vec3& v)
{
    return rotateVector(rot, v);
}

inline Vec3 worldToBody(const Quat& rot, const Vec3& v)
{
    return rotateVector(quatConjugate(rot), v);
}

inline void applyNumericalGuards(FlightState& state, const FlightConfig& config)
{
    state.rot = quatNormalize(state.rot);
    state.manualElevatorTrim = clamp(
        sanitize(state.manualElevatorTrim, 0.0f),
        -std::max(0.0f, config.maxManualElevatorTrimRad),
        std::max(0.0f, config.maxManualElevatorTrimRad));
    state.manualRudderTrim = clamp(
        sanitize(state.manualRudderTrim, 0.0f),
        -std::max(0.0f, config.maxManualRudderTrimRad),
        std::max(0.0f, config.maxManualRudderTrimRad));
    state.pos.x = clamp(sanitize(state.pos.x, 0.0f), -config.maxPositionAbs, config.maxPositionAbs);
    state.pos.y = clamp(sanitize(state.pos.y, 0.0f), -config.maxPositionAbs, config.maxPositionAbs);
    state.pos.z = clamp(sanitize(state.pos.z, 0.0f), -config.maxPositionAbs, config.maxPositionAbs);
    state.flightVel = clampMagnitude({
        sanitize(state.flightVel.x, 0.0f),
        sanitize(state.flightVel.y, 0.0f),
        sanitize(state.flightVel.z, 0.0f)
    }, std::max(20.0f, config.maxLinearSpeed));
    state.flightAngVel = clampMagnitude({
        sanitize(state.flightAngVel.x, 0.0f),
        sanitize(state.flightAngVel.y, 0.0f),
        sanitize(state.flightAngVel.z, 0.0f)
    }, std::max(radians(20.0f), config.maxAngularRateRad));
    state.inertialPos = toDVec3(state.pos);
    state.inertialVel = toDVec3(state.flightVel);
}

inline void updatePilotInputs(FlightState& state, float dt, float nowSeconds, const InputState& input, const FlightConfig& config)
{
    const float throttleAxis = clamp(axis(input.flightThrottleUp, input.flightThrottleDown) + input.flightThrottleAnalog, -1.0f, 1.0f);
    state.throttle = clamp(state.throttle + (throttleAxis * state.throttleAccel * dt), 0.0f, 1.0f);
    if (input.flightAirBrakes) {
        state.throttle = clamp(state.throttle - (state.airBrakeStrength * dt), 0.0f, 1.0f);
    }

    const float pitchAxis = axis(input.flightPitchDown, input.flightPitchUp);
    const float yawAxis = axis(input.flightYawRight, input.flightYawLeft);
    const float rollAxis = axis(input.flightRollLeft, input.flightRollRight);
    const float autoCenterAlpha = clamp(state.yokeAutoCenterRate * dt, 0.0f, 1.0f);
    const bool holdMouseYoke = (nowSeconds - state.yokeLastMouseInputAt) <= std::max(0.0f, state.yokeMouseHoldDurationSec);
    const float absoluteBlend = clamp(dt * 8.5f, 0.0f, 1.0f);

    if (input.flightUseAnalogYoke) {
        state.yoke.pitch = mix(state.yoke.pitch, clamp(input.flightPitchAnalog, -1.0f, 1.0f), absoluteBlend);
        state.yoke.roll = mix(state.yoke.roll, clamp(input.flightRollAnalog, -1.0f, 1.0f), absoluteBlend);
    } else {
        state.yoke.pitch = clamp(state.yoke.pitch + (pitchAxis * state.yokeKeyboardRate * dt), -1.0f, 1.0f);
        state.yoke.roll = clamp(state.yoke.roll + (rollAxis * state.yokeKeyboardRate * dt), -1.0f, 1.0f);
    }
    const float yawTarget =
        yawAxis != 0.0f
            ? clamp(yawAxis, -1.0f, 1.0f)
            : (input.flightHoldYaw ? state.yoke.yaw : 0.0f);
    const float yawStep =
        std::max(
            0.5f,
            (yawAxis != 0.0f || input.flightHoldYaw) ? config.yawInputResponseRate : config.yawInputReturnRate) *
        dt;
    state.yoke.yaw = clamp(moveTowardsScalar(state.yoke.yaw, yawTarget, yawStep), -1.0f, 1.0f);

    if (!holdMouseYoke && !input.flightUseAnalogYoke && pitchAxis == 0.0f) {
        state.yoke.pitch += (0.0f - state.yoke.pitch) * autoCenterAlpha;
    }
    if (!holdMouseYoke && !input.flightUseAnalogYoke && rollAxis == 0.0f) {
        state.yoke.roll += (0.0f - state.yoke.roll) * autoCenterAlpha;
    }
}

inline float resolveCollisionRadius(const FlightState& state, const FlightEnvironment& environment)
{
    if (environment.collisionRadius > 0.0f) {
        return environment.collisionRadius;
    }
    if (state.collisionRadius > 0.0f) {
        return state.collisionRadius;
    }
    return 1.0f;
}

struct AeroState {
    float speed = 0.0f;
    float alpha = 0.0f;
    float beta = 0.0f;
    float dynamicPressure = 0.0f;
    Vec3 forceBody {};
    Vec3 momentBody {};
};

inline AeroState computeAero(
    const Vec3& airVelBody,
    const Vec3& angVelBody,
    float elevator,
    float aileron,
    float rudder,
    const AtmosphereSample& atmosphere,
    const FlightConfig& config)
{
    const float velRight = airVelBody.x;
    const float velUp = airVelBody.y;
    const float velForward = airVelBody.z;

    const float u = velForward;
    const float v = velRight;
    const float w = velUp;

    const float rollRate = angVelBody.z;
    const float pitchRate = -angVelBody.x;
    const float yawRate = angVelBody.y;

    const float speed = std::max(0.1f, length({ u, v, w }));
    const float invSpeed = 1.0f / speed;
    const float rawAlpha = std::atan2(-w, std::max(1.0e-4f, u));
    const float rawBeta = std::asin(clamp(v * invSpeed, -0.999f, 0.999f));
    const float qbar = 0.5f * std::max(0.02f, atmosphere.densityKgM3) * speed * speed;

    const float alphaModelClamp = std::max(config.alphaStallRad + radians(6.0f), radians(55.0f));
    const float betaModelClamp = radians(65.0f);
    const float clMin = -1.6f;
    const float clMax = 2.1f;
    const float cdMax = 2.5f;
    const float cyMax = 1.4f;
    const float cMomentMax = 2.8f;
    const float rateNormMax = 4.0f;

    const float alpha = clamp(rawAlpha, -alphaModelClamp, alphaModelClamp);
    const float beta = clamp(rawBeta, -betaModelClamp, betaModelClamp);

    const float stallBlend = clamp(
        (std::fabs(alpha) - config.alphaStallRad) / std::max(radians(3.0f), config.postStallSpinBlendRangeRad),
        0.0f,
        1.0f);

    const float betaNorm = clamp(beta / radians(18.0f), -1.5f, 1.5f);

    const float aileronEffectiveness = mix(1.0f, std::max(0.08f, config.postStallAileronEffectiveness), stallBlend);
    const float rudderEffectiveness = mix(1.0f, std::max(0.20f, config.postStallRudderEffectiveness), stallBlend);

    const float yawStabilityScale = mix(
        1.0f,
        clamp(config.postStallYawStabilityScale, 0.05f, 2.0f),
        stallBlend);

    const float pitchRateNorm = clamp((pitchRate * config.meanChord) / (2.0f * speed), -rateNormMax, rateNormMax);
    const float rollRateNorm = clamp((rollRate * config.wingSpan) / (2.0f * speed), -rateNormMax, rateNormMax);
    const float yawRateNorm = clamp((yawRate * config.wingSpan) / (2.0f * speed), -rateNormMax, rateNormMax);

    float clLinear = config.CL0 + (config.CLalpha * alpha) + (config.CLElevator * elevator);

    const float alphaAbs = std::fabs(alpha);
    float cl = clLinear;
    if (alphaAbs > config.alphaStallRad) {
        const float exceed = alphaAbs - config.alphaStallRad;
        const float width = std::max(radians(5.0f), config.postStallSpinBlendRangeRad + radians(2.0f));
        const float t = clamp(exceed / width, 0.0f, 1.0f);
        const float postStallLiftFloor = clamp(0.38f - clamp(config.stallLiftDropoff, 0.0f, 0.20f), 0.14f, 0.55f);
        const float postStallLiftScale = mix(1.0f, postStallLiftFloor, t);
        cl *= postStallLiftScale;
    }
    cl = clamp(cl, clMin, clMax);

    float cd = config.CD0 + (config.inducedDragK * cl * cl);
    cd += stallBlend * (0.85f + 0.35f * betaNorm * betaNorm);
    cd = clamp(cd, 0.01f, cdMax);

    const float cy = clamp(
        (config.CYbeta * beta * yawStabilityScale) +
        (config.CYrudder * rudder * rudderEffectiveness),
        -cyMax,
        cyMax);

    float cRoll =
        (config.ClBeta * beta) +
        (config.ClP * rollRateNorm) +
        (config.ClR * yawRateNorm) +
        (config.ClAileron * aileron * aileronEffectiveness) +
        (config.ClRudder * rudder * rudderEffectiveness);

    float cPitch =
        config.Cm0 +
        (config.CmAlpha * alpha) +
        (config.CmQ * pitchRateNorm) +
        (config.CmElevator * elevator);

    float cYaw =
        (config.CnBeta * beta * yawStabilityScale) +
        (config.CnR * yawRateNorm * yawStabilityScale) +
        (config.CnP * rollRateNorm) +
        (config.CnRudder * rudder * rudderEffectiveness) +
        (config.CnAileron * aileron * aileronEffectiveness);

    if (stallBlend > 0.0f) {
        const float deepStallBlend = clamp(
            (alphaAbs - config.alphaStallRad) /
            std::max(radians(2.0f), config.postStallSpinBlendRangeRad * 0.8f),
            0.0f,
            1.0f);
        const float spinDriver = clamp(
            (betaNorm * 0.55f) + (yawRateNorm * 0.75f) + (rudder * 0.80f) + (aileron * 0.12f),
            -2.0f,
            2.0f);
        const float spinGain = stallBlend * mix(1.0f, 1.75f, deepStallBlend);

        cRoll += (spinDriver * config.spinRollMoment * spinGain * 0.72f);
        cYaw += (spinDriver * config.spinYawMoment * spinGain * 0.62f);

        cRoll -= rollRateNorm * stallBlend * 0.26f;
        cYaw -= yawRateNorm * stallBlend * 0.24f;
    }

    cRoll = clamp(cRoll, -cMomentMax, cMomentMax);
    cPitch = clamp(cPitch, -cMomentMax, cMomentMax);
    cYaw = clamp(cYaw, -cMomentMax, cMomentMax);

    const Vec3 velocityHat = normalize({ velRight * invSpeed, velUp * invSpeed, velForward * invSpeed }, { 0.0f, 0.0f, 1.0f });
    const Vec3 dragDir = -velocityHat;
    Vec3 liftSeed = cross(velocityHat, { 1.0f, 0.0f, 0.0f });
    if (lengthSquared(liftSeed) <= 1.0e-5f) {
        liftSeed = cross(velocityHat, { 0.0f, 0.0f, 1.0f });
    }
    const Vec3 liftDir = normalize(liftSeed, { 0.0f, 1.0f, 0.0f });
    const Vec3 sideDir = normalize(cross(liftDir, velocityHat), { 1.0f, 0.0f, 0.0f });

    const float dragMag = qbar * config.wingArea * cd;
    const float liftMag = qbar * config.wingArea * cl;
    const float sideMag = qbar * config.wingArea * cy;

    const Vec3 forceBody = {
        (dragDir.x * dragMag) + (liftDir.x * liftMag) + (sideDir.x * sideMag),
        (dragDir.y * dragMag) + (liftDir.y * liftMag) + (sideDir.y * sideMag),
        (dragDir.z * dragMag) + (liftDir.z * liftMag) + (sideDir.z * sideMag)
    };

    const float momentRoll = qbar * config.wingArea * config.wingSpan * cRoll;
    const float momentPitch = qbar * config.wingArea * config.meanChord * cPitch;
    const float momentYaw = qbar * config.wingArea * config.wingSpan * cYaw;

    return {
        speed,
        alpha,
        beta,
        qbar,
        {
            sanitize(forceBody.x, 0.0f),
            sanitize(forceBody.y, 0.0f),
            sanitize(forceBody.z, 0.0f)
        },
        {
            -sanitize(momentPitch, 0.0f),
            sanitize(momentYaw, 0.0f),
            sanitize(momentRoll, 0.0f)
        }
    };
}

inline Vec3 computePlanetGravityAcceleration(const FlightEnvironment& environment, const Vec3& position)
{
    const Vec3 radial = position - environment.planetCenterWorld;
    const float radiusSq = std::max(1.0f, lengthSquared(radial));
    const float radius = std::sqrt(radiusSq);
    if (radius <= 1.0f) {
        return { 0.0f, -9.80665f, 0.0f };
    }
    const float accelMagnitude = static_cast<float>(environment.planet.gravitationalParameter / static_cast<double>(radiusSq));
    return radial * (-accelMagnitude / radius);
}

inline Vec3 computePlanetAtmosphereVelocity(const FlightEnvironment& environment, const Vec3& position)
{
    const Vec3 spinAxis = normalize(environment.planetSpinAxisWorld, { 0.0f, 1.0f, 0.0f });
    const Vec3 omega = spinAxis * static_cast<float>(environment.planet.rotationRateRadPerSec);
    return cross(omega, position - environment.planetCenterWorld);
}

inline void integrateRigidBody(
    FlightState& state,
    const FlightConfig& config,
    const Vec3& forceWorld,
    const Vec3& momentBody,
    const FlightEnvironment& environment,
    float gravityAccel,
    float dt)
{
    const float mass = std::max(0.1f, config.massKg);

    const Vec3 gravityWorld =
        environment.worldShape == WorldShape::Planet
            ? computePlanetGravityAcceleration(environment, state.pos)
            : Vec3 { 0.0f, -std::fabs(gravityAccel), 0.0f };
    const Vec3 accelWorld = (forceWorld / mass) + gravityWorld;
    state.flightVel += accelWorld * dt;
    state.pos += state.flightVel * dt;

    const float ixx = config.Ixx;
    const float iyy = config.Iyy;
    const float izz = config.Izz;
    const float ixz = config.Ixz;

    const Vec3 iOmega {
        (ixx * state.flightAngVel.x) - (ixz * state.flightAngVel.z),
        iyy * state.flightAngVel.y,
        (-ixz * state.flightAngVel.x) + (izz * state.flightAngVel.z)
    };
    const Vec3 gyro = cross(state.flightAngVel, iOmega);
    const Vec3 rhs = momentBody - gyro;

    float detXZ = (ixx * izz) - (ixz * ixz);
    if (std::fabs(detXZ) <= 1.0e-8f) {
        detXZ = 1.0e-8f;
    }
    const float invXX = izz / detXZ;
    const float invXZ = ixz / detXZ;
    const float invZZ = ixx / detXZ;
    const float invYY = (std::fabs(iyy) > 1.0e-8f) ? (1.0f / iyy) : 0.0f;
    const Vec3 angAccel {
        (invXX * rhs.x) + (invXZ * rhs.z),
        invYY * rhs.y,
        (invXZ * rhs.x) + (invZZ * rhs.z)
    };

    state.flightAngVel += angAccel * dt;
    const Quat omega { 0.0f, state.flightAngVel.x, state.flightAngVel.y, state.flightAngVel.z };
    const Quat qDot = quatMultiply(state.rot, omega);
    state.rot = quatNormalize({
        state.rot.w + (0.5f * qDot.w * dt),
        state.rot.x + (0.5f * qDot.x * dt),
        state.rot.y + (0.5f * qDot.y * dt),
        state.rot.z + (0.5f * qDot.z * dt)
    });
}

inline void applyTerrainContact(
    FlightState& state,
    FlightRuntimeState& runtime,
    const FlightConfig& config,
    const FlightEnvironment& environment,
    float dt)
{
    Vec3 pos = state.pos;
    Vec3 vel = state.flightVel;
    const float radius = resolveCollisionRadius(state, environment);
    bool onGround = false;
    const float stepDt = std::max(0.0f, dt);
    float groundHeight = std::numeric_limits<float>::quiet_NaN();
    if (environment.groundHeightAt) {
        groundHeight = environment.groundHeightAt(pos.x, pos.z);
    }

    const float waterHeight = environment.waterHeightAt
        ? environment.waterHeightAt(pos.x, pos.z)
        : std::numeric_limits<float>::quiet_NaN();
    const float waterDepth =
        isFinite(waterHeight) && isFinite(groundHeight)
            ? std::max(0.0f, waterHeight - groundHeight)
            : 0.0f;
    if (waterDepth > 0.6f) {
        const float bottomY = pos.y - radius;
        const float immersionDepth = waterHeight - bottomY;
        if (immersionDepth > 0.0f) {
            const float immersion = clamp(immersionDepth / std::max(0.5f, radius * 2.0f), 0.0f, 1.0f);
            const float horizontalSpeed = std::sqrt((vel.x * vel.x) + (vel.z * vel.z));
            const float maxSubmerge = radius * 0.65f;
            const float minCenterY = waterHeight + radius - maxSubmerge;
            if (pos.y < minCenterY) {
                pos.y += (minCenterY - pos.y) * clamp(0.25f + (immersion * 0.45f), 0.0f, 1.0f);
            }

            const float horizontalRetention = clamp(
                1.0f - (std::max(0.0f, config.waterLinearDrag) * stepDt * (0.30f + (immersion * 0.85f))),
                0.0f,
                1.0f);
            const float verticalRetention = clamp(
                1.0f - (std::max(0.0f, config.waterVerticalDrag) * stepDt * (0.45f + immersion)),
                0.0f,
                1.0f);
            vel.x *= horizontalRetention;
            vel.z *= horizontalRetention;
            vel.y *= verticalRetention;
            vel.y += std::max(0.0f, config.waterBuoyancyAccel) * immersion * stepDt;
            vel.y +=
                std::max(0.0f, config.waterSkimLift) *
                immersion *
                clamp(horizontalSpeed / 72.0f, 0.0f, 1.0f) *
                stepDt;

            const float angularRetention = clamp(
                1.0f - (std::max(0.0f, config.waterAngularDamping) * stepDt * (0.45f + immersion)),
                0.0f,
                1.0f);
            state.flightAngVel.x *= angularRetention;
            state.flightAngVel.y *= angularRetention;
            state.flightAngVel.z *= angularRetention;
        }
    }

    if (environment.sampleSdf) {
        bool shouldCheckSdf = true;
        if (isFinite(groundHeight)) {
            const float approxGround = groundHeight;
            const float verticalSpeed = std::fabs(vel.y);
            const float contactBand = std::max(8.0f, std::min(40.0f, 10.0f + (verticalSpeed * 0.35f)));
            if (pos.y > (approxGround + radius + contactBand)) {
                shouldCheckSdf = false;
            }
        }

        const float dist = shouldCheckSdf ? environment.sampleSdf(pos.x, pos.y, pos.z) : std::numeric_limits<float>::quiet_NaN();
        if (isFinite(dist) && dist < radius) {
            Vec3 normal { 0.0f, 1.0f, 0.0f };
            if (environment.sampleNormal) {
                normal = normalize(environment.sampleNormal(pos.x, pos.y, pos.z), { 0.0f, 1.0f, 0.0f });
            }

            const float vn = dot(vel, normal);
            bool crashedThisContact = false;

            if (config.crashEnabled && vn < 0.0f) {
                const float impactNormalSpeed = -vn;
                const float totalSpeed = length(vel);
                const float angularSpeed = length(state.flightAngVel);
                const bool shouldCrash =
                    impactNormalSpeed >= std::max(0.1f, config.crashNormalSpeed) ||
                    totalSpeed >= std::max(0.1f, config.crashTotalSpeed) ||
                    angularSpeed >= std::max(0.05f, config.crashAngularSpeed);

                if (shouldCrash) {
                    const float physicsHz = std::max(1.0f, config.physicsHz);
                    const int cooldownTicks = std::max(
                        1,
                        static_cast<int>(std::floor((std::max(0.1f, config.crashCooldownSec) * physicsHz) + 0.5f)));

                    if (runtime.tick >= (runtime.lastCrashTick + cooldownTicks)) {
                        runtime.pendingCrash = {
                            pos - (normal * radius),
                            normal,
                            vel,
                            radius,
                            impactNormalSpeed,
                            totalSpeed,
                            angularSpeed,
                            runtime.tick,
                            FlightCrashCause::Terrain
                        };
                        runtime.hasPendingCrash = true;
                        runtime.crashed = true;
                        runtime.lastCrashTick = runtime.tick;
                        crashedThisContact = true;
                    }
                }
            }

            const float penetration = radius - dist;
            pos += normal * (penetration + 1.0e-4f);
            onGround = true;

            if (crashedThisContact) {
                vel = { 0.0f, 0.0f, 0.0f };
                state.flightAngVel = { 0.0f, 0.0f, 0.0f };
            } else {
                if (vn < 0.0f) {
                    vel += normal * (-vn);
                }

                const Vec3 tangent = vel - (normal * dot(vel, normal));
                const float friction = clamp(config.groundFriction, 0.0f, 1.0f);
                const float tangentRetention = clamp(1.0f - (friction * stepDt), 0.0f, 1.0f);
                vel = (normal * dot(vel, normal)) + (tangent * tangentRetention);
            }

            pos += normal * (penetration + 1.0e-4f);

            if (vn < 0.0f) {
                vel += normal * (-vn);
            }

            const Vec3 tangent = vel - (normal * dot(vel, normal));
            const float friction = clamp(config.groundFriction, 0.0f, 1.0f);
            const float tangentRetention = clamp(1.0f - (friction * stepDt), 0.0f, 1.0f);
            vel = (normal * dot(vel, normal)) + (tangent * tangentRetention);
            onGround = true;
        }
    } else if (isFinite(groundHeight)) {
        if (pos.y <= (groundHeight + radius)) {
            pos.y = groundHeight + radius;
            if (vel.y < 0.0f) {
                vel.y = 0.0f;
            }
            const float friction = clamp(config.groundFriction, 0.0f, 1.0f);
            const float tangentRetention = clamp(1.0f - (friction * stepDt), 0.0f, 1.0f);
            vel.x *= tangentRetention;
            vel.z *= tangentRetention;
            onGround = true;
        }
    }

    state.pos = pos;
    state.flightVel = vel;
    state.onGround = onGround;
    if (onGround) {
        const float damp = clamp(config.groundAngularDamping, 0.0f, 1.0f);
        const float angularRetention = clamp(1.0f - (damp * stepDt), 0.0f, 1.0f);
        state.flightAngVel.x *= angularRetention;
        state.flightAngVel.y *= angularRetention;
        state.flightAngVel.z *= angularRetention;
    }
}

inline void stepFlight(
    FlightState& state,
    FlightRuntimeState& runtime,
    float frameDt,
    float nowSeconds,
    const InputState& input,
    const FlightEnvironment& environment,
    const FlightConfig& config)
{
    const float safeFrameDt = clamp(frameDt, 0.0f, std::max(1.0e-4f, config.maxFrameDt));
    updatePilotInputs(state, safeFrameDt, nowSeconds, input, config);

    const float physicsHz = std::max(1.0f, config.physicsHz);
    const float fixedDt = 1.0f / physicsHz;
    runtime.accumulator = clamp(runtime.accumulator + safeFrameDt, 0.0f, fixedDt * static_cast<float>(std::max(1, config.maxSubsteps)));

    const int engineCount = std::clamp(config.engineCount, 1, kMaxFlightEngines);
    const FlightEngineModel engineModel = config.engineModel;
    const bool jetModel = engineModel == FlightEngineModel::Turbojet;

    if (!runtime.bootstrapDone || runtime.engineCount != engineCount || runtime.engineModel != engineModel) {
        runtime.engineThrottle = clamp(state.throttle, 0.0f, 1.0f);
        runtime.fuelRemainingKg =
            runtime.bootstrapDone
                ? clamp(runtime.fuelRemainingKg, 0.0f, std::max(0.0f, config.fuelMassKg))
                : std::max(0.0f, config.fuelMassKg);
        runtime.engineCount = engineCount;
        runtime.engineModel = engineModel;
        runtime.elevatorTrim = 0.0f;
        runtime.trimTarget = 0.0f;
        runtime.trimNextUpdateTick = 0;
        runtime.lastCrashTick = -1000000;
        runtime.lastAlpha = 0.0f;
        runtime.lastBeta = 0.0f;
        runtime.lastDynamicPressure = 0.0f;
        runtime.lastThrustNewton = 0.0f;
        runtime.elevatorDeflection = 0.0f;
        runtime.aileronDeflection = 0.0f;
        runtime.rudderDeflection = 0.0f;
        runtime.crashed = false;
        runtime.hasPendingCrash = false;

        const float ambientPressureKpa = 101.325f;
        for (int i = 0; i < kMaxFlightEngines; ++i) {
            if (i < engineCount) {
                if (jetModel) {
                    const float idleN1 = 32.0f;
                    runtime.crankRpm[i] = idleN1;
                    runtime.shaftRpm[i] = idleN1;
                    runtime.propRpm[i] = 0.0f;
                    runtime.manifoldPressureKpa[i] = ambientPressureKpa * 0.45f;
                    runtime.exhaustGasTempK[i] = 780.0f;
                    runtime.cylinderHeadTempK[i] = 392.0f;
                    runtime.oilTempK[i] = 345.0f;
                    runtime.propellerEfficiency[i] = 0.0f;
                } else {
                    runtime.crankRpm[i] = config.idleCrankRpm;
                    runtime.shaftRpm[i] = config.idleCrankRpm;
                    runtime.propRpm[i] = config.idleCrankRpm / std::max(0.25f, config.propellerGearRatio);
                    runtime.manifoldPressureKpa[i] = ambientPressureKpa;
                    runtime.exhaustGasTempK[i] = 720.0f;
                    runtime.cylinderHeadTempK[i] = 360.0f;
                    runtime.oilTempK[i] = 330.0f;
                    runtime.propellerEfficiency[i] = 0.45f;
                }
                runtime.fuelFlowKgPerSec[i] = 0.0f;
                runtime.airMassFlowKgPerSec[i] = 0.0f;
                runtime.enginePowerKw[i] = 0.0f;
                runtime.thrustNewton[i] = 0.0f;
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

        runtime.bootstrapDone = true;
    }

    int substeps = 0;
    state.planetMode = environment.worldShape == WorldShape::Planet;

    while (runtime.accumulator >= fixedDt && substeps < config.maxSubsteps) {
        runtime.accumulator -= fixedDt;
        ++runtime.tick;
        ++substeps;

        float atmosphereAltitudeMeters = state.pos.y * config.metersPerUnit;
        if (environment.worldShape == WorldShape::Planet) {
            const float radialDistance = length(state.pos - environment.planetCenterWorld);
            atmosphereAltitudeMeters = (radialDistance - static_cast<float>(environment.planet.radiusMeters)) * config.metersPerUnit;
        }

        const AtmosphereSample atmosphere = sampleAtmosphere(atmosphereAltitudeMeters);
        runtime.lastAtmosphere = atmosphere;

        // Flight state is simulated in the planet-fixed local frame, so the atmosphere is already stationary here.
        const Vec3 airVelWorld = state.flightVel - environment.wind;
        const Vec3 airVelBody = worldToBody(state.rot, airVelWorld);
        const float trueAirspeed = length(airVelBody);

        const float throttleAlpha = clamp(fixedDt / std::max(0.05f, config.throttleTimeConstant), 0.0f, 1.0f);
        runtime.engineThrottle += (state.throttle - runtime.engineThrottle) * throttleAlpha;

        const float boostMultiplier = input.flightAfterburner ? std::max(1.0f, config.afterburnerMultiplier) : 1.0f;
        const float propellerDiameter = std::max(0.5f, config.propellerDiameterMeters);
        const float ambientPressureKpa = std::max(12.0f, atmosphere.pressurePa * 0.001f);
        const float throttleCommand = clamp(runtime.engineThrottle, 0.0f, 1.0f);
        const float idleManifoldRatio = clamp(32.0f / 101.325f, 0.12f, 0.55f);
        const float displacementM3 = std::max(0.5f, config.engineDisplacementLiters) * 0.001f;
        const float effectiveAirspeed = std::max(22.0f, trueAirspeed);

        float totalFuelFlowKgPerSec = 0.0f;
        float totalAirMassFlowKgPerSec = 0.0f;
        float totalEnginePowerKw = 0.0f;
        float totalThrustNewton = 0.0f;
        float weightedPropEfficiency = 0.0f;
        Vec3 engineForceBody { 0.0f, 0.0f, 0.0f };
        Vec3 engineMomentBody { 0.0f, 0.0f, 0.0f };

        const float brakePowerLimitPerEngine =
            std::max(15.0f, config.maxBrakePowerKw * std::max(0.20f, atmosphere.sigma) * boostMultiplier);

        for (int i = 0; i < engineCount; ++i) {
            const float combustionAvailability = runtime.fuelRemainingKg > 0.0f ? 1.0f : 0.0f;

            if (jetModel) {
                const float idleN1 = 32.0f;
                const float maxN1 = input.flightAfterburner ? 108.0f : 100.0f;
                const float n1Target = mix(idleN1, maxN1, throttleCommand * combustionAvailability);
                const float spoolTau = n1Target > runtime.crankRpm[i] ? 1.8f : 2.8f;
                runtime.crankRpm[i] +=
                    (n1Target - runtime.crankRpm[i]) * clamp(fixedDt / std::max(0.15f, spoolTau), 0.0f, 1.0f);
                runtime.shaftRpm[i] = runtime.crankRpm[i];
                runtime.propRpm[i] = 0.0f;

                const float n1Norm =
                    clamp((runtime.crankRpm[i] - idleN1) / std::max(1.0f, maxN1 - idleN1), 0.0f, 1.2f);
                const float densityFactor = std::pow(std::max(0.12f, atmosphere.sigma), 0.72f);
                const float mach = trueAirspeed / std::max(120.0f, atmosphere.speedOfSound);
                const float speedFactor = clamp(1.0f - (mach * 0.18f), 0.62f, 1.04f);
                const float throttleCurve = 0.16f + (0.84f * std::pow(n1Norm, 1.08f));
                runtime.thrustNewton[i] = clamp(
                    config.maxThrustSeaLevel * densityFactor * speedFactor * throttleCurve * combustionAvailability * boostMultiplier,
                    0.0f,
                    std::max(400.0f, config.maxThrustSeaLevel * boostMultiplier * 1.45f));

                runtime.manifoldPressureKpa[i] =
                    mix(ambientPressureKpa * 0.35f, ambientPressureKpa * 1.65f, clamp(n1Norm, 0.0f, 1.0f));
                runtime.propellerEfficiency[i] = 0.0f;

                const float tsfcKgPerNHour = input.flightAfterburner ? 0.098f : 0.073f;
                runtime.fuelFlowKgPerSec[i] = runtime.thrustNewton[i] * (tsfcKgPerNHour / 3600.0f);
                runtime.airMassFlowKgPerSec[i] = runtime.fuelFlowKgPerSec[i] * 46.0f;
                runtime.enginePowerKw[i] = (runtime.thrustNewton[i] * std::max(35.0f, trueAirspeed)) * 0.001f;

                const float egtTargetK = 770.0f + (n1Norm * 320.0f) + (input.flightAfterburner ? 170.0f : 0.0f);
                const float chtTargetK = 390.0f + (n1Norm * 72.0f);
                const float oilTargetK = 350.0f + (n1Norm * 48.0f);

                runtime.exhaustGasTempK[i] +=
                    (egtTargetK - runtime.exhaustGasTempK[i]) * clamp(fixedDt * 1.4f, 0.0f, 1.0f);
                runtime.cylinderHeadTempK[i] +=
                    (chtTargetK - runtime.cylinderHeadTempK[i]) * clamp(fixedDt * 0.35f, 0.0f, 1.0f);
                runtime.oilTempK[i] +=
                    (oilTargetK - runtime.oilTempK[i]) * clamp(fixedDt * 0.24f, 0.0f, 1.0f);
            } else {
                const float manifoldTargetKpa = ambientPressureKpa * clamp(
                    idleManifoldRatio + (throttleCommand * 0.76f * boostMultiplier * combustionAvailability),
                    idleManifoldRatio,
                    boostMultiplier * 1.05f);

                runtime.manifoldPressureKpa[i] +=
                    (manifoldTargetKpa - runtime.manifoldPressureKpa[i]) *
                    clamp(fixedDt * 6.0f, 0.0f, 1.0f);

                const float windmillRpm = clamp(
                    (trueAirspeed / propellerDiameter) * 32.0f,
                    std::max(350.0f, config.idleCrankRpm * 0.45f),
                    std::max(800.0f, config.maxCrankRpm * 0.78f));

                const float propTipSpeed = kPi * propellerDiameter * std::max(5.0f, runtime.propRpm[i] / 60.0f);
                const float advanceRatio = trueAirspeed / std::max(18.0f, propTipSpeed);
                const float rpmLoadFactor = clamp(1.06f - (advanceRatio * 0.12f), 0.88f, 1.08f);

                const float commandedRpm =
                    mix(
                        std::max(450.0f, config.idleCrankRpm),
                        std::max(config.idleCrankRpm + 200.0f, config.maxCrankRpm),
                        throttleCommand * combustionAvailability);

                const float rpmTarget = std::max(windmillRpm, commandedRpm * rpmLoadFactor);
                runtime.crankRpm[i] += (rpmTarget - runtime.crankRpm[i]) * clamp(fixedDt * 4.0f, 0.0f, 1.0f);
                runtime.shaftRpm[i] = runtime.crankRpm[i];
                runtime.propRpm[i] = runtime.shaftRpm[i] / std::max(0.25f, config.propellerGearRatio);

                const float manifoldDensity =
                    std::max(0.05f, atmosphere.densityKgM3 * (runtime.manifoldPressureKpa[i] / ambientPressureKpa));

                runtime.airMassFlowKgPerSec[i] =
                    displacementM3 *
                    (std::max(0.0f, runtime.crankRpm[i]) / 120.0f) *
                    manifoldDensity *
                    0.86f *
                    combustionAvailability;

                runtime.fuelFlowKgPerSec[i] = runtime.airMassFlowKgPerSec[i] / 12.8f;

                const float chemicalPowerKw = runtime.fuelFlowKgPerSec[i] * 43000.0f;
                runtime.enginePowerKw[i] = std::min(chemicalPowerKw * 0.35f, brakePowerLimitPerEngine);

                const float propAdvanceNorm = clamp(advanceRatio / 0.85f, 0.0f, 2.0f);
                runtime.propellerEfficiency[i] = clamp(
                    0.26f + (0.58f * std::exp(-((propAdvanceNorm - 0.85f) * (propAdvanceNorm - 0.85f)) / 0.18f)),
                    0.22f,
                    0.86f);

                runtime.thrustNewton[i] = clamp(
                    (runtime.enginePowerKw[i] * 1000.0f * runtime.propellerEfficiency[i]) / effectiveAirspeed,
                    0.0f,
                    std::max(400.0f, config.maxThrustSeaLevel * boostMultiplier * 1.25f));

                const float powerNorm = runtime.enginePowerKw[i] / std::max(1.0e-3f, brakePowerLimitPerEngine);
                const float coolingFlow =
                    clamp((trueAirspeed / std::max(25.0f, config.maxEffectivePropSpeed)) + 0.12f, 0.12f, 1.8f);

                const float egtTargetK = 720.0f + (powerNorm * 220.0f);
                const float chtTargetK = 365.0f + (powerNorm * 105.0f) - (coolingFlow * 42.0f);
                const float oilTargetK = 330.0f + (powerNorm * 62.0f) - (coolingFlow * 20.0f);

                runtime.exhaustGasTempK[i] +=
                    (egtTargetK - runtime.exhaustGasTempK[i]) * clamp(fixedDt * 1.8f, 0.0f, 1.0f);
                runtime.cylinderHeadTempK[i] +=
                    (chtTargetK - runtime.cylinderHeadTempK[i]) * clamp(fixedDt * 0.45f, 0.0f, 1.0f);
                runtime.oilTempK[i] +=
                    (oilTargetK - runtime.oilTempK[i]) * clamp(fixedDt * 0.22f, 0.0f, 1.0f);
            }

            totalAirMassFlowKgPerSec += runtime.airMassFlowKgPerSec[i];
            totalFuelFlowKgPerSec += runtime.fuelFlowKgPerSec[i];
            totalEnginePowerKw += runtime.enginePowerKw[i];
            totalThrustNewton += runtime.thrustNewton[i];
            weightedPropEfficiency += runtime.propellerEfficiency[i] * runtime.thrustNewton[i];

            engineForceBody.z += runtime.thrustNewton[i];
        }

        if (runtime.fuelRemainingKg > 0.0f) {
            const float fuelUsed = totalFuelFlowKgPerSec * fixedDt;
            if (fuelUsed >= runtime.fuelRemainingKg && fuelUsed > 0.0f) {
                const float scale = runtime.fuelRemainingKg / fuelUsed;

                for (int i = 0; i < engineCount; ++i) {
                    runtime.airMassFlowKgPerSec[i] *= scale;
                    runtime.fuelFlowKgPerSec[i] *= scale;
                    runtime.enginePowerKw[i] *= scale;
                    runtime.thrustNewton[i] *= scale;
                }

                totalAirMassFlowKgPerSec *= scale;
                totalFuelFlowKgPerSec *= scale;
                totalEnginePowerKw *= scale;
                totalThrustNewton *= scale;
                weightedPropEfficiency *= scale;
                engineForceBody *= scale;
                engineMomentBody *= scale;
                runtime.fuelRemainingKg = 0.0f;
            } else {
                runtime.fuelRemainingKg -= fuelUsed;
            }
        } else {
            totalAirMassFlowKgPerSec = 0.0f;
            totalFuelFlowKgPerSec = 0.0f;
            totalEnginePowerKw = 0.0f;
            totalThrustNewton = 0.0f;
            weightedPropEfficiency = 0.0f;
            engineForceBody = Vec3 { 0.0f, 0.0f, 0.0f };
            engineMomentBody = Vec3 { 0.0f, 0.0f, 0.0f };

            for (int i = 0; i < engineCount; ++i) {
                runtime.airMassFlowKgPerSec[i] = 0.0f;
                runtime.fuelFlowKgPerSec[i] = 0.0f;
                runtime.enginePowerKw[i] = 0.0f;
                runtime.thrustNewton[i] = 0.0f;
            }
        }

        runtime.lastThrustNewton = totalThrustNewton;

        if (config.enableAutoTrim) {
            if (runtime.tick >= runtime.trimNextUpdateTick) {
                runtime.trimTarget =
                    -estimateElevatorForLevelFlight(config, atmosphere, std::max(20.0f, trueAirspeed));
                const int trimIntervalTicks =
                    std::max(1, static_cast<int>(std::round(config.physicsHz / std::max(0.5f, config.autoTrimUpdateHz))));
                runtime.trimNextUpdateTick = runtime.tick + trimIntervalTicks;
            }
            runtime.elevatorTrim += (runtime.trimTarget - runtime.elevatorTrim) * clamp(fixedDt * 0.6f, 0.0f, 1.0f);
        } else {
            runtime.elevatorTrim = 0.0f;
            runtime.trimTarget = 0.0f;
        }

        const float baseControlAuthority = clamp(
            trueAirspeed / std::max(5.0f, config.controlAuthoritySpeed),
            clamp(config.minControlAuthority, 0.05f, 1.0f),
            1.0f);

        const float alphaAbsForAuthority = std::fabs(runtime.lastAlpha);
        const float stallAuthorityBlend = clamp(
            (alphaAbsForAuthority - config.alphaStallRad) / std::max(radians(3.0f), config.postStallSpinBlendRangeRad),
            0.0f,
            1.0f);

        const float elevatorAuthority = baseControlAuthority * mix(1.0f, 0.65f, stallAuthorityBlend);
        const float aileronAuthority = baseControlAuthority * mix(1.0f, 0.20f, stallAuthorityBlend);
        const float rudderAuthority = baseControlAuthority * mix(1.0f, 0.55f, stallAuthorityBlend);

        const float elevatorTarget = clamp(
            (state.yoke.pitch * elevatorAuthority * std::max(0.05f, config.pitchControlScale) * config.maxElevatorDeflectionRad) +
                runtime.elevatorTrim +
                state.manualElevatorTrim,
            -config.maxElevatorDeflectionRad,
            config.maxElevatorDeflectionRad);

        const float aileronTarget = clamp(
            state.yoke.roll * aileronAuthority * std::max(0.05f, config.rollControlScale) * config.maxAileronDeflectionRad,
            -config.maxAileronDeflectionRad,
            config.maxAileronDeflectionRad);

        const float rudderTarget = clamp(
            (-state.yoke.yaw * rudderAuthority * std::max(0.05f, config.yawControlScale) * config.maxRudderDeflectionRad) +
                state.manualRudderTrim,
            -config.maxRudderDeflectionRad,
            config.maxRudderDeflectionRad);

        runtime.elevatorDeflection = elevatorTarget;
        runtime.aileronDeflection = aileronTarget;
        runtime.rudderDeflection = rudderTarget;

        const AeroState aero = computeAero(
            airVelBody,
            state.flightAngVel,
            runtime.elevatorDeflection,
            runtime.aileronDeflection,
            runtime.rudderDeflection,
            atmosphere,
            config);

        runtime.lastAlpha = aero.alpha;
        runtime.lastBeta = aero.beta;
        runtime.lastDynamicPressure = aero.dynamicPressure;

        Vec3 forceBody = aero.forceBody + engineForceBody;
        forceBody = clampMagnitude(forceBody, std::max(1000.0f, config.maxForceNewton));
        const Vec3 forceWorld = bodyToWorld(state.rot, forceBody);

        Vec3 momentBody = aero.momentBody + engineMomentBody;
        if (config.stabilityAugmentation) {
            const float postStallBlend = clamp(
                (std::fabs(aero.alpha) - config.alphaStallRad) / std::max(radians(2.0f), config.postStallSpinBlendRangeRad),
                0.0f,
                1.0f);

            const float postStallDampingScale = clamp(config.postStallDampingScale, 0.0f, 3.0f);
            const float dampingScale = mix(1.0f, postStallDampingScale, postStallBlend);

            const float pitchDamping = std::max(0.0f, config.pitchRateDampingMoment) * dampingScale;
            const float yawDamping = std::max(0.0f, config.yawRateDampingMoment) * dampingScale;
            const float rollDamping = std::max(0.0f, config.rollRateDampingMoment) * dampingScale;

            momentBody.x -= state.flightAngVel.x * pitchDamping;
            momentBody.y -= state.flightAngVel.y * yawDamping;
            momentBody.z -= state.flightAngVel.z * rollDamping;

            const float extraYawDamper = mix(0.0f, std::max(0.0f, config.yawRateDampingMoment) * 0.75f, postStallBlend);
            const float extraRollDamper = mix(0.0f, std::max(0.0f, config.rollRateDampingMoment) * 0.35f, postStallBlend);

            momentBody.y -= state.flightAngVel.y * extraYawDamper;
            momentBody.z -= state.flightAngVel.z * extraRollDamper;
        }
        momentBody = clampMagnitude(momentBody, std::max(1000.0f, config.maxMomentNewtonMeter));

        integrateRigidBody(state, config, forceWorld, momentBody, environment, atmosphere.gravity, fixedDt);
        applyTerrainContact(state, runtime, config, environment, fixedDt);
        applyNumericalGuards(state, config);

        if (runtime.crashed) {
            break;
        }
    }

    state.vel = state.flightVel;

    state.debug.tick = runtime.tick;
    state.debug.substeps = substeps;
    state.debug.airDensity = runtime.lastAtmosphere.densityKgM3;
    state.debug.alpha = runtime.lastAlpha;
    state.debug.beta = runtime.lastBeta;
    state.debug.qbar = runtime.lastDynamicPressure;
    state.debug.thrust = runtime.lastThrustNewton;
    state.debug.throttle = runtime.engineThrottle;
    state.debug.speed = length(state.flightVel);
    state.debug.crankRpm = averageEngineValue(runtime.crankRpm, engineCount);
    state.debug.propRpm = averageEngineValue(runtime.propRpm, engineCount);
    state.debug.manifoldPressureKpa = averageEngineValue(runtime.manifoldPressureKpa, engineCount);
    state.debug.enginePowerKw = averageEngineValue(runtime.enginePowerKw, engineCount);
    state.debug.fuelFlowKgPerHour = averageEngineValue(runtime.fuelFlowKgPerSec, engineCount) * 3600.0f;
    state.debug.exhaustGasTempC = averageEngineValue(runtime.exhaustGasTempK, engineCount) - 273.15f;
    state.debug.cylinderHeadTempC = averageEngineValue(runtime.cylinderHeadTempK, engineCount) - 273.15f;
    state.debug.oilTempC = averageEngineValue(runtime.oilTempK, engineCount) - 273.15f;
    state.debug.fuelRemainingKg = runtime.fuelRemainingKg;
    state.inertialPos = toDVec3(state.pos);
    state.inertialVel = toDVec3(state.flightVel);
}

}  // namespace NativeGame
