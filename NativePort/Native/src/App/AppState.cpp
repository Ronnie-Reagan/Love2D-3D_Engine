#include "App/AppState.hpp"

namespace TrueFlightApp {

UiState defaultUiState()
{
    UiState state {};
    state.chaseCamera = true;
    state.chaseCameraMode = ChaseCameraMode::Dynamic;
    state.showMap = true;
    state.showDebug = true;
    state.showCrosshair = true;
    state.showThrottleHud = true;
    state.showControlIndicator = true;
    state.showGeoInfo = true;
    state.invertLookY = false;
    state.mapNorthUp = false;
    state.mapHeld = false;
    state.mapUsedForZoom = false;
    state.zoomHeld = false;
    state.audioEnabled = true;
    state.scaleHudWithUi = false;
    state.mapZoomIndex = 2;
    state.cameraFovDegrees = 82.0f;
    state.uiScale = 1.0f;
    state.walkingMoveSpeed = kWalkingSpeedUnitsPerSecond;
    state.mouseSensitivity = 1.0f;
    state.masterVolume = 1.0f;
    state.engineVolume = 1.0f;
    state.ambienceVolume = 1.0f;
    state.combatVolume = 1.0f;
    state.flybyVolume = 1.0f;
    state.mapZoomExtents = { 200.0f, 400.0f, 800.0f, 1600.0f, 3200.0f};
    return state;
}

GraphicsSettings defaultGraphicsSettings()
{
    GraphicsSettings settings {};
    settings.windowMode = WindowMode::Windowed;
    settings.resolutionWidth = 1280;
    settings.resolutionHeight = 720;
    settings.renderScale = 1.0f;
    settings.drawDistance = 5000.0f;
    settings.horizonFog = true;
    settings.textureMipmaps = true;
    settings.vsync = false;
    return settings;
}

LightingSettings defaultLightingSettings()
{
    LightingSettings settings {};
    settings.showSunMarker = false;
    settings.sunYawDegrees = 20.0f;
    settings.sunPitchDegrees = 50.0f;
    settings.sunIntensity = 0.5f;
    settings.ambient = 0.06f;
    settings.markerDistance = 5000.0f;
    settings.markerSize = 180.0f;
    settings.shadowEnabled = true;
    settings.shadowSoftness = 1.6f;
    settings.shadowDistance = 1800.0f;
    settings.specularAmbient = 0.18f;
    settings.bounceStrength = 0.10f;
    settings.fogDensity = 0.00018f;
    settings.fogHeightFalloff = 0.0017f;
    settings.exposureEv = 0.0f;
    settings.turbidity = 2.4f;
    settings.sunTint = { 1.0f, 0.95f, 0.86f };
    settings.skyTint = { 1.0f, 1.0f, 1.0f };
    settings.groundTint = { 1.0f, 1.0f, 1.0f };
    settings.fogColor = { 0.64f, 0.73f, 0.84f };
    return settings;
}

HudSettings defaultHudSettings()
{
    HudSettings settings {};
    settings.showInfoPanel = true;
    settings.showSpeedometer = true;
    settings.showDebug = true;
    settings.showThrottle = true;
    settings.showControls = true;
    settings.showMap = true;
    settings.showGeoInfo = true;
    settings.showCrosshair = true;
    settings.showPeerIndicators = false;
    settings.speedometerMaxKph = 420;
    settings.speedometerMinorStepKph = 10;
    settings.speedometerMajorStepKph = 20;
    settings.speedometerLabelStepKph = 40;
    settings.speedometerRedlineKph = 320;
    return settings;
}

OnlineSettings defaultOnlineSettings()
{
    OnlineSettings settings {};
    settings.steamEnabled = TRUEFLIGHT_ENABLE_STEAMWORKS != 0;
    settings.multiplayerEnabled = false;
    settings.voiceEnabled = false;
    settings.voiceLoopback = false;
    settings.pushToTalk = true;
    settings.radioChannel = 1;
    settings.sessionMode = "offline";
    settings.callsign = "Pilot";
    settings.lastLobbyId.clear();
    settings.lastJoinHostId.clear();
    return settings;
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
