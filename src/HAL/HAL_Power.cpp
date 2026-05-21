#include "HAL.h"
#include <Arduino.h>
#include "Config.h"

void HAL::power_off(void)
{
    pinMode(PIN_PWR_EN, OUTPUT);
    digitalWrite(PIN_PWR_EN, LOW); // 关闭电源
}

// 由于PIN_PWR_EN引脚带有外部上拉电阻，故默认状态为高电平，这里只需提供关机的接口