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
     * @param playersCount [IN] A reference to an integer that will receive the number of players.
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
     * @param creditsToAdd [IN] A reference to an integer that will receive the number of credits to
     * add.
     * @return Returns true on success, or false if an error occurs (e.g., wrong event type was
     * given).
     */
    bool getCreditsAddRequested(int &creditsToAdd) const
    {
        return ::sb_event_credits_add_requested(m_event, &creditsToAdd);
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

private:
    const sb_event_t *m_event;
};

} // namespace scorbit
