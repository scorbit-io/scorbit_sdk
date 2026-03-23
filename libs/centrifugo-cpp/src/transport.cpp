#include "transport.h"

#include <cstdint>
#include <string>
#include <system_error>

#include <boost/url.hpp>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include <centrifugo/common.h>
#include <centrifugo/error.h>
#include "protocol_all.h"

namespace centrifugo {

constexpr auto TERMINAL_DISCONNECT_CODES = 3500;

auto parseUrl(std::string const &url) -> outcome::result<UrlComponents, Error>
{
    auto parseResult = boost::urls::parse_uri(url);
    if (!parseResult) {
        return Error {parseResult.error(), parseResult.error().message()};
    }

    auto secure = bool {};
    if (parseResult->scheme() == "wss") {
        secure = true;
    } else if (parseResult->scheme() == "ws") {
        secure = false;
    } else {
        return Error {std::make_error_code(std::errc::invalid_argument),
                      "URL must start with ws:// or wss://"};
    }

    if (parseResult->host().empty()) {
        return Error {std::make_error_code(std::errc::invalid_argument), "host cannot be empty"};
    }

    auto const port = !parseResult->port().empty() ? parseResult->port() : secure ? "443" : "80";
    auto const path = !parseResult->path().empty() ? parseResult->path() : "/";
    return UrlComponents {parseResult->host(), port, path, secure};
}

auto toError(std::error_code ec) -> Error
{
    return Error {ec, ec.message()};
}

Transport::Transport(net::strand<net::io_context::executor_type> const &strand, std::string &&url,
                     ClientConfig &&config)
    : config_ {std::move(config)}
    , url_ {std::move(url)}
    , resolver_ {strand}
    , ws_ {WsStream {strand}}
    , reconnectTimer_ {strand}
    , pingTimer_ {strand}
    , tokenRefreshTimer_ {strand}
    , rng_ {std::random_device {}()}
    , token_ {config.token}
{
    connectingSignal_.connect([this](auto const &) { reconnectAttempts_ = 0; });

    connectedSignal_.connect([this](ConnectResult const &result) {
        clientId_ = result.client;

        if (result.pong) {
            pingInterval_ = chrono::seconds {result.ping} + config_.maxPingDelay;
            startPingTimer();
        }

        if (result.expires) {
            startTokenRefreshTimer(result.ttl);
        }
    });

    disconnectedSignal_.connect([this](auto const &) {
        reconnectTimer_.cancel();
        pingTimer_.cancel();
        tokenRefreshTimer_.cancel();
        closeConnection();
    });
}

auto Transport::state() const -> ConnectionState
{
    return state_;
}

auto Transport::sentCommands() const -> std::unordered_map<std::uint32_t, Command> const &
{
    return sentCommands_;
}

auto Transport::initialConnect() -> outcome::result<void, Error>
{
    if (state_ != ConnectionState::Disconnected) {
        return Error {ErrorType::NotDisconnected, "not disconnected"};
    }
    if (config_.minReconnectDelay >= config_.maxReconnectDelay) {
        return Error {std::make_error_code(std::errc::invalid_argument),
                      "maxReconnectDelay should be greater than minReconnectDelay"};
    }
    if (config_.minReconnectDelay.count() > 0xFFFF) {
        return Error {std::make_error_code(std::errc::invalid_argument),
                      "minReconnectDelay can't be greater than 2^16"};
    }
    if (config_.name.length() > 16) {
        return Error {std::make_error_code(std::errc::invalid_argument),
                      "Name cannot be longer than 16 characters"};
    }
    if (config_.version.length() > 16) {
        return Error {std::make_error_code(std::errc::invalid_argument),
                      "Version cannot be longer than 16 characters"};
    }

    auto parseResult = parseUrl(url_);
    if (!parseResult) {
        return parseResult.error();
    }

    urlComponents_ = parseResult.assume_value();
    if (urlComponents_.secure) {
        sslContext_.emplace(net::ssl::context::tlsv13_client);
        sslContext_->set_verify_mode(net::ssl::verify_peer);
        if (sslContextConfigureCallback_) {
            if (!sslContextConfigureCallback_(*sslContext_)) {
                errorSignal_(
                        Error {ErrorType::NoError,
                               std::string {"Failed to configure SSL context with custom callback. "
                                            "Falling back to default verification paths."}});
                sslContext_->set_default_verify_paths();
            }
        } else {
            sslContext_->set_default_verify_paths();
        }
    }

    connect();
    return outcome::success();
}

auto Transport::disconnect(Error const &error) -> void
{
    setState(ConnectionState::Disconnected, error);
}

auto Transport::send(json const &j, Command &&cmd) -> void
{
    if (pendingWrites_.empty()) {
        pendingWrites_ += j.dump();
    } else {
        pendingWrites_ += "\n" + j.dump();
    }

    if (cmd.id != 0) {
        pendingCommands_.push_back(std::move(cmd));
    }

    withWs([this](auto &ws) { net::post(ws.get_executor(), [this] { flush(); }); });
}

auto Transport::connect() -> void
{
    setState(ConnectionState::Connecting, Error {ErrorType::NoError, "connect called"});

    if (token_.empty() && !refreshToken()) {
        return;
    }

    // Recreate the WebSocket stream for reconnection
    auto executor = resolver_.get_executor();
    if (urlComponents_.secure) {
        resetWebSocket<WssStream>(tcp::socket{executor}, *sslContext_);
    } else {
        resetWebSocket<WsStream>(tcp::socket{executor});
    }

    resolver_.async_resolve(
            urlComponents_.host, urlComponents_.port,
            [this](beast::error_code ec, tcp::resolver::results_type results) {
                if (ec) {
                    errorSignal_(toError(ec));
                    reconnect();
                    return;
                }

                withWs([this, results](auto &ws) {
                    using WS = std::decay_t<decltype(ws)>;

                    net::async_connect(
                            beast::get_lowest_layer(ws), results,
                            [this, &ws](beast::error_code ec,
                                        tcp::resolver::results_type::endpoint_type) {
                                if (ec) {
                                    if (ec != net::error::connection_refused) {
                                        errorSignal_(toError(ec));
                                    }
                                    reconnect();
                                    return;
                                }

                                // SSL hostname verification for WssStream
                                if constexpr (std::is_same_v<WS, WssStream>) {
                                    ws.next_layer().set_verify_callback(
                                            net::ssl::host_name_verification(urlComponents_.host));
                                } else {
                                    (void)ws;
                                }

                                handShake();
                            });
                });
            });
}

auto Transport::reconnect(Error const &error) -> void
{
    setState(ConnectionState::Connecting, error);
    ++reconnectAttempts_;
    auto const delay = calculateBackoffDelay();

    if (config_.logHandler) {
        config_.logHandler({LogLevel::Debug,
                            "reconnection attempt",
                            {{"attempt", reconnectAttempts_}, {"delay", delay.count()}}});
    }

    reconnectTimer_.expires_after(delay);
    reconnectTimer_.async_wait([this](beast::error_code ec) {
        if (ec) {
            if (ec == net::error::operation_aborted) {
                return;
            }
            errorSignal_(toError(ec));
            return;
        }

        connect();
    });
}

auto Transport::handShake() -> void
{
    withWs([this](auto &ws) {
        using StreamType = std::decay_t<decltype(ws)>;

        if constexpr (std::is_same_v<StreamType, WssStream>) {
            if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(),
                                          urlComponents_.host.c_str())) {
                auto ec = beast::error_code {static_cast<int>(::ERR_get_error()),
                                             net::error::get_ssl_category()};
                errorSignal_(toError(ec));
                reconnect();
                return;
            }

            ws.next_layer().async_handshake(
                    net::ssl::stream_base::client, [this](beast::error_code ec) {
                        if (ec) {
                            errorSignal_(toError(ec));
                            reconnect();
                            return;
                        }

                        withWs([this](auto &ws) {
                            ws.async_handshake(urlComponents_.host, urlComponents_.path,
                                               [this](beast::error_code ec) {
                                                   if (ec) {
                                                       errorSignal_(toError(ec));
                                                       reconnect();
                                                       return;
                                                   }
                                                   sendConnectCmd();
                                                   read();
                                               });
                        });
                    });
        } else {
            ws.async_handshake(urlComponents_.host, urlComponents_.path,
                               [this](beast::error_code ec) {
                                   if (ec) {
                                       errorSignal_(toError(ec));
                                       reconnect();
                                       return;
                                   }
                                   sendConnectCmd();
                                   read();
                               });
        }
    });
}

auto Transport::read() -> void
{
    withWs([this](auto &ws) {
        ws.async_read(buffer_, [this, &ws](beast::error_code ec, std::size_t) {
            if (ec && ec != net::ssl::error::stream_truncated) {
                if (ec == beast::errc::operation_canceled) {
                    return;
                }

                if (ec != websocket::error::closed) {
                    reconnect(Error {ec, ec.message()});
                    return;
                }

                auto error = Error {static_cast<ErrorType>(ws.reason().code),
                                    std::string {ws.reason().reason}};
                if (error.ec.value() >= TERMINAL_DISCONNECT_CODES) {
                    disconnect(error);
                    return;
                }
                reconnect(error);
                return;
            }

            auto data = beast::buffers_to_string(buffer_.data());
            buffer_.consume(buffer_.size());

            if (config_.logHandler) {
                config_.logHandler({LogLevel::Debug, "received message", {{"message", data}}});
            }

            auto ss = std::stringstream {data};
            auto line = std::string {};
            while (std::getline(ss, line)) {
                if (line.empty()) {
                    continue;
                }

                try {
                    handleReceivedMsg(json::parse(line));
                } catch (std::exception const &e) {
                    errorSignal_(Error {ErrorType::TransportError,
                                        std::string {"json parse error: "} + e.what()});
                }
            }

            read();
        });
    });
}

auto Transport::handleReceivedMsg(json const &json) -> void
{
    if (json.empty()) {
        if (pingTimer_.cancel() == 0) // do not pong if not pinging
            return;

        startPingTimer();
        send(json::object());
        return;
    }

    try {
        auto const reply = json.get<Reply>();
        std::visit(
                [this](auto const &result) {
                    using ResultType = std::decay_t<decltype(result)>;

                    if constexpr (std::is_same_v<ResultType, ErrorReply>) {
                        if (static_cast<ErrorType>(result.code) == ErrorType::TokenExpired) {
                            token_ = std::string {};
                            closeConnection();
                            reconnect();
                        }
                    } else if constexpr (std::is_same_v<ResultType, ConnectResult>) {
                        setState(ConnectionState::Connected, result);
                    } else if constexpr (std::is_same_v<ResultType, RefreshResult>) {
                        if (result.expires) {
                            startTokenRefreshTimer(result.ttl);
                        }
                    }
                },
                reply.result);

        replyReceivedSignal_(reply);
        sentCommands_.erase(reply.id);
    } catch (std::exception const &e) {
        errorSignal_(Error {ErrorType::TransportError, std::string {"error processing reply: "}
                                                               + e.what() + ", " + json.dump()});
    }
}

auto Transport::sendConnectCmd() -> void
{
    auto req = ConnectRequest {};
    req.token = token_;
    req.name = config_.name.empty() ? "cpp" : config_.name;
    req.version = config_.version;
    send(makeCommand(req));
}

auto Transport::flush() -> void
{
    if (isWriting_ || pendingWrites_.empty()) {
        return;
    }

    auto const messages = std::move(pendingWrites_);
    auto const commands = std::move(pendingCommands_);
    pendingWrites_.clear();
    pendingCommands_.clear();

    if (config_.logHandler) {
        config_.logHandler({LogLevel::Debug, "sending message", {{"message", messages}}});
    }

    isWriting_ = true;
    withWs([&](auto &ws) {
        ws.async_write(net::buffer(messages), [this, cmds = std::move(commands)](
                                                      beast::error_code ec, std::size_t) mutable {
            isWriting_ = false;

            if (ec) {
                errorSignal_(toError(ec));
                return;
            }

            for (auto &cmd : cmds) {
                sentCommands_.emplace(cmd.id, std::move(cmd));
            }

            if (!pendingWrites_.empty()) {
                flush();
            }
        });
    });
}

auto Transport::refreshToken() -> bool
{
    if (!config_.getToken) {
        errorSignal_(
                Error {ErrorType::TransportError, "getToken must be set to handle token refresh"});
        disconnect(Error {ErrorType::Unauthorized, "unauthorized"});
        return false;
    }

    auto const tokenResult = config_.getToken();
    if (!tokenResult) {
        errorSignal_(Error {ErrorType::TransportError,
                            "getToken failed: " + tokenResult.error().message()});
        disconnect(Error {ErrorType::Unauthorized, "unauthorized"});
        return false;
    }

    token_ = tokenResult.value();
    return true;
}

auto Transport::closeConnection() -> void
{
    withWs([](auto &ws) {
        beast::error_code ignore;
        beast::get_lowest_layer(ws).cancel(ignore);
        beast::get_lowest_layer(ws).close(ignore);
    });
}

auto Transport::startPingTimer() -> void
{
    pingTimer_.expires_after(pingInterval_);
    pingTimer_.async_wait([this](boost::system::error_code ec) {
        if (ec) {
            if (ec == net::error::operation_aborted) {
                return;
            }
            errorSignal_(toError(ec));
            return;
        }

        reconnect(Error {ErrorType::NoPing, "no ping"});
    });
}

auto Transport::startTokenRefreshTimer(std::uint32_t ttlSeconds) -> void
{
    auto expiryTime = std::chrono::seconds {ttlSeconds};
    if (expiryTime > config_.refreshTokenBeforeExpiry) {
        expiryTime -= config_.refreshTokenBeforeExpiry;
    }

    tokenRefreshTimer_.expires_after(expiryTime);
    tokenRefreshTimer_.async_wait([this](boost::system::error_code ec) {
        if (ec) {
            if (ec == net::error::operation_aborted) {
                return;
            }
            errorSignal_(toError(ec));
            return;
        }

        if (!refreshToken()) {
            return;
        }

        send(makeCommand(RefreshRequest {token_}));
    });
}

auto Transport::calculateBackoffDelay() -> std::chrono::milliseconds
{
    using namespace std;
    using namespace chrono;

    auto constexpr BIT_SHIFT_BASE = 1u;
    auto constexpr MAX_EXPONENTIAL_SHIFT = static_cast<decltype(reconnectAttempts_)>(
            numeric_limits<decltype(BIT_SHIFT_BASE)>::digits / 2);

    auto const exponentialDelay =
            config_.minReconnectDelay
            * (BIT_SHIFT_BASE << min(reconnectAttempts_, MAX_EXPONENTIAL_SHIFT));
    auto const cappedDelay = min(exponentialDelay, config_.maxReconnectDelay);

    return milliseconds {
            uniform_int_distribution<milliseconds::rep> {0, cappedDelay.count()}(rng_)};
}

}
