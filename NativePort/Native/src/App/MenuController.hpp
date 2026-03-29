#pragma once

#include "App/AppState.hpp"

#include <string>
#include <string_view>

namespace TrueFlightApp {

void clearMenuConfirmation(PauseState& pauseState);
void requestMenuConfirmation(PauseState& pauseState, int selectedIndex, std::string confirmText, float nowSeconds, float duration = 2.8f);
void refreshMenuConfirmation(PauseState& pauseState, float nowSeconds);
bool menuConfirmationMatches(const PauseState& pauseState, int selectedIndex, float nowSeconds);
void refreshPauseStatus(PauseState& pauseState, float nowSeconds);
void clearMenuPrompt(PauseState& pauseState);
void beginModelPathPrompt(PauseState& pauseState, CharacterSubTab role, std::string initialText = {});
void beginWorldNamePrompt(PauseState& pauseState, std::string initialText = {});
bool insertMenuPromptText(PauseState& pauseState, std::string_view text);
bool eraseMenuPromptText(PauseState& pauseState, bool backspace);
void moveMenuPromptCursor(PauseState& pauseState, int delta);

}  // namespace TrueFlightApp
