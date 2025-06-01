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

#include <munit.h>
#include <stdio.h>

typedef struct {
    sb_log_level_t level;
    int line;
    char message[1000];
    char file[1000];
} UserData;

void logCallback(const char *message, sb_log_level_t level, const char *file, int line,
                 int64_t timestamp, void *userData)
{
    UserData *data = (UserData *)userData;
    data->level = level;
    data->line = line;
    (void)timestamp;
    snprintf(data->message, sizeof(data->message), "%s", message);
    snprintf(data->file, sizeof(data->file), "%s", file);
}

static MunitResult test_sb_add_logger_callback(const MunitParameter params[], void *user_data)
{
    (void)params;
    (void)user_data;

    UserData data;
    sb_add_logger_callback(logCallback, &data);
    sb_reset_logger();

    return MUNIT_OK;
}

// =======================================================================================

// Test suite setup
static MunitTest tests[] = {{"/sb_add_logger_callback/add_callback",
                             test_sb_add_logger_callback, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
                            {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}};

// Test suite definition
static const MunitSuite test_suite = {"/scorbit_sdk_tests_c", tests, NULL, 1,
                                      MUNIT_SUITE_OPTION_NONE};

// Main function
int main(int argc, char *argv[])
{
    return munit_suite_main(&test_suite, "munit", argc, argv);
}
