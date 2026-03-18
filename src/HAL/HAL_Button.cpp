#include "HAL.h"
#include "Service/Console.h"

#define TURN_OFF_TIME 3000       // 长按关机时间（ms）
#define MULTI_PRESS_INTERVAL 500 // 两次按下最大间隔 (ms)

static QueueHandle_t cmd_queue;

static void button_task(void *param)
{
    uint32_t pressStart = 0;
    bool longPressTriggered = false;

    static uint8_t pressCount = 0;
    static uint32_t lastReleaseTime = 0;

    command_msg_t msg;
    memset(&msg, 0, sizeof(msg));

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
                msg.type = CMD_SYS_SHUTDOWN;
                xQueueSend(cmd_queue, &msg, portMAX_DELAY);
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
                msg.type = CMD_BLE_BLE;
                break;
            case 2:
                Serial.println("[Button] 2 short press action");
                msg.type = CMD_OTA_OTA;
                break;
            case 3:
                Serial.println("[Button] 3 short press action");

                break;
            default:
                Serial.println("[Button] 4 or more short presses, ignore");
                break;
            }

            pressCount = 0;                             // 重置计数
            xQueueSend(cmd_queue, &msg, portMAX_DELAY); // 发送消息到控制台处理
        }
        vTaskDelay(pdMS_TO_TICKS(20)); // 20ms扫描
    }
}

void HAL::button_init(void)
{
    pinMode(PIN_KEY, INPUT);

    cmd_queue = console_get_queue();

    // 长按关机任务
    xTaskCreatePinnedToCore(
        button_task,
        "button_task",
        2048,
        NULL,
        1,
        NULL,
        0);
}