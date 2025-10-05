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

class ProbeBase;
class ProbeCPU;
class ProbeDMD;
class ProbeNFC;

namespace spb {

using probe_t = uint32_t;

enum ProbeType : probe_t {
    None = 0,
    CPU = 1 << 0,
    DMD = 1 << 1,
    NFC = 1 << 2,

    All = CPU | DMD | NFC
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

    auto enumerate(probe_t probesSet, const ProbeDisplayCallback &callback = nullptr) -> void;

    auto cpu() const -> ProbeCPU * { return m_cpu.get(); }
    auto dmd() const -> ProbeDMD * { return m_dmd.get(); }
    auto nfc() const -> ProbeNFC * { return m_nfc.get(); }

    auto isNfcTagRead() const -> bool;
    auto setNfcTag(const std::string &tag) -> bool;
    auto setNfcLeds(NfcLedMode mode) -> bool;

private:
    std::shared_ptr<ProbeCPU> m_cpu;
    std::shared_ptr<ProbeDMD> m_dmd;
    std::shared_ptr<ProbeNFC> m_nfc;
};

} // namespace spb
