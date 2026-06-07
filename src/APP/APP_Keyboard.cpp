#include "APP_Keyboard.h"
#include "Service/BLE.h"

bool ble_keyboard_press_up(void)
{
    return ble_keyboard_tap_key(KEY_UP); // KEY_UP 定义在 BLEHIDKeys.h 中为 0x52
}

bool ble_keyboard_press_down(void)
{
    return ble_keyboard_tap_key(KEY_DOWN); // KEY_DOWN 定义在 BLEHIDKeys.h 中为 0x51
}