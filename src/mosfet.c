/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <zephyr.h>
#include <drivers/pwm.h>
#include <string.h>

#include "mosfet.h"
#include "mosfet_effect.h"

struct led {
	struct device *pwm_dev;

	size_t id;
	struct mosfet_color color;
	const struct mosfet_effect *effect;
	u16_t effect_step;
	u16_t effect_substep;

	struct k_delayed_work work;
};

static struct mosfet_color temp;

static const struct mosfet_effect effect[] = {
	[UI_LTE_DISCONNECTED] = LED_EFFECT_LED_BREATHE(UI_LED_ON_PERIOD_NORMAL,
					UI_LED_OFF_PERIOD_NORMAL,
					UI_LTE_DISCONNECTED_COLOR),
	[UI_LTE_CONNECTING] = LED_EFFECT_LED_BREATHE(UI_LED_ON_PERIOD_NORMAL,
					UI_LED_OFF_PERIOD_NORMAL,
					UI_LTE_CONNECTING_COLOR),
	[UI_LTE_CONNECTED] = LED_EFFECT_LED_BREATHE(UI_LED_ON_PERIOD_NORMAL,
					UI_LED_OFF_PERIOD_NORMAL,
					UI_LTE_CONNECTED_COLOR),
	[UI_CLOUD_CONNECTING] = LED_EFFECT_LED_BREATHE(UI_LED_ON_PERIOD_NORMAL,
					UI_LED_OFF_PERIOD_NORMAL,
					UI_CLOUD_CONNECTING_COLOR),
	[UI_CLOUD_CONNECTED] = LED_EFFECT_LED_BREATHE(UI_LED_ON_PERIOD_NORMAL,
					UI_LED_OFF_PERIOD_NORMAL,
					UI_CLOUD_CONNECTED_COLOR),
	[UI_CLOUD_PAIRING] = LED_EFFECT_LED_BREATHE(UI_LED_ON_PERIOD_NORMAL,
					UI_LED_OFF_PERIOD_NORMAL,
					UI_CLOUD_PAIRING_COLOR),
	[UI_ACCEL_CALIBRATING] = LED_EFFECT_LED_BREATHE(UI_LED_ON_PERIOD_NORMAL,
					UI_LED_OFF_PERIOD_NORMAL,
					UI_ACCEL_CALIBRATING_COLOR),
	[UI_LED_ERROR_CLOUD] = LED_EFFECT_LED_BREATHE(UI_LED_ON_PERIOD_ERROR,
					UI_LED_OFF_PERIOD_ERROR,
					UI_LED_ERROR_CLOUD_COLOR),
	[UI_LED_ERROR_BSD_REC] = LED_EFFECT_LED_BREATHE(UI_LED_ON_PERIOD_ERROR,
					UI_LED_OFF_PERIOD_ERROR,
					UI_LED_ERROR_BSD_REC_COLOR),
	[UI_LED_ERROR_BSD_IRREC] = LED_EFFECT_LED_BREATHE(
					UI_LED_ON_PERIOD_ERROR,
					UI_LED_OFF_PERIOD_ERROR,
					UI_LED_ERROR_BSD_IRREC_COLOR),
	[UI_LED_ERROR_LTE_LC] = LED_EFFECT_LED_BREATHE(UI_LED_ON_PERIOD_ERROR,
					UI_LED_OFF_PERIOD_ERROR,
					UI_LED_ERROR_LTE_LC_COLOR),
	[UI_LED_ERROR_UNKNOWN] = LED_EFFECT_LED_BREATHE(UI_LED_ON_PERIOD_ERROR,
					UI_LED_OFF_PERIOD_ERROR,
					UI_LED_ERROR_UNKNOWN_COLOR),
};

static struct mosfet_effect custom_effect =
	LED_EFFECT_LED_BREATHE(UI_LED_ON_PERIOD_NORMAL,
		UI_LED_OFF_PERIOD_NORMAL,
		LED_NOCOLOR());

static struct led leds;

static const size_t mosfet_pins[4] = {
	CONFIG_UI_MOSFET1_PIN,
	CONFIG_UI_MOSFET2_PIN,
	CONFIG_UI_MOSFET3_PIN,
	CONFIG_UI_MOSFET4_PIN,
};

static void pwm_out(struct led *led, struct mosfet_color *color)
{
	memset(&temp, 0, sizeof(struct mosfet_color));
	for (size_t i = 0; i < ARRAY_SIZE(color->c); i++) {
		pwm_pin_set_usec(led->pwm_dev, mosfet_pins[i],
				 255, color->c[i], 0);
		temp.c[i] = color->c[i];
	}
}

static void pwm_off(struct led *led)
{
	struct mosfet_color nocolor = {0};

	pwm_out(led, &nocolor);
}

static void work_handler(struct k_work *work)
{
	struct led *led = CONTAINER_OF(work, struct led, work);
	const struct mosfet_effect_step *effect_step =
		&leds.effect->steps[leds.effect_step];
	int substeps_left = effect_step->substep_count - leds.effect_substep;

	for (size_t i = 0; i < ARRAY_SIZE(leds.color.c); i++) {
		int diff = (effect_step->color.c[i] - leds.color.c[i]) /
			substeps_left;
		leds.color.c[i] += diff;
	}

	pwm_out(led, &leds.color);

	leds.effect_substep++;
	if (leds.effect_substep == effect_step->substep_count) {
		leds.effect_substep = 0;
		leds.effect_step++;

		if (leds.effect_step == leds.effect->step_count) {
			if (leds.effect->loop_forever) {
				leds.effect_step = 0;
			}
		} else {
			__ASSERT_NO_MSG(leds.effect->steps[leds.effect_step].substep_count > 0);
		}
	}

	if (leds.effect_step < leds.effect->step_count) {
		s32_t next_delay =
			leds.effect->steps[leds.effect_step].substep_time;

		k_delayed_work_submit(&leds.work, next_delay);
	}
}

static void led_update(struct led *led)
{
	k_delayed_work_cancel(&led->work);

	led->effect_step = 0;
	led->effect_substep = 0;

	if (!led->effect) {
		printk("No effect set");
		return;
	}

	__ASSERT_NO_MSG(led->effect->steps);

	if (led->effect->step_count > 0) {
		s32_t next_delay =
			led->effect->steps[led->effect_step].substep_time;

		k_delayed_work_submit(&led->work, next_delay);
	} else {
		printk("LED effect with no effect");
	}
}

int mosfet_init(void)
{
	const char *dev_name = "PWM_2";
	int err = 0;

	leds.pwm_dev = device_get_binding(dev_name);
	leds.id = 0;
	leds.effect = &effect[UI_LTE_DISCONNECTED];

	if (!leds.pwm_dev) {
		printk("Could not bind to device %s", dev_name);
		return -ENODEV;
	}

	k_delayed_work_init(&leds.work, work_handler);
	led_update(&leds);

	return err;
}

void mosfet_start(void)
{
#ifdef CONFIG_DEVICE_POWER_MANAGEMENT
	int err = device_set_power_state(leds.pwm_dev,
						DEVICE_PM_ACTIVE_STATE,
						NULL, NULL);
	if (err) {
		printk("PWM enable failed");
	}
#endif
	led_update(&leds);
}

void mosfet_stop(void)
{
	k_delayed_work_cancel(&leds.work);
#ifdef CONFIG_DEVICE_POWER_MANAGEMENT
	int err = device_set_power_state(leds.pwm_dev,
					 DEVICE_PM_SUSPEND_STATE,
					 NULL, NULL);
	if (err) {
		printk("PWM disable failed");
	}
#endif
	pwm_off(&leds);
}

void mosfet_set_effect(enum mosfet_pattern state)
{
	leds.effect = &effect[state];
	led_update(&leds);
}

int mosfet_set_rgb(u8_t red, u8_t green, u8_t blue, u8_t yellow)
{
	struct mosfet_effect effect =
		LED_EFFECT_LED_BREATHE(UI_LED_ON_PERIOD_NORMAL,
			UI_LED_OFF_PERIOD_NORMAL,
			LED_COLOR(red, green, blue, yellow));

	memcpy((void *)custom_effect.steps, (void *)effect.steps,
		effect.step_count * sizeof(struct mosfet_effect_step));

	leds.effect = &custom_effect;
	led_update(&leds);

	return 0;
}

void mosfet_get_rgb(struct mosfet_color *colors)
{
	colors = &temp;
}
