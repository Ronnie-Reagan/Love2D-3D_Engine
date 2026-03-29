#pragma once

#include "App/AppTypes.hpp"

namespace TrueFlightApp {

UiState defaultUiState();
GraphicsSettings defaultGraphicsSettings();
LightingSettings defaultLightingSettings();
HudSettings defaultHudSettings();
OnlineSettings defaultOnlineSettings();

void syncUiStateFromHud(UiState& uiState, const HudSettings& hudSettings);
void syncHudFromUiState(HudSettings& hudSettings, const UiState& uiState);
void pushHudNotification(BootResources& boot, std::string text, float nowSeconds, float duration);

}  // namespace TrueFlightApp
