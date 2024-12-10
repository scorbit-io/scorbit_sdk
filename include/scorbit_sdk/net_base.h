/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#pragma once

#include <string>

namespace scorbit {
namespace detail {

struct GameData;

class NetBase
{
public:
    NetBase() = default;
    virtual ~NetBase() = default;

    virtual void authenticate() = 0;

    virtual void sendInstalled(const std::string &type, const std::string &version,
                               bool success = true) = 0;
    virtual void sendGameData(const detail::GameData &data) = 0;
    virtual void sendHeartbeat() = 0;
};

} // namespace detail
} // namespace scorbit
