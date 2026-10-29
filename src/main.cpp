#include <Arduino.h>
#include <WiFi.h>

#include "Service/DualPrint.h"
#include "Service/EventBus.h"
#include "Service/Console.h"
#include "Service/DeviceConfig.h"
#include "Service/AP.h"
#include "Service/Service.h"

#include "HAL/HAL.h"

#include "Model/gesture_inference.h"

#include "APP/APP.h"
#include "APP/GestureBinding.h"
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
| BLE keyboard task | 1
| BLE battery task  | 1
| HA MQTT task      | 1
| Power task        | 0

Core 1:
| Task              | Priority
|-------------------|----------
| WS2812 animation  | 4
| IMU sampling      | 3
| APP task          | 2
| Gesture inference | 1
| ESP-NOW heartbeat | 1
*/

void setup()
{
  // ---------------系统初始化---------------
  Serial.begin(115200);

  DualSerial.println("[System] Initializing...");

  // WiFi 驱动默认会把 AP 配置持久化到自己的 NVS，旧值可能与运行时配置冲突。
  // 关闭后由 DeviceConfig 统一管理 AP 凭据，也避免驱动反复写 flash。
  WiFi.persistent(false);

  // 系统服务初始化
  device_config_init();
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
  APP_binding_init();

  // 网络接入：根据 DeviceConfig 选择 STA 或 AP，并启动 TCP/Web 服务
  network_init();

  APP_init();

  // 启动手势推理、BLE
  EventBus::publish(EVENT_IMU_SET_MUX, 2);
  EventBus::publish(EVENT_SYS_BLE);

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
