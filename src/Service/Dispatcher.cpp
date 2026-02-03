/*
处理控制台命令
*/

#include "dispatcher.h"
#include "Service/Console.h"

#include "Config.h"
#include "OTA.h"
#include "Sleep.h"

static QueueHandle_t cmd_queue;

/************ Dispatcher Task ************/
static void dispatcher_task(void *param)
{
    command_msg_t msg;

    while (1)
    {
        if (xQueueReceive(cmd_queue, &msg, portMAX_DELAY))
        {
            switch (msg.type)
            {

            case CMD_SYS_INFO:
                Serial.printf("Version: ");
                Serial.println(FIRMWARE_VER);
                Serial.printf("BuildDate: ");
                Serial.println(BUILD_DATE);
                Serial.printf("BuildTime: ");
                Serial.println(BUILD_TIME);
                Serial.printf("Temperature: %d°C\n", (int)temperatureRead());
                break;

            case CMD_SYS_REBOOT:
                Serial.println("Rebooting...");
                delay(500);
                ESP.restart();
                break;

            case CMD_OTA_OTA:
                // 可以先关闭其它task，再OTA
                OTA_begin();
                break;

            case CMD_SYS_SLEEP:
                sleep_enter();
                break;

            default:
                break;
            }
        }
    }
}

/************ Init ************/
void dispatcher_init()
{
    cmd_queue = console_get_queue();

    xTaskCreatePinnedToCore(
        dispatcher_task,
        "dispatcher_task",
        4096,
        NULL,
        3, // 优先级高于 console
        NULL,
        1);
}
