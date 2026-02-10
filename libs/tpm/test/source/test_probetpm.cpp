#include <doctest/doctest.h>
#include <tpm/tpm.h>
#include "crypto_helpers.h"
#include <utils/bytearray.h>
#include <nlohmann/json.hpp>

#include <atca_device.h>
#include <host/atca_host.h>
#include <cinttypes>

void dump(uint8_t buf[], size_t len)
{
    size_t i;
    for (i = 0; i < len; ++i) 
    {
        if (i % 16 == 15)
            printf("%02x\n", buf[i]);
        else
            printf("%02x ", buf[i]);
    }
    if (i % 16) putchar('\n');
}

// ============================================================================
// ProbeTpm Basic Tests
// ============================================================================

TEST_SUITE("ProbeTpm Basic Tests")
{
    TEST_CASE("ProbeTpm Basic Tests")
    {
        Tpm tpm;
		printf("------------- Tpm.ok() ? -------------\n");
		printf("ok(): %s\n", tpm.ok() ? "OK":"NOK");
		printf("------------- Serial Number -------------\n");
        auto data = tpm.tpmSerialNumber();
        dump(data.data(), data.size());
	    CHECK(data.size() == 9);
		printf("----------------- Infos -----------------\n");
        data = tpm.info();
        dump(data.data(), data.size());
	    CHECK(data.size() == 4);
		printf("---------------- SN+UUID ----------------\n");
		CHECK(tpm.readSerialUuid());
                printf("SN=%" PRIu64 "\n", tpm.serialNumber());
		dump(tpm.uuid().data(), tpm.uuid().size());
		printf("------------- CONFIG LOCK ---------------\n");
		printf("Config: %s\n", tpm.isConfigLocked() ? "LOCKED":"UNLOCKED");
		printf("-------------- DATA LOCK ----------------\n");
		printf("Data: %s\n", tpm.isDataLocked() ? "LOCKED":"UNLOCKED");
		printf("---------------- CONFIG -----------------\n");
		data = tpm.readConfig();
		dump(data.data(), data.size());
		CHECK(data.size()==128);
		printf("-----------------------------------------\n");
    }
}
