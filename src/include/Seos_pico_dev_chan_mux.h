/*
 *  Seos_pico_dev_chan_mux.h
 *
 *  Copyright (C) 2019, Hensoldt Cyber GmbH
*/

/**
 * @defgroup Seos_pico_dev_chan_mux SEOS API's for PICO driver Chanmux
 * @file     Seos_pico_dev_chan_mux.h
 * @brief    This file contains interfaces or API for PicoTCP to interact with Chanmux\n
             These APIs create and destroy a TAP device.
 *
 */

#ifndef INCLUDE_PICO_CHAN_MUX_TAP
#define INCLUDE_PICO_CHAN_MUX_TAP
#include "pico_config.h"
#include "pico_device.h"


/**
 * @details: %pico_chan_mux_tap_destroy, Destroy tap interface created. Since we do not have any physical tap interface,
             this is dummy at the moment.
 * @ingroup Seos_pico_dev_chan_mux
 * @param pico_device: the tap device which was created and needs to be destroyed.

 * @return none
 * @retval none
 *
 */

void pico_chan_mux_tap_destroy ( struct pico_device* tap );

/**
 * @details %pico_chan_mux_tap_create, Create tap interface using TAP on proxy. Since we do not have any physical
 *           tap interface it actually triggers proxy to get the necessary info such as mac, informs picotcp about this
 *           device and assigns picotcp call back functions for polling and writing data.
 *           Called when Network stack is initialized.

 * @ingroup Seos_pico_dev_chan_mux
 *
 * @param *name: the tap device which needs to be created (such as "tap0", "tap1" etc)
 * @return pico_device *, pointer to device created.
 * @retval pico_device*
 */

struct pico_device* pico_chan_mux_tap_create (char* name);

#endif
