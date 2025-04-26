/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Mar 2025
 *
 ****************************************************************************/

#pragma once

#include "net_base.h"
#include <boost/json.hpp>
#include <string>
#include <atomic>

namespace scorbit {
namespace detail {

class Updater
{
public:
    Updater(NetBase &net);

    void checkNewVersionAndUpdate(const boost::json::object &json);

private:
    void parseUrl(const boost::json::value &sdkVal);
    bool update(const std::string &archivePath);
    bool replaceLibrary(const std::string &libPath, const std::string &newLibPath);

private:
    NetBase &m_net;
    std::atomic_bool m_updateInProgress {false};

    std::string m_url;
    std::string m_version;
    std::string m_feedback;
};


} // namespace detail
} // namespace scorbit
