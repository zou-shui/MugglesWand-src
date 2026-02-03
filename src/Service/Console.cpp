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
    Serial.println("===== Magic Wand Console =====");
    Serial.println("help     - Show command list");
    Serial.println("info     - Show system information");
    Serial.println("reboot   - Restart device");
    Serial.println("sleep    - Enter deep sleep mode");
    Serial.println("ota      - Enter OTA mode");
    Serial.println("flow [s] - Trigger WS2812 flowing animation, optional speed (1-10)");
    Serial.println("last     - Toggle WS2812 last LED on/off");
}

/************ 串口命令解析 ************/
static void console_parse(char *cmd)
{
    Serial.print("\n> ");
    Serial.println(cmd);

    command_msg_t msg;
    memset(&msg, 0, sizeof(msg));

    // 使用 strtok 分割字符串
    char *token = strtok(cmd, " "); // 第一个单词是命令
    if (token == NULL)
        return;

    if (!strcmp(token, "info"))
    {
        msg.type = CMD_SYS_INFO;
    }
    else if (!strcmp(token, "reboot"))
    {
        msg.type = CMD_SYS_REBOOT;
    }
    else if (!strcmp(token, "sleep"))
    {
        msg.type = CMD_SYS_SLEEP;
    }
    else if (!strcmp(cmd, "ota"))
    {
        msg.type = CMD_OTA_OTA;
    }
    else if (!strcmp(token, "flow"))
    {
        msg.type = CMD_WS2812_FLOW;
        token = strtok(NULL, " "); // 参数1
        if (token != NULL)
            msg.arg1 = atoi(token);
    }
    else if (!strcmp(token, "last"))
    {
        msg.type = CMD_WS2812_LAST;
    }

    else if (!strcmp(token, "help"))
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
