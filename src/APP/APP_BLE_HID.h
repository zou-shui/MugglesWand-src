#pragma once
#include <stdint.h>
#include <stdbool.h>

bool APP_ble_keyboard_press_up(void);

bool APP_ble_keyboard_press_down(void);

// ==================== 鼠标模式 (手势4进入, 单击退出) ====================

/// @brief 进入鼠标模式：点亮蓝色指示灯并切换 IMU 数据流为 mux 3（角速度）
void APP_ble_mouse_mode_enter(void);

/// @brief 退出鼠标模式：熄灭指示灯并恢复 IMU 数据流为 mux 2（手势训练）
void APP_ble_mouse_mode_exit(void);

/// @brief 当前是否处于鼠标模式
bool APP_ble_mouse_in_mode(void);

// ==================== 音量旋钮模式 (手势6进入, 单击退出) ====================

/// @brief 进入音量模式：点亮红色指示灯并切换 IMU 数据流为 mux 4（修正角度）
void APP_ble_volume_mode_enter(void);

/// @brief 退出音量模式：熄灭指示灯并恢复 IMU 数据流为 mux 2（手势训练）
void APP_ble_volume_mode_exit(void);

/// @brief 当前是否处于音量模式
bool APP_ble_volume_in_mode(void);

/**
 * @brief 将 IMU 角速度映射为鼠标位移并发送
 * @param gx_mrad  valid_gx 值（毫弧度/秒），param1 原始值
 * @param gz_mrad  valid_gz 值（毫弧度/秒），param2 原始值
 */
void APP_ble_mouse_move(float gx_mrad, float gz_mrad);

/**
 * @brief 音量旋钮处理函数（相对旋转累积模式）
 * @param angle_deg 当前修正后的绝对角度（度），范围 (-180°, 180°)
 *
 * @details 调用频率 100Hz，内部自动完成：
 *  - EMA 低通滤波，抑制高频噪声
 *  - 角度环绕处理（±180° 跳变）
 *  - 死区过滤，防止静止漂移
 *  - 累积旋转角度，每超过 5° 阈值触发一次音量增/减
 *  - BLE 发送速率限制，防止快速旋转时报文洪泛
 */
void APP_ble_volume_knob(float angle_deg);
