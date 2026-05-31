#pragma once
#include "AnimationBase.hpp"

class AnimLumos : public AnimationBase
{
private:
    CRGB _baseColor;
    bool *_appStateBind; // 保存指向 APP 层状态变量的指针

    // --- 状态机控制变量 ---
    bool _isFlickering;       // 当前是否处于“闪烁期”
    uint32_t _stateStartTime; // 当前状态开始时的绝对时间戳
    uint32_t _stateDuration;  // 当前状态预设的持续时间（毫秒）

    /**
     * @brief 辅助函数：进入稳定状态，并随机生成下一次保持稳定的时间
     */
    void enterStableState()
    {
        _isFlickering = false;
        _stateStartTime = millis();
        // 闪烁出现的时机完全随机，最短间隔 500ms，这里设置 500ms 到 4000ms 之间随机
        _stateDuration = 500 + random16(3500);
    }

    /**
     * @brief 辅助函数：进入闪烁状态，并随机生成这次闪烁要持续的时长
     */
    void enterFlickerState()
    {
        _isFlickering = true;
        _stateStartTime = millis();
        // 闪烁时长从极短（如 10ms）到 800ms 随机
        _stateDuration = 10 + random16(790);
    }

public:
    /**
     * @param color 颜色
     * @param appStateBind 绑定 APP 层的 is_lumos_on 变量地址
     */
    AnimLumos(CRGB color, bool *appStateBind = nullptr)
        : _baseColor(color), _appStateBind(appStateBind) {}

    // 析构函数（当它被 delete 时，会自动执行这里）
    ~AnimLumos() override
    {
        if (_appStateBind != nullptr)
        {
            *_appStateBind = false; // 自动将上层的 is_lumos_on 刷回 false！
        }
    }

    void init() override
    {
        AnimationBase::init(); // 调用基类初始化
        enterStableState();    // 动画刚开始时，首先进入稳定期
    }

    void update(CRGB *leds, uint16_t numLeds) override
    {
        if (numLeds == 0)
            return;

        uint32_t currentTime = millis();

        // 1. 状态机时间轮询与切换
        if (currentTime - _stateStartTime >= _stateDuration)
        {
            if (_isFlickering)
            {
                // 如果闪烁期结束了，立刻回归稳定
                enterStableState();
            }
            else
            {
                // 如果稳定期结束了，立刻触发一次随机闪烁
                enterFlickerState();
            }
        }

        // 2. 根据当前所处的内部状态，计算渲染颜色
        CRGB finalColor = _baseColor;

        if (_isFlickering)
        {
            // 💡 闪烁期：每帧生成微小的亮度震荡
            uint8_t flicker = random8(200, 255); // 80%~100% 亮度抖动
            finalColor.nscale8(flicker);
        }
        else
        {
            // 💡 稳定期：直接保持原色的 100% 连续最大亮度
            // 不进行任何缩放，确保高占比的完美稳定
        }

        // 3. 强制点亮最后一颗灯珠
        leds[numLeds - 1] = finalColor;
    }
};