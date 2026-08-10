/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2025
 *
 ****************************************************************************/

#pragma once

#include "event_types.h"
#include "player_info.h"
#include "pricing_info.h"
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
     * @brief Helper function to process a pricing received event.
     *
     * Populates a @ref PricingInfo struct with free_play, payments_enabled, credit prices,
     * and bundle data from the event.
     *
     * The event type must be @ref scorbit::EventType::PricingReceived, otherwise the function
     * returns false.
     *
     * @param info [OUT] A reference to a PricingInfo that will receive the pricing data.
     * @return Returns true on success, or false if an error occurs (e.g., wrong event type).
     */
    bool getPricingReceived(PricingInfo &info) const
    {
        if (!::sb_event_pricing_free_play(m_event, &info.freePlay)) {
            return false;
        }
        ::sb_event_pricing_payments_enabled(m_event, &info.paymentsEnabled);

        const char *str = nullptr;
        if (::sb_event_pricing_credit_price(m_event, &str) && str) {
            info.creditPrice = str;
        }
        if (::sb_event_pricing_credit_regular_price(m_event, &str) && str) {
            info.creditRegularPrice = str;
        }
        if (::sb_event_pricing_credit_sale_price(m_event, &str) && str) {
            info.creditSalePrice = str;
        }

        int count = 0;
        ::sb_event_pricing_bundles_count(m_event, &count);
        info.bundles.clear();
        for (int i = 0; i < count; ++i) {
            BundlePrice bundle;
            ::sb_event_pricing_bundle_credits(m_event, i, &bundle.credits);
            if (::sb_event_pricing_bundle_price(m_event, i, &str) && str) {
                bundle.price = str;
            }
            if (::sb_event_pricing_bundle_regular_price(m_event, i, &str) && str) {
                bundle.regularPrice = str;
            }
            if (::sb_event_pricing_bundle_sale_price(m_event, i, &str) && str) {
                bundle.salePrice = str;
            }
            info.bundles.push_back(std::move(bundle));
        }
        return true;
    }

    // ------------------------------------------------------------------
    // Pairing
    // ------------------------------------------------------------------

    /**
     * @brief Helper function to process a pairing status changed event.
     *
     * The event type must be @ref scorbit::EventType::PairingStatusChanged, otherwise the function
     * returns false.
     *
     * @param isPaired [OUT] Whether the device is now paired.
     * @return Returns true on success, or false if the event type does not match.
     */
    bool getPairingStatusChanged(bool &isPaired) const
    {
        return ::sb_event_pairing_status_changed(m_event, &isPaired);
    }

    /**
     * @brief Helper function to process a players updated event.
     *
     * Populates a map of player numbers to @ref PlayerInfo structs from the event data.
     * Each player slot is either claimed (profile info populated) or unclaimed
     * (claimDeeplink populated).
     *
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
        for (sb_player_t p = 1; p <= static_cast<sb_player_t>(count); ++p) {
            PlayerInfo info;
            const char *str = nullptr;
            if (::sb_event_player_id(m_event, p, &str) && str) {
                info.id = str;
            }
            if (::sb_event_player_preferred_name(m_event, p, &str) && str) {
                info.preferredName = str;
            }
            if (::sb_event_player_name(m_event, p, &str) && str) {
                info.name = str;
            }
            if (::sb_event_player_initials(m_event, p, &str) && str) {
                info.initials = str;
            }
            if (::sb_event_player_picture_url(m_event, p, &str) && str) {
                info.pictureUrl = str;
            }
            if (::sb_event_player_claim_deeplink(m_event, p, &str) && str) {
                info.claimDeeplink = str;
            }
            players.emplace(p, std::move(info));
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

    // ------------------------------------------------------------------
    // Diagnostics
    // ------------------------------------------------------------------

    /**
     * @brief Helper function to process a diagnostics upload requested event.
     *
     * @param includeRecordings [OUT] Whether recordings should be included.
     * @return Returns true on success, or false if the event type does not match.
     */
    bool getDiagnosticsUploadRequested(bool &includeRecordings) const
    {
        return ::sb_event_diagnostics_upload_requested(m_event, &includeRecordings);
    }

    /**
     * @brief Helper function to process a diagnostics uploaded event.
     *
     * @param success [OUT] Whether the upload succeeded.
     * @return Returns true on success, or false if the event type does not match.
     */
    bool getDiagnosticsUploaded(bool &success) const
    {
        return ::sb_event_diagnostics_uploaded(m_event, &success);
    }

    // ------------------------------------------------------------------
    // Achievements
    // ------------------------------------------------------------------

    /**
     * @brief Helper function to process an achievement unlocked event.
     *
     * This is the server's authoritative unlock - a local match from
     * @ref scorbit::detail::AchievementManager is only predictive.
     *
     * The event type must be @ref scorbit::EventType::AchievementUnlocked, otherwise the function
     * returns false.
     *
     * @param key [OUT] The achievement key.
     * @param name [OUT] The achievement display name.
     * @param userId [OUT] The user's UUID. The server publishes a UUID here, not the numeric user
     * id taken by @ref GameState::unlockAchievement.
     * @param username [OUT] The user's username.
     * @param isTrophy [OUT] Whether this achievement is a trophy.
     * @return Returns true on success, or false if the event type does not match.
     */
    bool getAchievementUnlocked(std::string &key, std::string &name, std::string &userId,
                                std::string &username, bool &isTrophy) const
    {
        const char *keyCStr = nullptr;
        const char *nameCStr = nullptr;
        const char *userIdCStr = nullptr;
        const char *usernameCStr = nullptr;
        if (!::sb_event_achievement_unlocked(m_event, &keyCStr, &nameCStr, &userIdCStr,
                                             &usernameCStr, &isTrophy)) {
            return false;
        }
        key = keyCStr ? std::string(keyCStr) : std::string {};
        name = nameCStr ? std::string(nameCStr) : std::string {};
        userId = userIdCStr ? std::string(userIdCStr) : std::string {};
        username = usernameCStr ? std::string(usernameCStr) : std::string {};
        return true;
    }

    /**
     * @brief Helper function to process an achievement locked (trophy revoked) event.
     *
     * The event type must be @ref scorbit::EventType::AchievementLocked, otherwise the function
     * returns false.
     *
     * @param key [OUT] The achievement key.
     * @param name [OUT] The achievement display name.
     * @param userId [OUT] The UUID of the user who lost the achievement.
     * @param username [OUT] That user's username.
     * @param isTrophy [OUT] Whether this achievement is a trophy.
     * @return Returns true on success, or false if the event type does not match.
     */
    bool getAchievementLocked(std::string &key, std::string &name, std::string &userId,
                              std::string &username, bool &isTrophy) const
    {
        const char *keyCStr = nullptr;
        const char *nameCStr = nullptr;
        const char *userIdCStr = nullptr;
        const char *usernameCStr = nullptr;
        if (!::sb_event_achievement_locked(m_event, &keyCStr, &nameCStr, &userIdCStr, &usernameCStr,
                                           &isTrophy)) {
            return false;
        }
        key = keyCStr ? std::string(keyCStr) : std::string {};
        name = nameCStr ? std::string(nameCStr) : std::string {};
        userId = userIdCStr ? std::string(userIdCStr) : std::string {};
        username = usernameCStr ? std::string(usernameCStr) : std::string {};
        return true;
    }

    /**
     * @brief Helper function to process an achievement progress event.
     *
     * A progress event at 100% arrives alongside an
     * @ref scorbit::EventType::AchievementUnlocked event for the same achievement - show one
     * celebration, not two.
     *
     * The event type must be @ref scorbit::EventType::AchievementProgress, otherwise the function
     * returns false.
     *
     * @param key [OUT] The achievement key.
     * @param name [OUT] The achievement display name.
     * @param userId [OUT] The user's UUID.
     * @param username [OUT] The user's username.
     * @param progress [OUT] The server's current progress value.
     * @param target [OUT] The target value needed to unlock, or 0 when the server omitted it.
     * @return Returns true on success, or false if the event type does not match.
     */
    bool getAchievementProgress(std::string &key, std::string &name, std::string &userId,
                                std::string &username, int &progress, int &target) const
    {
        const char *keyCStr = nullptr;
        const char *nameCStr = nullptr;
        const char *userIdCStr = nullptr;
        const char *usernameCStr = nullptr;
        if (!::sb_event_achievement_progress(m_event, &keyCStr, &nameCStr, &userIdCStr,
                                             &usernameCStr, &progress, &target)) {
            return false;
        }
        key = keyCStr ? std::string(keyCStr) : std::string {};
        name = nameCStr ? std::string(nameCStr) : std::string {};
        userId = userIdCStr ? std::string(userIdCStr) : std::string {};
        username = usernameCStr ? std::string(usernameCStr) : std::string {};
        return true;
    }

    /**
     * @brief Icon URL from an achievement unlocked, locked, or progress event.
     *
     * @param iconUrl [OUT] The icon URL.
     * @return Returns true on success, or false if this is not an achievement event or the server
     * published no icon.
     */
    bool getAchievementIconUrl(std::string &iconUrl) const
    {
        const char *str = nullptr;
        if (!::sb_event_achievement_icon_url(m_event, &str) || !str) {
            return false;
        }
        iconUrl = str;
        return true;
    }

    /**
     * @brief Unlock timestamp from an achievement unlocked event.
     *
     * @param achievedTime [OUT] The ISO-8601 timestamp.
     * @return Returns true on success, or false if the event type does not match or the server
     * published no timestamp.
     */
    bool getAchievementAchievedTime(std::string &achievedTime) const
    {
        const char *str = nullptr;
        if (!::sb_event_achievement_achieved_time(m_event, &str) || !str) {
            return false;
        }
        achievedTime = str;
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
