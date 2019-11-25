/*
 *  SEOS Network Stack ChanMux wrapper driver Interface. This interacts with Chanmux for write/read
 *
 *
 *  Copyright (C) 2019, Hensoldt Cyber GmbH
 *
 */

#include "SeosNwChanmuxIf.h"
#include <string.h>
#include "LibDebug/Debug.h"
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include "seos_ethernet.h"
#include "seos_chanmux_ethernet.h"

static size_t
SeosNwChanmux_chanWriteSyncCtrl(
    const ChanMux_channelCtx_t*  ctrl_channel,
    const void*                  buf,
    size_t                       len)
{
    len = (len < PAGE_SIZE) ? len : PAGE_SIZE;
    // copy in the ctrl dataport
    memcpy(ctrl_channel->data_port, buf, len);

    // tell the other side how much data we want to send and in which channel
    size_t written = 0;
    seos_err_t err = ChanMux_write(ctrl_channel->id, len, &written);
    if (err != SEOS_SUCCESS)
    {
        Debug_LOG_ERROR("%s(),Error in writing, err= %d", __FUNCTION__, err);
    }

    return written;
}


//------------------------------------------------------------------------------
size_t
SeosNwChanmux_chanWriteSyncData(
    const ChanMux_channelCtx_t*  channel,
    const void*                  buf,
    size_t                       len)
{
    size_t written = 0;
    size_t remain_len = len;
    size_t w_size = 0;
    void* datawrbuf = channel->data_port;

    while (len > 0)   // loop to send all data if > PAGE_SIZE = 4096
    {
        len = (len < PAGE_SIZE) ? len : PAGE_SIZE;
        // copy in the normal dataport
        memcpy(datawrbuf, buf + w_size, len);
        // tell the other side how much data we want to send and in which channel
        seos_err_t err = ChanMux_write(channel->id, len, &written);
        if (err != SEOS_SUCCESS)
        {
            Debug_LOG_ERROR("%s(),Error in writing, err= %d", __FUNCTION__, err);
            break;
        }
        w_size = +written;
        len = remain_len - w_size;
    }

    return w_size;
}


//------------------------------------------------------------------------------
size_t
SeosNwChanmux_chanRead(
    const  ChanMux_channelCtx_t*  channel,
    void*                         buf,
    size_t                        len)
{
    size_t read = 0;
    seos_err_t err = ChanMux_read(channel->id, len, &read);
    if (err != SEOS_SUCCESS)
    {
        Debug_LOG_ERROR("%s(),Error in reading, err= %d", __FUNCTION__, err);
        return read;
    }

    if (read > 0)
    {
        memcpy(buf, channel->data_port, read);
    }

    return read;
}


//------------------------------------------------------------------------------
static size_t
SeosNwChanmux_chanReadBlocking (
    const  ChanMux_channelCtx_t*  channel,
    void*                         buf,
    size_t                        len)
{
    size_t lenRead = 0;
    uint8_t* buffer = (uint8_t*)buf;

    while (lenRead < len)
    {
        // Non-blocking read.
        size_t read = SeosNwChanmux_chanRead(channel,
                                             &buffer[lenRead],
                                             len - lenRead);
        if (0 == read)
        {
            ; // do nothing
        }
        else
        {
            lenRead += read;
        }
    }
    return lenRead;
}


//------------------------------------------------------------------------------
seos_err_t
SeosNwChanmux_open(
    const  ChanMux_channelCtx_t*  channel_ctrl,
    unsigned int                  chan_id_data)
{
    size_t ret;

    Debug_LOG_TRACE("%s", __FUNCTION__);

    uint8_t cmd[2] = { NW_CTRL_CMD_OPEN, chan_id_data };
    ret = SeosNwChanmux_chanWriteSyncCtrl(channel_ctrl, cmd, sizeof(cmd));
    if (ret != sizeof(cmd))
    {
        Debug_LOG_ERROR("%s() send command OPEN failed, result=%zu",
                        __FUNCTION__, ret);
        return SEOS_ERROR_GENERIC;
    }

    // 2 byte response
    uint8_t rsp[2];
    ret = SeosNwChanmux_chanReadBlocking(channel_ctrl, rsp, sizeof(rsp));
    if (ret != sizeof(rsp))
    {
        Debug_LOG_ERROR("%s() read response for OPEN failed, result=%zu",
                        __FUNCTION__, ret);
        return SEOS_ERROR_GENERIC;
    }

    uint8_t rsp_result = rsp[0];
    if (rsp_result != NW_CTRL_CMD_OPEN_CNF)
    {
        Debug_LOG_ERROR("%s() cmd OPEN failed, return code %u",
                        __FUNCTION__, rsp_result);
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
    size_t ret;

    Debug_LOG_TRACE("%s", __FUNCTION__);

    uint8_t cmd[2] = { NW_CTRL_CMD_GETMAC, chan_id_data };
    ret = SeosNwChanmux_chanWriteSyncCtrl(channel_ctrl, cmd, sizeof(cmd));
    if (ret != sizeof(cmd))
    {
        Debug_LOG_ERROR("%s() send command GETMAC failed, result=%zu",
                        __FUNCTION__, ret);
        return SEOS_ERROR_GENERIC;
    }

    // 8 byte response (2 byte status and 6 byte MAC)
    uint8_t rsp[8];
    ret = SeosNwChanmux_chanReadBlocking(channel_ctrl, rsp, sizeof(rsp));
    if (ret != sizeof(rsp))
    {
        Debug_LOG_ERROR("%s() read response for GETMAC failed, result=%zu",
                        __FUNCTION__, ret);

        return SEOS_ERROR_GENERIC;
    }

    uint8_t rsp_result = rsp[0];
    if (rsp_result != NW_CTRL_CMD_GETMAC_CNF)
    {
        Debug_LOG_ERROR("%s() cmd GETMAC response error, return code %u",
                        __FUNCTION__, rsp_result);
        return SEOS_ERROR_GENERIC;
    }

    uint8_t rsp_ctx = rsp[1];
    if (rsp_ctx != 0)
    {
        Debug_LOG_ERROR("%s() cmd GETMAC response ctx error, found %u",
                        __FUNCTION__, rsp_ctx);
        return SEOS_ERROR_GENERIC;
    }

    memcpy(mac, &rsp[2], MAC_SIZE);

    return SEOS_SUCCESS;
}
