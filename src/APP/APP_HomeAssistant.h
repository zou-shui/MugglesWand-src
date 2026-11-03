#pragma once

#include <Arduino.h>
#include <stdbool.h>

// ============================================================
// Home Assistant MQTT 应用
// ============================================================
// 通过 MQTT 把魔杖手势发布到 Home Assistant，并利用 HA 的 MQTT
// 自动发现创建 Event 实体与设备触发器。
//
// 配置（重启生效）：
//   config wifi <ssid>
//   config wifi-pass <password>
//   config mqtt <broker-host>
//   config mqtt-port <port>
//   config mqtt-user <user>
//   config mqtt-pass <password>
//
// 绑定（事件名必填，仅允许小写字母、数字、下划线，1~31 字符）：
//   map bind <gesture> ha <event_name>
// ============================================================

void APP_HomeAssistant_init(void);

bool APP_HomeAssistant_validate(const char *parameter);
bool APP_HomeAssistant_execute(const char *parameter);

// ha status：打印 WiFi/MQTT 连接状态、主题与 HA 绑定数量
void APP_HomeAssistant_console(const char *args);
