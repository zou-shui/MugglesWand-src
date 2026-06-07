#pragma once
#include <Arduino.h>

// 1. 定义系统中所有的事件 ID
enum EventID
{
    EVENT_NONE = 0,

    // 系统相关，由Service模块订阅和处理
    EVENT_SYS_INFO,
    EVENT_SYS_REBOOT,
    EVENT_SYS_SHUTDOWN,
    EVENT_SYS_OTA,
    EVENT_SYS_BLE,

    // IMU相关事件，由HAL_IMU模块订阅和处理
    EVENT_IMU_SET_MUX,   // IMU数据流向切换，该事件带一个参数(具体流向)
    EVENT_IMU_RESET_MUX, // IMU停止输出

    EVENT_GESTURE_DETECTED,


};

// 2. 定义事件结构体（传递的数据）
struct SystemEvent
{
    EventID id;
    int32_t param1;
    int32_t param2;
};

// 最大订阅数量限制
#define MAX_SUBSCRIBERS 50

class EventBus
{
private:
    struct Subscription
    {
        EventID eventId;
        QueueHandle_t queueHandle;
    };

    static Subscription subscribers[MAX_SUBSCRIBERS];
    static int subscriberCount;
    static SemaphoreHandle_t xMutex; // 保证多任务下操作订阅表的线程安全

public:
    static void init();
    static bool subscribe(EventID eventId, QueueHandle_t queueHandle);
    static bool publish(EventID eventId, int32_t param1 = 0, int32_t param2 = 0);
};