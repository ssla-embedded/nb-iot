/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */
/**@file
 *
 * @brief   Buzzer control for the User Interface module. The module uses PWM to
 *	    control the buzzer output frequency.
 */

#ifndef MOSFET_H__
#define MOSFET_H__

#include <zephyr.h>

#ifdef __cplusplus
extern "C" {
#endif

/**@brief Initialize buzzer in the user interface module. */
int mosfet_init(void);

int mosfet_set_frequency(u32_t frequency, u8_t intensity, u8_t mosfet);

int is_mosfet_enabled(u8_t mosfet);

#ifdef __cplusplus
}
#endif

#endif /* MOSFET_H__ */
