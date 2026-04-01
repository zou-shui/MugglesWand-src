/*
    处理来自Console或其他模块的命令，并执行相应的操作
*/
#include <Arduino.h>
#include "dispatcher.h"
#include "Console.h"
#include "BLE.h"

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

    while (1)
    {
        if (xQueueReceive(cmd_queue, &msg, portMAX_DELAY))
        {
            switch (msg.type)
            {
            case CMD_SYS_INFO:
                Serial.printf("Version: ");
                Serial.println(FIRMWARE_VER);
                Serial.printf("Build Time: ");
                Serial.println(BUILD_TIME);
                Serial.printf("Core Temperature: %d°C\n", (int)temperatureRead());
                Serial.printf("System Uptime: %d seconds\n", millis() / 1000);
                Serial.printf(
                    "Battery: %d%%, %.2f V, %s\n",
                    HAL::power_get_battery_percent(),
                    HAL::power_get_battery_voltage(),
                    HAL::power_is_charging() ? "CHARGE" : "DISCHARGE");
                break;
            case CMD_SYS_SLEEP:
                inference_stop();
                HAL::mpu6050_stop();
                HAL::mpu6050_motion_interrupt_enable(4, 20);
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
                HAL::power_stop();
                break;

            case CMD_USR_INFERENCE:
                HAL::mpu6050_start();
                inference_start();
                HAL::ws2812_trigger_breathe(0, 0);
                break;
            case CMD_USR_CHARGE:
                inference_stop();
                HAL::mpu6050_stop();
                HAL::ws2812_trigger_charge(HAL::power_get_battery_percent());
                break;

            case CMD_OTA_OTA:
                inference_stop();
                HAL::mpu6050_stop();
                OTA_begin();
                break;
            case CMD_BLE_BLE:
                ble_toggle();
                break;
            case CMD_MPU6050_IMU:
                HAL::mpu6050_start();
                inference_start();
                break;
            case CMD_CONS_STOP:
                inference_stop();
                HAL::mpu6050_stop();
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
void dispatcher_init()
{
    cmd_queue = console_get_queue();

    xTaskCreatePinnedToCore(
        dispatcher_task,
        "dispatcher_task",
        4096,
        NULL,
        3, // 优先级高于 console
        NULL,
        0);
}
