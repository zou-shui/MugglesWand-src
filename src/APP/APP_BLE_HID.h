#pragma once
#include <stdint.h>
#include <stdbool.h>

// ============================================================
// BLE HID 应用层
// ============================================================
// 提供三种手势映射动作：
//   - ble_keyboard：按键序列入队，由独立任务按序发送
//   - ble_mouse：IMU 角速度 → 鼠标相对位移
//   - ble_knob：IMU 修正角度 → 音量增减
// 各模式通过 IMU mux 获取对应数据流，模式指示灯复用 APP_Lumos。
// ============================================================

// ==================== 键盘 ========================
/// @brief 创建键盘序列队列和独立发送任务
void APP_ble_keyboard_init(void);

/// @brief 校验手势映射中的键盘序列
bool APP_ble_keyboard_validate(const char *parameter);

/// @brief 将手势映射中的键盘序列加入发送队列
bool APP_ble_keyboard_execute(const char *parameter);

// ==================== 鼠标模式 ====================

/// @brief 进入鼠标模式：点亮蓝色指示灯并切换 IMU 数据流为 mux 3（角速度）
void APP_ble_mouse_mode_enter(void);

/// @brief 退出鼠标模式：熄灭指示灯并恢复 IMU 数据流为 mux 2（手势训练）
void APP_ble_mouse_mode_exit(void);

/// @brief 当前是否处于鼠标模式
bool APP_ble_mouse_in_mode(void);

/// @brief 手势映射入口：进入鼠标模式，无参数
bool APP_ble_mouse_execute(const char *parameter);

/**
 * @brief 将 IMU 角速度映射为鼠标位移并发送
 * @param gx_rad valid_gx 值（弧度/秒）
 * @param gz_rad valid_gz 值（弧度/秒）
 */
void APP_ble_mouse_move(float gx_rad, float gz_rad);

// ==================== 音量旋钮模式 ====================

/// @brief 进入音量模式：点亮红色指示灯并切换 IMU 数据流为 mux 4（修正角度）
void APP_ble_volume_mode_enter(void);

/// @brief 退出音量模式：熄灭指示灯并恢复 IMU 数据流为 mux 2（手势训练）
void APP_ble_volume_mode_exit(void);

/// @brief 当前是否处于音量模式
bool APP_ble_volume_in_mode(void);

/// @brief 手势映射入口：进入音量旋钮模式，无参数
bool APP_ble_knob_execute(const char *parameter);

/**
 * @brief 音量旋钮处理函数（相对旋转累积模式）
 * @param angle_deg 当前修正后的绝对角度（度），范围 (-180°, 180°)
 *
 * @details 调用频率 100Hz，内部自动完成：
 *  - EMA 低通滤波，抑制高频噪声
 *  - 角度环绕处理（±180° 跳变）
 *  - 死区过滤，防止静止漂移
 *  - 累积旋转角度，每超过 1.5° 阈值触发一次音量增/减
 *  - BLE 发送速率限制，防止快速旋转时报文洪泛
 */
void APP_ble_volume_knob(float angle_deg);
