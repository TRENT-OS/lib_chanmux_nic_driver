/*
 *  SeosNwChanmuxIf.h
 *
 *  Copyright (C) 2019, Hensoldt Cyber GmbH
*/


/**
 * @defgroup SeosNwChanmuxIf SEOS Chanmux Interface
 * @file     SeosNwChanmuxIf.h
 * @brief    This file contains interfaces or API to interact with Chanmux \n
             This is mostly to send and receive data to/from proxy finally over TAP
 *
 */

#pragma once

#include <stdlib.h>
#include <stdint.h>
#include "SeosError.h"

typedef struct
{
    void*          data_port;
    unsigned int   id;
} ChanMux_channelCtx_t;


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
