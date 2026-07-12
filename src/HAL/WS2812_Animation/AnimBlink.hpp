#pragma once
#include "AnimationBase.hpp"

/**
 * @brief 一次性闪烁动画（fx 类型灯效，播放完毕自销毁）
 *
 * 以 _color 颜色点亮最后一颗灯珠，持续 _duration 毫秒后熄灭并标记 finished。
 */
class AnimBlink : public AnimationBase
{
private:
    CRGB _color;
    uint32_t _duration; // 持续时间（毫秒）

public:
    /**
     * @param color    灯珠颜色
     * @param durationMs 点亮持续时间（毫秒）
     */
    AnimBlink(CRGB color, uint32_t durationMs)
        : _color(color), _duration(durationMs) {}

    void update(CRGB *leds, uint16_t numLeds) override
    {
        if (numLeds == 0)
            return;

        uint32_t elapsed = millis() - _startTime;

        if (elapsed >= _duration)
        {
            // 持续时间到：熄灭灯珠并标记动画结束
            leds[numLeds - 1] = CRGB::Black;
            _isFinished = true;
            return;
        }

        // 持续期间：保持点亮
        leds[numLeds - 1] = _color;
    }
};
