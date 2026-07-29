/*
    负责处理系统级事件，如获取系统信息、进入睡眠、重启、关机等。
    通过事件总线(EventBus)接收来自其他模块的事件，并执行相应的操作。
*/
#include <Arduino.h>
#include "Config.h"
#include "Service.h"
#include "BLE.h"
#include "EventBus.h"
#include "OTA.h"
#include "AP.h"
#include "Sleep.h"
#include "HAL/HAL.h"

QueueHandle_t service_queue = NULL;

static void system_service_task(void *param)
{
    service_queue = xQueueCreate(8, sizeof(SystemEvent));
    EventBus::subscribe(EVENT_SYS_INFO, service_queue);
    EventBus::subscribe(EVENT_SYS_REBOOT, service_queue);
    EventBus::subscribe(EVENT_SYS_SLEEP, service_queue);
    EventBus::subscribe(EVENT_SYS_SHUTDOWN, service_queue);
    EventBus::subscribe(EVENT_SYS_OTA, service_queue);
    EventBus::subscribe(EVENT_SYS_BLE, service_queue);
    EventBus::subscribe(EVENT_SYS_AP, service_queue);
    EventBus::subscribe(EVENT_SYS_ESPNOW, service_queue);

    SystemEvent event;

    while (1)
    {
        if (xQueueReceive(service_queue, &event, portMAX_DELAY))
        {
            switch (event.id)
            {
            case EVENT_SYS_INFO:
                DualSerial.printf("Version: %s\nBuild Time: %s\nCore Temperature: %d°C\nSystem Uptime: %d seconds\nBattery: %.2f V, %.1f%%, %s, %.1f%%/h\n",
                                  FIRMWARE_VER,
                                  BUILD_TIME,
                                  (int)temperatureRead(),
                                  millis() / 1000,
                                  HAL::MAX17048_getVoltage(),
                                  HAL::MAX17048_getSOC(),
                                  HAL::power_getChargeStatus() ? "Charging" : "Discharging",
                                  HAL::MAX17048_getChangeRate());
                break;

            case EVENT_SYS_REBOOT:
                DualSerial.println("Rebooting...");
                ESP.restart();
                break;
            case EVENT_SYS_SLEEP:
                EventBus::publish(EVENT_IMU_RESET_MUX);
                EventBus::publish(EVENT_IMU_SET_WOM);
                HAL::ws2812_stop();
                sleep_enter();
                break;

            case EVENT_SYS_SHUTDOWN:
                DualSerial.println("Shutting down...");
                HAL::power_off();
                break;

            case EVENT_SYS_OTA:
                OTA_begin();
                break;

            case EVENT_SYS_BLE:
                ble_toggle();
                break;
            case EVENT_SYS_AP:
                ap_toggle();
                break;

            case EVENT_SYS_ESPNOW:
                espnow_toggle();
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
