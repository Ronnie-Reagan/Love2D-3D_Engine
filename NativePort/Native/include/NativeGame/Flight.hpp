#pragma once

#include "NativeGame/Math.hpp"

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

struct FlightConfig {
    float physicsHz = 120.0f;
    int maxSubsteps = 8;
    float maxFrameDt = 0.1f;
    float metersPerUnit = 1.0f;
    float massKg = 1150.0f;
    float Ixx = 1200.0f;
    float Iyy = 1700.0f;
    float Izz = 2500.0f;
    float Ixz = 0.0f;
    float wingArea = 16.2f;
    float wingSpan = 10.9f;
    float meanChord = 1.5f;
    float CL0 = 0.25f;
    float CLalpha = 5.5f;
    float CLElevator = 0.65f;
    float alphaStallRad = radians(15.0f);
    float stallLiftDropoff = 0.55f;
    float CD0 = 0.03f;
    float inducedDragK = 0.045f;
    float CYbeta = -0.95f;
    float CYrudder = 0.28f;
    float Cm0 = 0.04f;
    float CmAlpha = -1.2f;
    float CmQ = -12.0f;
    float CmElevator = -1.35f;
    float ClBeta = -0.12f;
    float ClP = -0.48f;
    float ClR = 0.16f;
    float ClAileron = 0.22f;
    float ClRudder = 0.03f;
    float CnBeta = 0.16f;
    float CnR = -0.24f;
    float CnP = -0.06f;
    float CnRudder = -0.17f;
    float CnAileron = 0.02f;
    float maxThrustSeaLevel = 3200.0f;
    float afterburnerMultiplier = 1.45f;
    float maxEffectivePropSpeed = 95.0f;
    float throttleTimeConstant = 0.8f;
    float maxElevatorDeflectionRad = radians(25.0f);
    float maxAileronDeflectionRad = radians(20.0f);
    float maxRudderDeflectionRad = radians(30.0f);
    float maxManualElevatorTrimRad = radians(8.0f);
    float pitchInputExpo = 0.72f;
    float rollInputExpo = 0.58f;
    float yawInputExpo = 0.38f;
    float elevatorSurfaceRateRadPerSec = radians(55.0f);
    float aileronSurfaceRateRadPerSec = radians(80.0f);
    float rudderSurfaceRateRadPerSec = radians(60.0f);
    float controlLoadingReferenceDynamicPressure = 2300.0f;
    float controlLoadingRateBlend = 0.45f;
    bool enableAutoTrim = false;
    float autoTrimUpdateHz = 6.0f;
    bool autoTrimUseWorker = true;
    float autoTrimWorkerTimeoutSec = 0.35f;
    float groundFriction = 0.92f;
    float groundAngularDamping = 0.82f;
    bool stabilityAugmentation = true;
    float pitchRateDampingMoment = 2600.0f;
    float yawRateDampingMoment = 1700.0f;
    float rollRateDampingMoment = 2100.0f;
    float controlAuthoritySpeed = 34.0f;
    float minControlAuthority = 0.28f;
    float pitchControlScale = 0.78f;
    float rollControlScale = 0.72f;
    float yawControlScale = 0.65f;
    bool crashEnabled = true;
    float maxLinearSpeed = 180.0f;
    float maxAngularRateRad = radians(240.0f);
    float maxForceNewton = 70000.0f;
    float maxMomentNewtonMeter = 120000.0f;
    float maxPositionAbs = 500000.0f;
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
};

struct FlightState {
    Vec3 pos { 0.0f, 0.0f, 0.0f };
    Quat rot = quatIdentity();
    Vec3 vel { 0.0f, 0.0f, 0.0f };
    Vec3 flightVel { 0.0f, 0.0f, 0.0f };
    Vec3 flightAngVel { 0.0f, 0.0f, 0.0f };
    struct {
        float pitch = 0.0f;
        float yaw = 0.0f;
        float roll = 0.0f;
    } yoke;
    float manualElevatorTrim = 0.0f;
    float throttle = 0.0f;
    float throttleAccel = 0.55f;
    float airBrakeStrength = 1.2f;
    float wheelThrottleStep = 0.06f;
    float wheelElevatorTrimStepRad = radians(0.25f);
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

struct FlightRuntimeState {
    float accumulator = 0.0f;
    int tick = 0;
    float engineThrottle = 0.0f;
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

    const float altitude = clamp(altitudeMeters, -2000.0f, 32000.0f);
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
    return {
        altitude,
        temperature,
        pressure,
        density,
        density / 1.225f,
        std::sqrt(gammaAir * gasConstantAir * temperature),
        gravity
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
}

inline void updatePilotInputs(FlightState& state, float dt, float nowSeconds, const InputState& input)
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
    state.yoke.yaw = clamp(state.yoke.yaw + (yawAxis * state.yokeKeyboardRate * dt), -1.0f, 1.0f);

    if (!holdMouseYoke && !input.flightUseAnalogYoke && pitchAxis == 0.0f) {
        state.yoke.pitch += (0.0f - state.yoke.pitch) * autoCenterAlpha;
    }
    if (!holdMouseYoke && !input.flightHoldYaw && yawAxis == 0.0f) {
        state.yoke.yaw += (0.0f - state.yoke.yaw) * autoCenterAlpha;
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

    const float alphaModelClamp = std::max(config.alphaStallRad + radians(2.0f), radians(50.0f));
    const float betaModelClamp = radians(65.0f);
    const float clMin = -1.4f;
    const float clMax = 1.8f;
    const float cdMax = 2.2f;
    const float cyMax = 1.4f;
    const float cMomentMax = 2.8f;
    const float rateNormMax = 4.0f;

    const float alpha = clamp(rawAlpha, -alphaModelClamp, alphaModelClamp);
    const float beta = clamp(rawBeta, -betaModelClamp, betaModelClamp);

    const float pitchRateNorm = clamp((pitchRate * config.meanChord) / (2.0f * speed), -rateNormMax, rateNormMax);
    const float rollRateNorm = clamp((rollRate * config.wingSpan) / (2.0f * speed), -rateNormMax, rateNormMax);
    const float yawRateNorm = clamp((yawRate * config.wingSpan) / (2.0f * speed), -rateNormMax, rateNormMax);

    float cl = config.CL0 + (config.CLalpha * alpha) + (config.CLElevator * elevator);
    if (std::fabs(alpha) > config.alphaStallRad) {
        const float exceed = std::fabs(alpha) - config.alphaStallRad;
        const float drop = std::exp(-exceed * config.stallLiftDropoff * 8.0f);
        cl *= clamp(drop, 0.15f, 1.0f);
    }
    cl = clamp(cl, clMin, clMax);

    const float cd = clamp(config.CD0 + (config.inducedDragK * cl * cl), 0.01f, cdMax);
    const float cy = clamp((config.CYbeta * beta) + (config.CYrudder * rudder), -cyMax, cyMax);

    float cRoll =
        (config.ClBeta * beta) +
        (config.ClP * rollRateNorm) +
        (config.ClR * yawRateNorm) +
        (config.ClAileron * aileron) +
        (config.ClRudder * rudder);
    float cPitch =
        config.Cm0 +
        (config.CmAlpha * alpha) +
        (config.CmQ * pitchRateNorm) +
        (config.CmElevator * elevator);
    float cYaw =
        (config.CnBeta * beta) +
        (config.CnR * yawRateNorm) +
        (config.CnP * rollRateNorm) +
        (config.CnRudder * rudder) +
        (config.CnAileron * aileron);

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

inline void integrateRigidBody(
    FlightState& state,
    const FlightConfig& config,
    const Vec3& forceWorld,
    const Vec3& momentBody,
    float gravityAccel,
    float dt)
{
    const float mass = std::max(0.1f, config.massKg);

    const Vec3 accelWorld {
        forceWorld.x / mass,
        (forceWorld.y / mass) - std::fabs(gravityAccel),
        forceWorld.z / mass
    };
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
                    }
                }
            }

            const float penetration = radius - dist;
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
    float dt,
    float nowSeconds,
    const InputState& input,
    const FlightEnvironment& environment,
    const FlightConfig& config)
{
    dt = clamp(dt, 0.0f, config.maxFrameDt);
    if (dt <= 0.0f) {
        return;
    }

    runtime.crashed = false;
    runtime.hasPendingCrash = false;
    applyNumericalGuards(state, config);
    updatePilotInputs(state, dt, nowSeconds, input);

    if (!runtime.bootstrapDone) {
        const AtmosphereSample atmosphere = sampleAtmosphere(state.pos.y * config.metersPerUnit);
        const Vec3 airVelWorld = state.flightVel - environment.wind;
        const Vec3 airVelBody = worldToBody(state.rot, airVelWorld);
        const float trimAirspeed = std::max(20.0f, length(airVelBody));
        runtime.elevatorTrim = config.enableAutoTrim ? estimateElevatorForLevelFlight(config, atmosphere, trimAirspeed) : 0.0f;
        runtime.trimTarget = runtime.elevatorTrim;
        runtime.elevatorDeflection = clamp(
            runtime.elevatorTrim,
            -config.maxElevatorDeflectionRad,
            config.maxElevatorDeflectionRad);
        runtime.aileronDeflection = 0.0f;
        runtime.rudderDeflection = 0.0f;
        runtime.lastAtmosphere = atmosphere;
        runtime.engineThrottle = state.throttle;
        runtime.bootstrapDone = true;
    }

    const float fixedDt = 1.0f / std::max(1.0f, config.physicsHz);
    runtime.accumulator += dt;
    int substeps = 0;

    while (runtime.accumulator >= fixedDt && substeps < config.maxSubsteps) {
        runtime.accumulator -= fixedDt;
        ++runtime.tick;
        ++substeps;

        const AtmosphereSample atmosphere = sampleAtmosphere(state.pos.y * config.metersPerUnit);
        runtime.lastAtmosphere = atmosphere;

        const Vec3 airVelWorld = state.flightVel - environment.wind;
        const Vec3 airVelBody = worldToBody(state.rot, airVelWorld);
        const float trueAirspeed = length(airVelBody);

        const float throttleAlpha = clamp(fixedDt / std::max(0.05f, config.throttleTimeConstant), 0.0f, 1.0f);
        runtime.engineThrottle += (state.throttle - runtime.engineThrottle) * throttleAlpha;
        const float speedFactor = clamp(1.0f - (trueAirspeed / std::max(40.0f, config.maxEffectivePropSpeed)), 0.18f, 1.0f);
        runtime.lastThrustNewton =
            std::max(0.0f, config.maxThrustSeaLevel) *
            std::max(0.05f, atmosphere.sigma) *
            clamp(runtime.engineThrottle, 0.0f, 1.0f) *
            speedFactor *
            (input.flightAfterburner ? std::max(1.0f, config.afterburnerMultiplier) : 1.0f);

        if (config.enableAutoTrim) {
            if (runtime.tick >= runtime.trimNextUpdateTick) {
                runtime.trimTarget = estimateElevatorForLevelFlight(config, atmosphere, std::max(20.0f, trueAirspeed));
                const int trimIntervalTicks = std::max(1, static_cast<int>(std::round(config.physicsHz / std::max(0.5f, config.autoTrimUpdateHz))));
                runtime.trimNextUpdateTick = runtime.tick + trimIntervalTicks;
            }
            runtime.elevatorTrim += (runtime.trimTarget - runtime.elevatorTrim) * clamp(fixedDt * 0.6f, 0.0f, 1.0f);
        } else {
            runtime.elevatorTrim = 0.0f;
            runtime.trimTarget = 0.0f;
        }

        const float controlAuthority = clamp(
            trueAirspeed / std::max(5.0f, config.controlAuthoritySpeed),
            clamp(config.minControlAuthority, 0.05f, 1.0f),
            1.0f);

        const float elevatorTarget = clamp(
            (state.yoke.pitch * controlAuthority * std::max(0.05f, config.pitchControlScale) * config.maxElevatorDeflectionRad) +
                runtime.elevatorTrim +
                state.manualElevatorTrim,
            -config.maxElevatorDeflectionRad,
            config.maxElevatorDeflectionRad);
        const float aileronTarget = clamp(
            state.yoke.roll * controlAuthority * std::max(0.05f, config.rollControlScale) * config.maxAileronDeflectionRad,
            -config.maxAileronDeflectionRad,
            config.maxAileronDeflectionRad);
        const float rudderTarget = clamp(
            -state.yoke.yaw * controlAuthority * std::max(0.05f, config.yawControlScale) * config.maxRudderDeflectionRad,
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

        Vec3 forceBody = aero.forceBody;
        forceBody.z += runtime.lastThrustNewton;
        forceBody = clampMagnitude(forceBody, std::max(1000.0f, config.maxForceNewton));
        const Vec3 forceWorld = bodyToWorld(state.rot, forceBody);

        Vec3 momentBody = aero.momentBody;
        if (config.stabilityAugmentation) {
            momentBody.x -= state.flightAngVel.x * std::max(0.0f, config.pitchRateDampingMoment);
            momentBody.y -= state.flightAngVel.y * std::max(0.0f, config.yawRateDampingMoment);
            momentBody.z -= state.flightAngVel.z * std::max(0.0f, config.rollRateDampingMoment);
        }
        momentBody = clampMagnitude(momentBody, std::max(1000.0f, config.maxMomentNewtonMeter));

        integrateRigidBody(state, config, forceWorld, momentBody, atmosphere.gravity, fixedDt);
        applyTerrainContact(state, runtime, config, environment, fixedDt);
        applyNumericalGuards(state, config);

        if (runtime.crashed) {
            break;
        }
    }

    if (runtime.crashed) {
        state.flightVel = {};
        state.flightAngVel = {};
        state.vel = {};
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
}

}  // namespace NativeGame
