#pragma once

#include "App/AppState.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace TrueFlightApp {

Quat composeVisualRotationOffset(const PlaneVisualState& visual);
VisualRigCutout defaultVisualRigCutout(int slotIndex);
bool autoSeedVisualRigCutout(PlaneVisualState& visual, int slotIndex);
bool autoSeedAllVisualRigCutouts(PlaneVisualState& visual);
bool mirrorVisualRigCutoutFromPairedSlot(PlaneVisualState& visual, int slotIndex);
void rebuildVisualRigModels(PlaneVisualState& visual);
bool visualUsesRigCutouts(const PlaneVisualState& visual);
float visualRigSlotAngleRadians(const PlaneVisualState& visual, int slotIndex, float worldTimeSeconds, float propRpm, float aileronNorm);
std::vector<AssetEntry> scanModelAssets();
void setBuiltinPlaneModel(PlaneVisualState& planeVisual);
void setBuiltinWalkingModel(PlaneVisualState& walkingVisual);
void applyVisualPreferenceData(PlaneVisualState& visual, const VisualPreferenceData& data);
bool loadPlaneModelFromPath(const std::filesystem::path& path, PlaneVisualState& planeVisual, std::string* statusText);
void restoreVisualFromPreferences(
    CharacterSubTab role,
    PlaneVisualState& visual,
    const VisualPreferenceData& prefs,
    const std::filesystem::path& fallbackPath,
    const char* logLabel);
void restoreVisualFromPreferences(
    PlaneVisualState& visual,
    const VisualPreferenceData& prefs,
    const std::filesystem::path& fallbackPath,
    const char* logLabel);
bool fillPaintOverlay(PlaneVisualState& visual, int presetIndex);
std::filesystem::path getPaintStoragePath(const std::string& paintHash);
bool loadPaintOverlayByHash(const std::string& paintHash, PlaneVisualState& visual, std::string* errorText);
bool commitPaintOverlay(const std::filesystem::path& paintDirectory, PlaneVisualState& visual, std::string* outPaintHash, std::string* errorText);

}  // namespace TrueFlightApp
