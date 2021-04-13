/*
 *  ChanMUX Ethernet TAP driver
 *
 *  Copyright (C) 2019, Hensoldt Cyber GmbH
 *
 */

#include "lib_debug/Debug.h"
#include "OS_Error.h"
#include "OS_Types.h"
#include "network/OS_Ethernet.h"
#include "network/OS_NetworkStack.h"
#include "ChanMux/ChanMuxCommon.h"
#include "chanmux_nic_drv.h"
#include <sel4/sel4.h> // needed for seL4_yield()
#include <string.h>

// If we pass the define from a system configuration header. CAmkES generation
// crashes when parsing this file. As a workaround we hardcode the value here
#define NIC_DRIVER_RINGBUFFER_NUMBER_ELEMENTS 16

//------------------------------------------------------------------------------
// Receive loop, waits for an interrupt signal from ChanMUX, reads data and
// notifies network stack when a frame is available.
// This function implements a FSM that has a big switch-case construct. Those
// kind of functions, when decomposed, often result in a less readable code.
// Therefore we suppress the cyclomatic complexity analysis for this function.
// metrix++: suppress std.code.complexity:cyclomatic
OS_Error_t
chanmux_nic_driver_loop(void)
{
    const ChanMux_ChannelOpsCtx_t* ctrl = get_chanmux_channel_ctrl();
    const ChanMux_ChannelOpsCtx_t* data = get_chanmux_channel_data();

    const OS_SharedBuffer_t* nw_input = get_network_stack_port_to();
    OS_NetworkStack_RxBuffer_t* nw_rx = (OS_NetworkStack_RxBuffer_t*)
                                        nw_input->buffer;

    static unsigned int pos = 0;
    size_t rx_slot_buffer_len = sizeof(nw_rx->data);

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

    // The Proxy needs to get a START command in order to
    // forward frames from the TAP interface
    OS_Error_t err = chanmux_nic_ctrl_startData(ctrl, data->id);
    if (err != OS_SUCCESS)
    {
        Debug_LOG_ERROR("chanmux_nic_ctrl_startData() failed, code %d", err);
        return err;
    }

    for (;;)
    {
        // we only block on reading new data if there is an explicit request to
        // to this. We can't do it every time the buffer is empty, because this
        // would block some state machine transitions.
        // ToDo: Current implementation will also block on the ChanMUX data
        //       notification, because that is the best option we have at the
        //       moment to do nothing and don't waste CPU time. We could
        //       improve things by adding error recovery options, e.g.  allow
        //       a reset of the NIC driver.
        while (doRead || (RECEIVE_ERROR == state))
        {
            // in error state we simply drop all remaining data
            if (RECEIVE_ERROR == state)
            {
                /// WARNING: this recovery mechanism should be based on
                /// inter-bytes delays as well but at the moment the timer
                /// server that should provide us the getTime() facility cannot
                /// handle more than one client
                Debug_LOG_WARNING("Chanmux receive error, resetting FIFO");
                OS_Error_t err = chanmux_nic_ctrl_stopData(ctrl, data->id);
                if (err != OS_SUCCESS)
                {
                    Debug_LOG_ERROR("chanmux_nic_ctrl_stopData() failed, code %d", err);
                    return err;
                }

                if (0 != buffer_len)
                {
                    Debug_LOG_ERROR("state RECEIVE_ERROR, drop %zu bytes", buffer_len);
                    buffer_len = 0;
                }
                // drain the channel FIFO
                do
                {
                    err = data->func.read(data->id, sizeof(buffer), &buffer_len);
                    if (err == OS_ERROR_OVERFLOW_DETECTED)
                    {
                        continue;
                    }
                }
                while (buffer_len > 0);

                state = RECEIVE_FRAME_START;

                err = chanmux_nic_ctrl_startData(ctrl, data->id);
                if (err != OS_SUCCESS)
                {
                    Debug_LOG_ERROR("chanmux_nic_ctrl_startData() failed, code %d", err);
                    return err;
                }
            }
            else if (doRead)
            {
                // if there was a read request, then the buffer must be empty.
                Debug_ASSERT( 0 == buffer_len );
            }

            // ToDo: actually, we want a single atomic blocking read RPC call
            //       here and not the two calls of wait() and read().
            chanmux_channel_data_wait();

            // read as much data as possible from the ChanMUX channel FIFO into
            // the shared memory data port. We do this even in the state
            // RECEIVE_ERROR, because we have to drain the FIFOs.
            OS_Error_t err = data->func.read(
                                 data->id,
                                 sizeof(buffer),
                                 &buffer_len);
            if (err != OS_SUCCESS)
            {
                Debug_LOG_ERROR("ChanMuxRpc_read() %s, error %d, state=%d",
                                (OS_ERROR_OVERFLOW_DETECTED == err) ? "reported OVERFLOW" : "failed",
                                err, state);
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
                memcpy(buffer, OS_Dataport_getBuf(data->port.read), buffer_len);
                buffer_offset = 0;
                doRead = false; // ensure we leave the loop
            }

        } // end while

        // when we arrive here, there might be data in the buffer to read or
        // the state machine just needs to make progress. But we can't be in
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
            doDropFrame = (frame_len > rx_slot_buffer_len);
            if (doDropFrame)
            {
                Debug_LOG_WARNING(
                    "frame length %zu exceeds frame buffer size %zu, drop it",
                    frame_len,
                    rx_slot_buffer_len);
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
                    Debug_ASSERT(chunk_len < rx_slot_buffer_len);
                    Debug_ASSERT(frame_offset + chunk_len < rx_slot_buffer_len);

                    // ToDo: we could try to avoid this copy operation and just
                    //       have one shared memory for the ChanMUX channel and the
                    //       network stack input. But that requires more
                    //       synchronization then and we have to deal with cases
                    //       where a frame wraps around in the buffer.
                    uint8_t* nw_in_buf = (uint8_t*)nw_rx[pos].data;
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
            nw_rx[pos].len = frame_len;
            pos            = (pos + 1) % NIC_DRIVER_RINGBUFFER_NUMBER_ELEMENTS;
            network_stack_notify();

            yield_counter = 0;
            Debug_ASSERT( !doRead );
            state = RECEIVE_PROCESSING;
            break; // could also fall through

        //----------------------------------------------------------------------
        case RECEIVE_PROCESSING:
            // check if the network stack has processed the frame.
            if (0 != nw_rx[pos].len)
            {
                // frame processing is still ongoing. Instead of going straight
                // into blocking here, we can do an optimization here in case
                // the buffer is empty - check if there is new data in the
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

                // As long as we yield, there is not too much gain in checking
                // nw_rx->len here again, as we basically run a big loop. But
                // once we block waiting on a signal, checking here makes much
                // sense, because we expect to find the length cleared. Note
                // that we can't blindly assume this, because there might be
                // corner cases where we could see spurious signals.
                if (0 != nw_rx[pos].len)
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
            // The more yields we see happening, the higher the priority gets
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
            // enum values. There is none for RECEIVE_ERROR, because we handle
            // this state somewhere else.
            Debug_LOG_ERROR("invalid state %d, drop %zu bytes", state, buffer_len);
            Debug_ASSERT( RECEIVE_ERROR == state );

            break;
        } // end switch (state)
    }
}


//------------------------------------------------------------------------------
// called by network stack to send an ethernet frame
OS_Error_t
chanmux_nic_driver_rpc_tx_data(
    size_t* pLen)
{
    size_t len = *pLen;
    *pLen = 0;

    Debug_LOG_TRACE("sending frame of %zu bytes ", len);

    // Ethernet frames used to be max 1518 bytes. Then 802.1Q added a 4 byte
    // Q-tag, so they can be 1522 bytes, which is a common default. However,
    // there is also 802.1ad "Q-in-Q", where multiple Q-tags can be present.
    // Thus we do not make any assumption here about the max size here and send
    // whatever the network stack give us. With our 2-byte length prefix, the
    // length can be up to 0xFFFF, so even jumbo frame with an MTU of 9000 byte
    // would work.
    if (len > 0xFFFF)
    {
        Debug_LOG_WARNING("can't send frame, len %zu exceeds max supported length %d",
                          len, 0xFFFF);
        return OS_ERROR_GENERIC;
    }

    const ChanMux_ChannelOpsCtx_t* data = get_chanmux_channel_data();
    uint8_t* port_buffer = OS_Dataport_getBuf(data->port.write);
    size_t port_size = OS_Dataport_getSize(data->port.write);
    size_t port_offset = 0;

    const OS_SharedBuffer_t* nw_output = get_network_stack_port_from();
    uint8_t* buffer_nw_out = (uint8_t*)nw_output->buffer;
    size_t offset_nw_out = 0;

    // send frame length as uint16 in big endian
    Debug_ASSERT(port_size >= 2);
    port_buffer[port_offset++] = (len >> 8) & 0xFF;
    port_buffer[port_offset++] = len & 0xFF;
    port_size -= 2;

    size_t remain_len = len;
    while (remain_len > 0)
    {
        size_t len_chunk = remain_len;
        if (len_chunk > port_size)
        {
            len_chunk = port_size;
            Debug_LOG_WARNING("can only send %zu of %zu bytes",
                              len_chunk, remain_len);
        }

        // copy data from network stack to ChanMUX buffer
        memcpy(&port_buffer[port_offset],
               &buffer_nw_out[offset_nw_out],
               len_chunk);

        // tell ChanMUX how much data is there. We have to take into account
        // the frame length prefix.
        size_t len_to_write = port_offset + len_chunk;
        size_t len_written = 0;
        OS_Error_t err = data->func.write(
                             data->id,
                             len_to_write,
                             &len_written);
        if (err != OS_SUCCESS)
        {
            Debug_LOG_ERROR("ChanMuxRpc_write() failed, error %d", err);
            return OS_ERROR_GENERIC;
        }

        Debug_ASSERT(len_written <= len_to_write);
        if (len_written != len_to_write)
        {
            Debug_LOG_WARNING("ChanMuxRpc_write() wrote only %zu of %zu bytes",
                              len_written, len_to_write);
            return OS_ERROR_GENERIC;
        }

        // len_written may include the frame length header, but remain_len does
        // not contain is. Thus we have to use len_chunk here.
        Debug_ASSERT(remain_len <= len_chunk);
        remain_len -= len_chunk;
        offset_nw_out += len_chunk;

        // full port buffer is available again
        port_offset = 0;
        port_size = OS_Dataport_getSize(data->port.write);
    }

    *pLen = len;
    return OS_SUCCESS;
}


//------------------------------------------------------------------------------
// called by network stack to get the MAC
OS_Error_t
chanmux_nic_driver_rpc_get_mac(void)
{
    const ChanMux_ChannelOpsCtx_t* ctrl = get_chanmux_channel_ctrl();
    const ChanMux_ChannelOpsCtx_t* data = get_chanmux_channel_data();

    // ChanMUX simulates an ethernet device, get the MAC address from it
    uint8_t mac[MAC_SIZE] = {0};
    OS_Error_t err = chanmux_nic_ctrl_get_mac(ctrl, data->id, mac);
    if (err != OS_SUCCESS)
    {
        Debug_LOG_ERROR("chanmux_nic_ctrl_get_mac() failed, error %d", err);
        return OS_ERROR_GENERIC;
    }

    // sanity check, the MAC address can't be all zero.
    const uint8_t empty_mac[MAC_SIZE] = {0};
    if (memcmp(mac, empty_mac, MAC_SIZE) == 0)
    {
        Debug_LOG_ERROR("MAC with all zeros is not allowed");
        return OS_ERROR_GENERIC;
    }

    Debug_LOG_INFO("MAC is %02x:%02x:%02x:%02x:%02x:%02x",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5] );

    const OS_SharedBuffer_t* nw_input = get_network_stack_port_to();
    OS_NetworkStack_RxBuffer_t* nw_rx = (OS_NetworkStack_RxBuffer_t*)
                                        nw_input->buffer;
    memcpy(nw_rx->data, mac, MAC_SIZE);

    return OS_SUCCESS;
}
