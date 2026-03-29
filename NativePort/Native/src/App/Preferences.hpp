#pragma once

#include "App/AppState.hpp"

#include <filesystem>
#include <string>

namespace TrueFlightApp {

std::filesystem::path getHudPreferenceFilePath();
std::filesystem::path getPaintStorageDirectory();
std::filesystem::path getWorldStorageDirectory();
std::filesystem::path getTerrainChunkCacheDirectory();

std::string toLowerAscii(std::string value);
std::string trimAscii(std::string value);
bool parseBoolValue(const std::string& value, bool fallback);
int parseIntValue(const std::string& value, int fallback);
float parseFloatValue(const std::string& value, float fallback);

bool savePreferences(
    const std::filesystem::path& path,
    const UiState& uiState,
    const GraphicsSettings& graphicsSettings,
    const LightingSettings& lightingSettings,
    const HudSettings& hudSettings,
    const OnlineSettings& onlineSettings,
    const ControlProfile& controls,
    const AircraftProfile& planeProfile,
    const TerrainParams& terrainParams,
    const PlaneVisualState& planeVisual,
    const PlaneVisualState& walkingVisual,
    std::string* errorText);
bool savePreferences(
    const std::filesystem::path& path,
    const UiState& uiState,
    const AircraftProfile& planeProfile,
    const TerrainParams& terrainParams,
    const PlaneVisualState& planeVisual,
    const PlaneVisualState& walkingVisual,
    std::string* errorText);
bool loadPreferences(
    const std::filesystem::path& path,
    UiState& uiState,
    GraphicsSettings& graphicsSettings,
    LightingSettings& lightingSettings,
    HudSettings& hudSettings,
    OnlineSettings& onlineSettings,
    ControlProfile& controls,
    AircraftProfile& planeProfile,
    TerrainParams& terrainParams,
    VisualPreferenceData& walkingPrefs,
    std::string* errorText);
bool loadPreferences(
    const std::filesystem::path& path,
    UiState& uiState,
    AircraftProfile& planeProfile,
    TerrainParams& terrainParams,
    VisualPreferenceData& walkingPrefs,
    std::string* errorText);
bool saveHudPreferences(const std::filesystem::path& path, const HudSettings& hudSettings, std::string* errorText);
bool loadHudPreferences(const std::filesystem::path& path, HudSettings& hudSettings, std::string* errorText);

}  // namespace TrueFlightApp
