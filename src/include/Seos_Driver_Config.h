/*
 *  Seos_Driver_Config.h
 *
 *  Copyright (C) 2019, Hensoldt Cyber GmbH
*/
#pragma once

#include "Seos_pico_dev_chan_mux.h"

/**
 * @brief   Seos_Driver_Config contains configuration of the driver. Must
 *          be called before initialising Nw stack
 * @ingroup Seos_Driver_Config.h
*/
typedef struct
{
    int  chan_ctrl;     /**< ChanMux Ctrl channel */
    int  chan_data;     /**< ChanMux Data channel */
    /**< pointer to driver Callback e.g tap create device */
    struct pico_device* (*device_create_callback)(void);
} seos_driver_config;


void Seos_NwDriver_setconfig(seos_driver_config* nw_driver_config);

seos_driver_config* Seos_NwDriver_getconfig(void);

seos_err_t
Seos_NwDriver_init(seos_driver_config* nw_driver_config);
