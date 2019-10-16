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
#include "seos_err.h"

/**
 * @details %ChanMux_write, Write data using ChanMux
 *
 * @ingroup SeosNwChanmuxIf
 * @param chan: Can take values. Possible values
                CHANNEL_NW_STACK_DATA
                CHANNEL_NW_STACK_DATA_2
                CHANNEL_NW_STACK_CTRL
                CHANNEL_NW_STACK_CTRL_2

 * @param len: Length of data to write
 * @param *written: is pointer to written which will contain how much of data was
                    actually written by Chanmux
 * @return Success or Failure.
 * @retval SEOS_SUCCESS or SEOS_ERROR_GENERIC
 *
 */

extern seos_err_t ChanMux_write(unsigned int chan, size_t len, size_t* written);

/**
 * @details %ChanMux_read, Read data using ChanMux
 * @ingroup SeosNwChanmuxIf

 * @param chan: Can take values. Possible values
                CHANNEL_NW_STACK_DATA
                CHANNEL_NW_STACK_DATA_2
                CHANNEL_NW_STACK_CTRL
                CHANNEL_NW_STACK_CTRL_2

 * @param len: Length of data to read

 * @param *read: is pointer to read which will contain how much of data was
                 actually read by Chanmux
 * @return Success or Failure.
 * @retval SEOS_SUCCESS or SEOS_ERROR_GENERIC
 *
 */

extern seos_err_t ChanMux_read(unsigned int chan, size_t len, size_t* read);


/**
 * @details %SeosNwChanmux_chanWriteSyncData, Write wrapper for ChanMux_write Data channel
 * @ingroup SeosNwChanmuxIf

 * @param *buf: Pointer of the data buffer containing data to be written

 * @param len: Length of data to write

 * @return Total number of bytes written
 * @retval length
 *
 */

size_t
SeosNwChanmux_chanWriteSyncData(
    const void*   buf,
    size_t        len);

/**
 * @details %SeosNwChanmux_chanWriteSyncCtrl, Write wrapper for ChanMux_write Ctrl channel
 * @ingroup SeosNwChanmuxIf

 * @param *buf: Pointer of the data buffer containing data to be written

 * @param len: Length of data to write

 * @return Total number of bytes written

 * @retval length
 *
 */

size_t
SeosNwChanmux_chanWriteSyncCtrl(
    const void*   buf,
    size_t        len);

/**
 * @details %SeosNwChanmux_chanRead, Read wrapper for ChanMux_Read non Blocking
 * @ingroup SeosNwChanmuxIf
 * @param chan: Chanmux Channel number to read from

 * @param buf: Buffer to read data into

 * @return Total number of bytes read

 * @retval length of bytes read
 *
 */

size_t
SeosNwChanmux_chanRead(
    unsigned int  chan,
    void*         buf,
    size_t        len);


/**
 * @details %SeosNwChanmux_chanReadBlocking, this is a wrapper for Chanmux_read. It is a blocking read.
 * @ingroup SeosNwChanmuxIf

 * @param chan: Chanmux Channel number to read from
   @param buf:  Pointer of the data buffer to be read into
 * @param len:  is the length of data to read

 * @return Total number of bytes read
 * @retval length of bytes read
 *
 */

size_t
SeosNwChanmux_chanReadBlocking(
    unsigned int  chan,
    char*         buf,
    size_t        len);


/**
 * @details %SeosNwChanmux_write_data, PicoTCP uses this API as an interface to use Chanmux.
 * @ingroup SeosNwChanmuxIf

 * @param *buffer: Pointer of the data buffer containing data to be written

 * @param len: is the length of the data to write

 * @return Total number of bytes written
 * @retval length written
 *
 */
int
SeosNwChanmux_write_data(
    void*   buffer,
    size_t  len);

/**
 * @details %SeosNwChanmux_read_data, PicoTCP uses this API as an interface to use Chanmux.
 * @ingroup SeosNwChanmuxIf

 * @param *buffer: Pointer of the data buffer containing data to be read into

 * @param len:  is the length of data to read

 * @return Total number of bytes read
 * @retval length read
 *
 */
int
SeosNwChanmux_read_data(
    void*   buffer,
    size_t  len);

/**
 * @details %SeosNwChanmux_get_mac, is an interface for picotcp to use Chanmux to get the tap mac address.
 * @ingroup SeosNwChanmuxIf

 * @param *name: name of the tap of which mac addr is requested (e.g. "tap0" or "tap1" etc)

 * @param *mac: will contain the mac addr filled

 * @return Mac addr filled for tap
 * @retval Mac addr filled
 *
 */

int
SeosNwChanmux_get_mac(
    char*     name,
    uint8_t*  mac);
