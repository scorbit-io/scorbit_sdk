/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
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

#include "achievement_service.h"
#include "json_codec.h"
#include <logger/logger.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <utility>

namespace scorbit {
namespace detail {
namespace achievements {

namespace {

/** A transport failure or a server-side one is worth retrying; a refused request is not. */
bool isRetryable(const ApiReply &reply)
{
    return reply.httpStatus == 0 || reply.httpStatus == 408 || reply.httpStatus == 429
        || reply.httpStatus >= 500;
}

template<typename T>
std::optional<T> parseJson(const std::string &body,
                           std::optional<T> (*parse)(const nlohmann::json &))
{
    try {
        return parse(nlohmann::json::parse(body));
    } catch (const std::exception &e) {
        ERR("Achievements: invalid JSON reply: {}", e.what());
        return std::nullopt;
    }
}

} // namespace

AchievementService::AchievementService(NetBase &net, AchievementStorage storage, Poster poster,
                                       NotificationSink sink)
    : m_net {net}
    , m_storage {std::move(storage)}
    , m_poster {std::move(poster)}
    , m_sink {std::move(sink)}
{
    if (const auto cached = m_storage.loadDefinitions()) {
        applyDefinitions(cached->body, cached->etag, false);
    }
}

AchievementService::~AchievementService()
{
    *m_alive = false;
}

// ---- Definitions -------------------------------------------------------------------------------

void AchievementService::refreshDefinitions()
{
    m_net.fetchAchievementDefinitions(
            m_etag, [this, alive = std::weak_ptr(m_alive)](ApiReply reply) {
                m_poster([this, alive, reply = std::move(reply)]() mutable {
                    if (alive.expired()) {
                        return;
                    }
                    if (reply.error != Error::Success) {
                        WRN("Achievements: can't fetch definitions, status {}", reply.httpStatus);
                        return;
                    }
                    if (reply.httpStatus == 304) {
                        DBG("Achievements: definitions not modified");
                        refreshFrames();
                        return;
                    }
                    applyDefinitions(reply.body, reply.etag, true);
                    refreshFrames();
                });
            });
}

void AchievementService::applyDefinitions(const std::string &body, const std::string &etag,
                                          bool persist)
{
    auto definitions = parseJson<DefinitionSet>(body, &parseDefinitionsResponse);
    if (!definitions) {
        return;
    }

    INF("Achievements: {} definitions for '{}'", definitions->size(), definitions->game());
    m_definitions = std::make_shared<const DefinitionSet>(std::move(*definitions));
    m_etag = etag;
    if (persist) {
        m_storage.saveDefinitions(body, etag);
    }

    // A running session picks the new definitions up from the next evaluation
    if (auto *session = current()) {
        for (auto &[player, claim] : session->claims) {
            if (claim.tracker) {
                evaluate(*session, player, claim);
            }
        }
    }
    updateView();
}

// ---- Frames ------------------------------------------------------------------------------------

void AchievementService::refreshFrames()
{
    const auto wanted = m_definitions->framesVersion();
    const auto &game = m_definitions->game();
    if (wanted <= 0 || game.empty() || m_downloadingFrames || m_storage.framesVersion() == wanted) {
        return;
    }

    m_downloadingFrames = true;
    const auto archive = m_storage.framesDownloadPath();
    m_net.downloadAchievementFrames(
            game, archive, [this, alive = std::weak_ptr(m_alive), archive, wanted](ApiReply reply) {
                m_poster([this, alive, archive, wanted, reply = std::move(reply)] {
                    if (alive.expired()) {
                        return;
                    }
                    m_downloadingFrames = false;
                    if (reply.error != Error::Success) {
                        WRN("Achievements: can't download frame bundle v{}", wanted);
                        return;
                    }
                    if (m_storage.installFrames(archive, wanted)) {
                        INF("Achievements: frame bundle v{} installed", wanted);
                    }
                });
            });
}

std::optional<std::vector<uint8_t>> AchievementService::frame(const std::string &key) const
{
    return m_storage.frame(key);
}

// ---- Session -----------------------------------------------------------------------------------

void AchievementService::onSessionStarted(int sessionId, const TimelineRow &firstRow)
{
    if (m_current && !m_current->finished) {
        onSessionFinished(firstRow.timeMs);
    }

    m_sessions.push_back(std::make_unique<Session>());
    m_current = m_sessions.back().get();
    m_current->id = ++m_lastSessionId;
    m_current->gameSessionId = sessionId;
    m_current->facts.apply(firstRow);
    updateView();
}

void AchievementService::onSessionCreated(int sessionId, const std::string &sessionUuid)
{
    const auto it = std::ranges::find(m_sessions, sessionId,
                                      [](const auto &session) { return session->gameSessionId; });
    if (it == m_sessions.end()) {
        return;
    }

    auto &session = **it;
    session.outbox->setSessionUuid(sessionUuid);
    if (&session == current()) {
        reconcileClaims();
    }
    pump(session.outbox);
}

void AchievementService::onRow(const TimelineRow &row)
{
    auto *session = current();
    if (!session || session->finished) {
        return;
    }

    session->facts.apply(row);
    for (auto &[player, claim] : session->claims) {
        if (claim.tracker) {
            evaluate(*session, player, claim);
        }
    }

    if (const auto ballEndedFor = session->facts.lastRowEndedBallOf()) {
        reportBallEnd(*session, *ballEndedFor);
    }

    updateView();
}

void AchievementService::onSessionFinished(int64_t timeMs)
{
    auto *session = current();
    if (!session || session->finished) {
        return;
    }

    session->facts.finish(timeMs);
    for (auto &[player, claim] : session->claims) {
        if (claim.tracker) {
            evaluate(*session, player, claim);
            session->outbox->add(claim.userId, claim.tracker->collectReport());
            publish(*session, player, claim);
        }
    }
    session->finished = true;
    m_current = nullptr;

    pump(session->outbox);
    dropDrainedSessions();
    updateView();
}

// ---- Claims ------------------------------------------------------------------------------------

void AchievementService::onPlayersChanged()
{
    // Before the server created the session the profiles may still be the previous game's
    if (auto *session = current(); session && !session->outbox->sessionUuid().empty()) {
        reconcileClaims();
    }
}

void AchievementService::reconcileClaims()
{
    auto *session = current();
    if (!session) {
        return;
    }

    for (PlayerNumber player = 1; player <= MAX_TIMELINE_PLAYERS; ++player) {
        const auto profile = m_net.playersManager().profile(player);
        const std::string userId = profile && profile->hasInfo() ? profile->id : std::string {};

        auto it = session->claims.find(player);
        if (it != session->claims.end() && it->second.userId == userId) {
            continue;
        }

        if (it != session->claims.end()) {
            // The slot changed hands: what the previous player earned is still reported
            if (it->second.tracker) {
                session->outbox->add(it->second.userId, it->second.tracker->collectReport());
            }
            session->claims.erase(it);
        }

        if (!userId.empty()) {
            INF("Achievements: player {} claimed by {}", player, userId);
            session->claims[player].userId = userId;
            fetchBaseline(player, userId);
        }
    }

    pump(session->outbox);
    updateView();
}

void AchievementService::fetchBaseline(PlayerNumber player, const std::string &userId)
{
    auto *session = current();
    if (!session) {
        return;
    }
    auto &claim = session->claims.at(player);
    if (claim.fetching || claim.tracker) {
        return;
    }
    claim.fetching = true;

    m_net.fetchAchievementProgress(userId, [this, alive = std::weak_ptr(m_alive),
                                            sessionId = session->id, player,
                                            userId](ApiReply reply) {
        m_poster([this, alive, sessionId, player, userId, reply = std::move(reply)]() mutable {
            if (!alive.expired()) {
                onBaseline(sessionId, player, userId, std::move(reply));
            }
        });
    });
}

void AchievementService::onBaseline(uint64_t sessionId, PlayerNumber player,
                                    const std::string &userId, ApiReply reply)
{
    // The session may have ended and the slot may have changed hands in the meantime
    auto *session = current();
    if (!session || session->id != sessionId) {
        return;
    }
    const auto it = session->claims.find(player);
    if (it == session->claims.end() || it->second.userId != userId) {
        return;
    }
    auto &claim = it->second;
    claim.fetching = false;

    std::optional<Baselines> baselines;
    if (reply.error == Error::Success) {
        baselines = parseJson<Baselines>(reply.body, &parseProgressResponse);
    }
    if (!baselines) {
        // Without the baseline a lifetime value can't be computed; retried at the next ball end
        WRN("Achievements: can't fetch state of {}, status {}", userId, reply.httpStatus);
        return;
    }

    claim.tracker = std::make_unique<PlayerTracker>(userId, std::move(*baselines));
    evaluate(*session, player, claim);
    updateView();
}

// ---- Evaluation and reporting ------------------------------------------------------------------

void AchievementService::evaluate(Session &session, PlayerNumber player, Claim &claim)
{
    claim.tracker->reevaluate(session.facts.player(player), *m_definitions);
    publish(session, player, claim);
}

void AchievementService::publish(Session &session, PlayerNumber player, Claim &claim)
{
    // in_session unlocks are claimed the moment they match (§7.7)
    session.outbox->add(claim.userId, claim.tracker->takeClaims());
    pump(session.outbox);

    for (auto &update : claim.tracker->takeUpdates()) {
        if (m_sink) {
            m_sink(AchievementNotification {std::move(update.key), player, claim.userId,
                                            update.status, std::move(update.rules)});
        }
    }
}

void AchievementService::reportBallEnd(Session &session, PlayerNumber player)
{
    if (const auto it = session.claims.find(player); it != session.claims.end()) {
        if (it->second.tracker) {
            session.outbox->add(it->second.userId, it->second.tracker->collectReport());
            publish(session, player, it->second);
        }
    }

    // A claim whose state fetch failed gets another chance between balls
    for (auto &[number, claim] : session.claims) {
        if (!claim.tracker && !claim.fetching) {
            fetchBaseline(number, claim.userId);
        }
    }
}

void AchievementService::flushReports()
{
    if (auto *session = current()) {
        for (auto &[player, claim] : session->claims) {
            if (claim.tracker) {
                session->outbox->add(claim.userId, claim.tracker->collectReport());
                publish(*session, player, claim);
            }
        }
    }
    for (const auto &session : m_sessions) {
        pump(session->outbox);
    }
}

void AchievementService::pump(const std::shared_ptr<ReportOutbox> &outbox)
{
    auto request = outbox->next();
    if (!request) {
        return;
    }

    DBG("Achievements: reporting {} item(s) for {}, sequence {}", request->items.size(),
        request->userId, request->sequence);

    m_net.postAchievementReport(
            encodeReportRequest(*request),
            [this, alive = std::weak_ptr(m_alive), weakOutbox = std::weak_ptr(outbox),
             request = *request](ApiReply reply) {
                m_poster([this, alive, weakOutbox, request, reply = std::move(reply)]() mutable {
                    if (alive.expired()) {
                        return;
                    }
                    if (auto outbox = weakOutbox.lock()) {
                        onReportReply(outbox, request, std::move(reply));
                    }
                });
            });
}

void AchievementService::onReportReply(const std::shared_ptr<ReportOutbox> &outbox,
                                       const ReportRequest &request, ApiReply reply)
{
    std::optional<std::vector<ReportOutcome>> outcomes;
    if (reply.error == Error::Success) {
        outcomes = parseJson<std::vector<ReportOutcome>>(reply.body, &parseReportResponse);
    }

    if (!outcomes) {
        const bool retryable = reply.error != Error::Success && isRetryable(reply);
        const auto delay = outbox->failed(retryable);
        if (retryable) {
            WRN("Achievements: report failed (status {}), retrying in {} ms", reply.httpStatus,
                delay.count());
            m_net.scheduleAchievementRetry(delay, [this, alive = std::weak_ptr(m_alive)] {
                m_poster([this, alive] {
                    if (!alive.expired()) {
                        onRetryTimer();
                    }
                });
            });
        } else {
            ERR("Achievements: report refused (status {}): {}", reply.httpStatus, reply.body);
            pump(outbox);
        }
        return;
    }

    outbox->delivered();

    auto *session = sessionOf(outbox);
    for (const auto &outcome : *outcomes) {
        if (outcome.status == ReportStatus::Rejected) {
            WRN("Achievements: '{}' rejected: {} {}", outcome.key, outcome.code, outcome.detail);
        }
        if (!session) {
            continue;
        }
        const auto sent = std::ranges::find(request.items, outcome.key, &ReportItem::key);
        if (sent == request.items.end()) {
            continue;
        }
        for (auto &[player, claim] : session->claims) {
            if (claim.userId == request.userId && claim.tracker) {
                claim.tracker->applyOutcome(*sent, outcome, session->facts.player(player),
                                            *m_definitions);
                publish(*session, player, claim);
            }
        }
    }

    pump(outbox);
    dropDrainedSessions();
    updateView();
}

void AchievementService::onRetryTimer()
{
    for (const auto &session : m_sessions) {
        session->outbox->retryReady();
        pump(session->outbox);
    }
}

void AchievementService::dropDrainedSessions()
{
    std::erase_if(m_sessions, [this](const std::unique_ptr<Session> &session) {
        return session.get() != m_current && session->finished && session->outbox->empty();
    });
}

AchievementService::Session *
AchievementService::sessionOf(const std::shared_ptr<ReportOutbox> &outbox)
{
    const auto it = std::ranges::find_if(
            m_sessions, [&](const auto &session) { return session->outbox == outbox; });
    return it == m_sessions.end() ? nullptr : it->get();
}

// ---- Read side ---------------------------------------------------------------------------------

void AchievementService::updateView()
{
    auto view = std::make_shared<AchievementsView>();
    view->definitions = m_definitions;
    if (const auto *session = m_current) {
        for (const auto &[player, claim] : session->claims) {
            auto &entry = view->players[player];
            entry.userId = claim.userId;
            if (claim.tracker) {
                entry.records = claim.tracker->records();
            }
        }
    }

    std::scoped_lock lock(m_viewMutex);
    m_view = std::move(view);
}

std::shared_ptr<const AchievementsView> AchievementService::view() const
{
    std::scoped_lock lock(m_viewMutex);
    return m_view;
}

} // namespace achievements
} // namespace detail
} // namespace scorbit
