/*
    MAX17048电池监测芯片功能实现
*/
#include "HAL.h"
#include <Arduino.h>
#include "config.h"

#include <SparkFun_MAX1704x_Fuel_Gauge_Arduino_Library.h>

SFE_MAX1704X lipo(MAX1704X_MAX17048); // Create a MAX17048

float HAL::MAX17048_getVoltage()
{
    return lipo.getVoltage();
}

float HAL::MAX17048_getSOC()
{
    return lipo.getSOC();
}

float HAL::MAX17048_getChangeRate()
{
    return lipo.getChangeRate();
}

bool HAL::MAX17048_getChargeStatus()
{
    return lipo.getChangeRate() > 0 ? 1 : 0;
}

bool HAL::MAX17048_init()
{
    Wire1.begin(PIN_BATT_GAUGE_SDA, PIN_BATT_GAUGE_SCL, 400000);

    if (lipo.begin(Wire1) == false)
    {
        DualSerial.println("[ERROR] MAX17048 not detected");
        return false;
    }

    return true;
}
