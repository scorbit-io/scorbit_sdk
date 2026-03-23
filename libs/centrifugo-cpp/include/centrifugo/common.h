#pragma once

#include <string>
#include <functional>

#include <boost/outcome/result.hpp>
#include <nlohmann/json.hpp>

namespace centrifugo {

namespace outcome = boost::outcome_v2;

enum class LogLevel { Debug, Error };

struct LogEntry {
    LogLevel level;
    std::string message;
    nlohmann::json fields;
};

struct ClientConfig {
    std::string token;
    std::function<outcome::result<std::string>()> getToken;
    std::string name;
    std::string version;

    std::chrono::seconds maxPingDelay {10};
    std::chrono::seconds refreshTokenBeforeExpiry {180}; // Refresh token when 3 minutes remain
    std::chrono::milliseconds minReconnectDelay {200};
    std::chrono::milliseconds maxReconnectDelay {20000};

    std::function<void(LogEntry)> logHandler;
};

enum class ConnectionState { Disconnected, Connecting, Connected };

}
