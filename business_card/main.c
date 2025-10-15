#include <stdbool.h>
#include <stdint.h>
#include "RTE_Components.h"
#include CMSIS_device_header
#include "./leds/leds.h"
#include "./networking/nfc.h"
#include "./networking/bluetooth.h"


#define LED_PIN         16U

static void busy_wait(uint32_t cycles) {
    while (cycles--) {
        __NOP();
    }
}

static void delay_ms(uint32_t milliseconds) {
    const uint32_t cycles_per_ms = 2300U;
    while (milliseconds--) {
        busy_wait(cycles_per_ms);
    }
}

int main(void) {
    nfc_init();
    nfc_start_sense();

    bluetooth_init();
    for (uint8_t i = 0; i < 8; i++) {
        setLedState(true, LED_PIN + i);
    }

    while (true) {
        nfc_poll();
        bluetooth_broadcast_tick();
        // delay_ms(100U);
    }
}
 