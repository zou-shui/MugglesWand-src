#pragma once

#include <Arduino.h>

// ==================== 接口函数声明 ====================

// AP 控制接口
void ap_toggle(void);
bool ap_is_running(void);
void ap_print(const char *buffer, size_t length);

// ESP-NOW 控制接口
void espnow_toggle(void);
bool espnow_is_running(void);
bool espnow_send_data(const uint8_t *data, size_t len);
