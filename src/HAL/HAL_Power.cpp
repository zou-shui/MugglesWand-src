#include "HAL.h"
#include <esp_adc_cal.h>
#include "Service/Console.h"

/************ 硬件参数配置 ************/

// 定义满电和低电阈值
#define VOLTAGE_MAX 4.10f
#define VOLTAGE_MIN 3.40f

// ADC 参数
#define ADC_WIDTH ADC_WIDTH_12Bit
#define ADC_ATTEN ADC_ATTEN_DB_12

// 分压电阻（单位：欧姆）
#define BAT_R1 100000.0f // 上拉
#define BAT_R2 100000.0f // 下拉

// ADC 参考电压 (mV)（ESP32 默认 eFuse 标定）
#define DEFAULT_VREF 1100

// 采样平均次数
#define BAT_ADC_SAMPLE_COUNT 10

/************ 内部变量 ************/
static esp_adc_cal_characteristics_t adc_chars;

/************ FreeRTOS 任务 ************/
void power_task(void *pvParameters)
{
    static int lastPercent = -1;     // 初始化为-1，确保第一次调用
    static bool wasCharging = false; // 跟踪上一次充电状态

    if (!HAL::power_is_charging())
    {
        wasCharging = true; // 如果刚开机时未充电，将过去设置为充电状态以便触发推理
    }

    while (true)
    {
        //======================充电和推理状态自动切换======================
        bool ChargeStatu = HAL::power_is_charging();
        if (ChargeStatu)
        {
            // 获取当前电量百分比并可视化
            int currentPercent = HAL::power_get_battery_percent();

            if (!wasCharging) // 只有从未充电状态切换到充电状态时执行一次
            {
                wasCharging = true;
                command_send(CMD_USR_CHARGE); // 发送充电状态命令，参数为当前电量百分比
            }

            if (abs(currentPercent - lastPercent) >= 2 || lastPercent == -1)
            {
                HAL::ws2812_trigger_charge(currentPercent); // 电量变化量超过2%更新动画
                lastPercent = currentPercent;
            }
        }
        else
        {
            if (wasCharging) // 只有从充电状态切换到未充电状态时执行一次
            {
                lastPercent = -1; // 充电断开时重置百分比，确保下次充电时能正确触发动画
                wasCharging = false;
                command_send(CMD_USR_INFERENCE); // 发送推理状态命令
            }
        }

        //===========================低电量保护===========================
        if (!ChargeStatu && HAL::power_get_battery_percent() <= 5)
        {
            Serial.println("[Power] Battery critically low, shutting down...");
            command_send(CMD_SYS_SHUTDOWN); // 发送关机命令
        }
        vTaskDelay(pdMS_TO_TICKS(1000)); // 每秒检查一次
    }
}

/************ 读取电池电压 ************/

float HAL::power_get_battery_voltage(void)
{
    uint32_t adc_sum = 0;

    for (int i = 0; i < BAT_ADC_SAMPLE_COUNT; i++)
    {
        adc_sum += analogRead(PIN_BATTERY_VOLTAGE);
        delay(2);
    }

    uint32_t adc_raw = adc_sum / BAT_ADC_SAMPLE_COUNT;

    // 转换成毫伏
    uint32_t voltage_mv = esp_adc_cal_raw_to_voltage(adc_raw, &adc_chars);

    // ADC脚电压 → 电池真实电压
    float bat_voltage =
        (float)voltage_mv / 1000.0f *
        ((BAT_R1 + BAT_R2) / BAT_R2);

    return bat_voltage;
}

/************ 计算电池剩余百分比 ************/
int HAL::power_get_battery_percent(void)
{
    float voltage = power_get_battery_voltage(); // 获取电池电压

    // 计算百分比
    int percent = (int)((voltage - VOLTAGE_MIN) / (VOLTAGE_MAX - VOLTAGE_MIN) * 100.0f + 0.5f);

    // 限制在 0~100%
    if (percent > 100)
        percent = 100;
    if (percent < 0)
        percent = 0;

    return percent;
}

/************ 读取充电状态 ************/

bool HAL::power_is_charging(void)
{
    int level = digitalRead(PIN_BATTERY_CHG_DET);

    return (level == LOW);
}

void HAL::power_stop(void)
{
    digitalWrite(PIN_PWR_EN, LOW); // 关闭电源
    esp_deep_sleep_start();        // 马上停止所有程序（防止未松手时程序持续运行）
}

/************ 初始化 ************/
void HAL::power_init(void)
{
    /*电源使能保持*/
    pinMode(PIN_PWR_EN, OUTPUT);
    digitalWrite(PIN_PWR_EN, HIGH);        // 使能电源
    gpio_hold_dis((gpio_num_t)PIN_PWR_EN); // 释放引脚，允许修改引脚状态
    while (digitalRead(PIN_KEY) == LOW)
    {
        Serial.println("[Power] Enabled, release the button to turn on.");
        esp_task_wdt_reset(); // 喂狗，防止看门狗重启
        delay(100);
    }

    // ---------- 充电检测引脚 ----------
    pinMode(PIN_BATTERY_CHG_DET, INPUT_PULLUP);

    // ---------- ADC 初始化 ----------
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_BATTERY_VOLTAGE, ADC_ATTENDB_MAX);

    // ADC 校准
    esp_adc_cal_characterize(
        ADC_UNIT_1,
        ADC_ATTEN,
        ADC_WIDTH,
        DEFAULT_VREF,
        &adc_chars);

    xTaskCreatePinnedToCore(
        power_task,
        "power_task",
        2048,
        NULL,
        1,
        NULL,
        0);

    Serial.println("[HAL] Power module init done");
}
