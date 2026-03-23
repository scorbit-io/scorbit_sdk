#include "centrifugo/error.h"
#include <functional>
#include <iostream>
#include <chrono>
#include <memory>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

#include <centrifugo.h>
#include <centrifugo/common.h>

namespace outcome = boost::outcome_v2;

auto getJwtToken() -> outcome::result<std::string>
{
    namespace beast = boost::beast;
    namespace http = beast::http;
    namespace net = boost::asio;
    using tcp = net::ip::tcp;

    try {
        auto ioc = net::io_context {};
        auto stream = beast::tcp_stream {ioc};

        // Resolve and connect to JWT service
        auto const results = tcp::resolver {ioc}.resolve("localhost", "3001");
        stream.connect(results);

        // Set up HTTP GET request
        auto req = http::request<http::string_body> {http::verb::get,
                                                     "/token/server-side-user?seconds=300", 11};
        req.set(http::field::host, "localhost:3001");
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        // Send the HTTP request
        http::write(stream, req);

        // Read the HTTP response
        auto buffer = beast::flat_buffer {};
        auto res = http::response<http::string_body> {};
        http::read(stream, buffer, res);

        // Check response status
        if (res.result() != http::status::ok) {
            return std::make_error_code(std::errc::connection_refused);
        }

        // Extract token from response body
        auto const token = std::string {res.body()};

        // Gracefully close the socket
        auto ec = beast::error_code {};
        auto const _ = stream.socket().shutdown(tcp::socket::shutdown_both, ec);

        return token;
    } catch (std::exception const &e) {
        return std::make_error_code(std::errc::network_unreachable);
    }
}

auto logger(centrifugo::LogEntry log) -> void
{
    if (log.message.find("received") != std::string::npos) {
        std::cout << "\033[34m┌── " << log.message << ":\033[0m\n"
                  << log.fields["message"].get<std::string>() << "\n\033[34m└──\033[0m"
                  << std::endl;
    } else if (log.message.find("sending") != std::string::npos) {
        std::cout << "\033[31m┌── " << log.message << ":\033[0m\n"
                  << log.fields["message"].get<std::string>() << "\n\033[31m└──\033[0m"
                  << std::endl;
    } else {
        std::cout << "DEBUG  " << log.message << ": " << log.fields << std::endl;
    }
}

int main()
{
    auto ioc = boost::asio::io_context {};
    auto strand = boost::asio::make_strand(ioc);

    auto config = centrifugo::ClientConfig {};
    config.getToken = getJwtToken;
    config.logHandler = logger;
    auto client = centrifugo::Client {strand, "ws://localhost:8000/connection/websocket", config};

    client.onConnecting([](centrifugo::Error const &error) {
        std::cout << "[CLIENT] Connecting to Centrifugo... (" << error.ec.value() << ", "
                  << error.message << ')' << std::endl;
    });

    client.onConnected([&client, &ioc] {
        std::cout << "[CLIENT] Connected to Centrifugo!" << std::endl;

        // if centrifugo does not handle sent messages, client will disconnect
        // if (auto const result = client.send({{"message", "Hello World!"}}) !result) {
        //     std::cout << "failed to send message: " << result.error().message << std::endl;
        // }

        auto timer = std::make_shared<boost::asio::steady_timer>(ioc, std::chrono::seconds(10));
        timer->async_wait([&client, timer](boost::system::error_code ec) {
            if (!ec) {
                client.disconnect();
            }
        });
    });

    client.onDisconnected([](centrifugo::Error const &error) {
        std::cout << "[CLIENT] Disconnected from Centrifugo (" << error.ec.value() << ", "
                  << error.message << ')' << std::endl;
    });

    client.onSubscribing([](std::string const &channel) {
        std::cout << "[SERVER-SUB:" << channel << "] Subscribing..." << std::endl;
    });

    client.onSubscribed([&client](std::string const &channel) {
        std::cout << "[SERVER-SUB:" << channel << "] Subscribed successfully!" << std::endl;

        auto pubRes = client.publish(channel, {{"message", "I am freeeeeee!!"}});
        if (!pubRes) {
            std::cout << "failed to publish: " << pubRes.error().message << std::endl;
        }
    });

    client.onUnsubscribed([](std::string const &channel) {
        std::cout << "[SERVER-SUB:" << channel << "] Unsubscribed" << std::endl;
    });

    client.onPublication([](std::string const &channel, centrifugo::Publication const &pub) {
        std::cout << "[SERVER-SUB:" << channel << "] Publication received:" << std::endl;
        std::cout << "  Data: " << pub.data << std::endl;
        std::cout << "  Offset: " << pub.offset << std::endl;
        if (pub.info) {
            std::cout << "  From user: " << pub.info->user << " (client: " << pub.info->client
                      << ")" << std::endl;
        }
    });

    client.onError([](centrifugo::Error const &err) {
        std::cout << "[CLIENT] Error: (" << err.ec.value() << ") " << err.message << std::endl;
    });

    auto subCreateRes = client.newSubscription("mychan");
    if (!subCreateRes) {
        std::cout << "failed creating subscription: " << subCreateRes.error() << std::endl;
        return 1;
    }
    auto &sub = subCreateRes.value().get();

    sub.onSubscribing([ch = sub.channel()] {
        std::cout << "[CLIENT-SUB:" << ch << "] Subscribing..." << std::endl;
    });

    sub.onSubscribed([&ioc, subRef = subCreateRes.value()] {
        auto &sub = subRef.get();
        std::cout << "[CLIENT-SUB:" << sub.channel() << "] Subscribed successfully!" << std::endl;

        auto pubRes = sub.publish({{"message", "I am freeeeeee!!"}});
        if (!pubRes) {
            std::cout << "failed to publish: " << pubRes.error().message << std::endl;
        }

        auto timer = std::make_shared<boost::asio::steady_timer>(ioc, std::chrono::seconds(5));
        timer->async_wait([subRef, timer](boost::system::error_code ec) {
            if (!ec) {
                subRef.get().unsubscribe();
            }
        });
    });

    sub.onUnsubscribed([ch = sub.channel()] {
        std::cout << "[CLIENT-SUB:" << ch << "] Unsubscribed" << std::endl;
    });

    sub.onPublication([ch = sub.channel()](centrifugo::Publication const &pub) {
        std::cout << "[CLIENT-SUB:" << ch << "] Publication received:" << std::endl;
        std::cout << "  Data: " << pub.data << std::endl;
        std::cout << "  Offset: " << pub.offset << std::endl;
        if (pub.info) {
            std::cout << "  From user: " << pub.info->user << " (client: " << pub.info->client
                      << ")" << std::endl;
        }
    });

    sub.onError([ch = sub.channel()](centrifugo::Error const &err) {
        std::cout << "[CLIENT-SUB:" << ch << "] Error: (" << err.ec.value() << ") " << err.message
                  << std::endl;
    });

    if (auto subRes = sub.subscribe(); subRes.has_error()) {
        std::cout << "failed creating subscription: " << subCreateRes.error() << std::endl;
        return 1;
    }

    std::cout << "Starting Centrifugo client..." << std::endl;

    if (auto const conRes = client.connect(); !conRes) {
        std::cout << "Failed to connect: (" << conRes.error().ec.value() << ") "
                  << conRes.error().message << std::endl;
        return 1;
    }

    ioc.run();

    return 0;
}
