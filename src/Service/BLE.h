#pragma once
#include <Arduino.h>
#include "BLEHIDKeys.h"

/**
 * @brief 切换 BLE 的开启与关闭状态
 * @details 内部自动判断当前状态。关闭时会断开连接、停止广播并注销堆栈以最大化省电。
 * @return bool 返回切换后的 BLE 状态：true 表示已开启，false 表示已关闭
 */
bool ble_toggle(void);

/**
 * @brief 模拟单击指定的键盘按键（按下 + 延迟 + 释放）
 * @param keycode 键码 (定义在 BLEHIDKeys.h 中)
 * @return bool 发送成功返回 true，未连接或未开启返回 false
 */
bool ble_keyboard_tap_key(uint8_t keycode);

/**
 * @brief 更新并发送当前电池电量
 * @return true 发送成功，false 发送失败或未连接
 */
bool ble_update_battery(void);