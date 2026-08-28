#include <doctest/doctest.h>

#if defined(__linux__) && defined(ATCA_HAL_KIT_UART_LIBUSB)

#    include <hal/hal_linux_cdc_libusb.h>

TEST_SUITE("libusb CDC path")
{
    TEST_CASE("accepts generated paths")
    {
        CHECK(hal_cdc_libusb_is_path("usb:001:002:3"));
        CHECK(hal_cdc_libusb_is_path("usb:255:255:255"));
    }

    TEST_CASE("rejects malformed paths")
    {
        CHECK_FALSE(hal_cdc_libusb_is_path(nullptr));
        CHECK_FALSE(hal_cdc_libusb_is_path(""));
        CHECK_FALSE(hal_cdc_libusb_is_path("usb:1:2"));
        CHECK_FALSE(hal_cdc_libusb_is_path("usb:1:2:3:4"));
        CHECK_FALSE(hal_cdc_libusb_is_path("usb:1:2:3x"));
        CHECK_FALSE(hal_cdc_libusb_is_path("usb:-1:2:3"));
        CHECK_FALSE(hal_cdc_libusb_is_path("usb:256:2:3"));
        CHECK_FALSE(hal_cdc_libusb_is_path("/dev/ttyACM0"));
    }
}

#endif
