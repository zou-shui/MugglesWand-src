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
#include <esp_heap_caps.h>
#include <esp_system.h>

// 把 esp_reset_reason_t 枚举翻译成可读字符串。
static const char *reset_reason_to_str(esp_reset_reason_t reason)
{
    switch (reason)
    {
    case ESP_RST_POWERON:
        return "Power-on reset";
    case ESP_RST_EXT:
        return "External reset (pin)";
    case ESP_RST_SW:
        return "Software reset (esp_restart)";
    case ESP_RST_PANIC:
        return "Software reset due to exception/panic";
    case ESP_RST_INT_WDT:
        return "Interrupt watchdog";
    case ESP_RST_TASK_WDT:
        return "Task watchdog";
    case ESP_RST_WDT:
        return "Other watchdog";
    case ESP_RST_DEEPSLEEP:
        return "Wake from deep sleep";
    case ESP_RST_BROWNOUT:
        return "Brownout reset";
    case ESP_RST_SDIO:
        return "Reset by SDIO";
    case ESP_RST_UNKNOWN:
    default:
        return "Unknown";
    }
}

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
    EventBus::subscribe(EVENT_SYS_WIFI, service_queue);
    EventBus::subscribe(EVENT_SYS_DEBUG, service_queue);
    EventBus::subscribe(EVENT_NET_STA_GOT_IP, service_queue);
    EventBus::subscribe(EVENT_NET_STA_DISCONNECTED, service_queue);
    EventBus::subscribe(EVENT_BTN_SHORT_PRESS, service_queue);

    SystemEvent event;

    while (1)
    {
        if (xQueueReceive(service_queue, &event, portMAX_DELAY))
        {
            switch (event.id)
            {
            case EVENT_SYS_INFO:
            {
                DualSerial.printf("%s V%s\nBuild Time: %s\nCore Temperature: %d°C\nSystem Uptime: %d seconds\nBattery: %.2fV, %.1f%%, %s, %.1f%%/h\n",
                                  PROJECT_NAME,
                                  FIRMWARE_VER,
                                  BUILD_TIME,
                                  (int)temperatureRead(),
                                  millis() / 1000,
                                  HAL::MAX17048_getVoltage(),
                                  HAL::MAX17048_getSOC(),
                                  HAL::power_getChargeStatus() ? "Charging" : "Discharging",
                                  HAL::MAX17048_getChangeRate());

                // 无线开关状态，BLE / WiFi 额外显示连接状态
                const char *ble_state =
                    !ble_is_enabled() ? "OFF"
                                      : (ble_is_connected() ? "Connected" : "Connecting..");
                const char *wifi_state =
                    !network_is_sta_enabled()
                        ? "OFF"
                        : (network_is_sta_connected() ? "Connected" : "Connecting..");
                DualSerial.printf("BLE: %s, ", ble_state);
                DualSerial.printf("AP: %s, ", ap_is_running() ? "ON" : "OFF");
                DualSerial.printf("WiFi: %s, ", wifi_state);
                DualSerial.printf("ESP-NOW: %s\n", espnow_is_running() ? "ON" : "OFF");
                /*
                // 内部堆水位，用于核对低内存自愈阈值
                DualSerial.printf("Internal heap: %uKB free, %uKB largest block\n",
                                  (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024),
                                  (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024));*/
                break;
            }

            case EVENT_SYS_REBOOT:
                DualSerial.println("Rebooting...");
                HAL::ws2812_stop();
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
            case EVENT_SYS_WIFI:
                wifi_toggle();
                break;

            case EVENT_NET_STA_GOT_IP:
                network_handle_sta_got_ip(event.param1.i32);
                break;

            case EVENT_NET_STA_DISCONNECTED:
                network_handle_sta_disconnected();
                break;

            case EVENT_BTN_SHORT_PRESS:
                // 双击硬编码为切换 AP，用于 WiFi 配置错误时从热点恢复
                if (event.param1.i32 == 2)
                {
                    ap_toggle();
                }
                break;

            case EVENT_SYS_DEBUG:
                DualSerial.printf("[System] Reset reason: %s\n", reset_reason_to_str(esp_reset_reason())); // 打印系统重启原因
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
