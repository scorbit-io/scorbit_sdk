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

    // ---------------- Achievement Event Helpers ----------------

    /**
     * @brief Helper function to process an achievement unlocked event.
     *
     * This function processes an event representing an achievement unlock.
     * The event type must be @ref scorbit::EventType::AchievementUnlocked, otherwise the function
     * returns false.
     *
     * @param key [OUT] A reference to a string that will receive the achievement key.
     * @param name [OUT] A reference to a string that will receive the achievement name.
     * @param userId [OUT] A reference to a string that will receive the user ID (UUID).
     * @param username [OUT] A reference to a string that will receive the username.
     * @param iconUrl [OUT] A reference to a string that will receive the icon URL (may be empty).
     * @param isTrophy [OUT] A reference to a bool that will receive whether this is a trophy.
     * @return Returns true on success, or false if an error occurs (e.g., wrong event type).
     */
    bool getAchievementUnlocked(std::string &key, std::string &name, std::string &userId,
                                std::string &username, std::string &iconUrl, bool &isTrophy) const
    {
        const char *keyCStr = nullptr;
        const char *nameCStr = nullptr;
        const char *userIdCStr = nullptr;
        const char *usernameCStr = nullptr;
        const char *iconUrlCStr = nullptr;
        if (!::sb_event_achievement_unlocked(m_event, &keyCStr, &nameCStr, &userIdCStr,
                                             &usernameCStr, &iconUrlCStr, &isTrophy)) {
            return false;
        }
        key = keyCStr ? std::string(keyCStr) : std::string {};
        name = nameCStr ? std::string(nameCStr) : std::string {};
        userId = userIdCStr ? std::string(userIdCStr) : std::string {};
        username = usernameCStr ? std::string(usernameCStr) : std::string {};
        iconUrl = iconUrlCStr ? std::string(iconUrlCStr) : std::string {};
        return true;
    }

    /**
     * @brief Helper function to process an achievement locked event.
     *
     * This function processes an event representing an achievement being revoked.
     * The event type must be @ref scorbit::EventType::AchievementLocked, otherwise the function
     * returns false.
     *
     * @param key [OUT] A reference to a string that will receive the achievement key.
     * @param name [OUT] A reference to a string that will receive the achievement name.
     * @param userId [OUT] A reference to a string that will receive the user ID (UUID).
     * @param username [OUT] A reference to a string that will receive the username.
     * @param iconUrl [OUT] A reference to a string that will receive the icon URL (may be empty).
     * @return Returns true on success, or false if an error occurs (e.g., wrong event type).
     */
    bool getAchievementLocked(std::string &key, std::string &name, std::string &userId,
                              std::string &username, std::string &iconUrl) const
    {
        const char *keyCStr = nullptr;
        const char *nameCStr = nullptr;
        const char *userIdCStr = nullptr;
        const char *usernameCStr = nullptr;
        const char *iconUrlCStr = nullptr;
        if (!::sb_event_achievement_locked(m_event, &keyCStr, &nameCStr, &userIdCStr, &usernameCStr,
                                           &iconUrlCStr)) {
            return false;
        }
        key = keyCStr ? std::string(keyCStr) : std::string {};
        name = nameCStr ? std::string(nameCStr) : std::string {};
        userId = userIdCStr ? std::string(userIdCStr) : std::string {};
        username = usernameCStr ? std::string(usernameCStr) : std::string {};
        iconUrl = iconUrlCStr ? std::string(iconUrlCStr) : std::string {};
        return true;
    }

    /**
     * @brief Helper function to process an achievement progress event.
     *
     * This function processes an event representing progress towards an achievement.
     * The event type must be @ref scorbit::EventType::AchievementProgress, otherwise the function
     * returns false.
     *
     * @param key [OUT] A reference to a string that will receive the achievement key.
     * @param name [OUT] A reference to a string that will receive the achievement name.
     * @param userId [OUT] A reference to a string that will receive the user ID (UUID).
     * @param username [OUT] A reference to a string that will receive the username.
     * @param iconUrl [OUT] A reference to a string that will receive the icon URL (may be empty).
     * @param currentValue [OUT] A reference to an int that will receive the current progress.
     * @param targetValue [OUT] A reference to an int that will receive the target value.
     * @return Returns true on success, or false if an error occurs (e.g., wrong event type).
     */
    bool getAchievementProgress(std::string &key, std::string &name, std::string &userId,
                                std::string &username, std::string &iconUrl, int &currentValue,
                                int &targetValue) const
    {
        const char *keyCStr = nullptr;
        const char *nameCStr = nullptr;
        const char *userIdCStr = nullptr;
        const char *usernameCStr = nullptr;
        const char *iconUrlCStr = nullptr;
        if (!::sb_event_achievement_progress(m_event, &keyCStr, &nameCStr, &userIdCStr,
                                             &usernameCStr, &iconUrlCStr, &currentValue,
                                             &targetValue)) {
            return false;
        }
        key = keyCStr ? std::string(keyCStr) : std::string {};
        name = nameCStr ? std::string(nameCStr) : std::string {};
        userId = userIdCStr ? std::string(userIdCStr) : std::string {};
        username = usernameCStr ? std::string(usernameCStr) : std::string {};
        iconUrl = iconUrlCStr ? std::string(iconUrlCStr) : std::string {};
        return true;
    }

private:
    const sb_event_t *m_event;
};

} // namespace scorbit
