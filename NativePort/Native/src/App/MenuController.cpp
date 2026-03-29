#include "App/MenuController.hpp"

namespace TrueFlightApp {

void clearMenuConfirmation(PauseState& pauseState)
{
    pauseState.confirmPending = false;
    pauseState.confirmSelectedIndex = -1;
    pauseState.confirmUntil = 0.0f;
    pauseState.confirmText.clear();
}

void requestMenuConfirmation(PauseState& pauseState, int selectedIndex, std::string confirmText, float nowSeconds, float duration)
{
    pauseState.confirmPending = true;
    pauseState.confirmSelectedIndex = selectedIndex;
    pauseState.confirmUntil = nowSeconds + duration;
    pauseState.confirmText = std::move(confirmText);
}

void refreshMenuConfirmation(PauseState& pauseState, float nowSeconds)
{
    if (pauseState.confirmPending && nowSeconds >= pauseState.confirmUntil) {
        clearMenuConfirmation(pauseState);
    }
}

bool menuConfirmationMatches(const PauseState& pauseState, int selectedIndex, float nowSeconds)
{
    return pauseState.confirmPending &&
        pauseState.confirmSelectedIndex == selectedIndex &&
        nowSeconds <= pauseState.confirmUntil;
}

void refreshPauseStatus(PauseState& pauseState, float nowSeconds)
{
    if (pauseState.statusUntil > 0.0f && nowSeconds >= pauseState.statusUntil) {
        pauseState.statusText.clear();
        pauseState.statusUntil = 0.0f;
    }
}

void clearMenuPrompt(PauseState& pauseState)
{
    pauseState.promptActive = false;
    pauseState.promptMode = MenuPromptMode::None;
    pauseState.promptRole = CharacterSubTab::Plane;
    pauseState.promptText.clear();
    pauseState.promptCursor = 0;
}

void beginModelPathPrompt(PauseState& pauseState, CharacterSubTab role, std::string initialText)
{
    pauseState.promptActive = true;
    pauseState.promptMode = MenuPromptMode::ModelPath;
    pauseState.promptRole = role;
    pauseState.promptText = std::move(initialText);
    pauseState.promptCursor = static_cast<int>(pauseState.promptText.size());
}

void beginWorldNamePrompt(PauseState& pauseState, std::string initialText)
{
    pauseState.promptActive = true;
    pauseState.promptMode = MenuPromptMode::WorldName;
    pauseState.promptRole = CharacterSubTab::Plane;
    pauseState.promptText = std::move(initialText);
    pauseState.promptCursor = static_cast<int>(pauseState.promptText.size());
}

bool insertMenuPromptText(PauseState& pauseState, std::string_view text)
{
    if (!pauseState.promptActive || text.empty()) {
        return false;
    }

    const int insertAt = std::clamp(pauseState.promptCursor, 0, static_cast<int>(pauseState.promptText.size()));
    pauseState.promptText.insert(static_cast<std::size_t>(insertAt), text);
    pauseState.promptCursor = insertAt + static_cast<int>(text.size());
    return true;
}

bool eraseMenuPromptText(PauseState& pauseState, bool backspace)
{
    if (!pauseState.promptActive || pauseState.promptText.empty()) {
        return false;
    }

    if (backspace) {
        if (pauseState.promptCursor <= 0) {
            return false;
        }
        pauseState.promptText.erase(static_cast<std::size_t>(pauseState.promptCursor - 1), 1);
        pauseState.promptCursor = std::max(0, pauseState.promptCursor - 1);
        return true;
    }

    if (pauseState.promptCursor >= static_cast<int>(pauseState.promptText.size())) {
        return false;
    }
    pauseState.promptText.erase(static_cast<std::size_t>(pauseState.promptCursor), 1);
    return true;
}

void moveMenuPromptCursor(PauseState& pauseState, int delta)
{
    if (!pauseState.promptActive) {
        return;
    }
    pauseState.promptCursor = std::clamp(
        pauseState.promptCursor + delta,
        0,
        static_cast<int>(pauseState.promptText.size()));
}

}  // namespace TrueFlightApp
