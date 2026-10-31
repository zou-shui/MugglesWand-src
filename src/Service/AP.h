#pragma once

#include <Arduino.h>
#include <stdint.h>

// ==================== WiFi 网络管理接口 ====================
// 统一管理 AP / STA / ESP-NOW 的射频资源，以及 TCP/Web 服务。
// 开机时由 network_init() 根据 DeviceConfig 决定走 AP 还是 STA；
// Web/TCP 服务与 AP 射频解耦，两种模式下都可用。

// 启动网络：有 WiFi 配置则连接 STA，否则开 AP；同时启动 TCP/Web 服务
void network_init(void);

// 是否配置了 WiFi STA（SSID 非空）
bool network_is_sta_configured(void);

// WiFi STA 服务是否开启（运行时可切换）
bool network_is_sta_enabled(void);

// STA 是否已连接并拿到 IP
bool network_is_sta_connected(void);

// ==================== WiFi STA 控制接口 ====================
// 开/关 WiFi STA：关闭时断开连接并停止自动重连，AP 与 ESP-NOW 不受影响
void wifi_toggle(void);

// 当前射频信道是否允许 ESP-NOW（ESP-NOW 依赖信道 1）
bool network_espnow_allowed(void);

// 由 Service 任务处理 WiFi 事件
void network_handle_sta_got_ip(int32_t channel);
void network_handle_sta_disconnected(void);

// OTA 接管网络前调用：停止 Web/TCP/mDNS/AP/STA/ESP-NOW
void network_suspend(void);

// mDNS 主机名（STA 模式由 BLE 名称 + MAC 后缀生成；未启动时为空串）
const char *network_get_mdns_hostname(void);

// ==================== AP 控制接口 ====================
void ap_toggle(void);
bool ap_is_running(void);
void ap_print(const char *buffer, size_t length);

// ==================== ESP-NOW 控制接口 ====================
void espnow_toggle(void);
bool espnow_is_running(void);
bool espnow_send_data(const uint8_t *data, size_t len);
