/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * MIT License
 */

#pragma once

#include "wifi_diagnostics.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace scorbit {
namespace detail {
namespace wifi {

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
        std::string publicProbeTarget {"1.1.1.1"};
        std::string scorbitProbeTarget {"sws.scorbit.io"};
        CommandRunner commandRunner {runCommand};
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
    void stop(const std::string &endReason);

    bool isActive() const;
    const std::string &runId() const;

    static std::optional<State> recoverState(const std::string &stateFilePath = defaultStateFilePath());

private:
    void run();
    Sample collectSample(bool includeProbes, bool isFinal = false);
    void maybeEmitLinkEvent(const LinkInfo &link);
    void maybeEmitScanEvent();
    void emitEvent(std::string kind, std::string payloadJson = {}, std::optional<int> reasonCode = {});
    void emitFinalSample();

    Options m_options;
    Callbacks m_callbacks;
    std::atomic_bool m_active {false};
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_thread;
    std::string m_stopReason;
    std::optional<LinkInfo> m_lastLink;
    std::optional<Sample> m_lastSample;
};

} // namespace wifi
} // namespace detail
} // namespace scorbit
