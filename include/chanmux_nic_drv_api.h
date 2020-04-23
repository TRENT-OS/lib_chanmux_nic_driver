/*
 *  Seos_Driver_Config.h
 *
 *  Copyright (C) 2019, Hensoldt Cyber GmbH
 */

#pragma once

#include "SeosError.h"
#include "seos_types.h"
#include "seos_chanmux.h"
#include <stdint.h>
#include <stddef.h>


typedef struct
{
    // if the driver initialization is complete, this notification is triggered
    // and the driver can be used by the network stack to send and receive data
    event_notify_func_t       notify_init_complete;

    struct
    {
        ChanMux_channelCtx_t        ctrl;
        ChanMux_channelDuplexCtx_t  data;
        event_notify_func_t         wait; // wait for incoming data
    } chanmux;

    struct
    {
        seos_shared_buffer_t  to;   // NIC -> stack
        seos_shared_buffer_t  from; // stack -> NIC
        event_notify_func_t   notify; // one ore more frames are available
    } network_stack;

    struct
    {
        mutex_lock_func_t    lock;
        mutex_unlock_func_t  unlock;
    } nic_control_channel_mutex;

} chanmux_nic_drv_config_t;


/**
 * @brief run the ChanMUX-Proxy-TAP ethernet driver
 *
 * @param config configuration for the driver
 *
 * @return an error code is returned if something fails, otherwise the function
 *         is not supposed to return. It may terminate gracefully
 */
seos_err_t
chanmux_nic_driver_run(
    const chanmux_nic_drv_config_t*  driver_config);


seos_err_t
chanmux_nic_driver_rpc_tx_data(
    size_t* pLen);

seos_err_t
chanmux_nic_driver_rpc_get_mac(void);

