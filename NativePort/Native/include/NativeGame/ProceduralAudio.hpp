#pragma once

#include <SDL3/SDL.h>

#include "NativeGame/Math.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace NativeGame {

struct PropAudioConfig {
    float baseRpm = 28.0f;
    float loadRpmContribution = 172.0f;
    float engineFrequencyScale = 1.0f;
    float engineTonalMix = 1.0f;
    float propHarmonicMix = 1.0f;
    float engineNoiseAmount = 1.0f;
    float ambienceFrequencyScale = 1.0f;
    float waterAmbienceGain = 1.0f;
    float groundAmbienceGain = 1.0f;
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
    bool afterburner = false;
    float dt = 1.0f / 60.0f;
    float masterVolume = 1.0f;
    float engineVolume = 1.0f;
    float ambienceVolume = 1.0f;
    float trueAirspeed = 0.0f;
    float dynamicPressure = 0.0f;
    float thrustNewton = 0.0f;
    float alphaRad = 0.0f;
    float betaRad = 0.0f;
    float verticalSpeed = 0.0f;
    float angularRateRad = 0.0f;
    float waterProximity = 0.0f;
    float foliageBrush = 0.0f;
    float foliageImpact = 0.0f;
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
    float foliageNoise = 0.0f;
    float engineLoad = 0.0f;
    float propLoad = 0.0f;
    float speedNorm = 0.0f;
    float alphaNorm = 0.0f;
    float slipNorm = 0.0f;
    float climbNorm = 0.0f;
    float angularRateNorm = 0.0f;
    float groundFactor = 0.0f;
    float waterProximity = 0.0f;
    float foliageFactor = 0.0f;
    float foliageImpactPulse = 0.0f;
    float afterburnerBlend = 0.0f;
    float pausedBlend = 0.0f;
    std::vector<float> scratchBuffer {};

    bool initialize(int preferredSampleRate, int preferredBufferSamples, int preferredQueueDepth, std::string* errorText = nullptr)
    {
        shutdown();

        sampleRate = std::max(8000, preferredSampleRate);
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
        engineLoad = mix(engineLoad, clamp(frame.thrustNewton / 3200.0f, 0.0f, 1.35f), blend);
        propLoad = mix(propLoad, clamp(frame.dynamicPressure / 6500.0f, 0.0f, 1.35f), blend);
        speedNorm = mix(speedNorm, clamp(frame.trueAirspeed / 110.0f, 0.0f, 1.35f), blend);
        alphaNorm = mix(alphaNorm, clamp(std::fabs(frame.alphaRad) / radians(18.0f), 0.0f, 1.0f), fastBlend);
        slipNorm = mix(slipNorm, clamp(std::fabs(frame.betaRad) / radians(16.0f), 0.0f, 1.0f), fastBlend);
        climbNorm = mix(climbNorm, clamp(std::fabs(frame.verticalSpeed) / 40.0f, 0.0f, 1.0f), blend);
        angularRateNorm = mix(angularRateNorm, clamp(frame.angularRateRad / radians(150.0f), 0.0f, 1.0f), fastBlend);
        groundFactor = mix(groundFactor, frame.onGround ? 1.0f : 0.0f, clamp(dt * 9.0f, 0.0f, 1.0f));
        waterProximity = mix(waterProximity, clamp(frame.waterProximity, 0.0f, 1.0f), clamp(dt * 2.4f, 0.0f, 1.0f));
        foliageFactor = mix(foliageFactor, clamp(frame.foliageBrush, 0.0f, 1.0f), clamp(dt * 9.5f, 0.0f, 1.0f));
        foliageImpactPulse = std::max(clamp(frame.foliageImpact, 0.0f, 1.0f), foliageImpactPulse * clamp(1.0f - (dt * 3.8f), 0.0f, 1.0f));
        afterburnerBlend = mix(afterburnerBlend, frame.afterburner ? 1.0f : 0.0f, fastBlend);
        pausedBlend = mix(pausedBlend, frame.paused ? 1.0f : 0.0f, clamp(dt * 7.5f, 0.0f, 1.0f));

        const float masterVolume = clamp(frame.masterVolume, 0.0f, 1.5f);
        const float engineVolume = clamp(frame.engineVolume, 0.0f, 1.5f);
        const float ambienceVolume = clamp(frame.ambienceVolume, 0.0f, 1.5f);
        const PropAudioConfig propAudio = frame.propAudioConfig;
        const float propulsion = clamp(engineLoad * 0.68f + propLoad * 0.22f + speedNorm * 0.10f, 0.0f, 1.4f);
        const float aeroStress = clamp(alphaNorm * 0.35f + slipNorm * 0.40f + angularRateNorm * 0.25f, 0.0f, 1.25f);
        const float masterDuck = clamp(1.0f - (pausedBlend * 0.84f), 0.03f, 1.0f);
        const float engineGain =
            (0.14f + propulsion * 0.78f + groundFactor * speedNorm * 0.06f + afterburnerBlend * 0.20f) *
            masterDuck *
            masterVolume *
            engineVolume;
        const float ambienceGain =
            (0.08f + speedNorm * 0.38f + aeroStress * 0.18f + climbNorm * 0.05f + waterProximity * 0.10f + afterburnerBlend * 0.05f) *
            masterDuck *
            masterVolume *
            ambienceVolume;
        const float enginePitch = clamp(
            (0.72f + propulsion * 0.74f + propLoad * 0.15f + groundFactor * 0.06f + afterburnerBlend * 0.16f) *
                clamp(propAudio.engineFrequencyScale, 0.35f, 2.5f),
            0.3f,
            2.5f);
        const float ambiencePitch = clamp(
            (0.76f + speedNorm * 0.58f + aeroStress * 0.18f + waterProximity * 0.05f) *
                clamp(propAudio.ambienceFrequencyScale, 0.35f, 2.5f),
            0.3f,
            2.5f);

        const int targetQueuedBytes = bufferSamples * queueDepth * static_cast<int>(sizeof(float));
        int queuedBytes = SDL_GetAudioStreamQueued(stream);
        while (queuedBytes >= 0 && queuedBytes < targetQueuedBytes) {
            for (int i = 0; i < bufferSamples; ++i) {
                const float engineSample = stepEngineSample(enginePitch, propAudio) * engineGain;
                const float ambienceSample = stepAmbienceSample(ambiencePitch, propAudio) * ambienceGain;
                scratchBuffer[static_cast<std::size_t>(i)] = clamp(engineSample + ambienceSample, -1.0f, 1.0f);
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
        const float loadContribution = clamp(propAudio.loadRpmContribution, 20.0f, 420.0f);
        const float rpm =
            clamp(propAudio.baseRpm, 6.0f, 220.0f) +
            (engineLoad * loadContribution) +
            (propLoad * loadContribution * 0.52f) +
            (speedNorm * loadContribution * 0.19f) +
            (groundFactor * loadContribution * 0.08f) +
            (afterburnerBlend * 96.0f);
        const float delta = ((2.0f * kPi * rpm) / static_cast<float>(sampleRate)) * enginePitch;
        enginePhase1 = std::fmod(enginePhase1 + delta, 2.0f * kPi);
        enginePhase2 = std::fmod(enginePhase2 + delta * 1.97f, 2.0f * kPi);
        enginePhase3 = std::fmod(enginePhase3 + delta * 0.49f, 2.0f * kPi);
        propPhase = std::fmod(propPhase + delta * (0.28f + propLoad * 0.04f) + speedNorm * 0.008f, 2.0f * kPi);

        const float tonal =
            std::sin(enginePhase1) +
            std::sin(enginePhase2 + 0.42f) * 0.43f +
            std::sin(enginePhase3 + 1.31f) * 0.24f;
        const float prop = std::sin(propPhase) * (0.05f + propLoad * 0.10f + groundFactor * 0.05f);
        const float harmonic = std::sin((propPhase * 0.5f) + enginePhase2) * (0.02f + propLoad * 0.03f);
        const float white = nextNoiseSigned();
        engineNoise += (white - engineNoise) * (0.12f + slipNorm * 0.05f + angularRateNorm * 0.04f + groundFactor * 0.03f);
        const float turbulence =
            engineNoise *
            (0.03f + speedNorm * 0.07f + alphaNorm * 0.08f + slipNorm * 0.10f + angularRateNorm * 0.08f + climbNorm * 0.04f + afterburnerBlend * 0.12f) *
            clamp(propAudio.engineNoiseAmount, 0.0f, 2.0f);
        const float tonalMix = clamp(propAudio.engineTonalMix, 0.0f, 2.0f);
        const float harmonicMix = clamp(propAudio.propHarmonicMix, 0.0f, 2.0f);
        const float amplitude = 0.06f + engineLoad * 0.28f + propLoad * 0.14f + groundFactor * 0.04f + afterburnerBlend * 0.10f;
        return ((tonal * 0.24f * tonalMix) + (harmonic * harmonicMix) + prop + turbulence) * amplitude;
    }

    float stepAmbienceSample(float ambiencePitch, const PropAudioConfig& propAudio)
    {
        const float windWhite = nextNoiseSigned();
        windNoise += (windWhite - windNoise) * (0.025f + speedNorm * 0.05f + slipNorm * 0.05f + angularRateNorm * 0.03f);
        const float wind =
            windNoise *
            (0.02f + speedNorm * 0.24f + slipNorm * 0.16f + angularRateNorm * 0.08f + climbNorm * 0.05f);
        waterPhase = std::fmod(
            waterPhase + ((0.02f + waterProximity * (0.05f + speedNorm * 0.04f)) * ambiencePitch),
            2.0f * kPi);
        groundPhase = std::fmod(
            groundPhase + ((0.035f + groundFactor * (0.08f + speedNorm * 0.05f)) * ambiencePitch),
            2.0f * kPi);
        const float surf =
            (std::sin(waterPhase) * 0.6f + std::sin(waterPhase * 2.3f + 0.7f) * 0.4f) *
            (0.01f + waterProximity * (0.05f + speedNorm * 0.04f)) *
            clamp(propAudio.waterAmbienceGain, 0.0f, 2.0f);
        const float rumble =
            std::sin(groundPhase) *
            (0.01f + groundFactor * speedNorm * 0.08f) *
            clamp(propAudio.groundAmbienceGain, 0.0f, 2.0f);
        const float foliageWhite = nextNoiseSigned();
        foliageNoise += (foliageWhite - foliageNoise) * (0.05f + speedNorm * 0.06f + foliageFactor * 0.16f);
        const float foliage =
            foliageNoise *
            (0.01f + foliageFactor * (0.06f + speedNorm * 0.05f) + foliageImpactPulse * 0.09f);
        return wind + surf + rumble + foliage;
    }
};

}  // namespace NativeGame
