#pragma once

#include <SDL3/SDL.h>

#include "NativeGame/Math.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace NativeGame {

struct PropAudioConfig {
    float baseRpm = 24.0f;
    float loadRpmContribution = 148.0f;
    float propellerBladeCount = 3.0f;
    float propellerDiameterMeters = 2.05f;
    float engineFrequencyScale = 0.82f;
    float engineTonalMix = 0.78f;
    float propHarmonicMix = 1.28f;
    float engineNoiseAmount = 0.92f;
    float ambienceFrequencyScale = 0.94f;
    float waterAmbienceGain = 1.0f;
    float groundAmbienceGain = 1.08f;
};

inline PropAudioConfig defaultPropAudioConfig()
{
    return {};
}

struct ProceduralAudioFrame {
    bool active = true;
    bool paused = false;
    bool audioEnabled = true;
    bool onGround = false;
    bool exteriorView = false;
    bool afterburner = false;
    float dt = 1.0f / 60.0f;
    float masterVolume = 1.0f;
    float engineVolume = 1.0f;
    float ambienceVolume = 1.0f;
    float engineThrottle = 0.0f;
    float crankRpm = 780.0f;
    float propRpm = 780.0f;
    float maxCrankRpm = 2750.0f;
    float maxPropRpm = 2750.0f;
    float manifoldPressureKpa = 101.325f;
    float fuelFlowKgPerSec = 0.0f;
    float enginePowerKw = 0.0f;
    float maxBrakePowerKw = 215.0f;
    float exhaustGasTempK = 720.0f;
    float cylinderHeadTempK = 360.0f;
    float oilTempK = 330.0f;
    float engineCylinderCount = 6.0f;
    float trueAirspeed = 0.0f;
    float referenceSpeed = 95.0f;
    float dynamicPressure = 0.0f;
    float referenceDynamicPressure = 2300.0f;
    float thrustNewton = 0.0f;
    float maxThrustNewton = 3200.0f;
    float alphaRad = 0.0f;
    float stallAlphaRad = radians(15.0f);
    float betaRad = 0.0f;
    float verticalSpeed = 0.0f;
    float angularRateRad = 0.0f;
    float maxAngularRateRad = radians(240.0f);
    float speedOfSound = 340.0f;
    float groundSpeed = 0.0f;
    float heightAboveGroundMeters = 1000.0f;
    float pitchRateRad = 0.0f;
    float yawRateRad = 0.0f;
    float rollRateRad = 0.0f;
    float waterProximity = 0.0f;
    float foliageBrush = 0.0f;
    float foliageImpact = 0.0f;
    float gunshotImpulse = 0.0f;
    float terrainShotImpulse = 0.0f;
    float bombLatchImpulse = 0.0f;
    float explosionImpulse = 0.0f;
    float explosionDistanceMeters = 1000.0f;
    float projectileWhistleAmount = 0.0f;
    float projectilePitchScale = 1.0f;
    float projectileDoppler = 1.0f;
    float bombWhistleAmount = 0.0f;
    float bombPitchScale = 1.0f;
    float bombDoppler = 1.0f;
    PropAudioConfig propAudioConfig = defaultPropAudioConfig();
};

struct ProceduralAudioState {
    SDL_AudioStream* stream = nullptr;
    bool available = false;
    int sampleRate = 22050;
    int bufferSamples = 1024;
    int queueDepth = 8;
    std::uint32_t noiseSeed = 19771337u;
    float enginePhase1 = 0.0f;
    float enginePhase2 = 0.0f;
    float enginePhase3 = 0.0f;
    float propPhase = 0.0f;
    float waterPhase = 0.0f;
    float groundPhase = 0.0f;
    float engineNoise = 0.0f;
    float windNoise = 0.0f;
    float buffetNoise = 0.0f;
    float slipNoise = 0.0f;
    float foliageNoise = 0.0f;
    float crankRpmState = 780.0f;
    float propRpmState = 780.0f;
    float maxCrankRpmState = 2750.0f;
    float maxPropRpmState = 2750.0f;
    float manifoldPressureKpaState = 101.325f;
    float fuelFlowKgPerSecState = 0.0f;
    float enginePowerKwState = 0.0f;
    float maxBrakePowerKwState = 215.0f;
    float exhaustGasTempKState = 720.0f;
    float cylinderHeadTempKState = 360.0f;
    float oilTempKState = 330.0f;
    float engineCylinderCountState = 6.0f;
    float throttleNorm = 0.0f;
    float thrustNorm = 0.0f;
    float dynamicPressureNorm = 0.0f;
    float speedNorm = 0.0f;
    float referenceSpeedMetersPerSec = 95.0f;
    float alphaNorm = 0.0f;
    float slipNorm = 0.0f;
    float buffetNorm = 0.0f;
    float pitchRateNorm = 0.0f;
    float yawRateNorm = 0.0f;
    float rollRateNorm = 0.0f;
    float maneuverNorm = 0.0f;
    float groundFactor = 0.0f;
    float groundRollNorm = 0.0f;
    float nearGroundFactor = 0.0f;
    float waterProximity = 0.0f;
    float foliageFactor = 0.0f;
    float foliageImpactPulse = 0.0f;
    float afterburnerBlend = 0.0f;
    float exteriorViewBlend = 0.0f;
    float pausedBlend = 0.0f;
    float gunshotPulse = 0.0f;
    float terrainShotPulse = 0.0f;
    float bombLatchPulse = 0.0f;
    float explosionPulse = 0.0f;
    float explosionDelaySec = 0.0f;
    float pendingExplosionImpulse = 0.0f;
    float pendingExplosionDistanceNorm = 1.0f;
    float explosionDistanceNorm = 1.0f;
    float projectileWhistleAmount = 0.0f;
    float projectilePitchScale = 1.0f;
    float projectileDoppler = 1.0f;
    float bombWhistleAmount = 0.0f;
    float bombPitchScale = 1.0f;
    float bombDoppler = 1.0f;
    float speedOfSoundMetersPerSec = 340.0f;
    float gunshotPhase = 0.0f;
    float latchPhase = 0.0f;
    float explosionPhase1 = 0.0f;
    float explosionPhase2 = 0.0f;
    float projectilePhase = 0.0f;
    float bombPhase = 0.0f;
    float combatNoise1 = 0.0f;
    float combatNoise2 = 0.0f;
    std::vector<float> scratchBuffer {};

    bool initialize(int preferredSampleRate, int preferredBufferSamples, int preferredQueueDepth, std::string* errorText = nullptr)
    {
        shutdown();

        sampleRate = std::max(48000, preferredSampleRate);
        bufferSamples = std::max(256, preferredBufferSamples);
        queueDepth = std::max(2, preferredQueueDepth);
        scratchBuffer.assign(static_cast<std::size_t>(bufferSamples), 0.0f);

        const SDL_AudioSpec spec { SDL_AUDIO_F32, 1, sampleRate };
        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (stream == nullptr) {
            available = false;
            if (errorText != nullptr) {
                *errorText = SDL_GetError();
            }
            return false;
        }

        if (!SDL_ResumeAudioStreamDevice(stream)) {
            if (errorText != nullptr) {
                *errorText = SDL_GetError();
            }
            SDL_DestroyAudioStream(stream);
            stream = nullptr;
            available = false;
            return false;
        }

        available = true;
        return true;
    }

    void shutdown()
    {
        if (stream != nullptr) {
            SDL_DestroyAudioStream(stream);
            stream = nullptr;
        }
        available = false;
        scratchBuffer.clear();
    }

    void update(const ProceduralAudioFrame& frame)
    {
        if (!available || stream == nullptr) {
            return;
        }

        const bool active = frame.active && frame.audioEnabled;
        if (!active) {
            SDL_ClearAudioStream(stream);
            SDL_PauseAudioStreamDevice(stream);
            return;
        }

        SDL_ResumeAudioStreamDevice(stream);

        const float dt = clamp(frame.dt, 0.0f, 0.25f);
        const float blend = clamp(dt * 4.5f, 0.0f, 1.0f);
        const float fastBlend = clamp(dt * 7.0f, 0.0f, 1.0f);
        const float referenceSpeed = std::max(20.0f, frame.referenceSpeed);
        const float referenceDynamicPressure = std::max(150.0f, frame.referenceDynamicPressure);
        const float maxThrustNewton = std::max(50.0f, frame.maxThrustNewton);
        const float stallAlpha = std::max(radians(4.0f), std::fabs(frame.stallAlphaRad));
        const float betaReference = std::max(radians(4.0f), stallAlpha * 0.75f);
        const float rateReference = std::max(radians(40.0f), std::fabs(frame.maxAngularRateRad));
        const float flowReference = clamp(frame.dynamicPressure / referenceDynamicPressure, 0.0f, 2.0f);
        const float flowDrive = std::pow(clamp(flowReference / 1.25f, 0.0f, 1.75f), 0.65f);
        const float buffetOnset = stallAlpha * 0.72f;
        const float buffetRange = std::max(radians(1.0f), stallAlpha - buffetOnset);
        throttleNorm = mix(throttleNorm, clamp(frame.engineThrottle, 0.0f, 1.25f), blend);
        thrustNorm = mix(thrustNorm, clamp(frame.thrustNewton / maxThrustNewton, 0.0f, 1.35f), blend);
        dynamicPressureNorm = mix(dynamicPressureNorm, flowReference, blend);
        speedNorm = mix(speedNorm, clamp(frame.trueAirspeed / referenceSpeed, 0.0f, 1.6f), blend);
        referenceSpeedMetersPerSec = mix(referenceSpeedMetersPerSec, referenceSpeed, blend);
        alphaNorm = mix(alphaNorm, clamp(std::fabs(frame.alphaRad) / stallAlpha, 0.0f, 1.6f), fastBlend);
        slipNorm = mix(slipNorm, clamp(std::fabs(frame.betaRad) / betaReference, 0.0f, 1.6f), fastBlend);
        buffetNorm = mix(
            buffetNorm,
            clamp((std::fabs(frame.alphaRad) - buffetOnset) / buffetRange, 0.0f, 1.35f) * clamp(0.20f + flowDrive, 0.0f, 1.35f),
            fastBlend);
        pitchRateNorm = mix(pitchRateNorm, clamp(std::fabs(frame.pitchRateRad) / (rateReference * 0.55f), 0.0f, 1.4f), fastBlend);
        yawRateNorm = mix(yawRateNorm, clamp(std::fabs(frame.yawRateRad) / (rateReference * 0.60f), 0.0f, 1.4f), fastBlend);
        rollRateNorm = mix(rollRateNorm, clamp(std::fabs(frame.rollRateRad) / (rateReference * 0.75f), 0.0f, 1.4f), fastBlend);
        maneuverNorm = mix(
            maneuverNorm,
            clamp(
                ((pitchRateNorm * 0.44f) + (yawRateNorm * 0.34f) + (rollRateNorm * 0.22f)) * (0.35f + (flowDrive * 0.85f)),
                0.0f,
                1.35f),
            blend);
        groundFactor = mix(groundFactor, frame.onGround ? 1.0f : 0.0f, clamp(dt * 9.0f, 0.0f, 1.0f));
        groundRollNorm = mix(groundRollNorm, clamp(frame.groundSpeed / referenceSpeed, 0.0f, 1.4f), blend);
        nearGroundFactor = mix(
            nearGroundFactor,
            clamp(1.0f - (frame.heightAboveGroundMeters / std::max(8.0f, referenceSpeed * 0.40f)), 0.0f, 1.0f),
            blend);
        waterProximity = mix(waterProximity, clamp(frame.waterProximity, 0.0f, 1.0f), clamp(dt * 2.4f, 0.0f, 1.0f));
        foliageFactor = mix(foliageFactor, clamp(frame.foliageBrush, 0.0f, 1.0f), clamp(dt * 9.5f, 0.0f, 1.0f));
        foliageImpactPulse = std::max(clamp(frame.foliageImpact, 0.0f, 1.0f), foliageImpactPulse * clamp(1.0f - (dt * 3.8f), 0.0f, 1.0f));
        afterburnerBlend = mix(afterburnerBlend, frame.afterburner ? 1.0f : 0.0f, fastBlend);
        exteriorViewBlend = mix(exteriorViewBlend, frame.exteriorView ? 1.0f : 0.0f, clamp(dt * 6.5f, 0.0f, 1.0f));
        speedOfSoundMetersPerSec = mix(speedOfSoundMetersPerSec, clamp(frame.speedOfSound, 280.0f, 360.0f), blend);
        crankRpmState = mix(crankRpmState, std::max(450.0f, frame.crankRpm), blend);
        propRpmState = mix(propRpmState, std::max(450.0f, frame.propRpm), blend);
        maxCrankRpmState = mix(maxCrankRpmState, std::max(600.0f, frame.maxCrankRpm), blend);
        maxPropRpmState = mix(maxPropRpmState, std::max(600.0f, frame.maxPropRpm), blend);
        manifoldPressureKpaState = mix(manifoldPressureKpaState, std::max(12.0f, frame.manifoldPressureKpa), blend);
        fuelFlowKgPerSecState = mix(fuelFlowKgPerSecState, std::max(0.0f, frame.fuelFlowKgPerSec), blend);
        enginePowerKwState = mix(enginePowerKwState, std::max(0.0f, frame.enginePowerKw), blend);
        maxBrakePowerKwState = mix(maxBrakePowerKwState, std::max(10.0f, frame.maxBrakePowerKw), blend);
        exhaustGasTempKState = mix(exhaustGasTempKState, std::max(500.0f, frame.exhaustGasTempK), blend);
        cylinderHeadTempKState = mix(cylinderHeadTempKState, std::max(280.0f, frame.cylinderHeadTempK), blend);
        oilTempKState = mix(oilTempKState, std::max(280.0f, frame.oilTempK), blend);
        engineCylinderCountState = mix(engineCylinderCountState, clamp(frame.engineCylinderCount, 1.0f, 18.0f), blend);
        pausedBlend = mix(pausedBlend, frame.paused ? 1.0f : 0.0f, clamp(dt * 7.5f, 0.0f, 1.0f));
        gunshotPulse = std::max(clamp(frame.gunshotImpulse, 0.0f, 1.0f), gunshotPulse * clamp(1.0f - (dt * 24.0f), 0.0f, 1.0f));
        terrainShotPulse = std::max(clamp(frame.terrainShotImpulse, 0.0f, 1.0f), terrainShotPulse * clamp(1.0f - (dt * 14.0f), 0.0f, 1.0f));
        bombLatchPulse = std::max(clamp(frame.bombLatchImpulse, 0.0f, 1.0f), bombLatchPulse * clamp(1.0f - (dt * 6.4f), 0.0f, 1.0f));
        explosionPulse *= clamp(1.0f - (dt * 2.2f), 0.0f, 1.0f);
        if (frame.explosionImpulse > 0.0f) {
            pendingExplosionImpulse = std::max(pendingExplosionImpulse, clamp(frame.explosionImpulse, 0.0f, 1.0f));
            pendingExplosionDistanceNorm = clamp(frame.explosionDistanceMeters / 520.0f, 0.0f, 1.0f);
            explosionDelaySec = frame.explosionDistanceMeters / std::max(280.0f, speedOfSoundMetersPerSec);
        }
        projectileWhistleAmount = mix(projectileWhistleAmount, clamp(frame.projectileWhistleAmount, 0.0f, 1.4f), fastBlend);
        projectilePitchScale = mix(projectilePitchScale, clamp(frame.projectilePitchScale, 0.5f, 3.0f), fastBlend);
        projectileDoppler = mix(projectileDoppler, clamp(frame.projectileDoppler, 0.5f, 1.8f), fastBlend);
        bombWhistleAmount = mix(bombWhistleAmount, clamp(frame.bombWhistleAmount, 0.0f, 1.4f), fastBlend);
        bombPitchScale = mix(bombPitchScale, clamp(frame.bombPitchScale, 0.4f, 2.0f), fastBlend);
        bombDoppler = mix(bombDoppler, clamp(frame.bombDoppler, 0.6f, 1.6f), fastBlend);

        const float masterVolume = clamp(frame.masterVolume, 0.0f, 1.5f);
        const float engineVolume = clamp(frame.engineVolume, 0.0f, 1.5f);
        const float ambienceVolume = clamp(frame.ambienceVolume, 0.0f, 1.5f);
        const PropAudioConfig propAudio = frame.propAudioConfig;
        const float exterior = exteriorViewBlend;
        const float interior = 1.0f - exterior;
        const float airflow = std::pow(clamp(dynamicPressureNorm * 0.55f, 0.0f, 1.75f), 1.55f);
        const float masterDuck = clamp(1.0f - (pausedBlend * 0.84f), 0.03f, 1.0f);
        const float engineGain =
            (0.09f +
                (throttleNorm * 0.44f) +
                (thrustNorm * 0.32f) +
                (groundFactor * groundRollNorm * 0.08f) +
                (afterburnerBlend * 0.18f)) *
            (0.76f + (interior * 0.18f) + (exterior * 0.26f)) *
            masterDuck *
            masterVolume *
            engineVolume;
        const float ambienceGain =
            (0.01f +
                (airflow * 0.34f) +
                (buffetNorm * 0.16f) +
                (slipNorm * 0.12f) +
                (maneuverNorm * 0.08f) +
                (nearGroundFactor * 0.05f) +
                (waterProximity * 0.10f) +
                (foliageFactor * 0.06f) +
                (afterburnerBlend * 0.03f)) *
            (0.18f + (interior * 0.18f) + (exterior * 0.74f)) *
            masterDuck *
            masterVolume *
            ambienceVolume;
        const float enginePitch = clamp(
            (0.84f + afterburnerBlend * 0.06f) * clamp(propAudio.engineFrequencyScale, 0.35f, 2.0f),
            0.25f,
            2.2f);
        const float ambiencePitch = clamp(
            (0.84f + airflow * 0.18f + slipNorm * 0.05f + buffetNorm * 0.04f + waterProximity * 0.04f) *
                clamp(propAudio.ambienceFrequencyScale, 0.35f, 2.5f),
            0.3f,
            2.5f);
        const float combatGain =
            (0.01f +
                (gunshotPulse * 0.38f) +
                (terrainShotPulse * 0.26f) +
                (bombLatchPulse * 0.14f) +
                (explosionPulse * 0.68f) +
                (projectileWhistleAmount * 0.08f) +
                (bombWhistleAmount * 0.12f)) *
            masterDuck *
            masterVolume *
            (0.30f + (ambienceVolume * 0.48f) + (engineVolume * 0.14f));

        const int targetQueuedBytes = bufferSamples * queueDepth * static_cast<int>(sizeof(float));
        int queuedBytes = SDL_GetAudioStreamQueued(stream);
        while (queuedBytes >= 0 && queuedBytes < targetQueuedBytes) {
            if (pendingExplosionImpulse > 0.0f) {
                explosionDelaySec = std::max(
                    0.0f,
                    explosionDelaySec - (static_cast<float>(bufferSamples) / static_cast<float>(sampleRate)));
                if (explosionDelaySec <= 0.0f) {
                    explosionPulse = std::max(explosionPulse, pendingExplosionImpulse);
                    explosionDistanceNorm = pendingExplosionDistanceNorm;
                    pendingExplosionImpulse = 0.0f;
                }
            }
            for (int i = 0; i < bufferSamples; ++i) {
                const float engineSample = stepEngineSample(enginePitch, propAudio) * engineGain;
                const float ambienceSample = stepAmbienceSample(ambiencePitch, propAudio) * ambienceGain;
                const float combatSample = stepCombatSample() * combatGain;
                scratchBuffer[static_cast<std::size_t>(i)] = clamp(engineSample + ambienceSample + combatSample, -1.0f, 1.0f);
            }

            if (!SDL_PutAudioStreamData(stream, scratchBuffer.data(), bufferSamples * static_cast<int>(sizeof(float)))) {
                available = false;
                SDL_DestroyAudioStream(stream);
                stream = nullptr;
                scratchBuffer.clear();
                return;
            }
            queuedBytes = SDL_GetAudioStreamQueued(stream);
        }
    }

private:
    float nextNoiseSigned()
    {
        noiseSeed = (1664525u * noiseSeed) + 1013904223u;
        const float normalized = static_cast<float>(noiseSeed) / 4294967295.0f;
        return (normalized * 2.0f) - 1.0f;
    }

    float stepEngineSample(float enginePitch, const PropAudioConfig& propAudio)
    {
        const float crankRevHz = std::max(4.0f, crankRpmState / 60.0f);
        const float propRevHz = std::max(4.0f, propRpmState / 60.0f);
        const float firingHz = std::max(8.0f, (crankRpmState / 120.0f) * clamp(engineCylinderCountState, 1.0f, 18.0f));
        const float bladeCount = clamp(propAudio.propellerBladeCount, 2.0f, 6.0f);
        const float propellerDiameter = clamp(propAudio.propellerDiameterMeters, 0.5f, 5.0f);
        const float bladePassHz = propRevHz * bladeCount;
        const float engineDelta = ((2.0f * kPi * firingHz) / static_cast<float>(sampleRate)) * enginePitch;
        const float crankDelta = ((2.0f * kPi * crankRevHz) / static_cast<float>(sampleRate)) * enginePitch;
        const float bladePassDelta = ((2.0f * kPi * bladePassHz) / static_cast<float>(sampleRate)) * enginePitch;
        enginePhase1 = std::fmod(enginePhase1 + engineDelta, 2.0f * kPi);
        enginePhase2 = std::fmod(enginePhase2 + crankDelta, 2.0f * kPi);
        enginePhase3 = std::fmod(enginePhase3 + (engineDelta * 0.52f), 2.0f * kPi);
        propPhase = std::fmod(propPhase + bladePassDelta, 2.0f * kPi);

        const float tipSpeed =
            (kPi * propellerDiameter * propRevHz) +
            (speedNorm * referenceSpeedMetersPerSec * 0.35f);
        const float tipMach = tipSpeed / std::max(280.0f, speedOfSoundMetersPerSec);
        const float tipHarshness = clamp((tipMach - 0.55f) / 0.30f, 0.0f, 1.35f);
        const float exterior = exteriorViewBlend;
        const float interior = 1.0f - exterior;
        const float manifoldNorm = clamp(manifoldPressureKpaState / 115.0f, 0.0f, 1.45f);
        const float powerNorm = clamp(enginePowerKwState / std::max(10.0f, maxBrakePowerKwState), 0.0f, 1.5f);
        const float thermalStress = clamp(
            ((cylinderHeadTempKState - 380.0f) * 0.008f) +
                ((oilTempKState - 350.0f) * 0.006f) +
                ((exhaustGasTempKState - 780.0f) * 0.003f),
            0.0f,
            1.35f);

        const float combustionCore =
            (std::sin(enginePhase1) * 0.56f) +
            (std::sin((enginePhase1 * 2.0f) + 0.22f) * 0.18f) +
            (std::sin(enginePhase2 + 0.24f) * 0.20f);
        const float combustion = combustionCore - ((combustionCore * combustionCore * combustionCore) * 0.20f);
        const float growl =
            std::sin((enginePhase2 * 0.42f) + 0.6f) *
            (0.18f + (powerNorm * 0.10f) + (interior * 0.08f) + (afterburnerBlend * 0.06f));
        const float bladeChop =
            ((std::sin(propPhase) * 0.52f) +
                (std::sin((propPhase * 2.0f) + 0.17f) * 0.24f) +
                (std::sin((propPhase * 3.0f) + 0.84f) * 0.12f)) *
            (0.05f + (powerNorm * 0.10f) + (exterior * 0.10f) + (tipHarshness * 0.12f));
        const float compressor =
            std::sin((enginePhase2 * 0.71f) + (propPhase * 0.08f)) *
            (0.02f + (manifoldNorm * 0.03f) + (exterior * 0.03f) + (afterburnerBlend * 0.05f));
        const float structure =
            std::sin(enginePhase3 + 0.4f) *
            (0.05f + (maneuverNorm * 0.07f) + (interior * 0.05f) + (groundFactor * groundRollNorm * 0.03f) + (thermalStress * 0.04f));
        const float white = nextNoiseSigned();
        engineNoise +=
            (white - engineNoise) *
            (0.08f + (dynamicPressureNorm * 0.04f) + (slipNorm * 0.05f) + (maneuverNorm * 0.04f) + (tipHarshness * 0.03f) + (thermalStress * 0.03f));
        const float turbulence =
            engineNoise *
            (0.01f +
                (dynamicPressureNorm * 0.03f) +
                (buffetNorm * 0.08f) +
                (slipNorm * 0.06f) +
                (maneuverNorm * 0.06f) +
                (tipHarshness * 0.08f) +
                (thermalStress * 0.05f) +
                (afterburnerBlend * 0.10f)) *
            clamp(propAudio.engineNoiseAmount, 0.0f, 2.0f);
        const float tonalMix = clamp(propAudio.engineTonalMix, 0.0f, 2.0f);
        const float harmonicMix = clamp(propAudio.propHarmonicMix, 0.0f, 2.0f);
        const float amplitude =
            0.04f +
            (powerNorm * 0.18f) +
            (manifoldNorm * 0.08f) +
            (thrustNorm * 0.12f) +
            (groundFactor * groundRollNorm * 0.05f) +
            (afterburnerBlend * 0.12f);
        return
            ((combustion * 0.30f * tonalMix) +
                (growl * 0.32f) +
                (bladeChop * (0.65f + (exterior * 0.70f)) * harmonicMix) +
                (structure * 0.22f) +
                compressor +
                turbulence) *
            amplitude;
    }

    float stepAmbienceSample(float ambiencePitch, const PropAudioConfig& propAudio)
    {
        const float airflow = std::pow(clamp(dynamicPressureNorm * 0.55f, 0.0f, 1.8f), 1.55f);
        const float exterior = exteriorViewBlend;
        const float interior = 1.0f - exterior;
        const float windWhite = nextNoiseSigned();
        windNoise += (windWhite - windNoise) * (0.018f + (airflow * 0.08f) + (slipNorm * 0.04f));
        const float wind =
            windNoise *
            (0.01f + (airflow * 0.22f) + (slipNorm * 0.12f)) *
            (0.22f + (interior * 0.18f) + (exterior * 0.95f));
        const float buffetWhite = nextNoiseSigned();
        buffetNoise += (buffetWhite - buffetNoise) * (0.06f + (buffetNorm * 0.18f) + (maneuverNorm * 0.04f));
        const float buffet =
            buffetNoise *
            (0.01f + (buffetNorm * (0.08f + (airflow * 0.12f)))) *
            (0.30f + (exterior * 0.90f));
        const float slipWhite = nextNoiseSigned();
        slipNoise += (slipWhite - slipNoise) * (0.05f + (slipNorm * 0.16f) + (airflow * 0.03f));
        const float slipWash =
            slipNoise *
            (0.01f + (slipNorm * (0.06f + (airflow * 0.10f)))) *
            (0.26f + (exterior * 0.94f));
        waterPhase = std::fmod(
            waterPhase + ((0.018f + (waterProximity * (0.04f + (airflow * 0.04f)))) * ambiencePitch),
            2.0f * kPi);
        groundPhase = std::fmod(
            groundPhase + ((0.020f + (groundRollNorm * (0.10f + (nearGroundFactor * 0.05f)))) * ambiencePitch),
            2.0f * kPi);
        const float surf =
            (std::sin(waterPhase) * 0.6f + std::sin(waterPhase * 2.3f + 0.7f) * 0.4f) *
            (0.01f + (waterProximity * (0.04f + (airflow * 0.05f)))) *
            clamp(propAudio.waterAmbienceGain, 0.0f, 2.0f);
        const float reflection =
            std::sin(groundPhase) *
            (0.004f + (nearGroundFactor * (0.010f + (throttleNorm * 0.018f) + (speedNorm * 0.020f)))) *
            (0.30f + (exterior * 0.50f));
        const float rumble =
            std::sin((groundPhase * 0.53f) + 0.6f) *
            (0.01f + (groundFactor * groundRollNorm * 0.08f)) *
            clamp(propAudio.groundAmbienceGain, 0.0f, 2.0f);
        const float maneuverRattle =
            std::sin((groundPhase * 0.38f) + (waterPhase * 0.19f) + 0.5f) *
            (0.005f + (maneuverNorm * (0.04f + (nearGroundFactor * 0.03f))) + (groundFactor * groundRollNorm * 0.02f)) *
            (0.65f + (interior * 0.40f));
        const float foliageWhite = nextNoiseSigned();
        foliageNoise += (foliageWhite - foliageNoise) * (0.05f + (airflow * 0.03f) + (foliageFactor * 0.16f));
        const float foliage =
            foliageNoise *
            (0.01f + (foliageFactor * (0.06f + (airflow * 0.06f))) + (foliageImpactPulse * 0.09f));
        return wind + buffet + slipWash + surf + reflection + rumble + maneuverRattle + foliage;
    }

    float stepCombatSample()
    {
        const float white = nextNoiseSigned();
        combatNoise1 += (white - combatNoise1) * (0.28f + gunshotPulse * 0.18f + explosionPulse * 0.06f);
        combatNoise2 += (white - combatNoise2) * (0.08f + projectileWhistleAmount * 0.10f + bombWhistleAmount * 0.08f + explosionPulse * 0.05f);

        const float gunshotFrequency = 280.0f + (terrainShotPulse * 80.0f) + (gunshotPulse * 60.0f);
        gunshotPhase = std::fmod(gunshotPhase + ((2.0f * kPi * gunshotFrequency) / static_cast<float>(sampleRate)), 2.0f * kPi);
        const float latchFrequency = 420.0f + (bombLatchPulse * 160.0f);
        latchPhase = std::fmod(latchPhase + ((2.0f * kPi * latchFrequency) / static_cast<float>(sampleRate)), 2.0f * kPi);
        const float explosionFrequency = 28.0f + ((1.0f - explosionDistanceNorm) * 24.0f);
        explosionPhase1 = std::fmod(explosionPhase1 + ((2.0f * kPi * explosionFrequency) / static_cast<float>(sampleRate)), 2.0f * kPi);
        explosionPhase2 = std::fmod(explosionPhase2 + ((2.0f * kPi * (explosionFrequency * 1.7f)) / static_cast<float>(sampleRate)), 2.0f * kPi);

        const float projectileFrequency = clamp(
            (180.0f + (projectilePitchScale * 150.0f)) * projectileDoppler,
            90.0f,
            1200.0f);
        projectilePhase = std::fmod(projectilePhase + ((2.0f * kPi * projectileFrequency) / static_cast<float>(sampleRate)), 2.0f * kPi);
        const float bombFrequency = clamp(
            (90.0f + (bombPitchScale * 70.0f)) * bombDoppler,
            45.0f,
            520.0f);
        bombPhase = std::fmod(bombPhase + ((2.0f * kPi * bombFrequency) / static_cast<float>(sampleRate)), 2.0f * kPi);

        const float gunCrack =
            ((std::sin((gunshotPhase * 3.7f) + 0.2f) * 0.36f) + ((white - combatNoise1) * 0.64f)) *
            (gunshotPulse * (0.26f + (terrainShotPulse * 0.10f)));
        const float muzzleBang =
            ((std::sin((gunshotPhase * 0.18f) + 1.1f) * 0.76f) +
                (std::sin((gunshotPhase * 0.33f) + 0.2f) * 0.32f) +
                (combatNoise1 * 0.24f)) *
            (gunshotPulse * 0.24f);
        const float gunReport =
            ((std::sin((gunshotPhase * 0.42f) + 0.4f) * 0.82f) +
                (std::sin(gunshotPhase + 0.1f) * 0.18f) +
                (combatNoise1 * 0.22f)) *
            (gunshotPulse * (0.18f + (terrainShotPulse * 0.10f)));
        const float terrainThump =
            ((std::sin((gunshotPhase * 0.24f) + 1.2f) * 0.58f) + (combatNoise1 * 0.30f)) *
            (terrainShotPulse * 0.18f);
        const float latchClunk =
            ((std::sin(latchPhase) * 0.44f) + (std::sin((latchPhase * 0.52f) + 0.3f) * 0.56f) + (combatNoise2 * 0.12f)) *
            (bombLatchPulse * 0.16f);

        const float explosionNearness = 1.0f - explosionDistanceNorm;
        const float explosionBody =
            (std::sin(explosionPhase1) * 0.82f) +
            (std::sin(explosionPhase2 + 0.6f) * 0.36f) +
            (std::sin((explosionPhase1 * 0.47f) + 0.2f) * 0.52f) +
            (combatNoise1 * (0.52f + (explosionNearness * 0.34f))) +
            (combatNoise2 * (0.18f + (explosionNearness * 0.18f)));
        const float explosionSample =
            explosionBody *
            (explosionPulse * (0.34f + (explosionNearness * 0.28f)));

        const float projectileTone =
            ((std::sin(projectilePhase) * 0.62f) + (std::sin((projectilePhase * 1.23f) + 0.7f) * 0.24f) + (combatNoise2 * 0.12f)) *
            (projectileWhistleAmount * 0.08f);
        const float bombTone =
            ((std::sin(bombPhase) * 0.58f) + (std::sin((bombPhase * 0.52f) + 1.1f) * 0.34f) + (combatNoise2 * 0.16f)) *
            (bombWhistleAmount * 0.10f);

        return gunCrack + muzzleBang + gunReport + terrainThump + latchClunk + explosionSample + projectileTone + bombTone;
    }
};

}  // namespace NativeGame
