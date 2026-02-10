/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2025
 *
 ****************************************************************************/

#pragma once

#include "event_types.h"
#include <scorbit_sdk/event_helpers_c.h>
#include <string>

namespace scorbit {

class Event
{
public:
    Event(const sb_event_t *event)
        : m_event(event)
    {
    }

    /**
     * @brief This function returns the type of the given event.
     * @return The type of the event as an @ref scorbit::Event::EventType value.
     */
    EventType type() const { return static_cast<EventType>(::sb_event_type(m_event)); }

    /**
     * @brief Helper function to process a game start requested event.
     *
     * This function processes an event representing a game start request.
     * The event type must be @ref scorbit::Type::GameStartRequested, otherwise the function
     * returns an error.
     *
     * @param playersCount [OUT] A reference to an integer that will receive the number of players.
     * @return Returns true on success, or false if an error occurs (e.g., wrong event type was
     * given).
     */
    bool getGameStartRequested(int &playersCount) const
    {
        return ::sb_event_game_start_requested(m_event, &playersCount);
    }

    /**
     * @brief Helper function to process a credits add requested event.
     *
     * This function processes an event representing a credits add request.
     * The event type must be @ref scorbit::Type::CreditsAddRequested, otherwise the function
     * returns an error.
     *
     * @param credits [OUT] A reference to an integer that will receive the number of credits to
     * add.
     * @param transaction [OUT] A reference to a string that will receive the transaction ID.
     * @return Returns true on success, or false if an error occurs (e.g., wrong event type was
     * given).
     */
    bool getCreditsAddRequested(int &credits, std::string &transaction) const
    {
        const char *transactionCStr = nullptr;
        if (!::sb_event_credits_add_requested(m_event, &credits, &transactionCStr)) {
            return false;
        }
        transaction = transactionCStr ? std::string(transactionCStr) : std::string {};
        return true;
    }

    // ---------------- OEM providers can ignore the events below ----------------

    const sb_event_t *event() const { return m_event; }

    bool eventConfigReceived(std::string &configJson) const
    {
        const char *configCStr = nullptr;
        if (!::sb_event_config_received(m_event, &configCStr)) {
            return false;
        }
        configJson = configCStr ? std::string(configCStr) : std::string {};
        return true;
    }

    bool eventScorbitdUpdateReceived(std::string &updateJson) const
    {
        const char *updateCStr = nullptr;
        if (!::sb_event_scorbitd_update_received(m_event, &updateCStr)) {
            return false;
        }
        updateJson = updateCStr ? std::string(updateCStr) : std::string {};
        return true;
    }

    bool eventScorbitdUpdated(std::string &version, std::string &executablePath) const
    {
        const char *versionCStr = nullptr;
        const char *exePathCStr = nullptr;
        if (!::sb_event_scorbitd_updated(m_event, &versionCStr, &exePathCStr)) {
            return false;
        }
        version = versionCStr ? std::string(versionCStr) : std::string {};
        executablePath = exePathCStr ? std::string(exePathCStr) : std::string {};
        return true;
    }

private:
    const sb_event_t *m_event;
};

} // namespace scorbit
