#include <Arduino.h>
#include "Service/Console.h"
#include "Service/Dispatcher.h"
#include "Service/OTA.h"
#include "Service/Sleep.h"

void setup()
{
  Serial.begin(115200);

  console_init();
  dispatcher_init();
  sleep_init();
}

void loop()
{
}
