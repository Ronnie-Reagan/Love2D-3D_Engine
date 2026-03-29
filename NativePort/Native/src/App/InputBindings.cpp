#include "App/InputBindings.hpp"

#include "App/Preferences.hpp"

namespace TrueFlightApp {

namespace {

SDL_Keymod normalizeBindingModifiers(SDL_Keymod modifiers)
{
    SDL_Keymod normalized = SDL_KMOD_NONE;
    if ((modifiers & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL | SDL_KMOD_CTRL)) != 0) {
        normalized = static_cast<SDL_Keymod>(normalized | SDL_KMOD_CTRL);
    }
    if ((modifiers & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT | SDL_KMOD_SHIFT)) != 0) {
        normalized = static_cast<SDL_Keymod>(normalized | SDL_KMOD_SHIFT);
    }
    if ((modifiers & (SDL_KMOD_LALT | SDL_KMOD_RALT | SDL_KMOD_ALT)) != 0) {
        normalized = static_cast<SDL_Keymod>(normalized | SDL_KMOD_ALT);
    }
    return normalized;
}

bool keyMatchesBinding(SDL_Scancode binding, SDL_Scancode input)
{
    return binding == input;
}

std::string modifierPrefix(SDL_Keymod modifiers)
{
    std::string text;
    if ((modifiers & SDL_KMOD_CTRL) != 0) {
        text += "ctrl+";
    }
    if ((modifiers & SDL_KMOD_ALT) != 0) {
        text += "alt+";
    }
    if ((modifiers & SDL_KMOD_SHIFT) != 0) {
        text += "shift+";
    }
    return text;
}

std::string scancodeLabel(SDL_Scancode scancode)
{
    if (scancode == SDL_SCANCODE_UNKNOWN) {
        return "Unbound";
    }
    const char* name = SDL_GetScancodeName(scancode);
    return (name != nullptr && name[0] != '\0') ? std::string(name) : "Unbound";
}

}  // namespace

bool bindingModifiersMatch(SDL_Keymod required, SDL_Keymod current)
{
    const SDL_Keymod normalizedRequired = normalizeBindingModifiers(required);
    const SDL_Keymod normalizedCurrent = normalizeBindingModifiers(current);
    return (normalizedCurrent & normalizedRequired) == normalizedRequired;
}

ControlProfile defaultControlProfile()
{
    auto key = [](SDL_Scancode scancode, SDL_Keymod modifiers = SDL_KMOD_NONE) {
        InputBinding binding;
        binding.kind = BindingKind::Key;
        binding.scancode = scancode;
        binding.modifiers = modifiers;
        return binding;
    };
    auto mouseButton = [](std::uint8_t button, SDL_Keymod modifiers = SDL_KMOD_NONE) {
        InputBinding binding;
        binding.kind = BindingKind::MouseButton;
        binding.mouseButton = button;
        binding.modifiers = modifiers;
        return binding;
    };
    auto mouseAxis = [](char axis, int direction, SDL_Keymod modifiers = SDL_KMOD_NONE) {
        InputBinding binding;
        binding.kind = BindingKind::MouseAxis;
        binding.axis = axis;
        binding.direction = direction;
        binding.modifiers = modifiers;
        return binding;
    };
    auto mouseWheel = [](int direction, SDL_Keymod modifiers = SDL_KMOD_NONE) {
        InputBinding binding;
        binding.kind = BindingKind::MouseWheel;
        binding.direction = direction;
        binding.modifiers = modifiers;
        return binding;
    };

    ControlProfile profile;
    profile.actions = {
        { InputActionId::FlightPitchDown, "Pitch Forward / Nose Down", "Pitch the aircraft nose downward.", true, true, { key(SDL_SCANCODE_W), mouseAxis('y', -1) } },
        { InputActionId::FlightPitchUp, "Pitch Back / Nose Up", "Pitch the aircraft nose upward.", true, true, { key(SDL_SCANCODE_S), mouseAxis('y', 1) } },
        { InputActionId::FlightRollLeft, "Roll Left", "Roll left around the forward axis.", true, true, { key(SDL_SCANCODE_A), mouseAxis('x', -1) } },
        { InputActionId::FlightRollRight, "Roll Right", "Roll right around the forward axis.", true, true, { key(SDL_SCANCODE_D), mouseAxis('x', 1) } },
        { InputActionId::FlightYawLeft, "Yaw Left", "Yaw left.", true, true, { key(SDL_SCANCODE_Q), {} } },
        { InputActionId::FlightYawRight, "Yaw Right", "Yaw right.", true, true, { key(SDL_SCANCODE_E), {} } },
        { InputActionId::FlightThrottleDown, "Throttle Down", "Reduce throttle.", true, true, { key(SDL_SCANCODE_DOWN), mouseWheel(-1) } },
        { InputActionId::FlightThrottleUp, "Throttle Up", "Increase throttle.", true, true, { key(SDL_SCANCODE_UP), mouseWheel(1) } },
        { InputActionId::FlightAirBrakes, "Air Brakes", "Hold to bleed airspeed.", true, true, { key(SDL_SCANCODE_SPACE), {} } },
        { InputActionId::FlightZoom, "Zoom", "Hold to zoom view.", true, true, { mouseButton(SDL_BUTTON_RIGHT), {} } },
        { InputActionId::FlightTrimDown, "Trim Nose Up", "Adjust manual trim nose up.", true, true, { mouseWheel(-1, SDL_KMOD_CTRL), {} } },
        { InputActionId::FlightTrimUp, "Trim Nose Down", "Adjust manual trim nose down.", true, true, { mouseWheel(1, SDL_KMOD_CTRL), {} } },
        { InputActionId::FlightRudderTrimLeft, "Rudder Trim Left", "Bias the rudder trim left without holding pedal input.", true, true, { key(SDL_SCANCODE_LEFTBRACKET), {} } },
        { InputActionId::FlightRudderTrimRight, "Rudder Trim Right", "Bias the rudder trim right without holding pedal input.", true, true, { key(SDL_SCANCODE_RIGHTBRACKET), {} } },
        { InputActionId::ToggleCamera, "Toggle Camera", "Switch chase and cockpit cameras.", true, true, { key(SDL_SCANCODE_C), {} } },
        { InputActionId::ToggleMap, "Toggle Map", "Toggle the minimap overlay.", true, true, { key(SDL_SCANCODE_M), {} } },
        { InputActionId::ToggleDebug, "Toggle Debug", "Toggle the debug overlay.", true, true, { key(SDL_SCANCODE_F3), {} } },
        { InputActionId::ResetFlight, "Reset Flight", "Reset the aircraft.", true, true, { key(SDL_SCANCODE_R), {} } },
        { InputActionId::PaintBrush, "Paint Mode", "Switch paint editor to brush mode.", true, true, { key(SDL_SCANCODE_1), {} } },
        { InputActionId::PaintErase, "Erase Mode", "Switch paint editor to erase mode.", true, true, { key(SDL_SCANCODE_2), {} } },
        { InputActionId::PaintFill, "Fill Paint", "Fill the active paint overlay.", true, true, { key(SDL_SCANCODE_F), {} } },
        { InputActionId::PaintUndo, "Undo Paint", "Undo the latest paint step.", true, true, { key(SDL_SCANCODE_Z, SDL_KMOD_CTRL), {} } },
        { InputActionId::PaintRedo, "Redo Paint", "Redo the latest undone paint step.", true, true, { key(SDL_SCANCODE_Y, SDL_KMOD_CTRL), {} } },
        { InputActionId::PaintCommit, "Commit Paint", "Commit the current paint overlay.", true, true, { key(SDL_SCANCODE_P), {} } },
        { InputActionId::WalkLookDown, "Look Down", "Move the walking camera downward.", true, true, { mouseAxis('y', 1), {} } },
        { InputActionId::WalkLookUp, "Look Up", "Move the walking camera upward.", true, true, { mouseAxis('y', -1), {} } },
        { InputActionId::WalkLookLeft, "Look Left", "Move the walking camera left.", true, true, { mouseAxis('x', -1), {} } },
        { InputActionId::WalkLookRight, "Look Right", "Move the walking camera right.", true, true, { mouseAxis('x', 1), {} } },
        { InputActionId::WalkSprint, "Sprint", "Increase walking speed while held.", true, true, { key(SDL_SCANCODE_LSHIFT), {} } },
        { InputActionId::WalkJump, "Jump", "Jump while grounded.", true, true, { key(SDL_SCANCODE_SPACE), {} } },
        { InputActionId::WalkForward, "Walk Forward", "Move forward in walking mode.", true, true, { key(SDL_SCANCODE_W), {} } },
        { InputActionId::WalkBackward, "Walk Backward", "Move backward in walking mode.", true, true, { key(SDL_SCANCODE_S), {} } },
        { InputActionId::WalkLeft, "Strafe Left", "Strafe left in walking mode.", true, true, { key(SDL_SCANCODE_A), {} } },
        { InputActionId::WalkRight, "Strafe Right", "Strafe right in walking mode.", true, true, { key(SDL_SCANCODE_D), {} } },
        { InputActionId::VoicePushToTalk, "Voice Push-To-Talk", "Hold to transmit Steam voice when radio voice is set to push-to-talk.", true, true, { key(SDL_SCANCODE_V), {} } }
    };
    return profile;
}

const ControlActionBinding* findControlAction(const ControlProfile& profile, InputActionId actionId)
{
    for (const ControlActionBinding& action : profile.actions) {
        if (action.id == actionId) {
            return &action;
        }
    }
    return nullptr;
}

ControlActionBinding* findControlAction(ControlProfile& profile, InputActionId actionId)
{
    for (ControlActionBinding& action : profile.actions) {
        if (action.id == actionId) {
            return &action;
        }
    }
    return nullptr;
}

const char* controlActionStorageKey(InputActionId actionId)
{
    switch (actionId) {
    case InputActionId::FlightPitchDown:
        return "flight_pitch_down";
    case InputActionId::FlightPitchUp:
        return "flight_pitch_up";
    case InputActionId::FlightRollLeft:
        return "flight_roll_left";
    case InputActionId::FlightRollRight:
        return "flight_roll_right";
    case InputActionId::FlightYawLeft:
        return "flight_yaw_left";
    case InputActionId::FlightYawRight:
        return "flight_yaw_right";
    case InputActionId::FlightThrottleDown:
        return "flight_throttle_down";
    case InputActionId::FlightThrottleUp:
        return "flight_throttle_up";
    case InputActionId::FlightAirBrakes:
        return "flight_air_brakes";
    case InputActionId::FlightZoom:
        return "flight_zoom";
    case InputActionId::FlightTrimDown:
        return "flight_trim_down";
    case InputActionId::FlightTrimUp:
        return "flight_trim_up";
    case InputActionId::FlightRudderTrimLeft:
        return "flight_rudder_trim_left";
    case InputActionId::FlightRudderTrimRight:
        return "flight_rudder_trim_right";
    case InputActionId::ToggleCamera:
        return "toggle_camera";
    case InputActionId::ToggleMap:
        return "toggle_map";
    case InputActionId::ToggleDebug:
        return "toggle_debug";
    case InputActionId::ResetFlight:
        return "reset_flight";
    case InputActionId::PaintBrush:
        return "paint_brush";
    case InputActionId::PaintErase:
        return "paint_erase";
    case InputActionId::PaintFill:
        return "paint_fill";
    case InputActionId::PaintUndo:
        return "paint_undo";
    case InputActionId::PaintRedo:
        return "paint_redo";
    case InputActionId::PaintCommit:
        return "paint_commit";
    case InputActionId::WalkLookDown:
        return "walk_look_down";
    case InputActionId::WalkLookUp:
        return "walk_look_up";
    case InputActionId::WalkLookLeft:
        return "walk_look_left";
    case InputActionId::WalkLookRight:
        return "walk_look_right";
    case InputActionId::WalkSprint:
        return "walk_sprint";
    case InputActionId::WalkJump:
        return "walk_jump";
    case InputActionId::WalkForward:
        return "walk_forward";
    case InputActionId::WalkBackward:
        return "walk_backward";
    case InputActionId::WalkLeft:
        return "walk_left";
    case InputActionId::WalkRight:
        return "walk_right";
    case InputActionId::VoicePushToTalk:
        return "voice_ptt";
    default:
        return "";
    }
}

std::optional<InputActionId> controlActionFromStorageKey(std::string_view key)
{
    for (int index = 0; index < static_cast<int>(InputActionId::Count); ++index) {
        const InputActionId actionId = static_cast<InputActionId>(index);
        if (key == controlActionStorageKey(actionId)) {
            return actionId;
        }
    }
    return std::nullopt;
}

bool controlActionSupported(InputActionId actionId)
{
    (void)actionId;
    return true;
}

bool controlActionConfigurable(InputActionId actionId)
{
    (void)actionId;
    return true;
}

bool isControlActionDown(const ControlProfile& profile, InputActionId actionId, const bool* keyboardState, SDL_Keymod modifiers)
{
    const ControlActionBinding* action = findControlAction(profile, actionId);
    if (action == nullptr || !action->supported || keyboardState == nullptr) {
        return false;
    }

    for (const InputBinding& binding : action->slots) {
        if (binding.kind == BindingKind::Key &&
            binding.scancode != SDL_SCANCODE_UNKNOWN &&
            keyboardState[static_cast<int>(binding.scancode)] != 0 &&
            bindingModifiersMatch(binding.modifiers, modifiers)) {
            return true;
        }
    }
    return false;
}

bool controlActionTriggeredByKey(const ControlProfile& profile, InputActionId actionId, SDL_Scancode scancode, SDL_Keymod modifiers)
{
    const ControlActionBinding* action = findControlAction(profile, actionId);
    if (action == nullptr || !action->supported) {
        return false;
    }

    for (const InputBinding& binding : action->slots) {
        if (binding.kind == BindingKind::Key &&
            binding.scancode != SDL_SCANCODE_UNKNOWN &&
            keyMatchesBinding(binding.scancode, scancode) &&
            bindingModifiersMatch(binding.modifiers, modifiers)) {
            return true;
        }
    }
    return false;
}

bool controlActionTriggeredByMouseButton(const ControlProfile& profile, InputActionId actionId, std::uint8_t button, SDL_Keymod modifiers)
{
    const ControlActionBinding* action = findControlAction(profile, actionId);
    if (action == nullptr || !action->supported) {
        return false;
    }

    for (const InputBinding& binding : action->slots) {
        if (binding.kind == BindingKind::MouseButton &&
            binding.mouseButton == button &&
            bindingModifiersMatch(binding.modifiers, modifiers)) {
            return true;
        }
    }
    return false;
}

bool controlActionTriggeredByWheel(const ControlProfile& profile, InputActionId actionId, int wheelY, SDL_Keymod modifiers)
{
    const ControlActionBinding* action = findControlAction(profile, actionId);
    if (action == nullptr || !action->supported || wheelY == 0) {
        return false;
    }

    for (const InputBinding& binding : action->slots) {
        if (binding.kind != BindingKind::MouseWheel || !bindingModifiersMatch(binding.modifiers, modifiers)) {
            continue;
        }
        if (binding.direction < 0 && wheelY < 0) {
            return true;
        }
        if (binding.direction > 0 && wheelY > 0) {
            return true;
        }
    }
    return false;
}

float controlMouseAxisValue(const ControlProfile& profile, InputActionId actionId, float dx, float dy, SDL_Keymod modifiers)
{
    const ControlActionBinding* action = findControlAction(profile, actionId);
    if (action == nullptr || !action->supported) {
        return 0.0f;
    }

    float total = 0.0f;
    for (const InputBinding& binding : action->slots) {
        if (binding.kind != BindingKind::MouseAxis || !bindingModifiersMatch(binding.modifiers, modifiers)) {
            continue;
        }
        const float component = binding.axis == 'x' ? dx : dy;
        if (binding.direction < 0 && component < 0.0f) {
            total += std::fabs(component);
        } else if (binding.direction > 0 && component > 0.0f) {
            total += std::fabs(component);
        }
    }
    return total;
}

std::string formatInputBinding(const InputBinding& binding)
{
    if (binding.kind == BindingKind::None) {
        return "Unbound";
    }

    const std::string prefix = modifierPrefix(binding.modifiers);
    switch (binding.kind) {
    case BindingKind::Key:
        return prefix + scancodeLabel(binding.scancode);
    case BindingKind::MouseButton:
        return prefix + std::string("Mouse ") + std::to_string(static_cast<int>(binding.mouseButton));
    case BindingKind::MouseAxis:
        if (binding.axis == 'x') {
            return prefix + (binding.direction < 0 ? "Mouse Left" : "Mouse Right");
        }
        return prefix + (binding.direction < 0 ? "Mouse Up" : "Mouse Down");
    case BindingKind::MouseWheel:
        return prefix + (binding.direction < 0 ? "Wheel Down" : "Wheel Up");
    default:
        return "Unbound";
    }
}

std::string serializeInputBinding(const InputBinding& binding)
{
    if (binding.kind == BindingKind::None) {
        return {};
    }

    std::string prefix = modifierPrefix(binding.modifiers);
    switch (binding.kind) {
    case BindingKind::Key:
        return prefix + "key:" + std::to_string(static_cast<int>(binding.scancode));
    case BindingKind::MouseButton:
        return prefix + "mouse_button:" + std::to_string(static_cast<int>(binding.mouseButton));
    case BindingKind::MouseAxis:
        return prefix + "mouse_axis:" + std::string(1, binding.axis) + ":" + std::to_string(binding.direction);
    case BindingKind::MouseWheel:
        return prefix + "mouse_wheel:" + std::to_string(binding.direction);
    default:
        return {};
    }
}

InputBinding parseInputBinding(const std::string& value)
{
    InputBinding binding;
    if (value.empty()) {
        return binding;
    }

    std::string remaining = toLowerAscii(value);
    auto consumePrefix = [&](const char* prefix, SDL_Keymod flag) {
        const std::string token(prefix);
        if (remaining.rfind(token, 0) == 0) {
            binding.modifiers = static_cast<SDL_Keymod>(binding.modifiers | flag);
            remaining = remaining.substr(token.size());
            return true;
        }
        return false;
    };
    bool consumedModifier = true;
    while (consumedModifier) {
        consumedModifier = consumePrefix("ctrl+", SDL_KMOD_CTRL) ||
            consumePrefix("alt+", SDL_KMOD_ALT) ||
            consumePrefix("shift+", SDL_KMOD_SHIFT);
    }

    if (remaining.rfind("key:", 0) == 0) {
        binding.kind = BindingKind::Key;
        binding.scancode = static_cast<SDL_Scancode>(parseIntValue(remaining.substr(4), static_cast<int>(SDL_SCANCODE_UNKNOWN)));
    } else if (remaining.rfind("mouse_button:", 0) == 0) {
        binding.kind = BindingKind::MouseButton;
        binding.mouseButton = static_cast<std::uint8_t>(parseIntValue(remaining.substr(13), 0));
    } else if (remaining.rfind("mouse_axis:", 0) == 0) {
        binding.kind = BindingKind::MouseAxis;
        const std::size_t split = remaining.find(':', 11);
        binding.axis = split == std::string::npos ? 'x' : remaining[11];
        binding.direction = split == std::string::npos ? 0 : parseIntValue(remaining.substr(split + 1), 0);
    } else if (remaining.rfind("mouse_wheel:", 0) == 0) {
        binding.kind = BindingKind::MouseWheel;
        binding.direction = parseIntValue(remaining.substr(12), 0);
    }
    return binding;
}

}  // namespace TrueFlightApp
