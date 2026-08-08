#include "nfc.h"

#include <string.h>

#define NFC_PIN_CONFIG ((GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos) | \
                        (GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos) | \
                        (GPIO_PIN_CNF_PULL_Disabled << GPIO_PIN_CNF_PULL_Pos) | \
                        (GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos) | \
                        (GPIO_PIN_CNF_SENSE_Disabled << GPIO_PIN_CNF_SENSE_Pos))

/* T2T tag geometry: 1024-byte tag, 4-byte blocks, READ returns 4 blocks (16 bytes). */
#define NFC_T2T_MEMORY_SIZE       1024U
#define NFC_T2T_BLOCK_SIZE        4U
#define NFC_T2T_READ_BLOCKS       4U
#define NFC_T2T_CC_BLOCK          3U
#define NFC_T2T_FIRST_DATA_BLOCK  4U
#define NFC_T2T_MAX_READ_BLOCK    ((NFC_T2T_MEMORY_SIZE / NFC_T2T_BLOCK_SIZE) - NFC_T2T_READ_BLOCKS)

#define NFC_FRAME_BUFFER_SIZE     64U
#define NFC_T2T_UID_SIZE          7U

/* Frame delay window (13.56 MHz ticks): 0x1000 ~ 302 us. Matches Nordic's reference driver. */
#define NFC_FRAME_DELAY_MAX       0x1000U

/* NFC-A frame configuration: CRC16 + SoF + parity for both directions. */
#define NFC_FRAMECONFIG           (0x10U | 0x04U | 0x01U)

/* nRF52832 NFCT soft-reset register (peripheral base + 0xFFC). */
#define NFC_PERIPHERAL_RESET      (*(volatile uint32_t *)0x40005FFC)
/* NFCT AUTOCOLRES register: bit 0 = 0 enables the hardware anti-collision engine. */
#define NFC_AUTOCOLRES_REG        (*(volatile uint32_t *)0x4000559C)

/* vCard 3.0 payload served as the NDEF MIME record. */
static const char k_vcard[] =
    "BEGIN:VCARD\r\n"
    "VERSION:3.0\r\n"
    "FN:Adrian Vlasov\r\n"
    "N:Vlasov;Adrian;;;\r\n"
    "URL:https://morisinc.net\r\n"
    "END:VCARD\r\n";

static const char k_ndef_type[] = "text/vcard";

static bool s_nfc_initialized = false;
static bool s_field_present = false;
static uint8_t s_read_count = 0;

static uint8_t s_uid[NFC_T2T_UID_SIZE];
static uint8_t s_t2t_memory[NFC_T2T_MEMORY_SIZE];
__ALIGNED(4) static uint8_t s_frame_buffer[NFC_FRAME_BUFFER_SIZE];

static void nfc_ensure_hfclk(void) {
    if (NRF_CLOCK->EVENTS_HFCLKSTARTED) {
        return;
    }

    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0u;
    NRF_CLOCK->TASKS_HFCLKSTART = 1u;
    while (NRF_CLOCK->EVENTS_HFCLKSTARTED == 0u) {
        __NOP();
    }
}

static void nfc_configure_pins(void) {
    NRF_P0->PIN_CNF[NFC_PIN_NFC1] = NFC_PIN_CONFIG;
    NRF_P0->PIN_CNF[NFC_PIN_NFC2] = NFC_PIN_CONFIG;
}

/* Fixed 7-byte NFCID1 (double size). First byte must not be the cascade tag 0x88. */
static const uint8_t k_uid[NFC_T2T_UID_SIZE] = { 0x04u, 0x12u, 0x34u, 0x56u, 0x78u, 0x9Au, 0xBCu };

/* Build the 1024-byte T2T memory image: UID/BCC, CC block, NDEF TLV with the vCard. */
static void nfc_build_t2t_memory(void) {
    memset(s_t2t_memory, 0, sizeof(s_t2t_memory));

    /* Blocks 0-2: NFCID1 + BCC + internal/lock bytes. */
    memcpy(&s_t2t_memory[0], s_uid, 3u);
    s_t2t_memory[3] = s_uid[3];
    memcpy(&s_t2t_memory[4], &s_uid[4], 3u);
    s_t2t_memory[7] = s_uid[0] ^ s_uid[1] ^ s_uid[2] ^ s_uid[3] ^ s_uid[4] ^ s_uid[5] ^ s_uid[6];
    s_t2t_memory[10] = 0x48u; /* static lock: data area locked (read-only tag) */

    /* Block 3: Capability Container. 0x7E = 1008-byte data area / 8 (Nordic convention). */
    s_t2t_memory[NFC_T2T_CC_BLOCK * NFC_T2T_BLOCK_SIZE + 0] = 0xE1u;
    s_t2t_memory[NFC_T2T_CC_BLOCK * NFC_T2T_BLOCK_SIZE + 1] = 0x10u;
    s_t2t_memory[NFC_T2T_CC_BLOCK * NFC_T2T_BLOCK_SIZE + 2] = 0x06u; /* CCLEN: same as NTAG, accepted by all readers */
    s_t2t_memory[NFC_T2T_CC_BLOCK * NFC_T2T_BLOCK_SIZE + 3] = 0x00u;

    /* Block 4+: NDEF TLV wrapping the vCard MIME record, terminated by TLV 0xFE. */
    const size_t vcard_len = strlen(k_vcard);
    const size_t type_len = sizeof(k_ndef_type) - 1u;
    const size_t ndef_len = 3u + type_len + vcard_len; /* record header + type + payload */
    uint8_t *p = &s_t2t_memory[NFC_T2T_FIRST_DATA_BLOCK * NFC_T2T_BLOCK_SIZE];

    p[0] = 0x03u;              /* NDEF TLV tag */
    p[1] = (uint8_t)ndef_len;  /* 1-byte length, message < 256 bytes */
    p[2] = 0xD1u;              /* MB|ME|SR, TNF = 001 (MIME media) */
    p[3] = (uint8_t)type_len;
    p[4] = (uint8_t)vcard_len;
    memcpy(&p[5], k_ndef_type, type_len);
    memcpy(&p[5 + type_len], k_vcard, vcard_len);
    p[2 + ndef_len] = 0xFEu;   /* terminator TLV */
}

/* (Re)apply the full NFCT register configuration. Used at init and after the
 * peripheral soft-reset (nRF52832 anomalies 79/116). */
static void nfc_apply_config(void) {
    NRF_NFCT->INTENCLR = 0xFFFFFFFFu;
    NRF_NFCT->EVENTS_READY = 0;
    NRF_NFCT->EVENTS_FIELDDETECTED = 0;
    NRF_NFCT->EVENTS_FIELDLOST = 0;
    NRF_NFCT->EVENTS_TXFRAMESTART = 0;
    NRF_NFCT->EVENTS_TXFRAMEEND = 0;
    NRF_NFCT->EVENTS_RXFRAMESTART = 0;
    NRF_NFCT->EVENTS_RXFRAMEEND = 0;
    NRF_NFCT->EVENTS_ERROR = 0;
    NRF_NFCT->EVENTS_RXERROR = 0;
    NRF_NFCT->EVENTS_ENDRX = 0;
    NRF_NFCT->EVENTS_ENDTX = 0;
    NRF_NFCT->EVENTS_SELECTED = 0;
    NRF_NFCT->EVENTS_STARTED = 0;
    NRF_NFCT->ERRORSTATUS = NFCT_ERRORSTATUS_FRAMEDELAYTIMEOUT_Msk |
                            NFCT_ERRORSTATUS_NFCFIELDTOOSTRONG_Msk |
                            NFCT_ERRORSTATUS_NFCFIELDTOOWEAK_Msk;
    NRF_NFCT->FRAMESTATUS.RX = NFCT_FRAMESTATUS_RX_CRCERROR_Msk |
                               NFCT_FRAMESTATUS_RX_PARITYSTATUS_Msk |
                               NFCT_FRAMESTATUS_RX_OVERRUN_Msk;

    NRF_NFCT->SENSRES = (1u << NFCT_SENSRES_NFCIDSIZE_Pos) | /* double size (7 bytes) */
                        (NFCT_SENSRES_BITFRAMESDD_SDD00100 << NFCT_SENSRES_BITFRAMESDD_Pos);
    NRF_NFCT->SELRES = 0u; /* protocol 00: Type 2 Tag */
    NRF_NFCT->NFCID1_2ND_LAST = (uint32_t)s_uid[0] | ((uint32_t)s_uid[1] << 8) | ((uint32_t)s_uid[2] << 16);
    NRF_NFCT->NFCID1_LAST = (uint32_t)s_uid[3] | ((uint32_t)s_uid[4] << 8) |
                            ((uint32_t)s_uid[5] << 16) | ((uint32_t)s_uid[6] << 24);

    NFC_AUTOCOLRES_REG &= ~(0x1UL); /* bit 0 = 0: hardware anti-collision enabled */

    NRF_NFCT->SHORTS = NFCT_SHORTS_FIELDDETECTED_ACTIVATE_Msk |
                       NFCT_SHORTS_FIELDLOST_SENSE_Msk;

    NRF_NFCT->FRAMEDELAYMODE = 1u; /* WindowGrid */
    NRF_NFCT->FRAMEDELAYMIN = 0x800u; /* ~217 us: FDT so the reader is back in RX before we answer */
    NRF_NFCT->FRAMEDELAYMAX = NFC_FRAME_DELAY_MAX;

    NRF_NFCT->TXD.FRAMECONFIG = NFC_FRAMECONFIG;
    NRF_NFCT->RXD.FRAMECONFIG = NFC_FRAMECONFIG;

    NRF_NFCT->PACKETPTR = (uint32_t)(uintptr_t)s_frame_buffer;
    NRF_NFCT->MAXLEN = NFC_FRAME_BUFFER_SIZE;

    NVIC_ClearPendingIRQ(NFCT_IRQn);
    NVIC_SetPriority(NFCT_IRQn, 2u);
    NVIC_EnableIRQ(NFCT_IRQn);

    NRF_NFCT->INTENSET = NFCT_INTENSET_SELECTED_Set |
                         NFCT_INTENSET_RXFRAMEEND_Set;
}

static void nfc_arm_rx(void) {
    NRF_NFCT->TASKS_ENABLERXDATA = 1;
}

static void nfc_send_response(size_t byte_count) {
    NRF_NFCT->TXD.AMOUNT = (uint32_t)byte_count << NFCT_TXD_AMOUNT_TXDATABYTES_Pos;
    NRF_NFCT->TASKS_STARTTX = 1;
}

static void nfc_end_session(void) {
    /* End the tag session: sleep and stop reacting to re-selection so a
     * re-polling reader cannot keep the ISR busy (main-loop freeze). The
     * poll loop re-enables SELECTED on the next field loss. */
    NRF_NFCT->INTENCLR = NFCT_INTENCLR_SELECTED_Clear;
    NRF_NFCT->TASKS_GOSLEEP = 1;
}

void NFCT_IRQHandler(void) {
    if (NRF_NFCT->EVENTS_FIELDDETECTED) {
        NRF_NFCT->EVENTS_FIELDDETECTED = 0;
        s_field_present = true;
    }

    if (NRF_NFCT->EVENTS_SELECTED) {
        NRF_NFCT->EVENTS_SELECTED = 0;
        /* Discard anything received before the selection completed. */
        NRF_NFCT->EVENTS_RXFRAMESTART = 0;
        NRF_NFCT->EVENTS_RXFRAMEEND = 0;
        NRF_NFCT->EVENTS_RXERROR = 0;
        NRF_NFCT->EVENTS_TXFRAMESTART = 0;
        NRF_NFCT->EVENTS_TXFRAMEEND = 0;
        NRF_NFCT->FRAMEDELAYMAX = NFC_FRAME_DELAY_MAX;
        s_read_count = 0;
        nfc_arm_rx();
    }

    if (NRF_NFCT->EVENTS_RXFRAMEEND) {
        NRF_NFCT->EVENTS_RXFRAMEEND = 0;

        /* RXD.AMOUNT counts complete bytes including the 2 CRC bytes. */
        const uint32_t rx_bits = NRF_NFCT->RXD.AMOUNT;
        const size_t rx_bytes = (rx_bits >= (2u << NFCT_RXD_AMOUNT_RXDATABYTES_Pos))
                                    ? (size_t)((rx_bits >> NFCT_RXD_AMOUNT_RXDATABYTES_Pos) - 2u)
                                    : 0u;
        const uint8_t *cmd = s_frame_buffer;

        if (rx_bytes >= 1u) {
            switch (cmd[0]) {
                case 0x30u: { /* READ: 4 blocks of 4 bytes = 16-byte response */
                    const uint8_t block = cmd[1];
                    if (rx_bytes >= 2u && block <= NFC_T2T_MAX_READ_BLOCK) {
                        memcpy(s_frame_buffer,
                               &s_t2t_memory[(uint32_t)block * NFC_T2T_BLOCK_SIZE],
                               NFC_T2T_BLOCK_SIZE * NFC_T2T_READ_BLOCKS);
                        nfc_send_response(NFC_T2T_BLOCK_SIZE * NFC_T2T_READ_BLOCKS);
                        if (++s_read_count >= 96u) {
                            nfc_end_session();
                            return;
                        }
                    }
                    break;
                }
                case 0xA2u:  /* WRITE: read-only tag, respond NAK */
                case 0xC2u:  /* SECTOR_SELECT: only sector 0 */
                    s_frame_buffer[0] = 0x00u;
                    nfc_send_response(1u);
                    break;
                case 0x50u:  /* SLP_REQ: reader finished, end the session */
                    nfc_end_session();
                    return;
                default:     /* unknown command: ignore */
                    break;
            }
        }

        /* Re-arm RX for the next command. The reader only sends after our
         * response finishes (half-duplex + FDT), so arming here is safe. */
        nfc_arm_rx();
    }
}

void nfc_init(void) {
    if (s_nfc_initialized) {
        return;
    }

    nfc_ensure_hfclk();
    nfc_configure_pins();
    memcpy(s_uid, k_uid, NFC_T2T_UID_SIZE);
    nfc_build_t2t_memory();

    NRF_NFCT->TASKS_DISABLE = 1;
    nfc_apply_config();

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

    if (NRF_NFCT->EVENTS_STARTED) {
        NRF_NFCT->EVENTS_STARTED = 0;
    }

    /* Error/status flags are polled here instead of interrupting: ERROR and
     * RXERROR can re-assert immediately after clearing (e.g. NFCFIELDTOOSTRONG
     * with a close reader), which would otherwise starve the main loop. */
    if (NRF_NFCT->EVENTS_ERROR) {
        NRF_NFCT->EVENTS_ERROR = 0;
    }
    if (NRF_NFCT->EVENTS_RXERROR) {
        NRF_NFCT->EVENTS_RXERROR = 0;
    }
    NRF_NFCT->ERRORSTATUS = NFCT_ERRORSTATUS_FRAMEDELAYTIMEOUT_Msk |
                            NFCT_ERRORSTATUS_NFCFIELDTOOSTRONG_Msk |
                            NFCT_ERRORSTATUS_NFCFIELDTOOWEAK_Msk;
    NRF_NFCT->FRAMESTATUS.RX = NFCT_FRAMESTATUS_RX_CRCERROR_Msk |
                               NFCT_FRAMESTATUS_RX_PARITYSTATUS_Msk |
                               NFCT_FRAMESTATUS_RX_OVERRUN_Msk;

    if (NRF_NFCT->EVENTS_FIELDLOST) {
        NRF_NFCT->EVENTS_FIELDLOST = 0;
        s_field_present = false;
        NRF_NFCT->TASKS_DISABLE = 1;
        nfc_apply_config();
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
