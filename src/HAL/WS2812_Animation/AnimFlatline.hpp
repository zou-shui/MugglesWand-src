#pragma once
#include "AnimationBase.hpp"

/**
 * @brief 心跳停摆失败反馈动画（fx 类型灯效，播放完毕自销毁）
 *
 * 靠近底座的一段灯珠播放两次红色心跳，用于表示指令发送失败：
 *   - 第一次：快速起搏到峰值后衰减，像一次有力的心跳；
 *   - 短暂停歇后第二次更弱、余韵更长，随后彻底熄灭——"停摆"。
 * 亮度沿底座向灯尖轻微递减，像从握持处发出的脉搏；
 */
class AnimFlatline : public AnimationBase
{
private:
    CRGB _color;
    uint16_t _startIndex;    // 动画起始灯珠下标
    uint16_t _ledCount;      // 参与动画的灯珠数量
    uint8_t _peakBrightness; // 第一次心跳的峰值亮度（0~255）

    static constexpr uint32_t kBeatAttackMs = 70;   // 单次心跳起搏时长
    static constexpr uint32_t kBeatDecayMs = 160;   // 第一次心跳衰减时长
    static constexpr uint32_t kGapMs = 100;         // 两次心跳之间的停歇
    static constexpr uint32_t kSecondDecayMs = 300; // 第二次心跳衰减时长（更绵长）
    static constexpr uint32_t kSecondBeatStartMs =
        kBeatAttackMs + kBeatDecayMs + kGapMs;
    static constexpr uint32_t kTotalMs =
        kSecondBeatStartMs + kBeatAttackMs + kSecondDecayMs;
    static constexpr uint8_t kSecondBeatPercent = 75; // 第二次心跳峰值（相对第一次）
    static constexpr uint8_t kTipWeight = 0;          // 灯尖端亮度权重（255 = 100%）

    // 非线性曲线：起搏阶段先快后慢，模拟心跳的陡峭上升沿
    static float easeOutCubic(float u)
    {
        return 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u);
    }

    // 当前时刻的全局心跳包络（0~1），两次心跳共用同一条时间轴
    float pulse(uint32_t elapsed) const
    {
        if (elapsed >= kTotalMs)
            return 0.0f;

        if (elapsed < kBeatAttackMs)
        {
            float u = (float)elapsed / kBeatAttackMs;
            return easeOutCubic(u);
        }
        elapsed -= kBeatAttackMs;

        if (elapsed < kBeatDecayMs)
        {
            float u = (float)elapsed / kBeatDecayMs;
            float remain = 1.0f - u;
            return remain * remain; // 先快后慢的衰减，像脉搏回落
        }
        elapsed -= kBeatDecayMs;

        if (elapsed < kGapMs)
            return 0.0f;
        elapsed -= kGapMs;

        const float scale = kSecondBeatPercent / 100.0f;

        if (elapsed < kBeatAttackMs)
        {
            float u = (float)elapsed / kBeatAttackMs;
            return scale * easeOutCubic(u);
        }
        elapsed -= kBeatAttackMs;

        float u = (float)elapsed / kSecondDecayMs;
        float remain = 1.0f - u;
        return scale * remain * remain; // 第二次更弱、更长，随后停摆
    }

public:
    /**
     * @param color          心跳颜色
     * @param startIndex     动画起始灯珠下标
     * @param ledCount       参与动画的灯珠数量
     * @param peakBrightness 第一次心跳的峰值亮度（0~255）
     */
    AnimFlatline(CRGB color = CRGB::Red, uint16_t startIndex = 0,
                 uint16_t ledCount = 12, uint8_t peakBrightness = 50)
        : _color(color),
          _startIndex(startIndex),
          _ledCount(ledCount),
          _peakBrightness(peakBrightness) {}

    void update(CRGB *leds, uint16_t numLeds) override
    {
        if (numLeds == 0 || _ledCount == 0 || _startIndex >= numLeds)
        {
            _isFinished = true;
            return;
        }

        uint32_t elapsed = millis() - _startTime;
        if (elapsed >= kTotalMs)
        {
            _isFinished = true;
            return;
        }

        uint16_t count = _ledCount;
        if (_startIndex + count > numLeds)
            count = numLeds - _startIndex;

        const uint8_t level =
            (uint8_t)(_peakBrightness * pulse(elapsed) + 0.5f);
        if (level == 0)
            return;

        const uint16_t spread = count > 1 ? count - 1 : 1;
        for (uint16_t i = 0; i < count; ++i)
        {
            // 底座端最亮，向灯尖线性递减，形成有方向的脉动
            uint8_t weight = 255 - (uint8_t)((uint32_t)i * (255 - kTipWeight) / spread);
            uint8_t brightness = scale8(level, weight);
            if (brightness == 0)
                continue;

            CRGB lit = _color;
            lit.nscale8(brightness);
            leds[_startIndex + i] += lit; // 叠加混合，与底层动画兼容
        }
    }
};
