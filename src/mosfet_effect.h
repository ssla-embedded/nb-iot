/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#ifndef LED_EFFECT_H__
#define LED_EFFECT_H__

/**
 * @brief LED Effect
 * @defgroup led_effect LED Effect
 * @{
 */

#include <zephyr/types.h>
#include <dk_buttons_and_leds.h>

#ifdef __cplusplus
extern "C" {
#endif

struct mosfet_color {
	u8_t c[4];
};

struct mosfet_effect_step {
	struct mosfet_color color;
	u16_t substep_count;
	u16_t substep_time;
};

struct mosfet_effect {
	struct mosfet_effect_step *steps;
	u16_t step_count;
	bool loop_forever;
};
#define UI_LED_ON(x)			(x)
#define UI_LED_BLINK(x)			((x) << 8)
#define UI_LED_GET_ON(x)		((x) & 0xFF)
#define UI_LED_GET_BLINK(x)		(((x) >> 8) & 0xFF)

#ifdef CONFIG_UI_LED_USE_PWM

#define UI_LED_ON_PERIOD_NORMAL		500
#define UI_LED_OFF_PERIOD_NORMAL	5000
#define UI_LED_ON_PERIOD_ERROR		500
#define UI_LED_OFF_PERIOD_ERROR		500

#define UI_LED_MAX			50

#define UI_LED_COLOR_OFF		LED_COLOR(0, 0, 0, 0)
#define UI_LED_COLOR_RED		LED_COLOR(UI_LED_MAX, 0, 0, 0)
#define UI_LED_COLOR_GREEN		LED_COLOR(0, UI_LED_MAX, 0, 0)
#define UI_LED_COLOR_BLUE		LED_COLOR(0, 0, UI_LED_MAX, 0)
#define UI_LED_COLOR_WHITE		LED_COLOR(UI_LED_MAX, UI_LED_MAX,      \
						  UI_LED_MAX, UI_LED_MAX)
#define UI_LED_COLOR_YELLOW		LED_COLOR(UI_LED_MAX, UI_LED_MAX, 0, UI_LED_MAX)
#define UI_LED_COLOR_CYAN		LED_COLOR(0, UI_LED_MAX, UI_LED_MAX, 0)
#define UI_LED_COLOR_PURPLE		LED_COLOR(UI_LED_MAX, 0, UI_LED_MAX, 0)

#define UI_LTE_DISCONNECTED_COLOR	UI_LED_COLOR_OFF
#define UI_LTE_CONNECTING_COLOR		UI_LED_COLOR_BLUE
#define UI_LTE_CONNECTED_COLOR		UI_LED_COLOR_BLUE
#define UI_CLOUD_CONNECTING_COLOR	UI_LED_COLOR_BLUE
#define UI_CLOUD_CONNECTED_COLOR	UI_LED_COLOR_GREEN
#define UI_CLOUD_PAIRING_COLOR		UI_LED_COLOR_YELLOW
#define UI_ACCEL_CALIBRATING_COLOR	UI_LED_COLOR_PURPLE
#define UI_LED_ERROR_CLOUD_COLOR	UI_LED_COLOR_RED
#define UI_LED_ERROR_BSD_REC_COLOR	UI_LED_COLOR_RED
#define UI_LED_ERROR_BSD_IRREC_COLOR	UI_LED_COLOR_RED
#define UI_LED_ERROR_LTE_LC_COLOR	UI_LED_COLOR_RED
#define UI_LED_ERROR_UNKNOWN_COLOR	UI_LED_COLOR_WHITE

#else

#define UI_LED_ON_PERIOD_NORMAL		500
#define UI_LED_OFF_PERIOD_NORMAL	500

#endif /* CONFIG_UI_LED_USE_PWM */

/**@brief UI LED state pattern definitions. */
enum mosfet_pattern {
#ifdef CONFIG_UI_LED_USE_PWM
	UI_LTE_DISCONNECTED,
	UI_LTE_CONNECTING,
	UI_LTE_CONNECTED,
	UI_CLOUD_CONNECTING,
	UI_CLOUD_CONNECTED,
	UI_CLOUD_PAIRING,
	UI_ACCEL_CALIBRATING,
	UI_LED_ERROR_CLOUD,
	UI_LED_ERROR_BSD_REC,
	UI_LED_ERROR_BSD_IRREC,
	UI_LED_ERROR_LTE_LC,
	UI_LED_ERROR_UNKNOWN,
#else /* LED patterns without using PWM. */
	UI_LTE_DISCONNECTED	= UI_LED_ON(0),
	UI_LTE_CONNECTING	= UI_LED_BLINK(DK_LED3_MSK),
	UI_LTE_CONNECTED	= UI_LED_ON(DK_LED3_MSK),
	UI_CLOUD_CONNECTING	= UI_LED_BLINK(DK_LED3_MSK | DK_LED4_MSK),
	UI_CLOUD_CONNECTED	= UI_LED_ON(DK_LED4_MSK),
	UI_CLOUD_PAIRING	= UI_LED_BLINK(DK_LED3_MSK | DK_LED4_MSK),
	UI_ACCEL_CALIBRATING	= UI_LED_ON(DK_LED1_MSK | DK_LED3_MSK),
	UI_LED_ERROR_CLOUD	= UI_LED_BLINK(DK_LED1_MSK | DK_LED4_MSK),
	UI_LED_ERROR_BSD_REC	= UI_LED_BLINK(DK_LED1_MSK | DK_LED3_MSK),
	UI_LED_ERROR_BSD_IRREC	= UI_LED_BLINK(DK_ALL_LEDS_MSK),
	UI_LED_ERROR_LTE_LC	= UI_LED_BLINK(DK_LED1_MSK | DK_LED2_MSK),
	UI_LED_ERROR_UNKNOWN	= UI_LED_ON(DK_ALL_LEDS_MSK)
#endif /* CONFIG_UI_LED_USE_PWM */
};


#define LED_COLOR(_r, _g, _b, _y) {		\
		.c = {_r, _g, _b, _y}	\
}
#define LED_NOCOLOR() {			\
		.c = {0, 0, 0, 0}		\
}

#define LED_EFFECT_LED_ON(_color)					       \
	{								       \
		.steps = ((const struct mosfet_effect_step[]) {		       \
			{						       \
				.color = _color,			       \
				.substep_count = 1,			       \
				.substep_time = 0,			       \
			},						       \
		}),							       \
		.step_count = 1,					       \
		.loop_forever = false,					       \
	}

#define LED_EFFECT_LED_OFF() LED_EFFECT_LED_ON(LED_NOCOLOR())

#define LED_EFFECT_LED_BLINK(_period, _color)				       \
	{								       \
		.steps = ((const struct mosfet_effect_step[]) {		       \
			{						       \
				.color = _color,			       \
				.substep_count = 1,			       \
				.substep_time = (_period),		       \
			},						       \
			{						       \
				.color = LED_NOCOLOR(),			       \
				.substep_count = 1,			       \
				.substep_time = (_period),		       \
			},						       \
		}),							       \
		.step_count = 2,					       \
		.loop_forever = true,					       \
	}

#define _BREATH_SUBSTEPS 15
#define _BREATH_PAUSE_SUBSTEPS 1
#define LED_EFFECT_LED_BREATHE(_period, _pause, _color)			       \
	{								       \
		.steps = ((struct mosfet_effect_step[]) {			       \
			{						       \
				.color = _color,			       \
				.substep_count = _BREATH_SUBSTEPS,	       \
				.substep_time = ((_period +		       \
					(_BREATH_SUBSTEPS - 1))		       \
					/ _BREATH_SUBSTEPS),		       \
			},						       \
			{						       \
				.color = _color,			       \
				.substep_count = 1,			       \
				.substep_time = _period,		       \
			},						       \
			{						       \
				.color = LED_NOCOLOR(),			       \
				.substep_count = _BREATH_SUBSTEPS,	       \
				.substep_time = ((_period +		       \
					(_BREATH_SUBSTEPS - 1))		       \
					/ _BREATH_SUBSTEPS),		       \
			},						       \
			{						       \
				.color = LED_NOCOLOR(),			       \
				.substep_count = _BREATH_PAUSE_SUBSTEPS,       \
				.substep_time = _pause,			       \
			},						       \
		}),							       \
		.step_count = 4,					       \
		.loop_forever = true,					       \
	}

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* LED_EFFECT_H_ */
