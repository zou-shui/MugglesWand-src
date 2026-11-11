/*
    提供进入深度睡眠的功能，提供按键唤醒和IMU中断唤醒两种方式
*/
#include "Sleep.h"
#include <Arduino.h>
#include "esp_sleep.h"
#include "Config.h"
#include "DualPrint.h"

static void sleep_execute(uint64_t wakeup_mask, const char *tag)
{
    esp_sleep_enable_ext1_wakeup(wakeup_mask, ESP_EXT1_WAKEUP_ANY_LOW);

    delay(100); // 给 IMU/外设留出进入低功耗的时间
    DualSerial.printf("[SLEEP] Enter deep sleep (%s)\n", tag);
    delay(100); // 等待串口输出完成

    esp_deep_sleep_start();
}

// 休眠模式：按键 + IMU 运动唤醒
void sleep_enter()
{
    sleep_execute((1ULL << PIN_KEY) | (1ULL << PIN_IMU_INT), "wakeup:KEY+WoM");
}

// 关机风味儿休眠：仅按键唤醒
void shutdown_sleep_enter()
{
    // 长按触发关机时手指可能还按着；一直等到松开再睡，
    // 否则 EXT1 的低电平条件在入睡瞬间已成立，会立刻唤醒自己
    DualSerial.println("[SLEEP] Waiting for key release...");
    while (digitalRead(PIN_KEY) == LOW)
    {
        delay(10);
    }

    sleep_execute(1ULL << PIN_KEY, "wakeup:KEY");
}