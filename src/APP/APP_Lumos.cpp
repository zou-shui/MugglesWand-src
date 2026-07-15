#include "APP_Lumos.h"
#include "HAL/HAL.h"
#include "HAL/WS2812_Animation/AnimLumos.hpp"

static bool is_lumos_on = false; // 记录当前魔杖照明状态

void APP_Lumos_trigger(uint32_t color)
{
    if (!is_lumos_on)
    {
        is_lumos_on = true; // 先置为 true

        // 传入 &is_lumos_on。这样未来无论谁在 HAL 层 delete 了这个动画，析构函数都会瞬间把这里的 is_lumos_on 刷回 false
        HAL::ws2812_set_overlay(new AnimLumos(CRGB(color), &is_lumos_on));
    }
    else
    {
        // 如果是手动关闭，不要忘记恢复原来的AnimStatus
        HAL::enable_AnimStatus(); // AnimStatus将覆盖Lumos动效
    }
}

void APP_Lumos_on(uint32_t color)
{
    // 即使已经是 true，强制开启也应当允许刷新颜色或重新触发动画
    is_lumos_on = true;

    // 直接挂载新动画，AnimLumos 的析构函数会自动处理旧动画（如果 HAL::ws2812_set_overlay 内部有释放旧 overlay 的逻辑）
    HAL::ws2812_set_overlay(new AnimLumos(CRGB(color), &is_lumos_on));
}

void APP_Lumos_off(void)
{
    // 强制恢复状态机动画，这会覆盖现有的 Lumos 动效
    HAL::enable_AnimStatus();

    // 安全起见，手动将状态置为 false（防止 HAL 层没有立即释放 AnimLumos 对象导致指针回调延迟）
    is_lumos_on = false;
}