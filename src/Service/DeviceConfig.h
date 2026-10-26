#pragma once

#include <Arduino.h>

// 设备身份配置：AP SSID / AP 密码 / BLE 名称
// 默认值定义在 Config.h，运行时通过 console 的 config 命令修改并保存到 NVS。
// 修改只写 NVS 和 RAM 缓存，不热更新正在运行的 AP/BLE；重启后自然生效。

// 启动时从 NVS 加载配置到 RAM 缓存（必须在 AP/BLE 初始化之前调用）
void device_config_init(void);

// 读取当前生效值，写入调用者提供的缓冲区
void device_config_get_ap(char *ssid, size_t ssid_size,
                          char *pass, size_t pass_size);
void device_config_get_ble_name(char *name, size_t size);

// 校验并保存到 NVS，同时更新 RAM 缓存
// 失败时函数内部已通过 DualSerial 打印原因
bool device_config_set_ap_ssid(const char *value);
bool device_config_set_ap_pass(const char *value);
bool device_config_set_ble_name(const char *value);
bool device_config_reset(void);

// config 命令入口（由 Console 调用）
void device_config_console(const char *args);
