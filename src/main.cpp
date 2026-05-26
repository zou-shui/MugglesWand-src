#include <Arduino.h>
#include "Service/CommandBus.h"
#include "Service/Console.h"
#include "Service/Dispatcher.h"
#include "Service/OTA.h"
#include "Service/Sleep.h"
#include "Service/BLE_uart.h"
#include "HAL/HAL.h"
#include "Model/gesture_inference.h"

/*
核心 0:
- Wi-Fi / BLE
- Serial console parser
- Dispatcher
- MPU6050 sampling
- OTA (临时创建)
- Power task

核心 1:
- WS2812 animation
- Gesture reference
*/

void setup()
{
  bool ok = true;
  Serial.begin(115200);
  Serial.println("[System] Initializing...");

  command_init();
  console_init();
  dispatcher_init();
  ble_init("MagicWand");

  inference_init();

  HAL::ws2812_init();
  ok &= HAL::ICM42670P_init();
  ok &= HAL::MAX17048_init();
  HAL::button_init();

  HAL::ICM42670P_start(true);
  inference_start();

  if (!ok)
  {
    Serial.println("[System] Initialize failed, restart in 3 seconds");
    delay(1000);
    Serial.println("[System] Restart in 2s");
    delay(1000);
    Serial.println("[System] Restart in 1s");
    delay(1000);
    Serial.println("[System] Restarting...");
    ESP.restart();
  }
  Serial.println("[System] Initialization complete");
}

void loop()
{
}