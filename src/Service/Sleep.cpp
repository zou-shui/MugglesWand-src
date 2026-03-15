/*
    提供进入深度睡眠的功能
*/
#include "Sleep.h"
#include <Arduino.h>
#include "esp_sleep.h"
#include "Config.h"

/************ 进入 Deep Sleep ************/
void sleep_enter()
{
    // 1. 配置引脚
    pinMode(PIN_IMU_INT, INPUT_PULLUP);
    pinMode(PIN_KEY, INPUT_PULLUP);

    // 2. 构造唤醒掩码
    uint64_t wakeup_mask =
        (1ULL << PIN_IMU_INT) |
        (1ULL << PIN_KEY);

    // 3. 启用 EXT1 唤醒
    esp_sleep_enable_ext1_wakeup(
        wakeup_mask,
        ESP_EXT1_WAKEUP_ANY_LOW);

    // 4. 保持电源使能
    gpio_hold_en((gpio_num_t)PIN_PWR_EN);
    gpio_deep_sleep_hold_en();

    Serial.println("[SLEEP] Enter deep sleep");
    delay(50);
    esp_deep_sleep_start();
}