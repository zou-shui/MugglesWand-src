/*
    MAX17048电池监测芯片功能实现
*/
#include "HAL.h"
#include <Arduino.h>
#include "config.h"

#include <SparkFun_MAX1704x_Fuel_Gauge_Arduino_Library.h>

SFE_MAX1704X lipo(MAX1704X_MAX17048); // Create a MAX17048

// 电量计会被 BLE 上报、Web 心跳、info 命令等多个任务读取。
// SparkFun 库的一次读取由多段 I2C 事务组成，需要任务级互斥避免交错。
static SemaphoreHandle_t gauge_mutex = nullptr;

struct GaugeLock
{
    GaugeLock()
    {
        if (gauge_mutex != nullptr)
            xSemaphoreTake(gauge_mutex, portMAX_DELAY);
    }
    ~GaugeLock()
    {
        if (gauge_mutex != nullptr)
            xSemaphoreGive(gauge_mutex);
    }
};

float HAL::MAX17048_getVoltage()
{
    GaugeLock lock;
    return lipo.getVoltage();
}

float HAL::MAX17048_getSOC()
{
    GaugeLock lock;
    return lipo.getSOC();
}

float HAL::MAX17048_getChangeRate()
{
    GaugeLock lock;
    return lipo.getChangeRate();
}

bool HAL::MAX17048_init()
{
    if (gauge_mutex == nullptr)
        gauge_mutex = xSemaphoreCreateMutex();

    Wire1.begin(PIN_BATT_GAUGE_SDA, PIN_BATT_GAUGE_SCL, 400000);

    if (lipo.begin(Wire1) == false)
    {
        DualSerial.println("[ERROR] MAX17048 not detected");
        return false;
    }

    return true;
}
