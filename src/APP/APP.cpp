#include "APP.h"
#include "APP_Lumos.h"
#include "Service/EventBus.h"

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
                APP_Lumos_trigger(0xFF0000);
                break;
            case 2:
                APP_Lumos_trigger(0x00FF00);
                break;
            case 3:
                APP_Lumos_trigger(0x0000FF);
                break;
            case 4:
                APP_Lumos_trigger(0xFFFF00);
                break;
            case 5:
                APP_Lumos_trigger(0xFF00FF);
                break;
            case 6:
                APP_Lumos_trigger(0x00FFFF);
                break;
            case 7:
                APP_Lumos_trigger(0x111111);
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