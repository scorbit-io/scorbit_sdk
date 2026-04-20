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

#include <scorbit_sdk/event_helpers_c.h>
#include <scorbit_sdk/event_types.h>
#include "event_classes.h"

using namespace scorbit;

// ------------------------------  Public API implementation -------------------------------

sb_event_type_t sb_event_type(const sb_event_t *event)
{
    const auto eventBase = static_cast<const detail::EventBase *>(event);
    return static_cast<sb_event_type_t>(eventBase->type());
}

bool sb_event_game_start_requested(const sb_event_t *event, int *players_count)
{
    if (!event || !players_count) {
        return false;
    }

    auto derived = dynamic_cast<const scorbit::detail::GameStartRequestedEvent *>(event);
    if (!derived) {
        return false;
    }

    *players_count = derived->playersCount();
    return true;
}

bool sb_event_credits_add_requested(const sb_event_t *event, int *credits, const char **transaction)
{
    if (!event || !credits || !transaction) {
        return false;
    }

    auto derived = dynamic_cast<const scorbit::detail::CreditsAddRequestedEvent *>(event);
    if (!derived) {
        return false;
    }

    *credits = derived->credits();
    *transaction = derived->transaction().c_str();
    return true;
}

bool sb_event_diagnostics_upload_requested(const sb_event_t *event, bool *include_recordings)
{
    if (!event || !include_recordings) {
        return false;
    }

    auto derived = dynamic_cast<const scorbit::detail::DiagnosticsUploadRequestedEvent *>(event);
    if (!derived) {
        return false;
    }

    *include_recordings = derived->includeRecordings();
    return true;
}

bool sb_event_diagnostics_uploaded(const sb_event_t *event, bool *success)
{
    if (!event || !success) {
        return false;
    }

    auto derived = dynamic_cast<const scorbit::detail::DiagnosticsUploadedEvent *>(event);
    if (!derived) {
        return false;
    }

    *success = derived->success();
    return true;
}

bool sb_event_config_received(const sb_event_t *event, const char **config_json)
{
    if (!event || !config_json) {
        return false;
    }

    auto derived = dynamic_cast<const scorbit::detail::ConfigReceivedEvent *>(event);
    if (!derived) {
        return false;
    }

    *config_json = derived->configJsonCStr();
    return true;
}

bool sb_event_scorbitd_update_received(const sb_event_t *event, const char **update_json)
{
    if (!event || !update_json) {
        return false;
    }

    auto derived = dynamic_cast<const scorbit::detail::ScorbitdUpdateReceivedEvent *>(event);
    if (!derived) {
        return false;
    }

    *update_json = derived->updateJson().c_str();
    return true;
}

bool sb_event_scorbitd_updated(const sb_event_t *event, const char **version,
                               const char **executable_path)
{
    if (!event || !version || !executable_path) {
        return false;
    }

    auto derived = dynamic_cast<const scorbit::detail::ScorbitdUpdatedEvent *>(event);
    if (!derived) {
        return false;
    }

    *version = derived->version().c_str();
    *executable_path = derived->executable().c_str();
    return true;
}

bool sb_event_firmwares_list_received(const sb_event_t *event, const char **firmwares_list)
{
    if (!event || !firmwares_list) {
        return false;
    }

    auto derived = dynamic_cast<const scorbit::detail::FirmwaresListReceivedEvent *>(event);
    if (!derived) {
        return false;
    }

    *firmwares_list = derived->firmwaresList().c_str();
    return true;
}

// ----------------------- PlayersUpdated helpers -----------------------

static const scorbit::detail::PlayerProfile *toPlayerProfile(const sb_event_t *event,
                                                             sb_player_t player)
{
    if (!event) {
        return nullptr;
    }
    auto derived = dynamic_cast<const scorbit::detail::PlayersUpdatedEvent *>(event);
    if (!derived) {
        return nullptr;
    }
    return derived->playerByNumber(player);
}

bool sb_event_players_updated(const sb_event_t *event, int *count)
{
    if (!event || !count) {
        return false;
    }
    auto derived = dynamic_cast<const scorbit::detail::PlayersUpdatedEvent *>(event);
    if (!derived) {
        return false;
    }
    *count = derived->playersCount();
    return true;
}

bool sb_event_player_has_info(const sb_event_t *event, sb_player_t player, bool *has_info)
{
    if (!has_info) {
        return false;
    }
    auto profile = toPlayerProfile(event, player);
    if (!profile) {
        return false;
    }
    *has_info = profile->hasInfo();
    return true;
}

bool sb_event_player_id(const sb_event_t *event, sb_player_t player, const char **id)
{
    if (!id) {
        return false;
    }
    auto profile = toPlayerProfile(event, player);
    if (!profile) {
        return false;
    }
    *id = profile->id.c_str();
    return true;
}

bool sb_event_player_preferred_name(const sb_event_t *event, sb_player_t player, const char **name)
{
    if (!name) {
        return false;
    }
    auto profile = toPlayerProfile(event, player);
    if (!profile) {
        return false;
    }
    *name = profile->preferInitials ? profile->initials.c_str() : profile->name.c_str();
    return true;
}

bool sb_event_player_name(const sb_event_t *event, sb_player_t player, const char **name)
{
    if (!name) {
        return false;
    }
    auto profile = toPlayerProfile(event, player);
    if (!profile) {
        return false;
    }
    *name = profile->name.c_str();
    return true;
}

bool sb_event_player_initials(const sb_event_t *event, sb_player_t player, const char **initials)
{
    if (!initials) {
        return false;
    }
    auto profile = toPlayerProfile(event, player);
    if (!profile) {
        return false;
    }
    *initials = profile->initials.c_str();
    return true;
}

bool sb_event_player_picture_url(const sb_event_t *event, sb_player_t player, const char **url)
{
    if (!url) {
        return false;
    }
    auto profile = toPlayerProfile(event, player);
    if (!profile) {
        return false;
    }
    *url = profile->pictureUrl.c_str();
    return true;
}

bool sb_event_player_claim_deeplink(const sb_event_t *event, sb_player_t player, const char **url)
{
    if (!url) {
        return false;
    }
    auto profile = toPlayerProfile(event, player);
    if (!profile) {
        return false;
    }
    *url = profile->claimDeeplink.c_str();
    return true;
}

// ----------------------- PlayerPictureReady helper -----------------------

bool sb_event_player_picture_ready(const sb_event_t *event, sb_player_t *player,
                                   const uint8_t **data, size_t *size)
{
    if (!event || !player || !data || !size) {
        return false;
    }
    auto derived = dynamic_cast<const scorbit::detail::PlayerPictureReadyEvent *>(event);
    if (!derived) {
        return false;
    }
    *player = derived->player();
    *data = derived->pictureData();
    *size = derived->pictureSize();
    return true;
}

// ----------------------- Pricing helpers -----------------------

static const scorbit::detail::PricingReceivedEvent *toPricingEvent(const sb_event_t *event)
{
    if (!event) {
        return nullptr;
    }
    return dynamic_cast<const scorbit::detail::PricingReceivedEvent *>(event);
}

bool sb_event_pricing_free_play(const sb_event_t *event, bool *free_play)
{
    if (!free_play) {
        return false;
    }
    auto derived = toPricingEvent(event);
    if (!derived) {
        return false;
    }
    *free_play = derived->freePlay();
    return true;
}

bool sb_event_pricing_payments_enabled(const sb_event_t *event, bool *payments_enabled)
{
    if (!payments_enabled) {
        return false;
    }
    auto derived = toPricingEvent(event);
    if (!derived) {
        return false;
    }
    *payments_enabled = derived->paymentsEnabled();
    return true;
}

bool sb_event_pricing_credit_price(const sb_event_t *event, const char **price)
{
    if (!price) {
        return false;
    }
    auto derived = toPricingEvent(event);
    if (!derived || !derived->hasPrices()) {
        return false;
    }
    *price = derived->creditPrice().c_str();
    return true;
}

bool sb_event_pricing_credit_regular_price(const sb_event_t *event, const char **price)
{
    if (!price) {
        return false;
    }
    auto derived = toPricingEvent(event);
    if (!derived || !derived->hasPrices()) {
        return false;
    }
    *price = derived->creditRegularPrice().c_str();
    return true;
}

bool sb_event_pricing_credit_sale_price(const sb_event_t *event, const char **price)
{
    if (!price) {
        return false;
    }
    auto derived = toPricingEvent(event);
    if (!derived || !derived->hasPrices()) {
        return false;
    }
    const auto &salePrice = derived->creditSalePrice();
    if (salePrice.empty()) {
        return false;
    }
    *price = salePrice.c_str();
    return true;
}

bool sb_event_pricing_bundles_count(const sb_event_t *event, int *count)
{
    if (!count) {
        return false;
    }
    auto derived = toPricingEvent(event);
    if (!derived) {
        return false;
    }
    *count = derived->bundlesCount();
    return true;
}

bool sb_event_pricing_bundle_credits(const sb_event_t *event, int index, int *credits)
{
    if (!credits) {
        return false;
    }
    auto derived = toPricingEvent(event);
    if (!derived) {
        return false;
    }
    auto b = derived->bundle(index);
    if (!b) {
        return false;
    }
    *credits = b->credits;
    return true;
}

bool sb_event_pricing_bundle_price(const sb_event_t *event, int index, const char **price)
{
    if (!price) {
        return false;
    }
    auto derived = toPricingEvent(event);
    if (!derived) {
        return false;
    }
    auto b = derived->bundle(index);
    if (!b) {
        return false;
    }
    *price = b->price.c_str();
    return true;
}

bool sb_event_pricing_bundle_regular_price(const sb_event_t *event, int index, const char **price)
{
    if (!price) {
        return false;
    }
    auto derived = toPricingEvent(event);
    if (!derived) {
        return false;
    }
    auto b = derived->bundle(index);
    if (!b) {
        return false;
    }
    *price = b->regularPrice.c_str();
    return true;
}

bool sb_event_pricing_bundle_sale_price(const sb_event_t *event, int index, const char **price)
{
    if (!price) {
        return false;
    }
    auto derived = toPricingEvent(event);
    if (!derived) {
        return false;
    }
    auto b = derived->bundle(index);
    if (!b) {
        return false;
    }
    const auto &salePrice = b->salePrice;
    if (salePrice.empty()) {
        return false;
    }
    *price = salePrice.c_str();
    return true;
}
