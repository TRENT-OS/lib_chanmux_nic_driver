/*
 *  ChanMux NIC driver config
 *
 *  Copyright (C) 2019, Hensoldt Cyber GmbH
*/

#include "LibDebug/Debug.h"
#include "SeosError.h"
#include "seos_chanmux.h"
#include "chanmux_nic_drv_api.h"
#include "chanmux_nic_drv.h"
#include "chanmux_nic_drv_api.h"
#include "os_util/seos_network_stack.h"

static const chanmux_nic_drv_config_t* config;


//------------------------------------------------------------------------------
const ChanMux_channelCtx_t*
get_chanmux_channel_ctrl(void)
{
    const ChanMux_channelCtx_t* channel = &(config->chanmux.ctrl);

    Debug_ASSERT( NULL != channel );

    return channel;
}


//------------------------------------------------------------------------------
seos_err_t
chanmux_channel_ctrl_mutex_lock(void)
{
    mutex_lock_func_t lock = config->nic_control_channel_mutex.lock;
    if (!lock)
    {
        Debug_LOG_ERROR("nic_control_channel_mutex.lock not set");
        return SEOS_ERROR_ABORTED;
    }

    int ret = lock();

    if (ret != 0)
    {
        Debug_LOG_ERROR("Failure getting lock, returned %d", ret);
        return SEOS_ERROR_ABORTED;
    }
    return SEOS_SUCCESS;
}


//------------------------------------------------------------------------------
seos_err_t
chanmux_channel_ctrl_mutex_unlock(void)
{
    mutex_unlock_func_t unlock = config->nic_control_channel_mutex.unlock;
    if (!unlock)
    {
        Debug_LOG_ERROR("nic_control_channel_mutex.unlock not set");
        return SEOS_ERROR_ABORTED;
    }

    int ret = unlock();

    if (ret != 0)
    {
        Debug_LOG_ERROR("Failure releasing lock, returned %d", ret);
        return SEOS_ERROR_ABORTED;
    }
    return SEOS_SUCCESS;
}


//------------------------------------------------------------------------------
const ChanMux_channelDuplexCtx_t*
get_chanmux_channel_data(void)
{
    const ChanMux_channelDuplexCtx_t* channel = &(config->chanmux.data);

    Debug_ASSERT( NULL != channel );

    return channel;
}


//------------------------------------------------------------------------------
void
chanmux_wait(void)
{
    event_wait_func_t wait = config->chanmux.wait;
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
    const seos_shared_buffer_t* port = &(config->network_stack.to);

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
    const seos_shared_buffer_t* port = &(config->network_stack.from);

    Debug_ASSERT( NULL != port );
    Debug_ASSERT( NULL != port->buffer );
    Debug_ASSERT( 0 != port->len );

    return port;
}


//------------------------------------------------------------------------------
void
network_stack_notify(void)
{
    event_notify_func_t notify = config->network_stack.notify;
    if (!config->chanmux.wait)
    {
        Debug_LOG_ERROR("network_stack.notify() not set");
        return;
    }

    notify();
}


//------------------------------------------------------------------------------
seos_err_t
chanmux_nic_driver_init(
    const chanmux_nic_drv_config_t*  driver_config)
{
    Debug_LOG_INFO("network driver init");

    // save configuration
    config = driver_config;

    // initialize the shared memory, there is no data waiting in the buffer
    const seos_shared_buffer_t* nw_input = get_network_stack_port_to();
    Rx_Buffer* nw_rx = (Rx_Buffer*)nw_input->buffer;
    nw_rx->len = 0;

    // initialize the ChanMUX/Proxy connection
    const ChanMux_channelCtx_t* ctrl = get_chanmux_channel_ctrl();
    const ChanMux_channelDuplexCtx_t* data = get_chanmux_channel_data();

    Debug_LOG_INFO("ChanMUX channels: ctrl=%u, data=%u", ctrl->id, data->id);

    seos_err_t err = chanmux_nic_channel_open(ctrl, data->id);
    if (err != SEOS_SUCCESS)
    {
        Debug_LOG_ERROR("chanmux_nic_channel_open() failed, error:%d", err);
        return SEOS_ERROR_GENERIC;
    }

    Debug_LOG_INFO("network driver init successful");

    return SEOS_SUCCESS;
}


//------------------------------------------------------------------------------
seos_err_t
chanmux_nic_driver_run(void)
{
    Debug_LOG_INFO("start network driver loop");
    // this loop is not supposed to terminate
    seos_err_t err = chanmux_nic_driver_loop();
    if (err != SEOS_SUCCESS)
    {
        Debug_LOG_ERROR("chanmux_receive_loop() failed, error %d", err);
        return SEOS_ERROR_GENERIC;
    }

    // actually, the loop is not supposed to return without an error. If it
    // does, we assume this is a graceful termination
    Debug_LOG_INFO("chanmux_receive_loop() terminated gracefully");

    return SEOS_SUCCESS;
}
