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

#include "nfc_flash.h"

#include <memory>

namespace nfc {
class ProbesManager;
}

namespace scorbit {
namespace detail {
namespace nfc_flash {

/** The real tap pad: the SDK's ProbesManager, the tap pad lock, sysfs and libusb. */
class SdkPad : public Pad
{
public:
    explicit SdkPad(std::shared_ptr<nfc::ProbesManager> probes);

    std::optional<uint64_t> build() override;
    std::unique_ptr<Hold> hold(bool withProbe, bool &busy) override;
    std::vector<std::string> bootselPorts() override;
    bool probeAt(const std::string &port) override;
    std::unique_ptr<nfc::picoboot::Transport> openBootsel(const std::string &port) override;

private:
    std::shared_ptr<nfc::ProbesManager> m_probes;
};

/**
 * Whether this process can flash the pad: Linux, with the pad's USB device node open to it for
 * writing (root, or the package's udev rule). Decided from the process's own access, never from
 * the host's, so the SDK advertises nfc only where a flash can work.
 */
bool nfcFlashable(nfc::ProbesManager &probes);

} // namespace nfc_flash
} // namespace detail
} // namespace scorbit
