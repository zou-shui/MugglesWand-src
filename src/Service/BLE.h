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
 * @brief 查询 BLE 是否已开启（包括已开启但尚未连接主机的状态）
 */
bool ble_is_enabled(void);

/**
 * @brief 查询 BLE 是否已有主机连接
 */
bool ble_is_connected(void);

/**
 * @brief 模拟单击指定的键盘按键（按下 + 延迟 + 释放）
 * @param keycode 键码 (定义在 BLEHIDKeys.h 中)
 * @return bool 发送成功返回 true，未连接或未开启返回 false
 */
bool ble_keyboard_tap_key(uint8_t keycode);

/**
 * @brief 判断键盘 HID 是否已连接并可以发送报文
 */
bool ble_keyboard_is_ready(void);

/**
 * @brief 发送一次组合键：按下修饰键和普通键，保持一段时间后全部释放
 * @param modifiers HID 修饰键位图
 * @param keycode   普通键码；0 表示只按修饰键
 * @param hold_ms   按下保持时间
 */
bool ble_keyboard_tap_combo(uint8_t modifiers, uint8_t keycode,
                            uint16_t hold_ms);

/**
 * @brief 发送全部释放报文，清除可能残留的按键状态
 */
bool ble_keyboard_release_all(void);

/**
 * @brief 发送鼠标相对位移报文
 * @param dx X 轴位移 (-127 ~ 127)
 * @param dy Y 轴位移 (-127 ~ 127)
 * @return true 发送成功, false 未连接或未开启
 */
bool ble_mouse_move(int8_t dx, int8_t dy);

/**
 * @brief 发送 HID Consumer Control 报文（媒体键：音量、播放控制等）
 * @param usage_code Consumer Page usage code（定义在 BLEHIDKeys.h 的 MEDIA_* 宏）
 * @return true 发送成功, false 未连接或未开启
 */
bool ble_consumer_send(uint16_t usage_code);

/**
 * @brief 更新并发送当前电池电量
 * @return true 发送成功，false 发送失败或未连接
 */
bool ble_update_battery(void);
