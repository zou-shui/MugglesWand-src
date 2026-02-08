/*
    BLE 服务，用于通过蓝牙通信接收命令和发送电池信息
*/

#include "BLE.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include "Console.h"
#include "HAL/HAL.h"

// Nordic UART Service UUIDs
#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

TaskHandle_t ble_task_handle = NULL;

static NimBLEServer *pServer;

class ServerCallbacks : public NimBLEServerCallbacks
{
    void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override
    {
        Serial.printf("[BLE] Client connected\n");
    }

    void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) override
    {
        Serial.printf("[BLE] Client disconnected - start advertising\n");
        NimBLEDevice::startAdvertising();
    }

} serverCallbacks;

class CharacteristicCallbacks : public NimBLECharacteristicCallbacks
{
    void onRead(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override
    {
    }

    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override
    {
        std::string value = pCharacteristic->getValue();
        if (value.length() == 0)
            return;
        char cmd[value.length() + 1];
        memcpy(cmd, value.c_str(), value.length());
        cmd[value.length()] = '\0'; // 确保以 '\0' 结尾

        console_parse(cmd); // 将命令传递给控制台解析函数
    }

    void onSubscribe(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo, uint16_t subValue) override
    {
    }
} chrCallbacks;

void ble_delete()
{
    if (ble_task_handle)
    {
        vTaskDelete(ble_task_handle);
        ble_task_handle = NULL;
    }
}

static void ble_task(void *param)
{
    while (1)
    {
        if (pServer->getConnectedCount())
        {
            NimBLEService *pSvc = pServer->getServiceByUUID(SERVICE_UUID);
            if (pSvc)
            {
                NimBLECharacteristic *pChr = pSvc->getCharacteristic(CHARACTERISTIC_UUID_TX);
                if (pChr)
                {
                    // 获取电池状态并发送通知
                    char data[64];
                    snprintf(data, sizeof(data), "%d%%,%.2fV,%s\n",
                             HAL::power_get_battery_percent(),
                             HAL::power_get_battery_voltage(),
                             HAL::power_is_charging() ? "Charging" : "Discharging");

                    // 设置特征值并通知客户端
                    pChr->setValue(data);
                    pChr->notify();
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void ble_init(void)
{
    NimBLEDevice::init("MagicWand");

    // 创建服务器
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(&serverCallbacks);

    // 创建服务
    NimBLEService *pService = pServer->createService(SERVICE_UUID);

    // 创建TX特征（Notify方式发送数据）
    NimBLECharacteristic *pTxCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID_TX,
        NIMBLE_PROPERTY::NOTIFY);
    pTxCharacteristic->setValue("Hello World");
    pTxCharacteristic->setCallbacks(&chrCallbacks);

    // 创建RX特征（接收数据）
    NimBLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID_RX,
        NIMBLE_PROPERTY::WRITE);
    pRxCharacteristic->setCallbacks(&chrCallbacks);

    // 启动服务
    pService->start();

    // 开始广播
    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->setName("MagicWand");
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->enableScanResponse(false);
    pAdvertising->start();

    xTaskCreatePinnedToCore(
        ble_task,
        "ble_task",
        4096,
        NULL,
        1,
        &ble_task_handle,
        0);
}
