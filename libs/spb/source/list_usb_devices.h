/*
 * scorbitd
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * Proprietary License
 */

#pragma once

#include <string>
#include <vector>

namespace spb {

/**
 * Lists USB devices based on the platform.
 * On Windows, returns vector of COM<N> ports
 * On MacOS, returns vector of devices starting with "/dev/cu.usbmodem".
 * On Linux, returns vector of devices starting with "/dev/ttyACM".
 *
 * @return A vector of strings containing the paths of the USB devices.
 */
std::vector<std::string>  listUsbDevices();

} // namespace spb
