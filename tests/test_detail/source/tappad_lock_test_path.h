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

#include <tpm/tappad_lock.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

// Every process of this test binary points the tap pad lock at a private file in the temp
// directory before main(), so no test (the lock's own, or any that reaches a TappadLock through
// atcaRetry or TPM discovery) ever touches the real lock path.
namespace tappad_lock_test {

std::string privateLockPath();

// Point the lock back at privateLockPath(), after a test overrode it.
void usePrivateLockPath();

// Whether a probe or tap pad may be attached (a CDC tty, a libusb probe port or the probe's HID
// interface). A test whose TPM open is not stopped by a held lock skips itself when it is.
bool tapPadMayBePresent();

// Holds the tap pad lock on another thread from construction until release() or destruction.
class OtherHolder
{
public:
    OtherHolder()
        : m_thread([this] {
            const TappadLock hold;
            m_held = hold.ok();
            m_tried = true;
            while (!m_release) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        })
    {
        while (!m_tried) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    ~OtherHolder() { release(); }
    OtherHolder(const OtherHolder &) = delete;
    OtherHolder &operator=(const OtherHolder &) = delete;

    bool held() const { return m_held; }

    void release()
    {
        m_release = true;
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

private:
    std::atomic<bool> m_held {false};
    std::atomic<bool> m_tried {false};
    std::atomic<bool> m_release {false};
    std::thread m_thread;
};

} // namespace tappad_lock_test
