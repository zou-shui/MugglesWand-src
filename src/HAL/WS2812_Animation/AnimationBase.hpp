/*
    动画基类，所有动画类都应继承自该类并实现 update() 方法
*/
#pragma once
#include <Arduino.h>
#include <FastLED.h>

class AnimationBase
{
protected:
    uint32_t _startTime; // 动画启动的绝对时间戳
    bool _isFinished;    // 动画是否已播放完毕（用于单次动画自销毁）

public:
    AnimationBase() : _startTime(0), _isFinished(false) {}
    virtual ~AnimationBase() = default;

    // 动画初始化周期
    virtual void init()
    {
        _startTime = millis();
        _isFinished = false;
    }

    /**
     * @brief 每帧渲染核心虚函数
     * @param leds 当前图层的虚拟/实体 LED 缓冲区指针
     * @param numLeds 灯珠总数
     */
    virtual void update(CRGB *leds, uint16_t numLeds) = 0;

    // 获取动画结束状态
    virtual bool finished() const { return _isFinished; }
};