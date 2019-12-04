/*
 *  Copyright (C) 2019, Hensoldt Cyber GmbH
 *
 */

#pragma once

#include "SeosError.h"
#include "seos_chanmux.h"
#include <stddef.h>
#include <stdint.h>

//------------------------------------------------------------------------------
// Configuration Wrappers
//------------------------------------------------------------------------------
const ChanMux_channelCtx_t* get_chanmux_channel_ctrl(void);
const ChanMux_channelDuplexCtx_t* get_chanmux_channel_data(void);
void chanmux_wait(void);
const seos_shared_buffer_t* get_network_stack_port_to(void);
const seos_shared_buffer_t* get_network_stack_port_from(void);
void network_stack_notify(void);

//------------------------------------------------------------------------------
// internal functions
//------------------------------------------------------------------------------
seos_err_t chanmux_nic_driver_init(void);
seos_err_t chanmux_nic_driver_loop(void);


/**
 * @details %ChanMux_write, Write data using ChanMux
 *
 * @ingroup SeosNwChanmuxIf
 * @param chan: channel
 * @param len: Length of data to write
 * @param *written: is pointer to written which will contain how much of data was
                    actually written by Chanmux
 * @return Success or Failure.
 * @retval SEOS_SUCCESS or SEOS_ERROR_GENERIC
 *
 */
seos_err_t ChanMux_write(
    unsigned int  chan,
    size_t        len,
    size_t*       written);


/**
 * @details %ChanMux_read, Read data using ChanMux
 * @ingroup SeosNwChanmuxIf
 *
 * @param chan: channel
 * @param len: Length of data to read
 * @param *read: is pointer to read which will contain how much of data was
 *               actually read by Chanmux
 * @return Success or Failure.
 * @retval SEOS_SUCCESS or SEOS_ERROR_GENERIC
 *
 */
seos_err_t ChanMux_read(
    unsigned int  chan,
    size_t        len,
    size_t*       read);


/**
 * @details open ethernet device simulated via ChanMUX
 * @ingroup SeosNwChanmuxIf
 *
 * @param channel_ctrl control channel
 * @param chan_id_data data channel
 *
 * @retval SEOS_SUCCESS or error code
 *
 */
seos_err_t
SeosNwChanmux_open(
    const ChanMux_channelCtx_t*  channel_ctrl,
    unsigned int                 chan_id_data);


/**
 * @details get MAC from ethernet device simulated via ChanMUX
 * @ingroup SeosNwChanmuxIf
 *
 * @param channel_ctrl control channel
 * @param chan_id_data data channel
 * @param mac recevied the MAC
 *
 * @retval SEOS_SUCCESS or error code
 *
 */
seos_err_t
SeosNwChanmux_get_mac(
    const ChanMux_channelCtx_t*  channel_ctrl,
    unsigned int                 chan_id_data,
    uint8_t*                     mac);
