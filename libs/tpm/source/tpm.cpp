/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Jun 2020
 *
 ****************************************************************************/

#include "tpm/tpm.h"

#include <logger/logger.h>
#include <cryptoauthlib.h>
#include <atca_device.h>
#include <host/atca_host.h>
#include <assert.h>
#include <functional>
#include <thread>

using namespace std::chrono_literals;

constexpr uint8_t SLB_I2C_BUS = 1;
constexpr uint8_t SCORBITRON_V1_I2C_BUS = 0;
constexpr auto MAX_RETRY_TIMES = 5;
constexpr auto DELAY_BEFORE_RETRY = 20ms;

namespace {

ATCA_STATUS atcaRetry(std::function<ATCA_STATUS()> func)
{
    ATCA_STATUS status = ATCA_SUCCESS;
    for (int i = 0; i < MAX_RETRY_TIMES; ++i) {
        status = func();
        if (status == ATCA_SUCCESS) {
            return status;
        }
        WRN("{}: error {}, retrying in {}ms...", __func__, static_cast<int>(status),
            DELAY_BEFORE_RETRY.count());
        std::this_thread::sleep_for(DELAY_BEFORE_RETRY);
    }
    ERR("{}: error {}, giving up after {} retries...", __func__, static_cast<int>(status), MAX_RETRY_TIMES);
    return status;
}

} // namespace

struct Tpm::Impl {
    Impl()
        : uuid(16, 0)
    {
        assert(uuid.size() == 16);
    }

    ~Impl()
    {
        if (atcaDevice) {
            atcab_release_ext(&atcaDevice);
        }
    }

    ATCAIfaceCfg atcaConfig;
    ATCADevice atcaDevice {nullptr};

    ByteArray infoResult;
    ByteArray uuid;
    uint64_t serialNumber {0};
};

/********************************************************************
 * Tpm class
 ********************************************************************/

Tpm::Tpm(TpmBusFlags busFlags)
    : p {std::make_unique<Impl>()}
{
    bool found = false;

    // 1- Try to use the local I2C TPM
    if (!found && busFlags.hasFlag(TpmBus::I2C)) {
        found = tryI2cBus(p.get(), SLB_I2C_BUS);
        if (!found) {
            found = tryI2cBus(p.get(), SCORBITRON_V1_I2C_BUS);
        }
    }

    // 2- Try to use the TPM on the Scorbit probe (USB HID)
    if (!found && busFlags.hasFlag(TpmBus::USB)) {
        found = tryUsbBus(p.get());
    }

    if (found) {
        if (ok() && isDataLocked()) {
            readSerialUuid();
        }
    } else {
        WRN("Coudn't initialize HSM device");
    }
}

Tpm::~Tpm()
{
}

bool Tpm::ok() const
{
    return !p->infoResult.empty();
}

ByteArray Tpm::info()
{
    if (!p->infoResult.empty())
        return p->infoResult;

    ByteArray result(INFO_SIZE, 0);
    auto status = atcaRetry(std::bind(calib_info, p->atcaDevice, result.data()));
    if (status != ATCA_SUCCESS) {
        if (status != ATCA_COMM_FAIL) {
            ERR("{}: error {}", __func__, static_cast<int>(status));
        }
        return ByteArray();
    }

    return result;
}

ByteArray Tpm::tpmSerialNumber()
{
    ByteArray result(ATCA_SERIAL_NUM_SIZE, 0);
    auto status = atcaRetry(std::bind(calib_read_serial_number, p->atcaDevice, result.data()));
    if (status != ATCA_SUCCESS) {
        ERR("{}: error {}", __func__, static_cast<int>(status));
        return ByteArray();
    }

    return result;
}

uint64_t Tpm::serialNumber() const
{
    return p->serialNumber;
}

ByteArray Tpm::uuid() const
{
    return p->uuid;
}

bool Tpm::readSerialUuid()
{
    ByteArray data(ATCA_BLOCK_SIZE, 0);
    auto status = atcaRetry(std::bind(calib_read_bytes_zone, p->atcaDevice, ATCA_ZONE_DATA,
                                      SERIAL_SLOT, 0, data.data(), ATCA_BLOCK_SIZE));
    if (status != ATCA_SUCCESS) {
        ERR("{}: slot {}, error {}", __func__, SERIAL_SLOT, static_cast<int>(status));
        return false;
    }

    p->serialNumber = *reinterpret_cast<const uint64_t *>(data.data());
    auto it = data.cbegin() + sizeof(p->serialNumber);
    for (auto &a : p->uuid) {
        a = *it++;
    }

    return true;
}

bool Tpm::isConfigLocked()
{
    bool isLocked = false;
    auto status = atcaRetry(std::bind(calib_is_locked, p->atcaDevice, LOCK_ZONE_CONFIG, &isLocked));
    if (status != ATCA_SUCCESS) {
        ERR("{}: error {}", __func__, static_cast<int>(status));
    }

    return isLocked;
}

bool Tpm::isDataLocked()
{
    bool isLocked = false;
    auto status = atcaRetry(std::bind(calib_is_locked, p->atcaDevice, LOCK_ZONE_DATA, &isLocked));
    if (status != ATCA_SUCCESS) {
        ERR("{}: error {}", __func__, static_cast<int>(status));
    }

    return isLocked;
}

ByteArray Tpm::readConfig()
{
    ByteArray config(ATCA_ECC_CONFIG_SIZE, 0);
    auto status = atcaRetry(std::bind(calib_read_config_zone, p->atcaDevice, config.data()));
    if (status != ATCA_SUCCESS) {
        ERR("{}: error {}", __func__, static_cast<int>(status));
        return ByteArray();
    }

    return config;
}

bool Tpm::verifySignature(const ByteArray &message, const ByteArray &signature,
                          const ByteArray &publicKey)
{
    ByteArray digest(ATCA_SHA256_DIGEST_SIZE, 0);
    atcah_sha256(message.size(), message.data(), digest.data());

    bool isVerified;
    auto status = atcaRetry(std::bind(calib_verify_extern, p->atcaDevice, digest.data(),
                                      signature.data(), publicKey.data(), &isVerified));
    if (status != ATCA_SUCCESS) {
        ERR("{}: error {}", __func__, static_cast<int>(status));
        return false;
    }

    return isVerified;
}

ByteArray Tpm::signMessage(uint16_t keyId, const ByteArray &message)
{
    ByteArray digest(ATCA_SHA256_DIGEST_SIZE, 0);
    atcah_sha256(message.size(), message.data(), digest.data());

    return signDigest(keyId, digest);
}

ByteArray Tpm::signDigest(uint16_t keyId, const ByteArray &digest)
{
    ByteArray signature(ATCA_ECCP256_SIG_SIZE, 0);
    auto status =
            atcaRetry(std::bind(calib_sign, p->atcaDevice, keyId, digest.data(), signature.data()));
    if (status != ATCA_SUCCESS) {
        ERR("{}: error {}", __func__, static_cast<int>(status));
        return ByteArray();
    }

    return signature;
}

ByteArray Tpm::getPublicKey(uint16_t keyId)
{
    ByteArray data(ATCA_ECCP256_PUBKEY_SIZE, 0);
    auto status = atcaRetry(std::bind(calib_get_pubkey, p->atcaDevice, keyId, data.data()));
    if (status != ATCA_SUCCESS) {
        ERR("{}: error {}", __func__, static_cast<int>(status));
        return ByteArray();
    }

    return data;
}

bool Tpm::writeConfig(const uint8_t ATECC508A_CONFIGDATA[])
{
    auto status =
            atcaRetry(std::bind(calib_write_config_zone, p->atcaDevice, ATECC508A_CONFIGDATA));
    if (status != ATCA_SUCCESS) {
        ERR("{}: couldn't write config, error {}", __func__, static_cast<int>(status));
        return false;
    }

    return true;
}

bool Tpm::lockConfig()
{
    auto status = atcaRetry(std::bind(calib_lock_config_zone, p->atcaDevice));
    if (status != ATCA_SUCCESS) {
        ERR("{}: couldn't lock config, error {}", __func__, static_cast<int>(status));
        return false;
    }

    return true;
}

bool Tpm::lockData()
{
    if (isDataLocked())
        return false;

    auto status = atcaRetry(std::bind(calib_lock_data_zone, p->atcaDevice));
    if (status != ATCA_SUCCESS) {
        ERR("{}: couldn't lock data zone, error {}", __func__, static_cast<int>(status));
        return false;
    }

    return true;
}

bool Tpm::writeKey(uint16_t keyId, const ByteArray &key)
{
    if (key.size() != 4 + ATCA_ECCP256_KEY_SIZE) {
        ERR("{}: wrong key size {} for slot {}", __func__, key.size(), keyId);
        return false;
    }

    auto status = atcaRetry(
            std::bind(calib_priv_write, p->atcaDevice, keyId, key.data(), 0, nullptr, nullptr));
    if (status != ATCA_SUCCESS) {
        ERR("{}: slot {}, error {}", __func__, keyId, static_cast<int>(status));
        return false;
    }

    return true;
}

bool Tpm::genKey(uint16_t keyId)
{
    auto status = atcaRetry(std::bind(calib_genkey, p->atcaDevice, keyId, nullptr));
    if (status != ATCA_SUCCESS) {
        ERR("{}: couldn't generate key on slot {}, error {}", __func__, keyId,
            static_cast<int>(status));
        return false;
    }

    return true;
}

bool Tpm::writeData(uint16_t slot, const ByteArray &data)
{
    if (data.size() == 0 || data.size() % ATCA_BLOCK_SIZE != 0) {
        ERR("{}: wrong data size {} for slot {}", __func__, data.size(), slot);
        return false;
    }

    auto status = atcaRetry(std::bind(calib_write_bytes_zone, p->atcaDevice, ATCA_ZONE_DATA, slot,
                                      0, data.data(), data.size()));
    if (status != ATCA_SUCCESS) {
        ERR("{}: slot {}, error {}", __func__, slot, static_cast<int>(status));
        return false;
    }

    return true;
}

bool Tpm::tryI2cBus(Impl *p, uint8_t i2cBus)
{
    INF("Trying I2C TPM: on bus {}", i2cBus);

    // Re-initialize the atcaDevice and atcaConfig
    if (p->atcaDevice) {
        atcab_release_ext(&p->atcaDevice);
    }
    p->atcaDevice = nullptr;
    p->atcaConfig = ATCAIfaceCfg();

    p->atcaConfig.iface_type = ATCA_I2C_IFACE;
    p->atcaConfig.devtype = ATECC508A;
    p->atcaConfig.atcai2c.address = I2C_ADDRESS;
    p->atcaConfig.atcai2c.bus = i2cBus;
    p->atcaConfig.atcai2c.baud = 100000;
    p->atcaConfig.wake_delay = 1500;
    p->atcaConfig.rx_retries = 20;

    if (atcab_init_ext(&p->atcaDevice, &p->atcaConfig) != ATCA_SUCCESS) {
        return false;
    }

    p->infoResult = info();
    return ok();
}

bool Tpm::tryUsbBus(Impl *p)
{
    INF("Trying USB TPM");

    // Re-initialize the atcaDevice and atcaConfig
    if (p->atcaDevice) {
        atcab_release_ext(&p->atcaDevice);
    }
    p->atcaDevice = nullptr;
    p->atcaConfig = ATCAIfaceCfg();

// Requires "ATCA_HAL_KIT_HID ON" in lib_cryptoauth.cmake
#ifdef DM320118 // TPM on Microchip dev board
    p->atcaConfig.iface_type = ATCA_HID_IFACE;
    p->atcaConfig.devtype = ATECC608A;
    p->atcaConfig.atcahid.idx = 0;
    p->atcaConfig.atcahid.vid = 0x3eb;
    p->atcaConfig.atcahid.pid = 0x2312;
    p->atcaConfig.atcahid.packetsize = 64;
#else  // TPM on Scorbit Probe
    p->atcaConfig.iface_type = ATCA_HID_IFACE;
    p->atcaConfig.devtype = ATECC508A;
    p->atcaConfig.atcahid.idx = 0;
    p->atcaConfig.atcahid.vid = 0xCAFE;
    p->atcaConfig.atcahid.pid = 0x4005;
    p->atcaConfig.atcahid.packetsize = 64;
#endif // DM320118
    p->atcaConfig.wake_delay = 1500;
    p->atcaConfig.rx_retries = 20;

    if (auto status = atcab_init_ext(&p->atcaDevice, &p->atcaConfig); status != ATCA_SUCCESS) {
        ERR("atca_init_ext failed: {}", static_cast<int>(status));
        return false;
    }

    p->infoResult = info();
    return ok();
}
