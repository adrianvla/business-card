#include "bluetooth.h"

#include <string.h>

#define BLE_ACCESS_ADDRESS          0x8E89BED6UL
#define BLE_CRC_INIT                0x555555UL
#define BLE_CRC_POLY                0x00065BUL

#define BLE_ADV_HEADER_TYPE_NONCONN 0x42u /* ADV_NONCONN_IND (0x2) | TxAdd=1 (bit6, random addr) */
#define BLE_ADV_ADDRESS_SIZE        6u
#define BLE_ADV_HEADER_SIZE         2u

typedef struct {
    uint8_t rf_channel;
    uint8_t whiten_channel;
} ble_channel_config_t;

static const ble_channel_config_t k_adv_channels[] = {
    { 2u, 37u },  // 2402 MHz, ADV channel 37
    { 26u, 38u }, // 2426 MHz, ADV channel 38
    { 80u, 39u }  // 2480 MHz, ADV channel 39
};
static uint8_t s_adv_pdu[BLE_ADV_HEADER_SIZE + BLE_ADV_ADDRESS_SIZE + BLUETOOTH_MAX_ADV_PAYLOAD] = { 0 };
static size_t s_adv_payload_len = 0u;
static bool s_bluetooth_initialized = false;
static bool s_test_carrier_enabled = false;
static const uint8_t s_test_carrier_packet = 0xFFu;

static const uint8_t k_default_adv_address[BLE_ADV_ADDRESS_SIZE] = {
    0xC1, 0xDE, 0x1D, 0xEA, 0x52, 0xF1 /* 0xF1: top bits 11 = static random (valid AdvA class) */
};

static const uint8_t k_default_adv_payload[] = {
    0x02, 0x01, 0x06,             // Flags: LE General Discoverable, BR/EDR not supported
    0x05, 0x09, 'C', 'A', 'R', 'D' // Complete Local Name: "CARD"
};

static void bluetooth_enable_hfclk(void) {
    if (NRF_CLOCK->EVENTS_HFCLKSTARTED) {
        return;
    }

    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0u;
    NRF_CLOCK->TASKS_HFCLKSTART = 1u;
    while (NRF_CLOCK->EVENTS_HFCLKSTARTED == 0u) {
        __NOP();
    }
}

static void bluetooth_disable_hfclk(void) {
    NRF_CLOCK->TASKS_HFCLKSTOP = 1u;
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0u;
}

static void bluetooth_program_packet_length(size_t payload_len) {
    const size_t total_len = BLE_ADV_ADDRESS_SIZE + payload_len;
    s_adv_pdu[0] = BLE_ADV_HEADER_TYPE_NONCONN;
    s_adv_pdu[1] = (uint8_t)total_len;
}

static void bluetooth_apply_default_payload(void) {
    bluetooth_program_packet_length(s_adv_payload_len);
    memcpy(&s_adv_pdu[2], k_default_adv_address, BLE_ADV_ADDRESS_SIZE);
}

static void bluetooth_configure_radio(void) {
    NRF_RADIO->POWER = 1u;
    NRF_RADIO->TXPOWER = RADIO_TXPOWER_TXPOWER_0dBm;
    NRF_RADIO->MODE = RADIO_MODE_MODE_Ble_1Mbit;

    NRF_RADIO->PREFIX0 = (uint32_t)((BLE_ACCESS_ADDRESS >> 24) & 0xFFu);
    NRF_RADIO->BASE0 = (uint32_t)((BLE_ACCESS_ADDRESS << 8) & 0xFFFFFF00u);
    NRF_RADIO->TXADDRESS = 0u;
    NRF_RADIO->RXADDRESSES = 1u << 0;

    NRF_RADIO->PCNF0 = (1u << RADIO_PCNF0_S0LEN_Pos) |
                       (0u << RADIO_PCNF0_S1LEN_Pos) |
                       (8u << RADIO_PCNF0_LFLEN_Pos);

    NRF_RADIO->PCNF1 = ((uint32_t)(BLE_ADV_HEADER_SIZE + BLE_ADV_ADDRESS_SIZE + BLUETOOTH_MAX_ADV_PAYLOAD) << RADIO_PCNF1_MAXLEN_Pos) |
                       (0u << RADIO_PCNF1_STATLEN_Pos) |
                       (3u << RADIO_PCNF1_BALEN_Pos) |
                       (RADIO_PCNF1_ENDIAN_Little << RADIO_PCNF1_ENDIAN_Pos) |
                       (RADIO_PCNF1_WHITEEN_Enabled << RADIO_PCNF1_WHITEEN_Pos);

    NRF_RADIO->CRCCNF = (RADIO_CRCCNF_LEN_Three << RADIO_CRCCNF_LEN_Pos) |
                        (RADIO_CRCCNF_SKIPADDR_Skip << RADIO_CRCCNF_SKIPADDR_Pos);
    NRF_RADIO->CRCINIT = BLE_CRC_INIT;
    NRF_RADIO->CRCPOLY = BLE_CRC_POLY;

    NRF_RADIO->SHORTS = 0u;
}

static void bluetooth_apply_channel(const ble_channel_config_t *config) {
    NRF_RADIO->FREQUENCY = config->rf_channel;
    NRF_RADIO->DATAWHITEIV = (uint32_t)(config->whiten_channel & 0x3Fu);
}

static void bluetooth_wait_for_event(volatile uint32_t *event_reg) {
    while (*event_reg == 0u) {
        __NOP();
    }
}

static void bluetooth_transmit_on_channel(const ble_channel_config_t *channel) {
    bluetooth_apply_channel(channel);

    NRF_RADIO->PACKETPTR = (uint32_t)(uintptr_t)s_adv_pdu;

    NRF_RADIO->EVENTS_READY = 0u;
    NRF_RADIO->EVENTS_END = 0u;
    NRF_RADIO->EVENTS_DISABLED = 0u;

    NRF_RADIO->TASKS_TXEN = 1u;

    bluetooth_wait_for_event(&NRF_RADIO->EVENTS_READY);
    NRF_RADIO->EVENTS_READY = 0u;

    NRF_RADIO->TASKS_START = 1u;

    bluetooth_wait_for_event(&NRF_RADIO->EVENTS_END);
    NRF_RADIO->EVENTS_END = 0u;

    NRF_RADIO->TASKS_DISABLE = 1u;
    bluetooth_wait_for_event(&NRF_RADIO->EVENTS_DISABLED);
    NRF_RADIO->EVENTS_DISABLED = 0u;
}

void bluetooth_init(void) {
    if (s_bluetooth_initialized) {
        return;
    }

    bluetooth_enable_hfclk();

    s_adv_payload_len = 0u;
    memset(s_adv_pdu, 0, sizeof(s_adv_pdu));
    bluetooth_apply_default_payload();

    s_adv_payload_len = sizeof(k_default_adv_payload);
    memcpy(&s_adv_pdu[2u + BLE_ADV_ADDRESS_SIZE], k_default_adv_payload, s_adv_payload_len);
    bluetooth_program_packet_length(s_adv_payload_len);

    bluetooth_configure_radio();

    s_bluetooth_initialized = true;
}

bool bluetooth_is_ready(void) {
    return s_bluetooth_initialized;
}

void bluetooth_set_adv_payload(const uint8_t *payload, size_t length) {
    if (!s_bluetooth_initialized) {
        bluetooth_init();
    }

    if (payload == NULL || length == 0u) {
        s_adv_payload_len = 0u;
        bluetooth_program_packet_length(s_adv_payload_len);
        return;
    }

    if (length > BLUETOOTH_MAX_ADV_PAYLOAD) {
        length = BLUETOOTH_MAX_ADV_PAYLOAD;
    }

    memcpy(&s_adv_pdu[2u + BLE_ADV_ADDRESS_SIZE], payload, length);
    s_adv_payload_len = length;
    bluetooth_program_packet_length(s_adv_payload_len);
}

void bluetooth_broadcast_tick(void) {
    if (!s_bluetooth_initialized) {
        return;
    }

    const size_t channel_count = sizeof(k_adv_channels) / sizeof(k_adv_channels[0]);
    for (size_t index = 0u; index < channel_count; ++index) {
        bluetooth_transmit_on_channel(&k_adv_channels[index]);

        if (index + 1u < channel_count) {
            for (volatile uint32_t delay = 0u; delay < 200u; ++delay) {
                __NOP();
            }
        }
    }
}

void bluetooth_shutdown(void) {
    if (!s_bluetooth_initialized) {
        return;
    }

    NRF_RADIO->EVENTS_DISABLED = 0u;
    NRF_RADIO->TASKS_DISABLE = 1u;
    bluetooth_wait_for_event(&NRF_RADIO->EVENTS_DISABLED);

    NRF_RADIO->POWER = 0u;
    s_bluetooth_initialized = false;

    bluetooth_disable_hfclk();
}

void bluetooth_enable_test_carrier(void) {
    if (s_test_carrier_enabled) {
        return;
    }

    bluetooth_enable_hfclk();

    NRF_RADIO->EVENTS_DISABLED = 0u;
    NRF_RADIO->TASKS_DISABLE = 1u;
    while (NRF_RADIO->EVENTS_DISABLED == 0u) {
        __NOP();
    }
    NRF_RADIO->EVENTS_DISABLED = 0u;

    NRF_RADIO->POWER = 1u;
    NRF_RADIO->SHORTS = 0u;
    NRF_RADIO->TXPOWER = RADIO_TXPOWER_TXPOWER_Pos4dBm;
    NRF_RADIO->MODE = RADIO_MODE_MODE_Ble_1Mbit;
    NRF_RADIO->FREQUENCY = 40u; // 2440 MHz

    NRF_RADIO->PCNF0 = (0u << RADIO_PCNF0_S0LEN_Pos) |
                       (0u << RADIO_PCNF0_LFLEN_Pos) |
                       (0u << RADIO_PCNF0_S1LEN_Pos);

    NRF_RADIO->PCNF1 = (1u << RADIO_PCNF1_MAXLEN_Pos) |
                       (0u << RADIO_PCNF1_STATLEN_Pos) |
                       (3u << RADIO_PCNF1_BALEN_Pos) |
                       (RADIO_PCNF1_ENDIAN_Little << RADIO_PCNF1_ENDIAN_Pos) |
                       (0u << RADIO_PCNF1_WHITEEN_Pos);

    NRF_RADIO->MODECNF0 = (RADIO_MODECNF0_RU_Default << RADIO_MODECNF0_RU_Pos) |
                          (RADIO_MODECNF0_DTX_Center << RADIO_MODECNF0_DTX_Pos);
    NRF_RADIO->CRCCNF = 0u;
    NRF_RADIO->BASE0 = 0u;
    NRF_RADIO->PREFIX0 = 0u;
    NRF_RADIO->DATAWHITEIV = 0u;

    NRF_RADIO->PACKETPTR = (uint32_t)(uintptr_t)&s_test_carrier_packet;

    NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_END_START_Msk;

    NRF_RADIO->EVENTS_READY = 0u;
    NRF_RADIO->EVENTS_END = 0u;
    NRF_RADIO->EVENTS_DISABLED = 0u;

    NRF_RADIO->TASKS_TXEN = 1u;
    while (NRF_RADIO->EVENTS_READY == 0u) {
        __NOP();
    }
    NRF_RADIO->EVENTS_READY = 0u;

    NRF_RADIO->TASKS_START = 1u;

    s_test_carrier_enabled = true;
}
