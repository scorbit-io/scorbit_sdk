#pragma once

#include <chrono>
#include <optional>
#include <random>
#include <variant>
#include <vector>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/signals2/signal.hpp>
#include <boost/outcome/outcome.hpp>
#include <nlohmann/json.hpp>

#include <centrifugo/common.h>
#include <centrifugo/error.h>
#include <utility>
#include "protocol_all.h"

namespace centrifugo {

namespace chrono = std::chrono;
namespace net = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace outcome = boost::outcome_v2;
using json = nlohmann::json;
using tcp = net::ip::tcp;

struct UrlComponents {
    std::string host;
    std::string port;
    std::string path;
    bool secure = false;
};

class Transport
{
public:
    using ConnectingSignal = boost::signals2::signal<void(Error const &)>;
    using ConnectedSignal = boost::signals2::signal<void(ConnectResult const &)>;
    using DisconnectedSignal = boost::signals2::signal<void(Error const &)>;
    using ReplyReceivedSignal = boost::signals2::signal<void(Reply const &)>;
    using ErrorSignal = boost::signals2::signal<void(Error const &)>;

    Transport(net::strand<net::io_context::executor_type> const &strand, std::string &&url,
              ClientConfig &&config);

    auto state() const -> ConnectionState;
    auto sentCommands() const -> std::unordered_map<std::uint32_t, Command> const &;

    auto initialConnect() -> outcome::result<void, Error>;
    auto disconnect(Error const &error = {ErrorType::NoError, "disconnect called"}) -> void;

    template<typename T>
    auto send(T &&message) -> void
    {
        if constexpr (std::is_same_v<std::decay_t<T>, Command>) {
            send(json(message), std::forward<T>(message));
        } else {
            send(json(message), Command {});
        }
    }

    auto send(json const &j, Command &&cmd) -> void;

    auto onConnecting() -> ConnectingSignal & { return connectingSignal_; }
    auto onConnected() -> ConnectedSignal & { return connectedSignal_; }
    auto onDisconnected() -> DisconnectedSignal & { return disconnectedSignal_; }
    auto onReplyReceived() -> ReplyReceivedSignal & { return replyReceivedSignal_; }
    auto onError() -> ErrorSignal & { return errorSignal_; }
    auto onSslContextConfigure(std::function<bool(boost::asio::ssl::context &sslContext)> callback)
            -> void
    {
        sslContextConfigureCallback_ = std::move(callback);
    }

private:
    auto connect() -> void;
    auto reconnect(Error const &reason = {}) -> void;
    auto handShake() -> void;
    auto read() -> void;
    auto handleReceivedMsg(json const &json) -> void;
    auto sendConnectCmd() -> void;
    auto flush() -> void;
    auto refreshToken() -> bool;
    auto closeConnection() -> void;
    auto startPingTimer() -> void;
    auto startTokenRefreshTimer(std::uint32_t ttlSeconds) -> void;
    auto calculateBackoffDelay() -> std::chrono::milliseconds;

    template<typename F>
    auto withWs(F &&func) -> decltype(auto)
    {
        return std::visit(std::forward<F>(func), ws_);
    }

    template<typename... Args>
    auto setState(ConnectionState newState, Args &&...args) -> void
    {
        if (state_ == newState)
            return;

        state_ = newState;
        if constexpr ((std::is_same_v<std::decay_t<Args>, Error> && ...)) {
            if (state_ == ConnectionState::Connecting) {
                connectingSignal_(std::forward<Args>(args)...);
            } else if (state_ == ConnectionState::Disconnected) {
                disconnectedSignal_(std::forward<Args>(args)...);
            }
        } else if constexpr ((std::is_same_v<std::decay_t<Args>, ConnectResult> && ...)) {
            connectedSignal_(std::forward<Args>(args)...);
        }
    }

    template <typename StreamType, typename... Args>
    void resetWebSocket(Args&&... args) {
        // 1. Cancel and close any pending operations on the old stream
        withWs([](auto& ws) {
            beast::error_code ignore;
            beast::get_lowest_layer(ws).cancel(ignore);
            beast::get_lowest_layer(ws).close(ignore);
        });

        // 2. Reconstruct in-place
        ws_.emplace<StreamType>(std::forward<Args>(args)...);
    }

private:
    using WsStream = websocket::stream<tcp::socket>;
    using WssStream = websocket::stream<beast::ssl_stream<tcp::socket>>;
    using WebSocketVariant = std::variant<WsStream, WssStream>;

    ClientConfig config_;
    std::string url_;

    tcp::resolver resolver_;
    std::optional<net::ssl::context> sslContext_;
    WebSocketVariant ws_;
    beast::flat_buffer buffer_;
    net::steady_timer reconnectTimer_;
    net::steady_timer pingTimer_;
    net::steady_timer tokenRefreshTimer_;
    std::mt19937 rng_;

    ConnectionState state_ = ConnectionState::Disconnected;
    UrlComponents urlComponents_;
    std::string clientId_;
    chrono::seconds pingInterval_;
    std::uint32_t reconnectAttempts_ = 0;
    std::unordered_map<std::uint32_t, Command> sentCommands_;
    std::string token_;

    // deferred writes
    std::string pendingWrites_;
    std::vector<Command> pendingCommands_;
    bool isWriting_ = false;

    ConnectingSignal connectingSignal_;
    ConnectedSignal connectedSignal_;
    DisconnectedSignal disconnectedSignal_;
    ReplyReceivedSignal replyReceivedSignal_;
    ErrorSignal errorSignal_;

    std::function<bool(boost::asio::ssl::context &)> sslContextConfigureCallback_;
};

}
