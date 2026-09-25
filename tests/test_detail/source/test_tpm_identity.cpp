#include <catch2/catch_test_macros.hpp>

#include "tpm_identity.h"

using tpm_identity::isSameChip;
using utils::ByteArray;

namespace {
const ByteArray OURS {0x01, 0x02, 0x03, 0x04};
const ByteArray OTHER {0x09, 0x08, 0x07, 0x06};
} // namespace

TEST_CASE("the provisioned chip is recognised", "[tpm_identity]")
{
    CHECK(isSameChip(20558, OURS, 20558, OURS));
}

TEST_CASE("a chip with another uuid is refused", "[tpm_identity]")
{
    CHECK_FALSE(isSameChip(20558, OURS, 20558, OTHER));
}

TEST_CASE("a chip with another serial is refused", "[tpm_identity]")
{
    CHECK_FALSE(isSameChip(20558, OURS, 31871, OURS));
}

TEST_CASE("a chip that reports no identity is refused once ours is known", "[tpm_identity]")
{
    CHECK_FALSE(isSameChip(20558, OURS, 0, ByteArray {}));
}

TEST_CASE("an unknown expected identity matches no chip", "[tpm_identity]")
{
    CHECK_FALSE(isSameChip(0, ByteArray {}, 20558, OURS));
    CHECK_FALSE(isSameChip(0, ByteArray {}, 0, ByteArray {}));
}
