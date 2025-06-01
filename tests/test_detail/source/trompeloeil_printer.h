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

#include "game_data.h"
#include <trompeloeil.hpp>

// Custom printer for GameData
namespace trompeloeil {
template<>
struct printer<scorbit::detail::GameData> {
    static void print(std::ostream &os, const scorbit::detail::GameData &g)
    {
        os << "GameData { "
           << "isGameStarted: " << g.isGameActive << ", "
           << "ball: " << g.ball << ", "
           << "activePlayer: " << g.activePlayer << ", "
           << "players: {";

        for (const auto &p : g.players) {
            os << " Player " << p.second.player()
               << ": score = " << p.second.score() << ",";
        }
        os << " } }, ";

        os << "modes: \"" << g.modes.str() << "\"";
    }
};

template<>
struct printer<std::optional<bool>> {
    static void print(std::ostream &os, const std::optional<bool> &o)
    {
        if (o) {
            os << std::boolalpha << *o;
        } else {
            os << "nullopt";
        }
    }
};

template<>
struct printer<std::optional<std::string>> {
    static void print(std::ostream &os, const std::optional<std::string> &o)
    {
        if (o) {
            os << *o;
        } else {
            os << "nullopt";
        }
    }
};
}
