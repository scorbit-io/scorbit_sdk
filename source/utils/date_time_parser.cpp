/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Mar 2025
 *
 ****************************************************************************/

#include "date_time_parser.h"

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/time_facet.hpp>

// Function to parse HTTP date header and convert it to Unix timestamp
int64_t parseHttpDateToUnixTimestamp(const std::string &httpDate)
{
    using namespace boost::posix_time;

    // Create a time input facet for the HTTP date format
    time_input_facet *facet = new time_input_facet("%a, %d %b %Y %H:%M:%S GMT");
    std::istringstream ss(httpDate);
    ss.imbue(std::locale {ss.getloc(), facet});

    ptime pt;
    ss >> pt;

    if (pt == ptime()) {
        return -1;
    }

    // Convert ptime to Unix timestamp
    ptime epoch(boost::gregorian::date(1970, 1, 1));
    time_duration diff = pt - epoch;
    return diff.total_seconds();
}
