/*
    实现命令总线，供不同的模块发布命令，并由Dispatcher.cpp统一处理
*/
#include <Arduino.h>
#include "CommandBus.h"

#define CMD_QUEUE_SIZE 8 // 队列长度

static QueueHandle_t cmd_queue = NULL;

// 初始化队列
void command_init()
{
    if (cmd_queue == NULL)
    {
        cmd_queue = xQueueCreate(CMD_QUEUE_SIZE, sizeof(command_msg_t));
    }
}

// 发布命令到队列，供Dispatcher.cpp消费
void command_publish(command_type_t type, int32_t arg1, int32_t arg2)
{
    if (cmd_queue == NULL)
    {
        Serial.println("[CommandBus] cmd_queue is not initialized");
        return;
    }

    command_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = type;
    msg.arg1 = arg1;
    msg.arg2 = arg2;

    if (xQueueSend(cmd_queue, &msg, portMAX_DELAY) != pdTRUE)
    {
        Serial.println("[CommandBus] xQueueSend failed");
    }
}

// 获取命令队列句柄，供Dispatcher.cpp使用
QueueHandle_t command_get_queue()
{
    return cmd_queue;
}