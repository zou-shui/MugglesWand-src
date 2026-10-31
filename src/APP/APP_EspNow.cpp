#include "APP_EspNow.h"
#include "GestureBinding.h"
#include "HAL/HAL.h"
#include "HAL/WS2812_Animation/AnimBlink.hpp"
#include "HAL/WS2812_Animation/AnimFlow.hpp"
#include "Service/AP.h"
#include "Service/DualPrint.h"
#include "Service/EventBus.h"

#include <ctype.h>
#include <stdlib.h>

static TaskHandle_t s_heartbeat_task = nullptr;

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

// 心跳跟随 ESP-NOW 服务：无绑定被跳过启动时，手动开启也要补上心跳
void APP_espnow_ensure_heartbeat(void)
{
    if (s_heartbeat_task != nullptr)
        return;

    xTaskCreatePinnedToCore(
        heartbeatTask,
        "hb_task",
        2048,
        NULL,
        1,
        &s_heartbeat_task,
        1);
}

void APP_espnow_init()
{
    if (!APP_binding_uses_app("espnow"))
    {
        return;
    }

    DualSerial.println("[ESP-NOW] starting service...");
    APP_espnow_ensure_heartbeat();
    EventBus::publish(EVENT_SYS_ESPNOW);
}

// 运行期 map bind/clear/reset 后调用：只补启，不自动关闭
void APP_espnow_sync(void)
{
    if (!APP_binding_uses_app("espnow") || espnow_is_running())
        return;

    DualSerial.println("[ESP-NOW] Binding now requires it, starting service");
    APP_espnow_ensure_heartbeat();
    EventBus::publish(EVENT_SYS_ESPNOW);
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
