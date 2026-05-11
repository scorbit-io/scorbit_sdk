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

// ---------------- PricingReceived implementation ----------------

class PricingReceivedEvent : public EventBase
{
public:
    struct Bundle {
        int credits {0};
        std::string price;
        std::string regularPrice;
        std::string salePrice;
    };

    explicit PricingReceivedEvent(const nlohmann::json &pricingJson)
        : EventBase(EventType::PricingReceived, EventPriority::Normal)
    {
        m_freePlay = pricingJson.value("free_play", false);
        m_paymentsEnabled = pricingJson.value("payments_enabled", false);

        if (const auto pricesIt = pricingJson.find("prices");
            pricesIt != pricingJson.end() && pricesIt->is_object()) {
            const auto &prices = *pricesIt;

            if (const auto creditIt = prices.find("credit");
                creditIt != prices.end() && creditIt->is_object()) {
                m_creditPrice = creditIt->value("price", "");
                m_creditRegularPrice = creditIt->value("regular_price", "");
                if (const auto sp = creditIt->find("sale_price");
                    sp != creditIt->end() && sp->is_string()) {
                    m_creditSalePrice = sp->get<std::string>();
                }
            }

            if (const auto bundlesIt = prices.find("bundles");
                bundlesIt != prices.end() && bundlesIt->is_array()) {
                m_bundles.reserve(bundlesIt->size());
                for (const auto &b : *bundlesIt) {
                    if (!b.is_object()) {
                        continue;
                    }
                    Bundle bundle;
                    bundle.credits = b.value("credits", 0);
                    bundle.price = b.value("price", "");
                    bundle.regularPrice = b.value("regular_price", "");
                    if (const auto sp = b.find("sale_price"); sp != b.end() && sp->is_string()) {
                        bundle.salePrice = sp->get<std::string>();
                    }
                    m_bundles.push_back(std::move(bundle));
                }
            }
        }
    }

    auto freePlay() const -> bool { return m_freePlay; }
    auto paymentsEnabled() const -> bool { return m_paymentsEnabled; }

    auto hasPrices() const -> bool { return !m_creditPrice.empty(); }

    auto creditPrice() const -> const std::string & { return m_creditPrice; }
    auto creditRegularPrice() const -> const std::string & { return m_creditRegularPrice; }
    auto creditSalePrice() const -> const std::string & { return m_creditSalePrice; }

    auto bundlesCount() const -> int { return static_cast<int>(m_bundles.size()); }
    auto bundle(int index) const -> const Bundle *
    {
        if (index < 0 || index >= static_cast<int>(m_bundles.size())) {
            return nullptr;
        }
        return &m_bundles[index];
    }

private:
    bool m_freePlay {false};
    bool m_paymentsEnabled {false};
    std::string m_creditPrice;
    std::string m_creditRegularPrice;
    std::string m_creditSalePrice;
    std::vector<Bundle> m_bundles;
};

// ---------------- PairingStatusChanged implementation ----------------

class PairingStatusChangedEvent : public EventBase
{
public:
    explicit PairingStatusChangedEvent(bool isPaired)
        : EventBase(EventType::PairingStatusChanged, EventPriority::High)
        , m_isPaired {isPaired}
    {
    }

    auto isPaired() const -> bool { return m_isPaired; }

private:
    bool m_isPaired;
};

} // namespace detail
} // namespace scorbit
