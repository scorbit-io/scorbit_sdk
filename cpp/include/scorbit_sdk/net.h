/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#pragma once

#include <scorbit_sdk/export.h>
#include "net_base.h"

namespace scorbit {

class SCORBIT_SDK_EXPORT Net : public NetBase
{
public:
    Net();

    void sendGameData(const detail::GameData &data) override;
};

} // namespace scorbit
