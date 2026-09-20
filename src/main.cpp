#include <Arduino.h>

#include "Service/DualPrint.h"
#include "Service/EventBus.h"
#include "Service/Console.h"
#include "Service/Service.h"

#include "HAL/HAL.h"

#include "Model/gesture_inference.h"

#include "APP/APP.h"
#include "HAL/WS2812_Animation/AnimBootup.hpp"

/*
Core 0:
| Task              | Priority
|-------------------|----------
| Wi-Fi/BLE stack   | highest(default)
| OTA               | 4
| Service           | 3
| Console parser    | 2
| Button handling   | 1
| BLE battery task  | 0
| Power task        | 0

Core 1:
| Task              | Priority
|-------------------|----------
| WS2812 animation  | 4
| IMU sampling      | 3
| APP task          | 2
| Gesture reference | 1
| ESP-NOW heartbeat | 1
*/

void setup()
{
  // ---------------系统初始化---------------
  Serial.begin(115200);

  DualSerial.println("[System] Initializing...");

  // 系统服务初始化
  EventBus::init();
  console_init();
  service_init();

  // 模型载入
  inference_init();
  inference_start();

  // 硬件初始化
  HAL::power_init();
  HAL::ws2812_init();
  // 开机/唤醒动画：中心向两端扩散后整体熄灭，播完自销毁
  HAL::ws2812_start_fx(new AnimBootup());
  bool imu_ok = HAL::ICM42670P_init();
  bool batt_ok = HAL::MAX17048_init();
  HAL::button_init();

  // 用户APP初始化
  APP_init();

  // 启动手势推理、BLE、WiFi热点
  EventBus::publish(EVENT_IMU_SET_MUX, 2);
  EventBus::publish(EVENT_SYS_BLE);
  EventBus::publish(EVENT_SYS_AP);

  // 检查IMU和电池状态，如果初始化失败则重启
  if (!imu_ok || !batt_ok)
  {
    for (int i = 5; i > 0; i--)
    {
      if (!imu_ok)
        DualSerial.printf("[System] FAIL: ICM42670P (IMU) init failed, Restart in %ds\n", i);

      if (!batt_ok)
        DualSerial.printf("[System] FAIL: MAX17048 (battery gauge) init failed, Restart in %ds\n", i);

      delay(1000);
    }
    DualSerial.println("[System] Restarting...");
    ESP.restart();
  }

  DualSerial.println("[System] Initialization complete");
}

void loop()
{
}