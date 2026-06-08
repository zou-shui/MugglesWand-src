#include "APP.h"
#include "APP_Lumos.h"
#include "APP_BLE_HID.h"
#include "Service/EventBus.h"
#include "HAL/HAL.h"
#include "HAL/WS2812_Animation/AnimFlow.hpp"

QueueHandle_t app_queue = NULL;

static void app_task(void *pvParameters)
{
    SystemEvent event;
    while (1)
    {
        if (xQueueReceive(app_queue, &event, portMAX_DELAY))
        {
            switch (event.param1)
            {
            case 0:
                APP_Lumos_trigger(0xFFFFFF);
                break;
            case 1:
                HAL::ws2812_start_fx(new AnimFlow());
                break;
            case 2:
                HAL::ws2812_start_fx(new AnimFlow(0xFF0000));
                ble_keyboard_press_up();
                break;
            case 3:
                HAL::ws2812_start_fx(new AnimFlow(0x00FF00));
                ble_keyboard_press_down();
                break;
            case 4:
                HAL::ws2812_start_fx(new AnimFlow(0x0000FF));
                break;
            case 5:
                HAL::ws2812_start_fx(new AnimFlow(0xFFFF00));
                break;
            }
        }
    }
}

void APP_init()
{
    app_queue = xQueueCreate(8, sizeof(SystemEvent));
    EventBus::subscribe(EVENT_GESTURE_DETECTED, app_queue);

    xTaskCreatePinnedToCore(
        app_task,
        "app_task",
        4096,
        NULL,
        2,
        NULL,
        1);
}