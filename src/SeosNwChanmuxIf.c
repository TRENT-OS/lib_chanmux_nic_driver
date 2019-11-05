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
#include "Seos_pico_dev_chan_mux.h"
#include "Seos_Driver_Config.h"

extern Seos_nw_camkes_info* pnw_camkes;
#define MAC_SIZE 6

size_t
SeosNwChanmux_chanWriteSyncCtrl(
    const void*   buf,
    size_t        len)
{
    size_t written = 0;
    void* ctrlwrbuf = pnw_camkes->pportsglu->ChanMuxCtrlPort;
    unsigned int chan;
    seos_driver_config* pTapdrv = Seos_NwDriver_getconfig();

    chan = pTapdrv->chan_ctrl;

    len = len < PAGE_SIZE ? len : PAGE_SIZE;
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
        len = len < PAGE_SIZE ? len : PAGE_SIZE;
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

seos_err_t
SeosNwChanmux_get_mac(
    uint8_t*  mac)
{

    char command[2];
    char response[8];

    Debug_LOG_TRACE("%s", __FUNCTION__);

    seos_driver_config* pTapdrv = Seos_NwDriver_getconfig();


    /* First we send the OPEN and then the GETMAC cmd. This is for proxy which first needs to Open/activate the socket */
    command[0] = NW_CTRL_CMD_OPEN;
    command[1] = pTapdrv->chan_data;

    unsigned result = SeosNwChanmux_chanWriteSyncCtrl(
                          command,
                          sizeof(command));

    if (result != sizeof(command))
    {
        Debug_LOG_ERROR("%s() could not write OPEN cmd,result=%d", __FUNCTION__,
                        result);
        return SEOS_ERROR_GENERIC;
    }

    /* Read back 2 bytes for OPEN CNF response, is a blocking call. Only 2 bytes required here, for mac it is 8 bytes */

    size_t read = SeosNwChanmux_chanReadBlocking(pTapdrv->chan_ctrl, response, 2);

    if (read != 2)
    {
        Debug_LOG_ERROR("%s() could not read OPEN CNF response, result=%d",
                        __FUNCTION__, result);
        return SEOS_ERROR_GENERIC;
    }
    if (response[0] == NW_CTRL_CMD_OPEN_CNF)
    {
        // now start reading the mac

        command[0] = NW_CTRL_CMD_GETMAC;
        command[1] = pTapdrv->chan_data;   // this is required due to proxy

        Debug_LOG_INFO("Sending Get mac cmd:");

        unsigned result = SeosNwChanmux_chanWriteSyncCtrl(
                              command,
                              sizeof(command));
        if (result != sizeof(command))
        {
            Debug_LOG_ERROR("%s() result = %d", __FUNCTION__, result);
            return SEOS_ERROR_GENERIC;
        }
        size_t read = SeosNwChanmux_chanReadBlocking(
                          pTapdrv->chan_ctrl, response,
                          sizeof(response));

        if (read != sizeof(response))
        {
            Debug_LOG_ERROR("%s() read=%d", __FUNCTION__, result);
            return SEOS_ERROR_GENERIC;
        }
        /* response[1] must contain 0 as this is set by proxy when success */
        if ((NW_CTRL_CMD_GETMAC_CNF == response[0]) && (response[1] == 0))
        {
            memcpy(mac, &response[2], MAC_SIZE);
            Debug_LOG_INFO("%s() mac received:%02x:%02x:%02x:%02x:%02x:%02x", __FUNCTION__,
                           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5] );
            const uint8_t empty[MAC_SIZE] = {0};
            if (memcmp(mac, empty, MAC_SIZE) == 0)
            {
                Debug_LOG_ERROR("%s() Empty mac received 00:00:00:00:00:00", __FUNCTION__);
                return SEOS_ERROR_GENERIC;    // recvd six 0's from proxy tap for mac. This is not good. Check for tap on proxy !!
            }
        }

    }
    return SEOS_SUCCESS;
}
