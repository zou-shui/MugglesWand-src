#include "APP_EspNow.h"
#include "HAL/HAL.h"
#include "HAL/WS2812_Animation/AnimBlink.hpp"
#include "HAL/WS2812_Animation/AnimFlow.hpp"
#include "Service/AP.h"
#include "Service/DualPrint.h"
#include "Service/EventBus.h"

#include <ctype.h>
#include <stdlib.h>

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

bool APP_espnow_validate(const char *parameter)
{
    if (parameter == nullptr)
        return false;

    while (isspace((unsigned char)*parameter))
        ++parameter;
    if (*parameter == '\0')
        return false;

    char *end = nullptr;
    unsigned long value = strtoul(parameter, &end, 0);
    if (end == parameter || value > 0xFF)
        return false;

    while (isspace((unsigned char)*end))
        ++end;
    return *end == '\0';
}

bool APP_espnow_execute(const char *parameter)
{
    if (!APP_espnow_validate(parameter))
    {
        DualSerial.println("[ESP-NOW] Invalid signal parameter");
        return false;
    }

    while (isspace((unsigned char)*parameter))
        ++parameter;

    uint8_t signal = (uint8_t)strtoul(parameter, nullptr, 0);

    // 不同信号的反馈灯效由本 APP 自己决定
    switch (signal)
    {
    case MSG_SIGNAL_1: // 0x02：白色光流
        HAL::ws2812_start_fx(new AnimFlow(CRGB::White));
        break;
    case MSG_SIGNAL_2: // 0x03：白色闪烁
        HAL::ws2812_start_fx(new AnimBlink(CRGB::White, 200));
        break;
    default:
        HAL::ws2812_start_fx(new AnimFlow(CRGB::White));
        break;
    }

    APP_espnow_tx_signal(signal);

    return true;
}
