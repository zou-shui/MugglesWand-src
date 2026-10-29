#pragma once

#include <Arduino.h>
#include <stdint.h>

// 设备配置：BLE 名称 / AP 身份 / WiFi STA / MQTT
// BLE 与 AP 默认值定义在 Config.h；WiFi 与 MQTT 默认空。
// 运行时通过 console 的 config 命令修改并保存到 NVS；修改只写 NVS 和
// RAM 缓存，不热更新正在运行的服务，重启后生效。

struct DeviceMqttConfig
{
    char host[64];
    uint16_t port;
    char user[32];
    char pass[64];
};

// 启动时从 NVS 加载配置到 RAM 缓存（必须在网络/MQTT 初始化之前调用）
void device_config_init(void);

// 读取当前生效值，写入调用者提供的缓冲区
void device_config_get_ble_name(char *name, size_t size);
void device_config_get_ap(char *ssid, size_t ssid_size,
                          char *pass, size_t pass_size);
void device_config_get_wifi(char *ssid, size_t ssid_size,
                            char *pass, size_t pass_size);
void device_config_get_mqtt(DeviceMqttConfig *config);

// WiFi SSID 非空表示开机进入 STA 模式；MQTT host 非空表示启用 HA
bool device_config_has_wifi(void);
bool device_config_has_mqtt(void);

// 校验并保存到 NVS，同时更新 RAM 缓存
// 失败时函数内部已通过 DualSerial 打印原因
bool device_config_set_ble_name(const char *value);
bool device_config_set_ap_ssid(const char *value);
bool device_config_set_ap_pass(const char *value);
bool device_config_set_wifi_ssid(const char *value);
bool device_config_set_wifi_pass(const char *value);
bool device_config_set_mqtt_host(const char *value);
bool device_config_set_mqtt_port(const char *value);
bool device_config_set_mqtt_user(const char *value);
bool device_config_set_mqtt_pass(const char *value);

// 清除单个分组，保留其它配置
bool device_config_clear_wifi(void);
bool device_config_clear_mqtt(void);

// 恢复所有配置为默认值
bool device_config_reset(void);

// config 命令入口（由 Console 调用）
void device_config_console(const char *args);
