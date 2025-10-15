#include "./utils/utils.h"
#include "./leds/leds.h"
#include "./leds/animation.h"
#include "./networking/nfc.h"
#include "./networking/bluetooth.h"


#define LOOP_SLICE_MS                 5U
#define NFC_POLL_INTERVAL_MS          5U


int main(void) {
    set_all_leds(false);

    nfc_init();
    nfc_start_sense();

    (void)bluetooth_init();
    bluetooth_enable_test_carrier();

    bool show_odd_leds = true;
    uint32_t nfc_elapsed_ms = NFC_POLL_INTERVAL_MS;
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
            set_animation(2, 125U, 10);
        }
    }
}
 