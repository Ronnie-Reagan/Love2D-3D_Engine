#include "App/AppState.hpp"

namespace TrueFlightApp {

UiState defaultUiState()
{
    return {};
}

GraphicsSettings defaultGraphicsSettings()
{
    return {};
}

LightingSettings defaultLightingSettings()
{
    return {};
}

HudSettings defaultHudSettings()
{
    return {};
}

OnlineSettings defaultOnlineSettings()
{
    return {};
}

void syncUiStateFromHud(UiState& uiState, const HudSettings& hudSettings)
{
    uiState.showDebug = hudSettings.showDebug;
    uiState.showThrottleHud = hudSettings.showThrottle;
    uiState.showControlIndicator = hudSettings.showControls;
    uiState.showMap = hudSettings.showMap;
    uiState.showGeoInfo = hudSettings.showGeoInfo;
    uiState.showCrosshair = hudSettings.showCrosshair;
}

void syncHudFromUiState(HudSettings& hudSettings, const UiState& uiState)
{
    hudSettings.showDebug = uiState.showDebug;
    hudSettings.showThrottle = uiState.showThrottleHud;
    hudSettings.showControls = uiState.showControlIndicator;
    hudSettings.showMap = uiState.showMap;
    hudSettings.showGeoInfo = uiState.showGeoInfo;
    hudSettings.showCrosshair = uiState.showCrosshair;
}

void pushHudNotification(BootResources& boot, std::string text, float nowSeconds, float duration)
{
    if (text.empty()) {
        return;
    }
    boot.notifications.push_back({ std::move(text), nowSeconds + std::max(1.0f, duration) });
    while (boot.notifications.size() > 6u) {
        boot.notifications.pop_front();
    }
}

}  // namespace TrueFlightApp
