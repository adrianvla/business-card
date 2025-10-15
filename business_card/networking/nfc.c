#include "nfc.h"

#define NFC_PIN_CONFIG ((GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) | \
                        (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) | \
                        (GPIO_PIN_CNF_PULL_Disabled << GPIO_PIN_CNF_PULL_Pos) | \
                        (GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos) | \
                        (GPIO_PIN_CNF_SENSE_Disabled << GPIO_PIN_CNF_SENSE_Pos))

static bool s_nfc_initialized = false;
static bool s_field_present = false;

static void nfc_configure_pins(void) {
    NRF_P0->PIN_CNF[NFC_PIN_NFC1] = NFC_PIN_CONFIG;
    NRF_P0->PIN_CNF[NFC_PIN_NFC2] = NFC_PIN_CONFIG;
}

void nfc_init(void) {
    if (s_nfc_initialized) {
        return;
    }

    nfc_configure_pins();

    NRF_NFCT->TASKS_DISABLE = 1;
    NRF_NFCT->INTENCLR = 0xFFFFFFFFu;
    NRF_NFCT->SHORTS = NFCT_SHORTS_FIELDDETECTED_ACTIVATE_Msk |
                       NFCT_SHORTS_FIELDLOST_SENSE_Msk;

    NRF_NFCT->EVENTS_FIELDDETECTED = 0;
    NRF_NFCT->EVENTS_FIELDLOST = 0;
    NRF_NFCT->EVENTS_SELECTED = 0;
    NRF_NFCT->EVENTS_STARTED = 0;

    s_field_present = false;
    s_nfc_initialized = true;
}

void nfc_start_sense(void) {
    if (!s_nfc_initialized) {
        nfc_init();
    }

    NRF_NFCT->TASKS_SENSE = 1;
}

void nfc_stop(void) {
    if (!s_nfc_initialized) {
        return;
    }

    NRF_NFCT->TASKS_DISABLE = 1;
    s_field_present = false;
}

void nfc_poll(void) {
    if (!s_nfc_initialized) {
        return;
    }

    if (NRF_NFCT->EVENTS_FIELDDETECTED) {
        NRF_NFCT->EVENTS_FIELDDETECTED = 0;
        s_field_present = true;
        NRF_NFCT->TASKS_ACTIVATE = 1;
    }

    if (NRF_NFCT->EVENTS_SELECTED) {
        NRF_NFCT->EVENTS_SELECTED = 0;
        NRF_NFCT->TASKS_GOSLEEP = 1;
    }

    if (NRF_NFCT->EVENTS_STARTED) {
        NRF_NFCT->EVENTS_STARTED = 0;
    }

    if (NRF_NFCT->EVENTS_FIELDLOST) {
        NRF_NFCT->EVENTS_FIELDLOST = 0;
        s_field_present = false;
        NRF_NFCT->TASKS_DISABLE = 1;
        NRF_NFCT->TASKS_SENSE = 1;
    }
}

bool nfc_is_field_present(void) {
    return s_field_present;
}

void nfc_clear_events(void) {
    NRF_NFCT->EVENTS_FIELDDETECTED = 0;
    NRF_NFCT->EVENTS_FIELDLOST = 0;
    NRF_NFCT->EVENTS_SELECTED = 0;
    NRF_NFCT->EVENTS_STARTED = 0;
}
