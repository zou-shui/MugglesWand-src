/*
    解析串口/网络终端输入的控制台命令。
    串口输入由 console_task 轮询收集，网络回调通过 console_submit() 投递，
    统一由 console_task 串行解析，再通过 EventBus 或 APP 映射模块执行。
*/
#include <Arduino.h>
#include "Console.h"
#include "DualPrint.h"
#include "EventBus.h"
#include "APP/GestureBinding.h"
#include "APP/APP_POV.h" // pov 命令的图案清单由 APP 层输出

#include <ctype.h>

#define CONSOLE_BUF_SIZE 256
#define CONSOLE_QUEUE_DEPTH 4

struct ConsoleCommand
{
    char text[CONSOLE_BUF_SIZE];
};

static char rx_buf[CONSOLE_BUF_SIZE];
static uint8_t rx_index = 0;
static QueueHandle_t command_queue = nullptr;

/************ 显示帮助信息 ************/
static void console_print_help()
{
    DualSerial.println("================= Wand Console =================");
    DualSerial.println("help           - Show command list");
    DualSerial.println("debug          - Enable debug mode(disable auto sleep)");
    DualSerial.println("info           - Show system information");
    DualSerial.println("reboot         - Restart device");
    DualSerial.println("sleep          - Enter deep sleep mode");
    DualSerial.println("shutdown       - Turn off the power");
    DualSerial.println("ota            - Enter OTA mode");
    DualSerial.println("ap             - Toggle AP service on/off");
    DualSerial.println("espnow         - Toggle ESP-NOW service on/off");
    DualSerial.println("ble            - Toggle BLE service on/off");
    DualSerial.println("inference      - Switch IMU to inference mode (push data to gesture buffer)");
    DualSerial.println("imu            - Switch IMU to real-time output mode (send data via AP&UART)");
    DualSerial.println("stop           - Stop IMU task and cut off data output");
    DualSerial.println("gesture <id>   - Simulate gesture detection event with specified ID");
    DualSerial.println("pov [id] [rev] - Set POV pattern params");
    DualSerial.println("map            - Configure gesture mappings (type 'map' for usage)");
}

static bool match_command_name(const char *command, const char *name,
                               const char **arguments)
{
    while (*command != '\0' && isspace((unsigned char)*command))
        ++command;

    const char *start = command;
    while (*command != '\0' && !isspace((unsigned char)*command))
        ++command;

    size_t length = (size_t)(command - start);
    if (length != strlen(name) || strncmp(start, name, length) != 0)
        return false;

    *arguments = command;
    return true;
}

/************ 命令回显 ************/
static void console_print_echo(const char *cmd)
{
    while (cmd != nullptr && isspace((unsigned char)*cmd))
        ++cmd;

    if (cmd == nullptr || *cmd == '\0')
        return;

    const char *end = cmd + strlen(cmd);
    while (end > cmd && isspace((unsigned char)end[-1]))
        --end;

    DualSerial.print("\n> ");
    DualSerial.write((const uint8_t *)cmd, (size_t)(end - cmd));
    DualSerial.println();
}

/************ 命令解析（仅由 console_task 调用） ************/
static void console_parse(char *cmd)
{
    if (cmd == nullptr)
        return;

    console_print_echo(cmd);

    int32_t arg1 = 0, arg2 = 0;

    const char *map_arguments = nullptr;
    if (match_command_name(cmd, "map", &map_arguments))
    {
        APP_binding_console(map_arguments);
        return;
    }

    // 使用 strtok 分割字符串
    char *token = strtok(cmd, " \r\n"); // 第一个单词是命令
    if (token == NULL)
        return;

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
    else if (!strcmp(token, "sleep"))
    {
        EventBus::publish(EVENT_SYS_SLEEP);
    }
    else if (!strcmp(token, "shutdown"))
    {
        EventBus::publish(EVENT_SYS_SHUTDOWN);
    }

    else if (!strcmp(token, "ota"))
    {
        EventBus::publish(EVENT_SYS_OTA);
    }
    else if (!strcmp(token, "ap"))
    {
        EventBus::publish(EVENT_SYS_AP);
    }
    else if (!strcmp(token, "espnow"))
    {
        EventBus::publish(EVENT_SYS_ESPNOW);
    }
    else if (!strcmp(token, "ble"))
    {
        EventBus::publish(EVENT_SYS_BLE);
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

    else if (!strcmp(token, "gesture"))
    {
        token = strtok(NULL, " \r\n"); // 第二个单词是参数1
        if (token != NULL)
        {
            arg1 = atoi(token);
            EventBus::publish(EVENT_GESTURE_DETECTED, arg1);
            DualSerial.printf("Gesture %d triggered\n", arg1);
        }
        else
        {
            DualSerial.println("Error: gesture command requires an ID argument");
        }
    }

    else if (!strcmp(token, "pov"))
    {
        // 无参数: 由 APP 层打印图案清单与用法
        token = strtok(NULL, " \r\n");
        if (token == NULL)
        {
            APP_POV_list();
            DualSerial.println("Usage: pov <index> [reverse 0/1]");
            DualSerial.println("Note: this command sets params for next trigger");
            return;
        }

        // 索引越界检查与钳制由 APP 层 (APP_POV_set_params) 完成
        arg1 = atoi(token);
        token = strtok(NULL, " \r\n");
        arg2 = (token != NULL) ? atoi(token) : 0; // reverse, 默认 0

        EventBus::publish(EVENT_POV_SET, arg1, arg2);
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

/************ 外部命令提交 ************/
bool console_submit(const char *command, size_t length)
{
    if (command_queue == nullptr || command == nullptr || length == 0)
        return false;

    // 网络消息可能包含行尾，去除后再检查固件缓冲区容量
    while (length > 0 &&
           (command[length - 1] == '\0' || command[length - 1] == '\r' ||
            command[length - 1] == '\n'))
    {
        --length;
    }

    if (length == 0 || length >= CONSOLE_BUF_SIZE)
        return false;

    ConsoleCommand queued = {};
    memcpy(queued.text, command, length);
    queued.text[length] = '\0';

    // 不阻塞 AsyncTCP 回调；队列满时由调用方反馈错误
    return xQueueSend(command_queue, &queued, 0) == pdPASS;
}

/************ Console Task ************/
static void console_task(void *param)
{
    bool rx_overflow = false;

    while (1)
    {
        ConsoleCommand queued;
        while (command_queue != nullptr &&
               xQueueReceive(command_queue, &queued, 0) == pdPASS)
        {
            console_parse(queued.text);
        }

        while (Serial.available())
        {
            char c = Serial.read();

            // Enter
            if (c == '\n' || c == '\r')
            {
                rx_buf[rx_index] = 0;

                if (rx_overflow)
                {
                    DualSerial.printf("Error: command exceeds %u bytes\n",
                                      CONSOLE_BUF_SIZE - 1);
                }
                else if (rx_index > 0)
                {
                    console_parse(rx_buf);
                }

                rx_index = 0;
                rx_overflow = false;
            }
            else
            {
                if (rx_index < CONSOLE_BUF_SIZE - 1)
                {
                    rx_buf[rx_index++] = c;
                }
                else
                {
                    rx_overflow = true;
                }
            }
        }

        vTaskDelay(10);
    }
}

/************ Init ************/
void console_init()
{
    command_queue = xQueueCreate(CONSOLE_QUEUE_DEPTH, sizeof(ConsoleCommand));
    if (command_queue == nullptr)
    {
        DualSerial.println("[Console] Failed to create command queue");
    }

    xTaskCreatePinnedToCore(
        console_task,
        "console_task",
        6144,
        NULL,
        2,
        NULL,
        0);
}
