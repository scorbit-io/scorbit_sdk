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

#include <unordered_map>
#include <list>

namespace scorbit {
namespace detail {

template<typename Key, typename Value>
class LRUCache
{
public:
    LRUCache(size_t capacity)
        : m_capacity(capacity)
    {
    }

    bool has(const Key &key) const
    {
        return m_cache.find(key) != m_cache.end();
    }

    bool get(const Key &key, Value &value)
    {
        auto it = m_cache.find(key);
        if (it == m_cache.end())
            return false;

        // Move key to front (most recently used)
        m_usage.splice(m_usage.begin(), m_usage, it->second.second);
        value = it->second.first;
        return true;
    }

    template<typename V>
    void put(const Key &key, V &&value)
    {
        auto it = m_cache.find(key);
        if (it != m_cache.end()) {
            // Update value and move to front
            it->second.first = std::forward<V>(value);
            m_usage.splice(m_usage.begin(), m_usage, it->second.second);
        } else {
            // New entry
            if (m_cache.size() == m_capacity) {
                auto last = m_usage.back();
                m_usage.pop_back();
                m_cache.erase(last);
            }
            m_usage.push_front(key);
            m_cache[key] = {std::forward<V>(value), m_usage.begin()};
        }
    }

    void erase(const Key &key)
    {
        auto it = m_cache.find(key);
        if (it != m_cache.end()) {
            m_usage.erase(it->second.second);
            m_cache.erase(it);
        }
    }

private:
    size_t m_capacity;
    std::list<Key> m_usage;
    std::unordered_map<Key, std::pair<Value, typename std::list<Key>::iterator>> m_cache;
};

} // namespace detail
} // namespace scorbit
