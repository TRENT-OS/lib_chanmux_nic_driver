/*
 *  SEOS Network Stack ChanMux wrapper driver Interface. This interacts with Chanmux for write/read
 *
 *
 *  Copyright (C) 2019, Hensoldt Cyber GmbH
 *
 */
#include "SeosNwStack.h"
#include "SeosNwCommon.h"
#include "SeosNwChanmuxIf.h"
#include <string.h>
#include "LibDebug/Debug.h"
#include <stdint.h>
#include <stddef.h>
#include "seos_ethernet.h"
#include "Seos_pico_dev_chan_mux.h"
#include "Seos_Driver_Config.h"

extern Seos_nw_camkes_info* pnw_camkes;

static size_t
SeosNwChanmux_chanWriteSyncCtrl(
    const void*   buf,
    size_t        len)
{
    size_t written = 0;
    void* ctrlwrbuf = pnw_camkes->pportsglu->ChanMuxCtrlPort;
    unsigned int chan;
    seos_driver_config* pTapdrv = Seos_NwDriver_getconfig();

    chan = pTapdrv->chan_ctrl;

    len = (len < PAGE_SIZE) ? len : PAGE_SIZE;
    // copy in the ctrl dataport
    memcpy(ctrlwrbuf, buf, len);

    // tell the other side how much data we want to send and in which channel
    seos_err_t err = ChanMux_write(chan, len, &written);
    if (err != SEOS_SUCCESS)
    {
        Debug_LOG_ERROR("%s(),Error in writing, err= %d", __FUNCTION__, err);
    }

    return written;
}

size_t
SeosNwChanmux_chanWriteSyncData(
    const void*   buf,
    size_t        len)
{
    size_t written = 0;
    size_t remain_len = len;
    size_t w_size = 0;
    void* datawrbuf = pnw_camkes->pportsglu->ChanMuxDataPort;
    unsigned int chan;

    seos_driver_config* pTapdrv = Seos_NwDriver_getconfig();

    chan = pTapdrv->chan_data;

    while (len > 0)   // loop to send all data if > PAGE_SIZE = 4096
    {
        len = (len < PAGE_SIZE) ? len : PAGE_SIZE;
        // copy in the normal dataport
        memcpy(datawrbuf, buf + w_size, len);
        // tell the other side how much data we want to send and in which channel
        seos_err_t err = ChanMux_write(chan, len, &written);
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

size_t
SeosNwChanmux_chanRead(
    unsigned int  chan,
    void*         buf,
    size_t        len)
{
    size_t read = 0;
    seos_err_t err = ChanMux_read(chan, len, &read);

    if (err != SEOS_SUCCESS)
    {
        Debug_LOG_ERROR("%s(),Error in reading, err= %d", __FUNCTION__, err);
        return read;
    }
    void* chanctrlport = pnw_camkes->pportsglu->ChanMuxCtrlPort;
    void* chandataport = pnw_camkes->pportsglu->ChanMuxDataPort;
    seos_driver_config* pTapdrv = Seos_NwDriver_getconfig();

    if (read)
    {
        if (chan == pTapdrv->chan_data) // it is normal data
        {
            memcpy(buf, chandataport, read);
        }
        else if (chan == pTapdrv->chan_ctrl) // it is control data
        {
            memcpy(buf, chanctrlport, read);
        }
        else
        {
            Debug_LOG_ERROR("%s(): Wrong channel no passed", __FUNCTION__);
        }

    }
    return read;
}

size_t
SeosNwChanmux_chanReadBlocking (
    unsigned int  chan,
    char*         buf,
    size_t        len)
{
    size_t lenRead = 0;

    while (lenRead < len)
    {
        // Non-blocking read.
        size_t read = SeosNwChanmux_chanRead(chan,
                                             &buf[lenRead],
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

size_t
SeosNwChanmux_write_data(
    void*   buffer,
    size_t  len)
{
    int written = SeosNwChanmux_chanWriteSyncData(
                      buffer,
                      len);
    return written;
}

size_t
SeosNwChanmux_read_data(
    void*   buffer,
    size_t  len)
{
    seos_driver_config* pTapdrv = Seos_NwDriver_getconfig();

    unsigned int chan = pTapdrv->chan_data;

    return (SeosNwChanmux_chanRead(chan, buffer, len));
}


//------------------------------------------------------------------------------
seos_err_t
SeosNwChanmux_open(
    unsigned int chn_ctrl,
    unsigned int chn_data)
{
    size_t ret;

    Debug_LOG_TRACE("%s", __FUNCTION__);

    char cmd[2] = { NW_CTRL_CMD_OPEN, chn_data };
    ret = SeosNwChanmux_chanWriteSyncCtrl(cmd, sizeof(cmd));
    if (ret != sizeof(cmd))
    {
        Debug_LOG_ERROR("%s() send command OPEN failed, result=%zu",
                        __FUNCTION__, ret);
        return SEOS_ERROR_GENERIC;
    }

    // 2 byte response
    char rsp[2];
    ret = SeosNwChanmux_chanReadBlocking(chn_ctrl, rsp, sizeof(rsp));
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
    unsigned int chn_ctrl,
    unsigned int chn_data,
    uint8_t*  mac)
{
    size_t ret;

    Debug_LOG_TRACE("%s", __FUNCTION__);

    char cmd[2] = { NW_CTRL_CMD_GETMAC, chn_data };
    ret = SeosNwChanmux_chanWriteSyncCtrl(cmd, sizeof(cmd));
    if (ret != sizeof(cmd))
    {
        Debug_LOG_ERROR("%s() send command GETMAC failed, result=%zu",
                        __FUNCTION__, ret);
        return SEOS_ERROR_GENERIC;
    }

    // 8 byte response (2 byte status and 6 byte MAC)
    char rsp[8];
    ret = SeosNwChanmux_chanReadBlocking(chn_ctrl, rsp, sizeof(rsp));
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
