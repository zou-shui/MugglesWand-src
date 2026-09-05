#pragma once
#include "AnimationBase.hpp"

/**
 * @brief 开机/唤醒一次性动画（fx 类型灯效，播放完毕自销毁）
 *
 * 从中心点灯，向两端以 ease-out-cubic（非线性，先快后慢）扩散；
 * 扩散过程中整体亮度按 _decaySpeed 比例线性衰减；
 * 待灯带全填充后进入整体熄灭阶段，亮度继续线性降到 0，动画结束。
 * 引擎在 finished() 时自动 delete 本对象，露出底层动画。
 *
 * 可调参数：
 *   - totalMs         动画总时长（毫秒）
 *   - peakBrightness  中心峰值亮度（0~255，建议 ≤ 60 避免刺眼）
 *   - decaySpeed      亮度衰减速度（0~1，推荐 0.3~0.7；
 *                     1.0 表示全填充瞬间已为 0，0.0 表示全填充后不再衰减）
 *
 * 混合语义：使用 leds[idx] += lit 叠加写入，与底层动画（呼吸/状态灯）兼容；
 * 启动时无其他图层，叠加等同于覆盖。
 */
class AnimBootup : public AnimationBase
{
private:
    CRGB _color;
    uint16_t _startIndex;    // 动画起始灯珠下标
    uint16_t _ledCount;      // 参与动画的灯珠数量
    uint16_t _center;        // 中心灯珠下标（_startIndex + _ledCount / 2）
    uint32_t _totalMs;       // 动画总时长（毫秒）
    uint8_t _peakBrightness; // 中心峰值亮度（0~255）
    float _decaySpeed;       // 亮度衰减速度（0~1，钳位）

public:
    /**
     * @param color          灯光颜色
     * @param startIndex     动画起始灯珠下标
     * @param ledCount       参与动画的灯珠数量
     * @param totalMs        动画总时长（毫秒）
     * @param peakBrightness 中心峰值亮度（0~255，建议 ≤ 60 避免刺眼）
     * @param decaySpeed     亮度衰减速度（0~1）
     */
    AnimBootup(CRGB color = CRGB::White,
               uint16_t startIndex = 0, uint16_t ledCount = 41,
               uint32_t totalMs = 750,
               uint8_t peakBrightness = 20,
               float decaySpeed = 0.9f)
        : _color(color),
          _startIndex(startIndex),
          _ledCount(ledCount == 0 ? 1 : ledCount),
          _center((uint16_t)(startIndex + _ledCount / 2)),
          _totalMs(totalMs == 0 ? 1 : totalMs),
          _peakBrightness(peakBrightness),
          _decaySpeed(decaySpeed < 0.0f ? 0.0f : (decaySpeed > 1.0f ? 1.0f : decaySpeed))
    {
    }

    void update(CRGB *leds, uint16_t numLeds) override
    {
        // 越界保护：起始下标 + 占用数量超出灯带范围时直接结束，让引擎自销毁
        if (_startIndex + _ledCount > numLeds)
        {
            _isFinished = true;
            return;
        }

        float u = (float)(millis() - _startTime) / _totalMs;
        if (u >= 1.0f)
        {
            _isFinished = true;
            return;
        }

        // 阶段分配：扩散阶段 70% 时长，整体熄灭阶段 30%
        const float PHASE1_RATIO = 0.7f;

        if (u < PHASE1_RATIO)
        {
            // ========== 阶段 1：中心向两端 ease-out-cubic 扩散 ==========
            float u1 = u / PHASE1_RATIO;                                 // 0~1
            float ease = 1.0f - (1.0f - u1) * (1.0f - u1) * (1.0f - u1); // ease-out-cubic
            uint16_t maxRadius = _ledCount / 2;
            uint16_t radius = (uint16_t)(maxRadius * ease + 0.5f);

            // 整体亮度按 _decaySpeed 比例线性衰减
            float dimFactor = 1.0f - _decaySpeed * u1;
            if (dimFactor < 0.0f)
                dimFactor = 0.0f;
            uint8_t brightness = (uint8_t)(_peakBrightness * dimFactor);
            if (brightness < 1)
                return; // 亮度极低时跳过绘制，加速收敛

            CRGB lit = _color;
            lit.nscale8(brightness);

            // 中心对称铺亮 [-radius, +radius]
            for (int16_t i = -radius; i <= radius; i++)
            {
                int16_t idx = (int16_t)_center + i;
                leds[idx] += lit;
            }
        }
        else
        {
            // ========== 阶段 2：全填充后整体线性衰减到 0 ==========
            float u2 = (u - PHASE1_RATIO) / (1.0f - PHASE1_RATIO); // 0~1
            float startDim = 1.0f - _decaySpeed;                   // 阶段 1 结束时的剩余比例
            float dimFactor = startDim * (1.0f - u2);
            if (dimFactor < 0.0f)
                dimFactor = 0.0f;
            uint8_t brightness = (uint8_t)(_peakBrightness * dimFactor);
            if (brightness < 1)
            {
                _isFinished = true;
                return;
            }

            CRGB lit = _color;
            lit.nscale8(brightness);

            // 全灯带等亮度衰减
            for (uint16_t i = 0; i < _ledCount; i++)
            {
                leds[_startIndex + i] += lit;
            }
        }
    }
};
