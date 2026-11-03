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

#pragma once

#include "definition.h"
#include "../net_base.h"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace scorbit {
namespace detail {
namespace achievements {

/** Which artwork the host chose to have handed over with an unlock (§12.1). */
enum class ArtworkKind {
    None,
    Frame, ///< The DMD frame (`frame`, versioned by `frame_version`)
    Image, ///< The icon (`icon_url`, which has no version: a new URL is a new image)
};

/**
 * The artwork of the kind the host chose, held in memory to hand over with an unlock (§3.9).
 *
 * - **DMD frames** are small, so all of them are fetched ahead, and again when a `frame_version`
 *   changes. One that fails is not retried until the next refresh.
 * - **Icons** are large, so they are not preloaded: an icon is fetched when an unlock asks for it,
 *   or ahead for an achievement about to be unlocked, and the last @ref ICON_CACHE_SIZE are kept.
 *
 * Downloads run one at a time, so they share one connection to the CDN, and none starts while a
 * report is in flight, so artwork never delays a report.
 *
 * Threading: every method runs on the owner's thread; download replies are brought back to it
 * through the poster given at construction.
 */
class ArtworkStore
{
public:
    /** How many icons are kept in memory, the least recently used dropped first. */
    static constexpr size_t ICON_CACHE_SIZE = 8;

    /** How many icons fetched ahead may wait, unused, for their unlock. */
    static constexpr size_t PREFETCH_LIMIT = ICON_CACHE_SIZE / 2;

    /** Runs a function on the owner's thread. */
    using Poster = std::function<void(std::function<void()>)>;

    /** Receives an achievement's artwork once it is settled; empty when there is none. */
    using Ready = std::function<void(std::vector<uint8_t>)>;

    ArtworkStore(NetBase &net, Poster poster, ArtworkKind kind);
    ~ArtworkStore();

    ArtworkStore(const ArtworkStore &) = delete;
    ArtworkStore &operator=(const ArtworkStore &) = delete;

    ArtworkKind kind() const { return m_kind; }

    /**
     * Brings the artwork held in line with @p definitions: downloads each frame whose version
     * changed or that isn't held yet, and drops the artwork of achievements no longer delivered,
     * or whose icon changed.
     */
    void refresh(std::shared_ptr<const DefinitionSet> definitions);

    /** The artwork held for achievement @p key, without waiting for any download. */
    std::optional<std::vector<uint8_t>> cached(const std::string &key) const;

    /** The artwork held for @p key, now handed over with an unlock; empty when none is held. */
    std::vector<uint8_t> handOver(const std::string &key);

    /**
     * Calls @p ready with the artwork of @p key once no download of it is outstanding: at once
     * when none is. An icon not held yet is downloaded from @p iconUrl first. Calls for one key
     * are answered in the order they were made.
     */
    void whenReady(const std::string &key, const std::string &iconUrl, Ready ready);

    /**
     * Icons to fetch ahead, most wanted first, as achievement key → URL: those of achievements a
     * player is about to unlock. Replaces the previous list. They are downloaded after everything
     * else, and no more than @ref PREFETCH_LIMIT wait unused at a time.
     */
    void prefetch(std::vector<std::pair<std::string, std::string>> icons);

    /** Whether a report is in flight: no download starts until none is. */
    void setReportsInFlight(bool inFlight);

private:
    struct Download {
        std::string key;
        std::string url;
        /** The version the artwork is held at once downloaded: `frame_version`, or the URL. */
        std::string version;
        bool prefetch {false};
    };

    /** The frame URL of @p definition and its version. */
    std::pair<std::string, std::string> frameSource(const Definition &definition) const;
    bool needsFrame(const Definition &definition) const;
    /** Whether a download of @p key's artwork is queued or in flight. */
    bool pending(const std::string &key) const;
    std::optional<Download> nextDownload() const;
    void downloadNext();
    void onDownloaded(const Download &download, Error error, std::vector<uint8_t> data);
    void keepIcon(const std::string &key, const std::string &url, std::vector<uint8_t> data,
                  bool prefetched);
    /** Whether @p key's icon is held at @p url. */
    bool holdsIcon(const std::string &key, const std::string &url) const;
    void release(const std::string &key);

    NetBase &m_net;
    Poster m_poster;
    ArtworkKind m_kind;

    std::shared_ptr<const DefinitionSet> m_definitions = std::make_shared<DefinitionSet>();

    struct Artwork {
        std::string version;
        std::vector<uint8_t> data;
        /** For an icon, when it was last used, to drop the least recently used first. */
        uint64_t lastUse {0};
        /** For an icon, fetched ahead and not handed over yet. */
        bool prefetched {false};
    };

    /** The artwork held, by achievement key. */
    std::map<std::string, Artwork> m_artwork;
    uint64_t m_useClock {0};

    /**
     * Keys whose frame this refresh still has to download, or is downloading. One that fails
     * leaves it, so it isn't retried in a loop.
     */
    std::set<std::string> m_framesToDownload;

    /** Icons asked for and not held yet, in the order asked: key → URL. */
    std::vector<std::pair<std::string, std::string>> m_iconsToDownload;

    /** Icons to fetch ahead, most wanted first: key → URL. */
    std::vector<std::pair<std::string, std::string>> m_iconsToPrefetch;

    std::optional<Download> m_downloading;
    bool m_reportsInFlight {false};

    /** Callers waiting for an artwork, by achievement key, in the order they asked. */
    std::map<std::string, std::vector<Ready>> m_waiting;

    /** Guards the store against replies arriving after it was destroyed. */
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace achievements
} // namespace detail
} // namespace scorbit
