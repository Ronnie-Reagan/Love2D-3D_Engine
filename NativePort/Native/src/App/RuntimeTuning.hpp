#pragma once

namespace TrueFlightApp {

struct RuntimeTuning
{
    float primaryFireCooldownSec = 0.085f;
    float bombDropCooldownSec = 0.9f;
    float terrainGunCooldownSec = 0.12f;
    float gameplaySnapshotIntervalSec = 1.0f / 60.0f;
    float projectileLifetimeSec = 10.0f;
    float bombLifetimeSec = 60.0f;
    int enemyTargetCount = 6;
    int enemyEscortSquadCount = 2;
    int enemyEscortAircraftPerSquad = 3;
    float enemyEscortRespawnDelaySec = 12.0f;
    float enemyEscortPatrolAltitude = 135.0f;
    float planeHullMaxStrength = 100.0f;
    float planeFuselageMaxStrength = 100.0f;
    float planeWearMax = 100.0f;
};

constexpr RuntimeTuning defaultRuntimeTuning()
{
    return {};
}

}  // namespace TrueFlightApp
