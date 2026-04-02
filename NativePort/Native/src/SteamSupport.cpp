#include "NativeGame/SteamSupport.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
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
constexpr int kSteamSendBufferSizeBytes = 4 * 1024 * 1024;

void setStatus(std::string* statusText, const std::string& value)
{
    if (statusText != nullptr) {
        *statusText = value;
    }
}

std::mutex& steamStdoutMutex()
{
    static std::mutex mutex;
    return mutex;
}

void steamStdoutLog(std::string_view message)
{
    std::lock_guard<std::mutex> lock(steamStdoutMutex());
    std::cout << "[steam] " << message << std::endl;
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

std::string steamPersonaName(CSteamID steamId)
{
    if (steamId.IsValid() == false || SteamFriends() == nullptr) {
        return {};
    }
    const char* name = SteamFriends()->GetFriendPersonaName(steamId);
    return name != nullptr ? std::string(name) : std::string {};
}

const char* steamNetworkingAvailabilityLabel(ESteamNetworkingAvailability availability)
{
    switch (availability) {
    case k_ESteamNetworkingAvailability_CannotTry:
        return "cannot-try";
    case k_ESteamNetworkingAvailability_Failed:
        return "failed";
    case k_ESteamNetworkingAvailability_Previously:
        return "previously";
    case k_ESteamNetworkingAvailability_Retrying:
        return "retrying";
    case k_ESteamNetworkingAvailability_NeverTried:
        return "never-tried";
    case k_ESteamNetworkingAvailability_Waiting:
        return "waiting";
    case k_ESteamNetworkingAvailability_Attempting:
        return "attempting";
    case k_ESteamNetworkingAvailability_Current:
        return "current";
    case k_ESteamNetworkingAvailability_Unknown:
        return "unknown";
    default:
        return "other";
    }
}

const char* steamResultLabel(EResult result)
{
    switch (result) {
    case k_EResultOK:
        return "ok";
    case k_EResultInvalidParam:
        return "invalid-param";
    case k_EResultInvalidState:
        return "invalid-state";
    case k_EResultNoConnection:
        return "no-connection";
    case k_EResultIgnored:
        return "ignored";
    case k_EResultLimitExceeded:
        return "limit-exceeded";
    case k_EResultTimeout:
        return "timeout";
    default:
        return "other";
    }
}

void onSteamAuthenticationStatusChanged(SteamNetAuthenticationStatus_t* status)
{
    if (status == nullptr) {
        return;
    }
    steamStdoutLog(
        std::string("Steam auth status ") +
        steamNetworkingAvailabilityLabel(status->m_eAvail) +
        ": " +
        status->m_debugMsg);
}

void onSteamRelayNetworkStatusChanged(SteamRelayNetworkStatus_t* status)
{
    if (status == nullptr) {
        return;
    }
    steamStdoutLog(
        std::string("Steam relay status ") +
        steamNetworkingAvailabilityLabel(status->m_eAvail) +
        " net=" +
        steamNetworkingAvailabilityLabel(status->m_eAvailNetworkConfig) +
        " any=" +
        steamNetworkingAvailabilityLabel(status->m_eAvailAnyRelay) +
        ": " +
        status->m_debugMsg);
}

const char* steamConnectionStateLabel(ESteamNetworkingConnectionState state)
{
    switch (state) {
    case k_ESteamNetworkingConnectionState_None:
        return "idle";
    case k_ESteamNetworkingConnectionState_Connecting:
        return "connecting";
    case k_ESteamNetworkingConnectionState_FindingRoute:
        return "finding route";
    case k_ESteamNetworkingConnectionState_Connected:
        return "connected";
    case k_ESteamNetworkingConnectionState_ClosedByPeer:
        return "closed by peer";
    case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        return "problem detected locally";
    case k_ESteamNetworkingConnectionState_FinWait:
        return "fin wait";
    case k_ESteamNetworkingConnectionState_Linger:
        return "linger";
    case k_ESteamNetworkingConnectionState_Dead:
        return "dead";
    default:
        return "unknown";
    }
}

std::string describeSteamConnectionFailure(const SteamNetConnectionInfo_t& info)
{
    std::ostringstream stream;
    stream << "Steam connection failed: " << steamConnectionStateLabel(info.m_eState);
    if (info.m_eEndReason != 0) {
        stream << " (" << info.m_eEndReason << ")";
    }
    std::string debug = info.m_szEndDebug;
    while (!debug.empty() && static_cast<unsigned char>(debug.back()) <= 0x20u) {
        debug.pop_back();
    }
    if (!debug.empty()) {
        stream << " - " << debug;
    }
    return stream.str();
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

void configureSteamSendBuffer(HSteamNetConnection connection)
{
    if (!steamApisReady() || connection == k_HSteamNetConnection_Invalid) {
        return;
    }
    const bool configured = SteamNetworkingUtils()->SetConnectionConfigValueInt32(
        connection,
        k_ESteamNetworkingConfig_SendBufferSize,
        kSteamSendBufferSizeBytes);
    if (!configured) {
        std::ostringstream stream;
        stream << "Steam send buffer config failed conn=" << connection
               << " bytes=" << kSteamSendBufferSizeBytes;
        steamStdoutLog(stream.str());
    }
}

void configureConnectionLanes(HSteamNetConnection connection)
{
    if (!steamApisReady() || connection == k_HSteamNetConnection_Invalid) {
        return;
    }
    configureSteamSendBuffer(connection);
    const EResult result = SteamNetworkingSockets()->ConfigureConnectionLanes(
        connection,
        static_cast<int>(kSteamLanePriorities.size()),
        kSteamLanePriorities.data(),
        kSteamLaneWeights.data());
    if (result != k_EResultOK) {
        std::ostringstream stream;
        stream << "Steam configure lanes failed conn=" << connection
               << " result=" << steamResultLabel(result)
               << " (" << static_cast<int>(result) << ")";
        steamStdoutLog(stream.str());
    }
}

class SteamSocketsTransport;

struct SteamTransportRegistry {
    std::mutex mutex;
    std::unordered_map<HSteamListenSocket, std::weak_ptr<SteamSocketsTransport>> listenSockets;
    std::unordered_map<HSteamNetConnection, std::weak_ptr<SteamSocketsTransport>> connections;
    std::unordered_map<std::int64_t, std::weak_ptr<SteamSocketsTransport>> userDataOwners;
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

    [[nodiscard]] std::string statusText() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return statusText_;
    }

    [[nodiscard]] bool hasTerminalFailure() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return terminalFailure_;
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

        const int clampedLane = std::clamp(lane, 0, kSteamLaneCount - 1);
        std::memcpy(message->m_pData, payload.data(), payload.size());
        message->m_cbSize = static_cast<int>(payload.size());
        message->m_conn = connection;
        message->m_idxLane = static_cast<std::uint16_t>(clampedLane);
        message->m_nFlags = reliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_UnreliableNoDelay;

        SteamNetworkingMessage_t* messages[1] { message };
        int64 result = 0;
        SteamNetworkingSockets()->SendMessages(1, messages, &result);
        if (result < 0) {
            const EResult error = static_cast<EResult>(-result);
            std::ostringstream stream;
            stream << "Steam send failed peer=" << peerId
                   << " lane=" << clampedLane
                   << " reliable=" << (reliable ? 1 : 0)
                   << " bytes=" << payload.size()
                   << " result=" << steamResultLabel(error)
                   << " (" << static_cast<int>(error) << ")";
            steamStdoutLog(stream.str());
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
            if (steamApisReady()) {
                (void)SteamNetworkingSockets()->SetConnectionUserData(update.m_hConn, static_cast<int64>(transportUserData_));
            }
            registerConnection(update.m_hConn);
            updateStatus(
                mode_ == Mode::Client
                    ? std::string("Steam P2P connecting to host")
                    : std::string("Steam peer connection incoming"),
                false);
            if (mode_ == Mode::Host && update.m_info.m_hListenSocket == listenSocket_) {
                if (SteamNetworkingSockets()->AcceptConnection(update.m_hConn) != k_EResultOK) {
                    updateStatus("Steam connection failed: accept rejected", true);
                    (void)SteamNetworkingSockets()->CloseConnection(update.m_hConn, 0, "Accept failed", false);
                }
            }
            break;

        case k_ESteamNetworkingConnectionState_FindingRoute:
            updateStatus("Steam P2P finding relay route", false);
            break;

        case k_ESteamNetworkingConnectionState_Connected: {
            const std::uint64_t peerId = steamIdentityToPeerId(update.m_info.m_identityRemote, update.m_hConn);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                ready_ = true;
                terminalFailure_ = false;
                peerConnections_[peerId] = update.m_hConn;
                connectionPeers_[update.m_hConn] = peerId;
                statusText_ =
                    mode_ == Mode::Client
                        ? "Steam P2P connected to host"
                        : ("Steam peer connected " + std::to_string(peerId));
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
            const bool clientOutboundFailure =
                mode_ == Mode::Client && outboundConnection_ != k_HSteamNetConnection_Invalid && update.m_hConn == outboundConnection_;
            const std::string failureDetail = describeSteamConnectionFailure(update.m_info);
            if (clientOutboundFailure) {
                updateStatus(failureDetail, true);
            } else if (mode_ == Mode::Host && peerId != 0) {
                updateStatus("Steam peer disconnected " + std::to_string(peerId) + " - " + failureDetail, false);
            } else {
                updateStatus(failureDetail, false);
            }
            removeConnection(update.m_hConn, peerId, clientOutboundFailure);
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
        , transportUserData_(nextTransportUserDataToken().fetch_add(1, std::memory_order_relaxed))
    {
    }

    static std::atomic<std::int64_t>& nextTransportUserDataToken()
    {
        static std::atomic<std::int64_t> nextToken { 1 };
        return nextToken;
    }

    static std::array<SteamNetworkingConfigValue_t, 2> buildCreateOptions(std::int64_t transportUserData)
    {
        std::array<SteamNetworkingConfigValue_t, 2> options {};
        options[0].SetPtr(
            k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
            (void*)(&SteamSocketsTransport::onConnectionStatusChanged));
        options[1].SetInt64(k_ESteamNetworkingConfig_ConnectionUserData, transportUserData);
        return options;
    }

    bool initializeHost(std::string* statusText)
    {
        if (!steamApisReady()) {
            if (statusText != nullptr) {
                *statusText = "Steam networking unavailable.";
            }
            steamStdoutLog("Steam networking unavailable.");
            return false;
        }

        installGlobalCallback();
        SteamNetworkingUtils()->InitRelayNetworkAccess();
        registerTransportUserData();

        pollGroup_ = SteamNetworkingSockets()->CreatePollGroup();
        const auto options = buildCreateOptions(transportUserData_);
        listenSocket_ = SteamNetworkingSockets()->CreateListenSocketP2P(
            virtualPort_,
            static_cast<int>(options.size()),
            options.data());
        if (listenSocket_ == k_HSteamListenSocket_Invalid) {
            if (pollGroup_ != k_HSteamNetPollGroup_Invalid) {
                SteamNetworkingSockets()->DestroyPollGroup(pollGroup_);
                pollGroup_ = k_HSteamNetPollGroup_Invalid;
            }
            if (statusText != nullptr) {
                *statusText = "Failed to create Steam P2P listen socket.";
            }
            steamStdoutLog("Failed to create Steam P2P listen socket.");
            return false;
        }

        registerListenSocket();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ready_ = true;
            terminalFailure_ = false;
            statusText_ = "Steam host listen socket active";
        }
        if (statusText != nullptr) {
            std::ostringstream stream;
            stream << "Steam host listen socket active on virtual port " << virtualPort_;
            if (SteamUser() != nullptr) {
                stream << " as " << SteamUser()->GetSteamID().ConvertToUint64();
            }
            *statusText = stream.str();
        }
        steamStdoutLog("Steam host listen socket active on virtual port " + std::to_string(virtualPort_));
        return true;
    }

    bool initializeClient(std::uint64_t hostSteamId, std::string* statusText)
    {
        if (!steamApisReady()) {
            if (statusText != nullptr) {
                *statusText = "Steam networking unavailable.";
            }
            steamStdoutLog("Steam networking unavailable.");
            return false;
        }
        if (hostSteamId == 0) {
            if (statusText != nullptr) {
                *statusText = "No host SteamID was provided.";
            }
            steamStdoutLog("No host SteamID was provided.");
            return false;
        }

        installGlobalCallback();
        SteamNetworkingUtils()->InitRelayNetworkAccess();
        registerTransportUserData();

        pollGroup_ = SteamNetworkingSockets()->CreatePollGroup();

        SteamNetworkingIdentity remoteIdentity;
        remoteIdentity.Clear();
        remoteIdentity.SetSteamID(CSteamID(hostSteamId));
        const auto options = buildCreateOptions(transportUserData_);
        outboundConnection_ = SteamNetworkingSockets()->ConnectP2P(
            remoteIdentity,
            virtualPort_,
            static_cast<int>(options.size()),
            options.data());
        if (outboundConnection_ == k_HSteamNetConnection_Invalid) {
            if (pollGroup_ != k_HSteamNetPollGroup_Invalid) {
                SteamNetworkingSockets()->DestroyPollGroup(pollGroup_);
                pollGroup_ = k_HSteamNetPollGroup_Invalid;
            }
            if (statusText != nullptr) {
                *statusText = "Failed to start Steam P2P connection.";
            }
            steamStdoutLog("Failed to start Steam P2P connection.");
            return false;
        }

        registerConnection(outboundConnection_);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            terminalFailure_ = false;
            statusText_ = "Connecting to Steam host";
        }
        if (statusText != nullptr) {
            std::ostringstream stream;
            stream << "Connecting to Steam host " << hostSteamId << " on virtual port " << virtualPort_;
            *statusText = stream.str();
        }
        steamStdoutLog("Connecting to Steam host " + std::to_string(hostSteamId) + " on virtual port " + std::to_string(virtualPort_));
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
        terminalFailure_ = false;
        peerConnections_.clear();
        connectionPeers_.clear();
        events_.clear();
        outboundConnection_ = k_HSteamNetConnection_Invalid;
        statusText_.clear();
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

    void updateStatus(const std::string& value, bool terminalFailure)
    {
        bool changed = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            changed = statusText_ != value || terminalFailure_ != terminalFailure;
            statusText_ = value;
            terminalFailure_ = terminalFailure;
            if (terminalFailure) {
                ready_ = false;
            }
        }
        if (changed && !value.empty()) {
            steamStdoutLog(value + (terminalFailure ? " [terminal]" : ""));
        }
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

    void registerTransportUserData()
    {
        std::lock_guard<std::mutex> lock(steamTransportRegistry().mutex);
        steamTransportRegistry().userDataOwners[transportUserData_] = weak_from_this();
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
        steamTransportRegistry().userDataOwners.erase(transportUserData_);
    }

    static void installGlobalCallback()
    {
        SteamTransportRegistry& registry = steamTransportRegistry();
        std::lock_guard<std::mutex> lock(registry.mutex);
        if (registry.callbackInstalled || !steamApisReady()) {
            return;
        }
        const bool sendBufferConfigured =
            SteamNetworkingUtils()->SetGlobalConfigValueInt32(
                k_ESteamNetworkingConfig_SendBufferSize,
                kSteamSendBufferSizeBytes);
        const bool connectionInstalled =
            SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(&SteamSocketsTransport::onConnectionStatusChanged);
        const bool authInstalled =
            SteamNetworkingUtils()->SetGlobalCallback_SteamNetAuthenticationStatusChanged(&onSteamAuthenticationStatusChanged);
        const bool relayInstalled =
            SteamNetworkingUtils()->SetGlobalCallback_SteamRelayNetworkStatusChanged(&onSteamRelayNetworkStatusChanged);
        registry.callbackInstalled = connectionInstalled && authInstalled && relayInstalled;
        steamStdoutLog(
            "Steam callbacks connection=" +
            std::string(connectionInstalled ? "ok" : "failed") +
            " auth=" +
            (authInstalled ? "ok" : "failed") +
            " relay=" +
            (relayInstalled ? "ok" : "failed") +
            " sendbuf=" +
            (sendBufferConfigured ? "ok" : "failed"));
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
            if (!transport && update->m_info.m_nUserData > 0) {
                const auto userDataIt = registry.userDataOwners.find(static_cast<std::int64_t>(update->m_info.m_nUserData));
                if (userDataIt != registry.userDataOwners.end()) {
                    transport = userDataIt->second.lock();
                }
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
        } else {
            steamStdoutLog(
                "unowned Steam callback state=" +
                std::string(steamConnectionStateLabel(update->m_info.m_eState)) +
                " conn=" +
                std::to_string(update->m_hConn) +
                " listen=" +
                std::to_string(update->m_info.m_hListenSocket) +
                " user=" +
                std::to_string(static_cast<long long>(update->m_info.m_nUserData)));
            if (steamApisReady() &&
                (update->m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer ||
                 update->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)) {
                (void)SteamNetworkingSockets()->CloseConnection(update->m_hConn, 0, "Unowned connection closed", false);
            }
        }
    }

    mutable std::mutex mutex_;
    std::deque<NetEvent> events_;
    std::unordered_map<NetPeerId, HSteamNetConnection> peerConnections_;
    std::unordered_map<HSteamNetConnection, NetPeerId> connectionPeers_;
    Mode mode_ = Mode::Host;
    int virtualPort_ = 0;
    bool ready_ = false;
    bool terminalFailure_ = false;
    std::string statusText_;
    std::int64_t transportUserData_ = 0;
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
    if (SteamUser() != nullptr) {
        const CSteamID localSteamId = SteamUser()->GetSteamID();
        state_.localSteamId = localSteamId.ConvertToUint64();
        state_.personaName = steamPersonaName(localSteamId);
    }
    if (steamApisReady()) {
        SteamNetworkingUtils()->InitRelayNetworkAccess();
        std::ostringstream stream;
        stream << "Steam initialized";
        if (state_.localSteamId != 0) {
            if (!state_.personaName.empty()) {
                stream << " as " << state_.personaName << " (" << state_.localSteamId << ")";
            } else {
                stream << " as " << state_.localSteamId;
            }
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
        if (SteamNetworkingSockets() != nullptr) {
            SteamNetworkingSockets()->RunCallbacks();
        }
        if (SteamUtils() != nullptr) {
            state_.runningAppId = static_cast<std::uint32_t>(SteamUtils()->GetAppID());
            state_.overlayEnabled = SteamUtils()->IsOverlayEnabled();
        }
        if (SteamUser() != nullptr) {
            const CSteamID localSteamId = SteamUser()->GetSteamID();
            state_.localSteamId = localSteamId.ConvertToUint64();
            state_.personaName = steamPersonaName(localSteamId);
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
    return {};
#else
    (void)virtualPort;
    if (statusText != nullptr) {
        *statusText = "Steamworks not compiled; host transport unavailable.";
    }
    return {};
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
    return {};
#else
    (void)hostSteamId;
    (void)virtualPort;
    if (statusText != nullptr) {
        *statusText = "Steamworks not compiled; client transport unavailable.";
    }
    return {};
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
    std::vector<SteamDiscoveredLobby> discoveredLobbies;
    std::uint64_t selectedDiscoveredLobby = 0;
    std::chrono::steady_clock::time_point nextDiscoveryRefreshAt {};

    void scheduleDiscoveryRefresh(std::chrono::milliseconds delay = std::chrono::milliseconds::zero())
    {
        nextDiscoveryRefreshAt = std::chrono::steady_clock::now() + delay;
    }

    [[nodiscard]] std::size_t resolvedSelectedDiscoveredLobbyIndex() const
    {
        if (selectedDiscoveredLobby == 0) {
            return discoveredLobbies.size();
        }
        for (std::size_t index = 0; index < discoveredLobbies.size(); ++index) {
            if (discoveredLobbies[index].lobbyId == selectedDiscoveredLobby) {
                return index;
            }
        }
        return discoveredLobbies.size();
    }

    [[nodiscard]] std::uint64_t resolvedSelectedDiscoveredLobbyId() const
    {
        const std::size_t index = resolvedSelectedDiscoveredLobbyIndex();
        return index < discoveredLobbies.size() ? discoveredLobbies[index].lobbyId : 0ull;
    }

    void refreshDiscoveredLobbies(bool force = false)
    {
        using namespace std::chrono_literals;

        if (!runtime.available() || SteamFriends() == nullptr) {
            discoveredLobbies.clear();
            selectedDiscoveredLobby = 0;
            nextDiscoveryRefreshAt = {};
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (!force && nextDiscoveryRefreshAt != std::chrono::steady_clock::time_point {} && now < nextDiscoveryRefreshAt) {
            return;
        }
        nextDiscoveryRefreshAt = now + 3s;

        std::unordered_map<std::uint64_t, SteamDiscoveredLobby> discoveredByLobbyId;
        const int friendCount = SteamFriends()->GetFriendCount(k_EFriendFlagImmediate);
        for (int friendIndex = 0; friendIndex < friendCount; ++friendIndex) {
            const CSteamID friendSteamId = SteamFriends()->GetFriendByIndex(friendIndex, k_EFriendFlagImmediate);
            if (!friendSteamId.IsValid()) {
                continue;
            }

            FriendGameInfo_t gameInfo {};
            if (!SteamFriends()->GetFriendGamePlayed(friendSteamId, &gameInfo) || !gameInfo.m_gameID.IsValid()) {
                continue;
            }

            const std::uint32_t friendAppId = static_cast<std::uint32_t>(gameInfo.m_gameID.AppID());
            const std::uint32_t expectedAppId =
                buildConfig.appId != 0 ? buildConfig.appId : (runtime.state().appId != 0 ? runtime.state().appId : TRUEFLIGHT_STEAM_APP_ID);
            if (expectedAppId != 0 && friendAppId != expectedAppId) {
                continue;
            }
            if (!gameInfo.m_steamIDLobby.IsValid()) {
                continue;
            }

            const std::uint64_t lobbyId = gameInfo.m_steamIDLobby.ConvertToUint64();
            if (lobbyId == 0 || lobbyId == lobby.lobbyId) {
                continue;
            }

            SteamDiscoveredLobby& discovered = discoveredByLobbyId[lobbyId];
            if (discovered.lobbyId == 0) {
                discovered.lobbyId = lobbyId;
                discovered.appId = friendAppId;
            }

            if (discovered.sourceFriendPersonaName.empty()) {
                discovered.sourceFriendPersonaName = steamPersonaName(friendSteamId);
            }

            if (SteamMatchmaking() != nullptr) {
                const CSteamID remoteLobby(lobbyId);
                bool needsLobbyData = false;
                std::string value;
                if (readLobbyData(remoteLobby, "protocol_version", &value)) {
                    discovered.protocolVersion = value;
                } else {
                    needsLobbyData = true;
                }
                if (readLobbyData(remoteLobby, "build_id", &value)) {
                    discovered.buildId = value;
                } else {
                    needsLobbyData = true;
                }
                if (readLobbyData(remoteLobby, "world_id", &value)) {
                    discovered.worldId = value;
                } else {
                    needsLobbyData = true;
                }
                if (readLobbyData(remoteLobby, "session_nonce", &value)) {
                    discovered.sessionNonce = value;
                } else {
                    needsLobbyData = true;
                }
                if (readLobbyData(remoteLobby, "joinable", &value)) {
                    discovered.joinable = value != "0" && value != "false";
                } else {
                    needsLobbyData = true;
                }
                if (readLobbyData(remoteLobby, "max_players", &value)) {
                    discovered.maxPlayers = std::max(0, std::atoi(value.c_str()));
                } else {
                    needsLobbyData = true;
                }
                if (readLobbyData(remoteLobby, "host_steam_id", &value)) {
                    discovered.hostSteamId = std::strtoull(value.c_str(), nullptr, 10);
                } else if (discovered.hostSteamId == 0) {
                    needsLobbyData = true;
                }
                if (needsLobbyData) {
                    (void)SteamMatchmaking()->RequestLobbyData(remoteLobby);
                }
            }

            if (discovered.hostSteamId == runtime.state().localSteamId) {
                continue;
            }
            if (discovered.hostSteamId != 0 && discovered.hostPersonaName.empty()) {
                discovered.hostPersonaName = steamPersonaName(CSteamID(discovered.hostSteamId));
            }
            if (discovered.hostPersonaName.empty()) {
                discovered.hostPersonaName = discovered.sourceFriendPersonaName;
            }
        }

        std::vector<SteamDiscoveredLobby> refreshed;
        refreshed.reserve(discoveredByLobbyId.size());
        for (auto& [lobbyId, discovered] : discoveredByLobbyId) {
            (void)lobbyId;
            if (discovered.lobbyId == 0 || discovered.hostSteamId == runtime.state().localSteamId) {
                continue;
            }
            refreshed.push_back(std::move(discovered));
        }
        std::sort(
            refreshed.begin(),
            refreshed.end(),
            [](const SteamDiscoveredLobby& lhs, const SteamDiscoveredLobby& rhs) {
                const std::string lhsName = !lhs.hostPersonaName.empty() ? lhs.hostPersonaName : lhs.sourceFriendPersonaName;
                const std::string rhsName = !rhs.hostPersonaName.empty() ? rhs.hostPersonaName : rhs.sourceFriendPersonaName;
                if (lhsName != rhsName) {
                    return lhsName < rhsName;
                }
                return lhs.lobbyId < rhs.lobbyId;
            });

        const std::uint64_t preferredLobbyId = selectedDiscoveredLobby;
        discoveredLobbies = std::move(refreshed);
        if (discoveredLobbies.empty()) {
            selectedDiscoveredLobby = 0;
            return;
        }

        auto selectedIt = std::find_if(
            discoveredLobbies.begin(),
            discoveredLobbies.end(),
            [preferredLobbyId](const SteamDiscoveredLobby& discovered) {
                return preferredLobbyId != 0 && discovered.lobbyId == preferredLobbyId;
            });
        if (selectedIt == discoveredLobbies.end()) {
            selectedDiscoveredLobby = discoveredLobbies.front().lobbyId;
        }
    }

    void cycleDiscoveredLobbySelection(int delta)
    {
        refreshDiscoveredLobbies(true);
        if (discoveredLobbies.empty() || delta == 0) {
            return;
        }

        const std::size_t currentIndex = resolvedSelectedDiscoveredLobbyIndex();
        const std::size_t startIndex = currentIndex < discoveredLobbies.size() ? currentIndex : 0u;
        const std::size_t count = discoveredLobbies.size();
        const std::size_t nextIndex =
            delta > 0
                ? ((startIndex + 1u) % count)
                : ((startIndex + count - 1u) % count);
        selectedDiscoveredLobby = discoveredLobbies[nextIndex].lobbyId;
    }

    void refreshLobbyMemberSnapshot()
    {
        lobby.memberCount = 0;
        lobby.memberNames.clear();
        lobby.localPersonaName = runtime.state().personaName;
        lobby.hostPersonaName.clear();

        if (lobby.hostSteamId != 0) {
            lobby.hostPersonaName = steamPersonaName(CSteamID(lobby.hostSteamId));
        }

        if (SteamMatchmaking() == nullptr || lobby.lobbyId == 0) {
            return;
        }

        const CSteamID lobbySteamId(lobby.lobbyId);
        lobby.memberCount = std::max(0, SteamMatchmaking()->GetNumLobbyMembers(lobbySteamId));
        if (lobby.maxPlayers <= 0) {
            lobby.maxPlayers = std::max(1, SteamMatchmaking()->GetLobbyMemberLimit(lobbySteamId));
        }
        if (lobby.hostSteamId == 0) {
            lobby.hostSteamId = SteamMatchmaking()->GetLobbyOwner(lobbySteamId).ConvertToUint64();
        }
        if (lobby.hostSteamId != 0 && lobby.hostPersonaName.empty()) {
            lobby.hostPersonaName = steamPersonaName(CSteamID(lobby.hostSteamId));
        }

        lobby.memberNames.reserve(static_cast<std::size_t>(lobby.memberCount));
        for (int index = 0; index < lobby.memberCount; ++index) {
            const CSteamID memberSteamId = SteamMatchmaking()->GetLobbyMemberByIndex(lobbySteamId, index);
            std::string name = steamPersonaName(memberSteamId);
            if (name.empty()) {
                name = std::to_string(memberSteamId.ConvertToUint64());
            }
            if (memberSteamId.ConvertToUint64() == lobby.hostSteamId) {
                name += " [host]";
            }
            if (memberSteamId.ConvertToUint64() == lobby.localSteamId) {
                name += " [you]";
            }
            lobby.memberNames.push_back(std::move(name));
        }
    }

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
        steamStdoutLog("finalizing Steam lobby join for lobby " + std::to_string(lobbySteamId.ConvertToUint64()));
        if (SteamMatchmaking() == nullptr) {
            clearLobbySnapshot(true);
            lobby.status = "Steam matchmaking unavailable";
            steamStdoutLog(lobby.status);
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
            steamStdoutLog(lobby.status);
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
            steamStdoutLog(lobby.status);
            return false;
        }

        if (!pendingSettings.protocolVersion.empty() && protocolVersion != pendingSettings.protocolVersion) {
            clearLobbySnapshot(true);
            lobby.status = "Protocol mismatch";
            steamStdoutLog(lobby.status);
            return true;
        }
        if (!pendingSettings.buildId.empty() && buildId != pendingSettings.buildId) {
            clearLobbySnapshot(true);
            lobby.status = "Build mismatch";
            steamStdoutLog(lobby.status);
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
        {
            std::ostringstream stream;
            stream << "lobby metadata ready"
                   << " lobby=" << lobby.lobbyId
                   << " host=" << lobby.hostSteamId
                   << " world=" << lobby.worldId
                   << " build=" << lobby.buildId
                   << " protocol=" << lobby.protocolVersion;
            steamStdoutLog(stream.str());
        }

        if (lobby.transport == nullptr) {
            std::string transportStatus;
            lobby.transport = createSteamClientTransport(lobby.hostSteamId, pendingSettings.virtualPort, &transportStatus);
            if (lobby.transport == nullptr) {
                clearLobbySnapshot(true);
                lobby.status = transportStatus.empty() ? "Steam client transport unavailable" : transportStatus;
                steamStdoutLog(lobby.status);
                return true;
            }
        }

        lobby.transportReady = lobby.transport->ready();
        lobby.status = lobby.transportReady ? "Joined Steam lobby" : "Connecting to Steam host";
        steamStdoutLog(lobby.status);
        refreshLobbyMemberSnapshot();
        return true;
    }

    bool initialize(const SteamBuildConfig& config, std::string* statusText)
    {
        buildConfig = config;
        clearLobbySnapshot(false);
        unregisterCallbacks();
        discoveredLobbies.clear();
        selectedDiscoveredLobby = 0;
        nextDiscoveryRefreshAt = {};
        const bool initialized = runtime.initialize(config, statusText);
        if (initialized) {
            registerCallbacks();
            scheduleDiscoveryRefresh();
            refreshDiscoveredLobbies(true);
        }
        return initialized;
    }

    void shutdown()
    {
        leaveLobby();
        unregisterCallbacks();
        lobbyCreatedResult.Cancel();
        lobbyEnteredResult.Cancel();
        discoveredLobbies.clear();
        selectedDiscoveredLobby = 0;
        nextDiscoveryRefreshAt = {};
        runtime.shutdown();
    }

    void pump()
    {
        runtime.pump();
        refreshDiscoveredLobbies();
        lobby.transportReady = lobby.transport != nullptr && lobby.transport->ready();
        if (auto steamTransport = std::dynamic_pointer_cast<SteamSocketsTransport>(lobby.transport); steamTransport != nullptr) {
            const std::string transportStatus = steamTransport->statusText();
            if (!transportStatus.empty()) {
                if (steamTransport->hasTerminalFailure()) {
                    lobby.status = transportStatus;
                } else if (lobby.role == SteamLobbyState::Role::Client && !lobby.transportReady) {
                    lobby.status = transportStatus;
                }
            }
        }
        refreshLobbyMemberSnapshot();
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
        steamStdoutLog("joining Steam lobby " + std::to_string(lobbyId));

        const SteamAPICall_t call = SteamMatchmaking()->JoinLobby(CSteamID(lobbyId));
        if (call == k_uAPICallInvalid) {
            clearLobbySnapshot(true);
            lobby.status = "JoinLobby failed";
            steamStdoutLog(lobby.status);
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
            steamStdoutLog(lobby.status);
            setStatus(statusText, lobby.status);
            return false;
        }
        lobby.transportReady = lobby.transport->ready();
        lobby.lobbyReady = false;
        lobby.status = "Connecting to Steam host";
        steamStdoutLog(
            "direct Steam host connect host=" + std::to_string(hostSteamId) +
            " port=" + std::to_string(settings.virtualPort));
        setStatus(statusText, lobby.status);
        return true;
    }

    void leaveLobby()
    {
        lobbyCreatedResult.Cancel();
        lobbyEnteredResult.Cancel();
        clearLobbySnapshot(true);
        scheduleDiscoveryRefresh();
        refreshDiscoveredLobbies(true);
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
        steamStdoutLog("queued pending Steam join for lobby " + std::to_string(lobbyId));
        setStatus(statusText, lobby.status);
    }

    void selectNextDiscoveredLobby(int delta)
    {
        cycleDiscoveredLobbySelection(delta);
    }

    [[nodiscard]] std::uint64_t pendingJoinLobbyId() const
    {
        return hasPendingJoinRequest() ? lobby.pendingLobbyId : 0;
    }

    [[nodiscard]] std::size_t selectedDiscoveredLobbyIndex() const
    {
        return resolvedSelectedDiscoveredLobbyIndex();
    }

    [[nodiscard]] std::uint64_t selectedDiscoveredLobbyId() const
    {
        return resolvedSelectedDiscoveredLobbyId();
    }

    [[nodiscard]] const std::vector<SteamDiscoveredLobby>& discoveredLobbiesView() const
    {
        return this->discoveredLobbies;
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
            steamStdoutLog(lobby.status);
            return;
        }

        lobby.lobbyId = result->m_ulSteamIDLobby;
        lobby.localSteamId = SteamUser() != nullptr ? SteamUser()->GetSteamID().ConvertToUint64() : 0;
        lobby.hostSteamId = lobby.localSteamId;
        lobby.lobbyReady = true;
        lobby.transport = createSteamHostTransport(lobby.virtualPort, nullptr);
        lobby.transportReady = lobby.transport != nullptr && lobby.transport->ready();
        lobby.status = lobby.transportReady ? "Steam lobby ready" : "Steam host transport unavailable";
        steamStdoutLog(
            "created Steam lobby " + std::to_string(lobby.lobbyId) +
            " host=" + std::to_string(lobby.hostSteamId));
        steamStdoutLog(lobby.status);

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
        refreshLobbyMemberSnapshot();
        publishRichPresence();
        scheduleDiscoveryRefresh();
    }

    void onLobbyEntered(LobbyEnter_t* result, bool ioFailure)
    {
        if (result == nullptr || ioFailure || result->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess) {
            clearLobbySnapshot(true);
            lobby.status = "Lobby join failed";
            steamStdoutLog(lobby.status);
            return;
        }

        const CSteamID lobbySteamId(result->m_ulSteamIDLobby);
        lobby.lobbyId = result->m_ulSteamIDLobby;
        lobby.localSteamId = SteamUser() != nullptr ? SteamUser()->GetSteamID().ConvertToUint64() : 0;
        lobby.lobbyReady = false;
        steamStdoutLog("entered Steam lobby " + std::to_string(lobby.lobbyId));
        refreshLobbyMemberSnapshot();
        clearRichPresence();
        if (!finalizeJoinedLobby(lobbySteamId)) {
            (void)SteamMatchmaking()->RequestLobbyData(lobbySteamId);
        }
        scheduleDiscoveryRefresh();
    }

    void onGameLobbyJoinRequested(GameLobbyJoinRequested_t* callback)
    {
        if (callback == nullptr) {
            return;
        }
        setPendingJoin(callback->m_steamIDLobby.ConvertToUint64());
        steamStdoutLog("Steam overlay requested join for lobby " + std::to_string(callback->m_steamIDLobby.ConvertToUint64()));
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
        steamStdoutLog("Steam rich presence requested join for lobby " + std::to_string(lobbyId));
    }

    void onLobbyDataUpdate(LobbyDataUpdate_t* callback)
    {
        if (callback == nullptr || callback->m_ulSteamIDLobby == 0 || callback->m_bSuccess == 0) {
            return;
        }
        steamStdoutLog("Steam lobby data updated for lobby " + std::to_string(callback->m_ulSteamIDLobby));
        scheduleDiscoveryRefresh(std::chrono::milliseconds(250));
        if (lobby.lobbyId == callback->m_ulSteamIDLobby) {
            refreshLobbyMemberSnapshot();
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
    std::vector<SteamDiscoveredLobby> discoveredLobbies;
    std::uint64_t selectedDiscoveredLobby = 0;

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
        discoveredLobbies.clear();
        selectedDiscoveredLobby = 0;
        runtime.shutdown();
    }

    void pump()
    {
        runtime.pump();
        if (!discoveredLobbies.empty() && selectedDiscoveredLobby == 0) {
            selectedDiscoveredLobby = discoveredLobbies.front().lobbyId;
        }
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
        discoveredLobbies.clear();
        selectedDiscoveredLobby = 0;
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
        selectedDiscoveredLobby = lobbyId;
        lobby.status = "Pending Steam lobby invite #" + std::to_string(lobbyId);
        setStatus(statusText, lobby.status);
    }

    void selectNextDiscoveredLobby(int delta)
    {
        if (discoveredLobbies.empty() || delta == 0) {
            return;
        }

        auto currentIt = std::find_if(
            discoveredLobbies.begin(),
            discoveredLobbies.end(),
            [this](const SteamDiscoveredLobby& discovered) {
                return discovered.lobbyId == selectedDiscoveredLobby;
            });
        const std::size_t count = discoveredLobbies.size();
        const std::size_t currentIndex =
            currentIt == discoveredLobbies.end()
                ? 0u
                : static_cast<std::size_t>(std::distance(discoveredLobbies.begin(), currentIt));
        const std::size_t nextIndex =
            delta > 0
                ? ((currentIndex + 1u) % count)
                : ((currentIndex + count - 1u) % count);
        selectedDiscoveredLobby = discoveredLobbies[nextIndex].lobbyId;
    }

    [[nodiscard]] std::uint64_t pendingJoinLobbyId() const
    {
        return hasPendingJoinRequest() ? lobby.pendingLobbyId : 0;
    }

    [[nodiscard]] std::size_t selectedDiscoveredLobbyIndex() const
    {
        auto it = std::find_if(
            discoveredLobbies.begin(),
            discoveredLobbies.end(),
            [this](const SteamDiscoveredLobby& discovered) {
                return discovered.lobbyId == selectedDiscoveredLobby;
            });
        return it == discoveredLobbies.end()
            ? discoveredLobbies.size()
            : static_cast<std::size_t>(std::distance(discoveredLobbies.begin(), it));
    }

    [[nodiscard]] std::uint64_t selectedDiscoveredLobbyId() const
    {
        return selectedDiscoveredLobby;
    }

    [[nodiscard]] const std::vector<SteamDiscoveredLobby>& discoveredLobbiesView() const
    {
        return discoveredLobbies;
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

void SteamOnlineController::selectNextDiscoveredLobby(int delta)
{
    if (impl_ != nullptr) {
        impl_->selectNextDiscoveredLobby(delta);
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

std::size_t SteamOnlineController::selectedDiscoveredLobbyIndex() const
{
    return impl_ != nullptr ? impl_->selectedDiscoveredLobbyIndex() : 0u;
}

std::uint64_t SteamOnlineController::selectedDiscoveredLobbyId() const
{
    return impl_ != nullptr ? impl_->selectedDiscoveredLobbyId() : 0ull;
}

const std::vector<SteamDiscoveredLobby>& SteamOnlineController::discoveredLobbies() const
{
    static const std::vector<SteamDiscoveredLobby> kEmptyDiscoveredLobbies {};
    return impl_ != nullptr ? impl_->discoveredLobbiesView() : kEmptyDiscoveredLobbies;
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
