#pragma once
#include "AnimationBase.hpp"

/**
 * @brief 按键即时反馈动画（fx 类型灯效，播放完毕自销毁）
 *
 * 将"按下按键"的动作实时可视化为灯带的逐级点亮，反馈零延迟：
 *   - 按下瞬间：动画立刻开始，灯珠按"先快后慢"的 ease-out-cubic 曲线逐级点亮，
 *     极短时间的短按也能点亮较多灯珠（如 100ms 短按约点亮 1/4 灯带）；
 *   - 短按松开：以同样的先快后慢曲线逐颗熄灭，熄灭总时长与已点亮数量成正比，
 *     且默认以 2 倍点亮速度快速熄灭（drainSpeed 可调）；
 *   - 长按保持：持续点亮直到全部灯珠点亮。点亮曲线保证在 _totalMs 时刻恰好
 *     完全点亮——当 _totalMs 取长按判定时长时，灯带完全点亮的那一刻正好触发
 *     长按关机。
 *
 * 通过绑定按键模块的实时按下状态（const volatile bool &）轮询驱动，
 * 因此无需等待单击/多击判定结果，按键一按下动画立刻响应。
 */
class AnimTap : public AnimationBase
{
private:
    const volatile bool &_isPressed; // 按键实时按下状态（由按键模块维护）
    CRGB _color;
    uint16_t _startIndex; // 动画起始灯珠下标
    uint16_t _ledCount;   // 参与动画的灯珠数量
    uint32_t _totalMs;    // 从第一颗到完全点亮的总时长（毫秒）
    float _drainSpeed;    // 熄灭速度倍率（相对点亮速度，>1 表示熄灭更快）
    uint8_t _brightness;  // 点亮亮度（0~255，默认 20，避免刺眼）

    uint16_t _litCount;          // 当前已点亮的灯珠数量
    bool _draining;              // 是否处于熄灭阶段（按键已松开）
    uint32_t _drainStartTime;    // 熄灭阶段开始的时间戳
    uint32_t _drainTotalMs;      // 熄灭阶段总时长（与已点亮数量成正比）
    uint16_t _litCountAtDrainStart; // 进入熄灭阶段时已点亮的数量（快照）

public:
    /**
     * @param isPressed  按键实时按下状态引用（按下=true，松开=false）
     * @param color      点亮颜色
     * @param startIndex 动画起始灯珠下标
     * @param ledCount   参与动画的灯珠数量
     * @param totalMs    从第一颗到完全点亮的总时长（毫秒），取长按判定时长时
     *                   全亮时刻与长按关机同步
     * @param drainSpeed 熄灭速度倍率（相对点亮速度，>1 表示熄灭更快）
     * @param brightness 点亮亮度（0~255，默认 20 避免刺眼）
     */
    AnimTap(const volatile bool &isPressed, CRGB color = CRGB::White,
            uint16_t startIndex = 0, uint16_t ledCount = 41, uint32_t totalMs = 1000,
            float drainSpeed = 1.5f, uint8_t brightness = 20)
        : _isPressed(isPressed),
          _color(color),
          _startIndex(startIndex),
          _ledCount(ledCount == 0 ? 1 : ledCount),
          _totalMs(totalMs == 0 ? 1 : totalMs),
          _drainSpeed(drainSpeed <= 0.0f ? 1.0f : drainSpeed),
          _brightness(brightness),
          _litCount(0),
          _draining(false),
          _drainStartTime(0),
          _drainTotalMs(0),
          _litCountAtDrainStart(0) {}

    void init() override
    {
        AnimationBase::init();
        _litCount = 0;
        _draining = false;
        _drainStartTime = 0;
        _drainTotalMs = 0;
        _litCountAtDrainStart = 0;
    }

    void update(CRGB *leds, uint16_t numLeds) override
    {
        // 越界保护：起始下标 + 占用数量超出灯带范围时直接结束，让引擎自销毁
        if (_startIndex + _ledCount > numLeds)
        {
            _isFinished = true;
            return;
        }

        uint32_t now = millis();

        if (_isPressed)
        {
            // 点亮阶段：先快后慢的 ease-out-cubic 曲线，
            // 保证 _totalMs 时刻恰好完全点亮（与长按关机触发时刻同步）
            float u = (float)(now - _startTime) / _totalMs;
            if (u >= 1.0f)
            {
                _litCount = _ledCount;
            }
            else
            {
                float ease = 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u);
                _litCount = (uint16_t)(_ledCount * ease + 0.5f); // 四舍五入
            }
        }
        else if (!_draining)
        {
            // 按键松开：进入熄灭阶段，时长与已点亮数量成正比，
            // 并按 _drainSpeed 倍率缩短（熄灭比点亮更快）
            _draining = true;
            _drainStartTime = now;
            _litCountAtDrainStart = _litCount;
            _drainTotalMs = (uint32_t)((float)_totalMs * _litCount / _ledCount / _drainSpeed);
        }

        if (_draining)
        {
            // 熄灭阶段：以同样的先快后慢曲线逐颗熄灭
            uint32_t elapsed = now - _drainStartTime;
            if (elapsed >= _drainTotalMs)
            {
                _litCount = 0;
            }
            else
            {
                float v = (float)elapsed / _drainTotalMs;
                float remain = (1.0f - v) * (1.0f - v) * (1.0f - v);
                _litCount = (uint16_t)(_litCountAtDrainStart * remain + 0.5f);
            }

            if (_litCount == 0)
            {
                _isFinished = true;
                return;
            }
        }

        CRGB pixelColor = _color;
        pixelColor.nscale8(_brightness); // 默认 20 避免刺眼，可由构造参数覆盖

        for (uint16_t i = 0; i < _litCount; i++)
        {
            leds[_startIndex + i] += pixelColor; // 叠加混合，与底层动画兼容
        }
    }
};
