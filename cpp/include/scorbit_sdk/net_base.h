/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#pragma once

namespace scorbit {

namespace detail {
struct GameData;
} // namespace detail

class NetBase
{
public:
    NetBase() = default;
    virtual ~NetBase() = default;

    virtual void sendGameData(const detail::GameData &data) = 0;
};

} // namespace scorbit
