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

#include "event_types_c.h"
#include <scorbit_sdk/event_types_c.h>

namespace scorbit {

enum class EventType {
    GameStartRequested = SB_EVT_GAME_START_REQUESTED,
    CreditsAddRequested = SB_EVT_CREDITS_ADD_REQUESTED,
    CreditsStatusRequested = SB_EVT_CREDITS_STATUS_REQUESTED,
    ConfigReceived = SB_EVT_CONFIG_RECEIVED,
    PlayersUpdated = SB_EVT_PLAYERS_UPDATED,
    PlayerPictureReady = SB_EVT_PLAYER_PICTURE_READY,
    DiagnosticsUploadRequested = SB_EVT_DIAGNOSTICS_UPLOAD_REQUESTED,
    DiagnosticsUploaded = SB_EVT_DIAGNOSTICS_UPLOADED,
    PricingReceived = SB_EVT_PRICING_RECEIVED,
    PairingStatusChanged = SB_EVT_PAIRING_STATUS_CHANGED,

    // ---------------- OEM providers can ignore the events below ----------------

    None = SB_EVT_NONE, // This event shoud not be used
    ScorbitdUpdateReceived = SB_EVT_SCORBITD_UPDATE_RECEIVED,
    ScorbitdUpdated = SB_EVT_SCORBITD_UPDATED,
    FirmwaresListReceived = SB_EVT_FIRMWARES_LIST_RECEIVED,
};

} // namespace scorbit
