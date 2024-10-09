/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

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
           << "isGameStarted: " << g.isGameStarted << ", "
           << "ball: " << g.ball << ", "
           << "activePlayer: " << g.activePlayer << ", "
           << "players: {";

        for (const auto &p : g.players) {
            os << " Player " << p.second.player()
               << ": score = " << p.second.score() << ",";
        }
        os << " } }";
    }
};
}
