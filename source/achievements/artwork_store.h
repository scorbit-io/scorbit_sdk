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
 * Downloads run one at a time, so they share one connection to the CDN. An artwork that fails is
 * not retried until the next refresh.
 *
 * Threading: every method runs on the owner's thread; download replies are brought back to it
 * through the poster given at construction.
 */
class ArtworkStore
{
public:
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
     * Brings the artwork held in line with @p definitions: downloads each whose version changed or
     * that isn't held yet, and drops those no longer delivered.
     */
    void refresh(std::shared_ptr<const DefinitionSet> definitions);

    /** The artwork held for achievement @p key, without waiting for any download. */
    std::optional<std::vector<uint8_t>> cached(const std::string &key) const;

    /**
     * Calls @p ready with the artwork of @p key once no download of it is outstanding: at once
     * when none is. Calls for one key are answered in the order they were made.
     */
    void whenReady(const std::string &key, Ready ready);

private:
    /** The artwork URL of @p definition and the version it is held at. */
    std::pair<std::string, std::string> source(const Definition &definition) const;
    bool needsDownload(const Definition &definition) const;
    void downloadNext();
    void onDownloaded(const std::string &key, const std::string &version, Error error,
                      std::vector<uint8_t> data);
    void release(const std::string &key);

    NetBase &m_net;
    Poster m_poster;
    ArtworkKind m_kind;

    std::shared_ptr<const DefinitionSet> m_definitions = std::make_shared<DefinitionSet>();

    struct Artwork {
        std::string version;
        std::vector<uint8_t> data;
    };

    /** The artwork held, by achievement key. */
    std::map<std::string, Artwork> m_artwork;

    /**
     * Keys whose artwork this refresh still has to download, or is downloading. One that fails
     * leaves it, so it isn't retried in a loop.
     */
    std::set<std::string> m_toDownload;
    bool m_downloading {false};

    /** Callers waiting for an artwork, by achievement key, in the order they asked. */
    std::map<std::string, std::vector<Ready>> m_waiting;

    /** Guards the store against replies arriving after it was destroyed. */
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace achievements
} // namespace detail
} // namespace scorbit
