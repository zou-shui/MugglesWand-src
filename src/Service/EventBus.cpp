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

// 订阅接口：通常在各个任务的 setup 或初始化阶段调用
bool EventBus::subscribe(EventID eventId, QueueHandle_t queueHandle)
{
    if (xMutex == NULL || queueHandle == NULL)
        return false;

    // 加上互斥锁，防止核心 0 和核心 1 同时在初始化时写这个数组
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE)
    {
        if (subscriberCount >= MAX_SUBSCRIBERS)
        {
            xSemaphoreGive(xMutex);
            Serial.println("[EventBus] Error: Subscribers limit exceeded!");
            return false;
        }

        // 检查是否已经订阅过，防止重复订阅
        for (int i = 0; i < subscriberCount; i++)
        {
            if (subscribers[i].eventId == eventId && subscribers[i].queueHandle == queueHandle)
            {
                xSemaphoreGive(xMutex);
                return true;
            }
        }

        // 记录订阅关系
        subscribers[subscriberCount].eventId = eventId;
        subscribers[subscriberCount].queueHandle = queueHandle;
        subscriberCount++;

        xSemaphoreGive(xMutex);
        return true;
    }
    return false;
}

// 发布接口：任何任务都可以随时随地调用
bool EventBus::publish(EventID eventId, int32_t param1, int32_t param2)
{
    if (xMutex == NULL)
        return false;

    // 1. 在内部组装成结构体，方便复制进 FreeRTOS 队列
    SystemEvent event = {
        .id = eventId,
        .param1 = param1,
        .param2 = param2};

    bool delivered = false;

    // 2. 依然是原本的加锁遍历逻辑
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE)
    {
        for (int i = 0; i < subscriberCount; i++)
        {
            if (subscribers[i].eventId == event.id)
            {
                // 将组装好的结构体一整块拷贝进目标队列
                if (xQueueSend(subscribers[i].queueHandle, &event, 0) == pdPASS)
                {
                    delivered = true;
                }
                else
                {
                    Serial.printf("[EventBus] Warning: Queue full, missed event %d\n", event.id);
                }
            }
        }
        xSemaphoreGive(xMutex);
    }
    return delivered;
}

bool EventBus::publishFromISR(EventID eventId, int32_t param1, int32_t param2)
{
    if (xMutex == NULL)
        return false;

    // 1. 组装事件结构体
    SystemEvent event = {
        .id = eventId,
        .param1 = param1,
        .param2 = param2};

    bool delivered = false;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // 2. 在中断中尝试获取互斥锁（注意：中断中严禁阻塞，TickType_t 必须为 0）
    // 注意：某些标准 FreeRTOS 平台不允许在中断中获取 Mutex，但 ESP32 (Arduino) 的 Mutex 允许在中断中以 0 等待时间尝试获取。
    if (xSemaphoreTakeFromISR(xMutex, &xHigherPriorityTaskWoken) == pdTRUE)
    {
        for (int i = 0; i < subscriberCount; i++)
        {
            if (subscribers[i].eventId == event.id)
            {
                // 使用从中断发送队列的专用 API
                if (xQueueSendFromISR(subscribers[i].queueHandle, &event, &xHigherPriorityTaskWoken) == pdPASS)
                {
                    delivered = true;
                }
                // 注意：中断中严禁使用 Serial.printf 打印，否则会导致内核崩溃！这里去掉了 Warning 打印。
            }
        }

        // 释放互斥锁
        xSemaphoreGiveFromISR(xMutex, &xHigherPriorityTaskWoken);
    }

    // 3. 如果有高优先级任务被唤醒，触发上下文切换（确保实时性）
    if (xHigherPriorityTaskWoken == pdTRUE)
    {
        portYIELD_FROM_ISR();
    }

    return delivered;
}