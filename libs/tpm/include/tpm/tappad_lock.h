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

#pragma once

#include <atca_status.h>
#if defined(SCORBIT_TAPPAD_LOCK)
#    include <hal/hal_tappad_lock.h>
#endif

#include <cstdint>

/**
 * @brief Holds the cross-process tap pad lock for its lifetime.
 *
 * Every process that talks to a tap pad's secure element takes this lock
 * around a complete chip sequence, so no other process's command lands in the
 * middle of it (see libs/tpm/README-tappad-lock.md). It nests per thread. It is a sequence hold,
 * so after it is released this process briefly lets other processes in before taking it again.
 *
 * SCORBIT_TAPPAD_LOCK is set by lib_cryptoauth.cmake only where
 * cryptoauthlib-tappad-lock.patch is applied and compiled. Elsewhere (MSVC,
 * which builds cryptoauthlib unpatched) this is a no-op that always succeeds.
 */
class TappadLock
{
public:
    static constexpr uint32_t DEFAULT_TIMEOUT_MS = 2000;

#if defined(SCORBIT_TAPPAD_LOCK)
    static constexpr ATCA_STATUS BUSY = ATCA_TAPPAD_BUSY;

    explicit TappadLock(uint32_t timeoutMs = DEFAULT_TIMEOUT_MS)
        : m_status(hal_tappad_lock_sequence(timeoutMs))
    {
    }

    ~TappadLock()
    {
        if (ok()) {
            hal_tappad_unlock();
        }
    }

    /// False when the lock is held in-process only (no lock file could be opened).
    static bool isCrossProcess()
    {
        return hal_tappad_lock_path() != nullptr;
    }
#else
    static constexpr ATCA_STATUS BUSY = ATCA_TX_TIMEOUT;

    explicit TappadLock(uint32_t = DEFAULT_TIMEOUT_MS)
    {
    }

    static bool isCrossProcess()
    {
        return true;
    }
#endif

    TappadLock(const TappadLock &) = delete;
    TappadLock &operator=(const TappadLock &) = delete;

    bool ok() const
    {
        return m_status == ATCA_SUCCESS;
    }
    ATCA_STATUS status() const
    {
        return m_status;
    }

private:
    ATCA_STATUS m_status {ATCA_SUCCESS};
};
