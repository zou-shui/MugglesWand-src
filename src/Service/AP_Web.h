#pragma once

#include <Arduino.h>

// AP Web 服务控制接口
void ap_web_start(void);
void ap_web_stop(void);

// 向所有 WebSocket 客户端广播日志数据
void ap_web_print(const char *buffer, size_t length);
