#include "APP_BLE_HID.h"
#include "Service/BLE.h"
#include <Arduino.h>

// 鼠标灵敏度：弧度/秒 → 鼠标像素位移的缩放系数
// 例如 1 rad/s 的旋转 → 1 * 20 = 20 像素的位移
#define MOUSE_SENSITIVITY 20.0f

// 死区阈值（弧度/秒）：低于此值的角速度视为噪声，不产生鼠标移动
#define MOUSE_DEADZONE 0.15f

bool ble_keyboard_press_up(void)
{
    return ble_keyboard_tap_key(KEY_UP); // KEY_UP 定义在 BLEHIDKeys.h 中为 0x52
}

bool ble_keyboard_press_down(void)
{
    return ble_keyboard_tap_key(KEY_DOWN); // KEY_DOWN 定义在 BLEHIDKeys.h 中为 0x51
}

void ble_mouse_move_from_imu(int32_t gx_mrad, int32_t gz_mrad)
{
    // 恢复为弧度/秒
    float gx = (float)gx_mrad / 1000.0f;
    float gz = (float)gz_mrad / 1000.0f;

    // 死区过滤
    if (fabsf(gx) < MOUSE_DEADZONE) gx = 0.0f;
    if (fabsf(gz) < MOUSE_DEADZONE) gz = 0.0f;

    // 缩放并钳位到 int8_t 范围
    int8_t dx = (int8_t)constrain((int32_t)(-gx * MOUSE_SENSITIVITY), -127L, 127L);
    int8_t dy = (int8_t)constrain((int32_t)(gz * MOUSE_SENSITIVITY), -127L, 127L);

    // 两轴都为 0 则跳过，减少 BLE 报文发送
    if (dx == 0 && dy == 0) return;

    ble_mouse_move(dx, dy);
}