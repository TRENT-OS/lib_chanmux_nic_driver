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
seos_err_t chanmux_channel_ctrl_mutex_lock(void);
seos_err_t chanmux_channel_ctrl_mutex_unlock(void);
const seos_shared_buffer_t* get_network_stack_port_to(void);
const seos_shared_buffer_t* get_network_stack_port_from(void);
void network_stack_notify(void);

//------------------------------------------------------------------------------
// internal functions
//------------------------------------------------------------------------------
seos_err_t chanmux_nic_driver_init(void);
seos_err_t chanmux_nic_driver_loop(void);

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

seos_err_t
SeosNwChanmux_stopData(
    const ChanMux_channelCtx_t*  channel_ctrl,
    unsigned int                 chan_id_data);

seos_err_t
SeosNwChanmux_startData(
    const ChanMux_channelCtx_t*  channel_ctrl,
    unsigned int                 chan_id_data);
