/*
 * Copyright (C) 2019-2024, HENSOLDT Cyber GmbH
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * For commercial licensing, contact: info.cyber@hensoldt.net
 */

#pragma once

#include "OS_Error.h"
#include "ChanMux/ChanMuxCommon.h"
#include <stddef.h>
#include <stdint.h>

//------------------------------------------------------------------------------
// Configuration Wrappers
//------------------------------------------------------------------------------
const ChanMux_ChannelOpsCtx_t *get_chanmux_channel_ctrl(void);
const ChanMux_ChannelOpsCtx_t *get_chanmux_channel_data(void);
void chanmux_channel_data_wait(void);
void chanmux_channel_ctrl_wait(void);
OS_Error_t chanmux_channel_ctrl_mutex_lock(void);
OS_Error_t chanmux_channel_ctrl_mutex_unlock(void);
const OS_SharedBuffer_t *get_network_stack_port_to(void);
const OS_SharedBuffer_t *get_network_stack_port_from(void);
void network_stack_notify(void);

//------------------------------------------------------------------------------
// internal functions
//------------------------------------------------------------------------------
OS_Error_t chanmux_nic_driver_loop(void);

/**
 * @details open ethernet device simulated via ChanMUX
 * @ingroup NwChanmuxIf
 *
 * @param channel_ctrl control channel
 * @param chan_id_data data channel
 *
 * @retval OS_SUCCESS or error code
 *
 */
OS_Error_t
chanmux_nic_channel_open(
    const ChanMux_ChannelOpsCtx_t *channel_ctrl,
    unsigned int chan_id_data);

/**
 * @details get MAC from ethernet device simulated via ChanMUX
 * @ingroup NwChanmuxIf
 *
 * @param channel_ctrl control channel
 * @param chan_id_data data channel
 * @param mac recevied the MAC
 *
 * @retval OS_SUCCESS or error code
 *
 */
OS_Error_t
chanmux_nic_ctrl_get_mac(
    const ChanMux_ChannelOpsCtx_t *channel_ctrl,
    unsigned int chan_id_data,
    uint8_t *mac);

OS_Error_t
chanmux_nic_ctrl_stopData(
    const ChanMux_ChannelOpsCtx_t *channel_ctrl,
    unsigned int chan_id_data);

OS_Error_t
chanmux_nic_ctrl_startData(
    const ChanMux_ChannelOpsCtx_t *channel_ctrl,
    unsigned int chan_id_data);
