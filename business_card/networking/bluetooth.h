#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "RTE_Components.h"
#ifdef CMSIS_device_header
#include CMSIS_device_header // IWYU pragma: keep
#endif

#define BLUETOOTH_MAX_ADV_PAYLOAD 31U

#ifdef __cplusplus
extern "C" {
#endif

void bluetooth_init(void);
void bluetooth_set_adv_payload(const uint8_t *payload, size_t length);
void bluetooth_broadcast_tick(void);
void bluetooth_shutdown(void);
bool bluetooth_is_ready(void);

#ifdef __cplusplus
}
#endif
