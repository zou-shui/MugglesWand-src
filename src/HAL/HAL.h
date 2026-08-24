#pragma once
#include <stdint.h>
#include "Service/DualPrint.h"

class AnimationBase;

// 所有硬件API的声明都在这里
namespace HAL
{
    // Button
    void button_init(void);

    // IMU (ICM42670P)
    bool ICM42670P_init(void);
    void ICM42670P_start(int8_t data_mux);
    void ICM42670P_stop(void);
    void enable_AnimStatus();

    // MAX17048
    bool MAX17048_init(void);
    float MAX17048_getVoltage(void);
    float MAX17048_getSOC(void);
    float MAX17048_getChangeRate(void);

    // Power
    void power_init(void);
    void power_off(void);
    bool power_getChargeStatus(void);
    uint32_t power_getADCValue(void);

    // WS2812 统一动画管理引擎接口
    void ws2812_init(void);
    void ws2812_stop(void);

    // 基于现代图层架构的异步注入接口
    void ws2812_set_background(AnimationBase *anim);
    void ws2812_start_fx(AnimationBase *anim);
    void ws2812_set_overlay(AnimationBase *anim);
}
