/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2025
 *
 ****************************************************************************/

#ifndef TPM_USE_HW

#include <atca_status.h>
#include <atca_iface.h>

ATCA_STATUS hal_i2c_init(ATCAIface iface, ATCAIfaceCfg *cfg)
{
    (void)iface;
    (void)cfg;
    return ATCA_GEN_FAIL;
}

ATCA_STATUS hal_i2c_post_init(ATCAIface iface)
{
    (void)iface;
    return ATCA_GEN_FAIL;
}

ATCA_STATUS hal_i2c_send(ATCAIface iface, uint8_t word_address, uint8_t *txdata, int txlength)
{
    (void)iface;
    (void)word_address;
    (void)txdata;
    (void)txlength;
    return ATCA_GEN_FAIL;
}

ATCA_STATUS hal_i2c_receive(ATCAIface iface, uint8_t word_address, uint8_t *rxdata, uint16_t *rxlength)
{
    (void)iface;
    (void)word_address;
    (void)rxdata;
    (void)rxlength;
    return ATCA_GEN_FAIL;
}

ATCA_STATUS hal_i2c_control(ATCAIface iface, uint8_t option, void* param, size_t paramlen)
{
    (void)iface;
    (void)param;
    (void)paramlen;
    (void)option;
    return ATCA_GEN_FAIL;
}

ATCA_STATUS hal_i2c_release(void *hal_data)
{
    (void)hal_data;
    return ATCA_GEN_FAIL;
}

#endif
