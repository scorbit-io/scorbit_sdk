/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * MIT License
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include "scorbit_sdk/event_types.h"
#include "player_profiles_manager.h"
#include "identifiers.h"
#include <logger/logger.h>
#include <nlohmann/json.hpp>
#include <string>
#include <functional>
#include <optional>
#include <iterator>

struct sb_event_t {
    virtual ~sb_event_t() = default;

protected:
    sb_event_t() = default;
};

namespace scorbit {
namespace detail {

enum EventPriority {
    Normal,
    High,
    Highest,
};

class EventBase : public sb_event_t
{
public:
    auto type() const -> EventType { return m_type; }

    // To use in std::priority_queue
    auto operator<(const EventBase &other) const -> bool
    {
        if (m_priority == other.m_priority) {
            return m_order > other.m_order; // Lower number has higher priority
        }
        return m_priority < other.m_priority; // Higher priority value has higher priority
    }

protected:
    explicit EventBase(EventType type, EventPriority priority)
        : m_type {type}
        , m_priority {priority}
    {
    }

private:
    EventType m_type;

    EventPriority m_priority;
    size_t m_order {++s_orderCounter}; // Incremental order to maintain FIFO for same priority

    static size_t s_orderCounter;
};

inline size_t EventBase::s_orderCounter {0};

using EventCallback = std::function<void(const EventBase &event)>;

// --------------- GameStartRequestedEvent implementations ----------------

class GameStartRequestedEvent : public EventBase
{
public:
    explicit GameStartRequestedEvent(int playersCount)
        : EventBase(EventType::GameStartRequested, EventPriority::Highest)
        , m_playersCount {playersCount}
    {
    }

    auto playersCount() const -> int { return m_playersCount; }

private:
    int m_playersCount;
};

// ---------------- CreditsAddRequested implementations ----------------

class CreditsAddRequestedEvent : public EventBase
{
public:
    explicit CreditsAddRequestedEvent(int creditsToAdd, std::string transaction)
        : EventBase(EventType::CreditsAddRequested, EventPriority::High)
        , m_creditsToAdd {creditsToAdd}
        , m_transaction {std::move(transaction)}
    {
    }

    auto credits() const -> int { return m_creditsToAdd; }
    auto transaction() const -> const std::string & { return m_transaction; }

private:
    int m_creditsToAdd;
    std::string m_transaction;
};

// ---------------- CreditsStatusRequestedEvent implementations ----------------

class CreditsStatusRequestedEvent : public EventBase
{
public:
    explicit CreditsStatusRequestedEvent()
        : EventBase(EventType::CreditsStatusRequested, EventPriority::High)
    {
    }
};

// ---------------- ConfigReceived implementation ----------------

class ConfigReceivedEvent : public EventBase
{
public:
    explicit ConfigReceivedEvent(nlohmann::json configJson)
        : EventBase(EventType::ConfigReceived, EventPriority::Normal)
        // Important: do not use braces in initializion of m_configJson! It will create json array
        , m_configJson(std::move(configJson))
    {
    }

    auto configJson() const -> const nlohmann::json & { return m_configJson; }

    auto configJsonCStr() const -> const char *
    {
        if (m_configJsonStr.empty()) {
            m_configJsonStr = m_configJson.dump();
        }
        return m_configJsonStr.c_str();
    }

    auto paymentsEnabled() const -> std::optional<bool>
    {
        std::optional<bool> rv;
        if (const auto it = m_configJson.find(JKEY_SOBJ_PAYMENTS_ENABLED);
            it != m_configJson.end() && it->is_boolean()) {
            rv = it->get<bool>();
        }
        return rv;
    }

private:
    nlohmann::json m_configJson;
    mutable std::string m_configJsonStr;
};

// ---------------- ScorbitdUpdateReceived implementation ----------------

class ScorbitdUpdateReceivedEvent : public EventBase
{
public:
    explicit ScorbitdUpdateReceivedEvent(const std::string &updateJson)
        : EventBase(EventType::ScorbitdUpdateReceived, EventPriority::Normal)
        , m_updateJson {updateJson}
    {
    }

    auto updateJson() const -> const std::string & { return m_updateJson; }

private:
    std::string m_updateJson;
};

// ---------------- ScorbitdUpdated implementation ----------------

class ScorbitdUpdatedEvent : public EventBase
{
public:
    explicit ScorbitdUpdatedEvent(const std::string &version, const std::string &executable)
        : EventBase(EventType::ScorbitdUpdated, EventPriority::Normal)
        , m_version {version}
        , m_executable {executable}
    {
    }

    auto version() const -> const std::string & { return m_version; }
    auto executable() const -> const std::string & { return m_executable; }

private:
    std::string m_version;
    std::string m_executable;
};

// ---------------- FirmwaresListReceived implementation ----------------

class FirmwaresListReceivedEvent : public EventBase
{
public:
    explicit FirmwaresListReceivedEvent(const std::string &firmwaresList)
        : EventBase(EventType::FirmwaresListReceived, EventPriority::Normal)
        , m_firmwaresList {firmwaresList}
    {
    }

    auto firmwaresList() const -> const std::string & { return m_firmwaresList; }

private:
    std::string m_firmwaresList;
};

// ---------------- PlayersUpdated implementation ----------------

class PlayersUpdatedEvent : public EventBase
{
public:
    explicit PlayersUpdatedEvent(std::vector<PlayerProfile> players)
        : EventBase(EventType::PlayersUpdated, EventPriority::Normal)
        , m_players {std::move(players)}
    {
    }

    auto playersCount() const -> int { return static_cast<int>(m_players.size()); }

    auto playerByNumber(sb_player_t player) const -> const PlayerProfile *
    {
        if (player == 0 || player > m_players.size()) {
            ERR("Player number out of range: {}, players count: {}", player, playersCount());
            return nullptr;
        }
        return &m_players[player - 1];
    }

private:
    std::vector<PlayerProfile> m_players;
};

// ---------------- PlayerPictureReady implementation ----------------

class PlayerPictureReadyEvent : public EventBase
{
public:
    explicit PlayerPictureReadyEvent(sb_player_t player, std::vector<uint8_t> picture)
        : EventBase(EventType::PlayerPictureReady, EventPriority::Normal)
        , m_player {player}
        , m_picture {std::move(picture)}
    {
    }

    auto player() const -> sb_player_t { return m_player; }
    auto pictureData() const -> const uint8_t * { return m_picture.data(); }
    auto pictureSize() const -> size_t { return m_picture.size(); }

private:
    sb_player_t m_player;
    std::vector<uint8_t> m_picture;
};

// ---------------- DiagnosticsUploadRequested implementation ----------------

class DiagnosticsUploadRequestedEvent : public EventBase
{
public:
    explicit DiagnosticsUploadRequestedEvent(bool includeRecordings)
        : EventBase(EventType::DiagnosticsUploadRequested, EventPriority::Normal)
        , m_includeRecordings {includeRecordings}
    {
    }

    auto includeRecordings() const -> bool { return m_includeRecordings; }

private:
    bool m_includeRecordings;
};

// ---------------- DiagnosticsUploaded implementation ----------------

class DiagnosticsUploadedEvent : public EventBase
{
public:
    explicit DiagnosticsUploadedEvent(bool success)
        : EventBase(EventType::DiagnosticsUploaded, EventPriority::Normal)
        , m_success {success}
    {
    }

    auto success() const -> bool { return m_success; }

private:
    bool m_success;
};

} // namespace detail
} // namespace scorbit
