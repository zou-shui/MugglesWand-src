/*
    提供关机接口和实现空闲自动休眠功能，自动休眠功能只在IMU原始数据输出模式、OTA模式和debug模式下禁用
    另外提供充电检测功能，使用PIN_CHG_DET引脚进行分时复用，交替执行LED闪烁和ADC检测充电状态，电压高于CHG_DET_THRESHOLD_MV时认为正在充电
*/
#include "HAL.h"
#include <Arduino.h>
#include "Config.h"
#include "Service/EventBus.h"

static TimerHandle_t shutdown_timer = NULL;
static QueueHandle_t power_queue = NULL;

#define AUTO_POWER_OFF_TIME 60000 // 1分钟

static bool autoPowerOffDisabled = false;

// ========== 充电检测 + LED 闪烁（分时复用 PIN_CHG_DET）==========
static TimerHandle_t chg_led_timer = NULL;
static bool chg_led_phase = false;       // false=LED点亮, true=ADC检测
static bool last_charging_state = false; // 上一次充电状态（用于变化检测）
static uint32_t last_adc_value = 0;      // 最新 ADC 电压值（mV）

// 定时器回调：每 500ms 触发一次，交替执行 LED 亮 / ADC 检测
static void chg_led_timer_callback(TimerHandle_t xTimer)
{
    if (!chg_led_phase)
    {
        // LED 相位：配置为输出，输出高电平点亮 LED
        pinMode(PIN_CHG_DET, OUTPUT);
        digitalWrite(PIN_CHG_DET, HIGH);
    }
    else
    {
        // ADC 相位：配置为输入，检测充电电压
        pinMode(PIN_CHG_DET, INPUT);
        last_adc_value = analogReadMilliVolts(PIN_CHG_DET);
        // DualSerial.printf("[Power] ADC reading: %lumV\n", last_adc_value);   // 调试用

        bool is_charging = (last_adc_value > CHG_DET_THRESHOLD_MV);
        if (is_charging != last_charging_state)
        {
            last_charging_state = is_charging;
            if (is_charging)
            {
                DualSerial.printf("[Power] Charging detected\n");
                EventBus::publish(EVENT_POWER_CHARGING);
            }
            else
            {
                DualSerial.printf("[Power] Not charging\n");
                EventBus::publish(EVENT_POWER_NOT_CHARGING);
            }
        }
    }
    chg_led_phase = !chg_led_phase;
}

void HAL::power_off(void)
{
    pinMode(PIN_PWR_EN, OUTPUT);
    digitalWrite(PIN_PWR_EN, LOW); // 关闭电源
}

// 定时器超时回调函数：1分钟到了，执行休眠
void power_off_timer_callback(TimerHandle_t xTimer)
{
    DualSerial.println("[Power] IMU idle for 1 min. Sleeping...");

    EventBus::publish(EVENT_SYS_SLEEP);
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
                DualSerial.println("[Power] Auto sleep disabled");
                continue; // 不再处理其他逻辑（但任务继续运行）
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

    // 4. 充电检测 + LED 闪烁（分时复用 PIN_CHG_DET）
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_CHG_DET, ADC_11db);

    chg_led_timer = xTimerCreate(
        "ChgLedTimer",
        pdMS_TO_TICKS(500), // 500ms 周期
        pdTRUE,             // 自动重载
        (void *)0,
        chg_led_timer_callback);

    if (chg_led_timer != NULL)
    {
        xTimerStart(chg_led_timer, 0);
    }
}

bool HAL::power_getChargeStatus(void)
{
    return last_charging_state;
}

uint32_t HAL::power_getADCValue(void)
{
    return last_adc_value;
}
