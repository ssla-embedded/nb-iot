/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <stdio.h>
#include <zephyr.h>
#include <drivers/pwm.h>

#define MOSFET_MIN_FREQUENCY		CONFIG_UI_MOSFET_MIN_FREQUENCY
#define MOSFET_MAX_FREQUENCY		CONFIG_UI_MOSFET_MAX_FREQUENCY
#define MOSFET_MIN_INTENSITY		0
#define MOSFET_MAX_INTENSITY		100
#define MOSFET_MIN_DUTY_CYCLE_DIV	100
#define MOSFET_MAX_DUTY_CYCLE_DIV	2
#define MOSFET_MAX_DEVICES		4

struct device *mos_dev;
static atomic_t mosfet_enabled[MOSFET_MAX_DEVICES];
static int mosfet_intensity[MOSFET_MAX_DEVICES];

static const size_t mosfet_pins[MOSFET_MAX_DEVICES] = {
        CONFIG_UI_MOSFET1_PIN,
        CONFIG_UI_MOSFET2_PIN,
        CONFIG_UI_MOSFET3_PIN,
        CONFIG_UI_MOSFET4_PIN,
};

static u32_t intensity_to_duty_cycle_divisor(u8_t intensity)
{
	return MIN(
		MAX(((intensity - MOSFET_MIN_INTENSITY) *
		    (MOSFET_MAX_DUTY_CYCLE_DIV - MOSFET_MIN_DUTY_CYCLE_DIV) /
		    (MOSFET_MAX_INTENSITY - MOSFET_MIN_INTENSITY) +
		    MOSFET_MIN_DUTY_CYCLE_DIV),
		    MOSFET_MAX_DUTY_CYCLE_DIV),
		MOSFET_MIN_DUTY_CYCLE_DIV);
}

static int pwm_out(u32_t frequency, u8_t intensity, u8_t mosfet)
{
	static u32_t prev_period[MOSFET_MAX_DEVICES];
	u32_t period = (frequency > 0) ? USEC_PER_SEC / frequency : 0;
	u32_t duty_cycle = (intensity == 0) ? 0 :
		period / intensity_to_duty_cycle_divisor(intensity);

	/* Applying workaround due to limitations in PWM driver that doesn't
	 * allow changing period while PWM is running. Setting pulse to 0
	 * disables the PWM, but not before the current period is finished.
	 */
         printk("buzzer prev_period=%d, duty_cycle = %d, period = %d, pin number = %d \n", prev_period[mosfet], duty_cycle, period, mosfet_pins[mosfet]);
	if (prev_period[mosfet]) {
		pwm_pin_set_usec(mos_dev, mosfet_pins[mosfet],
				 prev_period[mosfet], 0, 0);
		k_sleep(MAX(K_MSEC(prev_period[mosfet] / USEC_PER_MSEC), K_MSEC(1)));
	}

	prev_period[mosfet] = period;

	return pwm_pin_set_usec(mos_dev, mosfet_pins[mosfet],
				period, duty_cycle, 0);
}

static void mosfet_disable(u8_t mosfet)
{
	atomic_set(&mosfet_enabled[mosfet], 0);
	mosfet_intensity[mosfet] = 0;
	pwm_out(0, 0, mosfet);

#ifdef CONFIG_DEVICE_POWER_MANAGEMENT
	int err = device_set_power_state(mos_dev,
					 DEVICE_PM_SUSPEND_STATE,
					 NULL, NULL);
	if (err) {
		printk("PWM disable failed");
	}
#endif
}

static int mosfet_enable(u8_t mosfet)
{
	int err = 0;

	atomic_set(&mosfet_enabled[mosfet], 1);
	mosfet_intensity[mosfet] = 0;
#ifdef CONFIG_DEVICE_POWER_MANAGEMENT
	err = device_set_power_state(mos_dev,
					 DEVICE_PM_ACTIVE_STATE,
					 NULL, NULL);
	if (err) {
		printk("PWM enable failed");
		return err;
	}
#endif

	return err;
}

int mosfet_init(void)
{
	const char *dev_name = "PWM_2";
	int err = 0;

	mos_dev = device_get_binding(dev_name);
	if (!mos_dev) {
                printk("Could not bind to MOSFET device\n");
		err = -ENODEV;
	}

	//mosfet_enable();
	return err;
}

int mosfet_set_frequency(u32_t frequency, u8_t intensity, u8_t mosfet)
{
	if (mosfet >= MOSFET_MAX_DEVICES || mosfet < 0) {
		return -EINVAL;
	}

	if (frequency == 0 || intensity == 0) {
		printk("Frequency set to 0, disabling PWM\n");
		mosfet_disable(mosfet);
		return 0;
	}

	if ((frequency < MOSFET_MIN_FREQUENCY) ||
	    (frequency > MOSFET_MAX_FREQUENCY)) {
		return -EINVAL;
	}

	if ((intensity < MOSFET_MIN_INTENSITY) ||
	    (intensity > MOSFET_MAX_INTENSITY)) {
		return -EINVAL;
	}

	if (!atomic_get(&mosfet_enabled[mosfet])) {
		mosfet_enable(mosfet);
	}
	mosfet_intensity[mosfet] = intensity;
	return pwm_out(frequency, intensity, mosfet);
}

int is_mosfet_enabled(u8_t mosfet)
{
	int temp = 0;

	if (atomic_get(&mosfet_enabled[mosfet])) {
		temp = mosfet_intensity[mosfet] << 4 ;
		temp |= 1 << 0;
	}
	return temp;
}

