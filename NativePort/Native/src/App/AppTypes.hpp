#pragma once

#include <SDL3/SDL.h>

#include "NativeGame/Flight.hpp"
#include "NativeGame/GltfLoader.hpp"
#include "NativeGame/Hash.hpp"
#include "NativeGame/HostedNetworking.hpp"
#include "NativeGame/HudCanvas.hpp"
#include "NativeGame/ProceduralAudio.hpp"
#include "NativeGame/RenderTypes.hpp"
#include "NativeGame/StlLoader.hpp"
#include "NativeGame/SteamSupport.hpp"
#include "NativeGame/TerrainChunkBakeCache.hpp"
#include "NativeGame/VulkanRenderer.hpp"
#include "NativeGame/World.hpp"
#include "NativeGame/WorldStore.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace TrueFlightApp
{

    using namespace NativeGame;

    inline constexpr float kWalkingSpeedUnitsPerSecond = 10.0f;
    inline const float kWalkingPitchLimitRadians = radians(89.0f);
    inline constexpr float kWalkingHalfHeight = 1.8f;
    inline constexpr float kWalkingCollisionRadius = 0.55f;
    inline constexpr float kPlaneHullMaxStrength = 100.0f;
    inline constexpr float kPlaneFuselageMaxStrength = 100.0f;
    inline constexpr float kPlaneWearMax = 100.0f;
    inline constexpr std::uint64_t kMiB = 1024ull * 1024ull;
    inline constexpr std::uint64_t kGiB = 1024ull * 1024ull * 1024ull;

    enum class AppScreen
    {
        BootLoading = 0,
        MainMenu = 1,
        WorldLoading = 2,
        InFlight = 3
    };

    enum class MenuMode
    {
        MainMenu = 0,
        PauseOverlay = 1
    };

    enum class WindowMode
    {
        Windowed = 0,
        Borderless = 1,
        Fullscreen = 2
    };

    enum class ChaseCameraMode
    {
        Close = 0,
        Far = 1,
        Dynamic = 2,
        SoftCentered = 3
    };

    enum class PauseTab
    {
        Main = 0,
        Settings = 1,
        Characters = 2,
        Paint = 3,
        Hud = 4,
        Instruments = 5,
        Controls = 6,
        Help = 7
    };

    enum class SettingsSubTab
    {
        Graphics = 0,
        Camera = 1,
        Sound = 2,
        Flight = 3,
        Terrain = 4,
        Lighting = 5,
        Online = 6
    };

    enum class CharacterSubTab
    {
        Plane = 0,
        Player = 1,
        Enemy = 2,
        Target = 3
    };

    enum class CharacterEditorMode
    {
        Model = 0,
        Rig = 1
    };

    enum class HudSubTab
    {
        Info = 0,
        Speedometer = 1,
        Controls = 2,
        Map = 3,
        Crosshair = 4,
        Debug = 5
    };

    enum class InstrumentSubTab
    {
        Panel = 0,
        Airspeed = 1,
        Attitude = 2,
        Altimeter = 3,
        TurnCoordinator = 4,
        Heading = 5,
        VerticalSpeed = 6
    };

    enum class PaintMode
    {
        Brush = 0,
        Erase = 1
    };

    enum class MenuPromptMode
    {
        None = 0,
        ModelPath = 1,
        WorldName = 2
    };

    enum class BindingKind
    {
        None = 0,
        Key = 1,
        MouseButton = 2,
        MouseAxis = 3,
        MouseWheel = 4
    };

    enum class InputActionId
    {
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
        FlightRudderTrimLeft = 12,
        FlightRudderTrimRight = 13,
        ToggleCamera = 14,
        ToggleMap = 15,
        ToggleDebug = 16,
        ResetFlight = 17,
        PaintBrush = 18,
        PaintErase = 19,
        PaintFill = 20,
        PaintUndo = 21,
        PaintRedo = 22,
        PaintCommit = 23,
        WalkLookDown = 24,
        WalkLookUp = 25,
        WalkLookLeft = 26,
        WalkLookRight = 27,
        WalkSprint = 28,
        WalkJump = 29,
        WalkForward = 30,
        WalkBackward = 31,
        WalkLeft = 32,
        WalkRight = 33,
        VoicePushToTalk = 34,
        Count = 35
    };

    struct GraphicsSettings
    {
        WindowMode windowMode = WindowMode::Windowed;
        int resolutionWidth = 1920;
        int resolutionHeight = 1080;
        float renderScale = 1.0f;
        float drawDistance = 5000.0f;
        bool horizonFog = true;
        bool textureMipmaps = true;
        bool vsync = false;
    };

    struct LightingSettings
    {
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
        Vec3 sunTint{1.0f, 0.95f, 0.86f};
        Vec3 skyTint{1.0f, 1.0f, 1.0f};
        Vec3 groundTint{1.0f, 1.0f, 1.0f};
        Vec3 fogColor{0.64f, 0.73f, 0.84f};
    };

    struct HudRgbColor
    {
        int r = 255;
        int g = 255;
        int b = 255;
    };

    struct HudElementStyle
    {
        float x = 0.0f;
        float y = 0.0f;
        float widthScale = 1.0f;
        float heightScale = 1.0f;
        HudRgbColor backgroundColor{5, 10, 18};
        int backgroundOpacity = 178;
        HudRgbColor accentColor{170, 210, 255};
        int accentOpacity = 255;
        HudRgbColor textColor{230, 240, 255};
        int textOpacity = 255;
    };

    struct InstrumentPanelSettings
    {
        bool showTitle = true;
        float panelWidth = 278.0f;
        float panelHeight = 190.0f;
        float titleX = 10.0f;
        float titleY = 8.0f;
        float titleScale = 1.0f;
        HudRgbColor titleColor{230, 240, 255};
        int titleOpacity = 255;
    };

    struct InstrumentDialStyle
    {
        bool visible = true;
        float centerX = 0.0f;
        float centerY = 0.0f;
        float dialSize = 82.0f;
        float ringRadiusScale = 0.40f;
        float annotationScale = 1.0f;
        float captionScale = 1.0f;
        float valueScale = 1.0f;
        float captionOffsetY = 8.0f;
        float valueOffsetY = 6.0f;
        HudRgbColor faceColor{8, 14, 22};
        int faceOpacity = 220;
        HudRgbColor accentColor{170, 210, 255};
        int accentOpacity = 228;
        HudRgbColor textColor{230, 240, 255};
        int textOpacity = 255;
        HudRgbColor valueColor{230, 240, 255};
        int valueOpacity = 255;
        HudRgbColor needleColor{110, 228, 182};
        int needleOpacity = 255;
        HudRgbColor warningColor{255, 118, 96};
        int warningOpacity = 255;
    };

    struct AirspeedIndicatorSettings
    {
        InstrumentDialStyle style{
            true, 48.0f, 49.0f, 82.0f, 0.46f, 0.55f, 0.70f, 0.75f, 4.0f, 21.0f, {8, 14, 22}, 220, {170, 210, 255}, 228, {230, 240, 255}, 255, {230, 240, 255}, 255, {255, 115, 100}, 255, {255, 118, 96}, 255};
        int maxKph = 420;
        int minorStepKph = 5;
        int majorStepKph = 20;
        int labelStepKph = 60;
        int redlineKph = 320;
        float startAngleDegrees = 140.0f;
        float sweepDegrees = 260.0f;
        float unitScale = 0.5f;
        float unitOffsetY = 5.0f;
    };

    struct AttitudeIndicatorSettings
    {
        InstrumentDialStyle style{
            true, 139.0f, 49.0f, 82.0f, 0.48f, 1.0f, 0.65f, 1.0f, 2.0f, 6.0f, {8, 14, 22}, 255, {170, 210, 255}, 228, {230, 240, 255}, 255, {230, 240, 255}, 255, {255, 214, 96}, 255, {255, 118, 96}, 255};
        float pitchPixelsPerDegree = 1.0f;
        int ladderMinorStepDegrees = 5;
        int ladderMajorStepDegrees = 10;
        float maxDisplayedPitchDegrees = 30.0f;
        float bankSmoothing = 0.02f;
    };

    struct AltimeterSettings
    {
        InstrumentDialStyle style{
            true, 230.0f, 49.0f, 82.0f, 0.48f, 0.75f, 0.65f, 0.80f, 2.0f, 11.0f, {8, 14, 22}, 220, {170, 210, 255}, 228, {230, 240, 255}, 255, {230, 240, 255}, 255, {110, 228, 182}, 255, {255, 214, 96}, 255};
        float digitRadiusScale = 0.72f;
        float hundredsPerTurnFeet = 1000.0f;
        float thousandsPerTurnFeet = 10000.0f;
        float tenThousandsPerTurnFeet = 100000.0f;
    };

    struct TurnCoordinatorSettings
    {
        InstrumentDialStyle style{
            true, 48.0f, 141.0f, 82.0f, 0.46f, 1.0f, 0.70f, 1.0f, 4.0f, 6.0f, {8, 14, 22}, 220, {170, 210, 255}, 228, {230, 240, 255}, 255, {230, 240, 255}, 255, {255, 214, 96}, 255, {255, 118, 96}, 255};
        float turnRateFullScaleDegreesPerSecond = 4.5f;
        float slipFullScaleDegrees = 16.0f;
        float needleTravel = 26.0f;
        float ballTravel = 12.0f;
    };

    struct HeadingIndicatorSettings
    {
        InstrumentDialStyle style{
            true, 139.0f, 141.0f, 82.0f, 0.44f, 0.50f, 0.60f, 0.85f, 6.0f, -5.0f, {8, 14, 22}, 220, {170, 210, 255}, 228, {230, 240, 255}, 255, {230, 240, 255}, 255, {255, 214, 96}, 255, {255, 118, 96}, 255};
        int minorStepDegrees = 5;
        int majorStepDegrees = 30;
        int labelStepDegrees = 30;
    };

    struct VerticalSpeedIndicatorSettings
    {
        InstrumentDialStyle style{
            true, 230.0f, 141.0f, 82.0f, 0.48f, 0.60f, 0.65f, 0.80f, 3.0f, -15.0f, {8, 14, 22}, 220, {170, 210, 255}, 228, {230, 240, 255}, 255, {230, 240, 255}, 255, {255, 170, 165}, 255, {255, 118, 96}, 255};
        int maxFpm = 5000;
        int tickStepFpm = 500;
        int labelStepFpm = 1000;
        float sweepDegrees = 120.0f;
    };

    struct SixPackSettings
    {
        InstrumentPanelSettings panel{};
        AirspeedIndicatorSettings airspeed{};
        AttitudeIndicatorSettings attitude{};
        AltimeterSettings altimeter{};
        TurnCoordinatorSettings turnCoordinator{};
        HeadingIndicatorSettings heading{};
        VerticalSpeedIndicatorSettings verticalSpeed{};
    };

    struct HudSettings
    {
        bool showInfoPanel = true;
        bool showSpeedometer = true;
        bool showDebug = true;
        bool showThrottle = true;
        bool showControls = true;
        bool showMap = false;
        bool showGeoInfo = true;
        bool showCrosshair = true;
        bool showPeerIndicators = true;

        int speedometerMaxKph = 420;
        int speedometerMinorStepKph = 5;
        int speedometerMajorStepKph = 20;
        int speedometerLabelStepKph = 60;
        int speedometerRedlineKph = 320;

        HudElementStyle infoPanel{0.0f, 0.0f, 1.55f, 0.95f, {5, 10, 18}, 120, {170, 210, 255}, 255, {230, 240, 255}, 255};
        HudElementStyle speedometer{0.36f, 0.74f, 1.95f, 1.95f, {8, 14, 22}, 105, {170, 210, 255}, 228, {230, 240, 255}, 255};
        HudElementStyle controls{0.28f, 0.91f, 1.0f, 1.0f, {7, 12, 18}, 178, {175, 214, 255}, 230, {220, 234, 255}, 255};
        HudElementStyle mapPanel{0.834f, 0.022f, 1.0f, 1.0f, {6, 12, 18}, 190, {255, 255, 255}, 240, {230, 240, 255}, 255};
        HudElementStyle crosshair{0.5f, 0.5f, 1.0f, 1.0f, {0, 0, 0}, 0, {255, 92, 54}, 220, {255, 92, 54}, 220};
        HudElementStyle debugFooter{0.0f, 0.86f, 0.65f, 0.85f, {5, 10, 18}, 145, {170, 210, 255}, 0, {230, 240, 255}, 255};

        SixPackSettings instruments{};
    };

    struct LoadingUiState
    {
        bool active = false;
        std::string stage = "Booting";
        std::string detail;
        float progress = 0.0f;
        float startedAt = 0.0f;
        float completedAt = 0.0f;
        int currentEntry = 0;
        std::vector<std::string> entries;
    };

    struct InputBinding
    {
        BindingKind kind = BindingKind::None;
        SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN;
        std::uint8_t mouseButton = 0;
        char axis = 'x';
        int direction = 0;
        SDL_Keymod modifiers = SDL_KMOD_NONE;
    };

    struct ControlActionBinding
    {
        InputActionId id = InputActionId::FlightPitchDown;
        const char *label = "";
        const char *help = "";
        bool configurable = true;
        bool supported = true;
        std::array<InputBinding, 2> slots{};
    };

    struct ControlProfile
    {
        std::vector<ControlActionBinding> actions;
    };

    struct UiState
    {
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
        bool scaleHudWithUi = false;
        int mapZoomIndex = 2;
        float cameraFovDegrees = 82.0f;
        float uiScale = 1.0f;
        float walkingMoveSpeed = kWalkingSpeedUnitsPerSecond;
        float mouseSensitivity = 1.0f;
        float masterVolume = 1.0f;
        float engineVolume = 1.0f;
        float ambienceVolume = 1.0f;
        float combatVolume = 1.0f;
        float flybyVolume = 1.0f;
        std::array<float, 5> mapZoomExtents{200.0f, 400.0f, 800.0f, 1600.0f, 3200.0f};
        ChaseCameraMode chaseCameraMode = ChaseCameraMode::Dynamic;
    };

    struct PauseState
    {
        bool active = false;
        MenuMode mode = MenuMode::PauseOverlay;
        PauseTab tab = PauseTab::Main;
        int selectedIndex = 0;
        SettingsSubTab settingsSubTab = SettingsSubTab::Graphics;
        HudSubTab hudSubTab = HudSubTab::Info;
        InstrumentSubTab instrumentSubTab = InstrumentSubTab::Panel;
        CharacterSubTab charactersSubTab = CharacterSubTab::Plane;
        CharacterSubTab paintSubTab = CharacterSubTab::Plane;
        CharacterEditorMode characterEditorMode = CharacterEditorMode::Model;
        int characterRigSlot = 0;
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

    struct WorldInstanceSummary
    {
        std::string worldId = "native_default";
        int seed = 1;
        float chunkSize = 128.0f;
        float worldRadius = 2048.0f;
        float waterLevel = -12.0f;
        int tunnelCount = 0;
        std::string createdAt;
        std::string updatedAt;
        std::uintmax_t cacheBytes = 0u;
        bool persistent = false;
    };

    struct HudNotification
    {
        std::string text;
        float until = 0.0f;
    };

    struct PeerStatComparison
    {
        std::string callsign;
        float distanceMeters = 0.0f;
        float peerSpeedKph = 0.0f;
        float localSpeedKph = 0.0f;
        float peerAltitudeAgl = 0.0f;
        float localAltitudeAgl = 0.0f;
        float hullStrength = kPlaneHullMaxStrength;
        float fuselageStrength = kPlaneFuselageMaxStrength;
        float wear = 0.0f;
        int targetsDestroyed = 0;
    };

    struct PlaneDurabilityState
    {
        float hullStrength = kPlaneHullMaxStrength;
        float fuselageStrength = kPlaneFuselageMaxStrength;
        float wear = 0.0f;
        int targetsDestroyed = 0;
    };

    struct WeaponCooldownState
    {
        float nextPrimaryFireAt = -1000.0f;
        float nextBombDropAt = -1000.0f;
        float nextTerrainShotAt = -1000.0f;
        bool terraformMode = false;
    };

    enum class GameplayObjectKind
    {
        Projectile = 0,
        Bomb = 1,
        TerrainAdd = 2,
        TerrainRemove = 3
    };

    struct GameplayObjectState
    {
        GameplayObjectKind kind = GameplayObjectKind::Projectile;
        int id = 0;
        int ownerId = 0;
        Vec3 pos{};
        Vec3 vel{};
        float radius = 0.35f;
        float ttl = 1.0f;
        float damage = 10.0f;
        float gravityScale = 1.0f;
        float blastRadius = 0.0f;
        float craterRadius = 0.0f;
        float craterDepth = 0.0f;
        float massKg = 0.02f;
        float dragCoefficient = 0.3f;
        float referenceAreaM2 = 0.00012f;
        float spinAngleRad = 0.0f;
        float spinRateRadPerSec = 0.0f;
        bool active = true;
    };

    struct EnemyTargetState
    {
        int id = 0;
        Vec3 pos{};
        float yawRadians = 0.0f;
        float radius = 2.6f;
        float halfHeight = 3.6f;
        float health = 80.0f;
        float maxHealth = 80.0f;
        float respawnAt = -1.0f;
    };

    enum class EnemyEscortDirective
    {
        Guard = 0,
        Intercept = 1,
        Rejoin = 2
    };

    struct EnemyEscortAircraftState
    {
        int id = 0;
        int squadId = 0;
        int wingIndex = 0;
        Vec3 pos{};
        Vec3 vel{0.0f, 0.0f, 72.0f};
        float yawRadians = 0.0f;
        float pitchRadians = 0.0f;
        float bankRadians = 0.0f;
        float radius = 1.8f;
        float halfHeight = 1.0f;
        float health = 65.0f;
        float maxHealth = 65.0f;
        float respawnAt = -1.0f;
        float nextFireAt = 0.0f;
        float desiredSpeedMps = 72.0f;
        float lodAccumulator = 0.0f;
    };

    struct EnemyEscortSquadState
    {
        int id = 0;
        int assignedTargetId = 0;
        EnemyEscortDirective directive = EnemyEscortDirective::Guard;
        int hostilePlayerId = 0;
        Vec3 guardAnchor{};
        Vec3 interceptPoint{};
        Vec3 formationForward{0.0f, 0.0f, 1.0f};
        float nextDirectiveAt = 0.0f;
        float lastThreatAt = -1000.0f;
    };

    struct EnemyAirDirectorState
    {
        float nextStrategicUpdateAt = 0.0f;
    };

    struct RectF
    {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };

    struct PauseLayout
    {
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

    struct AssetEntry
    {
        std::filesystem::path path;
        std::string label;
        bool supported = false;
    };

    enum class VisualRigAxis
    {
        PosX = 0,
        NegX = 1,
        PosY = 2,
        NegY = 3,
        PosZ = 4,
        NegZ = 5
    };

    struct VisualRigCutout
    {
        bool enabled = false;
        VisualRigAxis axis = VisualRigAxis::PosZ;
        Vec3 center{};
        Vec3 halfExtents{0.24f, 0.24f, 0.24f};
        Vec3 pivot{};
        float motionScale = 1.0f;
    };

    struct PlaneVisualState
    {
        Model sourceModel = makeCubeModel();
        Model model = makeCubeModel();
        std::string label = "builtin procedural plane";
        std::filesystem::path sourcePath{};
        bool usesStl = false;
        Quat importRotationOffset = quatIdentity();
        float forwardAxisYawDegrees = 0.0f;
        float defaultScale = 3.0f;
        float scale = 3.0f;
        float previewZoom = 1.0f;
        bool previewAutoSpin = true;
        float yawDegrees = 0.0f;
        float pitchDegrees = 0.0f;
        float rollDegrees = 0.0f;
        Vec3 modelOffset{};
        std::string paintTargetKey;
        std::string paintHash;
        std::map<std::string, std::string> paintHashesByModelKey;
        RgbaImage paintOverlay{};
        bool hasCommittedPaint = false;
        bool paintSupported = false;
        std::vector<RgbaImage> paintUndoStack;
        std::vector<RgbaImage> paintRedoStack;
        std::array<VisualRigCutout, 4> rigCutouts{};
        Model rigBaseModel{};
        std::array<Model, 4> rigSlotModels{};
        std::array<bool, 4> rigSlotActive{};
        bool rigPartitionValid = false;
    };

    struct PaintUiState
    {
        PaintMode mode = PaintMode::Brush;
        int colorIndex = 0;
        int brushSize = 28;
        float brushOpacity = 1.0f;
        float brushHardness = 0.75f;
        bool draggingCanvas = false;
        RectF canvasRect{};
    };

    enum class TerrainFarTileBand
    {
        Near = 0,
        Mid = 1,
        Horizon = 2
    };

    enum class TerrainFarTileDetail
    {
        Lod0 = 0,
        Lod1 = 1,
        Lod2 = 2
    };

    struct TerrainFarTile
    {
        TerrainFarTileBand band = TerrainFarTileBand::Horizon;
        TerrainFarTileDetail detail = TerrainFarTileDetail::Lod2;
        int tileScale = 1;
        int tileX = 0;
        int tileZ = 0;
        std::uint64_t paramsSignature = 0u;
        std::uint64_t sourceSignature = 0u;
        bool active = false;
        Vec3 cullCenter{};
        float cullRadius = 0.0f;
        Model terrainModel{};
        Model waterModel{};
        Model propModel{};
        std::vector<TerrainPropCollider> propColliders;
    };

    struct TerrainVisualCache
    {
        bool valid = false;
        std::uint64_t sourceGeneration = 0u;
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

    struct TerrainVisualStreamState;

    struct VisualPreferenceData
    {
        bool hasStoredPath = false;
        std::filesystem::path sourcePath{};
        float scale = 3.0f;
        float previewZoom = 1.0f;
        bool previewAutoSpin = true;
        float forwardAxisYawDegrees = -90.0f;
        float yawDegrees = 0.0f;
        float pitchDegrees = 0.0f;
        float rollDegrees = 0.0f;
        Vec3 modelOffset{};
        std::string paintHash;
        std::map<std::string, std::string> paintHashesByModelKey;
        std::array<VisualRigCutout, 4> rigCutouts{};
    };

    struct AircraftProfile
    {
        std::string id = "prop_plane";
        FlightConfig flightConfig = defaultFlightConfig();
        PropAudioConfig propAudioConfig = defaultPropAudioConfig();
        VisualPreferenceData visualPrefs{};
    };

    struct SessionVoicePlaybackState
    {
        SDL_AudioStream *stream = nullptr;
        bool available = false;
        int sampleRate = 22050;
        int queueDepth = 6;
        std::vector<std::int16_t> scratchBuffer;

        bool initialize(int preferredSampleRate, int preferredQueueDepth, std::string *errorText = nullptr)
        {
            shutdown();
            sampleRate = std::max(11025, preferredSampleRate);
            queueDepth = std::max(2, preferredQueueDepth);

            const SDL_AudioSpec spec{SDL_AUDIO_S16, 1, sampleRate};
            stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
            if (stream == nullptr)
            {
                if (errorText != nullptr)
                {
                    *errorText = SDL_GetError();
                }
                return false;
            }

            if (!SDL_ResumeAudioStreamDevice(stream))
            {
                if (errorText != nullptr)
                {
                    *errorText = SDL_GetError();
                }
                SDL_DestroyAudioStream(stream);
                stream = nullptr;
                return false;
            }

            available = true;
            return true;
        }

        void shutdown()
        {
            if (stream != nullptr)
            {
                SDL_DestroyAudioStream(stream);
                stream = nullptr;
            }
            available = false;
            scratchBuffer.clear();
        }

        void clear()
        {
            if (stream != nullptr)
            {
                SDL_ClearAudioStream(stream);
                SDL_PauseAudioStreamDevice(stream);
            }
        }

        void queuePcm(const std::int16_t *pcm, std::size_t sampleCount, float gain)
        {
            if (!available || stream == nullptr || pcm == nullptr || sampleCount == 0u)
            {
                return;
            }

            const int maxQueuedBytes = sampleRate * queueDepth * static_cast<int>(sizeof(std::int16_t));
            if (SDL_GetAudioStreamQueued(stream) > maxQueuedBytes)
            {
                SDL_ClearAudioStream(stream);
            }
            if (!SDL_ResumeAudioStreamDevice(stream))
            {
                available = false;
                return;
            }

            const float clampedGain = clamp(gain, 0.0f, 1.5f);
            const void *data = pcm;
            int dataBytes = static_cast<int>(sampleCount * sizeof(std::int16_t));
            if (std::fabs(clampedGain - 1.0f) > 1.0e-3f)
            {
                scratchBuffer.resize(sampleCount);
                for (std::size_t index = 0; index < sampleCount; ++index)
                {
                    const float scaled = static_cast<float>(pcm[index]) * clampedGain;
                    scratchBuffer[index] = static_cast<std::int16_t>(clamp(scaled, -32768.0f, 32767.0f));
                }
                data = scratchBuffer.data();
                dataBytes = static_cast<int>(scratchBuffer.size() * sizeof(std::int16_t));
            }

            if (!SDL_PutAudioStreamData(stream, data, dataBytes))
            {
                available = false;
                SDL_DestroyAudioStream(stream);
                stream = nullptr;
                scratchBuffer.clear();
            }
        }

        void queuePcmSpatial(const std::int16_t *pcm, std::size_t sampleCount, float leftGain, float rightGain)
        {
            if (!available || stream == nullptr || pcm == nullptr || sampleCount == 0u)
            {
                return;
            }

            const int maxQueuedBytes = sampleRate * queueDepth * 2 * static_cast<int>(sizeof(std::int16_t));
            if (SDL_GetAudioStreamQueued(stream) > maxQueuedBytes)
            {
                SDL_ClearAudioStream(stream);
            }
            if (!SDL_ResumeAudioStreamDevice(stream))
            {
                available = false;
                return;
            }

            const float clampedLeftGain = clamp(leftGain, 0.0f, 1.5f);
            const float clampedRightGain = clamp(rightGain, 0.0f, 1.5f);
            scratchBuffer.resize(sampleCount * 2u);
            for (std::size_t index = 0; index < sampleCount; ++index)
            {
                const float sample = static_cast<float>(pcm[index]);
                scratchBuffer[index * 2u] =
                    static_cast<std::int16_t>(clamp(sample * clampedLeftGain, -32768.0f, 32767.0f));
                scratchBuffer[index * 2u + 1u] =
                    static_cast<std::int16_t>(clamp(sample * clampedRightGain, -32768.0f, 32767.0f));
            }

            if (!SDL_PutAudioStreamData(
                    stream,
                    scratchBuffer.data(),
                    static_cast<int>(scratchBuffer.size() * sizeof(std::int16_t))))
            {
                available = false;
                SDL_DestroyAudioStream(stream);
                stream = nullptr;
                scratchBuffer.clear();
            }
        }
    };

    struct SessionVoiceRuntime
    {
        SessionVoicePlaybackState playback{};
        bool captureActive = false;
        bool captureSupported = false;
        bool playbackSupported = false;
        bool captureFailed = false;
        bool playbackFailed = false;
        std::vector<std::uint8_t> captureScratch;
        std::vector<std::uint8_t> decodeScratch;
    };

    struct BootResources
    {
        GeoConfig geoConfig{};
        UiState uiState{};
        GraphicsSettings graphics{};
        LightingSettings lighting{};
        HudSettings hud{};
        OnlineSettings onlineSettings{};
        SteamBuildConfig steamBuildConfig = defaultSteamBuildConfig();
        SteamOnlineController steamController{};
        SteamOnlineState steamOnline{};
        TerrainParams terrainParams = defaultTerrainParams();
        TerrainParams defaultTerrainParamsValues = defaultTerrainParams();
        AircraftProfile planeProfile{};
        VisualPreferenceData walkingPrefs{};
        VisualPreferenceData enemyEscortPrefs{};
        VisualPreferenceData groundTargetPrefs{};
        PlaneVisualState planeVisual{};
        PlaneVisualState walkingVisual{};
        PlaneVisualState enemyEscortVisual{};
        PlaneVisualState groundTargetVisual{};
        PaintUiState paintUi{};
        LoadingUiState loadingUi{};
        ControlProfile controls{};
        std::vector<AssetEntry> assetCatalog;
        HudCanvas hudCanvas{1280, 720};
        std::filesystem::path preferencesPath{};
        std::filesystem::path hudPreferencesPath{};
        std::string selectedWorldId = "native_default";
        std::vector<WorldInstanceSummary> worldInstances;
        std::deque<HudNotification> notifications;
        SteamOnlineState previousSteamOnline{};
        bool previousSteamOnlineValid = false;
        bool preferencesDirty = false;
        float preferencesNextSaveAt = 0.0f;
        bool enemyEscortVisualReady = false;
        bool groundTargetVisualReady = false;
    };

    struct GamepadState
    {
        SDL_Gamepad *handle = nullptr;
        SDL_JoystickID instanceId = 0;
        std::string name;
        std::array<bool, static_cast<std::size_t>(SDL_GAMEPAD_BUTTON_COUNT)> buttons{};
        std::array<bool, static_cast<std::size_t>(SDL_GAMEPAD_BUTTON_COUNT)> previousButtons{};
        std::array<float, static_cast<std::size_t>(SDL_GAMEPAD_AXIS_COUNT)> axes{};
        std::array<float, static_cast<std::size_t>(SDL_GAMEPAD_AXIS_COUNT)> previousAxes{};
        int verticalRepeatDirection = 0;
        float verticalRepeatAt = 0.0f;
        int horizontalRepeatDirection = 0;
        float horizontalRepeatAt = 0.0f;
        float trimAccumulator = 0.0f;
        bool rudderLatched = false;
        float lastProjectileTriggerAt = -1000.0f;
    };

    struct GameSession
    {
        std::optional<WorldStore> worldStore;
        std::optional<WorldStore> mirrorWorldStore;
        std::string terrainWorldId = "native_default";
        std::optional<TerrainChunkBakeCache> terrainChunkBakeCache;
        std::shared_ptr<std::shared_mutex> terrainWorldMutex{};
        std::shared_ptr<TerrainVisualStreamState> terrainStream{};
        TerrainVisualCache terrainCache{};
        TerrainFieldContext terrainContext{};
        OnlineSessionRole onlineRole = OnlineSessionRole::Offline;
        bool worldStoreMirrored = false;
        SteamOnlineState steamOnline{};
        std::shared_ptr<INetTransport> netTransport{};
        HostedWorldServer hostedServer{};
        ClientReplicationState clientReplication{};
        RemotePeerState localReplicationPeer{};
        std::mt19937 worldRng{};
        WindState windState{};
        CloudField cloudField{};
        FlightState plane{};
        FlightRuntimeState runtime{};
        ProceduralAudioState audioState{};
        VoiceSessionState voice{};
        SessionVoiceRuntime voiceRuntime{};
        float worldTime = 0.0f;
        PlayerMode playerMode = PlayerMode::Flight;
        bool flightMode = true;
        VehicleId activeVehicleId = 0;
        float walkYaw = 0.0f;
        float walkPitch = 0.0f;
        float flightLookYaw = 0.0f;
        float flightLookPitch = 0.0f;
        float foliageBrushAmount = 0.0f;
        float foliageImpactPulse = 0.0f;
        std::vector<GameplayObjectState> gameplayObjects;
        std::vector<EnemyTargetState> enemyTargets;
        std::vector<EnemyEscortAircraftState> enemyEscorts;
        std::vector<EnemyEscortSquadState> enemyEscortSquadrons;
        EnemyAirDirectorState enemyAirDirector{};
        std::unordered_map<int, PlaneDurabilityState> planeDurabilityByPlayerId;
        std::unordered_map<int, WeaponCooldownState> weaponStateByPlayerId;
        std::unordered_map<int, PlaneDurabilityState> replicatedDurabilityByPlayerId;
        float audioGunshotImpulse = 0.0f;
        float audioTerrainShotImpulse = 0.0f;
        float audioBombLatchImpulse = 0.0f;
        float audioExplosionImpulse = 0.0f;
        float audioExplosionDistanceMeters = 1000.0f;
        int nextGameplayObjectId = 1;
        float nextGameplaySnapshotAt = 0.0f;
        std::map<int, std::string> knownRemotePeers;
        std::unordered_map<std::string, PlaneVisualState> remoteVisualsByKey;
        std::unordered_map<ConstructId, ConstructBlueprint> constructBlueprintsById;
        std::unordered_map<ConstructId, ConstructState> constructStatesById;
        std::unordered_map<VehicleId, SharedVehicleState> sharedVehiclesById;
    };

    struct HudPeerIndicator
    {
        Vec3 worldPos{};
        std::string callsign;
        int radioChannel = 1;
        bool transmitting = false;
        float peerSpeedKph = 0.0f;
        float localSpeedKph = 0.0f;
        float peerAltitudeAgl = 0.0f;
        float localAltitudeAgl = 0.0f;
        float hullStrength = kPlaneHullMaxStrength;
        float fuselageStrength = kPlaneFuselageMaxStrength;
        float wear = 0.0f;
        int targetsDestroyed = 0;
        bool terraformMode = false;
    };

    struct HudTargetIndicator
    {
        int id = 0;
        Vec3 worldPos{};
        float halfHeight = 0.0f;
        float distanceMeters = 0.0f;
        std::string label;
        float health = 0.0f;
        float maxHealth = 1.0f;
        bool showHealth = true;
        bool highlighted = false;
    };

    enum class HardwareTier
    {
        Requirement = 0,
        Suggested = 1
    };

    enum class RuntimePressureState
    {
        Normal = 0,
        Pressure = 1,
        Critical = 2
    };

    struct SystemPressureSnapshot
    {
        bool valid = false;
        std::uint64_t totalPhysicalBytes = 0;
        std::uint64_t availablePhysicalBytes = 0;
        std::uint64_t totalCommitLimitBytes = 0;
        std::uint64_t commitHeadroomBytes = 0;
        std::uint64_t processWorkingSetBytes = 0;
        std::uint64_t processPrivateBytes = 0;
        std::uint64_t gpuLocalBudgetBytes = 0;
        std::uint64_t gpuLocalUsageBytes = 0;
        std::uint64_t gpuSharedBudgetBytes = 0;
        std::uint64_t gpuSharedUsageBytes = 0;
        std::string gpuAdapterName;
        float sampledAtSeconds = 0.0f;
    };

    struct TerrainStreamStats
    {
        int queuedCount = 0;
        int inflightCount = 0;
        int completedCount = 0;
        std::uint64_t droppedRequestCount = 0;
        std::uint64_t droppedResultCount = 0;
        std::uint64_t staleResultCount = 0;
        std::uint64_t adoptedResultCount = 0;
        std::uint64_t workerBuildCount = 0;
        float lastFrameAdoptionTimeMs = 0.0f;
        float lastWorkerBuildTimeMs = 0.0f;
        float lastFrameSyncBuildTimeMs = 0.0f;
    };

    struct TerrainStreamBudgetOverrides
    {
        int workerCount = -1;
        int maxPendingChunks = -1;
        int maxStaleChunks = -1;
        int resultBudgetPerFrame = -1;
        float resultBudgetTimeMs = -1.0f;
        int nearMissingBudget = -1;
        int midMissingBudget = -1;
        int horizonMissingBudget = -1;
        int upgradeBudget = -1;
        bool allowMidBand = true;
        bool allowHorizonBand = true;
        bool allowUpgrades = true;
    };

    struct PerformanceGovernor
    {
        HardwareTier hardwareTier = HardwareTier::Requirement;
        RuntimePressureState pressureState = RuntimePressureState::Normal;
        SystemPressureSnapshot lastSnapshot{};
        float targetFrameMs = 8.3f;
        float smoothedFrameMs = 8.3f;
        float nextPressureSampleAt = 0.0f;
        float lastQualityChangeAt = -1000.0f;
        int qualityStep = 0;
        float dynamicRenderScale = 1.0f;
        float drawDistanceScale = 1.0f;
        float shadowDistanceScale = 1.0f;
        float shadowSoftnessScale = 1.0f;
        float cloudDensityScale = 1.0f;
        std::size_t residentMeshBudgetBytes = 0;
        std::size_t sceneTextureBudgetBytes = 0;
        std::size_t maxUploadBytes = std::numeric_limits<std::size_t>::max();
        int terrainWorkerCount = 2;
        int terrainMaxPendingChunks = 24;
        int terrainMaxStaleChunks = 6;
        int terrainResultBudgetPerFrame = 2;
        float terrainResultBudgetTimeMs = 1.5f;
        int nearMissingBudget = 8;
        int midMissingBudget = 6;
        int horizonMissingBudget = 4;
        int upgradeBudget = 4;
        bool allowMidBand = true;
        bool allowHorizonBand = true;
        bool allowUpgrades = true;
    };

    struct WalkingInputState
    {
        bool forward = false;
        bool backward = false;
        bool left = false;
        bool right = false;
        bool sprint = false;
        bool jump = false;
        float forwardAxis = 0.0f;
        float rightAxis = 0.0f;
    };

    struct WalkingRigPoseSample
    {
        float yawRadians = 0.0f;
        float lookPitchRadians = 0.0f;
        float gaitPhaseRadians = 0.0f;
        float moveAmount = 0.0f;
        float bob = 0.0f;
        float strideAngle = 0.0f;
        float armSwingAngle = 0.0f;
        float forwardLean = 0.0f;
        float bankLean = 0.0f;
        bool airborne = false;
    };

    struct CombatAudioTelemetry
    {
        float gunshotImpulse = 0.0f;
        float terrainShotImpulse = 0.0f;
        float bombLatchImpulse = 0.0f;
        float explosionImpulse = 0.0f;
        float explosionDistanceMeters = 1000.0f;
        float projectileWhistleAmount = 0.0f;
        float projectilePitchScale = 1.0f;
        float projectileDoppler = 1.0f;
        float bombWhistleAmount = 0.0f;
        float bombPitchScale = 1.0f;
        float bombDoppler = 1.0f;
        float peerPlaneAmount = 0.0f;
        float peerPlanePitchScale = 1.0f;
        float peerPlaneDoppler = 1.0f;
    };

    struct TerrainTileDecorationResult
    {
        Model propModel{};
        std::vector<TerrainPropCollider> propColliders;
    };

    struct TerrainTileRequest
    {
        TerrainFarTileBand band = TerrainFarTileBand::Horizon;
        TerrainFarTileDetail detail = TerrainFarTileDetail::Lod2;
        int tileScale = 1;
        int tileX = 0;
        int tileZ = 0;
        std::uint64_t paramsSignature = 0u;
        std::uint64_t sourceSignature = 0u;
        float priority = 0.0f;
    };

    struct TerrainStreamGenerationSnapshot;

    struct TerrainChunkBuildRequest
    {
        TerrainTileRequest request{};
        std::shared_ptr<const TerrainStreamGenerationSnapshot> generationContext{};
        std::uint64_t generation = 0u;
    };

    struct TerrainChunkBuildResult
    {
        TerrainTileRequest request{};
        CompiledTerrainChunk compiledChunk{};
        std::uint64_t generation = 0u;
    };

    struct TerrainStreamGenerationSnapshot
    {
        TerrainFieldContext terrainContext{};
        std::optional<TerrainChunkBakeCache> bakeCache;
        std::string terrainWorldId;
        std::uint64_t generation = 0u;
    };

    struct TerrainVisualStreamState
    {
        std::mutex mutex;
        std::condition_variable condition;
        bool stopRequested = false;
        int workerCount = 0;
        int maxPendingRequests = 0;
        int maxCompletedResults = 0;
        std::uint64_t generation = 1u;
        std::vector<std::thread> workers;
        std::deque<TerrainChunkBuildRequest> queuedRequests;
        std::deque<TerrainChunkBuildResult> completedResults;
        std::map<std::string, TerrainTileRequest> desiredRequests;
        std::map<std::string, std::string> queuedRequestKeys;
        std::map<std::string, std::string> inflightRequestKeys;
        std::shared_ptr<const TerrainStreamGenerationSnapshot> activeGenerationContext{};
        TerrainStreamStats stats{};

        ~TerrainVisualStreamState()
        {
            shutdown();
        }

        void shutdown()
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                stopRequested = true;
                queuedRequests.clear();
                completedResults.clear();
                desiredRequests.clear();
                queuedRequestKeys.clear();
                inflightRequestKeys.clear();
                activeGenerationContext.reset();
            }
            condition.notify_all();
            for (std::thread &worker : workers)
            {
                if (worker.joinable())
                {
                    worker.join();
                }
            }
            workers.clear();
            stopRequested = false;
            workerCount = 0;
        }
    };

} // namespace TrueFlightApp
