#include <Arduino.h>
#include "Service/Console.h"
#include "Service/Dispatcher.h"
#include "Service/OTA.h"
#include "Service/Sleep.h"
#include "Service/BLE.h"
#include "HAL/HAL.h"

/*
核心 0: 系统 + 串口/Dispatcher
- Wi-Fi / BLE
- Serial console parser
- Dispatcher
- OTA (临时创建)

核心 1: 实时/计算密集任务
- WS2812 animation
- MPU6050 sampling
- Gesture recognition
- IR remote
*/

void setup()
{
  Serial.begin(115200);

  console_init();
  dispatcher_init();
  sleep_init();
  ble_init();

  HAL::ws2812_init();
  HAL::power_init();
}

void loop()
{
}
