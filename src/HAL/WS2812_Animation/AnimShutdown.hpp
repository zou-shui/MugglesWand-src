#pragma once
#include "AnimationBase.hpp"

/**
 * @brief 长按关机衔接动画（overlay 类型灯效，播放完毕自销毁）
 *
 * 衔接长按时完全点亮的灯带，将"关机"动作可视化：
 *   - 第一帧即全带点亮，与 AnimTap 的全亮状态无缝交接（无闪断）；
 *   - 暗区从灯带两端向中心汇聚收缩（先快后慢），亮区逐渐收窄；
 *   - 收窄到中心后，中心段回闪一次（正弦脉冲）随即全黑，动画结束。
 *
 * 与 AnimLumos 相同的状态绑定模式：构造函数接收外部变量的指针，
 * 播放完毕的收尾帧将 *done 置为 true，用于通知上层"关机已确认"。
 * （上层按键模块另有超时兜底：动画异常未播时也会照常发布关机命令。）
 *
 * 注意：动画播完后不自动销毁，而是保持全黑持续占据 overlay 层直到电源切断——
 * 否则引擎回收 overlay 后，底层 AnimTap 熄灭阶段残余的灯珠会短暂露出。
 *
 * overlay 语义：每帧先清底再强制绘制，覆盖 bg / fx / LED0 状态灯等一切动效。
 */
class AnimShutdown : public AnimationBase
{
private:
    CRGB _color;
    bool *_done;        // 绑定外部"关机已确认"标志（动画播完时置 true）
    uint16_t _startIndex; // 动画起始灯珠下标
    uint16_t _ledCount;   // 参与动画的灯珠数量
    uint32_t _totalMs;    // 动画总时长（毫秒）
    uint8_t _brightness;  // 汇聚阶段的全带亮度（0~255，默认 20 避免刺眼）；
                          // FLASH 回闪峰值固定为 200，属于"闪光能量"而非主亮度，不参数化

public:
    /**
     * @param color      灯光颜色
     * @param done       外部"关机已确认"标志地址（可空，为空则只播动画不置位）
     * @param startIndex 动画起始灯珠下标
     * @param ledCount   参与动画的灯珠数量
     * @param totalMs    动画总时长（毫秒）
     * @param brightness 汇聚阶段全带亮度（0~255，默认 20 与 AnimTap 对齐）
     */
    AnimShutdown(CRGB color = CRGB::White, bool *done = nullptr,
                 uint16_t startIndex = 0, uint16_t ledCount = 41, uint32_t totalMs = 500,
                 uint8_t brightness = 20)
        : _color(color),
          _done(done),
          _startIndex(startIndex),
          _ledCount(ledCount == 0 ? 1 : ledCount),
          _totalMs(totalMs == 0 ? 1 : totalMs),
          _brightness(brightness) {}

    void update(CRGB *leds, uint16_t numLeds) override
    {
        // 越界保护：起始下标 + 占用数量超出灯带范围时直接结束
        if (_startIndex + _ledCount > numLeds)
        {
            _isFinished = true;
            return;
        }

        // overlay 语义：强制覆盖，先清底再绘制
        fill_solid(leds, numLeds, CRGB::Black);

        float u = (float)(millis() - _startTime) / _totalMs;
        if (u >= 1.0f)
        {
            // 动画播完：保持全黑驻留 overlay 层，直到电源切断。
            // 不置 _isFinished——否则引擎回收本层后，底层 AnimTap 熄灭阶段
            // 残余的 LED0 会短促露出。此处置位 done 通知上层可关机。
            if (_done != nullptr)
            {
                *_done = true;
            }
            return;
        }

        const float FLASH_START = 0.85f; // 汇聚阶段占总时长的 85%，之后为回闪阶段
        const float FLASH_PEAK   = 200.0f; // 中心段正弦脉冲峰值（闪光能量，固定）

        if (u >= FLASH_START)
        {
            // 回闪阶段：中心段正弦脉冲，亮起后迅速熄灭
            float fp = (u - FLASH_START) / (1.0f - FLASH_START);
            float brightness = sin(fp * PI) * FLASH_PEAK;
            if (brightness > 1.0f)
            {
                CRGB flash = _color;
                flash.nscale8((uint8_t)brightness);
                leds[_startIndex + _ledCount / 2] = flash;
            }
        }
        else
        {
            // 汇聚阶段：暗区从两端向中心收缩（先快后慢的 ease-out-cubic）
            float collapse = u / FLASH_START;
            float ease = 1.0f - (1.0f - collapse) * (1.0f - collapse) * (1.0f - collapse);
            uint16_t shrink = (uint16_t)(_ledCount / 2.0f * ease + 0.5f);

            CRGB lit = _color;
            lit.nscale8(_brightness);

            for (uint16_t i = shrink; i + shrink < _ledCount; i++)
            {
                leds[_startIndex + i] = lit;
            }
        }
    }
};
