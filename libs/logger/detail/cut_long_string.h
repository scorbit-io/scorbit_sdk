/*
 * Logger internal helper (not part of public API).
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
 */

#pragma once

#include <fmt/format.h>
#include <string>

namespace logger {

/**
 * Truncate a string that exceeds maxLength, keeping the beginning and end
 * with an ellipsis in the middle.
 */
inline std::string cutLongString(const std::string &str, size_t maxLength)
{
    if (str.size() <= maxLength) {
        return str;
    }

    constexpr const char *ELIDE_STRING = " ... ";
    constexpr std::size_t ELIDE_STRING_LENGTH = std::char_traits<char>::length(ELIDE_STRING);
    const auto halfSize = (maxLength - ELIDE_STRING_LENGTH) / 2;
    return fmt::format("{}{}{}", str.substr(0, halfSize), ELIDE_STRING,
                       str.substr(str.length() - halfSize));
}

} // namespace logger
