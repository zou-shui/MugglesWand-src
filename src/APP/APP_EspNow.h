#pragma once

#include <Arduino.h>
#include <stdbool.h>

enum MsgType
{
    MSG_HEARTBEAT = 0x01,
    MSG_SIGNAL_1 = 0x02,
    MSG_SIGNAL_2 = 0x03
};

// 初始化 ESP-NOW 发送模块（内部会启动心跳定时器或任务）
void APP_espnow_init();

// 发送简短信号量的接口
void APP_espnow_tx_signal(uint8_t signalType);

// 手势映射入口：parameter 为 0~255 的十进制或十六进制字符串
bool APP_espnow_validate(const char *parameter);
bool APP_espnow_execute(const char *parameter);
