#pragma once

#include <SDL3/SDL.h>

namespace TrueFlightApp {

struct GamepadState;
struct UiState;

struct GamepadControlConfig
{
    float stickDeadzone = 0.25f;
    float triggerDeadzone = 0.06f;
    float menuStickDeadzone = 0.55f;
    float menuTriggerPressThreshold = 0.55f;
    float menuRepeatDelay = 0.32f;
    float menuRepeatInterval = 0.11f;
    float walkingPitchLimitRadians = 1.55334303f;
    float flightLookPitchLimitRadians = 1.39626340f;
    float flightLookYawSpeed = 2.2f;
    float flightLookPitchSpeed = 1.7f;
    float flightLookReturnRate = 3.8f;
    float walkingLookYawSpeed = 2.8f;
    float walkingLookPitchSpeed = 2.4f;
    float trimStepsPerSecond = 8.0f;
    float projectileCooldownSec = 0.2f;
};

GamepadControlConfig defaultGamepadControlConfig();

float normalizeGamepadStickAxis(Sint16 rawValue, float deadzone);
float normalizeGamepadTriggerAxis(Sint16 rawValue, float deadzone);
bool gamepadButtonDown(const GamepadState& gamepad, SDL_GamepadButton button);
bool gamepadButtonPressed(const GamepadState& gamepad, SDL_GamepadButton button);
float gamepadAxisValue(const GamepadState& gamepad, SDL_GamepadAxis axis);
bool gamepadAxisPressed(const GamepadState& gamepad, SDL_GamepadAxis axis, float threshold);
void pollGamepadState(GamepadState& gamepad, const GamepadControlConfig& config);
int consumeRepeatedGamepadDirection(
    bool negativeHeld,
    bool positiveHeld,
    float nowSeconds,
    int& heldDirection,
    float& repeatAt,
    const GamepadControlConfig& config);
void applyFlightGamepadLook(
    const GamepadControlConfig& config,
    const UiState& uiState,
    float& lookYaw,
    float& lookPitch,
    float lookX,
    float lookY,
    float dt);
bool applyWalkingGamepadLookAngles(
    const GamepadControlConfig& config,
    const UiState& uiState,
    float& walkYaw,
    float& walkPitch,
    float lookX,
    float lookY,
    float dt);

}  // namespace TrueFlightApp
