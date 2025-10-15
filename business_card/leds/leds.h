#include <stdbool.h>
#include <stdint.h>
#include "RTE_Components.h"
#ifdef CMSIS_device_header
#include CMSIS_device_header // IWYU pragma: keep
#endif

#define LED_FIRST_PIN   16U
#define LED_LAST_PIN    23U

static void setLedState(bool _state, char pin){
	uint8_t pin_index = (uint8_t)pin;

	if (pin_index < LED_FIRST_PIN || pin_index > LED_LAST_PIN) {
		return;
	}

	uint32_t mask = 1UL << pin_index;
	static uint32_t configured_pins_mask = 0U;

	if ((configured_pins_mask & mask) == 0U) {
		NRF_P0->PIN_CNF[pin_index] = (GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos) |
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