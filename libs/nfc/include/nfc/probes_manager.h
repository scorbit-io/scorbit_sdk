/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2025
 *
 ****************************************************************************/

#pragma once

#include <string>
#include <memory>
#include <functional>
#include <string>
#include <optional>

class ProbeBase;
class ProbeNFC;

namespace nfc {

using probe_t = uint32_t;

enum ProbeType : probe_t {
    None = 0,
    NFC = 1u << 2,

    All = NFC
};

enum class NfcLedMode {
    Idle,
    GameSession,
};

using ProbeDisplayCallback = std::function<void(ProbeBase *probe, const std::string &device)>;

class ProbesManager
{
public:
    ProbesManager() = default;

    auto enumerate(probe_t probesSet, const std::string pbspk2commPath = {},
                   const ProbeDisplayCallback &callback = nullptr) -> void;

    auto nfc() const -> const std::shared_ptr<ProbeNFC> & { return m_nfc; }

    auto isNfcTagRead() const -> bool;
    auto setNfcTag(const std::string &tag) -> bool;
    auto setNfcLeds(NfcLedMode mode) -> bool;

    auto probesBootReason(ProbeType probeType) -> std::optional<std::string>;

private:
    std::shared_ptr<ProbeNFC> m_nfc;
};

} // namespace nfc
