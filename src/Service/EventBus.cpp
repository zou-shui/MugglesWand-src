#include "EventBus.h"

// 静态变量初始化
EventBus::Subscription EventBus::subscribers[MAX_SUBSCRIBERS];
int EventBus::subscriberCount = 0;
SemaphoreHandle_t EventBus::xMutex = NULL;

void EventBus::init()
{
    if (xMutex == NULL)
    {
        xMutex = xSemaphoreCreateMutex();
        subscriberCount = 0;
    }
}

// 订阅接口：仅在初始化阶段调用，因此使用 portMAX_DELAY 等待锁是安全的
bool EventBus::subscribe(EventID eventId, QueueHandle_t queueHandle)
{
    if (xMutex == NULL || queueHandle == NULL)
        return false;

    bool result = false;

    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE)
    {
        // 检查容量上限（在锁内计算，在锁外打印日志）
        bool limit_exceeded = (subscriberCount >= MAX_SUBSCRIBERS);

        if (!limit_exceeded)
        {
            // 检查是否已经订阅过，防止重复订阅
            bool already_subscribed = false;
            for (int i = 0; i < subscriberCount; i++)
            {
                if (subscribers[i].eventId == eventId && subscribers[i].queueHandle == queueHandle)
                {
                    already_subscribed = true;
                    break;
                }
            }

            if (!already_subscribed)
            {
                // 记录新的订阅关系
                subscribers[subscriberCount].eventId = eventId;
                subscribers[subscriberCount].queueHandle = queueHandle;
                subscriberCount++;
                result = true;
            }
            else
            {
                result = true; // 已存在相同订阅，视为成功
            }
        }
        // else: limit_exceeded，result 保持 false

        xSemaphoreGive(xMutex);
    }

    // 日志在锁外执行
    if (!result)
    {
        Serial.println("[EventBus] Error: Subscribers limit exceeded!");
    }
    return result;
}

// 发布接口：任何任务都可以随时随地调用
// 设计说明：采用"快照-发送"两阶段模式——
//   阶段1（持锁）：将匹配的订阅者队列句柄复制到栈上快照数组
//   阶段2（无锁）：遍历快照执行 xQueueSend，不阻塞其他任务操作订阅表
// 这样避免了在持锁期间调用 xQueueSend（可能触发任务调度导致优先级反转）。
bool EventBus::publish(EventID eventId, EventParam param1, EventParam param2)
{
    if (xMutex == NULL)
        return false;

    // 组装事件结构体
    SystemEvent event = {eventId, param1, param2};

    // === 阶段1：持锁快照匹配的订阅者 ===
    QueueHandle_t targets[MAX_SUBSCRIBERS];
    int targetCount = 0;

    if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        for (int i = 0; i < subscriberCount && targetCount < MAX_SUBSCRIBERS; i++)
        {
            if (subscribers[i].eventId == event.id)
            {
                targets[targetCount++] = subscribers[i].queueHandle;
            }
        }
        xSemaphoreGive(xMutex);
    }
    else
    {
        // 无法在超时时间内获取锁（系统可能处于极端负载），放弃本次发布
        return false;
    }

    // === 阶段2：无锁发送 ===
    bool delivered = false;
    for (int i = 0; i < targetCount; i++)
    {
        if (xQueueSend(targets[i], &event, 0) == pdPASS)
        {
            delivered = true;
        }
        else
        {
            // 日志在锁外执行，不影响其他任务的订阅/发布操作
            Serial.printf("[EventBus] Warning: Queue full, missed event %d\n", event.id);
        }
    }
    return delivered;
}
