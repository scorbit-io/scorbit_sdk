/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * MIT License
 */

#pragma once

#include "wifi_diagnostics.h"
#include "wpa_supplicant_dbus.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace scorbit {
namespace detail {
namespace wifi {

/// wpa_supplicant over D-Bus on Linux; nothing elsewhere.
std::unique_ptr<EventListener> defaultEventListener(std::function<void(Event)> callback);

class NetworkMonitor
{
public:
    struct Options {
        std::string runId;
        std::chrono::seconds requestedDuration {std::chrono::minutes {5}};
        std::chrono::seconds sampleInterval {std::chrono::seconds {30}};
        std::chrono::seconds probeInterval {std::chrono::seconds {60}};
        std::chrono::seconds scanInterval {std::chrono::minutes {5}};
        bool scanEnabled {true};
        std::string stateFilePath {defaultStateFilePath()};
        std::string preferredInterface;
        /// Supplies passive connection liveness for dependency_checks. Optional: without it the
        /// rest443/wss443/clock chips stay "unknown" rather than being guessed.
        DependencyProvider dependencyProvider;
        /**
         * Set by the owner when ingest reports the run is over: HTTP 410 (the server closed it)
         * or 404 (the server does not know it). Both are terminal for the run.
         *
         * A shared flag rather than a call back into the monitor, deliberately. The status is observed
         * on a worker thread completing a POST, and having that thread reach into the monitor to
         * stop it would add a third writer to an object already touched by the Centrifugo
         * dispatcher and the shutdown path -- the ownership problem SB-3461 still has to solve.
         * The sampler polls this instead and retires itself, so no new cross-thread edge exists and
         * the shared_ptr keeps the flag alive regardless of which outlives which.
         */
        std::shared_ptr<std::atomic_bool> runClosed;
        /// Cadence of the ACTIVE dependency probe (DNS). Deliberately far coarser than the sample
        /// interval -- SPEC-0007 §137 calls running active probes every sample needlessly
        /// intrusive, and this one leaves the venue's resolver alone between rounds.
        std::chrono::seconds dependencyInterval {std::chrono::minutes {5}};
        std::string publicProbeTarget {"1.1.1.1"};
        std::string scorbitProbeTarget {"sws.scorbit.io"};
        CommandRunner commandRunner {runCommand};
        /// Source of the run's passive assoc/deauth/scan events. Injectable so a test can watch
        /// the run stop its listener, which no platform without D-Bus could otherwise observe.
        EventListenerFactory eventListenerFactory {defaultEventListener};
    };

    struct Callbacks {
        std::function<void(const Sample &)> onSample;
        std::function<void(const Event &)> onEvent;
    };

    explicit NetworkMonitor(Options options, Callbacks callbacks);
    ~NetworkMonitor();

    NetworkMonitor(const NetworkMonitor &) = delete;
    NetworkMonitor &operator=(const NetworkMonitor &) = delete;

    bool start();
    /// Non-blocking half of stop(): asks the sampler to finish without joining it. Idempotent.
    /// The sampler stops the event listener itself on its way out.
    void requestStop(const std::string &endReason);

    void stop(const std::string &endReason);

    bool isActive() const;
    const std::string &runId() const;

    /**
     * Why the run ended -- "expired", "manual_stop", "shutdown" -- or empty while still running.
     *
     * These are WifiCaptureRun.end_reason values, NOT WifiCaptureEvent.kind values; the monitor
     * deliberately emits no lifecycle event for them. Exposed so the owner can log or report the
     * reason, since this library has no logger of its own.
     *
     * Returns by VALUE, deliberately. Returning `const std::string&` would hand the caller a
     * reference to m_stopReason that outlives the lock taken to read it, so a caller reading it
     * while the sampler thread ends the run would race -- the lock would protect forming the
     * reference and nothing else. Do not "optimise" this back to a reference.
     */
    std::string endReason() const;

    static std::optional<State>
    recoverState(const std::string &stateFilePath = defaultStateFilePath());

private:
    void run();
    Sample collectSample(bool includeProbes, bool isFinal = false);
    void maybeEmitLinkEvent(const LinkInfo &link);
    void maybeEmitScanEvent();
    void startEventListener();
    void stopEventListener();
    void emitEvent(std::string kind, std::string payloadJson = {},
                   std::optional<int> reasonCode = {});
    void emitEvent(Event event);
    void emitFinalSample();

    Options m_options;
    Callbacks m_callbacks;
    std::atomic_bool m_active {false};
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_thread;
    std::string m_stopReason;
    /// Last ACTIVE DNS result, carried forward between dependency rounds.
    ///
    /// SPEC-0007 §137 says to leave active keys absent between re-checks and let the server carry
    /// the last result forward. The panel does not work that way -- it reads only the newest
    /// sample's dict and renders a missing key as "unknown" -- so omitting would make the dns chip
    /// blink to "unknown" for nine samples out of ten. Carried here instead, so the chip is stable.
    std::optional<bool> m_lastDnsOk;
    /// True when this run is sampling Ethernet. wlan0 stays associated on a cabled Scorbitron, so
    /// D-Bus assoc/deauth and scans would report the radio's events for a capture of the cable.
    bool m_ethernet {false};
    std::optional<LinkInfo> m_lastLink;
    std::optional<Sample> m_lastSample;
    /// Guards the pointer only. The sampler stops the listener as the run ends, and stop() may do
    /// the same from another thread at the same moment.
    std::mutex m_eventListenerMutex;
    std::unique_ptr<EventListener> m_eventListener;
    // Atomic for the same reason m_active above is: it is written by whichever
    // thread calls stop()/startEventListener() and read by the sampler thread in
    // maybeEmitLinkEvent(). As a plain bool that was a data race, so the sampler
    // could read a torn or stale value and emit a spurious assoc/deauth during
    // teardown -- and those kinds ARE accepted by the API, so the corruption
    // would land in real diagnostic data rather than being rejected.
    //
    // This removes the race, not the ordering question: a read that happens just
    // before the flag is cleared can still emit one last event. Establishing an
    // owning strand for the monitor is SB-3461's job.
    std::atomic_bool m_eventListenerActive {false};
};

} // namespace wifi
} // namespace detail
} // namespace scorbit
