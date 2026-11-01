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

// Compile-only: the vendored headers must build as C++17 on their own.

#include <table_signature/inflate.h>
#include <table_signature/table_signature.h>
#include <table_signature/vpx_reader.h>

// MSVC reports the standard in _MSVC_LANG; __cplusplus stays 199711L without /Zc:__cplusplus.
#if defined(_MSVC_LANG)
#    define TABLE_SIGNATURE_CXX _MSVC_LANG
#else
#    define TABLE_SIGNATURE_CXX __cplusplus
#endif
#if TABLE_SIGNATURE_CXX != 201703L
#    error "compile this file as C++17"
#endif
