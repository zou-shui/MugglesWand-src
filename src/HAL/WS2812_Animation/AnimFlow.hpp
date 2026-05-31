#pragma once
#include "AnimationBase.hpp"

class AnimFlow : public AnimationBase
{
private:
    CRGB _color;
    uint8_t _speedFactor;
    uint8_t _tailLength;
    float _currentPos; // 使用浮点数记录位置，使低速移动时也保持平滑

public:
    /**
     * @param color 光流核心颜色
     * @param speedFactor 速度因子（每帧移动的灯珠数，支持小数）
     * @param tailLength 拖尾长度（灯珠数）
     */
    AnimFlow(CRGB color = CRGB::White, uint8_t speedFactor = 16, uint8_t tailLength = 12)
        : _color(color), _speedFactor(speedFactor), _tailLength(tailLength), _currentPos(0.0f) {}

    void update(CRGB *leds, uint16_t numLeds) override
    {
        // 1. 更新当前头部位置
        // 假设原本 10ms 帧率，可以通过位置递增来实现动画
        _currentPos += (_speedFactor * 0.1f);

        int head = (int)_currentPos;

        // 2. 如果整个拖尾都已经滑出了灯带，宣告动画结束
        if (head - _tailLength >= numLeds)
        {
            _isFinished = true;
            return;
        }

        // 3. 渲染拖尾效果
        for (int i = 0; i < numLeds; i++)
        {
            // 计算当前灯珠与光流头部的距离
            int distance = head - i;

            if (distance >= 0 && distance < _tailLength)
            {
                // 距离头部越远，亮度越暗（线性衰减）
                uint8_t dynamicBrightness = 255 - ((distance * 255) / _tailLength);

                // 叠加颜色（使用 FastLED 变暗函数，不直接覆盖，以便和底层混合）
                CRGB pixelColor = _color;
                pixelColor.nscale8(dynamicBrightness);
                leds[i] += pixelColor;
            }
        }
    }
};