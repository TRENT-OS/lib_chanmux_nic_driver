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
        // we only block on reading new that if there is an explicit request to
        // to this. We can't do it every time the buffer is empty, becuase this
        // would block some state machine transitions.
        // ToDo: Current implementation will also block on the ChanMUX data
        //       notification, because that is the best option we have at the
        //       moment to do nothing and don't waste CPU time. We could
        //       improve things by adding error recovery options, e.g.  allow
        //       a reset of the NIC driver.
        while (doRead || (RECEIVE_ERROR == state))
        {
            // if there was a read request, then the buffer must be empty.
            Debug_ASSERT( (!doRead) || (0 == buffer_len) );

            // in error state we simply drop all remaining data
            if (RECEIVE_ERROR == state)
            {
                Debug_LOG_ERROR("state RECEIVE_ERROR, drop %zu bytes", buffer_len);
                buffer_len = 0;
            }

            // ToDo: actually, we want a single atomic blocking read RPC call
            //       here and not the two calls of wait() and read().
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

            // it can happen that we wanted to read new data, blocked on the
            // ChanMUX event and eventually got it. But unfortunately, there is
            // no new data for some reason. One day we should analyze this in
            // more detail and don't just consider this a spurious event that
            // happens every now and then. Until then, we just keep looping
            // and block until the next ChanMUX event comes, because we are
            // here exactly because the state machine has run out of data.
            if ((RECEIVE_ERROR != state) && (0 != buffer_len))
            {
                memcpy(buffer, data->port_read.buffer, buffer_len);
                buffer_offset = 0;
                doRead = false; // ensure we leave the loop
            }

        } // end while

        // when we arrive here, there might be data in the buffer to read or
        // the state achine just needs to make progress. But we can't be in
        // the error state, as the loop above is supposed to handle this state.
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
            Debug_LOG_TRACE("expecting ethernet frame of %zu bytes", frame_len);
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
            // check if the network stack has processed the frame.
            if (0 != (volatile size_t)nw_rx->len)
            {
                // frame processing is still ongoing. Instead of going straight
                // into blocking here, we can do an optimization here in case
                // the buffer is empt - check if there is new data in the
                // ChanMUX FIFO and fetch it into our buffer. Then the FIFO
                // becomes available again for more data. Note that this makes
                // sense in the current approach where we have a local buffer
                // here. Once we optimize out this local buffer, we might also
                // block here directly, because there is noting else we can do.
                // Note also, that we can't do a blocking wait on the ChanMUX
                // is there is still data in the buffer, because we have to
                // give this data as potential frame to the network stack once
                // is has processed the current frame.
                doRead = (0 == buffer_len);
                if (doRead)
                {
                    break;
                }

                // ToDo: here we should block on a signal that the network
                //       stack sets when it has processed the frame. Until we
                //       have this, yielding is the best thing we can do.
                yield_counter++;
                seL4_Yield();

                // As long as we yield, there is not too much again in checking
                // nw_rx->len here again, as we basically run a big loop. But
                // once we block waiting on a signal, checking here makes much
                // sense, because we expect to find the length cleared. Note
                // that we can't blindly assume this, because there might be
                // corner cases where we could see spurious signals.
                if (0 != (volatile size_t)nw_rx->len)
                {
                    break;
                }
            }

            // if we arrive here, the network stack has processed the frame, so
            // we can give it the next frame.

            // As long as we use yield instad of blocking on a singal, let's
            // have some statistics about how bad the yielding really is.
            // Ideally, we see no yields at all. But that happens very rarely
            // (especially in debug builds). One yield seem the standard case,
            // so we don't report this unless we are loggin at trace level.
            // The more yields we see happning, the higher the priority gets
            // that we more to signals and stop wasint CPU time.
            if (yield_counter > 0)
            {
                if (1 == yield_counter)
                {
                    Debug_LOG_TRACE("yield_counter is %zu", yield_counter);
                }
                else
                {
                    Debug_LOG_WARNING("yield_counter is %zu", yield_counter);
                }
            }

            Debug_ASSERT( !doRead );
            state = RECEIVE_FRAME_START;
            break;

        //----------------------------------------------------------------------
        // case RECEIVE_ERROR
        default:
            // This is basically a safe-guard. Practically, we should never
            // arrive here, since we have case-statements for all important
            // enum values. There is none for RECEIVE_ERROR, because we hande
            // this state somewhere else.
            Debug_LOG_ERROR("invalid state %d, drop %zu bytes", state, buffer_len);
            Debug_ASSERT( RECEIVE_ERROR == state );

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

    Debug_LOG_TRACE("sending frame of %zu bytes ", len);

    const ChanMux_channelDuplexCtx_t* data = get_chanmux_channel_data();

    const seos_shared_buffer_t* nw_output = get_network_stack_port_from();
    uint8_t* buffer_nw_out = (uint8_t*)nw_output->buffer;
    size_t offset_nw_out = 0;
    size_t remain_len = len;

    while (remain_len > 0)
    {
        size_t len_chunk = remain_len;
        if (len_chunk > data->port_read.len)
        {
            len_chunk = data->port_read.len;
            Debug_LOG_WARNING("can only send %zu of %zu bytes",
                              len_chunk, remain_len);
        }

        // copy data from network stack to ChanMUX buffer
        memcpy(data->port_write.buffer,
               &buffer_nw_out[offset_nw_out],
               len_chunk);

        // tell ChanMUX how much data is there
        size_t len_written = 0;
        seos_err_t err = ChanMux_write(data->id, len_chunk, &len_written);
        if (err != SEOS_SUCCESS)
        {
            Debug_LOG_ERROR("ChanMux_write() failed, error %d", err);
            return SEOS_ERROR_GENERIC;
        }

        if (len_chunk != len_written)
        {
            Debug_LOG_WARNING("ChanMux_write() wrote only %zu of %zu bytes",
                              len_written, len_chunk);
            Debug_ASSERT(len_written <= len_chunk);
        }

        Debug_ASSERT(remain_len <= len_written);
        remain_len -= len_written;
        offset_nw_out += len_written;
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