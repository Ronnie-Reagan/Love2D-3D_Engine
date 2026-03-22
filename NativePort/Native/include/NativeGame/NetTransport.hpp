#pragma once

#include "NativeGame/NetProtocol.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace NativeGame {

using NetPeerId = std::uint64_t;

struct NetEvent {
    enum class Type : std::uint8_t {
        Connected = 0,
        Disconnected = 1,
        Message = 2
    };

    Type type = Type::Message;
    NetPeerId peerId = 0;
    int lane = 0;
    bool reliable = false;
    std::string payload;
};

class INetTransport {
public:
    virtual ~INetTransport() = default;

    virtual bool ready() const = 0;
    virtual bool send(NetPeerId peerId, int lane, std::string_view payload, bool reliable) = 0;
    virtual void disconnectPeer(NetPeerId peerId) = 0;
    virtual std::vector<NetEvent> poll() = 0;
    virtual std::vector<NetPeerId> peers() const = 0;
};

class NullTransport final : public INetTransport {
public:
    [[nodiscard]] bool ready() const override
    {
        return false;
    }

    bool send(NetPeerId, int, std::string_view, bool) override
    {
        return false;
    }

    void disconnectPeer(NetPeerId) override
    {
    }

    [[nodiscard]] std::vector<NetEvent> poll() override
    {
        return {};
    }

    [[nodiscard]] std::vector<NetPeerId> peers() const override
    {
        return {};
    }
};

class LoopbackTransport final : public INetTransport, public std::enable_shared_from_this<LoopbackTransport> {
public:
    static std::pair<std::shared_ptr<LoopbackTransport>, std::shared_ptr<LoopbackTransport>> createLinkedPair()
    {
        auto a = std::shared_ptr<LoopbackTransport>(new LoopbackTransport());
        auto b = std::shared_ptr<LoopbackTransport>(new LoopbackTransport());
        a->peer_ = b;
        b->peer_ = a;
        a->localPeerId_ = 1;
        b->localPeerId_ = 2;
        a->remotePeerId_ = 2;
        b->remotePeerId_ = 1;
        a->pushEvent({ NetEvent::Type::Connected, a->remotePeerId_, 0, true, {} });
        b->pushEvent({ NetEvent::Type::Connected, b->remotePeerId_, 0, true, {} });
        return { a, b };
    }

    [[nodiscard]] bool ready() const override
    {
        return true;
    }

    bool send(NetPeerId peerId, int lane, std::string_view payload, bool reliable) override
    {
        const std::shared_ptr<LoopbackTransport> peer = peer_.lock();
        if (!peer || peerId != remotePeerId_) {
            return false;
        }
        peer->pushEvent({ NetEvent::Type::Message, localPeerId_, lane, reliable, std::string(payload) });
        return true;
    }

    void disconnectPeer(NetPeerId peerId) override
    {
        const std::shared_ptr<LoopbackTransport> peer = peer_.lock();
        if (!peer || peerId != remotePeerId_) {
            return;
        }
        pushEvent({ NetEvent::Type::Disconnected, remotePeerId_, 0, true, {} });
        peer->pushEvent({ NetEvent::Type::Disconnected, localPeerId_, 0, true, {} });
        peer_.reset();
    }

    [[nodiscard]] std::vector<NetEvent> poll() override
    {
        std::vector<NetEvent> events;
        std::lock_guard<std::mutex> lock(mutex_);
        while (!events_.empty()) {
            events.push_back(std::move(events_.front()));
            events_.pop_front();
        }
        return events;
    }

    [[nodiscard]] std::vector<NetPeerId> peers() const override
    {
        if (peer_.expired()) {
            return {};
        }
        return { remotePeerId_ };
    }

private:
    void pushEvent(NetEvent event)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(std::move(event));
    }

    mutable std::mutex mutex_;
    std::deque<NetEvent> events_;
    std::weak_ptr<LoopbackTransport> peer_;
    NetPeerId localPeerId_ = 0;
    NetPeerId remotePeerId_ = 0;
};

}  // namespace NativeGame
