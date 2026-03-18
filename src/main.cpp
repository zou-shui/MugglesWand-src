#include <Arduino.h>
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

void setup()
{
  Serial.begin(115200);

  console_init();
  dispatcher_init();

  HAL::button_init();
  HAL::mpu6050_init();
  HAL::ws2812_init();

  HAL::power_init();
}

void loop()
{
}
