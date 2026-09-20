#include "APP_EspNow.h"
#include "Service/AP.h"
#include "Service/EventBus.h"

// 高频心跳任务：每 30ms 发射一次，方便接收端瞬间捕捉
static void heartbeatTask(void *pvParameters)
{
    uint8_t heartbeatData = MSG_HEARTBEAT;
    while (true)
    {
        espnow_send_data(&heartbeatData, sizeof(heartbeatData));
        vTaskDelay(pdMS_TO_TICKS(30)); // 广播间隔：30ms
    }
}

void APP_espnow_init()
{
    EventBus::publish(EVENT_SYS_ESPNOW);
    xTaskCreatePinnedToCore(
        heartbeatTask,
        "hb_task",
        2048,
        NULL,
        1,
        NULL,
        1);
}

void APP_espnow_tx_signal(uint8_t signalType)
{
    espnow_send_data(&signalType, sizeof(signalType));
}