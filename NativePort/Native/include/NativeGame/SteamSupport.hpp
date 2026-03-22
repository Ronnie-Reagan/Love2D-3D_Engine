#pragma once

#include "NativeGame/NetTransport.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#ifndef TRUEFLIGHT_ENABLE_STEAMWORKS
#define TRUEFLIGHT_ENABLE_STEAMWORKS 0
#endif

#ifndef TRUEFLIGHT_STEAM_APP_ID
#define TRUEFLIGHT_STEAM_APP_ID 480
#endif

namespace NativeGame {

struct SteamBuildConfig {
    bool requested = false;
    std::uint32_t appId = 480;
    std::filesystem::path sdkRoot;
};

struct SteamRuntimeState {
    bool compiled = TRUEFLIGHT_ENABLE_STEAMWORKS != 0;
    bool initialized = false;
    bool restartRequired = false;
    bool offlineFallback = true;
    bool overlayEnabled = false;
    std::uint32_t appId = 0;
    std::uint32_t runningAppId = 0;
    std::uint64_t localSteamId = 0;
    std::string status = "Steamworks unavailable";
    std::string personaName;
};

class SteamRuntime {
public:
    bool initialize(const SteamBuildConfig& config, std::string* statusText = nullptr);
    void shutdown();
    void pump();

    [[nodiscard]] bool available() const
    {
        return state_.initialized;
    }

    [[nodiscard]] const SteamRuntimeState& state() const
    {
        return state_;
    }

private:
    SteamRuntimeState state_ {};
};

[[nodiscard]] SteamBuildConfig defaultSteamBuildConfig();

struct SteamLobbySettings {
    std::uint32_t appId = 480;
    int maxPlayers = 8;
    int virtualPort = 0;
    std::string protocolVersion = "1";
    std::string buildId = "native";
    std::string worldId = "native_default";
    std::uint64_t worldSeed = 0;
    bool joinable = true;
    bool voiceEnabled = true;
    std::string sessionNonce;
};

struct SteamLobbyState {
    enum class Role : std::uint8_t {
        Offline = 0,
        Host = 1,
        Client = 2
    };

    Role role = Role::Offline;
    std::uint64_t lobbyId = 0;
    std::uint64_t hostSteamId = 0;
    std::uint64_t localSteamId = 0;
    std::uint32_t appId = 480;
    int maxPlayers = 8;
    int virtualPort = 0;
    std::string protocolVersion = "1";
    std::string buildId = "native";
    std::string worldId = "native_default";
    std::uint64_t worldSeed = 0;
    std::string sessionNonce;
    bool joinable = true;
    bool voiceEnabled = true;
    bool joinRequested = false;
    std::uint64_t pendingLobbyId = 0;
    bool lobbyReady = false;
    bool transportReady = false;
    int memberCount = 0;
    std::string status = "Offline";
    std::string localPersonaName;
    std::string hostPersonaName;
    std::vector<std::string> memberNames;
    std::shared_ptr<INetTransport> transport;
};

[[nodiscard]] std::shared_ptr<INetTransport> createSteamHostTransport(int virtualPort, std::string* statusText = nullptr);
[[nodiscard]] std::shared_ptr<INetTransport> createSteamClientTransport(std::uint64_t hostSteamId, int virtualPort, std::string* statusText = nullptr);
[[nodiscard]] std::shared_ptr<INetTransport> createSteamOrFallbackTransport(
    const SteamBuildConfig& config,
    std::string* statusText = nullptr);

class SteamOnlineController {
public:
    SteamOnlineController();
    ~SteamOnlineController();

    bool initialize(const SteamBuildConfig& config, std::string* statusText = nullptr);
    void shutdown();
    void pump();

    bool createHostLobby(const SteamLobbySettings& settings, std::string* statusText = nullptr);
    bool joinLobbyAndConnectToHost(std::uint64_t lobbyId, const SteamLobbySettings& settings, std::string* statusText = nullptr);
    bool connectToHost(std::uint64_t hostSteamId, const SteamLobbySettings& settings, std::string* statusText = nullptr);
    void leaveLobby();
    [[nodiscard]] bool activateInviteOverlay(std::string* statusText = nullptr);

    [[nodiscard]] bool available() const;
    [[nodiscard]] bool initialized() const;
    void queuePendingJoinRequest(std::uint64_t lobbyId, std::string* statusText = nullptr);
    [[nodiscard]] bool hasPendingJoinRequest() const;
    [[nodiscard]] std::uint64_t pendingJoinLobbyId() const;
    [[nodiscard]] std::uint64_t consumePendingJoinRequest();
    [[nodiscard]] const SteamRuntimeState& runtimeState() const;
    [[nodiscard]] const std::string& status() const;
    [[nodiscard]] const SteamLobbyState& lobby() const;
    [[nodiscard]] std::shared_ptr<INetTransport> transport() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

bool ensureSteamAppIdFile(const std::filesystem::path& directory, std::uint32_t appId, std::string* errorText = nullptr);
std::string describeSteamBuildConfig(const SteamBuildConfig& config);

}  // namespace NativeGame
