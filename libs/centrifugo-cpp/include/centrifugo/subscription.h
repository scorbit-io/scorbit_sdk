#pragma once

#include <string>

#include <boost/outcome/outcome.hpp>

#include <centrifugo/procotol.h>
#include <centrifugo/error.h>

namespace centrifugo {

namespace outcome = boost::outcome_v2;

enum class SubscriptionState { UNSUBSCRIBED, SUBSCRIBING, SUBSCRIBED };

class SubscriptionImpl;

class Subscription
{
public:
    Subscription(SubscriptionImpl *impl);

    auto state() const -> SubscriptionState;
    auto channel() const -> std::string const &;

    auto subscribe() -> outcome::result<void, std::string>;
    auto unsubscribe() -> void;
    auto publish(nlohmann::json const &json) -> outcome::result<void, Error>;

    auto onSubscribing(std::function<void()> callback) -> void;
    auto onSubscribed(std::function<void()> callback) -> void;
    auto onUnsubscribed(std::function<void()> callback) -> void;
    auto onPublication(std::function<void(Publication const &)> callback) -> void;
    auto onError(std::function<void(Error const &)> callback) -> void;

private:
    SubscriptionImpl *impl = nullptr;
};

}
