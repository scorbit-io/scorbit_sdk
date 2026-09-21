/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * MIT License
 */

#include <diagnostics/wifi/network_monitor.h>
#include <nlohmann/json.hpp>

namespace scorbit {
namespace detail {
namespace wifi {
namespace {

bool sameConnection(const LinkInfo &lhs, const LinkInfo &rhs)
{
    return lhs.connected == rhs.connected && lhs.ssid == rhs.ssid && lhs.bssid == rhs.bssid
        && lhs.interfaceName == rhs.interfaceName;
}

std::string linkPayload(const LinkInfo &link)
{
    nlohmann::json j {
            {"interface", link.interfaceName},
            {"connected", link.connected},
            {"ssid", link.ssid},
            {"bssid", link.bssid},
    };
    if (link.rssiDbm) {
        j["rssi_dbm"] = *link.rssiDbm;
    }
    if (link.channel) {
        j["channel"] = *link.channel;
    }
    return j.dump();
}

} // namespace

std::unique_ptr<EventListener> defaultEventListener(std::function<void(Event)> callback)
{
#if defined(__linux__)
    return std::make_unique<WpaSupplicantDbusListener>(std::move(callback));
#else
    (void)callback;
    return nullptr;
#endif
}

NetworkMonitor::NetworkMonitor(Options options, Callbacks callbacks)
    : m_options(std::move(options))
    , m_callbacks(std::move(callbacks))
{
}

NetworkMonitor::~NetworkMonitor()
{
    stop("shutdown");
}

bool NetworkMonitor::start()
{
    bool expected = false;
    if (!m_active.compare_exchange_strong(expected, true)) {
        return false;
    }

    const auto startedAt = std::chrono::system_clock::now();
    writeStateFile(m_options.stateFilePath,
                   State {m_options.runId, startedAt, startedAt + m_options.requestedDuration});

    // Decided once, up front: the rest of the run's shape depends on it.
    if (const auto link = collectLinkInfo(m_options.commandRunner, m_options.preferredInterface);
        link) {
        m_ethernet = link->kind == InterfaceKind::Ethernet;
    }

    if (!m_ethernet) {
        startEventListener();
    }
    m_thread = std::thread {[this] { run(); }};
    return true;
}

void NetworkMonitor::requestStop(const std::string &endReason)
{
    {
        // Only the caller that ends the run names the reason.
        std::scoped_lock lock(m_mutex);
        if (m_active) {
            m_stopReason = endReason;
            m_active = false;
        }
    }
    m_cv.notify_all();
}

void NetworkMonitor::stop(const std::string &endReason)
{
    // Only claim the stop reason if we are the one stopping it, but ALWAYS fall
    // through to the join.
    //
    // The sampler clears m_active itself when its deadline expires, then exits.
    // The thread is finished but still joinable, and m_active is already false
    // -- so the previous `if (!m_active) return;` skipped the join, and the
    // std::thread destructor then ran on a joinable thread and called
    // std::terminate(). That fired on the ordinary completion path: every
    // capture that ran to its natural deadline aborted scorbitd when the
    // monitor was destroyed, since ~NetworkMonitor() calls stop().
    //
    // Guarding only the reason assignment also keeps the end_reason honest: a
    // capture that expired on its own must stay "expired" and not be relabelled
    // by the "shutdown" call that the destructor makes afterwards.
    requestStop(endReason);

    stopEventListener();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool NetworkMonitor::isActive() const
{
    return m_active;
}

const std::string &NetworkMonitor::runId() const
{
    return m_options.runId;
}

std::string NetworkMonitor::endReason() const
{
    std::scoped_lock lock(m_mutex);
    return m_stopReason;
}

std::optional<State> NetworkMonitor::recoverState(const std::string &stateFilePath)
{
    auto state = readStateFile(stateFilePath);
    removeStateFile(stateFilePath);
    return state;
}

void NetworkMonitor::run()
{
    using clock = std::chrono::steady_clock;

    const auto startedSteady = clock::now();
    const auto deadline = startedSteady + m_options.requestedDuration;
    auto nextSample = startedSteady;
    auto nextProbe = startedSteady;
    auto nextScan = startedSteady + m_options.scanInterval;
    // Due immediately, so the very first sample carries a dns verdict instead of "unknown".
    auto nextDependency = startedSteady;

    // Read live every time, never cached in a local. The flag is set by a worker thread finishing
    // a POST that came back 404 or 410, so it can flip at any instant -- including between the loop
    // exiting for an unrelated reason and the final sample being emitted below.
    const auto runIsClosed = [this] {
        return m_options.runClosed && m_options.runClosed->load(std::memory_order_acquire);
    };

    // No "capture_started" event. The server's WifiCaptureEvent.kind is a CLOSED enum --
    // assoc / deauth / scan / dhcp_renew / scorbitd_restart -- so every lifecycle value this
    // class used to emit was rejected with a 400. Run lifecycle is already server-side state
    // (WifiCaptureRun.end_reason + the is_final sample), so these events carried nothing the
    // server did not already have. Do not reintroduce them under a different name.

    while (m_active) {
        // The server has closed this run or does not know it, so everything after this point
        // would be posted into another 410 or 404. Retire immediately rather than finishing the
        // round: the whole point of the terminal-status contract is that the device stops talking
        // to a run the server will not accept.
        if (runIsClosed()) {
            std::scoped_lock lock(m_mutex);
            if (m_active) {
                m_stopReason = "run_closed";
                m_active = false;
            }
            break;
        }

        const auto now = clock::now();
        if (now >= deadline) {
            {
                // Symmetric with stop(): only claim the reason if we are the one
                // ending the capture. A stop() that lands between this loop's
                // m_active check and this lock has already recorded its own
                // reason -- "manual_stop" or "shutdown" -- and relabelling it
                // "expired" would report the wrong end_reason for the run.
                std::scoped_lock lock(m_mutex);
                if (m_active) {
                    m_stopReason = "expired";
                    m_active = false;
                }
            }
            break;
        }

        // Before the sample, so a fresh verdict lands in this round rather than the next one.
        if (now >= nextDependency) {
            m_lastDnsOk = resolveHost(m_options.scorbitProbeTarget);
            nextDependency = now + m_options.dependencyInterval;
        }

        if (now >= nextSample) {
            const bool includeProbes = now >= nextProbe;
            auto sample = collectSample(includeProbes);
            maybeEmitLinkEvent(sample.link);
            m_lastSample = sample;
            if (m_callbacks.onSample) {
                m_callbacks.onSample(sample);
            }
            nextSample = now + m_options.sampleInterval;
            if (includeProbes) {
                nextProbe = now + m_options.probeInterval;
            }
        }

        if (m_options.scanEnabled && !m_ethernet && now >= nextScan) {
            maybeEmitScanEvent();
            nextScan = now + m_options.scanInterval;
        }

        std::unique_lock lock(m_mutex);
        m_cv.wait_for(lock, std::chrono::seconds {1}, [this] { return !m_active.load(); });
    }

    // No final sample when the run is closed -- there is nothing left server-side to accept it,
    // and posting one is exactly the behaviour the 404 or 410 is telling us to stop.
    //
    // Re-read rather than reusing whatever was true when the loop exited: a run that ends on its
    // deadline or a manual stop can still be closed by the server a moment later, while the last
    // in-flight POST is still resolving. That overlap is not remote -- a 410 usually arrives
    // *because* the run is ending. A few instructions of residual window remain and are
    // unavoidable without coordination; the cost there is one POST the server rejects.
    //
    // The listener goes first, and it goes HERE rather than only in stop(). A run that ends on
    // its own -- deadline, 404 or 410 -- is never stop()ped by anyone: the owner keeps the object until
    // the next capture supersedes it. Left running, wpa_supplicant's own bgscan and re-association
    // signals kept posting scan/assoc events into a run that had ended hours earlier (SB-4938).
    stopEventListener();
    if (!runIsClosed()) {
        emitFinalSample();
    }
    // No end-of-run event either: "capture_stopped" / "expired" / "manual_stop" / "shutdown"
    // are all rejected by the closed kind enum (see the note at the top of the loop). Note
    // that `expired` and `manual_stop` ARE valid WifiCaptureRun.end_reason values -- end_reason
    // and event kind were conflated. The server sets end_reason itself.
    //
    // The reason is still recorded in m_stopReason and readable via endReason(), so the owner
    // -- which, unlike this library, has a logger -- can report it. This library deliberately
    // has no logging dependency: it links only Boost and nlohmann.
    removeStateFile(m_options.stateFilePath);
}

Sample NetworkMonitor::collectSample(bool includeProbes, bool isFinal)
{
    Sample sample;
    sample.ts = std::chrono::system_clock::now();
    sample.isFinal = isFinal;

    if (const auto link = collectLinkInfo(m_options.commandRunner, m_options.preferredInterface);
        link) {
        sample.link = *link;
    }

    if (includeProbes) {
        if (const auto gateway = defaultGateway(m_options.commandRunner); gateway) {
            sample.gateway = probeHost(*gateway, 3, m_options.commandRunner);
        }
        sample.publicInternet = probeHost(m_options.publicProbeTarget, 3, m_options.commandRunner);
        sample.scorbit = probeHost(m_options.scorbitProbeTarget, 3, m_options.commandRunner);
    } else if (m_lastSample) {
        sample.gateway = m_lastSample->gateway;
        sample.publicInternet = m_lastSample->publicInternet;
        sample.scorbit = m_lastSample->scorbit;
    }

    DependencySnapshot snapshot;
    if (m_options.dependencyProvider) {
        snapshot = m_options.dependencyProvider();
    }
    sample.dependencyChecks =
            buildDependencyChecks(sample.link, sample.gateway, snapshot, m_lastDnsOk);

    return sample;
}

void NetworkMonitor::maybeEmitLinkEvent(const LinkInfo &link)
{
    if (m_eventListenerActive) {
        m_lastLink = link;
        return;
    }

    if (!m_lastLink) {
        if (link.connected) {
            emitEvent("assoc", linkPayload(link));
        }
        m_lastLink = link;
        return;
    }

    if (sameConnection(*m_lastLink, link)) {
        return;
    }

    if (m_lastLink->connected && !link.connected) {
        emitEvent("deauth", linkPayload(*m_lastLink));
    } else if (link.connected) {
        emitEvent("assoc", linkPayload(link));
    }

    m_lastLink = link;
}

void NetworkMonitor::maybeEmitScanEvent()
{
#if defined(__linux__)
    if (!m_lastLink || m_lastLink->interfaceName.empty()
        || m_lastLink->kind != InterfaceKind::Wifi) {
        return;
    }

    const auto scan = m_options.commandRunner("iw", {"dev", m_lastLink->interfaceName, "scan"});
    if (scan.output.empty()) {
        return;
    }

    size_t apCount = 0;
    std::string::size_type pos = 0;
    while ((pos = scan.output.find("BSS ", pos)) != std::string::npos) {
        ++apCount;
        pos += 4;
    }

    nlohmann::json payload {{"ap_count", apCount}, {"exit_code", scan.exitCode}};
    emitEvent("scan", payload.dump());
#endif
}

void NetworkMonitor::startEventListener()
{
    if (!m_options.eventListenerFactory) {
        return;
    }
    auto listener =
            m_options.eventListenerFactory([this](Event event) { emitEvent(std::move(event)); });
    if (!listener) {
        return;
    }
    m_eventListenerActive = listener->start();

    std::scoped_lock lock(m_eventListenerMutex);
    m_eventListener = std::move(listener);
}

void NetworkMonitor::stopEventListener()
{
    // Take ownership under the lock, stop outside it: stop() joins the listener's thread, and
    // that thread may be inside the event callback, which must never need this mutex.
    std::unique_ptr<EventListener> listener;
    {
        std::scoped_lock lock(m_eventListenerMutex);
        listener = std::move(m_eventListener);
    }
    if (listener) {
        listener->stop();
    }
    m_eventListenerActive = false;
}

void NetworkMonitor::emitEvent(std::string kind, std::string payloadJson,
                               std::optional<int> reasonCode)
{
    Event event;
    event.ts = std::chrono::system_clock::now();
    event.kind = std::move(kind);
    event.reasonCode = reasonCode;
    event.payloadJson = std::move(payloadJson);
    emitEvent(std::move(event));
}

void NetworkMonitor::emitEvent(Event event)
{
    if (!m_callbacks.onEvent) {
        return;
    }

    m_callbacks.onEvent(event);
}

void NetworkMonitor::emitFinalSample()
{
    auto sample = collectSample(false, true);
    if (sample.link.interfaceName.empty() && m_lastSample) {
        sample.link = m_lastSample->link;
        sample.gateway = m_lastSample->gateway;
        sample.publicInternet = m_lastSample->publicInternet;
        sample.scorbit = m_lastSample->scorbit;

        // The fallback just replaced the very inputs collectSample() built dependencyChecks from,
        // so rebuild it. Otherwise the final sample ships a connected link alongside a
        // dependency_checks saying link:blocked -- one payload contradicting itself, on the row
        // that closes the run.
        DependencySnapshot snapshot;
        if (m_options.dependencyProvider) {
            snapshot = m_options.dependencyProvider();
        }
        sample.dependencyChecks =
                buildDependencyChecks(sample.link, sample.gateway, snapshot, m_lastDnsOk);
    }

    if (m_callbacks.onSample) {
        m_callbacks.onSample(sample);
    }
}

} // namespace wifi
} // namespace detail
} // namespace scorbit
