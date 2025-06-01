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

#include <scorbit_sdk/log_c.h>
#include "logger.h"

using namespace scorbit;

void sb_add_logger_callback(sb_log_callback_t callback, void *userData)
{

    detail::Logger::instance()->addCallback(
            [callback, userData](const std::string &message, LogLevel level, const char *file,
                                 int line, int64_t timestamp) {
                callback(message.c_str(), static_cast<sb_log_level_t>(level), file, line, timestamp,
                         userData);
            },
            userData);
}

void sb_reset_logger(void)
{
    detail::Logger::instance()->clear();
}
