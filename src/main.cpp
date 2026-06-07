#include <Arduino.h>

#include "Service/EventBus.h"
#include "Service/Console.h"
#include "Service/Service.h"
#include "Service/BLE.h"

#include "HAL/HAL.h"

#include "Model/gesture_inference.h"

#include "APP/APP.h"

/*
Core 0:
| Task              | Priority
|-------------------|----------
| Wi-Fi/BLE stack   | highest
| OTA               | 4
| Service           | 3
| Console parser    | 2
| Button handling   | 1

Core 1:
| Task              | Priority
|-------------------|----------
| WS2812 animation  | 4
| IMU sampling      | 3
| APP task          | 2
| Gesture reference | 1
*/

void setup()
{
  bool ok = true;
  Serial.begin(115200);
  Serial.println("[System] Initializing...");

  // system services init
  EventBus::init();
  console_init();
  service_init();
  ble_init("MagicWand");

  inference_init();
  inference_start();

  HAL::ws2812_init();
  ok &= HAL::ICM42670P_init();
  ok &= HAL::MAX17048_init();
  HAL::button_init();

  APP_init();

  EventBus::publish(EVENT_IMU_SET_MUX, 2);
  // EventBus::publish(EVENT_SYS_BLE);

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