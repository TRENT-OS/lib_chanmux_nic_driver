/*
 *  ChanMUX Ethernet TAP driver
 *
 *  Copyright (C) 2019, Hensoldt Cyber GmbH
 *
 */

#include "LibDebug/Debug.h"
#include "SeosError.h"
#include "seos_chanmux.h"
#include "seos_ethernet.h"
#include "seos_network_stack.h"
#include "chanmux_nic_drv.h"
#include <string.h>


//------------------------------------------------------------------------------
seos_err_t
chanmux_nic_driver_init(void)
{
    seos_err_t err;

    // initialize the shared memory, there is no data waiting in the buffer
    const seos_shared_buffer_t* nw_input = get_network_stack_port_to();
    Rx_Buffer* nw_rx = (Rx_Buffer*)nw_input->buffer;
    nw_rx->len = 0;

    // initialize the ChanMUX/Proxy connection
    const ChanMux_channelCtx_t* ctrl = get_chanmux_channel_ctrl();
    const ChanMux_channelDuplexCtx_t* data = get_chanmux_channel_data();

    Debug_LOG_INFO("ChanMUX channels: ctrl=%u, data=%u", ctrl->id, data->id);

    err = SeosNwChanmux_open(ctrl, data->id);
    if (err != SEOS_SUCCESS)
    {
        Debug_LOG_ERROR("SeosNwChanmux_open() failed, error:%d", err);
        return SEOS_ERROR_GENERIC;
    }

    return SEOS_SUCCESS;
}


//------------------------------------------------------------------------------
// Receive loop, waits for an interrupt signal from ChanMUX, reads data and
// notifies network stack when a frame is available
seos_err_t
chanmux_nic_driver_loop(void)
{
    const ChanMux_channelDuplexCtx_t* data = get_chanmux_channel_data();

    const seos_shared_buffer_t* nw_input = get_network_stack_port_to();
    Rx_Buffer* nw_rx = (Rx_Buffer*)nw_input->buffer;

    for (;;)
    {
        // wait for ChanMUX to signal there is new data
        chanmux_wait();

        size_t len_read = 0;
        seos_err_t err = ChanMux_read(data->id, data->port_read.len, &len_read);
        if (err != SEOS_SUCCESS)
        {
            Debug_LOG_ERROR("ChanMux_read() failed, error %d", err);
            return SEOS_ERROR_GENERIC;
        }

        if (len_read > 0)
        {
            // copy data into buffer
            memcpy(nw_rx->data, data->port_read.buffer, len_read);
            // set length
            nw_rx->len = len_read;
            // notify netwrok stack
            network_stack_notify();
        }
    }
}


//------------------------------------------------------------------------------
// called by network stack to send data
seos_err_t seos_chanmux_nic_driver_rpc_tx_data(
    size_t* pLen)
{
    size_t len = *pLen;
    *pLen = 0;

    const ChanMux_channelDuplexCtx_t* data = get_chanmux_channel_data();

    const seos_shared_buffer_t* nw_output = get_network_stack_port_from();
    uint8_t* buffer_nw_out = (uint8_t*)nw_output->buffer;

    size_t remain_len = len;

    while (remain_len > 0)
    {
        size_t len_chunk = remain_len;
        if (len_chunk > data->port_read.len)
        {
            len_chunk = data->port_read.len;
        }

        // copy data from network stack to ChanMUX buffer
        memcpy(data->port_write.buffer, buffer_nw_out, len_chunk);

        // tell ChanMUX how much data is there
        size_t len_written = 0;
        seos_err_t err = ChanMux_write(data->id, len_chunk, &len_written);
        if (err != SEOS_SUCCESS)
        {
            Debug_LOG_ERROR("ChanMux_write() failed, error %d", err);
            return SEOS_ERROR_GENERIC;
        }

        Debug_ASSERT(len_written <= len_chunk);

        Debug_ASSERT(remain_len <= len_chunk);
        remain_len -= len_written;

        buffer_nw_out = &buffer_nw_out[len_written];
    }

    *pLen = len;
    return SEOS_SUCCESS;
}


//------------------------------------------------------------------------------
// called by network stack to get the MAC
seos_err_t seos_chanmux_nic_driver_rpc_get_mac(void)
{
    const ChanMux_channelCtx_t* ctrl = get_chanmux_channel_ctrl();
    const ChanMux_channelDuplexCtx_t* data = get_chanmux_channel_data();

    // ChanMUX simulates an ethernet device, get the MAC address from it
    uint8_t mac[MAC_SIZE] = {0};
    seos_err_t err = SeosNwChanmux_get_mac(ctrl, data->id, mac);
    if (err != SEOS_SUCCESS)
    {
        Debug_LOG_ERROR("SeosNwChanmux_get_mac() failed, error %d", err);

        // ToDo: close SeosNwChanmux channel
        return SEOS_ERROR_GENERIC;
    }

    // sanity check, the MAC address can't be all zero.
    const uint8_t empty_mac[MAC_SIZE] = {0};
    if (memcmp(mac, empty_mac, MAC_SIZE) == 0)
    {
        Debug_LOG_ERROR("MAC with all zeros is not allowed");
        // ToDo: close SeosNwChanmux channel
        return SEOS_ERROR_GENERIC;
    }

    Debug_LOG_INFO("MAC is %02x:%02x:%02x:%02x:%02x:%02x",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5] );

    // ToDo: actually, the proxy is supposed to emulate a network interface
    //       with a proper MAC address. Currently, it uses a TAP device in
    //       Linux and is seems things only work if we use a different MAC
    //       address here - that's why we simply increment it by one. However,
    //       this is something the proxy should handle internally, we want to
    //       be agnostic of such things and just expect it to be a good network
    //       interface card simulation that has a proper MAC.
    mac[MAC_SIZE - 1]++;

    const seos_shared_buffer_t* nw_input = get_network_stack_port_to();
    Rx_Buffer* nw_rx = (Rx_Buffer*)nw_input->buffer;
    memcpy(nw_rx->data, mac, MAC_SIZE);

    return SEOS_SUCCESS;
}
