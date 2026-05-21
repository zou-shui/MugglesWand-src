#pragma once

typedef enum
{
    /*格式：CMD_<模块>_<操作>，
    其中<操作>应与Console解析的命令一致*/

    CMD_NONE = 0,

    // 系统命令
    CMD_SYS_INFO,
    CMD_SYS_SLEEP,
    CMD_SYS_REBOOT,
    CMD_SYS_SHUTDOWN,

    // 用户命令
    CMD_USR_INFERENCE,
    CMD_USR_CHARGE,

    // 其他模块命令
    CMD_OTA_OTA,
    CMD_BLE_BLE,
    CMD_MPU6050_IMU,
    CMD_CONS_STOP, // 停止所有打印活动

    // 灯带命令
    CMD_WS2812_BREA,
    CMD_WS2812_FLOW,
    CMD_WS2812_LAST,
    CMD_WS2812_BATT,

} command_type_t;

typedef struct
{
    command_type_t type;
    int32_t arg1;
    int32_t arg2;
} command_msg_t;

void command_init();
void command_publish(command_type_t type, int32_t arg1 = 0, int32_t arg2 = 0);
QueueHandle_t command_get_queue();