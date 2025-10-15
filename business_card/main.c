#include "./utils/utils.h"
#include "./leds/leds.h"
#include "./leds/animation.h"
#include "./networking/nfc.h"
#include "./networking/bluetooth.h"


#define LOOP_SLICE_MS                 5U
#define NFC_POLL_INTERVAL_MS          5U
#define BLUETOOTH_ADV_INTERVAL_MS     30U

static const uint8_t k_adv_payload[] = {
    0x02, 0x01, 0x06,                // Flags: LE General Discoverable, BR/EDR not supported
    0x08, 0x09, 'B', 'i', 'z', 'C', 'a', 'r', 'd' // Complete Local Name: "BizCard"
};


int main(void) {
    set_all_leds(false);

    nfc_init();
    nfc_start_sense();

    (void)bluetooth_init();
    bluetooth_set_adv_payload(k_adv_payload, sizeof(k_adv_payload));

    play_animation_once(ANIMATION_ALTERNATE, 125U, 10, LOOP_SLICE_MS);
    play_animation_once(ANIMATION_FROM_CENTER_RIPPLE, 125U, 20, LOOP_SLICE_MS);
    play_animation_once(ANIMATION_ALTERNATE, 125U, 10, LOOP_SLICE_MS);


    uint32_t nfc_elapsed_ms = NFC_POLL_INTERVAL_MS;
    uint32_t bluetooth_elapsed_ms = BLUETOOTH_ADV_INTERVAL_MS;
    bool field_present = false;    

    while (true) {
        delay_ms(LOOP_SLICE_MS);

        nfc_elapsed_ms += LOOP_SLICE_MS;
        if (nfc_elapsed_ms >= NFC_POLL_INTERVAL_MS) {
            nfc_poll();
            field_present = nfc_is_field_present();
            nfc_elapsed_ms = 0U;
            start_animation();
            if(field_present){
                update_animation(LOOP_SLICE_MS);
            }

        }

        if (!field_present) {
            stop_animation();
            set_animation(ANIMATION_RUNNING_LIGHT, 125U, 10);
        }

        bluetooth_elapsed_ms += LOOP_SLICE_MS;
        if (bluetooth_elapsed_ms >= BLUETOOTH_ADV_INTERVAL_MS && bluetooth_is_ready()) {
            // Rotate through the BLE advertising channels at a steady cadence.
            bluetooth_broadcast_tick();
            bluetooth_elapsed_ms = 0U;
        }
    }
}
 