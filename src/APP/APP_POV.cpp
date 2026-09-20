#include "APP_POV.h"
#include "APP_Lumos.h"
#include "APP_POVPatterns.h" // POV 内置图案库 (APP 层内容素材)
#include "HAL/HAL.h"
#include "HAL/WS2812_Animation/AnimPOV.hpp"
#include "Service/DualPrint.h"
#include "Service/EventBus.h"
#include <math.h>
#include <FastLED.h>

// ==================== POV 模式状态 ====================
static bool pov_mode_enabled = false; // 模式激活标志（单击退出）
static bool pov_trigger_armed = true; // 触发边沿检测：true=静止待命，false=触发冷却中
static bool pov_anim_playing = false; // 光绘动画播放中标志（由 AnimPOV 析构回调复位）

// 触发检测阈值（线性加速度幅度，单位 g）
#define POV_TRIGGER_ACCEL 0.2f // 正交于 Y 轴挥动的触发阈值
#define POV_ARM_ACCEL 0.1f     // 回到静止的重新武装阈值（滞回，防止挥动过程连续触发）

// ==================== 光绘参数（由 EVENT_POV_SET 设置） ====================
static uint8_t pov_pattern = 0;  // 当前图案索引
static bool pov_reverse = false; // 是否倒序播放

// 动画销毁回调：AnimPOV 被引擎 delete 时调用（= 播放完成销毁后），解除播放中标志
static void APP_POV_anim_done(void)
{
    pov_anim_playing = false;
}

// 用当前参数触发一次光绘（所有权转移给 WS2812 引擎，播完自动清理）
static void APP_POV_fire(void)
{
    pov_anim_playing = true; // 先置播放中（若队列满被引擎拒绝，动画析构回调会立即复位）

    HAL::ws2812_set_overlay(new AnimPOV(&kPovPatterns[pov_pattern], pov_reverse,
                                        255, APP_POV_anim_done));

    const POVPatternInfo *p = &kPovPatterns[pov_pattern];
    uint16_t duration = p->rows * 10; // 每行 10ms
    DualSerial.printf("[POV] fire pattern=%s rows=%d rev=%d duration=%dms\n",
                      p->name, p->rows, pov_reverse, duration);
}

void APP_POV_set_params(uint8_t patternIndex, bool reverse)
{
    if (patternIndex >= POV_PATTERN_COUNT)
    {
        DualSerial.printf("[POV] error, pattern index %d out of range (0~%d)\n",
                          patternIndex, POV_PATTERN_COUNT - 1);
        return;
    }

    pov_pattern = patternIndex;
    pov_reverse = reverse;

    const POVPatternInfo *p = &kPovPatterns[pov_pattern];
    DualSerial.printf("[POV] params set pattern=%s rev=%d\n", p->name, reverse);
}

void APP_POV_mode_enter(void)
{
    if (pov_mode_enabled)
        return;
    pov_mode_enabled = true;
    // 初始待命：若退出前遗留的光绘还在播放，则等其销毁且静止后再武装
    pov_trigger_armed = !pov_anim_playing;
    APP_Lumos_on(CRGB::Purple); // 紫色模式指示灯（区别于鼠标蓝、音量红）
    DualSerial.println("[APP] POV mode enabled");
    EventBus::publish(EVENT_IMU_SET_MUX, 5); // 数据流切换为剔除重力的线性加速度
}

void APP_POV_mode_exit(void)
{
    if (!pov_mode_enabled)
        return;
    pov_mode_enabled = false;
    APP_Lumos_off(); // 熄灭模式指示灯并恢复手势状态灯
    DualSerial.println("[APP] POV mode disabled");
    EventBus::publish(EVENT_IMU_SET_MUX, 2); // 数据流恢复为手势训练缓冲区
}

bool APP_POV_in_mode(void)
{
    return pov_mode_enabled;
}

void APP_POV_on_imu_data(float linearX, float linearZ)
{
    float mag = sqrtf(linearX * linearX + linearZ * linearZ);

    if (!pov_trigger_armed)
    {
        // 冷却中：必须【动画已播放完成并销毁】且【回到静止】才重新武装，
        // 防止挥动过程中的短暂减速或动画未播完就连续触发
        if (!pov_anim_playing && mag < POV_ARM_ACCEL)
            pov_trigger_armed = true;
        return;
    }

    // 待命中：正交于 Y 轴的挥动（X-Z 平面线性加速度超阈值）→ 立即触发光绘
    if (mag > POV_TRIGGER_ACCEL)
    {
        pov_trigger_armed = false;
        APP_POV_fire();
    }
}

void APP_POV_list(void)
{
    DualSerial.println("POV patterns:");
    for (uint8_t i = 0; i < POV_PATTERN_COUNT; i++)
    {
        DualSerial.printf("  [%d] %s (%d x %d)\n",
                          i, kPovPatterns[i].name, kPovPatterns[i].rows, kPovPatterns[i].width);
    }
}
