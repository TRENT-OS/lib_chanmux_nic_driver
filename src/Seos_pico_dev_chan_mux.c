/*
 *   Pico TCP CHAN Mux driver. This driver interacts with Picotcp for up/downlink and SeosNwChanmuxIf
 *
 *  Copyright (C) 2019, Hensoldt Cyber GmbH
 *
 */

#include "pico_device.h"
#include "LibDebug/Debug.h"
#include "seos_ethernet.h"
#include "Seos_pico_dev_chan_mux.h"
#include "pico_stack.h"
#include "SeosNwStack.h"
#include "SeosNwChanmuxIf.h"
#include "Seos_Driver_Config.h"
#include <limits.h>


struct pico_device_chan_mux_tap
{
    struct pico_device dev;
};

extern Seos_nw_camkes_info* pnw_camkes;

//------------------------------------------------------------------------------
// callback fuction for PicoTCP
static int
pico_chan_mux_tap_send(
    struct pico_device*  dev __attribute__((unused)),
    void*                buf,
    int                  len)
{
    seos_driver_config* pTapdrv = Seos_NwDriver_getconfig();
    unsigned int chan = pTapdrv->chan_data;
    void* data_port = pnw_camkes->pportsglu->ChanMuxDataPort;

    uint8_t* buffer = (uint8_t*)buf;
    size_t remain_len = len;

    while (remain_len > 0)   // loop to send all data if > PAGE_SIZE = 4096
    {
        size_t len_chunk = (len < PAGE_SIZE) ? len : PAGE_SIZE;
        // copy in the normal dataport
        memcpy(data_port, buffer, len_chunk);
        // tell the other side how much data is in the channel
        size_t len_written = 0;
        seos_err_t err = ChanMux_write(chan, len_chunk, &len_written);
        if (err != SEOS_SUCCESS)
        {
            Debug_LOG_ERROR("ChanMux_write() failed, error %d", err);
            return -1;
        }

        assert(len_written <= len_chunk);

        buffer = &buffer[len_written];
        remain_len -= len_written;
    }

    return len;
}


//------------------------------------------------------------------------------
// callback fuction for PicoTCP (part of stack tick)
static int
pico_chan_mux_tap_poll(
    struct pico_device*  dev,
    int                  loop_score)
{
    seos_driver_config* pTapdrv = Seos_NwDriver_getconfig();
    unsigned int chan = pTapdrv->chan_data;
    void* data_port = pnw_camkes->pportsglu->ChanMuxDataPort;

    // loop_score indicates max number of frames that PicoTCP can read in this
    // call. Since we don't preserve the frames, we can't really do anything
    // here besides passin all data from the buffer to PicoTCP.
    if (loop_score > 0)
    {
        size_t len_read = 0;
        seos_err_t err = ChanMux_read(chan, PAGE_SIZE, &len_read);
        if (err != SEOS_SUCCESS)
        {
            Debug_LOG_ERROR("ChanMux_read() failed, error %d", err);
            return loop_score;
        }

        if (len_read > 0)
        {
            loop_score--;
            // send frame to PicoTCP
            pico_stack_recv(dev, data_port, (uint32_t)len_read);
        }
    }
    return loop_score;
}


//------------------------------------------------------------------------------
// callback fuction or PicoTCP
void pico_chan_mux_tap_destroy(
    struct pico_device* dev)
{
    // Call free to destroy the device
    PICO_FREE(dev);
}


//------------------------------------------------------------------------------
struct pico_device* pico_chan_mux_tap_create(void)
{
    struct pico_device_chan_mux_tap* chan_mux_tap = PICO_ZALLOC(sizeof (
            struct pico_device_chan_mux_tap));
    uint8_t mac[MAC_SIZE] = {0};
    seos_err_t err;
    const char* drv_name = "tapdriver";

    if (!chan_mux_tap)
    {
        Debug_LOG_ERROR("memory alloc failure for tap device");
        return NULL;
    }

    seos_driver_config* pTapdrv = Seos_NwDriver_getconfig();
    if (!pTapdrv)
    {
        Debug_LOG_ERROR("invalid driver configuration");
        pico_chan_mux_tap_destroy((struct pico_device*)chan_mux_tap);
        return NULL;
    }

    const ChanMux_channelCtx_t channel_ctrl =
    {
        .data_port  = pnw_camkes->pportsglu->ChanMuxCtrlPort,
        .id         = pTapdrv->chan_ctrl
    };

    unsigned int chan_id_data = pTapdrv->chan_data;


    err = SeosNwChanmux_open(&channel_ctrl, chan_id_data);
    if (err != SEOS_SUCCESS)
    {
        Debug_LOG_ERROR("SeosNwChanmux_open() failed, error %d", err);
        pico_chan_mux_tap_destroy((struct pico_device*)chan_mux_tap);
        return NULL;
    }

    // ChanMUX simulates an etehrnet device, get the MAC address from it
    err = SeosNwChanmux_get_mac(&channel_ctrl, chan_id_data, mac);
    if (err != SEOS_SUCCESS)
    {
        Debug_LOG_ERROR("SeosNwChanmux_get_mac() failed, error %d", err);

        // ToDo: close SeosNwChanmux chnnel

        pico_chan_mux_tap_destroy((struct pico_device*)chan_mux_tap);
        return NULL;
    }

    // sanity check, the MAC address can't be all zero.
    const uint8_t empty_mac[MAC_SIZE] = {0};
    if (memcmp(mac, empty_mac, MAC_SIZE) == 0)
    {
        Debug_LOG_ERROR("MAC with all zeros is not allowed");
        pico_chan_mux_tap_destroy((struct pico_device*)chan_mux_tap);
        return NULL;
    }

    Debug_LOG_INFO("MAC is %02x:%02x:%02x:%02x:%02x:%02x",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5] );

    // ToDo: the proxy is supposed to emulate a network interface with a proper
    //       MAC address. Currently, with the TUN/TAp device it connects to in
    //       Linux, things only work if we use a differen MAC address here.
    //       That's why we simpli increment it by one. However, this is
    //       something the proxy should do, as we want to be agnostic of any
    //        Linux TUN/TAP spüecific things here.
    mac[MAC_SIZE - 1]++;

    int ret = pico_device_init((struct pico_device*)chan_mux_tap, drv_name, mac);
    if (0 != ret)
    {
        Debug_LOG_ERROR("pico_device_init() failed, error %d", ret);
        pico_chan_mux_tap_destroy((struct pico_device*)chan_mux_tap);
        return NULL;
    }

    chan_mux_tap->dev.send      = pico_chan_mux_tap_send;
    chan_mux_tap->dev.poll      = pico_chan_mux_tap_poll;
    chan_mux_tap->dev.destroy   = pico_chan_mux_tap_destroy;

    Debug_LOG_INFO("device '%s' created", drv_name);

    return (struct pico_device*)chan_mux_tap;
}
