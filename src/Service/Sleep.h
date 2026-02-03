#ifndef POWER_SLEEP_H
#define POWER_SLEEP_H

#include <Arduino.h>
#include "esp_sleep.h"

/************ API ************/
void sleep_init();
void sleep_enter();

#endif
