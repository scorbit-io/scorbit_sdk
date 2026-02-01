/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Jun 2020
 *
 ****************************************************************************/

#pragma once

#include <string>
#include <cstdint>
#include <utils/bytearray.h>

constexpr auto HW_PROVIDER = "scorbitron";
constexpr auto SW_PROVIDER = "vscorbitron";

class ITpm
{
public:
    ITpm() = default;
    virtual ~ITpm() = default;

    virtual bool isValid() const = 0;

    virtual std::string provider() const = 0;
    virtual uint64_t serial() const = 0;
    virtual std::string uuid() const = 0;

    virtual std::string signMessage(const utils::ByteArray &message) const = 0;
    virtual utils::ByteArray
    signDigest(const utils::ByteArray &message) const = 0;
};
