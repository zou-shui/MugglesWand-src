#pragma once
#include "Arduino.h"
#include "Config.h"

namespace HAL
{
    // Power
    void power_init(void);
    float power_get_battery_voltage(void);
    int power_get_battery_percent(void);
    bool power_is_charging(void);

    // WS2812
    void ws2812_init(void);
    void ws2812_trigger_flowing(uint32_t color, uint8_t speed_factor, uint8_t tail_length);
    void ws2812_toggle_last_led(uint32_t color);
    void ws2812_stop(void);
    void ws2812_set_solid(uint32_t color);
    void ws2812_delete(void);

    // MPU6050
    void mpu6050_start(void);
    void mpu6050_delete(void);
}
