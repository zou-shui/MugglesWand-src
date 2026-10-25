#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================
// BLE 键盘序列解析器 BLEKeySequence
// ============================================================
// 将手势映射参数中的字符串解析为一系列 HID 按键动作，供
// APP_BLE_HID 的键盘任务按顺序发送。
//
// 序列语法（动作之间用单个逗号分隔）：
//   - 普通键：a、Z、enter、esc、f1、left、space 等，单字符可直接书写
//   - 组合键：[ctrl+shift+s]、[alt+f4]，方括号内只能有一个非修饰键
//   - 时序：key(hold_ms,delay_ms)，省略项取默认值，单项最大 10000ms
//   - 字面逗号：连续 2*n+1 个逗号表示 n 个逗号键，如 a,,,b 等于 a , b
//   - 示例："[ctrl+c]"、"h,e,l,l,o,enter"、"f5(50,200)"
//
// 回调返回 false 会中止后续动作（例如 BLE 键盘已断开）；
// 传 nullptr 作为回调时只做语法校验，不执行任何动作。
// ============================================================

constexpr size_t BLE_KEY_SEQUENCE_MAX = 128;      // 序列缓冲上限（含结尾 '\0'）
constexpr uint16_t BLE_KEY_DEFAULT_HOLD_MS = 25;  // 默认按下保持时间
constexpr uint16_t BLE_KEY_DEFAULT_DELAY_MS = 25; // 默认动作间隔
constexpr uint16_t BLE_KEY_TIMING_MAX_MS = 10000; // hold/delay 单项上限

struct BLEKeyAction
{
    uint8_t modifiers; // HID 修饰键位图
    uint8_t keycode;   // HID 键码，0 表示只按修饰键
    uint16_t hold_ms;  // 按下保持时间（毫秒）
    uint16_t delay_ms; // 本动作后到下一动作的间隔（毫秒）
};

using BLEKeySequenceCallback = bool (*)(const BLEKeyAction &action, void *context);

// 按顺序解析并（可选）执行序列；返回 false 表示语法错误或回调中止。
bool BLEKeySequence_parse(const char *sequence,
                          BLEKeySequenceCallback callback,
                          void *context);
