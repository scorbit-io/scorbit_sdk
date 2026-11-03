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

#include "artwork_store.h"
#include <logger/logger.h>
#include <algorithm>

namespace scorbit {
namespace detail {
namespace achievements {

ArtworkStore::ArtworkStore(NetBase &net, Poster poster, ArtworkKind kind)
    : m_net {net}
    , m_poster {std::move(poster)}
    , m_kind {kind}
{
}

ArtworkStore::~ArtworkStore()
{
    *m_alive = false;
}

std::pair<std::string, std::string> ArtworkStore::frameSource(const Definition &definition) const
{
    if (m_kind != ArtworkKind::Frame) {
        return {};
    }
    return {definition.frameUrl, std::to_string(definition.frameVersion)};
}

bool ArtworkStore::needsFrame(const Definition &definition) const
{
    const auto [url, version] = frameSource(definition);
    if (url.empty()) {
        return false;
    }
    const auto it = m_artwork.find(definition.key);
    return it == m_artwork.end() || it->second.version != version;
}

void ArtworkStore::refresh(std::shared_ptr<const DefinitionSet> definitions)
{
    m_definitions = std::move(definitions);

    // Artwork no longer delivered, or no longer what the definition names, is dropped
    std::erase_if(m_artwork, [this](const auto &entry) {
        const auto *definition = m_definitions->find(entry.first);
        if (!definition) {
            return true;
        }
        return m_kind == ArtworkKind::Image ? definition->iconUrl != entry.second.version
                                            : frameSource(*definition).first.empty();
    });

    // Frames are fetched ahead; icons only when an unlock asks for one
    m_framesToDownload.clear();
    for (const auto &definition : m_definitions->all()) {
        if (needsFrame(definition)) {
            m_framesToDownload.insert(definition.key);
        }
    }

    // A caller waiting on a download this refresh no longer wants is answered now
    for (auto it = m_waiting.begin(); it != m_waiting.end();) {
        const auto key = (it++)->first;
        release(key);
    }
    downloadNext();
}

std::optional<std::vector<uint8_t>> ArtworkStore::cached(const std::string &key) const
{
    const auto it = m_artwork.find(key);
    if (it == m_artwork.end()) {
        return std::nullopt;
    }
    return it->second.data;
}

void ArtworkStore::whenReady(const std::string &key, const std::string &iconUrl, Ready ready)
{
    m_waiting[key].push_back(std::move(ready));

    if (m_kind == ArtworkKind::Image && !iconUrl.empty() && !pending(key)) {
        const auto it = m_artwork.find(key);
        if (it == m_artwork.end() || it->second.version != iconUrl) {
            m_iconsToDownload.emplace_back(key, iconUrl);
            downloadNext();
        }
    }
    release(key);
}

void ArtworkStore::setReportsInFlight(bool inFlight)
{
    m_reportsInFlight = inFlight;
    downloadNext();
}

bool ArtworkStore::pending(const std::string &key) const
{
    return m_framesToDownload.contains(key) || (m_downloading && m_downloading->key == key)
        || std::ranges::find(m_iconsToDownload, key, &std::pair<std::string, std::string>::first)
                   != m_iconsToDownload.end();
}

std::optional<ArtworkStore::Download> ArtworkStore::nextDownload() const
{
    if (!m_iconsToDownload.empty()) {
        const auto &[key, url] = m_iconsToDownload.front();
        return Download {key, url, url};
    }

    // Frames in definition order
    for (const auto &definition : m_definitions->all()) {
        if (m_framesToDownload.contains(definition.key)) {
            auto [url, version] = frameSource(definition);
            return Download {definition.key, std::move(url), std::move(version)};
        }
    }
    return std::nullopt;
}

void ArtworkStore::downloadNext()
{
    if (m_downloading || m_reportsInFlight) {
        return;
    }
    m_downloading = nextDownload();
    if (!m_downloading) {
        return;
    }
    if (!m_iconsToDownload.empty() && m_iconsToDownload.front().first == m_downloading->key) {
        m_iconsToDownload.erase(m_iconsToDownload.begin());
    }

    m_net.downloadAchievementArtwork(
            m_downloading->url, [this, alive = std::weak_ptr(m_alive), download = *m_downloading](
                                        Error error, const std::vector<uint8_t> &data) {
                m_poster([this, alive, download, error, data]() mutable {
                    if (!alive.expired()) {
                        onDownloaded(download, error, std::move(data));
                    }
                });
            });
}

void ArtworkStore::onDownloaded(const Download &download, Error error, std::vector<uint8_t> data)
{
    m_downloading.reset();
    const auto &key = download.key;
    if (error == Error::Success) {
        INF("Achievements: artwork of '{}' ({} bytes) ready", key, data.size());
        if (m_kind == ArtworkKind::Image) {
            keepIcon(key, download.version, std::move(data));
        } else {
            m_artwork[key] = {download.version, std::move(data)};
        }
    } else {
        WRN("Achievements: can't download artwork of '{}'", key);
    }

    // A frame is downloaded again if a refresh meanwhile delivered a newer version
    const auto *definition = m_definitions->find(key);
    if (error != Error::Success || !definition || !needsFrame(*definition)) {
        m_framesToDownload.erase(key);
    }
    release(key);
    downloadNext();
}

void ArtworkStore::keepIcon(const std::string &key, const std::string &url,
                            std::vector<uint8_t> data)
{
    m_artwork[key] = {url, std::move(data), ++m_useClock};

    // An icon a caller still waits for is kept, whatever its age
    while (m_artwork.size() > ICON_CACHE_SIZE) {
        auto oldest = m_artwork.end();
        for (auto it = m_artwork.begin(); it != m_artwork.end(); ++it) {
            if (it->first != key && !m_waiting.contains(it->first)
                && (oldest == m_artwork.end() || it->second.lastUse < oldest->second.lastUse)) {
                oldest = it;
            }
        }
        if (oldest == m_artwork.end()) {
            break;
        }
        m_artwork.erase(oldest);
    }
}

void ArtworkStore::release(const std::string &key)
{
    if (pending(key)) {
        return;
    }
    const auto node = m_waiting.extract(key);
    if (!node) {
        return;
    }

    std::vector<uint8_t> data;
    if (const auto it = m_artwork.find(key); it != m_artwork.end()) {
        it->second.lastUse = ++m_useClock;
        data = it->second.data;
    }
    for (const auto &ready : node.mapped()) {
        ready(data);
    }
}

} // namespace achievements
} // namespace detail
} // namespace scorbit
