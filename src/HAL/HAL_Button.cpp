/*
    按键事件处理和长按关机实现
*/
#include "HAL.h"
#include <Arduino.h>
#include "Config.h"
#include "Service/EventBus.h"
#include "HAL/WS2812_Animation/AnimTap.hpp"
#include "HAL/WS2812_Animation/AnimShutdown.hpp"

#define TURN_OFF_TIME 1000       // 长按关机时间（ms）
#define MULTI_PRESS_INTERVAL 500 // 两次按下最大间隔 (ms)
#define SHUTDOWN_ANIM_MS 500     // 关机衔接动画总时长（ms）

// 按键实时按下状态（供 AnimTap 反馈动画轮询读取，按下瞬间即时响应）
static volatile bool s_button_pressed = false;
// 关机动画"关机已确认"标志（动画播完的收尾帧置位，作为关机事件触发信号）
static bool s_shutdown_done = false;

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
            {
                // 按下瞬间：立即启动按键反馈动画。
                // 点亮曲线为"先快后慢"的 ease-out-cubic，总时长取 TURN_OFF_TIME，
                // 即长按到灯带完全点亮时，正好与长按关机触发时刻重合。
                pressStart = millis();
                s_button_pressed = true;
                HAL::ws2812_start_fx(new AnimTap(s_button_pressed, CRGB::White,
                                                 0, WS2812_LED_COUNT, TURN_OFF_TIME));
            }

            // 长按触发
            if (!longPressTriggered && millis() - pressStart > TURN_OFF_TIME)
            {
                longPressTriggered = true;

                // 让点亮动画退出（其全亮帧由 overlay 无缝接管，衔接不闪断）
                s_button_pressed = false;
                // 清掉背景层，避免关机动画结束后底层"回光"
                HAL::ws2812_set_background(nullptr);
                // overlay 层强制覆盖所有动效（含 LED0 状态灯），播放关机衔接动画
                HAL::ws2812_set_overlay(new AnimShutdown(CRGB::White, &s_shutdown_done,
                                                         0, WS2812_LED_COUNT, SHUTDOWN_ANIM_MS));
                // 等待动画播完（收尾帧置位 s_shutdown_done），超时兜底后发布关机命令
                uint32_t waitDeadline = millis() + SHUTDOWN_ANIM_MS + 500;
                while (!s_shutdown_done && millis() < waitDeadline)
                {
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
                EventBus::publish(EVENT_SYS_SHUTDOWN); // 发送关机命令
            }
        }
        else
        {
            // 按键松开（动画随即进入逐个熄灭阶段）
            s_button_pressed = false;

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
            // 根据 pressCount 执行不同功能（动画已在按下瞬间响应，此处只做业务逻辑）
            switch (pressCount)
            {
            case 1:
                DualSerial.println("[Button] 1 short press action");
                EventBus::publish(EVENT_BTN_SHORT_PRESS, 1);
                break;
            case 2:
                DualSerial.println("[Button] 2 short press action");
                break;
            case 3:
                DualSerial.println("[Button] 3 short press action");
                break;
            default:
                DualSerial.println("[Button] 4 or more short presses, ignore");
                break;
            }

            pressCount = 0; // 重置计数
        }
        vTaskDelay(pdMS_TO_TICKS(20)); // 20ms扫描
    }
}

void HAL::button_init(void)
{
    pinMode(PIN_KEY, INPUT);

    xTaskCreatePinnedToCore(
        button_task,
        "button_task",
        2048,
        NULL,
        1,
        NULL,
        0);
}
