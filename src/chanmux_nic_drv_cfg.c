/*
 * ChanMux NIC driver config
 *
 * Copyright (C) 2019-2024, HENSOLDT Cyber GmbH
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * For commercial licensing, contact: info.cyber@hensoldt.net
 */

#include "lib_debug/Debug.h"
#include "OS_Error.h"
#include "ChanMux/ChanMuxCommon.h"
#include "chanmux_nic_drv_api.h"
#include "chanmux_nic_drv.h"
#include "chanmux_nic_drv_api.h"
#include "network/OS_NetworkStackTypes.h"

static const chanmux_nic_drv_config_t *config;

//------------------------------------------------------------------------------
const ChanMux_ChannelOpsCtx_t *
get_chanmux_channel_ctrl(void)
{
    const ChanMux_ChannelOpsCtx_t *channel = &(config->chanmux.ctrl);

    Debug_ASSERT(NULL != channel);

    return channel;
}

//------------------------------------------------------------------------------
OS_Error_t
chanmux_channel_ctrl_mutex_lock(void)
{
    mutex_lock_func_t lock = config->nic_control_channel_mutex.lock;
    if (!lock)
    {
        Debug_LOG_ERROR("nic_control_channel_mutex.lock not set");
        return OS_ERROR_ABORTED;
    }

    int ret = lock();

    if (ret != 0)
    {
        Debug_LOG_ERROR("Failure getting lock, returned %d", ret);
        return OS_ERROR_ABORTED;
    }
    return OS_SUCCESS;
}

//------------------------------------------------------------------------------
OS_Error_t
chanmux_channel_ctrl_mutex_unlock(void)
{
    mutex_unlock_func_t unlock = config->nic_control_channel_mutex.unlock;
    if (!unlock)
    {
        Debug_LOG_ERROR("nic_control_channel_mutex.unlock not set");
        return OS_ERROR_ABORTED;
    }

    int ret = unlock();

    if (ret != 0)
    {
        Debug_LOG_ERROR("Failure releasing lock, returned %d", ret);
        return OS_ERROR_ABORTED;
    }
    return OS_SUCCESS;
}

//------------------------------------------------------------------------------
const ChanMux_ChannelOpsCtx_t *
get_chanmux_channel_data(void)
{
    const ChanMux_ChannelOpsCtx_t *channel = &(config->chanmux.data);

    Debug_ASSERT(NULL != channel);

    return channel;
}

//------------------------------------------------------------------------------
void chanmux_channel_data_wait(void)
{
    event_wait_func_t wait = config->chanmux.data.wait;
    if (!wait)
    {
        Debug_LOG_ERROR("chanmux.data.wait() not set");
        return;
    }

    wait();
}

//------------------------------------------------------------------------------
void chanmux_channel_ctrl_wait(void)
{
    event_wait_func_t wait = config->chanmux.ctrl.wait;
    if (!wait)
    {
        Debug_LOG_ERROR("chanmux.ctrl.wait() not set");
        return;
    }

    wait();
}

//------------------------------------------------------------------------------
const OS_SharedBuffer_t *
get_network_stack_port_to(void)
{
    Debug_ASSERT(!OS_Dataport_isUnset(config->network_stack.to));

    // TODO: this is a bit hacky, but we try to make things simpler by for the
    //       caller. And these are constants, so we don't expect any surprises.
    static OS_SharedBuffer_t s;
    s.buffer = OS_Dataport_getBuf(config->network_stack.to);
    s.len = OS_Dataport_getSize(config->network_stack.to);

    return &s;
}

//------------------------------------------------------------------------------
const OS_SharedBuffer_t *
get_network_stack_port_from(void)
{
    // network stack -> driver (aka output)
    Debug_ASSERT(!OS_Dataport_isUnset(config->network_stack.from));

    // TODO: this is a bit hacky, but we try to make things simpler by for the
    //       caller. And these are constants, so we don't expect any surprises.
    static OS_SharedBuffer_t s;
    s.buffer = OS_Dataport_getBuf(config->network_stack.from);
    s.len = OS_Dataport_getSize(config->network_stack.from);

    return &s;
}

//------------------------------------------------------------------------------
void network_stack_notify(void)
{
    event_notify_func_t notify = config->network_stack.notify;
    if (!notify)
    {
        Debug_LOG_ERROR("network_stack.notify() not set");
        return;
    }

    notify();
}

//------------------------------------------------------------------------------
OS_Error_t
chanmux_nic_driver_init(
    const chanmux_nic_drv_config_t *driver_config)
{
    Debug_LOG_INFO("network driver init");

    // save configuration
    config = driver_config;

    // initialize the shared memory, there is no data waiting in the buffer
    const OS_SharedBuffer_t *nw_input = get_network_stack_port_to();
    OS_NetworkStack_RxBuffer_t *nw_rx = (OS_NetworkStack_RxBuffer_t *)
                                            nw_input->buffer;
    nw_rx->len = 0;

    // initialize the ChanMUX/Proxy connection
    const ChanMux_ChannelOpsCtx_t *ctrl = get_chanmux_channel_ctrl();
    const ChanMux_ChannelOpsCtx_t *data = get_chanmux_channel_data();

    Debug_LOG_INFO("ChanMUX channels: ctrl=%u, data=%u", ctrl->id, data->id);

    OS_Error_t err = chanmux_nic_channel_open(ctrl, data->id);
    if (err != OS_SUCCESS)
    {
        Debug_LOG_ERROR("chanmux_nic_channel_open() failed, error:%d", err);
        return OS_ERROR_GENERIC;
    }

    Debug_LOG_INFO("network driver init successful");

    return OS_SUCCESS;
}

//------------------------------------------------------------------------------
OS_Error_t
chanmux_nic_driver_run(void)
{
    Debug_LOG_INFO("start network driver loop");
    // this loop is not supposed to terminate
    OS_Error_t err = chanmux_nic_driver_loop();
    if (err != OS_SUCCESS)
    {
        Debug_LOG_ERROR("chanmux_receive_loop() failed, error %d", err);
        return OS_ERROR_GENERIC;
    }

    // actually, the loop is not supposed to return without an error. If it
    // does, we assume this is a graceful termination
    Debug_LOG_INFO("chanmux_receive_loop() terminated gracefully");

    return OS_SUCCESS;
}
