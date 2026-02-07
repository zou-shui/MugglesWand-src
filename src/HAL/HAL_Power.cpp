#include "HAL.h"
#include <esp_adc_cal.h>

/************ 硬件参数配置 ************/

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

/************ 初始化 ************/

void HAL::power_init(void)
{
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

    Serial.println("[HAL] Power module init done");
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
char HAL::power_get_battery_percent(void)
{
    float voltage = power_get_battery_voltage(); // 获取电池电压

    // 定义满电和低电阈值
    const float VOLTAGE_MAX = 4.20f;
    const float VOLTAGE_MIN = 3.60f;

    // 计算百分比
    char percent = (char)((voltage - VOLTAGE_MIN) / (VOLTAGE_MAX - VOLTAGE_MIN) * 100.0f + 0.5f);

    // 限制在 0~100%
    if (percent > 100) percent = 100;
    if (percent < 0) percent = 0;

    return percent;
}

/************ 读取充电状态 ************/

bool HAL::power_is_charging(void)
{
    int level = digitalRead(PIN_BATTERY_CHG_DET);

    return (level == LOW);
}
