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
#include <string>
#include <functional>


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

// ---------------- CreditsNumberRequested implementations ----------------

class CreditsNumberRequestedEvent : public EventBase
{
public:
    explicit CreditsNumberRequestedEvent()
        : EventBase(EventType::CreditsNumberRequested, EventPriority::High)
    {
    }
};

// ---------------- ConfigReceived implementation ----------------

class ConfigReceivedEvent : public EventBase
{
public:
    explicit ConfigReceivedEvent(const std::string &configJson)
        : EventBase(EventType::ConfigReceived, EventPriority::Normal)
        , m_configJson {configJson}
    {
    }

    auto configJson() const -> const std::string & { return m_configJson; }

private:
    std::string m_configJson;
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


} // namespace detail
} // namespace scorbit

