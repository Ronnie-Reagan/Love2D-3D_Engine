#pragma once

#include "App/AppState.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace TrueFlightApp {

std::string sanitizeWorldInstanceName(std::string_view value);
std::vector<WorldInstanceSummary> scanWorldInstances();
void refreshWorldInstanceCatalog(BootResources& boot);
int selectedWorldInstanceIndex(const BootResources& boot);
const WorldInstanceSummary* selectedWorldInstance(const BootResources& boot);
void cycleSelectedWorldInstance(BootResources& boot, int direction);
std::string makeSuggestedWorldId(const BootResources& boot);
WorldInfoSnapshot buildWorldInfoSnapshot(const TerrainParams& terrainParams, std::string_view worldId, const Vec3& spawn = {});
bool createWorldInstance(BootResources& boot, const std::string& requestedName, std::string* errorText);
bool deleteWorldInstance(BootResources& boot, std::string_view worldId, std::string* errorText);

}  // namespace TrueFlightApp
