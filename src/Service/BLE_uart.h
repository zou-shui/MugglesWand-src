#pragma once

#include <Arduino.h>

/**
 * @brief 初始化 BLE 串口设备名称
 * @param devicename 设备广播显示的名称
 */
void ble_init(const char *devicename);

/**
 * @brief 切换 BLE 的开启与关闭状态
 * @details 内部自动判断当前状态。关闭时会断开连接、停止广播并注销堆栈以最大化省电。
 * @return bool 返回切换后的 BLE 状态：true 表示已开启，false 表示已关闭
 */
bool ble_toggle(void);

/**
 * @brief 通过 BLE 发送数据给已连接的主机
 * @param buffer 数据缓冲区
 * @param length 数据长度
 * @return bool 发送成功返回 true，未连接或发送失败返回 false
 */
bool ble_send(const char *buffer, size_t length);
