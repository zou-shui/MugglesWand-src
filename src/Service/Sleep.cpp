/*
    提供进入深度睡眠的功能，提供按键唤醒和IMU中断唤醒两种方式
*/
#include "Sleep.h"
#include <Arduino.h>
#include "esp_sleep.h"
#include "Config.h"
#include "DualPrint.h"

/************ 进入 Deep Sleep ************/
void sleep_enter()
{
    // 构造唤醒掩码
    // uint64_t wakeup_mask = (1ULL << PIN_KEY) | (1ULL << PIN_IMU_INT);
    uint64_t wakeup_mask = (1ULL << PIN_KEY);

    // 启用 EXT1 唤醒，任意低电平即唤醒
    esp_sleep_enable_ext1_wakeup(
        wakeup_mask,
        ESP_EXT1_WAKEUP_ANY_LOW);

    DualSerial.println("[SLEEP] Enter deep sleep");
    DualSerial.flush(); // 确保串口数据打印完毕

    esp_deep_sleep_start();
}