#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================
// 手势映射表 GestureBinding
// ============================================================
// 维护“手势 ID → APP 动作”的映射表，并持久化到 NVS（命名空间 wand_map）。
// 表项数量在初始化时由模型输出类别数决定；未绑定的手势在触发时被忽略，
// 记录缺失或损坏时恢复内置默认映射。
//
// 控制台命令（由 Console 转发）：
//   map list
//   map bind <gesture> <app> [parameter]
//   map clear <gesture>
//   map reset
// 可用 app 及参数格式见各模块的 *_validate()；ble_keyboard 的
// parameter 为 BLEKeySequence.h 定义的按键序列。
// ============================================================

constexpr size_t GESTURE_APP_NAME_SIZE = 16;   // APP 名称缓冲大小
constexpr size_t GESTURE_PARAMETER_SIZE = 128; // APP 参数缓冲大小

struct GestureBinding
{
    char app[GESTURE_APP_NAME_SIZE];        // 空串表示该手势未绑定
    char parameter[GESTURE_PARAMETER_SIZE]; // APP 参数，如按键序列
};

// Load the persistent mapping table and initialize the built-in defaults.
void APP_binding_init(void);

// Execute the app currently bound to a gesture ID.
void APP_binding_execute(int32_t gesture);

// Handle the raw argument string following the top-level "map" command.
void APP_binding_console(const char *args);
