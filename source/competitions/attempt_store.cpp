/*
 * Scorbit SDK
 *
 * (c) 2026 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
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

#include "attempt_store.h"
#include "../utils/durable_file.h"
#include <logger/logger.h>
#include <fstream>
#include <iterator>
#include <system_error>

namespace scorbit {
namespace detail {
namespace competitions {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

constexpr auto ATTEMPTS_DIR = "attempts";
constexpr auto SEQUENCE_FILE = "sequence";
constexpr auto RECORD_EXTENSION = ".json";

constexpr auto KEY_GOAL = "goal";
constexpr auto KEY_INSTALLED_AT_MS = "installed_at_ms";
constexpr auto KEY_INSTALLED = "installed";
constexpr auto KEY_START = "start";
constexpr auto KEY_STARTED = "started";
constexpr auto KEY_SESSION = "session";
constexpr auto KEY_POSITION = "position";
constexpr auto KEY_STARTED_AT_MS = "started_at_ms";
constexpr auto KEY_BOOT_ID = "boot_id";
constexpr auto KEY_SEQUENCE = "sequence";

const char *toString(Delivery delivery)
{
    switch (delivery) {
    case Delivery::Pending:
        return "pending";
    case Delivery::Saved:
        return "saved";
    case Delivery::Duplicate:
        return "duplicate";
    }
    return "pending";
}

Delivery deliveryFrom(const std::string &text)
{
    if (text == "saved") {
        return Delivery::Saved;
    }
    if (text == "duplicate") {
        return Delivery::Duplicate;
    }
    return Delivery::Pending;
}

std::optional<std::string> readFile(const fs::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

json encode(const AttemptRecord &record)
{
    json document {{KEY_GOAL, record.goal.message},
                   {KEY_INSTALLED_AT_MS, record.installedAtMs},
                   {KEY_INSTALLED, toString(record.installed)},
                   {KEY_START, nullptr},
                   {KEY_STARTED, toString(record.started)}};
    if (const auto &start = record.start) {
        document[KEY_START] = {{KEY_SESSION, start->session},
                               {KEY_POSITION, start->position},
                               {KEY_STARTED_AT_MS, start->startedAtMs},
                               {KEY_BOOT_ID, start->bootId},
                               {KEY_SEQUENCE, start->sequence}};
    }
    return document;
}

std::optional<AttemptRecord> decode(const json &document, std::string &error)
{
    try {
        AttemptRecord record;
        auto goal = parseGoal(document.at(KEY_GOAL), error);
        if (!goal) {
            return std::nullopt;
        }
        record.goal = std::move(*goal);
        record.installedAtMs = document.at(KEY_INSTALLED_AT_MS).get<int64_t>();
        record.installed = deliveryFrom(document.at(KEY_INSTALLED).get<std::string>());
        record.started = deliveryFrom(document.at(KEY_STARTED).get<std::string>());
        if (const auto &start = document.at(KEY_START); !start.is_null()) {
            record.start = AttemptStart {start.at(KEY_SESSION).get<std::string>(),
                                         start.at(KEY_POSITION).get<int>(),
                                         start.at(KEY_STARTED_AT_MS).get<int64_t>(),
                                         start.at(KEY_BOOT_ID).get<std::string>(),
                                         start.at(KEY_SEQUENCE).get<int64_t>()};
        }
        return record;
    } catch (const std::exception &e) {
        error = e.what();
        return std::nullopt;
    }
}

} // namespace

AttemptStore::AttemptStore(std::string dataDir)
    : m_root {fs::path(dataDir) / "competitions"}
{
}

std::vector<AttemptRecord> AttemptStore::load() const
{
    std::vector<AttemptRecord> records;
    std::error_code ec;
    for (fs::directory_iterator it {m_root / ATTEMPTS_DIR, ec}, end; !ec && it != end;
         it.increment(ec)) {
        const auto &path = it->path();
        if (path.extension() != RECORD_EXTENSION) {
            continue; // e.g. a write interrupted before its rename
        }
        std::string error;
        std::optional<AttemptRecord> record;
        if (const auto body = readFile(path)) {
            try {
                record = decode(json::parse(*body), error);
            } catch (const std::exception &e) {
                error = e.what();
            }
        } else {
            error = "can't read it";
        }
        if (!record) {
            ERR("Competitions: skipping attempt file {}: {}", path.string(), error);
            continue;
        }
        records.push_back(std::move(*record));
    }
    return records;
}

bool AttemptStore::save(const AttemptRecord &record) const
{
    // The attempt was checked to be a uuid when the goal was parsed: a safe file name
    const auto path =
            m_root / ATTEMPTS_DIR / (record.goal.identity.attempt + RECORD_EXTENSION);
    if (!replaceDurably(path, encode(record).dump())) {
        ERR("Competitions: can't save attempt {} to {}", record.goal.identity.attempt,
            path.string());
        return false;
    }
    return true;
}

void AttemptStore::remove(const std::string &attempt) const
{
    std::error_code ec;
    fs::remove(m_root / ATTEMPTS_DIR / (attempt + RECORD_EXTENSION), ec);
    if (ec) {
        WRN("Competitions: can't remove attempt {}: {}", attempt, ec.message());
    }
}

std::optional<int64_t> AttemptStore::nextSequence()
{
    const auto path = m_root / SEQUENCE_FILE;
    if (!m_lastSequence) {
        int64_t last = 0;
        if (const auto body = readFile(path)) {
            try {
                last = std::stoll(*body);
            } catch (const std::exception &) {
                // Starting over could reuse an ordinal: refuse rather than guess
                ERR("Competitions: device event sequence {} is unreadable", path.string());
                return std::nullopt;
            }
        }
        m_lastSequence = last;
    }

    const auto next = *m_lastSequence + 1;
    if (!replaceDurably(path, std::to_string(next))) {
        ERR("Competitions: can't save the device event sequence to {}", path.string());
        return std::nullopt;
    }
    m_lastSequence = next;
    return next;
}

} // namespace competitions
} // namespace detail
} // namespace scorbit
