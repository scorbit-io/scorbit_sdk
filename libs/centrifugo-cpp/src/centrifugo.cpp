#include <centrifugo.h>

#include <functional>
#include <optional>
#include <regex>
#include <unordered_set>
#include <iterator>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <nlohmann/json.hpp>

#include "centrifugo/common.h"
#include "centrifugo/error.h"
#include "centrifugo/subscription.h"
#include "protocol_all.h"
#include "transport.h"
#include "subscription_impl.h"

namespace centrifugo {

class Client::Impl
{
public:
    Impl(net::strand<net::io_context::executor_type> strand, std::string &&url,
         ClientConfig &&config)
        : logHandler_ {config.logHandler}
        , transport_ {strand, std::move(url), std::move(config)}
    {
        transport_.onReplyReceived().connect([this](Reply const &reply) {
            for (auto &[channel, subscription] : subscriptions_) {
                if (subscription.handleReply(reply)) {
                    return;
                }
            }

            std::visit(
                    [this](auto const &result) {
                        using ResultType = std::decay_t<decltype(result)>;

                        if constexpr (std::is_same_v<ResultType, ErrorReply>) {
                            if (onError_) {
                                onError_(Error {static_cast<ErrorType>(result.code),
                                                result.message});
                            }
                        } else if constexpr (std::is_same_v<ResultType, Push>) {
                            handlePush(result);
                        }
                    },
                    reply.result);
        });

        transport_.onConnecting().connect([this](auto const &) {
            if (onSubscribing_) {
                for (auto const &chan : serverSubscriptions_) {
                    onSubscribing_(chan);
                }
            }
        });

        transport_.onConnected().connect([this](ConnectResult const &result) {
            auto it = serverSubscriptions_.begin();
            while (it != serverSubscriptions_.end()) {
                if (result.subs.count(*it) == 0) {
                    if (onUnsubscribed_) {
                        onUnsubscribed_(*it);
                    }
                    it = serverSubscriptions_.erase(it);
                } else {
                    ++it;
                }
            }

            for (auto const &[channel, subResult] : result.subs) {
                if (serverSubscriptions_.count(channel) == 0) {
                    serverSubscriptions_.emplace(channel);
                    if (onSubscribing_) {
                        onSubscribing_(channel);
                    }
                }

                if (onSubscribed_) {
                    onSubscribed_(channel);
                }
            }
        });

        transport_.onDisconnected().connect([this](auto const &) {
            if (onUnsubscribed_) {
                for (auto const &chan : serverSubscriptions_) {
                    onUnsubscribed_(chan);
                }
            }
        });
    }

    auto transport() -> Transport & { return transport_; }

    auto newSubscription(std::string const &channel)
            -> outcome::result<std::reference_wrapper<Subscription>, std::string>
    {
        if (subscriptions_.find(channel) != subscriptions_.end()) {
            return std::string {"subscription already exists for channel " + channel};
        }
        if (serverSubscriptions_.find(channel) != serverSubscriptions_.end()) {
            return std::string {"channel " + channel
                                + " already exists as server-side subscription"};
        }
        auto &sub = subscriptions_.emplace(channel, SubscriptionImpl {channel, transport_})
                            .first->second.subscription();
        return sub;
    }

    auto removeSubscription(SubscriptionRef const &sub) -> void
    {
        subscriptions_.erase(sub.get().channel());
    }

    auto subscription(std::string const &channel) -> std::optional<SubscriptionRef>
    {
        if (auto const it = subscriptions_.find(channel); it != subscriptions_.end()) {
            return it->second.subscription();
        }
        return std::nullopt;
    }

    auto subscriptions() -> std::unordered_map<std::string, SubscriptionRef>
    {
        std::unordered_map<std::string, SubscriptionRef> res;
        res.reserve(subscriptions_.size());
        for (auto &[channel, impl] : subscriptions_) {
            res.emplace(channel, impl.subscription());
        }
        return res;
    }

    auto onSubscribing(std::function<void(std::string const &)> callback) -> void
    {
        onSubscribing_ = std::move(callback);
    }

    auto onSubscribed(std::function<void(std::string const &)> callback) -> void
    {
        onSubscribed_ = std::move(callback);
    }

    auto onUnsubscribed(std::function<void(std::string const &)> callback) -> void
    {
        onUnsubscribed_ = std::move(callback);
    }

    auto onPublication(std::function<void(std::string const &, Publication const &)> callback)
            -> void
    {
        onPublication_ = std::move(callback);
    }

    auto onError(std::function<void(Error const &)> callback) -> void
    {
        onError_ = std::move(callback);
        transport_.onError().connect(onError_);
    }

    auto publish(std::string const &channel, nlohmann::json const &data)
            -> outcome::result<void, Error>
    {
        if (transport_.state() != ConnectionState::Connected
            || serverSubscriptions_.count(channel) == 0) {
            return Error {ErrorType::NotSubscribed, "not subscribed"};
        }

        transport_.send(makeCommand(PublishRequest {channel, data}));
        return outcome::success();
    }

    auto send(nlohmann::json const &data) -> outcome::result<void, Error>
    {
        if (transport_.state() != ConnectionState::Connected) {
            return Error {ErrorType::NotConnected, "not connected"};
        }

        transport_.send(makeCommand(SendRequest {data}));
        return outcome::success();
    }

private:
    auto handlePush(Push const &push) -> void
    {
        std::visit(
                [this, &push](auto const &type) {
                    using PushType = std::decay_t<decltype(type)>;

                    if constexpr (std::is_same_v<PushType, Publication>) {
                        if (serverSubscriptions_.count(push.channel)) {
                            if (onPublication_) {
                                onPublication_(push.channel, type);
                            }
                            return;
                        }

                        if (auto itr = subscriptions_.find(push.channel);
                            itr != std::end(subscriptions_)) {
                            itr->second.handlePublish(type);
                            return;
                        }

                        if (logHandler_) {
                            logHandler_(LogEntry {LogLevel::Error,
                                                  "publication receive failed: subscription to "
                                                  "channel doesn't exist",
                                                  {{"channel", push.channel}}});
                        }
                    } else if constexpr (std::is_same_v<PushType, Subscribe>) {
                        if (auto const [_, inserted] = serverSubscriptions_.emplace(push.channel);
                            inserted && onSubscribing_) {
                            onSubscribing_(push.channel);
                        }
                        if (onSubscribed_) {
                            onSubscribed_(push.channel);
                        }
                    } else if constexpr (std::is_same_v<PushType, Unsubscribe>) {
                        if (serverSubscriptions_.erase(push.channel) && onUnsubscribed_) {
                            onUnsubscribed_(push.channel);
                        }
                    }
                },
                push.type);
    }

    auto sendSubscribeCmd(std::string const &channel) -> void
    {
        auto req = SubscribeRequest {};
        req.channel = channel;
        transport_.send(makeCommand(std::move(req)));
    }

private:
    std::function<void(LogEntry)> logHandler_;
    Transport transport_;
    std::unordered_map<std::string, SubscriptionImpl> subscriptions_;
    std::unordered_set<std::string> serverSubscriptions_;

    std::function<void(std::string const &)> onSubscribing_;
    std::function<void(std::string const &)> onSubscribed_;
    std::function<void(std::string const &)> onUnsubscribed_;
    std::function<void(std::string const &, Publication const &)> onPublication_;
    std::function<void(Error const &)> onError_;
};

Client::Client(net::strand<net::io_context::executor_type> const &strand, std::string url,
               ClientConfig config)
    : pImpl {std::make_unique<Impl>(strand, std::move(url), std::move(config))}
{
}

Client::~Client() = default;

auto Client::connect() -> outcome::result<void, Error>
{
    return pImpl->transport().initialConnect();
}

auto Client::disconnect() -> void
{
    pImpl->transport().disconnect();
}

auto Client::state() const -> ConnectionState
{
    return pImpl->transport().state();
}

auto Client::onConnecting(std::function<void(Error const &)> callback) -> void
{
    pImpl->transport().onConnecting().connect(callback);
}

auto Client::onConnected(std::function<void()> callback) -> void
{
    pImpl->transport().onConnected().connect(
            [callback = std::move(callback)](auto const &) { callback(); });
}

auto Client::onDisconnected(std::function<void(Error const &)> callback) -> void
{
    pImpl->transport().onDisconnected().connect(callback);
}

auto Client::newSubscription(std::string const &channel)
        -> outcome::result<std::reference_wrapper<Subscription>, std::string>
{
    return pImpl->newSubscription(channel);
}

auto Client::removeSubscription(SubscriptionRef const &sub) -> void
{
    pImpl->removeSubscription(sub);
}

auto Client::subscription(std::string const &channel) const -> std::optional<SubscriptionRef>
{
    return pImpl->subscription(channel);
}

auto Client::subscriptions() const -> std::unordered_map<std::string, SubscriptionRef>
{
    return pImpl->subscriptions();
}

auto Client::onSubscribing(std::function<void(std::string const &channel)> callback) -> void
{
    pImpl->onSubscribing(std::move(callback));
}

auto Client::onSubscribed(std::function<void(std::string const &channel)> callback) -> void
{
    pImpl->onSubscribed(std::move(callback));
}

auto Client::onUnsubscribed(std::function<void(std::string const &channel)> callback) -> void
{
    pImpl->onUnsubscribed(std::move(callback));
}

auto Client::onPublication(
        std::function<void(std::string const &channel, Publication const &)> callback) -> void
{
    pImpl->onPublication(std::move(callback));
}

auto Client::onError(std::function<void(Error const &)> callback) -> void
{
    pImpl->onError(std::move(callback));
}

void Client::onSslContextConfigure(std::function<bool(boost::asio::ssl::context &)> callback)
{
    pImpl->transport().onSslContextConfigure(std::move(callback));
}

auto Client::publish(std::string const &channel, nlohmann::json const &data)
        -> outcome::result<void, Error>
{
    return pImpl->publish(channel, data);
}

auto Client::send(nlohmann::json const &data) -> outcome::result<void, Error>
{
    return pImpl->send(data);
}
}
