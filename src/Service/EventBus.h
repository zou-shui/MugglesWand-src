#pragma once
#include <Arduino.h>

// ============================================================
// 事件总线 — 系统全局事件的发布/订阅机制
// ============================================================
//
// 设计要点：
//   1. 所有事件 ID 统一在此枚举中定义
//   2. publish() 采用"快照-发送"两阶段模式，持锁期间仅复制
//      订阅者队列句柄到栈上数组，释放锁后再执行 xQueueSend，
//      避免在临界区内触发任务调度造成优先级反转
//   3. subscribe() 仅限初始化阶段调用，运行时不修改订阅关系
//   4. SystemEvent 使用两个 int32_t 参数传递事件数据，具体语义
//      由各事件约定（见下方各事件注释）
//
// ============================================================

// 1. 定义系统中所有的事件 ID
enum EventID
{
    EVENT_NONE = 0,

    // --- 系统事件（由 Service 模块订阅和处理）---
    EVENT_SYS_DEBUG,    // 无参数
    EVENT_SYS_INFO,     // 无参数
    EVENT_SYS_REBOOT,   // 无参数
    EVENT_SYS_SHUTDOWN, // 无参数
    EVENT_SYS_OTA,      // 无参数
    EVENT_SYS_BLE,      // 无参数
    EVENT_SYS_AP,       // 无参数

    // --- IMU 事件 ---
    EVENT_IMU_SET_MUX,        // 设定IMU数据流向, param1: 数据流向 (1=实时输出, 2=训练缓冲区, -1=已停止)
    EVENT_IMU_RESET_MUX,      // 关闭IMU输出, 无参数
    EVENT_IMU_STATUS_CHANGED, // param1: 当前状态机状态, param2: 当前数据流向

    // --- 手势推理事件 ---
    EVENT_GESTURE_DETECTED, // 手势推理结果事件 param1: 手势 ID
};

// 2. 定义事件结构体（传递的数据）
struct SystemEvent
{
    EventID id;
    int32_t param1; // 语义由各 EventID 约定
    int32_t param2; // 语义由各 EventID 约定
};

// 最大订阅数量限制（当前系统实际订阅数约为 6，保留充足余量）
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
    static SemaphoreHandle_t xMutex; // 保护订阅表的互斥锁

public:
    /// @brief 初始化事件总线（必须在任何 subscribe/publish 之前调用）
    static void init();

    /// @brief 订阅事件（仅限初始化阶段调用）
    /// @param eventId    要订阅的事件 ID
    /// @param queueHandle 接收事件的 FreeRTOS 队列句柄
    /// @return true 订阅成功或已存在相同订阅，false 容量已满
    static bool subscribe(EventID eventId, QueueHandle_t queueHandle);

    /// @brief 发布事件（可在任意任务中调用，包括高频路径）
    /// @param eventId 事件 ID
    /// @param param1  事件参数1（语义由事件约定）
    /// @param param2  事件参数2（语义由事件约定）
    /// @return true 至少成功投递到一个订阅者，false 无订阅者或失败
    static bool publish(EventID eventId, int32_t param1 = 0, int32_t param2 = 0);
};