#pragma once
#include <Arduino.h>
#include "Config.h"

namespace HAL
{
    // Power
    void power_init(void);
    float power_get_battery_voltage(void);
    int power_get_battery_percent(void);
    bool power_is_charging(void);
    void power_stop(void);

    // WS2812
    void ws2812_init(void);
    void ws2812_trigger_breathe(uint32_t color, uint16_t period_ms);
    void ws2812_trigger_flow(uint32_t color, uint8_t speed_factor, uint8_t tail_length);
    void ws2812_toggle_last_led(uint32_t color);
    void ws2812_trigger_charge(uint8_t battery_percentage);
    void ws2812_stop(void);

    // MPU6050
    void mpu6050_init();
    void mpu6050_start(void);
    void mpu6050_stop(void);
    void mpu6050_motion_interrupt_enable(uint8_t threshold, uint8_t timeOut);

    // Button
    void button_init(void);

}
