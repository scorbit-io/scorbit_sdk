#pragma once

#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace centrifugo {

struct ClientInfo {
    std::string user;
    std::string client;
};

struct Publication {
    std::uint64_t offset {0};
    nlohmann::json data;
    std::optional<ClientInfo> info;
    std::unordered_map<std::string, std::string> tags;
};

}
