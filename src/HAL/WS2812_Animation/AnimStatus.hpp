#pragma once
#include "AnimationBase.hpp"

/**
 * @brief 状态指示动画（overlay 类型灯效，不自动销毁）
 *
 * 该动画用于指示手势识别的状态，绑定外部的状态变量 _gestureState，通过颜色变化和呼吸灯效果来显示当前状态。
 * 当 _gestureState 突然变为 4（识别成功）时，会触发一个 250ms 的锁定保护期，确保状态不会被瞬时的抖动干扰。
 * 颜色映射如下：
 *   - 0: 蓝色 (等待/就绪)
 *   - 1: 绿色 (捕捉到静止)
 *   - 2: 深青色 (正在寻找特征)
 *   - 3: 中紫红色 (寻找到特征)
 *   - 4: 白色 (手势识别成功)
 * 可选参数：ledIndex (目标灯珠), breathePeriodMs (呼吸周期毫秒), blendSpeed (切换颜色时混合速度)
 */
class AnimStatus : public AnimationBase
{
private:
    const volatile int8_t &_gestureState;
    uint16_t _targetLedIndex;
    uint16_t _breathePeriodMs;
    uint8_t _blendSpeed;

    CRGB _currentColor;

    // --- 脉冲状态锁定变量 ---
    int8_t _lastRenderedState; // 记录上一次真正用来渲染的状态
    uint32_t _lockStartTime;   // 锁定开始的绝对时间戳
    bool _isStateLocked;       // 当前是否处于锁定保护期

    // 内部辅助函数：根据状态码映射颜色
    CRGB get_color_by_state(int8_t state)
    {
        switch (state)
        {
        case 0:
            return CRGB::Blue; // 状态0：等待/就绪
        case 1:
            return CRGB::Green; // 状态1：捕捉到静止
        case 2:
            return CRGB::DarkCyan; // 状态2：正在寻找特征
        case 3:
            return CRGB::MediumVioletRed; // 状态3：寻找到特征
        case 4:
            return CRGB::White; // 状态4：手势识别成功（高亮白）
        default:
            return CRGB::Black;
        }
    }

public:
    AnimStatus(const volatile int8_t &stateRef, uint16_t ledIndex = 0, uint16_t breathePeriodMs = 0, uint8_t blendSpeed = 30)
        : _gestureState(stateRef),
          _targetLedIndex(ledIndex),
          _breathePeriodMs(breathePeriodMs),
          _blendSpeed(blendSpeed),
          _lastRenderedState(0),
          _lockStartTime(0),
          _isStateLocked(false) {}

    void init() override
    {
        AnimationBase::init();
        _currentColor = get_color_by_state(_gestureState);
        _lastRenderedState = _gestureState;
        _isStateLocked = false;
    }

    void update(CRGB *leds, uint16_t numLeds) override
    {
        if (_targetLedIndex >= numLeds)
            return;

        uint32_t currentTime = millis();
        int8_t targetState = _gestureState; // 获取外部真实状态

        // 1. 💡 锁定状态机核心逻辑
        if (_isStateLocked)
        {
            // 如果处于锁定保护期，检查时间是否到了 250ms
            if (currentTime - _lockStartTime >= 250)
            {
                _isStateLocked = false; // 解锁，恢复正常读取外部状态
            }
            else
            {
                // 在 250ms 的保护期内，强行将目标状态锁死为状态 4
                targetState = 4;
            }
        }
        else
        {
            // 如果未锁定，且发现外部突然传来了瞬时状态 4
            if (targetState == 4)
            {
                _isStateLocked = true;
                _lockStartTime = currentTime; // 开启 250ms 倒计时计时器
            }
        }

        // 2. 根据最终确定的状态码获取目标颜色
        CRGB targetColor = get_color_by_state(targetState);

        // 3. 颜色平滑混合 (Crossfade)
        nblend(_currentColor, targetColor, _blendSpeed);

        // 4. 复制并应用亮度缩放
        CRGB renderColor = _currentColor;

        // 视觉优化：当成功识别（状态4）时，我们通常希望白色“最亮闪烁”，暂时不需要被呼吸灯压暗
        if (targetState == 4)
        {
            renderColor.nscale8(80); // 识别成功时的亮度限制
        }
        else if (_breathePeriodMs > 0 && targetColor != CRGB::Black)
        {
            // 普通状态继续平滑呼吸
            uint32_t elapsedTime = currentTime - _startTime;
            uint8_t angle = (elapsedTime * 256) / _breathePeriodMs;
            uint8_t brightness = quadwave8(angle);
            brightness = map(brightness, 0, 255, 20, 50); // 呼吸灯亮度限制
            renderColor.nscale8(brightness);
        }
        else
        {
            renderColor.nscale8(50); // 常亮时亮度限制
        }

        // 5. 强制覆盖顶层灯珠
        leds[_targetLedIndex] = renderColor;
    }
};