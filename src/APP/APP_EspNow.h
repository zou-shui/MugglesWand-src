#pragma once

#include <Arduino.h>

// 初始化 ESP-NOW 发送模块（内部会启动心跳定时器或任务）
void APP_espnow_init();

// 发送简短信号量的接口
void APP_espnow_tx_signal();
