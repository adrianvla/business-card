#include <stdbool.h>
#include <stdint.h>
#include "RTE_Components.h"
#include CMSIS_device_header
#include "./leds/leds.h"
#include "./networking/nfc.h"
#include "./networking/bluetooth.h"


#define LED_COUNT                     8U
#define LOOP_SLICE_MS                 5U
#define NFC_POLL_INTERVAL_MS          5U

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

static void set_all_leds(bool state) {
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        setLedState(state, (char)i);
    }
}

static void set_alternate_leds(bool show_odd_group) {
    for (uint8_t i = 0; i < LED_COUNT; i++) {
        bool is_odd = (i & 0x01u) != 0u;
        bool led_on = show_odd_group ? is_odd : !is_odd;
        setLedState(led_on, (char)i);
    }
}

bool is_animation_running = false;
uint8_t animation_index = 0;
uint32_t animation_elapsed_ms = 0U;
uint32_t animation_speed_ms = 500U;
uint8_t animation_frame = 0;

static void update_animation(uint32_t delta_ms){
    if(!is_animation_running){
        return;
    }
    animation_elapsed_ms += delta_ms;
    if(animation_elapsed_ms >= animation_speed_ms){
        animation_elapsed_ms = 0U;
        animation_frame++;
        switch(animation_index){
            case 0: // alternate odd/even
                animation_frame %= 2;
                set_alternate_leds(animation_frame & 0x01u);
                break;
            case 1: //progress
                animation_frame %= (LED_COUNT + 1U);
                for(uint8_t i = 0; i < LED_COUNT; i++){
                    setLedState((LED_COUNT - animation_frame) < i, (char)i);
                }
                break;
        }
    }
}

static void set_animation(uint8_t index, uint32_t speed_ms){
    animation_index = index;
    animation_elapsed_ms = speed_ms;
    animation_speed_ms = speed_ms;
}

static void start_animation(){
    is_animation_running = true;
}

static void stop_animation(){
    is_animation_running = false;
    set_all_leds(false);
}

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
            set_animation(1, 125U);
        }
    }
}
 