/*
    负责处理系统级事件，如获取系统信息、进入睡眠、重启、关机等。
    通过事件总线(EventBus)接收来自其他模块的事件，并执行相应的操作。
*/
#include <Arduino.h>
#include "Config.h"
#include "Service.h"
#include "BLE_uart.h"
#include "EventBus.h"
#include "OTA.h"
#include "HAL/HAL.h"

QueueHandle_t service_queue = NULL;

static void system_service_task(void *param)
{
    service_queue = xQueueCreate(8, sizeof(SystemEvent));
    EventBus::subscribe(EVENT_SYS_INFO, service_queue);
    EventBus::subscribe(EVENT_SYS_REBOOT, service_queue);
    EventBus::subscribe(EVENT_SYS_SHUTDOWN, service_queue);
    EventBus::subscribe(EVENT_SYS_OTA, service_queue);
    EventBus::subscribe(EVENT_SYS_BLE, service_queue);

    char buffer[256];
    SystemEvent event;

    while (1)
    {
        if (xQueueReceive(service_queue, &event, portMAX_DELAY))
        {
            switch (event.id)
            {
            case EVENT_SYS_INFO:
                memset(buffer, 0, sizeof(buffer));
                sprintf(buffer, "Version: %s\nBuild Time: %s\nCore Temperature: %d°C\nSystem Uptime: %d seconds\nBattery: %.2f V, %.1f%%, %s, %.1f%%/h\n",
                        FIRMWARE_VER,
                        BUILD_TIME,
                        (int)temperatureRead(),
                        millis() / 1000,
                        HAL::MAX17048_getVoltage(),
                        HAL::MAX17048_getSOC(),
                        HAL::MAX17048_getChargeStatus() ? "Charging" : "Discharging",
                        HAL::MAX17048_getChangeRate());
                Serial.print(buffer);
                ble_send(buffer, strlen(buffer));
                break;

            case EVENT_SYS_REBOOT:
                Serial.println("Rebooting...");
                ESP.restart();
                break;

            case EVENT_SYS_SHUTDOWN:
                Serial.println("Shutting down...");
                HAL::ws2812_stop(); // 关机前清除灯珠状态，避免下次开机时灯珠的不确定状态
                HAL::power_off();
                break;

            case EVENT_SYS_OTA:
                OTA_begin();
                break;

            case EVENT_SYS_BLE:
                ble_toggle();
                break;

            default:
                break;
            }
        }
    }
}

/************ Init ************/
bool service_init()
{
    xTaskCreatePinnedToCore(
        system_service_task,
        "system_service_task",
        4096,
        NULL,
        3, // 优先级高于 console
        NULL,
        0);
    return true;
}
