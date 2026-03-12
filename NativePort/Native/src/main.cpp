#include <SDL3/SDL.h>

#include "NativeGame/Flight.hpp"
#include "NativeGame/GltfLoader.hpp"
#include "NativeGame/HudCanvas.hpp"
#include "NativeGame/ProceduralAudio.hpp"
#include "NativeGame/RenderTypes.hpp"
#include "NativeGame/StlLoader.hpp"
#include "NativeGame/TerrainChunkBakeCache.hpp"
#include "NativeGame/VulkanRenderer.hpp"
#include "NativeGame/World.hpp"
#include "NativeGame/WorldStore.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <limits>
#include <map>
#include <vector>

namespace {

using namespace NativeGame;

constexpr float kFlightMouseSensitivity = 0.001f;
constexpr float kFlightZoomFactor = 0.6f;
const float kMinimumZoomFovRadians = radians(20.0f);
const float kWalkingPitchLimitRadians = radians(89.0f);
constexpr float kWalkingSpeedUnitsPerSecond = 10.0f;
constexpr float kWalkingSprintMultiplier = 1.8f;
constexpr float kWalkingJumpSpeed = 5.0f;
constexpr float kWalkingGravity = -9.80665f;
constexpr float kWalkingHalfHeight = 1.8f;
constexpr float kWalkingCollisionRadius = 0.55f;
constexpr float kGamepadStickDeadzone = 0.18f;
constexpr float kGamepadTriggerDeadzone = 0.06f;
constexpr float kGamepadMenuStickDeadzone = 0.55f;
constexpr float kGamepadMenuRepeatDelay = 0.32f;
constexpr float kGamepadMenuRepeatInterval = 0.11f;
const float kGamepadFlightLookPitchLimitRadians = radians(80.0f);
constexpr float kGamepadFlightLookYawSpeed = 2.2f;
constexpr float kGamepadFlightLookPitchSpeed = 1.7f;
constexpr float kGamepadFlightLookReturnRate = 3.8f;
constexpr float kGamepadWalkingLookYawSpeed = 2.8f;
constexpr float kGamepadWalkingLookPitchSpeed = 2.4f;
constexpr float kGamepadTrimStepsPerSecond = 8.0f;
constexpr float kGamepadProjectileCooldownSec = 0.2f;

enum class AppScreen {
    BootLoading = 0,
    MainMenu = 1,
    WorldLoading = 2,
    InFlight = 3
};

enum class MenuMode {
    MainMenu = 0,
    PauseOverlay = 1
};

enum class WindowMode {
    Windowed = 0,
    Borderless = 1,
    Fullscreen = 2
};

enum class PauseTab {
    Main = 0,
    Settings = 1,
    Characters = 2,
    Paint = 3,
    Hud = 4,
    Controls = 5,
    Help = 6
};

enum class SettingsSubTab {
    Graphics = 0,
    Camera = 1,
    Sound = 2,
    Flight = 3,
    Terrain = 4,
    Lighting = 5,
    Online = 6
};

enum class CharacterSubTab {
    Plane = 0,
    Player = 1
};

enum class PaintMode {
    Brush = 0,
    Erase = 1
};

enum class MenuPromptMode {
    None = 0,
    ModelPath = 1
};

enum class BindingKind {
    None = 0,
    Key = 1,
    MouseButton = 2,
    MouseAxis = 3,
    MouseWheel = 4
};

enum class InputActionId {
    FlightPitchDown = 0,
    FlightPitchUp = 1,
    FlightRollLeft = 2,
    FlightRollRight = 3,
    FlightYawLeft = 4,
    FlightYawRight = 5,
    FlightThrottleDown = 6,
    FlightThrottleUp = 7,
    FlightAirBrakes = 8,
    FlightZoom = 9,
    FlightTrimDown = 10,
    FlightTrimUp = 11,
    ToggleCamera = 12,
    ToggleMap = 13,
    ToggleDebug = 14,
    ResetFlight = 15,
    PaintBrush = 16,
    PaintErase = 17,
    PaintFill = 18,
    PaintUndo = 19,
    PaintRedo = 20,
    PaintCommit = 21,
    WalkLookDown = 22,
    WalkLookUp = 23,
    WalkLookLeft = 24,
    WalkLookRight = 25,
    WalkSprint = 26,
    WalkJump = 27,
    WalkForward = 28,
    WalkBackward = 29,
    WalkLeft = 30,
    WalkRight = 31,
    VoicePushToTalk = 32,
    Count = 33
};

struct GraphicsSettings {
    WindowMode windowMode = WindowMode::Windowed;
    int resolutionWidth = 1280;
    int resolutionHeight = 720;
    float renderScale = 1.0f;
    float drawDistance = 5000.0f;
    bool horizonFog = true;
    bool textureMipmaps = true;
    bool vsync = false;
};

struct LightingSettings {
    bool showSunMarker = false;
    float sunYawDegrees = 20.0f;
    float sunPitchDegrees = 50.0f;
    float sunIntensity = 1.3f;
    float ambient = 0.16f;
    float markerDistance = 1400.0f;
    float markerSize = 180.0f;
    bool shadowEnabled = true;
    float shadowSoftness = 1.6f;
    float shadowDistance = 1800.0f;
    float specularAmbient = 0.18f;
    float bounceStrength = 0.10f;
    float fogDensity = 0.00018f;
    float fogHeightFalloff = 0.0017f;
    float exposureEv = 0.0f;
    float turbidity = 2.4f;
    Vec3 sunTint { 1.0f, 0.95f, 0.86f };
    Vec3 skyTint { 1.0f, 1.0f, 1.0f };
    Vec3 groundTint { 1.0f, 1.0f, 1.0f };
    Vec3 fogColor { 0.64f, 0.73f, 0.84f };
};

struct HudSettings {
    bool showSpeedometer = true;
    bool showDebug = true;
    bool showThrottle = true;
    bool showControls = true;
    bool showMap = true;
    bool showGeoInfo = true;
    bool showPeerIndicators = false;
    int speedometerMaxKph = 1000;
    int speedometerMinorStepKph = 20;
    int speedometerMajorStepKph = 100;
    int speedometerLabelStepKph = 200;
    int speedometerRedlineKph = 850;
};

struct LoadingUiState {
    bool active = false;
    std::string stage = "Booting";
    std::string detail;
    float progress = 0.0f;
    float startedAt = 0.0f;
    float completedAt = 0.0f;
    int currentEntry = 0;
    std::vector<std::string> entries;
};

struct InputBinding {
    BindingKind kind = BindingKind::None;
    SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN;
    std::uint8_t mouseButton = 0;
    char axis = 'x';
    int direction = 0;
    SDL_Keymod modifiers = SDL_KMOD_NONE;
};

struct ControlActionBinding {
    InputActionId id = InputActionId::FlightPitchDown;
    const char* label = "";
    const char* help = "";
    bool configurable = true;
    bool supported = true;
    std::array<InputBinding, 2> slots {};
};

struct ControlProfile {
    std::vector<ControlActionBinding> actions;
};

struct UiState {
    bool chaseCamera = true;
    bool showMap = true;
    bool showDebug = true;
    bool showCrosshair = true;
    bool showThrottleHud = true;
    bool showControlIndicator = true;
    bool showGeoInfo = true;
    bool invertLookY = false;
    bool mapNorthUp = false;
    bool mapHeld = false;
    bool mapUsedForZoom = false;
    bool zoomHeld = false;
    bool audioEnabled = true;
    int mapZoomIndex = 2;
    float cameraFovDegrees = 82.0f;
    float walkingMoveSpeed = kWalkingSpeedUnitsPerSecond;
    float mouseSensitivity = 1.0f;
    float masterVolume = 1.0f;
    float engineVolume = 1.0f;
    float ambienceVolume = 1.0f;
    std::array<float, 5> mapZoomExtents { 200.0f, 400.0f, 800.0f, 1600.0f, 3200.0f };
};

struct PauseState {
    bool active = false;
    MenuMode mode = MenuMode::PauseOverlay;
    PauseTab tab = PauseTab::Main;
    int selectedIndex = 0;
    SettingsSubTab settingsSubTab = SettingsSubTab::Graphics;
    CharacterSubTab charactersSubTab = CharacterSubTab::Plane;
    CharacterSubTab paintSubTab = CharacterSubTab::Plane;
    int controlsSelection = 0;
    int controlsSlot = 0;
    bool controlsCapturing = false;
    int controlsCaptureActionIndex = -1;
    int controlsCaptureSlot = -1;
    bool sessionFlightMode = true;
    bool promptActive = false;
    MenuPromptMode promptMode = MenuPromptMode::None;
    CharacterSubTab promptRole = CharacterSubTab::Plane;
    std::string promptText;
    int promptCursor = 0;
    bool confirmPending = false;
    int confirmSelectedIndex = -1;
    float confirmUntil = 0.0f;
    std::string confirmText;
    int helpScroll = 0;
    bool rowDragActive = false;
    float rowDragLastX = 0.0f;
    std::string statusText;
    float statusUntil = 0.0f;
};

struct RectF {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct PauseLayout {
    float panelX = 0.0f;
    float panelY = 0.0f;
    float panelW = 0.0f;
    float panelH = 0.0f;
    float tabY = 0.0f;
    float tabW = 0.0f;
    float tabH = 0.0f;
    float subTabY = 0.0f;
    float contentX = 0.0f;
    float contentY = 0.0f;
    float previewX = 0.0f;
    float previewY = 0.0f;
    float previewW = 0.0f;
    float previewH = 0.0f;
};

struct AssetEntry {
    std::filesystem::path path;
    std::string label;
    bool supported = false;
};

struct PlaneVisualState {
    Model sourceModel = makeCubeModel();
    Model model = makeCubeModel();
    std::string label = "builtin cube";
    std::filesystem::path sourcePath {};
    bool usesStl = false;
    Quat importRotationOffset = quatIdentity();
    float forwardAxisYawDegrees = -90.0f;
    float defaultScale = 3.0f;
    float scale = 3.0f;
    float previewZoom = 1.0f;
    bool previewAutoSpin = true;
    float yawDegrees = 0.0f;
    float pitchDegrees = 0.0f;
    float rollDegrees = 0.0f;
    Vec3 modelOffset {};
    std::string paintTargetKey;
    std::string paintHash;
    std::map<std::string, std::string> paintHashesByModelKey;
    RgbaImage paintOverlay {};
    bool hasCommittedPaint = false;
    bool paintSupported = false;
    std::vector<RgbaImage> paintUndoStack;
    std::vector<RgbaImage> paintRedoStack;
};

struct PaintUiState {
    PaintMode mode = PaintMode::Brush;
    int colorIndex = 0;
    int brushSize = 28;
    float brushOpacity = 1.0f;
    float brushHardness = 0.75f;
    bool draggingCanvas = false;
    RectF canvasRect {};
};

enum class TerrainFarTileBand {
    Near = 0,
    Mid = 1,
    Horizon = 2
};

enum class TerrainFarTileDetail {
    Lod0 = 0,
    Lod1 = 1,
    Lod2 = 2
};

struct TerrainFarTile {
    TerrainFarTileBand band = TerrainFarTileBand::Horizon;
    TerrainFarTileDetail detail = TerrainFarTileDetail::Lod2;
    int tileX = 0;
    int tileZ = 0;
    std::uint64_t paramsSignature = 0u;
    std::uint64_t sourceSignature = 0u;
    bool active = false;
    Model terrainModel {};
    Model waterModel {};
    Model propModel {};
    std::vector<TerrainPropCollider> propColliders;
};

struct TerrainVisualCache {
    bool valid = false;
    float nearAnchorX = 0.0f;
    float nearAnchorZ = 0.0f;
    float farAnchorX = 0.0f;
    float farAnchorZ = 0.0f;
    float nearHalfExtent = 0.0f;
    float midHalfExtent = 0.0f;
    float farHalfExtent = 0.0f;
    float lod0TileSize = 0.0f;
    float lod1TileSize = 0.0f;
    float lod2TileSize = 0.0f;
    TerrainParams paramsSnapshot = defaultTerrainParams();
    std::vector<TerrainFarTile> nearTiles;
    std::vector<TerrainFarTile> farTiles;
};

struct VisualPreferenceData {
    bool hasStoredPath = false;
    std::filesystem::path sourcePath {};
    float scale = 3.0f;
    float previewZoom = 1.0f;
    bool previewAutoSpin = true;
    float forwardAxisYawDegrees = -90.0f;
    float yawDegrees = 0.0f;
    float pitchDegrees = 0.0f;
    float rollDegrees = 0.0f;
    Vec3 modelOffset {};
    std::string paintHash;
    std::map<std::string, std::string> paintHashesByModelKey;
};

struct AircraftProfile {
    std::string id = "prop_plane";
    FlightConfig flightConfig = defaultFlightConfig();
    PropAudioConfig propAudioConfig = defaultPropAudioConfig();
    VisualPreferenceData visualPrefs {};
};

struct BootResources {
    GeoConfig geoConfig {};
    UiState uiState {};
    GraphicsSettings graphics {};
    LightingSettings lighting {};
    HudSettings hud {};
    TerrainParams terrainParams = defaultTerrainParams();
    TerrainParams defaultTerrainParamsValues = defaultTerrainParams();
    AircraftProfile planeProfile {};
    VisualPreferenceData walkingPrefs {};
    PlaneVisualState planeVisual {};
    PlaneVisualState walkingVisual {};
    PaintUiState paintUi {};
    LoadingUiState loadingUi {};
    ControlProfile controls {};
    std::vector<AssetEntry> assetCatalog;
    HudCanvas hudCanvas { 1280, 720 };
    std::filesystem::path preferencesPath {};
    bool preferencesDirty = false;
    float preferencesNextSaveAt = 0.0f;
};

struct GamepadState {
    SDL_Gamepad* handle = nullptr;
    SDL_JoystickID instanceId = 0;
    std::string name;
    std::array<bool, static_cast<std::size_t>(SDL_GAMEPAD_BUTTON_COUNT)> buttons {};
    std::array<bool, static_cast<std::size_t>(SDL_GAMEPAD_BUTTON_COUNT)> previousButtons {};
    std::array<float, static_cast<std::size_t>(SDL_GAMEPAD_AXIS_COUNT)> axes {};
    int verticalRepeatDirection = 0;
    float verticalRepeatAt = 0.0f;
    int horizontalRepeatDirection = 0;
    float horizontalRepeatAt = 0.0f;
    float trimAccumulator = 0.0f;
    bool rudderLatched = false;
    float lastProjectileTriggerAt = -1000.0f;
};

struct GameSession {
    std::optional<WorldStore> worldStore;
    std::string terrainWorldId = "native_default";
    std::optional<TerrainChunkBakeCache> terrainChunkBakeCache;
    TerrainVisualCache terrainCache {};
    TerrainFieldContext terrainContext {};
    std::mt19937 worldRng {};
    WindState windState {};
    CloudField cloudField {};
    FlightState plane {};
    FlightRuntimeState runtime {};
    ProceduralAudioState audioState {};
    float worldTime = 0.0f;
    bool flightMode = true;
    float walkYaw = 0.0f;
    float walkPitch = 0.0f;
    float flightLookYaw = 0.0f;
    float flightLookPitch = 0.0f;
    float foliageBrushAmount = 0.0f;
    float foliageImpactPulse = 0.0f;
};

struct WalkingInputState {
    bool forward = false;
    bool backward = false;
    bool left = false;
    bool right = false;
    bool sprint = false;
    bool jump = false;
    float forwardAxis = 0.0f;
    float rightAxis = 0.0f;
};

constexpr int kTerrainSettingCount = 38;
constexpr int kTerrainVisibleRows = 11;
constexpr int kGraphicsSettingCount = 9;
constexpr int kCameraSettingCount = 7;
constexpr int kSoundSettingCount = 13;
constexpr int kLightingSettingCount = 28;
constexpr int kOnlineSettingCount = 5;
constexpr int kHudSettingCount = 12;
constexpr int kControlsVisibleRows = 10;
constexpr int kSettingsVisibleRows = 13;
constexpr int kCharacterAssetListStart = 13;
constexpr int kPaintSettingCount = 11;
constexpr std::array<const char*, 7> kPauseTabs { "Home", "Settings", "Characters", "Paint", "HUD", "Controls", "Help" };

struct HelpLine {
    std::string_view text;
    bool title = false;
};

constexpr float kHelpLineHeight = 16.0f;
constexpr std::array<HelpLine, 26> kMenuHelpLines {
    HelpLine { "Flight Mode", true },
    HelpLine { "W / S or Left Stick Y: Pitch the aircraft nose down or up." },
    HelpLine { "A / D or Left Stick X: Roll left or right." },
    HelpLine { "Q / E or D-Pad Left/Right: Yaw and rudder adjustments." },
    HelpLine { "Up / Down, wheel, or RT/LT: Increase or decrease throttle." },
    HelpLine { "Ctrl + Wheel or D-Pad Up/Down: Adjust trim without also touching throttle." },
    HelpLine { "Space / B: Hold the air brakes. A: afterburner. RB: projectile trigger log." },
    HelpLine { "" },
    HelpLine { "Walking Mode", true },
    HelpLine { "W A S D or Left Stick: Move and strafe when on foot." },
    HelpLine { "Mouse X / Y or Right Stick: FPS look with clamped pitch." },
    HelpLine { "Shift: Sprint while held." },
    HelpLine { "Space or A: Jump while grounded." },
    HelpLine { "" },
    HelpLine { "Cameras and Map", true },
    HelpLine { "C: Toggle between cockpit and chase cameras." },
    HelpLine { "M: Toggle the mini-map overlay." },
    HelpLine { "Hold M with Up/Down or wheel: Change map zoom without toggling it." },
    HelpLine { "Left Alt: Release mouse capture temporarily without pausing." },
    HelpLine { "" },
    HelpLine { "Menu and Paint", true },
    HelpLine { "Tab / H: Cycle top pages. [ / ] or Q / E: change sub-pages." },
    HelpLine { "Wheel: Adjust ranges, drag rows horizontally, or scroll this Help page." },
    HelpLine { "Characters: Load From Path, choose scanned assets, or drag and drop files." },
    HelpLine { "Paint: Drag on the canvas, use 1 / 2 for brush modes, PgUp/PgDn for brush size." },
    HelpLine { "Controls: Enter to capture, Backspace/Delete to clear a slot, R to restore defaults." }
};

enum class MenuCommand {
    None = 0,
    StartFlight = 1,
    ReturnToMainMenu = 2,
    Quit = 3,
    ToggleFlightMode = 4
};

GraphicsSettings defaultGraphicsSettings();
LightingSettings defaultLightingSettings();
HudSettings defaultHudSettings();
ControlProfile defaultControlProfile();
void syncUiStateFromHud(UiState& uiState, const HudSettings& hudSettings);
void syncHudFromUiState(HudSettings& hudSettings, const UiState& uiState);
std::string toLowerAscii(std::string value);
int parseIntValue(const std::string& value, int fallback);
const ControlActionBinding* findControlAction(const ControlProfile& profile, InputActionId actionId);
ControlActionBinding* findControlAction(ControlProfile& profile, InputActionId actionId);
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

int pauseItemCount(const PauseState& pauseState, std::size_t assetCount);
int terrainVisibleStartIndex(int selectedIndex);
void activatePauseSelection(
    PauseState& pauseState,
    UiState& uiState,
    FlightState& plane,
    FlightRuntimeState& runtime,
    const TerrainFieldContext& terrainContext,
    bool& running);
void adjustSettingsValue(UiState& uiState, int settingIndex, int direction);
void resetSettingsValue(UiState& uiState, const UiState& defaults, int settingIndex);
void adjustTuningValue(FlightConfig& config, int tuningIndex, int direction);
void resetTuningValue(FlightConfig& config, const FlightConfig& defaults, int tuningIndex);
void adjustTerrainValue(TerrainParams& terrainParams, int terrainIndex, int direction);
void resetTerrainValue(TerrainParams& terrainParams, const TerrainParams& defaults, int terrainIndex);
void adjustHudValue(UiState& uiState, int hudIndex, int direction);
void resetHudValue(UiState& uiState, const UiState& defaults, int hudIndex);
int settingsLocalToLegacyIndex(SettingsSubTab subTab, int localIndex);
const char* settingsRowLabel(SettingsSubTab subTab, int localIndex);
std::string formatSettingsRowValue(SettingsSubTab subTab, int localIndex, const UiState& uiState, const PropAudioConfig& propAudioConfig);
const char* settingsRowHelpText(SettingsSubTab subTab, int localIndex);
void adjustSettingsRowValue(UiState& uiState, PropAudioConfig& propAudioConfig, SettingsSubTab subTab, int localIndex, int direction);
void resetSettingsRowValue(UiState& uiState, const UiState& defaults, PropAudioConfig& propAudioConfig, const PropAudioConfig& defaultPropAudioConfigValues, SettingsSubTab subTab, int localIndex);
const char* characterRowLabel(int rowIndex);
std::string formatCharacterRowValue(int rowIndex, const PlaneVisualState& visual);
const char* characterRowHelpText(int rowIndex);
void adjustCharacterRowValue(PlaneVisualState& visual, int rowIndex, int direction);
const char* paintRowLabel(int rowIndex);
std::string formatPaintRowValue(int rowIndex, const PaintUiState& paintUi, const PlaneVisualState& visual);
const char* paintRowHelpText(int rowIndex, const PlaneVisualState& visual);
void adjustPaintRowValue(PaintUiState& paintUi, int rowIndex, int direction);
void bindTerrainContextWorldStore(TerrainFieldContext& terrainContext, WorldStore* worldStore);
Quat composeWalkingRotation(float yaw, float pitch);
void syncWalkingLookFromRotation(const Quat& rotation, float& walkYaw, float& walkPitch);
void clearMenuConfirmation(PauseState& pauseState);
void requestMenuConfirmation(PauseState& pauseState, int selectedIndex, std::string confirmText, float nowSeconds, float duration = 2.8f);
void refreshMenuConfirmation(PauseState& pauseState, float nowSeconds);
bool menuConfirmationMatches(const PauseState& pauseState, int selectedIndex, float nowSeconds);
void clearMenuPrompt(PauseState& pauseState);
void beginModelPathPrompt(PauseState& pauseState, CharacterSubTab role, std::string initialText = {});
bool insertMenuPromptText(PauseState& pauseState, std::string_view text);
bool eraseMenuPromptText(PauseState& pauseState, bool backspace);
void moveMenuPromptCursor(PauseState& pauseState, int delta);
bool clearSelectedControlBindingSlot(PauseState& pauseState, ControlProfile& controls);
int maxMenuHelpScroll(const PauseLayout& layout);
void applyWalkingMouseInput(
    UiState& uiState,
    const ControlProfile& controls,
    FlightState& actor,
    float& walkYaw,
    float& walkPitch,
    float dx,
    float dy,
    SDL_Keymod modifiers);
void stepWalking(
    FlightState& actor,
    float dt,
    const WalkingInputState& input,
    const TerrainFieldContext& terrainContext,
    float baseMoveSpeed,
    const TerrainVisualCache* terrainCache,
    float* brushAmountOut);
TerrainCrater buildCrashCrater(float impactX, float impactY, float impactZ, float impactSpeed);

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

void syncUiStateFromHud(UiState& uiState, const HudSettings& hudSettings)
{
    uiState.showDebug = hudSettings.showDebug;
    uiState.showThrottleHud = hudSettings.showThrottle;
    uiState.showControlIndicator = hudSettings.showControls;
    uiState.showMap = hudSettings.showMap;
    uiState.showGeoInfo = hudSettings.showGeoInfo;
}

void syncHudFromUiState(HudSettings& hudSettings, const UiState& uiState)
{
    hudSettings.showDebug = uiState.showDebug;
    hudSettings.showThrottle = uiState.showThrottleHud;
    hudSettings.showControls = uiState.showControlIndicator;
    hudSettings.showMap = uiState.showMap;
    hudSettings.showGeoInfo = uiState.showGeoInfo;
}

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
        { InputActionId::VoicePushToTalk, "Voice Push-To-Talk", "Networking and voice are not yet implemented in native.", true, false, { key(SDL_SCANCODE_V), {} } }
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

bool controlActionSupported(InputActionId actionId)
{
    return actionId != InputActionId::VoicePushToTalk;
}

bool controlActionConfigurable(InputActionId actionId)
{
    return actionId != InputActionId::VoicePushToTalk;
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

bool keyMatchesBinding(SDL_Scancode binding, SDL_Scancode input)
{
    return binding == input;
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

HudColor makeHudColor(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255)
{
    return { r, g, b, a };
}

HudColor makeHudColor(const Vec3& color, std::uint8_t alpha = 255)
{
    return {
        static_cast<std::uint8_t>(clamp(color.x, 0.0f, 1.0f) * 255.0f),
        static_cast<std::uint8_t>(clamp(color.y, 0.0f, 1.0f) * 255.0f),
        static_cast<std::uint8_t>(clamp(color.z, 0.0f, 1.0f) * 255.0f),
        alpha
    };
}

HudColor makeHudColor(const Vec4& color)
{
    return {
        static_cast<std::uint8_t>(clamp(color.x, 0.0f, 1.0f) * 255.0f),
        static_cast<std::uint8_t>(clamp(color.y, 0.0f, 1.0f) * 255.0f),
        static_cast<std::uint8_t>(clamp(color.z, 0.0f, 1.0f) * 255.0f),
        static_cast<std::uint8_t>(clamp(color.w, 0.0f, 1.0f) * 255.0f)
    };
}

std::filesystem::path findAssetPath(const std::string& relativePath)
{
    std::vector<std::filesystem::path> roots;
    roots.push_back(std::filesystem::current_path());

    if (const char* basePath = SDL_GetBasePath(); basePath != nullptr) {
        std::filesystem::path root(basePath);
        for (int i = 0; i < 5; ++i) {
            roots.push_back(root);
            if (!root.has_parent_path()) {
                break;
            }
            root = root.parent_path();
        }
    }

    std::error_code ec;
    for (const auto& root : roots) {
        const auto candidate = root / relativePath;
        if (std::filesystem::exists(candidate, ec)) {
            return candidate;
        }
    }
    return {};
}

float sampleGroundHeight(float x, float z, const TerrainParams& terrainParams)
{
    return sampleSurfaceHeight(x, z, terrainParams);
}

float sampleGroundHeight(float x, float z, const TerrainFieldContext& terrainContext)
{
    return sampleSurfaceHeight(x, z, terrainContext);
}

float computeWaterProximity(const FlightState& plane, const TerrainFieldContext& terrainContext)
{
    const float ground = sampleGroundHeight(plane.pos.x, plane.pos.z, terrainContext);
    const float waterSurface = sampleWaterHeight(plane.pos.x, plane.pos.z, terrainContext);
    const float waterDepth = std::max(0.0f, waterSurface - ground);
    if (waterDepth <= 1.0f) {
        return 0.0f;
    }

    const float lowestPoint = plane.pos.y - std::max(plane.collisionRadius, 1.0f);
    const float clearance = lowestPoint - waterSurface;
    const float skimFactor = clearance <= 0.0f ? 1.0f : clamp(1.0f - (clearance / 18.0f), 0.0f, 1.0f);
    const float depthFactor = clamp((waterDepth - 1.0f) / 18.0f, 0.0f, 1.0f);
    return skimFactor * depthFactor;
}

ProceduralAudioFrame buildProceduralAudioFrame(
    const FlightState& plane,
    const FlightRuntimeState& runtime,
    const FlightEnvironment& environment,
    const TerrainFieldContext& terrainContext,
    const UiState& uiState,
    const PropAudioConfig& propAudioConfig,
    float foliageBrush,
    float foliageImpact,
    bool active,
    bool paused,
    float dt,
    bool afterburnerActive = false)
{
    ProceduralAudioFrame frame {};
    frame.active = active;
    frame.paused = paused;
    frame.audioEnabled = uiState.audioEnabled;
    frame.onGround = plane.onGround;
    frame.afterburner = afterburnerActive;
    frame.dt = dt;
    frame.masterVolume = uiState.masterVolume;
    frame.engineVolume = uiState.engineVolume;
    frame.ambienceVolume = uiState.ambienceVolume;

    const Vec3 airVelWorld = plane.flightVel - environment.wind;
    const Vec3 airVelBody = worldToBody(plane.rot, airVelWorld);
    frame.trueAirspeed = length(airVelBody);
    frame.dynamicPressure = runtime.lastDynamicPressure;
    frame.thrustNewton = runtime.lastThrustNewton;
    frame.alphaRad = runtime.lastAlpha;
    frame.betaRad = runtime.lastBeta;
    frame.verticalSpeed = airVelWorld.y;
    frame.angularRateRad = length(plane.flightAngVel);
    frame.waterProximity = computeWaterProximity(plane, terrainContext);
    frame.foliageBrush = clamp(foliageBrush, 0.0f, 1.0f);
    frame.foliageImpact = clamp(foliageImpact, 0.0f, 1.0f);
    frame.propAudioConfig = propAudioConfig;
    return frame;
}

float normalizeGamepadStickAxis(Sint16 rawValue, float deadzone = kGamepadStickDeadzone)
{
    const float normalized = clamp(static_cast<float>(rawValue) / 32767.0f, -1.0f, 1.0f);
    const float magnitude = std::fabs(normalized);
    if (magnitude <= deadzone) {
        return 0.0f;
    }

    const float scaled = (magnitude - deadzone) / std::max(0.001f, 1.0f - deadzone);
    return std::copysign(clamp(scaled, 0.0f, 1.0f), normalized);
}

float normalizeGamepadTriggerAxis(Sint16 rawValue, float deadzone = kGamepadTriggerDeadzone)
{
    const float normalized = clamp(static_cast<float>(rawValue) / 32767.0f, 0.0f, 1.0f);
    if (normalized <= deadzone) {
        return 0.0f;
    }
    return clamp((normalized - deadzone) / std::max(0.001f, 1.0f - deadzone), 0.0f, 1.0f);
}

bool gamepadButtonDown(const GamepadState& gamepad, SDL_GamepadButton button)
{
    const std::size_t index = static_cast<std::size_t>(button);
    return index < gamepad.buttons.size() ? gamepad.buttons[index] : false;
}

bool gamepadButtonPressed(const GamepadState& gamepad, SDL_GamepadButton button)
{
    const std::size_t index = static_cast<std::size_t>(button);
    return index < gamepad.buttons.size() && gamepad.buttons[index] && !gamepad.previousButtons[index];
}

float gamepadAxisValue(const GamepadState& gamepad, SDL_GamepadAxis axis)
{
    const std::size_t index = static_cast<std::size_t>(axis);
    return index < gamepad.axes.size() ? gamepad.axes[index] : 0.0f;
}

void pollGamepadState(GamepadState& gamepad)
{
    gamepad.previousButtons = gamepad.buttons;
    std::fill(gamepad.buttons.begin(), gamepad.buttons.end(), false);
    std::fill(gamepad.axes.begin(), gamepad.axes.end(), 0.0f);

    if (gamepad.handle == nullptr) {
        return;
    }

    for (int buttonIndex = 0; buttonIndex < SDL_GAMEPAD_BUTTON_COUNT; ++buttonIndex) {
        gamepad.buttons[static_cast<std::size_t>(buttonIndex)] =
            SDL_GetGamepadButton(gamepad.handle, static_cast<SDL_GamepadButton>(buttonIndex));
    }
    gamepad.axes[static_cast<std::size_t>(SDL_GAMEPAD_AXIS_LEFTX)] =
        normalizeGamepadStickAxis(SDL_GetGamepadAxis(gamepad.handle, SDL_GAMEPAD_AXIS_LEFTX));
    gamepad.axes[static_cast<std::size_t>(SDL_GAMEPAD_AXIS_LEFTY)] =
        normalizeGamepadStickAxis(SDL_GetGamepadAxis(gamepad.handle, SDL_GAMEPAD_AXIS_LEFTY));
    gamepad.axes[static_cast<std::size_t>(SDL_GAMEPAD_AXIS_RIGHTX)] =
        normalizeGamepadStickAxis(SDL_GetGamepadAxis(gamepad.handle, SDL_GAMEPAD_AXIS_RIGHTX));
    gamepad.axes[static_cast<std::size_t>(SDL_GAMEPAD_AXIS_RIGHTY)] =
        normalizeGamepadStickAxis(SDL_GetGamepadAxis(gamepad.handle, SDL_GAMEPAD_AXIS_RIGHTY));
    gamepad.axes[static_cast<std::size_t>(SDL_GAMEPAD_AXIS_LEFT_TRIGGER)] =
        normalizeGamepadTriggerAxis(SDL_GetGamepadAxis(gamepad.handle, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    gamepad.axes[static_cast<std::size_t>(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)] =
        normalizeGamepadTriggerAxis(SDL_GetGamepadAxis(gamepad.handle, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
}

int consumeRepeatedGamepadDirection(bool negativeHeld, bool positiveHeld, float nowSeconds, int& heldDirection, float& repeatAt)
{
    const int direction = positiveHeld ? 1 : (negativeHeld ? -1 : 0);
    if (direction == 0) {
        heldDirection = 0;
        repeatAt = nowSeconds;
        return 0;
    }
    if (direction != heldDirection) {
        heldDirection = direction;
        repeatAt = nowSeconds + kGamepadMenuRepeatDelay;
        return direction;
    }
    if (nowSeconds >= repeatAt) {
        repeatAt = nowSeconds + kGamepadMenuRepeatInterval;
        return direction;
    }
    return 0;
}

Quat applyCameraLookOffset(const Quat& baseRotation, float yawOffset, float pitchOffset)
{
    Quat rotation = baseRotation;
    if (std::fabs(yawOffset) > 1.0e-5f) {
        const Vec3 up = rotateVector(rotation, { 0.0f, 1.0f, 0.0f });
        rotation = quatNormalize(quatMultiply(quatFromAxisAngle(up, yawOffset), rotation));
    }
    if (std::fabs(pitchOffset) > 1.0e-5f) {
        const Vec3 right = rotateVector(rotation, { 1.0f, 0.0f, 0.0f });
        rotation = quatNormalize(quatMultiply(quatFromAxisAngle(right, pitchOffset), rotation));
    }
    return rotation;
}

void applyWalkingGamepadLook(
    UiState& uiState,
    FlightState& actor,
    float& walkYaw,
    float& walkPitch,
    float lookX,
    float lookY,
    float dt)
{
    float pitchAxis = lookY;
    if (uiState.invertLookY) {
        pitchAxis = -pitchAxis;
    }
    if (std::fabs(lookX) <= 1.0e-4f && std::fabs(pitchAxis) <= 1.0e-4f) {
        return;
    }

    walkPitch = clamp(
        walkPitch + (pitchAxis * kGamepadWalkingLookPitchSpeed * dt),
        -kWalkingPitchLimitRadians,
        kWalkingPitchLimitRadians);
    walkYaw = wrapAngle(walkYaw + (lookX * kGamepadWalkingLookYawSpeed * dt));
    actor.rot = composeWalkingRotation(walkYaw, walkPitch);
}

void applyFlightGamepadLook(
    UiState& uiState,
    float& lookYaw,
    float& lookPitch,
    float lookX,
    float lookY,
    float dt)
{
    float pitchAxis = lookY;
    if (uiState.invertLookY) {
        pitchAxis = -pitchAxis;
    }

    if (std::fabs(lookX) <= 1.0e-4f && std::fabs(pitchAxis) <= 1.0e-4f) {
        const float returnAlpha = clamp(kGamepadFlightLookReturnRate * dt, 0.0f, 1.0f);
        lookYaw = mix(lookYaw, 0.0f, returnAlpha);
        lookPitch = mix(lookPitch, 0.0f, returnAlpha);
        return;
    }

    lookYaw = wrapAngle(lookYaw + (lookX * kGamepadFlightLookYawSpeed * dt));
    lookPitch = clamp(
        lookPitch + (pitchAxis * kGamepadFlightLookPitchSpeed * dt),
        -kGamepadFlightLookPitchLimitRadians,
        kGamepadFlightLookPitchLimitRadians);
}

void invalidateTerrainCache(TerrainVisualCache& terrainCache)
{
    terrainCache.valid = false;
    terrainCache.nearTiles.clear();
    terrainCache.farTiles.clear();
}

void refreshTerrainContext(
    TerrainParams& terrainParams,
    TerrainFieldContext& terrainContext,
    TerrainVisualCache& terrainCache,
    WorldStore* worldStore = nullptr)
{
    terrainParams = normalizeTerrainParams(terrainParams);
    terrainContext = createTerrainFieldContext(terrainParams);
    bindTerrainContextWorldStore(terrainContext, worldStore);
    terrainParams = terrainContext.params;
    invalidateTerrainCache(terrainCache);
}

void keepPlaneAboveTerrain(FlightState& plane, const TerrainFieldContext& terrainContext, float clearance = 6.0f)
{
    const float ground = sampleGroundHeight(plane.pos.x, plane.pos.z, terrainContext);
    if (plane.pos.y < ground + clearance) {
        plane.pos.y = ground + clearance;
    }
}

void applyTerrainRuntimeChange(
    TerrainParams& terrainParams,
    TerrainFieldContext& terrainContext,
    TerrainVisualCache& terrainCache,
    FlightState& plane,
    WorldStore* worldStore = nullptr)
{
    refreshTerrainContext(terrainParams, terrainContext, terrainCache, worldStore);
    keepPlaneAboveTerrain(plane, terrainContext);
}

bool applyLocalizedTerrainCrater(
    TerrainParams& terrainParams,
    TerrainFieldContext& terrainContext,
    TerrainVisualCache& terrainCache,
    FlightState& plane,
    WorldStore* worldStore,
    float radius,
    float depth,
    float rim)
{
    if (worldStore == nullptr) {
        return false;
    }

    const TerrainCrater crater {
        plane.pos.x,
        sampleGroundHeight(plane.pos.x, plane.pos.z, terrainContext),
        plane.pos.z,
        radius,
        depth,
        rim
    };
    const auto craterResult = worldStore->applyCrater(crater);
    if (!craterResult.first) {
        return false;
    }

    worldStore->flushDirty(nullptr);
    refreshTerrainContext(terrainParams, terrainContext, terrainCache, worldStore);
    keepPlaneAboveTerrain(plane, terrainContext);
    return true;
}

float snapToSpacing(float value, float spacing)
{
    const float safeSpacing = std::max(1.0f, spacing);
    return std::round(value / safeSpacing) * safeSpacing;
}

float quantizeUp(float value, float spacing)
{
    const float safeSpacing = std::max(1.0f, spacing);
    return std::ceil(value / safeSpacing) * safeSpacing;
}

float computeLod0TerrainTileSize(const TerrainParams& params)
{
    return std::max(params.chunkSize, params.chunkSize * static_cast<float>(std::max(params.lod0ChunkScale, 1)));
}

float computeLod1TerrainTileSize(const TerrainParams& params)
{
    return std::max(params.chunkSize, params.chunkSize * static_cast<float>(std::max(params.lod1ChunkScale, 1)));
}

float computeLod2TerrainTileSize(const TerrainParams& params)
{
    const int lod1Scale = std::max(params.lod1ChunkScale, 1);
    const int lod2Scale = std::max(params.lod2ChunkScale, lod1Scale);
    const int lod2Ratio = std::max(1, (lod2Scale + lod1Scale - 1) / lod1Scale);
    return computeLod1TerrainTileSize(params) * static_cast<float>(lod2Ratio);
}

float computeNearHalfExtent(const TerrainParams& params, float lod0TileSize)
{
    const float nearRadius = std::max(
        params.gameplayRadiusMeters,
        params.chunkSize * static_cast<float>(std::max(params.lod0Radius, 4)));
    return quantizeUp(nearRadius, lod0TileSize);
}

float computeMidHalfExtent(const TerrainParams& params, float nearHalfExtent, float lod2TileSize)
{
    const float midRadius = std::max(params.midFieldRadiusMeters, nearHalfExtent + params.chunkSize);
    return quantizeUp(midRadius, lod2TileSize);
}

float computeFarHalfExtent(const TerrainParams& params, float midHalfExtent, float lod2TileSize)
{
    const float farRadius = std::max(params.horizonRadiusMeters, midHalfExtent + params.chunkSize);
    return quantizeUp(farRadius, lod2TileSize);
}

std::string makeTerrainAssetKeyPrefix(const TerrainParams& params)
{
    char buffer[256] {};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "terrain:s%d:surf%d:skirts%d:l0%.2f:l1%.2f:l2%.2f",
        params.seed,
        params.surfaceOnlyMeshing ? 1 : 0,
        params.enableSkirts ? 1 : 0,
        params.lod0CellSize,
        params.lod1CellSize,
        params.lod2CellSize);
    return buffer;
}

std::uint64_t hashTerrainSignatureString(const std::string& value)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char ch : value) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::uint64_t terrainParamsSignature(const TerrainParams& params)
{
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(5);
    stream
        << params.seed << '|'
        << params.chunkSize << '|'
        << params.worldRadius << '|'
        << params.baseHeight << '|'
        << params.heightAmplitude << '|'
        << params.heightFrequency << '|'
        << params.surfaceDetailAmplitude << '|'
        << params.surfaceDetailFrequency << '|'
        << params.ridgeAmplitude << '|'
        << params.ridgeFrequency << '|'
        << params.ridgeSharpness << '|'
        << params.macroWarpAmplitude << '|'
        << params.macroWarpFrequency << '|'
        << params.terraceStrength << '|'
        << params.terraceStep << '|'
        << params.waterLevel << '|'
        << params.shorelineBand << '|'
        << params.waterWaveAmplitude << '|'
        << params.waterWaveFrequency << '|'
        << params.snowLine << '|'
        << params.caveEnabled << '|'
        << params.caveFrequency << '|'
        << params.caveThreshold << '|'
        << params.caveStrength << '|'
        << params.surfaceOnlyMeshing << '|'
        << params.enableSkirts << '|'
        << params.skirtDepth << '|'
        << params.decoration.enabled << '|'
        << params.decoration.density << '|'
        << params.decoration.nearDensityScale << '|'
        << params.decoration.midDensityScale << '|'
        << params.decoration.farDensityScale << '|'
        << params.decoration.treeLineOffset << '|'
        << params.decoration.shoreBrushDensity << '|'
        << params.decoration.rockDensity << '|'
        << params.decoration.collisionEnabled << '|'
        << params.decoration.seedOffset << '|'
        << params.lod0CellSize << '|'
        << params.lod1CellSize << '|'
        << params.lod2CellSize << '|'
        << params.heightOctaves << '|'
        << params.caveOctaves << '|'
        << params.explicitTunnelSeeds.size();
    return hashTerrainSignatureString(stream.str());
}

std::string terrainChunkAssetKey(const TerrainChunkKey& key, const char* surfaceTag)
{
    std::ostringstream stream;
    stream
        << "terrain_chunk:"
        << key.worldId << ':'
        << key.seed << ':'
        << key.generatorVersion << ':'
        << key.band << ':'
        << key.detail << ':'
        << key.tileX << ':'
        << key.tileZ << ':'
        << key.paramsSignature << ':'
        << key.sourceSignature << ':'
        << surfaceTag;
    return stream.str();
}

std::uint64_t terrainSourceSignature(
    const TerrainFieldContext& terrainContext,
    float x0,
    float z0,
    float x1,
    float z1)
{
    return terrainContext.sampleChunkRevisionSignature
        ? terrainContext.sampleChunkRevisionSignature(x0, z0, x1, z1)
        : 0ull;
}

struct TerrainTileDecorationResult {
    Model propModel {};
    std::vector<TerrainPropCollider> propColliders;
};

std::uint32_t mixTerrainPropHash(std::uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

std::uint32_t terrainPropHash(const TerrainChunkKey& key, int salt)
{
    std::uint32_t hash = static_cast<std::uint32_t>(std::max(1, key.seed));
    hash ^= static_cast<std::uint32_t>(key.tileX * 73856093);
    hash ^= static_cast<std::uint32_t>(key.tileZ * 19349663);
    hash ^= static_cast<std::uint32_t>((key.band + 1) * 83492791);
    hash ^= static_cast<std::uint32_t>((key.detail + 1) * 2654435761u);
    hash ^= static_cast<std::uint32_t>(salt * 2246822519u);
    return mixTerrainPropHash(hash);
}

float terrainPropUnitFloat(std::uint32_t seed)
{
    return static_cast<float>(mixTerrainPropHash(seed) & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

float terrainPropSignedFloat(std::uint32_t seed)
{
    return (terrainPropUnitFloat(seed) * 2.0f) - 1.0f;
}

float terrainPropDensityScaleForBand(const TerrainParams& params, TerrainFarTileBand band)
{
    switch (band) {
    case TerrainFarTileBand::Near:
        return params.decoration.nearDensityScale;
    case TerrainFarTileBand::Mid:
        return params.decoration.midDensityScale;
    case TerrainFarTileBand::Horizon:
    default:
        return params.decoration.farDensityScale;
    }
}

float sampleTerrainSlope01(float x, float z, const TerrainFieldContext& terrainContext)
{
    constexpr float epsilon = 1.8f;
    const float hL = sampleSurfaceHeight(x - epsilon, z, terrainContext);
    const float hR = sampleSurfaceHeight(x + epsilon, z, terrainContext);
    const float hD = sampleSurfaceHeight(x, z - epsilon, terrainContext);
    const float hU = sampleSurfaceHeight(x, z + epsilon, terrainContext);
    return clamp(std::sqrt(((hR - hL) * (hR - hL)) + ((hU - hD) * (hU - hD))) * 0.14f, 0.0f, 1.0f);
}

Vec3 multiplyColors(const Vec3& lhs, const Vec3& rhs)
{
    return {
        clamp(lhs.x * rhs.x, 0.0f, 1.0f),
        clamp(lhs.y * rhs.y, 0.0f, 1.0f),
        clamp(lhs.z * rhs.z, 0.0f, 1.0f)
    };
}

Vec3 scaledColor(const Vec3& color, float scale)
{
    return {
        clamp(color.x * scale, 0.0f, 1.0f),
        clamp(color.y * scale, 0.0f, 1.0f),
        clamp(color.z * scale, 0.0f, 1.0f)
    };
}

Quat buildTerrainPropRotation(float yawRadians, float leanRadians, float leanSeed)
{
    const Quat yaw = quatFromAxisAngle({ 0.0f, 1.0f, 0.0f }, yawRadians);
    const Vec3 leanAxis = normalize(
        {
            std::cos(leanSeed * 6.2831853f),
            0.0f,
            std::sin(leanSeed * 6.2831853f)
        },
        { 1.0f, 0.0f, 0.0f });
    const Quat lean = quatFromAxisAngle(leanAxis, leanRadians);
    return quatNormalize(quatMultiply(yaw, lean));
}

Vec3 transformTerrainPropPoint(const Vec3& localPoint, const Vec3& origin, const Quat& rotation, const Vec3& scale)
{
    const Vec3 scaledPoint {
        localPoint.x * scale.x,
        localPoint.y * scale.y,
        localPoint.z * scale.z
    };
    return origin + rotateVector(rotation, scaledPoint);
}

void addTerrainPropTriangle(
    Model& model,
    const Vec3& origin,
    const Quat& rotation,
    const Vec3& scale,
    const Vec3& a,
    const Vec3& b,
    const Vec3& c,
    const Vec3& color)
{
    addColoredTriangle(
        model,
        transformTerrainPropPoint(a, origin, rotation, scale),
        transformTerrainPropPoint(b, origin, rotation, scale),
        transformTerrainPropPoint(c, origin, rotation, scale),
        color);
}

void addTerrainPropQuad(
    Model& model,
    const Vec3& origin,
    const Quat& rotation,
    const Vec3& scale,
    const Vec3& a,
    const Vec3& b,
    const Vec3& c,
    const Vec3& d,
    const Vec3& color)
{
    addColoredQuad(
        model,
        transformTerrainPropPoint(a, origin, rotation, scale),
        transformTerrainPropPoint(b, origin, rotation, scale),
        transformTerrainPropPoint(c, origin, rotation, scale),
        transformTerrainPropPoint(d, origin, rotation, scale),
        color);
}

void appendTerrainPropPrism(
    Model& model,
    const Vec3& origin,
    const Quat& rotation,
    const Vec3& scale,
    float radius,
    float height,
    int sides,
    const Vec3& sideColor,
    const Vec3& topColor)
{
    const int faceCount = std::max(3, sides);
    const float angleStep = (2.0f * kPi) / static_cast<float>(faceCount);
    const Vec3 topCenter { 0.0f, height, 0.0f };
    const Vec3 bottomCenter { 0.0f, 0.0f, 0.0f };

    for (int faceIndex = 0; faceIndex < faceCount; ++faceIndex) {
        const float angle0 = static_cast<float>(faceIndex) * angleStep;
        const float angle1 = static_cast<float>(faceIndex + 1) * angleStep;
        const Vec3 b0 { std::cos(angle0) * radius, 0.0f, std::sin(angle0) * radius };
        const Vec3 b1 { std::cos(angle1) * radius, 0.0f, std::sin(angle1) * radius };
        const Vec3 t0 { b0.x, height, b0.z };
        const Vec3 t1 { b1.x, height, b1.z };
        addTerrainPropQuad(model, origin, rotation, scale, b0, b1, t1, t0, sideColor);
        addTerrainPropTriangle(model, origin, rotation, scale, topCenter, t0, t1, topColor);
        addTerrainPropTriangle(model, origin, rotation, scale, bottomCenter, b1, b0, scaledColor(sideColor, 0.72f));
    }
}

void appendTerrainPropCone(
    Model& model,
    const Vec3& origin,
    const Quat& rotation,
    const Vec3& scale,
    float radius,
    float height,
    int sides,
    const Vec3& sideColor)
{
    const int faceCount = std::max(3, sides);
    const float angleStep = (2.0f * kPi) / static_cast<float>(faceCount);
    const Vec3 apex { 0.0f, height, 0.0f };
    const Vec3 baseCenter { 0.0f, 0.0f, 0.0f };
    for (int faceIndex = 0; faceIndex < faceCount; ++faceIndex) {
        const float angle0 = static_cast<float>(faceIndex) * angleStep;
        const float angle1 = static_cast<float>(faceIndex + 1) * angleStep;
        const Vec3 b0 { std::cos(angle0) * radius, 0.0f, std::sin(angle0) * radius };
        const Vec3 b1 { std::cos(angle1) * radius, 0.0f, std::sin(angle1) * radius };
        addTerrainPropTriangle(model, origin, rotation, scale, apex, b0, b1, sideColor);
        addTerrainPropTriangle(model, origin, rotation, scale, baseCenter, b1, b0, scaledColor(sideColor, 0.68f));
    }
}

void appendTerrainPropOctahedron(
    Model& model,
    const Vec3& origin,
    const Quat& rotation,
    const Vec3& scale,
    const Vec3& color)
{
    constexpr std::array<Vec3, 6> vertices {
        Vec3 { 0.0f, 1.0f, 0.0f },
        Vec3 { 1.0f, 0.0f, 0.0f },
        Vec3 { 0.0f, 0.0f, 1.0f },
        Vec3 { -1.0f, 0.0f, 0.0f },
        Vec3 { 0.0f, 0.0f, -1.0f },
        Vec3 { 0.0f, -1.0f, 0.0f }
    };
    constexpr std::array<std::array<int, 3>, 8> faces {{
        { 0, 1, 2 }, { 0, 2, 3 }, { 0, 3, 4 }, { 0, 4, 1 },
        { 5, 2, 1 }, { 5, 3, 2 }, { 5, 4, 3 }, { 5, 1, 4 }
    }};
    for (const auto& face : faces) {
        addTerrainPropTriangle(
            model,
            origin,
            rotation,
            scale,
            vertices[static_cast<std::size_t>(face[0])],
            vertices[static_cast<std::size_t>(face[1])],
            vertices[static_cast<std::size_t>(face[2])],
            color);
    }
}

TerrainPropPlacement buildTerrainPropPlacement(
    TerrainPropVariant variant,
    TerrainPropClass propClass,
    const Vec3& position,
    const Vec3& scale,
    const Vec3& tint,
    float yawRadians,
    float leanRadians)
{
    TerrainPropPlacement placement;
    placement.variant = variant;
    placement.propClass = propClass;
    placement.position = position;
    placement.scale = scale;
    placement.tint = tint;
    placement.yawRadians = yawRadians;
    placement.leanRadians = leanRadians;
    return placement;
}

void appendPlacementCollider(
    std::vector<TerrainPropCollider>& colliders,
    TerrainPropClass propClass,
    const Vec3& center,
    float radius,
    float halfHeight,
    float softness)
{
    TerrainPropCollider collider;
    collider.propClass = propClass;
    collider.center = center;
    collider.radius = std::max(0.0f, radius);
    collider.halfHeight = std::max(0.0f, halfHeight);
    collider.softness = clamp(softness, 0.0f, 1.0f);
    colliders.push_back(collider);
}

void appendConiferPlacement(Model& model, const TerrainPropPlacement& placement)
{
    const Quat rotation = buildTerrainPropRotation(placement.yawRadians, placement.leanRadians, placement.scale.x + placement.scale.z);
    const Vec3 trunkColor = multiplyColors({ 0.34f, 0.24f, 0.14f }, placement.tint);
    const Vec3 canopyColor = multiplyColors({ 0.16f, 0.34f, 0.18f }, placement.tint);
    const float trunkHeight = 0.30f;
    appendTerrainPropPrism(model, placement.position, rotation, placement.scale, 0.11f, trunkHeight, 5, trunkColor, scaledColor(trunkColor, 1.06f));
    appendTerrainPropCone(model, placement.position + rotateVector(rotation, { 0.0f, trunkHeight * placement.scale.y * 0.58f, 0.0f }), rotation, placement.scale, 0.50f, 0.74f, 6, canopyColor);
    appendTerrainPropCone(model, placement.position + rotateVector(rotation, { 0.0f, trunkHeight * placement.scale.y + (0.20f * placement.scale.y), 0.0f }), rotation, placement.scale * 0.82f, 0.48f, 0.72f, 6, scaledColor(canopyColor, 1.08f));
    appendTerrainPropCone(model, placement.position + rotateVector(rotation, { 0.0f, trunkHeight * placement.scale.y + (0.50f * placement.scale.y), 0.0f }), rotation, placement.scale * 0.58f, 0.40f, 0.64f, 5, scaledColor(canopyColor, 0.96f));
}

void appendBroadleafPlacement(Model& model, const TerrainPropPlacement& placement)
{
    const Quat rotation = buildTerrainPropRotation(placement.yawRadians, placement.leanRadians, placement.scale.y);
    const Vec3 trunkColor = multiplyColors({ 0.38f, 0.26f, 0.15f }, placement.tint);
    const Vec3 canopyColor = multiplyColors({ 0.24f, 0.47f, 0.23f }, placement.tint);
    const float trunkHeight = 0.34f;
    appendTerrainPropPrism(model, placement.position, rotation, placement.scale, 0.10f, trunkHeight, 6, trunkColor, scaledColor(trunkColor, 1.05f));
    appendTerrainPropOctahedron(model, placement.position + rotateVector(rotation, { 0.0f, 0.90f * placement.scale.y, 0.0f }), rotation, { placement.scale.x * 0.92f, placement.scale.y * 0.78f, placement.scale.z * 0.92f }, canopyColor);
    appendTerrainPropOctahedron(model, placement.position + rotateVector(rotation, { -0.28f * placement.scale.x, 0.82f * placement.scale.y, 0.18f * placement.scale.z }), rotation, { placement.scale.x * 0.58f, placement.scale.y * 0.46f, placement.scale.z * 0.58f }, scaledColor(canopyColor, 1.08f));
    appendTerrainPropOctahedron(model, placement.position + rotateVector(rotation, { 0.26f * placement.scale.x, 0.80f * placement.scale.y, -0.14f * placement.scale.z }), rotation, { placement.scale.x * 0.52f, placement.scale.y * 0.42f, placement.scale.z * 0.52f }, scaledColor(canopyColor, 0.94f));
}

void appendShrubPlacement(Model& model, const TerrainPropPlacement& placement)
{
    const Quat rotation = buildTerrainPropRotation(placement.yawRadians, placement.leanRadians, placement.scale.x * 0.7f);
    const Vec3 canopyColor = multiplyColors({ 0.29f, 0.46f, 0.23f }, placement.tint);
    appendTerrainPropOctahedron(model, placement.position + rotateVector(rotation, { 0.0f, 0.34f * placement.scale.y, 0.0f }), rotation, { placement.scale.x * 0.62f, placement.scale.y * 0.38f, placement.scale.z * 0.62f }, canopyColor);
    appendTerrainPropOctahedron(model, placement.position + rotateVector(rotation, { 0.20f * placement.scale.x, 0.28f * placement.scale.y, -0.12f * placement.scale.z }), rotation, { placement.scale.x * 0.38f, placement.scale.y * 0.22f, placement.scale.z * 0.38f }, scaledColor(canopyColor, 1.10f));
    appendTerrainPropOctahedron(model, placement.position + rotateVector(rotation, { -0.18f * placement.scale.x, 0.26f * placement.scale.y, 0.14f * placement.scale.z }), rotation, { placement.scale.x * 0.34f, placement.scale.y * 0.20f, placement.scale.z * 0.34f }, scaledColor(canopyColor, 0.92f));
}

void appendRockPlacement(Model& model, const TerrainPropPlacement& placement)
{
    const Quat rotation = buildTerrainPropRotation(placement.yawRadians, placement.leanRadians * 0.4f, placement.scale.z);
    const Vec3 rockColor = multiplyColors({ 0.48f, 0.46f, 0.44f }, placement.tint);
    appendTerrainPropOctahedron(model, placement.position + rotateVector(rotation, { 0.0f, 0.26f * placement.scale.y, 0.0f }), rotation, { placement.scale.x * 0.60f, placement.scale.y * 0.40f, placement.scale.z * 0.54f }, rockColor);
    appendTerrainPropOctahedron(model, placement.position + rotateVector(rotation, { 0.10f * placement.scale.x, 0.16f * placement.scale.y, 0.08f * placement.scale.z }), rotation, { placement.scale.x * 0.28f, placement.scale.y * 0.18f, placement.scale.z * 0.24f }, scaledColor(rockColor, 1.08f));
}

TerrainTileDecorationResult buildTerrainTileDecoration(
    const TerrainChunkKey& key,
    const TerrainPatchBounds& bounds,
    TerrainFarTileBand band,
    const TerrainFieldContext& terrainContext)
{
    TerrainTileDecorationResult result;
    const TerrainParams& params = terrainContext.params;
    if (!params.decoration.enabled || params.decoration.density <= 0.0f) {
        return result;
    }

    const float bandDensity = params.decoration.density * terrainPropDensityScaleForBand(params, band);
    if (bandDensity <= 0.0f) {
        return result;
    }

    const float spanX = std::max(1.0f, bounds.x1 - bounds.x0);
    const float spanZ = std::max(1.0f, bounds.z1 - bounds.z0);
    const float spacingBase = band == TerrainFarTileBand::Near ? 34.0f : (band == TerrainFarTileBand::Mid ? 56.0f : 104.0f);
    const float spacing = clamp(spacingBase / std::sqrt(std::max(0.15f, bandDensity)), 14.0f, 140.0f);
    const int gridX = std::max(1, static_cast<int>(std::ceil(spanX / spacing)));
    const int gridZ = std::max(1, static_cast<int>(std::ceil(spanZ / spacing)));
    const float cellX = spanX / static_cast<float>(gridX);
    const float cellZ = spanZ / static_cast<float>(gridZ);

    std::vector<TerrainPropPlacement> placements;
    placements.reserve(static_cast<std::size_t>(gridX * gridZ));
    result.propColliders.reserve(static_cast<std::size_t>(gridX * gridZ));

    for (int gz = 0; gz < gridZ; ++gz) {
        for (int gx = 0; gx < gridX; ++gx) {
            const int cellIndex = (gz * gridX) + gx;
            const std::uint32_t baseHash = terrainPropHash(key, cellIndex + params.decoration.seedOffset);
            const float jitterX = terrainPropSignedFloat(baseHash ^ 0x68bc21ebu) * (cellX * 0.32f);
            const float jitterZ = terrainPropSignedFloat(baseHash ^ 0x02e5be93u) * (cellZ * 0.32f);
            const float x = bounds.x0 + ((static_cast<float>(gx) + 0.5f) * cellX) + jitterX;
            const float z = bounds.z0 + ((static_cast<float>(gz) + 0.5f) * cellZ) + jitterZ;
            const TerrainMaterialSample material = sampleTerrainMaterial(x, 0.0f, z, terrainContext);
            const float slope = sampleTerrainSlope01(x, z, terrainContext);
            const float waterClearance = material.surfaceHeight - material.waterHeight;
            if (waterClearance < -0.1f) {
                continue;
            }

            const float treeline = params.snowLine + params.decoration.treeLineOffset;
            const float greenBand = clamp((material.biomeBlend - 0.18f) * 1.35f, 0.0f, 1.0f);
            const float lowSlope = clamp(1.0f - (slope * 1.6f), 0.0f, 1.0f);
            const float belowTreeline = clamp((treeline + 90.0f - material.surfaceHeight) / 140.0f, 0.0f, 1.0f);
            const float aboveWater = clamp((waterClearance + 0.4f) / 4.5f, 0.0f, 1.0f);
            const float coniferWeight =
                greenBand *
                lowSlope *
                aboveWater *
                clamp(0.55f + ((1.0f - material.biomeBlend) * 0.7f) + (material.snowWeight * 0.35f), 0.0f, 1.0f);
            const float broadleafWeight =
                greenBand *
                lowSlope *
                aboveWater *
                belowTreeline *
                clamp(((material.biomeBlend - 0.36f) * 1.45f) + (1.0f - material.rockWeight) * 0.25f, 0.0f, 1.0f);
            const float shrubWeight =
                params.decoration.shoreBrushDensity *
                clamp(material.wetness * 1.55f, 0.0f, 1.0f) *
                clamp(1.0f - (slope * 1.9f), 0.0f, 1.0f) *
                clamp((waterClearance + 0.1f) / 2.0f, 0.0f, 1.0f);
            const float rockWeight =
                params.decoration.rockDensity *
                clamp((material.rockWeight * 0.95f) + (material.snowWeight * 0.45f) + clamp((material.surfaceHeight - treeline + 28.0f) / 120.0f, 0.0f, 1.0f) * 0.35f, 0.0f, 1.0f) *
                clamp(0.45f + (slope * 0.7f), 0.0f, 1.0f);

            float weightedTotal = coniferWeight + broadleafWeight + shrubWeight + rockWeight;
            if (band == TerrainFarTileBand::Horizon) {
                weightedTotal = (coniferWeight * 0.75f) + (rockWeight * 0.95f);
            }
            if (weightedTotal <= 0.08f) {
                continue;
            }

            const float spawnChance = clamp(weightedTotal * (band == TerrainFarTileBand::Near ? 0.85f : (band == TerrainFarTileBand::Mid ? 0.55f : 0.24f)), 0.0f, 0.96f);
            if (terrainPropUnitFloat(baseHash ^ 0x83f1d263u) > spawnChance) {
                continue;
            }

            TerrainPropVariant variant = TerrainPropVariant::Conifer;
            if (band == TerrainFarTileBand::Horizon) {
                variant = terrainPropUnitFloat(baseHash ^ 0x11453c59u) > 0.56f ? TerrainPropVariant::Rock : TerrainPropVariant::Conifer;
            } else {
                float selector = terrainPropUnitFloat(baseHash ^ 0x11453c59u) * (coniferWeight + broadleafWeight + shrubWeight + rockWeight);
                if ((selector -= coniferWeight) <= 0.0f) {
                    variant = TerrainPropVariant::Conifer;
                } else if ((selector -= broadleafWeight) <= 0.0f) {
                    variant = TerrainPropVariant::Broadleaf;
                } else if ((selector -= shrubWeight) <= 0.0f) {
                    variant = TerrainPropVariant::Shrub;
                } else {
                    variant = TerrainPropVariant::Rock;
                }
            }

            const float yaw = terrainPropUnitFloat(baseHash ^ 0x99cd4d85u) * (2.0f * kPi);
            const float lean = terrainPropSignedFloat(baseHash ^ 0x6e624eb7u) * radians(variant == TerrainPropVariant::Rock ? 8.0f : 5.0f);
            const float sizeJitter = 0.8f + (terrainPropUnitFloat(baseHash ^ 0x57a9bcd1u) * 0.65f);
            const float tintJitter = 0.86f + (terrainPropUnitFloat(baseHash ^ 0x4dca28b9u) * 0.28f);
            const Vec3 position { x, material.surfaceHeight, z };

            if (variant == TerrainPropVariant::Conifer) {
                const Vec3 scale {
                    (band == TerrainFarTileBand::Horizon ? 4.8f : 3.0f) * sizeJitter,
                    (band == TerrainFarTileBand::Horizon ? 8.2f : 5.6f) * sizeJitter,
                    (band == TerrainFarTileBand::Horizon ? 4.8f : 3.0f) * sizeJitter
                };
                placements.push_back(buildTerrainPropPlacement(variant, TerrainPropClass::Blocker, position, scale, { 0.90f, tintJitter, 0.90f }, yaw, lean));
            } else if (variant == TerrainPropVariant::Broadleaf) {
                const Vec3 scale { 3.8f * sizeJitter, 6.0f * sizeJitter, 3.8f * sizeJitter };
                placements.push_back(buildTerrainPropPlacement(variant, TerrainPropClass::Blocker, position, scale, { 0.92f, tintJitter, 0.88f }, yaw, lean));
            } else if (variant == TerrainPropVariant::Shrub) {
                if (band == TerrainFarTileBand::Horizon) {
                    continue;
                }
                const Vec3 scale { 2.2f * sizeJitter, 1.6f * sizeJitter, 2.2f * sizeJitter };
                placements.push_back(buildTerrainPropPlacement(variant, TerrainPropClass::Brush, position, scale, { 0.94f, tintJitter, 0.92f }, yaw, lean));
            } else {
                const Vec3 scale {
                    (band == TerrainFarTileBand::Horizon ? 4.8f : 3.2f) * sizeJitter,
                    (band == TerrainFarTileBand::Horizon ? 3.4f : 2.2f) * sizeJitter,
                    (band == TerrainFarTileBand::Horizon ? 4.2f : 2.8f) * sizeJitter
                };
                placements.push_back(buildTerrainPropPlacement(variant, TerrainPropClass::Blocker, position, scale, { tintJitter, tintJitter, tintJitter }, yaw, lean));
            }
        }
    }

    for (const TerrainPropPlacement& placement : placements) {
        switch (placement.variant) {
        case TerrainPropVariant::Conifer:
            appendConiferPlacement(result.propModel, placement);
            appendPlacementCollider(result.propColliders, TerrainPropClass::Blocker, placement.position + Vec3 { 0.0f, placement.scale.y * 0.92f, 0.0f }, std::max(0.35f, placement.scale.x * 0.18f), placement.scale.y * 0.92f, 0.0f);
            appendPlacementCollider(result.propColliders, TerrainPropClass::Brush, placement.position + Vec3 { 0.0f, placement.scale.y * 2.2f, 0.0f }, std::max(0.6f, placement.scale.x * 0.72f), placement.scale.y * 1.32f, 0.82f);
            break;
        case TerrainPropVariant::Broadleaf:
            appendBroadleafPlacement(result.propModel, placement);
            appendPlacementCollider(result.propColliders, TerrainPropClass::Blocker, placement.position + Vec3 { 0.0f, placement.scale.y * 0.82f, 0.0f }, std::max(0.34f, placement.scale.x * 0.16f), placement.scale.y * 0.82f, 0.0f);
            appendPlacementCollider(result.propColliders, TerrainPropClass::Brush, placement.position + Vec3 { 0.0f, placement.scale.y * 1.58f, 0.0f }, std::max(0.7f, placement.scale.x * 0.84f), placement.scale.y * 0.96f, 0.78f);
            break;
        case TerrainPropVariant::Shrub:
            appendShrubPlacement(result.propModel, placement);
            appendPlacementCollider(result.propColliders, TerrainPropClass::Brush, placement.position + Vec3 { 0.0f, placement.scale.y * 0.42f, 0.0f }, std::max(0.45f, placement.scale.x * 0.42f), placement.scale.y * 0.42f, 0.88f);
            break;
        case TerrainPropVariant::Rock:
            appendRockPlacement(result.propModel, placement);
            appendPlacementCollider(result.propColliders, TerrainPropClass::Blocker, placement.position + Vec3 { 0.0f, placement.scale.y * 0.26f, 0.0f }, std::max(0.48f, placement.scale.x * 0.34f), placement.scale.y * 0.26f, 0.0f);
            break;
        default:
            break;
        }
    }

    return result;
}

void configureTerrainWaterMaterial(Model& waterModel)
{
    if (waterModel.faces.empty()) {
        waterModel.materials.clear();
        return;
    }

    Material waterMaterial {};
    waterMaterial.name = "terrain_water";
    waterMaterial.baseColorFactor = { 0.78f, 0.86f, 1.0f, 0.72f };
    waterMaterial.alphaMode = AlphaMode::Blend;
    waterMaterial.alphaCutoff = 0.01f;
    waterMaterial.doubleSided = true;
    waterModel.materials = { waterMaterial };
    for (Face& face : waterModel.faces) {
        face.materialIndex = 0;
    }
}

TerrainChunkData buildTerrainChunkSourceData(
    const TerrainChunkKey& key,
    const TerrainPatchBounds& bounds,
    float cellSize,
    const TerrainFieldContext& terrainContext)
{
    const TerrainParams& params = terrainContext.params;
    const float spanX = std::max(1.0f, bounds.x1 - bounds.x0);
    const float spanZ = std::max(1.0f, bounds.z1 - bounds.z0);
    const float requestedStep = std::max(1.0f, sanitize(cellSize, params.lod1CellSize));
    const float stepX = terrainPatchAxisStep(params, requestedStep, spanX);
    const float stepZ = terrainPatchAxisStep(params, requestedStep, spanZ);
    const int nx = std::max(2, static_cast<int>(std::floor(spanX / stepX)));
    const int nz = std::max(2, static_cast<int>(std::floor(spanZ / stepZ)));
    const float xStep = spanX / static_cast<float>(nx);
    const float zStep = spanZ / static_cast<float>(nz);

    TerrainChunkData data;
    data.key = key;
    data.bounds = bounds;
    data.cellSize = cellSize;
    data.gridWidth = nx + 1;
    data.gridHeight = nz + 1;
    const std::size_t valueCount = static_cast<std::size_t>(data.gridWidth * data.gridHeight);
    data.surfaceHeights.reserve(valueCount);
    data.snowWeights.reserve(valueCount);
    data.waterHeights.reserve(valueCount);
    data.waterWeights.reserve(valueCount);

    for (int iz = 0; iz <= nz; ++iz) {
        const float z = bounds.z0 + (static_cast<float>(iz) * zStep);
        for (int ix = 0; ix <= nx; ++ix) {
            const float x = bounds.x0 + (static_cast<float>(ix) * xStep);
            const TerrainMaterialSample material = sampleTerrainMaterial(x, 0.0f, z, terrainContext);
            data.surfaceHeights.push_back(material.surfaceHeight);
            data.snowWeights.push_back(material.snowWeight);
            data.waterHeights.push_back(material.waterHeight);
            data.waterWeights.push_back(clamp((material.waterHeight - material.surfaceHeight + 0.12f) / std::max(0.1f, params.shorelineBand), 0.0f, 1.0f));
        }
    }

    return data;
}

CompiledTerrainChunk loadOrBuildTerrainChunk(
    const TerrainChunkKey& key,
    const TerrainPatchBounds& bounds,
    float cellSize,
    TerrainFarTileBand band,
    const std::optional<TerrainChunkBakeCache>& bakeCache,
    const TerrainFieldContext& terrainContext)
{
    if (bakeCache.has_value()) {
        CompiledTerrainChunk cachedChunk;
        if (bakeCache->load(key, cachedChunk)) {
            cachedChunk.terrainModel.assetKey = terrainChunkAssetKey(key, "ground");
            cachedChunk.waterModel.assetKey = terrainChunkAssetKey(key, "water");
            cachedChunk.propModel.assetKey = terrainChunkAssetKey(key, "props");
            return cachedChunk;
        }
    }

    CompiledTerrainChunk chunk;
    chunk.key = key;
    chunk.sourceData = buildTerrainChunkSourceData(key, bounds, cellSize, terrainContext);

    if (band == TerrainFarTileBand::Near && !terrainContext.params.surfaceOnlyMeshing) {
        const float overlap = cellSize;
        const TerrainVolumeBounds tileVolume {
            bounds.x0 - overlap,
            bounds.x1 + overlap,
            terrainContext.params.minY,
            terrainContext.params.maxY,
            bounds.z0 - overlap,
            bounds.z1 + overlap
        };
        chunk.terrainModel = buildVolumetricTerrainPatch(terrainContext, tileVolume, cellSize);
    } else {
        TerrainFieldContext tileContext = terrainContext;
        if (band == TerrainFarTileBand::Near) {
            tileContext.params.enableSkirts = false;
        }
        chunk.terrainModel = buildSurfaceTerrainPatch(tileContext, bounds, cellSize);
    }

    appendTerrainWater(chunk.waterModel, terrainContext, bounds, std::max(cellSize, terrainContext.params.lod1CellSize));
    configureTerrainWaterMaterial(chunk.waterModel);
    TerrainTileDecorationResult decoration = buildTerrainTileDecoration(key, bounds, band, terrainContext);
    chunk.propModel = std::move(decoration.propModel);
    chunk.propColliders = std::move(decoration.propColliders);
    chunk.terrainModel.assetKey = terrainChunkAssetKey(key, "ground");
    chunk.waterModel.assetKey = terrainChunkAssetKey(key, "water");
    chunk.propModel.assetKey = terrainChunkAssetKey(key, "props");

    if (bakeCache.has_value()) {
        bakeCache->save(chunk, nullptr);
    }
    return chunk;
}

TerrainFarTile* findTerrainTile(std::vector<TerrainFarTile>& tiles, TerrainFarTileBand band, int tileX, int tileZ)
{
    for (TerrainFarTile& tile : tiles) {
        if (tile.band == band && tile.tileX == tileX && tile.tileZ == tileZ) {
            return &tile;
        }
    }
    return nullptr;
}

float tileCenterDistanceSquared(const Vec3& center, float tileSize, int tileX, int tileZ)
{
    const float sampleX = (static_cast<float>(tileX) + 0.5f) * tileSize;
    const float sampleZ = (static_cast<float>(tileZ) + 0.5f) * tileSize;
    const float dx = sampleX - center.x;
    const float dz = sampleZ - center.z;
    return (dx * dx) + (dz * dz);
}

bool tileInsideSquare(int tileX, int tileZ, float tileSize, float x0, float x1, float z0, float z1)
{
    const float tileMinX = static_cast<float>(tileX) * tileSize;
    const float tileMaxX = tileMinX + tileSize;
    const float tileMinZ = static_cast<float>(tileZ) * tileSize;
    const float tileMaxZ = tileMinZ + tileSize;
    constexpr float epsilon = 1.0e-4f;
    return tileMinX >= x0 - epsilon &&
        tileMaxX <= x1 + epsilon &&
        tileMinZ >= z0 - epsilon &&
        tileMaxZ <= z1 + epsilon;
}

float terrainTileSizeForBand(const TerrainVisualCache& terrainCache, TerrainFarTileBand band)
{
    switch (band) {
    case TerrainFarTileBand::Near:
        return terrainCache.lod0TileSize;
    case TerrainFarTileBand::Mid:
        return terrainCache.lod1TileSize;
    case TerrainFarTileBand::Horizon:
    default:
        return terrainCache.lod2TileSize;
    }
}

bool tileIntersectsRange(const TerrainVisualCache& terrainCache, const TerrainFarTile& tile, const Vec3& position, float range)
{
    const float tileSize = terrainTileSizeForBand(terrainCache, tile.band);
    const float tileMinX = static_cast<float>(tile.tileX) * tileSize;
    const float tileMinZ = static_cast<float>(tile.tileZ) * tileSize;
    const float tileMaxX = tileMinX + tileSize;
    const float tileMaxZ = tileMinZ + tileSize;
    return position.x + range >= tileMinX &&
        position.x - range <= tileMaxX &&
        position.z + range >= tileMinZ &&
        position.z - range <= tileMaxZ;
}

template <typename Callback>
void forEachNearbyPropCollider(const TerrainVisualCache& terrainCache, const Vec3& position, float range, Callback&& callback)
{
    for (const TerrainFarTile& tile : terrainCache.nearTiles) {
        if (!tile.active || tile.propColliders.empty() || !tileIntersectsRange(terrainCache, tile, position, range)) {
            continue;
        }
        for (const TerrainPropCollider& collider : tile.propColliders) {
            const float dx = position.x - collider.center.x;
            const float dz = position.z - collider.center.z;
            const float maxRange = range + collider.radius;
            if ((dx * dx) + (dz * dz) > (maxRange * maxRange)) {
                continue;
            }
            callback(collider);
        }
    }
}

bool actorVerticallyOverlapsCollider(float actorMinY, float actorMaxY, const TerrainPropCollider& collider)
{
    const float colliderMinY = collider.center.y - collider.halfHeight;
    const float colliderMaxY = collider.center.y + collider.halfHeight;
    return actorMaxY >= colliderMinY && actorMinY <= colliderMaxY;
}

float computeBrushContactAmount(
    const TerrainVisualCache& terrainCache,
    const Vec3& position,
    float actorRadius,
    float actorMinY,
    float actorMaxY)
{
    float brushAmount = 0.0f;
    forEachNearbyPropCollider(terrainCache, position, actorRadius + 6.0f, [&](const TerrainPropCollider& collider) {
        if (collider.propClass != TerrainPropClass::Brush || !actorVerticallyOverlapsCollider(actorMinY, actorMaxY, collider)) {
            return;
        }
        const float dx = position.x - collider.center.x;
        const float dz = position.z - collider.center.z;
        const float horizontalDistance = std::sqrt((dx * dx) + (dz * dz));
        const float threshold = collider.radius + actorRadius;
        if (horizontalDistance >= threshold) {
            return;
        }
        const float overlap = 1.0f - clamp(horizontalDistance / std::max(0.1f, threshold), 0.0f, 1.0f);
        brushAmount = std::max(brushAmount, overlap * std::max(0.2f, collider.softness));
    });
    return clamp(brushAmount, 0.0f, 1.0f);
}

bool detectFlightPropCollision(
    const TerrainVisualCache& terrainCache,
    const TerrainDecorationSettings& decoration,
    const FlightState& plane,
    int tick,
    FlightCrashEvent& outCrash)
{
    if (!decoration.collisionEnabled) {
        return false;
    }

    const float radius = std::max(0.3f, plane.collisionRadius);
    const float actorMinY = plane.pos.y - radius;
    const float actorMaxY = plane.pos.y + radius;
    bool collided = false;
    float bestDistanceSq = std::numeric_limits<float>::infinity();
    forEachNearbyPropCollider(terrainCache, plane.pos, radius + 3.0f, [&](const TerrainPropCollider& collider) {
        if (collided && bestDistanceSq <= 1.0e-4f) {
            return;
        }
        if (collider.propClass != TerrainPropClass::Blocker || !actorVerticallyOverlapsCollider(actorMinY, actorMaxY, collider)) {
            return;
        }
        const float dx = plane.pos.x - collider.center.x;
        const float dz = plane.pos.z - collider.center.z;
        const float limit = radius + collider.radius;
        const float distSq = (dx * dx) + (dz * dz);
        if (distSq > (limit * limit)) {
            return;
        }

        const float dist = std::sqrt(std::max(1.0e-6f, distSq));
        Vec3 normal { dx / dist, 0.0f, dz / dist };
        if (distSq <= 1.0e-6f) {
            normal = normalize({ -plane.flightVel.x, 0.0f, -plane.flightVel.z }, { 0.0f, 1.0f, 0.0f });
        }
        const float contactY = clamp(plane.pos.y, collider.center.y - collider.halfHeight, collider.center.y + collider.halfHeight);
        outCrash = {
            plane.pos - (normal * radius),
            normal,
            plane.flightVel,
            radius,
            std::max(0.0f, dot(-plane.flightVel, normal)),
            length(plane.flightVel),
            length(plane.flightAngVel),
            tick,
            FlightCrashCause::PropBlocker
        };
        outCrash.position.y = contactY;
        collided = true;
        bestDistanceSq = distSq;
    });
    return collided;
}

void resolveWalkingPropCollisions(
    const TerrainVisualCache& terrainCache,
    const TerrainDecorationSettings& decoration,
    FlightState& actor)
{
    if (!decoration.collisionEnabled) {
        return;
    }

    const float actorMinY = actor.pos.y - kWalkingHalfHeight;
    const float actorMaxY = actor.pos.y + 0.2f;
    for (int iteration = 0; iteration < 3; ++iteration) {
        bool pushed = false;
        forEachNearbyPropCollider(terrainCache, actor.pos, kWalkingCollisionRadius + 3.0f, [&](const TerrainPropCollider& collider) {
            if (pushed || collider.propClass != TerrainPropClass::Blocker || !actorVerticallyOverlapsCollider(actorMinY, actorMaxY, collider)) {
                return;
            }
            const float dx = actor.pos.x - collider.center.x;
            const float dz = actor.pos.z - collider.center.z;
            const float combinedRadius = collider.radius + kWalkingCollisionRadius;
            const float distSq = (dx * dx) + (dz * dz);
            if (distSq >= (combinedRadius * combinedRadius)) {
                return;
            }

            const float dist = std::sqrt(std::max(1.0e-6f, distSq));
            Vec3 normal { dx / dist, 0.0f, dz / dist };
            if (distSq <= 1.0e-6f) {
                normal = normalize(forwardFromRotation(actor.rot), { 0.0f, 0.0f, 1.0f });
                normal.y = 0.0f;
            }
            actor.pos += normal * ((combinedRadius - dist) + 1.0e-3f);
            const float intoSurface = (actor.vel.x * normal.x) + (actor.vel.z * normal.z);
            if (intoSurface < 0.0f) {
                actor.vel.x -= normal.x * intoSurface;
                actor.vel.z -= normal.z * intoSurface;
            }
            pushed = true;
        });
        if (!pushed) {
            break;
        }
    }
}

float terrainCellSizeForDetail(const TerrainFieldContext& terrainContext, TerrainFarTileDetail detail)
{
    switch (detail) {
    case TerrainFarTileDetail::Lod0:
        return terrainContext.params.lod0CellSize;
    case TerrainFarTileDetail::Lod1:
        return terrainContext.params.lod1CellSize;
    case TerrainFarTileDetail::Lod2:
    default:
        return terrainContext.params.lod2CellSize;
    }
}

const char* terrainBandAssetTag(TerrainFarTileBand band)
{
    switch (band) {
    case TerrainFarTileBand::Near:
        return "near";
    case TerrainFarTileBand::Mid:
        return "mid";
    case TerrainFarTileBand::Horizon:
    default:
        return "horizon";
    }
}

const char* terrainDetailAssetTag(TerrainFarTileDetail detail)
{
    switch (detail) {
    case TerrainFarTileDetail::Lod0:
        return "lod0";
    case TerrainFarTileDetail::Lod1:
        return "lod1";
    case TerrainFarTileDetail::Lod2:
    default:
        return "lod2";
    }
}

float terrainTilePriorityBias(TerrainFarTileBand band)
{
    switch (band) {
    case TerrainFarTileBand::Near:
        return 0.0f;
    case TerrainFarTileBand::Mid:
        return 1.0e8f;
    case TerrainFarTileBand::Horizon:
    default:
        return 2.0e8f;
    }
}

CompiledTerrainChunk buildTerrainTileChunk(
    TerrainFarTileBand band,
    TerrainFarTileDetail detail,
    int tileX,
    int tileZ,
    const TerrainVisualCache& terrainCache,
    const TerrainFieldContext& terrainContext,
    const std::optional<TerrainChunkBakeCache>& bakeCache,
    std::string_view terrainWorldId,
    std::uint64_t paramsSignature,
    std::uint64_t sourceSignature)
{
    const float tileSize = terrainTileSizeForBand(terrainCache, band);
    const float tileMinX = static_cast<float>(tileX) * tileSize;
    const float tileMinZ = static_cast<float>(tileZ) * tileSize;
    const float cellSize = terrainCellSizeForDetail(terrainContext, detail);
    const TerrainPatchBounds tileBounds {
        tileMinX,
        tileMinX + tileSize,
        tileMinZ,
        tileMinZ + tileSize,
        false,
        0.0f,
        0.0f,
        0.0f,
        0.0f
    };

    TerrainChunkKey key;
    key.worldId = terrainWorldId.empty() ? std::string("default") : std::string(terrainWorldId);
    key.seed = std::max(1, terrainContext.params.seed);
    key.generatorVersion = std::max(1, terrainContext.params.generatorVersion);
    key.band = static_cast<int>(band);
    key.detail = static_cast<int>(detail);
    key.tileX = tileX;
    key.tileZ = tileZ;
    key.paramsSignature = paramsSignature;
    key.sourceSignature = sourceSignature;
    return loadOrBuildTerrainChunk(key, tileBounds, cellSize, band, bakeCache, terrainContext);
}

int tileIndexBegin(float minCoord, float tileSize)
{
    return static_cast<int>(std::floor((minCoord / tileSize) + 1.0e-4f));
}

int tileIndexEnd(float maxCoord, float tileSize)
{
    return static_cast<int>(std::ceil((maxCoord / tileSize) - 1.0e-4f));
}

struct TerrainTileRequest {
    TerrainFarTileBand band = TerrainFarTileBand::Horizon;
    TerrainFarTileDetail detail = TerrainFarTileDetail::Lod2;
    int tileX = 0;
    int tileZ = 0;
    std::uint64_t paramsSignature = 0u;
    std::uint64_t sourceSignature = 0u;
    float priority = 0.0f;
};

const TerrainVisualCache& ensureTerrainVisualCache(
    TerrainVisualCache& terrainCache,
    const Vec3& center,
    const Vec3& velocity,
    const TerrainFieldContext& terrainContext,
    const std::optional<TerrainChunkBakeCache>& bakeCache,
    std::string_view terrainWorldId)
{
    const TerrainParams& params = terrainContext.params;
    const std::uint64_t paramsSignature = terrainParamsSignature(params);
    const float lod0TileSize = computeLod0TerrainTileSize(params);
    const float lod1TileSize = computeLod1TerrainTileSize(params);
    const float lod2TileSize = computeLod2TerrainTileSize(params);
    const float nearHalfExtent = computeNearHalfExtent(params, lod0TileSize);
    const float midHalfExtent = computeMidHalfExtent(params, nearHalfExtent, lod2TileSize);
    const float farHalfExtent = computeFarHalfExtent(params, midHalfExtent, lod2TileSize);
    const float nearAnchorX = snapToSpacing(center.x, lod0TileSize);
    const float nearAnchorZ = snapToSpacing(center.z, lod0TileSize);
    const float farAnchorX = snapToSpacing(center.x, lod2TileSize);
    const float farAnchorZ = snapToSpacing(center.z, lod2TileSize);
    const float leadSeconds = 0.75f;
    const Vec3 prefetchCenter {
        center.x + clamp(velocity.x * leadSeconds, -(lod0TileSize * 8.0f), lod0TileSize * 8.0f),
        center.y,
        center.z + clamp(velocity.z * leadSeconds, -(lod0TileSize * 8.0f), lod0TileSize * 8.0f)
    };
    const auto tileSourceSignature = [&](TerrainFarTileBand band, int tileX, int tileZ) {
        const float tileSize = terrainTileSizeForBand(terrainCache, band);
        const float x0 = static_cast<float>(tileX) * tileSize;
        const float z0 = static_cast<float>(tileZ) * tileSize;
        return terrainSourceSignature(terrainContext, x0, z0, x0 + tileSize, z0 + tileSize);
    };

    const bool paramsChanged =
        !terrainCache.valid ||
        !terrainParamsEquivalent(terrainCache.paramsSnapshot, params) ||
        std::fabs(terrainCache.lod0TileSize - lod0TileSize) > 1.0e-4f ||
        std::fabs(terrainCache.lod1TileSize - lod1TileSize) > 1.0e-4f ||
        std::fabs(terrainCache.lod2TileSize - lod2TileSize) > 1.0e-4f ||
        std::fabs(terrainCache.nearHalfExtent - nearHalfExtent) > 1.0e-4f ||
        std::fabs(terrainCache.midHalfExtent - midHalfExtent) > 1.0e-4f ||
        std::fabs(terrainCache.farHalfExtent - farHalfExtent) > 1.0e-4f;

    if (paramsChanged) {
        terrainCache.nearTiles.clear();
        terrainCache.farTiles.clear();
    }

    terrainCache.valid = true;
    terrainCache.nearAnchorX = nearAnchorX;
    terrainCache.nearAnchorZ = nearAnchorZ;
    terrainCache.farAnchorX = farAnchorX;
    terrainCache.farAnchorZ = farAnchorZ;
    terrainCache.nearHalfExtent = nearHalfExtent;
    terrainCache.midHalfExtent = midHalfExtent;
    terrainCache.farHalfExtent = farHalfExtent;
    terrainCache.lod0TileSize = lod0TileSize;
    terrainCache.lod1TileSize = lod1TileSize;
    terrainCache.lod2TileSize = lod2TileSize;
    terrainCache.paramsSnapshot = params;

    for (TerrainFarTile& tile : terrainCache.nearTiles) {
        tile.active = false;
    }
    for (TerrainFarTile& tile : terrainCache.farTiles) {
        tile.active = false;
    }

    const float nearX0 = nearAnchorX - nearHalfExtent;
    const float nearX1 = nearAnchorX + nearHalfExtent;
    const float nearZ0 = nearAnchorZ - nearHalfExtent;
    const float nearZ1 = nearAnchorZ + nearHalfExtent;
    const float midX0 = farAnchorX - midHalfExtent;
    const float midX1 = farAnchorX + midHalfExtent;
    const float midZ0 = farAnchorZ - midHalfExtent;
    const float midZ1 = farAnchorZ + midHalfExtent;
    const float farX0 = farAnchorX - farHalfExtent;
    const float farX1 = farAnchorX + farHalfExtent;
    const float farZ0 = farAnchorZ - farHalfExtent;
    const float farZ1 = farAnchorZ + farHalfExtent;

    std::vector<TerrainTileRequest> missingTiles;
    std::vector<TerrainTileRequest> upgradeTiles;

    for (int tileZ = tileIndexBegin(nearZ0, lod0TileSize); tileZ < tileIndexEnd(nearZ1, lod0TileSize); ++tileZ) {
        for (int tileX = tileIndexBegin(nearX0, lod0TileSize); tileX < tileIndexEnd(nearX1, lod0TileSize); ++tileX) {
            const std::uint64_t sourceSignature = tileSourceSignature(TerrainFarTileBand::Near, tileX, tileZ);
            TerrainFarTile* tile = findTerrainTile(terrainCache.nearTiles, TerrainFarTileBand::Near, tileX, tileZ);
            if (tile != nullptr) {
                tile->active = true;
                if (tile->detail != TerrainFarTileDetail::Lod0 ||
                    tile->paramsSignature != paramsSignature ||
                    tile->sourceSignature != sourceSignature) {
                    missingTiles.push_back({
                        TerrainFarTileBand::Near,
                        TerrainFarTileDetail::Lod0,
                        tileX,
                        tileZ,
                        paramsSignature,
                        sourceSignature,
                        tileCenterDistanceSquared(prefetchCenter, lod0TileSize, tileX, tileZ) +
                            terrainTilePriorityBias(TerrainFarTileBand::Near)
                    });
                }
                continue;
            }

            missingTiles.push_back({
                TerrainFarTileBand::Near,
                TerrainFarTileDetail::Lod0,
                tileX,
                tileZ,
                paramsSignature,
                sourceSignature,
                tileCenterDistanceSquared(prefetchCenter, lod0TileSize, tileX, tileZ) +
                    terrainTilePriorityBias(TerrainFarTileBand::Near)
            });
        }
    }

    for (int tileZ = tileIndexBegin(midZ0, lod1TileSize); tileZ < tileIndexEnd(midZ1, lod1TileSize); ++tileZ) {
        for (int tileX = tileIndexBegin(midX0, lod1TileSize); tileX < tileIndexEnd(midX1, lod1TileSize); ++tileX) {
            if (tileInsideSquare(tileX, tileZ, lod1TileSize, nearX0, nearX1, nearZ0, nearZ1)) {
                continue;
            }

            const std::uint64_t sourceSignature = tileSourceSignature(TerrainFarTileBand::Mid, tileX, tileZ);
            TerrainFarTile* tile = findTerrainTile(terrainCache.farTiles, TerrainFarTileBand::Mid, tileX, tileZ);
            if (tile != nullptr) {
                tile->active = true;
                if (tile->paramsSignature != paramsSignature || tile->sourceSignature != sourceSignature) {
                    missingTiles.push_back({
                        TerrainFarTileBand::Mid,
                        tile->detail,
                        tileX,
                        tileZ,
                        paramsSignature,
                        sourceSignature,
                        tileCenterDistanceSquared(prefetchCenter, lod1TileSize, tileX, tileZ) +
                            terrainTilePriorityBias(TerrainFarTileBand::Mid)
                    });
                } else if (tile->detail == TerrainFarTileDetail::Lod2) {
                    upgradeTiles.push_back({
                        TerrainFarTileBand::Mid,
                        TerrainFarTileDetail::Lod1,
                        tileX,
                        tileZ,
                        paramsSignature,
                        sourceSignature,
                        tileCenterDistanceSquared(prefetchCenter, lod1TileSize, tileX, tileZ)
                    });
                }
                continue;
            }

            missingTiles.push_back({
                TerrainFarTileBand::Mid,
                TerrainFarTileDetail::Lod2,
                tileX,
                tileZ,
                paramsSignature,
                sourceSignature,
                tileCenterDistanceSquared(prefetchCenter, lod1TileSize, tileX, tileZ) +
                    terrainTilePriorityBias(TerrainFarTileBand::Mid)
            });
        }
    }

    for (int tileZ = tileIndexBegin(farZ0, lod2TileSize); tileZ < tileIndexEnd(farZ1, lod2TileSize); ++tileZ) {
        for (int tileX = tileIndexBegin(farX0, lod2TileSize); tileX < tileIndexEnd(farX1, lod2TileSize); ++tileX) {
            if (tileInsideSquare(tileX, tileZ, lod2TileSize, midX0, midX1, midZ0, midZ1)) {
                continue;
            }

            const std::uint64_t sourceSignature = tileSourceSignature(TerrainFarTileBand::Horizon, tileX, tileZ);
            TerrainFarTile* tile = findTerrainTile(terrainCache.farTiles, TerrainFarTileBand::Horizon, tileX, tileZ);
            if (tile != nullptr) {
                tile->active = true;
                if (tile->paramsSignature != paramsSignature || tile->sourceSignature != sourceSignature) {
                    missingTiles.push_back({
                        TerrainFarTileBand::Horizon,
                        TerrainFarTileDetail::Lod2,
                        tileX,
                        tileZ,
                        paramsSignature,
                        sourceSignature,
                        tileCenterDistanceSquared(prefetchCenter, lod2TileSize, tileX, tileZ) +
                            terrainTilePriorityBias(TerrainFarTileBand::Horizon)
                    });
                }
                continue;
            }

            missingTiles.push_back({
                TerrainFarTileBand::Horizon,
                TerrainFarTileDetail::Lod2,
                tileX,
                tileZ,
                paramsSignature,
                sourceSignature,
                tileCenterDistanceSquared(prefetchCenter, lod2TileSize, tileX, tileZ) +
                    terrainTilePriorityBias(TerrainFarTileBand::Horizon)
            });
        }
    }

    std::sort(
        missingTiles.begin(),
        missingTiles.end(),
        [](const TerrainTileRequest& lhs, const TerrainTileRequest& rhs) {
            return lhs.priority < rhs.priority;
        });
    std::sort(
        upgradeTiles.begin(),
        upgradeTiles.end(),
        [](const TerrainTileRequest& lhs, const TerrainTileRequest& rhs) {
            return lhs.priority < rhs.priority;
        });

    const int missingBudget = std::max(8, params.meshBuildBudget * 4);
    const int upgradeBudget = std::max(2, params.meshBuildBudget);

    int builtMissing = 0;
    for (const TerrainTileRequest& request : missingTiles) {
        if (builtMissing >= missingBudget) {
            break;
        }

        std::vector<TerrainFarTile>& tileList =
            request.band == TerrainFarTileBand::Near ? terrainCache.nearTiles : terrainCache.farTiles;
        TerrainFarTile* existingTile = findTerrainTile(tileList, request.band, request.tileX, request.tileZ);
        const CompiledTerrainChunk compiledChunk = buildTerrainTileChunk(
            request.band,
            request.detail,
            request.tileX,
            request.tileZ,
            terrainCache,
            terrainContext,
            bakeCache,
            terrainWorldId,
            request.paramsSignature,
            request.sourceSignature);

        if (existingTile == nullptr) {
            TerrainFarTile tile;
            tile.band = request.band;
            tile.detail = request.detail;
            tile.tileX = request.tileX;
            tile.tileZ = request.tileZ;
            tile.paramsSignature = request.paramsSignature;
            tile.sourceSignature = request.sourceSignature;
            tile.active = true;
            tile.terrainModel = compiledChunk.terrainModel;
            tile.waterModel = compiledChunk.waterModel;
            tile.propModel = compiledChunk.propModel;
            tile.propColliders = compiledChunk.propColliders;
            tileList.push_back(std::move(tile));
        } else {
            existingTile->detail = request.detail;
            existingTile->paramsSignature = request.paramsSignature;
            existingTile->sourceSignature = request.sourceSignature;
            existingTile->active = true;
            existingTile->terrainModel = compiledChunk.terrainModel;
            existingTile->waterModel = compiledChunk.waterModel;
            existingTile->propModel = compiledChunk.propModel;
            existingTile->propColliders = compiledChunk.propColliders;
        }
        ++builtMissing;
    }

    int upgradedTiles = 0;
    for (const TerrainTileRequest& request : upgradeTiles) {
        if (upgradedTiles >= upgradeBudget) {
            break;
        }

        TerrainFarTile* tile = findTerrainTile(terrainCache.farTiles, request.band, request.tileX, request.tileZ);
        if (tile == nullptr || tile->detail == TerrainFarTileDetail::Lod1) {
            continue;
        }

        const CompiledTerrainChunk compiledChunk = buildTerrainTileChunk(
            tile->band,
            request.detail,
            tile->tileX,
            tile->tileZ,
            terrainCache,
            terrainContext,
            bakeCache,
            terrainWorldId,
            request.paramsSignature,
            request.sourceSignature);
        tile->detail = request.detail;
        tile->paramsSignature = request.paramsSignature;
        tile->sourceSignature = request.sourceSignature;
        tile->terrainModel = compiledChunk.terrainModel;
        tile->waterModel = compiledChunk.waterModel;
        tile->propModel = compiledChunk.propModel;
        tile->propColliders = compiledChunk.propColliders;
        ++upgradedTiles;
    }

    terrainCache.nearTiles.erase(
        std::remove_if(
            terrainCache.nearTiles.begin(),
            terrainCache.nearTiles.end(),
            [](const TerrainFarTile& tile) {
                return !tile.active;
            }),
        terrainCache.nearTiles.end());

    terrainCache.farTiles.erase(
        std::remove_if(
            terrainCache.farTiles.begin(),
            terrainCache.farTiles.end(),
            [](const TerrainFarTile& tile) {
                return !tile.active;
            }),
        terrainCache.farTiles.end());

    return terrainCache;
}

PauseLayout buildPauseLayout(int width, int height)
{
    PauseLayout layout;
    layout.panelW = std::max(240.0f, std::min(static_cast<float>(width) - 12.0f, 920.0f));
    layout.panelH = std::max(220.0f, std::min(static_cast<float>(height) - 12.0f, 560.0f));
    layout.panelX = (static_cast<float>(width) - layout.panelW) * 0.5f;
    layout.panelY = (static_cast<float>(height) - layout.panelH) * 0.5f;
    layout.tabY = layout.panelY + 72.0f;
    constexpr float tabGap = 8.0f;
    layout.tabW = (layout.panelW - 48.0f - (tabGap * static_cast<float>(kPauseTabs.size() - 1))) / static_cast<float>(kPauseTabs.size());
    layout.tabH = 28.0f;
    layout.subTabY = layout.tabY + 40.0f;
    layout.contentX = layout.panelX + 30.0f;
    layout.contentY = layout.tabY + 48.0f;
    const float availableContentWidth = layout.panelW - 60.0f;
    if (availableContentWidth > 420.0f) {
        layout.previewW = std::clamp(availableContentWidth * 0.34f, 180.0f, 280.0f);
    } else {
        layout.previewW = std::max(0.0f, availableContentWidth - 230.0f);
    }
    layout.previewH = std::max(0.0f, layout.panelH - 170.0f);
    layout.previewX = layout.panelX + layout.panelW - layout.previewW - 30.0f;
    layout.previewY = layout.contentY + 40.0f;
    return layout;
}

bool pointInRect(float x, float y, const RectF& rect)
{
    return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
}

const char* settingsSubTabLabel(SettingsSubTab subTab)
{
    switch (subTab) {
    case SettingsSubTab::Graphics:
        return "Graphics";
    case SettingsSubTab::Camera:
        return "Camera";
    case SettingsSubTab::Sound:
        return "Sound";
    case SettingsSubTab::Flight:
        return "Flight";
    case SettingsSubTab::Terrain:
        return "Terrain";
    case SettingsSubTab::Lighting:
        return "Lighting";
    case SettingsSubTab::Online:
        return "Online";
    default:
        return "";
    }
}

const char* characterRoleLabel(CharacterSubTab role)
{
    switch (role) {
    case CharacterSubTab::Plane:
        return "Plane";
    case CharacterSubTab::Player:
        return "Player";
    default:
        return "";
    }
}

PlaneVisualState& visualForRole(CharacterSubTab role, PlaneVisualState& planeVisual, PlaneVisualState& walkingVisual)
{
    return role == CharacterSubTab::Player ? walkingVisual : planeVisual;
}

const PlaneVisualState& visualForRole(CharacterSubTab role, const PlaneVisualState& planeVisual, const PlaneVisualState& walkingVisual)
{
    return role == CharacterSubTab::Player ? walkingVisual : planeVisual;
}

CharacterSubTab activeRoleForTab(const PauseState& pauseState, PauseTab tab)
{
    return tab == PauseTab::Paint ? pauseState.paintSubTab : pauseState.charactersSubTab;
}

void setActiveRoleForTab(PauseState& pauseState, PauseTab tab, CharacterSubTab role)
{
    if (tab == PauseTab::Paint) {
        pauseState.paintSubTab = role;
    } else if (tab == PauseTab::Characters) {
        pauseState.charactersSubTab = role;
    }
}

int settingsSubTabItemCount(SettingsSubTab subTab)
{
    switch (subTab) {
    case SettingsSubTab::Graphics:
        return kGraphicsSettingCount;
    case SettingsSubTab::Camera:
        return kCameraSettingCount;
    case SettingsSubTab::Sound:
        return kSoundSettingCount;
    case SettingsSubTab::Flight:
        return 13;
    case SettingsSubTab::Terrain:
        return kTerrainSettingCount;
    case SettingsSubTab::Lighting:
        return kLightingSettingCount;
    case SettingsSubTab::Online:
        return kOnlineSettingCount;
    default:
        return 0;
    }
}

int characterItemCount(std::size_t assetCount)
{
    return kCharacterAssetListStart + static_cast<int>(assetCount);
}

float pauseContentWidth(const PauseLayout& layout, PauseTab tab)
{
    const float fullWidth = layout.panelW - 60.0f;
    if ((tab == PauseTab::Characters || tab == PauseTab::Paint) && layout.previewW > 0.0f) {
        return std::max(220.0f, layout.previewX - layout.contentX - 18.0f);
    }
    return fullWidth;
}

RectF pauseSubTabRect(const PauseLayout& layout, PauseTab tab, int index, int count)
{
    if (count <= 0) {
        return {};
    }

    constexpr float gap = 8.0f;
    const float rowWidth = pauseContentWidth(layout, tab);
    const float width = (rowWidth - (gap * static_cast<float>(count - 1))) / static_cast<float>(count);
    return {
        layout.contentX + static_cast<float>(index) * (width + gap),
        layout.subTabY,
        width,
        24.0f
    };
}

int hitPauseSubTabIndex(const PauseLayout& layout, PauseTab tab, float mouseX, float mouseY)
{
    int count = 0;
    if (tab == PauseTab::Settings) {
        count = 7;
    } else if (tab == PauseTab::Characters || tab == PauseTab::Paint) {
        count = 2;
    } else {
        return -1;
    }

    for (int index = 0; index < count; ++index) {
        if (pointInRect(mouseX, mouseY, pauseSubTabRect(layout, tab, index, count))) {
            return index;
        }
    }
    return -1;
}

RectF paintCanvasRect(const PauseLayout& layout)
{
    if (layout.previewW <= 0.0f) {
        return {};
    }

    const float size = std::min(layout.previewW, std::max(0.0f, layout.previewH - 78.0f));
    return {
        layout.previewX,
        layout.previewY + 28.0f,
        size,
        size
    };
}

int maxMenuHelpScroll(const PauseLayout& layout)
{
    const float helpBottomY = layout.panelY + layout.panelH - 78.0f;
    const float visibleHeight = std::max(0.0f, helpBottomY - layout.contentY);
    const int visibleLines = std::max(1, static_cast<int>(std::floor(visibleHeight / kHelpLineHeight)));
    return std::max(0, static_cast<int>(kMenuHelpLines.size()) - visibleLines);
}

RectF pauseRowRect(const PauseLayout& layout, const PauseState& pauseState, std::size_t assetCount, int rowIndex)
{
    const float rowX = layout.contentX;
    float rowY = layout.contentY;
    float rowH = 0.0f;
    const float rowW = pauseContentWidth(layout, pauseState.tab);

    switch (pauseState.tab) {
    case PauseTab::Main:
        rowY += static_cast<float>(rowIndex) * 36.0f;
        rowH = 28.0f;
        break;
    case PauseTab::Settings:
        rowY += 38.0f;
        if (pauseState.settingsSubTab == SettingsSubTab::Flight) {
            rowY += static_cast<float>(rowIndex) * 27.0f;
            rowH = 24.0f;
        } else if (pauseState.settingsSubTab == SettingsSubTab::Terrain) {
            const int startIndex = terrainVisibleStartIndex(pauseState.selectedIndex);
            if (rowIndex < startIndex || rowIndex >= std::min(kTerrainSettingCount, startIndex + kTerrainVisibleRows)) {
                return {};
            }
            rowY += static_cast<float>(rowIndex - startIndex) * 27.0f;
            rowH = 24.0f;
        } else {
            rowY += static_cast<float>(rowIndex) * 32.0f;
            rowH = 24.0f;
        }
        break;
    case PauseTab::Characters:
        rowY += 38.0f + static_cast<float>(rowIndex) * 28.0f;
        rowH = 22.0f;
        break;
    case PauseTab::Paint:
        rowY += 38.0f + static_cast<float>(rowIndex) * 28.0f;
        rowH = 22.0f;
        break;
    case PauseTab::Hud:
        rowY += static_cast<float>(rowIndex) * 32.0f;
        rowH = 24.0f;
        break;
    default:
        return {};
    }

    if (pauseState.tab == PauseTab::Characters && rowIndex >= characterItemCount(assetCount)) {
        return {};
    }
    if (pauseState.tab == PauseTab::Paint && rowIndex >= kPaintSettingCount) {
        return {};
    }
    return { rowX, rowY, rowW, rowH };
}

int hitPauseTabIndex(const PauseLayout& layout, float mouseX, float mouseY)
{
    constexpr float tabGap = 8.0f;
    for (std::size_t i = 0; i < kPauseTabs.size(); ++i) {
        const RectF rect {
            layout.panelX + 24.0f + static_cast<float>(i) * (layout.tabW + tabGap),
            layout.tabY,
            layout.tabW,
            layout.tabH
        };
        if (pointInRect(mouseX, mouseY, rect)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int hitPauseItemIndex(const PauseLayout& layout, const PauseState& pauseState, std::size_t assetCount, float mouseX, float mouseY)
{
    const int count = pauseItemCount(pauseState, assetCount);
    for (int index = 0; index < count; ++index) {
        const RectF rect = pauseRowRect(layout, pauseState, assetCount, index);
        if (rect.w <= 0.0f || rect.h <= 0.0f) {
            continue;
        }
        if (pointInRect(mouseX, mouseY, rect)) {
            return index;
        }
    }
    return -1;
}

Quat makeLegacyStlRotationOffset()
{
    return quatNormalize(quatMultiply(
        quatFromAxisAngle({ 0.0f, 1.0f, 0.0f }, -kPi * 0.5f),
        quatFromAxisAngle({ 1.0f, 0.0f, 0.0f }, -kPi * 0.5f)));
}

Quat makePlaneImportRotationOffset(bool usesStlModel)
{
    if (!usesStlModel) {
        return quatIdentity();
    }
    return quatNormalize(makeLegacyStlRotationOffset());
}

std::string toLowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string trimAscii(std::string value)
{
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(start, end - start);
}

bool parseBoolValue(const std::string& value, bool fallback)
{
    const std::string lowered = toLowerAscii(trimAscii(value));
    if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on") {
        return true;
    }
    if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off") {
        return false;
    }
    return fallback;
}

int parseIntValue(const std::string& value, int fallback)
{
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str()) {
        return fallback;
    }
    return static_cast<int>(parsed);
}

float parseFloatValue(const std::string& value, float fallback)
{
    char* end = nullptr;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str()) {
        return fallback;
    }
    return parsed;
}

void clampUiStatePersistentValues(UiState& uiState)
{
    uiState.mapZoomIndex = std::clamp(uiState.mapZoomIndex, 0, static_cast<int>(uiState.mapZoomExtents.size()) - 1);
    uiState.cameraFovDegrees = clamp(uiState.cameraFovDegrees, 60.0f, 120.0f);
    uiState.walkingMoveSpeed = clamp(uiState.walkingMoveSpeed, 2.0f, 30.0f);
    uiState.mouseSensitivity = clamp(uiState.mouseSensitivity, 0.3f, 2.5f);
    uiState.masterVolume = clamp(uiState.masterVolume, 0.0f, 1.5f);
    uiState.engineVolume = clamp(uiState.engineVolume, 0.0f, 1.5f);
    uiState.ambienceVolume = clamp(uiState.ambienceVolume, 0.0f, 1.5f);
}

void clampGraphicsSettings(GraphicsSettings& graphicsSettings)
{
    graphicsSettings.resolutionWidth = std::clamp(graphicsSettings.resolutionWidth, 640, 7680);
    graphicsSettings.resolutionHeight = std::clamp(graphicsSettings.resolutionHeight, 360, 4320);
    graphicsSettings.renderScale = clamp(graphicsSettings.renderScale, 0.5f, 1.5f);
    graphicsSettings.drawDistance = clamp(graphicsSettings.drawDistance, 300.0f, 20000.0f);
}

Vec3 clampLightingColor(Vec3 color, float maxValue = 2.0f)
{
    color.x = clamp(color.x, 0.0f, maxValue);
    color.y = clamp(color.y, 0.0f, maxValue);
    color.z = clamp(color.z, 0.0f, maxValue);
    return color;
}

void clampLightingSettings(LightingSettings& lightingSettings)
{
    lightingSettings.markerDistance = clamp(lightingSettings.markerDistance, 200.0f, 6000.0f);
    lightingSettings.markerSize = clamp(lightingSettings.markerSize, 10.0f, 600.0f);
    lightingSettings.sunYawDegrees = clamp(lightingSettings.sunYawDegrees, -180.0f, 180.0f);
    lightingSettings.sunPitchDegrees = clamp(lightingSettings.sunPitchDegrees, -85.0f, 85.0f);
    lightingSettings.sunIntensity = clamp(lightingSettings.sunIntensity, 0.0f, 8.0f);
    lightingSettings.ambient = clamp(lightingSettings.ambient, 0.0f, 1.5f);
    lightingSettings.shadowSoftness = clamp(lightingSettings.shadowSoftness, 0.4f, 4.0f);
    lightingSettings.shadowDistance = clamp(lightingSettings.shadowDistance, 200.0f, 6000.0f);
    lightingSettings.specularAmbient = clamp(lightingSettings.specularAmbient, 0.0f, 1.5f);
    lightingSettings.bounceStrength = clamp(lightingSettings.bounceStrength, 0.0f, 1.5f);
    lightingSettings.fogDensity = clamp(lightingSettings.fogDensity, 0.0f, 0.01f);
    lightingSettings.fogHeightFalloff = clamp(lightingSettings.fogHeightFalloff, 0.0f, 0.02f);
    lightingSettings.exposureEv = clamp(lightingSettings.exposureEv, -4.0f, 4.0f);
    lightingSettings.turbidity = clamp(lightingSettings.turbidity, 1.0f, 10.0f);
    lightingSettings.sunTint = clampLightingColor(lightingSettings.sunTint, 2.0f);
    lightingSettings.skyTint = clampLightingColor(lightingSettings.skyTint, 2.0f);
    lightingSettings.groundTint = clampLightingColor(lightingSettings.groundTint, 2.0f);
    lightingSettings.fogColor = clampLightingColor(lightingSettings.fogColor, 2.0f);
}

void clampHudSettings(HudSettings& hudSettings)
{
    hudSettings.speedometerMaxKph = std::clamp(hudSettings.speedometerMaxKph, 50, 2500);
    hudSettings.speedometerMinorStepKph = std::clamp(hudSettings.speedometerMinorStepKph, 5, 200);
    hudSettings.speedometerMajorStepKph = std::clamp(hudSettings.speedometerMajorStepKph, hudSettings.speedometerMinorStepKph, 500);
    hudSettings.speedometerLabelStepKph = std::clamp(hudSettings.speedometerLabelStepKph, hudSettings.speedometerMajorStepKph, 600);
    hudSettings.speedometerRedlineKph = std::clamp(hudSettings.speedometerRedlineKph, 50, hudSettings.speedometerMaxKph);
}

void clampPropAudioConfigValues(PropAudioConfig& config)
{
    config.baseRpm = clamp(config.baseRpm, 6.0f, 220.0f);
    config.loadRpmContribution = clamp(config.loadRpmContribution, 20.0f, 420.0f);
    config.engineFrequencyScale = clamp(config.engineFrequencyScale, 0.35f, 2.5f);
    config.engineTonalMix = clamp(config.engineTonalMix, 0.0f, 2.0f);
    config.propHarmonicMix = clamp(config.propHarmonicMix, 0.0f, 2.0f);
    config.engineNoiseAmount = clamp(config.engineNoiseAmount, 0.0f, 2.0f);
    config.ambienceFrequencyScale = clamp(config.ambienceFrequencyScale, 0.35f, 2.5f);
    config.waterAmbienceGain = clamp(config.waterAmbienceGain, 0.0f, 2.0f);
    config.groundAmbienceGain = clamp(config.groundAmbienceGain, 0.0f, 2.0f);
}

void clampTuningConfig(FlightConfig& config)
{
    config.massKg = clamp(config.massKg, 450.0f, 4000.0f);
    config.maxThrustSeaLevel = clamp(config.maxThrustSeaLevel, 800.0f, 12000.0f);
    config.CLalpha = clamp(config.CLalpha, 2.0f, 8.5f);
    config.CD0 = clamp(config.CD0, 0.01f, 0.12f);
    config.inducedDragK = clamp(config.inducedDragK, 0.01f, 0.18f);
    config.CmAlpha = clamp(config.CmAlpha, -2.8f, -0.1f);
    config.pitchControlScale = clamp(config.pitchControlScale, 0.05f, 1.5f);
    config.rollControlScale = clamp(config.rollControlScale, 0.05f, 1.5f);
    config.yawControlScale = clamp(config.yawControlScale, 0.05f, 1.5f);
    config.maxElevatorDeflectionRad = clamp(config.maxElevatorDeflectionRad, radians(10.0f), radians(45.0f));
    config.maxAileronDeflectionRad = clamp(config.maxAileronDeflectionRad, radians(8.0f), radians(35.0f));
    config.maxRudderDeflectionRad = clamp(config.maxRudderDeflectionRad, radians(10.0f), radians(45.0f));
    config.groundFriction = clamp(config.groundFriction, 0.20f, 0.995f);
}

std::uint64_t fnv1a64(const std::uint8_t* data, std::size_t size)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= static_cast<std::uint64_t>(data[index]);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string hashBytesHex(const std::vector<std::uint8_t>& bytes)
{
    const std::uint64_t hash = fnv1a64(bytes.data(), bytes.size());
    char buffer[17] {};
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(hash));
    return buffer;
}

std::uint64_t nextModelCacheRevision()
{
    static std::uint64_t revision = 1;
    return ++revision;
}

void touchModelCacheRevision(Model& model)
{
    model.cacheRevision = nextModelCacheRevision();
}

std::string hashStringHex(std::string_view value)
{
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(value.data());
    const std::uint64_t hash = fnv1a64(bytes, value.size());
    char buffer[17] {};
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(hash));
    return buffer;
}

Quat composeVisualRotationOffset(const PlaneVisualState& visual)
{
    const Quat forwardAxis = quatFromAxisAngle({ 0.0f, 1.0f, 0.0f }, radians(visual.forwardAxisYawDegrees));
    const Quat yaw = quatFromAxisAngle({ 0.0f, 1.0f, 0.0f }, radians(visual.yawDegrees));
    const Quat pitch = quatFromAxisAngle({ 1.0f, 0.0f, 0.0f }, radians(visual.pitchDegrees));
    const Quat roll = quatFromAxisAngle({ 0.0f, 0.0f, 1.0f }, radians(visual.rollDegrees));
    return quatNormalize(
        quatMultiply(
            quatMultiply(
                quatMultiply(
                    quatMultiply(visual.importRotationOffset, forwardAxis),
                    yaw),
                pitch),
            roll));
}

void clearPaintHistory(PlaneVisualState& visual)
{
    visual.paintUndoStack.clear();
    visual.paintRedoStack.clear();
}

const RgbaImage* firstPaintBaseImage(const Model& model)
{
    for (const Material& material : model.materials) {
        if (material.baseColorTexture.valid() &&
            static_cast<std::size_t>(material.baseColorTexture.imageIndex) < model.images.size()) {
            return &model.images[static_cast<std::size_t>(material.baseColorTexture.imageIndex)];
        }
    }
    return nullptr;
}

const RgbaImage* firstPaintBaseImage(const PlaneVisualState& visual)
{
    return firstPaintBaseImage(visual.sourceModel);
}

std::string buildPaintTargetKey(const Model& model)
{
    std::ostringstream stream;
    stream
        << (model.assetKey.empty() ? "builtin:anonymous" : model.assetKey) << '|'
        << model.vertices.size() << '|'
        << model.faces.size() << '|'
        << model.texCoords.size() << '|'
        << model.materials.size() << '|'
        << (model.hasTexCoords ? 1 : 0) << '|'
        << (model.hasPaintableMaterial ? 1 : 0);
    if (const RgbaImage* baseImage = firstPaintBaseImage(model); baseImage != nullptr) {
        stream
            << '|'
            << baseImage->width
            << 'x'
            << baseImage->height
            << '|'
            << hashBytesHex(baseImage->pixels);
    } else {
        stream << "|no_base";
    }
    return hashStringHex(stream.str());
}

void updateVisualPaintTargetKey(PlaneVisualState& visual)
{
    visual.paintTargetKey = buildPaintTargetKey(visual.sourceModel);
}

std::string resolveStoredPaintHashForCurrentModel(const PlaneVisualState& visual)
{
    if (visual.paintTargetKey.empty()) {
        return visual.paintHash;
    }
    const auto found = visual.paintHashesByModelKey.find(visual.paintTargetKey);
    if (found == visual.paintHashesByModelKey.end()) {
        return {};
    }
    return found->second;
}

void storePaintHashForCurrentModel(PlaneVisualState& visual, const std::string& paintHash)
{
    if (visual.paintTargetKey.empty()) {
        return;
    }
    if (paintHash.empty()) {
        visual.paintHashesByModelKey.erase(visual.paintTargetKey);
        return;
    }
    visual.paintHashesByModelKey[visual.paintTargetKey] = paintHash;
}

bool visualSupportsPaint(const PlaneVisualState& visual)
{
    return visual.sourceModel.hasPaintableMaterial &&
        firstPaintBaseImage(visual) != nullptr &&
        visual.sourceModel.hasTexCoords;
}

RgbaImage makeTransparentOverlayLike(const RgbaImage& source)
{
    RgbaImage overlay;
    overlay.width = source.width;
    overlay.height = source.height;
    overlay.pixels.assign(source.pixels.size(), 0);
    overlay.version = 1;
    return overlay;
}

void compositeOverlayIntoImage(RgbaImage& baseImage, const RgbaImage& overlay)
{
    if (baseImage.width != overlay.width ||
        baseImage.height != overlay.height ||
        baseImage.pixels.size() != overlay.pixels.size()) {
        return;
    }

    for (std::size_t index = 0; index + 3 < baseImage.pixels.size(); index += 4) {
        const float srcA = static_cast<float>(overlay.pixels[index + 3]) / 255.0f;
        if (srcA <= 0.0f) {
            continue;
        }

        const float dstA = static_cast<float>(baseImage.pixels[index + 3]) / 255.0f;
        const float outA = srcA + (dstA * (1.0f - srcA));
        for (int channel = 0; channel < 3; ++channel) {
            const float src = static_cast<float>(overlay.pixels[index + channel]) / 255.0f;
            const float dst = static_cast<float>(baseImage.pixels[index + channel]) / 255.0f;
            const float out = (src * srcA) + (dst * (1.0f - srcA));
            baseImage.pixels[index + channel] = static_cast<std::uint8_t>(clamp(out, 0.0f, 1.0f) * 255.0f);
        }
        baseImage.pixels[index + 3] = static_cast<std::uint8_t>(clamp(outA, 0.0f, 1.0f) * 255.0f);
    }
    baseImage.version += 1;
}

void refreshVisualCompositeModel(PlaneVisualState& visual)
{
    visual.paintSupported = visualSupportsPaint(visual);
    visual.model = visual.sourceModel;
    if (visual.paintSupported && visual.hasCommittedPaint) {
        for (Material& material : visual.model.materials) {
            if (!material.baseColorTexture.valid() ||
                static_cast<std::size_t>(material.baseColorTexture.imageIndex) >= visual.model.images.size()) {
                continue;
            }
            compositeOverlayIntoImage(
                visual.model.images[static_cast<std::size_t>(material.baseColorTexture.imageIndex)],
                visual.paintOverlay);
        }
    }
    touchModelCacheRevision(visual.model);
    if (!visual.paintSupported || !visual.hasCommittedPaint) {
        return;
    }
}

void resetVisualPaint(PlaneVisualState& visual)
{
    visual.paintHash.clear();
    visual.paintOverlay = {};
    visual.hasCommittedPaint = false;
    clearPaintHistory(visual);
    refreshVisualCompositeModel(visual);
}

void ensurePaintOverlay(PlaneVisualState& visual)
{
    if (!visualSupportsPaint(visual)) {
        visual.paintSupported = false;
        return;
    }
    if (visual.paintOverlay.width > 0 &&
        visual.paintOverlay.height > 0 &&
        visual.hasCommittedPaint) {
        return;
    }
    const RgbaImage* baseImage = firstPaintBaseImage(visual);
    if (baseImage == nullptr) {
        visual.paintSupported = false;
        return;
    }
    visual.paintOverlay = makeTransparentOverlayLike(*baseImage);
    visual.hasCommittedPaint = true;
    clearPaintHistory(visual);
    refreshVisualCompositeModel(visual);
}

void pushPaintUndoSnapshot(PlaneVisualState& visual)
{
    if (!visual.hasCommittedPaint) {
        return;
    }
    visual.paintUndoStack.push_back(visual.paintOverlay);
    if (visual.paintUndoStack.size() > 24) {
        visual.paintUndoStack.erase(visual.paintUndoStack.begin());
    }
    visual.paintRedoStack.clear();
}

bool paintUndo(PlaneVisualState& visual)
{
    if (visual.paintUndoStack.empty()) {
        return false;
    }
    visual.paintRedoStack.push_back(visual.paintOverlay);
    visual.paintOverlay = visual.paintUndoStack.back();
    visual.paintUndoStack.pop_back();
    visual.paintOverlay.version += 1;
    refreshVisualCompositeModel(visual);
    return true;
}

bool paintRedo(PlaneVisualState& visual)
{
    if (visual.paintRedoStack.empty()) {
        return false;
    }
    visual.paintUndoStack.push_back(visual.paintOverlay);
    visual.paintOverlay = visual.paintRedoStack.back();
    visual.paintRedoStack.pop_back();
    visual.paintOverlay.version += 1;
    refreshVisualCompositeModel(visual);
    return true;
}

Vec4 paintPresetColor(int presetIndex)
{
    static const std::array<Vec4, 8> presets {
        Vec4 { 0.96f, 0.96f, 0.94f, 1.0f },
        Vec4 { 0.08f, 0.09f, 0.10f, 1.0f },
        Vec4 { 0.88f, 0.18f, 0.16f, 1.0f },
        Vec4 { 0.92f, 0.68f, 0.16f, 1.0f },
        Vec4 { 0.40f, 0.74f, 0.22f, 1.0f },
        Vec4 { 0.20f, 0.56f, 0.92f, 1.0f },
        Vec4 { 0.54f, 0.34f, 0.88f, 1.0f },
        Vec4 { 0.88f, 0.34f, 0.54f, 1.0f }
    };
    const int clampedIndex = std::clamp(presetIndex, 0, static_cast<int>(presets.size()) - 1);
    return presets[static_cast<std::size_t>(clampedIndex)];
}

bool applyPaintStroke(PlaneVisualState& visual, const PaintUiState& paintUi, float u, float v, bool captureUndo)
{
    if (!visualSupportsPaint(visual)) {
        return false;
    }
    ensurePaintOverlay(visual);
    if (!visual.hasCommittedPaint || visual.paintOverlay.width <= 0 || visual.paintOverlay.height <= 0) {
        return false;
    }

    const int centerX = std::clamp(static_cast<int>(u * static_cast<float>(visual.paintOverlay.width)), 0, visual.paintOverlay.width - 1);
    const int centerY = std::clamp(static_cast<int>(v * static_cast<float>(visual.paintOverlay.height)), 0, visual.paintOverlay.height - 1);
    const int radius = std::max(1, paintUi.brushSize / 2);
    const Vec4 color = paintPresetColor(paintUi.colorIndex);

    if (captureUndo) {
        pushPaintUndoSnapshot(visual);
    }

    for (int py = centerY - radius; py <= centerY + radius; ++py) {
        if (py < 0 || py >= visual.paintOverlay.height) {
            continue;
        }
        for (int px = centerX - radius; px <= centerX + radius; ++px) {
            if (px < 0 || px >= visual.paintOverlay.width) {
                continue;
            }
            const float dx = static_cast<float>(px - centerX);
            const float dy = static_cast<float>(py - centerY);
            const float dist = std::sqrt((dx * dx) + (dy * dy));
            if (dist > static_cast<float>(radius)) {
                continue;
            }
            const float normalized = 1.0f - (dist / static_cast<float>(radius));
            const float hardness = clamp(paintUi.brushHardness, 0.05f, 1.0f);
            const float influence = clamp(normalized * mix(0.25f, 1.0f, hardness), 0.0f, 1.0f) * clamp(paintUi.brushOpacity, 0.0f, 1.0f);

            const std::size_t index = static_cast<std::size_t>(((py * visual.paintOverlay.width) + px) * 4);
            if (paintUi.mode == PaintMode::Erase) {
                const float alpha = std::max(0.0f, static_cast<float>(visual.paintOverlay.pixels[index + 3]) / 255.0f - influence);
                visual.paintOverlay.pixels[index + 3] = static_cast<std::uint8_t>(alpha * 255.0f);
                continue;
            }

            const float srcA = influence * color.w;
            const float dstA = static_cast<float>(visual.paintOverlay.pixels[index + 3]) / 255.0f;
            const float outA = srcA + (dstA * (1.0f - srcA));
            const auto blendChannel = [&](const float srcColor, const std::uint8_t dstByte) -> std::uint8_t {
                const float dst = static_cast<float>(dstByte) / 255.0f;
                const float out = (srcColor * srcA) + (dst * (1.0f - srcA));
                return static_cast<std::uint8_t>(clamp(out, 0.0f, 1.0f) * 255.0f);
            };
            visual.paintOverlay.pixels[index + 0] = blendChannel(color.x, visual.paintOverlay.pixels[index + 0]);
            visual.paintOverlay.pixels[index + 1] = blendChannel(color.y, visual.paintOverlay.pixels[index + 1]);
            visual.paintOverlay.pixels[index + 2] = blendChannel(color.z, visual.paintOverlay.pixels[index + 2]);
            visual.paintOverlay.pixels[index + 3] = static_cast<std::uint8_t>(clamp(outA, 0.0f, 1.0f) * 255.0f);
        }
    }

    visual.paintOverlay.version += 1;
    refreshVisualCompositeModel(visual);
    return true;
}

bool fillPaintOverlay(PlaneVisualState& visual, int presetIndex)
{
    if (!visualSupportsPaint(visual)) {
        return false;
    }
    ensurePaintOverlay(visual);
    pushPaintUndoSnapshot(visual);
    const Vec4 color = paintPresetColor(presetIndex);
    for (std::size_t index = 0; index + 3 < visual.paintOverlay.pixels.size(); index += 4) {
        visual.paintOverlay.pixels[index + 0] = static_cast<std::uint8_t>(clamp(color.x, 0.0f, 1.0f) * 255.0f);
        visual.paintOverlay.pixels[index + 1] = static_cast<std::uint8_t>(clamp(color.y, 0.0f, 1.0f) * 255.0f);
        visual.paintOverlay.pixels[index + 2] = static_cast<std::uint8_t>(clamp(color.z, 0.0f, 1.0f) * 255.0f);
        visual.paintOverlay.pixels[index + 3] = 255;
    }
    visual.paintOverlay.version += 1;
    refreshVisualCompositeModel(visual);
    return true;
}

bool clearPaintOverlay(PlaneVisualState& visual)
{
    if (!visualSupportsPaint(visual)) {
        return false;
    }
    ensurePaintOverlay(visual);
    pushPaintUndoSnapshot(visual);
    std::fill(visual.paintOverlay.pixels.begin(), visual.paintOverlay.pixels.end(), static_cast<std::uint8_t>(0));
    visual.paintOverlay.version += 1;
    refreshVisualCompositeModel(visual);
    return true;
}

std::filesystem::path getPreferenceFilePath()
{
    char* prefPath = SDL_GetPrefPath("TrueFlight", "TrueFlight");
    if (prefPath == nullptr) {
        return {};
    }

    const std::filesystem::path result = std::filesystem::path(prefPath) / "native_settings.ini";
    SDL_free(prefPath);
    return result;
}

std::filesystem::path getPreferenceRootPath()
{
    const std::filesystem::path settingsPath = getPreferenceFilePath();
    return settingsPath.empty() ? std::filesystem::path {} : settingsPath.parent_path();
}

std::filesystem::path getPaintStorageDirectory()
{
    const std::filesystem::path root = getPreferenceRootPath();
    return root.empty() ? std::filesystem::path {} : root / "paint";
}

std::filesystem::path getWorldStorageDirectory()
{
    const std::filesystem::path root = getPreferenceRootPath();
    return root.empty() ? std::filesystem::path {} : root / "worlds";
}

std::filesystem::path getTerrainChunkCacheDirectory()
{
    const std::filesystem::path root = getPreferenceRootPath();
    return root.empty() ? std::filesystem::path {} : root / "terrain_chunk_cache";
}

WorldInfoSnapshot buildWorldInfoSnapshot(const TerrainParams& terrainParams, std::string_view worldId, const Vec3& spawn = {})
{
    WorldInfoSnapshot info;
    info.worldId = worldId.empty() ? std::string("default") : std::string(worldId);
    info.formatVersion = std::max(1, terrainParams.generatorVersion);
    info.seed = std::max(1, terrainParams.seed);
    info.chunkSize = terrainParams.chunkSize;
    info.horizonRadiusMeters = terrainParams.horizonRadiusMeters;
    info.heightAmplitude = terrainParams.heightAmplitude;
    info.heightFrequency = terrainParams.heightFrequency;
    info.waterLevel = terrainParams.waterLevel;
    info.tunnelSeeds = terrainParams.explicitTunnelSeeds;
    info.spawnX = spawn.x;
    info.spawnY = spawn.y;
    info.spawnZ = spawn.z;
    return info;
}

void bindTerrainContextWorldStore(TerrainFieldContext& terrainContext, WorldStore* worldStore)
{
    if (worldStore == nullptr) {
        terrainContext.sampleHeightDeltaAt = {};
        terrainContext.sampleVolumetricOverrideSdfAt = {};
        terrainContext.sampleChunkRevisionSignature = {};
        return;
    }

    terrainContext.sampleHeightDeltaAt = [worldStore](float x, float z) {
        return worldStore->sampleHeightDelta(x, z);
    };
    terrainContext.sampleVolumetricOverrideSdfAt = [worldStore](float x, float y, float z) {
        return worldStore->sampleVolumetricOverrideSdf(x, y, z);
    };
    terrainContext.sampleChunkRevisionSignature = [worldStore](float x0, float z0, float x1, float z1) {
        return worldStore->revisionSignatureForBounds(x0, z0, x1, z1, 1);
    };
}

std::filesystem::path getPaintStoragePath(const std::string& paintHash)
{
    if (paintHash.empty()) {
        return {};
    }
    return getPaintStorageDirectory() / (paintHash + ".png");
}

bool loadPaintOverlayByHash(const std::string& paintHash, PlaneVisualState& visual, std::string* errorText)
{
    if (paintHash.empty()) {
        return false;
    }

    RgbaImage image;
    if (!loadImageFile(getPaintStoragePath(paintHash), image, errorText)) {
        return false;
    }

    const RgbaImage* baseImage = firstPaintBaseImage(visual);
    if (baseImage == nullptr || image.width != baseImage->width || image.height != baseImage->height) {
        if (errorText != nullptr) {
            *errorText = "paint overlay dimensions do not match the selected model";
        }
        return false;
    }

    visual.paintOverlay = std::move(image);
    visual.paintHash = paintHash;
    visual.hasCommittedPaint = true;
    storePaintHashForCurrentModel(visual, paintHash);
    clearPaintHistory(visual);
    refreshVisualCompositeModel(visual);
    return true;
}

bool commitPaintOverlay(const std::filesystem::path& paintDirectory, PlaneVisualState& visual, std::string* outPaintHash, std::string* errorText)
{
    if (!visualSupportsPaint(visual) || !visual.hasCommittedPaint) {
        if (errorText != nullptr) {
            *errorText = "paint overlay unavailable";
        }
        return false;
    }

    std::vector<std::uint8_t> pngBytes;
    if (!encodePngBytes(visual.paintOverlay, pngBytes, errorText)) {
        return false;
    }

    const std::string paintHash = hashBytesHex(pngBytes);
    const std::filesystem::path path = paintDirectory / (paintHash + ".png");
    std::error_code ec;
    std::filesystem::create_directories(paintDirectory, ec);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        if (errorText != nullptr) {
            *errorText = "could not open paint overlay path for writing";
        }
        return false;
    }
    output.write(reinterpret_cast<const char*>(pngBytes.data()), static_cast<std::streamsize>(pngBytes.size()));
    if (!output.good()) {
        if (errorText != nullptr) {
            *errorText = "failed while writing paint overlay";
        }
        return false;
    }

    visual.paintHash = paintHash;
    visual.hasCommittedPaint = true;
    storePaintHashForCurrentModel(visual, paintHash);
    if (outPaintHash != nullptr) {
        *outPaintHash = paintHash;
    }
    return true;
}

void clampVisualPreferenceData(VisualPreferenceData& data)
{
    data.scale = clamp(data.scale, 0.15f, 24.0f);
    data.previewZoom = clamp(data.previewZoom, 0.35f, 3.0f);
    data.forwardAxisYawDegrees = clamp(data.forwardAxisYawDegrees, -180.0f, 180.0f);
    data.yawDegrees = clamp(data.yawDegrees, -180.0f, 180.0f);
    data.pitchDegrees = clamp(data.pitchDegrees, -180.0f, 180.0f);
    data.rollDegrees = clamp(data.rollDegrees, -180.0f, 180.0f);
    data.modelOffset.x = clamp(data.modelOffset.x, -24.0f, 24.0f);
    data.modelOffset.y = clamp(data.modelOffset.y, -24.0f, 24.0f);
    data.modelOffset.z = clamp(data.modelOffset.z, -24.0f, 24.0f);
}

void applyVisualPreferenceData(PlaneVisualState& visual, const VisualPreferenceData& data)
{
    visual.scale = data.scale;
    visual.previewZoom = data.previewZoom;
    visual.previewAutoSpin = data.previewAutoSpin;
    visual.forwardAxisYawDegrees = data.forwardAxisYawDegrees;
    visual.yawDegrees = data.yawDegrees;
    visual.pitchDegrees = data.pitchDegrees;
    visual.rollDegrees = data.rollDegrees;
    visual.modelOffset = data.modelOffset;
    visual.paintHashesByModelKey = data.paintHashesByModelKey;
    if (!visual.paintTargetKey.empty() &&
        !data.paintHash.empty() &&
        visual.paintHashesByModelKey.find(visual.paintTargetKey) == visual.paintHashesByModelKey.end()) {
        visual.paintHashesByModelKey[visual.paintTargetKey] = data.paintHash;
    }
    visual.paintHash = resolveStoredPaintHashForCurrentModel(visual);
    if (visual.paintHash.empty()) {
        visual.paintHash = data.paintHash;
    }
}

void saveVisualPreferenceData(std::ofstream& file, std::string_view roleKey, const PlaneVisualState& visual)
{
    std::map<std::string, std::string> paintHashesByModelKey = visual.paintHashesByModelKey;
    if (!visual.paintTargetKey.empty()) {
        if (visual.paintHash.empty()) {
            paintHashesByModelKey.erase(visual.paintTargetKey);
        } else {
            paintHashesByModelKey[visual.paintTargetKey] = visual.paintHash;
        }
    }
    file << "character." << roleKey << ".source_path=" << (visual.sourcePath.empty() ? "" : visual.sourcePath.generic_string()) << "\n";
    file << "character." << roleKey << ".scale=" << visual.scale << "\n";
    file << "character." << roleKey << ".preview_zoom=" << visual.previewZoom << "\n";
    file << "character." << roleKey << ".preview_auto_spin=" << (visual.previewAutoSpin ? 1 : 0) << "\n";
    file << "character." << roleKey << ".forward_axis_yaw_degrees=" << visual.forwardAxisYawDegrees << "\n";
    file << "character." << roleKey << ".yaw_degrees=" << visual.yawDegrees << "\n";
    file << "character." << roleKey << ".pitch_degrees=" << visual.pitchDegrees << "\n";
    file << "character." << roleKey << ".roll_degrees=" << visual.rollDegrees << "\n";
    file << "character." << roleKey << ".offset_x=" << visual.modelOffset.x << "\n";
    file << "character." << roleKey << ".offset_y=" << visual.modelOffset.y << "\n";
    file << "character." << roleKey << ".offset_z=" << visual.modelOffset.z << "\n";
    file << "paint." << roleKey << ".hash=" << visual.paintHash << "\n";
    for (const auto& [modelKey, paintHash] : paintHashesByModelKey) {
        if (modelKey.empty() || paintHash.empty()) {
            continue;
        }
        file << "paint." << roleKey << ".model." << modelKey << "=" << paintHash << "\n";
    }
}

bool applyVisualPreferenceValue(VisualPreferenceData& data, const std::string& suffix, const std::string& value)
{
    if (suffix == "source_path") {
        data.sourcePath = value.empty() ? std::filesystem::path {} : std::filesystem::path(value);
        data.hasStoredPath = true;
        return true;
    }
    if (suffix == "scale") {
        data.scale = parseFloatValue(value, data.scale);
        return true;
    }
    if (suffix == "preview_zoom") {
        data.previewZoom = parseFloatValue(value, data.previewZoom);
        return true;
    }
    if (suffix == "preview_auto_spin") {
        data.previewAutoSpin = parseBoolValue(value, data.previewAutoSpin);
        return true;
    }
    if (suffix == "forward_axis_yaw_degrees") {
        data.forwardAxisYawDegrees = parseFloatValue(value, data.forwardAxisYawDegrees);
        return true;
    }
    if (suffix == "yaw_degrees") {
        data.yawDegrees = parseFloatValue(value, data.yawDegrees);
        return true;
    }
    if (suffix == "pitch_degrees") {
        data.pitchDegrees = parseFloatValue(value, data.pitchDegrees);
        return true;
    }
    if (suffix == "roll_degrees") {
        data.rollDegrees = parseFloatValue(value, data.rollDegrees);
        return true;
    }
    if (suffix == "offset_x") {
        data.modelOffset.x = parseFloatValue(value, data.modelOffset.x);
        return true;
    }
    if (suffix == "offset_y") {
        data.modelOffset.y = parseFloatValue(value, data.modelOffset.y);
        return true;
    }
    if (suffix == "offset_z") {
        data.modelOffset.z = parseFloatValue(value, data.modelOffset.z);
        return true;
    }
    return false;
}

const char* windowModeToken(WindowMode windowMode)
{
    switch (windowMode) {
    case WindowMode::Borderless:
        return "borderless";
    case WindowMode::Fullscreen:
        return "fullscreen";
    case WindowMode::Windowed:
    default:
        return "windowed";
    }
}

WindowMode parseWindowModeToken(const std::string& value)
{
    const std::string lowered = toLowerAscii(trimAscii(value));
    if (lowered == "borderless") {
        return WindowMode::Borderless;
    }
    if (lowered == "fullscreen") {
        return WindowMode::Fullscreen;
    }
    return WindowMode::Windowed;
}

bool applyTerrainPreferenceValue(TerrainParams& terrainParams, const std::string& key, const std::string& value)
{
    if (key == "terrain.seed") {
        terrainParams.seed = parseIntValue(value, terrainParams.seed);
    } else if (key == "terrain.chunk_size") {
        terrainParams.chunkSize = parseFloatValue(value, terrainParams.chunkSize);
    } else if (key == "terrain.world_radius") {
        terrainParams.worldRadius = parseFloatValue(value, terrainParams.worldRadius);
    } else if (key == "terrain.gameplay_radius") {
        terrainParams.gameplayRadiusMeters = parseFloatValue(value, terrainParams.gameplayRadiusMeters);
    } else if (key == "terrain.mid_radius") {
        terrainParams.midFieldRadiusMeters = parseFloatValue(value, terrainParams.midFieldRadiusMeters);
    } else if (key == "terrain.horizon_radius") {
        terrainParams.horizonRadiusMeters = parseFloatValue(value, terrainParams.horizonRadiusMeters);
    } else if (key == "terrain.quality") {
        terrainParams.terrainQuality = parseFloatValue(value, terrainParams.terrainQuality);
    } else if (key == "terrain.mesh_budget") {
        terrainParams.meshBuildBudget = parseIntValue(value, terrainParams.meshBuildBudget);
    } else if (key == "terrain.lod0_radius") {
        terrainParams.lod0Radius = parseIntValue(value, terrainParams.lod0Radius);
    } else if (key == "terrain.lod1_radius") {
        terrainParams.lod1Radius = parseIntValue(value, terrainParams.lod1Radius);
    } else if (key == "terrain.lod2_radius") {
        terrainParams.lod2Radius = parseIntValue(value, terrainParams.lod2Radius);
    } else if (key == "terrain.height_amplitude") {
        terrainParams.heightAmplitude = parseFloatValue(value, terrainParams.heightAmplitude);
    } else if (key == "terrain.height_frequency") {
        terrainParams.heightFrequency = parseFloatValue(value, terrainParams.heightFrequency);
    } else if (key == "terrain.detail_amplitude") {
        terrainParams.surfaceDetailAmplitude = parseFloatValue(value, terrainParams.surfaceDetailAmplitude);
    } else if (key == "terrain.ridge_amplitude") {
        terrainParams.ridgeAmplitude = parseFloatValue(value, terrainParams.ridgeAmplitude);
    } else if (key == "terrain.terrace_strength") {
        terrainParams.terraceStrength = parseFloatValue(value, terrainParams.terraceStrength);
    } else if (key == "terrain.water_level") {
        terrainParams.waterLevel = parseFloatValue(value, terrainParams.waterLevel);
    } else if (key == "terrain.snow_line") {
        terrainParams.snowLine = parseFloatValue(value, terrainParams.snowLine);
    } else if (key == "terrain.props_enabled") {
        terrainParams.decoration.enabled = parseBoolValue(value, terrainParams.decoration.enabled);
    } else if (key == "terrain.prop_density") {
        terrainParams.decoration.density = parseFloatValue(value, terrainParams.decoration.density);
    } else if (key == "terrain.prop_density_near") {
        terrainParams.decoration.nearDensityScale = parseFloatValue(value, terrainParams.decoration.nearDensityScale);
    } else if (key == "terrain.prop_density_mid") {
        terrainParams.decoration.midDensityScale = parseFloatValue(value, terrainParams.decoration.midDensityScale);
    } else if (key == "terrain.prop_density_far") {
        terrainParams.decoration.farDensityScale = parseFloatValue(value, terrainParams.decoration.farDensityScale);
    } else if (key == "terrain.prop_shore_brush_density") {
        terrainParams.decoration.shoreBrushDensity = parseFloatValue(value, terrainParams.decoration.shoreBrushDensity);
    } else if (key == "terrain.prop_rock_density") {
        terrainParams.decoration.rockDensity = parseFloatValue(value, terrainParams.decoration.rockDensity);
    } else if (key == "terrain.prop_tree_line_offset") {
        terrainParams.decoration.treeLineOffset = parseFloatValue(value, terrainParams.decoration.treeLineOffset);
    } else if (key == "terrain.prop_collision") {
        terrainParams.decoration.collisionEnabled = parseBoolValue(value, terrainParams.decoration.collisionEnabled);
    } else if (key == "terrain.prop_seed_offset") {
        terrainParams.decoration.seedOffset = parseIntValue(value, terrainParams.decoration.seedOffset);
    } else if (key == "terrain.caves") {
        terrainParams.caveEnabled = parseBoolValue(value, terrainParams.caveEnabled);
    } else if (key == "terrain.cave_strength") {
        terrainParams.caveStrength = parseFloatValue(value, terrainParams.caveStrength);
    } else if (key == "terrain.cave_threshold") {
        terrainParams.caveThreshold = parseFloatValue(value, terrainParams.caveThreshold);
    } else if (key == "terrain.tunnel_count") {
        terrainParams.tunnelCount = parseIntValue(value, terrainParams.tunnelCount);
    } else if (key == "terrain.tunnel_radius_min") {
        terrainParams.tunnelRadiusMin = parseFloatValue(value, terrainParams.tunnelRadiusMin);
    } else if (key == "terrain.tunnel_radius_max") {
        terrainParams.tunnelRadiusMax = parseFloatValue(value, terrainParams.tunnelRadiusMax);
    } else if (key == "terrain.surface_only") {
        terrainParams.surfaceOnlyMeshing = parseBoolValue(value, terrainParams.surfaceOnlyMeshing);
    } else if (key == "terrain.skirts") {
        terrainParams.enableSkirts = parseBoolValue(value, terrainParams.enableSkirts);
    } else if (key == "terrain.skirt_depth") {
        terrainParams.skirtDepth = parseFloatValue(value, terrainParams.skirtDepth);
    } else if (key == "terrain.max_cells_per_axis") {
        terrainParams.maxChunkCellsPerAxis = parseIntValue(value, terrainParams.maxChunkCellsPerAxis);
    } else {
        return false;
    }
    return true;
}

bool savePreferences(
    const std::filesystem::path& path,
    const UiState& uiState,
    const GraphicsSettings& graphicsSettings,
    const LightingSettings& lightingSettings,
    const HudSettings& hudSettings,
    const ControlProfile& controls,
    const AircraftProfile& planeProfile,
    const TerrainParams& terrainParams,
    const PlaneVisualState& planeVisual,
    const PlaneVisualState& walkingVisual,
    std::string* errorText)
{
    if (path.empty()) {
        if (errorText != nullptr) {
            *errorText = "SDL_GetPrefPath returned no writable location.";
        }
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        if (errorText != nullptr) {
            *errorText = "Could not open " + path.string() + " for writing.";
        }
        return false;
    }

    const FlightConfig& config = planeProfile.flightConfig;
    const PropAudioConfig& propAudioConfig = planeProfile.propAudioConfig;

    file << "ui.chase_camera=" << (uiState.chaseCamera ? 1 : 0) << "\n";
    file << "ui.show_map=" << (uiState.showMap ? 1 : 0) << "\n";
    file << "ui.show_debug=" << (uiState.showDebug ? 1 : 0) << "\n";
    file << "ui.show_crosshair=" << (uiState.showCrosshair ? 1 : 0) << "\n";
    file << "ui.show_throttle=" << (uiState.showThrottleHud ? 1 : 0) << "\n";
    file << "ui.show_controls=" << (uiState.showControlIndicator ? 1 : 0) << "\n";
    file << "ui.show_geo=" << (uiState.showGeoInfo ? 1 : 0) << "\n";
    file << "ui.invert_look_y=" << (uiState.invertLookY ? 1 : 0) << "\n";
    file << "ui.map_north_up=" << (uiState.mapNorthUp ? 1 : 0) << "\n";
    file << "ui.map_zoom_index=" << uiState.mapZoomIndex << "\n";
    file << "ui.camera_fov_degrees=" << uiState.cameraFovDegrees << "\n";
    file << "ui.walking_move_speed=" << uiState.walkingMoveSpeed << "\n";
    file << "ui.mouse_sensitivity=" << uiState.mouseSensitivity << "\n";
    file << "ui.audio_enabled=" << (uiState.audioEnabled ? 1 : 0) << "\n";
    file << "ui.master_volume=" << uiState.masterVolume << "\n";
    file << "ui.engine_volume=" << uiState.engineVolume << "\n";
    file << "ui.ambience_volume=" << uiState.ambienceVolume << "\n";
    file << "graphics.window_mode=" << windowModeToken(graphicsSettings.windowMode) << "\n";
    file << "graphics.resolution_width=" << graphicsSettings.resolutionWidth << "\n";
    file << "graphics.resolution_height=" << graphicsSettings.resolutionHeight << "\n";
    file << "graphics.render_scale=" << graphicsSettings.renderScale << "\n";
    file << "graphics.draw_distance=" << graphicsSettings.drawDistance << "\n";
    file << "graphics.horizon_fog=" << (graphicsSettings.horizonFog ? 1 : 0) << "\n";
    file << "graphics.texture_mipmaps=" << (graphicsSettings.textureMipmaps ? 1 : 0) << "\n";
    file << "graphics.vsync=" << (graphicsSettings.vsync ? 1 : 0) << "\n";
    file << "lighting.show_sun_marker=" << (lightingSettings.showSunMarker ? 1 : 0) << "\n";
    file << "lighting.sun_yaw_degrees=" << lightingSettings.sunYawDegrees << "\n";
    file << "lighting.sun_pitch_degrees=" << lightingSettings.sunPitchDegrees << "\n";
    file << "lighting.sun_intensity=" << lightingSettings.sunIntensity << "\n";
    file << "lighting.ambient=" << lightingSettings.ambient << "\n";
    file << "lighting.marker_distance=" << lightingSettings.markerDistance << "\n";
    file << "lighting.marker_size=" << lightingSettings.markerSize << "\n";
    file << "lighting.shadow_enabled=" << (lightingSettings.shadowEnabled ? 1 : 0) << "\n";
    file << "lighting.shadow_softness=" << lightingSettings.shadowSoftness << "\n";
    file << "lighting.shadow_distance=" << lightingSettings.shadowDistance << "\n";
    file << "lighting.gi_specular=" << lightingSettings.specularAmbient << "\n";
    file << "lighting.gi_bounce=" << lightingSettings.bounceStrength << "\n";
    file << "lighting.fog_density=" << lightingSettings.fogDensity << "\n";
    file << "lighting.fog_height_falloff=" << lightingSettings.fogHeightFalloff << "\n";
    file << "lighting.exposure_ev=" << lightingSettings.exposureEv << "\n";
    file << "lighting.turbidity=" << lightingSettings.turbidity << "\n";
    file << "lighting.sun_tint_r=" << lightingSettings.sunTint.x << "\n";
    file << "lighting.sun_tint_g=" << lightingSettings.sunTint.y << "\n";
    file << "lighting.sun_tint_b=" << lightingSettings.sunTint.z << "\n";
    file << "lighting.sky_tint_r=" << lightingSettings.skyTint.x << "\n";
    file << "lighting.sky_tint_g=" << lightingSettings.skyTint.y << "\n";
    file << "lighting.sky_tint_b=" << lightingSettings.skyTint.z << "\n";
    file << "lighting.ground_tint_r=" << lightingSettings.groundTint.x << "\n";
    file << "lighting.ground_tint_g=" << lightingSettings.groundTint.y << "\n";
    file << "lighting.ground_tint_b=" << lightingSettings.groundTint.z << "\n";
    file << "lighting.fog_r=" << lightingSettings.fogColor.x << "\n";
    file << "lighting.fog_g=" << lightingSettings.fogColor.y << "\n";
    file << "lighting.fog_b=" << lightingSettings.fogColor.z << "\n";
    file << "hud.show_speedometer=" << (hudSettings.showSpeedometer ? 1 : 0) << "\n";
    file << "hud.show_debug=" << (hudSettings.showDebug ? 1 : 0) << "\n";
    file << "hud.show_throttle=" << (hudSettings.showThrottle ? 1 : 0) << "\n";
    file << "hud.show_controls=" << (hudSettings.showControls ? 1 : 0) << "\n";
    file << "hud.show_map=" << (hudSettings.showMap ? 1 : 0) << "\n";
    file << "hud.show_geo=" << (hudSettings.showGeoInfo ? 1 : 0) << "\n";
    file << "hud.speedometer_max_kph=" << hudSettings.speedometerMaxKph << "\n";
    file << "hud.speedometer_minor_step_kph=" << hudSettings.speedometerMinorStepKph << "\n";
    file << "hud.speedometer_major_step_kph=" << hudSettings.speedometerMajorStepKph << "\n";
    file << "hud.speedometer_label_step_kph=" << hudSettings.speedometerLabelStepKph << "\n";
    file << "hud.speedometer_redline_kph=" << hudSettings.speedometerRedlineKph << "\n";
    file << "aircraft.plane.audio.base_rpm=" << propAudioConfig.baseRpm << "\n";
    file << "aircraft.plane.audio.load_rpm_contribution=" << propAudioConfig.loadRpmContribution << "\n";
    file << "aircraft.plane.audio.engine_frequency_scale=" << propAudioConfig.engineFrequencyScale << "\n";
    file << "aircraft.plane.audio.engine_tonal_mix=" << propAudioConfig.engineTonalMix << "\n";
    file << "aircraft.plane.audio.prop_harmonic_mix=" << propAudioConfig.propHarmonicMix << "\n";
    file << "aircraft.plane.audio.engine_noise_amount=" << propAudioConfig.engineNoiseAmount << "\n";
    file << "aircraft.plane.audio.ambience_frequency_scale=" << propAudioConfig.ambienceFrequencyScale << "\n";
    file << "aircraft.plane.audio.water_ambience_gain=" << propAudioConfig.waterAmbienceGain << "\n";
    file << "aircraft.plane.audio.ground_ambience_gain=" << propAudioConfig.groundAmbienceGain << "\n";
    file << "flight.mass_kg=" << config.massKg << "\n";
    file << "flight.max_thrust=" << config.maxThrustSeaLevel << "\n";
    file << "flight.cl_alpha=" << config.CLalpha << "\n";
    file << "flight.cd0=" << config.CD0 << "\n";
    file << "flight.induced_drag_k=" << config.inducedDragK << "\n";
    file << "flight.cm_alpha=" << config.CmAlpha << "\n";
    file << "flight.pitch_control_scale=" << config.pitchControlScale << "\n";
    file << "flight.roll_control_scale=" << config.rollControlScale << "\n";
    file << "flight.yaw_control_scale=" << config.yawControlScale << "\n";
    file << "flight.max_elevator_deg=" << degrees(config.maxElevatorDeflectionRad) << "\n";
    file << "flight.max_aileron_deg=" << degrees(config.maxAileronDeflectionRad) << "\n";
    file << "flight.auto_trim=" << (config.enableAutoTrim ? 1 : 0) << "\n";
    file << "flight.ground_friction=" << config.groundFriction << "\n";
    file << "terrain.seed=" << terrainParams.seed << "\n";
    file << "terrain.chunk_size=" << terrainParams.chunkSize << "\n";
    file << "terrain.world_radius=" << terrainParams.worldRadius << "\n";
    file << "terrain.gameplay_radius=" << terrainParams.gameplayRadiusMeters << "\n";
    file << "terrain.mid_radius=" << terrainParams.midFieldRadiusMeters << "\n";
    file << "terrain.horizon_radius=" << terrainParams.horizonRadiusMeters << "\n";
    file << "terrain.quality=" << terrainParams.terrainQuality << "\n";
    file << "terrain.mesh_budget=" << terrainParams.meshBuildBudget << "\n";
    file << "terrain.lod0_radius=" << terrainParams.lod0Radius << "\n";
    file << "terrain.lod1_radius=" << terrainParams.lod1Radius << "\n";
    file << "terrain.lod2_radius=" << terrainParams.lod2Radius << "\n";
    file << "terrain.height_amplitude=" << terrainParams.heightAmplitude << "\n";
    file << "terrain.height_frequency=" << terrainParams.heightFrequency << "\n";
    file << "terrain.detail_amplitude=" << terrainParams.surfaceDetailAmplitude << "\n";
    file << "terrain.ridge_amplitude=" << terrainParams.ridgeAmplitude << "\n";
    file << "terrain.terrace_strength=" << terrainParams.terraceStrength << "\n";
    file << "terrain.water_level=" << terrainParams.waterLevel << "\n";
    file << "terrain.snow_line=" << terrainParams.snowLine << "\n";
    file << "terrain.props_enabled=" << (terrainParams.decoration.enabled ? 1 : 0) << "\n";
    file << "terrain.prop_density=" << terrainParams.decoration.density << "\n";
    file << "terrain.prop_density_near=" << terrainParams.decoration.nearDensityScale << "\n";
    file << "terrain.prop_density_mid=" << terrainParams.decoration.midDensityScale << "\n";
    file << "terrain.prop_density_far=" << terrainParams.decoration.farDensityScale << "\n";
    file << "terrain.prop_shore_brush_density=" << terrainParams.decoration.shoreBrushDensity << "\n";
    file << "terrain.prop_rock_density=" << terrainParams.decoration.rockDensity << "\n";
    file << "terrain.prop_tree_line_offset=" << terrainParams.decoration.treeLineOffset << "\n";
    file << "terrain.prop_collision=" << (terrainParams.decoration.collisionEnabled ? 1 : 0) << "\n";
    file << "terrain.prop_seed_offset=" << terrainParams.decoration.seedOffset << "\n";
    file << "terrain.caves=" << (terrainParams.caveEnabled ? 1 : 0) << "\n";
    file << "terrain.cave_strength=" << terrainParams.caveStrength << "\n";
    file << "terrain.cave_threshold=" << terrainParams.caveThreshold << "\n";
    file << "terrain.tunnel_count=" << terrainParams.tunnelCount << "\n";
    file << "terrain.tunnel_radius_min=" << terrainParams.tunnelRadiusMin << "\n";
    file << "terrain.tunnel_radius_max=" << terrainParams.tunnelRadiusMax << "\n";
    file << "terrain.surface_only=" << (terrainParams.surfaceOnlyMeshing ? 1 : 0) << "\n";
    file << "terrain.skirts=" << (terrainParams.enableSkirts ? 1 : 0) << "\n";
    file << "terrain.skirt_depth=" << terrainParams.skirtDepth << "\n";
    file << "terrain.max_cells_per_axis=" << terrainParams.maxChunkCellsPerAxis << "\n";
    saveVisualPreferenceData(file, "plane", planeVisual);
    saveVisualPreferenceData(file, "walking", walkingVisual);
    file << "model.source_path=" << (planeVisual.sourcePath.empty() ? "" : planeVisual.sourcePath.generic_string()) << "\n";
    for (const ControlActionBinding& action : controls.actions) {
        if (!action.configurable || !action.supported) {
            continue;
        }
        file << "controls." << controlActionStorageKey(action.id) << ".primary=" << serializeInputBinding(action.slots[0]) << "\n";
        file << "controls." << controlActionStorageKey(action.id) << ".secondary=" << serializeInputBinding(action.slots[1]) << "\n";
    }

    if (!file.good()) {
        if (errorText != nullptr) {
            *errorText = "Failed while writing " + path.string() + ".";
        }
        return false;
    }
    return true;
}

bool loadPreferences(
    const std::filesystem::path& path,
    UiState& uiState,
    GraphicsSettings& graphicsSettings,
    LightingSettings& lightingSettings,
    HudSettings& hudSettings,
    ControlProfile& controls,
    AircraftProfile& planeProfile,
    TerrainParams& terrainParams,
    VisualPreferenceData& walkingPrefs,
    std::string* errorText)
{
    if (path.empty()) {
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        if (errorText != nullptr) {
            *errorText = "Could not open " + path.string() + " for reading.";
        }
        return false;
    }

    bool hasLegacyModelPreference = false;
    bool hasHudSettings = false;
    std::filesystem::path legacyModelPath {};
    FlightConfig& config = planeProfile.flightConfig;
    PropAudioConfig& propAudioConfig = planeProfile.propAudioConfig;
    VisualPreferenceData& planePrefs = planeProfile.visualPrefs;
    std::string line;
    while (std::getline(file, line)) {
        line = trimAscii(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        const std::string key = trimAscii(line.substr(0, separator));
        const std::string value = trimAscii(line.substr(separator + 1));
        constexpr std::string_view planePrefix = "character.plane.";
        constexpr std::string_view walkingPrefix = "character.walking.";
        constexpr std::string_view planePaintPrefix = "paint.plane.";
        constexpr std::string_view walkingPaintPrefix = "paint.walking.";
        constexpr std::string_view planePaintModelPrefix = "paint.plane.model.";
        constexpr std::string_view walkingPaintModelPrefix = "paint.walking.model.";
        constexpr std::string_view controlsPrefix = "controls.";

        if (key.rfind(planePrefix, 0) == 0) {
            applyVisualPreferenceValue(planePrefs, key.substr(planePrefix.size()), value);
            continue;
        }
        if (key.rfind(walkingPrefix, 0) == 0) {
            applyVisualPreferenceValue(walkingPrefs, key.substr(walkingPrefix.size()), value);
            continue;
        }
        if (key.rfind(planePaintModelPrefix, 0) == 0) {
            const std::string modelKey = key.substr(planePaintModelPrefix.size());
            if (!modelKey.empty()) {
                if (value.empty()) {
                    planePrefs.paintHashesByModelKey.erase(modelKey);
                } else {
                    planePrefs.paintHashesByModelKey[modelKey] = value;
                }
            }
            continue;
        }
        if (key.rfind(walkingPaintModelPrefix, 0) == 0) {
            const std::string modelKey = key.substr(walkingPaintModelPrefix.size());
            if (!modelKey.empty()) {
                if (value.empty()) {
                    walkingPrefs.paintHashesByModelKey.erase(modelKey);
                } else {
                    walkingPrefs.paintHashesByModelKey[modelKey] = value;
                }
            }
            continue;
        }
        if (key.rfind(planePaintPrefix, 0) == 0) {
            if (key.substr(planePaintPrefix.size()) == "hash") {
                planePrefs.paintHash = value;
            }
            continue;
        }
        if (key.rfind(walkingPaintPrefix, 0) == 0) {
            if (key.substr(walkingPaintPrefix.size()) == "hash") {
                walkingPrefs.paintHash = value;
            }
            continue;
        }
        if (key.rfind(controlsPrefix, 0) == 0) {
            const std::string suffix = key.substr(controlsPrefix.size());
            const std::size_t split = suffix.find('.');
            if (split != std::string::npos) {
                const std::string actionKey = suffix.substr(0, split);
                const std::string slotKey = suffix.substr(split + 1);
                if (const auto actionId = controlActionFromStorageKey(actionKey); actionId.has_value()) {
                    if (ControlActionBinding* action = findControlAction(controls, *actionId); action != nullptr && action->supported) {
                        if (slotKey == "primary") {
                            action->slots[0] = parseInputBinding(value);
                        } else if (slotKey == "secondary") {
                            action->slots[1] = parseInputBinding(value);
                        }
                    }
                }
            }
            continue;
        }

        if (key == "ui.chase_camera") {
            uiState.chaseCamera = parseBoolValue(value, uiState.chaseCamera);
        } else if (key == "ui.show_map") {
            uiState.showMap = parseBoolValue(value, uiState.showMap);
        } else if (key == "ui.show_debug") {
            uiState.showDebug = parseBoolValue(value, uiState.showDebug);
        } else if (key == "ui.show_crosshair") {
            uiState.showCrosshair = parseBoolValue(value, uiState.showCrosshair);
        } else if (key == "ui.show_throttle") {
            uiState.showThrottleHud = parseBoolValue(value, uiState.showThrottleHud);
        } else if (key == "ui.show_controls") {
            uiState.showControlIndicator = parseBoolValue(value, uiState.showControlIndicator);
        } else if (key == "ui.show_geo") {
            uiState.showGeoInfo = parseBoolValue(value, uiState.showGeoInfo);
        } else if (key == "ui.invert_look_y") {
            uiState.invertLookY = parseBoolValue(value, uiState.invertLookY);
        } else if (key == "ui.map_north_up") {
            uiState.mapNorthUp = parseBoolValue(value, uiState.mapNorthUp);
        } else if (key == "ui.map_zoom_index") {
            uiState.mapZoomIndex = parseIntValue(value, uiState.mapZoomIndex);
        } else if (key == "ui.camera_fov_degrees") {
            uiState.cameraFovDegrees = parseFloatValue(value, uiState.cameraFovDegrees);
        } else if (key == "ui.walking_move_speed") {
            uiState.walkingMoveSpeed = parseFloatValue(value, uiState.walkingMoveSpeed);
        } else if (key == "ui.mouse_sensitivity") {
            uiState.mouseSensitivity = parseFloatValue(value, uiState.mouseSensitivity);
        } else if (key == "ui.audio_enabled") {
            uiState.audioEnabled = parseBoolValue(value, uiState.audioEnabled);
        } else if (key == "ui.master_volume") {
            uiState.masterVolume = parseFloatValue(value, uiState.masterVolume);
        } else if (key == "ui.engine_volume") {
            uiState.engineVolume = parseFloatValue(value, uiState.engineVolume);
        } else if (key == "ui.ambience_volume") {
            uiState.ambienceVolume = parseFloatValue(value, uiState.ambienceVolume);
        } else if (key == "graphics.window_mode") {
            graphicsSettings.windowMode = parseWindowModeToken(value);
        } else if (key == "graphics.resolution_width") {
            graphicsSettings.resolutionWidth = parseIntValue(value, graphicsSettings.resolutionWidth);
        } else if (key == "graphics.resolution_height") {
            graphicsSettings.resolutionHeight = parseIntValue(value, graphicsSettings.resolutionHeight);
        } else if (key == "graphics.render_scale") {
            graphicsSettings.renderScale = parseFloatValue(value, graphicsSettings.renderScale);
        } else if (key == "graphics.draw_distance") {
            graphicsSettings.drawDistance = parseFloatValue(value, graphicsSettings.drawDistance);
        } else if (key == "graphics.horizon_fog") {
            graphicsSettings.horizonFog = parseBoolValue(value, graphicsSettings.horizonFog);
        } else if (key == "graphics.texture_mipmaps") {
            graphicsSettings.textureMipmaps = parseBoolValue(value, graphicsSettings.textureMipmaps);
        } else if (key == "graphics.vsync") {
            graphicsSettings.vsync = parseBoolValue(value, graphicsSettings.vsync);
        } else if (key == "lighting.show_sun_marker") {
            lightingSettings.showSunMarker = parseBoolValue(value, lightingSettings.showSunMarker);
        } else if (key == "lighting.sun_yaw_degrees") {
            lightingSettings.sunYawDegrees = parseFloatValue(value, lightingSettings.sunYawDegrees);
        } else if (key == "lighting.sun_pitch_degrees") {
            lightingSettings.sunPitchDegrees = parseFloatValue(value, lightingSettings.sunPitchDegrees);
        } else if (key == "lighting.sun_intensity") {
            lightingSettings.sunIntensity = parseFloatValue(value, lightingSettings.sunIntensity);
        } else if (key == "lighting.ambient") {
            lightingSettings.ambient = parseFloatValue(value, lightingSettings.ambient);
        } else if (key == "lighting.marker_distance") {
            lightingSettings.markerDistance = parseFloatValue(value, lightingSettings.markerDistance);
        } else if (key == "lighting.marker_size") {
            lightingSettings.markerSize = parseFloatValue(value, lightingSettings.markerSize);
        } else if (key == "lighting.shadow_enabled") {
            lightingSettings.shadowEnabled = parseBoolValue(value, lightingSettings.shadowEnabled);
        } else if (key == "lighting.shadow_softness") {
            lightingSettings.shadowSoftness = parseFloatValue(value, lightingSettings.shadowSoftness);
        } else if (key == "lighting.shadow_distance") {
            lightingSettings.shadowDistance = parseFloatValue(value, lightingSettings.shadowDistance);
        } else if (key == "lighting.gi_specular") {
            lightingSettings.specularAmbient = parseFloatValue(value, lightingSettings.specularAmbient);
        } else if (key == "lighting.gi_bounce") {
            lightingSettings.bounceStrength = parseFloatValue(value, lightingSettings.bounceStrength);
        } else if (key == "lighting.fog_density") {
            lightingSettings.fogDensity = parseFloatValue(value, lightingSettings.fogDensity);
        } else if (key == "lighting.fog_height_falloff") {
            lightingSettings.fogHeightFalloff = parseFloatValue(value, lightingSettings.fogHeightFalloff);
        } else if (key == "lighting.exposure_ev") {
            lightingSettings.exposureEv = parseFloatValue(value, lightingSettings.exposureEv);
        } else if (key == "lighting.turbidity") {
            lightingSettings.turbidity = parseFloatValue(value, lightingSettings.turbidity);
        } else if (key == "lighting.sun_tint_r") {
            lightingSettings.sunTint.x = parseFloatValue(value, lightingSettings.sunTint.x);
        } else if (key == "lighting.sun_tint_g") {
            lightingSettings.sunTint.y = parseFloatValue(value, lightingSettings.sunTint.y);
        } else if (key == "lighting.sun_tint_b") {
            lightingSettings.sunTint.z = parseFloatValue(value, lightingSettings.sunTint.z);
        } else if (key == "lighting.sky_tint_r") {
            lightingSettings.skyTint.x = parseFloatValue(value, lightingSettings.skyTint.x);
        } else if (key == "lighting.sky_tint_g") {
            lightingSettings.skyTint.y = parseFloatValue(value, lightingSettings.skyTint.y);
        } else if (key == "lighting.sky_tint_b") {
            lightingSettings.skyTint.z = parseFloatValue(value, lightingSettings.skyTint.z);
        } else if (key == "lighting.ground_tint_r") {
            lightingSettings.groundTint.x = parseFloatValue(value, lightingSettings.groundTint.x);
        } else if (key == "lighting.ground_tint_g") {
            lightingSettings.groundTint.y = parseFloatValue(value, lightingSettings.groundTint.y);
        } else if (key == "lighting.ground_tint_b") {
            lightingSettings.groundTint.z = parseFloatValue(value, lightingSettings.groundTint.z);
        } else if (key == "lighting.fog_r") {
            lightingSettings.fogColor.x = parseFloatValue(value, lightingSettings.fogColor.x);
        } else if (key == "lighting.fog_g") {
            lightingSettings.fogColor.y = parseFloatValue(value, lightingSettings.fogColor.y);
        } else if (key == "lighting.fog_b") {
            lightingSettings.fogColor.z = parseFloatValue(value, lightingSettings.fogColor.z);
        } else if (key == "hud.show_speedometer") {
            hasHudSettings = true;
            hudSettings.showSpeedometer = parseBoolValue(value, hudSettings.showSpeedometer);
        } else if (key == "hud.show_debug") {
            hasHudSettings = true;
            hudSettings.showDebug = parseBoolValue(value, hudSettings.showDebug);
        } else if (key == "hud.show_throttle") {
            hasHudSettings = true;
            hudSettings.showThrottle = parseBoolValue(value, hudSettings.showThrottle);
        } else if (key == "hud.show_controls") {
            hasHudSettings = true;
            hudSettings.showControls = parseBoolValue(value, hudSettings.showControls);
        } else if (key == "hud.show_map") {
            hasHudSettings = true;
            hudSettings.showMap = parseBoolValue(value, hudSettings.showMap);
        } else if (key == "hud.show_geo") {
            hasHudSettings = true;
            hudSettings.showGeoInfo = parseBoolValue(value, hudSettings.showGeoInfo);
        } else if (key == "hud.speedometer_max_kph") {
            hasHudSettings = true;
            hudSettings.speedometerMaxKph = parseIntValue(value, hudSettings.speedometerMaxKph);
        } else if (key == "hud.speedometer_minor_step_kph") {
            hasHudSettings = true;
            hudSettings.speedometerMinorStepKph = parseIntValue(value, hudSettings.speedometerMinorStepKph);
        } else if (key == "hud.speedometer_major_step_kph") {
            hasHudSettings = true;
            hudSettings.speedometerMajorStepKph = parseIntValue(value, hudSettings.speedometerMajorStepKph);
        } else if (key == "hud.speedometer_label_step_kph") {
            hasHudSettings = true;
            hudSettings.speedometerLabelStepKph = parseIntValue(value, hudSettings.speedometerLabelStepKph);
        } else if (key == "hud.speedometer_redline_kph") {
            hasHudSettings = true;
            hudSettings.speedometerRedlineKph = parseIntValue(value, hudSettings.speedometerRedlineKph);
        } else if (key == "aircraft.plane.audio.base_rpm") {
            propAudioConfig.baseRpm = parseFloatValue(value, propAudioConfig.baseRpm);
        } else if (key == "aircraft.plane.audio.load_rpm_contribution") {
            propAudioConfig.loadRpmContribution = parseFloatValue(value, propAudioConfig.loadRpmContribution);
        } else if (key == "aircraft.plane.audio.engine_frequency_scale") {
            propAudioConfig.engineFrequencyScale = parseFloatValue(value, propAudioConfig.engineFrequencyScale);
        } else if (key == "aircraft.plane.audio.engine_tonal_mix") {
            propAudioConfig.engineTonalMix = parseFloatValue(value, propAudioConfig.engineTonalMix);
        } else if (key == "aircraft.plane.audio.prop_harmonic_mix") {
            propAudioConfig.propHarmonicMix = parseFloatValue(value, propAudioConfig.propHarmonicMix);
        } else if (key == "aircraft.plane.audio.engine_noise_amount") {
            propAudioConfig.engineNoiseAmount = parseFloatValue(value, propAudioConfig.engineNoiseAmount);
        } else if (key == "aircraft.plane.audio.ambience_frequency_scale") {
            propAudioConfig.ambienceFrequencyScale = parseFloatValue(value, propAudioConfig.ambienceFrequencyScale);
        } else if (key == "aircraft.plane.audio.water_ambience_gain") {
            propAudioConfig.waterAmbienceGain = parseFloatValue(value, propAudioConfig.waterAmbienceGain);
        } else if (key == "aircraft.plane.audio.ground_ambience_gain") {
            propAudioConfig.groundAmbienceGain = parseFloatValue(value, propAudioConfig.groundAmbienceGain);
        } else if (key == "flight.mass_kg") {
            config.massKg = parseFloatValue(value, config.massKg);
        } else if (key == "flight.max_thrust") {
            config.maxThrustSeaLevel = parseFloatValue(value, config.maxThrustSeaLevel);
        } else if (key == "flight.cl_alpha") {
            config.CLalpha = parseFloatValue(value, config.CLalpha);
        } else if (key == "flight.cd0") {
            config.CD0 = parseFloatValue(value, config.CD0);
        } else if (key == "flight.induced_drag_k") {
            config.inducedDragK = parseFloatValue(value, config.inducedDragK);
        } else if (key == "flight.cm_alpha") {
            config.CmAlpha = parseFloatValue(value, config.CmAlpha);
        } else if (key == "flight.pitch_control_scale") {
            config.pitchControlScale = parseFloatValue(value, config.pitchControlScale);
        } else if (key == "flight.roll_control_scale") {
            config.rollControlScale = parseFloatValue(value, config.rollControlScale);
        } else if (key == "flight.yaw_control_scale") {
            config.yawControlScale = parseFloatValue(value, config.yawControlScale);
        } else if (key == "flight.max_elevator_deg") {
            config.maxElevatorDeflectionRad = radians(parseFloatValue(value, degrees(config.maxElevatorDeflectionRad)));
        } else if (key == "flight.max_aileron_deg") {
            config.maxAileronDeflectionRad = radians(parseFloatValue(value, degrees(config.maxAileronDeflectionRad)));
        } else if (key == "flight.auto_trim") {
            config.enableAutoTrim = parseBoolValue(value, config.enableAutoTrim);
        } else if (key == "flight.ground_friction") {
            config.groundFriction = parseFloatValue(value, config.groundFriction);
        } else if (applyTerrainPreferenceValue(terrainParams, key, value)) {
        } else if (key == "model.source_path") {
            legacyModelPath = value.empty() ? std::filesystem::path {} : std::filesystem::path(value);
            hasLegacyModelPreference = true;
        }
    }

    if (!planePrefs.hasStoredPath && hasLegacyModelPreference) {
        planePrefs.sourcePath = legacyModelPath;
        planePrefs.hasStoredPath = true;
    }

    clampUiStatePersistentValues(uiState);
    clampGraphicsSettings(graphicsSettings);
    clampLightingSettings(lightingSettings);
    if (!hasHudSettings) {
        syncHudFromUiState(hudSettings, uiState);
    }
    clampHudSettings(hudSettings);
    syncUiStateFromHud(uiState, hudSettings);
    clampPropAudioConfigValues(propAudioConfig);
    clampTuningConfig(config);
    clampVisualPreferenceData(planePrefs);
    clampVisualPreferenceData(walkingPrefs);
    terrainParams = normalizeTerrainParams(terrainParams);
    return true;
}

void schedulePreferencesSave(bool& preferencesDirty, float& nextSaveAt, float nowSeconds)
{
    if (!preferencesDirty) {
        preferencesDirty = true;
        nextSaveAt = nowSeconds + 0.25f;
        return;
    }

    nextSaveAt = std::min(nextSaveAt, nowSeconds + 0.25f);
}

std::vector<AssetEntry> scanModelAssets()
{
    std::vector<AssetEntry> entries;
    const auto modelDirectory = findAssetPath("portSource/Assets/Models");
    if (modelDirectory.empty()) {
        return entries;
    }

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(modelDirectory, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }

        const std::string extension = toLowerAscii(entry.path().extension().string());
        if (extension != ".stl" && extension != ".glb" && extension != ".gltf") {
            continue;
        }

        AssetEntry asset;
        asset.path = entry.path();
        asset.label = entry.path().filename().string();
        asset.supported = extension == ".stl" || extension == ".glb" || extension == ".gltf";
        entries.push_back(std::move(asset));
    }

    std::sort(entries.begin(), entries.end(), [](const AssetEntry& lhs, const AssetEntry& rhs) {
        return lhs.label < rhs.label;
    });
    return entries;
}

void setPauseStatus(PauseState& pauseState, std::string statusText, float nowSeconds, float duration = 2.8f)
{
    pauseState.statusText = std::move(statusText);
    pauseState.statusUntil = nowSeconds + duration;
}

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

void setBuiltinPlaneModel(PlaneVisualState& planeVisual)
{
    planeVisual.sourceModel = makeCubeModel();
    planeVisual.sourceModel.assetKey = "builtin:cube";
    touchModelCacheRevision(planeVisual.sourceModel);
    updateVisualPaintTargetKey(planeVisual);
    planeVisual.model = planeVisual.sourceModel;
    planeVisual.label = "builtin cube";
    planeVisual.sourcePath.clear();
    planeVisual.usesStl = false;
    planeVisual.importRotationOffset = makePlaneImportRotationOffset(false);
    planeVisual.forwardAxisYawDegrees = -90.0f;
    planeVisual.scale = planeVisual.defaultScale;
    planeVisual.previewZoom = 1.0f;
    planeVisual.previewAutoSpin = true;
    planeVisual.yawDegrees = 0.0f;
    planeVisual.pitchDegrees = 0.0f;
    planeVisual.rollDegrees = 0.0f;
    planeVisual.modelOffset = {};
    resetVisualPaint(planeVisual);
}

std::string tryRestoreStoredPaintForCurrentModel(PlaneVisualState& visual)
{
    const std::string storedPaintHash = resolveStoredPaintHashForCurrentModel(visual);
    if (storedPaintHash.empty()) {
        return {};
    }

    std::string paintError;
    if (loadPaintOverlayByHash(storedPaintHash, visual, &paintError)) {
        return storedPaintHash;
    }

    storePaintHashForCurrentModel(visual, {});
    resetVisualPaint(visual);
    if (!paintError.empty()) {
        SDL_Log("Stored model paint load failed for %s: %s", visual.label.c_str(), paintError.c_str());
    }
    return {};
}

bool loadPlaneModelFromPath(const std::filesystem::path& path, PlaneVisualState& planeVisual, std::string* statusText)
{
    const std::string extension = toLowerAscii(path.extension().string());
    if (extension == ".stl") {
        std::string stlError;
        if (auto loaded = loadStl(path, &stlError)) {
            planeVisual.sourceModel = normalizeModel(*loaded, 2.2f);
            touchModelCacheRevision(planeVisual.sourceModel);
            updateVisualPaintTargetKey(planeVisual);
            planeVisual.model = planeVisual.sourceModel;
            planeVisual.label = path.filename().string();
            planeVisual.sourcePath = path;
            planeVisual.usesStl = true;
            planeVisual.importRotationOffset = makePlaneImportRotationOffset(true);
            planeVisual.forwardAxisYawDegrees = -90.0f;
            planeVisual.scale = planeVisual.defaultScale;
            planeVisual.previewZoom = 1.0f;
            planeVisual.previewAutoSpin = true;
            planeVisual.yawDegrees = 0.0f;
            planeVisual.pitchDegrees = 0.0f;
            planeVisual.rollDegrees = 0.0f;
            planeVisual.modelOffset = {};
            resetVisualPaint(planeVisual);
            const std::string restoredPaintHash = tryRestoreStoredPaintForCurrentModel(planeVisual);
            if (statusText != nullptr) {
                *statusText = "Loaded " + planeVisual.label;
                if (!restoredPaintHash.empty()) {
                    *statusText += " with saved paint " +
                        restoredPaintHash.substr(0, std::min<std::size_t>(8, restoredPaintHash.size()));
                }
            }
            return true;
        }

        if (statusText != nullptr) {
            *statusText = "STL load failed: " + stlError;
        }
        return false;
    }

    if (extension == ".glb" || extension == ".gltf") {
        std::string gltfError;
        if (auto loaded = loadGltf(path, &gltfError)) {
            planeVisual.sourceModel = normalizeModel(*loaded, 2.2f);
            touchModelCacheRevision(planeVisual.sourceModel);
            updateVisualPaintTargetKey(planeVisual);
            planeVisual.model = planeVisual.sourceModel;
            planeVisual.label = path.filename().string();
            planeVisual.sourcePath = path;
            planeVisual.usesStl = false;
            planeVisual.importRotationOffset = makePlaneImportRotationOffset(false);
            planeVisual.forwardAxisYawDegrees = -90.0f;
            planeVisual.scale = planeVisual.defaultScale;
            planeVisual.previewZoom = 1.0f;
            planeVisual.previewAutoSpin = true;
            planeVisual.yawDegrees = 0.0f;
            planeVisual.pitchDegrees = 0.0f;
            planeVisual.rollDegrees = 0.0f;
            planeVisual.modelOffset = {};
            resetVisualPaint(planeVisual);
            const std::string restoredPaintHash = tryRestoreStoredPaintForCurrentModel(planeVisual);
            if (statusText != nullptr) {
                *statusText = "Loaded " + planeVisual.label;
                if (!restoredPaintHash.empty()) {
                    *statusText += " with saved paint " +
                        restoredPaintHash.substr(0, std::min<std::size_t>(8, restoredPaintHash.size()));
                }
            }
            return true;
        }
        if (statusText != nullptr) {
            *statusText = "GLTF load failed: " + gltfError;
        }
        return false;
    }

    if (statusText != nullptr) {
        *statusText = "Unsupported model format: " + path.filename().string();
    }
    return false;
}

void resetFlight(FlightState& plane, FlightRuntimeState& runtime, const TerrainParams& terrainParams, float x = 0.0f, float z = 0.0f)
{
    plane = {};
    runtime = {};

    const float ground = sampleGroundHeight(x, z, terrainParams);
    plane.pos = { x, ground + 190.0f, z };
    plane.rot = quatNormalize(quatFromAxisAngle({ 1.0f, 0.0f, 0.0f }, radians(1.5f)));
    plane.flightVel = { 0.0f, 0.0f, 72.0f };
    plane.vel = plane.flightVel;
    plane.throttle = 0.64f;
    plane.collisionRadius = 3.2f;
}

void resetFlight(FlightState& plane, FlightRuntimeState& runtime, const TerrainFieldContext& terrainContext, float x = 0.0f, float z = 0.0f)
{
    plane = {};
    runtime = {};

    const float ground = sampleGroundHeight(x, z, terrainContext);
    plane.pos = { x, ground + 190.0f, z };
    plane.rot = quatNormalize(quatFromAxisAngle({ 1.0f, 0.0f, 0.0f }, radians(1.5f)));
    plane.flightVel = { 0.0f, 0.0f, 72.0f };
    plane.vel = plane.flightVel;
    plane.throttle = 0.64f;
    plane.collisionRadius = 3.2f;
}

Quat composeWalkingRotation(float yaw, float pitch)
{
    const Quat yawQuat = quatFromAxisAngle({ 0.0f, 1.0f, 0.0f }, wrapAngle(yaw));
    const Vec3 right = rotateVector(yawQuat, { 1.0f, 0.0f, 0.0f });
    const Quat pitchQuat = quatFromAxisAngle(right, clamp(pitch, -kWalkingPitchLimitRadians, kWalkingPitchLimitRadians));
    return quatNormalize(quatMultiply(pitchQuat, yawQuat));
}

void syncWalkingLookFromRotation(const Quat& rotation, float& walkYaw, float& walkPitch)
{
    const Vec3 forward = forwardFromRotation(rotation);
    const float flatMagnitude = std::sqrt((forward.x * forward.x) + (forward.z * forward.z));
    walkPitch = clamp(std::atan2(forward.y, std::max(flatMagnitude, 1.0e-6f)), -kWalkingPitchLimitRadians, kWalkingPitchLimitRadians);
    walkYaw = wrapAngle(std::atan2(forward.x, forward.z));
}

void applyWalkingMouseInput(
    UiState& uiState,
    const ControlProfile& controls,
    FlightState& actor,
    float& walkYaw,
    float& walkPitch,
    float dx,
    float dy,
    SDL_Keymod modifiers)
{
    const float mouseSensitivity = kFlightMouseSensitivity * clamp(uiState.mouseSensitivity, 0.1f, 4.0f);
    float pitchAxis =
        controlMouseAxisValue(controls, InputActionId::WalkLookDown, dx, dy, modifiers) -
        controlMouseAxisValue(controls, InputActionId::WalkLookUp, dx, dy, modifiers);
    float yawAxis =
        controlMouseAxisValue(controls, InputActionId::WalkLookRight, dx, dy, modifiers) -
        controlMouseAxisValue(controls, InputActionId::WalkLookLeft, dx, dy, modifiers);
    if (uiState.invertLookY) {
        pitchAxis = -pitchAxis;
    }

    if (pitchAxis == 0.0f && yawAxis == 0.0f) {
        return;
    }

    walkPitch = clamp(walkPitch + (pitchAxis * mouseSensitivity * 2.4f), -kWalkingPitchLimitRadians, kWalkingPitchLimitRadians);
    walkYaw = wrapAngle(walkYaw + (yawAxis * mouseSensitivity * 2.8f));
    actor.rot = composeWalkingRotation(walkYaw, walkPitch);
}

void stepWalking(
    FlightState& actor,
    float dt,
    const WalkingInputState& input,
    const TerrainFieldContext& terrainContext,
    float baseMoveSpeed,
    const TerrainVisualCache* terrainCache,
    float* brushAmountOut)
{
    dt = clamp(dt, 0.0f, 0.05f);
    if (dt <= 0.0f) {
        if (brushAmountOut != nullptr) {
            *brushAmountOut = 0.0f;
        }
        return;
    }

    Vec3 forward = forwardFromRotation(actor.rot);
    forward.y = 0.0f;
    if (lengthSquared(forward) > 1.0e-6f) {
        forward = normalize(forward, { 0.0f, 0.0f, 1.0f });
    }
    Vec3 right = rightFromRotation(actor.rot);
    right.y = 0.0f;
    if (lengthSquared(right) > 1.0e-6f) {
        right = normalize(right, { 1.0f, 0.0f, 0.0f });
    }

    Vec3 moveDir {};
    const float forwardAxis = clamp(
        (input.forward ? 1.0f : 0.0f) -
            (input.backward ? 1.0f : 0.0f) +
            input.forwardAxis,
        -1.0f,
        1.0f);
    const float rightAxis = clamp(
        (input.right ? 1.0f : 0.0f) -
            (input.left ? 1.0f : 0.0f) +
            input.rightAxis,
        -1.0f,
        1.0f);
    const float moveAmount = clamp(std::sqrt((forwardAxis * forwardAxis) + (rightAxis * rightAxis)), 0.0f, 1.0f);
    if (std::fabs(forwardAxis) > 1.0e-4f) {
        moveDir += forward * forwardAxis;
    }
    if (std::fabs(rightAxis) > 1.0e-4f) {
        moveDir += right * rightAxis;
    }
    if (lengthSquared(moveDir) > 1.0e-6f && moveAmount > 1.0e-4f) {
        moveDir = normalize(moveDir, {}) * moveAmount;
    }

    const float speed = clamp(baseMoveSpeed, 2.0f, 30.0f) * (input.sprint ? kWalkingSprintMultiplier : 1.0f);
    actor.pos += moveDir * (speed * dt);
    actor.vel.x = moveDir.x * speed;
    actor.vel.z = moveDir.z * speed;
    actor.vel.y += kWalkingGravity * dt;
    actor.pos.y += actor.vel.y * dt;

    const float ground = sampleGroundHeight(actor.pos.x, actor.pos.z, terrainContext);
    if (actor.pos.y <= (ground + kWalkingHalfHeight)) {
        actor.pos.y = ground + kWalkingHalfHeight;
        actor.vel.y = 0.0f;
        actor.onGround = true;
    } else {
        actor.onGround = false;
    }

    if (input.jump && actor.onGround) {
        actor.vel.y = kWalkingJumpSpeed;
        actor.onGround = false;
    }

    if (terrainCache != nullptr) {
        resolveWalkingPropCollisions(*terrainCache, terrainContext.params.decoration, actor);
    }

    if (brushAmountOut != nullptr && terrainCache != nullptr) {
        *brushAmountOut = computeBrushContactAmount(
            *terrainCache,
            actor.pos,
            kWalkingCollisionRadius,
            actor.pos.y - kWalkingHalfHeight,
            actor.pos.y + 0.25f);
    } else if (brushAmountOut != nullptr) {
        *brushAmountOut = 0.0f;
    }

    actor.flightVel = actor.vel;
    actor.flightAngVel = {};
    actor.debug.tick += 1;
    actor.debug.substeps = 1;
    actor.debug.alpha = 0.0f;
    actor.debug.beta = 0.0f;
    actor.debug.qbar = 0.0f;
    actor.debug.thrust = 0.0f;
    actor.debug.throttle = 0.0f;
    actor.debug.speed = length(actor.vel);
}

TerrainCrater buildCrashCrater(float impactX, float impactY, float impactZ, float impactSpeed)
{
    const float speed = std::max(0.0f, impactSpeed);
    const float radius = clamp(6.0f + (speed * 0.22f), 6.0f, 28.0f);
    const float depth = clamp(radius * 0.48f, 1.8f, 14.0f);
    return { impactX, impactY, impactZ, radius, depth, 0.14f };
}

void setMouseCapture(SDL_Window* window, bool capture)
{
    SDL_SetWindowRelativeMouseMode(window, capture);
    if (capture) {
        SDL_HideCursor();
    } else {
        SDL_ShowCursor();
    }
}

void setPauseActive(PauseState& pauseState, UiState& uiState, bool active)
{
    if (pauseState.active == active) {
        return;
    }

    pauseState.active = active;
    uiState.mapHeld = false;
    uiState.mapUsedForZoom = false;
    uiState.zoomHeld = false;
    pauseState.rowDragActive = false;
    clearMenuConfirmation(pauseState);
    if (active) {
        pauseState.tab = PauseTab::Main;
        pauseState.selectedIndex = 0;
    }
}

void cyclePauseTab(PauseState& pauseState, int delta)
{
    constexpr int tabCount = static_cast<int>(kPauseTabs.size());
    int index = static_cast<int>(pauseState.tab);
    index = (index + delta + tabCount) % tabCount;
    pauseState.tab = static_cast<PauseTab>(index);
    pauseState.selectedIndex = 0;
    pauseState.rowDragActive = false;
    clearMenuConfirmation(pauseState);
}

void cyclePauseSubTab(PauseState& pauseState, int delta)
{
    if (pauseState.tab == PauseTab::Settings) {
        constexpr int count = 7;
        int index = static_cast<int>(pauseState.settingsSubTab);
        index = (index + delta + count) % count;
        pauseState.settingsSubTab = static_cast<SettingsSubTab>(index);
        pauseState.selectedIndex = 0;
        pauseState.rowDragActive = false;
        clearMenuConfirmation(pauseState);
    } else if (pauseState.tab == PauseTab::Characters || pauseState.tab == PauseTab::Paint) {
        constexpr int count = 2;
        int index = static_cast<int>(activeRoleForTab(pauseState, pauseState.tab));
        index = (index + delta + count) % count;
        setActiveRoleForTab(pauseState, pauseState.tab, static_cast<CharacterSubTab>(index));
        pauseState.selectedIndex = 0;
        pauseState.rowDragActive = false;
        clearMenuConfirmation(pauseState);
    }
}

int pauseItemCount(const PauseState& pauseState, std::size_t assetCount)
{
    switch (pauseState.tab) {
    case PauseTab::Main:
        return 6;
    case PauseTab::Settings:
        return settingsSubTabItemCount(pauseState.settingsSubTab);
    case PauseTab::Characters:
        return characterItemCount(assetCount);
    case PauseTab::Paint:
        return kPaintSettingCount;
    case PauseTab::Hud:
        return 5;
    case PauseTab::Controls:
    case PauseTab::Help:
    default:
        return 0;
    }
}

void movePauseSelection(PauseState& pauseState, int delta, std::size_t assetCount)
{
    const int itemCount = pauseItemCount(pauseState, assetCount);
    if (itemCount <= 0) {
        return;
    }

    pauseState.selectedIndex = (pauseState.selectedIndex + delta + itemCount) % itemCount;
}

bool pauseTabSupportsMouseAdjustment(PauseTab tab)
{
    return tab == PauseTab::Settings || tab == PauseTab::Characters || tab == PauseTab::Paint || tab == PauseTab::Hud;
}

void resetCharacterRowValue(PlaneVisualState& visual, int rowIndex)
{
    switch (rowIndex) {
    case 1:
        visual.previewAutoSpin = true;
        break;
    case 2:
        visual.previewZoom = 1.0f;
        break;
    case 3:
        visual.scale = visual.defaultScale;
        break;
    case 4:
        visual.forwardAxisYawDegrees = -90.0f;
        break;
    case 5:
        visual.yawDegrees = 0.0f;
        break;
    case 6:
        visual.pitchDegrees = 0.0f;
        break;
    case 7:
        visual.rollDegrees = 0.0f;
        break;
    case 8:
        visual.modelOffset.x = 0.0f;
        break;
    case 9:
        visual.modelOffset.y = 0.0f;
        break;
    case 10:
        visual.modelOffset.z = 0.0f;
        break;
    default:
        break;
    }
}

void resetPaintRowValue(PaintUiState& paintUi, int rowIndex)
{
    switch (rowIndex) {
    case 0:
        paintUi.mode = PaintMode::Brush;
        break;
    case 1:
        paintUi.colorIndex = 0;
        break;
    case 2:
        paintUi.brushSize = 28;
        break;
    case 3:
        paintUi.brushOpacity = 1.0f;
        break;
    case 4:
        paintUi.brushHardness = 0.75f;
        break;
    default:
        break;
    }
}

bool tryPaintCanvasStroke(
    const PauseLayout& layout,
    const PauseState& pauseState,
    PaintUiState& paintUi,
    PlaneVisualState& planeVisual,
    PlaneVisualState& walkingVisual,
    float mouseX,
    float mouseY,
    bool captureUndo)
{
    if (pauseState.tab != PauseTab::Paint) {
        return false;
    }

    const RectF canvasRect = paintCanvasRect(layout);
    paintUi.canvasRect = canvasRect;
    if (canvasRect.w <= 0.0f || canvasRect.h <= 0.0f || !pointInRect(mouseX, mouseY, canvasRect)) {
        return false;
    }

    PlaneVisualState& visual = visualForRole(pauseState.paintSubTab, planeVisual, walkingVisual);
    const float u = clamp((mouseX - canvasRect.x) / std::max(canvasRect.w, 1.0f), 0.0f, 1.0f);
    const float v = clamp((mouseY - canvasRect.y) / std::max(canvasRect.h, 1.0f), 0.0f, 1.0f);
    return applyPaintStroke(visual, paintUi, u, v, captureUndo);
}

void handlePauseMouseMove(
    PauseState& pauseState,
    PaintUiState& paintUi,
    PlaneVisualState& planeVisual,
    PlaneVisualState& walkingVisual,
    int width,
    int height,
    std::size_t assetCount,
    float mouseX,
    float mouseY)
{
    const PauseLayout layout = buildPauseLayout(width, height);
    paintUi.canvasRect = paintCanvasRect(layout);
    if (paintUi.draggingCanvas) {
        tryPaintCanvasStroke(layout, pauseState, paintUi, planeVisual, walkingVisual, mouseX, mouseY, false);
        return;
    }

    const int itemIndex = hitPauseItemIndex(layout, pauseState, assetCount, mouseX, mouseY);
    if (itemIndex >= 0) {
        pauseState.selectedIndex = itemIndex;
    }
}

void handlePauseMouseButtonDown(
    PauseState& pauseState,
    UiState& uiState,
    PropAudioConfig& propAudioConfig,
    FlightState& plane,
    FlightRuntimeState& runtime,
    FlightConfig& config,
    TerrainParams& terrainParams,
    TerrainFieldContext& terrainContext,
    TerrainVisualCache& terrainCache,
    const UiState& defaultUiStateValues,
    const PropAudioConfig& defaultPropAudioConfigValues,
    const FlightConfig& defaultConfig,
    const TerrainParams& defaultTerrainParamsValues,
    std::vector<AssetEntry>& assetCatalog,
    PlaneVisualState& planeVisual,
    PlaneVisualState& walkingVisual,
    PaintUiState& paintUi,
    bool& running,
    bool& preferencesDirty,
    float& preferencesNextSaveAt,
    WorldStore* worldStore,
    int width,
    int height,
    float mouseX,
    float mouseY,
    std::uint8_t button,
    float nowSeconds)
{
    const PauseLayout layout = buildPauseLayout(width, height);
    const int tabIndex = hitPauseTabIndex(layout, mouseX, mouseY);
    if (tabIndex >= 0) {
        pauseState.tab = static_cast<PauseTab>(tabIndex);
        pauseState.selectedIndex = 0;
        paintUi.draggingCanvas = false;
        return;
    }

    const int subTabIndex = hitPauseSubTabIndex(layout, pauseState.tab, mouseX, mouseY);
    if (subTabIndex >= 0) {
        if (pauseState.tab == PauseTab::Settings) {
            pauseState.settingsSubTab = static_cast<SettingsSubTab>(subTabIndex);
        } else if (pauseState.tab == PauseTab::Characters || pauseState.tab == PauseTab::Paint) {
            setActiveRoleForTab(pauseState, pauseState.tab, static_cast<CharacterSubTab>(subTabIndex));
        }
        pauseState.selectedIndex = 0;
        paintUi.draggingCanvas = false;
        return;
    }

    if (button == SDL_BUTTON_LEFT && tryPaintCanvasStroke(layout, pauseState, paintUi, planeVisual, walkingVisual, mouseX, mouseY, true)) {
        paintUi.draggingCanvas = true;
        return;
    }

    const int itemIndex = hitPauseItemIndex(layout, pauseState, assetCatalog.size(), mouseX, mouseY);
    if (itemIndex < 0) {
        return;
    }
    pauseState.selectedIndex = itemIndex;

    if (pauseState.tab == PauseTab::Main && button == SDL_BUTTON_LEFT) {
        const int activatedIndex = pauseState.selectedIndex;
        activatePauseSelection(pauseState, uiState, plane, runtime, terrainContext, running);
        if (activatedIndex >= 2 && activatedIndex <= 4) {
            schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, nowSeconds);
        }
        return;
    }

    if (!pauseTabSupportsMouseAdjustment(pauseState.tab)) {
        return;
    }

    PlaneVisualState& selectedCharacterVisual = visualForRole(pauseState.charactersSubTab, planeVisual, walkingVisual);
    PlaneVisualState& selectedPaintVisual = visualForRole(pauseState.paintSubTab, planeVisual, walkingVisual);

    if (button == SDL_BUTTON_RIGHT || button == SDL_BUTTON_MIDDLE) {
        if (pauseState.tab == PauseTab::Settings) {
            if (pauseState.settingsSubTab == SettingsSubTab::Flight) {
                resetTuningValue(config, defaultConfig, pauseState.selectedIndex);
                setPauseStatus(pauseState, "Reset selected flight value to default.", nowSeconds, 2.2f);
            } else if (pauseState.settingsSubTab == SettingsSubTab::Terrain) {
                resetTerrainValue(terrainParams, defaultTerrainParamsValues, pauseState.selectedIndex);
                applyTerrainRuntimeChange(terrainParams, terrainContext, terrainCache, plane, worldStore);
                setPauseStatus(pauseState, "Reset selected terrain value to default.", nowSeconds, 2.2f);
            } else {
                resetSettingsRowValue(uiState, defaultUiStateValues, propAudioConfig, defaultPropAudioConfigValues, pauseState.settingsSubTab, pauseState.selectedIndex);
                setPauseStatus(pauseState, "Reset selected setting to default.", nowSeconds, 2.2f);
            }
        } else if (pauseState.tab == PauseTab::Hud) {
            resetHudValue(uiState, defaultUiStateValues, pauseState.selectedIndex);
            setPauseStatus(pauseState, "Reset selected HUD setting to default.", nowSeconds, 2.2f);
        } else if (pauseState.tab == PauseTab::Characters) {
            resetCharacterRowValue(selectedCharacterVisual, pauseState.selectedIndex);
            setPauseStatus(pauseState, "Reset selected character transform value.", nowSeconds, 2.2f);
        } else if (pauseState.tab == PauseTab::Paint) {
            resetPaintRowValue(paintUi, pauseState.selectedIndex);
            setPauseStatus(pauseState, "Reset selected paint control.", nowSeconds, 2.2f);
        }
        if (pauseState.tab != PauseTab::Paint || pauseState.selectedIndex <= 4) {
            schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, nowSeconds);
        }
        return;
    }

    if (button != SDL_BUTTON_LEFT) {
        return;
    }

    const RectF rowRect = pauseRowRect(layout, pauseState, assetCatalog.size(), pauseState.selectedIndex);
    const int direction = mouseX < (rowRect.x + (rowRect.w * 0.5f)) ? -1 : 1;
    if (pauseState.tab == PauseTab::Settings) {
        if (pauseState.settingsSubTab == SettingsSubTab::Flight) {
            adjustTuningValue(config, pauseState.selectedIndex, direction);
        } else if (pauseState.settingsSubTab == SettingsSubTab::Terrain) {
            adjustTerrainValue(terrainParams, pauseState.selectedIndex, direction);
            applyTerrainRuntimeChange(terrainParams, terrainContext, terrainCache, plane, worldStore);
        } else {
            adjustSettingsRowValue(uiState, propAudioConfig, pauseState.settingsSubTab, pauseState.selectedIndex, direction);
        }
        schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, nowSeconds);
    } else if (pauseState.tab == PauseTab::Hud) {
        adjustHudValue(uiState, pauseState.selectedIndex, direction);
        schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, nowSeconds);
    } else if (pauseState.tab == PauseTab::Characters) {
        if (pauseState.selectedIndex >= 1 && pauseState.selectedIndex <= 10) {
            adjustCharacterRowValue(selectedCharacterVisual, pauseState.selectedIndex, direction);
            schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, nowSeconds);
        } else if (pauseState.selectedIndex == 11) {
            setBuiltinPlaneModel(selectedCharacterVisual);
            setPauseStatus(pauseState, "Loaded builtin cube.", nowSeconds, 2.2f);
            schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, nowSeconds);
        } else if (pauseState.selectedIndex >= kCharacterAssetListStart &&
            pauseState.selectedIndex < characterItemCount(assetCatalog.size())) {
            const AssetEntry& asset = assetCatalog[static_cast<std::size_t>(pauseState.selectedIndex - kCharacterAssetListStart)];
            std::string loadStatus;
            loadPlaneModelFromPath(asset.path, selectedCharacterVisual, &loadStatus);
            setPauseStatus(pauseState, loadStatus, nowSeconds, asset.supported ? 3.0f : 4.5f);
            if (asset.supported) {
                schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, nowSeconds);
            }
        }
    } else if (pauseState.tab == PauseTab::Paint) {
        switch (pauseState.selectedIndex) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
            adjustPaintRowValue(paintUi, pauseState.selectedIndex, direction);
            break;
        case 5:
            if (paintUndo(selectedPaintVisual)) {
                setPauseStatus(pauseState, "Undid last paint step.", nowSeconds, 2.2f);
            }
            break;
        case 6:
            if (paintRedo(selectedPaintVisual)) {
                setPauseStatus(pauseState, "Redid paint step.", nowSeconds, 2.2f);
            }
            break;
        case 7:
            if (fillPaintOverlay(selectedPaintVisual, paintUi.colorIndex)) {
                setPauseStatus(pauseState, "Filled active paint overlay.", nowSeconds, 2.2f);
            }
            break;
        case 8:
            if (clearPaintOverlay(selectedPaintVisual)) {
                setPauseStatus(pauseState, "Cleared active paint overlay.", nowSeconds, 2.2f);
            }
            break;
        case 9: {
            std::string paintHash;
            std::string paintError;
            if (commitPaintOverlay(getPaintStorageDirectory(), selectedPaintVisual, &paintHash, &paintError)) {
                setPauseStatus(pauseState, "Committed paint overlay " + paintHash.substr(0, std::min<std::size_t>(8, paintHash.size())) + ".", nowSeconds, 2.6f);
                schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, nowSeconds);
            } else if (!paintError.empty()) {
                setPauseStatus(pauseState, paintError, nowSeconds, 3.6f);
            }
            break;
        }
        case 10: {
            if (selectedPaintVisual.paintHash.empty()) {
                setPauseStatus(pauseState, "No saved paint overlay is stored for this role.", nowSeconds, 3.2f);
                break;
            }
            std::string paintError;
            if (loadPaintOverlayByHash(selectedPaintVisual.paintHash, selectedPaintVisual, &paintError)) {
                setPauseStatus(pauseState, "Reloaded committed paint overlay.", nowSeconds, 2.4f);
            } else if (!paintError.empty()) {
                setPauseStatus(pauseState, paintError, nowSeconds, 3.6f);
            }
            break;
        }
        default:
            break;
        }
    }
}

void handlePauseMouseWheel(
    PauseState& pauseState,
    UiState& uiState,
    PropAudioConfig& propAudioConfig,
    FlightState& plane,
    FlightConfig& config,
    TerrainParams& terrainParams,
    TerrainFieldContext& terrainContext,
    TerrainVisualCache& terrainCache,
    PlaneVisualState& planeVisual,
    PlaneVisualState& walkingVisual,
    PaintUiState& paintUi,
    bool& preferencesDirty,
    float& preferencesNextSaveAt,
    WorldStore* worldStore,
    int wheelDelta,
    std::size_t assetCount,
    float nowSeconds)
{
    if (wheelDelta == 0) {
        return;
    }

    if (!pauseTabSupportsMouseAdjustment(pauseState.tab)) {
        movePauseSelection(pauseState, wheelDelta > 0 ? -1 : 1, assetCount);
        return;
    }

    const int direction = wheelDelta > 0 ? 1 : -1;
    if (pauseState.tab == PauseTab::Settings) {
        if (pauseState.settingsSubTab == SettingsSubTab::Flight) {
            adjustTuningValue(config, pauseState.selectedIndex, direction);
        } else if (pauseState.settingsSubTab == SettingsSubTab::Terrain) {
            adjustTerrainValue(terrainParams, pauseState.selectedIndex, direction);
            applyTerrainRuntimeChange(terrainParams, terrainContext, terrainCache, plane, worldStore);
        } else {
            adjustSettingsRowValue(uiState, propAudioConfig, pauseState.settingsSubTab, pauseState.selectedIndex, direction);
        }
        schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, nowSeconds);
    } else if (pauseState.tab == PauseTab::Hud) {
        adjustHudValue(uiState, pauseState.selectedIndex, direction);
        schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, nowSeconds);
    } else if (pauseState.tab == PauseTab::Characters) {
        if (pauseState.selectedIndex < 1 || pauseState.selectedIndex > 10) {
            movePauseSelection(pauseState, wheelDelta > 0 ? -1 : 1, assetCount);
            return;
        }
        PlaneVisualState& visual = visualForRole(pauseState.charactersSubTab, planeVisual, walkingVisual);
        adjustCharacterRowValue(visual, pauseState.selectedIndex, direction);
        schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, nowSeconds);
    } else if (pauseState.tab == PauseTab::Paint) {
        if (pauseState.selectedIndex < 0 || pauseState.selectedIndex > 4) {
            movePauseSelection(pauseState, wheelDelta > 0 ? -1 : 1, assetCount);
            return;
        }
        adjustPaintRowValue(paintUi, pauseState.selectedIndex, direction);
    }
}

void changeMapZoom(UiState& uiState, int delta)
{
    uiState.mapZoomIndex = std::clamp(
        uiState.mapZoomIndex + delta,
        0,
        static_cast<int>(uiState.mapZoomExtents.size()) - 1);
}

std::string formatTuningValue(int tuningIndex, const FlightConfig& config)
{
    switch (tuningIndex) {
    case 0:
        return std::to_string(static_cast<int>(std::round(config.massKg))) + " kg";
    case 1:
        return std::to_string(static_cast<int>(std::round(config.maxThrustSeaLevel))) + " N";
    case 2: {
        char buffer[32] {};
        std::snprintf(buffer, sizeof(buffer), "%.2f", config.CLalpha);
        return buffer;
    }
    case 3: {
        char buffer[32] {};
        std::snprintf(buffer, sizeof(buffer), "%.3f", config.CD0);
        return buffer;
    }
    case 4: {
        char buffer[32] {};
        std::snprintf(buffer, sizeof(buffer), "%.3f", config.inducedDragK);
        return buffer;
    }
    case 5: {
        char buffer[32] {};
        std::snprintf(buffer, sizeof(buffer), "%.2f", config.CmAlpha);
        return buffer;
    }
    case 6: {
        char buffer[32] {};
        std::snprintf(buffer, sizeof(buffer), "%.2f", config.pitchControlScale);
        return buffer;
    }
    case 7: {
        char buffer[32] {};
        std::snprintf(buffer, sizeof(buffer), "%.2f", config.rollControlScale);
        return buffer;
    }
    case 8: {
        char buffer[32] {};
        std::snprintf(buffer, sizeof(buffer), "%.2f", config.yawControlScale);
        return buffer;
    }
    case 9: {
        char buffer[32] {};
        std::snprintf(buffer, sizeof(buffer), "%.0f deg", degrees(config.maxElevatorDeflectionRad));
        return buffer;
    }
    case 10: {
        char buffer[32] {};
        std::snprintf(buffer, sizeof(buffer), "%.0f deg", degrees(config.maxAileronDeflectionRad));
        return buffer;
    }
    case 11:
        return config.enableAutoTrim ? "On" : "Off";
    case 12: {
        char buffer[32] {};
        std::snprintf(buffer, sizeof(buffer), "%.2f", config.groundFriction);
        return buffer;
    }
    default:
        return {};
    }
}

const char* tuningHelpText(int tuningIndex)
{
    switch (tuningIndex) {
    case 0:
        return "Aircraft mass used by the rigid-body integrator.";
    case 1:
        return "Sea-level maximum thrust used by the native flight model.";
    case 2:
        return "Lift slope per radian. Higher values increase lift response.";
    case 3:
        return "Zero-lift drag coefficient.";
    case 4:
        return "Quadratic induced drag term.";
    case 5:
        return "Pitch stability derivative. More negative is more stable.";
    case 6:
        return "Pitch control authority scale from the Lua flight model.";
    case 7:
        return "Roll control authority scale from the Lua flight model.";
    case 8:
        return "Yaw control authority scale from the Lua flight model.";
    case 9:
        return "Maximum elevator deflection in degrees.";
    case 10:
        return "Maximum aileron deflection in degrees.";
    case 11:
        return "Automatic trim assist for near-level cruise.";
    case 12:
        return "Tangential damping applied on terrain contact.";
    default:
        return "";
    }
}

void adjustTuningValue(FlightConfig& config, int tuningIndex, int direction)
{
    switch (tuningIndex) {
    case 0:
        config.massKg = clamp(config.massKg + (10.0f * static_cast<float>(direction)), 450.0f, 4000.0f);
        break;
    case 1:
        config.maxThrustSeaLevel = clamp(config.maxThrustSeaLevel + (50.0f * static_cast<float>(direction)), 800.0f, 12000.0f);
        break;
    case 2:
        config.CLalpha = clamp(config.CLalpha + (0.05f * static_cast<float>(direction)), 2.0f, 8.5f);
        break;
    case 3:
        config.CD0 = clamp(config.CD0 + (0.002f * static_cast<float>(direction)), 0.01f, 0.12f);
        break;
    case 4:
        config.inducedDragK = clamp(config.inducedDragK + (0.002f * static_cast<float>(direction)), 0.01f, 0.18f);
        break;
    case 5:
        config.CmAlpha = clamp(config.CmAlpha + (0.02f * static_cast<float>(direction)), -2.8f, -0.1f);
        break;
    case 6:
        config.pitchControlScale = clamp(config.pitchControlScale + (0.02f * static_cast<float>(direction)), 0.05f, 1.5f);
        break;
    case 7:
        config.rollControlScale = clamp(config.rollControlScale + (0.02f * static_cast<float>(direction)), 0.05f, 1.5f);
        break;
    case 8:
        config.yawControlScale = clamp(config.yawControlScale + (0.02f * static_cast<float>(direction)), 0.05f, 1.5f);
        break;
    case 9:
        config.maxElevatorDeflectionRad = clamp(
            config.maxElevatorDeflectionRad + radians(1.0f * static_cast<float>(direction)),
            radians(10.0f),
            radians(45.0f));
        break;
    case 10:
        config.maxAileronDeflectionRad = clamp(
            config.maxAileronDeflectionRad + radians(1.0f * static_cast<float>(direction)),
            radians(8.0f),
            radians(35.0f));
        break;
    case 11:
        if (direction != 0) {
            config.enableAutoTrim = !config.enableAutoTrim;
        }
        break;
    case 12:
        config.groundFriction = clamp(config.groundFriction + (0.01f * static_cast<float>(direction)), 0.20f, 0.995f);
        break;
    default:
        break;
    }
}

void resetTuningValue(FlightConfig& config, const FlightConfig& defaults, int tuningIndex)
{
    switch (tuningIndex) {
    case 0:
        config.massKg = defaults.massKg;
        break;
    case 1:
        config.maxThrustSeaLevel = defaults.maxThrustSeaLevel;
        break;
    case 2:
        config.CLalpha = defaults.CLalpha;
        break;
    case 3:
        config.CD0 = defaults.CD0;
        break;
    case 4:
        config.inducedDragK = defaults.inducedDragK;
        break;
    case 5:
        config.CmAlpha = defaults.CmAlpha;
        break;
    case 6:
        config.pitchControlScale = defaults.pitchControlScale;
        break;
    case 7:
        config.rollControlScale = defaults.rollControlScale;
        break;
    case 8:
        config.yawControlScale = defaults.yawControlScale;
        break;
    case 9:
        config.maxElevatorDeflectionRad = defaults.maxElevatorDeflectionRad;
        break;
    case 10:
        config.maxAileronDeflectionRad = defaults.maxAileronDeflectionRad;
        break;
    case 11:
        config.enableAutoTrim = defaults.enableAutoTrim;
        break;
    case 12:
        config.groundFriction = defaults.groundFriction;
        break;
    default:
        break;
    }
}

int terrainVisibleStartIndex(int selectedIndex)
{
    return std::clamp(selectedIndex - (kTerrainVisibleRows / 2), 0, std::max(0, kTerrainSettingCount - kTerrainVisibleRows));
}

int settingsVisibleStartIndex(SettingsSubTab subTab, int selectedIndex)
{
    const int itemCount = settingsSubTabItemCount(subTab);
    if (itemCount <= kSettingsVisibleRows) {
        return 0;
    }
    return std::clamp(selectedIndex - (kSettingsVisibleRows / 2), 0, std::max(0, itemCount - kSettingsVisibleRows));
}

std::string formatTerrainValue(int terrainIndex, const TerrainParams& terrainParams)
{
    char buffer[64] {};
    switch (terrainIndex) {
    case 0:
        return std::to_string(terrainParams.seed);
    case 1:
        return std::to_string(static_cast<int>(std::round(terrainParams.chunkSize))) + " u";
    case 2:
        return std::to_string(static_cast<int>(std::round(terrainParams.worldRadius))) + " u";
    case 3:
        return std::to_string(static_cast<int>(std::round(terrainParams.gameplayRadiusMeters))) + " u";
    case 4:
        return std::to_string(static_cast<int>(std::round(terrainParams.midFieldRadiusMeters))) + " u";
    case 5:
        return std::to_string(static_cast<int>(std::round(terrainParams.horizonRadiusMeters))) + " u";
    case 6:
        std::snprintf(buffer, sizeof(buffer), "%.1f", terrainParams.terrainQuality);
        return buffer;
    case 7:
        return std::to_string(terrainParams.meshBuildBudget);
    case 8:
        return std::to_string(terrainParams.lod0Radius);
    case 9:
        return std::to_string(terrainParams.lod1Radius);
    case 10:
        return std::to_string(terrainParams.lod2Radius);
    case 11:
        return std::to_string(static_cast<int>(std::round(terrainParams.heightAmplitude))) + " u";
    case 12:
        std::snprintf(buffer, sizeof(buffer), "%.4f", terrainParams.heightFrequency);
        return buffer;
    case 13:
        return std::to_string(static_cast<int>(std::round(terrainParams.surfaceDetailAmplitude))) + " u";
    case 14:
        return std::to_string(static_cast<int>(std::round(terrainParams.ridgeAmplitude))) + " u";
    case 15:
        std::snprintf(buffer, sizeof(buffer), "%.2f", terrainParams.terraceStrength);
        return buffer;
    case 16:
        return std::to_string(static_cast<int>(std::round(terrainParams.waterLevel))) + " u";
    case 17:
        return std::to_string(static_cast<int>(std::round(terrainParams.snowLine))) + " u";
    case 18:
        return terrainParams.decoration.enabled ? "On" : "Off";
    case 19:
        std::snprintf(buffer, sizeof(buffer), "%.2f", terrainParams.decoration.density);
        return buffer;
    case 20:
        std::snprintf(buffer, sizeof(buffer), "%.2f", terrainParams.decoration.nearDensityScale);
        return buffer;
    case 21:
        std::snprintf(buffer, sizeof(buffer), "%.2f", terrainParams.decoration.midDensityScale);
        return buffer;
    case 22:
        std::snprintf(buffer, sizeof(buffer), "%.2f", terrainParams.decoration.farDensityScale);
        return buffer;
    case 23:
        std::snprintf(buffer, sizeof(buffer), "%.2f", terrainParams.decoration.shoreBrushDensity);
        return buffer;
    case 24:
        std::snprintf(buffer, sizeof(buffer), "%.2f", terrainParams.decoration.rockDensity);
        return buffer;
    case 25:
        return std::to_string(static_cast<int>(std::round(terrainParams.decoration.treeLineOffset))) + " u";
    case 26:
        return terrainParams.decoration.collisionEnabled ? "On" : "Off";
    case 27:
        return std::to_string(terrainParams.decoration.seedOffset);
    case 28:
        return terrainParams.caveEnabled ? "On" : "Off";
    case 29:
        return std::to_string(static_cast<int>(std::round(terrainParams.caveStrength))) + " u";
    case 30:
        std::snprintf(buffer, sizeof(buffer), "%.2f", terrainParams.caveThreshold);
        return buffer;
    case 31:
        return std::to_string(terrainParams.tunnelCount);
    case 32:
        return std::to_string(static_cast<int>(std::round(terrainParams.tunnelRadiusMin))) + " u";
    case 33:
        return std::to_string(static_cast<int>(std::round(terrainParams.tunnelRadiusMax))) + " u";
    case 34:
        return terrainParams.surfaceOnlyMeshing ? "On" : "Off";
    case 35:
        return terrainParams.enableSkirts ? "On" : "Off";
    case 36:
        return std::to_string(static_cast<int>(std::round(terrainParams.skirtDepth))) + " u";
    case 37:
        return std::to_string(terrainParams.maxChunkCellsPerAxis);
    default:
        return {};
    }
}

const char* terrainHelpText(int terrainIndex)
{
    switch (terrainIndex) {
    case 0:
        return "Deterministic terrain seed used by the native SDF field and tunnel generator.";
    case 1:
        return "Base chunk world size used to derive terrain coverage and LOD ring spacing.";
    case 2:
        return "Overall terrain coordinate range used for deterministic tunnel spawn placement.";
    case 3:
        return "Near-field terrain radius rendered at the highest native terrain detail.";
    case 4:
        return "Mid-field terrain radius used for the secondary terrain ring.";
    case 5:
        return "Horizon terrain radius used for the far native terrain shell.";
    case 6:
        return "Scales derived terrain cell size. Higher values increase mesh density and cost.";
    case 7:
        return "Native terrain mesh finalization budget exposed for runtime tuning parity.";
    case 8:
        return "Near-field LOD radius in chunk units.";
    case 9:
        return "Mid-field LOD radius in chunk units.";
    case 10:
        return "Far-field LOD radius in chunk units.";
    case 11:
        return "Macro height amplitude for the terrain field.";
    case 12:
        return "Macro heightfield frequency. Higher values create tighter terrain features.";
    case 13:
        return "High-frequency surface variation amplitude layered onto the base terrain.";
    case 14:
        return "Ridge feature amplitude used to sharpen mountain silhouettes.";
    case 15:
        return "Blend amount between smooth terrain and terraced stepping.";
    case 16:
        return "Water plane base elevation before wave sampling.";
    case 17:
        return "Elevation where snow tint begins to dominate terrain shading.";
    case 18:
        return "Enable procedural terrain decoration so forests, shoreline brush, and alpine rocks are generated.";
    case 19:
        return "Overall biome-prop density across the active world.";
    case 20:
        return "Extra density multiplier for near-field terrain tiles.";
    case 21:
        return "Density multiplier for mid-field terrain tiles.";
    case 22:
        return "Far-field silhouette density multiplier for horizon decoration.";
    case 23:
        return "Boost or reduce shoreline brush and shrub placement.";
    case 24:
        return "Boost or reduce alpine and rocky boulder placement.";
    case 25:
        return "Bias the tree line above or below the snow line.";
    case 26:
        return "Enable blocker collisions against trunks and large rocks.";
    case 27:
        return "Deterministic offset used to reshuffle prop placement without changing terrain.";
    case 28:
        return "Enable or disable volumetric cave carving noise.";
    case 29:
        return "Cave subtraction strength in the volumetric SDF path.";
    case 30:
        return "Noise threshold for cave carving. Lower values open more cave volume.";
    case 31:
        return "Number of deterministic tunnel worms carved through the terrain.";
    case 32:
        return "Minimum generated tunnel radius.";
    case 33:
        return "Maximum generated tunnel radius.";
    case 34:
        return "Force the native terrain renderer onto the surface-mesh path only.";
    case 35:
        return "Enable or disable terrain skirts to seal outer terrain patch boundaries.";
    case 36:
        return "Depth of the boundary skirts added around terrain patches.";
    case 37:
        return "Hard cap on terrain grid resolution per axis for a generated terrain patch.";
    default:
        return "";
    }
}

void adjustTerrainValue(TerrainParams& terrainParams, int terrainIndex, int direction)
{
    if (direction == 0) {
        return;
    }

    switch (terrainIndex) {
    case 0:
        terrainParams.seed = std::clamp(terrainParams.seed + direction, 1, 999999);
        break;
    case 1:
        terrainParams.chunkSize = clamp(terrainParams.chunkSize + (8.0f * static_cast<float>(direction)), 32.0f, 256.0f);
        break;
    case 2:
        terrainParams.worldRadius = clamp(terrainParams.worldRadius + (256.0f * static_cast<float>(direction)), 512.0f, 16384.0f);
        break;
    case 3:
        terrainParams.gameplayRadiusMeters = clamp(terrainParams.gameplayRadiusMeters + (128.0f * static_cast<float>(direction)), 128.0f, 4096.0f);
        break;
    case 4:
        terrainParams.midFieldRadiusMeters = clamp(terrainParams.midFieldRadiusMeters + (256.0f * static_cast<float>(direction)), 512.0f, 8192.0f);
        break;
    case 5:
        terrainParams.horizonRadiusMeters = clamp(terrainParams.horizonRadiusMeters + (512.0f * static_cast<float>(direction)), 1024.0f, 20000.0f);
        break;
    case 6:
        terrainParams.terrainQuality = clamp(terrainParams.terrainQuality + (0.2f * static_cast<float>(direction)), 0.8f, 6.0f);
        break;
    case 7:
        terrainParams.meshBuildBudget = std::clamp(terrainParams.meshBuildBudget + direction, 1, 8);
        break;
    case 8:
        terrainParams.lod0Radius = std::clamp(terrainParams.lod0Radius + direction, 1, 12);
        break;
    case 9:
        terrainParams.lod1Radius = std::clamp(terrainParams.lod1Radius + direction, 2, 32);
        break;
    case 10:
        terrainParams.lod2Radius = std::clamp(terrainParams.lod2Radius + direction, 4, 96);
        break;
    case 11:
        terrainParams.heightAmplitude = clamp(terrainParams.heightAmplitude + (5.0f * static_cast<float>(direction)), 0.0f, 600.0f);
        break;
    case 12:
        terrainParams.heightFrequency = clamp(terrainParams.heightFrequency + (0.0001f * static_cast<float>(direction)), 0.0002f, 0.01f);
        break;
    case 13:
        terrainParams.surfaceDetailAmplitude = clamp(terrainParams.surfaceDetailAmplitude + static_cast<float>(direction), 0.0f, 80.0f);
        break;
    case 14:
        terrainParams.ridgeAmplitude = clamp(terrainParams.ridgeAmplitude + (2.0f * static_cast<float>(direction)), 0.0f, 160.0f);
        break;
    case 15:
        terrainParams.terraceStrength = clamp(terrainParams.terraceStrength + (0.05f * static_cast<float>(direction)), 0.0f, 1.0f);
        break;
    case 16:
        terrainParams.waterLevel = clamp(terrainParams.waterLevel + (2.0f * static_cast<float>(direction)), -180.0f, 240.0f);
        break;
    case 17:
        terrainParams.snowLine = clamp(terrainParams.snowLine + (10.0f * static_cast<float>(direction)), -64.0f, 800.0f);
        break;
    case 18:
        terrainParams.decoration.enabled = !terrainParams.decoration.enabled;
        break;
    case 19:
        terrainParams.decoration.density = clamp(terrainParams.decoration.density + (0.1f * static_cast<float>(direction)), 0.0f, 3.0f);
        break;
    case 20:
        terrainParams.decoration.nearDensityScale = clamp(terrainParams.decoration.nearDensityScale + (0.1f * static_cast<float>(direction)), 0.0f, 3.0f);
        break;
    case 21:
        terrainParams.decoration.midDensityScale = clamp(terrainParams.decoration.midDensityScale + (0.1f * static_cast<float>(direction)), 0.0f, 2.5f);
        break;
    case 22:
        terrainParams.decoration.farDensityScale = clamp(terrainParams.decoration.farDensityScale + (0.05f * static_cast<float>(direction)), 0.0f, 1.5f);
        break;
    case 23:
        terrainParams.decoration.shoreBrushDensity = clamp(terrainParams.decoration.shoreBrushDensity + (0.1f * static_cast<float>(direction)), 0.0f, 3.0f);
        break;
    case 24:
        terrainParams.decoration.rockDensity = clamp(terrainParams.decoration.rockDensity + (0.1f * static_cast<float>(direction)), 0.0f, 3.0f);
        break;
    case 25:
        terrainParams.decoration.treeLineOffset = clamp(terrainParams.decoration.treeLineOffset + (10.0f * static_cast<float>(direction)), -180.0f, 260.0f);
        break;
    case 26:
        terrainParams.decoration.collisionEnabled = !terrainParams.decoration.collisionEnabled;
        break;
    case 27:
        terrainParams.decoration.seedOffset = std::clamp(terrainParams.decoration.seedOffset + direction, -999999, 999999);
        break;
    case 28:
        terrainParams.caveEnabled = !terrainParams.caveEnabled;
        break;
    case 29:
        terrainParams.caveStrength = clamp(terrainParams.caveStrength + (2.0f * static_cast<float>(direction)), 1.0f, 128.0f);
        break;
    case 30:
        terrainParams.caveThreshold = clamp(terrainParams.caveThreshold + (0.02f * static_cast<float>(direction)), 0.05f, 0.95f);
        break;
    case 31:
        terrainParams.tunnelCount = std::clamp(terrainParams.tunnelCount + direction, 0, 64);
        break;
    case 32:
        terrainParams.tunnelRadiusMin = clamp(terrainParams.tunnelRadiusMin + static_cast<float>(direction), 2.0f, 40.0f);
        break;
    case 33:
        terrainParams.tunnelRadiusMax = clamp(terrainParams.tunnelRadiusMax + static_cast<float>(direction), 2.0f, 60.0f);
        break;
    case 34:
        terrainParams.surfaceOnlyMeshing = !terrainParams.surfaceOnlyMeshing;
        break;
    case 35:
        terrainParams.enableSkirts = !terrainParams.enableSkirts;
        break;
    case 36:
        terrainParams.skirtDepth = clamp(terrainParams.skirtDepth + (2.0f * static_cast<float>(direction)), 2.0f, 120.0f);
        break;
    case 37:
        terrainParams.maxChunkCellsPerAxis = std::clamp(terrainParams.maxChunkCellsPerAxis + (4 * direction), 24, 128);
        break;
    default:
        break;
    }
    terrainParams = normalizeTerrainParams(terrainParams);
}

void resetTerrainValue(TerrainParams& terrainParams, const TerrainParams& defaults, int terrainIndex)
{
    switch (terrainIndex) {
    case 0:
        terrainParams.seed = defaults.seed;
        break;
    case 1:
        terrainParams.chunkSize = defaults.chunkSize;
        break;
    case 2:
        terrainParams.worldRadius = defaults.worldRadius;
        break;
    case 3:
        terrainParams.gameplayRadiusMeters = defaults.gameplayRadiusMeters;
        break;
    case 4:
        terrainParams.midFieldRadiusMeters = defaults.midFieldRadiusMeters;
        break;
    case 5:
        terrainParams.horizonRadiusMeters = defaults.horizonRadiusMeters;
        break;
    case 6:
        terrainParams.terrainQuality = defaults.terrainQuality;
        break;
    case 7:
        terrainParams.meshBuildBudget = defaults.meshBuildBudget;
        break;
    case 8:
        terrainParams.lod0Radius = defaults.lod0Radius;
        break;
    case 9:
        terrainParams.lod1Radius = defaults.lod1Radius;
        break;
    case 10:
        terrainParams.lod2Radius = defaults.lod2Radius;
        break;
    case 11:
        terrainParams.heightAmplitude = defaults.heightAmplitude;
        break;
    case 12:
        terrainParams.heightFrequency = defaults.heightFrequency;
        break;
    case 13:
        terrainParams.surfaceDetailAmplitude = defaults.surfaceDetailAmplitude;
        break;
    case 14:
        terrainParams.ridgeAmplitude = defaults.ridgeAmplitude;
        break;
    case 15:
        terrainParams.terraceStrength = defaults.terraceStrength;
        break;
    case 16:
        terrainParams.waterLevel = defaults.waterLevel;
        break;
    case 17:
        terrainParams.snowLine = defaults.snowLine;
        break;
    case 18:
        terrainParams.decoration.enabled = defaults.decoration.enabled;
        break;
    case 19:
        terrainParams.decoration.density = defaults.decoration.density;
        break;
    case 20:
        terrainParams.decoration.nearDensityScale = defaults.decoration.nearDensityScale;
        break;
    case 21:
        terrainParams.decoration.midDensityScale = defaults.decoration.midDensityScale;
        break;
    case 22:
        terrainParams.decoration.farDensityScale = defaults.decoration.farDensityScale;
        break;
    case 23:
        terrainParams.decoration.shoreBrushDensity = defaults.decoration.shoreBrushDensity;
        break;
    case 24:
        terrainParams.decoration.rockDensity = defaults.decoration.rockDensity;
        break;
    case 25:
        terrainParams.decoration.treeLineOffset = defaults.decoration.treeLineOffset;
        break;
    case 26:
        terrainParams.decoration.collisionEnabled = defaults.decoration.collisionEnabled;
        break;
    case 27:
        terrainParams.decoration.seedOffset = defaults.decoration.seedOffset;
        break;
    case 28:
        terrainParams.caveEnabled = defaults.caveEnabled;
        break;
    case 29:
        terrainParams.caveStrength = defaults.caveStrength;
        break;
    case 30:
        terrainParams.caveThreshold = defaults.caveThreshold;
        break;
    case 31:
        terrainParams.tunnelCount = defaults.tunnelCount;
        break;
    case 32:
        terrainParams.tunnelRadiusMin = defaults.tunnelRadiusMin;
        break;
    case 33:
        terrainParams.tunnelRadiusMax = defaults.tunnelRadiusMax;
        break;
    case 34:
        terrainParams.surfaceOnlyMeshing = defaults.surfaceOnlyMeshing;
        break;
    case 35:
        terrainParams.enableSkirts = defaults.enableSkirts;
        break;
    case 36:
        terrainParams.skirtDepth = defaults.skirtDepth;
        break;
    case 37:
        terrainParams.maxChunkCellsPerAxis = defaults.maxChunkCellsPerAxis;
        break;
    default:
        break;
    }
    terrainParams = normalizeTerrainParams(terrainParams);
}

std::string formatSettingsValue(int settingIndex, const UiState& uiState)
{
    switch (settingIndex) {
    case 0:
        return std::to_string(static_cast<int>(std::round(uiState.cameraFovDegrees))) + " deg";
    case 1: {
        char buffer[32] {};
        std::snprintf(buffer, sizeof(buffer), "%.2f", uiState.mouseSensitivity);
        return buffer;
    }
    case 2:
        return uiState.invertLookY ? "On" : "Off";
    case 3:
        return uiState.showCrosshair ? "On" : "Off";
    case 4:
        return uiState.mapNorthUp ? "North-Up" : "Heading-Up";
    case 5:
        return uiState.audioEnabled ? "On" : "Off";
    case 6: {
        char buffer[32] {};
        std::snprintf(buffer, sizeof(buffer), "%.0f%%", uiState.masterVolume * 100.0f);
        return buffer;
    }
    case 7: {
        char buffer[32] {};
        std::snprintf(buffer, sizeof(buffer), "%.0f%%", uiState.engineVolume * 100.0f);
        return buffer;
    }
    case 8: {
        char buffer[32] {};
        std::snprintf(buffer, sizeof(buffer), "%.0f%%", uiState.ambienceVolume * 100.0f);
        return buffer;
    }
    default:
        return {};
    }
}

const char* settingsHelpText(int settingIndex)
{
    switch (settingIndex) {
    case 0:
        return "Adjust the native camera field of view in degrees.";
    case 1:
        return "Scales flight mouse/yoke sensitivity without changing keyboard input.";
    case 2:
        return "Invert the vertical mouse-look direction for flight control.";
    case 3:
        return "Show or hide the cockpit crosshair in non-chase view.";
    case 4:
        return "Switch the minimap between heading-up and north-up orientation.";
    case 5:
        return "Enable or mute the native procedural audio mix.";
    case 6:
        return "Overall volume for the native audio output.";
    case 7:
        return "Volume for the propulsion layer synthesized from thrust and airflow.";
    case 8:
        return "Volume for the wind and water ambience layer.";
    default:
        return "";
    }
}

void adjustSettingsValue(UiState& uiState, int settingIndex, int direction)
{
    switch (settingIndex) {
    case 0:
        uiState.cameraFovDegrees = clamp(uiState.cameraFovDegrees + (5.0f * static_cast<float>(direction)), 60.0f, 120.0f);
        break;
    case 1:
        uiState.mouseSensitivity = clamp(uiState.mouseSensitivity + (0.1f * static_cast<float>(direction)), 0.3f, 2.5f);
        break;
    case 2:
        if (direction != 0) {
            uiState.invertLookY = !uiState.invertLookY;
        }
        break;
    case 3:
        if (direction != 0) {
            uiState.showCrosshair = !uiState.showCrosshair;
        }
        break;
    case 4:
        if (direction != 0) {
            uiState.mapNorthUp = !uiState.mapNorthUp;
        }
        break;
    case 5:
        if (direction != 0) {
            uiState.audioEnabled = !uiState.audioEnabled;
        }
        break;
    case 6:
        uiState.masterVolume = clamp(uiState.masterVolume + (0.05f * static_cast<float>(direction)), 0.0f, 1.5f);
        break;
    case 7:
        uiState.engineVolume = clamp(uiState.engineVolume + (0.05f * static_cast<float>(direction)), 0.0f, 1.5f);
        break;
    case 8:
        uiState.ambienceVolume = clamp(uiState.ambienceVolume + (0.05f * static_cast<float>(direction)), 0.0f, 1.5f);
        break;
    default:
        break;
    }
}

void resetSettingsValue(UiState& uiState, const UiState& defaults, int settingIndex)
{
    switch (settingIndex) {
    case 0:
        uiState.cameraFovDegrees = defaults.cameraFovDegrees;
        break;
    case 1:
        uiState.mouseSensitivity = defaults.mouseSensitivity;
        break;
    case 2:
        uiState.invertLookY = defaults.invertLookY;
        break;
    case 3:
        uiState.showCrosshair = defaults.showCrosshair;
        break;
    case 4:
        uiState.mapNorthUp = defaults.mapNorthUp;
        break;
    case 5:
        uiState.audioEnabled = defaults.audioEnabled;
        break;
    case 6:
        uiState.masterVolume = defaults.masterVolume;
        break;
    case 7:
        uiState.engineVolume = defaults.engineVolume;
        break;
    case 8:
        uiState.ambienceVolume = defaults.ambienceVolume;
        break;
    default:
        break;
    }
}

std::string formatHudValue(int hudIndex, const UiState& uiState)
{
    switch (hudIndex) {
    case 0:
        return uiState.showDebug ? "On" : "Off";
    case 1:
        return uiState.showThrottleHud ? "On" : "Off";
    case 2:
        return uiState.showControlIndicator ? "On" : "Off";
    case 3:
        return uiState.showMap ? "On" : "Off";
    case 4:
        return uiState.showGeoInfo ? "On" : "Off";
    default:
        return {};
    }
}

const char* hudHelpText(int hudIndex)
{
    switch (hudIndex) {
    case 0:
        return "Toggle the native debug text block at the bottom of the screen.";
    case 1:
        return "Show or hide the throttle progress bar in the flight HUD.";
    case 2:
        return "Show or hide the yoke/rudder indicator panel.";
    case 3:
        return "Show or hide the top-right minimap module.";
    case 4:
        return "Show or hide the latitude/longitude lines in the flight HUD.";
    default:
        return "";
    }
}

void adjustHudValue(UiState& uiState, int hudIndex, int direction)
{
    if (direction == 0) {
        return;
    }

    switch (hudIndex) {
    case 0:
        uiState.showDebug = !uiState.showDebug;
        break;
    case 1:
        uiState.showThrottleHud = !uiState.showThrottleHud;
        break;
    case 2:
        uiState.showControlIndicator = !uiState.showControlIndicator;
        break;
    case 3:
        uiState.showMap = !uiState.showMap;
        break;
    case 4:
        uiState.showGeoInfo = !uiState.showGeoInfo;
        break;
    default:
        break;
    }
}

void resetHudValue(UiState& uiState, const UiState& defaults, int hudIndex)
{
    switch (hudIndex) {
    case 0:
        uiState.showDebug = defaults.showDebug;
        break;
    case 1:
        uiState.showThrottleHud = defaults.showThrottleHud;
        break;
    case 2:
        uiState.showControlIndicator = defaults.showControlIndicator;
        break;
    case 3:
        uiState.showMap = defaults.showMap;
        break;
    case 4:
        uiState.showGeoInfo = defaults.showGeoInfo;
        break;
    default:
        break;
    }
}

int settingsLocalToLegacyIndex(SettingsSubTab subTab, int localIndex)
{
    switch (subTab) {
    case SettingsSubTab::Graphics:
        return localIndex == 0 ? 3 : (localIndex == 1 ? 4 : -1);
    case SettingsSubTab::Camera:
        return localIndex >= 0 && localIndex <= 2 ? localIndex : -1;
    case SettingsSubTab::Sound:
        return localIndex >= 0 && localIndex <= 3 ? (5 + localIndex) : -1;
    default:
        return -1;
    }
}

std::string formatPropAudioRowValue(int localIndex, const PropAudioConfig& propAudioConfig)
{
    char buffer[32] {};
    switch (localIndex) {
    case 4:
        std::snprintf(buffer, sizeof(buffer), "%.0f", propAudioConfig.baseRpm);
        return buffer;
    case 5:
        std::snprintf(buffer, sizeof(buffer), "%.0f", propAudioConfig.loadRpmContribution);
        return buffer;
    case 6:
        std::snprintf(buffer, sizeof(buffer), "%.2f", propAudioConfig.engineFrequencyScale);
        return buffer;
    case 7:
        std::snprintf(buffer, sizeof(buffer), "%.2f", propAudioConfig.engineTonalMix);
        return buffer;
    case 8:
        std::snprintf(buffer, sizeof(buffer), "%.2f", propAudioConfig.propHarmonicMix);
        return buffer;
    case 9:
        std::snprintf(buffer, sizeof(buffer), "%.2f", propAudioConfig.engineNoiseAmount);
        return buffer;
    case 10:
        std::snprintf(buffer, sizeof(buffer), "%.2f", propAudioConfig.ambienceFrequencyScale);
        return buffer;
    case 11:
        std::snprintf(buffer, sizeof(buffer), "%.2f", propAudioConfig.waterAmbienceGain);
        return buffer;
    case 12:
        std::snprintf(buffer, sizeof(buffer), "%.2f", propAudioConfig.groundAmbienceGain);
        return buffer;
    default:
        return {};
    }
}

const char* settingsRowLabel(SettingsSubTab subTab, int localIndex)
{
    switch (subTab) {
    case SettingsSubTab::Graphics:
        switch (localIndex) {
        case 0:
            return "Crosshair";
        case 1:
            return "Map Orientation";
        default:
            return "";
        }
    case SettingsSubTab::Camera:
        switch (localIndex) {
        case 0:
            return "Field of View";
        case 1:
            return "Mouse Sensitivity";
        case 2:
            return "Invert Look Y";
        case 3:
            return "Camera Mode";
        default:
            return "";
        }
    case SettingsSubTab::Sound:
        switch (localIndex) {
        case 0:
            return "Audio";
        case 1:
            return "Master Volume";
        case 2:
            return "Engine Volume";
        case 3:
            return "Ambience Volume";
        case 4:
            return "Base RPM";
        case 5:
            return "Load RPM Range";
        case 6:
            return "Engine Freq";
        case 7:
            return "Engine Tone";
        case 8:
            return "Prop Harmonics";
        case 9:
            return "Engine Noise";
        case 10:
            return "Ambient Freq";
        case 11:
            return "Water Ambience";
        case 12:
            return "Ground Ambience";
        default:
            return "";
        }
    default:
        return "";
    }
}

std::string formatSettingsRowValue(SettingsSubTab subTab, int localIndex, const UiState& uiState, const PropAudioConfig& propAudioConfig)
{
    if (subTab == SettingsSubTab::Camera && localIndex == 3) {
        return uiState.chaseCamera ? "Chase" : "Cockpit";
    }
    if (subTab == SettingsSubTab::Sound && localIndex >= 4) {
        return formatPropAudioRowValue(localIndex, propAudioConfig);
    }

    const int legacyIndex = settingsLocalToLegacyIndex(subTab, localIndex);
    return legacyIndex >= 0 ? formatSettingsValue(legacyIndex, uiState) : std::string {};
}

const char* settingsRowHelpText(SettingsSubTab subTab, int localIndex)
{
    if (subTab == SettingsSubTab::Camera && localIndex == 3) {
        return "Switch the active native camera between chase and cockpit views.";
    }
    if (subTab == SettingsSubTab::Sound) {
        switch (localIndex) {
        case 4:
            return "Aircraft-local idle prop/engine RPM basis for the procedural starter audio model.";
        case 5:
            return "Aircraft-local loaded RPM range added as thrust and airflow rise.";
        case 6:
            return "Aircraft-local engine tone frequency scale for matching different prop-plane engines.";
        case 7:
            return "Aircraft-local tonal emphasis for the engine core harmonics.";
        case 8:
            return "Aircraft-local emphasis for prop-blade harmonic content.";
        case 9:
            return "Aircraft-local broadband engine/turbulence noise amount.";
        case 10:
            return "Aircraft-local ambient and wind frequency scale.";
        case 11:
            return "Aircraft-local water ambience gain near shorelines and sea-skimming flight.";
        case 12:
            return "Aircraft-local ground rumble ambience gain during low passes and taxi.";
        default:
            break;
        }
    }

    const int legacyIndex = settingsLocalToLegacyIndex(subTab, localIndex);
    return legacyIndex >= 0 ? settingsHelpText(legacyIndex) : "";
}

void adjustSettingsRowValue(UiState& uiState, PropAudioConfig& propAudioConfig, SettingsSubTab subTab, int localIndex, int direction)
{
    if (subTab == SettingsSubTab::Camera && localIndex == 3) {
        if (direction != 0) {
            uiState.chaseCamera = !uiState.chaseCamera;
        }
        return;
    }
    if (subTab == SettingsSubTab::Sound && localIndex >= 4) {
        switch (localIndex) {
        case 4:
            propAudioConfig.baseRpm += 2.0f * static_cast<float>(direction);
            break;
        case 5:
            propAudioConfig.loadRpmContribution += 10.0f * static_cast<float>(direction);
            break;
        case 6:
            propAudioConfig.engineFrequencyScale += 0.05f * static_cast<float>(direction);
            break;
        case 7:
            propAudioConfig.engineTonalMix += 0.05f * static_cast<float>(direction);
            break;
        case 8:
            propAudioConfig.propHarmonicMix += 0.05f * static_cast<float>(direction);
            break;
        case 9:
            propAudioConfig.engineNoiseAmount += 0.05f * static_cast<float>(direction);
            break;
        case 10:
            propAudioConfig.ambienceFrequencyScale += 0.05f * static_cast<float>(direction);
            break;
        case 11:
            propAudioConfig.waterAmbienceGain += 0.05f * static_cast<float>(direction);
            break;
        case 12:
            propAudioConfig.groundAmbienceGain += 0.05f * static_cast<float>(direction);
            break;
        default:
            break;
        }
        clampPropAudioConfigValues(propAudioConfig);
        return;
    }

    const int legacyIndex = settingsLocalToLegacyIndex(subTab, localIndex);
    if (legacyIndex >= 0) {
        adjustSettingsValue(uiState, legacyIndex, direction);
    }
}

void resetSettingsRowValue(UiState& uiState, const UiState& defaults, PropAudioConfig& propAudioConfig, const PropAudioConfig& defaultPropAudioConfigValues, SettingsSubTab subTab, int localIndex)
{
    if (subTab == SettingsSubTab::Camera && localIndex == 3) {
        uiState.chaseCamera = defaults.chaseCamera;
        return;
    }
    if (subTab == SettingsSubTab::Sound && localIndex >= 4) {
        switch (localIndex) {
        case 4:
            propAudioConfig.baseRpm = defaultPropAudioConfigValues.baseRpm;
            break;
        case 5:
            propAudioConfig.loadRpmContribution = defaultPropAudioConfigValues.loadRpmContribution;
            break;
        case 6:
            propAudioConfig.engineFrequencyScale = defaultPropAudioConfigValues.engineFrequencyScale;
            break;
        case 7:
            propAudioConfig.engineTonalMix = defaultPropAudioConfigValues.engineTonalMix;
            break;
        case 8:
            propAudioConfig.propHarmonicMix = defaultPropAudioConfigValues.propHarmonicMix;
            break;
        case 9:
            propAudioConfig.engineNoiseAmount = defaultPropAudioConfigValues.engineNoiseAmount;
            break;
        case 10:
            propAudioConfig.ambienceFrequencyScale = defaultPropAudioConfigValues.ambienceFrequencyScale;
            break;
        case 11:
            propAudioConfig.waterAmbienceGain = defaultPropAudioConfigValues.waterAmbienceGain;
            break;
        case 12:
            propAudioConfig.groundAmbienceGain = defaultPropAudioConfigValues.groundAmbienceGain;
            break;
        default:
            break;
        }
        return;
    }

    const int legacyIndex = settingsLocalToLegacyIndex(subTab, localIndex);
    if (legacyIndex >= 0) {
        resetSettingsValue(uiState, defaults, legacyIndex);
    }
}

const char* characterRowLabel(int rowIndex)
{
    switch (rowIndex) {
    case 0:
        return "Current Model";
    case 1:
        return "Auto Spin";
    case 2:
        return "Preview Zoom";
    case 3:
        return "Scale";
    case 4:
        return "Forward Axis";
    case 5:
        return "Yaw";
    case 6:
        return "Pitch";
    case 7:
        return "Roll";
    case 8:
        return "Offset X";
    case 9:
        return "Offset Y";
    case 10:
        return "Offset Z";
    case 11:
        return "Load From Path";
    case 12:
        return "Builtin Cube";
    default:
        return "Load Asset";
    }
}

std::string formatCharacterRowValue(int rowIndex, const PlaneVisualState& visual)
{
    char buffer[64] {};
    switch (rowIndex) {
    case 0:
        return visual.label;
    case 1:
        return visual.previewAutoSpin ? "On" : "Off";
    case 2:
        std::snprintf(buffer, sizeof(buffer), "%.0f%%", visual.previewZoom * 100.0f);
        return buffer;
    case 3:
        std::snprintf(buffer, sizeof(buffer), "%.2f", visual.scale);
        return buffer;
    case 4:
        std::snprintf(buffer, sizeof(buffer), "%.0f deg", visual.forwardAxisYawDegrees);
        return buffer;
    case 5:
        std::snprintf(buffer, sizeof(buffer), "%.0f deg", visual.yawDegrees);
        return buffer;
    case 6:
        std::snprintf(buffer, sizeof(buffer), "%.0f deg", visual.pitchDegrees);
        return buffer;
    case 7:
        std::snprintf(buffer, sizeof(buffer), "%.0f deg", visual.rollDegrees);
        return buffer;
    case 8:
        std::snprintf(buffer, sizeof(buffer), "%.2f", visual.modelOffset.x);
        return buffer;
    case 9:
        std::snprintf(buffer, sizeof(buffer), "%.2f", visual.modelOffset.y);
        return buffer;
    case 10:
        std::snprintf(buffer, sizeof(buffer), "%.2f", visual.modelOffset.z);
        return buffer;
    case 11:
        return visual.sourcePath.empty() ? "Type Path" : "Edit Path";
    case 12:
        return "Load";
    default:
        return "Load";
    }
}

const char* characterRowHelpText(int rowIndex)
{
    switch (rowIndex) {
    case 0:
        return "Active model for the selected role. Drag and drop STL/GLB/GLTF files to replace it.";
    case 1:
        return "Spin the paused preview automatically to inspect silhouette and material alignment.";
    case 2:
        return "Scale only the paused preview turntable without changing the in-flight transform.";
    case 3:
        return "World render scale for this character role.";
    case 4:
        return "Explicit per-model forward-axis calibration replacing the old hardcoded yaw compensation.";
    case 5:
        return "Additional yaw correction layered onto the calibrated imported model.";
    case 6:
        return "Additional pitch correction layered onto the calibrated imported model.";
    case 7:
        return "Additional roll correction layered onto the calibrated imported model.";
    case 8:
    case 9:
    case 10:
        return "Local model-space offset applied after the role transform.";
    case 11:
        return "Open a text prompt and load STL, GLB, or GLTF directly from disk for the selected role.";
    case 12:
        return "Reset the selected role back to the built-in cube.";
    default:
        return "Load a model from portSource/Assets/Models into the selected role.";
    }
}

void adjustCharacterRowValue(PlaneVisualState& visual, int rowIndex, int direction)
{
    if (direction == 0) {
        return;
    }

    switch (rowIndex) {
    case 1:
        visual.previewAutoSpin = !visual.previewAutoSpin;
        break;
    case 2:
        visual.previewZoom = clamp(visual.previewZoom + (0.1f * static_cast<float>(direction)), 0.35f, 3.0f);
        break;
    case 3:
        visual.scale = clamp(visual.scale + (0.1f * static_cast<float>(direction)), 0.15f, 24.0f);
        break;
    case 4:
        visual.forwardAxisYawDegrees = clamp(visual.forwardAxisYawDegrees + (5.0f * static_cast<float>(direction)), -180.0f, 180.0f);
        break;
    case 5:
        visual.yawDegrees = clamp(visual.yawDegrees + (5.0f * static_cast<float>(direction)), -180.0f, 180.0f);
        break;
    case 6:
        visual.pitchDegrees = clamp(visual.pitchDegrees + (5.0f * static_cast<float>(direction)), -180.0f, 180.0f);
        break;
    case 7:
        visual.rollDegrees = clamp(visual.rollDegrees + (5.0f * static_cast<float>(direction)), -180.0f, 180.0f);
        break;
    case 8:
        visual.modelOffset.x = clamp(visual.modelOffset.x + (0.1f * static_cast<float>(direction)), -24.0f, 24.0f);
        break;
    case 9:
        visual.modelOffset.y = clamp(visual.modelOffset.y + (0.1f * static_cast<float>(direction)), -24.0f, 24.0f);
        break;
    case 10:
        visual.modelOffset.z = clamp(visual.modelOffset.z + (0.1f * static_cast<float>(direction)), -24.0f, 24.0f);
        break;
    default:
        break;
    }
}

const char* paintRowLabel(int rowIndex)
{
    switch (rowIndex) {
    case 0:
        return "Mode";
    case 1:
        return "Color";
    case 2:
        return "Brush Size";
    case 3:
        return "Opacity";
    case 4:
        return "Hardness";
    case 5:
        return "Undo";
    case 6:
        return "Redo";
    case 7:
        return "Fill";
    case 8:
        return "Clear";
    case 9:
        return "Commit";
    case 10:
        return "Revert Saved";
    default:
        return "";
    }
}

std::string formatPaintRowValue(int rowIndex, const PaintUiState& paintUi, const PlaneVisualState& visual)
{
    static const std::array<const char*, 8> kColorNames { "Ivory", "Jet", "Red", "Amber", "Lime", "Blue", "Violet", "Rose" };
    char buffer[64] {};

    switch (rowIndex) {
    case 0:
        return paintUi.mode == PaintMode::Brush ? "Brush" : "Erase";
    case 1:
        return kColorNames[static_cast<std::size_t>(std::clamp(paintUi.colorIndex, 0, 7))];
    case 2:
        return std::to_string(paintUi.brushSize) + " px";
    case 3:
        std::snprintf(buffer, sizeof(buffer), "%.0f%%", paintUi.brushOpacity * 100.0f);
        return buffer;
    case 4:
        std::snprintf(buffer, sizeof(buffer), "%.0f%%", paintUi.brushHardness * 100.0f);
        return buffer;
    case 5:
        return std::to_string(visual.paintUndoStack.size()) + " steps";
    case 6:
        return std::to_string(visual.paintRedoStack.size()) + " steps";
    case 7:
        return "Flood";
    case 8:
        return "Empty";
    case 9:
        return visual.paintHash.empty() ? "Write PNG" : ("Write " + visual.paintHash.substr(0, std::min<std::size_t>(8, visual.paintHash.size())));
    case 10:
        return visual.paintHash.empty() ? "No saved overlay" : "Reload PNG";
    default:
        return {};
    }
}

const char* paintRowHelpText(int rowIndex, const PlaneVisualState& visual)
{
    if (!visual.paintSupported) {
        return "Paint editing requires a textured GLB/GLTF with UVs. STL and UV-less assets remain read-only.";
    }

    switch (rowIndex) {
    case 0:
        return "Brush adds paint onto the overlay. Erase removes overlay alpha.";
    case 1:
        return "Active preset color used by brush and fill.";
    case 2:
        return "Brush diameter in overlay pixels.";
    case 3:
        return "Alpha contribution of each stroke.";
    case 4:
        return "Controls how soft the brush edge falls off.";
    case 5:
        return "Restore the previous paint snapshot.";
    case 6:
        return "Reapply the next undone paint snapshot.";
    case 7:
        return "Fill the whole overlay with the active color.";
    case 8:
        return "Clear the full overlay back to transparent.";
    case 9:
        return "Persist the current overlay as a content-addressed PNG under the SDL pref path.";
    case 10:
        return "Reload the last committed PNG for this role and model.";
    default:
        return "";
    }
}

void adjustPaintRowValue(PaintUiState& paintUi, int rowIndex, int direction)
{
    if (direction == 0) {
        return;
    }

    switch (rowIndex) {
    case 0:
        paintUi.mode = paintUi.mode == PaintMode::Brush ? PaintMode::Erase : PaintMode::Brush;
        break;
    case 1:
        paintUi.colorIndex = (paintUi.colorIndex + direction + 8) % 8;
        break;
    case 2:
        paintUi.brushSize = std::clamp(paintUi.brushSize + (direction * 4), 4, 192);
        break;
    case 3:
        paintUi.brushOpacity = clamp(paintUi.brushOpacity + (0.05f * static_cast<float>(direction)), 0.05f, 1.0f);
        break;
    case 4:
        paintUi.brushHardness = clamp(paintUi.brushHardness + (0.05f * static_cast<float>(direction)), 0.05f, 1.0f);
        break;
    default:
        break;
    }
}

void adjustThrottleFromWheel(FlightState& plane, int wheelDelta)
{
    if (wheelDelta == 0) {
        return;
    }

    plane.throttle = clamp(
        plane.throttle + (plane.wheelThrottleStep * static_cast<float>(wheelDelta)),
        0.0f,
        1.0f);
}

void adjustElevatorTrimFromWheel(const FlightConfig& config, FlightState& plane, int wheelDelta)
{
    if (wheelDelta == 0) {
        return;
    }

    plane.manualElevatorTrim = clamp(
        plane.manualElevatorTrim + (plane.wheelElevatorTrimStepRad * static_cast<float>(wheelDelta)),
        -std::max(0.0f, config.maxManualElevatorTrimRad),
        std::max(0.0f, config.maxManualElevatorTrimRad));
}

std::string formatManualTrimStatus(const FlightState& plane)
{
    const float trimDegrees = degrees(plane.manualElevatorTrim);
    if (std::fabs(trimDegrees) < 0.05f) {
        return "Pitch Trim: Neutral";
    }

    char buffer[64] {};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "Pitch Trim: %s %.2f deg",
        trimDegrees < 0.0f ? "Nose Up" : "Nose Down",
        std::fabs(trimDegrees));
    return buffer;
}

void applyFlightMouseInput(
    const UiState& uiState,
    const ControlProfile& controls,
    FlightState& plane,
    float mouseDx,
    float mouseDy,
    SDL_Keymod modifiers,
    float nowSeconds)
{
    const float rollLeft = controlMouseAxisValue(controls, InputActionId::FlightRollLeft, mouseDx, mouseDy, modifiers);
    const float rollRight = controlMouseAxisValue(controls, InputActionId::FlightRollRight, mouseDx, mouseDy, modifiers);
    const float pitchDown = controlMouseAxisValue(controls, InputActionId::FlightPitchDown, mouseDx, mouseDy, modifiers);
    const float pitchUp = controlMouseAxisValue(controls, InputActionId::FlightPitchUp, mouseDx, mouseDy, modifiers);

    float clampedDx = clamp(rollRight - rollLeft, -42.0f, 42.0f);
    float clampedDy = clamp(pitchUp - pitchDown, -42.0f, 42.0f);
    if (std::fabs(clampedDx) < 0.2f) {
        clampedDx = 0.0f;
    }
    if (std::fabs(clampedDy) < 0.2f) {
        clampedDy = 0.0f;
    }
    if (clampedDx == 0.0f && clampedDy == 0.0f) {
        return;
    }

    const float pitchAxis = uiState.invertLookY ? clampedDy : -clampedDy;
    const float rollAxis = -clampedDx;
    const float sensitivity = kFlightMouseSensitivity * uiState.mouseSensitivity;

    plane.yoke.pitch = clamp(
        plane.yoke.pitch + (pitchAxis * sensitivity * plane.yokeMousePitchGain),
        -1.0f,
        1.0f);
    plane.yoke.roll = clamp(
        plane.yoke.roll + (rollAxis * sensitivity * plane.yokeMouseRollGain),
        -1.0f,
        1.0f);
    plane.yokeLastMouseInputAt = nowSeconds;
}

std::array<std::string, 6> buildPauseMainItems(const UiState& uiState)
{
    return {
        "Resume",
        "Reset Flight",
        std::string("Camera: ") + (uiState.chaseCamera ? "Chase" : "Cockpit"),
        std::string("Map: ") + (uiState.showMap ? "On" : "Off"),
        std::string("Debug Overlay: ") + (uiState.showDebug ? "On" : "Off"),
        "Quit"
    };
}

void activatePauseSelection(
    PauseState& pauseState,
    UiState& uiState,
    FlightState& plane,
    FlightRuntimeState& runtime,
    const TerrainFieldContext& terrainContext,
    bool& running)
{
    switch (pauseState.selectedIndex) {
    case 0:
        setPauseActive(pauseState, uiState, false);
        break;
    case 1:
        resetFlight(plane, runtime, terrainContext, plane.pos.x, plane.pos.z);
        setPauseActive(pauseState, uiState, false);
        break;
    case 2:
        uiState.chaseCamera = !uiState.chaseCamera;
        break;
    case 3:
        uiState.showMap = !uiState.showMap;
        break;
    case 4:
        uiState.showDebug = !uiState.showDebug;
        break;
    case 5:
        running = false;
        break;
    default:
        break;
    }
}

Camera buildRenderCamera(
    const FlightState& plane,
    const TerrainFieldContext& terrainContext,
    const UiState& uiState,
    bool flightMode = true,
    float lookYawRadians = 0.0f,
    float lookPitchRadians = 0.0f)
{
    const Vec3 forward = forwardFromRotation(plane.rot);
    const Vec3 up = upFromRotation(plane.rot);

    Camera camera;
    camera.rot = applyCameraLookOffset(plane.rot, lookYawRadians, lookPitchRadians);
    camera.fovRadians = radians(uiState.cameraFovDegrees);
    if (flightMode && uiState.zoomHeld) {
        camera.fovRadians = std::max(kMinimumZoomFovRadians, camera.fovRadians * kFlightZoomFactor);
    }
    if (uiState.chaseCamera) {
        if (flightMode) {
            camera.pos = plane.pos - (forward * 18.0f) + (up * 6.0f);
        } else {
            camera.pos = plane.pos - (forward * 6.0f) + Vec3 { 0.0f, 1.4f, 0.0f };
        }
    } else {
        camera.pos = flightMode
            ? plane.pos + (up * 1.5f) + (forward * 2.8f)
            : plane.pos + Vec3 { 0.0f, 0.2f, 0.0f };
    }

    const float cameraGround = sampleGroundHeight(camera.pos.x, camera.pos.z, terrainContext) + (flightMode ? 2.0f : 0.4f);
    if (camera.pos.y < cameraGround) {
        camera.pos.y = cameraGround;
    }
    return camera;
}

void drawCrosshair(HudCanvas& canvas, int width, int height)
{
    const float centerX = static_cast<float>(width) * 0.5f;
    const float centerY = static_cast<float>(height) * 0.5f;
    const HudColor color = makeHudColor(255, 92, 54, 220);
    canvas.line(centerX - 9.0f, centerY, centerX - 2.0f, centerY, color);
    canvas.line(centerX + 2.0f, centerY, centerX + 9.0f, centerY, color);
    canvas.line(centerX, centerY - 9.0f, centerX, centerY - 2.0f, color);
    canvas.line(centerX, centerY + 2.0f, centerX, centerY + 9.0f, color);
    canvas.point(centerX, centerY, color);
}

void drawControlIndicator(HudCanvas& canvas, int width, int height, const FlightState& plane)
{
    const float panelW = 180.0f;
    const float panelH = 92.0f;
    const float panelX = (static_cast<float>(width) * 0.5f) - (panelW * 0.5f);
    const float panelY = static_cast<float>(height) - panelH - 18.0f;
    const float centerX = panelX + 52.0f;
    const float centerY = panelY + 42.0f;
    const float throwDistance = 24.0f;
    const float handleX = centerX + clamp(-plane.yoke.roll, -1.0f, 1.0f) * throwDistance;
    const float handleY = centerY + clamp(-plane.yoke.pitch, -1.0f, 1.0f) * throwDistance;

    canvas.fillRect(panelX, panelY, panelW, panelH, makeHudColor(7, 12, 18, 178));
    canvas.strokeRect(panelX, panelY, panelW, panelH, makeHudColor(175, 214, 255, 230));
    canvas.line(centerX, panelY + 10.0f, centerX, panelY + 74.0f, makeHudColor(175, 214, 255, 230));
    canvas.line(panelX + 20.0f, centerY, panelX + 84.0f, centerY, makeHudColor(175, 214, 255, 230));
    canvas.fillRect(handleX - 6.0f, handleY - 6.0f, 12.0f, 12.0f, makeHudColor(240, 248, 255, 245));

    const float rudderX = panelX + 102.0f;
    const float rudderY = panelY + 30.0f;
    canvas.line(rudderX, rudderY, rudderX + 60.0f, rudderY, makeHudColor(175, 214, 255, 230));
    const float rudderT = (clamp(plane.yoke.yaw, -1.0f, 1.0f) + 1.0f) * 0.5f;
    canvas.fillRect((rudderX + (rudderT * 60.0f)) - 4.0f, rudderY - 6.0f, 8.0f, 12.0f, makeHudColor(82, 224, 142, 245));
    canvas.text(panelX + 14.0f, panelY + 76.0f, "Yoke", makeHudColor(220, 234, 255, 255));
    canvas.text(panelX + 104.0f, panelY + 40.0f, "Rudder", makeHudColor(220, 234, 255, 255));
}

void drawMapPanel(
    HudCanvas& canvas,
    int width,
    int height,
    const FlightState& plane,
    const TerrainFieldContext& terrainContext,
    const UiState& uiState)
{
    if (!uiState.showMap) {
        return;
    }

    const int zoomIndex = std::clamp(uiState.mapZoomIndex, 0, static_cast<int>(uiState.mapZoomExtents.size()) - 1);
    const float extent = uiState.mapZoomExtents[zoomIndex];
    const float heading = getStableYawFromRotation(plane.rot);
    const float panelSize = clamp(static_cast<float>(height) * 0.26f, 150.0f, 280.0f);
    const float panelX = static_cast<float>(width) - panelSize - 18.0f;
    const float panelY = 16.0f;
    const int cells = 40;
    const float cell = panelSize / static_cast<float>(cells);

    canvas.fillRect(panelX, panelY, panelSize, panelSize, makeHudColor(6, 12, 18, 190));
    for (int row = 0; row < cells; ++row) {
        for (int col = 0; col < cells; ++col) {
            const float normalizedX = ((static_cast<float>(col) + 0.5f) / static_cast<float>(cells)) * 2.0f - 1.0f;
            const float normalizedY = ((static_cast<float>(row) + 0.5f) / static_cast<float>(cells)) * 2.0f - 1.0f;
            const float mapX = normalizedX * extent;
            const float mapZ = -normalizedY * extent;
            const Vec2 worldDelta = mapToWorldDelta(mapX, mapZ, heading, uiState.mapNorthUp);
            const float worldX = plane.pos.x + worldDelta.x;
            const float worldZ = plane.pos.z + worldDelta.y;
            const float worldY = sampleGroundHeight(worldX, worldZ, terrainContext);
            const Vec3 color = sampleTerrainColor(worldX, worldY, worldZ, terrainContext);
            canvas.fillRect(
                panelX + (static_cast<float>(col) * cell),
                panelY + (static_cast<float>(row) * cell),
                cell + 1.0f,
                cell + 1.0f,
                makeHudColor(color));
        }
    }

    const float centerX = panelX + (panelSize * 0.5f);
    const float centerY = panelY + (panelSize * 0.5f);
    const HudColor marker = makeHudColor(255, 255, 255, 240);

    const float markerYaw = uiState.mapNorthUp ? heading : 0.0f;
    const auto rotateMarkerPoint = [centerX, centerY, markerYaw](float localX, float localY) -> Vec2 {
        const float cosYaw = std::cos(markerYaw);
        const float sinYaw = std::sin(markerYaw);
        return {
            centerX + (localX * cosYaw) - (localY * sinYaw),
            centerY + (localX * sinYaw) + (localY * cosYaw)
        };
    };

    const Vec2 nose = rotateMarkerPoint(0.0f, -10.0f);
    const Vec2 left = rotateMarkerPoint(-6.0f, 6.0f);
    const Vec2 right = rotateMarkerPoint(6.0f, 6.0f);
    canvas.line(nose.x, nose.y, left.x, left.y, marker);
    canvas.line(nose.x, nose.y, right.x, right.y, marker);
    canvas.line(left.x, left.y, right.x, right.y, marker);
    canvas.strokeRect(panelX, panelY, panelSize, panelSize, marker);

    const std::array<const char*, 5> zoomLabels { "Near", "Mid", "Wide", "Far", "Full" };
    canvas.text(
        panelX,
        panelY + panelSize + 6.0f,
        std::string("Map ") + zoomLabels[zoomIndex] + (uiState.mapNorthUp ? " North-Up" : " Heading-Up"),
        makeHudColor(230, 240, 255, 255));
}

void drawImagePreview(HudCanvas& canvas, const RgbaImage& image, const RgbaImage* overlay, const RectF& rect)
{
    if (rect.w <= 0.0f || rect.h <= 0.0f || image.width <= 0 || image.height <= 0 || image.pixels.empty()) {
        return;
    }

    for (int dy = 0; dy < static_cast<int>(std::floor(rect.h)); ++dy) {
        const int sy = std::clamp(static_cast<int>((static_cast<float>(dy) / std::max(rect.h, 1.0f)) * static_cast<float>(image.height)), 0, image.height - 1);
        for (int dx = 0; dx < static_cast<int>(std::floor(rect.w)); ++dx) {
            const int sx = std::clamp(static_cast<int>((static_cast<float>(dx) / std::max(rect.w, 1.0f)) * static_cast<float>(image.width)), 0, image.width - 1);
            const std::size_t index = static_cast<std::size_t>(((sy * image.width) + sx) * 4);

            Vec4 sample {
                static_cast<float>(image.pixels[index + 0]) / 255.0f,
                static_cast<float>(image.pixels[index + 1]) / 255.0f,
                static_cast<float>(image.pixels[index + 2]) / 255.0f,
                static_cast<float>(image.pixels[index + 3]) / 255.0f
            };

            if (overlay != nullptr &&
                overlay->width == image.width &&
                overlay->height == image.height &&
                overlay->pixels.size() == image.pixels.size()) {
                const float overlayAlpha = static_cast<float>(overlay->pixels[index + 3]) / 255.0f;
                sample.x = mix(sample.x, static_cast<float>(overlay->pixels[index + 0]) / 255.0f, overlayAlpha);
                sample.y = mix(sample.y, static_cast<float>(overlay->pixels[index + 1]) / 255.0f, overlayAlpha);
                sample.z = mix(sample.z, static_cast<float>(overlay->pixels[index + 2]) / 255.0f, overlayAlpha);
                sample.w = clamp(sample.w + overlayAlpha, 0.0f, 1.0f);
            }

            canvas.point(rect.x + static_cast<float>(dx), rect.y + static_cast<float>(dy), makeHudColor(sample));
        }
    }
}

void drawPauseMenu(
    HudCanvas& canvas,
    int width,
    int height,
    const PauseState& pauseState,
    const UiState& uiState,
    const FlightConfig& config,
    const PropAudioConfig& propAudioConfig,
    const TerrainParams& terrainParams,
    const std::vector<AssetEntry>& assetCatalog,
    const PlaneVisualState& planeVisual,
    const PlaneVisualState& walkingVisual,
    const PaintUiState& paintUi)
{
    canvas.fillRect(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), makeHudColor(2, 4, 7, 168));
    const PauseLayout layout = buildPauseLayout(width, height);

    canvas.fillRect(layout.panelX, layout.panelY, layout.panelW, layout.panelH, makeHudColor(8, 14, 22, 240));
    canvas.strokeRect(layout.panelX, layout.panelY, layout.panelW, layout.panelH, makeHudColor(186, 224, 255, 255));
    canvas.text(layout.panelX + 24.0f, layout.panelY + 22.0f, "TrueFlight", makeHudColor(240, 247, 255, 255));
    canvas.text(layout.panelX + 24.0f, layout.panelY + 40.0f, "Paused", makeHudColor(172, 208, 238, 255));

    const float tabGap = 8.0f;
    for (std::size_t i = 0; i < kPauseTabs.size(); ++i) {
        const float x = layout.panelX + 24.0f + static_cast<float>(i) * (layout.tabW + tabGap);
        const bool active = static_cast<int>(pauseState.tab) == static_cast<int>(i);
        canvas.fillRect(x, layout.tabY, layout.tabW, layout.tabH, active ? makeHudColor(58, 112, 168, 220) : makeHudColor(18, 28, 40, 220));
        canvas.strokeRect(x, layout.tabY, layout.tabW, layout.tabH, active ? makeHudColor(218, 239, 255, 255) : makeHudColor(96, 132, 164, 255));
        const std::string_view label(kPauseTabs[i]);
        const float labelWidth = static_cast<float>(label.size() * 8u);
        canvas.text(x + std::max(8.0f, (layout.tabW - labelWidth) * 0.5f), layout.tabY + 9.0f, label, makeHudColor(240, 247, 255, 255));
    }

    const auto drawSubTabs = [&](int count, auto labelForIndex, int activeIndex) {
        for (int index = 0; index < count; ++index) {
            const RectF rect = pauseSubTabRect(layout, pauseState.tab, index, count);
            const bool active = index == activeIndex;
            canvas.fillRect(rect.x, rect.y, rect.w, rect.h, active ? makeHudColor(40, 92, 144, 220) : makeHudColor(15, 24, 34, 220));
            canvas.strokeRect(rect.x, rect.y, rect.w, rect.h, active ? makeHudColor(218, 239, 255, 255) : makeHudColor(84, 118, 152, 255));
            const std::string label = labelForIndex(index);
            canvas.text(rect.x + 10.0f, rect.y + 8.0f, label, makeHudColor(240, 247, 255, 255));
        }
    };

    if (pauseState.tab == PauseTab::Settings) {
        drawSubTabs(5, [](int index) { return std::string(settingsSubTabLabel(static_cast<SettingsSubTab>(index))); }, static_cast<int>(pauseState.settingsSubTab));
    } else if (pauseState.tab == PauseTab::Characters || pauseState.tab == PauseTab::Paint) {
        drawSubTabs(2, [](int index) { return std::string(characterRoleLabel(static_cast<CharacterSubTab>(index))); }, static_cast<int>(activeRoleForTab(pauseState, pauseState.tab)));
    }

    const PlaneVisualState& characterVisual = visualForRole(pauseState.charactersSubTab, planeVisual, walkingVisual);
    const PlaneVisualState& paintVisual = visualForRole(pauseState.paintSubTab, planeVisual, walkingVisual);
    const float contentX = layout.contentX;
    const float contentY = layout.contentY;

    if (pauseState.tab == PauseTab::Main) {
        const auto items = buildPauseMainItems(uiState);
        for (std::size_t i = 0; i < items.size(); ++i) {
            const RectF rowRect = pauseRowRect(layout, pauseState, assetCatalog.size(), static_cast<int>(i));
            const bool selected = static_cast<int>(i) == pauseState.selectedIndex;
            if (selected) {
                canvas.fillRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(46, 83, 124, 210));
                canvas.strokeRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(220, 238, 255, 255));
            }
            canvas.text(rowRect.x + 12.0f, rowRect.y + 9.0f, items[i], makeHudColor(240, 247, 255, 255));
        }
    } else if (pauseState.tab == PauseTab::Settings) {
        if (pauseState.settingsSubTab == SettingsSubTab::Flight) {
            const std::array<const char*, 13> labels {
                "Mass",
                "Max Thrust",
                "CLalpha",
                "CD0",
                "Induced Drag K",
                "CmAlpha",
                "Pitch Control",
                "Roll Control",
                "Yaw Control",
                "Elevator Limit",
                "Aileron Limit",
                "Auto Trim",
                "Ground Friction"
            };

            for (std::size_t i = 0; i < labels.size(); ++i) {
                const RectF rowRect = pauseRowRect(layout, pauseState, assetCatalog.size(), static_cast<int>(i));
                const bool selected = static_cast<int>(i) == pauseState.selectedIndex;
                if (selected) {
                    canvas.fillRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(46, 83, 124, 210));
                    canvas.strokeRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(220, 238, 255, 255));
                }

                canvas.text(rowRect.x + 10.0f, rowRect.y + 8.0f, labels[i], makeHudColor(240, 247, 255, 255));
                canvas.text(rowRect.x + 250.0f, rowRect.y + 8.0f, formatTuningValue(static_cast<int>(i), config), makeHudColor(162, 230, 186, 255));
            }
            canvas.text(contentX, layout.panelY + layout.panelH - 62.0f, tuningHelpText(pauseState.selectedIndex), makeHudColor(205, 225, 242, 255));
        } else if (pauseState.settingsSubTab == SettingsSubTab::Terrain) {
            const std::array<const char*, kTerrainSettingCount> labels {
                "Seed",
                "Chunk Size",
                "World Radius",
                "Gameplay Radius",
                "Mid Radius",
                "Horizon Radius",
                "Terrain Quality",
                "Mesh Budget",
                "LOD0 Radius",
                "LOD1 Radius",
                "LOD2 Radius",
                "Height Amp",
                "Height Freq",
                "Detail Amp",
                "Ridge Amp",
                "Terrace Strength",
                "Water Level",
                "Snow Line",
                "Props",
                "Prop Density",
                "Near Density",
                "Mid Density",
                "Far Density",
                "Shore Brush",
                "Rock Density",
                "Tree Line",
                "Prop Collision",
                "Prop Seed",
                "Caves",
                "Cave Strength",
                "Cave Threshold",
                "Tunnel Count",
                "Tunnel Radius Min",
                "Tunnel Radius Max",
                "Surface-Only Mesh",
                "Skirts",
                "Skirt Depth",
                "Max Cells/Axis"
            };

            const int startIndex = terrainVisibleStartIndex(pauseState.selectedIndex);
            const int endIndex = std::min(kTerrainSettingCount, startIndex + kTerrainVisibleRows);
            for (int index = startIndex; index < endIndex; ++index) {
                const RectF rowRect = pauseRowRect(layout, pauseState, assetCatalog.size(), index);
                const bool selected = index == pauseState.selectedIndex;
                if (selected) {
                    canvas.fillRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(46, 83, 124, 210));
                    canvas.strokeRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(220, 238, 255, 255));
                }

                canvas.text(rowRect.x + 10.0f, rowRect.y + 8.0f, labels[static_cast<std::size_t>(index)], makeHudColor(240, 247, 255, 255));
                canvas.text(rowRect.x + 300.0f, rowRect.y + 8.0f, formatTerrainValue(index, terrainParams), makeHudColor(162, 230, 186, 255));
            }

            canvas.text(
                contentX,
                contentY + 38.0f + (static_cast<float>(kTerrainVisibleRows) * 27.0f) + 4.0f,
                std::string("Rows ") + std::to_string(startIndex + 1) + "-" + std::to_string(endIndex) + " / " + std::to_string(kTerrainSettingCount),
                makeHudColor(180, 214, 240, 255));
            canvas.text(contentX, layout.panelY + layout.panelH - 62.0f, terrainHelpText(pauseState.selectedIndex), makeHudColor(205, 225, 242, 255));
        } else {
            const int count = settingsSubTabItemCount(pauseState.settingsSubTab);
            for (int index = 0; index < count; ++index) {
                const RectF rowRect = pauseRowRect(layout, pauseState, assetCatalog.size(), index);
                const bool selected = index == pauseState.selectedIndex;
                if (selected) {
                    canvas.fillRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(46, 83, 124, 210));
                    canvas.strokeRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(220, 238, 255, 255));
                }

                canvas.text(rowRect.x + 10.0f, rowRect.y + 8.0f, settingsRowLabel(pauseState.settingsSubTab, index), makeHudColor(240, 247, 255, 255));
                canvas.text(rowRect.x + 290.0f, rowRect.y + 8.0f, formatSettingsRowValue(pauseState.settingsSubTab, index, uiState, propAudioConfig), makeHudColor(162, 230, 186, 255));
            }
            canvas.text(contentX, layout.panelY + layout.panelH - 62.0f, settingsRowHelpText(pauseState.settingsSubTab, pauseState.selectedIndex), makeHudColor(205, 225, 242, 255));
        }
    } else if (pauseState.tab == PauseTab::Characters) {
        canvas.text(contentX, contentY + 10.0f, "Per-role model selection, transforms, preview zoom, and turntable control.", makeHudColor(180, 214, 240, 255));
        canvas.text(contentX, contentY + 24.0f, "Drop STL/GLB/GLTF files on the window while this tab is open to load them into the active role.", makeHudColor(180, 214, 240, 255));

        const int totalRows = pauseItemCount(pauseState, assetCatalog.size());
        if (assetCatalog.empty()) {
            canvas.text(contentX, contentY + 38.0f + (static_cast<float>(kCharacterAssetListStart + 1) * 28.0f), "No models found in portSource/Assets/Models yet.", makeHudColor(255, 220, 168, 255));
        }
        for (int index = 0; index < totalRows; ++index) {
            const RectF rowRect = pauseRowRect(layout, pauseState, assetCatalog.size(), index);
            const bool selected = index == pauseState.selectedIndex;
            if (selected) {
                canvas.fillRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(46, 83, 124, 210));
                canvas.strokeRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(220, 238, 255, 255));
            }

            if (index < kCharacterAssetListStart) {
                canvas.text(rowRect.x + 10.0f, rowRect.y + 7.0f, characterRowLabel(index), makeHudColor(240, 247, 255, 255));
                canvas.text(rowRect.x + 240.0f, rowRect.y + 7.0f, formatCharacterRowValue(index, characterVisual), makeHudColor(162, 230, 186, 255));
            } else {
                const AssetEntry& asset = assetCatalog[static_cast<std::size_t>(index - kCharacterAssetListStart)];
                const bool currentAsset = !characterVisual.sourcePath.empty() && characterVisual.sourcePath == asset.path;
                const HudColor rowColor = asset.supported ? makeHudColor(240, 247, 255, 255) : makeHudColor(255, 196, 124, 255);
                canvas.text(rowRect.x + 10.0f, rowRect.y + 7.0f, currentAsset ? (std::string("* ") + asset.label) : asset.label, rowColor);
            }
        }

        if (layout.previewW > 0.0f) {
            canvas.fillRect(layout.previewX, layout.previewY, layout.previewW, layout.previewH, makeHudColor(6, 12, 18, 210));
            canvas.strokeRect(layout.previewX, layout.previewY, layout.previewW, layout.previewH, makeHudColor(120, 166, 208, 255));
            canvas.text(layout.previewX + 10.0f, layout.previewY + 8.0f, "Preview", makeHudColor(240, 247, 255, 255));
            canvas.text(layout.previewX + 10.0f, layout.previewY + 24.0f, characterVisual.label, makeHudColor(180, 214, 240, 255));
            canvas.text(layout.previewX + 10.0f, layout.previewY + 42.0f, characterVisual.previewAutoSpin ? "Turntable: auto-spin" : "Turntable: manual", makeHudColor(180, 214, 240, 255));
            canvas.text(layout.previewX + 10.0f, layout.previewY + 58.0f, characterVisual.paintSupported ? "Paint: supported" : "Paint: textured UV model required", characterVisual.paintSupported ? makeHudColor(162, 230, 186, 255) : makeHudColor(255, 196, 124, 255));
        }

        canvas.text(contentX, layout.panelY + layout.panelH - 62.0f, characterRowHelpText(pauseState.selectedIndex), makeHudColor(205, 225, 242, 255));
    } else if (pauseState.tab == PauseTab::Paint) {
        canvas.text(contentX, contentY + 10.0f, "Edit RGBA paint overlays on top of the active role's base-color texture.", makeHudColor(180, 214, 240, 255));
        canvas.text(contentX, contentY + 24.0f, "Brush on the right canvas, then Commit to persist a PNG under the SDL pref path.", makeHudColor(180, 214, 240, 255));

        for (int index = 0; index < kPaintSettingCount; ++index) {
            const RectF rowRect = pauseRowRect(layout, pauseState, assetCatalog.size(), index);
            const bool selected = index == pauseState.selectedIndex;
            if (selected) {
                canvas.fillRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(46, 83, 124, 210));
                canvas.strokeRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(220, 238, 255, 255));
            }

            canvas.text(rowRect.x + 10.0f, rowRect.y + 7.0f, paintRowLabel(index), makeHudColor(240, 247, 255, 255));
            canvas.text(rowRect.x + 220.0f, rowRect.y + 7.0f, formatPaintRowValue(index, paintUi, paintVisual), makeHudColor(162, 230, 186, 255));
        }

        if (layout.previewW > 0.0f) {
            canvas.fillRect(layout.previewX, layout.previewY, layout.previewW, layout.previewH, makeHudColor(6, 12, 18, 210));
            canvas.strokeRect(layout.previewX, layout.previewY, layout.previewW, layout.previewH, makeHudColor(120, 166, 208, 255));
            canvas.text(layout.previewX + 10.0f, layout.previewY + 8.0f, "Paint Canvas", makeHudColor(240, 247, 255, 255));

            const RectF canvasRect = paintCanvasRect(layout);
            canvas.fillRect(canvasRect.x, canvasRect.y, canvasRect.w, canvasRect.h, makeHudColor(18, 26, 36, 255));
            canvas.strokeRect(canvasRect.x, canvasRect.y, canvasRect.w, canvasRect.h, makeHudColor(180, 214, 240, 255));
            if (const RgbaImage* baseImage = firstPaintBaseImage(paintVisual); baseImage != nullptr) {
                drawImagePreview(canvas, *baseImage, paintVisual.hasCommittedPaint ? &paintVisual.paintOverlay : nullptr, canvasRect);
            } else {
                canvas.text(canvasRect.x + 10.0f, canvasRect.y + 10.0f, "No textured base image", makeHudColor(255, 196, 124, 255));
            }

            for (int swatchIndex = 0; swatchIndex < 8; ++swatchIndex) {
                const float swatchX = layout.previewX + 10.0f + (static_cast<float>(swatchIndex % 4) * 26.0f);
                const float swatchY = canvasRect.y + canvasRect.h + 12.0f + (static_cast<float>(swatchIndex / 4) * 26.0f);
                const bool active = swatchIndex == paintUi.colorIndex;
                canvas.fillRect(swatchX, swatchY, 20.0f, 20.0f, makeHudColor(paintPresetColor(swatchIndex)));
                canvas.strokeRect(swatchX, swatchY, 20.0f, 20.0f, active ? makeHudColor(240, 247, 255, 255) : makeHudColor(84, 118, 152, 255));
            }
        }

        canvas.text(contentX, layout.panelY + layout.panelH - 62.0f, paintRowHelpText(pauseState.selectedIndex, paintVisual), makeHudColor(205, 225, 242, 255));
    } else if (pauseState.tab == PauseTab::Hud) {
        const std::array<const char*, 5> labels {
            "Debug Overlay",
            "Throttle Bar",
            "Control Indicator",
            "Mini Map",
            "Geo Readout"
        };

        for (std::size_t i = 0; i < labels.size(); ++i) {
            const RectF rowRect = pauseRowRect(layout, pauseState, assetCatalog.size(), static_cast<int>(i));
            const bool selected = static_cast<int>(i) == pauseState.selectedIndex;
            if (selected) {
                canvas.fillRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(46, 83, 124, 210));
                canvas.strokeRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(220, 238, 255, 255));
            }

            canvas.text(rowRect.x + 10.0f, rowRect.y + 8.0f, labels[i], makeHudColor(240, 247, 255, 255));
            canvas.text(rowRect.x + 290.0f, rowRect.y + 8.0f, formatHudValue(static_cast<int>(i), uiState), makeHudColor(162, 230, 186, 255));
        }
        canvas.text(contentX, layout.panelY + layout.panelH - 62.0f, hudHelpText(pauseState.selectedIndex), makeHudColor(205, 225, 242, 255));
    } else if (pauseState.tab == PauseTab::Controls) {
        const std::array<const char*, 14> lines {
            "W / S: pitch down / pitch up",
            "A / D: roll left / roll right",
            "Q / E: yaw left / yaw right",
            "Up / Down: throttle up / throttle down",
            "Mouse wheel: throttle",
            "Ctrl + Mouse wheel: pitch trim (up = nose down)",
            "Mouse: flight yoke input",
            "Right mouse: zoom",
            "Space: air brakes",
            "C: chase / cockpit camera",
            "G: localized terrain crater test",
            "M tap toggles map, hold M + Up/Down changes zoom",
            "F3: debug overlay",
            "Esc: pause / resume"
        };

        float y = contentY;
        for (const char* line : lines) {
            canvas.text(contentX, y, line, makeHudColor(230, 240, 255, 255));
            y += 16.0f;
        }
    } else {
        const std::array<const char*, 8> lines {
            "Esc resumes flight and also opens this menu in-game.",
            "F3 toggles the debug overlay.",
            "R resets the aircraft after a crash or bad approach.",
            "Hold Left Alt to release mouse capture without pausing.",
            "Tap M toggles the minimap without changing zoom.",
            "Hold M and press Up/Down to change map zoom.",
            "Use Settings for graphics, camera, sound, flight, and terrain tuning.",
            "Use Characters and Paint to manage role-specific models and overlays."
        };

        float y = contentY;
        for (const char* line : lines) {
            canvas.text(contentX, y, line, makeHudColor(230, 240, 255, 255));
            y += 16.0f;
        }
    }

    if (!pauseState.statusText.empty()) {
        canvas.text(layout.panelX + 24.0f, layout.panelY + layout.panelH - 44.0f, pauseState.statusText, makeHudColor(255, 220, 168, 255));
    }

    const char* footer = "Tab/H switch tabs | Esc resume";
    if (pauseState.tab == PauseTab::Main) {
        footer = "Tab/H switch tabs | W/S or Up/Down select | Enter apply | Mouse click activates | Esc resume";
    } else if (pauseState.tab == PauseTab::Settings) {
        footer = "Tab/H switch tabs | Q/E subtab | W/S select | A/D or Wheel adjust | RMB reset row";
    } else if (pauseState.tab == PauseTab::Characters) {
        footer = "Tab/H switch tabs | Q/E role | W/S select | A/D or Wheel adjust | Enter/LMB load | RMB reset row";
    } else if (pauseState.tab == PauseTab::Paint) {
        footer = "Tab/H switch tabs | Q/E role | W/S select | Paint on canvas | A/D or Wheel adjust | Enter activates";
    } else if (pauseState.tab == PauseTab::Hud) {
        footer = "Tab/H switch tabs | W/S select | A/D toggle | LMB toggles | RMB reset row";
    }
    canvas.text(layout.panelX + 24.0f, layout.panelY + layout.panelH - 28.0f, footer, makeHudColor(180, 214, 240, 255));
}

const char* windowModeLabel(WindowMode mode)
{
    switch (mode) {
    case WindowMode::Borderless:
        return "Borderless";
    case WindowMode::Fullscreen:
        return "Fullscreen";
    case WindowMode::Windowed:
    default:
        return "Windowed";
    }
}

std::string formatFixed(float value, int precision = 2)
{
    char buffer[64] {};
    std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
    return buffer;
}

std::string formatResolutionLabel(int width, int height)
{
    return std::to_string(width) + "x" + std::to_string(height);
}

constexpr std::array<std::pair<int, int>, 7> kMenuResolutionPresets { {
    { 1280, 720 },
    { 1366, 768 },
    { 1600, 900 },
    { 1920, 1080 },
    { 2560, 1440 },
    { 3200, 1800 },
    { 3840, 2160 }
} };

int currentResolutionPresetIndex(const GraphicsSettings& graphicsSettings)
{
    for (std::size_t index = 0; index < kMenuResolutionPresets.size(); ++index) {
        if (graphicsSettings.resolutionWidth == kMenuResolutionPresets[index].first &&
            graphicsSettings.resolutionHeight == kMenuResolutionPresets[index].second) {
            return static_cast<int>(index);
        }
    }
    return 0;
}

void cycleGraphicsResolution(GraphicsSettings& graphicsSettings, int direction)
{
    if (direction == 0) {
        return;
    }

    int index = currentResolutionPresetIndex(graphicsSettings);
    index = (index + direction + static_cast<int>(kMenuResolutionPresets.size())) % static_cast<int>(kMenuResolutionPresets.size());
    graphicsSettings.resolutionWidth = kMenuResolutionPresets[static_cast<std::size_t>(index)].first;
    graphicsSettings.resolutionHeight = kMenuResolutionPresets[static_cast<std::size_t>(index)].second;
}

WindowMode cycleWindowMode(WindowMode mode, int direction)
{
    if (direction == 0) {
        return mode;
    }

    int index = static_cast<int>(mode);
    constexpr int count = 3;
    index = (index + direction + count) % count;
    return static_cast<WindowMode>(index);
}

const char* menuSettingsRowLabel(SettingsSubTab subTab, int localIndex)
{
    switch (subTab) {
    case SettingsSubTab::Graphics:
        switch (localIndex) {
        case 0:
            return "Window Mode";
        case 1:
            return "Resolution";
        case 2:
            return "Apply Display";
        case 3:
            return "Render Scale";
        case 4:
            return "Draw Distance";
        case 5:
            return "Horizon Fog";
        case 6:
            return "Texture Mipmaps";
        case 7:
            return "VSync";
        case 8:
            return "Graphics API";
        default:
            return "";
        }
    case SettingsSubTab::Camera:
        switch (localIndex) {
        case 0:
            return "Field of View";
        case 1:
            return "Move Speed";
        case 2:
            return "Mouse Sensitivity";
        case 3:
            return "Invert Look Y";
        case 4:
            return "Camera Mode";
        case 5:
            return "Crosshair";
        case 6:
            return "Map Orientation";
        default:
            return "";
        }
    case SettingsSubTab::Sound:
        return settingsRowLabel(subTab, localIndex);
    case SettingsSubTab::Lighting:
        switch (localIndex) {
        case 0:
            return "Sun Marker";
        case 1:
            return "Sun Yaw";
        case 2:
            return "Sun Pitch";
        case 3:
            return "Sun Intensity";
        case 4:
            return "Sun Ambient";
        case 5:
            return "Sun Box Dist";
        case 6:
            return "Sun Box Size";
        case 7:
            return "Shadows";
        case 8:
            return "Shadow Softness";
        case 9:
            return "Shadow Distance";
        case 10:
            return "GI Specular";
        case 11:
            return "GI Bounce";
        case 12:
            return "Fog Density";
        case 13:
            return "Fog Height";
        case 14:
            return "Exposure EV";
        case 15:
            return "Turbidity";
        case 16:
            return "Sun Tint R";
        case 17:
            return "Sun Tint G";
        case 18:
            return "Sun Tint B";
        case 19:
            return "Sky Tint R";
        case 20:
            return "Sky Tint G";
        case 21:
            return "Sky Tint B";
        case 22:
            return "Ground Tint R";
        case 23:
            return "Ground Tint G";
        case 24:
            return "Ground Tint B";
        case 25:
            return "Fog R";
        case 26:
            return "Fog G";
        case 27:
            return "Fog B";
        default:
            return "";
        }
    case SettingsSubTab::Online:
        switch (localIndex) {
        case 0:
            return "Multiplayer";
        case 1:
            return "Hosted Server";
        case 2:
            return "Relay";
        case 3:
            return "Voice";
        case 4:
            return "Radio";
        default:
            return "";
        }
    default:
        return "";
    }
}

bool menuSettingsRowDisabled(SettingsSubTab subTab, int localIndex)
{
    if (subTab == SettingsSubTab::Graphics) {
        return localIndex == 8;
    }
    return subTab == SettingsSubTab::Online;
}

bool menuSettingsRowAction(SettingsSubTab subTab, int localIndex)
{
    return subTab == SettingsSubTab::Graphics && localIndex == 2;
}

std::string formatGraphicsRowValue(int localIndex, const GraphicsSettings& graphicsSettings)
{
    switch (localIndex) {
    case 0:
        return windowModeLabel(graphicsSettings.windowMode);
    case 1:
        return formatResolutionLabel(graphicsSettings.resolutionWidth, graphicsSettings.resolutionHeight);
    case 2:
        return "Press Enter";
    case 3:
        return formatFixed(graphicsSettings.renderScale, 2) + "x";
    case 4:
        return std::to_string(static_cast<int>(std::round(graphicsSettings.drawDistance))) + " m";
    case 5:
        return graphicsSettings.horizonFog ? "On" : "Off";
    case 6:
        return graphicsSettings.textureMipmaps ? "On" : "Off";
    case 7:
        return graphicsSettings.vsync ? "On" : "Off";
    case 8:
        return "Vulkan Only";
    default:
        return {};
    }
}

std::string formatCameraRowValue(int localIndex, const UiState& uiState)
{
    switch (localIndex) {
    case 0:
        return std::to_string(static_cast<int>(std::round(uiState.cameraFovDegrees))) + " deg";
    case 1:
        return formatFixed(uiState.walkingMoveSpeed, 0) + " u/s";
    case 2:
        return formatFixed(uiState.mouseSensitivity, 2);
    case 3:
        return uiState.invertLookY ? "On" : "Off";
    case 4:
        return uiState.chaseCamera ? "Chase" : "Cockpit";
    case 5:
        return uiState.showCrosshair ? "On" : "Off";
    case 6:
        return uiState.mapNorthUp ? "North-Up" : "Heading-Up";
    default:
        return {};
    }
}

std::string formatLightingRowValue(int localIndex, const LightingSettings& lightingSettings)
{
    switch (localIndex) {
    case 0:
        return lightingSettings.showSunMarker ? "On" : "Off";
    case 1:
    case 2:
        return formatFixed(localIndex == 1 ? lightingSettings.sunYawDegrees : lightingSettings.sunPitchDegrees, 0) + " deg";
    case 3:
        return formatFixed(lightingSettings.sunIntensity, 2);
    case 4:
        return formatFixed(lightingSettings.ambient, 2);
    case 5:
        return formatFixed(lightingSettings.markerDistance, 0) + " m";
    case 6:
        return formatFixed(lightingSettings.markerSize, 0) + " u";
    case 7:
        return lightingSettings.shadowEnabled ? "On" : "Off";
    case 8:
        return formatFixed(lightingSettings.shadowSoftness, 2);
    case 9:
        return formatFixed(lightingSettings.shadowDistance, 0) + " m";
    case 10:
        return formatFixed(lightingSettings.specularAmbient, 2);
    case 11:
        return formatFixed(lightingSettings.bounceStrength, 2);
    case 12:
        return formatFixed(lightingSettings.fogDensity, 4);
    case 13:
        return formatFixed(lightingSettings.fogHeightFalloff, 4);
    case 14:
        return formatFixed(lightingSettings.exposureEv, 1);
    case 15:
        return formatFixed(lightingSettings.turbidity, 1);
    case 16:
        return formatFixed(lightingSettings.sunTint.x, 2);
    case 17:
        return formatFixed(lightingSettings.sunTint.y, 2);
    case 18:
        return formatFixed(lightingSettings.sunTint.z, 2);
    case 19:
        return formatFixed(lightingSettings.skyTint.x, 2);
    case 20:
        return formatFixed(lightingSettings.skyTint.y, 2);
    case 21:
        return formatFixed(lightingSettings.skyTint.z, 2);
    case 22:
        return formatFixed(lightingSettings.groundTint.x, 2);
    case 23:
        return formatFixed(lightingSettings.groundTint.y, 2);
    case 24:
        return formatFixed(lightingSettings.groundTint.z, 2);
    case 25:
        return formatFixed(lightingSettings.fogColor.x, 2);
    case 26:
        return formatFixed(lightingSettings.fogColor.y, 2);
    case 27:
        return formatFixed(lightingSettings.fogColor.z, 2);
    default:
        return {};
    }
}

std::string formatOnlineRowValue(int)
{
    return "Unavailable";
}

std::string menuFormatSettingsRowValue(
    SettingsSubTab subTab,
    int localIndex,
    const UiState& uiState,
    const GraphicsSettings& graphicsSettings,
    const LightingSettings& lightingSettings,
    const PropAudioConfig& propAudioConfig)
{
    switch (subTab) {
    case SettingsSubTab::Graphics:
        return formatGraphicsRowValue(localIndex, graphicsSettings);
    case SettingsSubTab::Camera:
        return formatCameraRowValue(localIndex, uiState);
    case SettingsSubTab::Sound:
        return formatSettingsRowValue(subTab, localIndex, uiState, propAudioConfig);
    case SettingsSubTab::Lighting:
        return formatLightingRowValue(localIndex, lightingSettings);
    case SettingsSubTab::Online:
        return formatOnlineRowValue(localIndex);
    default:
        return {};
    }
}

const char* graphicsRowHelpText(int localIndex)
{
    switch (localIndex) {
    case 0:
        return "Switch between windowed, borderless fullscreen, and fullscreen modes.";
    case 1:
        return "Cycle the target display resolution used when applying video mode.";
    case 2:
        return "Apply the selected window mode and resolution immediately.";
    case 3:
        return "Internal render-scale preference. Persistence is ready even though the renderer still uses swapchain resolution.";
    case 4:
        return "Controls terrain/object fog distance and culling range.";
    case 5:
        return "Enable or disable horizon fog blending.";
    case 6:
        return "Texture mipmap preference for native assets.";
    case 7:
        return "Vertical sync preference for swapchain presentation.";
    case 8:
        return "Only the Vulkan renderer backend exists in the native port today.";
    default:
        return "";
    }
}

const char* cameraRowHelpText(int localIndex)
{
    switch (localIndex) {
    case 0:
        return "Adjust the active camera field of view in degrees.";
    case 1:
        return "Adjust walking-mode movement speed before sprint is applied.";
    case 2:
        return "Scales native mouse yoke sensitivity.";
    case 3:
        return "Invert the vertical mouse look direction.";
    case 4:
        return "Switch between chase and cockpit cameras.";
    case 5:
        return "Show or hide the cockpit crosshair when not in chase view.";
    case 6:
        return "Switch the minimap between heading-up and north-up orientation.";
    default:
        return "";
    }
}

const char* lightingRowHelpText(int localIndex)
{
    switch (localIndex) {
    case 0:
        return "Show the large analytic-sun debug cube used by the Lua renderer.";
    case 1:
        return "Rotate the sun around world up.";
    case 2:
        return "Raise or lower the sun elevation angle.";
    case 3:
        return "Direct-light intensity used by the native renderer.";
    case 4:
        return "Ambient fill factor used when deriving sky and scene lighting.";
    case 5:
        return "Distance from the camera to the analytic-sun debug cube.";
    case 6:
        return "Scale of the analytic-sun debug cube.";
    case 7:
        return "Enable native pseudo-shadow attenuation for nearby terrain and objects.";
    case 8:
        return "Softens the native shadow attenuation curve.";
    case 9:
        return "Limits how far from the camera pseudo-shadows are applied.";
    case 10:
        return "Controls sky-driven ambient specular energy.";
    case 11:
        return "Controls ground-bounce light strength for sunlit upward-facing surfaces.";
    case 12:
        return "Controls distance-fog density used for object fading.";
    case 13:
        return "Controls how quickly fog falls off with altitude.";
    case 14:
        return "Scene exposure offset in EV stops before tone mapping.";
    case 15:
        return "Analytic sky haze factor used when deriving the sky tone.";
    case 16:
    case 17:
    case 18:
        return "Per-channel tint multiplier applied to analytic sunlight.";
    case 19:
    case 20:
    case 21:
        return "Per-channel tint multiplier applied to the analytic sky dome.";
    case 22:
    case 23:
    case 24:
        return "Per-channel tint multiplier applied to ground hemisphere lighting.";
    case 25:
    case 26:
    case 27:
        return "Per-channel fog color used for atmospheric blending.";
    default:
        return "";
    }
}

const char* onlineRowHelpText(int)
{
    return "Networking, relay, and voice flows are still unsupported in the native port.";
}

const char* menuSettingsRowHelpText(SettingsSubTab subTab, int localIndex)
{
    switch (subTab) {
    case SettingsSubTab::Graphics:
        return graphicsRowHelpText(localIndex);
    case SettingsSubTab::Camera:
        return cameraRowHelpText(localIndex);
    case SettingsSubTab::Sound:
        return settingsRowHelpText(subTab, localIndex);
    case SettingsSubTab::Lighting:
        return lightingRowHelpText(localIndex);
    case SettingsSubTab::Online:
        return onlineRowHelpText(localIndex);
    default:
        return "";
    }
}

void adjustGraphicsRowValue(GraphicsSettings& graphicsSettings, int localIndex, int direction)
{
    switch (localIndex) {
    case 0:
        graphicsSettings.windowMode = cycleWindowMode(graphicsSettings.windowMode, direction);
        break;
    case 1:
        cycleGraphicsResolution(graphicsSettings, direction);
        break;
    case 3:
        graphicsSettings.renderScale += 0.05f * static_cast<float>(direction);
        break;
    case 4:
        graphicsSettings.drawDistance += 100.0f * static_cast<float>(direction);
        break;
    case 5:
        if (direction != 0) {
            graphicsSettings.horizonFog = !graphicsSettings.horizonFog;
        }
        break;
    case 6:
        if (direction != 0) {
            graphicsSettings.textureMipmaps = !graphicsSettings.textureMipmaps;
        }
        break;
    case 7:
        if (direction != 0) {
            graphicsSettings.vsync = !graphicsSettings.vsync;
        }
        break;
    default:
        break;
    }
    clampGraphicsSettings(graphicsSettings);
}

void resetGraphicsRowValue(GraphicsSettings& graphicsSettings, const GraphicsSettings& defaults, int localIndex)
{
    switch (localIndex) {
    case 0:
        graphicsSettings.windowMode = defaults.windowMode;
        break;
    case 1:
        graphicsSettings.resolutionWidth = defaults.resolutionWidth;
        graphicsSettings.resolutionHeight = defaults.resolutionHeight;
        break;
    case 3:
        graphicsSettings.renderScale = defaults.renderScale;
        break;
    case 4:
        graphicsSettings.drawDistance = defaults.drawDistance;
        break;
    case 5:
        graphicsSettings.horizonFog = defaults.horizonFog;
        break;
    case 6:
        graphicsSettings.textureMipmaps = defaults.textureMipmaps;
        break;
    case 7:
        graphicsSettings.vsync = defaults.vsync;
        break;
    default:
        break;
    }
}

void adjustCameraRowValue(UiState& uiState, int localIndex, int direction)
{
    switch (localIndex) {
    case 0:
        uiState.cameraFovDegrees = clamp(uiState.cameraFovDegrees + (5.0f * static_cast<float>(direction)), 60.0f, 120.0f);
        break;
    case 1:
        uiState.walkingMoveSpeed = clamp(uiState.walkingMoveSpeed + static_cast<float>(direction), 2.0f, 30.0f);
        break;
    case 2:
        uiState.mouseSensitivity = clamp(uiState.mouseSensitivity + (0.1f * static_cast<float>(direction)), 0.3f, 2.5f);
        break;
    case 3:
        if (direction != 0) {
            uiState.invertLookY = !uiState.invertLookY;
        }
        break;
    case 4:
        if (direction != 0) {
            uiState.chaseCamera = !uiState.chaseCamera;
        }
        break;
    case 5:
        if (direction != 0) {
            uiState.showCrosshair = !uiState.showCrosshair;
        }
        break;
    case 6:
        if (direction != 0) {
            uiState.mapNorthUp = !uiState.mapNorthUp;
        }
        break;
    default:
        break;
    }
}

void resetCameraRowValue(UiState& uiState, const UiState& defaults, int localIndex)
{
    switch (localIndex) {
    case 0:
        uiState.cameraFovDegrees = defaults.cameraFovDegrees;
        break;
    case 1:
        uiState.walkingMoveSpeed = defaults.walkingMoveSpeed;
        break;
    case 2:
        uiState.mouseSensitivity = defaults.mouseSensitivity;
        break;
    case 3:
        uiState.invertLookY = defaults.invertLookY;
        break;
    case 4:
        uiState.chaseCamera = defaults.chaseCamera;
        break;
    case 5:
        uiState.showCrosshair = defaults.showCrosshair;
        break;
    case 6:
        uiState.mapNorthUp = defaults.mapNorthUp;
        break;
    default:
        break;
    }
}

void adjustLightingRowValue(LightingSettings& lightingSettings, int localIndex, int direction)
{
    switch (localIndex) {
    case 0:
        if (direction != 0) {
            lightingSettings.showSunMarker = !lightingSettings.showSunMarker;
        }
        break;
    case 1:
        lightingSettings.sunYawDegrees += 2.0f * static_cast<float>(direction);
        break;
    case 2:
        lightingSettings.sunPitchDegrees += 2.0f * static_cast<float>(direction);
        break;
    case 3:
        lightingSettings.sunIntensity += 0.1f * static_cast<float>(direction);
        break;
    case 4:
        lightingSettings.ambient += 0.02f * static_cast<float>(direction);
        break;
    case 5:
        lightingSettings.markerDistance += 50.0f * static_cast<float>(direction);
        break;
    case 6:
        lightingSettings.markerSize += 10.0f * static_cast<float>(direction);
        break;
    case 7:
        if (direction != 0) {
            lightingSettings.shadowEnabled = !lightingSettings.shadowEnabled;
        }
        break;
    case 8:
        lightingSettings.shadowSoftness += 0.1f * static_cast<float>(direction);
        break;
    case 9:
        lightingSettings.shadowDistance += 50.0f * static_cast<float>(direction);
        break;
    case 10:
        lightingSettings.specularAmbient += 0.02f * static_cast<float>(direction);
        break;
    case 11:
        lightingSettings.bounceStrength += 0.02f * static_cast<float>(direction);
        break;
    case 12:
        lightingSettings.fogDensity += 0.0001f * static_cast<float>(direction);
        break;
    case 13:
        lightingSettings.fogHeightFalloff += 0.0001f * static_cast<float>(direction);
        break;
    case 14:
        lightingSettings.exposureEv += 0.1f * static_cast<float>(direction);
        break;
    case 15:
        lightingSettings.turbidity += 0.1f * static_cast<float>(direction);
        break;
    case 16:
        lightingSettings.sunTint.x += 0.05f * static_cast<float>(direction);
        break;
    case 17:
        lightingSettings.sunTint.y += 0.05f * static_cast<float>(direction);
        break;
    case 18:
        lightingSettings.sunTint.z += 0.05f * static_cast<float>(direction);
        break;
    case 19:
        lightingSettings.skyTint.x += 0.05f * static_cast<float>(direction);
        break;
    case 20:
        lightingSettings.skyTint.y += 0.05f * static_cast<float>(direction);
        break;
    case 21:
        lightingSettings.skyTint.z += 0.05f * static_cast<float>(direction);
        break;
    case 22:
        lightingSettings.groundTint.x += 0.05f * static_cast<float>(direction);
        break;
    case 23:
        lightingSettings.groundTint.y += 0.05f * static_cast<float>(direction);
        break;
    case 24:
        lightingSettings.groundTint.z += 0.05f * static_cast<float>(direction);
        break;
    case 25:
        lightingSettings.fogColor.x += 0.05f * static_cast<float>(direction);
        break;
    case 26:
        lightingSettings.fogColor.y += 0.05f * static_cast<float>(direction);
        break;
    case 27:
        lightingSettings.fogColor.z += 0.05f * static_cast<float>(direction);
        break;
    default:
        break;
    }
    clampLightingSettings(lightingSettings);
}

void resetLightingRowValue(LightingSettings& lightingSettings, const LightingSettings& defaults, int localIndex)
{
    switch (localIndex) {
    case 0:
        lightingSettings.showSunMarker = defaults.showSunMarker;
        break;
    case 1:
        lightingSettings.sunYawDegrees = defaults.sunYawDegrees;
        break;
    case 2:
        lightingSettings.sunPitchDegrees = defaults.sunPitchDegrees;
        break;
    case 3:
        lightingSettings.sunIntensity = defaults.sunIntensity;
        break;
    case 4:
        lightingSettings.ambient = defaults.ambient;
        break;
    case 5:
        lightingSettings.markerDistance = defaults.markerDistance;
        break;
    case 6:
        lightingSettings.markerSize = defaults.markerSize;
        break;
    case 7:
        lightingSettings.shadowEnabled = defaults.shadowEnabled;
        break;
    case 8:
        lightingSettings.shadowSoftness = defaults.shadowSoftness;
        break;
    case 9:
        lightingSettings.shadowDistance = defaults.shadowDistance;
        break;
    case 10:
        lightingSettings.specularAmbient = defaults.specularAmbient;
        break;
    case 11:
        lightingSettings.bounceStrength = defaults.bounceStrength;
        break;
    case 12:
        lightingSettings.fogDensity = defaults.fogDensity;
        break;
    case 13:
        lightingSettings.fogHeightFalloff = defaults.fogHeightFalloff;
        break;
    case 14:
        lightingSettings.exposureEv = defaults.exposureEv;
        break;
    case 15:
        lightingSettings.turbidity = defaults.turbidity;
        break;
    case 16:
        lightingSettings.sunTint.x = defaults.sunTint.x;
        break;
    case 17:
        lightingSettings.sunTint.y = defaults.sunTint.y;
        break;
    case 18:
        lightingSettings.sunTint.z = defaults.sunTint.z;
        break;
    case 19:
        lightingSettings.skyTint.x = defaults.skyTint.x;
        break;
    case 20:
        lightingSettings.skyTint.y = defaults.skyTint.y;
        break;
    case 21:
        lightingSettings.skyTint.z = defaults.skyTint.z;
        break;
    case 22:
        lightingSettings.groundTint.x = defaults.groundTint.x;
        break;
    case 23:
        lightingSettings.groundTint.y = defaults.groundTint.y;
        break;
    case 24:
        lightingSettings.groundTint.z = defaults.groundTint.z;
        break;
    case 25:
        lightingSettings.fogColor.x = defaults.fogColor.x;
        break;
    case 26:
        lightingSettings.fogColor.y = defaults.fogColor.y;
        break;
    case 27:
        lightingSettings.fogColor.z = defaults.fogColor.z;
        break;
    default:
        break;
    }
}

std::string formatHudRowValue(int hudIndex, const HudSettings& hudSettings)
{
    switch (hudIndex) {
    case 0:
        return hudSettings.showSpeedometer ? "On" : "Off";
    case 1:
        return std::to_string(hudSettings.speedometerMaxKph);
    case 2:
        return std::to_string(hudSettings.speedometerMinorStepKph);
    case 3:
        return std::to_string(hudSettings.speedometerMajorStepKph);
    case 4:
        return std::to_string(hudSettings.speedometerLabelStepKph);
    case 5:
        return std::to_string(hudSettings.speedometerRedlineKph);
    case 6:
        return hudSettings.showDebug ? "On" : "Off";
    case 7:
        return hudSettings.showThrottle ? "On" : "Off";
    case 8:
        return hudSettings.showControls ? "On" : "Off";
    case 9:
        return hudSettings.showMap ? "On" : "Off";
    case 10:
        return hudSettings.showGeoInfo ? "On" : "Off";
    case 11:
        return "Unavailable";
    default:
        return {};
    }
}

const char* hudRowLabel(int hudIndex)
{
    switch (hudIndex) {
    case 0:
        return "Speedometer";
    case 1:
        return "Max KPH";
    case 2:
        return "Minor Tick Step";
    case 3:
        return "Major Tick Step";
    case 4:
        return "Label Step";
    case 5:
        return "Redline KPH";
    case 6:
        return "Debug Overlay";
    case 7:
        return "Throttle Panel";
    case 8:
        return "Control Indicator";
    case 9:
        return "Mini Map";
    case 10:
        return "Geo Readout";
    case 11:
        return "Peer Indicators";
    default:
        return "";
    }
}

const char* hudRowHelpText2(int hudIndex)
{
    switch (hudIndex) {
    case 0:
        return "Show or hide the speedometer gauge.";
    case 1:
        return "Maximum speed shown by the speedometer arc.";
    case 2:
        return "KPH between minor speedometer ticks.";
    case 3:
        return "KPH between major speedometer ticks.";
    case 4:
        return "KPH between numeric speedometer labels.";
    case 5:
        return "KPH threshold where overspeed highlighting begins.";
    case 6:
        return "Toggle the native debug text block.";
    case 7:
        return "Show or hide the throttle module.";
    case 8:
        return "Show or hide the yoke/rudder indicator.";
    case 9:
        return "Show or hide the mini-map.";
    case 10:
        return "Show or hide geo information lines.";
    case 11:
        return "Peer indicators depend on networking, which is not yet present in the native port.";
    default:
        return "";
    }
}

bool hudRowDisabled(int hudIndex)
{
    return hudIndex == 11;
}

void adjustHudRowValue(HudSettings& hudSettings, int hudIndex, int direction)
{
    if (direction == 0 || hudRowDisabled(hudIndex)) {
        return;
    }

    switch (hudIndex) {
    case 0:
        hudSettings.showSpeedometer = !hudSettings.showSpeedometer;
        break;
    case 1:
        hudSettings.speedometerMaxKph += 50 * direction;
        break;
    case 2:
        hudSettings.speedometerMinorStepKph += 5 * direction;
        break;
    case 3:
        hudSettings.speedometerMajorStepKph += 10 * direction;
        break;
    case 4:
        hudSettings.speedometerLabelStepKph += 10 * direction;
        break;
    case 5:
        hudSettings.speedometerRedlineKph += 50 * direction;
        break;
    case 6:
        hudSettings.showDebug = !hudSettings.showDebug;
        break;
    case 7:
        hudSettings.showThrottle = !hudSettings.showThrottle;
        break;
    case 8:
        hudSettings.showControls = !hudSettings.showControls;
        break;
    case 9:
        hudSettings.showMap = !hudSettings.showMap;
        break;
    case 10:
        hudSettings.showGeoInfo = !hudSettings.showGeoInfo;
        break;
    default:
        break;
    }
    clampHudSettings(hudSettings);
}

void resetHudRowValue(HudSettings& hudSettings, const HudSettings& defaults, int hudIndex)
{
    switch (hudIndex) {
    case 0:
        hudSettings.showSpeedometer = defaults.showSpeedometer;
        break;
    case 1:
        hudSettings.speedometerMaxKph = defaults.speedometerMaxKph;
        break;
    case 2:
        hudSettings.speedometerMinorStepKph = defaults.speedometerMinorStepKph;
        break;
    case 3:
        hudSettings.speedometerMajorStepKph = defaults.speedometerMajorStepKph;
        break;
    case 4:
        hudSettings.speedometerLabelStepKph = defaults.speedometerLabelStepKph;
        break;
    case 5:
        hudSettings.speedometerRedlineKph = defaults.speedometerRedlineKph;
        break;
    case 6:
        hudSettings.showDebug = defaults.showDebug;
        break;
    case 7:
        hudSettings.showThrottle = defaults.showThrottle;
        break;
    case 8:
        hudSettings.showControls = defaults.showControls;
        break;
    case 9:
        hudSettings.showMap = defaults.showMap;
        break;
    case 10:
        hudSettings.showGeoInfo = defaults.showGeoInfo;
        break;
    case 11:
        hudSettings.showPeerIndicators = defaults.showPeerIndicators;
        break;
    default:
        break;
    }
}

void adjustMenuSettingsRowValue(
    UiState& uiState,
    GraphicsSettings& graphicsSettings,
    PropAudioConfig& propAudioConfig,
    LightingSettings& lightingSettings,
    SettingsSubTab subTab,
    int localIndex,
    int direction)
{
    if (menuSettingsRowDisabled(subTab, localIndex) || menuSettingsRowAction(subTab, localIndex)) {
        return;
    }

    switch (subTab) {
    case SettingsSubTab::Graphics:
        adjustGraphicsRowValue(graphicsSettings, localIndex, direction);
        break;
    case SettingsSubTab::Camera:
        adjustCameraRowValue(uiState, localIndex, direction);
        break;
    case SettingsSubTab::Sound:
        adjustSettingsRowValue(uiState, propAudioConfig, subTab, localIndex, direction);
        break;
    case SettingsSubTab::Lighting:
        adjustLightingRowValue(lightingSettings, localIndex, direction);
        break;
    case SettingsSubTab::Online:
    default:
        break;
    }
}

void resetMenuSettingsRowValue(
    UiState& uiState,
    const UiState& defaultUiStateValues,
    GraphicsSettings& graphicsSettings,
    const GraphicsSettings& defaultGraphicsSettingsValues,
    PropAudioConfig& propAudioConfig,
    const PropAudioConfig& defaultPropAudioConfigValues,
    LightingSettings& lightingSettings,
    const LightingSettings& defaultLightingSettingsValues,
    SettingsSubTab subTab,
    int localIndex)
{
    if (menuSettingsRowDisabled(subTab, localIndex) || menuSettingsRowAction(subTab, localIndex)) {
        return;
    }

    switch (subTab) {
    case SettingsSubTab::Graphics:
        resetGraphicsRowValue(graphicsSettings, defaultGraphicsSettingsValues, localIndex);
        break;
    case SettingsSubTab::Camera:
        resetCameraRowValue(uiState, defaultUiStateValues, localIndex);
        break;
    case SettingsSubTab::Sound:
        resetSettingsRowValue(uiState, defaultUiStateValues, propAudioConfig, defaultPropAudioConfigValues, subTab, localIndex);
        break;
    case SettingsSubTab::Lighting:
        resetLightingRowValue(lightingSettings, defaultLightingSettingsValues, localIndex);
        break;
    case SettingsSubTab::Online:
    default:
        break;
    }
}

int menuControlsVisibleStartIndex(int selection, int itemCount)
{
    if (itemCount <= kControlsVisibleRows) {
        return 0;
    }

    const int maxStart = itemCount - kControlsVisibleRows;
    return std::clamp(selection - (kControlsVisibleRows / 2), 0, maxStart);
}

int menuItemCount(const PauseState& pauseState, const ControlProfile& controls, std::size_t assetCount)
{
    switch (pauseState.tab) {
    case PauseTab::Main:
        return pauseState.mode == MenuMode::MainMenu ? 3 : 5;
    case PauseTab::Settings:
        return settingsSubTabItemCount(pauseState.settingsSubTab);
    case PauseTab::Characters:
        return characterItemCount(assetCount);
    case PauseTab::Paint:
        return kPaintSettingCount;
    case PauseTab::Hud:
        return kHudSettingCount;
    case PauseTab::Controls:
        return static_cast<int>(controls.actions.size());
    case PauseTab::Help:
    default:
        return 0;
    }
}

RectF menuControlSlotRect(const PauseLayout& layout, int rowYIndex, int slotIndex)
{
    const float rowX = layout.contentX;
    const float rowW = pauseContentWidth(layout, PauseTab::Controls);
    const float slotW = clamp(rowW * 0.22f, 120.0f, 170.0f);
    const float slotGap = 8.0f;
    const float slot2X = rowX + rowW - slotW;
    const float slot1X = slot2X - slotGap - slotW;
    const float y = layout.contentY + 28.0f + static_cast<float>(rowYIndex) * 32.0f;
    return {
        slotIndex == 0 ? slot1X : slot2X,
        y + 2.0f,
        slotW,
        24.0f
    };
}

RectF menuRowRect(
    const PauseLayout& layout,
    const PauseState& pauseState,
    const ControlProfile& controls,
    std::size_t assetCount,
    int rowIndex)
{
    const float rowX = layout.contentX;
    float rowY = layout.contentY;
    float rowH = 0.0f;
    const float rowW = pauseContentWidth(layout, pauseState.tab);

    switch (pauseState.tab) {
    case PauseTab::Main:
        rowY += static_cast<float>(rowIndex) * 36.0f;
        rowH = 28.0f;
        break;
    case PauseTab::Settings:
        rowY += 38.0f;
        if (pauseState.settingsSubTab == SettingsSubTab::Flight) {
            rowY += static_cast<float>(rowIndex) * 27.0f;
            rowH = 24.0f;
        } else if (pauseState.settingsSubTab == SettingsSubTab::Terrain) {
            const int startIndex = terrainVisibleStartIndex(pauseState.selectedIndex);
            if (rowIndex < startIndex || rowIndex >= std::min(kTerrainSettingCount, startIndex + kTerrainVisibleRows)) {
                return {};
            }
            rowY += static_cast<float>(rowIndex - startIndex) * 27.0f;
            rowH = 24.0f;
        } else {
            const int itemCount = settingsSubTabItemCount(pauseState.settingsSubTab);
            const int startIndex = settingsVisibleStartIndex(pauseState.settingsSubTab, pauseState.selectedIndex);
            const int endIndex = std::min(itemCount, startIndex + kSettingsVisibleRows);
            if (rowIndex < startIndex || rowIndex >= endIndex) {
                return {};
            }
            rowY += static_cast<float>(rowIndex - startIndex) * 30.0f;
            rowH = 24.0f;
        }
        break;
    case PauseTab::Characters:
    case PauseTab::Paint:
        rowY += 38.0f + static_cast<float>(rowIndex) * 28.0f;
        rowH = 22.0f;
        break;
    case PauseTab::Hud:
        rowY += static_cast<float>(rowIndex) * 28.0f;
        rowH = 24.0f;
        break;
    case PauseTab::Controls: {
        const int itemCount = static_cast<int>(controls.actions.size());
        const int startIndex = menuControlsVisibleStartIndex(pauseState.controlsSelection, itemCount);
        if (rowIndex < startIndex || rowIndex >= std::min(itemCount, startIndex + kControlsVisibleRows)) {
            return {};
        }
        rowY += 28.0f + static_cast<float>(rowIndex - startIndex) * 32.0f;
        rowH = 28.0f;
        break;
    }
    default:
        return {};
    }

    if (pauseState.tab == PauseTab::Characters && rowIndex >= characterItemCount(assetCount)) {
        return {};
    }
    if (pauseState.tab == PauseTab::Paint && rowIndex >= kPaintSettingCount) {
        return {};
    }
    return { rowX, rowY, rowW, rowH };
}

int menuHitItemIndex(
    const PauseLayout& layout,
    const PauseState& pauseState,
    const ControlProfile& controls,
    std::size_t assetCount,
    float mouseX,
    float mouseY)
{
    const int count = menuItemCount(pauseState, controls, assetCount);
    for (int index = 0; index < count; ++index) {
        const RectF rect = menuRowRect(layout, pauseState, controls, assetCount, index);
        if (rect.w <= 0.0f || rect.h <= 0.0f) {
            continue;
        }
        if (pointInRect(mouseX, mouseY, rect)) {
            return index;
        }
    }
    return -1;
}

void beginControlBindingCapture(PauseState& pauseState, int actionIndex, int slotIndex)
{
    pauseState.controlsCapturing = true;
    pauseState.controlsCaptureActionIndex = actionIndex;
    pauseState.controlsCaptureSlot = slotIndex;
    pauseState.controlsSelection = actionIndex;
    pauseState.controlsSlot = slotIndex;
}

void clearControlBindingCapture(PauseState& pauseState)
{
    pauseState.controlsCapturing = false;
    pauseState.controlsCaptureActionIndex = -1;
    pauseState.controlsCaptureSlot = -1;
}

bool clearSelectedControlBindingSlot(PauseState& pauseState, ControlProfile& controls)
{
    if (pauseState.controlsSelection < 0 ||
        pauseState.controlsSelection >= static_cast<int>(controls.actions.size()) ||
        pauseState.controlsSlot < 0 ||
        pauseState.controlsSlot >= 2) {
        return false;
    }

    ControlActionBinding& action = controls.actions[static_cast<std::size_t>(pauseState.controlsSelection)];
    if (!action.supported || !action.configurable) {
        return false;
    }

    action.slots[static_cast<std::size_t>(pauseState.controlsSlot)] = {};
    return true;
}

bool commitControlBindingCapture(PauseState& pauseState, ControlProfile& controls, const InputBinding& binding)
{
    if (!pauseState.controlsCapturing ||
        pauseState.controlsCaptureActionIndex < 0 ||
        pauseState.controlsCaptureActionIndex >= static_cast<int>(controls.actions.size()) ||
        pauseState.controlsCaptureSlot < 0 ||
        pauseState.controlsCaptureSlot >= 2) {
        return false;
    }

    ControlActionBinding& action = controls.actions[static_cast<std::size_t>(pauseState.controlsCaptureActionIndex)];
    if (!action.configurable || !action.supported) {
        clearControlBindingCapture(pauseState);
        return false;
    }

    action.slots[static_cast<std::size_t>(pauseState.controlsCaptureSlot)] = binding;
    clearControlBindingCapture(pauseState);
    return true;
}

bool captureBindingFromKey(PauseState& pauseState, ControlProfile& controls, SDL_Scancode scancode, SDL_Keymod modifiers)
{
    if (scancode == SDL_SCANCODE_ESCAPE) {
        clearControlBindingCapture(pauseState);
        return false;
    }

    InputBinding binding;
    binding.kind = BindingKind::Key;
    binding.scancode = scancode;
    binding.modifiers = normalizeBindingModifiers(modifiers);
    return commitControlBindingCapture(pauseState, controls, binding);
}

bool captureBindingFromMouseButton(PauseState& pauseState, ControlProfile& controls, std::uint8_t button, SDL_Keymod modifiers)
{
    InputBinding binding;
    binding.kind = BindingKind::MouseButton;
    binding.mouseButton = button;
    binding.modifiers = normalizeBindingModifiers(modifiers);
    return commitControlBindingCapture(pauseState, controls, binding);
}

bool captureBindingFromWheel(PauseState& pauseState, ControlProfile& controls, int wheelY, SDL_Keymod modifiers)
{
    if (wheelY == 0) {
        return false;
    }

    InputBinding binding;
    binding.kind = BindingKind::MouseWheel;
    binding.direction = wheelY > 0 ? 1 : -1;
    binding.modifiers = normalizeBindingModifiers(modifiers);
    return commitControlBindingCapture(pauseState, controls, binding);
}

bool captureBindingFromMouseMotion(PauseState& pauseState, ControlProfile& controls, float dx, float dy, SDL_Keymod modifiers)
{
    const float absX = std::fabs(dx);
    const float absY = std::fabs(dy);
    if (absX < 5.0f && absY < 5.0f) {
        return false;
    }

    InputBinding binding;
    binding.kind = BindingKind::MouseAxis;
    binding.modifiers = normalizeBindingModifiers(modifiers);
    if (absX >= absY) {
        binding.axis = 'x';
        binding.direction = dx >= 0.0f ? 1 : -1;
    } else {
        binding.axis = 'y';
        binding.direction = dy >= 0.0f ? 1 : -1;
    }
    return commitControlBindingCapture(pauseState, controls, binding);
}

void beginLoadingUi(LoadingUiState& loadingUi, const std::string& stage, float nowSeconds)
{
    loadingUi.active = true;
    loadingUi.stage = stage;
    loadingUi.detail.clear();
    loadingUi.progress = 0.0f;
    loadingUi.startedAt = nowSeconds;
    loadingUi.completedAt = 0.0f;
    loadingUi.currentEntry = 0;
    loadingUi.entries.clear();
}

void updateLoadingUi(LoadingUiState& loadingUi, const std::string& stage, float progress, const std::string& detail)
{
    loadingUi.stage = stage;
    loadingUi.detail = detail;
    loadingUi.progress = clamp(progress, 0.0f, 1.0f);
    if (!detail.empty()) {
        if (loadingUi.entries.empty() || loadingUi.entries.back() != detail) {
            loadingUi.entries.push_back(detail);
        }
        loadingUi.currentEntry = static_cast<int>(loadingUi.entries.size());
    }
}

void finishLoadingUi(LoadingUiState& loadingUi, float nowSeconds)
{
    loadingUi.progress = 1.0f;
    loadingUi.completedAt = nowSeconds;
    loadingUi.active = false;
}

Vec3 computeLightingSunDirection(const LightingSettings& lightingSettings)
{
    const float sunYaw = radians(lightingSettings.sunYawDegrees);
    const float sunPitch = radians(lightingSettings.sunPitchDegrees);
    return normalize(Vec3 {
        std::cos(sunPitch) * std::sin(sunYaw),
        std::sin(sunPitch),
        std::cos(sunPitch) * std::cos(sunYaw)
    }, { 0.0f, 1.0f, 0.0f });
}

Vec3 toneMapAcesColor(const Vec3& color)
{
    const Vec3 linear {
        std::max(0.0f, color.x),
        std::max(0.0f, color.y),
        std::max(0.0f, color.z)
    };
    return {
        clamp((linear.x * (2.51f * linear.x + 0.03f)) / (linear.x * (2.43f * linear.x + 0.59f) + 0.14f), 0.0f, 1.0f),
        clamp((linear.y * (2.51f * linear.y + 0.03f)) / (linear.y * (2.43f * linear.y + 0.59f) + 0.14f), 0.0f, 1.0f),
        clamp((linear.z * (2.51f * linear.z + 0.03f)) / (linear.z * (2.43f * linear.z + 0.59f) + 0.14f), 0.0f, 1.0f)
    };
}

Vec3 linearToSrgbColor(const Vec3& color)
{
    return {
        std::pow(std::max(0.0f, color.x), 1.0f / 2.2f),
        std::pow(std::max(0.0f, color.y), 1.0f / 2.2f),
        std::pow(std::max(0.0f, color.z), 1.0f / 2.2f)
    };
}

RendererLightingState evaluateRendererLightingState(const LightingSettings& lightingSettings, bool horizonFogEnabled)
{
    const Vec3 sunDirection = computeLightingSunDirection(lightingSettings);
    const float sunHeight = clamp((sunDirection.y * 0.5f) + 0.5f, 0.0f, 1.0f);
    const float haze = clamp((lightingSettings.turbidity - 1.0f) / 9.0f, 0.0f, 1.0f);

    const Vec3 zenith {
        mix(0.15f, 0.23f, sunHeight),
        mix(0.24f, 0.42f, sunHeight),
        mix(0.45f, 0.82f, sunHeight)
    };
    const Vec3 horizon {
        mix(0.45f, 0.78f, sunHeight),
        mix(0.42f, 0.63f, sunHeight),
        mix(0.36f, 0.46f, sunHeight)
    };
    const Vec3 analyticSky {
        mix(zenith.x, horizon.x, haze * 0.55f),
        mix(zenith.y, horizon.y, haze * 0.55f),
        mix(zenith.z, horizon.z, haze * 0.55f)
    };
    const Vec3 analyticGround {
        mix(0.16f, 0.24f, sunHeight),
        mix(0.13f, 0.20f, sunHeight),
        mix(0.10f, 0.15f, sunHeight)
    };
    const Vec3 analyticSunTint {
        mix(1.0f, 0.96f, haze),
        mix(0.98f, 0.88f, haze),
        mix(0.92f, 0.72f, haze)
    };

    RendererLightingState state;
    state.sunDirection = sunDirection;
    state.lightColor = clampLightingColor(hadamard(analyticSunTint, lightingSettings.sunTint) * lightingSettings.sunIntensity, 16.0f);
    state.skyColor = clampLightingColor(hadamard(analyticSky, lightingSettings.skyTint), 4.0f);
    state.groundColor = clampLightingColor(hadamard(analyticGround, lightingSettings.groundTint), 4.0f);
    state.fogColor = clampLightingColor(lightingSettings.fogColor, 4.0f);
    state.ambientStrength = lightingSettings.ambient;
    state.specularAmbientStrength = lightingSettings.specularAmbient;
    state.bounceStrength = lightingSettings.bounceStrength;
    state.fogDensity = horizonFogEnabled ? lightingSettings.fogDensity : 0.0f;
    state.fogHeightFalloff = lightingSettings.fogHeightFalloff;
    state.exposureEv = lightingSettings.exposureEv;
    state.turbidity = lightingSettings.turbidity;
    state.shadowEnabled = lightingSettings.shadowEnabled;
    state.shadowSoftness = lightingSettings.shadowSoftness;
    state.shadowDistance = lightingSettings.shadowDistance;

    const Vec3 exposedSky = state.skyColor * std::pow(2.0f, state.exposureEv);
    state.backgroundColor = linearToSrgbColor(toneMapAcesColor(exposedSky));
    return state;
}

Vec3 computeLightingSkyColor(const LightingSettings& lightingSettings)
{
    return evaluateRendererLightingState(lightingSettings, true).backgroundColor;
}

void applyFogSettings(RenderObject& object, const GraphicsSettings& graphicsSettings, const LightingSettings& lightingSettings)
{
    const float fogFar = std::max(200.0f, graphicsSettings.drawDistance);
    const float fogFactor = graphicsSettings.horizonFog ? clamp(lightingSettings.fogDensity * 4500.0f, 0.12f, 0.92f) : 0.96f;
    object.fogFar = fogFar;
    object.fogNear = std::max(60.0f, fogFar * fogFactor);
}

void drawLoadingOverlay(
    HudCanvas& canvas,
    int width,
    int height,
    const LoadingUiState& loadingUi,
    const std::string& rendererLabel,
    float nowSeconds)
{
    canvas.fillRect(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), makeHudColor(8, 14, 24, 255));
    canvas.fillRect(static_cast<float>(width) * 0.65f, -40.0f, static_cast<float>(width) * 0.5f, static_cast<float>(height) * 0.55f, makeHudColor(32, 58, 94, 96));
    canvas.fillRect(-80.0f, static_cast<float>(height) * 0.62f, static_cast<float>(width) * 0.58f, static_cast<float>(height) * 0.5f, makeHudColor(18, 34, 58, 92));

    const float panelW = std::min(820.0f, static_cast<float>(width) * 0.82f);
    const float panelH = std::min(360.0f, static_cast<float>(height) * 0.68f);
    const float panelX = (static_cast<float>(width) - panelW) * 0.5f;
    const float panelY = (static_cast<float>(height) - panelH) * 0.5f;

    canvas.fillRect(panelX + 4.0f, panelY + 4.0f, panelW, panelH, makeHudColor(0, 0, 0, 90));
    canvas.fillRect(panelX, panelY, panelW, panelH, makeHudColor(6, 12, 20, 244));
    canvas.fillRect(panelX + 2.0f, panelY + 2.0f, panelW - 4.0f, std::max(16.0f, panelH * 0.26f), makeHudColor(188, 226, 255, 28));
    canvas.strokeRect(panelX, panelY, panelW, panelH, makeHudColor(188, 226, 255, 255));

    const float spinnerCx = panelX + panelW - 52.0f;
    const float spinnerCy = panelY + 58.0f;
    const float spinnerOuter = 13.0f;
    const float spinnerInner = 7.0f;
    for (int index = 0; index < 12; ++index) {
        const float angle = (static_cast<float>(index) / 12.0f) * (kPi * 2.0f) + (nowSeconds * 2.6f);
        const std::uint8_t alpha = static_cast<std::uint8_t>(70 + (index * 14));
        const float x0 = spinnerCx + std::cos(angle) * spinnerInner;
        const float y0 = spinnerCy + std::sin(angle) * spinnerInner;
        const float x1 = spinnerCx + std::cos(angle) * spinnerOuter;
        const float y1 = spinnerCy + std::sin(angle) * spinnerOuter;
        canvas.line(x0, y0, x1, y1, makeHudColor(198, 236, 255, alpha));
    }

    canvas.text(panelX + 28.0f, panelY + 24.0f, loadingUi.stage, makeHudColor(238, 248, 255, 255));
    canvas.text(panelX + 28.0f, panelY + 74.0f, loadingUi.detail, makeHudColor(184, 214, 236, 255));

    const int firstEntry = std::max(0, static_cast<int>(loadingUi.entries.size()) - 5);
    float listY = panelY + 118.0f;
    for (int index = firstEntry; index < static_cast<int>(loadingUi.entries.size()); ++index) {
        const int row = index - firstEntry;
        const float y = listY + static_cast<float>(row) * 24.0f;
        const bool completed = index < (loadingUi.currentEntry - 1);
        const bool active = index == (loadingUi.currentEntry - 1);
        const char* prefix = active ? "[>]" : (completed ? "[x]" : "[ ]");
        const HudColor color = active
            ? makeHudColor(230, 246, 255, 255)
            : (completed ? makeHudColor(92, 232, 158, 255) : makeHudColor(144, 176, 204, 204));
        canvas.text(panelX + 30.0f, y, std::string(prefix) + " " + loadingUi.entries[static_cast<std::size_t>(index)], color);
    }

    const float barX = panelX + 28.0f;
    const float barY = panelY + panelH - 72.0f;
    const float barW = panelW - 56.0f;
    const float barH = 17.0f;
    const float fillW = clamp((barW - 4.0f) * loadingUi.progress, 0.0f, barW - 4.0f);
    canvas.fillRect(barX, barY, barW, barH, makeHudColor(16, 26, 38, 255));
    canvas.strokeRect(barX, barY, barW, barH, makeHudColor(188, 226, 255, 198));
    canvas.fillRect(barX + 2.0f, barY + 2.0f, fillW, barH - 4.0f, makeHudColor(62, 226, 146, 236));
    canvas.text(barX + barW - 44.0f, barY + 1.0f, std::to_string(static_cast<int>(std::round(loadingUi.progress * 100.0f))) + "%", makeHudColor(216, 236, 255, 255));

    const float elapsed = std::max(0.0f, nowSeconds - loadingUi.startedAt);
    char elapsedBuffer[64] {};
    std::snprintf(elapsedBuffer, sizeof(elapsedBuffer), "Startup %.2fs", elapsed);
    canvas.text(barX, barY + 24.0f, elapsedBuffer, makeHudColor(168, 204, 232, 255));
    canvas.text(barX + barW - (static_cast<float>(rendererLabel.size()) * 8.0f), barY + 24.0f, rendererLabel, makeHudColor(168, 204, 232, 255));
}

bool presentLoadingUi(
    SDL_Window* window,
    VulkanRenderer& renderer,
    HudCanvas& hudCanvas,
    const LoadingUiState& loadingUi,
    const std::string& rendererLabel,
    const LightingSettings& lightingSettings,
    std::string* errorText)
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    hudCanvas.resize(width, height);
    hudCanvas.clear({ 0, 0, 0, 0 });
    drawLoadingOverlay(
        hudCanvas,
        width,
        height,
        loadingUi,
        rendererLabel,
        static_cast<float>(SDL_GetTicks()) * 0.001f);

    Camera camera {};
    camera.pos = { 0.0f, 0.0f, -10.0f };
    camera.rot = quatIdentity();
    camera.fovRadians = radians(70.0f);
    std::vector<RenderObject> empty;
    const RendererLightingState lightingState = evaluateRendererLightingState(lightingSettings, true);
    return renderer.render(
        camera,
        lightingState,
        empty,
        empty,
        hudCanvas,
        errorText);
}

bool applyGraphicsSettingsToWindow(SDL_Window* window, const GraphicsSettings& graphicsSettings, std::string* errorText)
{
    if (window == nullptr) {
        return false;
    }

    const SDL_DisplayID displayId = SDL_GetDisplayForWindow(window);
    SDL_Rect bounds {};
    SDL_GetDisplayBounds(displayId, &bounds);

    if (graphicsSettings.windowMode == WindowMode::Windowed) {
        if (!SDL_SetWindowFullscreen(window, false)) {
            if (errorText != nullptr) {
                *errorText = SDL_GetError();
            }
            return false;
        }
        SDL_SetWindowBordered(window, true);
        SDL_SetWindowSize(window, graphicsSettings.resolutionWidth, graphicsSettings.resolutionHeight);
        if (bounds.w > 0 && bounds.h > 0) {
            const int x = bounds.x + ((bounds.w - graphicsSettings.resolutionWidth) / 2);
            const int y = bounds.y + ((bounds.h - graphicsSettings.resolutionHeight) / 2);
            SDL_SetWindowPosition(window, x, y);
        }
    } else if (graphicsSettings.windowMode == WindowMode::Borderless) {
        if (!SDL_SetWindowFullscreen(window, false)) {
            if (errorText != nullptr) {
                *errorText = SDL_GetError();
            }
            return false;
        }
        SDL_SetWindowBordered(window, false);
        if (bounds.w > 0 && bounds.h > 0) {
            SDL_SetWindowPosition(window, bounds.x, bounds.y);
            SDL_SetWindowSize(window, bounds.w, bounds.h);
        }
    } else {
        SDL_SetWindowBordered(window, false);
        SDL_SetWindowFullscreenMode(window, nullptr);
        if (!SDL_SetWindowFullscreen(window, true)) {
            if (errorText != nullptr) {
                *errorText = SDL_GetError();
            }
            return false;
        }
    }

    SDL_SyncWindow(window);
    return true;
}

void drawMenuOverlay(
    HudCanvas& canvas,
    int width,
    int height,
    const PauseState& pauseState,
    const UiState& uiState,
    const GraphicsSettings& graphicsSettings,
    const LightingSettings& lightingSettings,
    const HudSettings& hudSettings,
    const ControlProfile& controls,
    const FlightConfig& config,
    const PropAudioConfig& propAudioConfig,
    const TerrainParams& terrainParams,
    const std::vector<AssetEntry>& assetCatalog,
    const PlaneVisualState& planeVisual,
    const PlaneVisualState& walkingVisual,
    const PaintUiState& paintUi)
{
    canvas.fillRect(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), makeHudColor(2, 4, 7, 168));
    const PauseLayout layout = buildPauseLayout(width, height);

    canvas.fillRect(layout.panelX, layout.panelY, layout.panelW, layout.panelH, makeHudColor(8, 14, 22, 240));
    canvas.strokeRect(layout.panelX, layout.panelY, layout.panelW, layout.panelH, makeHudColor(186, 224, 255, 255));
    canvas.text(layout.panelX + 24.0f, layout.panelY + 22.0f, "TrueFlight", makeHudColor(240, 247, 255, 255));
    canvas.text(
        layout.panelX + 24.0f,
        layout.panelY + 40.0f,
        pauseState.mode == MenuMode::MainMenu
            ? "Main Menu"
            : (pauseState.sessionFlightMode ? "Paused - Flight" : "Paused - Walking"),
        makeHudColor(172, 208, 238, 255));

    const float tabGap = 8.0f;
    for (std::size_t i = 0; i < kPauseTabs.size(); ++i) {
        const float x = layout.panelX + 24.0f + static_cast<float>(i) * (layout.tabW + tabGap);
        const bool active = static_cast<int>(pauseState.tab) == static_cast<int>(i);
        canvas.fillRect(x, layout.tabY, layout.tabW, layout.tabH, active ? makeHudColor(58, 112, 168, 220) : makeHudColor(18, 28, 40, 220));
        canvas.strokeRect(x, layout.tabY, layout.tabW, layout.tabH, active ? makeHudColor(218, 239, 255, 255) : makeHudColor(96, 132, 164, 255));
        const std::string_view label(kPauseTabs[i]);
        const float labelWidth = static_cast<float>(label.size() * 8u);
        canvas.text(x + std::max(8.0f, (layout.tabW - labelWidth) * 0.5f), layout.tabY + 9.0f, label, makeHudColor(240, 247, 255, 255));
    }

    const auto drawSubTabs = [&](int count, auto labelForIndex, int activeIndex) {
        for (int index = 0; index < count; ++index) {
            const RectF rect = pauseSubTabRect(layout, pauseState.tab, index, count);
            const bool active = index == activeIndex;
            canvas.fillRect(rect.x, rect.y, rect.w, rect.h, active ? makeHudColor(40, 92, 144, 220) : makeHudColor(15, 24, 34, 220));
            canvas.strokeRect(rect.x, rect.y, rect.w, rect.h, active ? makeHudColor(218, 239, 255, 255) : makeHudColor(84, 118, 152, 255));
            canvas.text(rect.x + 10.0f, rect.y + 8.0f, labelForIndex(index), makeHudColor(240, 247, 255, 255));
        }
    };

    if (pauseState.tab == PauseTab::Settings) {
        drawSubTabs(7, [](int index) { return std::string(settingsSubTabLabel(static_cast<SettingsSubTab>(index))); }, static_cast<int>(pauseState.settingsSubTab));
    } else if (pauseState.tab == PauseTab::Characters || pauseState.tab == PauseTab::Paint) {
        drawSubTabs(2, [](int index) { return std::string(characterRoleLabel(static_cast<CharacterSubTab>(index))); }, static_cast<int>(activeRoleForTab(pauseState, pauseState.tab)));
    }

    const PlaneVisualState& characterVisual = visualForRole(pauseState.charactersSubTab, planeVisual, walkingVisual);
    const PlaneVisualState& paintVisual = visualForRole(pauseState.paintSubTab, planeVisual, walkingVisual);
    const float contentX = layout.contentX;
    const float contentY = layout.contentY;

    if (pauseState.tab == PauseTab::Main) {
        std::vector<std::string> items;
        if (pauseState.mode == MenuMode::MainMenu) {
            items = { "Start Flight", "Restore Defaults", "Quit" };
        } else {
            items = {
                "Resume",
                pauseState.sessionFlightMode ? "Switch to Walking Mode" : "Switch to Flight Mode",
                "Reset Flight",
                "Return to Main Menu",
                "Quit"
            };
        }

        for (std::size_t i = 0; i < items.size(); ++i) {
            const RectF rowRect = menuRowRect(layout, pauseState, controls, assetCatalog.size(), static_cast<int>(i));
            const bool selected = static_cast<int>(i) == pauseState.selectedIndex;
            if (selected) {
                canvas.fillRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(46, 83, 124, 210));
                canvas.strokeRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(220, 238, 255, 255));
            }
            std::string itemLabel = items[i];
            if (pauseState.confirmPending && pauseState.confirmSelectedIndex == static_cast<int>(i)) {
                itemLabel += " [Confirm]";
            }
            canvas.text(rowRect.x + 12.0f, rowRect.y + 9.0f, itemLabel, makeHudColor(240, 247, 255, 255));
        }
    } else if (pauseState.tab == PauseTab::Settings) {
        if (pauseState.settingsSubTab == SettingsSubTab::Flight) {
            const std::array<const char*, 13> labels {
                "Mass", "Max Thrust", "CLalpha", "CD0", "Induced Drag K", "CmAlpha", "Pitch Control", "Roll Control", "Yaw Control", "Elevator Limit", "Aileron Limit", "Auto Trim", "Ground Friction"
            };
            for (std::size_t i = 0; i < labels.size(); ++i) {
                const RectF rowRect = menuRowRect(layout, pauseState, controls, assetCatalog.size(), static_cast<int>(i));
                const bool selected = static_cast<int>(i) == pauseState.selectedIndex;
                if (selected) {
                    canvas.fillRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(46, 83, 124, 210));
                    canvas.strokeRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(220, 238, 255, 255));
                }
                canvas.text(rowRect.x + 10.0f, rowRect.y + 8.0f, labels[i], makeHudColor(240, 247, 255, 255));
                canvas.text(rowRect.x + 250.0f, rowRect.y + 8.0f, formatTuningValue(static_cast<int>(i), config), makeHudColor(162, 230, 186, 255));
            }
            canvas.text(contentX, layout.panelY + layout.panelH - 62.0f, tuningHelpText(pauseState.selectedIndex), makeHudColor(205, 225, 242, 255));
        } else if (pauseState.settingsSubTab == SettingsSubTab::Terrain) {
            const std::array<const char*, kTerrainSettingCount> labels {
                "Seed", "Chunk Size", "World Radius", "Gameplay Radius", "Mid Radius", "Horizon Radius", "Terrain Quality", "Mesh Budget", "LOD0 Radius", "LOD1 Radius", "LOD2 Radius", "Height Amp", "Height Freq", "Detail Amp", "Ridge Amp", "Terrace Strength", "Water Level", "Snow Line", "Props", "Prop Density", "Near Density", "Mid Density", "Far Density", "Shore Brush", "Rock Density", "Tree Line", "Prop Collision", "Prop Seed", "Caves", "Cave Strength", "Cave Threshold", "Tunnel Count", "Tunnel Radius Min", "Tunnel Radius Max", "Surface-Only Mesh", "Skirts", "Skirt Depth", "Max Cells/Axis"
            };
            const int startIndex = terrainVisibleStartIndex(pauseState.selectedIndex);
            const int endIndex = std::min(kTerrainSettingCount, startIndex + kTerrainVisibleRows);
            for (int index = startIndex; index < endIndex; ++index) {
                const RectF rowRect = menuRowRect(layout, pauseState, controls, assetCatalog.size(), index);
                const bool selected = index == pauseState.selectedIndex;
                if (selected) {
                    canvas.fillRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(46, 83, 124, 210));
                    canvas.strokeRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(220, 238, 255, 255));
                }
                canvas.text(rowRect.x + 10.0f, rowRect.y + 8.0f, labels[static_cast<std::size_t>(index)], makeHudColor(240, 247, 255, 255));
                canvas.text(rowRect.x + 300.0f, rowRect.y + 8.0f, formatTerrainValue(index, terrainParams), makeHudColor(162, 230, 186, 255));
            }
            canvas.text(contentX, contentY + 38.0f + (static_cast<float>(kTerrainVisibleRows) * 27.0f) + 4.0f, std::string("Rows ") + std::to_string(startIndex + 1) + "-" + std::to_string(endIndex) + " / " + std::to_string(kTerrainSettingCount), makeHudColor(180, 214, 240, 255));
            canvas.text(contentX, layout.panelY + layout.panelH - 62.0f, terrainHelpText(pauseState.selectedIndex), makeHudColor(205, 225, 242, 255));
        } else {
            const int count = settingsSubTabItemCount(pauseState.settingsSubTab);
            const int startIndex = settingsVisibleStartIndex(pauseState.settingsSubTab, pauseState.selectedIndex);
            const int endIndex = std::min(count, startIndex + kSettingsVisibleRows);
            for (int index = startIndex; index < endIndex; ++index) {
                const RectF rowRect = menuRowRect(layout, pauseState, controls, assetCatalog.size(), index);
                const bool selected = index == pauseState.selectedIndex;
                const bool disabled = menuSettingsRowDisabled(pauseState.settingsSubTab, index);
                const bool actionRow = menuSettingsRowAction(pauseState.settingsSubTab, index);
                if (selected) {
                    canvas.fillRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(46, 83, 124, 210));
                    canvas.strokeRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(220, 238, 255, 255));
                }
                canvas.text(rowRect.x + 10.0f, rowRect.y + 8.0f, menuSettingsRowLabel(pauseState.settingsSubTab, index), makeHudColor(240, 247, 255, 255));
                const HudColor valueColor = disabled ? makeHudColor(255, 196, 124, 255) : (actionRow ? makeHudColor(194, 226, 255, 255) : makeHudColor(162, 230, 186, 255));
                canvas.text(rowRect.x + 290.0f, rowRect.y + 8.0f, menuFormatSettingsRowValue(pauseState.settingsSubTab, index, uiState, graphicsSettings, lightingSettings, propAudioConfig), valueColor);
            }
            if (count > kSettingsVisibleRows) {
                canvas.text(
                    contentX,
                    contentY + 38.0f + (static_cast<float>(kSettingsVisibleRows) * 30.0f) + 4.0f,
                    std::string("Rows ") + std::to_string(startIndex + 1) + "-" + std::to_string(endIndex) + " / " + std::to_string(count),
                    makeHudColor(180, 214, 240, 255));
            }
            canvas.text(contentX, layout.panelY + layout.panelH - 62.0f, menuSettingsRowHelpText(pauseState.settingsSubTab, pauseState.selectedIndex), makeHudColor(205, 225, 242, 255));
        }
    } else if (pauseState.tab == PauseTab::Characters) {
        canvas.text(contentX, contentY + 10.0f, "Per-role model selection, transforms, preview zoom, and turntable control.", makeHudColor(180, 214, 240, 255));
        canvas.text(contentX, contentY + 24.0f, "Use Load From Path, choose a scanned asset below, or drag STL/GLB/GLTF files onto the window.", makeHudColor(180, 214, 240, 255));
        const int totalRows = characterItemCount(assetCatalog.size());
        if (assetCatalog.empty()) {
            canvas.text(contentX, contentY + 38.0f + (static_cast<float>(kCharacterAssetListStart + 1) * 28.0f), "No models found in portSource/Assets/Models yet.", makeHudColor(255, 220, 168, 255));
        }
        for (int index = 0; index < totalRows; ++index) {
            const RectF rowRect = menuRowRect(layout, pauseState, controls, assetCatalog.size(), index);
            const bool selected = index == pauseState.selectedIndex;
            if (selected) {
                canvas.fillRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(46, 83, 124, 210));
                canvas.strokeRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(220, 238, 255, 255));
            }
            if (index < kCharacterAssetListStart) {
                canvas.text(rowRect.x + 10.0f, rowRect.y + 7.0f, characterRowLabel(index), makeHudColor(240, 247, 255, 255));
                canvas.text(rowRect.x + 240.0f, rowRect.y + 7.0f, formatCharacterRowValue(index, characterVisual), makeHudColor(162, 230, 186, 255));
            } else {
                const AssetEntry& asset = assetCatalog[static_cast<std::size_t>(index - kCharacterAssetListStart)];
                const bool currentAsset = !characterVisual.sourcePath.empty() && characterVisual.sourcePath == asset.path;
                const HudColor rowColor = asset.supported ? makeHudColor(240, 247, 255, 255) : makeHudColor(255, 196, 124, 255);
                canvas.text(rowRect.x + 10.0f, rowRect.y + 7.0f, currentAsset ? (std::string("* ") + asset.label) : asset.label, rowColor);
            }
        }
        if (layout.previewW > 0.0f) {
            canvas.fillRect(layout.previewX, layout.previewY, layout.previewW, layout.previewH, makeHudColor(6, 12, 18, 210));
            canvas.strokeRect(layout.previewX, layout.previewY, layout.previewW, layout.previewH, makeHudColor(120, 166, 208, 255));
            canvas.text(layout.previewX + 10.0f, layout.previewY + 8.0f, "Preview", makeHudColor(240, 247, 255, 255));
            canvas.text(layout.previewX + 10.0f, layout.previewY + 24.0f, characterVisual.label, makeHudColor(180, 214, 240, 255));
            canvas.text(layout.previewX + 10.0f, layout.previewY + 42.0f, characterVisual.previewAutoSpin ? "Turntable: auto-spin" : "Turntable: manual", makeHudColor(180, 214, 240, 255));
            canvas.text(layout.previewX + 10.0f, layout.previewY + 58.0f, characterVisual.paintSupported ? "Paint: supported" : "Paint: textured UV model required", characterVisual.paintSupported ? makeHudColor(162, 230, 186, 255) : makeHudColor(255, 196, 124, 255));
        }
        canvas.text(contentX, layout.panelY + layout.panelH - 62.0f, characterRowHelpText(pauseState.selectedIndex), makeHudColor(205, 225, 242, 255));
    } else if (pauseState.tab == PauseTab::Paint) {
        canvas.text(contentX, contentY + 10.0f, "Edit RGBA paint overlays on top of the active role's base-color texture.", makeHudColor(180, 214, 240, 255));
        canvas.text(contentX, contentY + 24.0f, "Brush on the right canvas, then Commit to persist a PNG under the SDL pref path.", makeHudColor(180, 214, 240, 255));
        for (int index = 0; index < kPaintSettingCount; ++index) {
            const RectF rowRect = menuRowRect(layout, pauseState, controls, assetCatalog.size(), index);
            const bool selected = index == pauseState.selectedIndex;
            if (selected) {
                canvas.fillRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(46, 83, 124, 210));
                canvas.strokeRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(220, 238, 255, 255));
            }
            canvas.text(rowRect.x + 10.0f, rowRect.y + 7.0f, paintRowLabel(index), makeHudColor(240, 247, 255, 255));
            canvas.text(rowRect.x + 220.0f, rowRect.y + 7.0f, formatPaintRowValue(index, paintUi, paintVisual), makeHudColor(162, 230, 186, 255));
        }
        if (layout.previewW > 0.0f) {
            canvas.fillRect(layout.previewX, layout.previewY, layout.previewW, layout.previewH, makeHudColor(6, 12, 18, 210));
            canvas.strokeRect(layout.previewX, layout.previewY, layout.previewW, layout.previewH, makeHudColor(120, 166, 208, 255));
            canvas.text(layout.previewX + 10.0f, layout.previewY + 8.0f, "Paint Canvas", makeHudColor(240, 247, 255, 255));
            const RectF canvasRect = paintCanvasRect(layout);
            canvas.fillRect(canvasRect.x, canvasRect.y, canvasRect.w, canvasRect.h, makeHudColor(18, 26, 36, 255));
            canvas.strokeRect(canvasRect.x, canvasRect.y, canvasRect.w, canvasRect.h, makeHudColor(180, 214, 240, 255));
            if (const RgbaImage* baseImage = firstPaintBaseImage(paintVisual); baseImage != nullptr) {
                drawImagePreview(canvas, *baseImage, paintVisual.hasCommittedPaint ? &paintVisual.paintOverlay : nullptr, canvasRect);
            } else {
                canvas.text(canvasRect.x + 10.0f, canvasRect.y + 10.0f, "No textured base image", makeHudColor(255, 196, 124, 255));
            }
            for (int swatchIndex = 0; swatchIndex < 8; ++swatchIndex) {
                const float swatchX = layout.previewX + 10.0f + (static_cast<float>(swatchIndex % 4) * 26.0f);
                const float swatchY = canvasRect.y + canvasRect.h + 12.0f + (static_cast<float>(swatchIndex / 4) * 26.0f);
                const bool active = swatchIndex == paintUi.colorIndex;
                canvas.fillRect(swatchX, swatchY, 20.0f, 20.0f, makeHudColor(paintPresetColor(swatchIndex)));
                canvas.strokeRect(swatchX, swatchY, 20.0f, 20.0f, active ? makeHudColor(240, 247, 255, 255) : makeHudColor(84, 118, 152, 255));
            }
        }
        canvas.text(contentX, layout.panelY + layout.panelH - 62.0f, paintRowHelpText(pauseState.selectedIndex, paintVisual), makeHudColor(205, 225, 242, 255));
    } else if (pauseState.tab == PauseTab::Hud) {
        for (int index = 0; index < kHudSettingCount; ++index) {
            const RectF rowRect = menuRowRect(layout, pauseState, controls, assetCatalog.size(), index);
            const bool selected = index == pauseState.selectedIndex;
            if (selected) {
                canvas.fillRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(46, 83, 124, 210));
                canvas.strokeRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(220, 238, 255, 255));
            }
            canvas.text(rowRect.x + 10.0f, rowRect.y + 8.0f, hudRowLabel(index), makeHudColor(240, 247, 255, 255));
            canvas.text(rowRect.x + 290.0f, rowRect.y + 8.0f, formatHudRowValue(index, hudSettings), hudRowDisabled(index) ? makeHudColor(255, 196, 124, 255) : makeHudColor(162, 230, 186, 255));
        }
        canvas.text(contentX, layout.panelY + layout.panelH - 62.0f, hudRowHelpText2(pauseState.selectedIndex), makeHudColor(205, 225, 242, 255));
    } else if (pauseState.tab == PauseTab::Controls) {
        const int itemCount = static_cast<int>(controls.actions.size());
        const int startIndex = menuControlsVisibleStartIndex(pauseState.controlsSelection, itemCount);
        const int endIndex = std::min(itemCount, startIndex + kControlsVisibleRows);
        for (int index = startIndex; index < endIndex; ++index) {
            const RectF rowRect = menuRowRect(layout, pauseState, controls, assetCatalog.size(), index);
            const bool selected = index == pauseState.controlsSelection;
            if (selected) {
                canvas.fillRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(46, 83, 124, 210));
                canvas.strokeRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(220, 238, 255, 255));
            } else {
                canvas.fillRect(rowRect.x, rowRect.y, rowRect.w, rowRect.h, makeHudColor(15, 24, 34, 210));
            }

            const ControlActionBinding& action = controls.actions[static_cast<std::size_t>(index)];
            const HudColor labelColor = action.supported ? makeHudColor(240, 247, 255, 255) : makeHudColor(255, 196, 124, 255);
            canvas.text(rowRect.x + 10.0f, rowRect.y + 8.0f, action.label, labelColor);

            const int visibleIndex = index - startIndex;
            for (int slotIndex = 0; slotIndex < 2; ++slotIndex) {
                const RectF slotRect = menuControlSlotRect(layout, visibleIndex, slotIndex);
                const bool activeSlot = selected && pauseState.controlsSlot == slotIndex;
                const bool listening = pauseState.controlsCapturing && pauseState.controlsCaptureActionIndex == index && pauseState.controlsCaptureSlot == slotIndex;
                canvas.fillRect(slotRect.x, slotRect.y, slotRect.w, slotRect.h, listening ? makeHudColor(204, 146, 48, 255) : (activeSlot ? makeHudColor(52, 96, 148, 255) : makeHudColor(28, 38, 52, 255)));
                canvas.strokeRect(slotRect.x, slotRect.y, slotRect.w, slotRect.h, activeSlot ? makeHudColor(220, 238, 255, 255) : makeHudColor(96, 132, 164, 255));
                std::string bindingText = action.supported ? formatInputBinding(action.slots[static_cast<std::size_t>(slotIndex)]) : "Unavailable";
                if (listening) {
                    bindingText = "Listening...";
                }
                const float textX = slotRect.x + std::max(6.0f, (slotRect.w - static_cast<float>(bindingText.size() * 8u)) * 0.5f);
                canvas.text(textX, slotRect.y + 8.0f, bindingText, makeHudColor(240, 247, 255, 255));
            }
        }
        const std::string help = pauseState.controlsCapturing
            ? "Press a key, mouse button, mouse wheel, or move the mouse. Esc cancels capture."
            : (itemCount > 0 ? controls.actions[static_cast<std::size_t>(std::clamp(pauseState.controlsSelection, 0, itemCount - 1))].help : "");
        canvas.text(contentX, layout.panelY + layout.panelH - 62.0f, help, makeHudColor(205, 225, 242, 255));
    } else {
        const int maxScroll = maxMenuHelpScroll(layout);
        const int scrollStart = std::clamp(pauseState.helpScroll, 0, maxScroll);
        const float helpBottomY = layout.panelY + layout.panelH - 78.0f;
        const float visibleHeight = std::max(0.0f, helpBottomY - contentY);
        const int visibleLines = std::max(1, static_cast<int>(std::floor(visibleHeight / kHelpLineHeight)));
        const int lastLine = std::min(static_cast<int>(kMenuHelpLines.size()), scrollStart + visibleLines);
        float y = contentY;
        for (int lineIndex = scrollStart; lineIndex < lastLine; ++lineIndex) {
            const HelpLine& line = kMenuHelpLines[static_cast<std::size_t>(lineIndex)];
            const HudColor lineColor = line.title ? makeHudColor(236, 244, 255, 255) : makeHudColor(210, 228, 245, 255);
            canvas.text(contentX, y, line.text, lineColor);
            y += kHelpLineHeight;
        }
        if (maxScroll > 0) {
            canvas.text(
                contentX,
                layout.panelY + layout.panelH - 62.0f,
                std::string("Help lines ") + std::to_string(scrollStart + 1) + "-" + std::to_string(lastLine) + " / " + std::to_string(kMenuHelpLines.size()),
                makeHudColor(180, 214, 240, 255));
        }
    }

    const std::string statusText = !pauseState.statusText.empty() ? pauseState.statusText : pauseState.confirmText;
    if (!statusText.empty()) {
        canvas.text(layout.panelX + 24.0f, layout.panelY + layout.panelH - 44.0f, statusText, makeHudColor(255, 220, 168, 255));
    }

    const char* footer = pauseState.mode == MenuMode::MainMenu ? "Tab/H switch tabs | Enter activate | Esc quit" : "Tab/H switch tabs | Esc resume";
    if (pauseState.promptActive) {
        footer = "Type model path | Enter load | Ctrl+V paste | Esc cancel";
    } else if (pauseState.confirmPending && pauseState.tab == PauseTab::Main) {
        footer = "Enter or re-click confirm | Esc cancel";
    } else if (pauseState.tab == PauseTab::Settings) {
        footer = "Tab/H switch tabs | Q/E subtab | W/S select | A/D or Wheel adjust | Enter action | RMB reset row";
    } else if (pauseState.tab == PauseTab::Characters) {
        footer = "Tab/H switch tabs | Q/E role | W/S select | Drag or A/D adjust | Enter/LMB load | F5 refresh";
    } else if (pauseState.tab == PauseTab::Paint) {
        footer = "Tab/H switch tabs | Q/E role | W/S select | Paint on canvas | PgUp/PgDn brush size | Enter activates";
    } else if (pauseState.tab == PauseTab::Hud) {
        footer = "Tab/H switch tabs | W/S select | Drag or A/D adjust | LMB toggles | RMB reset row";
    } else if (pauseState.tab == PauseTab::Controls) {
        footer = "Tab/H switch tabs | W/S choose action | A/D choose slot | Enter capture | Backspace clear | R defaults";
    } else if (pauseState.tab == PauseTab::Help) {
        footer = "Tab/H switch tabs | Wheel or Up/Down scroll | Esc resume";
    }
    canvas.text(layout.panelX + 24.0f, layout.panelY + layout.panelH - 28.0f, footer, makeHudColor(180, 214, 240, 255));

    if (pauseState.promptActive) {
        const float promptW = clamp(layout.panelW * 0.72f, 340.0f, 760.0f);
        const float promptH = 112.0f;
        const float promptX = layout.panelX + (layout.panelW - promptW) * 0.5f;
        const float promptY = layout.panelY + (layout.panelH - promptH) * 0.5f;
        canvas.fillRect(promptX, promptY, promptW, promptH, makeHudColor(10, 16, 24, 245));
        canvas.strokeRect(promptX, promptY, promptW, promptH, makeHudColor(214, 234, 255, 255));
        canvas.text(
            promptX + 14.0f,
            promptY + 12.0f,
            pauseState.promptRole == CharacterSubTab::Plane ? "Load Plane Model" : "Load Walking Model",
            makeHudColor(240, 247, 255, 255));
        canvas.text(
            promptX + 14.0f,
            promptY + 28.0f,
            "Type a STL, GLB, or GLTF path. The prompt stays open if loading fails so you can correct it.",
            makeHudColor(180, 214, 240, 255));
        canvas.fillRect(promptX + 14.0f, promptY + 52.0f, promptW - 28.0f, 28.0f, makeHudColor(18, 28, 40, 255));
        canvas.strokeRect(promptX + 14.0f, promptY + 52.0f, promptW - 28.0f, 28.0f, makeHudColor(96, 132, 164, 255));
        std::string promptText = pauseState.promptText;
        const int cursor = std::clamp(pauseState.promptCursor, 0, static_cast<int>(promptText.size()));
        promptText.insert(static_cast<std::size_t>(cursor), "|");
        canvas.text(promptX + 20.0f, promptY + 60.0f, promptText, makeHudColor(240, 247, 255, 255));
        canvas.text(promptX + 14.0f, promptY + 88.0f, "Enter load | Ctrl+V paste | Backspace/Delete edit | Esc cancel", makeHudColor(180, 214, 240, 255));
    }
}

void drawSpeedometerGauge(HudCanvas& canvas, int width, int height, float speedKph, const HudSettings& hudSettings)
{
    if (!hudSettings.showSpeedometer) {
        return;
    }

    const float maxKph = static_cast<float>(std::max(200, hudSettings.speedometerMaxKph));
    const float redlineKph = static_cast<float>(std::clamp(hudSettings.speedometerRedlineKph, 50, hudSettings.speedometerMaxKph));
    const float clampedKph = clamp(speedKph, 0.0f, maxKph);
    const float dialSize = clamp(static_cast<float>(std::min(width, height)) * 0.22f, 130.0f, 220.0f);
    const float dialX = 24.0f + (dialSize * 0.5f);
    float dialY = static_cast<float>(height) - (dialSize * 0.5f) - 22.0f;
    if (dialY < (dialSize * 0.5f) + 14.0f) {
        dialY = (dialSize * 0.5f) + 14.0f;
    }

    const float radius = dialSize * 0.42f;
    const float startAngle = 2.35619449f;
    const float sweep = 4.71238898f;

    canvas.fillRect(dialX - dialSize * 0.5f, dialY - dialSize * 0.5f, dialSize, dialSize, makeHudColor(6, 10, 16, 186));
    canvas.strokeRect(dialX - dialSize * 0.5f, dialY - dialSize * 0.5f, dialSize, dialSize, makeHudColor(170, 210, 255, 228));

    Vec2 previousArc {};
    bool hasPreviousArc = false;
    for (int tick = 0; tick <= hudSettings.speedometerMaxKph; tick += std::max(5, hudSettings.speedometerMinorStepKph)) {
        const float t = clamp(static_cast<float>(tick) / maxKph, 0.0f, 1.0f);
        const float angle = startAngle + (t * sweep);
        const float cosA = std::cos(angle);
        const float sinA = std::sin(angle);
        const bool major = (tick % std::max(hudSettings.speedometerMajorStepKph, hudSettings.speedometerMinorStepKph)) == 0;
        const float outerR = radius;
        const float innerR = radius - (major ? 14.0f : 8.0f);
        const HudColor tickColor = tick >= hudSettings.speedometerRedlineKph ? makeHudColor(255, 118, 96, 255) : makeHudColor(200, 226, 255, 240);

        const Vec2 outer { dialX + (cosA * outerR), dialY + (sinA * outerR) };
        const Vec2 inner { dialX + (cosA * innerR), dialY + (sinA * innerR) };
        canvas.line(inner.x, inner.y, outer.x, outer.y, tickColor);
        if (hasPreviousArc) {
            canvas.line(previousArc.x, previousArc.y, outer.x, outer.y, makeHudColor(96, 132, 164, 220));
        }
        previousArc = outer;
        hasPreviousArc = true;

        if (major && (tick % std::max(hudSettings.speedometerLabelStepKph, hudSettings.speedometerMajorStepKph)) == 0) {
            const float labelR = radius - 30.0f;
            const std::string label = std::to_string(tick);
            canvas.text(
                dialX + (cosA * labelR) - (static_cast<float>(label.size()) * 4.0f),
                dialY + (sinA * labelR) - 4.0f,
                label,
                makeHudColor(220, 234, 255, 255));
        }
    }

    const float needleAngle = startAngle + (clampedKph / maxKph) * sweep;
    const float needleLen = radius - 18.0f;
    const HudColor needleColor = speedKph > redlineKph ? makeHudColor(255, 112, 90, 255) : makeHudColor(110, 228, 182, 255);
    canvas.line(dialX, dialY, dialX + (std::cos(needleAngle) * needleLen), dialY + (std::sin(needleAngle) * needleLen), needleColor);
    canvas.fillRect(dialX - 3.0f, dialY - 3.0f, 6.0f, 6.0f, makeHudColor(240, 248, 255, 255));

    const std::string speedText = std::to_string(static_cast<int>(std::round(speedKph))) + " kph";
    const std::string modeText = speedKph > redlineKph ? "OVERSPEED" : "IAS";
    const float textW = std::max(static_cast<float>(speedText.size()), static_cast<float>(modeText.size())) * 8.0f + 12.0f;
    canvas.fillRect(dialX - textW * 0.5f, dialY - 20.0f, textW, 42.0f, makeHudColor(8, 14, 22, 220));
    canvas.strokeRect(dialX - textW * 0.5f, dialY - 20.0f, textW, 42.0f, makeHudColor(96, 132, 164, 220));
    canvas.text(dialX - (static_cast<float>(speedText.size()) * 4.0f), dialY - 16.0f, speedText, makeHudColor(230, 240, 255, 255));
    canvas.text(dialX - (static_cast<float>(modeText.size()) * 4.0f), dialY - 1.0f, modeText, speedKph > redlineKph ? makeHudColor(255, 118, 96, 255) : makeHudColor(172, 208, 238, 255));
}

void drawHud(
    HudCanvas& canvas,
    int width,
    int height,
    const FlightState& plane,
    const FlightRuntimeState& runtime,
    const TerrainFieldContext& terrainContext,
    const GeoConfig& geoConfig,
    const WindState& windState,
    const UiState& uiState,
    const HudSettings& hudSettings,
    const std::string& modelLabel,
    const std::string& rendererLabel,
    float fps,
    bool mouseCaptured,
    bool flightMode)
{
    const float ground = sampleGroundHeight(plane.pos.x, plane.pos.z, terrainContext);
    const float altitudeAgl = plane.pos.y - ground;
    const float speedKph = (flightMode ? plane.debug.speed : length(plane.vel)) * 3.6f;
    const Vec2 latLon = worldToGeo(plane.pos.x, plane.pos.z, geoConfig);
    UiState hudUiState = uiState;
    syncUiStateFromHud(hudUiState, hudSettings);

    char latBuffer[64] {};
    char lonBuffer[64] {};
    std::snprintf(latBuffer, sizeof(latBuffer), "Lat %.6f", latLon.x);
    std::snprintf(lonBuffer, sizeof(lonBuffer), "Lon %.6f", latLon.y);

    std::vector<std::string> lines;
    lines.reserve(10);
    lines.push_back("TrueFlight");
    lines.push_back(rendererLabel);
    lines.push_back("Model: " + modelLabel);
    lines.push_back("Speed: " + std::to_string(static_cast<int>(std::round(speedKph))) + " kph");
    lines.push_back("Altitude AGL: " + std::to_string(static_cast<int>(std::round(altitudeAgl))) + " u");
    if (flightMode) {
        lines.push_back("Throttle: " + std::to_string(static_cast<int>(std::round(runtime.engineThrottle * 100.0f))) + "%");
        lines.push_back(formatManualTrimStatus(plane));
    } else {
        lines.push_back(std::string("Mode: walking  Grounded: ") + (plane.onGround ? "yes" : "no"));
        lines.push_back(std::string("Vertical Speed: ") + std::to_string(static_cast<int>(std::round(plane.vel.y * 3.6f))) + " kph");
    }
    lines.push_back(std::string("Wind: ") + std::to_string(static_cast<int>(std::round(windState.speed))) + " u/s @ " + std::to_string(static_cast<int>(std::round(windState.angle * 57.29578f))));
    if (hudSettings.showGeoInfo) {
        lines.push_back(latBuffer);
        lines.push_back(lonBuffer);
    }
    if (flightMode) {
        lines.push_back(runtime.crashed ? "Status: impact reset available (R)" : (std::string("Camera: ") + (uiState.chaseCamera ? "chase" : "cockpit")));
    } else {
        lines.push_back(std::string("Camera: ") + (uiState.chaseCamera ? "third person" : "first person"));
    }

    const float panelX = 14.0f;
    const float panelY = 14.0f;
    const float panelW = 422.0f;
    const float panelH = 16.0f + (static_cast<float>(lines.size()) * 14.0f) + ((flightMode && hudSettings.showThrottle) ? 26.0f : 10.0f);

    canvas.fillRect(panelX, panelY, panelW, panelH, makeHudColor(5, 10, 18, 178));
    canvas.strokeRect(panelX, panelY, panelW, panelH, makeHudColor(170, 210, 255, 255));

    float textY = panelY + 10.0f;
    for (const std::string& line : lines) {
        canvas.text(panelX + 10.0f, textY, line, makeHudColor(230, 240, 255, 255));
        textY += 14.0f;
    }

    if (flightMode && hudSettings.showThrottle) {
        const float barY = panelY + panelH - 18.0f;
        canvas.fillRect(panelX + 10.0f, barY, 220.0f, 10.0f, makeHudColor(16, 24, 34, 220));
        canvas.fillRect(panelX + 10.0f, barY, 220.0f * clamp(runtime.engineThrottle, 0.0f, 1.0f), 10.0f, makeHudColor(52, 214, 136, 255));
    }

    if (hudSettings.showDebug) {
        char debugBuffer[256] {};
        if (flightMode) {
            std::snprintf(
                debugBuffer,
                sizeof(debugBuffer),
                "FPS %.0f | AoA %.1f deg | Beta %.1f deg | qbar %.0f | Tick %d",
                fps,
                plane.debug.alpha * 57.29578f,
                plane.debug.beta * 57.29578f,
                plane.debug.qbar,
                plane.debug.tick);
        } else {
            const Vec3 walkForward = forwardFromRotation(plane.rot);
            std::snprintf(
                debugBuffer,
                sizeof(debugBuffer),
                "FPS %.0f | Walk Yaw %.1f deg | Walk Pitch %.1f deg | Tick %d",
                fps,
                degrees(getStableYawFromRotation(plane.rot)),
                degrees(std::asin(clamp(walkForward.y, -1.0f, 1.0f))),
                plane.debug.tick);
        }
        canvas.text(14.0f, static_cast<float>(height) - 64.0f, debugBuffer, makeHudColor(230, 240, 255, 255));
    }

    if (flightMode) {
        drawSpeedometerGauge(canvas, width, height, speedKph, hudSettings);
    }

    canvas.text(
        14.0f,
        static_cast<float>(height) - 48.0f,
        flightMode
            ? "Flight: Mouse/RS look  LS yoke  D-Pad rudder/trim  RT/LT throttle  A burner  B brakes"
            : "Walk: Mouse/RS look  LS move  A jump  C/X view  Y mode swap  R relaunch",
        makeHudColor(230, 240, 255, 255));
    canvas.text(
        14.0f,
        static_cast<float>(height) - 34.0f,
        flightMode
            ? "View: RMB/LB zoom  C/X camera  Y mode  RB shoot log  M map  F3 debug  Start/Esc pause"
            : "View: C/X camera  Y mode  M map  F3 debug  Start/Esc pause",
        makeHudColor(230, 240, 255, 255));
    canvas.text(
        static_cast<float>(width) - 130.0f,
        static_cast<float>(height) - 20.0f,
        mouseCaptured ? "Mouse locked" : "Cursor free",
        makeHudColor(180, 214, 240, 255));

    if (flightMode && hudSettings.showControls) {
        drawControlIndicator(canvas, width, height, plane);
    }
    drawMapPanel(canvas, width, height, plane, terrainContext, hudUiState);
    if (!uiState.chaseCamera && uiState.showCrosshair) {
        drawCrosshair(canvas, width, height);
    }
}

bool isControlActionHeld(
    const ControlProfile& controls,
    InputActionId actionId,
    const bool* keyboardState,
    SDL_MouseButtonFlags mouseButtons,
    SDL_Keymod modifiers)
{
    if (isControlActionDown(controls, actionId, keyboardState, modifiers)) {
        return true;
    }

    const ControlActionBinding* action = findControlAction(controls, actionId);
    if (action == nullptr || !action->supported) {
        return false;
    }

    for (const InputBinding& binding : action->slots) {
        if (binding.kind != BindingKind::MouseButton || !bindingModifiersMatch(binding.modifiers, modifiers)) {
            continue;
        }
        if ((mouseButtons & SDL_BUTTON_MASK(binding.mouseButton)) != 0u) {
            return true;
        }
    }
    return false;
}

bool saveBootPreferences(const BootResources& boot, std::string* errorText)
{
    return savePreferences(
        boot.preferencesPath,
        boot.uiState,
        boot.graphics,
        boot.lighting,
        boot.hud,
        boot.controls,
        boot.planeProfile,
        boot.terrainParams,
        boot.planeVisual,
        boot.walkingVisual,
        errorText);
}

void restoreVisualFromPreferences(
    PlaneVisualState& visual,
    const VisualPreferenceData& prefs,
    const std::filesystem::path& fallbackPath,
    const char* logLabel)
{
    if (prefs.hasStoredPath && !prefs.sourcePath.empty()) {
        std::string loadStatus;
        if (!loadPlaneModelFromPath(prefs.sourcePath, visual, &loadStatus)) {
            setBuiltinPlaneModel(visual);
            SDL_Log("Stored %s model load failed: %s", logLabel, loadStatus.c_str());
        }
    } else if (!fallbackPath.empty()) {
        std::string loadStatus;
        if (!loadPlaneModelFromPath(fallbackPath, visual, &loadStatus)) {
            visual.label = std::string("fallback cube (") + logLabel + " load failed)";
        }
    }

    applyVisualPreferenceData(visual, prefs);
    refreshVisualCompositeModel(visual);

    if (!visual.paintHash.empty()) {
        std::string paintError;
        if (!loadPaintOverlayByHash(visual.paintHash, visual, &paintError)) {
            storePaintHashForCurrentModel(visual, {});
            resetVisualPaint(visual);
            if (!paintError.empty()) {
                SDL_Log("Stored %s paint load failed: %s", logLabel, paintError.c_str());
            }
        }
    }
}

void shutdownGameSession(GameSession& session)
{
    session.audioState.shutdown();
    if (session.worldStore.has_value()) {
        session.worldStore->flushDirty(nullptr);
    }
}

bool startGameSession(
    GameSession& session,
    BootResources& boot,
    bool audioSubsystemReady,
    SDL_Window* window,
    VulkanRenderer& renderer,
    const std::string& rendererLabel,
    std::string* errorText)
{
    session = {};
    beginLoadingUi(boot.loadingUi, "Loading World", static_cast<float>(SDL_GetTicks()) * 0.001f);
    const std::string terrainWorldName = "native_default";

    const auto stage = [&](const std::string& stageLabel, float progress, const std::string& detail) -> bool {
        updateLoadingUi(boot.loadingUi, stageLabel, progress, detail);
        SDL_PumpEvents();
        std::string renderError;
        if (!presentLoadingUi(window, renderer, boot.hudCanvas, boot.loadingUi, rendererLabel, boot.lighting, &renderError)) {
            if (errorText != nullptr) {
                *errorText = renderError;
            }
            return false;
        }
        return true;
    };

    if (!stage("Opening World", 0.12f, "Opening world store metadata.")) {
        return false;
    }

    {
        WorldStoreOptions worldOptions;
        worldOptions.name = terrainWorldName;
        worldOptions.storageRoot = getWorldStorageDirectory();
        worldOptions.createIfMissing = true;
        worldOptions.regionSize = 8;
        worldOptions.chunkResolution = 16;
        worldOptions.groundParams = boot.terrainParams;

        std::string worldError;
        if (auto openedWorld = WorldStore::open(worldOptions, &worldError); openedWorld.has_value()) {
            session.worldStore = std::move(*openedWorld);
            const WorldInfoSnapshot worldInfo = buildWorldInfoSnapshot(boot.terrainParams, terrainWorldName);
            if (!session.worldStore->applyWorldInfo(worldInfo, &worldError) && !worldError.empty()) {
                SDL_Log("Native world metadata update failed: %s", worldError.c_str());
            }
            boot.terrainParams = session.worldStore->buildGroundParams(boot.terrainParams).terrainParams;
            session.terrainWorldId = session.worldStore->getMeta().worldId;
        } else if (!worldError.empty()) {
            SDL_Log("Native world store unavailable: %s", worldError.c_str());
            session.terrainWorldId = terrainWorldName;
        }
    }

    if (!stage("Opening Terrain Cache", 0.28f, "Preparing baked terrain-cache storage.")) {
        return false;
    }

    {
        std::string cacheError;
        session.terrainChunkBakeCache = TerrainChunkBakeCache::open(getTerrainChunkCacheDirectory(), &cacheError);
        if (!session.terrainChunkBakeCache.has_value() && !cacheError.empty()) {
            SDL_Log("Native terrain chunk cache unavailable: %s", cacheError.c_str());
        }
    }

    if (!stage("Building Terrain", 0.48f, "Refreshing terrain field context.")) {
        return false;
    }

    WorldStore* worldStorePtr = session.worldStore.has_value() ? &*session.worldStore : nullptr;
    refreshTerrainContext(boot.terrainParams, session.terrainContext, session.terrainCache, worldStorePtr);

    if (!stage("Clouds and Flight", 0.66f, "Initializing wind, clouds, and aircraft state.")) {
        return false;
    }

    session.worldRng.seed(static_cast<std::uint32_t>(boot.terrainParams.seed));
    session.windState.speed = 0.0f;
    session.windState.targetSpeed = 0.0f;
    session.windState.gustAmplitude = 0.0f;
    session.windState.gustFrequency = 0.0f;
    session.windState.nextTargetAt = std::numeric_limits<float>::infinity();
    initializeCloudField(session.cloudField, session.worldRng, { 0.0f, 0.0f, 0.0f });
    resetFlight(session.plane, session.runtime, session.terrainContext);
    session.flightMode = true;
    syncWalkingLookFromRotation(session.plane.rot, session.walkYaw, session.walkPitch);

    if (!stage("Starting Audio", 0.82f, "Starting procedural audio synth.")) {
        return false;
    }

    if (audioSubsystemReady) {
        std::string audioError;
        if (!session.audioState.initialize(22050, 1024, 8, &audioError) && !audioError.empty()) {
            SDL_Log("Native audio disabled: %s", audioError.c_str());
        }
    }

    if (!stage("Prewarming Terrain", 0.94f, "Generating near-field terrain visuals.")) {
        return false;
    }

    (void)ensureTerrainVisualCache(
        session.terrainCache,
        session.plane.pos,
        session.plane.flightVel,
        session.terrainContext,
        session.terrainChunkBakeCache,
        session.terrainWorldId);

    finishLoadingUi(boot.loadingUi, static_cast<float>(SDL_GetTicks()) * 0.001f);
    return true;
}

}  // namespace

#if 0
int main(int, char**)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    const bool audioSubsystemReady = SDL_InitSubSystem(SDL_INIT_AUDIO);
    if (!audioSubsystemReady) {
        SDL_Log("SDL audio init failed: %s", SDL_GetError());
    }
    const bool gamepadSubsystemReady = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    if (!gamepadSubsystemReady) {
        SDL_Log("SDL gamepad init failed: %s", SDL_GetError());
    }

    SDL_Window* window = SDL_CreateWindow("TrueFlight", 1280, 720, SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    const auto shaderDirectory = findAssetPath("Shaders");
    if (shaderDirectory.empty()) {
        SDL_Log("Unable to locate compiled shaders directory.");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    VulkanRenderer nativeRenderer;
    std::string rendererError;
    if (!nativeRenderer.initialize(window, shaderDirectory, &rendererError)) {
        SDL_Log("Vulkan renderer initialization failed: %s", rendererError.c_str());
        nativeRenderer.shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    const std::string rendererLabel = std::string("Renderer: ") + (nativeRenderer.backendName() != nullptr ? nativeRenderer.backendName() : "unknown");

    const Vec3 skyColor { 0.64f, 0.73f, 0.84f };
    const float sunYaw = radians(20.0f);
    const float sunPitch = radians(50.0f);
    const Vec3 sunDirection = normalize(Vec3 {
        std::cos(sunPitch) * std::sin(sunYaw),
        std::sin(sunPitch),
        std::cos(sunPitch) * std::cos(sunYaw)
    });

    TerrainParams terrainParams = defaultTerrainParams();
    const TerrainParams defaultTerrainParamsValues = defaultTerrainParams();
    GeoConfig geoConfig {};
    const UiState defaultUiStateValues = defaultUiState();
    UiState uiState = defaultUiStateValues;
    PauseState pauseState {};

    AircraftProfile planeProfile {};
    planeProfile.visualPrefs.scale = 3.0f;
    FlightConfig& config = planeProfile.flightConfig;
    PropAudioConfig& propAudioConfig = planeProfile.propAudioConfig;
    VisualPreferenceData& planePrefs = planeProfile.visualPrefs;
    const FlightConfig defaultConfig = defaultFlightConfig();
    const PropAudioConfig defaultPropAudioConfigValues = defaultPropAudioConfig();
    const std::filesystem::path preferencesPath = getPreferenceFilePath();
    VisualPreferenceData walkingPrefs {};
    walkingPrefs.scale = 1.0f;
    std::string preferenceError;
    if (!loadPreferences(preferencesPath, uiState, planeProfile, terrainParams, walkingPrefs, &preferenceError) && !preferenceError.empty()) {
        SDL_Log("Native preference load failed: %s", preferenceError.c_str());
    }
    std::optional<WorldStore> worldStore;
    const std::string terrainWorldName = "native_default";
    {
        WorldStoreOptions worldOptions;
        worldOptions.name = terrainWorldName;
        worldOptions.storageRoot = getWorldStorageDirectory();
        worldOptions.createIfMissing = true;
        worldOptions.regionSize = 8;
        worldOptions.chunkResolution = 16;
        worldOptions.groundParams = terrainParams;

        std::string worldError;
        if (auto openedWorld = WorldStore::open(worldOptions, &worldError); openedWorld.has_value()) {
            worldStore = std::move(*openedWorld);
            const WorldInfoSnapshot worldInfo = buildWorldInfoSnapshot(terrainParams, terrainWorldName);
            if (!worldStore->applyWorldInfo(worldInfo, &worldError) && !worldError.empty()) {
                SDL_Log("Native world metadata update failed: %s", worldError.c_str());
            }
            terrainParams = worldStore->buildGroundParams(terrainParams).terrainParams;
        } else if (!worldError.empty()) {
            SDL_Log("Native world store unavailable: %s", worldError.c_str());
        }
    }

    WorldStore* worldStorePtr = worldStore.has_value() ? &*worldStore : nullptr;
    const std::string terrainWorldId = worldStore.has_value() ? worldStore->getMeta().worldId : terrainWorldName;
    std::optional<TerrainChunkBakeCache> terrainChunkBakeCache;
    {
        std::string cacheError;
        terrainChunkBakeCache = TerrainChunkBakeCache::open(getTerrainChunkCacheDirectory(), &cacheError);
        if (!terrainChunkBakeCache.has_value() && !cacheError.empty()) {
            SDL_Log("Native terrain chunk cache unavailable: %s", cacheError.c_str());
        }
    }

    TerrainVisualCache terrainCache {};
    TerrainFieldContext terrainContext {};
    refreshTerrainContext(terrainParams, terrainContext, terrainCache, worldStorePtr);

    std::mt19937 worldRng(static_cast<std::uint32_t>(terrainParams.seed));
    WindState windState {};
    windState.speed = 0.0f;
    windState.targetSpeed = 0.0f;
    windState.gustAmplitude = 0.0f;
    windState.gustFrequency = 0.0f;
    windState.nextTargetAt = std::numeric_limits<float>::infinity();
    CloudField cloudField {};
    initializeCloudField(cloudField, worldRng, { 0.0f, 0.0f, 0.0f });

    PlaneVisualState planeVisual;
    planeVisual.defaultScale = 3.0f;
    setBuiltinPlaneModel(planeVisual);
    PlaneVisualState walkingVisual;
    walkingVisual.defaultScale = 1.0f;
    setBuiltinPlaneModel(walkingVisual);
    walkingVisual.label = "builtin player cube";
    PaintUiState paintUi {};

    const auto restoreVisual = [&](PlaneVisualState& visual, const VisualPreferenceData& prefs, const std::filesystem::path& fallbackPath, const char* logLabel) {
        restoreVisualFromPreferences(visual, prefs, fallbackPath, logLabel);
    };

    std::filesystem::path planeFallbackPath {};
    if (const auto glbPath = findAssetPath("portSource/Assets/Models/DualEngine.glb"); !glbPath.empty()) {
        planeFallbackPath = glbPath;
    } else if (const auto stlPath = findAssetPath("portSource/Assets/Models/DualEngine.stl"); !stlPath.empty()) {
        planeFallbackPath = stlPath;
    }
    restoreVisual(planeVisual, planePrefs, planeFallbackPath, "plane");
    restoreVisual(walkingVisual, walkingPrefs, {}, "walking");
    const Model cloudModel = makeOctahedronModel();
    std::vector<AssetEntry> assetCatalog = scanModelAssets();

    FlightState plane;
    FlightRuntimeState runtime;
    resetFlight(plane, runtime, terrainContext);

    ProceduralAudioState audioState {};
    if (audioSubsystemReady) {
        std::string audioError;
        if (!audioState.initialize(22050, 1024, 8, &audioError) && !audioError.empty()) {
            SDL_Log("Native audio disabled: %s", audioError.c_str());
        }
    }

    HudCanvas hudCanvas(1280, 720);

    bool running = true;
    bool windowHasFocus = true;
    bool mouseCaptured = false;
    bool preferencesDirty = false;
    float preferencesNextSaveAt = 0.0f;
    setMouseCapture(window, true);
    mouseCaptured = true;
    float fpsSmoothed = 120.0f;
    float worldTime = 0.0f;
    std::uint64_t previousCounter = SDL_GetPerformanceCounter();
    const double counterFrequency = static_cast<double>(SDL_GetPerformanceFrequency());

    while (running) {
        const float uiNowSeconds = static_cast<float>(SDL_GetTicks()) * 0.001f;
        float frameMouseDx = 0.0f;
        float frameMouseDy = 0.0f;
        int throttleWheelDelta = 0;
        int trimWheelDelta = 0;

        SDL_Event event {};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                windowHasFocus = false;
                uiState.zoomHeld = false;
                uiState.mapHeld = false;
                uiState.mapUsedForZoom = false;
            } else if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
                windowHasFocus = true;
            } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
                if (pauseState.active) {
                    int pauseWidth = 0;
                    int pauseHeight = 0;
                    SDL_GetWindowSizeInPixels(window, &pauseWidth, &pauseHeight);
                    handlePauseMouseMove(
                        pauseState,
                        paintUi,
                        planeVisual,
                        walkingVisual,
                        pauseWidth,
                        pauseHeight,
                        assetCatalog.size(),
                        static_cast<float>(event.motion.x),
                        static_cast<float>(event.motion.y));
                } else if (mouseCaptured) {
                    frameMouseDx += static_cast<float>(event.motion.xrel);
                    frameMouseDy += static_cast<float>(event.motion.yrel);
                }
            } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                if (pauseState.active) {
                    handlePauseMouseWheel(
                        pauseState,
                        uiState,
                        propAudioConfig,
                        plane,
                        config,
                        terrainParams,
                        terrainContext,
                        terrainCache,
                        planeVisual,
                        walkingVisual,
                        paintUi,
                        preferencesDirty,
                        preferencesNextSaveAt,
                        worldStorePtr,
                        static_cast<int>(event.wheel.y),
                        assetCatalog.size(),
                        uiNowSeconds);
                } else {
                    const bool trimModifier = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
                    if (trimModifier) {
                        trimWheelDelta += static_cast<int>(event.wheel.y);
                    } else {
                        throttleWheelDelta += static_cast<int>(event.wheel.y);
                    }
                }
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (pauseState.active) {
                    int pauseWidth = 0;
                    int pauseHeight = 0;
                    SDL_GetWindowSizeInPixels(window, &pauseWidth, &pauseHeight);
                    handlePauseMouseButtonDown(
                        pauseState,
                        uiState,
                        propAudioConfig,
                        plane,
                        runtime,
                        config,
                        terrainParams,
                        terrainContext,
                        terrainCache,
                        defaultUiStateValues,
                        defaultPropAudioConfigValues,
                        defaultConfig,
                        defaultTerrainParamsValues,
                        assetCatalog,
                        planeVisual,
                        walkingVisual,
                        paintUi,
                        running,
                        preferencesDirty,
                        preferencesNextSaveAt,
                        worldStorePtr,
                        pauseWidth,
                        pauseHeight,
                        static_cast<float>(event.button.x),
                        static_cast<float>(event.button.y),
                        event.button.button,
                        uiNowSeconds);
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    uiState.zoomHeld = true;
                }
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                if (pauseState.active && event.button.button == SDL_BUTTON_LEFT) {
                    paintUi.draggingCanvas = false;
                }
                if (event.button.button == SDL_BUTTON_RIGHT) {
                    uiState.zoomHeld = false;
                }
            } else if (event.type == SDL_EVENT_DROP_FILE) {
                std::string loadStatus;
                const std::filesystem::path droppedPath = event.drop.data != nullptr ? std::filesystem::path(event.drop.data) : std::filesystem::path {};
                CharacterSubTab dropRole = CharacterSubTab::Plane;
                if (pauseState.active && (pauseState.tab == PauseTab::Characters || pauseState.tab == PauseTab::Paint)) {
                    dropRole = activeRoleForTab(pauseState, pauseState.tab);
                }
                PlaneVisualState& dropVisual = visualForRole(dropRole, planeVisual, walkingVisual);
                const bool loaded = !droppedPath.empty() && loadPlaneModelFromPath(droppedPath, dropVisual, &loadStatus);
                if (loadStatus.empty()) {
                    loadStatus = "Dropped file could not be processed.";
                }
                setPauseActive(pauseState, uiState, true);
                pauseState.tab = PauseTab::Characters;
                pauseState.charactersSubTab = dropRole;
                pauseState.selectedIndex = 0;
                setPauseStatus(pauseState, loadStatus, uiNowSeconds, loaded ? 3.5f : 4.5f);
                SDL_Log("%s", loadStatus.c_str());
                assetCatalog = scanModelAssets();
                if (loaded) {
                    schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, uiNowSeconds);
                }
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                const SDL_Scancode scancode = event.key.scancode;
                if (scancode == SDL_SCANCODE_ESCAPE) {
                    if (!pauseState.active) {
                        assetCatalog = scanModelAssets();
                    }
                    setPauseActive(pauseState, uiState, !pauseState.active);
                    continue;
                }

                if (pauseState.active) {
                    PlaneVisualState& selectedCharacterVisual = visualForRole(pauseState.charactersSubTab, planeVisual, walkingVisual);
                    PlaneVisualState& selectedPaintVisual = visualForRole(pauseState.paintSubTab, planeVisual, walkingVisual);
                    const auto adjustPausedSelection = [&](int direction) {
                        if (pauseState.tab == PauseTab::Settings) {
                            if (pauseState.settingsSubTab == SettingsSubTab::Flight) {
                                adjustTuningValue(config, pauseState.selectedIndex, direction);
                            } else if (pauseState.settingsSubTab == SettingsSubTab::Terrain) {
                                adjustTerrainValue(terrainParams, pauseState.selectedIndex, direction);
                                applyTerrainRuntimeChange(terrainParams, terrainContext, terrainCache, plane, worldStorePtr);
                            } else {
                                adjustSettingsRowValue(uiState, propAudioConfig, pauseState.settingsSubTab, pauseState.selectedIndex, direction);
                            }
                            schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, uiNowSeconds);
                        } else if (pauseState.tab == PauseTab::Hud) {
                            adjustHudValue(uiState, pauseState.selectedIndex, direction);
                            schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, uiNowSeconds);
                        } else if (pauseState.tab == PauseTab::Characters) {
                            if (pauseState.selectedIndex >= 1 && pauseState.selectedIndex <= 10) {
                                adjustCharacterRowValue(selectedCharacterVisual, pauseState.selectedIndex, direction);
                                schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, uiNowSeconds);
                            }
                        } else if (pauseState.tab == PauseTab::Paint) {
                            if (pauseState.selectedIndex >= 0 && pauseState.selectedIndex <= 4) {
                                adjustPaintRowValue(paintUi, pauseState.selectedIndex, direction);
                            }
                        }
                    };

                    const auto resetSelectedPauseRow = [&]() {
                        if (pauseState.tab == PauseTab::Settings) {
                            if (pauseState.settingsSubTab == SettingsSubTab::Flight) {
                                resetTuningValue(config, defaultConfig, pauseState.selectedIndex);
                                setPauseStatus(pauseState, "Reset selected flight value to default.", uiNowSeconds, 2.2f);
                            } else if (pauseState.settingsSubTab == SettingsSubTab::Terrain) {
                                resetTerrainValue(terrainParams, defaultTerrainParamsValues, pauseState.selectedIndex);
                                applyTerrainRuntimeChange(terrainParams, terrainContext, terrainCache, plane, worldStorePtr);
                                setPauseStatus(pauseState, "Reset selected terrain value to default.", uiNowSeconds, 2.2f);
                            } else {
                                resetSettingsRowValue(uiState, defaultUiStateValues, propAudioConfig, defaultPropAudioConfigValues, pauseState.settingsSubTab, pauseState.selectedIndex);
                                setPauseStatus(pauseState, "Reset selected setting to default.", uiNowSeconds, 2.2f);
                            }
                            schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, uiNowSeconds);
                        } else if (pauseState.tab == PauseTab::Hud) {
                            resetHudValue(uiState, defaultUiStateValues, pauseState.selectedIndex);
                            setPauseStatus(pauseState, "Reset selected HUD setting to default.", uiNowSeconds, 2.2f);
                            schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, uiNowSeconds);
                        } else if (pauseState.tab == PauseTab::Characters) {
                            resetCharacterRowValue(selectedCharacterVisual, pauseState.selectedIndex);
                            setPauseStatus(pauseState, "Reset selected character transform value.", uiNowSeconds, 2.2f);
                            schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, uiNowSeconds);
                        } else if (pauseState.tab == PauseTab::Paint) {
                            resetPaintRowValue(paintUi, pauseState.selectedIndex);
                            setPauseStatus(pauseState, "Reset selected paint control.", uiNowSeconds, 2.2f);
                        }
                    };

                    const auto activatePaintAction = [&]() {
                        switch (pauseState.selectedIndex) {
                        case 5:
                            if (paintUndo(selectedPaintVisual)) {
                                setPauseStatus(pauseState, "Undid last paint step.", uiNowSeconds, 2.2f);
                            }
                            break;
                        case 6:
                            if (paintRedo(selectedPaintVisual)) {
                                setPauseStatus(pauseState, "Redid paint step.", uiNowSeconds, 2.2f);
                            }
                            break;
                        case 7:
                            if (fillPaintOverlay(selectedPaintVisual, paintUi.colorIndex)) {
                                setPauseStatus(pauseState, "Filled active paint overlay.", uiNowSeconds, 2.2f);
                            }
                            break;
                        case 8:
                            if (clearPaintOverlay(selectedPaintVisual)) {
                                setPauseStatus(pauseState, "Cleared active paint overlay.", uiNowSeconds, 2.2f);
                            }
                            break;
                        case 9: {
                            std::string paintHash;
                            std::string paintError;
                            if (commitPaintOverlay(getPaintStorageDirectory(), selectedPaintVisual, &paintHash, &paintError)) {
                                setPauseStatus(pauseState, "Committed paint overlay " + paintHash.substr(0, std::min<std::size_t>(8, paintHash.size())) + ".", uiNowSeconds, 2.6f);
                                schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, uiNowSeconds);
                            } else if (!paintError.empty()) {
                                setPauseStatus(pauseState, paintError, uiNowSeconds, 3.6f);
                            }
                            break;
                        }
                        case 10: {
                            if (selectedPaintVisual.paintHash.empty()) {
                                setPauseStatus(pauseState, "No saved paint overlay is stored for this role.", uiNowSeconds, 3.0f);
                                break;
                            }
                            std::string paintError;
                            if (loadPaintOverlayByHash(selectedPaintVisual.paintHash, selectedPaintVisual, &paintError)) {
                                setPauseStatus(pauseState, "Reloaded committed paint overlay.", uiNowSeconds, 2.4f);
                            } else if (!paintError.empty()) {
                                setPauseStatus(pauseState, paintError, uiNowSeconds, 3.6f);
                            }
                            break;
                        }
                        default:
                            break;
                        }
                    };

                    if (scancode == SDL_SCANCODE_TAB || scancode == SDL_SCANCODE_H) {
                        cyclePauseTab(pauseState, 1);
                    } else if ((scancode == SDL_SCANCODE_Q || scancode == SDL_SCANCODE_LEFTBRACKET) &&
                        (pauseState.tab == PauseTab::Settings || pauseState.tab == PauseTab::Characters || pauseState.tab == PauseTab::Paint)) {
                        cyclePauseSubTab(pauseState, -1);
                    } else if ((scancode == SDL_SCANCODE_E || scancode == SDL_SCANCODE_RIGHTBRACKET) &&
                        (pauseState.tab == PauseTab::Settings || pauseState.tab == PauseTab::Characters || pauseState.tab == PauseTab::Paint)) {
                        cyclePauseSubTab(pauseState, 1);
                    } else if (scancode == SDL_SCANCODE_W || scancode == SDL_SCANCODE_UP) {
                        movePauseSelection(pauseState, -1, assetCatalog.size());
                    } else if (scancode == SDL_SCANCODE_S || scancode == SDL_SCANCODE_DOWN) {
                        movePauseSelection(pauseState, 1, assetCatalog.size());
                    } else if (scancode == SDL_SCANCODE_A || scancode == SDL_SCANCODE_LEFT) {
                        adjustPausedSelection(-1);
                    } else if (scancode == SDL_SCANCODE_D || scancode == SDL_SCANCODE_RIGHT) {
                        adjustPausedSelection(1);
                    } else if (scancode == SDL_SCANCODE_BACKSPACE && pauseState.tab == PauseTab::Settings) {
                        if (pauseState.settingsSubTab == SettingsSubTab::Flight) {
                            config = defaultConfig;
                            setPauseStatus(pauseState, "Restored native flight-model defaults.", uiNowSeconds);
                        } else if (pauseState.settingsSubTab == SettingsSubTab::Terrain) {
                            terrainParams = defaultTerrainParamsValues;
                            applyTerrainRuntimeChange(terrainParams, terrainContext, terrainCache, plane, worldStorePtr);
                            setPauseStatus(pauseState, "Restored native terrain defaults.", uiNowSeconds);
                        } else {
                            for (int index = 0; index < pauseItemCount(pauseState, 0); ++index) {
                                resetSettingsRowValue(uiState, defaultUiStateValues, propAudioConfig, defaultPropAudioConfigValues, pauseState.settingsSubTab, index);
                            }
                            setPauseStatus(pauseState, "Restored native settings defaults.", uiNowSeconds);
                        }
                        schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, uiNowSeconds);
                    } else if (scancode == SDL_SCANCODE_BACKSPACE && pauseState.tab == PauseTab::Hud) {
                        for (int index = 0; index < pauseItemCount(pauseState, 0); ++index) {
                            resetHudValue(uiState, defaultUiStateValues, index);
                        }
                        setPauseStatus(pauseState, "Restored native HUD defaults.", uiNowSeconds);
                        schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, uiNowSeconds);
                    } else if (scancode == SDL_SCANCODE_BACKSPACE && pauseState.tab == PauseTab::Characters) {
                        setBuiltinPlaneModel(selectedCharacterVisual);
                        setPauseStatus(pauseState, "Restored selected role to builtin defaults.", uiNowSeconds);
                        schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, uiNowSeconds);
                    } else if (scancode == SDL_SCANCODE_BACKSPACE && pauseState.tab == PauseTab::Paint) {
                        if (clearPaintOverlay(selectedPaintVisual)) {
                            setPauseStatus(pauseState, "Cleared active paint overlay.", uiNowSeconds);
                        }
                    } else if (scancode == SDL_SCANCODE_F5 && pauseState.tab == PauseTab::Characters) {
                        assetCatalog = scanModelAssets();
                        pauseState.selectedIndex = std::clamp(pauseState.selectedIndex, 0, std::max(0, pauseItemCount(pauseState, assetCatalog.size()) - 1));
                        setPauseStatus(pauseState, "Refreshed portSource/Assets/Models catalog.", uiNowSeconds, 2.2f);
                    } else if (scancode == SDL_SCANCODE_RETURN || scancode == SDL_SCANCODE_SPACE) {
                        if (pauseState.tab == PauseTab::Main) {
                            const int activatedIndex = pauseState.selectedIndex;
                            activatePauseSelection(pauseState, uiState, plane, runtime, terrainContext, running);
                            if (activatedIndex >= 2 && activatedIndex <= 4) {
                                schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, uiNowSeconds);
                            }
                        } else if (pauseState.tab == PauseTab::Settings) {
                            resetSelectedPauseRow();
                        } else if (pauseState.tab == PauseTab::Characters) {
                            if (pauseState.selectedIndex >= 1 && pauseState.selectedIndex <= 10) {
                                resetSelectedPauseRow();
                            } else if (pauseState.selectedIndex == 11) {
                                setBuiltinPlaneModel(selectedCharacterVisual);
                                setPauseStatus(pauseState, "Loaded builtin cube.", uiNowSeconds, 2.2f);
                                schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, uiNowSeconds);
                            } else if (pauseState.selectedIndex >= kCharacterAssetListStart &&
                                pauseState.selectedIndex < characterItemCount(assetCatalog.size())) {
                                const AssetEntry& asset = assetCatalog[static_cast<std::size_t>(pauseState.selectedIndex - kCharacterAssetListStart)];
                                std::string loadStatus;
                                loadPlaneModelFromPath(asset.path, selectedCharacterVisual, &loadStatus);
                                setPauseStatus(pauseState, loadStatus, uiNowSeconds, asset.supported ? 3.0f : 4.5f);
                                if (asset.supported) {
                                    schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, uiNowSeconds);
                                }
                            }
                        } else if (pauseState.tab == PauseTab::Paint) {
                            if (pauseState.selectedIndex <= 4) {
                                resetSelectedPauseRow();
                            } else {
                                activatePaintAction();
                            }
                        } else if (pauseState.tab == PauseTab::Hud) {
                            resetSelectedPauseRow();
                        }
                    }
                    continue;
                }

                if (scancode == SDL_SCANCODE_C) {
                    uiState.chaseCamera = !uiState.chaseCamera;
                    schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, uiNowSeconds);
                } else if (scancode == SDL_SCANCODE_G) {
                    if (applyLocalizedTerrainCrater(terrainParams, terrainContext, terrainCache, plane, worldStorePtr, 10.0f, 4.0f, 0.15f)) {
                        SDL_Log("Applied localized terrain crater at plane position");
                    }
                } else if (scancode == SDL_SCANCODE_M) {
                    uiState.mapHeld = true;
                    uiState.mapUsedForZoom = false;
                } else if (scancode == SDL_SCANCODE_UP && uiState.mapHeld) {
                    changeMapZoom(uiState, -1);
                    uiState.mapUsedForZoom = true;
                    schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, uiNowSeconds);
                } else if (scancode == SDL_SCANCODE_DOWN && uiState.mapHeld) {
                    changeMapZoom(uiState, 1);
                    uiState.mapUsedForZoom = true;
                    schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, uiNowSeconds);
                } else if (scancode == SDL_SCANCODE_F3) {
                    uiState.showDebug = !uiState.showDebug;
                    schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, uiNowSeconds);
                } else if (scancode == SDL_SCANCODE_R) {
                    resetFlight(plane, runtime, terrainContext, plane.pos.x, plane.pos.z);
                }
            } else if (event.type == SDL_EVENT_KEY_UP && !event.key.repeat) {
                if (event.key.scancode == SDL_SCANCODE_M) {
                    const bool shouldToggle = uiState.mapHeld && !uiState.mapUsedForZoom && !pauseState.active;
                    uiState.mapHeld = false;
                    uiState.mapUsedForZoom = false;
                    if (shouldToggle) {
                        uiState.showMap = !uiState.showMap;
                        schedulePreferencesSave(preferencesDirty, preferencesNextSaveAt, uiNowSeconds);
                    }
                }
            }
        }

        const std::uint64_t currentCounter = SDL_GetPerformanceCounter();
        const double dtSeconds = static_cast<double>(currentCounter - previousCounter) / counterFrequency;
        previousCounter = currentCounter;
        const float dt = clamp(static_cast<float>(dtSeconds), 1.0f / 240.0f, 0.05f);
        const float nowSeconds = worldTime + dt;
        fpsSmoothed = mix(fpsSmoothed, 1.0f / std::max(dt, 1.0e-4f), 0.08f);
        refreshPauseStatus(pauseState, uiNowSeconds);

        const bool* keys = SDL_GetKeyboardState(nullptr);
        const bool altHeld = keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT];
        const bool wantMouseCapture = windowHasFocus && !pauseState.active && !altHeld;
        if (wantMouseCapture != mouseCaptured) {
            setMouseCapture(window, wantMouseCapture);
            mouseCaptured = wantMouseCapture;
        }

        if (!pauseState.active) {
            if (mouseCaptured) {
                applyFlightMouseInput(uiState, plane, frameMouseDx, frameMouseDy, nowSeconds);
            }
            adjustElevatorTrimFromWheel(config, plane, trimWheelDelta);
            if (!uiState.mapHeld) {
                adjustThrottleFromWheel(plane, throttleWheelDelta);
            }
        }

        InputState input {};
        if (!pauseState.active) {
            const bool throttleBlocked = uiState.mapHeld;
            input.flightPitchDown = keys[SDL_SCANCODE_W];
            input.flightPitchUp = keys[SDL_SCANCODE_S];
            input.flightRollLeft = keys[SDL_SCANCODE_A];
            input.flightRollRight = keys[SDL_SCANCODE_D];
            input.flightYawLeft = keys[SDL_SCANCODE_Q];
            input.flightYawRight = keys[SDL_SCANCODE_E];
            input.flightThrottleUp = !throttleBlocked && keys[SDL_SCANCODE_UP];
            input.flightThrottleDown = !throttleBlocked && keys[SDL_SCANCODE_DOWN];
            input.flightAirBrakes = keys[SDL_SCANCODE_SPACE];
        }

        FlightEnvironment environment {};
        environment.wind = getWindVector3(windState);
        environment.groundHeightAt = [&terrainContext](float x, float z) {
            return sampleGroundHeight(x, z, terrainContext);
        };
        environment.sampleSdf = [&terrainContext](float x, float y, float z) {
            return sampleSdf(x, y, z, terrainContext);
        };
        environment.sampleNormal = [&terrainContext](float x, float y, float z) {
            return sampleTerrainNormal(x, y, z, terrainContext);
        };
        environment.collisionRadius = plane.collisionRadius;

        if (!pauseState.active) {
            updateCloudField(cloudField, windState, dt, nowSeconds, plane.pos, worldRng);
            environment.wind = getWindVector3(windState);
            if (!runtime.crashed) {
                stepFlight(plane, runtime, dt, nowSeconds, input, environment, config);
            }
            worldTime = nowSeconds;
        }

        const ProceduralAudioFrame audioFrame = buildProceduralAudioFrame(
            plane,
            runtime,
            environment,
            terrainContext,
            uiState,
            propAudioConfig,
            0.0f,
            0.0f,
            windowHasFocus && !runtime.crashed,
            pauseState.active,
            dt);
        audioState.update(audioFrame);

        if (preferencesDirty && uiNowSeconds >= preferencesNextSaveAt) {
            std::string saveError;
            if (savePreferences(preferencesPath, uiState, planeProfile, terrainParams, planeVisual, walkingVisual, &saveError)) {
                if (worldStorePtr != nullptr) {
                    worldStorePtr->flushDirty(nullptr);
                }
                preferencesDirty = false;
            } else {
                SDL_Log("Native preference save failed: %s", saveError.c_str());
                preferencesNextSaveAt = uiNowSeconds + 3.0f;
            }
        }

        const Camera renderCamera = buildRenderCamera(plane, terrainContext, uiState);
        const TerrainVisualCache& terrainVisuals = ensureTerrainVisualCache(
            terrainCache,
            plane.pos,
            plane.flightVel,
            terrainContext,
            terrainChunkBakeCache,
            terrainWorldId);

        std::vector<RenderObject> opaqueObjects;
        opaqueObjects.reserve(((terrainVisuals.nearTiles.size() + terrainVisuals.farTiles.size()) * 2u) + 4u);
        std::vector<RenderObject> translucentObjects;
        translucentObjects.reserve(
            (terrainVisuals.nearTiles.size() + terrainVisuals.farTiles.size()) +
            (cloudField.groups.size() * 12u));
        for (const TerrainFarTile& tile : terrainVisuals.farTiles) {
            if (!tile.active) {
                continue;
            }

            opaqueObjects.push_back({
                &tile.terrainModel,
                {},
                quatIdentity(),
                { 1.0f, 1.0f, 1.0f },
                { 1.0f, 1.0f, 1.0f },
                1.0f,
                450.0f,
                9000.0f,
                true,
                true
            });

            if (!tile.propModel.faces.empty()) {
                opaqueObjects.push_back({
                    &tile.propModel,
                    {},
                    quatIdentity(),
                    { 1.0f, 1.0f, 1.0f },
                    { 1.0f, 1.0f, 1.0f },
                    1.0f,
                    450.0f,
                    9000.0f,
                    true,
                    true
                });
            }

            if (!tile.waterModel.faces.empty()) {
                translucentObjects.push_back({
                    &tile.waterModel,
                    {},
                    quatIdentity(),
                    { 1.0f, 1.0f, 1.0f },
                    { 1.0f, 1.0f, 1.0f },
                    1.0f,
                    450.0f,
                    9000.0f,
                    false,
                    true
                });
            }
        }
        for (const TerrainFarTile& tile : terrainVisuals.nearTiles) {
            if (!tile.active) {
                continue;
            }

            opaqueObjects.push_back({
                &tile.terrainModel,
                {},
                quatIdentity(),
                { 1.0f, 1.0f, 1.0f },
                { 1.0f, 1.0f, 1.0f },
                1.0f,
                120.0f,
                3200.0f,
                false,
                true
            });

            if (!tile.propModel.faces.empty()) {
                opaqueObjects.push_back({
                    &tile.propModel,
                    {},
                    quatIdentity(),
                    { 1.0f, 1.0f, 1.0f },
                    { 1.0f, 1.0f, 1.0f },
                    1.0f,
                    120.0f,
                    3200.0f,
                    false,
                    true
                });
            }

            if (!tile.waterModel.faces.empty()) {
                translucentObjects.push_back({
                    &tile.waterModel,
                    {},
                    quatIdentity(),
                    { 1.0f, 1.0f, 1.0f },
                    { 1.0f, 1.0f, 1.0f },
                    1.0f,
                    120.0f,
                    3200.0f,
                    false,
                    true
                });
            }
        }

        const Quat planeRenderRotation = quatNormalize(quatMultiply(plane.rot, composeVisualRotationOffset(planeVisual)));
        const Vec3 planeRenderPosition = plane.pos + rotateVector(planeRenderRotation, planeVisual.modelOffset);
        if (uiState.chaseCamera) {
            opaqueObjects.push_back({
                &planeVisual.model,
                planeRenderPosition,
                planeRenderRotation,
                { planeVisual.scale, planeVisual.scale, planeVisual.scale },
                { 1.0f, 1.0f, 1.0f },
                1.0f,
                400.0f,
                2600.0f,
                true
            });
        }

        if (pauseState.active && (pauseState.tab == PauseTab::Characters || pauseState.tab == PauseTab::Paint)) {
            const PlaneVisualState& previewVisual =
                pauseState.tab == PauseTab::Paint
                    ? visualForRole(pauseState.paintSubTab, planeVisual, walkingVisual)
                    : visualForRole(pauseState.charactersSubTab, planeVisual, walkingVisual);
            const Vec3 cameraForward = forwardFromRotation(renderCamera.rot);
            const Vec3 cameraRight = rightFromRotation(renderCamera.rot);
            const Vec3 cameraUp = upFromRotation(renderCamera.rot);
            const Quat previewSpin = previewVisual.previewAutoSpin
                ? quatFromAxisAngle({ 0.0f, 1.0f, 0.0f }, uiNowSeconds * 0.8f)
                : quatIdentity();
            const Quat previewRotation = quatNormalize(quatMultiply(previewSpin, composeVisualRotationOffset(previewVisual)));
            const Vec3 previewAnchor = renderCamera.pos + (cameraForward * 34.0f) + (cameraRight * 18.0f) - (cameraUp * 6.0f);
            const Vec3 previewPosition = previewAnchor + rotateVector(previewRotation, previewVisual.modelOffset);

            opaqueObjects.push_back({
                &previewVisual.model,
                previewPosition,
                previewRotation,
                {
                    previewVisual.scale * previewVisual.previewZoom,
                    previewVisual.scale * previewVisual.previewZoom,
                    previewVisual.scale * previewVisual.previewZoom
                },
                { 1.0f, 1.0f, 1.0f },
                1.0f,
                40.0f,
                160.0f,
                true
            });
        }

        for (const CloudGroup& group : cloudField.groups) {
            for (const CloudPuff& puff : group.puffs) {
                translucentObjects.push_back({
                    &cloudModel,
                    group.center + puff.offset + Vec3 { 0.0f, std::sin((worldTime * 0.2f) + puff.bobPhase) * puff.bobAmplitude, 0.0f },
                    quatFromAxisAngle({ 0.0f, 1.0f, 0.0f }, puff.yaw),
                    { puff.scale, puff.scale * puff.stretchY, puff.scale },
                    puff.color,
                    0.78f,
                    500.0f,
                    3000.0f,
                    true
                });
            }
        }

        std::sort(translucentObjects.begin(), translucentObjects.end(), [&renderCamera](const RenderObject& lhs, const RenderObject& rhs) {
            return lengthSquared(lhs.pos - renderCamera.pos) > lengthSquared(rhs.pos - renderCamera.pos);
        });

        int drawableWidth = 0;
        int drawableHeight = 0;
        SDL_GetWindowSizeInPixels(window, &drawableWidth, &drawableHeight);
        hudCanvas.resize(drawableWidth, drawableHeight);
        hudCanvas.clear({ 0, 0, 0, 0 });
        drawHud(
            hudCanvas,
            drawableWidth,
            drawableHeight,
            plane,
            runtime,
            terrainContext,
            geoConfig,
            windState,
            uiState,
            planeVisual.label,
            rendererLabel,
            fpsSmoothed,
            mouseCaptured);
        if (pauseState.active) {
            drawPauseMenu(hudCanvas, drawableWidth, drawableHeight, pauseState, uiState, config, propAudioConfig, terrainParams, assetCatalog, planeVisual, walkingVisual, paintUi);
        }

        if (!nativeRenderer.render(renderCamera, sunDirection, skyColor, opaqueObjects, translucentObjects, hudCanvas, &rendererError)) {
            SDL_Log("Vulkan render failed: %s", rendererError.c_str());
            running = false;
        }
    }

    if (preferencesDirty) {
        std::string saveError;
        if (!savePreferences(preferencesPath, uiState, planeProfile, terrainParams, planeVisual, walkingVisual, &saveError)) {
            SDL_Log("Native preference save on shutdown failed: %s", saveError.c_str());
        }
    }
    if (worldStorePtr != nullptr) {
        worldStorePtr->flushDirty(nullptr);
    }

    audioState.shutdown();
    if (audioSubsystemReady) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    nativeRenderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
#endif

int main(int, char**)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    const bool audioSubsystemReady = SDL_InitSubSystem(SDL_INIT_AUDIO);
    if (!audioSubsystemReady) {
        SDL_Log("SDL audio init failed: %s", SDL_GetError());
    }
    const bool gamepadSubsystemReady = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    if (!gamepadSubsystemReady) {
        SDL_Log("SDL gamepad init failed: %s", SDL_GetError());
    }

    const UiState defaultUiStateValues = defaultUiState();
    const GraphicsSettings defaultGraphicsSettingsValues = defaultGraphicsSettings();
    const LightingSettings defaultLightingSettingsValues = defaultLightingSettings();
    const HudSettings defaultHudSettingsValues = defaultHudSettings();
    const ControlProfile defaultControlProfileValues = defaultControlProfile();
    const FlightConfig defaultFlightConfigValues = defaultFlightConfig();
    const PropAudioConfig defaultPropAudioConfigValues = defaultPropAudioConfig();
    const TerrainParams defaultTerrainParamsValues = defaultTerrainParams();

    BootResources boot;
    boot.uiState = defaultUiStateValues;
    boot.graphics = defaultGraphicsSettingsValues;
    boot.lighting = defaultLightingSettingsValues;
    boot.hud = defaultHudSettingsValues;
    boot.controls = defaultControlProfileValues;
    boot.terrainParams = defaultTerrainParamsValues;
    boot.defaultTerrainParamsValues = defaultTerrainParamsValues;
    boot.preferencesPath = getPreferenceFilePath();
    boot.planeProfile.visualPrefs.scale = 3.0f;
    boot.walkingPrefs.scale = 1.0f;
    boot.planeVisual.defaultScale = 3.0f;
    setBuiltinPlaneModel(boot.planeVisual);
    boot.walkingVisual.defaultScale = 1.0f;
    setBuiltinPlaneModel(boot.walkingVisual);
    boot.walkingVisual.label = "builtin player cube";

    SDL_Window* window = SDL_CreateWindow(
        "TrueFlight",
        boot.graphics.resolutionWidth,
        boot.graphics.resolutionHeight,
        SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        if (audioSubsystemReady) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }
        SDL_Quit();
        return 1;
    }

    const auto shaderDirectory = findAssetPath("Shaders");
    if (shaderDirectory.empty()) {
        SDL_Log("Unable to locate compiled shaders directory.");
        SDL_DestroyWindow(window);
        if (audioSubsystemReady) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }
        SDL_Quit();
        return 1;
    }

    VulkanRenderer nativeRenderer;
    std::string rendererError;
    if (!nativeRenderer.initialize(window, shaderDirectory, &rendererError)) {
        SDL_Log("Vulkan renderer initialization failed: %s", rendererError.c_str());
        nativeRenderer.shutdown();
        SDL_DestroyWindow(window);
        if (audioSubsystemReady) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }
        SDL_Quit();
        return 1;
    }

    const std::string rendererLabel = std::string("Renderer: ") + (nativeRenderer.backendName() != nullptr ? nativeRenderer.backendName() : "unknown");

    PauseState menuState {};
    menuState.active = true;
    menuState.mode = MenuMode::MainMenu;
    AppScreen screen = AppScreen::BootLoading;
    GamepadState gamepad {};

    const float bootStartSeconds = static_cast<float>(SDL_GetTicks()) * 0.001f;
    beginLoadingUi(boot.loadingUi, "Booting", bootStartSeconds);
    const auto bootStage = [&](const std::string& stageLabel, float progress, const std::string& detail) -> bool {
        updateLoadingUi(boot.loadingUi, stageLabel, progress, detail);
        SDL_PumpEvents();
        std::string loadingError;
        if (!presentLoadingUi(window, nativeRenderer, boot.hudCanvas, boot.loadingUi, rendererLabel, boot.lighting, &loadingError)) {
            if (!loadingError.empty()) {
                SDL_Log("Boot loading render failed: %s", loadingError.c_str());
            }
            return false;
        }
        return true;
    };

    if (!bootStage("Loading Preferences", 0.16f, "Reading native_settings.ini.")) {
        nativeRenderer.shutdown();
        SDL_DestroyWindow(window);
        if (audioSubsystemReady) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }
        SDL_Quit();
        return 1;
    }

    std::string preferenceError;
    if (!loadPreferences(
            boot.preferencesPath,
            boot.uiState,
            boot.graphics,
            boot.lighting,
            boot.hud,
            boot.controls,
            boot.planeProfile,
            boot.terrainParams,
            boot.walkingPrefs,
            &preferenceError) &&
        !preferenceError.empty()) {
        SDL_Log("Native preference load failed: %s", preferenceError.c_str());
    }

    if (!bootStage("Applying Display", 0.32f, "Applying display mode and resolution.")) {
        nativeRenderer.shutdown();
        SDL_DestroyWindow(window);
        if (audioSubsystemReady) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }
        SDL_Quit();
        return 1;
    }

    {
        std::string displayError;
        if (!applyGraphicsSettingsToWindow(window, boot.graphics, &displayError) && !displayError.empty()) {
            SDL_Log("Display apply failed during boot: %s", displayError.c_str());
        }
    }

    if (!bootStage("Scanning Models", 0.52f, "Cataloging preview assets.")) {
        nativeRenderer.shutdown();
        SDL_DestroyWindow(window);
        if (audioSubsystemReady) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }
        SDL_Quit();
        return 1;
    }
    boot.assetCatalog = scanModelAssets();

    if (!bootStage("Restoring Visuals", 0.74f, "Restoring stored models and paint overlays.")) {
        nativeRenderer.shutdown();
        SDL_DestroyWindow(window);
        if (audioSubsystemReady) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }
        SDL_Quit();
        return 1;
    }

    std::filesystem::path planeFallbackPath {};
    if (const auto glbPath = findAssetPath("portSource/Assets/Models/DualEngine.glb"); !glbPath.empty()) {
        planeFallbackPath = glbPath;
    } else if (const auto stlPath = findAssetPath("portSource/Assets/Models/DualEngine.stl"); !stlPath.empty()) {
        planeFallbackPath = stlPath;
    }

    restoreVisualFromPreferences(boot.planeVisual, boot.planeProfile.visualPrefs, planeFallbackPath, "plane");
    restoreVisualFromPreferences(boot.walkingVisual, boot.walkingPrefs, {}, "walking");
    if (boot.walkingVisual.sourcePath.empty() && boot.walkingVisual.label == "builtin cube") {
        boot.walkingVisual.label = "builtin player cube";
    }

    if (!bootStage("Menu Ready", 0.92f, "Preparing shared menu resources.")) {
        nativeRenderer.shutdown();
        SDL_DestroyWindow(window);
        if (audioSubsystemReady) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }
        SDL_Quit();
        return 1;
    }

    finishLoadingUi(boot.loadingUi, static_cast<float>(SDL_GetTicks()) * 0.001f);
    screen = AppScreen::MainMenu;
    setMouseCapture(window, false);

    std::optional<GameSession> session;
    bool running = true;
    bool windowHasFocus = true;
    bool mouseCaptured = false;
    float fpsSmoothed = 120.0f;
    std::uint64_t previousCounter = SDL_GetPerformanceCounter();
    const double counterFrequency = static_cast<double>(SDL_GetPerformanceFrequency());

    auto menuVisible = [&]() -> bool {
        return screen == AppScreen::MainMenu || (screen == AppScreen::InFlight && menuState.active);
    };

    auto currentWorldStore = [&]() -> WorldStore* {
        if (!session.has_value() || !session->worldStore.has_value()) {
            return nullptr;
        }
        return &*session->worldStore;
    };

    auto closeGamepad = [&]() {
        if (gamepad.handle != nullptr) {
            SDL_CloseGamepad(gamepad.handle);
        }
        gamepad = {};
    };

    auto openGamepad = [&](SDL_JoystickID instanceId, float nowSeconds, bool announce) -> bool {
        if (!gamepadSubsystemReady || !SDL_IsGamepad(instanceId)) {
            return false;
        }
        if (gamepad.handle != nullptr && gamepad.instanceId == instanceId) {
            return true;
        }

        closeGamepad();
        SDL_Gamepad* opened = SDL_OpenGamepad(instanceId);
        if (opened == nullptr) {
            SDL_Log("SDL_OpenGamepad failed for %d: %s", static_cast<int>(instanceId), SDL_GetError());
            return false;
        }

        gamepad.handle = opened;
        gamepad.instanceId = SDL_GetGamepadID(opened);
        if (const char* gamepadName = SDL_GetGamepadName(opened); gamepadName != nullptr) {
            gamepad.name = gamepadName;
        } else {
            gamepad.name = "gamepad";
        }
        pollGamepadState(gamepad);
        if (announce) {
            setPauseStatus(menuState, std::string("Controller ready: ") + gamepad.name + ".", nowSeconds, 2.6f);
        }
        return true;
    };

    auto ensureGamepadOpen = [&](float nowSeconds, bool announce) {
        if (!gamepadSubsystemReady || gamepad.handle != nullptr) {
            return;
        }
        int count = 0;
        SDL_JoystickID* gamepads = SDL_GetGamepads(&count);
        if (gamepads == nullptr) {
            return;
        }
        if (count > 0) {
            (void)openGamepad(gamepads[0], nowSeconds, announce);
        }
        SDL_free(gamepads);
    };

    auto syncHudUi = [&]() {
        syncUiStateFromHud(boot.uiState, boot.hud);
    };

    auto stopPromptTextInput = [&]() {
        if (menuState.promptActive) {
            SDL_StopTextInput(window);
        }
        clearMenuPrompt(menuState);
    };

    auto beginModelPrompt = [&](CharacterSubTab role, float nowSeconds) {
        clearControlBindingCapture(menuState);
        clearMenuConfirmation(menuState);
        menuState.rowDragActive = false;
        beginModelPathPrompt(
            menuState,
            role,
            visualForRole(role, boot.planeVisual, boot.walkingVisual).sourcePath.generic_string());
        SDL_StartTextInput(window);
        setPauseStatus(
            menuState,
            std::string(role == CharacterSubTab::Plane ? "Plane" : "Walking") + " model path: Enter to load, Esc to cancel.",
            nowSeconds,
            4.5f);
    };

    auto clampHelpScroll = [&]() {
        int menuWidth = 0;
        int menuHeight = 0;
        SDL_GetWindowSizeInPixels(window, &menuWidth, &menuHeight);
        const PauseLayout layout = buildPauseLayout(menuWidth, menuHeight);
        menuState.helpScroll = std::clamp(menuState.helpScroll, 0, maxMenuHelpScroll(layout));
    };

    auto clearMenuInteractions = [&]() {
        menuState.rowDragActive = false;
        clearMenuConfirmation(menuState);
    };

    auto setCurrentSelection = [&](int index, bool preserveConfirmation = false) {
        menuState.rowDragActive = false;
        const bool keepConfirmation =
            preserveConfirmation &&
            menuState.tab == PauseTab::Main &&
            menuState.selectedIndex == index &&
            menuState.confirmPending &&
            menuState.confirmSelectedIndex == index;
        if (!keepConfirmation) {
            clearMenuConfirmation(menuState);
        }
        if (menuState.tab == PauseTab::Controls) {
            menuState.controlsSelection = index;
        } else {
            menuState.selectedIndex = index;
        }
    };

    auto clampMenuSelection = [&]() {
        const int itemCount = menuItemCount(menuState, boot.controls, boot.assetCatalog.size());
        if (menuState.tab == PauseTab::Controls) {
            menuState.controlsSelection = itemCount > 0 ? std::clamp(menuState.controlsSelection, 0, itemCount - 1) : 0;
        } else {
            menuState.selectedIndex = itemCount > 0 ? std::clamp(menuState.selectedIndex, 0, itemCount - 1) : 0;
        }
        menuState.controlsSlot = std::clamp(menuState.controlsSlot, 0, 1);
        clampHelpScroll();
    };

    auto moveMenuSelection = [&](int delta) {
        if (menuState.tab == PauseTab::Help) {
            if (delta != 0) {
                clearMenuInteractions();
                menuState.helpScroll = std::max(0, menuState.helpScroll + delta);
                clampHelpScroll();
            }
            return;
        }
        const int itemCount = menuItemCount(menuState, boot.controls, boot.assetCatalog.size());
        if (itemCount <= 0) {
            return;
        }
        clearMenuInteractions();
        if (menuState.tab == PauseTab::Controls) {
            menuState.controlsSelection = (menuState.controlsSelection + delta + itemCount) % itemCount;
        } else {
            menuState.selectedIndex = (menuState.selectedIndex + delta + itemCount) % itemCount;
        }
    };

    auto activeCharacterVisual = [&]() -> PlaneVisualState& {
        return visualForRole(menuState.charactersSubTab, boot.planeVisual, boot.walkingVisual);
    };

    auto activePaintVisual = [&]() -> PlaneVisualState& {
        return visualForRole(menuState.paintSubTab, boot.planeVisual, boot.walkingVisual);
    };

    auto fixBuiltinRoleLabel = [&](CharacterSubTab role, PlaneVisualState& visual) {
        if (role == CharacterSubTab::Player && visual.sourcePath.empty() && visual.label == "builtin cube") {
            visual.label = "builtin player cube";
        }
    };

    auto queueSave = [&](float nowSeconds) {
        schedulePreferencesSave(boot.preferencesDirty, boot.preferencesNextSaveAt, nowSeconds);
    };

    auto submitModelPrompt = [&](float nowSeconds) {
        const CharacterSubTab promptRole = menuState.promptRole;
        std::string rawPath = trimAscii(menuState.promptText);
        if (rawPath.size() >= 2 &&
            ((rawPath.front() == '"' && rawPath.back() == '"') || (rawPath.front() == '\'' && rawPath.back() == '\''))) {
            rawPath = rawPath.substr(1, rawPath.size() - 2);
        }
        if (rawPath.empty()) {
            setPauseStatus(menuState, "Enter a STL, GLB, or GLTF path first.", nowSeconds, 3.0f);
            return;
        }

        PlaneVisualState& visual = visualForRole(promptRole, boot.planeVisual, boot.walkingVisual);
        std::string loadStatus;
        const bool loaded = loadPlaneModelFromPath(std::filesystem::path(rawPath), visual, &loadStatus);
        fixBuiltinRoleLabel(promptRole, visual);
        if (!loaded) {
            setPauseStatus(
                menuState,
                loadStatus.empty() ? "Model load failed. Correct the path and try again." : loadStatus,
                nowSeconds,
                4.5f);
            return;
        }

        stopPromptTextInput();
        menuState.tab = PauseTab::Characters;
        menuState.charactersSubTab = promptRole;
        menuState.paintSubTab = promptRole;
        menuState.selectedIndex = 0;
        boot.assetCatalog = scanModelAssets();
        queueSave(nowSeconds);
        setPauseStatus(menuState, loadStatus, nowSeconds, 3.4f);
    };

    auto switchSessionToWalking = [&](float nowSeconds, const std::string& statusText, bool snapToGround) {
        if (!session.has_value()) {
            return;
        }

        if (snapToGround) {
            const float ground = sampleGroundHeight(session->plane.pos.x, session->plane.pos.z, session->terrainContext);
            if (session->plane.pos.y < (ground + kWalkingHalfHeight)) {
                session->plane.pos.y = ground + kWalkingHalfHeight;
                session->plane.onGround = true;
            }
        }

        session->flightMode = false;
        session->plane.throttle = 0.0f;
        session->plane.vel = {};
        session->plane.flightVel = {};
        session->plane.flightAngVel = {};
        session->runtime.crashed = false;
        session->runtime.hasPendingCrash = false;
        syncWalkingLookFromRotation(session->plane.rot, session->walkYaw, session->walkPitch);
        session->plane.rot = composeWalkingRotation(session->walkYaw, session->walkPitch);
        session->flightLookYaw = 0.0f;
        session->flightLookPitch = 0.0f;
        gamepad.rudderLatched = false;
        gamepad.trimAccumulator = 0.0f;
        menuState.sessionFlightMode = false;
        setPauseStatus(menuState, statusText, nowSeconds, 3.0f);
    };

    auto switchSessionToFlight = [&](float nowSeconds, float x, float z, const std::string& statusText) {
        if (!session.has_value()) {
            return;
        }

        resetFlight(session->plane, session->runtime, session->terrainContext, x, z);
        session->flightMode = true;
        syncWalkingLookFromRotation(session->plane.rot, session->walkYaw, session->walkPitch);
        session->flightLookYaw = 0.0f;
        session->flightLookPitch = 0.0f;
        gamepad.rudderLatched = false;
        gamepad.trimAccumulator = 0.0f;
        menuState.sessionFlightMode = true;
        setPauseStatus(menuState, statusText, nowSeconds, 2.6f);
    };

    auto handleCrashTransition = [&](const FlightCrashEvent& crashEvent, float nowSeconds) {
        if (!session.has_value()) {
            return;
        }

        const float impactX = crashEvent.position.x;
        const float impactY = crashEvent.position.y;
        const float impactZ = crashEvent.position.z;
        const TerrainCrater crater = buildCrashCrater(impactX, impactY, impactZ, crashEvent.totalSpeed);
        const bool terrainImpact = crashEvent.cause != FlightCrashCause::PropBlocker;
        if (terrainImpact) {
            session->foliageImpactPulse = 0.0f;
        } else {
            session->foliageImpactPulse = 1.0f;
        }
        if (terrainImpact) {
            if (WorldStore* worldStore = currentWorldStore(); worldStore != nullptr) {
            const auto craterResult = worldStore->applyCrater(crater);
            if (craterResult.first) {
                worldStore->flushDirty(nullptr);
                refreshTerrainContext(boot.terrainParams, session->terrainContext, session->terrainCache, worldStore);
            }
        }
        }

        float nx = crashEvent.normal.x;
        float nz = crashEvent.normal.z;
        float horizontalLength = std::sqrt((nx * nx) + (nz * nz));
        if (horizontalLength <= 1.0e-4f) {
            nx = -crashEvent.velocity.x;
            nz = -crashEvent.velocity.z;
            horizontalLength = std::sqrt((nx * nx) + (nz * nz));
        }
        if (horizontalLength <= 1.0e-4f) {
            nx = 1.0f;
            nz = 0.0f;
            horizontalLength = 1.0f;
        }
        nx /= horizontalLength;
        nz /= horizontalLength;

        const float spawnOffset = (terrainImpact ? std::max(6.0f, crater.radius) : std::max(7.0f, crashEvent.radius * 3.0f)) + 4.5f;
        const float spawnX = impactX + (nx * spawnOffset);
        const float spawnZ = impactZ + (nz * spawnOffset);
        const float groundY = sampleGroundHeight(spawnX, spawnZ, session->terrainContext);
        const float spawnY = groundY + kWalkingHalfHeight + 0.2f;
        session->plane.pos = { spawnX, spawnY, spawnZ };
        session->plane.vel = {};
        session->plane.flightVel = {};
        session->plane.flightAngVel = {};
        session->plane.onGround = true;
        session->plane.throttle = 0.0f;

        const float toCraterX = impactX - spawnX;
        const float toCraterZ = impactZ - spawnZ;
        session->walkYaw = wrapAngle(std::atan2(toCraterX, toCraterZ));
        session->walkPitch = 0.0f;
        session->plane.rot = composeWalkingRotation(session->walkYaw, session->walkPitch);
        session->flightMode = false;
        session->flightLookYaw = 0.0f;
        session->flightLookPitch = 0.0f;
        gamepad.rudderLatched = false;
        gamepad.trimAccumulator = 0.0f;
        session->runtime.crashed = false;
        session->runtime.hasPendingCrash = false;
        menuState.sessionFlightMode = false;
        setPauseStatus(
            menuState,
            terrainImpact ? "Crash detected: switched to walking mode beside the impact crater." : "Crash detected: switched to walking mode beside the obstacle impact.",
            nowSeconds,
            4.0f);
    };

    auto applyDisplaySettings = [&](float nowSeconds) {
        std::string displayError;
        if (applyGraphicsSettingsToWindow(window, boot.graphics, &displayError)) {
            setPauseStatus(menuState, "Applied display settings.", nowSeconds, 2.2f);
            queueSave(nowSeconds);
        } else {
            setPauseStatus(
                menuState,
                displayError.empty() ? "Display apply failed." : ("Display apply failed: " + displayError),
                nowSeconds,
                4.0f);
        }
    };

    auto performPaintCommand = [&](int rowIndex, float nowSeconds) {
        PlaneVisualState& paintVisual = activePaintVisual();
        switch (rowIndex) {
        case 5:
            if (paintUndo(paintVisual)) {
                setPauseStatus(menuState, "Undid last paint step.", nowSeconds, 2.2f);
            }
            break;
        case 6:
            if (paintRedo(paintVisual)) {
                setPauseStatus(menuState, "Redid paint step.", nowSeconds, 2.2f);
            }
            break;
        case 7:
            if (fillPaintOverlay(paintVisual, boot.paintUi.colorIndex)) {
                setPauseStatus(menuState, "Filled active paint overlay.", nowSeconds, 2.2f);
            }
            break;
        case 8:
            if (clearPaintOverlay(paintVisual)) {
                setPauseStatus(menuState, "Cleared active paint overlay.", nowSeconds, 2.2f);
            }
            break;
        case 9: {
            std::string paintHash;
            std::string paintError;
            if (commitPaintOverlay(getPaintStorageDirectory(), paintVisual, &paintHash, &paintError)) {
                setPauseStatus(
                    menuState,
                    "Committed paint overlay " + paintHash.substr(0, std::min<std::size_t>(8, paintHash.size())) + ".",
                    nowSeconds,
                    2.8f);
                queueSave(nowSeconds);
            } else if (!paintError.empty()) {
                setPauseStatus(menuState, paintError, nowSeconds, 3.8f);
            }
            break;
        }
        case 10: {
            if (paintVisual.paintHash.empty()) {
                setPauseStatus(menuState, "No saved paint overlay is stored for this role.", nowSeconds, 3.2f);
                break;
            }
            std::string paintError;
            if (loadPaintOverlayByHash(paintVisual.paintHash, paintVisual, &paintError)) {
                setPauseStatus(menuState, "Reloaded committed paint overlay.", nowSeconds, 2.4f);
            } else if (!paintError.empty()) {
                setPauseStatus(menuState, paintError, nowSeconds, 3.8f);
            }
            break;
        }
        default:
            break;
        }
    };

    auto startFlight = [&](float nowSeconds) {
        stopPromptTextInput();
        clearMenuInteractions();
        gamepad.rudderLatched = false;
        gamepad.trimAccumulator = 0.0f;
        if (session.has_value()) {
            shutdownGameSession(*session);
            session.reset();
        }

        screen = AppScreen::WorldLoading;
        session.emplace();
        std::string sessionError;
        if (startGameSession(*session, boot, audioSubsystemReady, window, nativeRenderer, rendererLabel, &sessionError)) {
            screen = AppScreen::InFlight;
            session->flightMode = true;
            menuState.sessionFlightMode = true;
            menuState.active = false;
            menuState.mode = MenuMode::PauseOverlay;
            menuState.tab = PauseTab::Main;
            menuState.selectedIndex = 0;
            menuState.controlsSelection = 0;
            menuState.controlsSlot = 0;
            clearControlBindingCapture(menuState);
            boot.paintUi.draggingCanvas = false;
            return;
        }

        if (session.has_value()) {
            shutdownGameSession(*session);
            session.reset();
        }
        screen = AppScreen::MainMenu;
        menuState.active = true;
        menuState.mode = MenuMode::MainMenu;
        menuState.sessionFlightMode = true;
        setPauseStatus(
            menuState,
            sessionError.empty() ? "World startup failed." : sessionError,
            nowSeconds,
            4.5f);
    };

    auto returnToMainMenu = [&](float nowSeconds) {
        stopPromptTextInput();
        clearMenuInteractions();
        gamepad.rudderLatched = false;
        gamepad.trimAccumulator = 0.0f;
        if (session.has_value()) {
            shutdownGameSession(*session);
            session.reset();
        }
        screen = AppScreen::MainMenu;
        menuState.active = true;
        menuState.mode = MenuMode::MainMenu;
        menuState.sessionFlightMode = true;
        menuState.tab = PauseTab::Main;
        menuState.selectedIndex = 0;
        menuState.controlsSelection = 0;
        menuState.controlsSlot = 0;
        clearControlBindingCapture(menuState);
        boot.paintUi.draggingCanvas = false;
        setMouseCapture(window, false);
        mouseCaptured = false;
        setPauseStatus(menuState, "Returned to main menu.", nowSeconds, 2.0f);
    };

    auto restoreAllDefaults = [&](float nowSeconds) {
        clearMenuInteractions();
        boot.uiState = defaultUiStateValues;
        boot.graphics = defaultGraphicsSettingsValues;
        boot.lighting = defaultLightingSettingsValues;
        boot.hud = defaultHudSettingsValues;
        boot.controls = defaultControlProfileValues;
        boot.terrainParams = defaultTerrainParamsValues;
        boot.defaultTerrainParamsValues = defaultTerrainParamsValues;
        boot.planeProfile.flightConfig = defaultFlightConfigValues;
        boot.planeProfile.propAudioConfig = defaultPropAudioConfigValues;
        boot.planeProfile.visualPrefs = {};
        boot.planeProfile.visualPrefs.scale = 3.0f;
        boot.walkingPrefs = {};
        boot.walkingPrefs.scale = 1.0f;
        boot.paintUi = {};
        setBuiltinPlaneModel(boot.planeVisual);
        setBuiltinPlaneModel(boot.walkingVisual);
        boot.walkingVisual.label = "builtin player cube";
        syncHudUi();
        std::string displayError;
        if (!applyGraphicsSettingsToWindow(window, boot.graphics, &displayError) && !displayError.empty()) {
            SDL_Log("Display apply failed while restoring defaults: %s", displayError.c_str());
        }
        queueSave(nowSeconds);
        setPauseStatus(menuState, "Restored native defaults.", nowSeconds, 2.6f);
    };

    auto activateHomeSelection = [&](float nowSeconds) -> MenuCommand {
        if (menuState.mode == MenuMode::MainMenu) {
            switch (menuState.selectedIndex) {
            case 0:
                clearMenuConfirmation(menuState);
                return MenuCommand::StartFlight;
            case 1:
                if (!menuConfirmationMatches(menuState, menuState.selectedIndex, nowSeconds)) {
                    requestMenuConfirmation(menuState, menuState.selectedIndex, "Confirm restore defaults.", nowSeconds, 3.0f);
                    setPauseStatus(menuState, menuState.confirmText, nowSeconds, 3.0f);
                    return MenuCommand::None;
                }
                clearMenuConfirmation(menuState);
                restoreAllDefaults(nowSeconds);
                return MenuCommand::None;
            case 2:
                if (!menuConfirmationMatches(menuState, menuState.selectedIndex, nowSeconds)) {
                    requestMenuConfirmation(menuState, menuState.selectedIndex, "Confirm quit.", nowSeconds, 2.8f);
                    setPauseStatus(menuState, menuState.confirmText, nowSeconds, 2.8f);
                    return MenuCommand::None;
                }
                clearMenuConfirmation(menuState);
                return MenuCommand::Quit;
            default:
                return MenuCommand::None;
            }
        }

        if (!session.has_value()) {
            return MenuCommand::ReturnToMainMenu;
        }

        switch (menuState.selectedIndex) {
        case 0:
            clearMenuConfirmation(menuState);
            clearControlBindingCapture(menuState);
            setPauseActive(menuState, boot.uiState, false);
            return MenuCommand::None;
        case 1:
            clearMenuConfirmation(menuState);
            return MenuCommand::ToggleFlightMode;
        case 2:
            clearMenuConfirmation(menuState);
            resetFlight(session->plane, session->runtime, session->terrainContext, session->plane.pos.x, session->plane.pos.z);
            session->flightMode = true;
            menuState.sessionFlightMode = true;
            clearControlBindingCapture(menuState);
            setPauseActive(menuState, boot.uiState, false);
            return MenuCommand::None;
        case 3:
            if (!menuConfirmationMatches(menuState, menuState.selectedIndex, nowSeconds)) {
                requestMenuConfirmation(menuState, menuState.selectedIndex, "Confirm return to main menu.", nowSeconds, 3.0f);
                setPauseStatus(menuState, menuState.confirmText, nowSeconds, 3.0f);
                return MenuCommand::None;
            }
            clearMenuConfirmation(menuState);
            return MenuCommand::ReturnToMainMenu;
        case 4:
            if (!menuConfirmationMatches(menuState, menuState.selectedIndex, nowSeconds)) {
                requestMenuConfirmation(menuState, menuState.selectedIndex, "Confirm quit.", nowSeconds, 2.8f);
                setPauseStatus(menuState, menuState.confirmText, nowSeconds, 2.8f);
                return MenuCommand::None;
            }
            clearMenuConfirmation(menuState);
            return MenuCommand::Quit;
        default:
            return MenuCommand::None;
        }
    };

    auto applyMenuCommand = [&](MenuCommand command, float nowSeconds) {
        switch (command) {
        case MenuCommand::StartFlight:
            startFlight(nowSeconds);
            break;
        case MenuCommand::ReturnToMainMenu:
            returnToMainMenu(nowSeconds);
            break;
        case MenuCommand::Quit:
            running = false;
            break;
        case MenuCommand::ToggleFlightMode:
            if (session.has_value()) {
                if (session->flightMode) {
                    switchSessionToWalking(nowSeconds, "Walking mode enabled.", false);
                } else {
                    switchSessionToFlight(nowSeconds, session->plane.pos.x, session->plane.pos.z, "Flight mode restored.");
                }
                clearControlBindingCapture(menuState);
                setPauseActive(menuState, boot.uiState, false);
            }
            break;
        case MenuCommand::None:
        default:
            break;
        }
    };

    auto resetSelectedRow = [&](float nowSeconds) {
        switch (menuState.tab) {
        case PauseTab::Settings:
            if (menuState.settingsSubTab == SettingsSubTab::Flight) {
                resetTuningValue(boot.planeProfile.flightConfig, defaultFlightConfigValues, menuState.selectedIndex);
                setPauseStatus(menuState, "Reset selected flight value to default.", nowSeconds, 2.2f);
                queueSave(nowSeconds);
            } else if (menuState.settingsSubTab == SettingsSubTab::Terrain) {
                resetTerrainValue(boot.terrainParams, boot.defaultTerrainParamsValues, menuState.selectedIndex);
                if (session.has_value()) {
                    applyTerrainRuntimeChange(boot.terrainParams, session->terrainContext, session->terrainCache, session->plane, currentWorldStore());
                }
                setPauseStatus(menuState, "Reset selected terrain value to default.", nowSeconds, 2.2f);
                queueSave(nowSeconds);
            } else if (!menuSettingsRowDisabled(menuState.settingsSubTab, menuState.selectedIndex) &&
                       !menuSettingsRowAction(menuState.settingsSubTab, menuState.selectedIndex)) {
                resetMenuSettingsRowValue(
                    boot.uiState,
                    defaultUiStateValues,
                    boot.graphics,
                    defaultGraphicsSettingsValues,
                    boot.planeProfile.propAudioConfig,
                    defaultPropAudioConfigValues,
                    boot.lighting,
                    defaultLightingSettingsValues,
                    menuState.settingsSubTab,
                    menuState.selectedIndex);
                setPauseStatus(menuState, "Reset selected setting to default.", nowSeconds, 2.2f);
                queueSave(nowSeconds);
            }
            break;
        case PauseTab::Hud:
            resetHudRowValue(boot.hud, defaultHudSettingsValues, menuState.selectedIndex);
            syncHudUi();
            setPauseStatus(menuState, "Reset selected HUD setting to default.", nowSeconds, 2.2f);
            queueSave(nowSeconds);
            break;
        case PauseTab::Characters:
            if (menuState.selectedIndex >= 1 && menuState.selectedIndex <= 10) {
                PlaneVisualState& visual = activeCharacterVisual();
                resetCharacterRowValue(visual, menuState.selectedIndex);
                fixBuiltinRoleLabel(menuState.charactersSubTab, visual);
                setPauseStatus(menuState, "Reset selected character transform value.", nowSeconds, 2.2f);
                queueSave(nowSeconds);
            }
            break;
        case PauseTab::Paint:
            if (menuState.selectedIndex >= 0 && menuState.selectedIndex <= 4) {
                resetPaintRowValue(boot.paintUi, menuState.selectedIndex);
                setPauseStatus(menuState, "Reset selected paint control.", nowSeconds, 2.2f);
            }
            break;
        case PauseTab::Controls:
            if (menuState.controlsSelection >= 0 &&
                menuState.controlsSelection < static_cast<int>(boot.controls.actions.size())) {
                ControlActionBinding& action = boot.controls.actions[static_cast<std::size_t>(menuState.controlsSelection)];
                if (action.supported && action.configurable) {
                    if (const ControlActionBinding* defaults = findControlAction(defaultControlProfileValues, action.id); defaults != nullptr) {
                        action.slots[static_cast<std::size_t>(menuState.controlsSlot)] = defaults->slots[static_cast<std::size_t>(menuState.controlsSlot)];
                        setPauseStatus(menuState, "Reset selected binding slot to default.", nowSeconds, 2.2f);
                        queueSave(nowSeconds);
                    }
                }
            }
            break;
        default:
            break;
        }
    };

    auto adjustSelectedRow = [&](int direction, float nowSeconds) {
        if (direction == 0) {
            return;
        }

        switch (menuState.tab) {
        case PauseTab::Settings:
            if (menuState.settingsSubTab == SettingsSubTab::Flight) {
                adjustTuningValue(boot.planeProfile.flightConfig, menuState.selectedIndex, direction);
                queueSave(nowSeconds);
            } else if (menuState.settingsSubTab == SettingsSubTab::Terrain) {
                adjustTerrainValue(boot.terrainParams, menuState.selectedIndex, direction);
                if (session.has_value()) {
                    applyTerrainRuntimeChange(boot.terrainParams, session->terrainContext, session->terrainCache, session->plane, currentWorldStore());
                }
                queueSave(nowSeconds);
            } else if (!menuSettingsRowDisabled(menuState.settingsSubTab, menuState.selectedIndex) &&
                       !menuSettingsRowAction(menuState.settingsSubTab, menuState.selectedIndex)) {
                adjustMenuSettingsRowValue(
                    boot.uiState,
                    boot.graphics,
                    boot.planeProfile.propAudioConfig,
                    boot.lighting,
                    menuState.settingsSubTab,
                    menuState.selectedIndex,
                    direction);
                queueSave(nowSeconds);
            }
            break;
        case PauseTab::Hud:
            if (!hudRowDisabled(menuState.selectedIndex)) {
                adjustHudRowValue(boot.hud, menuState.selectedIndex, direction);
                syncHudUi();
                queueSave(nowSeconds);
            }
            break;
        case PauseTab::Characters:
            if (menuState.selectedIndex >= 1 && menuState.selectedIndex <= 10) {
                PlaneVisualState& visual = activeCharacterVisual();
                adjustCharacterRowValue(visual, menuState.selectedIndex, direction);
                fixBuiltinRoleLabel(menuState.charactersSubTab, visual);
                queueSave(nowSeconds);
            }
            break;
        case PauseTab::Paint:
            if (menuState.selectedIndex >= 0 && menuState.selectedIndex <= 4) {
                adjustPaintRowValue(boot.paintUi, menuState.selectedIndex, direction);
            }
            break;
        case PauseTab::Controls:
            menuState.controlsSlot = std::clamp(menuState.controlsSlot + direction, 0, 1);
            break;
        default:
            break;
        }
    };

    auto resetCurrentPage = [&](float nowSeconds) {
        switch (menuState.tab) {
        case PauseTab::Settings:
            if (menuState.settingsSubTab == SettingsSubTab::Flight) {
                boot.planeProfile.flightConfig = defaultFlightConfigValues;
                setPauseStatus(menuState, "Restored native flight-model defaults.", nowSeconds, 2.6f);
                queueSave(nowSeconds);
            } else if (menuState.settingsSubTab == SettingsSubTab::Terrain) {
                boot.terrainParams = boot.defaultTerrainParamsValues;
                if (session.has_value()) {
                    applyTerrainRuntimeChange(boot.terrainParams, session->terrainContext, session->terrainCache, session->plane, currentWorldStore());
                }
                setPauseStatus(menuState, "Restored native terrain defaults.", nowSeconds, 2.6f);
                queueSave(nowSeconds);
            } else {
                const int count = settingsSubTabItemCount(menuState.settingsSubTab);
                for (int index = 0; index < count; ++index) {
                    resetMenuSettingsRowValue(
                        boot.uiState,
                        defaultUiStateValues,
                        boot.graphics,
                        defaultGraphicsSettingsValues,
                        boot.planeProfile.propAudioConfig,
                        defaultPropAudioConfigValues,
                        boot.lighting,
                        defaultLightingSettingsValues,
                        menuState.settingsSubTab,
                        index);
                }
                if (menuState.settingsSubTab == SettingsSubTab::Graphics) {
                    std::string displayError;
                    if (!applyGraphicsSettingsToWindow(window, boot.graphics, &displayError) && !displayError.empty()) {
                        SDL_Log("Display apply failed while restoring settings defaults: %s", displayError.c_str());
                    }
                }
                setPauseStatus(menuState, "Restored native settings defaults.", nowSeconds, 2.6f);
                queueSave(nowSeconds);
            }
            break;
        case PauseTab::Hud:
            boot.hud = defaultHudSettingsValues;
            syncHudUi();
            setPauseStatus(menuState, "Restored native HUD defaults.", nowSeconds, 2.6f);
            queueSave(nowSeconds);
            break;
        case PauseTab::Characters: {
            PlaneVisualState& visual = activeCharacterVisual();
            setBuiltinPlaneModel(visual);
            fixBuiltinRoleLabel(menuState.charactersSubTab, visual);
            setPauseStatus(menuState, "Restored selected role to builtin defaults.", nowSeconds, 2.6f);
            queueSave(nowSeconds);
            break;
        }
        case PauseTab::Paint:
            if (clearPaintOverlay(activePaintVisual())) {
                setPauseStatus(menuState, "Cleared active paint overlay.", nowSeconds, 2.6f);
            }
            break;
        case PauseTab::Controls:
            boot.controls = defaultControlProfileValues;
            clearControlBindingCapture(menuState);
            setPauseStatus(menuState, "Restored native control bindings.", nowSeconds, 2.6f);
            queueSave(nowSeconds);
            break;
        default:
            break;
        }
    };

    auto activateSelectedRow = [&](float nowSeconds) {
        switch (menuState.tab) {
        case PauseTab::Main:
            applyMenuCommand(activateHomeSelection(nowSeconds), nowSeconds);
            break;
        case PauseTab::Settings:
            if (menuState.settingsSubTab == SettingsSubTab::Graphics && menuState.selectedIndex == 2) {
                applyDisplaySettings(nowSeconds);
            } else {
                resetSelectedRow(nowSeconds);
            }
            break;
        case PauseTab::Characters:
            if (menuState.selectedIndex >= 1 && menuState.selectedIndex <= 10) {
                resetSelectedRow(nowSeconds);
            } else if (menuState.selectedIndex == 11) {
                beginModelPrompt(menuState.charactersSubTab, nowSeconds);
            } else if (menuState.selectedIndex == 12) {
                PlaneVisualState& visual = activeCharacterVisual();
                setBuiltinPlaneModel(visual);
                fixBuiltinRoleLabel(menuState.charactersSubTab, visual);
                setPauseStatus(menuState, "Loaded builtin cube.", nowSeconds, 2.2f);
                queueSave(nowSeconds);
            } else if (menuState.selectedIndex >= kCharacterAssetListStart &&
                       menuState.selectedIndex < characterItemCount(boot.assetCatalog.size())) {
                PlaneVisualState& visual = activeCharacterVisual();
                const AssetEntry& asset = boot.assetCatalog[static_cast<std::size_t>(menuState.selectedIndex - kCharacterAssetListStart)];
                std::string loadStatus;
                loadPlaneModelFromPath(asset.path, visual, &loadStatus);
                fixBuiltinRoleLabel(menuState.charactersSubTab, visual);
                setPauseStatus(menuState, loadStatus, nowSeconds, asset.supported ? 3.0f : 4.5f);
                if (asset.supported) {
                    queueSave(nowSeconds);
                }
            }
            break;
        case PauseTab::Paint:
            if (menuState.selectedIndex <= 4) {
                resetSelectedRow(nowSeconds);
            } else {
                performPaintCommand(menuState.selectedIndex, nowSeconds);
            }
            break;
        case PauseTab::Hud:
            resetSelectedRow(nowSeconds);
            break;
        case PauseTab::Controls:
            if (menuState.controlsSelection >= 0 &&
                menuState.controlsSelection < static_cast<int>(boot.controls.actions.size())) {
                const ControlActionBinding& action = boot.controls.actions[static_cast<std::size_t>(menuState.controlsSelection)];
                if (!action.supported || !action.configurable) {
                    setPauseStatus(menuState, "This action is currently unavailable in the native runtime.", nowSeconds, 3.0f);
                } else {
                    beginControlBindingCapture(menuState, menuState.controlsSelection, menuState.controlsSlot);
                    setPauseStatus(menuState, std::string("Listening for ") + action.label + ".", nowSeconds, 6.0f);
                }
            }
            break;
        default:
            break;
        }
    };

    auto handleTriggeredAction = [&](InputActionId actionId, float nowSeconds) -> bool {
        switch (actionId) {
        case InputActionId::ToggleCamera:
            if (screen != AppScreen::InFlight || menuVisible()) {
                return false;
            }
            boot.uiState.chaseCamera = !boot.uiState.chaseCamera;
            queueSave(nowSeconds);
            return true;
        case InputActionId::ToggleMap:
            if (screen != AppScreen::InFlight || menuVisible()) {
                return false;
            }
            boot.hud.showMap = !boot.hud.showMap;
            syncHudUi();
            queueSave(nowSeconds);
            return true;
        case InputActionId::ToggleDebug:
            if (screen != AppScreen::InFlight || menuVisible()) {
                return false;
            }
            boot.hud.showDebug = !boot.hud.showDebug;
            syncHudUi();
            queueSave(nowSeconds);
            return true;
        case InputActionId::ResetFlight:
            if (!session.has_value() || menuVisible()) {
                return false;
            }
            resetFlight(session->plane, session->runtime, session->terrainContext, session->plane.pos.x, session->plane.pos.z);
            session->flightMode = true;
            menuState.sessionFlightMode = true;
            return true;
        case InputActionId::PaintBrush:
            if (!menuVisible() || menuState.tab != PauseTab::Paint) {
                return false;
            }
            boot.paintUi.mode = PaintMode::Brush;
            return true;
        case InputActionId::PaintErase:
            if (!menuVisible() || menuState.tab != PauseTab::Paint) {
                return false;
            }
            boot.paintUi.mode = PaintMode::Erase;
            return true;
        case InputActionId::PaintFill:
            if (!menuVisible() || menuState.tab != PauseTab::Paint) {
                return false;
            }
            performPaintCommand(7, nowSeconds);
            return true;
        case InputActionId::PaintUndo:
            if (!menuVisible() || menuState.tab != PauseTab::Paint) {
                return false;
            }
            performPaintCommand(5, nowSeconds);
            return true;
        case InputActionId::PaintRedo:
            if (!menuVisible() || menuState.tab != PauseTab::Paint) {
                return false;
            }
            performPaintCommand(6, nowSeconds);
            return true;
        case InputActionId::PaintCommit:
            if (!menuVisible() || menuState.tab != PauseTab::Paint) {
                return false;
            }
            performPaintCommand(9, nowSeconds);
            return true;
        default:
            return false;
        }
    };

    const std::array<InputActionId, 10> triggerActions {
        InputActionId::ToggleCamera,
        InputActionId::ToggleMap,
        InputActionId::ToggleDebug,
        InputActionId::ResetFlight,
        InputActionId::PaintBrush,
        InputActionId::PaintErase,
        InputActionId::PaintFill,
        InputActionId::PaintUndo,
        InputActionId::PaintRedo,
        InputActionId::PaintCommit
    };

    auto triggerBoundActionsFromKey = [&](SDL_Scancode scancode, SDL_Keymod modifiers, float nowSeconds) -> bool {
        bool handled = false;
        for (InputActionId actionId : triggerActions) {
            if (controlActionTriggeredByKey(boot.controls, actionId, scancode, modifiers)) {
                handled = handleTriggeredAction(actionId, nowSeconds) || handled;
            }
        }
        return handled;
    };

    auto triggerBoundActionsFromMouseButton = [&](std::uint8_t button, SDL_Keymod modifiers, float nowSeconds) -> bool {
        bool handled = false;
        for (InputActionId actionId : triggerActions) {
            if (controlActionTriggeredByMouseButton(boot.controls, actionId, button, modifiers)) {
                handled = handleTriggeredAction(actionId, nowSeconds) || handled;
            }
        }
        return handled;
    };

    auto triggerBoundActionsFromWheel = [&](int wheelY, SDL_Keymod modifiers, float nowSeconds) -> bool {
        bool handled = false;
        for (InputActionId actionId : triggerActions) {
            if (controlActionTriggeredByWheel(boot.controls, actionId, wheelY, modifiers)) {
                handled = handleTriggeredAction(actionId, nowSeconds) || handled;
            }
        }
        return handled;
    };

    auto selectedRowSupportsHorizontalDrag = [&]() -> bool {
        switch (menuState.tab) {
        case PauseTab::Settings:
            if (menuState.settingsSubTab == SettingsSubTab::Flight || menuState.settingsSubTab == SettingsSubTab::Terrain) {
                return true;
            }
            return !menuSettingsRowDisabled(menuState.settingsSubTab, menuState.selectedIndex) &&
                !menuSettingsRowAction(menuState.settingsSubTab, menuState.selectedIndex);
        case PauseTab::Hud:
            return !hudRowDisabled(menuState.selectedIndex);
        case PauseTab::Characters:
            return menuState.selectedIndex >= 1 && menuState.selectedIndex <= 10;
        case PauseTab::Paint:
            return menuState.selectedIndex >= 0 && menuState.selectedIndex <= 4;
        default:
            return false;
        }
    };

    auto handleGamepadMenuBack = [&](float nowSeconds) {
        if (menuState.promptActive) {
            stopPromptTextInput();
            setPauseStatus(menuState, "Cancelled model path entry.", nowSeconds, 2.2f);
            return;
        }
        if (menuState.controlsCapturing) {
            clearControlBindingCapture(menuState);
            setPauseStatus(menuState, "Cancelled control capture.", nowSeconds, 2.0f);
            return;
        }
        if (menuState.confirmPending) {
            clearMenuConfirmation(menuState);
            setPauseStatus(menuState, "Cancelled pending confirmation.", nowSeconds, 2.0f);
            return;
        }
        if (screen == AppScreen::MainMenu) {
            if (menuState.tab != PauseTab::Main) {
                clearMenuInteractions();
                menuState.tab = PauseTab::Main;
                menuState.selectedIndex = 0;
                menuState.controlsSelection = 0;
                menuState.controlsSlot = 0;
                boot.paintUi.draggingCanvas = false;
                clampMenuSelection();
                setPauseStatus(menuState, "Returned to Home.", nowSeconds, 1.8f);
            }
            return;
        }
        clearControlBindingCapture(menuState);
        boot.paintUi.draggingCanvas = false;
        menuState.rowDragActive = false;
        setPauseActive(menuState, boot.uiState, false);
    };

    auto handleGamepadMenuInput = [&](float nowSeconds) {
        if (gamepad.handle == nullptr || !menuVisible()) {
            return;
        }

        if (menuState.promptActive) {
            if (gamepadButtonPressed(gamepad, SDL_GAMEPAD_BUTTON_SOUTH)) {
                submitModelPrompt(nowSeconds);
            } else if (gamepadButtonPressed(gamepad, SDL_GAMEPAD_BUTTON_EAST)) {
                handleGamepadMenuBack(nowSeconds);
            }
            return;
        }

        if (menuState.controlsCapturing) {
            if (gamepadButtonPressed(gamepad, SDL_GAMEPAD_BUTTON_EAST)) {
                handleGamepadMenuBack(nowSeconds);
            }
            return;
        }

        if (gamepadButtonPressed(gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) {
            cyclePauseTab(menuState, -1);
            boot.paintUi.draggingCanvas = false;
            clampMenuSelection();
        } else if (gamepadButtonPressed(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
            cyclePauseTab(menuState, 1);
            boot.paintUi.draggingCanvas = false;
            clampMenuSelection();
        }

        if (gamepadButtonPressed(gamepad, SDL_GAMEPAD_BUTTON_WEST) &&
            (menuState.tab == PauseTab::Settings || menuState.tab == PauseTab::Characters || menuState.tab == PauseTab::Paint)) {
            cyclePauseSubTab(menuState, -1);
            boot.paintUi.draggingCanvas = false;
            clampMenuSelection();
        } else if (gamepadButtonPressed(gamepad, SDL_GAMEPAD_BUTTON_NORTH) &&
                   (menuState.tab == PauseTab::Settings || menuState.tab == PauseTab::Characters || menuState.tab == PauseTab::Paint)) {
            cyclePauseSubTab(menuState, 1);
            boot.paintUi.draggingCanvas = false;
            clampMenuSelection();
        }

        const bool navUp =
            gamepadButtonDown(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP) ||
            gamepadAxisValue(gamepad, SDL_GAMEPAD_AXIS_LEFTY) <= -kGamepadMenuStickDeadzone;
        const bool navDown =
            gamepadButtonDown(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN) ||
            gamepadAxisValue(gamepad, SDL_GAMEPAD_AXIS_LEFTY) >= kGamepadMenuStickDeadzone;
        const bool navLeft =
            gamepadButtonDown(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT) ||
            gamepadAxisValue(gamepad, SDL_GAMEPAD_AXIS_LEFTX) <= -kGamepadMenuStickDeadzone;
        const bool navRight =
            gamepadButtonDown(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ||
            gamepadAxisValue(gamepad, SDL_GAMEPAD_AXIS_LEFTX) >= kGamepadMenuStickDeadzone;

        const int verticalMove = consumeRepeatedGamepadDirection(navUp, navDown, nowSeconds, gamepad.verticalRepeatDirection, gamepad.verticalRepeatAt);
        if (verticalMove != 0) {
            moveMenuSelection(verticalMove);
        }

        const int horizontalMove = consumeRepeatedGamepadDirection(navLeft, navRight, nowSeconds, gamepad.horizontalRepeatDirection, gamepad.horizontalRepeatAt);
        if (horizontalMove != 0) {
            adjustSelectedRow(horizontalMove, nowSeconds);
        }

        if (gamepadButtonPressed(gamepad, SDL_GAMEPAD_BUTTON_SOUTH)) {
            activateSelectedRow(nowSeconds);
        }
        if (gamepadButtonPressed(gamepad, SDL_GAMEPAD_BUTTON_EAST)) {
            handleGamepadMenuBack(nowSeconds);
        }
    };

    auto handleGamepadGameplayButtons = [&](float nowSeconds) {
        if (gamepad.handle == nullptr || menuVisible()) {
            return;
        }

        if (screen == AppScreen::InFlight && gamepadButtonPressed(gamepad, SDL_GAMEPAD_BUTTON_START)) {
            menuState.mode = MenuMode::PauseOverlay;
            clearControlBindingCapture(menuState);
            boot.paintUi.draggingCanvas = false;
            setPauseActive(menuState, boot.uiState, true);
            clampMenuSelection();
            return;
        }

        if (screen != AppScreen::InFlight || !session.has_value()) {
            return;
        }

        if (gamepadButtonPressed(gamepad, SDL_GAMEPAD_BUTTON_WEST)) {
            boot.uiState.chaseCamera = !boot.uiState.chaseCamera;
            queueSave(nowSeconds);
        }
        if (gamepadButtonPressed(gamepad, SDL_GAMEPAD_BUTTON_NORTH)) {
            if (session->flightMode) {
                switchSessionToWalking(nowSeconds, "Walking mode enabled.", false);
            } else {
                switchSessionToFlight(nowSeconds, session->plane.pos.x, session->plane.pos.z, "Flight mode restored.");
            }
        }
        if (session->flightMode &&
            gamepadButtonPressed(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) &&
            (nowSeconds - gamepad.lastProjectileTriggerAt) >= kGamepadProjectileCooldownSec) {
            gamepad.lastProjectileTriggerAt = nowSeconds;
            SDL_Log("Projectile fire input received (projectile spawning not implemented yet).");
        }
    };

    clampMenuSelection();
    ensureGamepadOpen(static_cast<float>(SDL_GetTicks()) * 0.001f, false);

    while (running) {
        const float uiNowSeconds = static_cast<float>(SDL_GetTicks()) * 0.001f;
        float frameMouseDx = 0.0f;
        float frameMouseDy = 0.0f;
        int throttleWheelSteps = 0;
        int trimWheelSteps = 0;

        SDL_Event event {};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
                continue;
            }

            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                windowHasFocus = false;
                boot.uiState.zoomHeld = false;
                boot.uiState.mapHeld = false;
                boot.uiState.mapUsedForZoom = false;
                continue;
            }

            if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
                windowHasFocus = true;
                continue;
            }

            if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
                if (gamepad.handle == nullptr) {
                    (void)openGamepad(event.gdevice.which, uiNowSeconds, true);
                }
                continue;
            }

            if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                if (gamepad.handle != nullptr && gamepad.instanceId == event.gdevice.which) {
                    closeGamepad();
                    setPauseStatus(menuState, "Controller disconnected.", uiNowSeconds, 2.4f);
                }
                continue;
            }

            if (event.type == SDL_EVENT_GAMEPAD_REMAPPED) {
                if (gamepad.handle != nullptr && gamepad.instanceId == event.gdevice.which) {
                    if (const char* gamepadName = SDL_GetGamepadName(gamepad.handle); gamepadName != nullptr) {
                        gamepad.name = gamepadName;
                    }
                }
                continue;
            }

            if (menuVisible() && menuState.promptActive) {
                if (event.type == SDL_EVENT_TEXT_INPUT) {
                    insertMenuPromptText(menuState, event.text.text);
                    continue;
                }

                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                    const SDL_Scancode scancode = event.key.scancode;
                    const SDL_Keymod modifiers = static_cast<SDL_Keymod>(event.key.mod);
                    if (scancode == SDL_SCANCODE_ESCAPE) {
                        stopPromptTextInput();
                        setPauseStatus(menuState, "Cancelled model path entry.", uiNowSeconds, 2.2f);
                    } else if (scancode == SDL_SCANCODE_RETURN || scancode == SDL_SCANCODE_KP_ENTER) {
                        submitModelPrompt(uiNowSeconds);
                    } else if (scancode == SDL_SCANCODE_LEFT) {
                        moveMenuPromptCursor(menuState, -1);
                    } else if (scancode == SDL_SCANCODE_RIGHT) {
                        moveMenuPromptCursor(menuState, 1);
                    } else if (scancode == SDL_SCANCODE_HOME) {
                        menuState.promptCursor = 0;
                    } else if (scancode == SDL_SCANCODE_END) {
                        menuState.promptCursor = static_cast<int>(menuState.promptText.size());
                    } else if (scancode == SDL_SCANCODE_BACKSPACE) {
                        (void)eraseMenuPromptText(menuState, true);
                    } else if (scancode == SDL_SCANCODE_DELETE) {
                        (void)eraseMenuPromptText(menuState, false);
                    } else if (scancode == SDL_SCANCODE_V && bindingModifiersMatch(SDL_KMOD_CTRL, modifiers)) {
                        if (char* clipboardText = SDL_GetClipboardText(); clipboardText != nullptr) {
                            std::string pasted(clipboardText);
                            SDL_free(clipboardText);
                            pasted.erase(std::remove(pasted.begin(), pasted.end(), '\r'), pasted.end());
                            pasted.erase(std::remove(pasted.begin(), pasted.end(), '\n'), pasted.end());
                            insertMenuPromptText(menuState, pasted);
                        }
                    }
                    continue;
                }

                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                    event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
                    event.type == SDL_EVENT_MOUSE_MOTION ||
                    event.type == SDL_EVENT_MOUSE_WHEEL) {
                    continue;
                }
            }

            if (menuState.controlsCapturing) {
                const int capturedActionIndex = menuState.controlsCaptureActionIndex;
                const int capturedSlotIndex = menuState.controlsCaptureSlot;

                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                    const SDL_Scancode scancode = event.key.scancode;
                    const SDL_Keymod modifiers = static_cast<SDL_Keymod>(event.key.mod);
                    if (captureBindingFromKey(menuState, boot.controls, scancode, modifiers)) {
                        queueSave(uiNowSeconds);
                        const ControlActionBinding& action = boot.controls.actions[static_cast<std::size_t>(capturedActionIndex)];
                        setPauseStatus(
                            menuState,
                            std::string("Bound ") + action.label + " to " +
                                formatInputBinding(action.slots[static_cast<std::size_t>(capturedSlotIndex)]) + ".",
                            uiNowSeconds,
                            2.8f);
                    } else if (!menuState.controlsCapturing && scancode == SDL_SCANCODE_ESCAPE) {
                        setPauseStatus(menuState, "Cancelled control capture.", uiNowSeconds, 2.0f);
                    }
                    continue;
                }

                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    const SDL_Keymod modifiers = SDL_GetModState();
                    if (captureBindingFromMouseButton(menuState, boot.controls, event.button.button, modifiers)) {
                        queueSave(uiNowSeconds);
                        const ControlActionBinding& action = boot.controls.actions[static_cast<std::size_t>(capturedActionIndex)];
                        setPauseStatus(
                            menuState,
                            std::string("Bound ") + action.label + " to " +
                                formatInputBinding(action.slots[static_cast<std::size_t>(capturedSlotIndex)]) + ".",
                            uiNowSeconds,
                            2.8f);
                    }
                    continue;
                }

                if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                    const SDL_Keymod modifiers = SDL_GetModState();
                    if (captureBindingFromWheel(menuState, boot.controls, static_cast<int>(event.wheel.y), modifiers)) {
                        queueSave(uiNowSeconds);
                        const ControlActionBinding& action = boot.controls.actions[static_cast<std::size_t>(capturedActionIndex)];
                        setPauseStatus(
                            menuState,
                            std::string("Bound ") + action.label + " to " +
                                formatInputBinding(action.slots[static_cast<std::size_t>(capturedSlotIndex)]) + ".",
                            uiNowSeconds,
                            2.8f);
                    }
                    continue;
                }

                if (event.type == SDL_EVENT_MOUSE_MOTION) {
                    const SDL_Keymod modifiers = SDL_GetModState();
                    if (captureBindingFromMouseMotion(menuState, boot.controls, static_cast<float>(event.motion.xrel), static_cast<float>(event.motion.yrel), modifiers)) {
                        queueSave(uiNowSeconds);
                        const ControlActionBinding& action = boot.controls.actions[static_cast<std::size_t>(capturedActionIndex)];
                        setPauseStatus(
                            menuState,
                            std::string("Bound ") + action.label + " to " +
                                formatInputBinding(action.slots[static_cast<std::size_t>(capturedSlotIndex)]) + ".",
                            uiNowSeconds,
                            2.8f);
                    }
                    continue;
                }
            }

            if (event.type == SDL_EVENT_DROP_FILE) {
                stopPromptTextInput();
                clearMenuInteractions();
                std::string loadStatus;
                const std::filesystem::path droppedPath =
                    event.drop.data != nullptr ? std::filesystem::path(event.drop.data) : std::filesystem::path {};
                CharacterSubTab dropRole = CharacterSubTab::Plane;
                if (menuVisible() && (menuState.tab == PauseTab::Characters || menuState.tab == PauseTab::Paint)) {
                    dropRole = activeRoleForTab(menuState, menuState.tab);
                }
                PlaneVisualState& dropVisual = visualForRole(dropRole, boot.planeVisual, boot.walkingVisual);
                const bool loaded = !droppedPath.empty() && loadPlaneModelFromPath(droppedPath, dropVisual, &loadStatus);
                fixBuiltinRoleLabel(dropRole, dropVisual);
                if (loadStatus.empty()) {
                    loadStatus = "Dropped file could not be processed.";
                }

                if (screen == AppScreen::InFlight) {
                    menuState.mode = MenuMode::PauseOverlay;
                    setPauseActive(menuState, boot.uiState, true);
                } else {
                    menuState.active = true;
                    menuState.mode = MenuMode::MainMenu;
                }
                menuState.tab = PauseTab::Characters;
                menuState.charactersSubTab = dropRole;
                menuState.paintSubTab = dropRole;
                menuState.selectedIndex = 0;
                menuState.controlsSelection = 0;
                clearControlBindingCapture(menuState);
                boot.paintUi.draggingCanvas = false;
                setPauseStatus(menuState, loadStatus, uiNowSeconds, loaded ? 3.5f : 4.5f);
                SDL_Log("%s", loadStatus.c_str());
                boot.assetCatalog = scanModelAssets();
                clampMenuSelection();
                if (loaded) {
                    queueSave(uiNowSeconds);
                }
                continue;
            }

            if (menuVisible() && event.type == SDL_EVENT_MOUSE_MOTION) {
                int menuWidth = 0;
                int menuHeight = 0;
                SDL_GetWindowSizeInPixels(window, &menuWidth, &menuHeight);
                const PauseLayout layout = buildPauseLayout(menuWidth, menuHeight);
                boot.paintUi.canvasRect = paintCanvasRect(layout);

                if (boot.paintUi.draggingCanvas) {
                    tryPaintCanvasStroke(
                        layout,
                        menuState,
                        boot.paintUi,
                        boot.planeVisual,
                        boot.walkingVisual,
                        static_cast<float>(event.motion.x),
                        static_cast<float>(event.motion.y),
                        false);
                    continue;
                }

                if (menuState.rowDragActive) {
                    constexpr float dragStepPixels = 18.0f;
                    float deltaX = static_cast<float>(event.motion.x) - menuState.rowDragLastX;
                    while (std::fabs(deltaX) >= dragStepPixels) {
                        const int direction = deltaX > 0.0f ? 1 : -1;
                        adjustSelectedRow(direction, uiNowSeconds);
                        menuState.rowDragLastX += dragStepPixels * static_cast<float>(direction);
                        deltaX = static_cast<float>(event.motion.x) - menuState.rowDragLastX;
                    }
                    continue;
                }

                if (menuState.tab == PauseTab::Controls) {
                    const int itemCount = static_cast<int>(boot.controls.actions.size());
                    const int startIndex = menuControlsVisibleStartIndex(menuState.controlsSelection, itemCount);
                    const int endIndex = std::min(itemCount, startIndex + kControlsVisibleRows);
                    bool hitSlot = false;
                    for (int index = startIndex; index < endIndex && !hitSlot; ++index) {
                        const int visibleIndex = index - startIndex;
                        for (int slotIndex = 0; slotIndex < 2; ++slotIndex) {
                            if (pointInRect(
                                    static_cast<float>(event.motion.x),
                                    static_cast<float>(event.motion.y),
                                    menuControlSlotRect(layout, visibleIndex, slotIndex))) {
                                menuState.controlsSelection = index;
                                menuState.controlsSlot = slotIndex;
                                hitSlot = true;
                                break;
                            }
                        }
                    }
                    if (hitSlot) {
                        continue;
                    }
                }

                if (menuState.tab == PauseTab::Help) {
                    continue;
                }

                const int itemIndex = menuHitItemIndex(
                    layout,
                    menuState,
                    boot.controls,
                    boot.assetCatalog.size(),
                    static_cast<float>(event.motion.x),
                    static_cast<float>(event.motion.y));
                if (itemIndex >= 0) {
                    setCurrentSelection(itemIndex);
                }
                continue;
            }

            if (menuVisible() && event.type == SDL_EVENT_MOUSE_WHEEL) {
                const SDL_Keymod modifiers = SDL_GetModState();
                const int wheelY = static_cast<int>(event.wheel.y);
                if (triggerBoundActionsFromWheel(wheelY, modifiers, uiNowSeconds)) {
                    continue;
                }

                const int adjustDirection = wheelY > 0 ? 1 : -1;
                const int moveDirection = wheelY > 0 ? -1 : 1;
                if (menuState.tab == PauseTab::Controls) {
                    moveMenuSelection(moveDirection);
                } else if (menuState.tab == PauseTab::Settings) {
                    const bool adjustable =
                        menuState.settingsSubTab == SettingsSubTab::Flight ||
                        menuState.settingsSubTab == SettingsSubTab::Terrain ||
                        (!menuSettingsRowDisabled(menuState.settingsSubTab, menuState.selectedIndex) &&
                         !menuSettingsRowAction(menuState.settingsSubTab, menuState.selectedIndex));
                    if (adjustable) {
                        adjustSelectedRow(adjustDirection, uiNowSeconds);
                    } else {
                        moveMenuSelection(moveDirection);
                    }
                } else if (menuState.tab == PauseTab::Hud) {
                    if (!hudRowDisabled(menuState.selectedIndex)) {
                        adjustSelectedRow(adjustDirection, uiNowSeconds);
                    } else {
                        moveMenuSelection(moveDirection);
                    }
                } else if (menuState.tab == PauseTab::Characters) {
                    if (menuState.selectedIndex >= 1 && menuState.selectedIndex <= 10) {
                        adjustSelectedRow(adjustDirection, uiNowSeconds);
                    } else {
                        moveMenuSelection(moveDirection);
                    }
                } else if (menuState.tab == PauseTab::Paint) {
                    if (menuState.selectedIndex >= 0 && menuState.selectedIndex <= 4) {
                        adjustSelectedRow(adjustDirection, uiNowSeconds);
                    } else {
                        moveMenuSelection(moveDirection);
                    }
                } else {
                    moveMenuSelection(moveDirection);
                }
                continue;
            }

            if (menuVisible() && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                int menuWidth = 0;
                int menuHeight = 0;
                SDL_GetWindowSizeInPixels(window, &menuWidth, &menuHeight);
                const PauseLayout layout = buildPauseLayout(menuWidth, menuHeight);
                const float mouseX = static_cast<float>(event.button.x);
                const float mouseY = static_cast<float>(event.button.y);

                const int tabIndex = hitPauseTabIndex(layout, mouseX, mouseY);
                if (tabIndex >= 0) {
                    clearMenuInteractions();
                    menuState.tab = static_cast<PauseTab>(tabIndex);
                    menuState.selectedIndex = 0;
                    menuState.controlsSelection = 0;
                    menuState.controlsSlot = 0;
                    boot.paintUi.draggingCanvas = false;
                    clampMenuSelection();
                    continue;
                }

                const int subTabIndex = hitPauseSubTabIndex(layout, menuState.tab, mouseX, mouseY);
                if (subTabIndex >= 0) {
                    clearMenuInteractions();
                    if (menuState.tab == PauseTab::Settings) {
                        menuState.settingsSubTab = static_cast<SettingsSubTab>(subTabIndex);
                    } else if (menuState.tab == PauseTab::Characters || menuState.tab == PauseTab::Paint) {
                        setActiveRoleForTab(menuState, menuState.tab, static_cast<CharacterSubTab>(subTabIndex));
                    }
                    menuState.selectedIndex = 0;
                    menuState.controlsSelection = 0;
                    menuState.controlsSlot = 0;
                    boot.paintUi.draggingCanvas = false;
                    clampMenuSelection();
                    continue;
                }

                if (event.button.button == SDL_BUTTON_LEFT &&
                    tryPaintCanvasStroke(layout, menuState, boot.paintUi, boot.planeVisual, boot.walkingVisual, mouseX, mouseY, true)) {
                    boot.paintUi.draggingCanvas = true;
                    continue;
                }

                if (menuState.tab == PauseTab::Controls && event.button.button == SDL_BUTTON_LEFT) {
                    const int itemCount = static_cast<int>(boot.controls.actions.size());
                    const int startIndex = menuControlsVisibleStartIndex(menuState.controlsSelection, itemCount);
                    const int endIndex = std::min(itemCount, startIndex + kControlsVisibleRows);
                    bool hitSlot = false;
                    for (int index = startIndex; index < endIndex && !hitSlot; ++index) {
                        const int visibleIndex = index - startIndex;
                        for (int slotIndex = 0; slotIndex < 2; ++slotIndex) {
                            if (pointInRect(mouseX, mouseY, menuControlSlotRect(layout, visibleIndex, slotIndex))) {
                                menuState.controlsSelection = index;
                                menuState.controlsSlot = slotIndex;
                                activateSelectedRow(uiNowSeconds);
                                hitSlot = true;
                                break;
                            }
                        }
                    }
                    if (hitSlot) {
                        continue;
                    }
                }

                const int itemIndex = menuHitItemIndex(layout, menuState, boot.controls, boot.assetCatalog.size(), mouseX, mouseY);
                if (itemIndex < 0) {
                    if (triggerBoundActionsFromMouseButton(event.button.button, SDL_GetModState(), uiNowSeconds)) {
                        continue;
                    }
                    continue;
                }

                setCurrentSelection(itemIndex, event.button.button == SDL_BUTTON_LEFT);
                if (event.button.button == SDL_BUTTON_RIGHT || event.button.button == SDL_BUTTON_MIDDLE) {
                    resetSelectedRow(uiNowSeconds);
                    continue;
                }
                if (event.button.button != SDL_BUTTON_LEFT) {
                    continue;
                }

                const RectF rowRect = menuRowRect(layout, menuState, boot.controls, boot.assetCatalog.size(), itemIndex);
                const int direction = mouseX < (rowRect.x + (rowRect.w * 0.5f)) ? -1 : 1;
                if (menuState.tab == PauseTab::Main) {
                    activateSelectedRow(uiNowSeconds);
                } else if (menuState.tab == PauseTab::Settings) {
                    if (menuState.settingsSubTab == SettingsSubTab::Graphics && menuState.selectedIndex == 2) {
                        activateSelectedRow(uiNowSeconds);
                    } else {
                        adjustSelectedRow(direction, uiNowSeconds);
                        if (selectedRowSupportsHorizontalDrag()) {
                            menuState.rowDragActive = true;
                            menuState.rowDragLastX = mouseX;
                        }
                    }
                } else if (menuState.tab == PauseTab::Hud) {
                    adjustSelectedRow(direction, uiNowSeconds);
                    if (selectedRowSupportsHorizontalDrag()) {
                        menuState.rowDragActive = true;
                        menuState.rowDragLastX = mouseX;
                    }
                } else if (menuState.tab == PauseTab::Characters) {
                    if (menuState.selectedIndex >= 1 && menuState.selectedIndex <= 10) {
                        adjustSelectedRow(direction, uiNowSeconds);
                        menuState.rowDragActive = true;
                        menuState.rowDragLastX = mouseX;
                    } else {
                        activateSelectedRow(uiNowSeconds);
                    }
                } else if (menuState.tab == PauseTab::Paint) {
                    if (menuState.selectedIndex <= 4) {
                        adjustSelectedRow(direction, uiNowSeconds);
                        menuState.rowDragActive = true;
                        menuState.rowDragLastX = mouseX;
                    } else {
                        activateSelectedRow(uiNowSeconds);
                    }
                } else if (menuState.tab == PauseTab::Controls) {
                    activateSelectedRow(uiNowSeconds);
                }
                continue;
            }

            if (menuVisible() && event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    boot.paintUi.draggingCanvas = false;
                    menuState.rowDragActive = false;
                }
                continue;
            }

            if (menuVisible() && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                const SDL_Scancode scancode = event.key.scancode;
                const SDL_Keymod modifiers = static_cast<SDL_Keymod>(event.key.mod);

                if (triggerBoundActionsFromKey(scancode, modifiers, uiNowSeconds)) {
                    continue;
                }

                if (scancode == SDL_SCANCODE_ESCAPE) {
                    if (menuState.confirmPending) {
                        clearMenuConfirmation(menuState);
                        setPauseStatus(menuState, "Cancelled pending confirmation.", uiNowSeconds, 2.0f);
                        continue;
                    }
                    clearControlBindingCapture(menuState);
                    boot.paintUi.draggingCanvas = false;
                    menuState.rowDragActive = false;
                    if (screen == AppScreen::MainMenu) {
                        running = false;
                    } else {
                        setPauseActive(menuState, boot.uiState, false);
                    }
                    continue;
                }

                if (scancode == SDL_SCANCODE_TAB || scancode == SDL_SCANCODE_H) {
                    cyclePauseTab(menuState, 1);
                    boot.paintUi.draggingCanvas = false;
                    clampMenuSelection();
                    continue;
                }

                if ((scancode == SDL_SCANCODE_Q || scancode == SDL_SCANCODE_LEFTBRACKET) &&
                    (menuState.tab == PauseTab::Settings || menuState.tab == PauseTab::Characters || menuState.tab == PauseTab::Paint)) {
                    cyclePauseSubTab(menuState, -1);
                    boot.paintUi.draggingCanvas = false;
                    clampMenuSelection();
                    continue;
                }

                if ((scancode == SDL_SCANCODE_E || scancode == SDL_SCANCODE_RIGHTBRACKET) &&
                    (menuState.tab == PauseTab::Settings || menuState.tab == PauseTab::Characters || menuState.tab == PauseTab::Paint)) {
                    cyclePauseSubTab(menuState, 1);
                    boot.paintUi.draggingCanvas = false;
                    clampMenuSelection();
                    continue;
                }

                if (scancode == SDL_SCANCODE_W || scancode == SDL_SCANCODE_UP) {
                    moveMenuSelection(-1);
                    continue;
                }

                if (scancode == SDL_SCANCODE_S || scancode == SDL_SCANCODE_DOWN) {
                    moveMenuSelection(1);
                    continue;
                }

                if (scancode == SDL_SCANCODE_A || scancode == SDL_SCANCODE_LEFT) {
                    adjustSelectedRow(-1, uiNowSeconds);
                    continue;
                }

                if (scancode == SDL_SCANCODE_D || scancode == SDL_SCANCODE_RIGHT) {
                    adjustSelectedRow(1, uiNowSeconds);
                    continue;
                }

                if (scancode == SDL_SCANCODE_BACKSPACE) {
                    if (menuState.tab == PauseTab::Controls) {
                        if (clearSelectedControlBindingSlot(menuState, boot.controls)) {
                            setPauseStatus(menuState, "Cleared selected binding slot.", uiNowSeconds, 2.2f);
                            queueSave(uiNowSeconds);
                        } else {
                            setPauseStatus(menuState, "This action cannot be rebound in the native runtime.", uiNowSeconds, 2.8f);
                        }
                        continue;
                    }
                    resetCurrentPage(uiNowSeconds);
                    continue;
                }

                if (scancode == SDL_SCANCODE_DELETE && menuState.tab == PauseTab::Controls) {
                    if (clearSelectedControlBindingSlot(menuState, boot.controls)) {
                        setPauseStatus(menuState, "Cleared selected binding slot.", uiNowSeconds, 2.2f);
                        queueSave(uiNowSeconds);
                    } else {
                        setPauseStatus(menuState, "This action cannot be rebound in the native runtime.", uiNowSeconds, 2.8f);
                    }
                    continue;
                }

                if (scancode == SDL_SCANCODE_R && menuState.tab == PauseTab::Controls) {
                    boot.controls = defaultControlProfileValues;
                    clearControlBindingCapture(menuState);
                    setPauseStatus(menuState, "Restored native control bindings.", uiNowSeconds, 2.6f);
                    queueSave(uiNowSeconds);
                    continue;
                }

                if (scancode == SDL_SCANCODE_F5 && menuState.tab == PauseTab::Characters) {
                    boot.assetCatalog = scanModelAssets();
                    clampMenuSelection();
                    setPauseStatus(menuState, "Refreshed portSource/Assets/Models catalog.", uiNowSeconds, 2.2f);
                    continue;
                }

                if (menuState.tab == PauseTab::Paint &&
                    (scancode == SDL_SCANCODE_PAGEUP || scancode == SDL_SCANCODE_PAGEDOWN)) {
                    adjustPaintRowValue(boot.paintUi, 2, scancode == SDL_SCANCODE_PAGEUP ? 1 : -1);
                    setPauseStatus(
                        menuState,
                        std::string("Brush Size: ") + std::to_string(boot.paintUi.brushSize) + " px",
                        uiNowSeconds,
                        1.8f);
                    continue;
                }

                if (scancode == SDL_SCANCODE_RETURN || scancode == SDL_SCANCODE_SPACE) {
                    activateSelectedRow(uiNowSeconds);
                    continue;
                }
            }

            if (event.type == SDL_EVENT_MOUSE_MOTION && screen == AppScreen::InFlight && mouseCaptured && !menuVisible()) {
                frameMouseDx += static_cast<float>(event.motion.xrel);
                frameMouseDy += static_cast<float>(event.motion.yrel);
                continue;
            }

            if (event.type == SDL_EVENT_MOUSE_WHEEL && screen == AppScreen::InFlight && !menuVisible()) {
                const SDL_Keymod modifiers = SDL_GetModState();
                const int wheelY = static_cast<int>(event.wheel.y);
                const bool trimUpTriggered = session.has_value() &&
                    session->flightMode &&
                    controlActionTriggeredByWheel(boot.controls, InputActionId::FlightTrimUp, wheelY, modifiers);
                const bool trimDownTriggered = session.has_value() &&
                    session->flightMode &&
                    controlActionTriggeredByWheel(boot.controls, InputActionId::FlightTrimDown, wheelY, modifiers);
                if (trimUpTriggered) {
                    trimWheelSteps += std::abs(wheelY);
                }
                if (trimDownTriggered) {
                    trimWheelSteps -= std::abs(wheelY);
                }

                if (!(trimUpTriggered || trimDownTriggered) && session.has_value() && session->flightMode) {
                    if (controlActionTriggeredByWheel(boot.controls, InputActionId::FlightThrottleUp, wheelY, modifiers)) {
                        throttleWheelSteps += std::abs(wheelY);
                    }
                    if (controlActionTriggeredByWheel(boot.controls, InputActionId::FlightThrottleDown, wheelY, modifiers)) {
                        throttleWheelSteps -= std::abs(wheelY);
                    }
                }
                (void)triggerBoundActionsFromWheel(wheelY, modifiers, uiNowSeconds);
                continue;
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && screen == AppScreen::InFlight && !menuVisible()) {
                (void)triggerBoundActionsFromMouseButton(event.button.button, SDL_GetModState(), uiNowSeconds);
                continue;
            }

            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && screen == AppScreen::InFlight && !menuVisible()) {
                const SDL_Scancode scancode = event.key.scancode;
                const SDL_Keymod modifiers = static_cast<SDL_Keymod>(event.key.mod);
                if (scancode == SDL_SCANCODE_ESCAPE) {
                    menuState.mode = MenuMode::PauseOverlay;
                    clearControlBindingCapture(menuState);
                    boot.paintUi.draggingCanvas = false;
                    setPauseActive(menuState, boot.uiState, true);
                    clampMenuSelection();
                    continue;
                }
                (void)triggerBoundActionsFromKey(scancode, modifiers, uiNowSeconds);
                continue;
            }
        }

        const std::uint64_t currentCounter = SDL_GetPerformanceCounter();
        const double dtSeconds = static_cast<double>(currentCounter - previousCounter) / counterFrequency;
        previousCounter = currentCounter;
        const float dt = clamp(static_cast<float>(dtSeconds), 1.0f / 240.0f, 0.05f);
        fpsSmoothed = mix(fpsSmoothed, 1.0f / std::max(dt, 1.0e-4f), 0.08f);
        refreshPauseStatus(menuState, uiNowSeconds);
        refreshMenuConfirmation(menuState, uiNowSeconds);
        clampHelpScroll();
        ensureGamepadOpen(uiNowSeconds, false);
        pollGamepadState(gamepad);
        handleGamepadGameplayButtons(uiNowSeconds);
        handleGamepadMenuInput(uiNowSeconds);

        const bool* keys = SDL_GetKeyboardState(nullptr);
        const SDL_Keymod modifiers = SDL_GetModState();
        const bool altHeld = keys[SDL_SCANCODE_LALT] != 0 || keys[SDL_SCANCODE_RALT] != 0;
        const SDL_MouseButtonFlags mouseButtons = SDL_GetMouseState(nullptr, nullptr);
        const float gamepadLeftX = gamepadAxisValue(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
        const float gamepadLeftY = gamepadAxisValue(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
        const float gamepadRightX = gamepadAxisValue(gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
        const float gamepadRightY = gamepadAxisValue(gamepad, SDL_GAMEPAD_AXIS_RIGHTY);
        const float gamepadLeftTrigger = gamepadAxisValue(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        const float gamepadRightTrigger = gamepadAxisValue(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        const bool wantsMouseCapture = windowHasFocus && screen == AppScreen::InFlight && !menuVisible() && !altHeld;
        if (wantsMouseCapture != mouseCaptured) {
            setMouseCapture(window, wantsMouseCapture);
            mouseCaptured = wantsMouseCapture;
        }

        boot.uiState.zoomHeld =
            screen == AppScreen::InFlight &&
            session.has_value() &&
            session->flightMode &&
            !menuVisible() &&
            (isControlActionHeld(boot.controls, InputActionId::FlightZoom, keys, mouseButtons, modifiers) ||
             gamepadButtonDown(gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));

        if (screen == AppScreen::InFlight && session.has_value()) {
            bool flightAfterburnerActive = false;
            if (!menuVisible()) {
                if (mouseCaptured) {
                    if (session->flightMode) {
                        applyFlightMouseInput(
                            boot.uiState,
                            boot.controls,
                            session->plane,
                            frameMouseDx,
                            frameMouseDy,
                            modifiers,
                            session->worldTime + dt);
                    } else {
                        applyWalkingMouseInput(
                            boot.uiState,
                            boot.controls,
                            session->plane,
                            session->walkYaw,
                            session->walkPitch,
                            frameMouseDx,
                            frameMouseDy,
                            modifiers);
                    }
                }
                if (session->flightMode) {
                    applyFlightGamepadLook(
                        boot.uiState,
                        session->flightLookYaw,
                        session->flightLookPitch,
                        gamepadRightX,
                        gamepadRightY,
                        dt);

                    const bool rudderLeftHeld = gamepadButtonDown(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
                    const bool rudderRightHeld = gamepadButtonDown(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
                    if (rudderLeftHeld || rudderRightHeld) {
                        gamepad.rudderLatched = true;
                    } else if (std::fabs(session->plane.yoke.yaw) <= 0.02f) {
                        gamepad.rudderLatched = false;
                    }

                    const float trimAxis =
                        (gamepadButtonDown(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP) ? 1.0f : 0.0f) -
                        (gamepadButtonDown(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN) ? 1.0f : 0.0f);
                    gamepad.trimAccumulator += trimAxis * kGamepadTrimStepsPerSecond * dt;
                    while (gamepad.trimAccumulator >= 1.0f) {
                        ++trimWheelSteps;
                        gamepad.trimAccumulator -= 1.0f;
                    }
                    while (gamepad.trimAccumulator <= -1.0f) {
                        --trimWheelSteps;
                        gamepad.trimAccumulator += 1.0f;
                    }
                } else {
                    gamepad.rudderLatched = false;
                    gamepad.trimAccumulator = 0.0f;
                    applyWalkingGamepadLook(
                        boot.uiState,
                        session->plane,
                        session->walkYaw,
                        session->walkPitch,
                        gamepadRightX,
                        gamepadRightY,
                        dt);
                }
                if (session->flightMode) {
                    adjustElevatorTrimFromWheel(boot.planeProfile.flightConfig, session->plane, trimWheelSteps);
                    adjustThrottleFromWheel(session->plane, throttleWheelSteps);
                }
            } else {
                gamepad.trimAccumulator = 0.0f;
            }

            InputState input {};
            WalkingInputState walkingInput {};
            if (!menuVisible()) {
                if (session->flightMode) {
                    const bool rudderLeftHeld = gamepadButtonDown(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
                    const bool rudderRightHeld = gamepadButtonDown(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
                    input.flightPitchDown = isControlActionHeld(boot.controls, InputActionId::FlightPitchDown, keys, mouseButtons, modifiers);
                    input.flightPitchUp = isControlActionHeld(boot.controls, InputActionId::FlightPitchUp, keys, mouseButtons, modifiers);
                    input.flightRollLeft = isControlActionHeld(boot.controls, InputActionId::FlightRollLeft, keys, mouseButtons, modifiers);
                    input.flightRollRight = isControlActionHeld(boot.controls, InputActionId::FlightRollRight, keys, mouseButtons, modifiers);
                    input.flightYawLeft = isControlActionHeld(boot.controls, InputActionId::FlightYawLeft, keys, mouseButtons, modifiers) || rudderLeftHeld;
                    input.flightYawRight = isControlActionHeld(boot.controls, InputActionId::FlightYawRight, keys, mouseButtons, modifiers) || rudderRightHeld;
                    input.flightThrottleUp = isControlActionHeld(boot.controls, InputActionId::FlightThrottleUp, keys, mouseButtons, modifiers);
                    input.flightThrottleDown = isControlActionHeld(boot.controls, InputActionId::FlightThrottleDown, keys, mouseButtons, modifiers);
                    input.flightAirBrakes =
                        isControlActionHeld(boot.controls, InputActionId::FlightAirBrakes, keys, mouseButtons, modifiers) ||
                        gamepadButtonDown(gamepad, SDL_GAMEPAD_BUTTON_EAST);
                    input.flightAfterburner = gamepadButtonDown(gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
                    input.flightUseAnalogYoke = std::fabs(gamepadLeftX) > 1.0e-4f || std::fabs(gamepadLeftY) > 1.0e-4f;
                    input.flightHoldYaw = gamepad.rudderLatched;
                    input.flightThrottleAnalog = clamp(gamepadRightTrigger - gamepadLeftTrigger, -1.0f, 1.0f);
                    input.flightPitchAnalog = -gamepadLeftY;
                    input.flightRollAnalog = -gamepadLeftX;
                    flightAfterburnerActive = input.flightAfterburner;
                } else {
                    walkingInput.forward = isControlActionHeld(boot.controls, InputActionId::WalkForward, keys, mouseButtons, modifiers);
                    walkingInput.backward = isControlActionHeld(boot.controls, InputActionId::WalkBackward, keys, mouseButtons, modifiers);
                    walkingInput.left = isControlActionHeld(boot.controls, InputActionId::WalkLeft, keys, mouseButtons, modifiers);
                    walkingInput.right = isControlActionHeld(boot.controls, InputActionId::WalkRight, keys, mouseButtons, modifiers);
                    walkingInput.sprint = isControlActionHeld(boot.controls, InputActionId::WalkSprint, keys, mouseButtons, modifiers);
                    walkingInput.jump =
                        isControlActionHeld(boot.controls, InputActionId::WalkJump, keys, mouseButtons, modifiers) ||
                        gamepadButtonDown(gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
                    walkingInput.forwardAxis = -gamepadLeftY;
                    walkingInput.rightAxis = gamepadLeftX;
                }
            }

            FlightEnvironment environment {};
            environment.wind = getWindVector3(session->windState);
            environment.groundHeightAt = [&terrainContext = session->terrainContext](float x, float z) {
                return sampleGroundHeight(x, z, terrainContext);
            };
            environment.waterHeightAt = [&terrainContext = session->terrainContext](float x, float z) {
                return sampleWaterHeight(x, z, terrainContext);
            };
            environment.sampleSdf = [&terrainContext = session->terrainContext](float x, float y, float z) {
                return sampleSdf(x, y, z, terrainContext);
            };
            environment.sampleNormal = [&terrainContext = session->terrainContext](float x, float y, float z) {
                return sampleTerrainNormal(x, y, z, terrainContext);
            };
            environment.collisionRadius = session->plane.collisionRadius;
            const TerrainVisualCache& terrainVisuals = ensureTerrainVisualCache(
                session->terrainCache,
                session->plane.pos,
                session->flightMode ? session->plane.flightVel : session->plane.vel,
                session->terrainContext,
                session->terrainChunkBakeCache,
                session->terrainWorldId);
            session->foliageImpactPulse *= clamp(1.0f - (dt * 5.0f), 0.0f, 1.0f);

            if (!menuVisible()) {
                updateCloudField(session->cloudField, session->windState, dt, session->worldTime + dt, session->plane.pos, session->worldRng);
                environment.wind = getWindVector3(session->windState);
                if (session->flightMode && !session->runtime.crashed) {
                    stepFlight(
                        session->plane,
                        session->runtime,
                        dt,
                        session->worldTime + dt,
                        input,
                        environment,
                        boot.planeProfile.flightConfig);
                    session->foliageBrushAmount = computeBrushContactAmount(
                        terrainVisuals,
                        session->plane.pos,
                        std::max(0.6f, session->plane.collisionRadius * 1.45f),
                        session->plane.pos.y - std::max(0.6f, session->plane.collisionRadius * 1.2f),
                        session->plane.pos.y + std::max(0.6f, session->plane.collisionRadius * 1.2f));
                    if (session->foliageBrushAmount > 0.0f) {
                        const float brushDrag = clamp(1.0f - (session->foliageBrushAmount * dt * 1.8f), 0.72f, 1.0f);
                        session->plane.flightVel *= brushDrag;
                    }

                    if (!session->runtime.hasPendingCrash) {
                        FlightCrashEvent propCrash {};
                        if (detectFlightPropCollision(
                                terrainVisuals,
                                session->terrainContext.params.decoration,
                                session->plane,
                                session->runtime.tick,
                                propCrash)) {
                            session->runtime.pendingCrash = propCrash;
                            session->runtime.hasPendingCrash = true;
                            session->runtime.crashed = true;
                            session->runtime.lastCrashTick = session->runtime.tick;
                            session->foliageImpactPulse = 1.0f;
                        }
                    }

                    if (session->runtime.hasPendingCrash) {
                        handleCrashTransition(session->runtime.pendingCrash, uiNowSeconds);
                        session->runtime.hasPendingCrash = false;
                    }
                } else if (!session->flightMode) {
                    stepWalking(
                        session->plane,
                        dt,
                        walkingInput,
                        session->terrainContext,
                        boot.uiState.walkingMoveSpeed,
                        &terrainVisuals,
                        &session->foliageBrushAmount);
                }
                session->worldTime += dt;
            }

            const ProceduralAudioFrame audioFrame = buildProceduralAudioFrame(
                session->plane,
                session->runtime,
                environment,
                session->terrainContext,
                boot.uiState,
                boot.planeProfile.propAudioConfig,
                session->foliageBrushAmount,
                session->foliageImpactPulse,
                windowHasFocus && session->flightMode && !session->runtime.crashed,
                menuVisible() || !session->flightMode,
                dt,
                flightAfterburnerActive);
            session->audioState.update(audioFrame);

            if (boot.preferencesDirty && uiNowSeconds >= boot.preferencesNextSaveAt) {
                std::string saveError;
                if (saveBootPreferences(boot, &saveError)) {
                    if (WorldStore* worldStore = currentWorldStore(); worldStore != nullptr) {
                        worldStore->flushDirty(nullptr);
                    }
                    boot.preferencesDirty = false;
                } else {
                    SDL_Log("Native preference save failed: %s", saveError.c_str());
                    boot.preferencesNextSaveAt = uiNowSeconds + 3.0f;
                }
            }

            const Camera renderCamera = buildRenderCamera(
                session->plane,
                session->terrainContext,
                boot.uiState,
                session->flightMode,
                session->flightMode ? session->flightLookYaw : 0.0f,
                session->flightMode ? session->flightLookPitch : 0.0f);
            const RendererLightingState lightingState = evaluateRendererLightingState(boot.lighting, boot.graphics.horizonFog);

            std::vector<RenderObject> opaqueObjects;
            opaqueObjects.reserve(((terrainVisuals.nearTiles.size() + terrainVisuals.farTiles.size()) * 2u) + 4u);
            std::vector<RenderObject> translucentObjects;
            translucentObjects.reserve(
                (terrainVisuals.nearTiles.size() + terrainVisuals.farTiles.size()) +
                (session->cloudField.groups.size() * 12u));

            for (const TerrainFarTile& tile : terrainVisuals.farTiles) {
                if (!tile.active) {
                    continue;
                }

                RenderObject terrainObject {
                    &tile.terrainModel,
                    {},
                    quatIdentity(),
                    { 1.0f, 1.0f, 1.0f },
                    { 1.0f, 1.0f, 1.0f },
                    1.0f,
                    450.0f,
                    9000.0f,
                    true,
                    true
                };
                applyFogSettings(terrainObject, boot.graphics, boot.lighting);
                opaqueObjects.push_back(terrainObject);

                if (!tile.propModel.faces.empty()) {
                    RenderObject propObject {
                        &tile.propModel,
                        {},
                        quatIdentity(),
                        { 1.0f, 1.0f, 1.0f },
                        { 1.0f, 1.0f, 1.0f },
                        1.0f,
                        450.0f,
                        9000.0f,
                        true,
                        true
                    };
                    applyFogSettings(propObject, boot.graphics, boot.lighting);
                    opaqueObjects.push_back(propObject);
                }

                if (!tile.waterModel.faces.empty()) {
                    RenderObject waterObject {
                        &tile.waterModel,
                        {},
                        quatIdentity(),
                        { 1.0f, 1.0f, 1.0f },
                        { 1.0f, 1.0f, 1.0f },
                        1.0f,
                        450.0f,
                        9000.0f,
                        false,
                        true
                    };
                    applyFogSettings(waterObject, boot.graphics, boot.lighting);
                    translucentObjects.push_back(waterObject);
                }
            }

            for (const TerrainFarTile& tile : terrainVisuals.nearTiles) {
                if (!tile.active) {
                    continue;
                }

                RenderObject terrainObject {
                    &tile.terrainModel,
                    {},
                    quatIdentity(),
                    { 1.0f, 1.0f, 1.0f },
                    { 1.0f, 1.0f, 1.0f },
                    1.0f,
                    120.0f,
                    3200.0f,
                    false,
                    true
                };
                applyFogSettings(terrainObject, boot.graphics, boot.lighting);
                opaqueObjects.push_back(terrainObject);

                if (!tile.propModel.faces.empty()) {
                    RenderObject propObject {
                        &tile.propModel,
                        {},
                        quatIdentity(),
                        { 1.0f, 1.0f, 1.0f },
                        { 1.0f, 1.0f, 1.0f },
                        1.0f,
                        120.0f,
                        3200.0f,
                        false,
                        true
                    };
                    applyFogSettings(propObject, boot.graphics, boot.lighting);
                    opaqueObjects.push_back(propObject);
                }

                if (!tile.waterModel.faces.empty()) {
                    RenderObject waterObject {
                        &tile.waterModel,
                        {},
                        quatIdentity(),
                        { 1.0f, 1.0f, 1.0f },
                        { 1.0f, 1.0f, 1.0f },
                        1.0f,
                        120.0f,
                        3200.0f,
                        false,
                        true
                    };
                    applyFogSettings(waterObject, boot.graphics, boot.lighting);
                    translucentObjects.push_back(waterObject);
                }
            }

            const PlaneVisualState& activeVisual = session->flightMode ? boot.planeVisual : boot.walkingVisual;
            const Quat actorRenderRotation = quatNormalize(quatMultiply(session->plane.rot, composeVisualRotationOffset(activeVisual)));
            const Vec3 actorRenderPosition = session->plane.pos + rotateVector(actorRenderRotation, activeVisual.modelOffset);
            if (boot.uiState.chaseCamera) {
                RenderObject actorObject {
                    &activeVisual.model,
                    actorRenderPosition,
                    actorRenderRotation,
                    { activeVisual.scale, activeVisual.scale, activeVisual.scale },
                    { 1.0f, 1.0f, 1.0f },
                    1.0f,
                    400.0f,
                    2600.0f,
                    true
                };
                applyFogSettings(actorObject, boot.graphics, boot.lighting);
                opaqueObjects.push_back(actorObject);
            }

            if (menuVisible() && (menuState.tab == PauseTab::Characters || menuState.tab == PauseTab::Paint)) {
                const PlaneVisualState& previewVisual =
                    menuState.tab == PauseTab::Paint
                        ? visualForRole(menuState.paintSubTab, boot.planeVisual, boot.walkingVisual)
                        : visualForRole(menuState.charactersSubTab, boot.planeVisual, boot.walkingVisual);
                const Vec3 cameraForward = forwardFromRotation(renderCamera.rot);
                const Vec3 cameraRight = rightFromRotation(renderCamera.rot);
                const Vec3 cameraUp = upFromRotation(renderCamera.rot);
                const Quat previewSpin = previewVisual.previewAutoSpin
                    ? quatFromAxisAngle({ 0.0f, 1.0f, 0.0f }, uiNowSeconds * 0.8f)
                    : quatIdentity();
                const Quat previewRotation = quatNormalize(quatMultiply(previewSpin, composeVisualRotationOffset(previewVisual)));
                const Vec3 previewAnchor = renderCamera.pos + (cameraForward * 34.0f) + (cameraRight * 18.0f) - (cameraUp * 6.0f);
                const Vec3 previewPosition = previewAnchor + rotateVector(previewRotation, previewVisual.modelOffset);

                RenderObject previewObject {
                    &previewVisual.model,
                    previewPosition,
                    previewRotation,
                    {
                        previewVisual.scale * previewVisual.previewZoom,
                        previewVisual.scale * previewVisual.previewZoom,
                        previewVisual.scale * previewVisual.previewZoom
                    },
                    { 1.0f, 1.0f, 1.0f },
                    1.0f,
                    40.0f,
                    160.0f,
                    true
                };
                applyFogSettings(previewObject, boot.graphics, boot.lighting);
                opaqueObjects.push_back(previewObject);
            }

            const Model cloudModel = makeOctahedronModel();
            for (const CloudGroup& group : session->cloudField.groups) {
                for (const CloudPuff& puff : group.puffs) {
                    RenderObject cloudObject {
                        &cloudModel,
                        group.center + puff.offset + Vec3 { 0.0f, std::sin((session->worldTime * 0.2f) + puff.bobPhase) * puff.bobAmplitude, 0.0f },
                        quatFromAxisAngle({ 0.0f, 1.0f, 0.0f }, puff.yaw),
                        { puff.scale, puff.scale * puff.stretchY, puff.scale },
                        puff.color,
                        0.78f,
                        500.0f,
                        3000.0f,
                        true
                    };
                    applyFogSettings(cloudObject, boot.graphics, boot.lighting);
                    translucentObjects.push_back(cloudObject);
                }
            }

            if (boot.lighting.showSunMarker) {
                static Model sunMarkerModel = [] {
                    Model model = makeCubeModel();
                    model.assetKey = "builtin:sun_marker";
                    return model;
                }();
                const float markerDistance = std::max(80.0f, boot.lighting.markerDistance);
                const float markerSize = std::max(1.0f, boot.lighting.markerSize);
                RenderObject sunMarker {
                    &sunMarkerModel,
                    renderCamera.pos + (lightingState.sunDirection * markerDistance),
                    quatIdentity(),
                    { markerSize, markerSize, markerSize },
                    lightingState.lightColor,
                    1.0f,
                    10.0f,
                    std::max(200.0f, markerDistance + markerSize + 50.0f),
                    false
                };
                applyFogSettings(sunMarker, boot.graphics, boot.lighting);
                opaqueObjects.push_back(sunMarker);
            }

            std::sort(translucentObjects.begin(), translucentObjects.end(), [&renderCamera](const RenderObject& lhs, const RenderObject& rhs) {
                return lengthSquared(lhs.pos - renderCamera.pos) > lengthSquared(rhs.pos - renderCamera.pos);
            });

            int drawableWidth = 0;
            int drawableHeight = 0;
            SDL_GetWindowSizeInPixels(window, &drawableWidth, &drawableHeight);
            boot.hudCanvas.resize(drawableWidth, drawableHeight);
            boot.hudCanvas.clear({ 0, 0, 0, 0 });
            drawHud(
                boot.hudCanvas,
                drawableWidth,
                drawableHeight,
                session->plane,
                session->runtime,
                session->terrainContext,
                boot.geoConfig,
                session->windState,
                boot.uiState,
                boot.hud,
                session->flightMode ? boot.planeVisual.label : boot.walkingVisual.label,
                rendererLabel,
                fpsSmoothed,
                mouseCaptured,
                session->flightMode);
            if (menuVisible()) {
                drawMenuOverlay(
                    boot.hudCanvas,
                    drawableWidth,
                    drawableHeight,
                    menuState,
                    boot.uiState,
                    boot.graphics,
                    boot.lighting,
                    boot.hud,
                    boot.controls,
                    boot.planeProfile.flightConfig,
                    boot.planeProfile.propAudioConfig,
                    boot.terrainParams,
                    boot.assetCatalog,
                    boot.planeVisual,
                    boot.walkingVisual,
                    boot.paintUi);
            }

            std::string renderError;
            if (!nativeRenderer.render(
                    renderCamera,
                    lightingState,
                    opaqueObjects,
                    translucentObjects,
                    boot.hudCanvas,
                    &renderError)) {
                SDL_Log("Vulkan render failed: %s", renderError.c_str());
                running = false;
            }
        } else {
            if (boot.preferencesDirty && uiNowSeconds >= boot.preferencesNextSaveAt) {
                std::string saveError;
                if (saveBootPreferences(boot, &saveError)) {
                    boot.preferencesDirty = false;
                } else {
                    SDL_Log("Native preference save failed: %s", saveError.c_str());
                    boot.preferencesNextSaveAt = uiNowSeconds + 3.0f;
                }
            }

            int drawableWidth = 0;
            int drawableHeight = 0;
            SDL_GetWindowSizeInPixels(window, &drawableWidth, &drawableHeight);
            boot.hudCanvas.resize(drawableWidth, drawableHeight);
            boot.hudCanvas.clear({ 0, 0, 0, 0 });
            drawMenuOverlay(
                boot.hudCanvas,
                drawableWidth,
                drawableHeight,
                menuState,
                boot.uiState,
                boot.graphics,
                boot.lighting,
                boot.hud,
                boot.controls,
                boot.planeProfile.flightConfig,
                boot.planeProfile.propAudioConfig,
                boot.terrainParams,
                boot.assetCatalog,
                boot.planeVisual,
                boot.walkingVisual,
                boot.paintUi);

            std::vector<RenderObject> opaqueObjects;
            std::vector<RenderObject> translucentObjects;
            const PlaneVisualState& previewVisual =
                menuState.tab == PauseTab::Paint
                    ? visualForRole(menuState.paintSubTab, boot.planeVisual, boot.walkingVisual)
                    : (menuState.tab == PauseTab::Characters
                        ? visualForRole(menuState.charactersSubTab, boot.planeVisual, boot.walkingVisual)
                        : boot.planeVisual);
            const Quat previewSpin = previewVisual.previewAutoSpin
                ? quatFromAxisAngle({ 0.0f, 1.0f, 0.0f }, uiNowSeconds * 0.8f)
                : quatIdentity();
            const Quat previewRotation = quatNormalize(quatMultiply(previewSpin, composeVisualRotationOffset(previewVisual)));
            const Vec3 previewPosition = Vec3 { 0.0f, -2.5f, 18.0f } + rotateVector(previewRotation, previewVisual.modelOffset);
            RenderObject previewObject {
                &previewVisual.model,
                previewPosition,
                previewRotation,
                {
                    previewVisual.scale * previewVisual.previewZoom,
                    previewVisual.scale * previewVisual.previewZoom,
                    previewVisual.scale * previewVisual.previewZoom
                },
                { 1.0f, 1.0f, 1.0f },
                1.0f,
                80.0f,
                2600.0f,
                true
            };
            applyFogSettings(previewObject, boot.graphics, boot.lighting);
            opaqueObjects.push_back(previewObject);

            Camera previewCamera {};
            previewCamera.pos = { 0.0f, 0.0f, -22.0f };
            previewCamera.rot = quatIdentity();
            previewCamera.fovRadians = radians(std::clamp(boot.uiState.cameraFovDegrees, 60.0f, 100.0f));
            const RendererLightingState lightingState = evaluateRendererLightingState(boot.lighting, boot.graphics.horizonFog);
            if (boot.lighting.showSunMarker) {
                static Model sunMarkerModel = [] {
                    Model model = makeCubeModel();
                    model.assetKey = "builtin:sun_marker";
                    return model;
                }();
                const float markerDistance = std::max(80.0f, boot.lighting.markerDistance);
                const float markerSize = std::max(1.0f, boot.lighting.markerSize);
                RenderObject sunMarker {
                    &sunMarkerModel,
                    previewCamera.pos + (lightingState.sunDirection * markerDistance),
                    quatIdentity(),
                    { markerSize, markerSize, markerSize },
                    lightingState.lightColor,
                    1.0f,
                    10.0f,
                    std::max(200.0f, markerDistance + markerSize + 50.0f),
                    false
                };
                applyFogSettings(sunMarker, boot.graphics, boot.lighting);
                opaqueObjects.push_back(sunMarker);
            }

            std::string renderError;
            if (!nativeRenderer.render(
                    previewCamera,
                    lightingState,
                    opaqueObjects,
                    translucentObjects,
                    boot.hudCanvas,
                    &renderError)) {
                SDL_Log("Vulkan render failed: %s", renderError.c_str());
                running = false;
            }
        }
    }

    if (boot.preferencesDirty) {
        std::string saveError;
        if (!saveBootPreferences(boot, &saveError)) {
            SDL_Log("Native preference save on shutdown failed: %s", saveError.c_str());
        }
    }

    if (session.has_value()) {
        shutdownGameSession(*session);
        session.reset();
    }

    if (menuState.promptActive) {
        SDL_StopTextInput(window);
    }
    closeGamepad();
    nativeRenderer.shutdown();
    SDL_DestroyWindow(window);
    if (audioSubsystemReady) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    if (gamepadSubsystemReady) {
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    }
    SDL_Quit();
    return 0;
}
