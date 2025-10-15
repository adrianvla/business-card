#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "../utils/utils.h"
#include "RTE_Components.h"
#include "../leds/leds.h"
#ifdef CMSIS_device_header
#include CMSIS_device_header // IWYU pragma: keep
#endif


enum AnimationType {
    ANIMATION_ALTERNATE = 0,
    ANIMATION_PROGRESS = 1,
    ANIMATION_RUNNING_LIGHT = 2,
    ANIMATION_FROM_CENTER_RIPPLE = 3,
    ANIMATION_SCANNING_LIGHT = 4
};

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

static bool update_animation(uint32_t delta_ms){
    if(!is_animation_running){
        return true;
    }
    animation_elapsed_ms += delta_ms;
    if(animation_elapsed_ms >= animation_speed_ms){
        animation_elapsed_ms = 0U;
        animation_frame++;
        switch(animation_index){
            case ANIMATION_ALTERNATE: // alternate odd/even
                animation_frame %= 2;
                set_alternate_leds(animation_frame & 0x01u);
                break;
            case ANIMATION_PROGRESS: //progress
                animation_frame %= (LED_COUNT + 1U);
                for(uint8_t i = 0; i < LED_COUNT; i++){
                    setLedState((LED_COUNT - animation_frame) < i, (char)i);
                }
                break;
            case ANIMATION_RUNNING_LIGHT: //running light
                animation_frame %= (LED_COUNT + 1U);
                for(uint8_t i = 0; i < LED_COUNT; i++){
                    setLedState(((LED_COUNT - animation_frame) != i) && ((LED_COUNT - animation_frame) != ((i+1)%LED_COUNT)), (char)i);
                }
                break;
            case ANIMATION_FROM_CENTER_RIPPLE: //from center ripple
                animation_frame %= ((LED_COUNT / 2) + 2U);
                for(uint8_t i = 0; i < LED_COUNT; i++){
                    if(LED_COUNT % 2 == 0){
                        setLedState(((LED_COUNT / 2 - animation_frame) <= i) && (i < (LED_COUNT / 2 + animation_frame)), (char)i);
                    } else {
                        setLedState(((LED_COUNT / 2 - animation_frame) < i) && (i < (LED_COUNT / 2 + 1 + animation_frame)), (char)i);
                    }
                }
                break;
            case ANIMATION_SCANNING_LIGHT: //scanning light from left to right and back
                {
                    uint8_t position = animation_frame % (2 * LED_COUNT - 2);
                    if(position >= LED_COUNT){
                        position = 2 * LED_COUNT - 2 - position;
                    }
                    for(uint8_t i = 0; i < LED_COUNT; i++){
                        setLedState(i == position, (char)i);
                    }
                }
                break;
        }
        if(animation_timeout_ticks > 0){
            animation_timeout_ticks--;
            if(animation_timeout_ticks == 0){
                stop_animation();
                return false;
            }
        }
    }
    return true;
}

static void set_animation(uint8_t name, uint32_t speed_ms, int32_t timeout_ticks){
    animation_index = name;
    animation_elapsed_ms = speed_ms;
    animation_speed_ms = speed_ms;
    animation_timeout_ticks = timeout_ticks;
}

static void start_animation(){
    is_animation_running = true;
}

static void play_animation_once(uint8_t name, uint32_t speed_ms, int32_t timeout_ticks, uint32_t LOOP_MS){
    set_animation(name, speed_ms, timeout_ticks);
    start_animation();
    while(true){
        if(!update_animation(LOOP_MS)){
            break;
        }
        delay_ms(LOOP_MS);
    }
}