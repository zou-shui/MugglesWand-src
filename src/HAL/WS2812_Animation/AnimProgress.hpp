#pragma once
#include "AnimationBase.hpp"

/**
 * @brief 进度条动画（bg/overlay 类型灯效，持续运行不自动销毁）
 *
 * 绑定外部 uint8_t 变量（0~100），以 Flow 风格在灯带上渲染进度条。
 * 拖尾长度等于 WS2812_LED_COUNT，头部位置由进度百分比决定。
 * 当进度跳变时，视觉位置会平滑过渡到目标位置。
 */
class AnimProgress : public AnimationBase
{
private:
	const uint8_t &_progressRef; // 外部进度变量引用（0~100）
	CRGB _color;
	float _visualPos;	 // 当前平滑后的视觉位置（浮点，亚像素精度）
	float _smoothFactor; // 每帧向目标靠近的比例（0~1，值越大越跟手）

public:
	/**
	 * @param progressRef 外部进度变量（uint8_t, 0~100）
	 * @param color       光流颜色
	 * @param smoothFactor 平滑过渡因子（0.05~0.5 推荐，默认 0.15）
	 */
	AnimProgress(const uint8_t &progressRef, CRGB color = CRGB::White, float smoothFactor = 0.1f)
		: _progressRef(progressRef), _color(color), _visualPos(0.0f), _smoothFactor(smoothFactor) {}

	void init() override
	{
		AnimationBase::init();
		// 初始化时直接跳到目标位置，避免从 0 滑入的延迟
		_visualPos = 0.0f;
	}

	void update(CRGB *leds, uint16_t numLeds) override
	{
		if (numLeds == 0)
			return;

		// 1. 从外部变量计算目标头部位置
		uint8_t progress = _progressRef;
		if (progress > 100)
			progress = 100;
		float targetPos = (progress / 100.0f) * (numLeds - 1);

		// 2. 平滑过渡：每帧向目标靠近 _smoothFactor 比例的剩余距离
		_visualPos += (targetPos - _visualPos) * _smoothFactor;

		// 当距离目标极近时，直接吸附避免永远追不上
		if (fabs(targetPos - _visualPos) < 0.05f)
			_visualPos = targetPos;

		int head = (int)_visualPos;

		// 3. 渲染进度条：均匀点亮 0 ~ head 区间的灯珠
		for (int i = 0; i <= head && i < numLeds; i++)
		{
			CRGB pixelColor = _color;
			pixelColor.nscale8(20); // 亮度缩放，避免过亮
			leds[i] += pixelColor;
		}
	}
};
