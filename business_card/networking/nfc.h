#pragma once


#include <stdbool.h>

#include "RTE_Components.h"
#ifdef CMSIS_device_header
#include CMSIS_device_header // IWYU pragma: keep
#endif

#define NFC_PIN_NFC1   9U
#define NFC_PIN_NFC2   10U

#ifdef __cplusplus
extern "C" {
#endif

void nfc_init(void);
void nfc_start_sense(void);
void nfc_stop(void);
void nfc_poll(void);
bool nfc_is_field_present(void);
void nfc_clear_events(void);

#ifdef __cplusplus
}
#endif
