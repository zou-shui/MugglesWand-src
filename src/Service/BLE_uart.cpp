#include "BLE_uart.h"
#include "Console.h"
#include <NimBLEDevice.h>

// BLE Nordic UART UUIDs
#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"           // UART 核心服务
#define RX_CHARACTERISTIC_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" // MCU接收 (主机写 W/WO_RESP)
#define TX_CHARACTERISTIC_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" // MCU发送 (主机通知 NOTIFY)

// 全局静态变量管理内部状态
static String _device_name = "ESP32_S3_BLE";
static bool _is_ble_enabled = false;
static bool _is_connected = false;

// NimBLE 核心指针
static NimBLEServer *pServer = nullptr;
static NimBLEService *pService = nullptr;
static NimBLECharacteristic *pTxCharacteristic = nullptr;
static NimBLECharacteristic *pRxCharacteristic = nullptr;

/**
 * @brief 服务器回调类，用于监听连接和断开事件
 */
class MyServerCallbacks : public NimBLEServerCallbacks
{
    void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override
    {
        _is_connected = true;
        // 允许连接后更新参数以优化功耗和速度（可选）
        pServer->updateConnParams(connInfo.getConnHandle(), 24, 40, 0, 200);
        Serial.printf("[BLE] Client connected\n");
    }

    void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) override
    {
        _is_connected = false;
        Serial.printf("[BLE] Client disconnected\n");
        // 注意：如果 BLE 被主动 toggle 关闭，不需要在这里重新开启广告
        if (_is_ble_enabled)
        {
            NimBLEDevice::startAdvertising();
        }
    }
};

/**
 * @brief 特征值回调类，用于接收来自主机的数据
 */
class MyCharacteristicCallbacks : public NimBLECharacteristicCallbacks
{
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override
    {
        std::string rxValue = pCharacteristic->getValue();
        if (rxValue.length() > 0)
        {
            console_parse((char *)rxValue.c_str()); // 将接收到的数据传递给串口命令解析器
        }
    }
};

void ble_init(const char *devicename)
{
    if (devicename != nullptr && strlen(devicename) > 0)
    {
        _device_name = String(devicename);
    }
    _is_ble_enabled = false;
    _is_connected = false;
}

bool ble_toggle(void)
{
    if (!_is_ble_enabled)
    {
        // --- 开启 BLE 逻辑 ---
        Serial.println("[BLE] Initializing BLE service...");

        // 1. 初始化 NimBLE 堆栈
        NimBLEDevice::init(_device_name.c_str());

        // 2. 创建或恢复 Server
        pServer = NimBLEDevice::createServer();
        pServer->setCallbacks(new MyServerCallbacks()); // 内部会自动管理内存或允许重复设置

        // 3. 创建服务
        pService = pServer->createService(SERVICE_UUID);

        // 4. 创建 TX 特征值 (Notify)
        pTxCharacteristic = pService->createCharacteristic(
            TX_CHARACTERISTIC_UUID,
            NIMBLE_PROPERTY::NOTIFY);

        // 5. 创建 RX 特征值 (Write / Write No Response)
        pRxCharacteristic = pService->createCharacteristic(
            RX_CHARACTERISTIC_UUID,
            NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
        pRxCharacteristic->setCallbacks(new MyCharacteristicCallbacks());

        // 6. 启动服务与广告
        // pService->start();

        // 开始广播
        NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
        pAdvertising->setName(_device_name.c_str());
        pAdvertising->addServiceUUID(SERVICE_UUID);
        pAdvertising->enableScanResponse(false);
        pAdvertising->start();

        _is_ble_enabled = true;
        Serial.println("[BLE] Service started, advertising as '" + _device_name + "'\n");
    }
    else
    {
        // --- 关闭 BLE 逻辑 ---
        _is_ble_enabled = false;
        _is_connected = false;

        // 1. 停止广播
        NimBLEDevice::getAdvertising()->stop();

        // 2. 断开所有已有连接
        if (pServer != nullptr)
        {
            // 获取当前所有客户端连接的句柄并强制断开
            std::vector<uint16_t> peerIDs = pServer->getPeerDevices();
            for (auto peerID : peerIDs)
            {
                pServer->disconnect(peerID);
            }
        }

        // 3. 彻底注销 NimBLE 驱动并释放相关射频(RF)与内存资源
        // 传入 true 会释放占用的内存，确保射频硬件关闭，达到真正零功耗（相对于BLE而言）
        NimBLEDevice::deinit(true);

        // 指针归零，防止野指针
        pServer = nullptr;
        pService = nullptr;
        pTxCharacteristic = nullptr;
        pRxCharacteristic = nullptr;

        Serial.println("[BLE] Service stopped");
    }

    return _is_ble_enabled;
}

bool ble_send(const char *buffer, size_t length)
{
    if (!_is_ble_enabled || !_is_connected || pTxCharacteristic == nullptr)
    {
        return false;
    }

    // NimBLE 会自动处理 MTU 大小的分包发送
    pTxCharacteristic->setValue((const uint8_t *)buffer, length);
    pTxCharacteristic->notify();

    return true;
}