#include "BLE.h"
#include "Console.h"
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "HAL/HAL.h"

// BLE Nordic UART UUIDs
#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"           // UART 核心服务
#define RX_CHARACTERISTIC_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" // MCU接收 (主机写 W/WO_RESP)
#define TX_CHARACTERISTIC_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" // MCU发送 (主机通知 NOTIFY)

// 全局静态变量管理内部状态
static String _device_name = "MagicWand"; // 默认设备名称
static bool _is_ble_enabled = false;
static bool _is_connected = false;

// NimBLE 核心指针
static NimBLEServer *pServer = nullptr;
static NimBLEService *pService = nullptr;
static NimBLECharacteristic *pTxCharacteristic = nullptr;
static NimBLECharacteristic *pRxCharacteristic = nullptr;

// HID 核心指针与特征值
static NimBLEHIDDevice *pHID = nullptr;
static NimBLECharacteristic *pKeyboardInput = nullptr;

// 标准 104 键 HID 键盘描述符
static const uint8_t _hidReportDescriptor[] = {
    0x05, 0x01, // Usage Page (Generic Desktop)
    0x09, 0x06, // Usage (Keyboard)
    0xA1, 0x01, // Collection (Application)
    0x85, 0x01, //   Report ID (1)

    // 修饰键字节 (Ctrl, Shift, Alt, GUI)
    0x05, 0x07, //   Usage Page (Key Codes)
    0x19, 0xE0, //   Usage Minimum (Left Control)
    0x29, 0xE7, //   Usage Maximum (Right GUI)
    0x15, 0x00, //   Logical Minimum (0)
    0x25, 0x01, //   Logical Maximum (1)
    0x75, 0x01, //   Report Size (1 bit)
    0x95, 0x08, //   Report Count (8)
    0x81, 0x02, //   Input (Data, Variable, Absolute)

    // 保留字节值
    0x95, 0x01, //   Report Count (1)
    0x75, 0x08, //   Report Size (8 bits)
    0x81, 0x01, //   Input (Constant)

    // 6键无冲无优先级键码阵列 (6KRO)
    0x95, 0x06,       //   Report Count (6)
    0x75, 0x08,       //   Report Size (8 bits)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xE7, 0x00, //   Logical Maximum (231)
    0x05, 0x07,       //   Usage Page (Key Codes)
    0x19, 0x00,       //   Usage Minimum (0)
    0x2A, 0xE7, 0x00, //   Usage Maximum (231)
    0x81, 0x00,       //   Input (Data, Array, Absolute)

    0xC0 // End Collection
};

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

bool ble_update_battery(void)
{
    if (!_is_ble_enabled || !_is_connected || pHID == nullptr)
    {
        return false;
    }

    float level = HAL::MAX17048_getSOC();
    if (level > 100)
        level = 100; // 限制最大值为 100

    pHID->setBatteryLevel((uint8_t)level, 1);

    return true;
}

// 辅助函数：发送标准的8字节键盘HID报文
static void send_keyboard_report(uint8_t modifiers, uint8_t keycode)
{
    if (!_is_connected || pKeyboardInput == nullptr)
        return;

    uint8_t report[8] = {0};
    report[0] = modifiers; // 修饰键字节
    report[2] = keycode;   // 第一个键码槽

    pKeyboardInput->setValue(report, sizeof(report));
    pKeyboardInput->notify();
}

// 模拟一次完整的按键点击（按下 + 延迟 + 释放）
bool ble_keyboard_tap_key(uint8_t keycode)
{
    if (!_is_ble_enabled || !_is_connected || pKeyboardInput == nullptr)
    {
        return false;
    }

    // 1. 发送按键按下报文
    send_keyboard_report(0, keycode);
    delay(25); // 保持时间

    // 2. 发送全释放（空）报文
    send_keyboard_report(0, 0);
    delay(25); // 间隙时间

    ble_update_battery();
    return true;
}

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
        Serial.println("[BLE] Initializing BLE service with HID Keyboard...");

        // 1. 初始化 NimBLE 堆栈
        NimBLEDevice::init(_device_name.c_str());

        NimBLEDevice::setSecurityAuth(true, true, true);

        // 2. 创建 Server
        pServer = NimBLEDevice::createServer();
        pServer->setCallbacks(new MyServerCallbacks());

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

        // 6.加入 HID 键盘初始化逻辑 (参考 Hijel 实现)
        pHID = new NimBLEHIDDevice(pServer);
        pHID->setReportMap((uint8_t *)_hidReportDescriptor, sizeof(_hidReportDescriptor));
        pHID->setPnp(0x02, 0x05ac, 0x0255, 0x0110);       // 设置设备 PnP 信息 (可选，增强系统兼容性)
        pHID->setHidInfo(0x00, 0x01);                     // 0x01 代表键盘设备属性
        pHID->setBatteryLevel(HAL::MAX17048_getSOC(), 1); // 设置初始电量

        // 为电池电量特征值赋予加密读权限，防止某些系统报安全警告
        NimBLECharacteristic *pBatteryChar = pHID->getBatteryLevel();

        // 提取 Report ID 1 对应的键盘输入特征值指针
        pKeyboardInput = pHID->getInputReport(1);

        // 7. 开始广播
        NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
        pAdvertising->setName(_device_name.c_str());
        pAdvertising->addServiceUUID(SERVICE_UUID);
        pAdvertising->addServiceUUID(pHID->getHidService()->getUUID()); // 同时广播 HID 服务 UUID 确保设备类型识别
        pAdvertising->setAppearance(0x03C1);                            // 设置外观为标准键盘外设 (0x03C1 为键盘类)
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
        pHID = nullptr;
        pKeyboardInput = nullptr;

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