set(headers
    include/tpm/hardwaretpm.h
    include/tpm/itpm.h
    include/tpm/softwaretpm.h
    include/tpm/tpm.h
    include/tpm/crypto_helpers.h
)

set(sources
    source/hardwaretpm.cpp
    source/softwaretpm.cpp
    source/tpm.cpp
    source/atca_retry.h
    source/tpm_identity.h
    source/crypto_helpers.cpp
    source/crypto_utils.h
    source/crypto_utils.cpp
    source/crypto_constants.h
)

set(c_sources
    source/fake_atca_hal.c
)
