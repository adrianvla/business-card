#ifndef LEDS_H
#define LEDS_H

#include <stdbool.h>
#include <stdint.h>
#include "RTE_Components.h"
#ifdef CMSIS_device_header
#include CMSIS_device_header // IWYU pragma: keep
#endif

#define LED_COUNT       8U
static const uint8_t k_led_gpio_pins[LED_COUNT] = {
    16U, 17U, 18U, 19U, 20U, 21U, 22U, 23U
};

static void setLedState(bool _state, char pin){
	int index = (int)pin;
	if (index < 0 || index >= LED_COUNT) {
		return;
	}

	uint8_t gpio_pin = k_led_gpio_pins[(uint8_t)index];
	uint32_t mask = 1UL << gpio_pin;
	static uint32_t configured_pins_mask = 0U;

	if ((configured_pins_mask & mask) == 0U) {
		NRF_P0->PIN_CNF[gpio_pin] = (GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos) |
									 (GPIO_PIN_CNF_INPUT_Disconnect << GPIO_PIN_CNF_INPUT_Pos) |
									 (GPIO_PIN_CNF_PULL_Disabled << GPIO_PIN_CNF_PULL_Pos) |
									 (GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos) |
									 (GPIO_PIN_CNF_SENSE_Disabled << GPIO_PIN_CNF_SENSE_Pos);
		configured_pins_mask |= mask;
	}

	if (_state) {
		NRF_P0->OUTCLR = mask; // Active-low LED: drive low to turn on
	} else {
		NRF_P0->OUTSET = mask; // Drive high to turn off
	}
}


static void set_all_leds(bool state) {
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        setLedState(state, (char)i);
    }
}

static void set_alternate_leds(bool show_odd_group) {
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        bool is_odd = (i & 0x01u) != 0u;
        bool led_on = show_odd_group ? is_odd : !is_odd;
        setLedState(led_on, (char)i);
    }
}

#endif