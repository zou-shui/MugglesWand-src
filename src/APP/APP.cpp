#include "APP.h"
#include "APP_Lumos.h"
#include "APP_BLE_HID.h"
#include "APP_EspNow.h"
#include "Service/DualPrint.h"
#include "Service/EventBus.h"
#include "HAL/HAL.h"
#include "HAL/WS2812_Animation/AnimFlow.hpp"
#include "HAL/WS2812_Animation/AnimBlink.hpp"

QueueHandle_t app_queue = NULL;

static void app_task(void *pvParameters)
{
    static bool mouse_mode = false; // 鼠标模式开关状态
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
                    APP_Lumos_trigger(CRGB::White);
                    break;
                case 1:
                    HAL::ws2812_start_fx(new AnimBlink(CRGB::White, 200));
                    APP_espnow_tx_signal();
                    break;
                case 2:
                    HAL::ws2812_start_fx(new AnimFlow(0xFF0000));
                    APP_ble_keyboard_press_up();
                    break;
                case 3:
                    HAL::ws2812_start_fx(new AnimFlow(0x00FF00));
                    APP_ble_keyboard_press_down();
                    break;
                case 4:
                    HAL::ws2812_start_fx(new AnimFlow(0x0000FF));
                    break;
                case 5:
                    HAL::ws2812_start_fx(new AnimFlow(0xFFFFFF));
                    break;
                }
                break;

            case EVENT_APP_MOUSE_TOGGLE:
                mouse_mode = !mouse_mode;
                if (mouse_mode)
                {
                    APP_Lumos_on(CRGB::Blue);
                    DualSerial.println("[APP] Mouse mode enabled");
                    EventBus::publish(EVENT_IMU_SET_MUX, 3);
                }
                else
                {
                    APP_Lumos_off();
                    DualSerial.println("[APP] Mouse mode disabled");
                    EventBus::publish(EVENT_IMU_SET_MUX, 2);
                }
                break;

            case EVENT_IMU_DATA_UPDATED:
                if (mouse_mode)
                {
                    APP_ble_mouse_move(event.param1.f32, event.param2.f32);
                }
                break;

            default:
                break;
            }
        }
    }
}

void APP_init()
{
    app_queue = xQueueCreate(16, sizeof(SystemEvent)); // 增大队列以容纳高频鼠标数据
    EventBus::subscribe(EVENT_GESTURE_DETECTED, app_queue);
    EventBus::subscribe(EVENT_APP_MOUSE_TOGGLE, app_queue);
    EventBus::subscribe(EVENT_IMU_DATA_UPDATED, app_queue);

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