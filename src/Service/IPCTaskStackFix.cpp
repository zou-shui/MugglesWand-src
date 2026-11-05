/*
    扩大 IDF 的 ipc0 / ipc1 任务栈。

    背景：arduino-esp32 2.0.17 使用预编译的 ESP-IDF 库，其中
    CONFIG_ESP_IPC_TASK_STACK_SIZE = 1024 字节。跨核回调（典型的是
    spi_flash_op_block_func：另一核写/擦 flash 时，本核 IPC 任务要挂起调度器
    并忙等）运行在 ipc 任务上；当 tick 中断叠在回调栈帧之上时，1KB 栈可能
    被写穿，触发 FreeRTOS 的 stack canary watchpoint（Unhandled debug
    exception），表现为开机偶发重启。

    IPC 任务由 IDF 的 esp_ipc_init() 构造函数创建，没有公开 API 可以改栈大小。
    这里用链接选项 -Wl,--wrap=xTaskCreatePinnedToCore 拦截创建调用，仅在任务名
    为 ipc0/ipc1 时使用更大的栈；其他任务完全不受影响。

    注意：这是对预编译 SDK 的运行时绕过。若以后自行编译 IDF 并调大
    CONFIG_ESP_IPC_TASK_STACK_SIZE，可删除本文件及 platformio.ini 中对应的
    -Wl,--wrap=xTaskCreatePinnedToCore。
*/
#include <Arduino.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// 原值 1024；3072 给跨核回调和 tick 中断留足余量，额外占用 2 * 2KB 内部 RAM
#define IPC_TASK_STACK_SIZE_FIXED 3072

static bool is_ipc_task_name(const char *name)
{
    // 不依赖 strcmp，避免在 IDF 构造函数阶段引入额外库调用
    return name != nullptr &&
           name[0] == 'i' && name[1] == 'p' && name[2] == 'c' &&
           (name[3] == '0' || name[3] == '1') && name[4] == '\0';
}

extern "C" BaseType_t __real_xTaskCreatePinnedToCore(
    TaskFunction_t pvTaskCode,
    const char *pcName,
    const uint32_t usStackDepth,
    void *pvParameters,
    UBaseType_t uxPriority,
    TaskHandle_t *pvCreatedTask,
    BaseType_t xCoreID);

extern "C" BaseType_t __wrap_xTaskCreatePinnedToCore(
    TaskFunction_t pvTaskCode,
    const char *pcName,
    const uint32_t usStackDepth,
    void *pvParameters,
    UBaseType_t uxPriority,
    TaskHandle_t *pvCreatedTask,
    BaseType_t xCoreID)
{
    uint32_t stack_depth = usStackDepth;

    if (is_ipc_task_name(pcName) && stack_depth < IPC_TASK_STACK_SIZE_FIXED)
        stack_depth = IPC_TASK_STACK_SIZE_FIXED;

    return __real_xTaskCreatePinnedToCore(pvTaskCode, pcName, stack_depth,
                                          pvParameters, uxPriority,
                                          pvCreatedTask, xCoreID);
}
