#include <Arduino.h>
#include "esp_task_wdt.h"
#include "Service/Console.h"
#include "Service/Dispatcher.h"
#include "Service/OTA.h"
#include "Service/Sleep.h"
#include "Service/BLE.h"
#include "HAL/HAL.h"
#include "Model/gesture_inference.h"

/*
核心 0:
- Wi-Fi / BLE
- Serial console parser
- Dispatcher
- MPU6050 sampling
- Power task
- OTA (临时创建)

核心 1:
- WS2812 animation
- Gesture reference

*/

// 看门狗超时时间（秒）
#define WDT_TIMEOUT_SECONDS 1

void setup()
{
  Serial.begin(115200);

  // 监控所有核心的空闲任务
  ESP_ERROR_CHECK(esp_task_wdt_init(WDT_TIMEOUT_SECONDS, true)); // true = panic 重启
  // 将当前任务添加到看门狗监控
  ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

  console_init();
  dispatcher_init();

  HAL::button_init();
  HAL::mpu6050_init();
  HAL::ws2812_init();

  HAL::power_init();
}

void loop()
{
  // 喂狗 - 必须在看门狗超时时间内执行
  esp_task_wdt_reset();
  delay(10); // 10ms 周期，远小于看门狗超时时间
}