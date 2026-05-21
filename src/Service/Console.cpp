/*
    解析串口控制台命令输入，并将命令通过CommandBus发送给Dispatcher处理
*/
#include <Arduino.h>
#include "Console.h"
#include "CommandBus.h"

#define CONSOLE_BUF_SIZE 64

static char rx_buf[CONSOLE_BUF_SIZE];
static uint8_t rx_index = 0;

/************ 显示帮助信息 ************/
static void console_print_help()
{
    Serial.println("========= Magic Wand Console =========");
    Serial.println("help          - Show command list");
    Serial.println("info          - Show system information");
    Serial.println("sleep         - Enter deep sleep mode");
    Serial.println("reboot        - Restart device");
    Serial.println("shutdown      - Turn off the power");
    Serial.println("inference     - Enter gesture inference mode");
    Serial.println("charge        - Enter charge mode");
    Serial.println("ota           - Enter OTA mode");
    Serial.println("ble           - Toggle BLE service on/off");
    Serial.println("imu           - Print MPU6050 IMU data");
    Serial.println("stop          - Stop all printing activities");
    Serial.println("brea [c] [p]  - Trigger WS2812 breathe animation, optional color and period (ms)");
    Serial.println("flow [s] [t]  - Trigger WS2812 flowing animation, optional speed (1-50) and tail length (1-50)");
    Serial.println("last [c]      - Toggle WS2812 last LED on/off, optional color");
}

/************ 串口命令解析 ************/
void console_parse(char *cmd)
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
    else if (!strcmp(token, "sleep"))
    {
        msg.type = CMD_SYS_SLEEP;
    }
    else if (!strcmp(token, "reboot"))
    {
        msg.type = CMD_SYS_REBOOT;
    }
    else if (!strcmp(token, "shutdown"))
    {
        msg.type = CMD_SYS_SHUTDOWN;
    }

    else if (!strcmp(token, "inference"))
    {
        msg.type = CMD_USR_INFERENCE;
    }
    else if (!strcmp(token, "charge"))
    {
        msg.type = CMD_USR_CHARGE;
    }

    else if (!strcmp(token, "ota"))
    {
        msg.type = CMD_OTA_OTA;
    }
    else if (!strcmp(token, "ble"))
    {
        msg.type = CMD_BLE_BLE;
    }
    else if (!strcmp(token, "imu"))
    {
        msg.type = CMD_MPU6050_IMU;
    }
    else if (!strcmp(token, "stop"))
    {
        msg.type = CMD_CONS_STOP;
    }
    else if (!strcmp(token, "brea"))
    {
        msg.type = CMD_WS2812_BREA;
        token = strtok(NULL, " "); // 参数1
        if (token != NULL)
            msg.arg1 = atoi(token);
        token = strtok(NULL, " "); // 参数2
        if (token != NULL)
            msg.arg2 = atoi(token);
    }
    else if (!strcmp(token, "flow"))
    {
        msg.type = CMD_WS2812_FLOW;
        token = strtok(NULL, " "); // 参数1
        if (token != NULL)
            msg.arg1 = atoi(token);
        token = strtok(NULL, " "); // 参数2
        if (token != NULL)
            msg.arg2 = atoi(token);
    }
    else if (!strcmp(token, "last"))
    {
        msg.type = CMD_WS2812_LAST;
        token = strtok(NULL, " "); // 参数1
        if (token != NULL)
            msg.arg1 = atoi(token);
    }
    else if (!strcmp(token, "batt"))
    {
        msg.type = CMD_WS2812_BATT;
        token = strtok(NULL, " "); // 参数1
        if (token != NULL)
            msg.arg1 = atoi(token);
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

    command_publish(msg.type, msg.arg1, msg.arg2);
}

/************ Console Task ************/
static void console_task(void *param)
{
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

/************ Init ************/
void console_init()
{
    xTaskCreatePinnedToCore(
        console_task,
        "console_task",
        4096,
        NULL,
        2,
        NULL,
        0);
}
