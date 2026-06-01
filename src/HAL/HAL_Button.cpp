/*
    按键事件处理和长按关机实现
*/
#include "HAL.h"
#include <Arduino.h>
#include "Config.h"
#include "Service/EventBus.h"

#define TURN_OFF_TIME 1000       // 长按关机时间（ms）
#define MULTI_PRESS_INTERVAL 500 // 两次按下最大间隔 (ms)

static void button_task(void *param)
{
    uint32_t pressStart = 0;
    bool longPressTriggered = false;

    static uint8_t pressCount = 0;
    static uint32_t lastReleaseTime = 0;

    while (1)
    {
        bool pressed = digitalRead(PIN_KEY) == LOW;

        if (pressed)
        {
            if (pressStart == 0)
                pressStart = millis();

            // 长按触发
            if (!longPressTriggered && millis() - pressStart > TURN_OFF_TIME)
            {
                longPressTriggered = true;
                EventBus::publish(EVENT_SYS_SHUTDOWN); // 发送关机命令
            }
        }
        else
        {
            // 按键松开
            if (!longPressTriggered && pressStart > 0)
            {
                uint32_t pressDuration = millis() - pressStart;
                if (pressDuration < TURN_OFF_TIME)
                {
                    // 短按逻辑
                    pressCount++;
                    lastReleaseTime = millis();
                }
            }
            pressStart = 0;
            longPressTriggered = false;
        }

        // 检查是否超过多次按键窗口
        if (pressCount > 0 && (millis() - lastReleaseTime > MULTI_PRESS_INTERVAL))
        {
            // 根据 pressCount 执行不同功能
            switch (pressCount)
            {
            case 1:
                Serial.println("[Button] 1 short press action");
                EventBus::publish(EVENT_SYS_BLE); // 发送 BLE 切换命令
                break;
            case 2:
                Serial.println("[Button] 2 short press action");
                EventBus::publish(EVENT_SYS_OTA); // 发送 OTA 切换命令
                break;
            case 3:
                Serial.println("[Button] 3 short press action");
                break;
            default:
                Serial.println("[Button] 4 or more short presses, ignore");
                break;
            }

            pressCount = 0; // 重置计数
        }
        vTaskDelay(pdMS_TO_TICKS(20)); // 20ms扫描
    }
}

void HAL::button_init(void)
{
    pinMode(PIN_KEY, INPUT_PULLUP);

    xTaskCreatePinnedToCore(
        button_task,
        "button_task",
        2048,
        NULL,
        1,
        NULL,
        0);
}