/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Jun 2020
 *
 ****************************************************************************/

#pragma once

#include "utils/bytearray.h"
#include "utils/flags.h"
#include <string>
#include <memory>

using ByteArray = utils::ByteArray;

constexpr uint8_t I2C_ADDRESS = 0xC0;
constexpr uint16_t KEY0_SLOT = 0;
constexpr uint16_t KEY1_SLOT = 1;
constexpr uint16_t KEY2_SLOT = 2;
constexpr uint16_t KEY3_SLOT = 3;
constexpr uint16_t KEY4_SLOT = 4;
constexpr uint16_t SERIAL_SLOT = 5;

enum TpmBus : uint8_t {
    None = 0,
    I2C = 1 << 0,
    USB = 1 << 1,

    All = I2C | USB,
};
using TpmBusFlags = utils::Flags<TpmBus>;

extern std::string hex(const ByteArray &array, const std::string &separator = " ");

struct TpmDevice {
    TpmBus bus {TpmBus::None};
    uint8_t i2cBus {0};
    std::string usbDevicePath;

    bool isValid() const { return bus != TpmBus::None; }
};

class Tpm
{
    struct Impl;

public:
    Tpm(TpmBusFlags busFlags = TpmBus::All, const std::string &usbDevicePath = {});
    explicit Tpm(const TpmDevice &device);
    Tpm(Tpm &&other) noexcept;
    Tpm &operator=(Tpm &&other) noexcept;
    ~Tpm();

    bool ok() const;
    TpmDevice device() const;

    ByteArray info();
    ByteArray tpmSerialNumber();

    uint64_t serialNumber() const;
    ByteArray uuid() const;
    bool readSerialUuid();

    bool isConfigLocked();
    bool isDataLocked();

    ByteArray readConfig();

    bool verifySignature(const ByteArray &message, const ByteArray &signature,
                         const ByteArray &publicKey);
    ByteArray signMessage(uint16_t keyId, const ByteArray &message);
    ByteArray signDigest(uint16_t keyId, const ByteArray &digest);
    ByteArray getPublicKey(uint16_t keyId);

    bool writeConfig(const uint8_t ATECC508A_CONFIGDATA[]);
    bool lockConfig();
    bool lockData();

    bool writeKey(uint16_t keyId, const ByteArray &key);
    bool genKey(uint16_t keyId);
    bool writeData(uint16_t slot, const ByteArray &data);

private:
    bool tryI2cBus(Impl *p, uint8_t i2cBus, bool quiet = false);
    bool tryUsbBus(Impl *p, const std::string &devicePath, bool quiet = false);
    bool readIdentity();

private:
    std::unique_ptr<Impl> p;
};
