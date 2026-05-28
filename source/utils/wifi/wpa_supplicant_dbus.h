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
#include <functional>
#include <mutex>
#include <thread>

namespace scorbit {
namespace detail {
namespace wifi {

class WpaSupplicantDbusListener
{
public:
    using EventCallback = std::function<void(Event)>;

    explicit WpaSupplicantDbusListener(EventCallback callback);
    ~WpaSupplicantDbusListener();

    WpaSupplicantDbusListener(const WpaSupplicantDbusListener &) = delete;
    WpaSupplicantDbusListener &operator=(const WpaSupplicantDbusListener &) = delete;

    bool start();
    void stop();
    bool isRunning() const;

private:
    void run();
    void emit(Event event);

    EventCallback m_callback;
    std::atomic_bool m_running {false};
    std::thread m_thread;
    std::mutex m_callbackMutex;
};

} // namespace wifi
} // namespace detail
} // namespace scorbit
