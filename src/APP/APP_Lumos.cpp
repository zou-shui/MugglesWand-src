#include "APP_Lumos.h"
#include "HAL/HAL.h"

void APP_Lumos_trigger(uint32_t color)
{
    HAL::ws2812_toggle_last_led(color); // 白色
}
