#include "APP_Lumos.h"
#include "HAL/HAL.h"

void APP_Lumos_trigger()
{
    HAL::ws2812_toggle_last_led(0); // 白色
}
