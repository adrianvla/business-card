#include <stdbool.h>
#include <stdint.h>
#include "RTE_Components.h"
#include CMSIS_device_header
#include "./leds/leds.h"


#define LED_PIN         16U
#define LED_PIN_MASK    (1UL << LED_PIN)

static void busy_wait(uint32_t cycles) {
    while (cycles--) {
        __NOP();
    }
}

static void delay_ms(uint32_t milliseconds) {
    const uint32_t cycles_per_ms = 2300U; // Approximation for 16 MHz core clock
    while (milliseconds--) {
        busy_wait(cycles_per_ms);
    }
}

int main(void) {
    NRF_P0->PIN_CNF[LED_PIN] = (GPIO_PIN_CNF_DIR_Output << GPIO_PIN_CNF_DIR_Pos) |
                               (GPIO_PIN_CNF_INPUT_Disconnect << GPIO_PIN_CNF_INPUT_Pos) |
                               (GPIO_PIN_CNF_PULL_Disabled << GPIO_PIN_CNF_PULL_Pos) |
                               (GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos) |
                               (GPIO_PIN_CNF_SENSE_Disabled << GPIO_PIN_CNF_SENSE_Pos);

    NRF_P0->OUTCLR = LED_PIN_MASK;

    bool led_on = false;

    while (true) {
        if (led_on) {
            NRF_P0->OUTCLR = LED_PIN_MASK;
        } else {
            NRF_P0->OUTSET = LED_PIN_MASK;
        }

        led_on = !led_on;
        delay_ms(500U);
    }
}
 