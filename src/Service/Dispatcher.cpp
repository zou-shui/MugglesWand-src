/*
    处理来自CommandBus的命令，并执行相应的操作
*/
#include <Arduino.h>
#include "dispatcher.h"
#include "CommandBus.h"
#include "BLE_uart.h"

#include "Config.h"
#include "OTA.h"
#include "Sleep.h"
#include "HAL/HAL.h"
#include "Model/gesture_inference.h"

static QueueHandle_t cmd_queue;

/************ Dispatcher Task ************/
static void dispatcher_task(void *param)
{
    command_msg_t msg;
    char buffer[256];

    while (1)
    {
        if (xQueueReceive(cmd_queue, &msg, portMAX_DELAY))
        {
            switch (msg.type)
            {
            case CMD_SYS_INFO:
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
            case CMD_SYS_SLEEP:
                inference_stop();
                HAL::ICM42670P_stop();
                HAL::ICM42670_WakeOnMotion();
                HAL::ws2812_stop(); // 休眠前清除灯珠状态，避免下次启动时灯珠的不确定状态
                sleep_enter();
                break;
            case CMD_SYS_REBOOT:
                Serial.println("Rebooting...");
                ESP.restart();
                break;
            case CMD_SYS_SHUTDOWN:
                Serial.println("Shutting down...");
                HAL::ws2812_stop(); // 关机前清除灯珠状态，避免下次开机时灯珠的不确定状态
                HAL::power_off();
                break;

            case CMD_USR_INFERENCE:
                HAL::ICM42670P_start();
                inference_start();
                HAL::ws2812_trigger_breathe(0, 0);
                break;
            case CMD_USR_CHARGE:
                inference_stop();
                HAL::ICM42670P_stop();
                HAL::ws2812_trigger_charge(HAL::MAX17048_getSOC());
                break;

            case CMD_OTA_OTA:
                inference_stop();
                HAL::ICM42670P_stop();
                OTA_begin();
                break;
            case CMD_BLE_BLE:
                ble_toggle();
                break;
            case CMD_MPU6050_IMU:
                HAL::ICM42670P_start();
                inference_start();
                break;
            case CMD_CONS_STOP:
                inference_stop();
                HAL::ICM42670P_stop();
                break;

            case CMD_WS2812_BREA:
                HAL::ws2812_trigger_breathe(msg.arg1, msg.arg2);
                break;
            case CMD_WS2812_FLOW:
                HAL::ws2812_trigger_flow(0xFFFFFF, msg.arg1, msg.arg2);
                break;
            case CMD_WS2812_LAST:
                HAL::ws2812_toggle_last_led(msg.arg1);
                break;
            case CMD_WS2812_BATT:
                HAL::ws2812_trigger_charge(msg.arg1);

            default:
                break;
            }
        }
    }
}

/************ Init ************/
bool dispatcher_init()
{
    cmd_queue = command_get_queue();
    if (cmd_queue == NULL)
    {
        Serial.println("[Dispatcher] Failed to get command queue.");
        return false;
    }

    xTaskCreatePinnedToCore(
        dispatcher_task,
        "dispatcher_task",
        4096,
        NULL,
        3, // 优先级高于 console
        NULL,
        0);
    return true;
}
