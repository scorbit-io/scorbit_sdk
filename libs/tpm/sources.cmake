set(headers
    include/tpm/hardwaretpm.h
    include/tpm/itpm.h
    include/tpm/softwaretpm.h
    include/tpm/tpm.h
)

set(sources
    source/hardwaretpm.cpp
    source/softwaretpm.cpp
    source/tpm.cpp
    source/crypto_helpers.h
    source/crypto_helpers.cpp
    source/crypto_utils.h
    source/crypto_utils.cpp
    source/crypto_constants.h
)

set(c_sources
    source/fake_atca_hal.c
)
