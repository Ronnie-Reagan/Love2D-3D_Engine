#pragma once

#include "App/AppState.hpp"

#include <string>

namespace TrueFlightApp {

bool bindingModifiersMatch(SDL_Keymod required, SDL_Keymod current);
ControlProfile defaultControlProfile();
const ControlActionBinding* findControlAction(const ControlProfile& profile, InputActionId actionId);
ControlActionBinding* findControlAction(ControlProfile& profile, InputActionId actionId);
const char* controlActionStorageKey(InputActionId actionId);
std::optional<InputActionId> controlActionFromStorageKey(std::string_view key);
bool isControlActionDown(const ControlProfile& profile, InputActionId actionId, const bool* keyboardState, SDL_Keymod modifiers);
bool controlActionTriggeredByKey(const ControlProfile& profile, InputActionId actionId, SDL_Scancode scancode, SDL_Keymod modifiers);
bool controlActionTriggeredByMouseButton(const ControlProfile& profile, InputActionId actionId, std::uint8_t button, SDL_Keymod modifiers);
bool controlActionTriggeredByWheel(const ControlProfile& profile, InputActionId actionId, int wheelY, SDL_Keymod modifiers);
float controlMouseAxisValue(const ControlProfile& profile, InputActionId actionId, float dx, float dy, SDL_Keymod modifiers);
std::string formatInputBinding(const InputBinding& binding);
std::string serializeInputBinding(const InputBinding& binding);
InputBinding parseInputBinding(const std::string& value);
bool controlActionSupported(InputActionId actionId);
bool controlActionConfigurable(InputActionId actionId);
bool clearSelectedControlBindingSlot(PauseState& pauseState, ControlProfile& controls);

}  // namespace TrueFlightApp
