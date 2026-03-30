/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2025
 *
 ****************************************************************************/

#pragma once

#include "event_types.h"
#include "player_info.h"
#include <scorbit_sdk/event_helpers_c.h>
#include <string>
#include <map>
#include <vector>

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

    /**
     * @brief Helper function to extract payments enabled status from a
     * @ref scorbit::Type::ConfigReceived event.
     *
     * This function extracts the payments_enabled field from a config received event.
     * The event type must be @ref scorbit::EventType::ConfigReceived, otherwise the function
     * returns an error.
     *
     * @param paymentsEnabled [OUT] A reference to a bool that will receive the payments enabled
     * value.
     * @return Returns true on success, or false if an error occurs (e.g., wrong event type was
     * given).
     */
    bool getConfigPaymentsEnabled(bool &paymentsEnabled) const
    {
        return ::sb_event_config_payments_enabled(m_event, &paymentsEnabled);
    }

    /**
     * @brief Helper function to process a players updated event.
     *
     * Populates a map of player numbers to @ref PlayerInfo structs from the event data.
     * The event type must be @ref scorbit::EventType::PlayersUpdated, otherwise the function
     * returns false.
     *
     * @param players [OUT] A reference to a map that will receive the player data.
     * @return Returns true on success, or false if an error occurs (e.g., wrong event type).
     */
    bool getPlayersUpdated(std::map<sb_player_t, PlayerInfo> &players) const
    {
        int count = 0;
        if (!::sb_event_players_updated(m_event, &count)) {
            return false;
        }
        players.clear();
        for (int i = 0; i < count; ++i) {
            sb_player_t playerNum = 0;
            if (!::sb_event_player_number(m_event, i, &playerNum)) {
                continue;
            }
            PlayerInfo info;
            const char *str = nullptr;
            if (::sb_event_player_id(m_event, i, &str) && str) {
                info.id = str;
            }
            if (::sb_event_player_preferred_name(m_event, i, &str) && str) {
                info.preferredName = str;
            }
            if (::sb_event_player_name(m_event, i, &str) && str) {
                info.name = str;
            }
            if (::sb_event_player_initials(m_event, i, &str) && str) {
                info.initials = str;
            }
            if (::sb_event_player_picture_url(m_event, i, &str) && str) {
                info.pictureUrl = str;
            }
            players.emplace(playerNum, std::move(info));
        }
        return true;
    }

    /**
     * @brief Helper function to process a player picture ready event.
     *
     * Retrieves the player number and picture binary data from the event.
     * The event type must be @ref scorbit::EventType::PlayerPictureReady, otherwise the function
     * returns false.
     *
     * @param player [OUT] A reference to receive the player number.
     * @param picture [OUT] A reference to a vector that will receive the picture bytes (JPEG).
     * @return Returns true on success, or false if an error occurs (e.g., wrong event type).
     */
    bool getPlayerPictureReady(sb_player_t &player, std::vector<uint8_t> &picture) const
    {
        const uint8_t *data = nullptr;
        size_t size = 0;
        if (!::sb_event_player_picture_ready(m_event, &player, &data, &size)) {
            return false;
        }
        picture.assign(data, data + size);
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

    bool eventFirmwaresListReceived(std::string &firmwaresList) const
    {
        const char *firmwaresListCStr = nullptr;
        if (!::sb_event_firmwares_list_received(m_event, &firmwaresListCStr)) {
            return false;
        }
        firmwaresList = firmwaresListCStr ? std::string(firmwaresListCStr) : std::string {};
        return true;
    }

private:
    const sb_event_t *m_event;
};

} // namespace scorbit
