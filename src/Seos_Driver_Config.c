/*
 *  Seos_Driver_Config.c
 *
 *  Copyright (C) 2019, Hensoldt Cyber GmbH
*/
#include "Seos_Driver_Config.h"
#include "LibDebug/Debug.h"

static seos_driver_config  tapConfig;

void Seos_NwDriver_setconfig(seos_driver_config* nw_driver_config)
{
    tapConfig.chan_ctrl                       = nw_driver_config->chan_ctrl;
    tapConfig.chan_data                       = nw_driver_config->chan_data;
    nw_driver_config->device_create_callback  = pico_chan_mux_tap_create;
}


seos_driver_config* Seos_NwDriver_getconfig(void)
{
    return &tapConfig;
}


seos_err_t
Seos_NwDriver_init(seos_driver_config* nw_driver_config)
{
    if (!nw_driver_config)
    {
        Debug_LOG_FATAL("%s(): Driver tap config failed", __FUNCTION__);
        return SEOS_ERROR_GENERIC;
    }

    Seos_NwDriver_setconfig(nw_driver_config);

    Debug_LOG_INFO("%s(): ctrl chan: %d, data chan:%d", __FUNCTION__,
                   nw_driver_config->chan_ctrl, nw_driver_config->chan_data);
    return SEOS_SUCCESS;
}
