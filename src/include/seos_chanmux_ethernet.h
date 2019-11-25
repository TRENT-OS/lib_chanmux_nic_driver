/*
 *  seos_chanmux_ethernet.h
 *
 *  Copyright (C) 2019, Hensoldt Cyber GmbH
*/


#pragma once

typedef enum
{
    NW_CTRL_CMD_OPEN       =  0,   /*!< Open Channel */
    NW_CTRL_CMD_OPEN_CNF   =  1,   /*!< Open Confirmation */
    NW_CTRL_CMD_CLOSE      =  2,   /*!< Close Channel */
    NW_CTRL_CMD_CLOSE_CNF  =  3,   /*!< Close Confirmation */
    NW_CTRL_CMD_GETMAC     =  4,   /*!< GetMac for TAP */
    NW_CTRL_CMD_GETMAC_CNF =  5    /*!< GetMac Confirmation */
} Seos_NwCtrlCommand;
