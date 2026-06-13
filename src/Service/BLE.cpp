#include "BLE.h"
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "HAL/HAL.h"
#include "Config.h"

// 全局静态变量管理内部状态
static String _device_name = BLE_DEVICE_NAME;
static bool _is_ble_enabled = false;
static bool _is_connected = false;

// FreeRTOS 任务句柄与退出标志
static TaskHandle_t _battery_task_handle = nullptr;
static volatile bool _battery_task_should_exit = false;

// NimBLE 核心指针
static NimBLEServer *pServer = nullptr;

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
        // 注意：如果 BLE 被主动 toggle 关闭，不需要在这里重新开启广播
        if (_is_ble_enabled)
        {
            NimBLEDevice::startAdvertising();
        }
    }
};

bool ble_update_battery(void)
{
    if (!_is_ble_enabled || !_is_connected || pHID == nullptr)
    {
        return false;
    }

    float level = (HAL::MAX17048_getSOC() + 0.5f); // 加0.5为了取整时四舍五入
    if (level > 100)
        level = 100; // 限制最大值为 100

    pHID->setBatteryLevel((uint8_t)level, 1);

    return true;
}

/**
 * @brief 电池电量定期上报的 FreeRTOS 任务函数
 * @details 通过 _battery_task_should_exit 标志位实现自退出，
 *          避免外部 vTaskDelete 在任务执行关键操作时强制终止造成状态损坏。
 */
static void ble_battery_task(void *pvParameters)
{
    while (!_battery_task_should_exit)
    {
        // 尝试更新电量，内部已包含状态校验
        ble_update_battery();

        // 延时 30 秒 (30000 毫秒)，期间分段检查退出标志以提升响应速度
        for (int i = 0; i < 30 && !_battery_task_should_exit; i++)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    _battery_task_handle = nullptr;
    vTaskDelete(nullptr); // 任务自退出
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

    return true;
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

        // 3. 加入 HID 键盘初始化逻辑
        pHID = new NimBLEHIDDevice(pServer);
        pHID->setReportMap((uint8_t *)_hidReportDescriptor, sizeof(_hidReportDescriptor));
        pHID->setPnp(0x02, 0x05ac, 0x0255, 0x0110);       // 设置设备 PnP 信息 (可选，增强系统兼容性)
        pHID->setHidInfo(0x00, 0x01);                     // 0x01 代表键盘设备属性
        pHID->setBatteryLevel(HAL::MAX17048_getSOC(), 1); // 设置初始电量

        // 提取 Report ID 1 对应的键盘输入特征值指针
        pKeyboardInput = pHID->getInputReport(1);

        // 4. 开始广播
        NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
        pAdvertising->setName(_device_name.c_str());
        pAdvertising->addServiceUUID(pHID->getHidService()->getUUID()); // 广播 HID 服务 UUID 确保设备类型识别
        pAdvertising->setAppearance(0x03C4);                            // 设置外观
        pAdvertising->enableScanResponse(true);                         // 开启 scan response，提升部分主机（如 Windows）的设备名识别兼容性
        pAdvertising->start();

        _is_ble_enabled = true;

        // 5. 创建电量定期上报任务
        if (_battery_task_handle == nullptr)
        {
            _battery_task_should_exit = false;
            xTaskCreatePinnedToCore(
                ble_battery_task,      // 任务函数
                "ble_bat_task",        // 任务名称
                4096,                  // 栈大小小于2472字节将会溢出
                nullptr,               // 传递给任务的参数
                1,                     // 任务优先级
                &_battery_task_handle, // 任务句柄
                0);                    // 固定在核心 0
        }

        Serial.println("[BLE] Service started, advertising as '" + _device_name + "'\n");
    }
    else
    {
        // --- 关闭 BLE 逻辑 ---

        // 1. 通知电量上报任务自行退出，避免外部强制 delete 造成状态损坏
        if (_battery_task_handle != nullptr)
        {
            _battery_task_should_exit = true;
            // 等待任务自退出（任务内部会将 _battery_task_handle 置空）
            while (_battery_task_handle != nullptr)
            {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }

        _is_ble_enabled = false;
        _is_connected = false;

        // 2. 停止广播
        NimBLEDevice::getAdvertising()->stop();

        // 3. 断开所有已有连接
        if (pServer != nullptr)
        {
            // 获取当前所有客户端连接的句柄并强制断开
            std::vector<uint16_t> peerIDs = pServer->getPeerDevices();
            for (auto peerID : peerIDs)
            {
                pServer->disconnect(peerID);
            }
        }

        // 4. 彻底注销 NimBLE 驱动并释放相关射频(RF)与内存资源
        NimBLEDevice::deinit(true);

        // 指针归零，防止野指针
        pServer = nullptr;
        pHID = nullptr;
        pKeyboardInput = nullptr;

        Serial.println("[BLE] Service stopped");
    }

    return _is_ble_enabled;
}