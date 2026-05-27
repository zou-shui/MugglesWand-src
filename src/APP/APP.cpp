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
                APP_Lumos_trigger();
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