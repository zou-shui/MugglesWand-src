#pragma once
#include <stdint.h>

bool ble_keyboard_press_up(void);

bool ble_keyboard_press_down(void);

/**
 * @brief 将 IMU 角速度映射为鼠标位移并发送
 * @param gx_mrad  valid_gx 值（毫弧度/秒），param1 原始值
 * @param gz_mrad  valid_gz 值（毫弧度/秒），param2 原始值
 */
void ble_mouse_move_from_imu(float gx_mrad, float gz_mrad);
