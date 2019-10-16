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

extern Seos_nw_camkes_info* pnw_camkes;

/* Function to send ctrl cmds.
 *
 * Ctrl cmds will always fit within PAGE_SIZE.
 *
 */
size_t
SeosNwChanmux_chanWriteSyncCtrl(
    const void*   buf,
    size_t        len)
{
    size_t written = 0;
    void* ctrlwrbuf;
    unsigned int chan;


    ctrlwrbuf = pnw_camkes->pportsglu->ChanMuxCtrlPort;

    if (pnw_camkes->instanceID == SEOS_NWSTACK_AS_CLIENT)
    {
        chan = CHANNEL_NW_STACK_CTRL;
    }
    else
    {
        chan = CHANNEL_NW_STACK_CTRL_2;
    }

    len = len < PAGE_SIZE ? len : PAGE_SIZE;
    // copy in the ctrl dataport
    memcpy(ctrlwrbuf, buf, len);

    // tell the other side how much data we want to send and in which channel
    seos_err_t err = ChanMux_write(chan, len, &written);
    Debug_ASSERT_PRINTFLN(!err, "seos err %d", err);

    return written;
}

/* Function to send NW data.
 *
 * Data may not fit within PAGE_SIZE. Hence loop if required until all data is sent
 * w_size finally contains how many actually bytes were written.
 */
size_t
SeosNwChanmux_chanWriteSyncData(
    const void*   buf,
    size_t        len)
{
    size_t written = 0;
    size_t remain_len = len;
    size_t w_size = 0;
    void* datawrbuf;
    unsigned int chan;

    datawrbuf = pnw_camkes->pportsglu->ChanMuxDataPort;
    if (pnw_camkes->instanceID == SEOS_NWSTACK_AS_CLIENT)
    {

        chan = CHANNEL_NW_STACK_DATA;

    }
    else
    {
        chan = CHANNEL_NW_STACK_DATA_2;
    }

    while (len > 0)   // loop to send all data if > PAGE_SIZE = 4096
    {
        len = len < PAGE_SIZE ? len : PAGE_SIZE;
        // copy in the normal dataport
        memcpy(datawrbuf, buf + w_size, len);
        // tell the other side how much data we want to send and in which channel
        seos_err_t err = ChanMux_write(chan, len, &written);
        Debug_ASSERT_PRINTFLN(!err, "seos err %d", err);
        w_size = +written;
        len = remain_len - w_size;
        if (err < 0)
        {
            Debug_LOG_INFO("error in writing, err= %d\n", err);
            break;
        }
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
    Debug_ASSERT_PRINTFLN(!err, "seos err %d", err);

    void* chanctrlport = pnw_camkes->pportsglu->ChanMuxCtrlPort;
    void* chandataport = pnw_camkes->pportsglu->ChanMuxDataPort;

    if (read)
    {
        if (pnw_camkes->instanceID == SEOS_NWSTACK_AS_CLIENT)
        {
            // Debug_ASSERT(read <= len);
            if (chan == CHANNEL_NW_STACK_DATA)
            {
                memcpy(buf, chandataport, read);
            }
            else   // it is control data
            {
                memcpy(buf, chanctrlport, read);
            }
        }

        else
        {
            if (chan == CHANNEL_NW_STACK_DATA_2)
            {
                memcpy(buf, chandataport, read);
            }
            else   // it is control data
            {
                memcpy(buf, chanctrlport, read);
            }
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


//------------------------------------------------------------------------------
// called by PicoTCP

/*
 *   Function: NwStack_write_data()
 *
 *   Return values:
 *   written bytes = success
 *   0 = failure
 */
int
SeosNwChanmux_write_data(
    void*   buffer,
    size_t  len)
{
    int written = SeosNwChanmux_chanWriteSyncData(
                      buffer,
                      len);
    return written;
}

//------------------------------------------------------------------------------
/*
 *   Function: NwStack_read_data()
 *
 *   Return values:
 *   read bytes = success
 *   0 = nothing to read
 */
// called by PicoTCP
int
SeosNwChanmux_read_data(
    void*   buffer,
    size_t  len)
{
    unsigned int chan;
    if (pnw_camkes->instanceID == SEOS_NWSTACK_AS_CLIENT)
    {
        chan = CHANNEL_NW_STACK_DATA;
    }
    else
    {
        chan = CHANNEL_NW_STACK_DATA_2;
    }

    return (SeosNwChanmux_chanRead(chan, buffer, len));
}


/*
 *   Function: NwChanmux_get_mac()
 *
 *   Return values:
 *   0 and gets the mac address filled
 */
//------------------------------------------------------------------------------
// called by PicoTCP
int
SeosNwChanmux_get_mac(
    char*     name,
    uint8_t*  mac)
{

    char command[2];
    char response[8];
    uint8_t datachan, ctrlchan;

    Debug_LOG_INFO("%s\n", __FUNCTION__);
    if (pnw_camkes->instanceID == SEOS_NWSTACK_AS_CLIENT)
    {
        datachan = CHANNEL_NW_STACK_DATA;
        ctrlchan = CHANNEL_NW_STACK_CTRL;
    }
    else
    {
        datachan = CHANNEL_NW_STACK_DATA_2;
        ctrlchan = CHANNEL_NW_STACK_CTRL_2;
    }

    /* First we send the OPEN and then the GETMAC cmd. This is for proxy which first needs to Open/activate the socket */
    command[0] = NW_CTRL_CMD_OPEN;
    command[1] = datachan;

    unsigned result = SeosNwChanmux_chanWriteSyncCtrl(
                          command,
                          sizeof(command));

    if (result != sizeof(command))
    {
        Debug_LOG_INFO("%s could not write OPEN cmd , result = %d\n", __FUNCTION__,
                       result);
        return -1;
    }

    /* Read back 2 bytes for OPEN CNF response, is a blocking call. Only 2 bytes required here, for mac it is 8 bytes */

    size_t read = SeosNwChanmux_chanReadBlocking(ctrlchan, response, 2);

    if (read != 2)
    {
        Debug_LOG_INFO("%s could not read OPEN CNF response, result = %d\n",
                       __FUNCTION__, result);
        return -1;
    }
    if (response[0] == NW_CTRL_CMD_OPEN_CNF)
    {
        // now start reading the mac

        command[0] = NW_CTRL_CMD_GETMAC;
        command[1] = datachan;   // this is required due to proxy

        Debug_LOG_INFO("Sending Get mac cmd: \n");

        unsigned result = SeosNwChanmux_chanWriteSyncCtrl(
                              command,
                              sizeof(command));
        if (result != sizeof(command))
        {
            Debug_LOG_INFO("%s result = %d\n", __FUNCTION__, result);
            return -1;
        }
        size_t read = SeosNwChanmux_chanReadBlocking(
                          ctrlchan, response,
                          sizeof(response));

        if (read != sizeof(response))
        {
            Debug_LOG_INFO("%s read = %d\n", __FUNCTION__, result);
            return -1;
        }
        /* response[1] must contain 0 as this is set by proxy when success */
        if ((NW_CTRL_CMD_GETMAC_CNF == response[0]) && (response[1] == 0))
        {
            memcpy(mac, &response[2], 6);
            Debug_LOG_INFO ( "exit %s mac received =%x %x %x %x %x %x \n", __FUNCTION__,
                             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5] );
            const  uint8_t empty[6] = { 0, };
            if (memcmp(mac, empty, 6) == 0)
            {
                return -1;    // recvd six 0's from proxy tap for mac. This is not good. Check for tap on proxy !!
            }
        }

    }
    return 0;
}


