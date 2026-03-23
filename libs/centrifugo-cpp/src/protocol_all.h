#pragma once

#include <optional>
#include <string>
#include <cstdint>
#include <variant>
#include <vector>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include <centrifugo/procotol.h>

namespace centrifugo {

struct ConnectRequest {
    std::string token;
    std::string data;
    std::string name;
    std::string version;
};

struct SubscribeRequest {
    std::string channel;
    std::string token;
    bool recover {false};
    std::string epoch;
    std::uint64_t offset {0};
    nlohmann::json data;
    bool positioned {false};
    bool recoverable {false};
    bool join_leave {false};
    std::string delta;
};

struct UnsubscribeRequest {
    std::string channel;
};

struct PublishRequest {
    std::string channel;
    nlohmann::json data;
};

struct RefreshRequest {
    std::string token;
};

struct SendRequest {
    nlohmann::json data;
};

struct SubscribeResult {
    bool expires {false};
    std::uint32_t ttl {0};
    bool recoverable {false};
    std::string epoch;
    std::vector<Publication> publications;
    bool recovered {false};
    std::uint64_t offset {0};
    bool positioned {false};
    std::vector<std::uint8_t> data;
    bool was_recovering {false};
    bool delta {false};
};

struct UnsubscribeResult {
};

struct ConnectResult {
    std::string client;
    std::string version;
    bool expires {false};
    std::uint32_t ttl {0};
    std::optional<std::string> data;
    std::unordered_map<std::string, SubscribeResult> subs;
    std::uint32_t ping {0};
    bool pong {false};
    std::string session;
    std::string node;
    std::int64_t time {0};
};

struct PublishResult {
};

struct RefreshResult {
    std::string client;
    std::string version;
    bool expires {false};
    std::uint32_t ttl {0};
};

struct SendResult {
};

struct Subscribe {
    bool recoverable {false};
    std::string epoch;
    std::uint64_t offset {0};
    bool positioned {false};
    // std::vector<std::uint8_t> data;
};

struct Unsubscribe {
    std::uint32_t code {0};
    std::string reason;
};

struct Push {
    using PushType = std::variant<Publication, Subscribe, Unsubscribe>;

    std::string channel;
    PushType type;
};

struct ErrorReply {
    std::uint32_t code {0};
    std::string message;
    bool temporary {false};
};

struct Command {
    using RequestType = std::variant<ConnectRequest, SubscribeRequest, UnsubscribeRequest,
                                     PublishRequest, RefreshRequest, SendRequest>;

    std::uint32_t id;
    RequestType request;
};

struct Reply {
    using ResultType = std::variant<ConnectResult, SubscribeResult, UnsubscribeResult,
                                    PublishResult, RefreshResult, SendResult, Push, ErrorReply>;

    std::uint32_t id;
    ResultType result;
};

auto to_json(nlohmann::json &j, ConnectRequest const &req) -> void;
auto to_json(nlohmann::json &j, SubscribeRequest const &req) -> void;
auto to_json(nlohmann::json &j, UnsubscribeRequest const &req) -> void;
auto to_json(nlohmann::json &j, PublishRequest const &req) -> void;
auto to_json(nlohmann::json &j, RefreshRequest const &req) -> void;
auto to_json(nlohmann::json &j, SendRequest const &req) -> void;

auto from_json(nlohmann::json const &j, ConnectResult &result) -> void;
auto from_json(nlohmann::json const &j, SubscribeResult &result) -> void;
auto from_json(nlohmann::json const &j, UnsubscribeResult &result) -> void;
auto from_json(nlohmann::json const &j, PublishResult &result) -> void;
auto from_json(nlohmann::json const &j, RefreshResult &result) -> void;
auto from_json(nlohmann::json const &j, SendResult &result) -> void;

auto from_json(nlohmann::json const &j, ClientInfo &info) -> void;
auto from_json(nlohmann::json const &j, Publication &pub) -> void;
auto from_json(nlohmann::json const &j, Subscribe &sub) -> void;
auto from_json(nlohmann::json const &j, Unsubscribe &unsub) -> void;
auto from_json(nlohmann::json const &j, Push &push) -> void;
auto from_json(nlohmann::json const &j, ErrorReply &error) -> void;

auto to_json(nlohmann::json &j, Command const &cmd) -> void;
auto from_json(nlohmann::json const &j, Reply &reply) -> void;

inline auto makeCommand(Command::RequestType &&req) -> Command
{
    static auto commandId = 0u;
    auto cmd = Command {};
    cmd.id = ++commandId;
    cmd.request = std::move(req);
    return cmd;
}

}
