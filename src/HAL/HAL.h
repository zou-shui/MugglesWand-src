#pragma once
#include "Arduino.h"
#include "Config.h"

namespace HAL
{
    // WS2812
    void ws2812_init(void);
    void ws2812_trigger_flowing(uint32_t color, uint8_t speed_factor, uint8_t tail_length);
    void ws2812_toggle_last_led(uint32_t color);
    void ws2812_stop(void);
    void ws2812_set_solid(uint32_t color);

}
