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

#include <string>
#include <vector>

namespace scorbit {

struct BundlePrice {
    int credits {0};
    std::string price;        /// Effective price (sale or regular)
    std::string regularPrice; /// Non-discounted price
    std::string salePrice;    /// Discounted price, empty if no sale active
};

struct PricingInfo {
    bool freePlay {false};
    bool paymentsEnabled {false};
    std::string creditPrice;        /// Effective price per credit (sale or regular)
    std::string creditRegularPrice; /// Non-discounted price per credit
    std::string creditSalePrice;    /// Discounted price per credit, empty if no sale active
    std::vector<BundlePrice> bundles;
};

} // namespace scorbit
