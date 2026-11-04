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
#include <fmt/format.h>
#include <logger/logger.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <functional>
#include <iterator>
#include <sstream>
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

/** The flat `{code, detail}` of a refused request, or the raw body when it has none. */
std::string describeRefusal(const std::string &body)
{
    try {
        const auto document = nlohmann::json::parse(body);
        if (document.is_object() && document.contains("code")) {
            return fmt::format("{}: {}", document.value("code", std::string {}),
                               document.value("detail", std::string {}));
        }
    } catch (const std::exception &) {
    }
    return body;
}

/**
 * Logs @p body pretty-printed, one line per message: the logger cuts messages longer than its
 * limit (512 by default), and a whole reply is easily longer.
 */
void logJsonLines(const std::string &title, const std::string &body)
{
    std::string pretty;
    try {
        pretty = nlohmann::json::parse(body).dump(2);
    } catch (const std::exception &) {
        pretty = body;
    }

    INF("{}:", title);
    std::istringstream lines(pretty);
    for (std::string line; std::getline(lines, line);) {
        INF("  {}", line);
    }
}

template<typename T>
std::optional<T> parseJson(const std::string &body,
                           std::optional<T> (*parse)(const nlohmann::json &))
{
    try {
        return parse(nlohmann::json::parse(body));
    } catch (const std::exception &e) {
        ERR("Achievements: invalid JSON reply: {}, body: {}", e.what(), body);
        return std::nullopt;
    }
}

/** @p key unlocked for @p player, described from @p definitions, and the URL of its icon. */
std::pair<AchievementUnlock, std::string> describeUnlock(const DefinitionSet &definitions,
                                                         std::string key, PlayerNumber player)
{
    std::pair<AchievementUnlock, std::string> described;
    auto &[unlock, iconUrl] = described;
    unlock.key = std::move(key);
    unlock.player = player;
    if (const auto *definition = definitions.find(unlock.key)) {
        unlock.name = definition->name;
        unlock.description = definition->description;
        iconUrl = definition->iconUrl;
    }
    return described;
}

/** Definitions are revalidated at least this often (§10.3). */
constexpr int64_t REVALIDATE_EVERY_MS = 24 * 3'600'000;

/**
 * How long after midnight a machine may wait to revalidate, so that a time zone's machines don't
 * all ask at once.
 */
constexpr int64_t MIDNIGHT_SPREAD_MS = 10 * 60'000;

} // namespace

int64_t systemWallClock()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

AchievementService::AchievementService(NetBase &net, AchievementStorage storage, Poster poster,
                                       UnlockSink sink, ArtworkKind artworkKind,
                                       Presentation presentation, WallClock wallClock)
    : m_net {net}
    , m_storage {std::move(storage)}
    , m_poster {std::move(poster)}
    , m_artwork {net, m_poster, artworkKind}
    , m_presenter {m_artwork, std::move(sink), presentation}
    , m_wallClock {std::move(wallClock)}
{
    if (const auto cached = m_storage.loadDefinitions();
        cached && !applyDefinitions(*cached, false)) {
        // E.g. written by a build that knew another format: revalidating it could only answer 304
        WRN("Achievements: discarding cached definitions that no longer parse");
        m_storage.discardDefinitions();
    }
    updateView();
}

AchievementService::~AchievementService()
{
    *m_alive = false;
}

// ---- Definitions -------------------------------------------------------------------------------

void AchievementService::refreshDefinitions()
{
    if (m_fetchingDefinitions) {
        return;
    }
    m_fetchingDefinitions = true;

    auto onReply = [this, alive = std::weak_ptr(m_alive)](ApiReply reply) {
        m_poster([this, alive, reply = std::move(reply)]() mutable {
            if (!alive.expired()) {
                onDefinitions(std::move(reply));
            }
        });
    };
    m_net.fetchAchievementDefinitions(m_feedEtag, std::move(onReply));
}

void AchievementService::onDefinitions(ApiReply reply)
{
    m_fetchingDefinitions = false;
    if (reply.error != Error::Success) {
        WRN("Achievements: can't fetch definitions, status {}", reply.httpStatus);
        return;
    }

    // 304: the definitions in force are still current
    if (reply.httpStatus != 304
        && !applyDefinitions({std::move(reply.body), std::move(reply.etag)}, true)) {
        return;
    }
    scheduleRevalidation();
    refreshArtwork();
}

void AchievementService::scheduleRevalidation()
{
    m_validatedMs = m_wallClock();

    // Spread after midnight, but never past the daily bound
    std::uniform_int_distribution<int64_t> spread {0, MIDNIGHT_SPREAD_MS};
    const auto at =
            std::min(revalidationDueMs() + spread(m_random), *m_validatedMs + REVALIDATE_EVERY_MS);
    const auto delay = std::chrono::milliseconds {at - *m_validatedMs};
    INF("Achievements: definitions are current, next revalidation in {} s",
        std::chrono::duration_cast<std::chrono::seconds>(delay).count());

    m_net.scheduleDefinitionsRevalidation(delay, [this, alive = std::weak_ptr(m_alive)] {
        m_poster([this, alive] {
            if (!alive.expired()) {
                refreshDefinitions();
            }
        });
    });
}

int64_t AchievementService::revalidationDueMs() const
{
    return std::min(*m_validatedMs + REVALIDATE_EVERY_MS,
                    nextLocalMidnight(m_feed->calendar.offsets, *m_validatedMs));
}

bool AchievementService::revalidationDue() const
{
    return m_validatedMs && m_wallClock() >= revalidationDueMs();
}

bool AchievementService::applyDefinitions(const CachedDefinitions &response, bool persist)
{
    auto feed = parseJson<DefinitionFeed>(response.body, &parseDefinitionFeed);
    if (!feed) {
        return false;
    }

    INF("Achievements: {} definitions", feed->definitions.size());
    m_feed = std::make_shared<const DefinitionFeed>(std::move(*feed));
    m_feedEtag = response.etag;
    if (persist) {
        m_storage.saveDefinitions(response);
    }

    // Not applied to a game in progress: it keeps the definitions it started with (§10.3)
    updateView();
    return true;
}

// ---- Artwork ----------------------------------------------------------------------------------

void AchievementService::refreshArtwork()
{
    m_artwork.refresh({m_feed, &m_feed->definitions});
}

std::optional<std::vector<uint8_t>> AchievementService::artwork(const std::string &key) const
{
    return m_artwork.cached(key);
}

// ---- Session -----------------------------------------------------------------------------------

void AchievementService::onSessionStarted(int sessionId, const TimelineRow &firstRow)
{
    if (m_current && !m_current->finished) {
        onSessionFinished(firstRow.timeMs);
    }

    // The revalidation was missed, e.g. offline at midnight: it can only serve the next game now
    if (revalidationDue()) {
        refreshDefinitions();
    }

    m_sessions.push_back(std::make_unique<Session>());
    m_current = m_sessions.back().get();
    m_current->id = ++m_lastSessionId;
    m_current->gameSessionId = sessionId;
    m_current->feed = m_feed;
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

    prefetchIcons(*session);
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
                session->sentBySlot[{player, it->second.userId}] = it->second.tracker->sentValues();
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
    if (reply.error == Error::Success) {
        logJsonLines(fmt::format("Achievements: state of {} as received", userId), reply.body);
    }

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

    const auto [first, isFirst] = session->firstBaselines.try_emplace({player, userId}, *baselines);
    if (!isFirst) {
        const auto sent = session->sentBySlot.find({player, userId});
        *baselines = reclaimBaselines(first->second, std::move(*baselines),
                                      sent == session->sentBySlot.end()
                                              ? std::map<std::string, RuleProgressMap> {}
                                              : sent->second,
                                      session->definitions());
    }
    claim.tracker = std::make_unique<PlayerTracker>(userId, std::move(*baselines));
    evaluate(*session, player, claim);
    prefetchIcons(*session);
    updateView();
}

// ---- Evaluation and reporting ------------------------------------------------------------------

void AchievementService::evaluate(Session &session, PlayerNumber player, Claim &claim)
{
    claim.tracker->reevaluate(session.seat(player), session.definitions());
    publish(session, player, claim);
}

void AchievementService::publish(Session &session, PlayerNumber player, Claim &claim)
{
    // in_session unlocks are claimed the moment they match (§7.7)
    session.outbox->add(claim.userId, claim.tracker->takeClaims());
    pump(session.outbox);

    // What the machine decided alone may still be refused, so by default game code hears of it
    // once Scorbit grants it; the presenter knows when (§7.2)
    for (auto &update : claim.tracker->takeUpdates()) {
        if (update.status != AchievementStatus::UnlockedLocally
            && update.status != AchievementStatus::Confirmed) {
            continue;
        }
        auto [unlock, iconUrl] = describeUnlock(session.definitions(), update.key, player);
        GrantId grant {session.id, claim.userId, std::move(update.key)};
        if (update.status == AchievementStatus::UnlockedLocally) {
            m_presenter.decided(grant, std::move(unlock));
        } else {
            m_presenter.granted(grant, std::move(unlock), iconUrl);
        }
    }
}

void AchievementService::prefetchIcons(const Session &session)
{
    if (m_artwork.kind() != ArtworkKind::Image) {
        return;
    }

    // The closest first, across every claimed player
    std::vector<std::pair<std::string, UnlockProximity>> near;
    for (const auto &[player, claim] : session.claims) {
        if (claim.tracker) {
            std::ranges::move(claim.tracker->nearUnlocks(session.definitions()),
                              std::back_inserter(near));
        }
    }
    std::ranges::stable_sort(near, std::greater {},
                             [](const auto &entry) { return entry.second.score; });

    std::vector<std::pair<std::string, std::string>> icons;
    for (auto &[key, proximity] : near) {
        if (const auto *definition = session.definitions().find(key)) {
            icons.emplace_back(std::move(key), definition->iconUrl);
        }
    }
    m_artwork.prefetch(std::move(icons));
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
        syncReportsInFlight();
        return;
    }

    INF("Achievements: reporting {} item(s) for {}, sequence {}", request->items.size(),
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
    syncReportsInFlight();
}

void AchievementService::syncReportsInFlight()
{
    // Artwork waits while a report is out, so it never delays one (§3.9)
    m_artwork.setReportsInFlight(std::ranges::any_of(
            m_sessions, [](const auto &session) { return session->outbox->hasInFlight(); }));
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
            syncReportsInFlight();
            m_net.scheduleAchievementRetry(delay, [this, alive = std::weak_ptr(m_alive)] {
                m_poster([this, alive] {
                    if (!alive.expired()) {
                        onRetryTimer();
                    }
                });
            });
        } else {
            ERR("Achievements: report refused (status {}): {}", reply.httpStatus,
                describeRefusal(reply.body));
            pump(outbox);
        }
        return;
    }

    outbox->delivered();

    // The player's slot was unclaimed this session: the API acknowledged and discarded the whole
    // report (§7.3). It was delivered, so it is not retried; nothing in it was acknowledged, so
    // its records may ride a later report; and nothing was refused, so nothing is retracted.
    if (outcomes->empty() && !request.items.empty()) {
        INF("Achievements: report for {} acknowledged and discarded", request.userId);
    }

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
                claim.tracker->applyOutcome(*sent, outcome, session->seat(player),
                                            session->definitions());
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
        const bool drained =
                session.get() != m_current && session->finished && session->outbox->empty();
        if (drained) {
            m_presenter.forgetSession(session->id);
        }
        return drained;
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
    // While a game runs, the definitions it is evaluated against; a newer set shows once it ends
    // The definitions in force for game code: a game in progress keeps those it started with
    const auto &feed = m_current ? m_current->feed : m_feed;
    view->definitions = std::shared_ptr<const DefinitionSet> {feed, &feed->definitions};
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
