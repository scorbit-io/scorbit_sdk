/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Feb 2025
 *
 ****************************************************************************/

#include "utils/decrypt.h"
#include "utils/bytearray.h"

#include <catch2/catch_test_macros.hpp>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;

TEST_CASE("Decrypt")
{
    /*
        "encrypted" test variable was created with the following command from main directory:
        SCORBIT_SDK_ENCRYPT_SECRET=salt cmake -B test_encrypt_tool_build -S encrypt_tool && cmake --build test_encrypt_tool_build --parallel && ./test_encrypt_tool_build/encrypt_tool 07D54D8B3B2743550071639FDA6F5ED7BB0407C479FDA3A5CDBD22B09870DCF9 test

        Then you can remove build directory:
        rm -rf test_encrypt_tool_build
     */
    std::string encrypted = R"(qIsFDWq3tORiu5Hmhnlg8LtA8Y8IWPtxvmn9c/VbcL5RmDTU33i5hhCY6re/Fd5+2mDKOyDWKSQKGo8DgzVV9XkJQeAiqpU/2MssrA==)";
    std::string provider = "testsalt";

    auto decrypted = ByteArray(decryptSecret(encrypted, provider)).hex();
    CHECK(decrypted == "07D54D8B3B2743550071639FDA6F5ED7BB0407C479FDA3A5CDBD22B09870DCF9");
}
