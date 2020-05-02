/*
 *  Seos_Driver_Config.h
 *
 *  Copyright (C) 2019, Hensoldt Cyber GmbH
 */

#pragma once

#include "SeosError.h"
#include "seos_types.h"
#include "ChanMux/ChanMuxCommon.h"
#include <stdint.h>
#include <stddef.h>


typedef struct
{
    struct
    {
        ChanMux_channelCtx_t        ctrl;
        ChanMux_channelDuplexCtx_t  data;
        event_notify_func_t         wait; // wait for incoming data
    } chanmux;

    struct
    {
        ChanMux_dataport_t   to;   // NIC -> stack
        ChanMux_dataport_t   from; // stack -> NIC
        event_notify_func_t  notify; // one ore more frames are available
    } network_stack;

    struct
    {
        mutex_lock_func_t    lock;
        mutex_unlock_func_t  unlock;
    } nic_control_channel_mutex;

} chanmux_nic_drv_config_t;


/**
 * @brief initialize the driver
 *
 * @param config configuration for the driver
 *
 * @return SEOS_ERROR_GENERIC initialization failed
 * @return SEOS_SUCCESS initialization successful
 */
seos_err_t
chanmux_nic_driver_init(
    const chanmux_nic_drv_config_t*  driver_config);


/**
 * @brief run the driver main loop
 *
 * @return SEOS_ERROR_GENERIC driver main loop failed
 * @return SEOS_SUCCESS driver main loop terminated gracefully
 */
seos_err_t
chanmux_nic_driver_run(void);


seos_err_t
chanmux_nic_driver_rpc_tx_data(
    size_t* pLen);

seos_err_t
chanmux_nic_driver_rpc_get_mac(void);

