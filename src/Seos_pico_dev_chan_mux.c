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
#include "Seos_pico_dev_chan_mux.h"
#include "pico_stack.h"
#include "SeosNwChanmuxIf.h"

struct pico_device_chan_mux_tap
{
    struct pico_device dev;
};

#define TUN_MTU  2048


static int pico_chan_mux_tap_send(struct pico_device* dev, void* buf, int len)
{
    return (SeosNwChanmux_write_data(buf, len));
}

/*
 *  Poll called by Picotcp stack during reading of Data as part of tick
 *
 *
 */
static int pico_chan_mux_tap_poll( struct pico_device* dev, int loop_score )
{
    // struct pico_device_chan_mux_tap *tap = ( struct pico_device_chan_mux_tap * ) dev;
    unsigned char buf[TUN_MTU];
    int len;

    while (loop_score > 0)
    {
        len = SeosNwChanmux_read_data(buf, TUN_MTU);
        if (len > 0)
        {
            loop_score--;
            pico_stack_recv(dev, buf, (uint32_t)len);
        }
        else
        {
            return loop_score;
        }

    }
    return 0;

}

void pico_chan_mux_tap_destroy ( struct pico_device* dev )
{
    // As of now nothing to destroy
}


static int pico_chan_mux_tap_open (char* name )
{
    //As of now open does nothing
    return 0;
}


static int pico_chan_mux_tap_get_mac(char* name, uint8_t* mac)
{
    return (SeosNwChanmux_get_mac(name, mac));
}

struct pico_device* pico_chan_mux_tap_create (char* name)
{
    struct pico_device_chan_mux_tap* chan_mux_tap = PICO_ZALLOC(sizeof (
            struct pico_device_chan_mux_tap));
    uint8_t mac[6] = {};

    if ( !chan_mux_tap )
    {
        return NULL;
    }

    pico_chan_mux_tap_open(name);

    printf("%s\n", __FUNCTION__);


    /* Host's mac address is generated * by the host kernel and is
     * retrieved via tap_get_mac().
     */
    if (pico_chan_mux_tap_get_mac(name, mac) < 0 )
    {
        dbg ( "Pico MUX CHAN Tap mac query failed.\n" );
        pico_chan_mux_tap_destroy ((struct pico_device* ) chan_mux_tap );
        return NULL;
    }

    /* To act as a second endpoint in the same subnet, the picoTCP
     * app using the tap device must have a different mac address.
     * For simplicity, we just add 1 to the last byte of the linux
     * endpoint so the two addresses are consecutive.
     */
    mac[5]++;

    if ( 0 != pico_device_init ( ( struct pico_device* ) chan_mux_tap, name, mac ) )
    {
        dbg ( "Chan Mux Tap init failed.\n" );
        pico_chan_mux_tap_destroy ( ( struct pico_device* ) chan_mux_tap );
        return NULL;
    }

    chan_mux_tap->dev.send = pico_chan_mux_tap_send;
    chan_mux_tap->dev.poll = pico_chan_mux_tap_poll;
    chan_mux_tap->dev.destroy = pico_chan_mux_tap_destroy;
    dbg ( "Device %s created.\n", chan_mux_tap->dev.name );
    return ( struct pico_device* ) chan_mux_tap;
}

