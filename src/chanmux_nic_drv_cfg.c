/*
 *  Seos_Driver_Config.c
 *
 *  Copyright (C) 2019, Hensoldt Cyber GmbH
*/

#include "LibDebug/Debug.h"
#include "SeosError.h"
#include "seos_chanmux.h"
#include "seos_api_chanmux_nic_drv.h"
#include "chanmux_nic_drv.h"

static const seos_camkes_chanmx_nic_drv_config_t* camkes_cfg;


//------------------------------------------------------------------------------
const ChanMux_channelCtx_t*
get_chanmux_channel_ctrl(void)
{
    const ChanMux_channelCtx_t* channel = &(camkes_cfg->chanmux.ctrl);

    Debug_ASSERT( NULL != channel );

    return channel;
}


//------------------------------------------------------------------------------
const ChanMux_channelDuplexCtx_t*
get_chanmux_channel_data(void)
{
    const ChanMux_channelDuplexCtx_t* channel = &(camkes_cfg->chanmux.data);

    Debug_ASSERT( NULL != channel );

    return channel;
}


//------------------------------------------------------------------------------
void
chanmux_wait(void)
{
    event_wait_func_t wait = camkes_cfg->chanmux.wait;
    if (!wait)
    {
        Debug_LOG_ERROR("chanmux.wait() not set");
        return;
    }

    wait();
}


//------------------------------------------------------------------------------
const seos_shared_buffer_t*
get_network_stack_port_to(void)
{
    const seos_shared_buffer_t* port = &(camkes_cfg->network_stack.to);

    Debug_ASSERT( NULL != port );
    Debug_ASSERT( NULL != port->buffer );
    Debug_ASSERT( 0 != port->len );

    return port;
}


//------------------------------------------------------------------------------
const seos_shared_buffer_t*
get_network_stack_port_from(void)
{
    // network stack -> driver (aka output)
    const seos_shared_buffer_t* port = &(camkes_cfg->network_stack.from);

    Debug_ASSERT( NULL != port );
    Debug_ASSERT( NULL != port->buffer );
    Debug_ASSERT( 0 != port->len );

    return port;
}


//------------------------------------------------------------------------------
void
network_stack_notify(void)
{
    event_notify_func_t notify = camkes_cfg->network_stack.notify;
    if (!camkes_cfg->chanmux.wait)
    {
        Debug_LOG_ERROR("network_stack.notify() not set");
        return;
    }

    notify();
}


//------------------------------------------------------------------------------
seos_err_t
seos_chanmux_nic_driver_run(
    const seos_camkes_chanmx_nic_drv_config_t*  camkes_config)
{
    seos_err_t err;

    // save configuration
    camkes_cfg = camkes_config;

    // initialize driver
    err = chanmux_nic_driver_init();
    if (err != SEOS_SUCCESS)
    {
        Debug_LOG_ERROR("driver_init() failed, error:%d", err);
        return SEOS_ERROR_GENERIC;
    }

    Debug_LOG_INFO("send driver init complete notification");
    camkes_cfg->notify_init_complete();

    Debug_LOG_INFO("start network driver loop");
    // this loop is not supposed to terminate
    err = chanmux_nic_driver_loop();
    if (err != SEOS_SUCCESS)
    {
        Debug_LOG_ERROR("chanmux_receive_loop() failed, error:%d", err);
        return SEOS_ERROR_GENERIC;
    }

    // actually, the loop is not supposed to return without an error. If it
    // does, we assume this is a graceful termination
    Debug_LOG_INFO("chanmux_receive_loop() terminated gracefully");

    return SEOS_SUCCESS;
}
