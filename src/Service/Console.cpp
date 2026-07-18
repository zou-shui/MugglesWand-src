/*
    解析串口控制台命令输入，并将命令通过CommandBus发送给Dispatcher处理
*/
#include <Arduino.h>
#include "Console.h"
#include "DualPrint.h"
#include "EventBus.h"

#define CONSOLE_BUF_SIZE 64

static char rx_buf[CONSOLE_BUF_SIZE];
static uint8_t rx_index = 0;

/************ 显示帮助信息 ************/
static void console_print_help()
{
    DualSerial.println("========= Magic Wand Console =========");
    DualSerial.println("help          - Show command list");
    DualSerial.println("info          - Show system information");
    DualSerial.println("reboot        - Restart device");
    DualSerial.println("shutdown      - Turn off the power");
    DualSerial.println("ota           - Enter OTA mode");
    DualSerial.println("ble           - Toggle BLE service on/off");
    DualSerial.println("ap            - Toggle AP service on/off");
    DualSerial.println("inference     - Switch IMU to inference mode (push data to training buffer)");
    DualSerial.println("imu           - Switch IMU to real-time output mode (send data via AP&UART)");
    DualSerial.println("stop          - Stop IMU task");
}

/************ 串口命令解析 ************/
void console_parse(char *cmd)
{
    int32_t arg1 = 0, arg2 = 0;

    // 使用 strtok 分割字符串
    char *token = strtok(cmd, " \r\n"); // 第一个单词是命令
    if (token == NULL)
        return;

    DualSerial.print("\n> ");
    DualSerial.println(token);

    if (!strcmp(token, "debug"))
    {
        EventBus::publish(EVENT_SYS_DEBUG);
    }
    else if (!strcmp(token, "info"))
    {
        EventBus::publish(EVENT_SYS_INFO);
    }
    else if (!strcmp(token, "reboot"))
    {
        EventBus::publish(EVENT_SYS_REBOOT);
    }
    else if (!strcmp(token, "shutdown"))
    {
        EventBus::publish(EVENT_SYS_SHUTDOWN);
    }
    else if (!strcmp(token, "ota"))
    {
        EventBus::publish(EVENT_SYS_OTA);
    }
    else if (!strcmp(token, "ble"))
    {
        EventBus::publish(EVENT_SYS_BLE);
    }
    else if (!strcmp(token, "ap"))
    {
        EventBus::publish(EVENT_SYS_AP);
    }
    else if (!strcmp(token, "inference"))
    {
        EventBus::publish(EVENT_IMU_SET_MUX, 2);
    }
    else if (!strcmp(token, "imu"))
    {
        EventBus::publish(EVENT_IMU_SET_MUX, 1);
    }

    else if (!strcmp(token, "stop"))
    {
        EventBus::publish(EVENT_IMU_RESET_MUX);
    }

    else if (!strcmp(token, "help"))
    {
        console_print_help();
        return;
    }
    else
    {
        DualSerial.println("Unknown command");
        return;
    }
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
