#include <functional>
#include <iostream>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>

#include <centrifugo.h>

auto getJwtToken() -> std::string
{
    namespace beast = boost::beast;
    namespace http = beast::http;
    namespace net = boost::asio;
    using tcp = net::ip::tcp;

    auto constexpr SCORBITRON_UUID = "c7f1fd0b-82f7-5504-8fbe-740c09bc7dab";
    auto constexpr SCORBIT_TOKEN =
            "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
            "eyJ0b2tlbl90eXBlIjoiYWNjZXNzIiwiZXhwIjoxNzU1NTQxNzc1LCJpYXQiOjE3NTU1Mzk5NzUsImp0aSI6Ij"
            "RiNGEyMjExYjA5MjQ2OTE5OWYwZjhlNWUwNTYzZDlkIiwiZGV2aWNlX3V1aWQiOiJjN2YxZmQwYi04MmY3LTU1"
            "MDQtOGZiZS03NDBjMDliYzdkYWIiLCJpc19zY29yYml0cm9uIjp0cnVlfQ.7PuljRy06LFeadFChYIIE3wqDN-"
            "Ud8MU5fuQCS1GF9E";

    auto ioc = net::io_context {};
    auto ctx = boost::asio::ssl::context {boost::asio::ssl::context::tlsv13_client};
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(boost::asio::ssl::verify_peer);

    auto stream = beast::ssl_stream<beast::tcp_stream> {ioc, ctx};

    if (!SSL_set_tlsext_host_name(stream.native_handle(), "staging.scorbit.io")) {
        auto ec = beast::error_code {static_cast<int>(::ERR_get_error()),
                                     net::error::get_ssl_category()};
        std::cerr << "failed to set SNI hostname: " << ec.message() << std::endl;
        return "";
    }

    auto const results = tcp::resolver {ioc}.resolve("staging.scorbit.io", "443");
    beast::get_lowest_layer(stream).connect(results);
    stream.handshake(boost::asio::ssl::stream_base::client);

    auto req = http::request<http::string_body> {
            http::verb::get, "/api/v2/scorbitrons/" + std::string {SCORBITRON_UUID} + "/socket/",
            11};
    req.set(http::field::host, "staging.scorbit.io");
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    req.set(http::field::authorization, "Bearer " + std::string {SCORBIT_TOKEN});
    http::write(stream, req);

    auto buffer = beast::flat_buffer {};
    auto res = http::response<http::string_body> {};
    http::read(stream, buffer, res);

    if (res.result() != http::status::ok) {
        std::cerr << "HTTP error: " << res.result_int() << std::endl;
        return "";
    }

    auto ec = beast::error_code {};
    if (stream.shutdown(ec) && ec != net::ssl::error::stream_truncated) {
        std::cerr << "failed to shutdown ssl connection: " << ec.message() << std::endl;
        return "";
    }

    std::cout << "received token: " << res.body() << std::endl;
    return nlohmann::json::parse(res.body()).at("token");
}

int main()
{
    boost::asio::io_context ioc;
    auto strand = boost::asio::make_strand(ioc);

    centrifugo::Client client(strand, "wss://sws.scorbit.io/connection/websocket",
                              centrifugo::ClientConfig {"",
                                                        std::function<std::string()>(getJwtToken),
                                                        "cpp-user", "1.0.0"});

    client.onConnecting([](centrifugo::Error const &error) {
        std::cout << "[CLIENT] Connecting to Centrifugo... (" << error.ec.value() << ", "
                  << error.message << ')' << std::endl;
    });

    client.onConnected([] { std::cout << "[CLIENT] Connected to Centrifugo!" << std::endl; });

    client.onDisconnected([](centrifugo::Error const &error) {
        std::cout << "[CLIENT] Disconnected from Centrifugo (" << error.ec.value() << ", "
                  << error.message << ')' << std::endl;
    });

    client.onSubscribing([](std::string const &channel) {
        std::cout << "[SERVER-SUB:" << channel << "] Subscribing..." << std::endl;
    });

    client.onSubscribed([&client](std::string const &channel) {
        std::cout << "[SERVER-SUB:" << channel << "] Subscribed successfully!" << std::endl;
    });

    client.onUnsubscribed([](std::string const &channel) {
        std::cout << "[SERVER-SUB:" << channel << "] Unsubscribed" << std::endl;
    });

    client.onPublication([](std::string const &channel, centrifugo::Publication const &pub) {
        std::cout << "[SERVER-SUB:" << channel << "] Publication received:" << std::endl;
        if (pub.info) {
            std::cout << "  From user: " << pub.info->user << " (client: " << pub.info->client
                      << ")" << std::endl;
        }
        std::cout << "  Data: " << pub.data << std::endl;
        std::cout << "  Offset: " << pub.offset << std::endl;
    });

    std::cout << "Starting Centrifugo client with server-side subscriptions..." << std::endl;

    if (auto const conRes = client.connect(); !conRes) {
        std::cout << "failed to connect: (" << conRes.error().ec.value() << ") "
                  << conRes.error().message << std::endl;
        return 1;
    }

    ioc.run();

    return 0;
}
