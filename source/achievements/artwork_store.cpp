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

std::pair<std::string, std::string> ArtworkStore::source(const Definition &definition) const
{
    switch (m_kind) {
    case ArtworkKind::Frame:
        return {definition.frameUrl, std::to_string(definition.frameVersion)};
    case ArtworkKind::Image:
        return {definition.iconUrl, definition.iconUrl};
    case ArtworkKind::None:
        break;
    }
    return {};
}

bool ArtworkStore::needsDownload(const Definition &definition) const
{
    const auto [url, version] = source(definition);
    if (url.empty()) {
        return false;
    }
    const auto it = m_artwork.find(definition.key);
    return it == m_artwork.end() || it->second.version != version;
}

void ArtworkStore::refresh(std::shared_ptr<const DefinitionSet> definitions)
{
    m_definitions = std::move(definitions);

    // Artwork no longer delivered, or no longer of the kind kept, is dropped
    std::erase_if(m_artwork, [this](const auto &entry) {
        const auto *definition = m_definitions->find(entry.first);
        return !definition || source(*definition).first.empty();
    });

    m_toDownload.clear();
    for (const auto &definition : m_definitions->all()) {
        if (needsDownload(definition)) {
            m_toDownload.insert(definition.key);
        }
    }

    // A caller waiting on a download this refresh no longer wants is answered now
    for (auto it = m_waiting.begin(); it != m_waiting.end();) {
        const auto key = (it++)->first;
        release(key);
    }
    downloadNext();
}

void ArtworkStore::downloadNext()
{
    if (m_downloading) {
        return;
    }

    const auto &definitions = m_definitions->all();
    const auto next = std::ranges::find_if(definitions, [this](const Definition &definition) {
        return m_toDownload.contains(definition.key);
    });
    if (next == definitions.end()) {
        return;
    }

    m_downloading = true;
    auto [url, version] = source(*next);
    m_net.downloadAchievementArtwork(
            std::move(url),
            [this, alive = std::weak_ptr(m_alive), key = next->key,
             version = std::move(version)](Error error, const std::vector<uint8_t> &data) {
                m_poster([this, alive, key, version, error, data]() mutable {
                    if (!alive.expired()) {
                        onDownloaded(key, version, error, std::move(data));
                    }
                });
            });
}

void ArtworkStore::onDownloaded(const std::string &key, const std::string &version, Error error,
                                std::vector<uint8_t> data)
{
    m_downloading = false;
    if (error == Error::Success) {
        INF("Achievements: artwork of '{}' ({} bytes) ready", key, data.size());
        m_artwork[key] = {version, std::move(data)};
    } else {
        WRN("Achievements: can't download artwork of '{}'", key);
    }

    // Downloaded again if a refresh meanwhile delivered a newer version
    const auto *definition = m_definitions->find(key);
    if (error != Error::Success || !definition || !needsDownload(*definition)) {
        m_toDownload.erase(key);
    }
    release(key);
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

void ArtworkStore::whenReady(const std::string &key, Ready ready)
{
    m_waiting[key].push_back(std::move(ready));
    release(key);
}

void ArtworkStore::release(const std::string &key)
{
    if (m_toDownload.contains(key)) {
        return;
    }
    const auto node = m_waiting.extract(key);
    if (!node) {
        return;
    }
    for (const auto &ready : node.mapped()) {
        ready(cached(key).value_or(std::vector<uint8_t> {}));
    }
}

} // namespace achievements
} // namespace detail
} // namespace scorbit
