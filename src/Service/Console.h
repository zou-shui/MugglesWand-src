#pragma once
#include <Arduino.h>

/******** 控制台事件 ********/
typedef enum
{
    /*格式：CMD_<模块>_<操作>，
    其中<操作>应与Console解析的命令一致*/

    CMD_NONE = 0,

    CMD_SYS_INFO,
    CMD_SYS_REBOOT,
    CMD_SYS_SLEEP,

    CMD_OTA_OTA,

    CMD_WS2812_FLOW,

} command_type_t;

typedef struct
{
    command_type_t type;

    int32_t arg1;
    int32_t arg2;

} command_msg_t;

/******** 接口 ********/
void console_init();

/******** 获取队列句柄 ********/
QueueHandle_t console_get_queue();
