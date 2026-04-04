#include "App/GamepadControls.hpp"

#include "App/AppTypes.hpp"

#include <algorithm>
#include <cmath>

namespace TrueFlightApp {

GamepadControlConfig defaultGamepadControlConfig()
{
    return {};
}

float normalizeGamepadStickAxis(Sint16 rawValue, float deadzone)
{
    const float normalized = clamp(static_cast<float>(rawValue) / 32767.0f, -1.0f, 1.0f);
    const float magnitude = std::fabs(normalized);
    if (magnitude <= deadzone) {
        return 0.0f;
    }

    const float scaled = (magnitude - deadzone) / std::max(0.001f, 1.0f - deadzone);
    return std::copysign(clamp(scaled, 0.0f, 1.0f), normalized);
}

float normalizeGamepadTriggerAxis(Sint16 rawValue, float deadzone)
{
    const float normalized = clamp(static_cast<float>(rawValue) / 32767.0f, 0.0f, 1.0f);
    if (normalized <= deadzone) {
        return 0.0f;
    }
    return clamp((normalized - deadzone) / std::max(0.001f, 1.0f - deadzone), 0.0f, 1.0f);
}

bool gamepadButtonDown(const GamepadState& gamepad, SDL_GamepadButton button)
{
    const std::size_t index = static_cast<std::size_t>(button);
    return index < gamepad.buttons.size() ? gamepad.buttons[index] : false;
}

bool gamepadButtonPressed(const GamepadState& gamepad, SDL_GamepadButton button)
{
    const std::size_t index = static_cast<std::size_t>(button);
    return index < gamepad.buttons.size() && gamepad.buttons[index] && !gamepad.previousButtons[index];
}

float gamepadAxisValue(const GamepadState& gamepad, SDL_GamepadAxis axis)
{
    const std::size_t index = static_cast<std::size_t>(axis);
    return index < gamepad.axes.size() ? gamepad.axes[index] : 0.0f;
}

bool gamepadAxisPressed(const GamepadState& gamepad, SDL_GamepadAxis axis, float threshold)
{
    const std::size_t index = static_cast<std::size_t>(axis);
    return index < gamepad.axes.size() &&
           gamepad.axes[index] >= threshold &&
           gamepad.previousAxes[index] < threshold;
}

void pollGamepadState(GamepadState& gamepad, const GamepadControlConfig& config)
{
    gamepad.previousButtons = gamepad.buttons;
    gamepad.previousAxes = gamepad.axes;
    std::fill(gamepad.buttons.begin(), gamepad.buttons.end(), false);
    std::fill(gamepad.axes.begin(), gamepad.axes.end(), 0.0f);

    if (gamepad.handle == nullptr) {
        return;
    }

    SDL_UpdateGamepads();
    for (int buttonIndex = 0; buttonIndex < SDL_GAMEPAD_BUTTON_COUNT; ++buttonIndex) {
        gamepad.buttons[static_cast<std::size_t>(buttonIndex)] =
            SDL_GetGamepadButton(gamepad.handle, static_cast<SDL_GamepadButton>(buttonIndex));
    }
    gamepad.axes[static_cast<std::size_t>(SDL_GAMEPAD_AXIS_LEFTX)] =
        normalizeGamepadStickAxis(SDL_GetGamepadAxis(gamepad.handle, SDL_GAMEPAD_AXIS_LEFTX), config.stickDeadzone);
    gamepad.axes[static_cast<std::size_t>(SDL_GAMEPAD_AXIS_LEFTY)] =
        normalizeGamepadStickAxis(SDL_GetGamepadAxis(gamepad.handle, SDL_GAMEPAD_AXIS_LEFTY), config.stickDeadzone);
    gamepad.axes[static_cast<std::size_t>(SDL_GAMEPAD_AXIS_RIGHTX)] =
        normalizeGamepadStickAxis(SDL_GetGamepadAxis(gamepad.handle, SDL_GAMEPAD_AXIS_RIGHTX), config.stickDeadzone);
    gamepad.axes[static_cast<std::size_t>(SDL_GAMEPAD_AXIS_RIGHTY)] =
        normalizeGamepadStickAxis(SDL_GetGamepadAxis(gamepad.handle, SDL_GAMEPAD_AXIS_RIGHTY), config.stickDeadzone);
    gamepad.axes[static_cast<std::size_t>(SDL_GAMEPAD_AXIS_LEFT_TRIGGER)] =
        normalizeGamepadTriggerAxis(SDL_GetGamepadAxis(gamepad.handle, SDL_GAMEPAD_AXIS_LEFT_TRIGGER), config.triggerDeadzone);
    gamepad.axes[static_cast<std::size_t>(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)] =
        normalizeGamepadTriggerAxis(SDL_GetGamepadAxis(gamepad.handle, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), config.triggerDeadzone);
}

int consumeRepeatedGamepadDirection(
    bool negativeHeld,
    bool positiveHeld,
    float nowSeconds,
    int& heldDirection,
    float& repeatAt,
    const GamepadControlConfig& config)
{
    const int direction = positiveHeld ? 1 : (negativeHeld ? -1 : 0);
    if (direction == 0) {
        heldDirection = 0;
        repeatAt = nowSeconds;
        return 0;
    }
    if (direction != heldDirection) {
        heldDirection = direction;
        repeatAt = nowSeconds + config.menuRepeatDelay;
        return direction;
    }
    if (nowSeconds >= repeatAt) {
        repeatAt = nowSeconds + config.menuRepeatInterval;
        return direction;
    }
    return 0;
}

void applyFlightGamepadLook(
    const GamepadControlConfig& config,
    const UiState& uiState,
    float& lookYaw,
    float& lookPitch,
    float lookX,
    float lookY,
    float dt)
{
    float pitchAxis = lookY;
    if (uiState.invertLookY) {
        pitchAxis = -pitchAxis;
    }

    if (std::fabs(lookX) <= 1.0e-4f && std::fabs(pitchAxis) <= 1.0e-4f) {
        const float returnAlpha = clamp(config.flightLookReturnRate * dt, 0.0f, 1.0f);
        lookYaw = mix(lookYaw, 0.0f, returnAlpha);
        lookPitch = mix(lookPitch, 0.0f, returnAlpha);
        return;
    }

    lookYaw = wrapAngle(lookYaw + (lookX * config.flightLookYawSpeed * dt));
    lookPitch = clamp(
        lookPitch + (pitchAxis * config.flightLookPitchSpeed * dt),
        -config.flightLookPitchLimitRadians,
        config.flightLookPitchLimitRadians);
}

bool applyWalkingGamepadLookAngles(
    const GamepadControlConfig& config,
    const UiState& uiState,
    float& walkYaw,
    float& walkPitch,
    float lookX,
    float lookY,
    float dt)
{
    float pitchAxis = lookY;
    if (uiState.invertLookY) {
        pitchAxis = -pitchAxis;
    }
    if (std::fabs(lookX) <= 1.0e-4f && std::fabs(pitchAxis) <= 1.0e-4f) {
        return false;
    }

    walkPitch = clamp(
        walkPitch + (pitchAxis * config.walkingLookPitchSpeed * dt),
        -config.walkingPitchLimitRadians,
        config.walkingPitchLimitRadians);
    walkYaw = wrapAngle(walkYaw + (lookX * config.walkingLookYawSpeed * dt));
    return true;
}

}  // namespace TrueFlightApp
