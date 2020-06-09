/*
 *  Seos_Driver_Config.h
 *
 *  Copyright (C) 2019, Hensoldt Cyber GmbH
 */

#pragma once

#include "OS_Error.h"
#include "OS_Dataport.h"
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
        OS_Dataport_t        to;   // NIC -> stack
        OS_Dataport_t        from; // stack -> NIC
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
 * @return OS_ERROR_GENERIC initialization failed
 * @return OS_SUCCESS initialization successful
 */
OS_Error_t
chanmux_nic_driver_init(
    const chanmux_nic_drv_config_t*  driver_config);


/**
 * @brief run the driver main loop
 *
 * @return OS_ERROR_GENERIC driver main loop failed
 * @return OS_SUCCESS driver main loop terminated gracefully
 */
OS_Error_t
chanmux_nic_driver_run(void);


OS_Error_t
chanmux_nic_driver_rpc_tx_data(
    size_t* pLen);

OS_Error_t
chanmux_nic_driver_rpc_get_mac(void);

