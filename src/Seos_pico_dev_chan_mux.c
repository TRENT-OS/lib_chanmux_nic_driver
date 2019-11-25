/*
 *   Pico TCP CHAN Mux driver. This driver interacts with Picotcp for up/downlink and SeosNwChanmuxIf
 *
 *  Copyright (C) 2019, Hensoldt Cyber GmbH
 *
 */

#include <fcntl.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include "pico_device.h"
#include "LibDebug/Debug.h"
#include "seos_ethernet.h"
#include "Seos_pico_dev_chan_mux.h"
#include "pico_stack.h"
#include "SeosNwStack.h"
#include "SeosNwChanmuxIf.h"
#include "Seos_Driver_Config.h"


struct pico_device_chan_mux_tap
{
    struct pico_device dev;
};

#define TUN_MTU     4096 /* This value is chosen due to PAGE_SIZE of platform */

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

    const ChanMux_channelCtx_t channel_data =
    {
        .data_port  = pnw_camkes->pportsglu->ChanMuxDataPort,
        .id         = pTapdrv->chan_data
    };

    return SeosNwChanmux_chanWriteSyncData(&channel_data, buf, len);
}


//------------------------------------------------------------------------------
// callback fuction for PicoTCP (part of stack tick)
static int
pico_chan_mux_tap_poll(
    struct pico_device*  dev,
    int                  loop_score)
{
    seos_driver_config* pTapdrv = Seos_NwDriver_getconfig();

    const ChanMux_channelCtx_t channel_data =
    {
        .data_port  = pnw_camkes->pportsglu->ChanMuxDataPort,
        .id         = pTapdrv->chan_data
    };


    unsigned char buf[TUN_MTU];

    // loop_score indicates max number of frames that PicoTCP can read in this
    // call.
    while (loop_score > 0)
    {
        int len = SeosNwChanmux_chanRead(&channel_data, buf, sizeof(buf));
        if (len <= 0)
        {
            return loop_score;
        }

        loop_score--;
        pico_stack_recv(dev, buf, (uint32_t)len);
    }

    return 0;
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
        Debug_LOG_ERROR("%s():Memory alloc failure for tap device",
                        __FUNCTION__);
        return NULL;
    }

    seos_driver_config* pTapdrv = Seos_NwDriver_getconfig();
    if (!pTapdrv)
    {
        Debug_LOG_ERROR("%s():Incorrect Tap Driver Config",
                        __FUNCTION__);
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
        Debug_LOG_ERROR("%s(): SeosNwChanmux_open failed, error:%d",
                        __FUNCTION__, err);
        pico_chan_mux_tap_destroy((struct pico_device*)chan_mux_tap);
        return NULL;
    }

    // ChanMUX simulates an etehrnet device, get the MAC address from it
    err = SeosNwChanmux_get_mac(&channel_ctrl, chan_id_data, mac);
    if (err != SEOS_SUCCESS)
    {
        Debug_LOG_ERROR("%s(): SeosNwChanmux_get_mac failed, error:%d",
                        __FUNCTION__, err);

        // ToDo: close SeosNwChanmux chnnel

        pico_chan_mux_tap_destroy((struct pico_device*)chan_mux_tap);
        return NULL;
    }

    Debug_LOG_INFO("%s() MAC is %02x:%02x:%02x:%02x:%02x:%02x",
                   __FUNCTION__,
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5] );

    // sanity check, the MAC address can't be all zero.
    const uint8_t empty_mac[MAC_SIZE] = {0};
    if (memcmp(mac, empty_mac, MAC_SIZE) == 0)
    {
        Debug_LOG_ERROR("%s() empty MAC is not allowed", __FUNCTION__);
        pico_chan_mux_tap_destroy((struct pico_device*)chan_mux_tap);
        return NULL;
    }

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
        Debug_LOG_ERROR("%s():Tap device init failed, ret = %d", __FUNCTION__, ret);
        pico_chan_mux_tap_destroy((struct pico_device*)chan_mux_tap);
        return NULL;
    }

    chan_mux_tap->dev.send      = pico_chan_mux_tap_send;
    chan_mux_tap->dev.poll      = pico_chan_mux_tap_poll;
    chan_mux_tap->dev.destroy   = pico_chan_mux_tap_destroy;

    Debug_LOG_INFO("Device %s created", drv_name);

    return (struct pico_device*)chan_mux_tap;
}
