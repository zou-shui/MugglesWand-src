#include "APP.h"
#include "APP_Lumos.h"
#include "APP_BLE_HID.h"
#include "APP_EspNow.h"
#include "APP_POV.h"
#include "Service/EventBus.h"
#include "HAL/HAL.h"
#include "HAL/WS2812_Animation/AnimFlow.hpp"
#include "HAL/WS2812_Animation/AnimBlink.hpp"

QueueHandle_t app_queue = NULL;

static void app_task(void *pvParameters)
{
    SystemEvent event;

    while (1)
    {
        if (xQueueReceive(app_queue, &event, portMAX_DELAY))
        {
            switch (event.id)
            {
            case EVENT_GESTURE_DETECTED:
                switch (event.param1.i32)
                {
                case 0:
                    HAL::ws2812_start_fx(new AnimFlow(0xFFFFFF));
                    APP_espnow_tx_signal(MSG_SIGNAL_1); // 关灯
                    break;
                case 1:
                    HAL::ws2812_start_fx(new AnimBlink(CRGB::White, 200));
                    APP_espnow_tx_signal(MSG_SIGNAL_2); // 开灯
                    break;
                case 2:
                    HAL::ws2812_start_fx(new AnimFlow(0xFF0000));
                    APP_ble_keyboard_press_up(); // 键盘↑
                    break;
                case 3:
                    HAL::ws2812_start_fx(new AnimFlow(0x00FF00));
                    APP_ble_keyboard_press_down(); // 键盘↓
                    break;
                case 4:
                    APP_ble_mouse_mode_enter(); // 鼠标模式
                    break;
                case 5:
                    APP_Lumos_trigger(CRGB::White); // Lumos开关
                    break;
                case 6:
                    APP_ble_volume_mode_enter(); // 音量旋钮模式
                    break;
                case 7:
                    APP_POV_mode_enter(); // POV 光绘模式
                    break;
                }
                break;

            case EVENT_BTN_SHORT_PRESS:
                if (1 == event.param1.i32)
                {
                    // 单击退出当前所有开启的模式（与各模块的模式状态保持一致）
                    if (APP_ble_mouse_in_mode())
                        APP_ble_mouse_mode_exit();
                    if (APP_ble_volume_in_mode())
                        APP_ble_volume_mode_exit();
                    if (APP_POV_in_mode())
                        APP_POV_mode_exit();
                }
                break;

            case EVENT_IMU_DATA_UPDATED:
                // 数据语义由各模式的 mux 决定：3=角速度 / 4=修正角度 / 5=线性加速度
                if (APP_ble_mouse_in_mode())
                {
                    APP_ble_mouse_move(event.param1.f32, event.param2.f32);
                }
                if (APP_ble_volume_in_mode())
                {
                    APP_ble_volume_knob(event.param1.f32);
                }
                if (APP_POV_in_mode())
                {
                    // 剔除重力后的 X-Z 平面线性加速度 (单位 g)，挥动超阈值即触发光绘
                    APP_POV_on_imu_data(event.param1.f32, event.param2.f32);
                }
                break;

            case EVENT_POV_SET:
                // 参数设置事件：修改下一次光绘触发时的图案/方向
                // param1: 图案索引, param2: reverse (0/1)
                APP_POV_set_params((uint8_t)event.param1.i32, event.param2.i32 != 0);
                break;

            default:
                break;
            }
        }
    }
}

void APP_init()
{
    app_queue = xQueueCreate(16, sizeof(SystemEvent)); // 深度16，容纳高频鼠标数据
    EventBus::subscribe(EVENT_GESTURE_DETECTED, app_queue);
    EventBus::subscribe(EVENT_BTN_SHORT_PRESS, app_queue);
    EventBus::subscribe(EVENT_IMU_DATA_UPDATED, app_queue);
    EventBus::subscribe(EVENT_POV_SET, app_queue);

    APP_espnow_init();

    xTaskCreatePinnedToCore(
        app_task,
        "app_task",
        4096,
        NULL,
        2,
        NULL,
        1);
}