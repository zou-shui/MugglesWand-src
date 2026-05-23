/*
    提供进入深度睡眠的功能，提供按键唤醒和IMU中断唤醒两种方式
*/
#include "Sleep.h"
#include <Arduino.h>
#include "esp_sleep.h"
#include "Config.h"
#include "driver/rtc_io.h"

/************ 进入 Deep Sleep ************/
void sleep_enter()
{
    // 把KEY引脚配置为 RTC 上拉，禁用下拉；IMU中断引脚由ICM42670内部上拉
    rtc_gpio_pullup_en((gpio_num_t)PIN_KEY);
    rtc_gpio_pulldown_dis((gpio_num_t)PIN_KEY);

    // 构造唤醒掩码
    uint64_t wakeup_mask = (1ULL << PIN_KEY) | (1ULL << PIN_IMU_INT);

    // 启用 EXT1 唤醒，任意低电平即唤醒
    esp_sleep_enable_ext1_wakeup(
        wakeup_mask,
        ESP_EXT1_WAKEUP_ANY_LOW);

    // 确保休眠期间 RTC 隔离
    rtc_gpio_isolate((gpio_num_t)PIN_KEY);

    Serial.println("[SLEEP] Enter deep sleep");
    Serial.flush(); // 确保串口数据打印完毕

    esp_deep_sleep_start();
}