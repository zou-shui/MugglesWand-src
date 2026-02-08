/*
    提供进入深度睡眠的功能
*/
#include "Sleep.h"
#include <Arduino.h>
#include "esp_sleep.h"
#include "Config.h"

/************ GPIO 唤醒 ************/
void sleep_init()
{
    // 1. 配置 IMU 中断引脚为输入，下拉防止浮空
    pinMode(PIN_IMU_INT, INPUT_PULLDOWN);

    // 2. 配置 EXT0 唤醒，高电平唤醒
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_IMU_INT, 1);
}

/************ 进入 Deep Sleep ************/
void sleep_enter()
{
    Serial.println("[SLEEP] Enter deep sleep");
    delay(50);
    esp_deep_sleep_start();
}
