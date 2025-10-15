#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "RTE_Components.h"
#include CMSIS_device_header

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
