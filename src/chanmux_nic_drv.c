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
#include <sel4/sel4.h> // needed for seL4_yield()
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
    const seos_shared_buffer_t nw_in =
    {
        .buffer = &(nw_rx->data),
        .len = sizeof(nw_rx->data)
    };

    // since the ChanMUX channel data port is used by send and receive, we
    // have to copy the data into an intermediate buffer, otherwise it will
    // be overwritten. Define the buffer as static will not create it on the
    // stack
    static uint8_t buffer[ETHERNET_FRAME_MAX_SIZE];
    size_t buffer_offset = 0;
    size_t buffer_len = 0;

    // Data format is:  2 byte frame length | frame data | .....
    enum state_e
    {
        RECEIVE_ERROR = 0,
        RECEIVE_FRAME_START,
        RECEIVE_FRAME_LEN,
        RECEIVE_FRAME_DATA,
        RECEIVE_PROCESSING
    } state = RECEIVE_FRAME_START;

    size_t size_len = 0;
    size_t frame_len = 0;
    size_t frame_offset = 0;
    size_t yield_counter = 0;
    int doRead = true;
    int doDropFrame = false;

    for (;;)
    {
        Debug_ASSERT( (!doRead) || (0 == buffer_len) );
        while (doRead || (RECEIVE_ERROR == state))
        {
            Debug_ASSERT( 0 == buffer_len );
            Debug_LOG_DEBUG("chanmux_wait, state %d", state);
            chanmux_wait();

            // read as much data as possible from the ChanMUX channel FIFO into
            // the shared memory data port. We do this even in the state
            // RECEIVE_ERROR, because we have to drain the FIFOs.
            seos_err_t err = ChanMux_read(data->id,
                                          sizeof(buffer),
                                          &buffer_len);
            if (err != SEOS_SUCCESS)
            {
                Debug_LOG_ERROR("ChanMux_read() %s failed, state=%d, error %d",
                                (SEOS_ERROR_OVERFLOW_DETECTED == err) ? "reported OVERFLOW" : "failed",
                                size_len, err);
                state = RECEIVE_ERROR;
            }

            // in error state we simply drop all data
            if (RECEIVE_ERROR == state)
            {
                Debug_LOG_ERROR("state RECEIVE_ERROR, drop %zu bytes", buffer_len);
                buffer_len = 0; // causes wait for the next ChanMUX signal
                continue;
            }

            if (0 == buffer_len)
            {
                Debug_LOG_DEBUG("no data from ChanMUX");
                continue;
            }

            memcpy(buffer, data->port_read.buffer, buffer_len);
            buffer_offset = 0;
            doRead = false;
        } // end while

        // when we arrive here, there might be data in the buffer to read or
        // the state achine just needs to make progress.

        Debug_ASSERT( RECEIVE_ERROR != state );
        switch (state)
        {
        //----------------------------------------------------------------------
        case RECEIVE_FRAME_START:
            size_len = 2;
            frame_len = 0;
            frame_offset = 0;
            doDropFrame = false;
            Debug_ASSERT( !doRead );
            state = RECEIVE_FRAME_LEN;
            break; // could also fall through

        //----------------------------------------------------------------------
        case RECEIVE_FRAME_LEN:

            Debug_ASSERT( 0 != size_len );
            if (0 == buffer_len)
            {
                doRead = true;
                break;
            }

            do
            {
                Debug_ASSERT( buffer_offset + buffer_len <= sizeof(buffer) );

                uint8_t len_byte = buffer[buffer_offset++];
                buffer_len--;
                size_len--;

                // frame length is send in network byte order (big endian), so we
                // build the value as: 0x0000 -> 0x00AA -> 0xAABB
                frame_len <<= 8;
                frame_len |= len_byte;

            }
            while ((buffer_len > 0) && (size_len > 0));

            if (size_len > 0)
            {
                Debug_ASSERT( 0 == buffer_len );
                doRead = true;
                break;
            }

            // we have read the length, make some sanity check and then
            // change state to read the frame data
            Debug_LOG_DEBUG("expecting ethernet frame of %zu bytes", frame_len);
            Debug_ASSERT( 0 == frame_offset );
            Debug_ASSERT( frame_len <= ETHERNET_FRAME_MAX_SIZE );
            // if the frame is too big for our buffer, then the only option is
            // dropping it
            doDropFrame = (frame_len > nw_in.len);
            if (doDropFrame)
            {
                Debug_LOG_WARNING("frame length %zu exceeds frame buffer size %d, drop it",
                                  frame_len, nw_in.len);
            }

            // read the frame data
            Debug_ASSERT( !doRead );
            state = RECEIVE_FRAME_DATA;
            break; // could also fall through

        //----------------------------------------------------------------------
        case RECEIVE_FRAME_DATA:
            if (0 == buffer_len)
            {
                doRead = true;
                break;
            }

            {
                size_t chunk_len = frame_len - frame_offset;
                if (chunk_len > buffer_len)
                {
                    chunk_len = buffer_len;
                }

                if (!doDropFrame)
                {
                    // we can't handle frame bigger than our buffer and the only
                    // option in this case is dropping the frame
                    Debug_ASSERT(chunk_len < nw_in.len);
                    Debug_ASSERT( frame_offset + chunk_len < nw_in.len );

                    // ToDo: we could try to avoid this copy operation and just
                    //       have one shared memory for the ChanMUX channel and the
                    //       network stack input. But that requires more
                    //       synchronization then and we have to deal with cases
                    //       where a frame wraps around in the buffer.
                    uint8_t* nw_in_buf = (uint8_t*)nw_in.buffer;
                    memcpy(&nw_in_buf[frame_offset],
                           &buffer[buffer_offset],
                           chunk_len);
                }

                Debug_ASSERT( buffer_len >= chunk_len );
                buffer_len -= chunk_len;
                buffer_offset += chunk_len;
                frame_offset += chunk_len;
            }

            // check if we have received the full frame. If not then wait for
            // more data
            if (frame_offset < frame_len)
            {
                Debug_ASSERT( 0 == buffer_len );
                doRead = true;
                break;
            }

            // we have a full frame. If we skip the frame then we are done and
            // try to read the next frame
            if (doDropFrame)
            {
                Debug_ASSERT( !doRead );
                state = RECEIVE_FRAME_START;
                break;
            }

            // notify network stack that it can process an new frame
            // Debug_LOG_DEBUG("got ethernet frame of %zu bytes", frame_len);
            nw_rx->len = frame_len;
            network_stack_notify();

            yield_counter = 0;
            Debug_ASSERT( !doRead );
            state = RECEIVE_PROCESSING;
            break; // could also fall through

        //----------------------------------------------------------------------
        case RECEIVE_PROCESSING:
            // The only option here is polling for "nw_in->len = 0". We can't
            // wait for a ChanMAX input notification because that comes just
            // for a new frame. At some point there could be no more frames
            // coming and we would be stuck.
            // we end up in a deadlock.
            if (0 != (volatile size_t)nw_rx->len)
            {
                // we can try to read new data (and drain the ChanMUX FIFO)
                // while instead of just waiting.
                doRead = (0 == buffer_len);
                if (!doRead)
                {
                    // we have more pending data already, but the frame has not
                    // been processed, so all we can do now is wait. Loop
                    // over a yield is the best we can do, since there is no
                    // signal we can block on
                    yield_counter++;
                    seL4_Yield();
                }
                break;
            }

            // if we arrive here, the network stack has processed the frame, so
            // we can receive and process the next frame
            if (yield_counter > 0)
            {
                Debug_LOG_WARNING("yield_counter is %zu", yield_counter);
            }

            Debug_ASSERT( !doRead );
            state = RECEIVE_FRAME_START;
            break;

        //----------------------------------------------------------------------
        default:
            Debug_LOG_ERROR("invalid state %d, drop %zu bytes", state, buffer_len);
            state = RECEIVE_ERROR;
            Debug_ASSERT(false); // we should never be here
            break;
        } // end switch (state)
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
