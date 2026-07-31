#pragma once
#include <stdint.h>

bool APP_ble_keyboard_press_up(void);

bool APP_ble_keyboard_press_down(void);

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
