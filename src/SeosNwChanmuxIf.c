/*
 *  SEOS Network Stack ChanMux wrapper driver Interface. This interacts with Chanmux for write/read
 *
 *
 *  Copyright (C) 2019, Hensoldt Cyber GmbH
 *
 */

#include "SeosNwChanmuxIf.h"
#include "LibDebug/Debug.h"
#include "seos_ethernet.h"
#include "seos_chanmux_ethernet.h"
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <string.h>


//------------------------------------------------------------------------------
// write command into control channel. There is no point in returning the
// written bytes as full command must be send or there is an error
static seos_err_t
chanmux_ctrl_write(
    const ChanMux_channelCtx_t*  ctrl_channel,
    const void*                  buf,
    size_t                      len)
{
    if (len > PAGE_SIZE)
    {
        Debug_LOG_ERROR("len (%zu) bigger than max len (%d)", len, PAGE_SIZE);
        return SEOS_ERROR_GENERIC;
    }

    // copy in the ctrl dataport
    memcpy(ctrl_channel->data_port, buf, len);

    // tell the other side how much data we want to send and in which channel
    size_t sent_len = 0;
    seos_err_t ret = ChanMux_write(ctrl_channel->id, len, &sent_len);
    if (ret != SEOS_SUCCESS)
    {
        Debug_LOG_ERROR("ChanMux_write() failed, error %d", ret);
        return SEOS_ERROR_GENERIC;
    }

    if (sent_len != len)
    {
        Debug_LOG_ERROR("ChanMux_write() sent len invalid: %zu", sent_len);
        return SEOS_ERROR_GENERIC;
    }

    return SEOS_SUCCESS;
}


//------------------------------------------------------------------------------
// read response from control channel. There is no point in returning the read
// length as full responses must be read or there is an error
static seos_err_t
chanmux_ctrl_readBlocking(
    const ChanMux_channelCtx_t*  ctrl_channel,
    void*                        buf,
    size_t                       len)
{
    if (len > PAGE_SIZE)
    {
        Debug_LOG_ERROR("len (%zu) bigger than max len (%d)", len, PAGE_SIZE);
        return SEOS_ERROR_GENERIC;
    }

    unsigned int chan_id = ctrl_channel->id;
    void* data_port = ctrl_channel->data_port;

    uint8_t* buffer = (uint8_t*)buf;
    size_t lenRemaining = len;

    // we are a graceful receiver and allow a response in multiple chunks.
    while (lenRemaining > 0)
    {
        size_t chunk_read = 0;

        // this is a non-blocking read, so we are effectively polling here if
        // the response is not recevied in one chunk. That is bad actually if
        // we ever really have chunked data - so far this luckily never
        // happens ...
        seos_err_t err = ChanMux_read(chan_id, lenRemaining, &chunk_read);
        if (err != SEOS_SUCCESS)
        {
            Debug_LOG_ERROR("ChanMux_read() failed, error %d", err);
            return SEOS_ERROR_GENERIC;
        }

        assert(chunk_read <= lenRemaining);

        if (chunk_read > 0)
        {
            memcpy(buffer, data_port, chunk_read);

            buffer = &buffer[chunk_read];
            lenRemaining -= chunk_read;

        }
    }

    return SEOS_SUCCESS;
}


//------------------------------------------------------------------------------
seos_err_t
SeosNwChanmux_open(
    const  ChanMux_channelCtx_t*  channel_ctrl,
    unsigned int                  chan_id_data)
{
    Debug_LOG_TRACE("%s", __FUNCTION__);
    seos_err_t ret;

    uint8_t cmd[2] = { NW_CTRL_CMD_OPEN, chan_id_data };
    ret = chanmux_ctrl_write(channel_ctrl, cmd, sizeof(cmd));
    if (ret != SEOS_SUCCESS)
    {
        Debug_LOG_ERROR("sending OPEN returned error %d", ret);
        return SEOS_ERROR_GENERIC;
    }

    // 2 byte response
    uint8_t rsp[2];
    ret = chanmux_ctrl_readBlocking(channel_ctrl, rsp, sizeof(rsp));
    if (ret != SEOS_SUCCESS)
    {
        Debug_LOG_ERROR("reading response for OPEN returned error %d", ret);
        return SEOS_ERROR_GENERIC;
    }

    uint8_t rsp_result = rsp[0];
    if (rsp_result != NW_CTRL_CMD_OPEN_CNF)
    {
        Debug_LOG_ERROR("command OPEN failed, status code %u", rsp_result);
        return SEOS_ERROR_GENERIC;
    }

    return SEOS_SUCCESS;
}


//------------------------------------------------------------------------------
seos_err_t
SeosNwChanmux_get_mac(
    const ChanMux_channelCtx_t*  channel_ctrl,
    unsigned int                 chan_id_data,
    uint8_t*                     mac)
{
    Debug_LOG_TRACE("%s", __FUNCTION__);
    seos_err_t ret;

    uint8_t cmd[2] = { NW_CTRL_CMD_GETMAC, chan_id_data };
    ret = chanmux_ctrl_write(channel_ctrl, cmd, sizeof(cmd));
    if (ret != SEOS_SUCCESS)
    {
        Debug_LOG_ERROR("sending GETMAC returned error %d", ret);
        return SEOS_ERROR_GENERIC;
    }

    // 8 byte response (2 byte status and 6 byte MAC)
    uint8_t rsp[8];
    ret = chanmux_ctrl_readBlocking(channel_ctrl, rsp, sizeof(rsp));
    if (ret != SEOS_SUCCESS)
    {
        Debug_LOG_ERROR("reading response for GETMAC returned error %d", ret);
        return SEOS_ERROR_GENERIC;
    }

    uint8_t rsp_result = rsp[0];
    if (rsp_result != NW_CTRL_CMD_GETMAC_CNF)
    {
        Debug_LOG_ERROR("command GETMAC failed, status code %u", rsp_result);
        return SEOS_ERROR_GENERIC;
    }

    uint8_t rsp_ctx = rsp[1];
    if (rsp_ctx != 0)
    {
        Debug_LOG_ERROR("command GETMAC response ctx error, found %u", rsp_ctx);
        return SEOS_ERROR_GENERIC;
    }

    memcpy(mac, &rsp[2], MAC_SIZE);

    return SEOS_SUCCESS;
}
