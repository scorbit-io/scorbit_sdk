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

#include "thread_priority.h"

#if defined(__linux__)
#    include <sched.h>
#    include <sys/resource.h>
#    include <sys/syscall.h>
#    include <unistd.h>
#    include <cerrno>
#elif defined(__APPLE__)
#    include <pthread.h>
#endif

namespace scorbit {
namespace detail {

void applySdkThreadNice(int niceValue)
{
#if defined(__linux__)
    if (niceValue == 0) {
        return;
    }
    // setpriority with PRIO_PROCESS and tid==0 would affect the whole process on
    // some glibc versions.  Use the raw syscall with the Linux thread-id instead
    // so only the calling thread is re-niced.
    const pid_t tid = static_cast<pid_t>(syscall(SYS_gettid));

    // SCHED_BATCH marks the thread as CPU-bound background work: the scheduler stops treating it
    // as interactive, so it no longer gets the wakeup preemption bonus that lets it repeatedly
    // cut ahead of a running thread.  On a single-core board that bonus is what allows a burst of
    // SDK work (TLS handshakes, JSON parsing) to keep preempting the host's game loop.  nice
    // below then sets how much CPU share it gets when both are runnable.
    // sched_priority must be 0 for SCHED_BATCH.
    struct sched_param param = {};
    param.sched_priority = 0;
    sched_setscheduler(tid, SCHED_BATCH, &param); // best-effort: containers may forbid it

    if (setpriority(PRIO_PROCESS, static_cast<id_t>(tid), niceValue) != 0) {
        // Best-effort; ignore EACCES (may lack CAP_SYS_NICE)
    }
#elif defined(__APPLE__)
    if (niceValue == 0) {
        return;
    }
    // macOS doesn't have per-thread nice values exposed via setpriority.
    // QOS_CLASS_BACKGROUND is the lowest non-maintenance class.
    pthread_set_qos_class_self_np(QOS_CLASS_BACKGROUND, 0);
#else
    (void)niceValue;
    // No-op on unsupported platforms (Windows, etc.)
#endif
}

} // namespace detail
} // namespace scorbit
