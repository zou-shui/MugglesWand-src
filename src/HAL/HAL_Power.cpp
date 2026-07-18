/*
    提供关机接口和实现空闲自动关机功能，自动关机功能只在IMU原始数据输出模式和OTA模式下禁用
*/
#include "HAL.h"
#include <Arduino.h>
#include "Config.h"
#include "Service/EventBus.h"

static TimerHandle_t shutdown_timer = NULL;
static QueueHandle_t power_queue = NULL;

#define AUTO_POWER_OFF_TIME 60000 // 1分钟

static bool autoPowerOffDisabled = false;

void HAL::power_off(void)
{
    pinMode(PIN_PWR_EN, OUTPUT);
    digitalWrite(PIN_PWR_EN, LOW); // 关闭电源
}

// 定时器超时回调函数：1分钟到了，执行关机
void power_off_timer_callback(TimerHandle_t xTimer)
{
    DualSerial.println("[Power] IMU idle for 1 min in training mode. Powering off...");

    EventBus::publish(EVENT_SYS_SHUTDOWN);
}

// Power 模块的任务，负责接收事件
static void power_task(void *pvParameters)
{
    SystemEvent event;
    while (1)
    {
        if (xQueueReceive(power_queue, &event, portMAX_DELAY) == pdTRUE)
        {
            // 处理 OTA/DEBUG 事件：关闭定时器并禁用后续自动关机逻辑
            if (event.id == EVENT_SYS_OTA || event.id == EVENT_SYS_DEBUG)
            {
                if (xTimerIsTimerActive(shutdown_timer) == pdTRUE)
                {
                    xTimerStop(shutdown_timer, 0);
                }
                autoPowerOffDisabled = true; // 进入禁用模式
                continue;                    // 不再处理其他逻辑（但任务继续运行）
            }

            // 如果已禁用自动关机，则只消费事件，不处理任何逻辑
            if (autoPowerOffDisabled)
            {
                continue;
            }

            // 正常处理 IMU 状态变化事件
            if (event.id == EVENT_IMU_STATUS_CHANGED)
            {
                int8_t sta = event.param1.i32;
                int8_t mux = event.param2.i32;

                if (mux == 2 && sta == 1)
                {
                    if (xTimerIsTimerActive(shutdown_timer) == pdFALSE)
                    {
                        xTimerStart(shutdown_timer, 0);
                    }
                }
                else
                {
                    if (xTimerIsTimerActive(shutdown_timer) == pdTRUE)
                    {
                        xTimerStop(shutdown_timer, 0);
                    }
                }
            }
        }
    }
}

// Power 模块初始化
void HAL::power_init()
{
    // 1. 创建事件队列并订阅
    power_queue = xQueueCreate(8, sizeof(SystemEvent));
    EventBus::subscribe(EVENT_IMU_STATUS_CHANGED, power_queue); // 订阅状态机
    EventBus::subscribe(EVENT_SYS_OTA, power_queue);            // 订阅OTA事件，进入OTA模式后也不自动关机
    EventBus::subscribe(EVENT_SYS_DEBUG, power_queue);          // 订阅DEBUG事件，进入DEBUG模式后也不自动关机

    // 2. 创建一个单次触发的软件定时器（pdFALSE 表示不循环）
    shutdown_timer = xTimerCreate(
        "ShutdownTimer",
        pdMS_TO_TICKS(AUTO_POWER_OFF_TIME), // 定时器周期
        pdFALSE,                            // 单次触发
        (void *)0,
        power_off_timer_callback);

    // 3. 创建电源管理任务
    xTaskCreatePinnedToCore(
        power_task,
        "Power_Task",
        2048,
        NULL,
        0,
        NULL,
        0);
}
