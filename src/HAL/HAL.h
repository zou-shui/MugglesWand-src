#pragma once
#include <stdint.h>

// 所有硬件API的声明都在这里
namespace HAL
{
    // Button
    void button_init(void);

    // IMU (ICM42670P)
    bool ICM42670P_init(void);
    void ICM42670P_start(int8_t data_mux);
    void ICM42670P_stop(void);
    void ICM42670_WakeOnMotion();

    // MAX17048
    bool MAX17048_init(void);
    float MAX17048_getVoltage(void);
    float MAX17048_getSOC(void);
    float MAX17048_getChangeRate(void);
    bool MAX17048_getChargeStatus(void);

    // Power
    void power_off(void);

    // WS2812
    void ws2812_init(void);
    void ws2812_trigger_breathe(uint32_t color, uint16_t period_ms);
    void ws2812_trigger_flow(uint32_t color, uint8_t speed_factor, uint8_t tail_length);
    void ws2812_toggle_last_led(uint32_t color);
    void ws2812_trigger_charge(uint8_t battery_percentage);
    void ws2812_stop(void);

}
