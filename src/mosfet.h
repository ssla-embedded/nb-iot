/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */
/**@file
 *
 * @brief   MOSFET control for the User Interface module. The module uses PWM to
 *	    control RGB colors and light intensity.
 */

#ifndef MOSFET_H__
#define MOSFET_H__

#include <zephyr.h>
#include "mosfet_effect.h"

#ifdef __cplusplus
extern "C" {
#endif

/**@brief Initializes MOSFET in the user interface module. */
int mosfet_init(void);

/**@brief Starts the PWM and handling of MOSFET effects. */
void mosfet_start(void);

/**@brief Stops the MOSFET effects. */
void mosfet_stop(void);

/**@brief Sets MOSFET effect based in MOSFET state. */
void mosfet_set_effect(enum mosfet_pattern state);

/**@brief Sets MOSFET RGB and light intensity values, in 0 - 255 ranges. */
int mosfet_set_rgb(u8_t red, u8_t green, u8_t blue, u8_t yellow);

// Get the rgb MOSFET values
void mosfet_get_rgb(struct mosfet_color *colors);

#ifdef __cplusplus
}
#endif

#endif /* MOSFET_H__ */
