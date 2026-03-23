#pragma once

#include <unordered_set>

#include <boost/signals2/signal.hpp>

#include <centrifugo/subscription.h>
#include "transport.h"

namespace centrifugo {

class SubscriptionImpl
{
public:
    using SubscribingSignal = boost::signals2::signal<void()>;
    using SubscribedSignal = boost::signals2::signal<void()>;
    using UnsubscribedSignal = boost::signals2::signal<void()>;
    using PublicationSignal = boost::signals2::signal<void(Publication const &)>;
    using ErrorSignal = boost::signals2::signal<void(Error const &)>;

    SubscriptionImpl(std::string const &channel, Transport &transport);
    ~SubscriptionImpl();

    SubscriptionImpl(SubscriptionImpl const &) = delete;
    auto operator=(SubscriptionImpl const &) -> SubscriptionImpl & = delete;

    SubscriptionImpl(SubscriptionImpl &&other) noexcept;
    // deleted because I don't need it and don't want to implement it
    auto operator=(SubscriptionImpl &&other) noexcept -> SubscriptionImpl & = delete;

    auto state() const -> SubscriptionState;
    auto channel() const -> std::string const &;
    auto subscription() -> Subscription &;

    auto subscribe() -> outcome::result<void, std::string>;
    auto unsubscribe() -> void;
    auto publish(nlohmann::json const &json) -> outcome::result<void, Error>;

    auto handleReply(Reply const &reply) -> bool;
    auto handlePublish(Publication const &publication) -> void;

    auto onSubscribing() -> SubscribingSignal &;
    auto onSubscribed() -> SubscribedSignal &;
    auto onUnsubscribed() -> UnsubscribedSignal &;
    auto onPublication() -> PublicationSignal &;
    auto onError() -> ErrorSignal &;

private:
    auto init() -> void;
    auto deinit() -> void;
    auto sendCmd(Command &&cmd) -> void;
    auto sendSubscribeCmd() -> void;
    auto setState(SubscriptionState newState) -> void;

private:
    std::string channel_;
    Transport &transport_;

    Subscription subscription_;
    SubscriptionState state_ = SubscriptionState::UNSUBSCRIBED;
    std::unordered_set<std::uint32_t> waitingReplies_;

    // Stream recovery state
    std::string epoch_;
    std::uint64_t offset_ {0};
    bool recoverable_ {false};

    SubscribingSignal subscribingSignal_;
    SubscribedSignal subscribedSignal_;
    UnsubscribedSignal unsubscribedSignal_;
    PublicationSignal publicationSignal_;
    ErrorSignal errorSignal_;

    boost::signals2::connection onConnectingConnection_;
    boost::signals2::connection onConnectedConnection_;
    boost::signals2::connection onReplyReceivedConnection_;
};

}
