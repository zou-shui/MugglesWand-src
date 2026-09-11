#pragma once
#include "AnimationBase.hpp"

/**
 * @brief 连击次数反馈动画（fx 类型灯效，播放完毕自销毁）
 *
 * 在 switch(pressCount) 不同 case 分支中调用，根据连击数在灯带上"分段点亮"反馈：
 *   - 单击：点亮 1-3 段（默认绿色）；
 *   - 双击：点亮 1-3 段 + 4-6 段（绿/黄）；
 *   - 三击：点亮 1-3 段 + 4-6 段 + 7-9 段（绿/黄/红）；
 *   - 以此类推：每多一连击就多亮一段，每段占 _pairSize 颗灯珠（默认 3），
 *     段与段紧邻（_gapSize 默认 0），靠颜色区分段界，索引从 _startIndex 起算。
 *   - 第 4 段及以后使用 _fallbackColor（默认白色）。
 *
 * 所有段同时参与淡入淡出：使用 ease-out-cubic 非线性曲线快速达到目标亮度，
 * 维持一帧后用 ease-in-cubic 非线性曲线柔和熄灭，避免突变。
 *
 * 适用于"用户按完键后告知按了几下"的一过性反馈场景。
 */
class AnimCombo : public AnimationBase
{
private:
    CRGB _segmentColors[3]; // 前 3 段颜色（默认 绿/黄/红）
    CRGB _fallbackColor;    // 第 4 段及以后颜色（默认白）
    uint8_t _pressCount;    // 连击次数（>=1），同时也是点亮段数
    uint8_t _startIndex;    // 第一段起始灯珠下标（0-based，默认 1）
    uint8_t _pairSize;      // 每段灯珠数量（默认 3）
    uint8_t _gapSize;       // 段与段之间熄灯珠数量（默认 0，无间隔）
    uint32_t _fadeInMs;     // 渐入时长（毫秒）
    uint32_t _holdMs;       // 维持时长（毫秒）
    uint32_t _fadeOutMs;    // 渐灭时长（毫秒）
    uint8_t _brightness;    // 点亮峰值亮度（0~255，默认 20 与 AnimTap/AnimShutdown 对齐）

    enum class Phase : uint8_t
    {
        FadeIn,
        Hold,
        FadeOut
    };
    Phase _phase;

    // 计算指定段在灯带上的有效绘制范围（带越界保护，超出范围时 begin >= end）
    void getPairRange(uint8_t pairIdx, uint16_t numLeds, uint16_t &begin, uint16_t &end) const
    {
        uint16_t start = (uint16_t)_startIndex + (uint16_t)pairIdx * (_pairSize + _gapSize);
        begin = start;
        end = start + _pairSize;
        if (begin >= numLeds)
        {
            begin = numLeds; // 让 begin >= end，下游直接跳过
            return;
        }
        if (end > numLeds)
            end = numLeds;
    }

    // 非线性曲线：渐入阶段先快后慢
    static float easeOutCubic(float u)
    {
        return 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u);
    }

    // 非线性曲线：渐灭阶段先慢后快
    static float easeInCubic(float u)
    {
        return u * u * u;
    }

public:
    /**
     * @param pressCount     连击次数（>=1，传 0 自动校正为 1）
     * @param segmentColors  前 3 段自定义颜色数组指针（传 nullptr 则使用默认 绿/黄/红）
     * @param fallbackColor  第 4 段及以后回退颜色（默认白）
     * @param startIndex     第一段起始灯珠下标（默认 1）
     * @param pairSize       每段灯珠数量（默认 3）
     * @param gapSize        段与段之间的熄灯珠数量（默认 0，无间隔）
     * @param fadeInMs       渐入时长（毫秒）
     * @param holdMs         维持时长（毫秒）
     * @param fadeOutMs      渐灭时长（毫秒）
     * @param brightness     峰值亮度（0~255）
     */
    AnimCombo(uint8_t pressCount = 1,
              const CRGB *segmentColors = nullptr,
              CRGB fallbackColor = CRGB::White,
              uint8_t startIndex = 1,
              uint8_t pairSize = 3,
              uint8_t gapSize = 0,
              uint32_t fadeInMs = 50,
              uint32_t holdMs = 200,
              uint32_t fadeOutMs = 100,
              uint8_t brightness = 40)
        : _fallbackColor(fallbackColor),
          _pressCount(pressCount == 0 ? 1 : pressCount),
          _startIndex(startIndex),
          _pairSize(pairSize == 0 ? 1 : pairSize),
          _gapSize(gapSize),
          _fadeInMs(fadeInMs == 0 ? 1 : fadeInMs),
          _holdMs(holdMs),
          _fadeOutMs(fadeOutMs == 0 ? 1 : fadeOutMs),
          _brightness(brightness),
          _phase(Phase::FadeIn)
    {
        if (segmentColors != nullptr)
        {
            _segmentColors[0] = segmentColors[0];
            _segmentColors[1] = segmentColors[1];
            _segmentColors[2] = segmentColors[2];
        }
        else
        {
            // 默认前 3 段颜色：绿 / 黄 / 红
            _segmentColors[0] = CRGB::Green;
            _segmentColors[1] = CRGB::Yellow;
            _segmentColors[2] = CRGB::Red;
        }
    }

    void init() override
    {
        AnimationBase::init();
        _phase = Phase::FadeIn;
    }

    void update(CRGB *leds, uint16_t numLeds) override
    {
        if (numLeds == 0)
        {
            _isFinished = true;
            return;
        }

        uint32_t now = millis();
        uint32_t elapsed = now - _startTime;

        // 1. 计算当前阶段的亮度系数 ratio（0~1）
        float ratio;
        if (_phase == Phase::FadeIn)
        {
            float u = (float)elapsed / _fadeInMs;
            if (u >= 1.0f)
            {
                // 渐入完成：切到 Hold 阶段并复位计时
                ratio = 1.0f;
                _phase = Phase::Hold;
                _startTime = now;
                elapsed = 0;
            }
            else
            {
                ratio = easeOutCubic(u); // 非线性渐入：先快后慢
            }
        }
        else if (_phase == Phase::Hold)
        {
            ratio = 1.0f;
            if (elapsed >= _holdMs)
            {
                // 维持结束：切到 FadeOut 阶段并复位计时
                _phase = Phase::FadeOut;
                _startTime = now;
                elapsed = 0;
            }
        }
        else // FadeOut
        {
            float u = (float)elapsed / _fadeOutMs;
            if (u >= 1.0f)
            {
                // 渐灭完成：动画结束，引擎自销毁
                _isFinished = true;
                return;
            }
            ratio = 1.0f - easeInCubic(u); // 非线性渐灭：先慢后快
        }

        // 2. 应用当前亮度系数（所有段共用一个亮度比例，便于统一脉动）
        uint8_t curBrightness = (uint8_t)(_brightness * ratio);

        // 3. 逐段绘制（每段使用各自的颜色，统一亮度曲线）
        for (uint8_t p = 0; p < _pressCount; p++)
        {
            // 取本段颜色：前 3 段用 _segmentColors，4 段起回退到 _fallbackColor
            CRGB segColor = (p < 3) ? _segmentColors[p] : _fallbackColor;

            CRGB lit = segColor;
            lit.nscale8(curBrightness);

            uint16_t begin, end;
            getPairRange(p, numLeds, begin, end);
            if (begin >= end)
                continue;
            for (uint16_t i = begin; i < end; i++)
            {
                leds[i] += lit; // 叠加混合，与底层动画兼容
            }
        }
    }
};
