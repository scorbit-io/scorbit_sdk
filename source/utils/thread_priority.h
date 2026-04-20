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

namespace scorbit {
namespace detail {

// Adjusts the calling thread's scheduling priority for SDK background work.
// @param niceValue On Linux, passed to setpriority(2) for this thread only; higher values mean
//        lower CPU priority. If 0, scheduling is left unchanged (default).
//        On macOS, any non-zero value selects QOS_CLASS_BACKGROUND for this thread; 0 leaves it
//        unchanged. Unsupported platforms ignore this call.
void applySdkThreadNice(int niceValue);

} // namespace detail
} // namespace scorbit
