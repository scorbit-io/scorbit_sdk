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
class SLB_Sam;
class Spike;

namespace spb {

using probe_t = uint32_t;

enum ProbeType : probe_t {
    None = 0,
    CPU = 1u << 0,
    DMD = 1u << 1,
    NFC = 1u << 2,
    SAM = 1u << 3,
    SPIKE = 1u << 4,

    All = CPU | DMD | NFC | SAM | SPIKE
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

    auto cpu() const -> const std::shared_ptr<ProbeCPU> & { return m_cpu; }
    auto dmd() const -> const std::shared_ptr<ProbeDMD> & { return m_dmd; }
    auto nfc() const -> const std::shared_ptr<ProbeNFC> & { return m_nfc; }
    auto sam() const -> const std::shared_ptr<SLB_Sam> & { return m_sam; }
    auto spike() const -> const std::shared_ptr<Spike> & { return m_spike; }

    auto isNfcTagRead() const -> bool;
    auto setNfcTag(const std::string &tag) -> bool;
    auto setNfcLeds(NfcLedMode mode) -> bool;

private:
    std::shared_ptr<ProbeCPU> m_cpu;
    std::shared_ptr<ProbeDMD> m_dmd;
    std::shared_ptr<ProbeNFC> m_nfc;
    std::shared_ptr<SLB_Sam> m_sam;
    std::shared_ptr<Spike> m_spike;
};

} // namespace spb
