#pragma once
#include "AnimationBase.hpp"

/**
 * @brief 全灯带呼吸动画（bg / overlay 类型灯效，持续运行不自动销毁）
 *
 * 所有灯珠以相同颜色同步呼吸，亮度在 _brightMin 与 _brightMax 之间正弦波振荡。
 * 颜色、周期、最大/最小亮度均可调。
 */
class AnimBreathe : public AnimationBase
{
private:
	CRGB _color;
	uint16_t _periodMs; // 一个完整呼吸周期（毫秒）
	uint8_t _brightMin; // 最小亮度（0~255）
	uint8_t _brightMax; // 最大亮度（0~255）

public:
	/**
	 * @param color     呼吸颜色
	 * @param periodMs  呼吸周期（毫秒）
	 * @param brightMin 最小亮度（0~255）
	 * @param brightMax 最大亮度（0~255）
	 */
	AnimBreathe(CRGB color = CRGB::White, uint16_t periodMs = 4000,
				uint8_t brightMin = 0, uint8_t brightMax = 20)
		: _color(color), _periodMs(periodMs), _brightMin(brightMin), _brightMax(brightMax) {}

	void update(CRGB *leds, uint16_t numLeds) override
	{
		if (numLeds == 0 || _periodMs == 0)
			return;

		// 用运行时长驱动正弦波相位
		uint32_t elapsed = millis() - _startTime;
		uint8_t phase = (elapsed * 256) / _periodMs;

		// quadwave8 产生 0→255→0 的平滑呼吸曲线
		uint8_t wave = quadwave8(phase);

		// 映射到 [_brightMin, _brightMax] 区间
		uint8_t brightness = map(wave, 0, 255, _brightMin, _brightMax);

		CRGB pixelColor = _color;
		pixelColor.nscale8(brightness);

		for (int i = 0; i < numLeds; i++)
		{
			leds[i] += pixelColor;
		}
	}
};
