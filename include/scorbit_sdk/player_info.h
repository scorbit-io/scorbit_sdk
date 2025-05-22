/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Feb 2025
 *
 ****************************************************************************/

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace scorbit {

using Picture = std::vector<uint8_t>; // The profile picture binary (jpg)

struct PlayerInfo {
    std::string preferredName;        /// Either name or initials
    std::string name;                 /// The player's name to display
    std::string initials;             /// The player's initials, e.g. "DTM"
    std::vector<uint8_t> picture;     /// The profile picture binary (jpg), can be empty
};

}
