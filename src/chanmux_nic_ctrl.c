/*
 * ChanMUX Ethernet TAP driver, control channel handling
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
#include "network/OS_NetworkTypes.h"
#include "ChanMuxNic.h"
#include "chanmux_nic_drv.h"
#include "chanmux_nic_drv_api.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

//------------------------------------------------------------------------------
// write command into control channel. There is no point in returning the
// written bytes as full command must be send or there is an error
static OS_Error_t
chanmux_ctrl_write(
    const ChanMux_ChannelOpsCtx_t *ctrl_channel,
    const void *buf,
    size_t len)
{
    size_t port_size;

    port_size = OS_Dataport_getSize(ctrl_channel->port.write);
    if (len > port_size)
    {
        Debug_LOG_ERROR("len (%zu) exceeds buffer size (%zu)", len, port_size);
        return OS_ERROR_GENERIC;
    }

    // copy data into the ctrl dataport
    memcpy(OS_Dataport_getBuf(ctrl_channel->port.write), buf, len);

    // tell the other side how much data we want to send and in which channel
    size_t sent_len = 0;
    OS_Error_t ret = ctrl_channel->func.write(ctrl_channel->id, len, &sent_len);
    if (ret != OS_SUCCESS)
    {
        Debug_LOG_ERROR("ChanMuxRpc_write() failed, error %d", ret);
        return OS_ERROR_GENERIC;
    }

    if (sent_len != len)
    {
        Debug_LOG_ERROR("ChanMuxRpc_write() sent len invalid: %zu", sent_len);
        return OS_ERROR_GENERIC;
    }

    return OS_SUCCESS;
}

//------------------------------------------------------------------------------
// read response from control channel. There is no point in returning the read
// length as full responses must be read or there is an error
static OS_Error_t
chanmux_ctrl_readBlocking(
    const ChanMux_ChannelOpsCtx_t *ctrl_channel,
    void *buf,
    size_t len)
{
    size_t port_size;

    port_size = OS_Dataport_getSize(ctrl_channel->port.read);
    if (len > port_size)
    {
        Debug_LOG_ERROR("len (%zu) exceeds buffer size (%zu)", len, port_size);
        return OS_ERROR_GENERIC;
    }

    uint8_t *buffer = (uint8_t *)buf;
    size_t lenRemaining = len;

    // we are a graceful receiver and allow a response in multiple chunks.
    while (lenRemaining > 0)
    {
        chanmux_channel_ctrl_wait();

        size_t chunk_read = 0;
        do
        {
            // this is a non-blocking read, so we are effectively polling here if
            // the response is not recevied in one chunk. That is bad actually if
            // we ever really have chunked data - so far this luckily never
            // happens ...
            OS_Error_t err = ctrl_channel->func.read(
                ctrl_channel->id,
                lenRemaining,
                &chunk_read);
            if (err != OS_SUCCESS)
            {
                Debug_LOG_ERROR("ChanMux_read() failed, error %d", err);
                return OS_ERROR_GENERIC;
            }

            assert(chunk_read <= lenRemaining);

            if (chunk_read > 0)
            {
                memcpy(buffer, OS_Dataport_getBuf(ctrl_channel->port.read),
                       chunk_read);

                buffer = &buffer[chunk_read];
                lenRemaining -= chunk_read;
            }
        } while (chunk_read > 0);
    }
    return OS_SUCCESS;
}

//------------------------------------------------------------------------------
static OS_Error_t
chanmux_nic_channel_ctrl_request_reply(
    const ChanMux_ChannelOpsCtx_t *channel_ctrl,
    uint8_t *cmd,
    size_t cmd_len,
    uint8_t *rsp,
    size_t rsp_len)
{
    OS_Error_t ret;

    ret = chanmux_ctrl_write(channel_ctrl, cmd, cmd_len);
    if (ret != OS_SUCCESS)
    {
        Debug_LOG_ERROR("Writing command for %d returned error %d", cmd[0], ret);
        return ret;
    }

    ret = chanmux_ctrl_readBlocking(channel_ctrl, rsp, rsp_len);
    if (ret != OS_SUCCESS)
    {
        Debug_LOG_ERROR("Reading response for %d returned error %d", cmd[0], ret);
        return ret;
    }

    return OS_SUCCESS;
}

//------------------------------------------------------------------------------
static OS_Error_t
chanmux_nic_channel_ctrl_cmd(
    const ChanMux_ChannelOpsCtx_t *channel_ctrl,
    uint8_t *cmd,
    size_t cmd_len,
    uint8_t *rsp,
    size_t rsp_len)
{
    OS_Error_t ret_mux;

    ret_mux = chanmux_channel_ctrl_mutex_lock();
    if (ret_mux != OS_SUCCESS)
    {
        Debug_LOG_ERROR("Failure getting lock, returned %d", ret_mux);
        return OS_ERROR_GENERIC;
    }

    OS_Error_t ret = chanmux_nic_channel_ctrl_request_reply(
        channel_ctrl,
        cmd,
        cmd_len,
        rsp,
        rsp_len);

    // we have to release the mutex even if the command failed
    ret_mux = chanmux_channel_ctrl_mutex_unlock();
    if (ret_mux != OS_SUCCESS)
    {
        Debug_LOG_ERROR("Failure releasing lock, returned %d", ret_mux);
    }

    return ret;
}

//------------------------------------------------------------------------------
OS_Error_t
chanmux_nic_channel_open(
    const ChanMux_ChannelOpsCtx_t *channel_ctrl,
    unsigned int chan_id_data)
{
    OS_Error_t ret;

    uint8_t cmd[2] = {CHANMUX_NIC_CMD_OPEN, chan_id_data};
    uint8_t rsp[2];
    ret = chanmux_nic_channel_ctrl_cmd(
        channel_ctrl,
        cmd,
        sizeof(cmd),
        rsp,
        sizeof(rsp));
    if (ret != OS_SUCCESS)
    {
        Debug_LOG_ERROR("Sending OPEN returned error %d", ret);
        return OS_ERROR_GENERIC;
    }
    uint8_t rsp_result = rsp[0];
    if (rsp_result != CHANMUX_NIC_RSP_OPEN)
    {
        Debug_LOG_ERROR("command OPEN failed, status code %u", rsp_result);
        return OS_ERROR_GENERIC;
    }

    return OS_SUCCESS;
}

//------------------------------------------------------------------------------
OS_Error_t
chanmux_nic_ctrl_get_mac(
    const ChanMux_ChannelOpsCtx_t *channel_ctrl,
    unsigned int chan_id_data,
    uint8_t *mac)
{
    OS_Error_t ret;

    uint8_t cmd[2] = {CHANMUX_NIC_CMD_GET_MAC, chan_id_data};
    // 8 byte response (2 byte status and 6 byte MAC)
    uint8_t rsp[8];
    ret = chanmux_nic_channel_ctrl_cmd(
        channel_ctrl,
        cmd,
        sizeof(cmd),
        rsp,
        sizeof(rsp));
    if (ret != OS_SUCCESS)
    {
        Debug_LOG_ERROR("Sending GET_MAC returned error %d", ret);
        return OS_ERROR_GENERIC;
    }
    uint8_t rsp_result = rsp[0];
    if (rsp_result != CHANMUX_NIC_RSP_GET_MAC)
    {
        Debug_LOG_ERROR("command GETMAC failed, status code %u", rsp_result);
        return OS_ERROR_GENERIC;
    }
    uint8_t rsp_ctx = rsp[1];
    if (rsp_ctx != 0)
    {
        Debug_LOG_ERROR("command GETMAC response ctx error, found %u", rsp_ctx);
        return OS_ERROR_GENERIC;
    }

    memcpy(mac, &rsp[2], MAC_SIZE);

    return OS_SUCCESS;
}

//------------------------------------------------------------------------------
OS_Error_t
chanmux_nic_ctrl_stopData(
    const ChanMux_ChannelOpsCtx_t *channel_ctrl,
    unsigned int chan_id_data)
{
    OS_Error_t ret;
    uint8_t cmd[2] = {CHANMUX_NIC_CMD_STOP_READ, chan_id_data};
    // 2 byte response
    uint8_t rsp[2];
    ret = chanmux_nic_channel_ctrl_cmd(
        channel_ctrl,
        cmd, sizeof(cmd),
        rsp,
        sizeof(rsp));
    if (ret != OS_SUCCESS)
    {
        Debug_LOG_ERROR("Sending STOP_READ returned error %d", ret);
        return OS_ERROR_GENERIC;
    }
    uint8_t rsp_result = rsp[0];
    if (rsp_result != CHANMUX_NIC_RSP_STOP_READ)
    {
        Debug_LOG_ERROR("command STOP_READ failed, status code %u", rsp_result);
        return OS_ERROR_GENERIC;
    }

    return OS_SUCCESS;
}

//------------------------------------------------------------------------------
OS_Error_t
chanmux_nic_ctrl_startData(
    const ChanMux_ChannelOpsCtx_t *channel_ctrl,
    unsigned int chan_id_data)
{
    OS_Error_t ret;
    uint8_t cmd[2] = {CHANMUX_NIC_CMD_START_READ, chan_id_data};
    // 2 byte response
    uint8_t rsp[2];
    ret = chanmux_nic_channel_ctrl_cmd(
        channel_ctrl,
        cmd,
        sizeof(cmd),
        rsp,
        sizeof(rsp));
    if (ret != OS_SUCCESS)
    {
        Debug_LOG_ERROR("Sending START_READ returned error %d", ret);
        return OS_ERROR_GENERIC;
    }
    uint8_t rsp_result = rsp[0];
    if (rsp_result != CHANMUX_NIC_RSP_START_READ)
    {
        Debug_LOG_ERROR("command START_READ failed, status code %u", rsp_result);
        return OS_ERROR_GENERIC;
    }

    return OS_SUCCESS;
}
