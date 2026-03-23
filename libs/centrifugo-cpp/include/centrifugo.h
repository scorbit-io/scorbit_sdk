#pragma once

#include <optional>

#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ssl.hpp>

#include <centrifugo/subscription.h>
#include <centrifugo/common.h>
#include <nlohmann/json.hpp>

namespace centrifugo {

namespace net = boost::asio;

class Client
{
public:
    using SubscriptionRef = std::reference_wrapper<Subscription>;

    Client(net::strand<net::io_context::executor_type> const &strand, std::string url,
           ClientConfig config);

    ~Client();

    auto state() const -> ConnectionState;

    auto connect() -> outcome::result<void, Error>;
    auto disconnect() -> void;

    auto publish(std::string const &channel, nlohmann::json const &data)
            -> outcome::result<void, Error>;

    auto send(nlohmann::json const &data) -> outcome::result<void, Error>;

    auto newSubscription(std::string const &channel)
            -> outcome::result<std::reference_wrapper<Subscription>, std::string>;
    auto removeSubscription(SubscriptionRef const &sub) -> void;
    auto subscription(std::string const &channel) const -> std::optional<SubscriptionRef>;
    auto subscriptions() const -> std::unordered_map<std::string, SubscriptionRef>;

    auto onConnecting(std::function<void(Error const &)> callback) -> void;
    auto onConnected(std::function<void()> callback) -> void;
    auto onDisconnected(std::function<void(Error const &)> callback) -> void;

    auto onSubscribing(std::function<void(std::string const &channel)> callback) -> void;
    auto onSubscribed(std::function<void(std::string const &channel)> callback) -> void;
    auto onUnsubscribed(std::function<void(std::string const &channel)> callback) -> void;
    auto
    onPublication(std::function<void(std::string const &channel, Publication const &)> callback)
            -> void;
    auto onError(std::function<void(Error const &)> callback) -> void;
    auto onSslContextConfigure(std::function<bool(boost::asio::ssl::context &)> callback) -> void;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

}
