#include "protocol_all.h"

namespace centrifugo {

using json = nlohmann::json;

auto to_json(json &j, ConnectRequest const &req) -> void
{
    j = json {{"name", req.name}};

    if (!req.token.empty())
        j["token"] = req.token;
    if (!req.data.empty())
        j["data"] = req.data;
    if (!req.version.empty())
        j["version"] = req.version;
}

auto to_json(json &j, SubscribeRequest const &req) -> void
{
    j = json {{"channel", req.channel}};

    if (!req.token.empty())
        j["token"] = req.token;
    if (req.recover)
        j["recover"] = req.recover;
    if (!req.epoch.empty())
        j["epoch"] = req.epoch;
    if (req.offset != 0)
        j["offset"] = req.offset;
    if (!req.data.empty())
        j["data"] = req.data;
    if (req.positioned)
        j["positioned"] = req.positioned;
    if (req.recoverable)
        j["recoverable"] = req.recoverable;
    if (req.join_leave)
        j["join_leave"] = req.join_leave;
    if (!req.delta.empty())
        j["delta"] = req.delta;
}

auto to_json(json &j, UnsubscribeRequest const &req) -> void
{
    j = json {{"channel", req.channel}};
}

auto to_json(json &j, PublishRequest const &req) -> void
{
    j = json {{"channel", req.channel}, {"data", req.data}};
}

auto to_json(json &j, RefreshRequest const &req) -> void
{
    j = json {{"token", req.token}};
}

auto to_json(json &j, SendRequest const &req) -> void
{
    j = json {{"data", req.data}};
}

auto from_json(json const &j, ConnectResult &result) -> void
{
    if (j.contains("client"))
        j.at("client").get_to(result.client);
    if (j.contains("version"))
        j.at("version").get_to(result.version);
    if (j.contains("expires"))
        j.at("expires").get_to(result.expires);
    if (j.contains("ttl"))
        j.at("ttl").get_to(result.ttl);
    if (j.contains("data"))
        j.at("data").get_to(result.data.value());
    if (j.contains("subs"))
        j.at("subs").get_to(result.subs);
    if (j.contains("ping"))
        j.at("ping").get_to(result.ping);
    if (j.contains("pong"))
        j.at("pong").get_to(result.pong);
    if (j.contains("session"))
        j.at("session").get_to(result.session);
    if (j.contains("node"))
        j.at("node").get_to(result.node);
    if (j.contains("time"))
        j.at("time").get_to(result.time);
}

auto from_json(json const &j, SubscribeResult &result) -> void
{
    if (j.contains("expires"))
        j.at("expires").get_to(result.expires);
    if (j.contains("ttl"))
        j.at("ttl").get_to(result.ttl);
    if (j.contains("recoverable"))
        j.at("recoverable").get_to(result.recoverable);
    if (j.contains("epoch"))
        j.at("epoch").get_to(result.epoch);
    if (j.contains("publications"))
        j.at("publications").get_to(result.publications);
    if (j.contains("recovered"))
        j.at("recovered").get_to(result.recovered);
    if (j.contains("offset"))
        j.at("offset").get_to(result.offset);
    if (j.contains("positioned"))
        j.at("positioned").get_to(result.positioned);
    if (j.contains("data"))
        j.at("data").get_to(result.data);
    if (j.contains("was_recovering"))
        j.at("was_recovering").get_to(result.was_recovering);
    if (j.contains("delta"))
        j.at("delta").get_to(result.delta);
}

auto from_json(json const &, UnsubscribeResult &) -> void
{
}

auto from_json(json const &, PublishResult &) -> void
{
}

auto from_json(json const &j, RefreshResult &result) -> void
{
    if (j.contains("client"))
        j.at("client").get_to(result.client);
    if (j.contains("version"))
        j.at("version").get_to(result.version);
    if (j.contains("expires"))
        j.at("expires").get_to(result.expires);
    if (j.contains("ttl"))
        j.at("ttl").get_to(result.ttl);
}

auto from_json(json const &, SendResult &) -> void
{
}

auto to_json(json &j, Command const &cmd) -> void
{
    j["id"] = cmd.id;

    std::visit(
            [&j](auto const &req) {
                if constexpr (std::is_same_v<std::decay_t<decltype(req)>, ConnectRequest>) {
                    j["connect"] = req;
                } else if constexpr (std::is_same_v<std::decay_t<decltype(req)>,
                                                    SubscribeRequest>) {
                    j["subscribe"] = req;
                } else if constexpr (std::is_same_v<std::decay_t<decltype(req)>, PublishRequest>) {
                    j["publish"] = req;
                } else if constexpr (std::is_same_v<std::decay_t<decltype(req)>, RefreshRequest>) {
                    j["refresh"] = req;
                } else if constexpr (std::is_same_v<std::decay_t<decltype(req)>, SendRequest>) {
                    j["send"] = req;
                } else if constexpr (std::is_same_v<std::decay_t<decltype(req)>,
                                                    UnsubscribeRequest>) {
                    j["unsubscribe"] = req;
                }
            },
            cmd.request);
}

auto from_json(json const &j, ErrorReply &error) -> void
{
    if (j.contains("code"))
        j.at("code").get_to(error.code);
    if (j.contains("message"))
        j.at("message").get_to(error.message);
    if (j.contains("temporary"))
        j.at("temporary").get_to(error.temporary);
}

auto from_json(json const &j, Reply &reply) -> void
{
    if (j.contains("id"))
        j.at("id").get_to(reply.id);

    if (j.contains("error")) {
        reply.result = j.at("error").get<ErrorReply>();
    } else if (j.contains("connect")) {
        reply.result = j.at("connect").get<ConnectResult>();
    } else if (j.contains("subscribe")) {
        reply.result = j.at("subscribe").get<SubscribeResult>();
    } else if (j.contains("publish")) {
        reply.result = j.at("publish").get<PublishResult>();
    } else if (j.contains("refresh")) {
        reply.result = j.at("refresh").get<RefreshResult>();
    } else if (j.contains("send")) {
        reply.result = j.at("send").get<SendResult>();
    } else if (j.contains("unsubscribe")) {
        reply.result = j.at("unsubscribe").get<UnsubscribeResult>();
    } else if (j.contains("push")) {
        reply.result = j.at("push").get<Push>();
    }
}

auto from_json(json const &j, ClientInfo &info) -> void
{
    if (j.contains("user"))
        j.at("user").get_to(info.user);
    if (j.contains("client"))
        j.at("client").get_to(info.client);
}

auto from_json(json const &j, Publication &pub) -> void
{
    if (j.contains("offset"))
        j.at("offset").get_to(pub.offset);
    if (j.contains("data"))
        j.at("data").get_to(pub.data);
    if (j.contains("info"))
        pub.info = j.at("info").get<ClientInfo>();
    if (j.contains("tags"))
        j.at("tags").get_to(pub.tags);
}

auto from_json(json const &j, Subscribe &sub) -> void
{
    if (j.contains("recoverable"))
        j.at("recoverable").get_to(sub.recoverable);
    if (j.contains("epoch"))
        j.at("epoch").get_to(sub.epoch);
    if (j.contains("offset"))
        j.at("offset").get_to(sub.offset);
    if (j.contains("positioned"))
        j.at("positioned").get_to(sub.positioned);
    // if (j.contains("data"))
    //     j.at("data").get_to(sub.data);
}

auto from_json(json const &j, Unsubscribe &unsub) -> void
{
    if (j.contains("code"))
        j.at("code").get_to(unsub.code);
    if (j.contains("reason"))
        j.at("reason").get_to(unsub.reason);
}

auto from_json(json const &j, Push &push) -> void
{
    if (j.contains("channel"))
        j.at("channel").get_to(push.channel);

    if (j.contains("pub")) {
        push.type = j.at("pub").get<Publication>();
    } else if (j.contains("subscribe")) {
        push.type = j.at("subscribe").get<Subscribe>();
    } else if (j.contains("unsubscribe")) {
        push.type = j.at("unsubscribe").get<Unsubscribe>();
    }
}
}
