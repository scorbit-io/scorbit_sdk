/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#pragma once

#include "net_types.h"
#include "common_types_c.h"
#include <string>

namespace scorbit {
namespace detail {

struct GameData;

class NetBase
{
public:
    NetBase() = default;
    virtual ~NetBase() = default;

    virtual AuthStatus status() const = 0;

    virtual void authenticate() = 0;

    virtual void sendInstalled(const std::string &type, const std::string &version,
                               bool success = true) = 0;
    virtual void sendGameData(const detail::GameData &data) = 0;
    virtual void sendHeartbeat() = 0;
    virtual void requestPairCode(StringCallback cb) = 0;

    virtual std::string getMachineUuid() const = 0;
    virtual std::string getPairDeeplink() const = 0;
    virtual std::string getClaimDeeplink(int player) const = 0;

    virtual const DeviceInfo &deviceInfo() const = 0;

    virtual void requestTopScores(sb_score_t scoreFilter, StringCallback callback) = 0;
};

} // namespace detail
} // namespace scorbit
