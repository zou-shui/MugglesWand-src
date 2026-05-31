#include "APP_Lumos.h"
#include "HAL/HAL.h"
#include "HAL/WS2812_Animation/AnimLumos.hpp"

static bool is_lumos_on = false; // 记录当前魔杖照明状态

void APP_Lumos_trigger(uint32_t color)
{
    if (!is_lumos_on)
    {
        is_lumos_on = true; // 先置为 true

        // 传入 &is_lumos_on。这样未来无论谁在 HAL 层 delete 了这个动画，
        // 析构函数都会瞬间把这里的 is_lumos_on 刷回 false
        HAL::ws2812_set_background(new AnimLumos(CRGB(color), &is_lumos_on));
    }
    else
    {
        // 如果是手动关闭，给 HAL 传 nullptr，HAL 执行 delete，
        // 同样会触发析构函数，is_lumos_on 也会被自动刷回 false
        HAL::ws2812_set_background(nullptr);
    }
}
