/*
用于解析串口控制台命令输入，供Dispatcher.cpp处理
*/
#include "Console.h"

#define CONSOLE_QUEUE_SIZE 8
#define CONSOLE_BUF_SIZE 64

static QueueHandle_t cmd_queue;

static char rx_buf[CONSOLE_BUF_SIZE];
static uint8_t rx_index = 0;

/************ 显示帮助信息 ************/
static void console_print_help()
{
    Serial.println("=== Magic Wand Console ===");
    Serial.println("help    - Show command list");
    Serial.println("info    - Show system information");
    Serial.println("reboot  - Restart device");
    Serial.println("ota     - Enter OTA mode");
}

/************ 串口命令解析 ************/
static void console_parse(char *cmd)
{
    Serial.print("\n> ");
    Serial.println(cmd);

    command_msg_t msg;
    memset(&msg, 0, sizeof(msg));

    if (!strcmp(cmd, "info"))
    {
        msg.type = CMD_SYS_INFO;
    }
    else if (!strcmp(cmd, "reboot"))
    {
        msg.type = CMD_SYS_REBOOT;
    }

    else if (!strcmp(cmd, "ota"))
    {
        msg.type = CMD_OTA_OTA;
    }

    else if (!strcmp(cmd, "help"))
    {
        console_print_help();
        return;
    }
    else
    {
        Serial.println("Unknown command");
        return;
    }
    xQueueSend(cmd_queue, &msg, portMAX_DELAY);
}

/************ Console Task ************/
static void console_task(void *param)
{
    Serial.println("[Console] Ready");

    while (1)
    {
        while (Serial.available())
        {
            char c = Serial.read();

            // Enter
            if (c == '\n' || c == '\r')
            {
                rx_buf[rx_index] = 0;

                if (rx_index > 0)
                {
                    console_parse(rx_buf);
                }

                rx_index = 0;
            }
            else
            {
                if (rx_index < CONSOLE_BUF_SIZE - 1)
                {
                    rx_buf[rx_index++] = c;
                }
            }
        }

        vTaskDelay(10);
    }
}

/************ Getter ************/
QueueHandle_t console_get_queue()
{
    return cmd_queue;
}

/************ Init ************/
void console_init()
{
    cmd_queue = xQueueCreate(CONSOLE_QUEUE_SIZE, sizeof(command_msg_t));

    xTaskCreatePinnedToCore(
        console_task,
        "console_task",
        4096,
        NULL,
        2,
        NULL,
        0);
}
