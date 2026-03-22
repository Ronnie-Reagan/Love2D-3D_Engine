#include "NativeGame/SteamSupport.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <memory>
#include <random>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#if TRUEFLIGHT_ENABLE_STEAMWORKS
#include <steam/steam_api.h>
#endif

namespace NativeGame {

namespace {

constexpr int kSteamLaneCount = 4;
constexpr std::array<int, kSteamLaneCount> kSteamLanePriorities { 100, 90, 20, 95 };
constexpr std::array<std::uint16_t, kSteamLaneCount> kSteamLaneWeights { 4, 3, 1, 2 };

void setStatus(std::string* statusText, const std::string& value)
{
    if (statusText != nullptr) {
        *statusText = value;
    }
}

#if TRUEFLIGHT_ENABLE_STEAMWORKS

std::uint64_t fallbackPeerIdForConnection(HSteamNetConnection connection)
{
    return connection <= 0 ? 0ull : static_cast<std::uint64_t>(connection);
}

std::uint64_t steamIdentityToPeerId(const SteamNetworkingIdentity& identity, HSteamNetConnection connection = k_HSteamNetConnection_Invalid)
{
    if (const std::uint64_t steamId = static_cast<std::uint64_t>(identity.GetSteamID64()); steamId != 0) {
        return steamId;
    }
    return fallbackPeerIdForConnection(connection);
}

bool steamApisReady()
{
    return SteamUser() != nullptr && SteamNetworkingSockets() != nullptr && SteamNetworkingUtils() != nullptr;
}

std::string makeSessionNonce()
{
    std::random_device rd;
    std::mt19937_64 rng((static_cast<std::uint64_t>(rd()) << 32) ^ static_cast<std::uint64_t>(rd()));
    std::ostringstream stream;
    stream << std::hex << rng() << rng();
    return stream.str();
}

bool writeLobbyData(CSteamID lobbyId, const char* key, const std::string& value)
{
    return SteamMatchmaking() != nullptr && SteamMatchmaking()->SetLobbyData(lobbyId, key, value.c_str());
}

bool readLobbyData(CSteamID lobbyId, const char* key, std::string* out)
{
    if (out == nullptr || SteamMatchmaking() == nullptr) {
        return false;
    }
    const char* value = SteamMatchmaking()->GetLobbyData(lobbyId, key);
    if (value == nullptr || value[0] == '\0') {
        return false;
    }
    *out = value;
    return true;
}

void configureConnectionLanes(HSteamNetConnection connection)
{
    if (!steamApisReady() || connection == k_HSteamNetConnection_Invalid) {
        return;
    }
    (void)SteamNetworkingSockets()->ConfigureConnectionLanes(
        connection,
        static_cast<int>(kSteamLanePriorities.size()),
        kSteamLanePriorities.data(),
        kSteamLaneWeights.data());
}

class SteamSocketsTransport;

struct SteamTransportRegistry {
    std::mutex mutex;
    std::unordered_map<HSteamListenSocket, std::weak_ptr<SteamSocketsTransport>> listenSockets;
    std::unordered_map<HSteamNetConnection, std::weak_ptr<SteamSocketsTransport>> connections;
    bool callbackInstalled = false;
};

SteamTransportRegistry& steamTransportRegistry()
{
    static SteamTransportRegistry registry;
    return registry;
}

class SteamSocketsTransport final : public INetTransport, public std::enable_shared_from_this<SteamSocketsTransport> {
public:
    enum class Mode : std::uint8_t {
        Host = 0,
        Client = 1
    };

    static std::shared_ptr<SteamSocketsTransport> createHost(int virtualPort, std::string* statusText)
    {
        auto transport = std::shared_ptr<SteamSocketsTransport>(new SteamSocketsTransport(Mode::Host, virtualPort));
        if (!transport->initializeHost(statusText)) {
            return {};
        }
        return transport;
    }

    static std::shared_ptr<SteamSocketsTransport> createClient(std::uint64_t hostSteamId, int virtualPort, std::string* statusText)
    {
        auto transport = std::shared_ptr<SteamSocketsTransport>(new SteamSocketsTransport(Mode::Client, virtualPort));
        if (!transport->initializeClient(hostSteamId, statusText)) {
            return {};
        }
        return transport;
    }

    ~SteamSocketsTransport() override
    {
        close();
    }

    [[nodiscard]] bool ready() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return ready_;
    }

    bool send(NetPeerId peerId, int lane, std::string_view payload, bool reliable) override
    {
        if (!steamApisReady() || payload.empty()) {
            return false;
        }

        HSteamNetConnection connection = k_HSteamNetConnection_Invalid;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto found = peerConnections_.find(peerId);
            if (found == peerConnections_.end()) {
                return false;
            }
            connection = found->second;
        }

        SteamNetworkingMessage_t* message = SteamNetworkingUtils()->AllocateMessage(static_cast<int>(payload.size()));
        if (message == nullptr || message->m_pData == nullptr) {
            if (message != nullptr) {
                message->Release();
            }
            return false;
        }

        std::memcpy(message->m_pData, payload.data(), payload.size());
        message->m_cbSize = static_cast<int>(payload.size());
        message->m_conn = connection;
        message->m_idxLane = static_cast<std::uint16_t>(std::clamp(lane, 0, kSteamLaneCount - 1));
        message->m_nFlags = reliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_UnreliableNoDelay;

        SteamNetworkingMessage_t* messages[1] { message };
        int64 result = 0;
        SteamNetworkingSockets()->SendMessages(1, messages, &result);
        if (result < 0) {
            return false;
        }
        return true;
    }

    void disconnectPeer(NetPeerId peerId) override
    {
        HSteamNetConnection connection = k_HSteamNetConnection_Invalid;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto found = peerConnections_.find(peerId);
            if (found == peerConnections_.end()) {
                return;
            }
            connection = found->second;
        }

        if (steamApisReady() && connection != k_HSteamNetConnection_Invalid) {
            (void)SteamNetworkingSockets()->CloseConnection(connection, 0, "TrueFlight disconnect", false);
        }
        removeConnection(connection, peerId, true);
    }

    [[nodiscard]] std::vector<NetEvent> poll() override
    {
        pumpMessages();

        std::vector<NetEvent> out;
        std::lock_guard<std::mutex> lock(mutex_);
        while (!events_.empty()) {
            out.push_back(std::move(events_.front()));
            events_.pop_front();
        }
        return out;
    }

    [[nodiscard]] std::vector<NetPeerId> peers() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<NetPeerId> out;
        out.reserve(peerConnections_.size());
        for (const auto& [peerId, connection] : peerConnections_) {
            (void)connection;
            out.push_back(peerId);
        }
        return out;
    }

    void handleConnectionStatus(const SteamNetConnectionStatusChangedCallback_t& update)
    {
        switch (update.m_info.m_eState) {
        case k_ESteamNetworkingConnectionState_Connecting:
            if (mode_ == Mode::Host && update.m_info.m_hListenSocket == listenSocket_) {
                if (SteamNetworkingSockets()->AcceptConnection(update.m_hConn) != k_EResultOK) {
                    (void)SteamNetworkingSockets()->CloseConnection(update.m_hConn, 0, "Accept failed", false);
                }
            }
            break;

        case k_ESteamNetworkingConnectionState_Connected: {
            const std::uint64_t peerId = steamIdentityToPeerId(update.m_info.m_identityRemote, update.m_hConn);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                ready_ = true;
                peerConnections_[peerId] = update.m_hConn;
                connectionPeers_[update.m_hConn] = peerId;
            }
            configureConnectionLanes(update.m_hConn);
            if (pollGroup_ != k_HSteamNetPollGroup_Invalid) {
                (void)SteamNetworkingSockets()->SetConnectionPollGroup(update.m_hConn, pollGroup_);
            }
            (void)SteamNetworkingSockets()->SetConnectionUserData(update.m_hConn, static_cast<int64>(peerId));
            registerConnection(update.m_hConn);
            pushEvent({ NetEvent::Type::Connected, peerId, 0, true, {} });
            break;
        }

        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally: {
            const std::uint64_t peerId = steamIdentityToPeerId(update.m_info.m_identityRemote, update.m_hConn);
            removeConnection(update.m_hConn, peerId, false);
            if (steamApisReady()) {
                (void)SteamNetworkingSockets()->CloseConnection(update.m_hConn, 0, "Connection closed", false);
            }
            break;
        }

        default:
            break;
        }
    }

private:
    explicit SteamSocketsTransport(Mode mode, int virtualPort)
        : mode_(mode)
        , virtualPort_(std::max(0, virtualPort))
    {
    }

    bool initializeHost(std::string* statusText)
    {
        if (!steamApisReady()) {
            if (statusText != nullptr) {
                *statusText = "Steam networking unavailable.";
            }
            return false;
        }

        installGlobalCallback();
        SteamNetworkingUtils()->InitRelayNetworkAccess();

        pollGroup_ = SteamNetworkingSockets()->CreatePollGroup();
        listenSocket_ = SteamNetworkingSockets()->CreateListenSocketP2P(virtualPort_, 0, nullptr);
        if (listenSocket_ == k_HSteamListenSocket_Invalid) {
            if (pollGroup_ != k_HSteamNetPollGroup_Invalid) {
                SteamNetworkingSockets()->DestroyPollGroup(pollGroup_);
                pollGroup_ = k_HSteamNetPollGroup_Invalid;
            }
            if (statusText != nullptr) {
                *statusText = "Failed to create Steam P2P listen socket.";
            }
            return false;
        }

        registerListenSocket();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ready_ = true;
        }
        if (statusText != nullptr) {
            std::ostringstream stream;
            stream << "Steam host listen socket active on virtual port " << virtualPort_;
            if (SteamUser() != nullptr) {
                stream << " as " << SteamUser()->GetSteamID().ConvertToUint64();
            }
            *statusText = stream.str();
        }
        return true;
    }

    bool initializeClient(std::uint64_t hostSteamId, std::string* statusText)
    {
        if (!steamApisReady()) {
            if (statusText != nullptr) {
                *statusText = "Steam networking unavailable.";
            }
            return false;
        }
        if (hostSteamId == 0) {
            if (statusText != nullptr) {
                *statusText = "No host SteamID was provided.";
            }
            return false;
        }

        installGlobalCallback();
        SteamNetworkingUtils()->InitRelayNetworkAccess();

        pollGroup_ = SteamNetworkingSockets()->CreatePollGroup();

        SteamNetworkingIdentity remoteIdentity;
        remoteIdentity.Clear();
        remoteIdentity.SetSteamID(CSteamID(hostSteamId));
        outboundConnection_ = SteamNetworkingSockets()->ConnectP2P(remoteIdentity, virtualPort_, 0, nullptr);
        if (outboundConnection_ == k_HSteamNetConnection_Invalid) {
            if (pollGroup_ != k_HSteamNetPollGroup_Invalid) {
                SteamNetworkingSockets()->DestroyPollGroup(pollGroup_);
                pollGroup_ = k_HSteamNetPollGroup_Invalid;
            }
            if (statusText != nullptr) {
                *statusText = "Failed to start Steam P2P connection.";
            }
            return false;
        }

        registerConnection(outboundConnection_);
        if (statusText != nullptr) {
            std::ostringstream stream;
            stream << "Connecting to Steam host " << hostSteamId << " on virtual port " << virtualPort_;
            *statusText = stream.str();
        }
        return true;
    }

    void close()
    {
        unregister();

        if (steamApisReady()) {
            std::vector<HSteamNetConnection> connections;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (const auto& [peerId, connection] : peerConnections_) {
                    (void)peerId;
                    connections.push_back(connection);
                }
            }

            for (HSteamNetConnection connection : connections) {
                if (connection != k_HSteamNetConnection_Invalid) {
                    (void)SteamNetworkingSockets()->CloseConnection(connection, 0, "Transport shutdown", false);
                }
            }

            if (listenSocket_ != k_HSteamListenSocket_Invalid) {
                (void)SteamNetworkingSockets()->CloseListenSocket(listenSocket_);
                listenSocket_ = k_HSteamListenSocket_Invalid;
            }
            if (pollGroup_ != k_HSteamNetPollGroup_Invalid) {
                (void)SteamNetworkingSockets()->DestroyPollGroup(pollGroup_);
                pollGroup_ = k_HSteamNetPollGroup_Invalid;
            }
        }

        std::lock_guard<std::mutex> lock(mutex_);
        ready_ = false;
        peerConnections_.clear();
        connectionPeers_.clear();
        events_.clear();
        outboundConnection_ = k_HSteamNetConnection_Invalid;
    }

    void pumpMessages()
    {
        if (!steamApisReady() || pollGroup_ == k_HSteamNetPollGroup_Invalid) {
            return;
        }

        SteamNetworkingMessage_t* messages[32] {};
        while (true) {
            const int count = SteamNetworkingSockets()->ReceiveMessagesOnPollGroup(pollGroup_, messages, static_cast<int>(std::size(messages)));
            if (count <= 0) {
                break;
            }

            for (int index = 0; index < count; ++index) {
                SteamNetworkingMessage_t* message = messages[index];
                if (message == nullptr) {
                    continue;
                }

                NetPeerId peerId = 0;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    const auto found = connectionPeers_.find(message->m_conn);
                    if (found != connectionPeers_.end()) {
                        peerId = found->second;
                    }
                }
                if (peerId == 0) {
                    peerId = steamIdentityToPeerId(message->m_identityPeer, message->m_conn);
                }

                NetEvent event;
                event.type = NetEvent::Type::Message;
                event.peerId = peerId;
                event.lane = static_cast<int>(message->m_idxLane);
                event.reliable = (message->m_nFlags & k_nSteamNetworkingSend_Reliable) != 0;
                if (message->m_pData != nullptr && message->m_cbSize > 0) {
                    event.payload.assign(static_cast<const char*>(message->m_pData), static_cast<std::size_t>(message->m_cbSize));
                }
                pushEvent(std::move(event));
                message->Release();
            }
        }
    }

    void pushEvent(NetEvent event)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(std::move(event));
    }

    void removeConnection(HSteamNetConnection connection, NetPeerId fallbackPeerId, bool emitEvent)
    {
        NetPeerId peerId = fallbackPeerId;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto peerIt = connectionPeers_.find(connection);
            if (peerIt != connectionPeers_.end()) {
                peerId = peerIt->second;
                peerConnections_.erase(peerIt->second);
                connectionPeers_.erase(peerIt);
            } else if (fallbackPeerId != 0) {
                peerConnections_.erase(fallbackPeerId);
            }
        }
        unregisterConnection(connection);
        if (emitEvent && peerId != 0) {
            pushEvent({ NetEvent::Type::Disconnected, peerId, 0, true, {} });
        }
    }

    void registerListenSocket()
    {
        std::lock_guard<std::mutex> lock(steamTransportRegistry().mutex);
        steamTransportRegistry().listenSockets[listenSocket_] = weak_from_this();
    }

    void registerConnection(HSteamNetConnection connection)
    {
        std::lock_guard<std::mutex> lock(steamTransportRegistry().mutex);
        steamTransportRegistry().connections[connection] = weak_from_this();
    }

    void unregisterConnection(HSteamNetConnection connection)
    {
        std::lock_guard<std::mutex> lock(steamTransportRegistry().mutex);
        steamTransportRegistry().connections.erase(connection);
    }

    void unregister()
    {
        std::lock_guard<std::mutex> registryLock(steamTransportRegistry().mutex);
        if (listenSocket_ != k_HSteamListenSocket_Invalid) {
            steamTransportRegistry().listenSockets.erase(listenSocket_);
        }
        for (const auto& [peerId, connection] : peerConnections_) {
            (void)peerId;
            steamTransportRegistry().connections.erase(connection);
        }
        if (outboundConnection_ != k_HSteamNetConnection_Invalid) {
            steamTransportRegistry().connections.erase(outboundConnection_);
        }
    }

    static void installGlobalCallback()
    {
        SteamTransportRegistry& registry = steamTransportRegistry();
        std::lock_guard<std::mutex> lock(registry.mutex);
        if (registry.callbackInstalled || !steamApisReady()) {
            return;
        }
        registry.callbackInstalled = SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(&SteamSocketsTransport::onConnectionStatusChanged);
    }

    static void onConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* update)
    {
        if (update == nullptr) {
            return;
        }

        std::shared_ptr<SteamSocketsTransport> transport;
        {
            SteamTransportRegistry& registry = steamTransportRegistry();
            std::lock_guard<std::mutex> lock(registry.mutex);
            const auto connectionIt = registry.connections.find(update->m_hConn);
            if (connectionIt != registry.connections.end()) {
                transport = connectionIt->second.lock();
            }
            if (!transport && update->m_info.m_hListenSocket != k_HSteamListenSocket_Invalid) {
                const auto listenIt = registry.listenSockets.find(update->m_info.m_hListenSocket);
                if (listenIt != registry.listenSockets.end()) {
                    transport = listenIt->second.lock();
                }
            }
        }

        if (transport) {
            transport->handleConnectionStatus(*update);
        } else if (steamApisReady() &&
                   (update->m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer ||
                    update->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)) {
            (void)SteamNetworkingSockets()->CloseConnection(update->m_hConn, 0, "Unowned connection closed", false);
        }
    }

    mutable std::mutex mutex_;
    std::deque<NetEvent> events_;
    std::unordered_map<NetPeerId, HSteamNetConnection> peerConnections_;
    std::unordered_map<HSteamNetConnection, NetPeerId> connectionPeers_;
    Mode mode_ = Mode::Host;
    int virtualPort_ = 0;
    bool ready_ = false;
    HSteamListenSocket listenSocket_ = k_HSteamListenSocket_Invalid;
    HSteamNetConnection outboundConnection_ = k_HSteamNetConnection_Invalid;
    HSteamNetPollGroup pollGroup_ = k_HSteamNetPollGroup_Invalid;
};

#endif

}  // namespace

SteamBuildConfig defaultSteamBuildConfig()
{
    SteamBuildConfig config;
#if defined(TRUEFLIGHT_STEAM_APP_ID)
    config.appId = static_cast<std::uint32_t>(TRUEFLIGHT_STEAM_APP_ID);
#else
    config.appId = 480;
#endif
    config.requested = TRUEFLIGHT_ENABLE_STEAMWORKS != 0;
    config.sdkRoot = std::filesystem::path("steamworks_sdk_164") / "sdk";
    return config;
}

bool ensureSteamAppIdFile(const std::filesystem::path& directory, std::uint32_t appId, std::string* errorText)
{
    const std::filesystem::path path = directory / "steam_appid.txt";
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        if (errorText != nullptr) {
            *errorText = "Unable to write " + path.string();
        }
        return false;
    }

    file << appId << "\n";
    if (!file.good()) {
        if (errorText != nullptr) {
            *errorText = "Failed while writing " + path.string();
        }
        return false;
    }
    return true;
}

std::string describeSteamBuildConfig(const SteamBuildConfig& config)
{
    std::ostringstream stream;
    stream << "requested=" << (config.requested ? "1" : "0")
           << " appId=" << config.appId;
    if (!config.sdkRoot.empty()) {
        stream << " sdk=" << config.sdkRoot.generic_string();
    }
    return stream.str();
}

bool SteamRuntime::initialize(const SteamBuildConfig& config, std::string* statusText)
{
    state_ = {};
    state_.compiled = TRUEFLIGHT_ENABLE_STEAMWORKS != 0;
    state_.appId = config.appId;

#if TRUEFLIGHT_ENABLE_STEAMWORKS
    if (!config.requested) {
        state_.offlineFallback = true;
        state_.status = "Steam disabled; offline fallback active.";
        if (statusText != nullptr) {
            *statusText = state_.status;
        }
        return false;
    }

    if (SteamAPI_RestartAppIfNecessary(config.appId)) {
        state_.restartRequired = true;
        state_.offlineFallback = false;
        state_.status = "Steam requested a relaunch through the client.";
        if (statusText != nullptr) {
            *statusText = state_.status;
        }
        return false;
    }

    SteamErrMsg error {};
    if (SteamAPI_InitEx(&error) != k_ESteamAPIInitResult_OK) {
        state_.offlineFallback = true;
        state_.status = error[0] != '\0'
            ? std::string("Steam initialization failed: ") + error
            : std::string("Steam initialization failed.");
        if (statusText != nullptr) {
            *statusText = state_.status;
        }
        return false;
    }

    state_.initialized = true;
    state_.offlineFallback = false;
    if (SteamUtils() != nullptr) {
        state_.runningAppId = static_cast<std::uint32_t>(SteamUtils()->GetAppID());
        state_.overlayEnabled = SteamUtils()->IsOverlayEnabled();
    }
    if (steamApisReady()) {
        SteamNetworkingUtils()->InitRelayNetworkAccess();
        std::ostringstream stream;
        stream << "Steam initialized";
        if (SteamUser() != nullptr) {
            stream << " as " << SteamUser()->GetSteamID().ConvertToUint64();
        }
        if (state_.runningAppId != 0 && state_.runningAppId != state_.appId) {
            stream << " (running app " << state_.runningAppId << ")";
        }
        if (!state_.overlayEnabled) {
            stream << "; overlay unavailable";
        }
        state_.status = stream.str();
    } else {
        state_.status = "Steam initialized without networking interfaces.";
    }
    if (statusText != nullptr) {
        *statusText = state_.status;
    }
    return true;
#else
    (void)config;
    state_.offlineFallback = true;
    state_.status = "Steamworks not compiled; offline fallback active.";
    if (statusText != nullptr) {
        *statusText = state_.status;
    }
    return false;
#endif
}

void SteamRuntime::shutdown()
{
#if TRUEFLIGHT_ENABLE_STEAMWORKS
    if (state_.initialized) {
        SteamAPI_Shutdown();
    }
#endif
    const bool compiled = state_.compiled;
    const std::uint32_t appId = state_.appId;
    state_ = {};
    state_.compiled = compiled;
    state_.appId = appId;
    state_.offlineFallback = true;
    state_.status = compiled ? "Steam shut down." : "Steamworks unavailable";
}

void SteamRuntime::pump()
{
#if TRUEFLIGHT_ENABLE_STEAMWORKS
    if (state_.initialized) {
        SteamAPI_RunCallbacks();
        if (SteamUtils() != nullptr) {
            state_.runningAppId = static_cast<std::uint32_t>(SteamUtils()->GetAppID());
            state_.overlayEnabled = SteamUtils()->IsOverlayEnabled();
        }
    }
#endif
}

std::shared_ptr<INetTransport> createSteamHostTransport(int virtualPort, std::string* statusText)
{
#if TRUEFLIGHT_ENABLE_STEAMWORKS
    if (auto transport = SteamSocketsTransport::createHost(virtualPort, statusText)) {
        return transport;
    }
    if (statusText != nullptr && statusText->empty()) {
        *statusText = "Steam host transport unavailable.";
    }
    return std::make_shared<NullTransport>();
#else
    (void)virtualPort;
    if (statusText != nullptr) {
        *statusText = "Steamworks not compiled; host transport unavailable.";
    }
    return std::make_shared<NullTransport>();
#endif
}

std::shared_ptr<INetTransport> createSteamClientTransport(std::uint64_t hostSteamId, int virtualPort, std::string* statusText)
{
#if TRUEFLIGHT_ENABLE_STEAMWORKS
    if (auto transport = SteamSocketsTransport::createClient(hostSteamId, virtualPort, statusText)) {
        return transport;
    }
    if (statusText != nullptr && statusText->empty()) {
        *statusText = "Steam client transport unavailable.";
    }
    return std::make_shared<NullTransport>();
#else
    (void)hostSteamId;
    (void)virtualPort;
    if (statusText != nullptr) {
        *statusText = "Steamworks not compiled; client transport unavailable.";
    }
    return std::make_shared<NullTransport>();
#endif
}

std::shared_ptr<INetTransport> createSteamOrFallbackTransport(const SteamBuildConfig& config, std::string* statusText)
{
    if (!config.requested) {
        if (statusText != nullptr) {
            *statusText = "Steam disabled; using offline fallback transport.";
        }
        return std::make_shared<NullTransport>();
    }

    std::shared_ptr<INetTransport> transport = createSteamHostTransport(0, statusText);
    if (!transport || !transport->ready()) {
        return std::make_shared<NullTransport>();
    }
    return transport;
}

#if TRUEFLIGHT_ENABLE_STEAMWORKS

bool parseLobbyConnectString(std::string_view connectString, std::uint64_t* lobbyId)
{
    if (lobbyId == nullptr || connectString.rfind("lobby:", 0) != 0) {
        return false;
    }

    const std::string numeric(connectString.substr(std::strlen("lobby:")));
    if (numeric.empty()) {
        return false;
    }

    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(numeric.c_str(), &end, 10);
    if (end == numeric.c_str() || end == nullptr || *end != '\0' || parsed == 0ull) {
        return false;
    }

    *lobbyId = static_cast<std::uint64_t>(parsed);
    return true;
}

struct SteamOnlineController::Impl {
    SteamRuntime runtime;
    SteamBuildConfig buildConfig;
    SteamLobbySettings pendingSettings;
    SteamLobbyState lobby;
    CCallbackManual<Impl, GameLobbyJoinRequested_t> lobbyJoinRequestedCallback;
    CCallbackManual<Impl, GameRichPresenceJoinRequested_t> richPresenceJoinRequestedCallback;
    CCallbackManual<Impl, LobbyDataUpdate_t> lobbyDataUpdateCallback;
    CCallResult<Impl, LobbyCreated_t> lobbyCreatedResult;
    CCallResult<Impl, LobbyEnter_t> lobbyEnteredResult;

    void registerCallbacks()
    {
        lobbyJoinRequestedCallback.Register(this, &Impl::onGameLobbyJoinRequested);
        richPresenceJoinRequestedCallback.Register(this, &Impl::onGameRichPresenceJoinRequested);
        lobbyDataUpdateCallback.Register(this, &Impl::onLobbyDataUpdate);
    }

    void unregisterCallbacks()
    {
        lobbyJoinRequestedCallback.Unregister();
        richPresenceJoinRequestedCallback.Unregister();
        lobbyDataUpdateCallback.Unregister();
    }

    void clearRichPresence()
    {
#if TRUEFLIGHT_ENABLE_STEAMWORKS
        if (SteamFriends() != nullptr) {
            SteamFriends()->ClearRichPresence();
        }
#endif
    }

    void publishRichPresence()
    {
#if TRUEFLIGHT_ENABLE_STEAMWORKS
        if (SteamFriends() == nullptr) {
            return;
        }

        if (lobby.role != SteamLobbyState::Role::Host || lobby.lobbyId == 0 || !lobby.transportReady) {
            SteamFriends()->ClearRichPresence();
            return;
        }

        const std::string connectString = "lobby:" + std::to_string(lobby.lobbyId);
        (void)SteamFriends()->SetRichPresence("connect", connectString.c_str());
        (void)SteamFriends()->SetRichPresence("status", lobby.status.c_str());
#endif
    }

    void clearLobbySnapshot(bool leaveSteamLobby)
    {
        const bool preserveJoinRequest = lobby.joinRequested && lobby.pendingLobbyId != 0;
        const std::uint64_t preservedPendingLobbyId = preserveJoinRequest ? lobby.pendingLobbyId : 0;
#if TRUEFLIGHT_ENABLE_STEAMWORKS
        if (leaveSteamLobby && lobby.lobbyId != 0 && SteamMatchmaking() != nullptr) {
            SteamMatchmaking()->LeaveLobby(CSteamID(lobby.lobbyId));
        }
#endif
        lobby.transport.reset();
        lobby = {};
        if (buildConfig.appId != 0) {
            lobby.appId = buildConfig.appId;
        }
        lobby.joinRequested = preserveJoinRequest;
        lobby.pendingLobbyId = preservedPendingLobbyId;
        lobby.status = preserveJoinRequest
            ? ("Pending Steam lobby invite #" + std::to_string(preservedPendingLobbyId))
            : "Offline";
        pendingSettings = {};
        clearRichPresence();
    }

    void setPendingJoin(std::uint64_t lobbyId)
    {
        if (lobbyId == 0) {
            return;
        }
        lobby.joinRequested = true;
        lobby.pendingLobbyId = lobbyId;
        lobby.status = "Pending Steam lobby invite #" + std::to_string(lobbyId);
    }

    bool finalizeJoinedLobby(CSteamID lobbySteamId)
    {
        if (SteamMatchmaking() == nullptr) {
            clearLobbySnapshot(true);
            lobby.status = "Steam matchmaking unavailable";
            return true;
        }

        std::string protocolVersion;
        std::string buildId;
        std::string worldId;
        std::string worldSeed;
        std::string maxPlayers;
        std::string joinable;
        std::string voiceEnabled;
        std::string sessionNonce;
        auto requireLobbyData = [&](const char* key, std::string& value) -> bool {
            if (readLobbyData(lobbySteamId, key, &value)) {
                return true;
            }
            (void)SteamMatchmaking()->RequestLobbyData(lobbySteamId);
            lobby.status = std::string("Waiting for Steam lobby metadata (") + key + ")";
            return false;
        };
        if (!requireLobbyData("protocol_version", protocolVersion) ||
            !requireLobbyData("build_id", buildId) ||
            !requireLobbyData("world_id", worldId) ||
            !requireLobbyData("world_seed", worldSeed) ||
            !requireLobbyData("max_players", maxPlayers) ||
            !requireLobbyData("joinable", joinable) ||
            !requireLobbyData("voice_enabled", voiceEnabled) ||
            !requireLobbyData("session_nonce", sessionNonce)) {
            return false;
        }

        std::string hostSteamIdText;
        if (readLobbyData(lobbySteamId, "host_steam_id", &hostSteamIdText)) {
            lobby.hostSteamId = std::strtoull(hostSteamIdText.c_str(), nullptr, 10);
        }
        if (lobby.hostSteamId == 0) {
            lobby.hostSteamId = SteamMatchmaking()->GetLobbyOwner(lobbySteamId).ConvertToUint64();
        }
        if (lobby.hostSteamId == 0) {
            lobby.status = "Lobby host unavailable";
            return false;
        }

        if (!pendingSettings.protocolVersion.empty() && protocolVersion != pendingSettings.protocolVersion) {
            clearLobbySnapshot(true);
            lobby.status = "Protocol mismatch";
            return true;
        }
        if (!pendingSettings.buildId.empty() && buildId != pendingSettings.buildId) {
            clearLobbySnapshot(true);
            lobby.status = "Build mismatch";
            return true;
        }

        lobby.protocolVersion = protocolVersion;
        lobby.buildId = buildId;
        lobby.worldId = worldId;
        lobby.worldSeed = std::strtoull(worldSeed.c_str(), nullptr, 10);
        lobby.maxPlayers = std::max(1, std::atoi(maxPlayers.c_str()));
        lobby.joinable = joinable == "1" || joinable == "true";
        lobby.voiceEnabled = voiceEnabled == "1" || voiceEnabled == "true";
        lobby.sessionNonce = sessionNonce;
        lobby.lobbyReady = true;

        if (lobby.transport == nullptr) {
            std::string transportStatus;
            lobby.transport = createSteamClientTransport(lobby.hostSteamId, pendingSettings.virtualPort, &transportStatus);
            if (lobby.transport == nullptr) {
                clearLobbySnapshot(true);
                lobby.status = transportStatus.empty() ? "Steam client transport unavailable" : transportStatus;
                return true;
            }
        }

        lobby.transportReady = lobby.transport->ready();
        lobby.status = lobby.transportReady ? "Joined Steam lobby" : "Connecting to Steam host";
        return true;
    }

    bool initialize(const SteamBuildConfig& config, std::string* statusText)
    {
        buildConfig = config;
        clearLobbySnapshot(false);
        unregisterCallbacks();
        const bool initialized = runtime.initialize(config, statusText);
        if (initialized) {
            registerCallbacks();
        }
        return initialized;
    }

    void shutdown()
    {
        leaveLobby();
        unregisterCallbacks();
        lobbyCreatedResult.Cancel();
        lobbyEnteredResult.Cancel();
        runtime.shutdown();
    }

    void pump()
    {
        runtime.pump();
        lobby.transportReady = lobby.transport != nullptr && lobby.transport->ready();
        if (lobby.role == SteamLobbyState::Role::Host && lobby.lobbyId != 0 && lobby.transportReady) {
            publishRichPresence();
        } else if (lobby.role == SteamLobbyState::Role::Host && lobby.lobbyId != 0) {
            clearRichPresence();
        } else if (lobby.role == SteamLobbyState::Role::Client && lobby.lobbyId != 0 && lobby.lobbyReady && lobby.transport != nullptr) {
            lobby.status = lobby.transportReady ? "Joined Steam lobby" : "Connecting to Steam host";
        }
    }

    bool createHostLobby(const SteamLobbySettings& settings, std::string* statusText)
    {
        if (!runtime.available() || SteamMatchmaking() == nullptr || SteamUser() == nullptr) {
            setStatus(statusText, runtime.state().status);
            lobby.status = runtime.state().status;
            return false;
        }

        leaveLobby();
        pendingSettings = settings;
        if (pendingSettings.appId == 0) {
            pendingSettings.appId = buildConfig.appId != 0 ? buildConfig.appId : TRUEFLIGHT_STEAM_APP_ID;
        }
        if (pendingSettings.sessionNonce.empty()) {
            pendingSettings.sessionNonce = makeSessionNonce();
        }

        lobby.role = SteamLobbyState::Role::Host;
        lobby.appId = pendingSettings.appId;
        lobby.maxPlayers = pendingSettings.maxPlayers;
        lobby.virtualPort = pendingSettings.virtualPort;
        lobby.protocolVersion = pendingSettings.protocolVersion;
        lobby.buildId = pendingSettings.buildId;
        lobby.worldId = pendingSettings.worldId;
        lobby.worldSeed = pendingSettings.worldSeed;
        lobby.joinable = pendingSettings.joinable;
        lobby.voiceEnabled = pendingSettings.voiceEnabled;
        lobby.sessionNonce = pendingSettings.sessionNonce;
        lobby.joinRequested = false;
        lobby.pendingLobbyId = 0;
        lobby.status = "Creating Steam lobby";

        const SteamAPICall_t call = SteamMatchmaking()->CreateLobby(k_ELobbyTypeFriendsOnly, pendingSettings.maxPlayers);
        if (call == k_uAPICallInvalid) {
            clearLobbySnapshot(true);
            lobby.status = "CreateLobby failed";
            setStatus(statusText, lobby.status);
            return false;
        }

        lobbyCreatedResult.Set(call, this, &Impl::onLobbyCreated);
        setStatus(statusText, lobby.status);
        return true;
    }

    bool joinLobbyAndConnectToHost(std::uint64_t lobbyId, const SteamLobbySettings& settings, std::string* statusText)
    {
        if (!runtime.available() || SteamMatchmaking() == nullptr) {
            setStatus(statusText, runtime.state().status);
            lobby.status = runtime.state().status;
            return false;
        }

        leaveLobby();
        pendingSettings = settings;
        if (pendingSettings.appId == 0) {
            pendingSettings.appId = buildConfig.appId != 0 ? buildConfig.appId : TRUEFLIGHT_STEAM_APP_ID;
        }
        lobby.role = SteamLobbyState::Role::Client;
        lobby.lobbyId = lobbyId;
        lobby.joinRequested = true;
        lobby.pendingLobbyId = lobbyId;
        lobby.lobbyReady = false;
        lobby.transportReady = false;
        lobby.status = "Joining Steam lobby";

        const SteamAPICall_t call = SteamMatchmaking()->JoinLobby(CSteamID(lobbyId));
        if (call == k_uAPICallInvalid) {
            clearLobbySnapshot(true);
            lobby.status = "JoinLobby failed";
            setStatus(statusText, lobby.status);
            return false;
        }

        lobbyEnteredResult.Set(call, this, &Impl::onLobbyEntered);
        setStatus(statusText, lobby.status);
        return true;
    }

    bool connectToHost(std::uint64_t hostSteamId, const SteamLobbySettings& settings, std::string* statusText)
    {
        if (!runtime.available()) {
            setStatus(statusText, runtime.state().status);
            lobby.status = runtime.state().status;
            return false;
        }

        leaveLobby();
        pendingSettings = settings;
        lobby.role = SteamLobbyState::Role::Client;
        lobby.hostSteamId = hostSteamId;
        lobby.appId = settings.appId != 0 ? settings.appId : (buildConfig.appId != 0 ? buildConfig.appId : TRUEFLIGHT_STEAM_APP_ID);
        lobby.maxPlayers = settings.maxPlayers;
        lobby.virtualPort = settings.virtualPort;
        lobby.protocolVersion = settings.protocolVersion;
        lobby.buildId = settings.buildId;
        lobby.worldId = settings.worldId;
        lobby.worldSeed = settings.worldSeed;
        lobby.sessionNonce = settings.sessionNonce;
        lobby.joinable = settings.joinable;
        lobby.voiceEnabled = settings.voiceEnabled;
        lobby.joinRequested = true;
        lobby.pendingLobbyId = hostSteamId;
        if (SteamUser() != nullptr) {
            lobby.localSteamId = SteamUser()->GetSteamID().ConvertToUint64();
        }

        lobby.transport = createSteamClientTransport(hostSteamId, settings.virtualPort, statusText);
        if (lobby.transport == nullptr) {
            lobby.transportReady = false;
            lobby.lobbyReady = false;
            lobby.status = "Steam client transport unavailable";
            setStatus(statusText, lobby.status);
            return false;
        }
        lobby.transportReady = lobby.transport->ready();
        lobby.lobbyReady = false;
        lobby.status = "Connecting to Steam host";
        setStatus(statusText, lobby.status);
        return true;
    }

    void leaveLobby()
    {
        lobbyCreatedResult.Cancel();
        lobbyEnteredResult.Cancel();
        clearLobbySnapshot(true);
    }

    bool activateInviteOverlay(std::string* statusText)
    {
#if TRUEFLIGHT_ENABLE_STEAMWORKS
        if (SteamFriends() == nullptr || SteamUtils() == nullptr || lobby.lobbyId == 0 || lobby.role != SteamLobbyState::Role::Host) {
            setStatus(statusText, "Steam invite overlay unavailable.");
            return false;
        }
        if (!SteamUtils()->IsOverlayEnabled()) {
            setStatus(statusText, "Steam overlay unavailable. Launch the Steam-enabled build from the Steam client.");
            return false;
        }
        SteamFriends()->ActivateGameOverlayInviteDialog(CSteamID(lobby.lobbyId));
        setStatus(statusText, "Steam invite overlay opened.");
        return true;
#else
        setStatus(statusText, "Steamworks not compiled; invite overlay unavailable.");
        return false;
#endif
    }

    [[nodiscard]] bool hasPendingJoinRequest() const
    {
        return lobby.joinRequested && lobby.pendingLobbyId != 0;
    }

    void queuePendingJoinRequest(std::uint64_t lobbyId, std::string* statusText)
    {
        setPendingJoin(lobbyId);
        setStatus(statusText, lobby.status);
    }

    [[nodiscard]] std::uint64_t pendingJoinLobbyId() const
    {
        return hasPendingJoinRequest() ? lobby.pendingLobbyId : 0;
    }

    [[nodiscard]] std::uint64_t consumePendingJoinRequest()
    {
        const std::uint64_t pendingLobbyId = pendingJoinLobbyId();
        lobby.joinRequested = false;
        lobby.pendingLobbyId = 0;
        return pendingLobbyId;
    }

    [[nodiscard]] const SteamRuntimeState& runtimeState() const
    {
        return runtime.state();
    }

    [[nodiscard]] const std::string& status() const
    {
        return lobby.status;
    }

    void onLobbyCreated(LobbyCreated_t* result, bool ioFailure)
    {
        if (result == nullptr || ioFailure || result->m_eResult != k_EResultOK) {
            clearLobbySnapshot(true);
            lobby.status = "Lobby creation failed";
            return;
        }

        lobby.lobbyId = result->m_ulSteamIDLobby;
        lobby.localSteamId = SteamUser() != nullptr ? SteamUser()->GetSteamID().ConvertToUint64() : 0;
        lobby.hostSteamId = lobby.localSteamId;
        lobby.lobbyReady = true;
        lobby.transport = createSteamHostTransport(lobby.virtualPort, nullptr);
        lobby.transportReady = lobby.transport != nullptr && lobby.transport->ready();
        lobby.status = lobby.transportReady ? "Steam lobby ready" : "Steam host transport unavailable";

        if (SteamMatchmaking() != nullptr) {
            const CSteamID lobbySteamId(lobby.lobbyId);
            (void)SteamMatchmaking()->SetLobbyMemberLimit(lobbySteamId, lobby.maxPlayers);
            (void)SteamMatchmaking()->SetLobbyJoinable(lobbySteamId, lobby.joinable);
            (void)writeLobbyData(lobbySteamId, "protocol_version", lobby.protocolVersion);
            (void)writeLobbyData(lobbySteamId, "build_id", lobby.buildId);
            (void)writeLobbyData(lobbySteamId, "host_steam_id", std::to_string(lobby.hostSteamId));
            (void)writeLobbyData(lobbySteamId, "world_id", lobby.worldId);
            (void)writeLobbyData(lobbySteamId, "world_seed", std::to_string(lobby.worldSeed));
            (void)writeLobbyData(lobbySteamId, "max_players", std::to_string(lobby.maxPlayers));
            (void)writeLobbyData(lobbySteamId, "joinable", lobby.joinable ? "1" : "0");
            (void)writeLobbyData(lobbySteamId, "voice_enabled", lobby.voiceEnabled ? "1" : "0");
            (void)writeLobbyData(lobbySteamId, "session_nonce", lobby.sessionNonce.empty() ? makeSessionNonce() : lobby.sessionNonce);
        }
        publishRichPresence();
    }

    void onLobbyEntered(LobbyEnter_t* result, bool ioFailure)
    {
        if (result == nullptr || ioFailure || result->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess) {
            clearLobbySnapshot(true);
            lobby.status = "Lobby join failed";
            return;
        }

        const CSteamID lobbySteamId(result->m_ulSteamIDLobby);
        lobby.lobbyId = result->m_ulSteamIDLobby;
        lobby.localSteamId = SteamUser() != nullptr ? SteamUser()->GetSteamID().ConvertToUint64() : 0;
        lobby.lobbyReady = false;
        clearRichPresence();
        if (!finalizeJoinedLobby(lobbySteamId)) {
            (void)SteamMatchmaking()->RequestLobbyData(lobbySteamId);
        }
    }

    void onGameLobbyJoinRequested(GameLobbyJoinRequested_t* callback)
    {
        if (callback == nullptr) {
            return;
        }
        setPendingJoin(callback->m_steamIDLobby.ConvertToUint64());
    }

    void onGameRichPresenceJoinRequested(GameRichPresenceJoinRequested_t* callback)
    {
        if (callback == nullptr) {
            return;
        }
        std::uint64_t lobbyId = 0;
        if (!parseLobbyConnectString(callback->m_rgchConnect, &lobbyId)) {
            return;
        }
        setPendingJoin(lobbyId);
    }

    void onLobbyDataUpdate(LobbyDataUpdate_t* callback)
    {
        if (callback == nullptr || callback->m_ulSteamIDLobby == 0 || callback->m_bSuccess == 0) {
            return;
        }
        if (lobby.role != SteamLobbyState::Role::Client || lobby.lobbyId != callback->m_ulSteamIDLobby || lobby.lobbyReady) {
            return;
        }
        (void)finalizeJoinedLobby(CSteamID(callback->m_ulSteamIDLobby));
    }
};

#else

struct SteamOnlineController::Impl {
    SteamRuntime runtime;
    SteamLobbyState lobby;

    bool initialize(const SteamBuildConfig& config, std::string* statusText)
    {
        lobby = {};
        const bool initialized = runtime.initialize(config, statusText);
        if (initialized) {
            lobby.appId = config.appId != 0 ? config.appId : TRUEFLIGHT_STEAM_APP_ID;
        }
        return initialized;
    }

    void shutdown()
    {
        leaveLobby();
        runtime.shutdown();
    }

    void pump()
    {
        runtime.pump();
    }

    bool createHostLobby(const SteamLobbySettings& settings, std::string* statusText)
    {
        lobby = {};
        lobby.role = SteamLobbyState::Role::Host;
        lobby.appId = settings.appId != 0 ? settings.appId : TRUEFLIGHT_STEAM_APP_ID;
        lobby.maxPlayers = settings.maxPlayers;
        lobby.virtualPort = settings.virtualPort;
        lobby.protocolVersion = settings.protocolVersion;
        lobby.buildId = settings.buildId;
        lobby.worldId = settings.worldId;
        lobby.worldSeed = settings.worldSeed;
        lobby.sessionNonce = settings.sessionNonce;
        lobby.joinable = settings.joinable;
        lobby.voiceEnabled = settings.voiceEnabled;
        lobby.transport = createSteamHostTransport(settings.virtualPort, statusText);
        lobby.transportReady = lobby.transport != nullptr && lobby.transport->ready();
        lobby.status = lobby.transportReady ? "Steam host transport active." : "Steam host transport unavailable.";
        if (statusText != nullptr && statusText->empty()) {
            *statusText = lobby.status;
        }
        return lobby.transportReady;
    }

    bool joinLobbyAndConnectToHost(std::uint64_t lobbyId, const SteamLobbySettings& settings, std::string* statusText)
    {
        return connectToHost(lobbyId, settings, statusText);
    }

    bool connectToHost(std::uint64_t hostSteamId, const SteamLobbySettings& settings, std::string* statusText)
    {
        lobby = {};
        lobby.role = SteamLobbyState::Role::Client;
        lobby.hostSteamId = hostSteamId;
        lobby.appId = settings.appId != 0 ? settings.appId : TRUEFLIGHT_STEAM_APP_ID;
        lobby.maxPlayers = settings.maxPlayers;
        lobby.virtualPort = settings.virtualPort;
        lobby.protocolVersion = settings.protocolVersion;
        lobby.buildId = settings.buildId;
        lobby.worldId = settings.worldId;
        lobby.worldSeed = settings.worldSeed;
        lobby.sessionNonce = settings.sessionNonce;
        lobby.joinable = settings.joinable;
        lobby.voiceEnabled = settings.voiceEnabled;
        lobby.transport = createSteamClientTransport(hostSteamId, settings.virtualPort, statusText);
        lobby.transportReady = lobby.transport != nullptr && lobby.transport->ready();
        lobby.status = lobby.transportReady ? "Steam client transport connecting." : "Steam client transport unavailable.";
        if (statusText != nullptr && statusText->empty()) {
            *statusText = lobby.status;
        }
        return lobby.transportReady;
    }

    void leaveLobby()
    {
        lobby.transport.reset();
        lobby = {};
        if (runtime.state().appId != 0) {
            lobby.appId = runtime.state().appId;
        }
        lobby.status = "Offline";
    }

    bool activateInviteOverlay(std::string* statusText)
    {
        setStatus(statusText, "Steamworks not compiled; invite overlay unavailable.");
        return false;
    }

    [[nodiscard]] bool hasPendingJoinRequest() const
    {
        return lobby.joinRequested && lobby.pendingLobbyId != 0;
    }

    void queuePendingJoinRequest(std::uint64_t lobbyId, std::string* statusText)
    {
        if (lobbyId == 0) {
            return;
        }
        lobby.joinRequested = true;
        lobby.pendingLobbyId = lobbyId;
        lobby.status = "Pending Steam lobby invite #" + std::to_string(lobbyId);
        setStatus(statusText, lobby.status);
    }

    [[nodiscard]] std::uint64_t pendingJoinLobbyId() const
    {
        return hasPendingJoinRequest() ? lobby.pendingLobbyId : 0;
    }

    [[nodiscard]] std::uint64_t consumePendingJoinRequest()
    {
        const std::uint64_t pendingLobbyId = pendingJoinLobbyId();
        lobby.joinRequested = false;
        lobby.pendingLobbyId = 0;
        return pendingLobbyId;
    }

    [[nodiscard]] const SteamRuntimeState& runtimeState() const
    {
        return runtime.state();
    }

    [[nodiscard]] const std::string& status() const
    {
        return lobby.status;
    }
};

#endif

SteamOnlineController::SteamOnlineController()
    : impl_(std::make_unique<Impl>())
{
}

SteamOnlineController::~SteamOnlineController() = default;

bool SteamOnlineController::initialize(const SteamBuildConfig& config, std::string* statusText)
{
    return impl_ != nullptr && impl_->initialize(config, statusText);
}

void SteamOnlineController::shutdown()
{
    if (impl_ != nullptr) {
        impl_->shutdown();
    }
}

void SteamOnlineController::pump()
{
    if (impl_ != nullptr) {
        impl_->pump();
    }
}

bool SteamOnlineController::createHostLobby(const SteamLobbySettings& settings, std::string* statusText)
{
    return impl_ != nullptr && impl_->createHostLobby(settings, statusText);
}

bool SteamOnlineController::joinLobbyAndConnectToHost(std::uint64_t lobbyId, const SteamLobbySettings& settings, std::string* statusText)
{
    return impl_ != nullptr && impl_->joinLobbyAndConnectToHost(lobbyId, settings, statusText);
}

bool SteamOnlineController::connectToHost(std::uint64_t hostSteamId, const SteamLobbySettings& settings, std::string* statusText)
{
    return impl_ != nullptr && impl_->connectToHost(hostSteamId, settings, statusText);
}

void SteamOnlineController::leaveLobby()
{
    if (impl_ != nullptr) {
        impl_->leaveLobby();
    }
}

bool SteamOnlineController::activateInviteOverlay(std::string* statusText)
{
    return impl_ != nullptr && impl_->activateInviteOverlay(statusText);
}

bool SteamOnlineController::available() const
{
    return impl_ != nullptr && impl_->runtime.available();
}

bool SteamOnlineController::initialized() const
{
    return impl_ != nullptr && impl_->runtime.state().initialized;
}

void SteamOnlineController::queuePendingJoinRequest(std::uint64_t lobbyId, std::string* statusText)
{
    if (impl_ != nullptr) {
        impl_->queuePendingJoinRequest(lobbyId, statusText);
    }
}

bool SteamOnlineController::hasPendingJoinRequest() const
{
    return impl_ != nullptr && impl_->hasPendingJoinRequest();
}

std::uint64_t SteamOnlineController::pendingJoinLobbyId() const
{
    return impl_ != nullptr ? impl_->pendingJoinLobbyId() : 0ull;
}

std::uint64_t SteamOnlineController::consumePendingJoinRequest()
{
    return impl_ != nullptr ? impl_->consumePendingJoinRequest() : 0ull;
}

const SteamRuntimeState& SteamOnlineController::runtimeState() const
{
    static const SteamRuntimeState kEmptyRuntimeState {};
    return impl_ != nullptr ? impl_->runtimeState() : kEmptyRuntimeState;
}

const std::string& SteamOnlineController::status() const
{
    static const std::string kEmptyStatus {};
    return impl_ != nullptr ? impl_->status() : kEmptyStatus;
}

const SteamLobbyState& SteamOnlineController::lobby() const
{
    static const SteamLobbyState kEmptyLobby {};
    return impl_ != nullptr ? impl_->lobby : kEmptyLobby;
}

std::shared_ptr<INetTransport> SteamOnlineController::transport() const
{
    return impl_ != nullptr ? impl_->lobby.transport : std::shared_ptr<INetTransport> {};
}

}  // namespace NativeGame
