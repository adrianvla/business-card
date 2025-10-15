#ifndef ANIMATION_H
#define ANIMATION_H


#include <stdbool.h>
#include <stdint.h>
#include "RTE_Components.h"
#include "../leds/leds.h"
#ifdef CMSIS_device_header
#include CMSIS_device_header // IWYU pragma: keep
#endif

bool is_animation_running = false;
uint8_t animation_index = 0;
uint32_t animation_elapsed_ms = 0U;
uint32_t animation_speed_ms = 500U;
uint8_t animation_frame = 0;
int32_t animation_timeout_ticks = -1;


static void stop_animation(){
    is_animation_running = false;
    set_all_leds(false);
}

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
            case 2: //running light
                animation_frame %= (LED_COUNT + 1U);
                for(uint8_t i = 0; i < LED_COUNT; i++){
                    setLedState(((LED_COUNT - animation_frame) != i) && ((LED_COUNT - animation_frame) != ((i+1)%LED_COUNT)), (char)i);
                }
                break;
        }
        if(animation_timeout_ticks > 0){
            animation_timeout_ticks--;
            if(animation_timeout_ticks == 0){
                stop_animation();
            }
        }
    }
}

static void set_animation(uint8_t index, uint32_t speed_ms, int32_t timeout_ticks){
    animation_index = index;
    animation_elapsed_ms = speed_ms;
    animation_speed_ms = speed_ms;
    animation_timeout_ticks = timeout_ticks;
}

static void start_animation(){
    is_animation_running = true;
}

#endif