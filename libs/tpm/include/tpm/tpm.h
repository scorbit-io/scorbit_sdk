/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Jun 2020
 *
 ****************************************************************************/

#pragma once

#include "utils/bytearray.h"
#include "utils/flags.h"
#include <functional>
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

/// Whether an opened chip, known by its serial and UUID, is one to use. Empty accepts any.
using TpmChipFilter = std::function<bool(uint64_t serial, const ByteArray &uuid)>;

extern std::string hex(const ByteArray &array, const std::string &separator = " ");

struct TpmDevice {
    TpmBus bus {TpmBus::None};
    uint8_t i2cBus {0};
    std::string usbDevicePath;

    bool isValid() const { return bus != TpmBus::None; }
};

/// What a locator found: the CDC node the TPM answers on now (empty if none), or that it could not
/// look because another process held the tap pad.
struct TpmLocation {
    TpmLocation(std::string where = {})
        : path(std::move(where))
    {
    }
    TpmLocation(const char *where)
        : path(where ? where : "")
    {
    }
    static TpmLocation busyTapPad()
    {
        TpmLocation location;
        location.busy = true;
        return location;
    }

    std::string path;
    bool busy {false};
};

class Tpm
{
    struct Impl;

public:
    Tpm(TpmBusFlags busFlags = TpmBus::All, const std::string &usbDevicePath = {},
        TpmChipFilter accept = {});
    explicit Tpm(const TpmDevice &device, TpmChipFilter accept = {});
    Tpm(Tpm &&other) noexcept;
    Tpm &operator=(Tpm &&other) noexcept;
    ~Tpm();

    bool ok() const;
    /// An open or a chip call gave up because another process held the tap pad for the whole
    /// wait. When not ok(), the chip is not missing, only busy: worth trying again later.
    bool busy() const;
    TpmDevice device() const;

    ByteArray info();
    ByteArray tpmSerialNumber();

    uint64_t serialNumber() const;
    ByteArray uuid() const;
    bool readIdentity();
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
    /// atcaRetry on this chip, holding the tap pad lock only for a tap pad (not an I2C chip).
    int retry(const std::function<int()> &func);
    bool accepted(Impl *p);
    bool tryI2cBus(Impl *p, uint8_t i2cBus, bool quiet = false);
    bool tryUsbBus(Impl *p, const std::string &devicePath, bool quiet = false);

private:
    std::unique_ptr<Impl> p;
};
