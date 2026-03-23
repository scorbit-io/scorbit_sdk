#include <centrifugo/subscription.h>

#include "subscription_impl.h"

namespace centrifugo {

Subscription::Subscription(SubscriptionImpl *impl)
    : impl {impl}
{
}

auto Subscription::state() const -> SubscriptionState
{
    return impl->state();
}

auto Subscription::channel() const -> std::string const &
{
    return impl->channel();
}

auto Subscription::subscribe() -> outcome::result<void, std::string>
{
    return impl->subscribe();
}

auto Subscription::unsubscribe() -> void
{
    impl->unsubscribe();
}

auto Subscription::publish(nlohmann::json const &json) -> outcome::result<void, Error>
{
    return impl->publish(json);
}

auto Subscription::onSubscribing(std::function<void()> callback) -> void
{
    impl->onSubscribing().connect(callback);
}

auto Subscription::onSubscribed(std::function<void()> callback) -> void
{
    impl->onSubscribed().connect(callback);
}

auto Subscription::onUnsubscribed(std::function<void()> callback) -> void
{
    impl->onUnsubscribed().connect(callback);
}

auto Subscription::onPublication(std::function<void(Publication const &)> callback) -> void
{
    impl->onPublication().connect(callback);
}

auto Subscription::onError(std::function<void(Error const &)> callback) -> void
{
    impl->onError().connect(callback);
}

}
